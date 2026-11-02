/*
 * decode_verify —— 验证采集数据有效性 + 两条解码路线的性能
 * =============================================================================
 *  对同一批 MJPEG 帧分别走：
 *    ① Qt 软解（QImage::loadFromData）—— 验证 buffer 里是不是完整有效的 JPEG
 *    ② Cedar 硬解（AWVideoDecoder）—— 测真实性能
 *
 *  输出：
 *    [qt]    帧N: OK/FAIL  1280x720  XXms      ← FAIL 说明采集数据有问题
 *    [cedar] 帧N: OK/FAIL  XXms
 *    统计：成功率 + 平均耗时 + fps 上限
 * =============================================================================
 */

#include <QImage>
#include <QElapsedTimer>
#include <cstdio>
#include <cstring>
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
        printf("[test] 采集启动失败\n");
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

    const int TOTAL = 30;
    int    n        = 0;
    int    qtOk     = 0;
    int    cedarOk  = 0;
    qint64 qtTotal  = 0;
    qint64 cedarTotal = 0;

    while (n < TOTAL) {
        if (capture_grab(fd) != 0) {
            printf("[test] 取帧失败（第 %d 帧）\n", n);
            break;
        }

        const uchar *jpg = (const uchar *)capture_frame_data();
        size_t       len = capture_frame_size();

        /* ---- ① Qt 软解：验证数据有效性 ---- */
        QElapsedTimer t1;
        t1.start();
        QImage img;
        bool ok = img.loadFromData(jpg, (int)len, "JPG");
        qint64 qtMs = t1.elapsed();
        qtTotal += qtMs;
        if (ok) ++qtOk;

        if (n < 3 || !ok)
            printf("[qt]    帧%-2d: %s  %dx%d  %lldms  (%zu 字节)\n",
                   n, ok ? "OK  " : "FAIL", img.width(), img.height(), qtMs, len);

        /* ---- ② Cedar 硬解 ---- */
        QElapsedTimer t2;
        t2.start();
        int ret = decoder_decode(jpg, len);
        /* 异步：等回调出帧（最多 200ms）*/
        while (!decoder_take_new() && t2.elapsed() < 200)
            usleep(500);
        qint64 cedarMs = t2.elapsed();
        cedarTotal += cedarMs;
        if (ret >= 0) ++cedarOk;

        if (n < 3 || ret < 0)
            printf("[cedar] 帧%-2d: %s  %lldms  Yvir=%p Yphy=%p\n",
                   n, ret >= 0 ? "OK  " : "FAIL", cedarMs,
                   (void *)decoder_y_virt(), (void *)decoder_y_phys());

        capture_release(fd);
        ++n;
    }

    printf("\n========== 统计（%d 帧）==========\n", n);
    if (n > 0) {
        printf("Qt 软解   : %d/%d 成功   平均 %lld ms → %.1f fps 上限\n",
               qtOk, n, qtTotal / n, 1000.0 * n / (qtTotal ? qtTotal : 1));
        printf("Cedar 硬解: %d/%d 成功   平均 %lld ms → %.1f fps 上限\n",
               cedarOk, n, cedarTotal / n, 1000.0 * n / (cedarTotal ? cedarTotal : 1));
    }

    decoder_destroy();
    capture_stop(fd);
    close(fd);
    return 0;
}
