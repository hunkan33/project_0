#!/bin/bash
# =====================================================================
#  deploy.sh —— 编译 / 部署 / 后台运行（板子专用开发模式）
# =====================================================================
#  位置：scripts/deploy.sh
#
#  【运行模型】
#    板子专跑本程序，程序后台常驻，不用 weston。
#    改代码 → build+deploy 自动杀旧进程跑新的，不需要重启板子。
#
#  用法：
#      bash scripts/deploy.sh              # 编译 + 传 + 后台启动（默认）
#      bash scripts/deploy.sh run          # 只重启程序
#      bash scripts/deploy.sh stop         # 停止程序
#      bash scripts/deploy.sh restart      # 停止再启动
#      bash scripts/deploy.sh status       # 进程状态 + 最近日志
#      bash scripts/deploy.sh cleanup      # 只清理上次运行残余（杀进程+清日志+清屏）
#      bash scripts/deploy.sh log          # 实时看日志（Ctrl+C 退出）
#      bash scripts/deploy.sh shell        # 登录板子
#      bash scripts/deploy.sh build|push   # 单独执行某步
# =====================================================================

set -e
#切到项目根目录
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ"
#远程登录开发板时使用的用户名等
BOARD_USER="root"
BOARD_DEST="/root"
APP="work"
LOG="/tmp/work.log"

# 板子 IP
BOARD=""
[ -f config/board_ip.txt ] && BOARD=$(tr -d ' \n\r' < config/board_ip.txt)

# ---- 参数解析 ----
case "${1:-}" in
    build|push|run|stop|restart|status|log|shell|cleanup|all)
        ACTION="$1" ;;
    "")
        ACTION="all" ;;
    *)
        BOARD="$1"; ACTION="${2:-all}" ;;
esac

if [ -z "$BOARD" ]; then
    echo "❌ 不知道板子 IP"
    echo "   写进 config/board_ip.txt:   echo 192.168.5.4 > config/board_ip.txt"
    exit 1
fi

echo "=============================================="
echo "  work    板子: $BOARD    动作: $ACTION"
echo "=============================================="

# =====================================================================
#  动作
# =====================================================================

do_cleanup() {
    echo ""
    echo ">>> 清理上次运行残余"
    ssh "$BOARD_USER@$BOARD" "
        # ① 先温柔停止（SIGTERM）—— 让程序执行 decoder_destroy / capture_stop，
        #    Cedar 的 VE 硬件才能正常关闭。直接 kill -9 会让 VE 停在脏状态，
        #    下一个进程 InitializeVideoDecoder 返回成功但永远不出帧（Yvir=nil）。
        for p in $APP de_display video_show decoder_test cvbs_test; do
            if killall -TERM \$p 2>/dev/null; then
                echo \"  TERM → \$p\"
            fi
        done

        # 等它真的退干净再继续（最多 3 秒），而不是死等 2 秒。
        # 为什么必须等：SIGTERM 后程序要执行 decoder_destroy（关 Cedar VE）
        #   + capture_stop（STREAMOFF）+ pthread_join。没退完就继续、或者
        #   直接 kill -9，会让 VE 停在脏状态：下一次 InitializeVideoDecoder
        #   返回成功但永远不出帧（Yvir=nil）。
        #
        # 【必须用 killall -0 而不是 pidof】实测：进程收到 SIGTERM 后进入
        #   退出过程时，pidof 在 100ms 内就返回空，但进程实际要 ~500ms 才
        #   真正消失。用 pidof 判断会导致提前强杀，正好踩上面那个坑。
        #   killall -0 一直能看到它，直到内核真正回收。
        n=0
        while [ \$n -lt 30 ]; do
            killall -0 $APP 2>/dev/null || break
            sleep 0.1
            n=\$((n+1))
        done

        # ② 再强杀（万一 SIGTERM 没退，比如卡在阻塞调用里）
        for p in $APP de_display video_show decoder_test cvbs_test; do
            if killall -9 \$p 2>/dev/null; then
                echo \"  强杀 → \$p\"
            fi
        done

        # ② 清掉旧日志（避免混淆本次运行的输出）
        rm -f $LOG

        # ③ 清屏（保持视频区干净，DE/UI 层不会残留上一帧）
        dd if=/dev/zero of=/dev/fb0 bs=4096 count=600 2>/dev/null

        # ④ 摄像头状态检查：如果设备还在但 STREAMON 残留，提示用 reset
        if [ -e /dev/video0 ]; then
            echo '  摄像头节点存在'
        else
            echo '  ⚠️ /dev/video0 不存在，可能需要拔插摄像头或 bash tests/video_show/reset_cam.sh'
        fi
        echo '  清理完成'
    "
}

