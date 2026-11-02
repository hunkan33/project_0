/*
 * decoder_test —— 单独验证 Cedar 硬解（不涉及显示）
 * =============================================================================
 *  流程：采集一帧 MJPEG → 喂给解码器 → 等回调 → 打印耗时和物理地址
 *  验收：平均 13~14ms/帧，Yphys 非 NULL，尺寸 1280x720
 * =============================================================================
 */

#include <QElapsedTimer>
#include <cstdio>
#include <unistd.h>

#include "capture.h"
#include "decoder.h"

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---- 采集 ---- */
    int fd = capture_init();
    if (fd < 0) {
        printf("[test] 找不到摄像头\n");
        return 1;
    }
    if (capture_start(fd) < 0) {
        printf("[test] 启动采集失败\n");
        capture_stop(fd);
        close(fd);
        return 1;
    }

    /* ---- 解码器 ---- */
    if (decoder_init(1280, 720) != 0) {
        printf("[test] 解码器初始化失败\n");
        capture_stop(fd);
        close(fd);
        return 1;
    }

    const int TOTAL = 60;
    int     n     = 0;
    qint64  total = 0;
    int     timeoutCnt = 0;

    while (n < TOTAL) {
        if (capture_grab(fd) != 0) {
            printf("[test] 取帧失败（第 %d 帧）\n", n);
            break;
        }

        size_t len = capture_frame_size();
        const void *jpg = capture_frame_data();

        QElapsedTimer t;
        t.start();

        if (decoder_decode(jpg, len) < 0) {
            printf("[test] decode 调用失败（第 %d 帧）\n", n);
            capture_release(fd);
            break;
        }
        capture_release(fd);          /* 立刻还，不等解码 */

        /* 等回调（最多 200ms）*/
        while (!decoder_take_new() && t.elapsed() < 200)
            usleep(500);

        qint64 ms = t.elapsed();
        if (ms >= 200) {
            ++timeoutCnt;
            printf("[test] 第 %d 帧等回调超时\n", n);
            continue;
        }

        total += ms;
        if (n < 3 || n % 10 == 0) {
            printf("[dec] 帧%-3d  %3lld ms  Yvir=%p  Yphy=%p  %dx%d\n",
                   n, ms,
                   (void *)decoder_y_virt(), (void *)decoder_y_phys(),
                   decoder_w(), decoder_h());
        }
        n++;
    }

    printf("\n[dec] 成功 %d 帧，超时 %d 帧\n", n, timeoutCnt);
    if (n > 0)
        printf("[dec] 平均 %lld ms/帧 → %.1f fps 上限\n",
               total / n, 1000.0 * n / total);

    decoder_destroy();
    capture_stop(fd);
    close(fd);
    return 0;
}
