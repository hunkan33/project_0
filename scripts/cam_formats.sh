#!/bin/bash
# =====================================================================
#  cam_formats.sh —— 查指定摄像头支持的全部【格式 / 分辨率 / 帧率】
# =====================================================================
#  位置：scripts/cam_formats.sh
#
#  【用哪个设备】
#      bash scripts/cam_formats.sh                # 自动找：第一个 uvcvideo 设备
#      bash scripts/cam_formats.sh /dev/video0
#      bash scripts/cam_formats.sh video0         # 简写
#      bash scripts/cam_formats.sh 0              # 简写
#
#  【其它用法】
#      bash scripts/cam_formats.sh --list         # 只列板上有哪些视频节点
#      bash scripts/cam_formats.sh --raw          # 打印 v4l2-ctl 原始输出
#      bash scripts/cam_formats.sh --local        # 在本机跑，不 ssh 到板子
#
#  【为什么默认走 ssh】
#     摄像头接在板子上。脚本从 config/board_ip.txt 取板子 IP，
#     和 deploy.sh / build.sh 用的是同一份配置。
#
#  【自动找设备的规则】和 capture.c 的 capture_init() 完全一致：
#     ① driver == "uvcvideo"            —— 排除 sunxi-tvd
#     ② Device Caps 里有 Video Capture  —— 排除同一个摄像头的 metadata 节点
#     两条缺一不可：这颗 UVC 的 /dev/video1 也是 uvcvideo 驱动，
#     但它的 Device Caps 只有 Metadata Capture，选错就打不开流。
#
#  【为什么不在板子上解析】
#     板子上的 busybox awk 功能不全。这里只让目标跑 v4l2-ctl，
#     原始输出拉回本机解析，逻辑集中在一处。
# =====================================================================

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ" 2>/dev/null || cd "$(dirname "$0")/.."

BOARD_USER="root"
BOARD=""
[ -f config/board_ip.txt ] && BOARD=$(tr -d ' \n\r' < config/board_ip.txt)

LOCAL=0
MODE="table"       # table | raw | list
DEV=""

usage() {
    cat <<'TXT'
用法:
  bash scripts/cam_formats.sh                自动找第一个 UVC 摄像头
  bash scripts/cam_formats.sh /dev/video0    指定设备
  bash scripts/cam_formats.sh video0         简写
  bash scripts/cam_formats.sh 0              简写

其它:
  --list     只列有哪些视频节点
  --raw      打印 v4l2-ctl 原始输出
  --local    在本机跑，不 ssh 到板子
  -h         显示本帮助
TXT
}

# ---------------- 参数 ----------------
while [ $# -gt 0 ]; do
    case "$1" in
        --local)   LOCAL=1 ;;
        --raw)     MODE="raw" ;;
        --list)    MODE="list" ;;
        -h|--help) usage; exit 0 ;;
        -*)        echo "❌ 不认识的参数: $1"; echo; usage; exit 1 ;;
        *)         DEV="$1" ;;
    esac
    shift
done

# 设备名简写归一化
case "$DEV" in
    "")           ;;
    /dev/video*)  ;;
    video*)       DEV="/dev/$DEV" ;;
    *[!0-9]*)     echo "❌ 设备名看不懂: $DEV（要 /dev/videoN、videoN 或 N）"; exit 1 ;;
    *)            DEV="/dev/video$DEV" ;;
esac

# ---------------- 在目标上执行一条命令 ----------------
run() {
    if [ "$LOCAL" = "1" ]; then
        bash -c "$1"
    else
        if [ -z "$BOARD" ]; then
            echo "❌ 不知道板子 IP：config/board_ip.txt 不存在或为空" >&2
            echo "   要么 echo 192.168.5.4 > config/board_ip.txt，要么加 --local 在本机跑" >&2
            return 1
        fi
        ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "$BOARD_USER@$BOARD" "$1"
    fi
}

WHERE="板子 $BOARD"
[ "$LOCAL" = "1" ] && WHERE="本机"

# ---------- 先确认目标可达、工具在 ----------
if ! run "true" 2>/dev/null; then
    echo "❌ 连不上 $WHERE"
    exit 1
fi

if ! run "command -v v4l2-ctl >/dev/null 2>&1"; then
    echo "❌ 目标上没有 v4l2-ctl（$WHERE）"
    echo "   板子：自己编 v4l-utils"
    echo "   本机：apt install v4l-utils"
    exit 1
fi

# ---------- --list ----------
if [ "$MODE" = "list" ]; then
    echo "=============================================="
    echo "  $WHERE 的视频节点"
    echo "=============================================="
    run "v4l2-ctl --list-devices"
    exit $?
fi

# ---------- 自动找设备 ----------
# 一次性把每个节点的 --info 拉回来，在本机判定，避免来回 ssh
SNIPPET_INFO='for n in 0 1 2 3 4 5 6 7 8 9; do d=/dev/video$n; if [ -e "$d" ]; then echo "###DEV $d"; v4l2-ctl -d "$d" --info 2>&1; fi; done'

