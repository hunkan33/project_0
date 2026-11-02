/*
 * fd_probe —— 探测 Cedar 输出的 buffer 能不能拿到 dma_buf fd
 * =============================================================================
 *  背景：T113 的 DE 启用 IOMMU，物理地址直给会黑屏。
 *        如果能把 Cedar 的 buffer 转成 dma_buf fd，就能用
 *        DISP_LAYER_SET_CONFIG2 的 fb.fd 字段，由内核自动建映射 → 零拷贝。
 *
 *  探测方法：
 *    MemAdapter 的 ScMemOpsS 函数表里有 get_fd_by_vir(虚拟地址 → fd)，
 *    用导出的 MemAdapterGetOpsS() 拿到函数表，对解码输出的 Y 平面地址试一次。
 *
 *  三种结果：
 *    fd > 0   → 可行，继续做 DE 零拷贝
 *    fd = -1  → 函数在但没找到这块 buffer，换 pAddrPhy0 再试
 *    函数指针 NULL → 版本不支持，放弃零拷贝
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "capture.h"
#include "AWVideoDecoder.h"
#include "sc_interface.h"

using namespace awvideodecoder;

/* MemAdapter 导出的函数表获取入口 */
extern "C" void *MemAdapterGetOpsS(void);

/* ---- 回调：存解码结果 ---- */
static volatile int   g_got = 0;
static unsigned char *g_yVir = nullptr;
static unsigned char *g_uvVir = nullptr;
static unsigned char *g_yPhy = nullptr;
static unsigned char *g_uvPhy = nullptr;

class ProbeCb : public AWVideoDecoderDataCallback
{
public:
    virtual int decoderDataReady(AVPacket *pkt)
    {
        g_yVir  = pkt->pAddrVir0;
        g_uvVir = pkt->pAddrVir1;
        g_yPhy  = pkt->pAddrPhy0;
        g_uvPhy = pkt->pAddrPhy1;
        g_got   = 1;
        return 0;
    }
};
static ProbeCb g_cb;

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---- 1. 采集 ---- */
    int cfd = capture_init();
    if (cfd < 0) { printf("[probe] 找不到摄像头\n"); return 1; }
    if (capture_start(cfd) < 0) { printf("[probe] 采集启动失败\n"); capture_stop(cfd); close(cfd); return 1; }

    /* ---- 2. 解码器 ---- */
    DecodeParam param;
    memset(&param, 0, sizeof(param));
    param.codecType   = CODEC_MJPEG;
    param.pixelFormat = PIXEL_NV12;
    param.srcW        = 1280;
    param.srcH        = 720;
    param.rotation    = Angle_0;

    AWVideoDecoder *dec = AWVideoDecoder::create();
    if (!dec) { printf("[probe] 解码器创建失败\n"); return 1; }
    if (dec->init(&param, &g_cb) < 0) { printf("[probe] 解码器初始化失败\n"); return 1; }

    /* ---- 3. 拿 MemAdapter 的函数表 ---- */
    ScMemOpsS *ops = (ScMemOpsS *)MemAdapterGetOpsS();
    printf("\n========== 函数表探测 ==========\n");
    printf("[probe] ScMemOpsS*        = %p\n", (void *)ops);
    if (!ops) {
        printf("[probe] ❌ 拿不到函数表，MemAdapterGetOpsS 返回空\n");
        return 1;
    }
    printf("[probe] get_fd_by_vir     = %p\n", (void *)ops->get_fd_by_vir);
    printf("[probe] get_vir_by_fd     = %p\n", (void *)ops->get_vir_by_fd);
    printf("[probe] get_phy_by_fd     = %p\n", (void *)ops->get_phy_by_fd);
    printf("[probe] ve_get_phyaddr    = %p\n", (void *)ops->ve_get_phyaddr);
    printf("================================\n\n");

    if (!ops->get_fd_by_vir) {
        printf("[probe] ❌ get_fd_by_vir 为空（版本不支持）\n");
        return 1;
    }

    /* ---- 4. 解 5 帧，每帧试一次 get_fd_by_vir ---- */
    int ok = 0;
    for (int i = 0; i < 5; ++i) {
        /* 取帧 */
        int waited = 0;
        while (capture_grab(cfd) != 0 && waited++ < 2000) usleep(1000);
        if (capture_frame_size() < 1000) { capture_release(cfd); continue; }

        AVPacket in;
        memset(&in, 0, sizeof(in));
        in.pAddrVir0 = (unsigned char *)capture_frame_data();
        in.dataLen0  = (unsigned int)capture_frame_size();
        int ret = dec->decode(&in);
        capture_release(cfd);

        /* 等回调 */
        waited = 0;
        while (!g_got && waited++ < 400) usleep(500);
        g_got = 0;
        if (!g_yVir) { printf("[probe] 帧%d 解码没出结果 (decode=%d)\n", i, ret); continue; }

        /* ★ 探测 fd */
        int fd_y  = ops->get_fd_by_vir(g_yVir);
        int fd_uv = ops->get_fd_by_vir(g_uvVir);

        printf("[probe] 帧%d  Yvir=%p  UVvir=%p\n", i, (void *)g_yVir, (void *)g_uvVir);
        printf("          Yphy=%p  UVphy=%p\n", (void *)g_yPhy, (void *)g_uvPhy);
        printf("          → Y 的 fd=%d   UV 的 fd=%d\n", fd_y, fd_uv);

        if (fd_y > 0) ++ok;
    }

    printf("\n========== 结论 ==========\n");
    if (ok > 0)
        printf("[probe] ✅ %d/5 帧拿到 fd → Cedar buffer 可以转 dma_buf，零拷贝可行\n", ok);
    else
        printf("[probe] ❌ 全部拿不到 fd → 零拷贝这条路走不通，继续用 CPU 转 ARGB\n");

    AWVideoDecoder::destroy(dec);
    capture_stop(cfd);
    close(cfd);
    return 0;
}
