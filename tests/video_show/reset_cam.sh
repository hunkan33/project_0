#!/bin/bash
# =====================================================================
#  reset_cam.sh —— 测试前重置摄像头
# =====================================================================
#  解决：上次进程被 kill -9 强杀后，STREAMON 状态残留在内核里，
#        导致 "Device or resource busy" 或者 S_FMT 报 EIO。
#
#  用法：bash reset_cam.sh
# =====================================================================
BOARD="${1:-root@192.168.5.4}"

echo "=============================================="
echo "  重置摄像头: $BOARD"
echo "=============================================="

ssh "$BOARD" '
# 1. 杀掉所有可能占用设备的进程
killall video_show work 2>/dev/null
sleep 2

# 2. 找摄像头的 USB 路径（按 idVendor=05a3）
D=""
for d in /sys/bus/usb/devices/*; do
    if [ -f "$d/idVendor" ] && [ "$(cat $d/idVendor 2>/dev/null)" = "05a3" ]; then
        D="$d"
        break
    fi
done

# 3. 软 reset（断电再上电）
if [ -n "$D" ]; then
    echo "  找到摄像头: $(basename $D)"
    echo 0 > "$D/authorized" 2>/dev/null
    sleep 1
    echo 1 > "$D/authorized" 2>/dev/null
    echo "  已 reset，等 4 秒重新枚举..."
    sleep 4
else
    echo "  ⚠️  没找到摄像头（idVendor=05a3）"
fi

# 4. 重建 /dev 节点
mdev -s 2>/dev/null

# 5. 验证
echo ""
echo "=== 结果 ==="
if lsusb | grep -q "05a3"; then
    echo "  ✅ 摄像头在线: $(lsusb | grep 05a3)"
    echo "  --- 设备节点 ---"
    ls -l /dev/video* 2>/dev/null | sed "s/^/  /"
    echo "  --- 能否打开 ---"
    v4l2-ctl --list-devices 2>&1 | sed "s/^/  /"
else
    echo "  ❌ 摄像头不在 lsusb 里，需要物理重新插拔"
fi
'
