// SPDX-License-Identifier: MIT
/** @file app_controller.h
 *  @brief 页面切换 + 显示层联动
 */
#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <QObject>
#include <QMap>
#include "page.h"

class AppState;
class MainUi;
class SettingsPage;
class CameraPage;
class StoragePage;
class RecordController;
class PageObject;
class QTimer;
struct DisplayLayout;

/* =============================================================================
 *  AppController —— 页面调度的唯一入口
 * =============================================================================
 *  【它做什么】
 *    1. 接线：UI 事件 → AppState（只接页面事件）
 *    2. 调度：页面变了，按顺序调对应页面对象的 onEnter / onLeave，
 *       并处理"全局显示资源"（视频层开关与布局、清 fb0）
 *
 *  【加新页面】
 *    写一个 PageObject 实现 → 接线。**注意：insert 一行还不够**，
 *    实际要接 8 处（按钮信号、页面对象、状态机表、布局……详见 AGENT.md P6.1）。
 *    applyPage() 一行都不用改。
 * ========================================================================== */
class AppController : public QObject
{
    Q_OBJECT

public:
    /** @brief 构造控制器并完成接线（不显示任何窗口）。
     *
     *  @param state    页面状态机。
     *  @param mainUi   顶栏 + 底栏。
     *  @param settings 设置页。
     *  @param camera   摄像头设置页。
     *  @param storage  存储页。
     *  @param recorder 录制控制器。底栏「录制」→ 它 → 界面回写的接线在本构造函数里完成
     *                  —— main.cpp 只做装配，不该知道"哪个按钮连到哪个业务"。
     *                  录制与页面正交（录像中可以进设置页），但接线归编排层。
     *  @param parent   Qt 父对象。
     */
    AppController(AppState *state, MainUi *mainUi, SettingsPage *settings,
                  CameraPage *camera, StoragePage *storage,
                  RecordController *recorder, QObject *parent = nullptr);

    /** @brief 启动：首次显示窗口 + 250ms 后做一次完整布局。
     *
     *  @warning 必须在 QApplication::exec() **之前**调用 —— 否则定时器不会跑，
     *           界面永远停在"窗口显示了但没布局"的状态。
     *  @note 为什么延 250ms：等 Qt 把首帧画完再算布局，否则算出来的脏区不可靠。
     */
    void start();

private slots:
    /** @brief 页面已经变完，按新页面调度（只接这一个信号）。
     *
     *  @param from 原页面。
     *  @param to   新页面。
     */
    void onPageChanged(Page from, Page to);

    /** @brief 源尺寸就绪后，把挂起的页面调度补上。
     *
     *  @note 采集线程跑完 capture_query_format + display_init 之后触发；
     *        bootApply 是 250ms 定时器，两者先后没保证，所以要有这个补偿路径。
     */
    void onSourceReady();

private:
    /// 按页面调度（新页面进场 → 视频层 → 旧页面退场 → 清 fb0）
    void applyPage(Page p);

    /// 开机首次布局（250ms 定时器触发，只跑一次）
    void bootApply();

    AppState     *m_state    = nullptr;   ///< 页面状态机（不持有）
    MainUi       *m_mainUi   = nullptr;   ///< 顶栏 + 底栏（不持有）
    SettingsPage *m_settings = nullptr;   ///< 设置页（不持有）
    CameraPage   *m_camera   = nullptr;   ///< 摄像头设置页（不持有）
    StoragePage  *m_storage  = nullptr;   ///< 存储页（不持有）
    RecordController *m_recorder = nullptr;  ///< 录制控制器（不持有；接线见构造函数）

    // ---- 页面对象表 ----
    // 加新页面：这里 insert 一行只是第一步，applyPage() 通常还要加分支（8 处接线见 AGENT.md P6.1）。
    // 没注册的页面视为"没有自己的窗口"，控制器只按 PageLayout 处理顶栏底栏和视频层。
    //
    // 用 QMap 而不是 QHash：QHash 要求 key 有 qHash()，enum class 没有；
    // QMap 只需要 operator<，枚举天然支持。
    QMap<Page, PageObject *> m_pageObjects;   ///< Page → 该页的窗口对象
    PageObject *m_currentPage = nullptr;      ///< 当前正在显示的页对象；nullptr = 无

    // ---- 等待源尺寸就绪 ----
    // 页面调度要算 crop，而 crop 得按实际采集分辨率等比换算。实际分辨率要等
    // 采集线程跑完 capture_query_format + display_init 才知道，而 bootApply 是
    // 250ms 定时器，两者先后没保证 —— 所以没就绪时先把页面记下来，轮询到了再补。
    QTimer *m_waitSrc     = nullptr;            ///< 200ms 轮询源尺寸是否就绪
    Page    m_pendingPage = Page::Main;         ///< 等待期间暂存的页面
    bool    m_hasPending  = false;              ///< 是否有挂起的页面调度

    int     m_appliedSrcW = 0;   ///< 上次下发到 DE 的源宽；和 display_src_w() 不一致说明换了分辨率
    int     m_appliedSrcH = 0;   ///< 上次下发到 DE 的源高

    // 为什么要有这个闸：m_waitSrc 是 200ms，比 bootApply 的 250ms 早，
    // 不加闸的话启动时会连续 apply 两次（日志里两遍"布局完成"）。
    bool    m_booted = false;     ///< bootApply 是否已经跑过
};

#endif /* APP_CONTROLLER_H */
