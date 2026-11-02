#!/bin/bash
# =====================================================================
#  gate.sh —— 提交 / 交付前的可判定门禁
# =====================================================================
#  位置：scripts/gate.sh
#  规则来源：AGENT.md 二.8「能用 CI/脚本检查的尽量自动化……规范尽量可判定」
#
#  【它只收"能做到确定性判定"的检查项】
#    判不了的（注释写得对不对、摘要写得清不清楚）一律不塞进来。
#    塞进来只会被 --no-verify 绕过，等于没有。
#
#  用法：
#      bash scripts/gate.sh --staged    # 只看暂存区（pre-commit 钩子调它）
#      bash scripts/gate.sh --all       # 全树（已跟踪文件）检查，手工自查用
#      bash scripts/gate.sh --full      # 全树 + 编译 + Doxygen 告警统计
#
#  退出码：0 全过；1 有失败项
# =====================================================================

# 【不开 set -e】grep 没命中返回 1，会把脚本静默挂掉；
# 本项目在 build.sh 上已经踩过一次同类坑（见 build.sh 里 `|| true` 的注释）。
set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ" || { echo "❌ 进不去工程根 $PROJ"; exit 1; }

MODE="staged"
DO_BUILD=0
DO_DOXY=0
case "${1:-}" in
    --staged) MODE="staged" ;;
    --all)    MODE="all" ;;
    --full)   MODE="all"; DO_BUILD=1; DO_DOXY=1 ;;
    "")       MODE="staged" ;;
    *)
        echo "用法：bash scripts/gate.sh --staged | --all | --full"
        exit 2
        ;;
esac

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "❌ 当前目录不是 git 仓库：$PROJ"
    exit 1
fi

FAIL=0
BAD=()

fail() {
    FAIL=$((FAIL + 1))
    BAD+=("$1")
    echo "  ❌ $1"
}

echo "=============================================="
echo "  门禁 gate.sh   模式: $MODE"
echo "  工程: $PROJ"
echo "=============================================="

# ---- 取文件清单 ----
if [ "$MODE" = "staged" ]; then
    FILES=$(git diff --cached --name-only --diff-filter=ACMR)
    ADDED=$(git diff --cached --name-only --diff-filter=A)
else
    FILES=$(git ls-files)
    ADDED=""
fi

if [ -z "$FILES" ]; then
    echo "  暂存区没有文件，跳过（MODE=$MODE）"
    [ "$MODE" = "staged" ] && exit 0
fi

# ---- 1. 禁止入库的路径 ----
echo "[1/7] 禁止入库路径（生成物 / 备份 / 外部资料）"
for f in $FILES; do
    case "$f" in
        build/*|备份/*|参考/*|third_party/*|docs/doxygen/*)
            fail "不该入库：$f（见 .gitignore 的取舍说明）" ;;
        */__pycache__/*|*.pyc)
            fail "不该入库：$f（Python 缓存）" ;;
        *.o|*.a|*.so|*.raw|*.log)
            fail "不该入库：$f（编译产物 / 运行产物）" ;;
    esac
done

# ---- 2. 单个文件体积 ----
echo "[2/7] 单文件体积上限 2MB"
for f in $FILES; do
    SZ=$(git cat-file -s ":$f" 2>/dev/null || echo 0)
    if [ "$SZ" -gt 2097152 ]; then
        fail "$f 体积 $((SZ / 1024))KB 超过 2MB"
    fi
done

# ---- 3. 二进制内容（仅暂存区模式可判定）----
echo "[3/7] 二进制内容"
if [ "$MODE" = "staged" ]; then
    BIN=$(git diff --cached --numstat | awk -F'\t' '$1 == "-" { print $3 }')
    for f in $BIN; do
        case "$f" in
            *.png|*.jpg|*.jpeg|*.ttf|*.otf|*.woff|*.woff2)
                echo "  · 允许的二进制资源：$f" ;;
            *)
                fail "二进制文件入库：$f（源码仓库只收文本）" ;;
        esac
    done
else
    echo "  · 全树模式不判定（git 不记录工作区二进制标记）"
fi

# ---- 4. shell 语法 ----
echo "[4/7] shell 语法（bash -n）"
for f in $FILES; do
    case "$f" in
        *.sh|*/githooks/pre-commit|*/githooks/commit-msg)
            [ -f "$f" ] || continue
            bash -n "$f" 2>/dev/null || fail "$f 语法错误（bash -n 不通过）" ;;
    esac
