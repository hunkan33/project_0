// SPDX-License-Identifier: MIT
/** @file Theme.h
 *  @brief 配色 / 尺寸 / QSS 的唯一来源
 */
#ifndef THEME_H
#define THEME_H

#include <QString>

/* =============================================================================
 *  Theme —— AE 风格 UI（像素凸凹工业风）
 * =============================================================================
 *  【设计来源】Minecraft 模组「AE」(Applied Energistics) 的终端 / 合成界面
 *
 *  【三个灵魂】
 *    ① 凸凹质感   凸起：左上亮线 + 右下暗线；凹陷：方向反过来
 *                 这是整套 UI 的物理感来源，比色阶重要得多
 *    ② 零圆角     半径永远是 0，加一个圆角就变成"现代扁平风"
 *    ③ 单一强调色  只有一个浅天蓝 Accent 用作主操作，其余全是冷灰蓝
 *
 *  【本文件只放"常量 + QSS 字符串"，不放任何逻辑】
 *
 *  【为什么这里没有按钮样式】
 *    按钮由 AEButton 在 paintEvent 里全自绘 —— QSS 渐变做不出 1px 硬边，
 *    且 border 只有一层，做不出"外框 + 上方灰边"的两层结构。
 *    所以别再往这里加 QSS 按钮样式，加了也不会有人调用。
 *    按钮文字字号同理：来自 QApplication 全局字体（main.cpp 里 setPixelSize(27)）。
 *    （早先这里指向 Theme::Font::Body，该常量已随字体统一删除。）
 *
 *  【QWidget 用 QSS 背景的前提】
 *    普通 QWidget 子类默认不绘制 QSS 的 background/border，必须
 *        setAttribute(Qt::WA_StyledBackground, true);
 *    QPushButton / QLabel / QLineEdit 这些原生控件不需要。
 * =============================================================================
 */
/** @brief 配色 / 尺寸 / QSS 的唯一来源 —— 改外观只改这个文件。
 *
 *  @note 任何 .cpp 里都不许出现 "#RRGGBB" 字面量，一律引用这里的常量。
 */
