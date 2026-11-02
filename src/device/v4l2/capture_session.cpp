// SPDX-License-Identifier: MIT
/** @file capture_session.cpp
 *  @brief capture_session.h 的实现
 */
#include "capture_session.h"

#include "capture.h"        /* 设备/流 + 格式协商 + WIN_*/CROP_* 启动几何 */
#include "camera_ctl.h"     /* camera_ctl_apply_settings（控制项下发的时机归它） */
#include "pipeline.h"       /* 解码器 + 显示层（唯一开关入口） */
#include "display.h"        /* display_is_visible / display_set_visible（门控读） */
#include "settings_c.h"     /* 协商失败时退回的配置值（C 面，无 Qt） */
#include "log.h"

#include <unistd.h>
#include <sys/types.h>      /* size_t */

/* =============================================================================
 *  ① 等设备
 * ========================================================================== */
bool CaptureSession::waitForDevice(volatile const bool &running)
{
    while (running) {
        m_fd = capture_init();
        if (m_fd >= 0)
            return true;
        sleep(2);       /* 没插摄像头就在这里等，不用重启程序 */
    }
    return false;       /* 等待期间被要求退出 */
}

/* =============================================================================
 *  ②③④ 协商 → 管线 → 开流 → 下发
 * ========================================================================== */
bool CaptureSession::start()
{
    /* ---- ② 格式协商 ----
     * 失败时退回配置值：至少让解码/显示链路按一个确定的尺寸建起来，
     * 否则 display_src_w() 永远是 0，界面会一直等下去。 */
    unsigned srcFps = 0;
    if (capture_negotiate(m_fd, &m_srcW, &m_srcH, &srcFps) < 0) {
        m_srcW = (unsigned)settings_get_int("capture/width",  CAP_W);
        m_srcH = (unsigned)settings_get_int("capture/height", CAP_H);
        LOGW("cap", "格式协商失败，按配置 %ux%u 继续", m_srcW, m_srcH);
    }

    /* ---- ③ 建出图管线（解码器 + DE 图层，必须用协商后的实际尺寸）----
     * crop 传的是 720p 基准值（capture.h 的 CROP_Y/CROP_H），
     * AppController 会在源尺寸就绪后按实际分辨率等比换算再下发一次。 */
    if (pipeline_open((int)m_srcW, (int)m_srcH, CROP_Y, CROP_H,
                      WIN_X, WIN_Y, WIN_W, WIN_H) < 0) {
        LOGE("cap", "建出图管线失败 %ux%u", m_srcW, m_srcH);
        close(m_fd);                     /* 没有出图能力，继续跑一个黑屏线程没意义 */
        m_fd = -1;
        return false;
    }

    /* ---- ④ 开流 ---- */
    if (capture_start(m_fd) < 0) {
        LOGE("cap", "启动采集失败");
        pipeline_close();                /* 开不了流，管线也别留着 */
        close(m_fd);
        m_fd = -1;
        return false;
    }

    /* 下发配置里的图像控制项（时机归 camera_ctl，见它的头文件注释） */
    camera_ctl_apply_settings();
    return true;
}

/* =============================================================================
 *  换分辨率 / 帧率（设置页「保存」触发）
 * =============================================================================
 *  必须由本线程做：video_fd、解码器、DE 图层句柄都在它手里。
 *
 *  顺序有硬约束：
 *    停流 + 归还 buffer   ← 不归还的话 S_FMT 会返回 EBUSY（V4L2 规定）
 *    ↓
 *    S_FMT                ← 提要求
 *    ↓
 *    G_FMT                ← 读回实际生效值，写回配置（界面显示的就是这个）
 *    ↓
 *    pipeline_reconfigure ← 只重建解码器 + 改源尺寸（★ 不能 close+open，见 pipeline.h）
 *    ↓
 *    重新 REQBUFS + STREAMON
 *
 *  crop 不在这里算 —— 由 AppController 发现源尺寸变了之后重算并下发。
 * ========================================================================== */
