/*
 * de_fd —— DE 图层 + dma_buf fd 零拷贝显示验证
 * =============================================================================
 *  链路：采集 → Cedar 硬解 → NV12（fd）→ DE ch[0] 直接显示
 *
 *  关键点：
 *    ① Cedar 输出用 MemAdapter 的 get_fd_by_vir 转 dma_buf fd
 *    ② DE 用 disp_layer_config2.info.fb.fd（内核自动建 IOMMU 映射）
 *    ③ ioctl 必须是 DISP_LAYER_SET_CONFIG2(0x49)，不是 SET_INFO(0x42)
 *    ④ ch[1]（fb0）绝不能 disable —— 那是整个屏幕的显示通道
 *       但 ch[1] 在 ch[0] 上面，所以要把 fb0 清 0（alpha=0）让视频透出来
 *    ⑤ fd 按虚拟地址缓存（每帧调 get_fd_by_vir 会产生新 fd，泄漏会爆 fd 表）
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "capture.h"
#include "AWVideoDecoder.h"
#include "sc_interface.h"

#include "typedef.h"
#include "sunxi_display2_k54.h"

using namespace awvideodecoder;

extern "C" void *MemAdapterGetOpsS(void);

#define SRC_W     1280
#define SRC_H     720
#define CROP_Y    60
#define CROP_H    600
#define WIN_X     0
#define WIN_Y     48
#define WIN_W     1024
#define WIN_H     480
#define FB_STRIDE (1024 * 4)

/* ---- DE 状态 ---- */
static int                  g_disp = -1;
static struct disp_layer_config2 g_cfg;
static ScMemOpsS           *g_ops  = nullptr;

/* ---- fd 缓存：解码器只有 4~5 个 buffer 轮换 ---- */
#define FD_CACHE_MAX 8
struct FdEntry { unsigned char *vir; int fd; };
static FdEntry g_cache[FD_CACHE_MAX];
static int     g_cacheCnt = 0;

static int vir_to_fd(unsigned char *vir)
{
    if (!vir || !g_ops || !g_ops->get_fd_by_vir)
        return -1;

    /* 先查缓存 */
    for (int i = 0; i < g_cacheCnt; ++i)
        if (g_cache[i].vir == vir)
            return g_cache[i].fd;

    /* 未命中才转 */
    int fd = g_ops->get_fd_by_vir(vir);
    if (fd > 0 && g_cacheCnt < FD_CACHE_MAX) {
        g_cache[g_cacheCnt].vir = vir;
        g_cache[g_cacheCnt].fd  = fd;
        ++g_cacheCnt;
        printf("[de] 缓存 fd=%d (vir=%p)  共 %d 个\n", fd, (void *)vir, g_cacheCnt);
    }
    return fd;
}

/* ---- DE 初始化 ---- */
static int de_init(void)
{
    g_disp = open("/dev/disp", O_RDWR);
    if (g_disp < 0) { perror("open /dev/disp"); return -1; }

    g_ops = (ScMemOpsS *)MemAdapterGetOpsS();
    if (!g_ops || !g_ops->get_fd_by_vir) {
        printf("[de] MemAdapter 函数表不可用\n");
        return -1;
    }

    /* fb0 清 0：让 ch[1]（UI 层）在视频区变透明，ch[0] 的视频才能透出来 */
    int fb = open("/dev/fb0", O_WRONLY);
    if (fb >= 0) {
        static unsigned char zero[FB_STRIDE * 600];
        memset(zero, 0, sizeof(zero));
        pwrite(fb, zero, sizeof(zero), 0);
        close(fb);
        printf("[de] fb0 已清 0（ch[1] 透明）\n");
    }

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.channel  = 0;                 /* ch0 = 视频层 */
    g_cfg.layer_id = 0;
    g_cfg.enable   = 0;                 /* 第一帧来了再 enable */

    g_cfg.info.mode        = LAYER_MODE_BUFFER;
    g_cfg.info.zorder      = 1;
    g_cfg.info.alpha_mode  = 1;
    g_cfg.info.alpha_value = 0xff;

    /* NV12 */
    g_cfg.info.fb.format          = DISP_FORMAT_YUV420_SP_UVUV;
    g_cfg.info.fb.size[0].width   = SRC_W;
    g_cfg.info.fb.size[0].height  = SRC_H;
    g_cfg.info.fb.size[1].width   = SRC_W / 2;
    g_cfg.info.fb.size[1].height  = SRC_H / 2;
    g_cfg.info.fb.size[2].width   = SRC_W / 2;
    g_cfg.info.fb.size[2].height  = SRC_H / 2;

    /* crop 32.32 定点 */
    g_cfg.info.fb.crop.x      = 0;
    g_cfg.info.fb.crop.y      = (long long)CROP_Y << 32;
    g_cfg.info.fb.crop.width  = (long long)SRC_W << 32;
    g_cfg.info.fb.crop.height = (long long)CROP_H << 32;

    /* 目标窗口 */
    g_cfg.info.screen_win.x      = WIN_X;
    g_cfg.info.screen_win.y      = WIN_Y;
    g_cfg.info.screen_win.width  = WIN_W;
    g_cfg.info.screen_win.height = WIN_H;

    g_cfg.info.fb.fd = -1;

    printf("[de] 初始化完成  src=%dx%d crop(y=%d,h=%d) → win(%d,%d,%d,%d)\n",
           SRC_W, SRC_H, CROP_Y, CROP_H, WIN_X, WIN_Y, WIN_W, WIN_H);
    return 0;
}

