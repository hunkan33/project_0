// SPDX-License-Identifier: MIT
/** @file BottomBar.h
 *  @brief 底栏四个功能按钮
 */
#ifndef BOTTOMBAR_H
#define BOTTOMBAR_H

#include <QWidget>
#include <QPushButton>
#include "Theme.h"
#include "AEButton.h"

class BottomBar : public QWidget
{
    Q_OBJECT

public:
    /** @brief 构造底栏（四个按钮 + 面板底色）。 @param parent 父控件。 */
    explicit BottomBar(QWidget *parent = nullptr);

    /** @brief 析构。 */
    ~BottomBar();

signals:
    /** @brief 「录制」被点击。
     *
     *  @note 只上报"被点了"，**不携带状态**。状态由 AppState 决定，
     *        再通过 setRecording() 回写 —— 这样"录制中"全程序只有一个真相源。
     */
    void recordClicked();

    /** @brief 「拍照」被点击。**【预留】目前没有接收方**
     *
     *  @note 按钮已经画在底栏上、信号也接好了，保留是为了让这个按钮
     *        "有反应"的接线不丢，不是死代码。
     */
    void snapshotClicked();

    /** @brief 「存储」被点击。已接线：AppController 收下后切到 ClickStorage 页。
     *
     *  @note 2026-10-31 从 `playbackClicked` 改名 —— 底栏那个位置放的是
     *        「存储」不是「回放」，信号名要和按钮语义一致，否则接线时
     *        会出现"连了 playbackClicked 却在处理存储页"的误读。
     */
    void storageClicked();

    /** @brief 「设置」被点击。 */
    void settingsClicked();

public slots:
    /** @brief 回写录制状态：更新按钮文字与高亮。
     *  @param on true = 录制中（按钮变「停止」+ 高亮）。
     *  @note 由 AppState 的录制状态变化驱动，不自己改状态。
     */
    void setRecording(bool on);

private slots:
    /// 「录制」按钮的内部处理（转发成 recordClicked）
    void onRecordClicked();

private:
//四个按钮（左→右）：录制 / 拍照 / 存储 / 设置
    AEButton *m_btnRecord   = nullptr;   ///< 录制 / 停止
    AEButton *m_btnPhoto    = nullptr;   ///< 拍照（信号已发，**暂无接收方**——见 snapshotClicked）
    AEButton *m_btnStorage  = nullptr;   ///< 存储（已接线：storageClicked → AppController → Storage 页）
    AEButton *m_btnSettings = nullptr;   ///< 设置

    // 注意：原来的 bool m_recording 已删除 ——
    // 它是第二个真相源，会和 AppState 里的录制状态不同步。
};

#endif // BOTTOMBAR_H
