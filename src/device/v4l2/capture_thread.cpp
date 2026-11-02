// SPDX-License-Identifier: MIT
/** @file capture_thread.cpp
 *  @brief 采集线程入口 —— **只剩线程函数**
 *
 *  【本文件为什么这么短】
 *    采集的全部行为分散在四个模块（见下），本文件只负责把会话跑起来：
 *
 *      · 设备与流、格式协商   → capture.c（capture_init / capture_negotiate）
 *      · 控制项下发的时机     → camera_ctl.c（camera_ctl_apply_settings）
 *      · 解码器 + 显示层      → pipeline.cpp（pipeline_open/feed/reconfigure/close）
 *      · 组装与恢复策略       → capture_session.cpp（CaptureSession）
 *
 *    本文件只剩：**创建会话 → 等 → 跑 → 收尾**。它也因此不 include 任何
 *    capture/decoder/display 的头 —— 会话把它包住了。
 *
 *  【线程模型】
 *    main.cpp：pthread_create(capture_thread) → 本函数；
 *    g_running（main.cpp 定义）置 false → 会话退出 → pthread_join。
 *    video_fd / 解码器 / DE 句柄都是有状态资源，只能单线程持有 ——
 *    UI 只能通过 capture_request_format() 的请求队列登记，会话在主循环里执行。
 */
#include "capture_thread.h"
#include "capture_session.h"
#include "log.h"

/* 定义在 main.cpp。主线程置 false → 会话在下一个检查点退出。 */
extern volatile bool g_running;

void *capture_thread(void *)
{
    CaptureSession session;

    /* ① 等设备（每 2 秒重试；等待期间被要求退出就直接结束） */
    if (!session.waitForDevice(g_running))
        return nullptr;

    /* ②③④ 格式协商 + 建出图管线 + 开流。
     * 失败即致命（会话内部已清理），没有出图能力的线程没有存在的意义。 */
    if (!session.start())
        return nullptr;

    /* ⑤ 主循环：换格式请求 / 取帧 / 门控 / 解码送显 / 断线重连 */
    session.run(g_running);

    /* ⑥ 收尾：关视频层 → 销毁解码器 → 关显示 → 停流 → 关设备 */
    session.shutdown();
    return nullptr;
}