done

# ---- 5. Python 语法 ----
echo "[5/7] Python 语法（ast.parse）"
for f in $FILES; do
    case "$f" in
        *.py)
            [ -f "$f" ] || continue
            python3 -c 'import ast,sys; ast.parse(open(sys.argv[1],encoding="utf-8").read())' "$f" 2>/dev/null \
                || fail "$f 语法错误（ast.parse 不通过）" ;;
    esac
done

# ---- 6. work.pro 与源码一致性 ----
echo "[6/7] work.pro 与源码一致性"
NEED_PRO=0
for f in $FILES; do
    case "$f" in
        src/*|include/*|work.pro) NEED_PRO=1 ;;
    esac
done
if [ "$NEED_PRO" = "1" ]; then
    if [ -f scripts/update_pro.py ]; then
        if ! python3 scripts/update_pro.py --check >/tmp/gate_pro.txt 2>&1; then
            fail "work.pro 与 src/ include/ 不同步 —— 跑 python3 scripts/update_pro.py"
            sed 's/^/     /' /tmp/gate_pro.txt | head -20
        fi
    else
        fail "缺 scripts/update_pro.py，无法校验 work.pro"
    fi
else
    echo "  · 本次未改动 src/ include/ work.pro，跳过"
fi

# ---- 7. 新增源文件必须有 Doxygen 注释 ----
#  【为什么只查 src/ 与 include/】
#    这两个目录是交付代码。tests/ 是隔离验证程序（各自独立 main，不链接主程序），
#    改它们不影响产品行为，AGENT.md 二.1 也把「测试用代码」与生产代码区别对待。
#    对 tests/ 硬拦只会让人为了过门禁去补空注释，那种注释比没有更糟。
#    这条范围写在这里，是要能一眼看出边界在哪、为什么划在这。
echo "[7/7] 新增源文件的 Doxygen 注释（限 src/ 与 include/）"
for f in $ADDED; do
    case "$f" in
        src/*|include/*)
            case "$f" in
                *.c|*.cpp|*.h)
                    [ -f "$f" ] || continue
                    head -40 "$f" | grep -qE '@brief|/\*\*' \
                        || fail "$f 前 40 行没有 @brief 或 /** 文档块（AGENT.md 二.2）" ;;
            esac ;;
    esac
done

# ---- 可选：编译 ----
if [ "$DO_BUILD" = "1" ]; then
    echo "[+] 编译（bash scripts/build.sh）"
    if ! bash scripts/build.sh >/tmp/gate_build.txt 2>&1; then
        fail "编译失败，日志：/tmp/gate_build.txt"
        tail -20 /tmp/gate_build.txt | sed 's/^/     /'
    else
        tail -3 /tmp/gate_build.txt | sed 's/^/  /'
    fi
fi

# ---- 可选：Doxygen 告警统计 ----
if [ "$DO_DOXY" = "1" ]; then
    echo "[+] Doxygen 告警统计"
    if ! command -v doxygen >/dev/null 2>&1; then
        echo "  · 本机没有 doxygen，跳过"
    elif [ ! -f Doxyfile ]; then
        fail "缺 Doxyfile"
    else
        ( cd "$PROJ" && doxygen Doxyfile >/tmp/gate_doxy.txt 2>&1 ) || true
        WARN=$(grep -ciE 'warning:' /tmp/gate_doxy.txt || true)
        echo "  · doxygen 告警 $WARN 条（日志 /tmp/gate_doxy.txt）"
        [ "$WARN" -gt 0 ] && fail "Doxygen 有 $WARN 条告警"
    fi
fi

# ---- 汇总 ----
echo "----------------------------------------------"
if [ "$FAIL" -gt 0 ]; then
    echo "  ❌ 门禁未通过：$FAIL 项"
    for b in "${BAD[@]}"; do
        echo "     - $b"
    done
    echo "  修完重跑：bash scripts/gate.sh --all"
    exit 1
fi
echo "  ✅ 门禁全过"
exit 0
