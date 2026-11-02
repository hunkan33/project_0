// SPDX-License-Identifier: MIT
/** @file AEComboBox.cpp
 *  @brief AEComboBox 绘制与弹出列表
 */
#include "AEComboBox.h"
#include "Theme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

/* =============================================================================
 *  弹出列表
 * =============================================================================
 *  故意不继承 Q_OBJECT：它不需要信号槽，只要覆盖 paintEvent / mousePressEvent
 *  和 eventFilter 这三个虚函数。少一个 moc 文件，也少一层耦合。
 * ========================================================================== */
class AEComboPopup : public QWidget
{
public:
    explicit AEComboPopup(AEComboBox *owner);
    ~AEComboPopup() override;

    void showFor(const QStringList &items, const QVector<bool> &enabled, int current);
    bool itemEnabled(int i) const;
    void dismiss();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *e) override;

private:
    QRect rowRect(int i) const;
    int   rowAt(const QPoint &p) const;

    AEComboBox *m_owner   = nullptr;
    QStringList   m_items;
    QVector<bool> m_enabled;      /* false = 列出来但点不中 */
    int         m_current = -1;
    int         m_hover   = -1;
    bool        m_filterInstalled = false;
};

AEComboPopup::AEComboPopup(AEComboBox *owner)
    : QWidget(owner->window())
    , m_owner(owner)
{
    setMouseTracking(true);     /* 悬停要高亮，必须开 */
    hide();
}

AEComboPopup::~AEComboPopup()
{
    if (m_filterInstalled) {
        QWidget *w = window();
        if (w)
            w->removeEventFilter(this);
    }
}

QRect AEComboPopup::rowRect(int i) const
{
    const int pad = Theme::Combo::Pad;
    return QRect(pad,
                 pad + i * (Theme::Combo::RowHeight + Theme::Combo::RowGap),
                 width() - 2 * pad,
                 Theme::Combo::RowHeight);
}

bool AEComboPopup::itemEnabled(int i) const
{
    return (i >= 0 && i < m_enabled.size()) ? m_enabled.at(i) : true;
}

int AEComboPopup::rowAt(const QPoint &p) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (rowRect(i).contains(p))
            return i;
    return -1;
}

void AEComboPopup::showFor(const QStringList &items, const QVector<bool> &enabled, int current)
{
    m_items   = items;
    m_enabled = enabled;
    m_current = current;
    m_hover   = current;

    const int n = m_items.size();
    const int h = Theme::Combo::Pad * 2
                + n * Theme::Combo::RowHeight
                + (n > 1 ? (n - 1) * Theme::Combo::RowGap : 0);

    QWidget *win = window();
    if (!win)
        return;

    const QPoint origin = m_owner->mapTo(win, QPoint(0, 0));
    const int w = m_owner->width();

    /* 默认贴在下沿；下方放不下就翻到上方；两边都放不下就贴住下边 */
    int y = origin.y() + m_owner->height() + 2;
    if (y + h > win->height() - 4)
        y = origin.y() - h - 2;
    if (y < 4)
        y = win->height() - h - 4;
    if (y < 4)
        y = 4;

    setGeometry(origin.x(), y, w, h);

    if (!m_filterInstalled) {
        win->installEventFilter(this);   /* 点到外面就收起 */
        m_filterInstalled = true;
    }
    show();
    raise();
    update();
}

void AEComboPopup::dismiss()
{
    if (m_filterInstalled) {
        QWidget *w = window();
        if (w)
            w->removeEventFilter(this);
        m_filterInstalled = false;
    }
    hide();
}

/* -----------------------------------------------------------------------------
 *  画一行 —— 就是 AE2 的 button.png 按比例放大：
 *      外框 1px / 亮边 1px / 主体 / 亮边 1px / 底部阴影 4px / 外框 1px
 *  AE2 原生是 20px 高（阴影 3），这里是 32px 高，阴影取 4。
 * -------------------------------------------------------------------------- */
static void drawRow(QPainter &p, const QRect &row, bool on, bool usable)
{
    const char *cLight  = on ? Theme::AE::AccentLight : Theme::AE::BtnEdgeTop;
    const char *cBody   = on ? Theme::AE::Accent      : Theme::AE::BtnFill;
    const char *cShadow = on ? Theme::AE::AccentDark  : Theme::AE::ToggleTrack;

    if (!usable) {
        /* 不可选的项：和"禁用按钮"用同一套灰（AE2 button_disabled），
         * 而不是压平成浅色 —— 浅色会和面板底色糊在一起，看不出是"禁用"还是"没画"。 */
        cLight  = Theme::AE::ToggleKnob;    /* #878FA5 */
        cBody   = Theme::AE::ToggleTrack;   /* #696D88 */
        cShadow = Theme::AE::TextMuted;     /* #5A5F75 */
    }

    const int sh = 4;

    p.fillRect(row, QColor(Theme::AE::BtnFrame));

    const QRect in = row.adjusted(1, 1, -1, -1);

    if (usable) {
        p.fillRect(in, QColor(cLight));
        const QRect body(in.left() + 1, in.top() + 1, in.width() - 2, in.height() - 2 - sh);
        if (body.width() > 0 && body.height() > 0)
            p.fillRect(body, QColor(cBody));
        p.fillRect(QRect(in.left(), in.bottom() - sh + 1, in.width(), sh), QColor(cShadow));
    } else {
        /* 不可选行：外框内整块一色，没有内圈灰边也没有阴影 */
        p.fillRect(in, QColor(cBody));
    }
}

void AEComboPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect r = rect();

    /* 面板：外框 1px + 内白线 1px + 底色 —— 就是 AE2 background.png 的结构 */
    p.fillRect(r, QColor(Theme::AE::BtnFrame));
    p.fillRect(r.adjusted(1, 1, -1, -1), QColor(Theme::AE::PanelLine));
    p.fillRect(r.adjusted(2, 2, -2, -2), QColor(Theme::AE::BgLight));

    const QFont f = font();
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect row = rowRect(i);
        const bool usable = itemEnabled(i);
        const bool on = usable && ((i == m_current) || (i == m_hover));

        drawRow(p, row, on, usable);

        p.setFont(f);
        p.setPen(QColor(!usable ? Theme::AE::DisabledText
                                 : (on ? Theme::AE::AccentText : Theme::AE::Text)));
        p.drawText(row.adjusted(Theme::Combo::TextPadX, 0, -Theme::Combo::TextPadX, 0),
                   Qt::AlignLeft | Qt::AlignVCenter, m_items.at(i));
    }
}

void AEComboPopup::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const int i = rowAt(e->pos());
        if (i >= 0 && itemEnabled(i))       /* 灰色项点不中 */
            m_owner->selectFromPopup(i);
    }
    /* 点在面板空白处也收起，避免"点了没反应" */
    dismiss();
}

void AEComboPopup::mouseMoveEvent(QMouseEvent *e)
{
    int i = rowAt(e->pos());
    if (i >= 0 && !itemEnabled(i))
        i = -1;                             /* 灰色项不高亮 */
    if (i != m_hover) {
        m_hover = i;
        update();
    }
}

/* -----------------------------------------------------------------------------
 *  点到弹出层和下拉框之外 → 收起
 * -----------------------------------------------------------------------------
 *  必须把"下拉框自己在的那个矩形"排除掉，否则按下去时：
 *  过滤器先收起 → 事件再传回 AEComboBox::mousePressEvent → 又打开，
 *  点一下等于没点。
 * -------------------------------------------------------------------------- */
bool AEComboPopup::eventFilter(QObject *watched, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress && isVisible()) {
        QMouseEvent *me = static_cast<QMouseEvent *>(e);
        const QPoint gp = me->globalPos();

        const bool inPopup = rect().contains(mapFromGlobal(gp));
        const bool inOwner = m_owner && m_owner->rect().contains(m_owner->mapFromGlobal(gp));

        if (!inPopup && !inOwner)
            dismiss();
    }
    return QWidget::eventFilter(watched, e);
}

/* =============================================================================
 *  AEComboBox
 * ========================================================================== */
AEComboBox::AEComboBox(QWidget *parent) : QWidget(parent)
{
    setFixedSize(Theme::Combo::Width, Theme::Combo::Height);
    setFocusPolicy(Qt::NoFocus);
}

AEComboBox::~AEComboBox()
{
    /* 弹出层的父对象是 window()，不会跟着本控件析构；QPointer 已经帮我们
     * 判断它是否还活着（窗口先销毁时这里就是 nullptr）。 */
    if (m_popup)
        delete m_popup.data();
}

QSize AEComboBox::sizeHint() const
{
    return QSize(Theme::Combo::Width, Theme::Combo::Height);
}

void AEComboBox::addItem(const QString &text, const QVariant &data, bool enabled)
{
    m_texts   << text;
    m_data    << data;
    m_enabled << enabled;
    if (m_index < 0 && !m_texts.isEmpty())
        m_index = 0;
    update();
}

void AEComboBox::clearItems()
{
    m_texts.clear();
    m_data.clear();
    m_enabled.clear();
    m_index = -1;
    update();
}

int AEComboBox::indexOfData(const QVariant &data) const
{
    for (int i = 0; i < m_data.size(); ++i)
        if (m_data.at(i) == data)
            return i;
    return -1;
}

QVariant AEComboBox::currentData() const
{
    if (m_index < 0 || m_index >= m_data.size())
        return QVariant();
    return m_data.at(m_index);
}

QString AEComboBox::currentText() const
{
    if (m_index < 0 || m_index >= m_texts.size())
        return QString();
    return m_texts.at(m_index);
}

void AEComboBox::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_texts.size())
        return;
    m_index = index;
    update();
}

