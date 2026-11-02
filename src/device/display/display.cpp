// SPDX-License-Identifier: MIT
/** @file display.cpp
 *  @brief DE 视频图层实现（layer 配置 + 可见性门控）
 */
#include "display.h"
#include "log.h"
#include <pthread.h>

/* ---- DE 句柄与图层配置 ---- */
static int g_disp = -1;                        // /dev/disp 的 fd
static struct disp_layer_config2 g_cfg;        // 图层配置（全局，每帧改 fd）

/* ---- 门控状态（方案 C 新增，全部受 g_lock 保护）---- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_visible  = 0;      // 视频层是否该显示（0=关，1=开）
static int g_hasFrame = 0;      // 是否已经收到过至少一帧
static int g_lastFd   = -1;     // 最近一次的 dma_buf fd（重新显示时补这个）
static int g_srcW     = 0;      // 解码输出宽（display_init 入参，给 AppController 查）
static int g_srcH     = 0;      // 解码输出高

/* 下发一次图层配置。调用者必须已经持有 g_lock。 */
static int push_locked(void)
{
    unsigned long arg[4] = { 0, (unsigned long)&g_cfg, 1, 0 };
    return ioctl(g_disp, DISP_LAYER_SET_CONFIG2, arg);
}

/* 注意：这里不定义任何布局预设。
 * 每一页显示什么区域由 page.h 的 pageLayout() 唯一描述，
 * AppController 把它转成 DisplayLayout 后传进来。 */

