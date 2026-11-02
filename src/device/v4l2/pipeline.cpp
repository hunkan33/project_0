// SPDX-License-Identifier: MIT
/** @file pipeline.cpp
 *  @brief pipeline.h 的实现
 */
#include "pipeline.h"

#include "decoder.h"
#include "display.h"
#include "log.h"

#include <unistd.h>

/* ---- 每 1000 帧打一行，确认线程还活着、没卡住（原来在 capture_thread.cpp 里） ---- */
static const int kLogEveryFrames = 1000;

/* ---- 等解码回调的上限：500us × 400 = 200ms ----
 * 正常一帧 MJPEG 解码 5~15ms，200ms 还没出来说明 VE 卡住了。 */
static const int kWaitLoops = 400;
static const int kWaitUsec  = 500;

static int g_open   = 0;
static int g_frames = 0;

int pipeline_open(int srcW, int srcH, int cropY, int cropH,
                  int winX, int winY, int winW, int winH)
{
    if (g_open)
        pipeline_close();

    if (decoder_init(srcW, srcH) < 0) {
        LOGE("pipe", "解码器初始化失败 %dx%d", srcW, srcH);
        return -1;
    }

    if (display_init(srcW, srcH, cropY, cropH, winX, winY, winW, winH) < 0) {
        LOGE("pipe", "显示层初始化失败（/dev/disp？）");
        decoder_destroy();              /* 显示失败要回滚解码器，不留半开状态 */
        return -1;
    }

    g_open   = 1;
    g_frames = 0;
    LOGI("pipe", "出图管线就绪 %dx%d", srcW, srcH);
    return 0;
}

int pipeline_feed(const void *jpg, size_t len)
{
    if (!g_open || !jpg || len == 0)
        return -1;

    decoder_decode(jpg, len);

    int waited = 0;
    while (!decoder_take_new() && waited++ < kWaitLoops)
        usleep(kWaitUsec);

    if (waited >= kWaitLoops) {
        /* ★ 注意：这里只报错、不重试、不销毁解码器。
         *   原因：销毁重建要停流，代价远大于丢一帧；连续超时由上层（采集线程）
         *   的重连策略处理。 */
        LOGE("pipe", "解码超时：%dms 内回调没触发", (kWaitLoops * kWaitUsec) / 1000);
        return -1;
    }

    display_update_fd(decoder_y_fd());

    if (++g_frames % kLogEveryFrames == 0)
        LOGI("dec", "第 %d 帧 Yvir=%p", g_frames, g_yVirt);

    return 0;
}

int pipeline_reconfigure(int srcW, int srcH)
{
    if (!g_open) {
        LOGE("pipe", "管线没开，无法重配到 %dx%d", srcW, srcH);
        return -1;
    }

    /* 只重建解码器：显示层保持不动（它的 crop/win 由 AppController 之后重算） */
    decoder_destroy();
    if (decoder_init(srcW, srcH) < 0) {
        LOGE("pipe", "重配时解码器初始化失败 %dx%d", srcW, srcH);
        g_open = 0;                    /* 标记成"没开"，让上层别继续 feed */
        return -1;
    }

    display_set_src_size(srcW, srcH);  /* 只改尺寸，不重建、不清 fb0、不动 g_visible */

    g_frames = 0;
    LOGI("pipe", "管线重配 %dx%d（显示层不重建）", srcW, srcH);
    return 0;
}

void pipeline_close(void)
{
    if (!g_open)
        return;

    /* ★★ 顺序不能动，这就是 decoder.h 那条契约的唯一兑现处：
     *    ① 先关视频层：DE 还引用着即将被 free 的 dma_buf fd
     *    ② 再销毁解码器
     *    ③ 最后关显示（close /dev/disp） */
    display_set_visible(0);
    decoder_destroy();
    display_destroy();

    g_open   = 0;
    g_frames = 0;
    LOGI("pipe", "出图管线已关闭");
}

int pipeline_src_w(void) { return g_open ? display_src_w() : 0; }
int pipeline_src_h(void) { return g_open ? display_src_h() : 0; }
