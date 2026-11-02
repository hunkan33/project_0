#!/bin/bash
# =====================================================================
#  build.sh —— 测试工程编译脚本
# =====================================================================
#  用法：bash build.sh
#  产物：build/video_show
# =====================================================================

set -e

cd "$(dirname "$0")"

echo "=============================================="
echo "  编译 video_show"
echo "=============================================="

TC="/opt/atk-dlt113is-toolchain"
QMAKE="$TC/bin/qmake"

if [ ! -x "$QMAKE" ]; then
    echo "❌ 找不到 qmake: $QMAKE"
    exit 1
fi

rm -rf build
mkdir -p build
cd build

"$QMAKE" ..
make -j4

cd ..

if [ ! -f build/video_show ]; then
    echo "❌ 没有生成 build/video_show"
    exit 1
fi

echo ""
echo "=============================================="
file -b build/video_show | cut -c1-60
ls -lh build/video_show | awk '{print "  大小: "$5}'
echo "=============================================="
echo ""
echo "下一步："
echo "    scp build/video_show root@192.168.5.4:/root/"
echo ""
