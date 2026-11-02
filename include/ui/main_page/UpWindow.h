// SPDX-License-Identifier: MIT
/** @file UpWindow.h
 *  @brief 顶栏（时间 / 录制状态 / 存储剩余）
 */
#ifndef UPWINDOW_H
#define UPWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include "Theme.h"
#include "AEButton.h"
/** @brief 顶栏（1024 × 48，位置 0,0）—— 显示三块状态信息。
 *
 *  - 左侧：时间，每秒刷新
 *  - 中间：录制状态（就绪 / ● REC），由 setRecording() 控制
 *  - 右侧：存储卡剩余空间，每 5 秒刷新（storage.h），没插卡显示「无卡」
 *
 *  @note 本窗口 setStyleSheet(qssPanel())，所以内部 QLabel 必须用
 *        qssTextFlat() —— 否则会继承到面板边框，长出"突起"。
 */
class UpWindow : public QWidget
{
    Q_OBJECT

public:
    /** @brief 构造顶栏。 @param parent 父控件（作为顶层窗口时传 nullptr）。 */
    explicit UpWindow(QWidget *parent = nullptr);

    /** @brief 析构：停掉两个刷新定时器。 */
    ~UpWindow();

    /** @brief 切换录制状态。
     *  @param on @c true 显示 ● REC（红）；@c false 显示 就绪（灰）。
     *  @note 由底栏的录制按钮通过信号调用，顶栏自己不改状态。
     */
    void setRecording(bool on);

private:
// ---- 时间 状态 存储 ----

    QLabel *m_lblTime  = nullptr;   ///< 左侧：时间
    QLabel *m_lblRec   = nullptr;   ///< 中间：就绪 / ● REC
    QLabel *m_lblStore = nullptr;   ///< 右侧：存储卡剩余（或「无卡」）

    // ---- 定时器 ----
    QTimer *m_timerClock = nullptr;   ///< 时间刷新，1 秒
    QTimer *m_timerStore = nullptr;   ///< 存储刷新，5 秒

private:
    /// 刷时间标签（每秒）
    void updateTime();
    /// 刷存储标签（每 5 秒；没挂载显示「无卡」）
    void updateStorage();
};

#endif // UPWINDOW_H
