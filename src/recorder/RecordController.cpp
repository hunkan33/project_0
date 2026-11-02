// SPDX-License-Identifier: MIT
/** @file RecordController.cpp
 *  @brief 录制状态控制实现
 */
#include "RecordController.h"
#include "log.h"
#include <cstdio>

RecordController::RecordController(QObject *parent)
    : QObject(parent)
{
}

void RecordController::toggle()
{
    /* 将来这里要加前置条件判断：
     *   · 磁盘满            → 拒绝开始，提示用户
     *   · 正在收尾(fsync)   → 拒绝重新开始
     *   · 摄像头掉线        → 拒绝开始
     * 现在只是翻转状态；录像文件的读写由本目录下的写盘线程负责（还没写）。
     * ★ 目录沿革：recorder/ →（10-31）media/ →（11-01 目录重构）recorder/。
     *   现在就叫 recorder/，本目录将来的 AVI 封装 / 帧队列都归它。 */
    m_state = (m_state == RecState::Recording) ? RecState::Idle : RecState::Recording;

    LOGI("rec", "%s", m_state == RecState::Recording ? "开始" : "停止");

    emit stateChanged(m_state);
}
