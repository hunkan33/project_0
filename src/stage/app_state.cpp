// SPDX-License-Identifier: MIT
/** @file app_state.cpp
 *  @brief 页面状态机实现
 */
#include "app_state.h"
#include "log.h"
#include <cstdio>

/* ---- 页面转移表：加页面/事件只加表项 ---- */
struct PageTransition {
    Page  from;
    Event evt;
    Page  to;
};

static const PageTransition kPageTable[] = {
    { Page::Main,       Event::ClickSettings,   Page::Settings   },
    { Page::Main,       Event::ClickFullscreen, Page::Fullscreen },
    { Page::Main,       Event::ClickStorage,    Page::Storage    },
    /* 设置页 ⇄ 摄像头设置页：平级的两页，靠「摄像头设置」/「返回」互跳。
     * 摄像头页的「返回」回到设置页而不是主界面 —— 用户是从设置页进去的。 */
    { Page::Settings,       Event::ClickCamera, Page::CameraSettings },
    { Page::CameraSettings, Event::ClickBack,   Page::Settings       },

    { Page::Settings,   Event::ClickBack,       Page::Main       },
    { Page::Fullscreen, Event::ClickBack,       Page::Main       },
    { Page::Storage,    Event::ClickBack,       Page::Main       },

    /* 其余事件不加表项 = 被忽略（handleEvent 会打日志） */
};

/* ---- 查表：定义在 handleEvent 之前，无需前置声明 ---- */
static bool lookupPage(Page from, Event e, Page *out)
{
    for (size_t i = 0; i < sizeof(kPageTable) / sizeof(kPageTable[0]); ++i) {
        if (kPageTable[i].from == from && kPageTable[i].evt == e) {
            *out = kPageTable[i].to;
            return true;
        }
    }
    return false;
}

AppState::AppState(QObject *parent)
    : QObject(parent)
{
    /* m_page 的初始值在头文件里给了 Page::Main，这里不用重复 */
}

bool AppState::handleEvent(Event e)
{
    Page to = m_page;

    if (!lookupPage(m_page, e, &to)) {
        LOGW("state", "忽略 %s（当前 %s，表里没有）", eventName(e), pageName(m_page));
        return false;
    }
    return gotoPage(to, e);
}

bool AppState::gotoPage(Page to, Event e)
{
    /* 表里不该有指向自己的行；真出现了当无变化处理，不发信号（避免控制器白重绘） */
    if (to == m_page)
        return false;

    const Page from = m_page;
    m_page = to;

    emit pageChanged(from, to);

    LOGI("state", "%s --%s--> %s", pageName(from), eventName(e), pageName(to));
    return true;
}
