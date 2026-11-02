// SPDX-License-Identifier: MIT
/** @file MainUi.h
 *  @brief 顶栏 + 底栏的容器
 */
#ifndef MAINUI_H
#define MAINUI_H

#include "UpWindow.h"
#include "BottomBar.h"
#include "page_object.h"

/* =============================================================================
 *  MainUi —— 主界面的常驻 UI（顶栏 + 底栏），同时是主界面这个"页面"
 * =============================================================================
 *  【为什么不是一个窗口】
 *    顶栏(y=0~48)和底栏(y=528~600)之间那 480 行是视频区，必须露给 DE 视频层。
 *    linuxfb 不支持透明窗口，所以不能做成一个 1024×600 的容器 ——
 *    那样中间的 480 行会被容器涂成不透明，视频全被遮住。
 *
 *    所以内部仍是两个独立的顶层窗口，本类只是【统一管理对象】。
 *
 *  【两个身份】
 *    · UI 容器    —— 持有 UpWindow + BottomBar，提供成对的 show/hide/setRecording
 *    · PageObject —— 作为主界面这一页，实现 onEnter/onLeave
 *
 *    和 SettingsPage 一致：谁是一页，谁就实现 PageObject。
 *    这样 AppController 的注册表里全是 PageObject*，不用为主界面特殊处理。
 *
 *  【为什么不继承 QWidget】
 *    继承的话 up/bar 会变成它的子控件，被裁剪到父窗口范围里；
 *    而父窗口本身不该显示（1024×600 会遮视频）。所以它是普通 C++ 类，
 *    构造时同时构造两个顶层窗口，析构时自动一起销毁。
 * ========================================================================== */
class MainUi : public PageObject
{
public:
    MainUi() = default;

    /* ---- PageObject ---- */
    /// 进场：顶栏底栏亮出来
    void onEnter() override;
    /// 退场：顶栏底栏收起来
    void onLeave() override;

    /* ---- 成对操作 ---- */
    /// 显示顶栏 + 底栏
    void show();
    /// 隐藏顶栏 + 底栏
    void hide();
    /// 把顶栏底栏抬到最上层
    void raise();

    /** @brief 回写录制状态：顶栏变 ● REC，底栏按钮变文字 + 高亮。
     *  @param on true = 正在录；false = 就绪。
     */
    void setRecording(bool on);

    /* ---- 取内部窗口 ----
     * 仅供接线用（connect 底栏信号、调顶栏定时器）。
     * 日常的 show/hide/setRecording 请走上面的方法，别绕过。 */
    /// 取顶栏（仅供接线）
    UpWindow  &upWindow()  { return m_up; }
    /// 取底栏（仅供接线）
    BottomBar &bottomBar() { return m_bar; }

private:
    // 两个都是顶层窗口（parent = nullptr），MainUi 不接管它们的父窗口关系
    UpWindow  m_up;    ///< 顶栏（时间 / 录制状态 / 存储剩余）
    BottomBar m_bar;   ///< 底栏（录制 / 拍照 / 存储 / 设置）
};

#endif /* MAINUI_H */