static int de_update(unsigned char *yVir)
{
    if (g_disp < 0) return -1;

    int fd = vir_to_fd(yVir);
    if (fd < 0) { fprintf(stderr, "[de] 拿不到 fd\n"); return -1; }

    g_cfg.info.fb.fd = fd;
    g_cfg.enable     = 1;

    unsigned long arg[4] = { 0, (unsigned long)&g_cfg, 1, 0 };
    int ret = ioctl(g_disp, DISP_LAYER_SET_CONFIG2, arg);
    if (ret != 0) { fprintf(stderr, "[de] SET_CONFIG2 失败 ret=%d\n", ret); return -1; }
    return 0;
}

/* ---- 解码回调 ---- */
static volatile int   g_newFrame = 0;
static unsigned char *g_yVir = nullptr;

class Cb : public AWVideoDecoderDataCallback
{
public:
    virtual int decoderDataReady(AVPacket *pkt)
    {
        g_yVir = pkt->pAddrVir0;
        g_newFrame = 1;
        return 0;
    }
};
static Cb g_cb;

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 采集 */
    int cfd = capture_init();
    if (cfd < 0) { printf("[main] 没找到摄像头\n"); return 1; }
    if (capture_start(cfd) < 0) { printf("[main] 采集启动失败\n"); return 1; }

    /* 解码 */
    DecodeParam param;
    memset(&param, 0, sizeof(param));
    param.codecType   = CODEC_MJPEG;
    param.pixelFormat = PIXEL_NV12;
    param.srcW = SRC_W;
    param.srcH = SRC_H;
    param.rotation = Angle_0;

    AWVideoDecoder *dec = AWVideoDecoder::create();
    if (!dec || dec->init(&param, &g_cb) < 0) { printf("[main] 解码器失败\n"); return 1; }

    /* DE */
    if (de_init() != 0) { printf("[main] DE 初始化失败\n"); return 1; }

    /* 主循环 */
    int cnt = 0;
    time_t t0 = time(nullptr);
    int fpsCnt = 0;
    time_t now = t0;
    unsigned long start = (unsigned long)time(nullptr);

    while (1) {
        int waited = 0;
        while (capture_grab(cfd) != 0 && waited++ < 2000) usleep(1000);

        size_t len = capture_frame_size();
        if (len < 1000) { capture_release(cfd); continue; }

        AVPacket in;
        memset(&in, 0, sizeof(in));
        in.pAddrVir0 = (unsigned char *)capture_frame_data();
        in.dataLen0  = (unsigned int)len;
        dec->decode(&in);
        capture_release(cfd);

        waited = 0;
        while (!g_newFrame && waited++ < 400) usleep(500);
        g_newFrame = 0;
        if (!g_yVir) continue;

        if (de_update(g_yVir) == 0) {
            ++cnt; ++fpsCnt;
            if (cnt <= 3 || cnt % 120 == 0)
                printf("[de] 第 %d 帧  已显示  fd 缓存 %d 个\n", cnt, g_cacheCnt);
        }

        now = time(nullptr);
        if (now != t0) {
            printf("[fps] %d\n", fpsCnt);
            fpsCnt = 0;
            t0 = now;
        }
        if ((unsigned long)now - start > 20) break;   /* 跑 20 秒自动退出 */
    }

    printf("[main] 结束，共显示 %d 帧，fd 缓存 %d 个\n", cnt, g_cacheCnt);
    AWVideoDecoder::destroy(dec);
    capture_stop(cfd);
    close(cfd);
    return 0;
}
