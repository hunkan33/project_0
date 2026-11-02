// SPDX-License-Identifier: MIT
/** @file page.h
 *  @brief 页面 / 布局 / 事件的【声明】（纯数据，一行 Qt、一行硬件代码都没有）
 *
 *  实现全在 page.cpp：
 *  · 各页面的坐标表（现 5 张，想改哪页显示在哪，看 page.cpp 顶部）
 *  · pageName() / eventName()（日志用）
 *  · pageLayout()（按页取坐标）
 *  【为什么单独一个模块】
 *  "某个页面长什么样" 必须只有一份定义。不然就会重演老毛病：
 *  视频区坐标散落在 capture_thread.cpp（WIN_*）、dvr_app.cpp（第三份硬编码）、
 *  display.cpp（从参数传进来），改一处就错两处。
 *  【改这里的情况】
 *  加一个页面  → Page 加枚举值（page.h）
 *  + pageName() 加一行（page.cpp）
 *  + 坐标表加一行（page.cpp）
 *  加一个事件  → Event 加枚举值（page.h）
 *  + eventName() 加一行（page.cpp）
 *  + app_state.cpp 的 kPageTable 加转换行
 *  调某页坐标  → 只改 page.cpp 顶部那张表
 *
 *  ★ 注意：上面只是**本模块内**的步骤。加一个能进出的页面，模块外还要接线
 *  （UI 控件信号 → 状态机、页面注册进 AppController、applyPage 分支等，
 *  共 8 处，清单见 AGENT.md P6.1）—— 照上面三步做完页面不会动，别以为漏了编译。
 */
#ifndef PAGE_H
#define PAGE_H

/* 页面：互斥，一次只有一个 */
enum class Page {
    Main,            /* 顶栏 48 + 视频 480 + 底栏 72 */
    Fullscreen,      /* 视频铺满 600，OSD 叠加在上面 */
    Settings,        /* 全屏设置面板，视频必须完全隐藏 */
    CameraSettings,  /* 全屏摄像头设置面板，与 Settings 同等级，视频同样隐藏 */
    Storage          /* 全屏存储面板，与 Settings 同等级，视频同样隐藏 */
};

/** @brief 一页的视频布局：从源图裁哪块（crop）→ 铺到屏幕哪块（win）。
 *
 *  @warning crop 的宽高比必须等于 win 的宽高比 —— DE 只做直接映射，
 *           不做等比缩放，比例不对画面就会被拉伸。
 */
struct PageLayout {
    int  cropX, cropY, cropW, cropH;   ///< 从源图（1280×720 基准）裁哪一块
    int  winX,  winY,  winW,  winH;    ///< 铺到屏幕（1024×600）哪一块
    bool videoVisible;                 ///< 该页是否需要 ch[0] 显示视频
};

/** @brief 送进页面状态机的刺激（用户动作）。
 *
 *  @warning 加枚举值后**必须**去 page.cpp 的 eventName() 和
 *           app_state.cpp 的 kPageTable 补上，否则这个事件会被当作
 *           "表里没有"而静默忽略 —— 表现为"点了按钮没反应"。
 */
enum class Event {
    ClickSettings,      ///< 底栏「设置」
    ClickCamera,        ///< 设置页「摄像头设置」
    ClickBack,          ///< 各页「返回」
    ClickFullscreen,    ///< 「全屏」页（预留，底栏没有这个按钮）
    ClickStorage        ///< 「存储」页（已接线：BottomBar::storageClicked → AppController）
};

/** @brief 取页面名的字符串形式（打日志用）。
 *  @param p 页面。
 *  @return 静态字符串，如 @c "Main" / @c "Settings"；不归调用方释放。
 */
const char       *pageName(Page p);

/** @brief 取事件名的字符串形式（打日志用）。
 *  @param e 事件。
 *  @return 静态字符串，如 @c "ClickSettings"。
 */
const char       *eventName(Event e);

/** @brief 取某页的布局表项。
 *  @param p 页面。
 *  @return 布局的**常引用** —— 生命周期是整个程序（指向静态表），调用方不要存指针。
 *  @note 这是页面坐标的唯一真相源，AppController 从这里取再换算下发。
 */
const PageLayout &pageLayout(Page p);

/** @brief 屏幕宽（像素）。DE 图层和 fb0 清屏都用它，集中在这。 */
#define SCR_W   1024
/** @brief 屏幕高（像素）。 */
#define SCR_H   600

/* 页面坐标表的基准源尺寸。
 *
 * page.cpp 里所有 crop 数值都是按 1280×720 写的。
 * 实际采集分辨率换了（设置页可以选 640×480 / 1920×1080），
 * AppController 会按 srcW/PAGE_REF_W、srcH/PAGE_REF_H 等比换算。
 *
 * 所以：改 page.cpp 的坐标时，心里要按这个基准尺寸算；
 * 换摄像头导致基准变了，只要改这两个宏。 */
/** @brief 页面坐标表的基准源宽。
 *
 *  @note page.cpp 里所有 crop 数值都是按 1280×720 写的。实际采集分辨率换了
 *        （640×480 / 1920×1080…），AppController 会按 srcW/PAGE_REF_W 等比换算。
 *  @note 换摄像头导致基准变了，只要改这个宏。
 */
#define PAGE_REF_W  1280
/** @brief 页面坐标表的基准源高。见 @ref PAGE_REF_W。 */
#define PAGE_REF_H  720

#endif /* PAGE_H */
