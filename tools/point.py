#!/usr/bin/env python3
"""Put the game's mouse cursor at an absolute screen position, and read it back.

    tools/point.py X Y [--port N]        place it there
    tools/point.py X Y --click           place it there, then left-click
    tools/point.py --where               print where it is

This is now a thin wrapper over the control socket's `cursor` command, which
writes ADDR_CURSOR_X, ADDR_CURSOR_Y and the packed ADDR_CURSOR_POINT that 32
sites in the image read to decide what the pointer is over. One round trip,
exact, and it answers with where the cursor ended up.

What it used to be is worth remembering, because the reason it could stop being
that is the whole point. The game reads BUFFERED DirectInput rather than
absolute cursor state, so the socket could only offer relative deltas; Wine
applies non-linear acceleration on top (~1.75x for a 100-pixel step, ~2.0x for
a 300-pixel one), so a single computed delta overshot. The answer was to close
the loop on a screenshot: move, find the cursor by colour, correct, repeat. It
worked, and it carried a palette sampled from a real frame because a loose
threshold matched title-screen rubble and reported that as the pointer.

Two things it could never do, both of which cost time:

  - Where the cursor is not DRAWN there is nothing to find. The Boot Camp
    briefing screen and the full-screen instruction sign both defeated it, and
    the failure was silent -- every click went nowhere and the counters simply
    did not move.
  - Two runs of the same script landed in slightly different places, because
    the acceleration made the correction sequence timing-dependent.

Reconstructing UpdateMouseState (0x00426F40) is what removed the need for any
of it: the absolute cursor those deltas were feeding is a global, and the
globals are the GAME's, so this works unchanged under AM2_NOPATCH=1 and both
halves of an A/B can be driven with identical coordinates.

It does not exercise PollMouse or UpdateMouseState -- nothing is read from the
device. When the input path itself is what is under test, use
`drive.sh ctl "mouse move DX DY"`, which is still the honest way in.
"""
import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ctl(port, command):
    out = subprocess.run(
        [os.path.join(REPO, "tools", "am2ctl.py"), "--port", str(port), command],
        capture_output=True, text=True)
    return (out.stdout or "").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("x", nargs="?", type=int)
    ap.add_argument("y", nargs="?", type=int)
    ap.add_argument("--click", action="store_true")
    # Same default and same env var as before, so ab.sh's calls are unchanged.
    ap.add_argument("--port", type=int,
                    default=int(os.environ.get("AM2_CTLPORT", 31436)))
    ap.add_argument("--where", action="store_true", help="just report it")
    # Accepted and ignored: the screenshot path is gone, but ab.sh and any
    # scripted run may still pass it.
    ap.add_argument("--display", default=os.environ.get("AM2_DISPLAY", ":99"))
    args = ap.parse_args()

    if args.where or args.x is None:
        print(ctl(args.port, "cursor"))
        return 0

    reply = ctl(args.port, "cursor %d %d" % (args.x, args.y))
    if not reply.startswith("ok "):
        print("cursor failed: %s" % reply, file=sys.stderr)
        return 1

    # The socket answers with where it ended up, which differs from what was
    # asked for only when the clamp bit -- so this is a real check, not an echo.
    got = tuple(int(v) for v in reply.split()[2:4])
    if got != (args.x, args.y):
        print("clamped to %d,%d (screen edge)" % got, file=sys.stderr)

    if args.click:
        ctl(args.port, "mouse left tap")
    print("%d %d" % got)
    return 0


if __name__ == "__main__":
    sys.exit(main())
