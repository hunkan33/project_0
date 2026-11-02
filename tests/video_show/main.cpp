/*
 * video_show —— 路线 B 验证程序（软解 + Qt 显示）
 * =============================================================================
 *  采集线程 30fps → g_latest（共享最新帧）
 *  主线程 QTimer(33ms) → 解码 → 缩放 → 裁剪 → 写 fb0
 *
 *  【为什么用 write() 而不是 mmap 写 fb0】
 *    实测：mmap 写 /dev/fb0 后，读回数据正确（ff ff ff），
 *          但屏幕上完全不变——数据停在 CPU cache 里，没到物理内存。
 *    原因：全志的 fb0 驱动 mmap 时没有设 uncached，DE 通过 DMA
 *          直接读物理内存，读不到 cache 里的数据。
 *    write() 走 fb_write，内核会主动 flush cache，所以有效。
 *    代价：1.9MB / 70MB/s ≈ 27ms（解码占 99ms，可接受）
 *
 *  【路线 A 不会有这个问题】
 *    DE 图层直接用物理地址，CPU 完全不参与，绕开 cache。
 * =============================================================================
 */

#include <QApplication>
#include <QThread>
#include <QByteArray>
#include <QImage>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QDebug>

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

#include "capture.h"

/* 视频区：和主程序一致 (0, 48, 1024, 480) */
static const int VX = 0;
static const int VY = 48;
static const int VW = 1024;
static const int VH = 480;

/* fb0 每行字节数（1024 * 4） */
static const int FB_STRIDE = 1024 * 4;

/* ---- fb0：用 write 而不是 mmap ---- */
static int        g_fb = -1;
static QByteArray g_blitBuf;          /* 拼接用，非紧凑格式时启用 */

static bool fb_open()
{
    g_fb = open("/dev/fb0", O_WRONLY);
    if (g_fb < 0) {
        perror("open /dev/fb0");
        return false;
    }
    return true;
}

static void fb_blit(const QImage &img)
{
    const bool compact = (img.bytesPerLine() == FB_STRIDE && img.width() == VW);
    const void *src;
    size_t len;

    if (compact) {
        src = img.constBits();
        len = (size_t)FB_STRIDE * VH;
    } else {
        g_blitBuf.resize(FB_STRIDE * VH);
        for (int y = 0; y < VH; ++y)
            memcpy(g_blitBuf.data() + (size_t)y * FB_STRIDE,
                   img.scanLine(y), (size_t)VW * 4);
        src = g_blitBuf.constData();
        len = (size_t)g_blitBuf.size();
    }

    /* fb0 是 1024x1200（双缓冲），DE 可能扫描上半或下半，
       两块都写，无论显示哪块都能看到画面 */
    for (int pass = 0; pass < 2; ++pass) {
        const off_t off = (off_t)(VY + pass * 600) * FB_STRIDE;
        if (pwrite(g_fb, src, len, off) < 0)
            perror("pwrite fb0");
    }
}

/* ---- 采集线程 → 主线程 的唯一缓冲 ---- */
static QMutex     g_lock;
static QByteArray g_latest;
static bool       g_hasNew = false;

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    QApplication app(argc, argv);

    if (!fb_open())
        return 1;
    printf("[fb] /dev/fb0 已打开（write 模式）\n");

    static volatile bool running = true;

    /* ---- 采集线程：只往 g_latest 里塞 ---- */
    QThread *th = QThread::create([&]() {
        int fd = capture_init();
        if (fd < 0) { qWarning() << "找不到摄像头"; return; }
        if (capture_start(fd) < 0) { qWarning() << "启动采集失败"; return; }

        while (running) {
            int r = capture_grab(fd);

            /* 任何异常都重连：
                 r == -2  poll 超时（URB 停了）
                 r <  0   端点异常（POLLERR/POLLHUP，设备断开）*/
            if (r != 0) {
                fprintf(stderr, "[test] 采集异常 r=%d，2 秒后重连\n", r);
                if (fd >= 0) { capture_stop(fd); ::close(fd); fd = -1; }
                sleep(2);
                fd = capture_init();
                if (fd < 0) { sleep(2); continue; }
                if (capture_start(fd) < 0) { ::close(fd); fd = -1; sleep(2); continue; }
                fprintf(stderr, "[test] 重连成功\n");
                continue;
            }

            QByteArray jpg((const char *)capture_frame_data(),
                           (int)capture_frame_size());
            capture_release(fd);

            {
                QMutexLocker lk(&g_lock);
                g_latest = jpg;         /* 替换，旧帧自动释放 */
                g_hasNew = true;
            }
        }

        capture_stop(fd);               /* STREAMOFF + munmap，避免下次 busy */
        ::close(fd);
        printf("[test] 采集线程退出\n");
    });

    th->start();

    /* ---- 主线程：33ms 取最新帧 → 解码 → 显示 ---- */
    QTimer *timer = new QTimer();
    QObject::connect(timer, &QTimer::timeout, [&]() {
        QByteArray jpg;
        {
            QMutexLocker lk(&g_lock);
            if (!g_hasNew) return;
            jpg = g_latest;
            g_hasNew = false;
        }

        QElapsedTimer tt; tt.start();

        QImage img;
        if (!img.loadFromData(jpg, "JPG")) {
            printf("[test] 解码失败 size=%d\n", jpg.size());
            return;
        }
        qint64 tDec = tt.elapsed();

        /* 等比放大到铺满，再居中裁剪到 1024×480 */
        QImage s = img.scaled(VW, VH,
                              Qt::KeepAspectRatioByExpanding,
                              Qt::FastTransformation);
        qint64 tScale = tt.elapsed();

        QImage c = s.copy((s.width()  - VW) / 2,
                          (s.height() - VH) / 2, VW, VH);
        qint64 tCrop = tt.elapsed();

        /* ★ 转成 RGB32，保证 bytesPerLine == 4096，可整块 pwrite */
        QImage c32 = (c.format() == QImage::Format_RGB32 ||
                      c.format() == QImage::Format_ARGB32)
                     ? c : c.convertToFormat(QImage::Format_RGB32);

        fb_blit(c32);
        qint64 tBlit = tt.elapsed();

        static int shown = 0;
        ++shown;
        if (shown <= 3 || shown % 30 == 0)
            printf("[show] 第 %d 帧 | 解码=%lld 缩放=%lld 裁剪=%lld 写屏=%lld ms | %dx%d fmt=%d\n",
                   shown, tDec, tScale - tDec, tCrop - tScale, tBlit - tCrop,
                   c32.width(), c32.height(), (int)c32.format());
    });
    timer->start(33);

    int ret = app.exec();

    running = false;
    th->wait();
    delete th;
    if (g_fb >= 0) ::close(g_fb);

    return ret;
}
