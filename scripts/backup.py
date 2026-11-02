#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""backup.py —— 改文件之前先备份

用法（在工程根目录或任意位置都行）：
    python3 scripts/backup.py src/ui/setting_page/SettingsPage.cpp   # 备份一个或多个文件
    python3 scripts/backup.py --tag 1027a src/app/controller/app_controller.cpp  # 自定义标签
    python3 scripts/backup.py --all                # 备份所有"改过但还没备份"的文件
    python3 scripts/backup.py --status             # 看哪些文件改过没备份（只读）
    python3 scripts/backup.py --list [--day 1027]  # 看已有的备份（只读）
    python3 scripts/backup.py --restore 备份/src/app/controller/app_controller.cpp.bak_1027f
    python3 scripts/backup.py --dry-run ...        # 只打印，不写盘

【为什么需要它】
  这个工程有一条硬规矩：**改任何文件之前先备份，备份一律放 备份/，源码树里不留 .bak**。
  靠人手动 cp 有两个必然的失效点：
    1. 忘了改名字，第二次备份把第一次覆盖掉 —— 想回滚时发现只剩"改了一半"的版本
    2. 忘了放对目录，.bak 散落在 src/ 里，编译报错时被误当成源文件
  脚本把这两件事变成不可能：目标名冲突就自动换字母，路径一律按 备份/<相对路径> 展开。

【命名规则（跟历史备份保持一致）】
    备份/src/app/app_controller.cpp.bak_1027      ← 当天第一次
    备份/src/app/app_controller.cpp.bak_1027b     ← 当天第二次（跳过 a，和已有历史对齐）
    备份/src/app/app_controller.cpp.bak_1027c     ← 第三次
    … z 之后是 aa / ab / ac …
  同一天同一文件永远不覆盖，只往后加字母。

【--status 是干什么的】
  "改之前要备份"这条规矩最容易破的地方是**忘了**。--status 会比对
  "源文件的 mtime" 和 "该文件最新备份的 mtime"，列出所有"改过但没备份"的文件。
  建议在 build.sh 开头挂一次（见文件末尾的说明），忘了就会在编译时被提醒。
