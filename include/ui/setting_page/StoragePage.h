// SPDX-License-Identifier: MIT
/** @file StoragePage.h
 *  @brief 存储页（底栏「存储」进入）—— 显示存储卡状态与容量
 *
 *  放在 ui/setting/ 下不是因为它叫"设置"，而是因为它和 SettingsPage /
 *  CameraPage 是**同一族**：底栏进入、全屏面板、视频层必须完全隐藏。
 *
 *  【现在有什么】
 *    卡在不在 / 总容量 / 可用 / 已用 / 挂载点 / 录像目录（标"未实现"）
 *  【还没有什么】
 *    录像文件列表、删除、格式化 —— 这些要等录制落盘做完（见 16 号文档）
 */
#ifndef STORAGEPAGE_H
#define STORAGEPAGE_H

#include <QLabel>

#include "AEPage.h"
#include "page_object.h"

class AEButton;

class StoragePage : public AEPage, public PageObject
{
    Q_OBJECT

public:
    explicit StoragePage(QWidget *parent = nullptr);

    /** @brief 重新读一遍存储卡状态并刷新数值。
     *  @note onEnter() 内部会调，外部一般不用直接调。
     */
    void refresh();

    /* ---- PageObject ---- */
    /// 进场：刷新数值 + 亮出来 + 同步刷屏（面板要盖住视频层）
    void onEnter() override;
    /// 退场：收起来
    void onLeave() override;

signals:
    /** @brief 用户点了「返回」。切页由状态机决定，本页不自己切。 */
    void backRequested();

private:
    /** @brief 建一行「标签 + 值」。
     *  @param y     该行纵向位置。
     *  @param label 标签文字（左侧，灰色）。
     *  @return 右侧值标签的指针，后续用 setText 更新。
     */
    QLabel *addRow(int y, const QString &label);

    AEButton *m_btnBack = nullptr;   ///< 「返回」

    QLabel *m_valState = nullptr;    ///< 存储卡状态（已安装 / 未插入）
    QLabel *m_valTotal = nullptr;    ///< 总容量
    QLabel *m_valAvail = nullptr;    ///< 可用容量
    QLabel *m_valUsed  = nullptr;    ///< 已用容量
    QLabel *m_valMount = nullptr;    ///< 挂载点
    QLabel *m_valDir   = nullptr;    ///< 录像目录
};

#endif /* STORAGEPAGE_H */
