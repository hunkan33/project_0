// SPDX-License-Identifier: MIT
/** @file CameraPage.h
 *  @brief 摄像头设置页（8 行 × 2 列，可滚动）
 */
#ifndef CAMERAPAGE_H
#define CAMERAPAGE_H

#include "AEPage.h"
#include "AEButton.h"
#include "AEComboBox.h"
#include "ToggleSwitch.h"
#include "page_object.h"

#include <QLabel>
#include <QSize>

class AEValueField;
class AENumberDialog;

#include "camera_service.h"  /* ★ 摄像头操作唯一门面（CamMode → CameraService::ModeInfo） */

/* =============================================================================
 *  CameraPage —— 摄像头设置页（从设置页的「摄像头设置」进）
 * =============================================================================
 *  【它管什么】
 *    ① 采集格式三兄弟：格式 / 分辨率 / 帧率 —— 改完立刻请求采集线程换流
 *    ② V4L2 控制项里"选项不超过 3 个"的那几个：
 *         白平衡自动（开关，1/0）
 *         抗工频闪烁（关闭 / 50Hz / 60Hz）
 *         曝光模式（设备只报手动，固定 1，显示出来但不让改）
 *    ③ 其余数值型控制项（亮度 / 对比度 / 饱和度 / 伽马 / 增益 / 色温档位 /
 *       锐度 / 逆光补偿 / 手动曝光值 / 对焦位置）—— 用户要求"先只留个位置"，
 *       这里画成灰色的范围提示，**不接控件**。后续再做数值输入。
 *
 *  【它是一个【页面】，不是设置页的子窗口】（2026-10-29 改）
 *    和 SettingsPage 完全平级：
 *      · 在 main.cpp 里是栈对象、parent = nullptr 的顶层窗口；
 *      · 实现 PageObject，注册进 AppController::m_pageObjects；
 *      · 显隐由页面状态机驱动（Settings --ClickCamera--> CameraSettings，
 *        CameraSettings --ClickBack--> Settings），自己不 hide 别人。
 *
 *    这么改的直接好处：视频层开关、清屏时序、crop 重算都走
 *    AppController::applyPage() 那一条路，不用在设置页里手工补一遍。
 *
 *  【保存】
 *    界面改动先只停在控件上；点「保存」才写配置 + 下发设备。
 *    保存路径和原设置页完全一致：AppSettings 落盘 /etc/dvr/settings.ini，
 *    格式换流走 capture_request_format()，控制项走 camera_ctl_set()。
 *
 *  【可滚动】
 *    16 行排不进 416px 的正文区，所以正文整块放在 ScrollView 里，上下拖动查看；
 *    标题和「返回」固定不动。以后加行只改 kRows，不用重排版式。
 *
 *  【返回】
 *    只发 backRequested()，由 AppController 转成状态机事件 ClickBack，
 *    目标页是 Settings（不是 Main）。
 * ========================================================================== */
class CameraPage : public AEPage, public PageObject
{
    Q_OBJECT

public:
    /** @brief 构造摄像头设置页（建 8 行 × 2 列全部控件 + 数值弹窗）。
     *  @param parent 父控件（通常传 nullptr，页面自己是顶层）。
     */
    explicit CameraPage(QWidget *parent = nullptr);

    /** @brief 重新读配置与设备状态，刷新全部控件。
     *
     *  @note 每次都重读：能力表可能变了（换了摄像头），配置也可能被保存过。
     *  @note onEnter() 内部会调它，**外部一般不用直接调**。
     */
    void refresh();

    /* ---- PageObject ---- */
    /// 进场：重读配置 + 亮出来 + 同步刷屏（面板要盖住视频层，不能等下一帧）
    void onEnter() override;
    /// 退场：先关掉数值弹窗（它是本页子控件，不 hide 会残留在别的页上），再收起来
    void onLeave() override;

signals:
    void backRequested();

private:
    /** @brief 建一行标签（贴左、纵向居中）。
     *  @param col  哪一列（0 = 左，1 = 右）。
     *  @param row  哪一行（0..7）。
     *  @param text 标签文字。
     *  @return 建好的标签（由 m_body 持有）。
     */
    QLabel *addLabel(int col, int row, const QString &text);

    /** @brief 建一行数值控件：只读值框 + 「修改」按钮。
     *  @param col  哪一列。
     *  @param row  哪一行。
     *  @param slot m_num 里的槽位（同时也是 kNumSpecs 的下标）。
     *  @note 点「修改」打开 AENumberDialog；弹窗点「设置」只标 dirty + 更新值框，
     *        **真正下发在 onSave()**。
     */
    void addNumericRow(int col, int row, int slot);

    /** @brief 打开某个数值项的弹窗（初值从设备读，不读配置）。
     *  @param slot m_num 里的槽位（0..9）。
     */
    void openNumericDialog(int slot);

