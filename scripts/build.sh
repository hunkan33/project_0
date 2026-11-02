#!/bin/bash
# =====================================================================
#  build.sh —— 编译 work（在 Ubuntu 虚拟机里运行）
# =====================================================================
#  位置：scripts/build.sh
#  用法：
#      bash scripts/build.sh          编译
#      bash scripts/build.sh clean    清理后重编
#
#  产物：build/work
#
#  【它保证什么】
#    1. work.pro 比 Makefile 新 → 自动重跑 qmake（否则新增文件不进编译）
#    2. 编完自检产物新鲜度：只要还有源文件比 build/work 新，就判定"产物不是从当前源码来的"，
#       直接 exit 1 —— 防止旧二进制被 deploy.sh 推上板子（2026-11-01 加）
# =====================================================================

set -e

# 脚本在 scripts/ 下，工程根是上一级
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ"

echo "=============================================="
echo "  编译 work"
echo "  工程: $PROJ"
echo "=============================================="

# ---- 1. 找工具链 ----
TC="${EDG_TOOLCHAIN_ROOT:-}"
if [ -z "$TC" ]; then
    for c in /opt/atk-dlt113is-toolchain /opt/atk; do
        [ -x "$c/bin/qmake" ] && { TC="$c"; break; }
    done
fi
if [ -z "$TC" ]; then
    TC=$(ls -d /opt/*/ 2>/dev/null | while read d; do
             [ -x "${d}bin/qmake" ] && echo "${d%/}" && break
         done)
fi
if [ -z "$TC" ]; then
    echo ""
    echo "❌ 找不到工具链（需要 bin/qmake）"
    echo "   默认路径: /opt/atk-dlt113is-toolchain"
    echo "   或者指定: EDG_TOOLCHAIN_ROOT=/your/path bash scripts/build.sh"
    exit 1
fi
QMAKE="$TC/bin/qmake"
echo "    工具链: $TC"

# ---- 2. 检查工程文件 ----
if [ ! -f work.pro ]; then
    echo "❌ 找不到 work.pro（应该在工程根 $PROJ）"
    exit 1
fi
if [ ! -f third_party/sdk/include/vdecoder.h ] && [ ! -d third_party/sdk/include ]; then
    echo "⚠️  third_party/sdk/include 不存在，SDK 头文件可能缺失"
fi

# ---- 2.5 平台层零 Qt 守卫 ----
# 【为什么查符号，而不是查 #include <Q…>】
#   QString 往往是**经 infra/app_settings.h 间接**带进来的 ——
#   2026-11-01 就是这样漏掉的：capture_thread.cpp 里冒出 QString::fromLatin1，
#   但该文件一个 Qt 头都没 include，include 级检查抓不到。
# 【为什么剔除注释】
#   注释里提到 QString 是合法的（就在记录这个坑），不能因此拦编译。
# 【违规了怎么办】
#   平台层要读配置 → 用 include/app/config/settings_c.h（C 接口，只认 const char*），
#   不要为了图省事把 app_settings.h 拖进来。
#
# 【2026-11-01 目录重构后】零 Qt 的范围从 src/platform/ 变成三处：
#   src/device/  设备层（v4l2 / decoder / display / hal）
#   src/camera/  摄像头控制与能力表（C 实现 + 无 Qt 的 C++ 门面）
#   src/common/  日志（纯 C）
# 头文件已搬到 include/，所以两侧一起查（include/device 等镜像目录）。
# ★ ui_probe.cpp / ui_probe.h 按文件名排除：它是 UI 探针
#   （QApplication::widgetAt / sendEvent），天生依赖 Qt，被目录重构误扫进 device/hal。
ZERO_QT_DIRS="src/device src/camera src/common"
for _d in $ZERO_QT_DIRS; do
    if [ ! -d "$_d" ]; then
        echo "❌ 找不到 $_d/ —— 目录被改名了？零 Qt 守卫必须跟着改"
        exit 1
    fi
done
QT_PAT='\b(QString|QByteArray|QMap|QHash|QVector|QList|QFile|QFileInfo|QDir|QTimer|QObject|QVariant|QStringLiteral|qDebug|qWarning|QApplication)\b'
QT_HITS=$(grep -rnE "$QT_PAT" $ZERO_QT_DIRS \
              include/device include/camera include/common \
              --include='*.c' --include='*.cpp' --include='*.h' 2>/dev/null \
          | grep -v 'ui_probe' \
          | grep -v '/\*' | grep -v '//' | grep -vE ':[0-9]+:[[:space:]]*\*' || true)
# ★ 上面的 `|| true` 不能删：本脚本开了 set -e，而 grep 在"没有命中"时返回 1，
#   命令替换会把 1 传给赋值语句 → 脚本**静默退出**（不报错、不编译）。
#   2026-11-01 第一次装这个守卫时就是这么把自己挂掉的。
if [ -n "$QT_HITS" ]; then
    echo ""
    echo "❌ 平台层（$ZERO_QT_DIRS）出现 Qt 符号 —— 这一层必须零 Qt："
    echo "$QT_HITS" | head -10 | sed 's/^/     /'
    echo "   → 配置读写请用 include/app/config/settings_c.h，不要 include app_settings.h"
    exit 1
fi

# ---- 2.6 表现层不许直连平台层 ----
# 【规则】ui/ 摸摄像头只能走 core/camera_service.h；storage.h 是只读查询，允许。
# 【为什么】UI 一旦直接 include 平台头，"换平台只动 platform/" 就成了空话
#   （2026-11-01 查过：CameraPage 曾直接 include capture/camera_ctl/camera_info，
#    现已全部改走门面，这条守卫就是防止它再长回来）。
# 【2026-11-01】UI 现在是 src/ui（main_page + setting_page）+ src/ui_qt（控件），两处都要查。
PLATFORM_INC=$(grep -rnE '#include[[:space:]]*"(capture|camera_ctl|camera_info|decoder|display|pipeline)\.h"' \
               src/ui src/ui_qt --include='*.cpp' --include='*.h' 2>/dev/null || true)
if [ -n "$PLATFORM_INC" ]; then
    echo ""
    echo "❌ 表现层（src/ui/ src/ui_qt/）直接 include 了平台层头 —— 请走 camera/camera_service.h："
    echo "$PLATFORM_INC" | head -10 | sed 's/^/     /'
    exit 1
fi

# ---- 3. 清理 ----
if [ "$1" = "clean" ] || [ ! -f build/Makefile ]; then
    echo "    清理 build/ ..."
    rm -rf build
fi

# ---- 4. qmake + make ----
mkdir -p build
cd build

# 【重要】改了 work.pro 必须重新 qmake，否则 make 不会看到新的源文件列表。
#
# ★★ 2026-11-01 修：这里原来写的是 `[ work.pro -nt Makefile ]`，但前面已经 `cd build`，
#    `build/work.pro` 根本不存在 → **条件永远为假** → qmake 只在 "build/Makefile 不存在"
#    时才跑。后果：update_pro.py 往 work.pro 里加了新文件，build.sh 却复用旧 Makefile，
#    新文件根本不参与编译，最终在链接期报 undefined reference ——
#    而那条报错完全看不出"少了一个文件"，极难定位。
#    .pro 在上一级，必须写 `../work.pro`。
#    另：去掉了原来的 `2>/dev/null` —— 吞掉 stderr 会掩盖真正的错误（见 AGENT.md G8.2）。
if [ ../work.pro -nt Makefile ] || [ ! -f Makefile ]; then
    echo "    qmake（work.pro 有更新）..."
    "$QMAKE" ../work.pro
    # ★ 强制重链接：qmake 只是重写 Makefile，而 make 看到所有 .o 都比目标新时
    #   可能**完全不动目标**。那样上板的二进制还是用旧的 LIBS / QT 设置链出来的 ——
    #   改了 .pro 里的链接库却毫无效果。删掉目标就必然重链一次。
    rm -f work
fi

echo "    make -j4 ..."
# 用 bear 包装 make：
#   - bear 会记录每个文件的编译命令，生成 build/compile_commands.json
#   - IntelliSense 靠这个文件才能准确解析类型（否则会把 AEButton 当成 QPushButton）
#   - -a 是「追加模式」：增量编译时不会覆盖之前的记录，数据库始终保持完整
if command -v bear >/dev/null 2>&1; then
    if ! bear -a make -j4; then
        echo ""
        echo "❌ 编译失败"
        exit 1
    fi
else
    echo "    ⚠️  没装 bear，跳过 compile_commands.json 生成"
    echo "        （IntelliSense 补全可能不准，apt install bear 可解决）"
    if ! make -j4; then
        echo ""
        echo "❌ 编译失败"
        exit 1
    fi
fi

cd "$PROJ"

# 清理 VS Code 的 IntelliSense 数据库缓存
# 改了头文件（比如给类加方法）后，缓存可能还记着旧定义，
# 导致报"没有成员 xxx"这种假错误。删掉强制重建。
if [ -d .vscode ]; then
    rm -f .vscode/browse.vc.db .vscode/browse.vc.db-shm \
          .vscode/browse.vc.db-wal .vscode/browse.vc.db.lock 2>/dev/null || true
    echo "    🧹 已清理 IntelliSense 缓存"
fi

# ---- 5. 检查产物 ----
if [ ! -f build/work ]; then
    echo "❌ 没有生成 build/work"
    exit 1
fi

# ---- 5.1 ★ 产物新鲜度自检 ----
# 为什么要有这一步：make 只认 mtime，而"改了没生效"在这类项目里代价特别高 ——
#   要打包、要上板、要看日志，最后还要怀疑自己是不是看错了。
# 这里做一个便宜的兜底：**只要有源文件比 build/work 新，就说明产物不是从当前源码编出来的**，
#   直接失败，别让一个旧二进制被 deploy.sh 推上板子。
# （2026-11-01 加：那天出现过"改了代码、编译通过、上板行为不变"，排查代价极大。）
#
# 只比 src/ 下的源文件，**不比 work.pro** —— .pro 被碰过不等于内容过期
# （qmake 已在上一步强制重链接，那种情况会自动被修好）。
# 【2026-11-01】头文件已搬到 include/，原来只 find src 会漏掉改头文件不触发重编的坑。
STALE=$(find src include -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' \) \
        -newer build/work 2>/dev/null | head -5)

if [ -n "$STALE" ]; then
    echo ""
    echo "❌ build/work 比源码旧 —— 这个产物不是从当前源码编出来的，拒绝交付"
    echo "   比 build/work 新的文件（最多列 5 个）："
    echo "$STALE" | sed 's/^/     /'
    echo ""
    echo "   先跑一次全量重编（make 的依赖判定没抓到这种变化）："
    echo "       bash scripts/build.sh clean"
    exit 1
fi

echo ""
echo "=============================================="
FILE_INFO=$(file -b build/work)
case "$FILE_INFO" in
    *ARM*)  echo "  ✅ ARM 程序: build/work ($(du -h build/work | cut -f1))" ;;
    *)      echo "  ⚠️ 不是 ARM 程序，检查工具链" ;;
esac
echo "=============================================="
echo ""
echo "下一步："
echo "    bash scripts/deploy.sh              # 编译+部署+后台运行"
echo "    bash scripts/deploy.sh status       # 进程状态 + 最近日志"
echo "    bash scripts/deploy.sh log          # 实时日志"
echo "    bash scripts/deploy.sh stop         # 停止程序"
echo ""
