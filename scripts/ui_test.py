#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ui_test.py —— 自动点击 + 抓屏 + 日志归档（板子上的 UI 回归工具）

======================================================================
它解决什么问题
======================================================================
这块板子的内核**没有 uinput**（`/dev/uinput` 不存在，`modprobe uinput` 找不到模块），
所以没法从外部造虚拟触摸设备。唯一可行的注入点在**进程内**：
应用里有个探针 `src/device/hal/ui_probe.cpp`，只要环境变量 `DVR_UI_PROBE=1`，
它就会每 100ms 读 `/tmp/dvr_ui.cmd`，按里面的 `click X Y` 把 QMouseEvent post 给目标控件。

于是分工是：
    本脚本（在 VM 上跑）  ←→  板子
    ├─ 写 /tmp/dvr_ui.cmd        → 探针执行点击
    ├─ cat /dev/fb0              → 抓屏（32bpp BGRA，stride 4096，1024×1200）
    └─ tail /tmp/work.log        → 取应用日志增量

======================================================================
用法
======================================================================
    # 1) 先让应用带探针跑起来（--launch 会自动重启应用）
    python3 scripts/ui_test.py --seq scripts/seq/camera_save.json --launch

    # 2) 应用已经在跑（且启动时带过 DVR_UI_PROBE=1）
    python3 scripts/ui_test.py --seq scripts/seq/camera_save.json

    # 3) 看历史运行（按时间倒序）
    python3 scripts/ui_test.py --list

    # 4) 从已保存的 raw 重新生成 PNG（比如当时没装 zlib 或想换裁切）
    python3 scripts/ui_test.py --rebuild ui_runs/20261101-162233_camera_save

======================================================================
依赖
======================================================================
    · VM 上的 python3（标准库即可：zlib / struct / json / subprocess）
    · VM 能免密 ssh 到板子（root@<config/board_ip.txt>）
    · 板上跑的应用必须带 `DVR_UI_PROBE=1`（`--launch` 会帮你设）
    · **不需要** PIL / ImageMagick —— PNG 是脚本里现编的（见 write_png）

======================================================================
产物（一次运行一个目录，按时间排序、可回溯）
======================================================================
    ui_runs/20261101-162233_camera_save/
        timeline.log        带毫秒时间戳的操作流水 + 每步之后的应用日志增量
        seq.json            本次用的序列（原样拷贝，改过也能对上）
        00_00_start.png     截图（1024×600，屏幕可见区）
        00_00_start_full.png  整块 framebuffer（1024×1200，虚拟高度）
        00_00_start.raw     原始字节（回溯时可用 --rebuild 重新出图）
        work.log            运行结束时的完整应用日志
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
from datetime import datetime

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # .../my/main
RUNS_DIR = os.path.join(PROJ, "ui_runs")

FB_W, FB_H = 1024, 1200          # /sys/class/graphics/fb0/virtual_size = 1024,1200
FB_STRIDE = 4096                 # 每行字节（1024 × 4）
SCREEN_H = 600                   # 实际可见高度
FB_PATH = "/dev/fb0"
APP_LOG = "/tmp/work.log"
CMD_PATH = "/tmp/dvr_ui.cmd"

# 应用启动所需的整套环境（与 scripts/deploy.sh 保持一致）
APP_ENV = ("XDG_RUNTIME_DIR=/dev/shm QT_QPA_PLATFORM=linuxfb "
           "QT_QPA_FB_FORCE_FULLSCREEN=0 QT_QPA_FONTDIR=/usr/share/fonts/otf")
APP_DIR = "/root"