do_build() {
    echo ""
    echo ">>> 编译"
    bash "$PROJ/scripts/build.sh"
}

do_push() {
    echo ""
    echo ">>> 传到板子"
    if [ ! -f "build/$APP" ]; then
        echo "❌ 没有 build/$APP，请先 bash scripts/build.sh"
        exit 1
    fi
    # 程序在跑时文件被占用，先杀掉
    ssh "$BOARD_USER@$BOARD" "killall -9 $APP 2>/dev/null; rm -f $BOARD_DEST/$APP" 2>/dev/null || true
    scp -q "build/$APP" "$BOARD_USER@$BOARD:$BOARD_DEST/"
    echo "    ✅ 传完了"

    # ---- camera.ini 种子 ----
    # 能力表是运行时生成的，板上没有就推一份种子（指纹留空）。
    # 程序首次启动会枚举并覆盖它 —— 所以推的是格式说明，不是权威数据。
    if ! ssh "$BOARD_USER@$BOARD" "[ -f /etc/dvr/camera.ini ]" 2>/dev/null; then
        if [ -f "config/camera.ini" ]; then
            ssh "$BOARD_USER@$BOARD" "mkdir -p /etc/dvr"
            scp -q "config/camera.ini" "$BOARD_USER@$BOARD:/etc/dvr/camera.ini"
            echo "    ✅ 已推送 camera.ini 种子（首次启动会重新枚举覆盖）"
        fi
    fi

    # ---- 字体检查：思源黑体 CN（2026-10-30 起全工程唯一字体）----
    # 【2026-11-01 更正】这里原来检查 zpix.ttf 并自动补推 —— 那是像素字体时代的逻辑。
    #   10-30 起字体统一到思源黑体 CN 27px（`main.cpp` 一处设定），全工程已无 Zpix 使用者，
    #   再推 zpix.ttf 是**无效操作**（推上去也没人用）。
    # 【为什么仍然要检查】字体丢了程序**不会报错**，会静默回落到别的字体：
    #   功能正常，但 27px 的汉字宽度变了，三页的标签列宽（280）和按钮文字会跟着变。
    # 【为什么只告警不补推】工程里没有思源字体文件（third_party/fonts 只有历史遗留的 zpix.ttf），
    #   没有可推的源。要补只能从 rootfs 或自行下载。
    if ! ssh "$BOARD_USER@$BOARD" "[ -f /usr/share/fonts/otf/SourceHanSansCN-Regular.otf ]" 2>/dev/null; then
        echo "    ⚠️  板端缺思源黑体（/usr/share/fonts/otf/SourceHanSansCN-Regular.otf）"
        echo "       界面会回落到其它字体：功能不受影响，但字号与行宽全变"
    fi
}

