// SPDX-License-Identifier: MIT
/** @file MainUi.cpp
 *  @brief 顶栏 + 底栏装配
 */
#include "MainUi.h"

/* =============================================================================
 *  PageObject 实现
 * =============================================================================
 *  主界面的"进场/退场"就是顶栏底栏的显隐，没有别的。
 *  视频层不在这里处理 —— 由 AppController 按 PageLayout.videoVisible 统一开关。
 * ========================================================================== */

void MainUi::onEnter()
{
    show();
    raise();
}

void MainUi::onLeave()
{
    hide();
}

/* =============================================================================
 *  成对操作
 * =============================================================================
 *  成对这件事只在 MainUi 里出现一次，以后主界面再加常驻元素也只改这里，
 *  AppController 和 main.cpp 都不用动。
 * ========================================================================== */

void MainUi::show()
{
    m_up.show();
    m_bar.show();
}

void MainUi::hide()
{
    m_up.hide();
    m_bar.hide();
}

void MainUi::raise()
{
    m_up.raise();
    m_bar.raise();
}

void MainUi::setRecording(bool on)
{
    m_up.setRecording(on);
    m_bar.setRecording(on);
}