# --------------------------------------------------------------------------
# 板子访问
# --------------------------------------------------------------------------
class Board:
    def __init__(self, ip=None, user="root"):
        self.ip = ip or self._read_board_ip()
        self.user = user

    @staticmethod
    def _read_board_ip():
        p = os.path.join(PROJ, "config", "board_ip.txt")
        with open(p, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    return line
        raise SystemExit("config/board_ip.txt 里没有可用 IP")

    def run(self, cmd, text=True, timeout=30):
        """在板子上执行一条命令（ssh 到板子；本脚本假定自己在 VM 上跑）"""
        r = subprocess.run(["ssh", "-o", "ConnectTimeout=5",
                            "%s@%s" % (self.user, self.ip), cmd],
                           capture_output=True, timeout=timeout)
        if r.returncode != 0 and text:
            sys.stderr.write("[ssh %s] %s\n" % (r.returncode,
                                                r.stderr.decode("utf-8", "replace").strip()))
        return r.stdout.decode("utf-8", "replace") if text else r.stdout

    # ---- 具体动作 ----
    def click(self, x, y):
        # 用 printf 写文件，避免 echo 的转义差异
        self.run("printf 'click %d %d\\n' > %s" % (x, y, CMD_PATH))

    def grab_fb(self):
        return self.run("cat " + FB_PATH, text=False)

    def log_lines(self):
        out = self.run("cat " + APP_LOG)
        return out.splitlines()

    def launch(self):
        """重启应用，并带上探针"""
        self.run("killall -TERM work 2>/dev/null; sleep 2; "
                 "killall -9 work 2>/dev/null; rm -f %s %s; "
                 "dd if=/dev/zero of=/dev/fb0 bs=4096 count=600 2>/dev/null; "
                 "cd %s && nohup env %s DVR_UI_PROBE=1 ./work > %s 2>&1 < /dev/null &"
                 % (CMD_PATH, APP_LOG, APP_DIR, APP_ENV, APP_LOG))
        # 等它起来
        for _ in range(50):
            if self.run("pidof work").strip():
                return True
            time.sleep(0.2)
        return False


# --------------------------------------------------------------------------
# PNG（纯标准库：BGRA → RGB，zlib 压缩）
# --------------------------------------------------------------------------
def bgra_to_rows(data, w, h, stride, crop_h=None):
    """把 framebuffer 字节转成 RGB 行列表"""
    rows = []
    rows_total = h if crop_h is None else min(h, crop_h)
    for y in range(rows_total):
        base = y * stride
        line = data[base: base + w * 4]
        if len(line) < w * 4:
            line += b"\x00" * (w * 4 - len(line))
        rgb = bytearray(w * 3)
        # BGRA(4B) → RGB(3B)：跳掉 alpha
        rgb[0::3] = line[2::4]
        rgb[1::3] = line[1::4]
        rgb[2::3] = line[0::4]
        rows.append(bytes(rgb))
    return rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)          # filter type 0

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8bit truecolor
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def save_shot(board, out_dir, idx, name):
    data = board.grab_fb()
    need = FB_STRIDE * FB_H
    if len(data) < need:
        # 有些内核只给可见区
        need = min(len(data), FB_STRIDE * SCREEN_H)
    stem = "%02d_%s" % (idx, name)
    raw_path = os.path.join(out_dir, stem + ".raw")
    with open(raw_path, "wb") as f:
        f.write(data)

    # 可见区（上半屏）
    write_png(os.path.join(out_dir, stem + ".png"), FB_W, SCREEN_H,
              bgra_to_rows(data, FB_W, SCREEN_H, FB_STRIDE))
    # 整块虚拟屏（回溯用）
    full_h = min(FB_H, len(data) // FB_STRIDE)
    write_png(os.path.join(out_dir, stem + "_full.png"), FB_W, full_h,
              bgra_to_rows(data, FB_W, full_h, FB_STRIDE))
    return stem + ".png"


# --------------------------------------------------------------------------
# 序列执行
# --------------------------------------------------------------------------
class Timeline:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "w", encoding="utf-8")

    def mark(self, kind, detail=""):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = "[%s] %-8s %s" % (ts, kind, detail)
        print(line)
        self.f.write(line + "\n")
        self.f.flush()

    def close(self):
        self.f.close()


