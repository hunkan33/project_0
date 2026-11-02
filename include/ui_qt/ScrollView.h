// SPDX-License-Identifier: MIT
/** @file ScrollView.h
 *  @brief 可上下拖动的视口（自绘，非 QScrollArea）
 */
#ifndef SCROLLVIEW_H
#define SCROLLVIEW_H

#include <QWidget>

#include <functional>

/* =============================================================================
 *  ScrollView —— 可上下拖动的视口（自绘，不是 QScrollArea）
 * =============================================================================
 *  【它解决什么问题】
 *    一页内容比可用区域高的时候，需要"窗口 + 内容板"两层：
 *      · 本控件 = 窗口，尺寸固定，超出的部分被裁掉
 *      · body   = 内容板，比窗口高，装在窗口里上下移动
 *
 *    滚动 = 把 body 往上挪 offset 像素（内部就一句 m_body->move(0, -offset)）。
 *    没有离屏缓冲、没有动画、没有滚动条。
 *
 *  【为什么不用 QScrollArea / QScroller】
 *    ① 本工程是 linuxfb + 触摸/鼠标单击，没有滚轮手势体系，QScrollArea 那套用不上；
 *    ② QScrollArea 会画自己的 viewport 底色和滚动条，和 AEPage 的凸凹面板对不上；
 *    ③ 真正需要的行为就三件：跟手拖、松手对齐、右侧一根指示条 ——
 *       自己写比把 QScrollArea 调到"看起来一样"短得多。
 *
 *  【拖动从哪里开始】
 *    QLabel 默认不吃鼠标事件 → 事件冒泡到本控件，所以拖标签、拖空白都能滚。
 *    下拉框/开关这类控件自己要吃点击（accept），在它们身上按下不会拖页 ——
 *    这是对的：点到控件就是点控件，别把用户的手势解释成滚动。
 *
 *  【rowStep：松手吸附】
 *    > 0 时，松手会把 offset 对齐到 rowStep 的整数倍，停下来永远是"整行"，
 *      不会出现顶部/底部半行。
 *    = 0 时不吸附，可以停在任意位置（给非列表类内容用）。
 *
 *    ★ 吸附能成立有个前提：内容高度 − 视口高度 必须是 rowStep 的整数倍，
 *      否则最大 offset 对不齐，最后一行会露不全。
 *      这个前提由调用方保证，本控件不检查（它也检查不了：body 高度是外部设的）。
 *      CameraPage 里 kBodyH / kViewH / kRowStep 三个常量的注释写的就是这件事。
 * ========================================================================== */
class ScrollView : public QWidget
{
public:
    /** @brief 构造视口。
     *  @param body    内容板（会立刻被 setParent(this) 收编，不接管拥有权）。
     *  @param parent  父控件。
     *  @param rowStep 松手吸附步长；0 = 不吸附（连续滚动）。含义见文件头。
     */
    ScrollView(QWidget *body, QWidget *parent, int rowStep = 0);

    /** @brief 内容比视口高多少。
     *  @return 最大滚动量；<= 0 表示不需要滚动。
     */
    int maxOffset() const;

    /** @brief 当前滚动量。 @return 0 = 顶部。 */
    int offset() const;

    /** @brief 设置滚动量。
     *  @param v 目标滚动量；内部自动夹到 [0, maxOffset()]，越界不会拖出空白。
     */
    void setOffset(int v);

    /** @brief 「真正开始拖动」时的回调。
     *
     *  @note 位移超过阈值（6px）才触发，**不是一按下就触发** ——
     *        轻点一下页面不应该被当成拖动。
     *  @note CameraPage 用它收起展开的下拉框：弹层坐标是相对页面的，
     *        页面一动它还留在原地就成了"飘着的框"。
     */
    std::function<void()> onDragStart;

protected:
    /// 铺面板底色 + 内容超出一屏时画右侧 4px 指示条
    void paintEvent(QPaintEvent *e) override;
    /// 记下按下位置与当时的 offset
    void mousePressEvent(QMouseEvent *e) override;
    /// 跟手拖（只认从本控件按下的拖动，详见 .cpp）
    void mouseMoveEvent(QMouseEvent *e) override;
    /// 松手吸附到 rowStep 的整数倍
    void mouseReleaseEvent(QMouseEvent *e) override;
    /// 滚轮（触摸屏没有，接鼠标调试时用）
    void wheelEvent(QWheelEvent *e) override;

private:
    QWidget *m_body    = nullptr;   ///< 内容板（本控件的子控件，随 offset 上下移动）
    int      m_rowStep = 0;         ///< 吸附步长；0 = 不吸附
    int      m_offset  = 0;         ///< 当前滚动量

    int      m_pressY  = 0;      /* 按下时的 y */
    int      m_start   = 0;      /* 按下时的 offset */
    bool     m_drag    = false;  /* 位移超过阈值，真的在拖了 */
    bool     m_pressed = false;  /* 这次按下落在本控件上（不是子控件） */
};

#endif /* SCROLLVIEW_H */