int display_init(int srcW, int srcH, int cropY, int cropH,
                 int winX, int winY, int winW, int winH)
{
    /* ---- ① open 并存到全局 ---- */
    g_disp = open("/dev/disp", O_RDWR);
    if (g_disp < 0) { perror("[display] /dev/disp"); return -1; }

    /* ---- ② 只清视频区（★ 不能清整屏，否则 Qt 画的顶栏底栏会被擦掉）----
     * fb0 是 1024×600、stride 4096 字节/行。
     *   y = 0        ~ winY          顶栏（Qt 画的，不动）
     *   y = winY     ~ winY+winH     视频区（清 0 → alpha=0 → ch[0] 透出）
     *   y = winY+winH~ 600           底栏（Qt 画的，不动）
     * */
    int fb = open("/dev/fb0", O_WRONLY);
    if (fb >= 0) {
        const size_t clear_len = (size_t)winH * 4096;
        unsigned char *zero = new unsigned char[clear_len]();
        if (zero) {
            ssize_t n = pwrite(fb, zero, clear_len, (off_t)winY * 4096);
            (void)n;
            delete[] zero;
        }
        close(fb);
    } else {
        perror("[display] /dev/fb0");
    }

    /* ---- ③ 配 cfg（用参数，不硬编码）---- */
    memset(&g_cfg, 0, sizeof(g_cfg));

    /* 【DE 三级索引：channel / layer / layer_id】
     *   disp → channel → layer 三层，混合顺序由 **channel** 决定，
     *   zorder 只在同一个 channel 内部生效（这一步是本项目的核心结论，见 08-DE显示引擎）。
     *
     *   出厂配置：ch[0] = 视频层(YUV)，ch[1] = weston UI 层(RGB)。
     *   我们只碰 ch[0]，Qt 画在 fb0 上、由 ch[1] 扫描出去。
     *
     *   channel/layer_id 都固定 0：本工程只用一个视频图层，不做多层叠加。 */
    g_cfg.channel  = 0;
    g_cfg.layer_id = 0;
    g_cfg.enable   = 0;      /* 先关着，等 AppController 判断该页要不要显示视频再开 */

    /* 【字段为什么是这些值】
     *   mode = LAYER_MODE_BUFFER   用 dma_buf fd 直接扫，不做内存拷贝（零拷贝的前提）
     *   zorder = 1                 同 channel 内最低层；跨 channel 无效，别指望它压 UI
     *   alpha_mode = 2 (PIXEL)     用像素自带 alpha；fb0 那块 alpha=0 就是"让下面透出来"
     *   alpha_value = 0xff         全局 alpha 拉满，具体透明度交给 alpha_mode 逐像素决定
     *
     *   fmt = YUV420_SP_UVUV       即 NV12（Y 平面 + 交错 UV 平面），Cedar 解码器的输出格式
     *   size[0] = 全尺寸（Y），size[1]/size[2] = 半尺寸（UV）
     *                               ★ NV12 的 UV 平面宽高都只有 Y 的一半，填错会整屏花
     *
     *   crop.* 是 32.32 定点数（高 32 位整数、低 32 位小数），所以 << 32。
     *           DE 只做"裁一块 → 拉到 window"，不做等比缩放，
     *           所以 crop 的宽高比必须等于 window 的宽高比，否则画面变形。
     *           这个比例由 AppController::toDisplayLayout() 保证。 */

    g_cfg.info.mode        = LAYER_MODE_BUFFER;
    g_cfg.info.zorder      = 1;
    g_cfg.info.alpha_mode  = 2;
    g_cfg.info.alpha_value = 0xff;

    g_cfg.info.fb.format         = DISP_FORMAT_YUV420_SP_UVUV;
    g_cfg.info.fb.size[0].width  = srcW;
    g_cfg.info.fb.size[0].height = srcH;
    g_cfg.info.fb.size[1].width  = srcW / 2;
    g_cfg.info.fb.size[1].height = srcH / 2;
    g_cfg.info.fb.size[2].width  = srcW / 2;
    g_cfg.info.fb.size[2].height = srcH / 2;

    g_cfg.info.fb.crop.x      = 0;
    g_cfg.info.fb.crop.y      = (long long)cropY << 32;
    g_cfg.info.fb.crop.width  = (long long)srcW  << 32;
    g_cfg.info.fb.crop.height = (long long)cropH << 32;

    g_cfg.info.screen_win.x      = winX;
    g_cfg.info.screen_win.y      = winY;
    g_cfg.info.screen_win.width  = winW;
    g_cfg.info.screen_win.height = winH;

    g_cfg.info.fb.align[0] = 4;
    g_cfg.info.fb.flags    = DISP_BF_NORMAL;
    g_cfg.info.fb.scan     = DISP_SCAN_PROGRESSIVE;
    g_cfg.info.id          = 0;
    g_cfg.info.fb.fd       = -1;

    /* ---- ④ 初始化门控状态 ----
     * 初始 g_visible = 0：图层先关着，等 AppController 切到主界面时再打开 */
    pthread_mutex_lock(&g_lock);
    /* ★ 这里不清 g_visible（见下方说明）。
     *   AppController::bootApply 是 250ms 定时器，本函数在采集线程里跑，
     *   两者先后没有保证。实测（2026-10-28）bootApply 先到：它把
     *   g_visible 置 1，紧接着本函数如果把它清 0，那个"要显示视频"的
     *   决定就永久丢了 —— 之后每一帧 display_update_fd() 都只缓存 fd、
     *   不下发 ioctl，DE 里只剩 ch[1]（fb0），视频区是透明洞
     *   但背后没有图层 → 全黑。
     *   所以：g_visible 归调用者（AppController）管，本函数只清"帧"状态。 */
    g_hasFrame = 0;
    g_lastFd   = -1;
    /* 源尺寸存下来：AppController 要用它把 page.cpp 的基准 crop
     * 换算到实际采集分辨率。 */
    g_srcW     = srcW;
    g_srcH     = srcH;

    /* ---- ⑤ 下发一次（enable=0，配置记住但不显示）---- */
    if (push_locked() != 0) {
        pthread_mutex_unlock(&g_lock);
        LOGE("disp", "初始配置失败");
        close(g_disp);
        g_disp = -1;
        return -1;
    }
    pthread_mutex_unlock(&g_lock);

    LOGI("disp", "初始化成功 %dx%d crop(y=%d,h=%d) → win(%d,%d,%d,%d)", srcW, srcH, cropY, cropH, winX, winY, winW, winH);
    return 0;
}

/* =============================================================================
 *  每帧送显（capture_thread 调用）
 * ========================================================================== */
int display_update_fd(int fd)
{
    if (fd < 0) return -1;

    pthread_mutex_lock(&g_lock);

    if (g_disp < 0) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    /* 不管显不显示，都记住最新帧 */
    g_lastFd   = fd;
    g_hasFrame = 1;

    int ret = 0;
    if (g_visible) {
        g_cfg.info.fb.fd = fd;
        g_cfg.enable     = 1;
        ret = push_locked();
    }

    pthread_mutex_unlock(&g_lock);
    return ret;
}

/* =============================================================================
 *  开关视频层（AppController 切页时调用）
 * =============================================================================
 *  on = 1：若已有缓存帧，立即用 g_lastFd 打开 —— 不需要等下一帧解码，
 *          所以从遮挡页返回时视频是"立刻"出现的。
 *  on = 0：关闭图层。之后 display_update_fd 只缓存不发。
 * ========================================================================== */