def run_sequence(board, seq, seq_path, out_dir):
    shutil.copy(seq_path, os.path.join(out_dir, "seq.json"))
    tl = Timeline(os.path.join(out_dir, "timeline.log"))
    tl.mark("START", "序列 %s（%d 步）" % (seq.get("name", "?"), len(seq["steps"])))

    seen = 0
    shot_idx = 0
    for i, step in enumerate(seq["steps"], 1):
        if "click" in step:
            x, y = step["click"]
            board.click(int(x), int(y))
            tl.mark("CLICK", "(%d,%d) %s" % (x, y, step.get("note", "")))
        elif "wait" in step:
            ms = int(step["wait"])
            tl.mark("WAIT", "%dms %s" % (ms, step.get("note", "")))
            time.sleep(ms / 1000.0)
        elif "shot" in step:
            shot_idx += 1
            png = save_shot(board, out_dir, shot_idx, step["shot"])
            tl.mark("SHOT", png + " " + step.get("note", ""))
        elif "log" in step:
            tl.mark("MARK", step["log"])
        else:
            tl.mark("SKIP", "看不懂的步骤：%s" % step)
            continue

        # 每步之后把新增的应用日志抄进流水（这样时间顺序是混在一起的）
        time.sleep(float(seq.get("settle", 0.25)))
        lines = board.log_lines()
        for ln in lines[seen:]:
            tl.mark("app", ln)
        seen = len(lines)

        if "expect" in step:
            hit = any(step["expect"] in ln for ln in lines[seen:] + lines)
            tl.mark("CHECK", ("✅ 命中 " if hit else "❌ 未命中 ") + step["expect"])

    tl.mark("END", "序列结束")
    tl.close()
    with open(os.path.join(out_dir, "work.log"), "w", encoding="utf-8") as f:
        f.write("\n".join(board.log_lines()))
    return out_dir


# --------------------------------------------------------------------------
# 回溯 / 列表
# --------------------------------------------------------------------------
def cmd_list():
    if not os.path.isdir(RUNS_DIR):
        print("还没有运行记录（%s 不存在）" % RUNS_DIR)
        return
    names = sorted(os.listdir(RUNS_DIR), reverse=True)
    for n in names:
        d = os.path.join(RUNS_DIR, n)
        if not os.path.isdir(d):
            continue
        pngs = [x for x in os.listdir(d) if x.endswith(".png") and not x.endswith("_full.png")]
        print("%-40s %2d 张截图  %s" % (n, len(pngs),
                                        os.path.join(d, "timeline.log")))


def cmd_rebuild(run_dir):
    if not os.path.isdir(run_dir):
        raise SystemExit("目录不存在：%s" % run_dir)
    n = 0
    for fn in sorted(os.listdir(run_dir)):
        if not fn.endswith(".raw"):
            continue
        with open(os.path.join(run_dir, fn), "rb") as f:
            data = f.read()
        stem = fn[:-4]
        write_png(os.path.join(run_dir, stem + ".png"), FB_W, SCREEN_H,
                  bgra_to_rows(data, FB_W, SCREEN_H, FB_STRIDE))
        full_h = min(FB_H, len(data) // FB_STRIDE)
        write_png(os.path.join(run_dir, stem + "_full.png"), FB_W, full_h,
                  bgra_to_rows(data, FB_W, full_h, FB_STRIDE))
        n += 1
    print("重新生成 %d 张 PNG → %s" % (n, run_dir))


# --------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="板端 UI 自动点击 + 抓屏 + 日志归档")
    ap.add_argument("--seq", help="点击序列 JSON")
    ap.add_argument("--launch", action="store_true", help="先重启应用并带上 DVR_UI_PROBE=1")
    ap.add_argument("--out", help="输出目录（默认 ui_runs/<时间>_<序列名>）")
    ap.add_argument("--ip", help="板子 IP（默认读 config/board_ip.txt）")
    ap.add_argument("--list", action="store_true", help="列出历史运行")
    ap.add_argument("--rebuild", metavar="DIR", help="从 .raw 重新生成 PNG")
    args = ap.parse_args(argv)

    if args.list:
        return cmd_list()
    if args.rebuild:
        return cmd_rebuild(args.rebuild)
    if not args.seq:
        ap.error("要么给 --seq，要么用 --list / --rebuild")

    with open(args.seq, encoding="utf-8") as f:
        seq = json.load(f)

    board = Board(args.ip)
    if args.launch:
        if not board.launch():
            raise SystemExit("应用没起来")
        print("应用已重启（带 DVR_UI_PROBE=1）")
        time.sleep(1.0)

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.out or os.path.join(RUNS_DIR, "%s_%s" % (stamp, seq.get("name", "seq")))
    os.makedirs(out_dir, exist_ok=True)

    run_sequence(board, seq, args.seq, out_dir)
    print("\n产物：%s" % out_dir)
    for fn in sorted(os.listdir(out_dir)):
        print("   " + fn)
    return 0


if __name__ == "__main__":
    sys.exit(main())
