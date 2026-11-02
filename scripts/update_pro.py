#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""update_pro.py —— 扫描 src/ 与 include/，把新增的目录/源文件/头文件同步进 work.pro

【2026-11-01 目录重构后】
  src/     只有 .c / .cpp（按域分层：app/ camera/ common/ device/ recorder/ stage/ ui/ ui_qt/）
  include/ 只有 .h（与 src/ 的目录一一镜像，是工程根的独立目录，和 src/ 平级）
  所以本脚本：源文件扫 src/，头文件扫 include/，INCLUDEPATH 取 include/ 的子目录。

用法（在工程根目录或任意位置都行）：
    python3 scripts/update_pro.py              # 扫描并写回 work.pro
    python3 scripts/update_pro.py --dry-run    # 只打印差异，不写文件
    python3 scripts/update_pro.py --check      # 有差异就返回退出码 1（可挂到 build.sh 里）

【设计原则】
  1. 幂等：跑第二次不会产生任何改动（已存在的项不会重复添加、不会重排）
  2. 只碰三块：INCLUDEPATH（仅第一个、即全是 src/ 的那个）、SOURCES、HEADERS
  3. 其余内容一个字不改：注释、LIBS、QT、TARGET 全部原样保留
  4. 块内有 # 注释时拒绝自动改（避免把用户写的注释冲掉）
  5. 顺序：原有项保持原顺序，新增项按字母序追加到末尾

【为什么需要它】
  work.pro 有三处必须手工维护：
    · 新增 .cpp → SOURCES
    · 新增 .h   → HEADERS（Qt 靠它决定 moc，漏了会报
                            undefined reference to vtable for XXX）
    · 新增模块目录 → INCLUDEPATH
  漏任何一处，报错都不会指向 work.pro，极难定位。这个脚本把它们自动化。
