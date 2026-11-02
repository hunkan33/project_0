// SPDX-License-Identifier: MIT
/** @file ToggleSwitch.h
 *  @brief AE 风格开关
 */
#ifndef TOGGLESWITCH_H
#define TOGGLESWITCH_H

#include <QAbstractButton>

class QPainter;

/* =============================================================================
 *  ToggleSwitch —— AE 风格开关（全自绘，与 AEButton 同一套凸凹语言）
 * =============================================================================
 *  结构照 Minecraft/AE 配置界面的两格开关还原（色值几何由 ui-image-inspector
 *  从参考截图上量出来，见 09 计划书）：
 *
 *      OFF                                ON
 *      ┌──────────┬──────────┐           ┌──────────┬──────────┐
 *      │ ┌──────┐ │ ┌──────┐ │           │ ┌──────┐ │ ┌──────┐ │
 *      │ │ 凸起 │ │ │ 凹槽 │ │           │ │ 凹槽 │ │ │ 凸起 │ │
 *      │ │ 灰格 │ │ │ +方环│ │           │ │ +竖条│ │ │ 灰格 │ │
 *      │ └──────┘ │ └──────┘ │           │ └──────┘ │ └──────┘ │
 *      └──────────┴──────────┘           └──────────┴──────────┘
 *
 *  【三条规则】
 *    ① 永远一格「凸起灰格」+ 一格「凹槽」
 *    ② 凹槽永远在滑块所在的那一侧：OFF 在右，ON 在左
 *    ③ 凹槽底色 = OFF 时 #696D88 / ON 时 #9CD3FF
 *
 *  【为什么继承 QAbstractButton 而不是 QWidget】
 *    checkable、clicked / toggled 信号、按下态 isDown()、鼠标按下释放
 *    全部由基类处理，不用自己写状态机；而且和 AEButton（QPushButton
 *    → QAbstractButton）是同一个基类族，语义统一。
 *
 *  用法：
 *      ToggleSwitch *sw = new ToggleSwitch(this);
 *      sw->setGeometry(x, y, Theme::Toggle::Width, Theme::Toggle::Height);
 *      sw->setChecked(true);
 *      connect(sw, &QAbstractButton::toggled, ...);
 * ========================================================================== */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT

public:
    /** @brief 构造一个开关控件（默认 OFF）。 @param parent 父控件。 */
    explicit ToggleSwitch(QWidget *parent = nullptr);

    /** @brief 首选尺寸（宽度 = 两格 + 分隔，高度 = 一格）。 */
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    /// 画凸起灰格（3px 亮边 + 填充 + 亮边 + 6px 底部阴影；pressed 时换强调色）
    void drawRaised(QPainter &p, const QRect &cell, bool pressed) const;

    /// 画凹槽（纯色填充，无凸凹；on 决定强调色还是深槽色）
    void drawWell(QPainter &p, const QRect &well, bool on) const;

    /// 画滑块（ON = 浅色竖条；OFF = 带缺口的方环）
    void drawKnob(QPainter &p, const QRect &well, bool on, bool pressed) const;
};

#endif // TOGGLESWITCH_H