int CaptureSession::switchFormat(unsigned nw, unsigned nh, unsigned nf)
{
    LOGI("cap", "切换格式 → 请求 %ux%u @%ufps（当前 %ux%u）",
         nw, nh, nf, m_srcW, m_srcH);

    /* ★ 先关视频层：DE 图层此刻还引用着旧解码器的 dma_buf fd，
     *   decoder_destroy 会 free 那些内存并 close 对应的 fd，
     *   不先关层的话 DE 下一个 vsync 就会 dma_buf_get 失败。 */
    display_set_visible(0);

    capture_stop(m_fd);                  /* STREAMOFF + munmap + REQBUFS(0) */

    if (capture_set_format(m_fd, nw, nh, nf) < 0) {
        /* 切不过去：按原来的尺寸重建，保证画面至少还能出来 */
        LOGW("cap", "S_FMT 失败，按原尺寸 %ux%u 重建", m_srcW, m_srcH);
        capture_set_format(m_fd, m_srcW, m_srcH, 0);   /* fps 传 0 = 不动帧率 */
        capture_start(m_fd);
        camera_ctl_apply_settings();
        return -1;
    }

    /* 读回实际生效值（可能和 nw/nh 不一样，驱动会给最接近的） */
    unsigned aw = m_srcW, ah = m_srcH, af = 0;
    capture_query_format(m_fd, &aw, &ah, &af);
    if (aw == 0 || ah == 0) {
        aw = nw;                         /* 读回失败就信任请求值，总比 0 好 */
        ah = nh;
    }

    /* 实际生效值写回配置 —— 界面读文件，所以显示的必然是硬件真在跑的 */
    settings_set_int("capture/width",  (int)aw);
    settings_set_int("capture/height", (int)ah);
    if (af > 0)
        settings_set_int("capture/fps", (int)af);

    /* ★ 按新尺寸重配解码器。
     *   这里**不能**用 pipeline_close()+pipeline_open()：display_init() 会把 fb0 的
     *   视频区刷 0（那是开机用的），而此刻 Qt 的界面正画在那块内存上 ——
     *   擦掉后 Qt 不知道、不重绘 → 换格式后直接黑屏。
     *   实际踩过：摄像头页点「保存」，屏幕中间黑掉。 */
    if (pipeline_reconfigure((int)aw, (int)ah) < 0)
        LOGE("cap", "重配出图管线失败（%ux%u）", aw, ah);

    m_srcW = aw;                         /* 就地更新"当前尺寸"，主循环靠它 */
    m_srcH = ah;

    if (capture_start(m_fd) < 0) {
        LOGE("cap", "重新开流失败");
        return -1;                       /* 调用方会走重连流程 */
    }

    /* 切完再下发一次控制项：保持"输出稳定在配置值"（实测 STREAMON 不复位控制项）。 */
    camera_ctl_apply_settings();

    LOGI("cap", "已切换 → 生效 %ux%u @%ufps", aw, ah, af);
    return 0;
}

/* =============================================================================
 *  断线重连（主循环 else 分支）
 * ========================================================================== */
