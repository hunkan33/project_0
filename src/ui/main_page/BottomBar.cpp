// SPDX-License-Identifier: MIT
/** @file BottomBar.cpp
 *  @brief 底栏实现
 */
#include "BottomBar.h"

#include <QApplication>

BottomBar::BottomBar(QWidget *parent) : QWidget(parent)
{
    /* ---- 窗口本身 ---- */
    setGeometry(0, 528, 1024, 72);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(Theme::qssPanel());

    /* 字体跟随应用默认（main.cpp 里 QApplication::setFont() 设的思源 27px）。
     * AEButton 是自绘的（paintEvent 里 p.setFont(font())），
     * 所以底栏四个按钮的文字实际来自这里设的字体。
     *
     * 2026-10-30 统一：这里原来是全工程唯一还在用 Zpix 24px + NoAntialias 的地方，
     * 和页面/顶栏的思源 27px 两套并存。不要再往这里加 QFont("Zpix")。 */
    setFont(QApplication::font());

    /* ---- 四个按钮 ----
       x 从 30 开始，每个间距 254（按钮 200 + 间隔 54）
       最后一个是 792，右边留 32 */
    m_btnRecord = new AEButton("录制", this);
    m_btnRecord->setGeometry(30, 8, 200, 56);
    m_btnRecord->setCheckable(true);        // 录制按钮需要保持激活状态
    m_btnRecord->setToggleMode(true);      // checked 时保持蓝色

    m_btnPhoto = new AEButton("拍照", this);
    m_btnPhoto->setGeometry(284, 8, 200, 56);

    m_btnStorage = new AEButton("存储", this);
    m_btnStorage->setGeometry(538, 8, 200, 56);

    m_btnSettings = new AEButton("设置", this);
    m_btnSettings->setGeometry(792, 8, 200, 56);

    /* ---- 连接 ----
       录制按钮走 onRecordClicked（只上报事件，不改状态）
       其他三个直接把 clicked 转发成自己的信号 */
    connect(m_btnRecord, &QPushButton::clicked, this, &BottomBar::onRecordClicked);
    connect(m_btnPhoto,    &QPushButton::clicked, this, &BottomBar::snapshotClicked);
    connect(m_btnStorage,  &QPushButton::clicked, this, &BottomBar::storageClicked);
    connect(m_btnSettings, &QPushButton::clicked, this, &BottomBar::settingsClicked);
}

BottomBar::~BottomBar()
{
}

void BottomBar::onRecordClicked()
{
    /* 只报告"被点了"。不要在这里翻转状态 ——
     * 状态由 AppState::handleEvent 统一决定，
     * 翻转完会通过 recChanged → setRecording 回到这里。 */
    emit recordClicked();
}

void BottomBar::setRecording(bool on)
{
    m_btnRecord->setText(on ? "停止" : "录制");
    /* setChecked 以前从来没被调用过，导致 setCheckable(true) +
     * setToggleMode(true) 白设了，按钮从不保持高亮。这里补上。 */
    m_btnRecord->setChecked(on);
}
