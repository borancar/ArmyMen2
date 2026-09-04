#!/bin/sh
# samenu.sh -- does the STANDALONE build still reach the main menu, and does
# its title screen match the injected build's?
#
# The standalone port replacing ArmyMen2.exe is the project's headline claim,
# and until this existed it rested on someone looking at a screenshot. This
# runs both halves and compares the two things that can be compared: the
# game's OWN log lines, which should be identical, and the title screen,
# which is static on both sides and so can be diffed with a small budget --
# unlike every live-play configuration in tools/ab.sh, whose pixel checks are
# disabled by construction.
#
# It is deliberately not a configuration of tools/ab.sh: that compares the
# same binary with and without our patches, and this compares two DIFFERENT
# binaries. Wiring it in would have meant teaching every stage about a second
# executable.
#
#     tools/samenu.sh            # builds, runs both, compares
#
# The budget is for the cursor and nothing else. CLAUDE.md measures the menu
# pointer as not reproducible frame for frame even when both sides are driven
# identically -- `controls` sees 45 to 54 pixels in a 10x13 box about one run
# in five -- so a handful here is the pointer and never a caption.
set -e

DISP=${AM2_DISPLAY:-:99}
BUDGET=${AM2_SA_PIXELS:-600}
WORK=${TMPDIR:-/tmp}/samenu.$$
PREFIX="$PWD/.wine"
GAMEDIR="$PREFIX/drive_c/GOG Games/Army Men II"
mkdir -p "$WORK"

say() { printf '%s\n' "$*"; }
# Kill by PID, never by pattern. CLAUDE.md records a pattern kill taking a
# login session down, and a `pkill -f` here reached past its target twice
# while this script was being written.
SA_PID=""
# The kill must be tolerant: `set -e` aborts on a failing last command
# in an && list, and the process is often already gone -- which made
# the script exit 1 after printing that it had PASSED.
# Killing SA_PID alone is NOT enough: that is the `wine explorer` wrapper,
# and the game it started survives it -- three of them accumulated across
# runs, each still holding ArmyMenMutex, which silently makes the NEXT
# injected run exit and made tools/ab.sh compare two failed drives and call
# them clean. The pattern is safe here in a way it is not from an interactive
# shell: this script's own command line is `sh tools/samenu.sh` and contains
# no such literal, so it cannot match itself.
cleanup() {
    [ -n "$SA_PID" ] && kill "$SA_PID" 2>/dev/null
    pkill -f 'am2port\.exe' 2>/dev/null
    return 0
}
# Preserve the status: an EXIT trap whose last command is a kill can
# otherwise decide the script's exit code, which made a PASSING run
# report failure.
trap 'st=$?; cleanup; exit $st' EXIT

# The game holds ArmyMenMutex, so a survivor from a previous run makes the
# next one exit silently -- CLAUDE.md records this costing a session.
AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1 || true
sleep 2
if [ "$(pgrep -c -f 'ArmyMen2[.]ex[e]' || true)" != "0" ]; then
    say "samenu: VOID -- a game process survived the stop"; exit 1
fi

say "samenu: building the standalone"
make -s standalone >/dev/null

# ---- half one: the injected build, which is the reference ----------------
say "samenu: injected half"
AM2_DISPLAY=$DISP tools/drive.sh start >"$WORK/inj.out" 2>&1
sleep 8
DISPLAY=$DISP import -window root "$WORK/inj.png" 2>/dev/null
LOGPATH=$(DISPLAY=$DISP make -s config 2>/dev/null | sed -n "s/^LOGPATH='\(.*\)'$/\1/p")
# Both sides go through the SAME normalisation, or a stray carriage return
# on one of them reads as a behavioural difference. The game's loading bars
# end in CR, which is why they are dropped rather than trimmed.
norm() { tr -d '\r' | grep -vE '^[[:space:]]*$' | sed 's/[[:space:]]*$//'; }
grep -vE '^(patch:|am2hook|verify:|dinput:|control:|gamelog:|====)' "$LOGPATH" \
    | grep -v '^\]' | norm > "$WORK/inj.log" || true
AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1
sleep 2

# ---- half two: the standalone, which is the thing under test -------------
say "samenu: standalone half"
cp build/ArmyMen2.exe "$GAMEDIR/am2port.exe"
rm -f "$GAMEDIR/am2port.log"
( cd "$GAMEDIR" && WINEPREFIX="$PREFIX" DISPLAY=$DISP WINEDEBUG=-all \
    nohup wine explorer /desktop=samenu,1024x768 \
    "C:\\GOG Games\\Army Men II\\am2port.exe" -nointro \
    >"$WORK/sa.out" 2>&1 & echo $! > "$WORK/sa.pid" ) || true
# `set -e` aborts on a failing command substitution, and the pid file
# races with the backgrounded subshell that writes it -- which made an
# early silent exit look like the budget check failing.
SA_PID=$(cat "$WORK/sa.pid" 2>/dev/null || true)
sleep 26
DISPLAY=$DISP import -window root "$WORK/sa.png" 2>/dev/null
norm < "$GAMEDIR/am2port.log" > "$WORK/sa.log" 2>/dev/null || true
# The EXIT trap does the killing. Doing it HERE ended the script before the
# comparison ran, silently, which read as the budget check failing.

# ---- compare -------------------------------------------------------------
rc=0
if [ ! -s "$WORK/sa.log" ]; then
    say "samenu: FAIL -- the standalone wrote no log; it did not start"; exit 1
fi
if grep -q '^GAP:' "$WORK/sa.log"; then
    say "samenu: FAIL -- a seam this build has not redefined was called:"
    grep '^GAP:' "$WORK/sa.log" | sed 's/^/    /'
    rc=1
fi
if diff -u "$WORK/inj.log" "$WORK/sa.log" >"$WORK/log.diff"; then
    say "samenu: log identical ($(wc -l < "$WORK/sa.log" | tr -d ' ') game messages)"
else
    say "samenu: FAIL -- the game's own log differs:"; sed 's/^/    /' "$WORK/log.diff"; rc=1
fi

./.venv/bin/python - "$WORK/inj.png" "$WORK/sa.png" "$BUDGET" <<'PY' || rc=1
import sys
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert("RGB").crop((0, 0, 640, 480))
b = Image.open(sys.argv[2]).convert("RGB").crop((0, 0, 640, 480))
budget = int(sys.argv[3])
diff = ImageChops.difference(a, b)
n = sum(diff.convert("L").point(lambda v: 1 if v else 0).histogram()[1:])
colours = len(b.getcolors(maxcolors=1 << 20) or [])
print("samenu: title screen %d of 307200 pixels differ (budget %d), "
      "%d colours" % (n, budget, colours))
if colours < 64:
    print("samenu: FAIL -- the standalone frame is nearly blank, so it is "
          "not showing the menu")
    raise SystemExit(1)
raise SystemExit(1 if n > budget else 0)
PY

[ $rc -eq 0 ] && say "samenu: the standalone reaches the main menu and matches" \
              || say "samenu: FAILED"
exit $rc
