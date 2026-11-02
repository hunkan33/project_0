// SPDX-License-Identifier: MIT
/** @file app_state.h
 *  @brief 页面状态机（只管「现在在哪一页」）
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <QObject>
#include "page.h"

/* =============================================================================
 *  AppState —— 页面状态机（转移表驱动）
 * =============================================================================
 *  【它只管页面】
 *    Page —— 页面，互斥多选 1（现 5 个），走转移表（app_state.cpp 的 kPageTable）
 *
 *  【它不管什么】
 *    录制、拍照这类业务动作不走这里 —— 它们各有自己的 Controller
 *    （录制见 recorder/RecordController.h；沿革：recorder→media→recorder），状态机只负责"现在在哪一页"。
 * ========================================================================== */
class AppState : public QObject
{
    Q_OBJECT

public:
    explicit AppState(QObject *parent = nullptr);

    /** @brief 当前页。 */
    Page page() const { return m_page; }

    /** @brief 唯一的事件入口：喂一个事件，走转移表。
     *
     *  @param e 要处理的事件。
     *  @return @c true = 状态真的变了（pageChanged 已发）；
     *          @c false = 表里没有这条事件，或本来就在这个状态。
     *
     *  @note 外部**只能**通过这里改页面，不允许直接指定目标页。
     */
    bool handleEvent(Event e);

signals:
    /** @brief 页面已经变完 —— 状态机只发这一个信号。
     *
     *  @param from 原页面。
     *  @param to   新页面。
     *
     *  @note 切页时 show/hide 的顺序由 AppController::applyPage() 内部四步保证
     *        （新页面 onEnter → 视频层 → 旧页面 onLeave → 清 fb0），
     *        不靠信号先后，所以一个信号就够。
     */
    void pageChanged(Page from, Page to);

private:
    /** @brief 真正执行切页。
     *  @param to 目标页。
     *  @param e  触发它的事件（用于日志）。
     *  @return 是否真的切了。
     *  @note private：外部只能走 handleEvent 的转移表，不能绕规则直接指定目标页。
     */
    bool gotoPage(Page to, Event e);

    Page m_page = Page::Main;   ///< 当前页
};

#endif /* APP_STATE_H */