namespace Theme {

/* =============================================================================
 *  结构常量
 * ============================================================================= */
/** @brief 控件厚度常量（AEButton 与 ToggleSwitch 共用一套）。 */
namespace Metric {

constexpr int BevelWidth = 2;    // QSS 面板边框（qssPanel / AEPage 用）

/* ---- 控件厚度：AEButton / ToggleSwitch 共用的唯一来源 ----
 * 数值来自 AEButton 当年按截图 237px 按钮等比换算到 56px 的结果，没有改动。
 *
 * ★ 它和上面的 BevelWidth=2 不是一套东西，不要合并：
 *     BevelWidth  是 QSS 边框，只有一层线；
 *     下面这套是「外框 + 内侧灰边 + 底部亮线 + 阴影」的四层结构，
 *     只有 paintEvent 画得出来，QSS 做不到。 */
constexpr int Frame      = 3;    // ① 外框（四周）
constexpr int Edge       = 3;    // ③ 内侧灰边（上 + 左右，U 形）
constexpr int Line       = 3;    //    底部亮线（通宽）
constexpr int Shadow     = 8;    // ② 底部阴影（常态）
constexpr int ShadowDown = 6;    //    底部阴影（按下变薄）

/* ---- 52px 小方块（数值弹窗的关闭按钮）专用紧凑套 ----
 *  ★ 不要和上面那套 56px 按钮的混用：3+3+3+8 在 52px 方块里只剩 29px 填充，
 *    装不下 30px 高的像素图标。这里 2+2+2+6 → 填充 36px。 */
constexpr int ChipFrame      = 2;
constexpr int ChipEdge       = 2;
constexpr int ChipLine       = 2;
constexpr int ChipShadow     = 6;
constexpr int ChipShadowDown = 4;

} // namespace Metric

/* =============================================================================
 *  开关控件尺寸（ToggleSwitch）
 * -----------------------------------------------------------------------------
 *      ┌───────────────────────────┐
 *      │ 3 │  左格 22  │ 2 │ 右格 22 │ 3 │   ← 宽 52
 *      └───────────────────────────┘
 *        高 = 3 + 22 + 3 = 28
 *
 *  全部是整数，落在 2px 网格上，不会出现半像素缝。
 * ============================================================================= */
/** @brief 开关控件的尺寸常量（两格 + 分隔）。 */
namespace Toggle {

/* 参考图原始尺寸 88×48（单格 40、外框 4、分隔 4），
 * 但那是配 32px 字形的比例。本工程正文是 24px，按 0.75 缩放：
 *      外框 4 → 3    单格 40 → 30    分隔 4 → 3
 *      亮边 4 → 3    底部阴影 8 → 6  滑块环 20 → 15
 * 缩放后开关高 36 = 正文 24 的 1.5 倍，与参考图（48 / 32 = 1.5）一致。 */
constexpr int Cell    = 30;   // 单格边长
constexpr int Divider = 3;    // 两格之间的分隔（与 Frame 同宽）
constexpr int Width   = Metric::Frame * 2 + Cell * 2 + Divider;  // 69
constexpr int Height  = Metric::Frame * 2 + Cell;                // 36

constexpr int Bevel   = 3;    // 凸起格内侧亮边厚度
constexpr int Shadow  = 6;    // 凸起格底部阴影厚度

constexpr int Knob    = 15;   // OFF 态滑块方环的外边长
constexpr int KnobGap = 3;    // OFF 态滑块方环的描边宽度（同时也是缺口边长）
constexpr int BarW    = 3;    // ON 态竖条宽
constexpr int BarH    = 15;   // ON 态竖条高

} // namespace Toggle

/* =============================================================================
 *  下拉框尺寸（AEComboBox）
 * -----------------------------------------------------------------------------
 *  收起态 240×52 —— 和 AEButton 同构（外框3/亮边3/亮线3/阴影8），
 *  留给文字的主体高度 = 52 - 6 - 3 - 3 - 8 = 32，够放 24px 的字。
 *
 *  展开态每行 32 高、行间 1px 间隙 —— 这个 1px 直接抄 AE2 的
 *  CPUSelectionList（行距 = buttonBg.getSrcHeight() + 1）。
 * ========================================================================== */
/** @brief 下拉框的尺寸常量（收起态 / 弹出列表 / 箭头）。 */
namespace Combo {

constexpr int Width     = 240;   // 收起态宽
constexpr int Height    = 52;    // 收起态高
constexpr int RowHeight = 32;    // 弹出列表每行高
constexpr int RowGap    = 1;     // 行间缝隙（AE2 的行距规则）
constexpr int Pad       = 4;     // 弹出层内容内边距（外框1 + 白线1 + 空2）
constexpr int TextPadX  = 12;    // 文字左右边距
constexpr int ArrowW    = 12;    // 下拉箭头宽
constexpr int ArrowH    = 6;     // 下拉箭头高

} // namespace Combo

/* =============================================================================
 *  AE 配色
 * -----------------------------------------------------------------------------
 *  色阶（明 → 暗）：
 *      #FFFFFF  BevelLight  凸起高光（最高亮）
 *      #CBCCD4  BgLight     标题栏 / 浅格子 / 按钮禁用态
 *      #B0B2C0  BgPanel     ★ 主面板
 *      #9A9FB4  BtnFill     按钮填充
 *      #555566  BevelDark   凸起暗边（阴影线）
 *      #273245  Text        主文字
 * ============================================================================= */
/** @brief 配色常量（AE2 实测取样）—— 全工程颜色的唯一定义处。 */
namespace AE {

/* ---- 背景层 ---- */
constexpr const char *BgLight = "#CBCCD4";   // 标题栏 / 浅格子 / 禁用填充
constexpr const char *BgPanel = "#B0B2C0";   // ★ 主面板

/* ---- 凸凹线 ---- */
constexpr const char *BevelLight = "#FFFFFF";   // 凸起亮边（左上）
constexpr const char *BevelDark  = "#555566";   // 凸起暗边（右下）

/* ---- 文字 ---- */
constexpr const char *Text      = "#273245";   // 主文字（深蓝黑）
constexpr const char *TextMuted = "#5A5F75";   // 次级文字 / 禁用文字

/* ---- 按钮（AEButton 自绘用，五部分结构）----
 *      ① 外框      #413F54
 *      ③ 上方灰边  #ADB0C4
 *      ④ 填充      #9A9FB4
 *      ② 下底阴影  #413F54
 *      ⑤ 文字      #FFFFFF
 *  ─────────────────────────────────────── */
constexpr const char *BtnFrame   = "#413F54";   // ① 外框
constexpr const char *BtnEdgeTop = "#ADB0C4";   // ③ 上方灰边
constexpr const char *BtnFill    = "#9A9FB4";   // ④ 填充
constexpr const char *BtnShadow  = "#413F54";   // ② 下底阴影
constexpr const char *BtnText    = "#FFFFFF";   // ⑤ 白字

/* ---- 开关专用（参考图 ui-image-inspector 实测色值）----
 *  #696D88 出现率 1.81%、#878FA5 出现率 0.20%，
 *  都是按钮色板里没有的中间调，专供开关的凹槽与滑块。 */
constexpr const char *ToggleTrack = "#696D88";   // 凹槽底色 / 凸起格底部阴影
constexpr const char *ToggleKnob  = "#878FA5";   // OFF 态滑块的方环描边

/* ---- 禁用态 / 次要信息（AE2 button_disabled.png 实测）----
 *   主体 #696D88、亮边 #878FA5、外框 #413F54
 * 注意：这三个灰和开关用的 ToggleTrack / ToggleKnob 是同一组值 ——
 * 凹槽、滑块描边、禁用按钮本来就是一套灰阶，所以直接复用，不新增颜色。
 * 只补一个文字色：压在 #696D88 上，对比度低但仍可读。 */
constexpr const char *DisabledText = "#B2B6CA";

/* ---- 顶栏录制状态（UpWindow 用）----
 * 红 = 正在录（要抓眼）、灰 = 就绪（要退到背景里）。
 * 这两个颜色原来硬编码在 UpWindow.cpp 里，绕过 Theme 就没人管得住。 */
constexpr const char *RecRed  = "#E53935";   /* ● REC */
constexpr const char *RecIdle = "#8A8A8A";   /* 就绪 */

/* ---- 面板内白线（AE2 background.png 实测：外框 1px #413F54 → 内白线 1px → 底色）----
 * 下拉弹出层用的是这套，和 AEPage 的四边凸凹不是一回事。 */
constexpr const char *PanelLine = "#F2F2F2";

/* ---- 强调色（主操作按钮 / 按下态）---- */
constexpr const char *Accent      = "#9CD3FF";   // ★ 填充（浅天蓝）
constexpr const char *AccentLight = "#DAFFFF";   // 内层高光线（顶部 / 底部）
constexpr const char *AccentDark  = "#708CBA";   // 内层暗边（底部）
constexpr const char *AccentText  = "#0E2233";   // 高亮按钮上的文字

/* ---- 数值弹窗右上「关闭」方块（用户参考图实测色）----
 *  #413F54 外框 / #696D88 面 / #F2F2F2 像素图标。
 *  突起靠"上亮下暗"：U 形亮边 → 面 → 底部亮线 → 阴影；
 *  按下时面压暗一档、阴影变薄、图标下移 1px。 */
constexpr const char *CloseFace     = "#696D88";
constexpr const char *CloseEdge     = "#878FA5";
constexpr const char *CloseLine     = "#A7ADBF";
constexpr const char *CloseShadow   = "#413F54";
constexpr const char *CloseInk      = "#F2F2F2";
constexpr const char *CloseFaceDown = "#5C6080";
constexpr const char *CloseEdgeDown = "#787E96";
constexpr const char *CloseInkDown  = "#CFD3E2";

} // namespace AE

/* =============================================================================
 *  字体
 * =============================================================================
 *  层级：只有两级 —— Small(12) 次级信息 / Body(24) 正文 · 按钮 · 页面标题
 *
 *  AEPage 会把 Body 设成自己和所有子控件（含页面标题）的默认字体；
 *  AEButton 是自绘的（paintEvent 里 p.setFont(font())），
 *  按钮字号实际来自这里，不受任何 QSS 影响 —— 要改按钮字号就改 Body。
 * ============================================================================= */
/** @brief 字体相关常量（全局字体在 main.cpp 设定，这里只剩次级字号）。 */
namespace Font {

/* =============================================================================
 *  全局字体：Source Han Sans CN 27px（思源黑体 CN）
 * =============================================================================
 *  【2026-10-30 统一】
 *    原来页面/顶栏跟随 QApplication::font()（平台默认，实际解析成思源 27px），
 *    底栏却单独设 Zpix 24px + NoAntialias —— 两套字体并存，
 *    还造成"AEPage 注释写着不要用像素字体、BottomBar 正在用"的矛盾。
 *
 *    现在只在 main.cpp 里 QApplication::setFont() 设一次，所有窗口/页面/控件跟随：
 *    AEButton / AEComboBox / ToggleSwitch 都是自绘且 p.setFont(font())，
 *    自动跟着父控件走。
 *
 *  【所以这里不再有 Family / Body】
 *    要改全局字体或字号，改 main.cpp 里那一处 setFont。
 *    族名 "Source Han Sans CN" 来自板端 fc-list，不是文件名。
 * ========================================================================== */

/* 次级信息字号（顶栏右侧的内存占用）：比正文小一号但看得清。
 * 保留它是因为"相对正文缩小"和"换一套字体"是两件不同的事。 */
constexpr int Secondary = 20;

} // namespace Font

/* =============================================================================
 *  QSS 生成 —— 主面板
 * =============================================================================
 *  凸起的大块区域：左上亮线 + 右下暗线 = 从面板上"凸出来"。
 *  用于顶栏（UpWindow）和底栏（BottomBar）。
 * ========================================================================== */
/** @brief 生成面板 QSS：底色 + 左上亮、右下暗的 2px 凸起边框。
 *  @return 可直接喂给 setStyleSheet() 的字符串。
 *  @warning 这条规则**没有选择器**，Qt 会把它一起套到子控件上 ——
 *           所以子控件的文字必须用 qssTextFlat() 显式抹掉边框。
 */
inline QString qssPanel()
{
    QString s;
    s += QString("background:%1;").arg(AE::BgPanel);
    s += QString("border-top:%1px solid %2;").arg(Metric::BevelWidth).arg(AE::BevelLight);
    s += QString("border-left:%1px solid %2;").arg(Metric::BevelWidth).arg(AE::BevelLight);
    s += QString("border-bottom:%1px solid %2;").arg(Metric::BevelWidth).arg(AE::BevelDark);
    s += QString("border-right:%1px solid %2;").arg(Metric::BevelWidth).arg(AE::BevelDark);
    return s;
}

/* =============================================================================
 *  QSS 生成 —— 标签文字（全工程唯一入口）
 * =============================================================================
 *  【为什么只留这一个】
 *    以前有两个：qssText() 只设 color，qssTextFlat() 还会抹掉 background/border。
 *    两者视觉上"通常"一样，但只有后者在"祖先控件设过样式表"时是安全的 ——
 *    这就成了一道要靠记性选对的选择题，而且已经有 3 处代码直接手写了
 *    QString("color:%1;") 绕过这两个函数（重复实现 + 同样的隐患）。
 *
 *    2026-10-31 起：删掉 qssText，只留 qssTextFlat —— 它是超集，没有任何场景
 *    需要"只上色但不抹边框"。要什么颜色传什么颜色，不传就是正文色。
 *  ★ 坑：父控件 setStyleSheet(qssPanel()) 时，Qt 会把没有选择器的规则
 *    **一起套到子控件上**，于是顶栏里每个 QLabel 都长出"左上亮、右下暗"的
 *    2px 边框（屏幕上看起来就是顶栏三块字下面各有一条突起）。
 *    子控件一旦自己也 setStyleSheet()，那套继承来的边框就会生效 ——
 *    所以这些标签必须显式写死 background:transparent / border:none 抹掉。
 *
 *  color 留空 = AE::Text；要换色（如 REC 红）就传一个 QSS 颜色串。
 * ========================================================================== */
/** @brief 生成"只上色、不要凸起盒子"的标签 QSS。
 *  @param color 文字颜色；留空 = AE::Text。要换色（如 REC 红）就传 QSS 颜色串。
 *  @return color + background:transparent + border:none + padding:0。
 *  @note 全工程唯一的 label 样式入口 —— 不要手写 QString("color:%1;")。
 */
inline QString qssTextFlat(const QString &color = QString())
{
    const QString c = color.isEmpty() ? QString::fromUtf8(AE::Text) : color;
    return QString("color:%1; background:transparent; border:none; padding:0px;").arg(c);
}

} // namespace Theme

#endif // THEME_H
