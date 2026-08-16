#!/usr/bin/env python3
"""Move the game's mouse cursor to an absolute screen position.

The control socket speaks relative deltas, because that is what the game reads:
it takes buffered DirectInput data, not absolute cursor state. Wine applies
mouse acceleration on the way through, and the gain is not constant -- measured
at roughly 1.75x for a 100-pixel step and 2.0x for a 300-pixel one -- so a
single computed delta overshoots. Rather than model the curve, close the loop:
move, look at where the cursor actually is, correct, repeat. Converges in a
handful of steps and does not care what the curve looks like.

Finding the cursor: it is a saturated orange arrow, and the exact palette was
sampled from a frame with the cursor pinned to the corner rather than guessed --
(239,107,2), (250,126,2), (250,146,2), (254,166,2). The threshold has to be
tight on red and blue, because a loose one (R>180, B<90) also matches the dirt
in the title-screen scenery at (181,156,88) and silently reports rubble as the
pointer.

    tools/point.py X Y [--port N]        move there
    tools/point.py X Y --click           move there, then left-click
"""
import argparse
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ctl(port, *args):
    subprocess.run([os.path.join(REPO, "tools", "am2ctl.py"), "--port", str(port)]
                   + [str(a) for a in args],
                   capture_output=True)


def cursor(display, tmp):
    subprocess.run(["import", "-display", display, "-window", "root", tmp],
                   capture_output=True)
    from PIL import Image
    import numpy as np
    a = np.array(Image.open(tmp).convert("RGB")).astype(int)
    m = ((a[:, :, 0] >= 230) & (a[:, :, 1] >= 90) &
         (a[:, :, 1] <= 180) & (a[:, :, 2] <= 20))
    ys, xs = np.nonzero(m)
    if len(xs) < 8:          # a real arrow is ~60-80 pixels; fewer is noise
        return None
    return int(xs.min()), int(ys.min())


def point(port, display, tx, ty, tmp, tries=8):
    # Pin to the top-left first so the first correction starts from a known
    # place even if the cursor was parked off in a corner.
    ctl(port, "mouse", "move", -3000, -3000)
    time.sleep(0.4)
    gain = 1.8
    for _ in range(tries):
        pos = cursor(display, tmp)
        if pos is None:
            return None
        cx, cy = pos
        ex, ey = tx - cx, ty - cy
        if abs(ex) <= 2 and abs(ey) <= 2:
            return cx, cy
        ctl(port, "mouse", "move", int(round(ex / gain)), int(round(ey / gain)))
        time.sleep(0.4)
        # After the first real step the gain is measurable; use it.
        npos = cursor(display, tmp)
        if npos and (npos[0] - cx) != 0 and abs(ex) > 20:
            g = (npos[0] - cx) / (ex / gain)
            if 0.5 < g < 4.0:
                gain = g
    return cursor(display, tmp)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("x", type=int)
    p.add_argument("y", type=int)
    p.add_argument("--port", type=int, default=int(os.environ.get("AM2_CTLPORT", 31436)))
    p.add_argument("--display", default=os.environ.get("AM2_DISPLAY", ":99"))
    p.add_argument("--click", action="store_true")
    a = p.parse_args()

    tmp = os.path.join(REPO, "build", f"cursor-{a.port}.png")
    got = point(a.port, a.display, a.x, a.y, tmp)
    print(f"target ({a.x},{a.y}) -> cursor {got}")
    if got is None:
        return 1
    if a.click:
        ctl(a.port, "mouse", "left", "tap")
        print("clicked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
