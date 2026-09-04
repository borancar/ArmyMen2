#!/bin/sh
# saquit.sh -- does the STANDALONE shut down the way the injected build does?
#
# samenu.sh compares a title screen and samission.sh compares a live mission's
# object table. Neither reaches the TEARDOWN: both kill the process, so the
# comm shutdown, the sprite frees and the leak report never run.
#
#     tools/saquit.sh
#
# That path is worth a check of its own. tools/ab.sh quit exists for the same
# reason on the injected side and found a real bug the first time it ran --
# ReleaseSprite logging an error the original never logged -- because nothing
# had ever executed those functions. For the standalone there is more at
# stake: its C++ static initializers ran from our own runtime rather than the
# MSVC CRT's, so anything they registered comes down here or not at all.
#
# THE GAME'S OWN MESSAGES ARE WHAT IS COMPARED. The injected build also logs
# harness lines -- the attach banner, the patch list, the DirectInput hook,
# the detach -- and the standalone has none of them by construction, so those
# are filtered rather than treated as differences.
#
# TWO LINES ARE NOT ORDER-STABLE AND ONE CAN BE ABSENT, which CLAUDE.md
# records from ab.sh quit: the packet thread and the receive thread each log
# as they finish and swap places between runs, and the receive thread
# sometimes exits before it logs at all. They are compared as a SET, and a
# single missing line here is a re-run rather than a result.
set -e

DISP=${AM2_DISPLAY:-:99}
SA_PORT=${AM2_SA_PORT:-31500}
WORK=${TMPDIR:-/tmp}/saquit.$$
PREFIX="$PWD/.wine"
GAMEDIR="$PREFIX/drive_c/GOG Games/Army Men II"
mkdir -p "$WORK"

say() { printf '%s\n' "$*"; }
cleanup() {
    AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1 || true
    pkill -f 'am2port[.]exe' 2>/dev/null || true
    return 0
}
trap 'st=$?; cleanup; exit $st' EXIT

hold() { DISPLAY=$DISP xdotool mousedown 1; sleep 0.6; DISPLAY=$DISP xdotool mouseup 1; }
ctl()  { printf '%s\n' "$1" | timeout 5 nc 127.0.0.1 "$2" 2>/dev/null | head -1; }

# QUIT on the title screen, then OK on CONFIRM GAME EXIT.
quit_it() {
    ctl "cursor 306 383" "$1" >/dev/null; hold; sleep 5
    ctl "cursor 475 224" "$1" >/dev/null; hold; sleep 10
}

# The harness's own lines, which the standalone cannot produce.
norm() {
    grep -vE '^(patch|control|hook|trace|dinput|gamelog|verify):' "$1" \
        | grep -vE '^(====|am2hook)' | sed '/^[[:space:]]*$/d'
}
# The two racing thread lines, compared as a set.
RACY='Packet Thread Exited|Receive thread got event'

cleanup; sleep 3
say "saquit: building"
make -s standalone >/dev/null

say "saquit: injected half"
AM2_DISPLAY=$DISP tools/drive.sh start >"$WORK/inj.out" 2>&1
sleep 8
INJ_PORT=$(DISPLAY=$DISP make -s config 2>/dev/null | sed -n "s/^CTLPORT='\(.*\)'$/\1/p")
[ -n "$INJ_PORT" ] || INJ_PORT=31436
INJ_LOG=$(DISPLAY=$DISP make -s config 2>/dev/null | sed -n "s/^LOGPATH='\(.*\)'$/\1/p")
quit_it "$INJ_PORT"
alive_inj=$(pgrep -c -f 'ArmyMen2[.]exe' || true)
cp "$INJ_LOG" "$WORK/inj.log" 2>/dev/null || : > "$WORK/inj.log"

say "saquit: standalone half"
cp build/ArmyMen2.exe "$GAMEDIR/am2port.exe"
rm -f "$GAMEDIR/am2port.log"
( cd "$GAMEDIR" && WINEPREFIX="$PREFIX" DISPLAY=$DISP WINEDEBUG=-all \
    AM2_CONTROL=1 AM2_CTL_PORT="$SA_PORT" \
    nohup wine explorer /desktop=saquit,1024x768 \
    "C:\\GOG Games\\Army Men II\\am2port.exe" -nointro \
    >"$WORK/sa.out" 2>&1 & ) || true
sleep 30
quit_it "$SA_PORT"
alive_sa=$(pgrep -c -f 'am2port[.]exe' || true)
cp "$GAMEDIR/am2port.log" "$WORK/sa.log" 2>/dev/null || : > "$WORK/sa.log"

rc=0
# An empty log means the run never started, and two of those diff as equal --
# the VOID arm samission.sh already carries for the same reason.
for f in inj sa; do
    n=$(norm "$WORK/$f.log" | wc -l)
    if [ "$n" -lt 5 ]; then
        say "saquit: VOID -- the $f log has $n game lines, so that half never"
        say "        ran. Check for a process holding ArmyMenMutex."
        rc=1
    fi
done

if [ "$alive_inj" != "0" ] || [ "$alive_sa" != "0" ]; then
    say "saquit: FAIL -- a process survived the quit (inj=$alive_inj sa=$alive_sa)"
    rc=1
fi

if [ $rc -eq 0 ]; then
    for f in inj sa; do
        norm "$WORK/$f.log" | grep -vE "$RACY" > "$WORK/$f.body"
        norm "$WORK/$f.log" | grep -E "$RACY" | sort > "$WORK/$f.racy"
    done
    if diff -u "$WORK/inj.body" "$WORK/sa.body" > "$WORK/body.diff"; then
        say "saquit: teardown log identical ($(wc -l < "$WORK/inj.body" \
             | tr -d ' ') game messages)"
    else
        say "saquit: FAIL -- the teardown logs differ:"
        head -20 "$WORK/body.diff" | sed 's/^/    /'
        rc=1
    fi
    if ! diff -q "$WORK/inj.racy" "$WORK/sa.racy" >/dev/null; then
        say "saquit: NOTE -- the two racing thread lines differ as a set;"
        say "        one thread can exit before it logs. Re-run before"
        say "        believing this."
    fi
    if grep -q 'Unreleased memory (0) blocks' "$WORK/sa.log"; then
        say "saquit: standalone reports 0 unreleased blocks"
    else
        say "saquit: FAIL -- the standalone did not report a clean heap"
        rc=1
    fi
fi

[ $rc -eq 0 ] && say "saquit: the standalone shuts down like the injected build" \
              || say "saquit: FAILED"
exit $rc
