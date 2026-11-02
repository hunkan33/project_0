// SPDX-License-Identifier: MIT
/** @file decoder.cpp
 *  @brief 解码器封装实现
 */
#include "decoder.h"

#include "log.h"      /* 统一日志；★ 不要换成 sdklog.h，见 decoder.h 顶部说明 */

#include <unistd.h>   /* close */

extern "C" void *MemAdapterGetOpsS(void);//获取内存操作函数指针
using namespace awvideodecoder;

/* ---- 这些变量的唯一定义处（decoder.h 里是 extern 声明）----
 * 之前它们被 static 定义在头文件里，导致每个包含 decoder.h 的 .cpp
 * 都拿到一份独立副本：回调在 decoder.cpp 里改的是 A 份，
 * capture_thread.cpp 打印的是 B 份（永远是 NULL）。
 * extern + 单一定义就解决了。 */
AWVideoDecoder *g_dec    = nullptr;
unsigned char  *g_yVirt  = nullptr;
volatile int    g_newFrame = 0;
AEdecoderCb     g_cb;

/* ---- 块 A：fd 缓存,每调一次 get_fd_by_vir 都会新建一个 dma_buf fd,所以要限制---- */
#define FD_CACHE_MAX 8
static ScMemOpsS *g_ops = NULL;

struct fd_cache_t {
    unsigned char *vir;   //虚拟地址
    int            fd;    //fd
};

static fd_cache_t g_fd_cache[FD_CACHE_MAX];
static int g_fd_cache_cnt = 0;
static pthread_mutex_t g_fdMutex = PTHREAD_MUTEX_INITIALIZER;

//继承来注册回调函数e
int AEdecoderCb::decoderDataReady(AVPacket *pkt)
{
    if (pkt == nullptr) {
        LOGD("dec", "回调收到空包");
        return -1;
    }

    g_yVirt  = pkt->pAddrVir0;
    g_newFrame = 1;

    return 0;
}

//初始化解码器
int decoder_init(int srcW, int srcH)
{
    DecodeParam param;          //初始化结构体
    memset(&param, 0, sizeof(param));
    param.codecType   = CODEC_MJPEG;    // 我输入的是 MJPEG
    param.pixelFormat = PIXEL_NV12;     // 我要 NV12 输出
    param.srcW        = srcW;
    param.srcH        = srcH;
    param.rotation    = Angle_0;

    g_dec = AWVideoDecoder::create();
    if (!g_dec) return -1;
    g_ops = (ScMemOpsS *)MemAdapterGetOpsS();//获取内存操作函数指针
    if (!g_ops) {
        LOGE("dec", "MemAdapterGetOpsS 失败，内存操作接口拿不到");
    }

    /* ★ 清掉上一个解码器实例的残留状态。
     * 不清的话：切分辨率后 g_yVirt 还指向上一个实例（已被 free）的 NV12 buffer，
     * g_newFrame 可能还残留 1 —— 主循环第一次 decoder_take_new() 就会立刻返回 1，
     * 然后 decoder_y_fd() 拿到的是已释放内存的 fd → DE 调 dma_buf_get 失败。 */
    g_yVirt    = nullptr;
    g_newFrame = 0;

    return g_dec->init(&param, &g_cb);  // <0 失败
}

//解码
int decoder_decode(const void *jpg, size_t len)
{
    if (!g_dec) return -1;

    AVPacket in;
    memset(&in, 0, sizeof(in));
    in.pAddrVir0 = (unsigned char *)jpg;   // 压缩数据放这里
    in.dataLen0  = (unsigned int)len;

    return g_dec->decode(&in);   // ★ >0 成功，<0 失败（不是 ==0 成功！）
}

/* ---- 3. 取结果 ---- */
int decoder_take_new(void) {          // 有新帧返回 1，并清零
    if (g_newFrame) { g_newFrame = 0; return 1; }
    return 0;
}

/* ---- 4. 销毁（程序退出时调一次）---- */
void decoder_destroy(void)
{
        if (g_dec) {
        AWVideoDecoder::destroy(g_dec);
        g_dec = nullptr;
    }

    /* ★ 解码器没了，它输出的 NV12 buffer 已被 free（ION 内存释放），
     *   对应的 dma_buf fd 也随之失效。必须：
     *   ① close 掉我们通过 get_fd_by_vir 创建的 fd（否则泄漏）
     *   ② 清空 g_yVirt / g_newFrame（否则上层可能拿旧 fd 去喂 DE）*/
    pthread_mutex_lock(&g_fdMutex);
    for (int i = 0; i < g_fd_cache_cnt; i++) {
        if (g_fd_cache[i].fd >= 0)
            close(g_fd_cache[i].fd);
        g_fd_cache[i].vir = nullptr;
        g_fd_cache[i].fd  = -1;
    }
    g_fd_cache_cnt = 0;
    pthread_mutex_unlock(&g_fdMutex);

    g_yVirt    = nullptr;
    g_newFrame = 0;
}
static int vir_to_fd(unsigned char *vir)
{
    if (!vir || !g_ops || !g_ops->get_fd_by_vir)
        return -1;

    for (int i = 0; i < g_fd_cache_cnt; i++) {
        if (g_fd_cache[i].vir == vir) {
            return g_fd_cache[i].fd;
        }
    }
        /* ② 表里没有 → 才真正转一次 */
    /* 一次转换会新建一个 dma_buf fd，所以必须缓存复用，
     * 否则每秒 30 帧就是每秒 30 个 fd（试过，fd 表很快就涨起来）。 */
    int fd = g_ops->get_fd_by_vir(vir);

    /* ③ 记到表的下一格，下次就命中 */
    if (fd >= 0 && g_fd_cache_cnt < FD_CACHE_MAX) {
        g_fd_cache[g_fd_cache_cnt].vir = vir;
        g_fd_cache[g_fd_cache_cnt].fd  = fd;
        ++g_fd_cache_cnt;
    }
    return fd;

}

int decoder_y_fd(void)

{

    return vir_to_fd(g_yVirt);
}
