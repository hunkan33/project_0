# =====================================================================
#  work.pro —— DVR 工程描述文件（标准嵌入式工程结构）
# =====================================================================
#  目录约定（2026-11-01 重构：源码与头文件分离）
#      src/                  源码（**只有 .c / .cpp**）
#        main.cpp            入口
#        app/                应用层：controller（页面编排）/ config（配置读写）
#        stage/              页面状态机：Page / PageLayout / AppState
#        camera/             摄像头：控制项 / 能力表 / 无 Qt 的 C++ 门面
#        device/             设备层（**零 Qt**）：v4l2 / decoder / display / hal
#        recorder/           录制（音视频落盘）
#        common/             通用工具（日志，纯 C）
#        ui/                 页面：main_page（顶底栏）/ setting_page（三个设置页）
#        ui_qt/              Qt 控件库（AEButton / AEComboBox / AEPage / …）
#      include/              头文件（**与 src/ 目录一一镜像**，工程根独立目录）
#                            由 scripts/update_pro.py 自动同步进下面的三块
#      third_party/sdk/      全志 SDK
#        include/            头文件
#        lib/                .so
#      scripts/              构建与部署脚本
#      config/               板子 IP 等配置
#      docs/                 文档
#      build/                编译产物（自动生成）
# =====================================================================
# ---------------------------------------------------------------------
#  QT 模块依赖
# ---------------------------------------------------------------------
QT += core gui widgets

CONFIG += c++11 console

# -rdynamic：链接时导出符号表，崩溃处理器的 backtrace 才能显示函数名
QMAKE_LFLAGS += -rdynamic

TARGET = work

# ---------------------------------------------------------------------
#  头文件搜索路径
# ---------------------------------------------------------------------
#  把每个模块目录都加进来，好处是源码里可以继续写
#      #include "capture.h"
#  而不用改成
#      #include "capture/capture.h"
#  移动文件时只要同步改这里即可，源码一行不用动。
# ---------------------------------------------------------------------
INCLUDEPATH += \
    include \
    include/app \
    include/app/config \
    include/app/controller \
    include/camera \
    include/common \
    include/device \
    include/device/decoder \
    include/device/display \
    include/device/hal \
    include/device/v4l2 \
    include/net \
    include/recorder \
    include/stage \
    include/ui \
    include/ui/main_page \
    include/ui/setting_page \
    include/ui_qt

# ---------------------------------------------------------------------
#  全志 SDK 头文件
# ---------------------------------------------------------------------
#  disp2  : 显示相关（hwdisp2.h / sunxi_display2.h）
#  camera : 相机相关
INCLUDEPATH += \
    third_party/sdk/include \
    third_party/sdk/include/disp2 \
    third_party/sdk/include/camera

# ---------------------------------------------------------------------
#  源文件
# ---------------------------------------------------------------------
SOURCES += \
    src/main.cpp \
    src/recorder/RecordController.cpp \
    src/app/config/app_settings.cpp \
    src/app/config/settings_c.cpp \
    src/app/controller/app_controller.cpp \
    src/camera/camera_ctl.c \
    src/camera/camera_info.c \
    src/camera/camera_service.cpp \
    src/common/log.c \
    src/device/decoder/decoder.cpp \
    src/device/display/display.cpp \
    src/device/hal/storage.c \
    src/device/hal/ui_probe.cpp \
    src/device/v4l2/capture.c \
    src/device/v4l2/capture_session.cpp \
    src/device/v4l2/capture_thread.cpp \
    src/device/v4l2/pipeline.cpp \
    src/stage/app_state.cpp \
    src/stage/page.cpp \
    src/ui/main_page/BottomBar.cpp \
    src/ui/main_page/MainUi.cpp \
    src/ui/main_page/UpWindow.cpp \
    src/ui/setting_page/CameraPage.cpp \
    src/ui/setting_page/SettingsPage.cpp \
    src/ui/setting_page/StoragePage.cpp \
    src/ui_qt/AEButton.cpp \
    src/ui_qt/AEComboBox.cpp \
    src/ui_qt/AENumberDialog.cpp \
    src/ui_qt/AEPage.cpp \
    src/ui_qt/AEValueField.cpp \
    src/ui_qt/ScrollView.cpp \
    src/ui_qt/ToggleSwitch.cpp \
    src/ui_qt/page_chrome.cpp

HEADERS += \
    include/app/config/app_settings.h \
    include/app/config/settings_c.h \
    include/app/controller/app_controller.h \
    include/camera/camera_ctl.h \
    include/camera/camera_info.h \
    include/camera/camera_service.h \
    include/common/log.h \
    include/device/decoder/decoder.h \
    include/device/display/display.h \
    include/device/hal/storage.h \
    include/device/hal/ui_probe.h \
    include/device/v4l2/capture.h \
    include/device/v4l2/capture_session.h \
    include/device/v4l2/capture_thread.h \
    include/device/v4l2/pipeline.h \
    include/recorder/RecordController.h \
    include/stage/app_state.h \
    include/stage/page.h \
    include/stage/page_object.h \
    include/ui/main_page/BottomBar.h \
    include/ui/main_page/MainUi.h \
    include/ui/main_page/UpWindow.h \
    include/ui/setting_page/CameraPage.h \
    include/ui/setting_page/SettingsPage.h \
    include/ui/setting_page/StoragePage.h \
    include/ui_qt/AEButton.h \
    include/ui_qt/AEComboBox.h \
    include/ui_qt/AENumberDialog.h \
    include/ui_qt/AEPage.h \
    include/ui_qt/AEValueField.h \
    include/ui_qt/ScrollView.h \
    include/ui_qt/Theme.h \
    include/ui_qt/ToggleSwitch.h \
    include/ui_qt/page_chrome.h

# =====================================================================
#  链接 SDK 库
# =====================================================================
#  【现在保持注释】
#    各模块还是空壳，没有调用任何 SDK 函数，链接了反而会因依赖链缺符号而失败。
#    等 decoder 真正调用 AWVideoDecoder 时再取消注释。
#
#  库文件位置：third_party/sdk/lib/
# =====================================================================
LIBS += -L$$PWD/third_party/sdk/lib
LIBS += -lsdk_decoder -lsdk_memory -lsdk_log -lrpbuf \
        -lvdecoder -lcedarse -lMemAdapter -lVE -lvideoengine \
        -lawmjpeg -lcdx_base -lcdx_common -lcdx_ion
