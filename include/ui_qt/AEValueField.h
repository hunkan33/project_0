// SPDX-License-Identifier: MIT
/** @file AEValueField.h
 *  @brief 只读数值框
 */
#ifndef AEVALUEFIELD_H
#define AEVALUEFIELD_H

#include <QWidget>

/* =============================================================================
 *  AEValueField —— 只读数值框（AE 风格）
 * =============================================================================
 *  【它是什么】
 *    一个**显示数值、不能交互**的框。用来放在「数值 + 修改按钮」这一对里的左半边。
 *
 *  【为什么不用 QLabel 直接显示】
 *    QLabel 是一行字，看不出"这是一格字段"。做成带外框的框，
 *    视觉上和右边的「修改」按钮构成"字段 + 操作"，一眼知道左边是值、右边能改。
 *
 *  【为什么画得比 AEButton 扁平】
 *    它不可点 —— 所以画法刻意和按钮区分开：
 *      本控件：外框 + 一圈内亮边 + 纯色填充，**没有底部阴影** → 像输入框
 *      AEButton：外框 + U 形边 + 填充 + 底部亮线 + 底部阴影 → 像凸起按钮
 *    如果它长得和按钮一样，用户会去点它，然后发现没反应。
 *
 *  【和 AEComboBox 的关系】
 *    收起态的 AEComboBox 也是这个画法，但它有箭头、能点开。
 *    这里刻意不复用 —— 复用会带出"看起来能点"的暗示。
 * ========================================================================== */
class AEValueField : public QWidget
{
    Q_OBJECT

public:
    /** @brief 构造一个只读数值框。 @param parent 父控件。 */
    explicit AEValueField(QWidget *parent = nullptr);

    /** @brief 设置要显示的值（内部转成字符串）。
     *  @param v 新值。
     */
    void setValue(int v);

    /** @brief 取当前显示的值。
     *  @return 显示的整数；显示的是占位文案时返回 **0**。
     *
     *  @note 有了它，"页面上显示的数"和"弹窗里的初值"才能保证是同一个数
     *        （否则要么从控件反读文本、要么另存一份，都会出现两个真相源）。
     */
    int value() const;

    /** @brief 设置占位文案（没有可用值时显示，如「—」或「定焦」）。
     *  @param text 占位文本。
     */
    void setPlaceholder(const QString &text);

    /** @brief 设置可用性（只影响观感，控件本来就不可交互）。
     *  @param on @c false 时文字用 DisabledText 色、底色压暗一档。
     */
    void setUsable(bool on);

protected:
    /// 画外框 + 一圈亮边 + 纯色填充 + 居中文字（比 AEButton 扁平，没有底部阴影）
    void paintEvent(QPaintEvent *e) override;

private:
    QString m_text    = QStringLiteral("-");   ///< 当前显示的文字
    bool    m_usable  = true;                  ///< 是否可用（决定配色）
};

#endif /* AEVALUEFIELD_H */
