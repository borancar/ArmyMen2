#!/bin/sh
# samission.sh -- does the STANDALONE build play the same mission as the
# original, object for object?
#
# tools/samenu.sh compares a static title screen. This drives both builds into
# a live Boot Camp mission -- past the briefing and the instruction sign, into
# ordinary play -- and diffs the whole object table with NO budget: 1,609
# objects, each with its type, flags, army, position, tile, both rectangles,
# health, cell count, AI mode and pose. It is the artifact tools/ab.sh already
# treats as its sharpest, and the only one that says anything about the port
# in PLAY rather than at a menu.
#
#     tools/samission.sh
#
# WHY THE CURSOR GOES THROUGH THE SOCKET. Wine's pointer acceleration is
# non-linear, so a relative move lands somewhere other than where it was
# aimed -- CLAUDE.md measures ~1.75x for a 100-pixel step. `cursor X Y` writes
# the GAME's own three globals, so both builds take identical coordinates.
# The BUTTON still comes from xdotool: the standalone has no DirectInput hook,
# so the socket's `mouse` command is inert there, and using the same real
# button on both sides keeps the two drives identical.
#
# The click must be HELD across a poll. The game reads buffered DirectInput
# and a fast tap can fall between two frames, which is why the harness holds
# its own taps rather than timing them.
set -e

DISP=${AM2_DISPLAY:-:99}
SA_PORT=${AM2_SA_PORT:-31500}
WORK=${TMPDIR:-/tmp}/samission.$$
PREFIX="$PWD/.wine"
GAMEDIR="$PREFIX/drive_c/GOG Games/Army Men II"
REPO="$PWD"
mkdir -p "$WORK"

say() { printf '%s\n' "$*"; }
SA_PID=""
cleanup() {
    [ -n "$SA_PID" ] && kill "$SA_PID" 2>/dev/null
    pkill -f 'am2port\.exe' 2>/dev/null
    return 0
}
trap 'st=$?; cleanup; exit $st' EXIT

hold() { DISPLAY=$DISP xdotool mousedown 1; sleep 0.7; DISPLAY=$DISP xdotool mouseup 1; }
ctl()  { printf '%s\n' "$1" | timeout 5 nc 127.0.0.1 "$2" 2>/dev/null | head -1; }

# The same four steps on both sides: BOOT CAMP, the briefing, MESSAGE FROM HQ,
# and one more click for the instruction sign.
drive_in() {
    port=$1
    ctl "cursor 306 143" "$port" >/dev/null; hold; sleep 8
    ctl "key RETURN tap" "$port" >/dev/null; sleep 5
    ctl "cursor 476 224" "$port" >/dev/null; hold; sleep 5
    ctl "cursor 300 300" "$port" >/dev/null; hold; sleep 5
}

AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1 || true
sleep 2
say "samission: building"
make -s standalone >/dev/null

# ---- the injected build, which is the reference -------------------------
say "samission: injected half"
AM2_DISPLAY=$DISP tools/drive.sh start >"$WORK/inj.out" 2>&1
sleep 6
INJ_PORT=$(DISPLAY=$DISP make -s config 2>/dev/null | sed -n "s/^CTLPORT='\(.*\)'$/\1/p")
[ -n "$INJ_PORT" ] || INJ_PORT=31436
drive_in "$INJ_PORT"
sub=$(ctl "peek 0x00511DBC 1" "$INJ_PORT")
say "samission: injected sub-state $sub"
"$REPO/.venv/bin/python" tools/objdump.py --port "$INJ_PORT" --table \
    > "$WORK/inj.table" 2>/dev/null || true
AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1
sleep 2

# ---- the standalone ------------------------------------------------------
say "samission: standalone half"
cp build/ArmyMen2.exe "$GAMEDIR/am2port.exe"
rm -f "$GAMEDIR/am2port.log"
( cd "$GAMEDIR" && WINEPREFIX="$PREFIX" DISPLAY=$DISP WINEDEBUG=-all \
    AM2_CONTROL=1 AM2_CTL_PORT="$SA_PORT" \
    nohup wine explorer /desktop=samission,1024x768 \
    "C:\\GOG Games\\Army Men II\\am2port.exe" -nointro \
    >"$WORK/sa.out" 2>&1 & echo $! > "$WORK/sa.pid" ) || true
SA_PID=$(cat "$WORK/sa.pid" 2>/dev/null || true)
sleep 27
drive_in "$SA_PORT"
sasub=$(ctl "peek 0x00511DBC 1" "$SA_PORT")
say "samission: standalone sub-state $sasub"
"$REPO/.venv/bin/python" tools/objdump.py --port "$SA_PORT" --table \
    > "$WORK/sa.table" 2>/dev/null || true

# ---- compare -------------------------------------------------------------
rc=0
# An empty or tiny dump means the drive never reached the mission, and two of
# those diff as identical -- which is how three wrong conclusions were reached
# before this check existed.
for f in inj sa; do
    n=$(wc -l < "$WORK/$f.table" 2>/dev/null || echo 0)
    if [ "$n" -lt 100 ]; then
        say "samission: VOID -- the $f dump has $n lines, so that drive never"
        say "           reached the mission and this run compares nothing."
        say "           Check for a process holding ArmyMenMutex."
        rc=1
    fi
done
if [ "$sasub" != "$sub" ]; then
    say "samission: FAIL -- sub-states differ: injected $sub, standalone $sasub"
    rc=1
fi
if [ $rc -eq 0 ]; then
    if diff -u "$WORK/inj.table" "$WORK/sa.table" > "$WORK/table.diff"; then
        say "samission: object table IDENTICAL ($(wc -l < "$WORK/sa.table" \
             | tr -d ' ') lines, no budget)"
    else
        say "samission: FAIL -- the live object table differs:"
        head -20 "$WORK/table.diff" | sed 's/^/    /'
        rc=1
    fi
fi

[ $rc -eq 0 ] && say "samission: the standalone plays the same mission" \
              || say "samission: FAILED"
exit $rc