"""

import json
import os
import sys

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRO_FILE = os.path.join(PROJ, "work.pro")
SRC_DIR = os.path.join(PROJ, "src")
INC_DIR = os.path.join(PROJ, "include")

C_EXTS = (".c",)
CXX_EXTS = (".cpp", ".cc", ".cxx")
H_EXTS = (".h", ".hpp", ".hh")

BLOCK_KEYS = ("INCLUDEPATH", "SOURCES", "HEADERS")


# ---------------------------------------------------------------- 扫描磁盘
def scan_src():
    """返回 (include 子目录列表, 源文件列表, 头文件列表)，路径都是相对工程根的 posix 形式

    【为什么两个目录分开扫】
      重构后 .c/.cpp 在 src/、.h 在 include/ —— 不再混在一起。
      源文件决定 SOURCES，头文件决定 HEADERS（Qt 靠它生成 moc），
      include/ 的子目录决定 INCLUDEPATH。三者来源不同，必须分开走。
    """
    if not os.path.isdir(SRC_DIR):
        sys.exit("ERROR: 找不到 src 目录: %s" % SRC_DIR)
    if not os.path.isdir(INC_DIR):
        sys.exit("ERROR: 找不到 include 目录: %s" % INC_DIR)

    dirs, sources, headers = [], [], []

    # ---- 源文件：src/ 下所有 .c / .cpp ----
    for root, dirnames, filenames in os.walk(SRC_DIR):
        dirnames.sort()
        for fn in sorted(filenames):
            if fn.lower().endswith(C_EXTS + CXX_EXTS):
                sources.append(os.path.relpath(os.path.join(root, fn), PROJ)
                               .replace(os.sep, "/"))

    # ---- 头文件 + 目录：include/ ----
    for root, dirnames, filenames in os.walk(INC_DIR):
        dirnames.sort()
        rel_dir = os.path.relpath(root, PROJ).replace(os.sep, "/")
        if rel_dir != "include":        # include 本身由 want_includes 单独加
            dirs.append(rel_dir)
        for fn in sorted(filenames):
            if fn.lower().endswith(H_EXTS):
                headers.append(os.path.relpath(os.path.join(root, fn), PROJ)
                               .replace(os.sep, "/"))

    return sorted(dirs), sorted(sources), sorted(headers)


# ---------------------------------------------------------------- 解析 .pro
def parse_blocks(lines):
    """找出所有 `KEY += \\` 块。

    返回 [{key, start, end, items}]：
      start = `KEY += \\` 所在行号
      end   = 最后一个条目所在行号（含）
      items = 条目列表（已去掉行尾反斜杠和首尾空白）
    """
    blocks = []
    i = 0
    n = len(lines)
    while i < n:
        m = None
        for key in BLOCK_KEYS:
            if lines[i].strip().startswith(key + " +="):
                m = key
                break
        if not m:
            i += 1
            continue

        start = i
        items = []
        j = i
        # `KEY += \` 同一行后面如果直接跟了内容，先忽略（本工程没这种写法）
        while j + 1 < n:
            nxt = lines[j + 1]
            s = nxt.strip()
            if not s:                       # 空行 = 块结束
                break
            if s.startswith("#"):           # 注释 = 块结束
                break
            if not nxt[:1].isspace():       # 没有缩进 = 不是条目
                break
            items.append(s.rstrip("\\").strip())
            j += 1

        blocks.append({"key": m, "start": start, "end": j, "items": items})
        i = j + 1

    return blocks


def pick_block(blocks, key, want_inc_only=False):
    """取指定 key 的块。

    INCLUDEPATH 有两个：一个是**工程头**搜索路径，一个是 **SDK 头**。
    判别方式用**排除法**：工程块里绝不会出现 third_party。

    ★ 不能用「所有项都以 include/ 开头」来认块 —— 重构刚完成那一刻，
      工程块里还全是 src/xxx，用前缀匹配会一个块都找不到（鸡生蛋）。
      2026-11-01 第一次跑就撞了这个："找不到 INCLUDEPATH 块"。
    """
    for b in blocks:
        if b["key"] != key:
            continue
        if want_inc_only:
            if not any(x.startswith("third_party") for x in b["items"]):
                return b
        else:
            return b
    return None


# ---------------------------------------------------------------- 合并条目
def merge(old_items, wanted):
    """保留原顺序，把 wanted 里多出来的追加到末尾。

    返回 (新列表, 新增列表, 移除列表)
    """
    added = [x for x in wanted if x not in old_items]
    removed = [x for x in old_items if x not in wanted]
    kept = [x for x in old_items if x in wanted]
    return kept + added, added, removed


def render_block(key, items):
    """生成 `KEY += \\` 块的文本行"""
    out = ["%s += \\" % key]
    for idx, it in enumerate(items):
        suffix = " \\" if idx < len(items) - 1 else ""
        out.append("    %s%s" % (it, suffix))
    return out


# ---------------------------------------------------------------- 主流程
def prune_compile_commands():
    """清理 build/compile_commands.json 里指向已不存在文件的条目。

    【为什么需要】
      build.sh 用的是 `bear -a`（追加模式）。这个模式的好处是增量编译时
      不会丢掉之前文件而是补上新的；坏处是**文件移动/改名后旧条目会永久残留**。

      而 VSCode 的 C/C++ 扩展（cpptools）读的就是这个文件。
      里面留着旧路径，IntelliSense 就会报"无法打开源文件 XxxPage.h"，
      但实际上编译完全正常（gcc 用的是 Makefile 里的 INCLUDEPATH）。

    【做什么】
      把 "源文件已不存在" 的条目删掉，让 IntelliSense 拿到干净的编译参数。

    【什么时候需要】
      每次移动/改名/删除源文件之后。所以干脆挂在 work.pro 同步里一起做。
    """
    path = os.path.join(PROJ, "build", "compile_commands.json")
    if not os.path.isfile(path):
        return

    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print("  ! compile_commands.json 解析失败，跳过：%s" % e)
        return

    if not isinstance(data, list):
        return

    kept, dropped = [], []
    for e in data:
        src = e.get("file", "")
        # compile_commands.json 里的 file 通常是相对路径（如 ../src/app/x.cpp），
        # 基准是同一项的 directory（如 .../build）。不能直接拿它去 isfile。
        if os.path.isabs(src):
            full = src
        else:
            full = os.path.normpath(os.path.join(e.get("directory", PROJ), src))

        if os.path.isfile(full):
            kept.append(e)
        else:
            dropped.append(src)

    if not dropped:
        print("  VSCode 索引库：%d 条，无失效条目" % len(kept))
        return

    with open(path, "w", encoding="utf-8") as f:
        json.dump(kept, f, indent=2, ensure_ascii=False)

    print("  VSCode 索引库：清理 %d 条失效条目（%d → %d）"
          % (len(dropped), len(data), len(kept)))
    for d in dropped[:5]:
        print("      删除:", d)
    if len(dropped) > 5:
        print("      …… 还有 %d 条" % (len(dropped) - 5))


def main():
    dry = "--dry-run" in sys.argv
    check = "--check" in sys.argv

    if not os.path.isfile(PRO_FILE):
        sys.exit("ERROR: 找不到 %s" % PRO_FILE)

    with open(PRO_FILE, encoding="utf-8") as f:
        lines = f.read().split("\n")

    blocks = parse_blocks(lines)
    dirs, sources, headers = scan_src()

    # INCLUDEPATH 要含 include 本身（保持在第一位）
    want_includes = ["include"] + dirs

    plan = []
    for key, wanted in (("INCLUDEPATH", want_includes),
                        ("SOURCES", sources),
                        ("HEADERS", headers)):
        blk = pick_block(blocks, key, want_inc_only=(key == "INCLUDEPATH"))
        if blk is None:
            sys.exit("ERROR: work.pro 里找不到 %s 块（格式被改过了？）" % key)

        # 安全阀：块内若有注释行，说明是手工精细维护的，不自动改
        for ln in lines[blk["start"]: blk["end"] + 1]:
            if ln.strip().startswith("#"):
                sys.exit("ERROR: %s 块内有注释，拒绝自动改写，请手工处理" % key)

        new_items, added, removed = merge(blk["items"], wanted)
        plan.append({**blk, "new_items": new_items, "added": added, "removed": removed})

    # ---- 打印报告 ----
    print("扫描 %s（源文件）与 %s（头文件）" % (SRC_DIR, INC_DIR))
    print("  目录 %d 个 / 源文件 %d 个 / 头文件 %d 个" % (len(dirs), len(sources), len(headers)))
    print()

    total_changed = 0
    for p in plan:
        print("%s  (现有 %d 项)" % (p["key"], len(p["items"])))
        for x in p["added"]:
            print("  + %s" % x)
            total_changed += 1
        for x in p["removed"]:
            print("  - %s   (磁盘上已不存在)" % x)
            total_changed += 1
        if not p["added"] and not p["removed"]:
            print("  = 无变化")
        print()

    # ---- 顺便清理 VSCode 索引库 ----
    #  文件移动/改名后 bear -a 会残留旧条目，导致 IntelliSense 报
    #  "无法打开源文件 xxx.h"。这里跟着 work.pro 一起清掉。
    if not dry:
        prune_compile_commands()

    if total_changed == 0:
        print("work.pro 已是最新，未做任何改动。")
        return 0

    if check:
        print("--check 模式：存在差异，退出码 1")
        return 1

    if dry:
        print("--dry-run 模式：以上为待写入内容，未修改 work.pro。")
        return 0

    # ---- 写回（从后往前替换，避免行号错位）----
    # 备份统一放 备份/ 目录，源码树里不留 .bak 文件
    backup_dir = os.path.join(PROJ, "备份")
    os.makedirs(backup_dir, exist_ok=True)
    backup = os.path.join(backup_dir, "work.pro.bak")
    with open(backup, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("已备份原文件 → %s" % backup)

    for p in sorted(plan, key=lambda x: x["start"], reverse=True):
        lines[p["start"]: p["end"] + 1] = render_block(p["key"], p["new_items"])

    with open(PRO_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print("已更新 %s（%d 处变更）" % (PRO_FILE, total_changed))
    print("下一步：bash scripts/build.sh   （build.sh 会发现 .pro 比 Makefile 新并自动重跑 qmake）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
