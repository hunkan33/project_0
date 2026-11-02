#!/bin/bash
# =====================================================================
#  ctrl_enum —— 用 V4L2_CTRL_FLAG_NEXT_CTRL 遍历全部控制项
# =====================================================================
#  用途：不硬编码 CID，列出设备支持的所有控制项；顺带用 QUERYMENU
#        展开 menu 类型的选项名（能看出哪些 index 实际不存在）。
#
#  用法：bash tests/ctrl_enum/build.sh && ssh root@192.168.5.4 /tmp/ctrl_enum
#  注意：主程序在跑也没关系（uvcvideo 允许多次 open，本程序不开流）。
# =====================================================================
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"
BOARD=""
[ -f "$PROJ/config/board_ip.txt" ] && BOARD=$(tr -d ' \n\r' < "$PROJ/config/board_ip.txt")
TC=/opt/atk-dlt113is-toolchain/bin/arm-linux-gnueabi-gcc

echo ">>> 编译"
"$TC" -O2 -Wall -o "$HERE/ctrl_enum" "$HERE/main.c"

[ -n "$BOARD" ] || { echo "❌ 没有 config/board_ip.txt"; exit 1; }
echo ">>> 推送到板子 /tmp/ctrl_enum"
scp -q "$HERE/ctrl_enum" "root@$BOARD:/tmp/ctrl_enum"
echo ">>> 完成。在板子上跑：ssh root@$BOARD /tmp/ctrl_enum"