void CaptureSession::reconnect()
{
    /* 走到这里说明 capture_grab 返回 -1（真错误）**或 -2（poll 1 秒超时）**。
     * 现状（有意为之）：**-2 也重连**。理由：30fps 下 poll 等 1 秒还没帧，
     * 等于丢了 30 帧，这已经不是抖动而是真 stall，重连是合理的恢复手段。
     * 代价是恢复期间约 1~2 秒无画面。
     * 实测：12000 帧运行 0 次触发（`grep -c '设备异常'` = 0）。 */
    LOGE("cap", "设备异常，开始重连");

    capture_stop(m_fd);                  /* STREAMOFF 会失败（设备没了），无害 */
    close(m_fd);

    sleep(1);                            /* 给 USB 枚举一点时间，立刻重开常常还是失败的 */
    m_fd = capture_init();
    if (m_fd < 0) {
        sleep(2);
        return;                          /* 下一轮再试（grab 会因 fd=-1 再次失败进入这里） */
    }

    /* 重新协商。配置里存的就是上次的实际值，通常尺寸不变；
     * 万一变了，解码器和 DE 图层已按旧尺寸建好，这里只报警不重建
     * —— 重建要同时动 decoder / display / page 三处，属于独立的一步。 */
    unsigned nw = m_srcW, nh = m_srcH, nf = 0;
    if (capture_negotiate(m_fd, &nw, &nh, &nf) == 0
        && (nw != m_srcW || nh != m_srcH)) {
        LOGW("cap", "注意：重连后分辨率变了 %ux%u → %ux%u，"
                    "显示链路未重建（需重启程序）",
             m_srcW, m_srcH, nw, nh);
    }

    if (capture_start(m_fd) < 0) {
        close(m_fd);
        m_fd = -1;                       /* 标记失败，下一轮再试 */
        sleep(2);
        return;
    }
    camera_ctl_apply_settings();         /* 重连等于重新开流，控制项再下发一次 */
    LOGI("cap", "重连成功");
}

/* =============================================================================
 *  ⑤ 主循环
 * ========================================================================== */
void CaptureSession::run(volatile const bool &running)
{
    while (running) {
        /* 设置页点了「保存」就在这里换格式，换完再继续取帧。
         * 放在 grab 之前：换格式要停流，不能让旧帧先出来。 */
        unsigned rw = 0, rh = 0, rf = 0;
        if (capture_take_format_request(&rw, &rh, &rf))
            switchFormat(rw, rh, rf);

        int r = capture_grab(m_fd);
        if (r == 0) {
            /* ★ 视频层关着（设置页 / 摄像头页）→ 跳过整条解码流水线。
             *  【省的是什么】省的是"解码"本身（Cedar 1080p ≈ 单核 41%），
             *  如果只在送显前跳过，解码照样在跑，一分钱省不到。
             *  【顺序：必须先 capture_release 再 continue】
             *  4 块缓冲全靠 QBUF 循环复用，漏还一次就少一块，4 次之后队列空
             *  → poll 永远超时 → 被 else 判成"设备异常"去重连。
             *  表现是"在设置页待一会儿，回来就黑屏重连"。 */
            if (!display_is_visible()) {
                capture_release(m_fd);
                continue;
            }

            const size_t len = capture_frame_size();

            /* USB 掉线时会拿到空帧/残帧，喂给解码器会让 VE 卡住超时。
             * 1000 字节是经验阈值：720p 的 MJPEG 一帧通常 20~60KB，
             * 小于 1KB 只可能是 USB 断流的残包，不可能是正常画面。 */
            if (len < 100) {
                LOGW("cap", "残帧 %zu 字节，丢弃", len);
                capture_release(m_fd);
                continue;
            }

            /* 解码 + 等回调 + 取 dma_buf fd + 送 DE，全在 pipeline 里
             * （含 200ms 超时判定与每 1000 帧一行的存活日志）。
             * 出错只记一行就继续下一帧 —— 销毁重建要停流，代价远大于丢一帧。 */
            if (pipeline_feed(capture_frame_data(), len) < 0)
                LOGE("dec", "本帧未出图");

            capture_release(m_fd);       /* 用完立刻还，别攒着 */

        } else {
            reconnect();
        }
    }
}

/* =============================================================================
 *  ⑥ 收尾
 * ========================================================================== */
void CaptureSession::shutdown()
{
    /* ★ 第一句必须是 pipeline_close()：它内部保证
     *   `display_set_visible(0)` → `decoder_destroy()` → `display_destroy()`。
     *   以前直接 `decoder_destroy()` 漏掉了关视频层 ——
     *   实测：进程退出后 DE ch[0] 仍 enable 指着已释放的 ION 缓冲，屏幕冻结
     *   （20 号文 C1；契约由 pipeline 单点兑现后漏不掉）。 */
    pipeline_close();
    capture_stop(m_fd);
    close(m_fd);
    m_fd = -1;
}
