// SPDX-License-Identifier: MIT
/** @file AEButton.h
 *  @brief AE 风格凸起按钮
 */
#ifndef AEBUTTON_H
#define AEBUTTON_H

#include <QPushButton>

/*
 * AEButton —— AE 风格凸凹按钮（全自绘）
 * =============================================================================
 *  为什么不用 QSS：
 *      QSS 的 background 渐变始终是"近似"——浮点取整会导致边框附近
 *      出现 1px 的漏色（白线），而且 border 只有一层，做不出
 *      "外框 + 上方灰边" 的两层结构。
 *      像素风必须 1px 就是 1px，只有 paintEvent 能保证。
 *
 *  绘制结构（和截图完全对应）：
 *      ┌──────────────────────────┐
 *      │ ① 外框     #413F54  2px  │
 *      │ ③ 上方灰边 #ADB0C4  2px  │
 *      │                          │
 *      │ ④ 填充     #9A9FB4 主体  │
 *      │                          │
 *      │ ② 底部阴影 #413F54  8px  │
 *      └──────────────────────────┘
 *         ⑤ 文字     #FFFFFF
 *
 *  按下 / 选中时，③④② 换成浅蓝系，① 外框不变。
 *
 *  用法：
 *      AEButton *btn = new AEButton("录制", parent);
 *      btn->setGeometry(...);
 *
 *      // 需要"点一下保持高亮"（如录制中）：
 *      btn->setCheckable(true);
 *      btn->setToggleMode(true);
 */
class AEButton : public QPushButton
{
    Q_OBJECT

public:
    /** @brief 构造一个带文字的按钮。
     *  @param text   按钮文字（会按当前字体居中绘制）。
     *  @param parent 父控件。
     */
    explicit AEButton(const QString &text, QWidget *parent = nullptr);

    /** @brief 构造一个无文字按钮（只画外观，用于叠图标）。
     *  @param parent 父控件。
     */
    explicit AEButton(QWidget *parent = nullptr);

    /** @brief 开启/关闭"切换模式"。
     *
     *  @param on @c false（默认）只有按住时变蓝，松手恢复；
     *            @c true 在 checked 时**一直**保持蓝色（适合"录制中"这类状态）。
     *
     *  @warning toggle 模式必须同时 setCheckable(true)，
     *           否则 isChecked() 恒为 false，按钮永远不变蓝。
     */
    void setToggleMode(bool on) { m_toggleMode = on; update(); }

    /** @brief 当前是否处于切换模式。 */
    bool toggleMode() const { return m_toggleMode; }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    bool m_toggleMode = false;
};

#endif // AEBUTTON_H
