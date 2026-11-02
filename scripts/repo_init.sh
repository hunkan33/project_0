#!/bin/bash
# =====================================================================
#  repo_init.sh —— 初始化 / 校准本仓库（幂等，可重复跑）
# =====================================================================
#  位置：scripts/repo_init.sh
#  用法：bash scripts/repo_init.sh
#
#  【它做四件事，都只动 .git 的配置和权限，不动源码】
#    1. 没有仓库就 git init，并把默认分支定为 main
#       （git 2.25 没有 `git init -b`，只能用符号引用改名，见下）
#    2. 把 core.hooksPath 指向 scripts/githooks
#       ★ 为什么不拷进 .git/hooks：钩子是仓库的一部分，要能随仓库一起
#         提交、评审、回滚。拷进去的那份不在版本控制里，谁改了什么没人知道。
#    3. 给两个钩子补可执行位（git 记录的权限位可能被 Windows 侧丢掉）
#    4. 自检 .gitignore 是否按预期挡住生成物 —— 挡不住的当场报出来
# =====================================================================

set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ" || { echo "❌ 进不去工程根 $PROJ"; exit 1; }

echo "=============================================="
echo "  仓库初始化 / 校准"
echo "  工程: $PROJ"
echo "=============================================="

# ---- 1. 仓库 ----
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "  · 已是 git 仓库"
else
    git init >/dev/null || { echo "❌ git init 失败"; exit 1; }
    echo "  · 已 git init"
fi

# 默认分支统一 main（git 2.25 的 init 只会建 master）
#
# ★ 判定"有没有提交"只能用 `git rev-parse --verify -q HEAD`：
#   空仓库里裸的 `git rev-parse HEAD` 会把 "HEAD" 打到 stdout 再报错，
#   于是 [ -z "$(...)" ] 判成"有提交"，分支永远改不过来。
#   2026-11-02 第一版就是这么写的，跑出来停在 master。
CUR_BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null || echo "")
if [ "$CUR_BRANCH" != "main" ]; then
    if [ -z "$(git rev-parse --verify -q HEAD 2>/dev/null)" ]; then
        git symbolic-ref HEAD refs/heads/main
        echo "  · 默认分支设为 main（尚无提交）"
    else
        echo "  · 当前分支 $CUR_BRANCH，已有提交，不改名"
    fi
fi

# ---- 2. 钩子路径 ----
if [ ! -d scripts/githooks ]; then
    echo "❌ 缺 scripts/githooks/ —— 仓库被裁剪过？"
    exit 1
fi
git config core.hooksPath scripts/githooks
echo "  · core.hooksPath = $(git config core.hooksPath)"

# ---- 3. 钩子可执行位 ----
for h in scripts/githooks/commit-msg scripts/githooks/pre-commit; do
    if [ -f "$h" ]; then
        chmod +x "$h"
        git update-index --chmod=+x "$h" 2>/dev/null || true
        echo "  · 可执行位 +x：$h"
    else
        echo "❌ 缺钩子：$h"
        exit 1
    fi
done

# ---- 4. 身份 ----
NAME=$(git config user.name 2>/dev/null || true)
MAIL=$(git config user.email 2>/dev/null || true)
if [ -z "$NAME" ] || [ -z "$MAIL" ]; then
    echo ""
    echo "  ⚠️  尚未配置提交身份（提交会被 git 拒绝）"
    echo "      设成你自己的 GitHub 账号，否则远端不认作者："
    echo "      git config user.name  \"你的名字\""
    echo "      git config user.email \"你的邮箱\""
    echo ""
else
    echo "  · 提交身份：$NAME <$MAIL>"
fi

# ---- 5. 忽略规则自检 ----
echo "  忽略规则自检（应当全部被忽略）:"
IGNORE_FAIL=0
for p in build/ 备份/ 参考/ third_party/ docs/doxygen/ scripts/__pycache__/; do
    if git check-ignore -q "$p" 2>/dev/null; then
        echo "    ✓ $p"
    else
        echo "    ✗ $p 没被忽略 —— .gitignore 漏了"
        IGNORE_FAIL=1
    fi
done

echo "----------------------------------------------"
[ "$IGNORE_FAIL" = "1" ] && echo "  ⚠️  忽略规则有缺口，先补 .gitignore 再提交"
echo "  下一步：bash scripts/gate.sh --all    # 提交前自查"
echo "=============================================="
exit 0
