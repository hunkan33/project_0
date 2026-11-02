// SPDX-License-Identifier: MIT
/** @file AENumberDialog.h
 *  @brief 数值修改弹窗（仿 AE2「选择数量」）
 */
#ifndef AENUMBERDIALOG_H
#define AENUMBERDIALOG_H

#include <QList>
#include <QRect>
#include <QString>
#include <QWidget>

class AEButton;
class AEValueField;
class QLabel;
class QPainter;

/** @brief 数值修改弹窗（AE 风格，仿 AE2 的「选择数量」）。
 *
 *  【它是弹窗，不是页面】
 *    一个覆盖在 owner（CameraPage）上的 QWidget：
 *      · 先铺一层半透明黑遮罩，吃掉所有下层点击
 *      · 中间一块面板，里面是标题栏 + 范围行 + 步进按钮 + 数值 + 设置
 *    **不进页面状态机**，也不挂 window() —— 它就是 owner 的一个子控件。
 *
 *    owner->onLeave() 时必须调 hide()，否则切页后弹窗会残留在别的页上。
 *
 *  【没有输入框】
 *    这块屏没有软键盘。参考图里那个数值框在 MC 上可以点进去打字，我们做不到 ——
 *    所以它只是**显示**，改动全靠 ± 按钮。别以为"点数值能输入"。
 *
 *  【弹窗内只改副本】
 *    所有 ± 操作只动 m_value 并刷新显示，**不下发**。
 *    点「设置」才发 accepted(value)；点右上关闭 / 点遮罩 = 取消，什么都不改。
 *    所以"取消"是真的取消，不会留半个改动在设备上。
 *
 *  【尺寸随档位数变】
 *    档位数 N 由 max 决定（见 stepsForMax），面板宽 = N×120 + (N-1)×12 + 44。
 *    按钮尺寸恒定 120×64，所以档位少的参数弹窗更窄而不是按钮更大。
 *
 *  【范围行】（2026-10-31 加）
 *    min ~ max 单独占标题下面一行、居中、次级字号。
 *    ★ 之前是在 paintEvent 里往标题栏下半部随手 drawText，
 *      结果压在「+1」按钮底下、只露出半个 "0 ~" —— 等于没有。
 *      范围是"这个参数能填多少"的唯一提示，必须独占一行、不被任何控件遮挡。
 * ========================================================================== */
class AENumberDialog : public QWidget
{
    Q_OBJECT

public:
    /** @brief 构造弹窗（初始隐藏，open() 时才显示）。
     *  @param owner 覆盖目标，通常是 CameraPage。
     */
    explicit AENumberDialog(QWidget *owner);

    /** @brief 打开弹窗。
     *
     *  @param title 参数名（如「亮度」），显示在标题栏。
     *  @param min   允许的最小值（来自 camera_ctl_query）。
     *  @param max   允许的最大值。
     *  @param value 初始值；内部会 clamp 到 [min, max]。
     *
     *  @note 每次调用都会按 (min,max) 重算档位并**重建全部按钮** ——
     *        档位数不是固定的（亮度 2 档、增益 4 档），所以不能画死。
     *  @note 弹窗会铺满 owner 并 raise，调用前不需要自己 show()。
     */
    void open(const QString &title, int min, int max, int value);

signals:
    /** @brief 用户点了「设置」。
     *  @param value 用户在弹窗里确认的新值（已 clamp 在 [min,max] 内）。
     *  @note 收到后由调用方负责下发到设备 + 写配置 —— 弹窗内部**不下发**，
     *        它只改副本，所以"取消"是真的取消。
     */
    void accepted(int value);

protected:
    /// 画遮罩 + 面板 + 范围行
    void paintEvent(QPaintEvent *e) override;
    /// 点面板外面（遮罩）= 取消；点面板内部空白不做任何事
    void mousePressEvent(QMouseEvent *e) override;

private:
    /// 算档位、定面板尺寸、按 N 重建全部子控件
    void rebuild(const QString &title);
    /// 把 m_value 加减 delta 并 clamp，然后只刷新显示（不下发）
    void applyStep(int delta);
    /// 把 m_value 写进值框
    void refreshValue();

    QRect m_panelRect;       ///< 面板在弹窗内的位置
    QRect m_rangeRect;       ///< 范围文字那一行（面板坐标）

    QLabel       *m_title = nullptr;    ///< 标题（参数名），居中
    QString      m_rangeText;           ///< "范围 min ~ max"，paintEvent 里画
    AEValueField *m_valueBox = nullptr; ///< 大号数值显示（只读，不可输入）
    AEButton     *m_ok = nullptr;       ///< 「设置」按钮
    QList<AEButton *> m_buttons;        ///< 步进按钮 + 关闭按钮，统一管理删除

    QList<int> m_steps;                 ///< 本次的档位列表，如 {1, 10, 100}
    int m_min = 0;                      ///< 下限
    int m_max = 0;                      ///< 上限
    int m_value = 0;                    ///< 当前副本值（未提交）
};

#endif /* AENUMBERDIALOG_H */
