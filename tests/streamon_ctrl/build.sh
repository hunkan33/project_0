#!/bin/bash
# =====================================================================
#  streamon_ctrl —— 验证「STREAMON 是否重置 V4L2 控制项」
# =====================================================================
#  用途：确认 capture_thread.cpp 里那句
#        「开流之后再下发图像控制项：uvc 会在 STREAMON 时重置一部分控制」
#        到底成不成立。这决定了「控制项下发时机」这个设计的依据。
#
#  【跑之前必须先停主程序】它占着 /dev/video0 并且已经在 STREAMON 状态：
#      bash scripts/deploy.sh stop
#      bash tests/streamon_ctrl/build.sh
#      ssh root@192.168.5.4 /tmp/streamon_ctrl
#      bash scripts/deploy.sh run
# =====================================================================
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"

BOARD=""
[ -f "$PROJ/config/board_ip.txt" ] && BOARD=$(tr -d ' \n\r' < "$PROJ/config/board_ip.txt")

# ★ .c 用 gcc 编、.cpp 用 g++ 编，不能全交给 g++：
#   g++ 会把 .c 也当 C++ 编译，符号名被 mangle，
#   而 main.cpp 里是用 extern "C" 声明的 —— 两边对不上，链接报 undefined。
GCC=/opt/atk-dlt113is-toolchain/bin/arm-linux-gnueabi-gcc
GXX=/opt/atk-dlt113is-toolchain/bin/arm-linux-gnueabi-g++

echo ">>> 编译"
rm -f "$HERE"/*.o
"$GCC" -O2 -Wall -I"$PROJ/src/capture" -c "$PROJ/src/capture/capture.c"     -o "$HERE/capture.o"
"$GCC" -O2 -Wall -I"$PROJ/src/capture" -c "$PROJ/src/capture/camera_ctl.c"  -o "$HERE/camera_ctl.o"
# capture.c 里 capture_init() 会调 camera_sync_caps()，它定义在 camera_info.c
"$GCC" -O2 -Wall -I"$PROJ/src/capture" -c "$PROJ/src/capture/camera_info.c" -o "$HERE/camera_info.o"
"$GXX" -O2 -Wall -I"$PROJ/src/capture" -c "$HERE/main.cpp"                  -o "$HERE/main.o"
"$GXX" -o "$HERE/streamon_ctrl" "$HERE/main.o" "$HERE/capture.o" "$HERE/camera_ctl.o" "$HERE/camera_info.o"

[ -n "$BOARD" ] || { echo "❌ 没有 config/board_ip.txt"; exit 1; }

echo ">>> 推送到板子 /tmp/streamon_ctrl"
scp -q "$HERE/streamon_ctrl" "root@$BOARD:/tmp/streamon_ctrl"

echo ">>> 完成。记得先停主程序再跑："
echo "    bash scripts/deploy.sh stop"
echo "    ssh root@$BOARD /tmp/streamon_ctrl"
echo "    bash scripts/deploy.sh run"
