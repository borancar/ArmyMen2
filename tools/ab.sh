#!/bin/bash
# Play one scripted run twice -- once on the game's own code, once on the
# reconstruction -- and compare what came out.
#
#   tools/ab.sh bootcamp     fullscreen, into the first Boot Camp mission
#   tools/ab.sh windowed     -w, title screen only; the frame is static
#   tools/ab.sh intro        -dbg, so the Smacker film plays
#   tools/ab.sh all          all three
#
# This is the strongest check available and the only one that compares against
# the original rather than against expectations. The registry invariant checks
# one subsystem; a screenshot checks that something plausible appeared. This
# checks that 70-odd reconstructed functions produce the same log and the same
# pixels as the code they replaced.
#
# AM2_NOPATCH=1 is what makes it possible: it installs the harness -- logger,
# input hook, control socket -- and none of the reconstruction. `run-stock`
# cannot serve, because it drops the harness too and so cannot be driven or
# logged, and does not start at all on this machine.
#
# Two things are deliberately not compared. The counts, because the per-function
# counters *are* the trace stubs and those are installed with everything else,
# so the unpatched side has none. And the harness's own log lines -- `patch:`,
# `trace `, `dinput:`, `gamelog:` -- which differ by construction and by
# allocation address.
#
# A moving scene will differ by a handful of pixels: the two runs are not
# frame-synchronised and nothing can make them be. `windowed` is the one to
# watch, because its frame is static and it should be pixel-perfect.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DISP="${AM2_DISPLAY:-:99}"
WORK="${AM2_AB_DIR:-/tmp/am2-ab-$$}"
mkdir -p "$WORK" || exit 1

# Everything the harness itself emits, plus one piece of game noise. What is
# left is the game's own messages.
#
# The `]` lines are a loading progress bar the game prints a character at a
# time, and how many arrive depends on how long the load took -- so they differ
# between two runs of the same thing and mean nothing. They also end in CR,
# which is why an earlier `^\]$` never matched them and this compared them for
# a while by accident, passing only because the two counts happened to agree.
# Line endings are normalised before filtering for the same reason.
HARNESS='^(patch:|trace |trace:|verify:|am2hook|gamelog:|dinput:|\]|  [A-Za-z_]+ +[0-9]+$|==== session)'

drive() { AM2_DISPLAY="$DISP" "$REPO/tools/drive.sh" "$@"; }

# Play one configuration once. $1 = side (orig|recon), $2 = config name.
play() {
    local side="$1" cfg="$2" args wait

    case "$cfg" in
        bootcamp) args="-nointro -dbg"    ; wait=20 ;;
        windowed) args="-nointro -dbg -w" ; wait=30 ;;
        intro)    args="-dbg"             ; wait=40 ;;
        *) echo "ab.sh: unknown configuration '$cfg'" >&2; return 1 ;;
    esac

    [ "$side" = orig ] && export AM2_NOPATCH=1 || unset AM2_NOPATCH
    AM2_MAKEVARS="TRACE=1" drive start "$wait" "ARGS=$args" >/dev/null 2>&1

    # Boot Camp needs driving; the other two show what they show.
    if [ "$cfg" = bootcamp ]; then
        "$REPO/tools/point.py" 306 143 --click >/dev/null 2>&1
        sleep 25
        drive ctl "key RETURN tap" >/dev/null 2>&1
        sleep 30
    fi

    drive shot "ab-$cfg-$side" >/dev/null 2>&1

    # DISPLAY must be set here, or `make config` answers for ID 0 and names a
    # log file this run never wrote. That happened, and because two missing
    # files diff as identical it reported a clean A/B on no data at all -- so
    # the emptiness check below is not paranoia, it is the bug.
    eval "$(cd "$REPO" && DISPLAY="$DISP" make -s config)"
    if [ ! -f "$GAMEDIR/$LOGFILE" ]; then
        echo "ab.sh: no log at $GAMEDIR/$LOGFILE -- did the run start?" >&2
        drive stop >/dev/null 2>&1
        return 1
    fi
    tr -d '\r' < "$GAMEDIR/$LOGFILE" | grep -vE "$HARNESS" | grep -v '^$' \
        > "$WORK/$cfg-$side.log"
    if [ ! -s "$WORK/$cfg-$side.log" ]; then
        echo "ab.sh: $cfg/$side produced no game log lines -- refusing to" >&2
        echo "       call that a match. Check AM2_GAMELOG and the filter." >&2
        drive stop >/dev/null 2>&1
        return 1
    fi
    drive stop >/dev/null 2>&1
    sleep 2
}

compare() {
    local cfg="$1" rc=0

    if diff -q "$WORK/$cfg-orig.log" "$WORK/$cfg-recon.log" >/dev/null; then
        echo "  log     identical ($(wc -l < "$WORK/$cfg-orig.log") game messages)"
    else
        echo "  log     DIFFERS:"
        diff "$WORK/$cfg-orig.log" "$WORK/$cfg-recon.log" | sed 's/^/          /'
        rc=1
    fi

    "$REPO/.venv/bin/python" - "$REPO/build/shots" "$cfg" <<'PY' || rc=1
import sys, os, glob
from PIL import Image
import numpy as np
shots, cfg = sys.argv[1], sys.argv[2]
def find(side):
    hits = glob.glob(os.path.join(shots, "*", "ab-%s-%s.png" % (cfg, side)))
    return max(hits, key=os.path.getmtime) if hits else None
o, r = find("orig"), find("recon")
if not o or not r:
    print("  pixels  no screenshots to compare"); sys.exit(0)
a = np.asarray(Image.open(o).convert("RGB")).astype(int)
b = np.asarray(Image.open(r).convert("RGB")).astype(int)
if a.shape != b.shape:
    print("  pixels  DIFFERENT SIZES %s vs %s" % (a.shape, b.shape)); sys.exit(1)
n = (np.abs(a - b).sum(axis=2) > 0).sum()
print("  pixels  %d of %d differ (%.4f%%)%s"
      % (n, a[..., 0].size, 100.0 * n / a[..., 0].size,
         "" if n == 0 else "  -- expected on a moving scene"))
PY
    return $rc
}

cfgs="${1:-bootcamp}"
[ "$cfgs" = all ] && cfgs="bootcamp windowed intro"

fail=0
for cfg in $cfgs; do
    echo "== $cfg"
    play orig  "$cfg" || exit 1
    play recon "$cfg" || exit 1
    compare "$cfg" || fail=1
done

echo
[ $fail -eq 0 ] && echo "A/B clean." \
                || echo "A/B found differences -- read them before believing them."
echo "artifacts in $WORK"
exit $fail
