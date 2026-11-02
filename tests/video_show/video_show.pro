QT += core gui widgets
CONFIG += c++11 console
TARGET = video_show
TEMPLATE = app

# 复用主程序的采集模块（不复制，改一处两边同步）
INCLUDEPATH += ../../src/capture
SOURCES += main.cpp
SOURCES += ../../src/capture/capture.c