pick_uvc() {
    awk '
        function check() {
            if (dev != "" && drv == "uvcvideo" && caps && !found)
                found = dev
        }
        /^###DEV / { check(); dev = $2; drv = ""; caps = 0; incaps = 0; next }
        /Driver name/ { s = $0; sub(/.*:[ \t]*/, "", s); drv = s }
        /Device Caps/ { incaps = 1; next }
        incaps == 1 && /Video Capture/ { caps = 1 }
        END { check(); if (found != "") print found }
    '
}

if [ -z "$DEV" ]; then
    echo "（未指定设备，自动查找 UVC 摄像头…）"
    DEV=$(run "$SNIPPET_INFO" | pick_uvc)
    if [ -z "$DEV" ]; then
        echo "❌ 没找到 uvcvideo 设备。先看板上有哪些节点："
        echo "   bash scripts/cam_formats.sh --list"
        exit 1
    fi
    echo "   找到：$DEV"
fi

# ---------- 一次取回：info + 格式表 + 当前配置 ----------
SNIPPET_ALL='echo "###INFO###"; v4l2-ctl -d '"$DEV"' --info 2>&1; echo "###FMT###"; v4l2-ctl -d '"$DEV"' --list-formats-ext 2>&1; echo "###INI###"; cat /etc/dvr/settings.ini 2>/dev/null'

DUMP=$(run "$SNIPPET_ALL")

if [ "$MODE" = "raw" ]; then
    echo "$DUMP" | sed -n '/###FMT###/,/###INI###/p' | sed '1d;$d'
    exit 0
fi

if [ -z "$DUMP" ]; then
    echo "❌ 读不到 $DEV"
    exit 1
fi

INFO=$(echo "$DUMP" | sed -n '/###INFO###/,/###FMT###/p' | sed '1d;$d')
FMT=$(  echo "$DUMP" | sed -n '/###FMT###/,/###INI###/p' | sed '1d;$d')
INI=$(  echo "$DUMP" | sed -n '/###INI###/,$p'           | sed '1d')

CARD=$(  echo "$INFO" | sed -n 's/.*Card type[ \t]*:[ \t]*//p'   | head -1)
DRIVER=$(echo "$INFO" | sed -n 's/.*Driver name[ \t]*:[ \t]*//p' | head -1)
BUS=$(   echo "$INFO" | sed -n 's/.*Bus info[ \t]*:[ \t]*//p'    | head -1)

if [ -z "$CARD" ]; then
    echo "❌ 打不开 $DEV（设备不存在，或被别的进程独占）"
    echo "   正在跑采集程序的话先停：bash scripts/deploy.sh stop"
    exit 1
fi

# 这个节点是不是真正的采集口？（metadata 节点的 Device Caps 里没有 Video Capture）
IS_CAPTURE=0
echo "$INFO" | sed -n '/Device Caps/,$p' | grep -q "Video Capture" && IS_CAPTURE=1

# ---------- 表头 ----------
echo ""
echo "=============================================="
echo "  $DEV   $CARD"
echo "  driver: $DRIVER    bus: $BUS"
echo "  查询位置: $WHERE"
echo "=============================================="
echo ""

if [ "$IS_CAPTURE" = "0" ]; then
    echo "⚠️  这个节点的 Device Caps 里没有 Video Capture —— 它不是采集口，"
    echo "    多半是同一个摄像头的 metadata 节点（下面不会有像素格式）。"
    echo "    用 --list 看全部节点，采集口通常是编号最小的那个（这颗摄像头是 /dev/video0，video1 是它的 metadata）。"
    echo ""
fi

