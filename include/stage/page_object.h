// SPDX-License-Identifier: MIT
/** @file page_object.h
 *  @brief 页面接口（放在 ui/ 下，因为它是"页面必须实现的契约"）
 *
 *  【它解决什么问题】
 *  以前"切到某页该干什么"全写在 AppController 里，每加一个页面就要回去
 *  改控制器的 switch。现在每个页面自己实现 onEnter / onLeave，
 *  控制器只负责"调用谁"，不知道"具体做什么"。
 *  【实现者】
 *  · MainUi        —— onEnter/onLeave 就是 show/hide（顶栏底栏亮出来 / 收起来）
 *  · SettingsPage  —— onEnter 时 show + raise + repaint，onLeave 时 hide
 *  · 将来的 PlaybackPage / PhotoPage … 同理
 *  【注意：视频层不归页面管】
 *  "开不开视频层、用什么布局"由 AppController 按 page.h 的 PageLayout 统一处理，
 *  因为视频层是一块全局显示资源，不是某个页面的私有窗口。
 *  页面只负责自己那些 Qt 窗口的显隐。
 */
#ifndef PAGE_OBJECT_H
#define PAGE_OBJECT_H

class PageObject
{
public:
    /** @brief 虚析构。 */
    virtual ~PageObject() = default;

    /** @brief 成为当前页：把屏幕摆成"我这一页"该有的样子。
     *
     *  @note 调用时**旧页面的窗口可能还没收**（旧页面在自己的 onLeave 里收），
     *        所以这里只管"把自己的东西亮出来"，不要去管别人。
     */
    virtual void onEnter() = 0;

    /** @brief 离开本页：收掉自己的东西。
     *
     *  @note 调用时机在「新页面的 onEnter」**之后** —— 保证新页面先盖住、
     *        再收旧的，中间不会露出 fb0 的底色（那是"闪黑一帧"的来源）。
     */
    virtual void onLeave() = 0;
};

#endif /* PAGE_OBJECT_H */