void AEComboBox::selectFromPopup(int index)
{
    closePopup();

    if (index < 0 || index >= m_texts.size() || index == m_index)
        return;
    if (index < m_enabled.size() && !m_enabled.at(index))
        return;                             /* 灰色项不接受选中 */

    m_index = index;
    update();
    emit currentIndexChanged(m_index);
}

void AEComboBox::dismissPopup()
{
    closePopup();
}

void AEComboBox::openPopup()
{
    if (m_texts.isEmpty())
        return;

    if (!m_popup)
        m_popup = new AEComboPopup(this);
    if (!m_popup)
        return;

    m_popup->showFor(m_texts, m_enabled, m_index);
    update();       /* 让下拉框自己切到"按下"配色 */
}

void AEComboBox::closePopup()
{
    if (m_popup)
        m_popup->dismiss();
    update();
}

void AEComboBox::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }

    /* 触摸屏上 Qt 会把触摸点转成鼠标左键，这里只认左键就够了 */
    if (m_popup && m_popup->isVisible())
        closePopup();
    else
        openPopup();
}

/* =============================================================================
 *  收起态：AEButton 的五层结构 + 左对齐文字 + 右侧向下箭头
 * ========================================================================== */
void AEComboBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect r = rect();
    const int F  = Theme::Metric::Frame;    /* 3 */
    const int E  = Theme::Metric::Edge;     /* 3 */
    const int L  = Theme::Metric::Line;     /* 3 */
    const int SH = Theme::Metric::Shadow;   /* 8 */

    if (r.width() <= 2 * F || r.height() <= 2 * F)
        return;

    /* 展开时保持"按下"配色，一眼能看出这个框是打开的 */
    const bool active = (m_popup && m_popup->isVisible());

    const char *cEdge   = active ? Theme::AE::AccentLight : Theme::AE::BtnEdgeTop;
    const char *cFill   = active ? Theme::AE::Accent      : Theme::AE::BtnFill;
    const char *cShadow = active ? Theme::AE::AccentDark  : Theme::AE::BtnShadow;
    const char *cText   = active ? Theme::AE::AccentText  : Theme::AE::BtnText;

    if (!isEnabled()) {
        /* 禁用态：AE2 button_disabled 的灰，不是"发白的空框"。
         * 主体 #696D88 / 亮边 #878FA5 / 外框不变，文字用低对比灰。 */
        cEdge   = Theme::AE::ToggleKnob;    /* #878FA5 */
        cFill   = Theme::AE::ToggleTrack;   /* #696D88 */
        cShadow = Theme::AE::TextMuted;     /* #5A5F75，阴影再暗一档 */
        cText   = Theme::AE::DisabledText;  /* #B2B6CA */
    }

    p.fillRect(r, QColor(Theme::AE::BtnFrame));

    const QRect in = r.adjusted(F, F, -F, -F);

    /* 文字 / 箭头的排版区：两种状态用同一个矩形，
     * 否则禁用行的文字会比启用行低几个像素。 */
    const QRect body(in.left() + E, in.top() + E,
                     in.width() - 2 * E, in.height() - E - L - SH);
    if (body.width() <= 0 || body.height() <= 0)
        return;

    if (isEnabled()) {
        /* 启用：内圈亮边 + 充填 + 底部亮线 + 底部阴影（上亮下暗 → 凸起）*/
        p.fillRect(in, QColor(cEdge));
        p.fillRect(body, QColor(cFill));
        p.fillRect(QRect(in.left(), body.bottom() + 1, in.width(), L), QColor(cEdge));
        p.fillRect(QRect(in.left(), body.bottom() + 1 + L, in.width(), SH), QColor(cShadow));
    } else {
        /* 禁用：外框里面整块一个颜色 ——
         * 没有内圈灰边、没有底部亮线、没有阴影。
         * （AE2 button_disabled 那 1px #878FA5 在 20px 高的按钮上是一道细描边，
         *  放到本工程 52px 高的控件上要放大到 3px，就变成一圈醒目的灰框了，
         *  和“不可点击的次要状态”相反：它反而比启用态更吸眼）*/
        p.fillRect(in, QColor(cFill));
    }

    /* 文字：左对齐，右边给箭头留位置 */
    p.setFont(font());
    p.setPen(QColor(cText));
    p.drawText(body.adjusted(Theme::Combo::TextPadX, 0,
                             -Theme::Combo::ArrowW - Theme::Combo::TextPadX, 0),
               Qt::AlignLeft | Qt::AlignVCenter, currentText());

    /* 箭头：逐行收窄的实心三角，纯 fillRect，保证硬边 */
    const int cx = body.right() - Theme::Combo::TextPadX - Theme::Combo::ArrowW / 2;
    const int y0 = body.center().y() - Theme::Combo::ArrowH / 2;
    for (int i = 0; i < Theme::Combo::ArrowH; ++i) {
        const int half = Theme::Combo::ArrowW / 2 - i;
        if (half <= 0)
            break;
        p.fillRect(QRect(cx - half, y0 + i, half * 2, 1), QColor(cText));
    }
}
