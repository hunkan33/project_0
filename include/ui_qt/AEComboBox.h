// SPDX-License-Identifier: MIT
/** @file AEComboBox.h
 *  @brief AE 风格下拉框（含弹出列表）
 */
#ifndef AECOMBOBOX_H
#define AECOMBOBOX_H

#include <QWidget>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QPointer>

class AEComboPopup;

/* =============================================================================
 *  AEComboBox —— AE 风格下拉框（全自绘）
 * =============================================================================
 *  AE2 本身没有下拉框，但它有「CPU 选择列表」（CPUSelectionList.java）：
 *  一块面板 + 若干行按钮，行距 = 行高 + 1px，选中行换高亮贴图。
 *  本控件把那套列表收进一个按钮下面，按钮沿用 AEButton 的分层。
 *
 *      收起（AEButton 的五层结构，文字左对齐、右边多一个箭头）
 *      ┌──────────────────────────────┐
 *      │  1280×720                 ▼  │
 *      └──────────────────────────────┘
 *              展开后浮在下面
 *      ┌──────────────────────────────┐
 *      │ ┌──────────────────────────┐ │  ← 行（未选中：灰按钮）
 *      │ │ 640×480                  │ │
 *      │ ├──────────────────────────┤ │  ← 1px 间隙，AE2 的行距规则
 *      │ │ 1280×720                 │ │  ← 当前项：高亮配色
 *      │ ├──────────────────────────┤ │
 *      │ │ 1920×1080                │ │
 *      │ └──────────────────────────┘ │
 *      └──────────────────────────────┘
 *
 *  【为什么弹出层是独立 widget】
 *    Qt 的绘制会被父控件矩形裁掉，一个 52px 高的控件没法在自己身上画
 *    100px 的列表。所以弹出层是独立 widget，挂在 window() 下，
 *    show + raise，自己处理行点击和"点到外面就收起"。
 *
 *  【数据模型】每项 = 显示文本 + QVariant 附带数据。
 *    分辨率那条塞 QSize，帧率塞 int，使用者不用回头解析字符串。
 * ========================================================================== */
class AEComboBox : public QWidget
{
    Q_OBJECT

public:
    /** @brief 构造下拉框（初始无项）。 @param parent 父控件。 */
    explicit AEComboBox(QWidget *parent = nullptr);

    /** @brief 析构（会一并删掉弹出层）。 */
    ~AEComboBox() override;

    /** @brief 追加一项。
     *
     *  @param text    显示文字。
     *  @param data    附带数据（回填/比较用），默认无效 QVariant。
     *  @param enabled @c false 的项**照样列在弹出层里，但画成灰色、点不中** ——
     *                 用来展示"硬件支持、程序目前切不过去"的项（比如 YUYV）。
     */
    void addItem(const QString &text, const QVariant &data = QVariant(),
                 bool enabled = true);

    /** @brief 清空所有项（同时收起弹出层）。 */
    void clearItems();

    /** @brief 项数。 */
    int      count() const { return m_texts.size(); }

    /** @brief 按附带数据找下标。
     *  @param data 要找的数据。
     *  @return 下标；找不到返回 **-1**。
     *  @note 用于"把配置文件里存的值回填到下拉框"。
     */
    int      indexOfData(const QVariant &data) const;

    /** @brief 当前选中项的下标；-1 = 没有选中项。 */
    int      currentIndex() const { return m_index; }

    /** @brief 当前选中项的附带数据。 */
    QVariant currentData()  const;

    /** @brief 当前选中项的文字。 */
    QString  currentText()  const;

    /** @brief 设置选中项**但不发** currentIndexChanged。
     *  @param index 目标下标。
     *  @note 专给初始化用：先按文件里的值设好，再连信号 ——
     *        否则初始化就会被当成"用户改了一次"，触发一轮无意义的换流。
     */
    void setCurrentIndex(int index);

    /** @brief 供弹出层回调。
     *  @param index 被点中的行下标。
     *  @warning 外部不要调 —— 只有 AEComboPopup 该用它。
     */
    void selectFromPopup(int index);

    /** @brief 收起弹出的列表（没展开时是空操作）。
     *
     *  @note 页面被滚动/拖动时由外部调用：弹出层坐标是相对 window 算的，
     *        控件自己被挪走以后它还留在原处，看起来就像"框飘在外面"。
     */
    void dismissPopup();

    /** @brief 首选尺寸（宽 240 × 高 52，见 Theme::Combo）。 */
    QSize sizeHint() const override;

signals:
    /** @brief 用户改了选中项。
     *  @param index 新下标。
     *  @note **只有用户点列表行才会发**；setCurrentIndex() 不发。
     */
    void currentIndexChanged(int index);

protected:
    /// 画收起态（外框 / 亮边 / 填充 / 文字 / 箭头）
    void paintEvent(QPaintEvent *e) override;
    /// 点一下 = 展开或收起弹出层
    void mousePressEvent(QMouseEvent *e) override;

private:
    /// 建弹出层并按当前项定位，show + raise
    void openPopup();
    /// 收起并释放弹出层
    void closePopup();

    QStringList   m_texts;       ///< 各项显示文字
    QVariantList  m_data;        ///< 各项附带数据
    QVector<bool> m_enabled;     ///< 每项是否可选（false = 灰显、点不中）
    int          m_index = -1;   ///< 当前下标；-1 = 无选中项

    // QPointer：弹出层父对象是 window() 而不是本控件，
    // 窗口析构时它可能先被删掉，用裸指针会悬空。
    QPointer<AEComboPopup> m_popup;   ///< 弹出层（弱引用，可能已被窗口析构）
};

#endif // AECOMBOBOX_H