"""

import os
import shutil
import sys
import time

# ------------------------------------------------------------------ 路径约定
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BACKUP_DIR = os.path.join(PROJ, "备份")

# --status 的扫描范围：会改的东西都在这
# 【2026-11-01】头文件已搬到工程根的 include/，必须一起纳入监视，
# 否则 --status 里"改了头文件却显示没动过"。
WATCH_DIRS = ("src", "include", "scripts", "docs", "config")
WATCH_FILES = ("work.pro",)

# 备份文件名的合法形态：<原名>.bak_<tag><后缀>
BAK_MARK = ".bak_"


# ------------------------------------------------------------------ 小工具
def rel_to_proj(path):
    """绝对路径 → 相对工程根的 posix 路径；不在工程内返回 None"""
    ap = os.path.abspath(path)
    try:
        rel = os.path.relpath(ap, PROJ)
    except ValueError:          # Windows 跨盘符
        return None
    if rel.startswith(".."):
        return None
    return rel.replace(os.sep, "/")


def suffixes():
    """备份名后缀的候选序列：'' / b..z / aa.."""
    yield ""
    for c in "bcdefghijklmnopqrstuvwxyz":
        yield c
    for c1 in "abcdefghijklmnopqrstuvwxyz":
        for c2 in "abcdefghijklmnopqrstuvwxyz":
            yield c1 + c2


def today_tag():
    return time.strftime("%m%d")


def find_backups(rel):
    """给定源文件相对路径，返回它在 备份/ 下的所有备份（绝对路径列表）"""
    base = os.path.join(BACKUP_DIR, rel.replace("/", os.sep))
    folder = os.path.dirname(base)
    name = os.path.basename(base)
    if not os.path.isdir(folder):
        return []
    out = []
    for fn in os.listdir(folder):
        if fn.startswith(name + BAK_MARK):
            out.append(os.path.join(folder, fn))
    return out


def latest_backup(rel):
    """该文件最新的备份路径；没有返回 None"""
    baks = find_backups(rel)
    if not baks:
        return None
    return max(baks, key=lambda p: os.path.getmtime(p))


def backups_of_same_name(rel):
    """文件名相同但目录不同的备份 —— 说明这个文件被移动/改名过。

    没有这一步的话，`git mv` 式的重构之后 --status 会把一堆老文件报成
    "从未备份"，看起来像违规，其实是历史备份留在了旧路径下。
    """
    name = os.path.basename(rel)
    hits = []
    if not os.path.isdir(BACKUP_DIR):
        return hits
    for root, _dirs, filenames in os.walk(BACKUP_DIR):
        for fn in filenames:
            if fn.startswith(name + BAK_MARK):
                hits.append(os.path.join(root, fn))
    return hits


def human_size(n):
    for unit in ("B", "KB", "MB"):
        if n < 1024 or unit == "MB":
            return "%.1f %s" % (n, unit) if unit != "B" else "%d B" % n
        n /= 1024.0


# ------------------------------------------------------------------ 备份
def backup_one(path, tag, dry=False, quiet=False):
    """备份一个文件，返回 (备份路径 or None, 错误信息 or None)"""
    src = os.path.abspath(path)

    if not os.path.isfile(src):
        return None, "文件不存在：%s" % path

    rel = rel_to_proj(src)
    if rel is None:
        return None, "不在工程目录内，拒绝备份：%s" % path
    if rel.startswith("备份/"):
        return None, "这是备份目录里的文件，拒绝再备份：%s" % rel

    base = os.path.join(BACKUP_DIR, rel.replace("/", os.sep))
    dest_dir = os.path.dirname(base)
    if not dry:
        os.makedirs(dest_dir, exist_ok=True)

    for suf in suffixes():
        dest = "%s%s%s%s" % (base, BAK_MARK, tag, suf)
        if os.path.exists(dest):
            continue                       # 已存在就换下一个字母，绝不覆盖
        if not dry:
            shutil.copy2(src, dest)        # copy2 保留 mtime，回滚时时间戳也一致
        if not quiet:
            print("  备份 → %s  (%s)" % (rel_to_proj(dest) or dest,
                                        human_size(os.path.getsize(src))))
        return dest, None

    return None, "同一天的字母后缀用完了（>700 个），请用 --tag 换个标签"


def cmd_backup(paths, tag, dry):
    if not paths:
        sys.exit("ERROR: 没给要备份的文件。用 --all 备份所有改过的，或 --status 先看看。")

    print("【备份】标签 %s%s" % (tag, "（dry-run，不写盘）" if dry else ""))
    ok, bad = 0, 0
    for p in paths:
        _dest, err = backup_one(p, tag, dry=dry)
        if err:
            print("  ✗ %s" % err)
            bad += 1
        else:
            ok += 1
    print("完成：成功 %d 个，失败 %d 个" % (ok, bad))
    return 0 if bad == 0 else 2


# ------------------------------------------------------------------ 状态
def collect_watch_files():
    """收集需要关注的源文件（相对路径 posix）"""
    files = []
    for d in WATCH_DIRS:
        root_dir = os.path.join(PROJ, d)
        if not os.path.isdir(root_dir):
            continue
        for root, dirnames, filenames in os.walk(root_dir):
            dirnames[:] = [x for x in dirnames if x not in (".git", "build", "__pycache__")]
            for fn in filenames:
                full = os.path.join(root, fn)
                rel = rel_to_proj(full)
                if rel and not rel.startswith("备份/"):
                    files.append(rel)
    for fn in WATCH_FILES:
        if os.path.isfile(os.path.join(PROJ, fn)):
            files.append(fn)
    return sorted(files)


def cmd_status(verbose=False):
    """列出"从未备份"和"改过未备份"的文件"""
    files = collect_watch_files()
    never, moved, dirty, clean = [], [], [], 0

    for rel in files:
        src = os.path.join(PROJ, rel.replace("/", os.sep))
        bak = latest_backup(rel)
        if bak is None:
            # 同名但不同目录 = 这个文件被移动/改名过，历史备份还在旧路径
            old = backups_of_same_name(rel)
            if old:
                moved.append((rel, max(old, key=lambda p: os.path.getmtime(p))))
            else:
                never.append(rel)
            continue
        if os.path.getmtime(src) > os.path.getmtime(bak) + 0.001:
            dirty.append((rel, bak))
        else:
            clean += 1

    print("【备份状态】共 %d 个文件：已备份 %d，改过未备份 %d，从未备份 %d，换过路径 %d"
          % (len(files), clean, len(dirty), len(never), len(moved)))

    if dirty:
        print("\n⚠️  改过但还没备份（改之前应该先备份）：")
        for rel, bak in dirty:
            dt = time.strftime("%m-%d %H:%M", time.localtime(os.path.getmtime(bak)))
            print("    %-52s 最新备份 %s" % (rel, dt))
        print("\n  一次性备份它们： python3 scripts/backup.py --all")
        print("  注意：--all 备份的是**现在**的内容，如果已经改坏了，先 git/手工回滚再备份。")

    if moved and verbose:
        print("\nℹ️  换过路径（历史备份在旧目录下，不算没备份）：")
        for rel, bak in moved:
            print("    %-40s ← %s" % (rel, rel_to_proj(bak)))

    if never and verbose:
        print("\nℹ️  从未备份过（新文件，一般不用管）：")
        for rel in never:
            print("    %s" % rel)

    # 有未备份的改动 → 退出码 1，可挂到 build.sh 里做提醒
    return 1 if dirty else 0


def cmd_all(tag, dry):
    """把所有"改过未备份"的文件批量备份"""
    files = collect_watch_files()
    todo = []
    for rel in files:
        src = os.path.join(PROJ, rel.replace("/", os.sep))
        bak = latest_backup(rel)
        if bak is None:
            continue                       # 从没备份过的（新文件）不掺和
        if os.path.getmtime(src) > os.path.getmtime(bak) + 0.001:
            todo.append(src)

    if not todo:
        print("【备份】没有需要备份的改动。")
        return 0

    print("【备份】%d 个文件改过未备份%s" % (len(todo), "（dry-run）" if dry else ""))
    return cmd_backup(todo, tag, dry)


# ------------------------------------------------------------------ 列表
def cmd_list(day=None):
    if not os.path.isdir(BACKUP_DIR):
        print("还没有任何备份（%s 不存在）" % BACKUP_DIR)
        return 0

    rows = []
    plain = []
    for root, _dirs, filenames in os.walk(BACKUP_DIR):
        for fn in filenames:
            full = os.path.join(root, fn)
            if BAK_MARK not in fn:
                # 无标签的 .bak（如 update_pro.py 写的 work.pro.bak）：
                # 不参与 mtime 对比，也不列进主表，但要让用户知道它存在。
                if fn.endswith(".bak"):
                    plain.append(full)
                continue
            if day:
                tag = fn.rsplit(BAK_MARK, 1)[1]
                if not tag.startswith(day):
                    continue
            rows.append(full)

    if not rows:
        print("没有匹配的带标签备份%s" % ("（day=%s）" % day if day else ""))
        if plain:
            print("（另有 %d 个无标签 .bak）" % len(plain))
        return 0

    rows.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    print("共 %d 个备份（按内容时间倒序）" % len(rows))
    print("注：时间是该版本文件本身的 mtime（copy2 会保留），不是备份动作发生的时间。\n")
    for p in rows:
        rel = rel_to_proj(p) or p
        # 从备份名反推源文件：去掉 备份/ 前缀，再去掉 ".bak_xxx" 尾巴
        inner = rel[len("备份/"):] if rel.startswith("备份/") else rel
        src_rel = inner.split(BAK_MARK, 1)[0]
        src = os.path.join(PROJ, src_rel.replace("/", os.sep))
        mark = "" if os.path.isfile(src) else "   [源文件已不存在]"
        print("  %s  %8s  %s%s"
              % (time.strftime("%m-%d %H:%M", time.localtime(os.path.getmtime(p))),
                 human_size(os.path.getsize(p)), rel, mark))

    if plain:
        print("\n另有 %d 个无标签 .bak（update_pro.py 之类写的，不参与 --status 比对）："
              % len(plain))
        for p in sorted(plain, key=os.path.getmtime, reverse=True)[:5]:
            print("  %s" % (rel_to_proj(p) or p))
    return 0


# ------------------------------------------------------------------ 还原
def cmd_restore(bak_path, dry):
    ap = os.path.abspath(bak_path)
    if not os.path.isfile(ap):
        sys.exit("ERROR: 找不到备份文件：%s" % bak_path)

    rel_bak = rel_to_proj(ap)
    if rel_bak is None or not rel_bak.startswith("备份/"):
        sys.exit("ERROR: 只能还原 备份/ 目录里的文件：%s" % bak_path)

    inner = rel_bak[len("备份/"):]
    if BAK_MARK not in inner:
        sys.exit("ERROR: 文件名里没有 %s 标记，不像备份文件：%s" % (BAK_MARK, inner))

    src_rel = inner.split(BAK_MARK, 1)[0]
    target = os.path.join(PROJ, src_rel.replace("/", os.sep))

    print("【还原】%s" % rel_bak)
    print("    → %s" % src_rel)

    # 先把"现在的内容"备份一份，避免还原完发现要回去
    if os.path.isfile(target):
        dest, err = backup_one(target, today_tag(), dry=dry, quiet=True)
        if err:
            print("   ✗ 还原前的备份失败：%s" % err)
            return 2
        if dest:
            print("    （还原前已把当前内容存为 %s）" % (rel_to_proj(dest) or dest))

    if dry:
        print("   dry-run：未写入")
        return 0

    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.copy2(ap, target)
    print("   ✓ 已还原，mtime 也一并恢复（%s）"
          % time.strftime("%m-%d %H:%M", time.localtime(os.path.getmtime(target))))
    print("   下一步：bash scripts/build.sh")
    return 0


# ------------------------------------------------------------------ 入口
def main(argv):
    args = argv[1:]
    dry = "--dry-run" in args
    verbose = "-v" in args or "--verbose" in args

    tag = None
    if "--tag" in args:
        i = args.index("--tag")
        if i + 1 >= len(args):
            sys.exit("ERROR: --tag 后面要跟标签，例如 --tag 1027a")
        tag = args[i + 1]
        del args[i:i + 2]

    day = None
    if "--day" in args:
        i = args.index("--day")
        if i + 1 >= len(args):
            sys.exit("ERROR: --day 后面要跟日期，例如 --day 1027")
        day = args[i + 1]
        del args[i:i + 2]

    flags = [a for a in args if a.startswith("-")]
    paths = [a for a in args if not a.startswith("-")]

    if "--help" in args or "-h" in args:
        print(__doc__)
        return 0

    if "--list" in flags:
        return cmd_list(day)

    if "--status" in flags:
        return cmd_status(verbose)

    if "--all" in flags:
        return cmd_all(tag or today_tag(), dry)

    if "--restore" in flags:
        if not paths:
            sys.exit("ERROR: --restore 后面要跟备份文件路径")
        return cmd_restore(paths[0], dry)

    return cmd_backup(paths, tag or today_tag(), dry)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