    /** @brief 弹窗确认后的回调：只更新值框 + 标记 dirty，不下发。
     *  @param slot  哪一项（m_num 的下标）。
     *  @param value 用户在弹窗里确认的新值。
     */
    void onNumericAccepted(int slot, int value);

    /** @brief 刷新 10 个数值项：读范围 + 当前值 + 可调性（INACTIVE 置灰）。 */
    void refreshNumericValues();

    /** @brief 读一次能力表。
     *  @param all 输出数组，调用方分配（容量 CAM_MAX_MODES）。
     *  @return 条数；<= 0 表示没插摄像头或 camera.ini 还没生成。
     *  @note 三个 rebuild 共用这一份，不再各自 fopen —— 之前一次进页面 fopen 三次。
     */
    int  loadModes(CameraService::ModeInfo *all);

    /// 重建「格式」下拉框（吃已经读好的能力表）
    void rebuildFormat(const CameraService::ModeInfo *all, int n);
    /// 重建「分辨率」下拉框（按当前格式过滤）
    void rebuildResolution(const CameraService::ModeInfo *all, int n);
    /// 重建「帧率」下拉框（按当前格式 + 分辨率过滤）
    void rebuildFps(const CameraService::ModeInfo *all, int n);

    /// 把设备当前的控制值读回来，填到开关/下拉框上
    void syncDeviceControls();

    /** @brief 「保存」：界面当前值 → 配置文件 + 立刻生效。
     *
     *  @note 三件事：① 下发所有 dirty 的数值项 ② 写 capture/ 与 camera/ 下的配置
     *        ③ 登记换流请求 + 写 wbAuto/powerLine。
     *  @note 换格式只能采集线程自己做（它持有 video_fd / 解码器 / DE 句柄），
     *        所以这里只 capture_request_format() 登记，不直接换。
     */
    void onSave();

    /** @brief 保存反馈：按钮文字短暂变成 @p text，0.8s 后变回「保存」。
     *  @param text 临时文字（如「已保存」/「无摄像头」）。
     */
    void flashSaved(const QString &text);

    /** @brief 整行置灰。
     *  @param label 该行标签；可为 NULL。
     *  @param ctrl  该行控件；可为 NULL。
     *  @param usable true = 恢复正常配色；false = 标签转灰 + 控件禁用。
     */
    void muteRow(QLabel *label, QWidget *ctrl, bool usable);

    AEButton *m_btnBack = nullptr;
    AEButton *m_btnSave = nullptr;

    // 滚动容器：m_view 是 96..516 的视口，m_body 是里面 480px 高的内容板。
    // 类型写成 QWidget* —— 本页只需要 show/hide/move，不需要 ScrollView 的完整定义。
    QWidget *m_view = nullptr;   ///< 视口（ScrollView 实例）
    QWidget *m_body = nullptr;   ///< 内容板（8 行 × 60px）

    /* ---- 10 个数值控制项（亮度/对比度/…/对焦位置）----
     * 一行 = 值框 + 「修改」按钮，点按钮开弹窗。
     * 范围 / 步长 / 可调性**运行时问设备**（camera_ctl_query），不在头文件里写死。
     * 标题 / CID / 配置键定义在 .cpp 的 kNumSpecs。 */
    struct NumItem {
        int           slot = -1;           ///< 对应 CameraService::ctrlXxx 的下标
        AEValueField *field = nullptr;     ///< 行上的只读值框
        AEButton     *btn = nullptr;       ///< 行上的「修改」按钮
        int           min = 0;             ///< 下限（来自 camera_ctl_query）
        int           max = 0;             ///< 上限
        bool          usable = false;      ///< 此刻是否可调
        bool          alwaysOff = false;   ///< 定焦这类"永远不可调"的
        bool          dirty = false;       ///< 弹窗里改过但还没点「保存」
        int           pendingValue = 0;    ///< 弹窗里改到的新值（保存时下发）
    };
    QList<NumItem> m_num;
    AENumberDialog *m_dlg = nullptr;
    int             m_dlgSlot = -1;    /* 当前弹窗对应哪一项 */

    QLabel     *m_lblFormat       = nullptr;
    AEComboBox *m_cmbFormat       = nullptr;

    QLabel     *m_lblResolution   = nullptr;
    AEComboBox *m_cmbResolution   = nullptr;

    QLabel     *m_lblFps          = nullptr;
    AEComboBox *m_cmbFps          = nullptr;

    QLabel       *m_lblWbAuto     = nullptr;
    ToggleSwitch *m_swWbAuto      = nullptr;

    QLabel     *m_lblFlicker      = nullptr;
    AEComboBox *m_cmbFlicker      = nullptr;

    QLabel     *m_lblExpMode      = nullptr;
    AEComboBox *m_cmbExpMode      = nullptr;

    QString m_fmtNow;

    /* 重建期间屏蔽"改下拉框 → 提交"的回调，否则 refresh 会被自己写爆 */
    bool m_syncing = false;
};

#endif /* CAMERAPAGE_H */
