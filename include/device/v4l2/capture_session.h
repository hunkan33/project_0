// SPDX-License-Identifier: MIT
/** @file capture_session.h
 *  @brief 一次"采集会话"：设备 fd + 当前格式 + 出图管线的**完整生命周期**
 *
 *  【分工】
 *    "取帧"是线程的本职；开设备、格式协商、控制项下发时机、重建解码器、
 *    断线重连这些**会话管理**，全在这一层。
 *      · `capture.c`          —— V4L2 设备与流（含 `capture_negotiate` 格式协商）
 *      · `camera_ctl.c`       —— 控制项下发（含 `camera_ctl_apply_settings` 时机）
 *      · `pipeline.cpp`       —— 解码器 + 显示层（销毁契约唯一出口）
 *      · `CaptureSession`     —— 把上面三者按正确顺序组装；**出错时的恢复策略**
 *      · `capture_thread.cpp` —— **只剩线程函数**：创建会话 → 等 → 跑 → 收尾
 *
 *  【线程模型不变】
 *    video_fd、解码器、DE 图层都是有状态资源，必须单线程持有（`:21` 的老注释依然成立）；
 *    UI 只能通过 `capture_request_format()` 的请求队列登记，会话在主循环里执行。
 */
#ifndef _CAPTURE_SESSION_H_
#define _CAPTURE_SESSION_H_

/** @brief 采集会话。**只能被采集线程使用**（句柄有状态，全程无锁）。 */
class CaptureSession {
public:
    /** @brief ① 等设备出现。每 2 秒重试一次（"先启动程序、后插摄像头"也能起）。
     *  @param running 主线程的退出标志；为 false 时立即返回 false。
     *  @return false = 等待期间被要求退出（此时不需要任何收尾）。 */
    bool waitForDevice(volatile const bool &running);

    /** @brief ②③④ 格式协商 → 建出图管线 → 开流 → 下发控制项。
     *  @return false = 致命失败（协商后仍无尺寸 / 管线建不起来 / 开不了流）。
     *          此时内部已把 fd 关掉，调用方直接结束线程即可，无需 shutdown()。 */
    bool start();

    /** @brief ⑤ 主循环：处理换格式请求 → 取帧 → 门控 → 送管线 → 重连。
     *  @param running 主线程的退出标志；循环的每个阻塞点之后都会检查它。
     *  直到它变 false 才返回。 */
    void run(volatile const bool &running);

    /** @brief ⑥ 收尾：关管线（含视频层）→ 停流 → 关设备。 */
    void shutdown();

private:
    /* 私有方法的完整契约（顺序硬约束 / 失败语义）写在 capture_session.cpp 定义处，
     * 这里只留一行职责，完整契约写在 .cpp 定义处。 */

    /** @brief 换分辨率/帧率：停流 → S_FMT → 读回写配置 → pipeline_reconfigure → 开流 → 下发控制。
     *  @param nw,nh,nf UI 选择的**期望值**；实际生效尺寸以 capture_query_format
     *         读回为准（会写进 m_srcW/m_srcH，并回写配置）—— 与 pipeline_reconfigure
     *         要"实际值"正好相反：这里收期望，那里收实际。 */
    int  switchFormat(unsigned nw, unsigned nh, unsigned nf);

    /** @brief 断线重连：stop → close → 等 1s → init → 重协商 → start → 下发控制。
     *  @note poll 超时（-2）也走这里 —— 有意为之，依据见实现处。 */
    void reconnect();

    int      m_fd   = -1;   ///< 设备 fd；-1 = 未打开（重连失败后也会置回）
    unsigned m_srcW = 0;    ///< 当前生效源宽（协商/换格式后写回；设置页显示的就是它）
    unsigned m_srcH = 0;    ///< 当前生效源高
};

#endif /* _CAPTURE_SESSION_H_ */
