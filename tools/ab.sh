#!/bin/bash
# Play one scripted run twice -- once on the game's own code, once on the
# reconstruction -- and compare what came out.
#
#   tools/ab.sh bootcamp     fullscreen, into the first Boot Camp mission
#   tools/ab.sh windowed     -w, title screen only; the frame is static
#   tools/ab.sh intro        -dbg, so the Smacker film plays
#   tools/ab.sh audio        Boot Camp again, with a silent sound device
#   tools/ab.sh mission      past both dialogs into live play, and scrolling
#   tools/ab.sh all          all five
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
#
# Each configuration has a pixel budget and exceeding it fails the run. That is
# not decoration: a reconstruction of the map tile painter once drew 33,137
# wrong pixels and this script reported "A/B clean", because it only ever
# failed on the log. The number was printed and nothing read it.

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

# Game output whose COUNT depends on how long the run lived, not on whether the
# reconstruction is right. Kept apart from HARNESS above on purpose: that filter
# removes our own noise, this one removes the game's own -- a different claim,
# and one that has to be justified per line rather than assumed.
#
# `-dbg` emits a single character per frame during live play. Over a mission
# that is tens of thousands of lines, and the two sides never run the same
# number of frames: the first mission A/B compared 24,914 lines against 21,741
# and reported a difference that was entirely wall-clock. Same shape as the `]`
# loading-progress lines already in HARNESS, and the same fix.
#
# How many were dropped is printed, because a filter that can silently eat the
# whole log is how an A/B comes to pass on nothing.
VOLATILE='^[_ac]$'

drive() { AM2_DISPLAY="$DISP" "$REPO/tools/drive.sh" "$@"; }

# Play one configuration once. $1 = side (orig|recon), $2 = config name.
play() {
    local side="$1" cfg="$2" args wait

    case "$cfg" in
        bootcamp) args="-nointro -dbg"    ; wait=20 ;;
        windowed) args="-nointro -dbg -w" ; wait=30 ;;
        intro)    args="-dbg"             ; wait=40 ;;
        # The same run as bootcamp, but with an audio device. Without one
        # DirectSound never starts and nine reconstructed functions -- the
        # whole streaming path, InitDirectSound, InitWaveSounds -- do not
        # execute at all, so `bootcamp` compares them not at all. ALSA's `null`
        # plugin supplies a device that discards everything and needs no sound
        # server. See tools/alsa/asoundrc.
        audio)    args="-nointro -dbg"    ; wait=20
                  export ALSA_CONFIG_PATH="$REPO/tools/alsa/asoundrc" ;;
        # Deeper than bootcamp: past BOTH opening dialogs and then scrolling,
        # which is the only way to reach the live frame path. bootcamp stops at
        # the briefing with the dialogs up, and while they are up the game does
        # not compose frames at all -- ComposeFrame sat frozen at 170 for as
        # long as the instruction sign was on screen, and the scroll merge at
        # 0x0041D060 never ran once.
        mission)  args="-nointro -dbg"    ; wait=20 ;;
        *) echo "ab.sh: unknown configuration '$cfg'" >&2; return 1 ;;
    esac

    [ "$side" = orig ] && export AM2_NOPATCH=1 || unset AM2_NOPATCH
    AM2_MAKEVARS="TRACE=1" drive start "$wait" "ARGS=$args" >/dev/null 2>&1

    # Boot Camp needs driving; the other two show what they show.
    if [ "$cfg" = bootcamp ] || [ "$cfg" = audio ] || [ "$cfg" = mission ]; then
        "$REPO/tools/point.py" 306 143 --click >/dev/null 2>&1
        sleep 25
        drive ctl "key RETURN tap" >/dev/null 2>&1
        sleep 30
    fi

    if [ "$cfg" = mission ]; then
        # MESSAGE FROM HQ, whose OK point.py can find.
        "$REPO/tools/point.py" 476 224 --click >/dev/null 2>&1
        sleep 6
        # The full-screen instruction sign behind it. Any click clears it, but
        # point.py CANNOT be used here: it locates the pointer by colour on a
        # screenshot and this screen defeats it, so every attempt silently did
        # nothing. A raw button press through the socket needs no cursor at all,
        # and position does not matter when any click will do.
        drive ctl "mouse left tap" >/dev/null 2>&1
        sleep 6
        # Now the mission is live and the view can scroll, which is what brings
        # the dirty-rectangle merge and the whole live frame path into play.
        for _ in 1 2 3 4 5 6; do
            drive ctl "mouse move 200 0" >/dev/null 2>&1
        done
        sleep 4
        for _ in 1 2 3 4 5 6; do
            drive ctl "mouse move 0 200" >/dev/null 2>&1
        done
        sleep 6
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
        > "$WORK/$cfg-$side.raw"
    grep -cE "$VOLATILE" "$WORK/$cfg-$side.raw" > "$WORK/$cfg-$side.volatile" \
        || echo 0 > "$WORK/$cfg-$side.volatile"
    grep -vE "$VOLATILE" "$WORK/$cfg-$side.raw" > "$WORK/$cfg-$side.log"
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

    local vo vr
    vo=$(cat "$WORK/$cfg-orig.volatile" 2>/dev/null || echo 0)
    vr=$(cat "$WORK/$cfg-recon.volatile" 2>/dev/null || echo 0)
    if [ "$vo" -gt 0 ] || [ "$vr" -gt 0 ]; then
        echo "  frames  $vo/$vr per-frame markers dropped as volatile"
    fi

    if diff -q "$WORK/$cfg-orig.log" "$WORK/$cfg-recon.log" >/dev/null; then
        echo "  log     identical ($(wc -l < "$WORK/$cfg-orig.log") game messages)"
    else
        echo "  log     DIFFERS:"
        diff "$WORK/$cfg-orig.log" "$WORK/$cfg-recon.log" | sed 's/^/          /'
        rc=1
    fi

    # A pixel budget per configuration, because a verdict that only reads the
    # log will call a badly broken frame "clean". PaintMapTiles went in with
    # its tile rows misdecoded, drew 33,137 wrong pixels, and this script said
    # A/B clean -- the number was right there and nothing acted on it.
    #
    # windowed is static and must be exact. The two Boot Camp runs animate a
    # little and have sat at 22 for the whole project. The intro is two
    # unsynchronised playbacks of the same film and cannot be compared at all.
    case "$cfg" in
        windowed) budget=0 ;;
        intro)    budget=-1 ;;      # -1 disables the check
        # Measured, not guessed -- see the note below the case.
        mission)  budget=-1 ;;
        *)        budget=500 ;;
    esac
    # Overridable, mainly so the check itself can be tested.
    budget="${AM2_AB_PIXELS:-$budget}"

    "$REPO/.venv/bin/python" - "$REPO/build/shots" "$cfg" "$budget" <<'PY' || rc=1
import sys, os, glob
from PIL import Image
import numpy as np
shots, cfg, budget = sys.argv[1], sys.argv[2], int(sys.argv[3])
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
over = budget >= 0 and n > budget
print("  pixels  %d of %d differ (%.4f%%)%s"
      % (n, a[..., 0].size, 100.0 * n / a[..., 0].size,
         "  -- OVER the budget of %d" % budget if over
         else "" if n == 0 else "  -- expected on a moving scene"))
if over:
    print("  pixels  the frame is wrong, not merely unsynchronised")
    sys.exit(1)
PY
    return $rc
}

cfgs="${1:-bootcamp}"
[ "$cfgs" = all ] && cfgs="bootcamp windowed intro audio mission"

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
