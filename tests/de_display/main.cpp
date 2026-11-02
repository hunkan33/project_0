/*
 * de_display —— 采集 + Cedar 硬解 + DE 图层（端到端验证）
 * =============================================================================
 *  链路：
 *      采集线程   grab(MJPEG) → decoder_decode() → release
 *      主线程     decoder_take_new() → display_update(物理地址)
 *
 *  DE 零拷贝：CPU 只在采集/解码里搬数据，显示这环只写一个地址寄存器。
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <pthread.h>

#include "capture.h"
#include "decoder.h"
#include "display.h"

static volatile int g_running = 1;

static void *capture_thread(void *)
{
    int fd = capture_init();
    if (fd < 0) {
        printf("[cap] 找不到摄像头\n");
        g_running = 0;
        return nullptr;
    }
    if (capture_start(fd) < 0) {
        printf("[cap] 启动采集失败\n");
        g_running = 0;
        capture_stop(fd);
        close(fd);
        return nullptr;
    }

    while (g_running) {
        if (capture_grab(fd) != 0) {
            /* 掉线：重连 */
            fprintf(stderr, "[cap] 采集中断，2 秒后重连\n");
            capture_stop(fd);
            close(fd);
            sleep(2);
            fd = capture_init();
            if (fd < 0) { sleep(2); continue; }
            if (capture_start(fd) < 0) { close(fd); fd = -1; sleep(2); continue; }
            fprintf(stderr, "[cap] 重连成功\n");
            continue;
        }

        decoder_decode(capture_frame_data(), capture_frame_size());
        capture_release(fd);
    }

    capture_stop(fd);
    close(fd);
    printf("[cap] 采集线程退出\n");
    return nullptr;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---- 1. 解码器 ---- */
    if (decoder_init(1280, 720) != 0) {
        printf("[main] 解码器初始化失败\n");
        return 1;
    }

    /* ---- 2. DE 图层：720p 裁上下 60 行 → 1024x480 @ (0,48) ---- */
    if (display_init(1280, 720, 60, 600, 0, 48, 1024, 480) != 0) {
        printf("[main] DE 图层初始化失败\n");
        decoder_destroy();
        return 1;
    }

    /* ---- 3. 采集线程 ---- */
    pthread_t th;
    pthread_create(&th, nullptr, capture_thread, nullptr);

    /* ---- 4. 主循环：有新帧就更新 DE 地址（零拷贝）---- */
    int      frames = 0;
    int      lastSec = 0;
    int      fpsCnt = 0;
    time_t   t0 = time(nullptr);

    while (g_running) {
        if (decoder_take_new()) {
            if (display_update(decoder_y_virt(), decoder_uv_virt()) == 0) {
                ++frames;
                ++fpsCnt;
            }

            if (frames <= 3 || frames % 60 == 0)
                printf("[show] 第 %d 帧  Yphy=%p  UVphy=%p  %dx%d\n",
                       frames, (void *)decoder_y_phys(), (void *)decoder_uv_phys(),
                       decoder_w(), decoder_h());
        }

        /* 每秒报一次 fps */
        time_t now = time(nullptr);
        if (now != t0) {
            printf("[fps] %d\n", fpsCnt);
            fpsCnt = 0;
            t0 = now;
        }

        usleep(1000);    /* 1ms 轮询 */
    }

    g_running = 0;
    pthread_join(th, nullptr);

    display_destroy();
    decoder_destroy();

    printf("[main] 结束，共显示 %d 帧\n", frames);
    return 0;
}