do_run() {
    do_cleanup
    echo ""
    echo ">>> 后台启动"
    ssh "$BOARD_USER@$BOARD" "
        # 杀旧进程（-9 保证杀掉）
        killall -9 $APP 2>/dev/null

        # 【必须】杀掉出厂 UI，否则它会和我们的程序抢 fb0，
        # 屏幕上会出现两套界面叠在一起（比如多出一个时间和日期）
        killall -9 systemui 2>/dev/null
        killall -9 weston   2>/dev/null

        # 等这两个真的退出（最多 2 秒）。同样用 killall -0 而不是 pidof，
        # 理由见上面 cleanup 里那段注释。
        n=0
        while [ \$n -lt 20 ]; do
            if ! killall -0 systemui 2>/dev/null && ! killall -0 weston 2>/dev/null; then
                break
            fi
            sleep 0.1
            n=\$((n+1))
        done

        # 清屏：让中间视频区保持 alpha=0，DE 视频层才能透出
        dd if=/dev/zero of=/dev/fb0 bs=4096 count=600 2>/dev/null

        cd $BOARD_DEST
        # nohup + 重定向 stdin/stdout，让程序脱离 SSH 会话常驻
        # QT_QPA_FB_FORCE_FULLSCREEN=0 是必须的（注意：整段 ssh 命令是双引号字符串，
        # 这里不能出现 ASCII 双引号，否则会把引号配平打断）。
        # Qt 的 linuxfb 插件会把第一个 show() 的窗口强制拉成全屏
        # （qfbwindow.cpp: forceFullScreen = windowCount()==0）。
        # 我们第一个 show 的是 UpWindow（本该 1024x48），被拉成 1024x600 后，
        # 它的 WA_StyledBackground + qssPanel() 会把整屏刷成顶栏底色，
        # DE 视频层被完全盖住 —— 表现为主界面没有视频。
        nohup env \
            XDG_RUNTIME_DIR=/dev/shm \
            QT_QPA_PLATFORM=linuxfb \
            QT_QPA_FB_FORCE_FULLSCREEN=0 \
            QT_QPA_FONTDIR=/usr/share/fonts/otf \
            ./$APP > $LOG 2>&1 < /dev/null &

        # 等进程起来再报 PID（最多 5 秒），而不是死等 2 秒
        n=0
        while [ \$n -lt 50 ]; do
            pidof $APP >/dev/null 2>&1 && break
            sleep 0.1
            n=\$((n+1))
        done

        if pidof $APP > /dev/null 2>&1; then
            echo \"  ✅ 已后台运行  PID=\$(pidof $APP)\"
            echo '     看屏幕；看日志: bash scripts/deploy.sh log'
        else
            echo '  ❌ 启动失败，最近日志：'
            tail -8 $LOG 2>/dev/null || echo '     (无日志)'
            exit 1
        fi
    "
}

do_stop() {
    echo ""
    echo ">>> 停止"
    ssh "$BOARD_USER@$BOARD" "
        if ! killall -TERM $APP 2>/dev/null; then
            echo '  没有在运行'
        else
            sleep 2
            if killall -0 $APP 2>/dev/null; then
                killall -9 $APP 2>/dev/null
                echo '  ⚠️ TERM 未退出，已强杀'
            else
                echo '  ✅ 已正常停止（VE 已干净关闭）'
            fi
        fi
    "
}

do_restart() {
    do_stop
    sleep 1
    do_run
}

do_status() {
    echo ""
    ssh "$BOARD_USER@$BOARD" "
        echo -n '  进程: '
        P=\$(pidof $APP 2>/dev/null)
        if [ -n \"\$P\" ]; then echo \"\$P\"; else echo '未运行'; fi
        echo ''
        echo '  --- 最近 10 行日志 ---'
        tail -10 $LOG 2>/dev/null || echo '  (无日志)'
    "
}

do_log() {
    echo ""
    echo ">>> 实时日志（Ctrl+C 退出）"
    echo ""
    ssh -t "$BOARD_USER@$BOARD" "tail -f $LOG 2>/dev/null || echo '没有日志文件，程序可能没启动过'"
}

do_shell() {
    echo ""
    ssh -t "$BOARD_USER@$BOARD" "exec bash"
}

# ---- 分发 ----
case "$ACTION" in
    build)   do_build ;;
    push)    do_push ;;
    run)     do_run ;;
    stop)    do_stop ;;
    cleanup) do_cleanup ;;
    restart) do_restart ;;
    status)  do_status ;;
    log)     do_log ;;
    shell)   do_shell ;;
    all)
        do_build
        do_push
        do_run
        ;;
    *)
        echo "未知动作: $ACTION"
        echo "可选: all|build|push|run|stop|restart|status|log|shell|cleanup"
        exit 1
        ;;
esac
