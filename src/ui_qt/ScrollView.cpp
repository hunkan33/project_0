// SPDX-License-Identifier: MIT
/** @file ScrollView.cpp
 *  @brief ScrollView 实现
 */
#include "ScrollView.h"

#include "Theme.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>

namespace {

/* 位移超过多少像素才算"拖动"。
 * 太小（比如 1px）会让轻点一下页面就跳一行；太大又显得拖不动。6 是试出来的：
 * 手指/鼠标按住时的抖动一般在 2~3px 以内，6px 能稳定区分"点"和"拖"。 */
constexpr int kDragThreshold = 6;

/* 右侧指示条：条宽、最小条高、距右边缘的距离 */
constexpr int kBarW      = 4;
constexpr int kBarMinH   = 40;   /* 内容再长，条也不会细到看不见/点不到 */
constexpr int kBarMargin = 7;

} // namespace

ScrollView::ScrollView(QWidget *body, QWidget *parent, int rowStep)
    : QWidget(parent), m_body(body), m_rowStep(rowStep)
{
    if (m_body)
        m_body->setParent(this);

    /* 不用系统底色：paintEvent 自己铺，和 AEPage 用的是同一块面板色 */
    setAttribute(Qt::WA_NoSystemBackground);
}

int ScrollView::maxOffset() const
{
    if (!m_body)
        return 0;
    return qMax(0, m_body->height() - height());
}

int ScrollView::offset() const
{
    return m_offset;
}

void ScrollView::setOffset(int v)
{
    /* 夹到合法范围。拖过头就停在边界，不做出界回弹 ——
     * 这块屏没有惯性滚动的手感预期，回弹反而会让人觉得"拖不住"。 */
    v = qBound(0, v, maxOffset());
    if (v == m_offset)
        return;

    m_offset = v;
    m_body->move(0, -m_offset);   /* ← 滚动的全部实现就这一句 */
    update();                     /* 只需要重画指示条 */
}

void ScrollView::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    /* 铺底色。不铺的话，body 没盖到的边缘会露出 Qt 未初始化的区域（花屏）。 */
    p.fillRect(rect(), QColor(Theme::AE::BgPanel));

    /* 内容超出一屏才画指示条 —— 能滚的时候得让人看得出来能滚 */
    if (!m_body || m_body->height() <= height())
        return;

    const int trackH = height();
    /* 条长按"可视比例"算：视口占内容的多少，条就占轨道的多少；
     * 但有最小高度，否则内容极长时条会细成一条线 */
    const int barH = qMax(kBarMinH, trackH * trackH / m_body->height());
    /* 条的位置跟着 offset 线性走；maxOffset 为 0 时除法会崩，所以 qMax(1, ...) */
    const int barY = (trackH - barH) * m_offset / qMax(1, maxOffset());

    p.fillRect(QRect(width() - kBarMargin, barY, kBarW, barH),
               QColor(Theme::AE::TextMuted));
}

void ScrollView::mousePressEvent(QMouseEvent *e)
{
    m_pressY  = e->y();
    m_start   = m_offset;
    m_drag    = false;
    m_pressed = true;
}

void ScrollView::mouseMoveEvent(QMouseEvent *e)
{
    /* ★ 只认"从本控件按下"的拖动。
     *
     *   子控件（下拉框/开关）吃了 press 但不管 move，move 会冒泡到这里。
     *   不判 m_pressed 的话，在控件上按下再划一下，弹层还开着、页面却滚走了 ——
     *   这就是当初"下拉框跟着页面跑"那个 bug 的来源。 */
    if (!m_pressed || !(e->buttons() & Qt::LeftButton))
        return;

    const int dy = e->y() - m_pressY;

    /* 小位移当抖动，超过阈值才算拖 */
    if (!m_drag && qAbs(dy) >= kDragThreshold) {
        m_drag = true;
        if (onDragStart)
            onDragStart();      /* 页面开始动 → 顺手把开着弹层的下拉框收起 */
    }

    if (m_drag)
        setOffset(m_start - dy);
}

void ScrollView::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag && m_rowStep > 0) {
        /* 松手吸附到整行：偏移永远是 rowStep 的整数倍，
         * 保证顶/底不会出现半行。
         * （+ rowStep/2 是四舍五入：拖过半行就吸附到下一行，不到就回上一行） */
        const int snapped = (m_offset + m_rowStep / 2) / m_rowStep * m_rowStep;
        setOffset(snapped);
    }

    m_drag    = false;
    m_pressed = false;
}

void ScrollView::wheelEvent(QWheelEvent *e)
{
    /* 触摸屏没有滚轮，但鼠标接调试口时会用到。
     * angleDelta().y() 一格是 120，除以 2 换算成"一格滚 60px" ——
     * 正好是 CameraPage 的一行，和拖动吸附后的粒度一致。 */
    setOffset(m_offset - e->angleDelta().y() / 2);
}
