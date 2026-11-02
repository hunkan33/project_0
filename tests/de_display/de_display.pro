QT -= core gui
CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = de_display
TEMPLATE = app

INCLUDEPATH += \
    ../../src/capture \
    ../../src/decoder \
    ../../src/display \
    ../../third_party/sdk/include \
    ../../third_party/sdk/include/disp2 \
    ../../third_party/sdk/include/camera

SOURCES += \
    main.cpp \
    ../../src/capture/capture.c \
    ../../src/decoder/decoder.cpp \
    ../../src/display/display.cpp

LIBS += -L$$PWD/../../third_party/sdk/lib
LIBS += -lsdk_decoder -lsdk_memory -lsdk_log -lrpbuf \
        -lvdecoder -lcedarse -lMemAdapter -lVE -lvideoengine \
        -lawmjpeg -lcdx_base -lcdx_common -lcdx_ion
LIBS += -lpthread
