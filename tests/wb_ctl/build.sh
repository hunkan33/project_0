#!/bin/bash
# =====================================================================
#  wb_ctl —— 白平衡自动控制项写入实验
# =====================================================================
#  用途：定位「程序启动时写 wb_auto=1 失败（EINVAL），
#        但 v4l2-ctl 和独立最小程序能写成功」这个现象。
#
#  程序会先打印 camera_ctl.h 里三个 CID 宏在**当前包含顺序下**算出来的值，
#  和设备上报的真实值比对 —— 这是定位问题的关键一步。
#
#  用法（在 VM 上）：
#      bash tests/wb_ctl/build.sh && ssh root@192.168.5.4 /tmp/wb_ctl
# =====================================================================
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"

BOARD=""
[ -f "$PROJ/config/board_ip.txt" ] && BOARD=$(tr -d ' \n\r' < "$PROJ/config/board_ip.txt")

TC=/opt/atk-dlt113is-toolchain/bin/arm-linux-gnueabi-gcc

echo ">>> 编译"
"$TC" -O2 -Wall -I"$PROJ/src/capture" -o "$HERE/wb_ctl" "$HERE/main.cpp"

[ -n "$BOARD" ] || { echo "❌ 没有 config/board_ip.txt"; exit 1; }

echo ">>> 推送到板子 /tmp/wb_ctl"
scp -q "$HERE/wb_ctl" "root@$BOARD:/tmp/wb_ctl"

echo ">>> 完成。在板子上跑："
echo "    ssh root@$BOARD /tmp/wb_ctl"
