// SPDX-License-Identifier: MIT
/** @file RecordController.h
 *  @brief 录制状态控制（落盘尚未实现）
 */
#ifndef RECORD_CONTROLLER_H
#define RECORD_CONTROLLER_H

#include <QObject>

/* 录制状态：只有两个值，和"在哪一页"完全无关 */
enum class RecState {
    Idle,        /* 未录像 */
    Recording    /* 录像中 */
};

/* =============================================================================
 *  RecordController —— 录制业务的控制器
 * =============================================================================
 *  【为什么从状态机里搬出来】
 *    录制不改变页面，它跟"在哪一页"完全无关（录像中可以进设置页）。
 *    把它塞进 AppState 会让状态机管两件不相干的事。
 *
 *    更重要的是：录制将来会变复杂 —— 分段、循环覆盖、fsync 收尾、
 *    磁盘满判断、写入失败上报。这些都不属于"页面状态机"。
 *
 *  【现在它做什么】
 *    只是一个状态开关：点一下在 Idle / Recording 之间翻转。
 *    录像文件的实际读写由 media/ 里的写盘线程负责（还没写）。
 *
 *  【将来往哪长】
 *    · toggle() 里加条件：磁盘满、正在收尾 → 拒绝
 *    · 状态从 2 值扩到 4 值：Idle / Recording / Finishing / Error
 *    · 和 AVI 写入模块对接
 * ========================================================================== */
class RecordController : public QObject
{
    Q_OBJECT

public:
    /** @brief 构造录制控制器。 @param parent Qt 父对象。 */
    explicit RecordController(QObject *parent = nullptr);

    /** @brief 当前录制状态。 */
    RecState state()     const { return m_state; }

    /** @brief 是否正在录制。 @return true = 正在录。 */
    bool     recording() const { return m_state == RecState::Recording; }

public slots:
    /** @brief 点一下底栏「录制」：在开始 / 停止之间切换。
     *
     *  @note 由 main.cpp 接 BottomBar::recordClicked。
     *  @note 状态切换后发 stateChanged，界面从那条信号回写 —— 自己不直接改 UI。
     */
    void toggle();

signals:
    /** @brief 录制状态变了。
     *  @param now 新状态。
     *  @note 界面（顶栏 ● REC、底栏按钮文字 + 高亮）从这条信号回写，
     *        保证"录制中"只有 RecState 一个真相源。
     */
    void stateChanged(RecState now);

private:
    RecState m_state = RecState::Idle;   ///< 当前状态；初始 Idle
};

#endif /* RECORD_CONTROLLER_H */
