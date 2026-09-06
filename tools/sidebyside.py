#!/usr/bin/env python3
"""Play two builds side by side through the same input, and trap on the
first frame that differs -- both games left alive at that frame.

    tools/sidebyside.py [-a BIN] [-b BIN] [--leader a|b] [--replay FILE]
                        [--video] [--fps N] [-o DIR] [--on-trap quit|wait]
                        [--ports PA PB] [-- GAME ARGS]

A is build/armymen2-hybrid-dev (the original over src/platform) and B is
build/armymen2-dev (the reconstruction over the same). Both run under
AM2_LOCKSTEP with AM2_STEP pointing at a socket this script listens on, so
neither pumps until told to. Each pump goes: the LEADER (B, the port,
unless --leader a) is stepped and reports the frames it presented and the
host events its window took; those events are handed to the follower,
which is stepped through the same pump; the two frame lists are compared.
On the first difference both games are stopped where they are:

  - the two frames are written to DIR and diffed (pixel count, box);
  - each game's control socket is still answering, so tools/objdump.py
    --table, `snap save` and `dump` work on both, at that frame;
  - with --on-trap wait (the default on a terminal) the script takes
    `s` (one more pump), `c` (run on to the next difference), `q` (quit).

Input comes from --replay FILE, the tests/replays grammar fed to BOTH
games at its pump numbers, or from the leader's own window with --video,
which gives it a real SDL window and dummy drivers to the follower; the
two can be combined (a replay to reach a mission, then hands). --fps paces
the pumps on the wall clock, 60 with --video and unpaced otherwise.

The follower is exactly one pump behind the leader while running, and level
with it on a trap. Sound is mixed and dropped in lockstep, so the leader is
silent. Exit 0 when the games ended with no difference (the replay's exit),
1 on a trap, 2 when a game failed.
"""
import argparse
import os
import select
import signal
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Side:
    def __init__(self, tag, binary, out, port, video, extra, env_extra):
        self.tag = tag
        self.binary = binary
        self.path = os.path.join(out, tag + ".sock")
        self.port = port
        if os.path.exists(self.path):
            os.unlink(self.path)
        self.listen = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listen.bind(self.path)
        self.listen.listen(1)
        env = dict(os.environ)
        env.update({
            "SDL_AUDIO_DRIVER": "dummy",
            "AM2_LOCKSTEP": "1",
            "AM2_STEP": self.path,
            "AM2_CTL_PORT": str(port),
            "AM2_LOG": os.path.join(out, tag + ".game.log"),
            "AM2_FRAMELOG": os.path.join(out, tag + ".frames"),
        })
        env.setdefault("AM2_GAMEDIR", os.path.join(REPO, ".wine/drive_c/GOG Games/Army Men II"))
        if not video:
            env["SDL_VIDEO_DRIVER"] = "dummy"
        env.update(env_extra)
        self.err = open(os.path.join(out, tag + ".err"), "wb")
        self.proc = subprocess.Popen([binary, "-nointro", "-dbg"] + extra,
                                     stdout=self.err, stderr=subprocess.STDOUT, env=env)
        self.conn = None
        self.buf = b""
        self.pump = 0

    def accept(self, timeout):
        r, _, _ = select.select([self.listen], [], [], timeout)
        if not r:
            return False
        self.conn, _ = self.listen.accept()
        return True

    def readline(self):
        while b"\n" not in self.buf:
            if self.proc.poll() is not None and not self.buf:
                return None
            r, _, _ = select.select([self.conn], [], [], 0.5)
            if not r:
                continue
            chunk = self.conn.recv(65536)
            if not chunk:
                return None
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("latin-1")

    def send(self, line):
        self.conn.sendall((line + "\n").encode("latin-1"))

    def report(self):
        """The report the game sends on entering a pump: (pump, frames, host)."""
        frames, host = [], []
        while True:
            line = self.readline()
            if line is None:
                return None
            if line.startswith("pump "):
                self.pump = int(line[5:])
            elif line.startswith("frame "):
                idx, h = line[6:].split()
                frames.append((int(idx), h))
            elif line.startswith("host "):
                host.append(line)
            elif line == "ready":
                return frames, host

    def last(self, path):
        self.send("last " + path)
        while True:
            line = self.readline()
            if line is None or line.startswith("ok last") or line.startswith("err last"):
                return line is not None and line.startswith("ok")

    def stop(self):
        if self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                pass
        self.err.close()
        try:
            os.unlink(self.path)
        except OSError:
            pass