int display_set_visible(int on)
{
    pthread_mutex_lock(&g_lock);

    g_visible = on ? 1 : 0;

    if (g_disp < 0) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    if (!on) {
        g_cfg.enable = 0;
    } else if (g_hasFrame) {
        g_cfg.info.fb.fd = g_lastFd;      /* 用最近一帧立即恢复 */
        g_cfg.enable     = 1;
    }
    /* 若 on=1 但还没收到过帧，保持 enable=0；
     * 第一帧到了之后 display_update_fd 会自动打开，不用在这里等。 */

    int ret = push_locked();

    pthread_mutex_unlock(&g_lock);

    LOGI("disp", "视频层 %s", on ? (g_hasFrame ? "打开" : "打开（等待首帧）") : "关闭");
    return ret;
}

/* =============================================================================
 *  查询视频层是否显示（读接口，给 capture_thread 用）
 * ========================================================================== */
int display_is_visible(void)
{
    pthread_mutex_lock(&g_lock);
    const int v = g_visible;
    pthread_mutex_unlock(&g_lock);
    return v;
}

/* =============================================================================
 *  应用一套显示布局（改 crop + screen_win，立即下发）
 * =============================================================================
 *  推荐调用顺序：先 display_set_layout()，再 display_set_visible(1)。
 *  这样视频层打开的那一刻就是正确布局，不会先闪一帧旧布局。
 * ========================================================================== */
int display_set_layout(const DisplayLayout *layout)
{
    if (!layout)
        return -1;

    pthread_mutex_lock(&g_lock);

    if (g_disp < 0) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    /* crop 用 32.32 定点：值 << 32 */
    g_cfg.info.fb.crop.x      = (long long)layout->cropX << 32;
    g_cfg.info.fb.crop.y      = (long long)layout->cropY << 32;
    g_cfg.info.fb.crop.width  = (long long)layout->cropW << 32;
    g_cfg.info.fb.crop.height = (long long)layout->cropH << 32;

    /* 屏幕窗口是普通整数，不用定点 */
    g_cfg.info.screen_win.x      = layout->winX;
    g_cfg.info.screen_win.y      = layout->winY;
    g_cfg.info.screen_win.width  = layout->winW;
    g_cfg.info.screen_win.height = layout->winH;

    const int ret = push_locked();

    pthread_mutex_unlock(&g_lock);

    LOGI("disp", "布局 crop(%d,%d,%d,%d) → win(%d,%d,%d,%d)", layout->cropX, layout->cropY, layout->cropW, layout->cropH,
            layout->winX,  layout->winY,  layout->winW,  layout->winH);
    return ret;
}

int display_set_src_size(int srcW, int srcH)
{
    if (srcW <= 0 || srcH <= 0)
        return -1;

    pthread_mutex_lock(&g_lock);

    g_srcW = srcW;
    g_srcH = srcH;

    /* ★★ 这里必须把"上一帧"作废。
     *   调用方（换分辨率）刚重建过解码器，旧的 dma_buf fd 已经 close 掉了。
     *   如果不清 g_hasFrame/g_lastFd，接下来 display_set_visible(1) 会拿这个
     *   **失效的 fd** 去 enable 图层 → 内核报 `dma_buf_get failed` + `disp dma map fail!`
     *   → DE 停在"映射失败"状态，之后每帧 update_fd 也救不回来（呈现为视频区全黑）。
     *   清掉之后：set_visible(1) 只会把 g_visible 置 1，图层保持 enable=0；
     *   等第一帧到达，display_update_fd() 用**新** fd 打开，一次干净 enable。 */
    g_hasFrame = 0;
    g_lastFd   = -1;

    /* NV12：Y 平面全尺寸，UV 平面各半尺寸 */
    g_cfg.info.fb.size[0].width  = srcW;
    g_cfg.info.fb.size[0].height = srcH;
    g_cfg.info.fb.size[1].width  = srcW / 2;
    g_cfg.info.fb.size[1].height = srcH / 2;
    g_cfg.info.fb.size[2].width  = srcW / 2;
    g_cfg.info.fb.size[2].height = srcH / 2;

    const int ret = push_locked();

    pthread_mutex_unlock(&g_lock);

    LOGI("disp", "源尺寸改为 %dx%d", srcW, srcH);
    return ret;
}

int display_src_w(void)
{
    pthread_mutex_lock(&g_lock);
    const int v = g_srcW;
    pthread_mutex_unlock(&g_lock);
    return v;
}

int display_src_h(void)
{
    pthread_mutex_lock(&g_lock);
    const int v = g_srcH;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void display_destroy(void)
{
    pthread_mutex_lock(&g_lock);

    if (g_disp < 0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    g_visible    = 0;
    g_srcW       = 0;
    g_srcH       = 0;
    g_cfg.enable = 0;
    push_locked();

    close(g_disp);
    g_disp = -1;

    pthread_mutex_unlock(&g_lock);

    LOGI("disp", "已关闭视频层");
}
