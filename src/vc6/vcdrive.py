#!/usr/bin/env python3
"""vcdrive.py -- drive the VC6-linked image and the stock binary through the
same route under Wine, and compare their frames.

    vcdrive.py ROUTE [--exe stock|vc6|PATH] [--tag TAG]   run one side
    vcdrive.py ROUTE --ab                                   stock, then vc6, then compare

Routes: bootcamp (title -> BOOT CAMP -> mission), campaign (SINGLE PLAYER ->
player -> SELECT -> NEW -> MAP 01 -> mission), load (... -> LOAD a save).
Shots land in build/vc6drive/<tag>_<NN><step>.png.

HOW IT DRIVES, and why each piece is the way it is:

- A 640x480 Wine VIRTUAL DESKTOP on Xvfb :99 (`explorer /desktop=`), so the
  game gets a full-colour fullscreen DirectDraw surface. The GDI `-w` path
  went black after mission start for stock too. Never the live session.
- The cwd is the game folder, because CheckBasePath uses the launch dir. The
  EXE is copied in as ArmyMen2_drv.exe and removed after.
- THE CURSOR IS PLACED BY WRITING THE GAME'S OWN GLOBALS through
  /proc/PID/mem -- ADDR_CURSOR_X/Y and the packed ADDR_CURSOR_POINT, the same
  three `drive.sh ctl cursor` writes -- and clicks are motionless button
  presses. Synthetic pointer motion does not work: the game reads relative
  DirectInput and Wine accelerates large deltas (a 306 px move landed near
  617), and pinning to a corner fails because X clamps the real pointer
  before the game's accumulated cursor reaches 0. UpdateMouseState ACCUMULATES
  onto the globals, so a write survives. Stock has them at 0x00485464; the VC6
  image has them at am2_mouse_state[3..5], found in ArmyMen2.map.
- THE GAME PROCESS IS THE ONE WHOSE argv[0] IS THE EXE. `explorer.exe
  /desktop=...`'s command line also starts with C: and names the EXE in its
  arguments, and comes first; matching on the whole command line writes into
  the wrong process and every click silently goes nowhere.
- Kill by pid, the game and its explorer wrapper.

Live play is timing-dependent (the scroll distance, where a squad has walked
to when a click lands), so compare against a SECOND stock run before calling
a mission-frame difference a divergence. Loaded-game and menu frames are
exact.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAMEDIR = os.path.join(REPO, ".wine", "drive_c", "GOG Games", "Army Men II")
STOCK = os.path.join(GAMEDIR, "ArmyMen2.exe")
VC6 = os.path.join(REPO, "build", "vc6sa", "ArmyMen2.exe")
VC6MAP = os.path.join(REPO, "build", "vc6sa", "ArmyMen2.map")
OUT = os.path.join(REPO, "build", "vc6drive")
DRV = "ArmyMen2_drv.exe"
DISPLAY = ":99"
STOCK_CURSOR = 0x00485464            # ADDR_CURSOR_X; Y and POINT follow

# (action, args..., shot-name-or-None). Coordinates are game coordinates.
ROUTES = {
    "bootcamp": [
        ("wait", 14, "title"),
        ("click", 306, 143, 6, "bootcamp"),
        ("key", "Return", 8, "briefing"),
        ("key", "space", 8, "mission"),
        ("key", "space", 8, "mission2"),
    ],
    "campaign": [
        ("wait", 14, "title"),
        ("click", 306, 182, 6, "selectplayer"),
        ("click", 240, 177, 3, "row"),
        ("click", 455, 221, 6, "loadgame"),
        ("click", 240, 173, 2, None),
        ("click", 455, 181, 25, "stratmap"),
        ("key", "Return", 25, "briefing"),
        ("click", 320, 240, 8, "mission"),
        ("key", "space", 8, "mission2"),
    ],
    "load": [
        ("wait", 14, "title"),
        ("click", 306, 182, 6, "selectplayer"),
        ("click", 240, 177, 3, "row"),
        ("click", 455, 221, 6, "loadgame"),
        ("click", 240, 209, 2, "saverow"),
        ("click", 455, 220, 30, "loaded"),
        ("wait", 5, "loaded2"),
        ("key", "Return", 6, "after"),
        ("wait", 6, "later"),
    ],
}


def x(*args):
    return subprocess.run(list(args), env=dict(os.environ, DISPLAY=DISPLAY),
                          capture_output=True, text=True)


def game_pid():
    for p in subprocess.run(["pgrep", "-f", "ArmyMen2_drv[.]exe"],
                            capture_output=True, text=True).stdout.split():
        try:
            argv0 = open("/proc/%s/cmdline" % p, "rb").read().split(b"\0")[0]
        except OSError:
            continue
        if argv0.lower().endswith(DRV.lower().encode()):
            return int(p)
    return None


def wrapper_pids():
    out = []
    for p in subprocess.run(["pgrep", "-f", "explorer[.]exe /desktop=am2drv"],
                            capture_output=True, text=True).stdout.split():
        out.append(int(p))
    return out


def cursor_va(side):
    if side == "stock":
        return STOCK_CURSOR
    for ln in open(VC6MAP, errors="replace"):
        m = re.search(r'\s(?:_am2_mouse_state|\?am2_mouse_state@@\S+)\s+'
                      r'([0-9a-f]{8})', ln)
        if m:
            return int(m.group(1), 16) + 12      # am2_mouse_state[3]
    sys.exit("vcdrive: am2_mouse_state not in %s" % VC6MAP)


def place(pid, va, cx, cy):
    with open("/proc/%d/mem" % pid, "r+b", 0) as f:
        f.seek(va)
        f.write(struct.pack("<iihh", cx, cy, cx, cy))


def run(route, exe, side, tag):
    os.makedirs(OUT, exist_ok=True)
    if x("xset", "q").returncode != 0:
        subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x16"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
    if game_pid():
        sys.exit("vcdrive: a game is already running; not starting another")
    va = cursor_va(side)
    shutil.copy(exe, os.path.join(GAMEDIR, DRV))
    env = dict(os.environ, WINEPREFIX=os.path.join(REPO, ".wine"),
               DISPLAY=DISPLAY, WINEDEBUG="fixme-all")
    log = open(os.path.join(OUT, "%s.wine.log" % tag), "w")
    subprocess.Popen(["wine", "explorer", "/desktop=am2drv,640x480",
                      "C:\\GOG Games\\Army Men II\\" + DRV, "-nointro"],
                     cwd=GAMEDIR, env=env, stdout=log, stderr=log)
    shots = []
    try:
        n = 0
        for step in ROUTES[route]:
            kind = step[0]
            if kind == "wait":
                time.sleep(step[1]); name = step[2]
                x("xdotool", "mousemove", "320", "240")   # park, once, in-window
            elif kind == "click":
                pid = game_pid()
                if not pid:
                    raise RuntimeError("the game is not running")
                place(pid, va, step[1], step[2])
                time.sleep(0.3)
                x("xdotool", "mousedown", "1"); time.sleep(0.15)
                x("xdotool", "mouseup", "1")
                time.sleep(step[3]); name = step[4]
            else:
                x("xdotool", "key", step[1])
                time.sleep(step[2]); name = step[3]
            if name:
                n += 1
                path = os.path.join(OUT, "%s_%02d%s.png" % (tag, n, name))
                x("import", "-window", "root", "-crop", "640x480+0+0",
                  "+repage", path)
                shots.append(path)
    finally:
        pid = game_pid()
        for p in ([pid] if pid else []) + wrapper_pids():
            try:
                os.kill(p, 9)
            except OSError:
                pass
        time.sleep(2)
        try:
            os.remove(os.path.join(GAMEDIR, DRV))
        except OSError:
            pass
    return shots


def ae(a, b):
    r = subprocess.run(["compare", "-metric", "AE", a, b, "/dev/null"],
                       capture_output=True, text=True)
    return r.stderr.split()[0] if r.stderr else "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("route", choices=sorted(ROUTES))
    ap.add_argument("--exe", default="vc6")
    ap.add_argument("--tag")
    ap.add_argument("--ab", action="store_true")
    a = ap.parse_args()
    if a.ab:
        s = run(a.route, STOCK, "stock", a.route + "-stock")
        v = run(a.route, VC6, "vc6", a.route + "-vc6")
        for ps, pv in zip(s, v):
            print("%-24s %s px" % (os.path.basename(ps).split("_", 1)[1], ae(ps, pv)))
        return 0
    exe = {"stock": STOCK, "vc6": VC6}.get(a.exe, a.exe)
    side = "stock" if exe == STOCK else "vc6"
    for p in run(a.route, exe, side, a.tag or "%s-%s" % (a.route, side)):
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
