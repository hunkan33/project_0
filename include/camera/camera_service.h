// SPDX-License-Identifier: MIT
/** @file camera_service.h
 *  @brief 摄像头操作的**唯一门面** —— UI 层只准通过它碰摄像头
 *
 *  【为什么要有这一层】
 *    硬件实现都在 `platform/`，且 build.sh 有守卫：
 *
 *        **`src/ui/` 不许 include `platform/` 的任何头**
 *
 *    但 `CameraPage` 原来直接 include 了 `camera_info.h` / `camera_ctl.h` / `capture.h`，
 *    用到了 `g_ctlItems`、`CamMode`、`camera_ctl_query/get/set`、`capture_request_format`。
 *    要让那条规则能落地，就必须给 UI 一个不暴露平台类型的门面。
 *
 *  【设计取舍】
 *    · 控制项用**下标**（0..ctrlCount()-1）而不是 V4L2 的 CID —— UI 不需要知道 V4L2
 *    · 能力表用**值拷贝**（ModeInfo），不外泄 `CamMode` 结构
 *    · 本层**不持有状态**，全部转发给 platform/ —— 它是"翻译"，不是"管理者"
 *
 *  @note core/ 允许 include platform/（见 AGENT.md P3.2 依赖表），所以本文件
 *        可以直接用 camera_ctl / camera_info / capture 的头。
 */
#ifndef _CAMERA_SERVICE_H_
#define _CAMERA_SERVICE_H_

namespace CameraService {

constexpr int kModeMax = 64;

/** @brief 一条采集能力（值拷贝，不外泄 platform 的 CamMode） */
struct ModeInfo {
    char fourcc[5];     ///< "MJPG" / "YUYV"
    int  width;
    int  height;
    int  fps;           ///< 0 = 驱动对这个尺寸没报帧率
};

/** @brief 能力表容量上限（= platform 的 CAM_MAX_MODES）。 */
int  modeMax();

/** @brief 把 `/etc/dvr/camera.ini` 里的能力表拷成 UI 用的值。
 *  @param out 输出数组，调用方分配。
 *  @param max out 的容量（条目数）；超出部分**静默截断**。
 *  @return 实际条数（≤ max）。 */
int  modeLoad(ModeInfo *out, int max);

/** @brief 控制项条数（= platform 的 g_ctlItemCount）。 */
int  ctrlCount();

/** @brief 第 i 项的中文名（弹窗标题用）。
 *  @param i 控制项下标（0 .. ctrlCount()-1）。越界返回空串。 */
const char *ctrlName(int i);

/** @brief 第 i 项的配置键（"camera/brightness"）。
 *  @param i 控制项下标。越界返回空串。 */
const char *ctrlKey(int i);

/** @brief 查询范围与"此刻可调性"。
 *  @param i   控制项下标。
 *  @param out.lo/out.hi 设备报的范围；out.inactive = 设备标了 INACTIVE
 *  @return false = 设备不认识这一项 */
struct CtrlRange {
    int  lo, hi;
    bool inactive;
};
bool ctrlQuery(int i, CtrlRange *out);

/** @brief 读设备当前值。
 *  @param i   控制项下标。
 *  @param val 输出设备当前值；失败时不写。
 *  @return false = 读失败。 */
bool ctrlGet(int i, int *val);

/** @brief 写设备。
 *  @param i   控制项下标。
 *  @param val 要写入设备的值；应在 ctrlQuery 查到的 [lo, hi] 内，
 *             超范围设备会拒绝（EINVAL）。
 *  @return false = 写失败（越界/INACTIVE/设备拒绝）。 */
bool ctrlSet(int i, int val);

/** ---- 三个"固定"控制项：给语义化的名字，UI 不用碰 V4L2_CID_* ---- */
bool getWbAuto(int *v);          ///< 白平衡自动（1=自动）
bool setWbAuto(int v);
bool getPowerLine(int *v);       ///< 抗工频闪烁（0 关 / 1 50Hz / 2 60Hz）
bool setPowerLine(int v);
bool getExposureAuto(int *v);    ///< 曝光模式（1=手动）

/** @brief 登记换流请求（采集线程异步执行）。
 *  @param w,h,fps UI 上选择的**期望值** —— 不是最终生效尺寸：
 *         S_FMT 是协商不是命令，驱动可能给最接近的；实际生效值由采集线程
 *         读回并写回配置（所以这里收"期望"，pipeline_reconfigure 收"实际"）。
 *  @return false = 请求队列满或参数非法。 */
bool requestFormat(int w, int h, int fps);

} // namespace CameraService

#endif /* _CAMERA_SERVICE_H_ */
