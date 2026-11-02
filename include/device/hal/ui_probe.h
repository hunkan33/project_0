// SPDX-License-Identifier: MIT
/** @file ui_probe.h
 *  @brief 让自动化脚本能"点"这个界面（**仅供测试**）
 *
 *  【为什么要在进程内做】
 *    这块板子的内核**没有 uinput**（`/dev/uinput` 不存在，`modprobe uinput` 也找不到模块），
 *    所以没法从外部造一个虚拟触摸设备注入事件。唯一可行的办法是在进程内
 *    直接把 `QMouseEvent` post 给目标控件。
 *
 *  【怎么开】
 *    环境变量 `DVR_UI_PROBE=1`。**不设就完全不启动**（生产模式零开销、零攻击面）。
 *
 *  【怎么用】
 *    往 `/tmp/dvr_ui.cmd` 写一行：
 *        click 512 300
 *    探针每 100ms 看一次，读到就执行并删掉文件（一次性命令，不会重复触发）。
 *    坐标是**屏幕坐标**（linuxfb 单屏，原点左上，1024×600）。
 *
 *  【它做了什么】
 *    找到该坐标下最上层的控件 → post 一次 MouseButtonPress + MouseButtonRelease
 *    （`QPushButton` 收到这对事件就会发 clicked），并打一行日志说明点到了什么：
 *        [probe] 点击 (512,300) → AEButton
 *
 *  @warning 这是**测试设施**，不要在业务代码里依赖它。
 */
#ifndef _UI_PROBE_H_
#define _UI_PROBE_H_

/** @brief 启动探针。**必须在 QApplication 构造之后调用**（要用到 widgetAt / 事件循环）。
 *  @note 环境变量 `DVR_UI_PROBE` 不是 "1" 时立即返回，什么都不做。
 */
void ui_probe_start(void);

#endif /* _UI_PROBE_H_ */