def load_replay(path):
    """{pump: [line, ...]} with the pump number stripped; `exit` included."""
    by_pump = {}
    with open(path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            words = line.split(None, 1)
            if len(words) < 2 or not words[0].isdigit():
                continue
            by_pump.setdefault(int(words[0]), []).append(words[1])
    return by_pump


def diff_frames(pa, pb):
    """Pixels differing and the bounding box, from two PPMs; None if unreadable."""
    def read(p):
        with open(p, "rb") as f:
            data = f.read()
        parts = data.split(b"\n", 3)
        w, h = map(int, parts[1].split())
        return w, h, parts[3]
    try:
        wa, ha, a = read(pa)
        wb, hb, b = read(pb)
    except (OSError, IndexError, ValueError):
        return None
    if (wa, ha) != (wb, hb):
        return ("size", wa, ha, wb, hb)
    pts = []
    for y in range(ha):
        ra = a[y * wa * 3:(y + 1) * wa * 3]
        rb = b[y * wb * 3:(y + 1) * wb * 3]
        if ra == rb:
            continue
        for x in range(wa):
            if ra[x * 3:x * 3 + 3] != rb[x * 3:x * 3 + 3]:
                pts.append((x, y))
    if not pts:
        return ("same", wa, ha)
    # Are the differences all swapped horizontal pairs (A,B against B,A)?
    def px(buf, w, x, y):
        return buf[(y * w + x) * 3:(y * w + x) * 3 + 3]
    left = set(pts)
    swaps = True
    for (x, y) in pts:
        if (x + 1, y) in left and px(a, wa, x, y) == px(b, wb, x + 1, y) and px(a, wa, x + 1, y) == px(b, wb, x, y):
            continue
        if (x - 1, y) in left and px(a, wa, x, y) == px(b, wb, x - 1, y) and px(a, wa, x - 1, y) == px(b, wb, x, y):
            continue
        swaps = False
        break
    return ("diff", len(pts), wa * ha, min(p[0] for p in pts), min(p[1] for p in pts),
            max(p[0] for p in pts), max(p[1] for p in pts), swaps)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-a", default=os.path.join(REPO, "build/armymen2-hybrid-dev"))
    ap.add_argument("-b", default=os.path.join(REPO, "build/armymen2-dev"))
    ap.add_argument("--leader", choices=("a", "b"), default="b")
    ap.add_argument("--replay")
    ap.add_argument("--video", action="store_true", help="the leader gets a real window")
    ap.add_argument("--fps", type=float, default=None)
    ap.add_argument("-o", "--out")
    ap.add_argument("--on-trap", choices=("quit", "wait"), default=None)
    ap.add_argument("--ports", nargs=2, type=int, default=(31341, 31342))
    ap.add_argument("--break", dest="breaks", default="",
                    help="comma-separated pump numbers to stop at as if trapped (the prompt's `c N` sets one too)")
    ap.add_argument("--tolerance", type=int, default=0,
                    help="pixels a frame may differ by without trapping (each such frame is diffed and counted)")
    ap.add_argument("--tolerate-swaps", action="store_true",
                    help="do not trap on a frame whose every difference is a horizontal pair of pixels with "
                         "the two colours swapped between the sides -- the known terrain defect, which grows "
                         "with the scroll and so cannot be a pixel count")
    ap.add_argument("--timeout", type=float, default=600, help="seconds to wait for a game to connect or pump")
    ap.add_argument("extra", nargs="*", help="arguments passed to both games")
    args = ap.parse_args()

    out = args.out or os.path.join(os.environ.get("TMPDIR", "/tmp"), "am2-sidebyside-%d" % os.getpid())
    os.makedirs(out, exist_ok=True)
    replay = load_replay(os.path.abspath(args.replay)) if args.replay else {}
    fps = args.fps if args.fps is not None else (60.0 if args.video else 0.0)
    on_trap = args.on_trap or ("wait" if sys.stdin.isatty() else "quit")

    sides = {
        "a": Side("a", args.a, out, args.ports[0], args.video and args.leader == "a", args.extra, {}),
        "b": Side("b", args.b, out, args.ports[1], args.video and args.leader == "b", args.extra, {}),
    }
    leader = sides[args.leader]
    follower = sides["a" if args.leader == "b" else "b"]
    rc = 2

    def finish(code):
        for s in sides.values():
            s.stop()
        print("sidebyside: artifacts in %s" % out)
        sys.exit(code)

    signal.signal(signal.SIGTERM, lambda *_: finish(2))
    signal.signal(signal.SIGINT, lambda *_: finish(2))
    sys.stdout.reconfigure(line_buffering=True)
    try:
        for s in (leader, follower):
            if not s.accept(args.timeout):
                print("sidebyside: %s (%s) never connected; see %s" % (s.tag, s.binary, s.err.name))
                finish(2)
        # The first report of each, on entering pump 1: nothing in it.
        if leader.report() is None or follower.report() is None:
            print("sidebyside: a game left before its first pump")
            finish(2)
        print("sidebyside: leader %s (%s) pid %d port %d, follower %s (%s) pid %d port %d"
              % (leader.tag, leader.binary, leader.proc.pid, leader.port,
                 follower.tag, follower.binary, follower.proc.pid, follower.port))

        interval = 1.0 / fps if fps > 0 else 0.0
        state = {"next_at": time.monotonic(), "last_index": -1, "actions": [], "tolerated": 0,
                 "breaks": set(int(x) for x in args.breaks.split(",") if x.strip())}

        def pump_once():
            """Step the leader, then the follower through the same pump.
            Returns (frames_l, frames_f), or None when a game has left."""
            pump = leader.pump
            if interval:
                now = time.monotonic()
                if now < state["next_at"]:
                    time.sleep(state["next_at"] - now)
                state["next_at"] = max(state["next_at"] + interval, now - interval)
            actions = replay.get(pump, [])
            state["actions"] = actions
            for line in actions:
                leader.send(line)
            leader.send("step")
            rl = leader.report()
            if rl is None:
                return None
            frames_l, host_l = rl
            for line in actions:
                follower.send(line)
            for line in host_l:
                follower.send(line)
            follower.send("step")
            rf = follower.report()
            if rf is None:
                return None
            for idx, _ in frames_l:
                state["last_index"] = max(state["last_index"], idx)
            return frames_l, rf[0]

        def left():
            """A game has gone: the replay's exit, or a failure."""
            if "exit" in state["actions"]:
                # The follower's exit is at the same pump; let it go too.
                if follower.proc.poll() is None and follower.conn:
                    for line in state["actions"]:
                        follower.send(line)
                    follower.send("step")
                    follower.report()
                if state["tolerated"]:
                    print("sidebyside: no frame beyond what is tolerated through presented frame %d (%d frames tolerated)"
                          % (state["last_index"], state["tolerated"]))
                else:
                    print("sidebyside: IDENTICAL through presented frame %d" % state["last_index"])
                finish(0)
            gone = leader if leader.proc.poll() is not None else follower
            print("sidebyside: %s left at pump %d; see %s" % (gone.tag, leader.pump, gone.err.name))
            finish(2)

        def first_differing(frames_l, frames_f):
            for (il, hl), (jf, hf) in zip(frames_l, frames_f):
                if il != jf or hl != hf:
                    return il
            if frames_l or frames_f:
                return (frames_l + frames_f)[min(len(frames_l), len(frames_f))][0]
            return -1

        def within_tolerance(frames_l, frames_f):
            """A differing frame that --tolerance or --tolerate-swaps forgives: diffed, counted, not trapped."""
            if (args.tolerance <= 0 and not args.tolerate_swaps) or len(frames_l) != len(frames_f):
                return False
            if [i for i, _ in frames_l] != [i for i, _ in frames_f]:
                return False
            idx = first_differing(frames_l, frames_f)
            pl = os.path.join(out, "tol-%s.ppm" % leader.tag)
            pf = os.path.join(out, "tol-%s.ppm" % follower.tag)
            if not (leader.last(pl) and follower.last(pf)):
                return False
            d = diff_frames(pl, pf)
            if not d or d[0] != "diff":
                return False
            if d[1] > args.tolerance and not (args.tolerate_swaps and d[7]):
                return False
            state["tolerated"] += 1
            if state["tolerated"] <= 3:
                print("sidebyside: frame %d differs by %d pixels, box %d,%d-%d,%d%s: tolerated"
                      % ((idx, d[1]) + d[3:7] + (", all swapped pairs" if d[7] else "",)))
            elif state["tolerated"] == 4:
                print("sidebyside: (further tolerated frames are counted, not printed)")
            return True

        def trap(frames_l, frames_f):
            """Report the difference; both games sit at the top of the next pump."""
            idx = first_differing(frames_l, frames_f)
            print("sidebyside: TRAP at pump %d, presented frame %d: %s presented %s, %s presented %s"
                  % (leader.pump - 1, idx, leader.tag, [h for _, h in frames_l] or "nothing",
                     follower.tag, [h for _, h in frames_f] or "nothing"))
            pl = os.path.join(out, "%s-%d.ppm" % (leader.tag, idx))
            pf = os.path.join(out, "%s-%d.ppm" % (follower.tag, idx))
            if leader.last(pl) and follower.last(pf):
                d = diff_frames(pl, pf)
                if d and d[0] == "diff":
                    print("sidebyside: %d of %d pixels differ, box %d,%d-%d,%d%s"
                          % (d[1:7] + (" (all swapped horizontal pairs)" if d[7] else "",)))
                elif d and d[0] == "same":
                    print("sidebyside: the frames are identical pixel for pixel (the hash differs in the palette only)")
                elif d:
                    print("sidebyside: the frames differ in size: %dx%d against %dx%d" % d[1:])
                print("sidebyside: frames %s and %s" % (pl, pf))
            print("sidebyside: both games are alive at this frame; control sockets %d (%s) and %d (%s)"
                  % (leader.port, leader.tag, follower.port, follower.tag))

        while True:
            res = pump_once()
            if res is None:
                left()
            frames_l, frames_f = res
            same = frames_l == frames_f or within_tolerance(frames_l, frames_f)
            if same and (leader.pump - 1) not in state["breaks"]:
                continue
            if same:
                print("sidebyside: BREAK at pump %d, presented %s; both games alive, control sockets %d (%s) and %d (%s)"
                      % (leader.pump - 1, [h for _, h in frames_l] or "nothing",
                         leader.port, leader.tag, follower.port, follower.tag))
            else:
                trap(frames_l, frames_f)
                rc = 1
            if on_trap == "quit":
                finish(rc if not same else 0)
            # Hold here: step one pump at a time, or run on to the next difference.
            while True:
                sys.stdout.write("sidebyside [s]tep [c]ontinue [c N: to pump N] [q]uit> ")
                sys.stdout.flush()
                cmd = sys.stdin.readline().strip().lower()
                if cmd in ("q", ""):
                    finish(rc)
                if cmd.startswith("c"):
                    rest = cmd[1:].strip()
                    if rest.isdigit():
                        state["breaks"].add(int(rest))
                    break
                if cmd != "s":
                    continue
                res = pump_once()
                if res is None:
                    left()
                frames_l, frames_f = res
                if frames_l != frames_f and not within_tolerance(frames_l, frames_f):
                    trap(frames_l, frames_f)
                else:
                    print("sidebyside: pump %d: both presented %s" % (leader.pump - 1, [h for _, h in frames_l] or "nothing"))
    except KeyboardInterrupt:
        finish(rc)


if __name__ == "__main__":
    main()