# ---------- 解析格式表 ----------
# 输入形如：
#     [0]: 'MJPG' (Motion-JPEG, compressed)
#         Size: Discrete 3840x2160
#             Interval: Discrete 0.033s (30.000 fps)
# 两个坑：
#   ① 同一尺寸会重复出现（同一帧率列两遍），要按 fps 去重
#   ② 尺寸有 Discrete / Stepwise 两种写法
echo "$FMT" | awk '
    function dash(n,   s) { s = ""; while (length(s) < n) s = s "-"; return s }

    function fps_of(s,   b, c, v, sec) {
        if (index(s, "fps") > 0) {              # 常见：Discrete 0.033s (30.000 fps)
            split(s, b, "(")
            split(b[2], c, " ")
            v = c[1] + 0
            return int(v + 0.5)
        }
        # Continuous 之类没有 fps 字段，用第一个秒数换算
        split(s, b, " ")
        sec = b[3]; sub(/s$/, "", sec)
        if (sec + 0 > 0) return int(1 / sec + 0.5)
        return 0
    }

    /^[ \t]*\[[0-9]+\]:/ {
        split($0, a, "\047")                    # 按单引号切，a[2] 是 fourcc
        fmt = a[2]; gsub(/^ +| +$/, "", fmt)
        size = ""
        if (!(fmt in seenfmt)) { seenfmt[fmt] = 1; nf++ }
        next
    }
    /^[ \t]*Size:/ {
        if ($2 == "Stepwise") size = $3 " ~ " $5      # Stepwise 0x0 - 4096x2160 with step 16
        else                  size = $3               # Discrete 1280x720
        next
    }
    /^[ \t]*Interval:/ {
        if (fmt == "" || size == "") next
        key = fmt "|" size
        if (!(key in cfmt)) {
            cfmt[key] = fmt; csize[key] = size
            order[n++] = key; flist[key] = ","        # 前后带逗号，方便查重
        }
        f = fps_of($0)
        if (f > 0 && index(flist[key], "," f ",") == 0)
            flist[key] = flist[key] f ","
        next
    }
    END {
        if (n == 0) { print "（这个设备没有报告任何格式）"; exit }

        wf = 6; ws = 6
        for (i = 0; i < n; i++) {
            k = order[i]
            if (length(cfmt[k])  > wf) wf = length(cfmt[k])
            if (length(csize[k]) > ws) ws = length(csize[k])
        }

        printf "%-*s  %-*s  %s\n", wf, "格式", ws, "分辨率", "帧率(fps)"
        printf "%s  %s  %s\n", dash(wf), dash(ws), dash(26)

        nfps = 0
        for (i = 0; i < n; i++) {
            k = order[i]
            s = flist[k]; gsub(/^,|,$/, "", s)
            nfps += split(s, tmp, ",")
            gsub(/,/, ", ", s)
            printf "%-*s  %-*s  %s\n", wf, cfmt[k], ws, csize[k], s
        }

        printf "\n共 %d 种格式，%d 组「格式×分辨率」，%d 条「格式×分辨率×帧率」\n", nf, n, nfps
    }
'

# ---------- 当前配置是否被支持 ----------
CURW=$(echo "$INI" | sed -n 's/^width=//p')
CURH=$(echo "$INI" | sed -n 's/^height=//p')
CURF=$(echo "$INI" | sed -n 's/^fps=//p')

if [ "$IS_CAPTURE" = "1" ] && [ -n "$CURW" ] && [ -n "$CURH" ]; then
    echo ""
    echo "----------------------------------------------"
    echo "当前配置（/etc/dvr/settings.ini）：${CURW}x${CURH} @${CURF:-?}fps"

    # ★ 帧率是【按格式】上报的，不能按尺寸合并：
    #   1920x1080 在 MJPG 下是 30fps，在 YUYV 下只有 5fps。
    #   程序固定用 MJPEG（capture.h 的 CAP_FMT），所以判定只看 MJPG。
    AVAIL=$(echo "$FMT" | awk -v w="$CURW" -v h="$CURH" '
        /^[ \t]*\[[0-9]+\]:/ { split($0, a, "\047"); f = a[2]; gsub(/^ +| +$/, "", f); next }
        /^[ \t]*Size:/ { insize = ($3 == w "x" h); next }
        insize && /fps/ && match($0, /\([0-9.]+ fps\)/) {
            v = substr($0, RSTART + 1, RLENGTH - 6) + 0
            print f, int(v + 0.5)
        }' | sort -u)

    if [ -z "$AVAIL" ]; then
        echo "  ⚠️  没有任何格式支持这个「宽x高」——"
        echo "      摄像头协商时会换成最接近的值，程序启动后会把实际生效值写回配置文件"
        echo "      （见 src/device/v4l2/capture.c 的 capture_negotiate）"
        echo ""
        exit 0
    fi

    echo "  支持这个分辨率，各格式上报的帧率："
    echo "$AVAIL" | awk '{ printf "    %-6s %sfps\n", $1, $2 }'

    MJPG_FPS=$(echo "$AVAIL" | awk '$1 == "MJPG" { printf "%s ", $2 }')
    MJPG_FPS=${MJPG_FPS% }          # 去掉末尾空格

    if [ -z "$MJPG_FPS" ]; then
        echo "  ⚠️  MJPG 不支持这个分辨率，而程序固定用 MJPEG —— 协商会换尺寸"
    elif [ -z "$CURF" ]; then
        echo "  ℹ️  配置里没写 fps（程序会用默认 30 去协商）"
    elif echo " $MJPG_FPS " | grep -q " $CURF "; then
        echo "  ✅  程序用 MJPG，${CURF}fps 在其中"
    else
        echo "  ⚠️  程序用 MJPG，但 ${CURF}fps 不在 MJPG 的 [$MJPG_FPS] 里 ——"
        echo "      S_PARM 会失败，实际帧率由驱动决定，程序启动后会把实际值写回配置文件"
    fi
fi

echo ""
