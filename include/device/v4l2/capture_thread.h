// SPDX-License-Identifier: MIT
/** @file capture_thread.h
 *  @brief 采集线程的入口声明
 *
 *  本文件只声明线程入口。**采集的全部行为都在 `capture_session.h` 的
 *  `CaptureSession` 里**（等设备 → 协商 → 建管线 → 开流 → 主循环 → 收尾），
 *  想改"取帧之后发生什么"，去那里。
 *
 *  【怎么用】main.cpp 里：
 *      volatile bool g_running = true;
 *      pthread_t tid;
 *      pthread_create(&tid, nullptr, capture_thread, nullptr);
 *      ...   // 主线程跑 Qt
 *      g_running = false;          // 让会话退出
 *      pthread_join(tid, nullptr);
 *
 *  【为什么参数是 void*】pthread_create 要求的签名：void *(*)(void *)。
 *  会话不需要外部参数（配置自己读、尺寸自己协商），C++ 里可省略形参名。
 *  【g_running 为什么不在头文件声明】它定义在 main.cpp，只有实现文件用
 *  extern 引一次；放头文件会让每个包含者都以为自己是定义处。
 */
#ifndef _CAPTURE_THREAD_H_
#define _CAPTURE_THREAD_H_

/** @brief 采集线程入口（pthread 用）。
 *  @param 未使用 —— pthread 要求的签名；会话需要的配置与设备都是自己拿的。
 *  @return 永远是 nullptr；由 g_running = false 触发退出，不做 cancel。 */
void *capture_thread(void *);

#endif /* _CAPTURE_THREAD_H_ */
