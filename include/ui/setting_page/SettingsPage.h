// SPDX-License-Identifier: MIT
/** @file SettingsPage.h
 *  @brief 系统设置页
 */
#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include "AEPage.h"
#include "AEButton.h"
#include "ToggleSwitch.h"
#include "page_object.h"

#include <QLabel>

/* =============================================================================
 *  SettingsPage —— 设置页
 * =============================================================================
 *  两重身份：
 *    · AEPage      —— 提供"铺满整屏 + 背景不透明 + 像素风边框"
 *    · PageObject  —— 提供 onEnter / onLeave，让控制器不用知道设置页的细节
 *
 *  【这里现在只剩两件事】
 *    上电自动开始录像 / 卡满时覆盖最旧的片段
 *
 *  【摄像头相关的设置全部搬到 CameraPage 了】（2026-10-29）
 *    格式 / 分辨率 / 帧率 / 白平衡 / 抗工频闪烁 原来在这里（前三项），
 *    用户要求「把设置主页面的摄像头相关的设置删除掉」，只留一个「摄像头设置」
 *    入口按钮进到独立的一页。好处是设置页不再随摄像头能力表变化而改版式，
 *    也不会出现"这里改一半、那里改一半"的两套保存路径。
 *
 *    那一页是【平级的页面】（Page::CameraSettings），由页面状态机管，
 *    本页只负责发 cameraRequested()，既不知道它在哪，也不负责它的显隐。
 *
 *  【状态不在这里存副本】
 *    开关的真相是控件的 isChecked()，持久化的真相是 AppSettings，
 *    两者靠信号单向同步，不另存 bool。
 * ========================================================================== */
class SettingsPage : public AEPage, public PageObject
{
    Q_OBJECT

public:
    /** @brief 构造系统设置页。 @param parent 父控件（通常 nullptr）。 */
    explicit SettingsPage(QWidget *parent = nullptr);

    /** @brief 析构。 */
    ~SettingsPage() override;

    /* ---- PageObject ---- */
    /// 进场：显示面板并**同步**重绘，盖住视频层
    void onEnter() override;
    /// 退场：藏起面板
    void onLeave() override;

signals:
    /** @brief 用户点了「返回」。 */
    void backRequested();

    /** @brief 用户点了「摄像头设置」。
     *  @note 只上报意图，切页由状态机
     *        （Settings --ClickCamera--> CameraSettings）决定。
     */
    void cameraRequested();

private slots:
    /// 「摄像头设置」按钮的处理：只发信号，不自己切页
    void onOpenCamera();

private:
    /// 建一行标签（纵向居中）。控件高度随类型不同，这里只管居中。
    QLabel *makeLabel(int rowY, const QString &text);

    /** @brief 「保存」：把两个开关的当前状态写进配置，并给用户明确反馈。
     *
     *  @note **幂等** —— 开关是"点亮即写盘"（toggled → setBool），
     *        所以这个按钮实质是再提交一遍。
     *  @warning 这两个键（`record/autoStart` / `record/loopOverwrite`）**目前没有任何消费者**：
     *           录像模块还没落地，全工程只有本页读写它们。所以"保存成功"只代表
     *           **配置写进了 `/etc/dvr/settings.ini`**，不代表录像行为会变。
     *           页面上那行说明文字就是为此加的，别删。等录像接通后再去掉。
     *  @note `AppSettings::setBool()` 没有返回值，"落盘失败"这里探测不到；
     *        提示语只能说"已保存"，不能说"已生效"。
     */
    void onSave();

    /** @brief 让「保存」按钮短暂显示一段文字，800ms 后恢复。
     *  @param text 要显示的结果文字（如「已保存」）。
     *  @note 抄自 `CameraPage::flashSaved()`，两页保持同一套反馈方式。
     */
    void flashSaved(const QString &text);

    AEButton   *m_btnBack   = nullptr;   ///< 「返回」
    AEButton   *m_btnCamera = nullptr;   ///< 「摄像头设置」入口（只发信号）
    AEButton   *m_btnSave   = nullptr;   ///< 「保存」

    ToggleSwitch *m_swAutoStart      = nullptr;   ///< 「上电自动开始录像」开关
    QLabel       *m_lblAutoStart     = nullptr;   ///< 该行标签

    ToggleSwitch *m_swLoopOverwrite  = nullptr;   ///< 「卡满时覆盖最旧的片段」开关
    QLabel       *m_lblLoopOverwrite = nullptr;   ///< 该行标签

    QLabel       *m_lblNote          = nullptr;   ///< 页面中部那行说明（"两项为预留"）
};

#endif /* SETTINGSPAGE_H */
