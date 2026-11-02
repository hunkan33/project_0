// SPDX-License-Identifier: MIT
/** @file camera_service.cpp
 *  @brief camera_service.h 的实现 —— 全是转发，本层不持有状态
 */
#include "camera_service.h"

#include "camera_info.h"     /* cam_ini_load / CAMERA_INI_PATH / CAM_MAX_MODES */
#include "camera_ctl.h"      /* g_ctlItems / camera_ctl_query|get|set */
#include "capture.h"         /* capture_request_format */
#include "log.h"

#include <linux/videodev2.h> /* V4L2_CID_* / V4L2_CTRL_FLAG_INACTIVE */

#include <cstring>

namespace CameraService {

int modeMax() { return CAM_MAX_MODES; }

int modeLoad(ModeInfo *out, int max)
{
    CamMode raw[CAM_MAX_MODES];
    const int n = cam_ini_load(CAMERA_INI_PATH, nullptr, raw, CAM_MAX_MODES);
    const int c = (max < n) ? max : n;
    for (int i = 0; i < c; ++i) {
        std::memcpy(out[i].fourcc, raw[i].fourcc, sizeof(out[i].fourcc));
        out[i].width  = static_cast<int>(raw[i].width);
        out[i].height = static_cast<int>(raw[i].height);
        out[i].fps    = static_cast<int>(raw[i].fps);
    }
    return c;
}

int ctrlCount() { return g_ctlItemCount; }

const char *ctrlName(int i)
{
    return (i >= 0 && i < g_ctlItemCount) ? g_ctlItems[i].name : "";
}

const char *ctrlKey(int i)
{
    return (i >= 0 && i < g_ctlItemCount) ? g_ctlItems[i].key : "";
}

bool ctrlQuery(int i, CtrlRange *out)
{
    if (i < 0 || i >= g_ctlItemCount || !out)
        return false;

    unsigned flags = 0;
    if (camera_ctl_query(g_ctlItems[i].cid, &out->lo, &out->hi,
                         nullptr, nullptr, &flags) != 0)
        return false;
    out->inactive = (flags & V4L2_CTRL_FLAG_INACTIVE) != 0;
    return true;
}

bool ctrlGet(int i, int *val)
{
    return (i >= 0 && i < g_ctlItemCount)
        && camera_ctl_get(g_ctlItems[i].cid, val) == 0;
}

bool ctrlSet(int i, int val)
{
    return (i >= 0 && i < g_ctlItemCount)
        && camera_ctl_set(g_ctlItems[i].cid, val) == 0;
}

bool getWbAuto(int *v)    { return camera_ctl_get(V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO, v) == 0; }
bool setWbAuto(int v)     { return camera_ctl_set(V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO, v) == 0; }
bool getPowerLine(int *v) { return camera_ctl_get(V4L2_CID_POWER_LINE_FREQUENCY, v) == 0; }
bool setPowerLine(int v)  { return camera_ctl_set(V4L2_CID_POWER_LINE_FREQUENCY, v) == 0; }
bool getExposureAuto(int *v) { return camera_ctl_get(V4L2_CID_EXPOSURE_AUTO, v) == 0; }

bool requestFormat(int w, int h, int fps)
{
    return capture_request_format(static_cast<unsigned>(w),
                                  static_cast<unsigned>(h),
                                  fps) == 0;
}

} // namespace CameraService
