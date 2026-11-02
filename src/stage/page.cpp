// SPDX-License-Identifier: MIT
/** @file page.cpp
 *  @brief 页面布局表（crop / 视频区 / 顶底栏坐标）
 */
#include "page.h"

/* =============================================================================
 *  各页面的显示参数 —— 整个工程唯一的坐标定义处（现 5 张：Main / Settings / CameraSettings / Storage / 预留）
 * =============================================================================
 *  【下面的 crop 数值是 720p 基准，不是"实际采集分辨率"】
 *    采集分辨率是**运行时协商**的（可能 640×360 / 1280×960 / 1920×1080 / 2560×1440…），
 *    这里的 1280×720 只是写死这四张表时的参考尺寸。
 *    真正下发给 DE 的 crop 由 AppController::toDisplayLayout() 按
 *      「实际源尺寸 + 本页 winW×winH」重新算一遍，规则见那个函数。
 *    所以改这里不影响能跑哪些分辨率，只影响"这一页想要源图的哪一块比例"。
 *  DE 图层不做等比缩放，只做 crop → window 的直接映射，
 *  所以每一页的 cropW/cropH 和 winW/winH 必须同比例，否则画面变形。
 *
 *  字段顺序（结构体 PageLayout 的定义见 page.h）：
 *      { cropX, cropY, cropW, cropH,  winX, winY, winW, winH,  videoVisible }
 *        └── 从源图裁哪块 ──────┘   └── 铺到屏幕哪块 ──────┘   └ 要不要显示视频
 *
 *  【Main】纵向从 y=60 起裁 600 行（720-600，上下各去掉 60）
 *         比例 1280/600 = 2.133，1024/480 = 2.133  → 不变形
 *
 *  【Fullscreen】纵向用满 720，按 1024:600 = 1.7067 反推 cropW：
 *         cropW = 720 × 1.7067 ≈ 1228，左右各裁 (1280-1228)/2 = 26
 *         比例 1228/720 = 1.7056 vs 1024/600 = 1.7067，误差 0.06%（约 0.7 像素）
 *
 *  【Settings】画面被 Qt 的全屏面板完全遮住，videoVisible = false。
 *             坐标仍填 Main 的，只作兜底 —— 隐藏时根本不会被下发。
 * ========================================================================== */
static const PageLayout kMain       = {   0, 60, 1280, 600,   0, 48, 1024, 480, true  };  /* 顶栏48 + 视频480 + 底栏72 */
static const PageLayout kFullscreen = {  26,  0, 1228, 720,   0,  0, 1024, 600, true  };  /* 视频铺满整屏 600 */
static const PageLayout kSettings   = {   0, 60, 1280, 600,   0, 48, 1024, 480, false };  /* Qt 面板全屏遮挡，视频不显示 */
static const PageLayout kCamera     = {   0, 60, 1280, 600,   0, 48, 1024, 480, false };  /* 同上：全屏面板，视频隐藏 */
static const PageLayout kStorage    = {   0, 60, 1280, 600,   0, 48, 1024, 480, false };  /* 同上：全屏面板，视频隐藏 */

const char *pageName(Page p)
{
    switch (p) {
    case Page::Main:       return "Main";
    case Page::Fullscreen: return "Fullscreen";
    case Page::Settings:   return "Settings";
    case Page::CameraSettings: return "CameraSettings";
    case Page::Storage:    return "Storage";
    }
    return "?";
}

const char *eventName(Event e)
{
    switch (e) {
    case Event::ClickSettings:   return "ClickSettings";
    case Event::ClickCamera:     return "ClickCamera";
    case Event::ClickBack:       return "ClickBack";
    case Event::ClickFullscreen: return "ClickFullscreen";
    case Event::ClickStorage:    return "ClickStorage";
    }
    return "?";
}

const PageLayout &pageLayout(Page p)
{
    switch (p) {
    case Page::Main:       return kMain;
    case Page::Fullscreen: return kFullscreen;
    case Page::Settings:   return kSettings;
    case Page::CameraSettings: return kCamera;
    case Page::Storage:    return kStorage;
    }
    return kMain;      /* 越界兜底：用 Main 的参数，不至于崩 */
}
