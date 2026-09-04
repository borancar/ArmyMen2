#!/bin/sh
# movecheck.sh -- does the player actually MOVE under real input?
#
# This is the configuration the suite did not have, and its absence let a
# transcription error stand for a month: StepType2's player-input gate was
# written as a test for null where the original compares the followed object
# against THIS object, so no key and no click moved anything.
#
#     tools/movecheck.sh
#
# WHY NOTHING ELSE CATCHES IT. Every other configuration drives input through
# the control socket, which WRITES THE GAME'S GLOBALS directly -- `cursor` and
# `key` never touch a device. So the whole DirectInput-to-gameplay path is
# unexercised, and worse, an A/B over it is silent by construction: both sides
# receive the same input and agree about ignoring it. bootcamp's object dump
# is taken at the briefing, before anything moves; mission's pixel check is
# disabled; and every counter on that path is blind, its callers being ours.
#
# So this drives REAL X EVENTS through xdotool, which reach the game the way a
# player's keyboard does -- X -> Wine -> DirectInput -- and asks the only
# question that matters: did the unit's position change?
#
# THE ASSERTION IS DISPLACEMENT, NOT EQUALITY, and that is deliberate. Two
# unsynchronised runs cannot be expected to leave Sarge on the same tile: they
# see different frame deltas, so he walks for different lengths of time. What
# IS comparable is whether he moved at all. The failure this guards is zero
# against hundreds, which no budget has to be tuned to see.
set -e

DISP=${AM2_DISPLAY:-:99}
PORT=31436
SARGE=000003e8
MIN=${AM2_MOVE_MIN:-20}          # world units; the bug gives exactly 0
WORK=${TMPDIR:-/tmp}/movecheck.$$
mkdir -p "$WORK"

say() { printf '%s\n' "$*"; }
cleanup() {
    AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1 || true
    pkill -f 'ArmyMen2[.]exe' 2>/dev/null || true
    return 0
}
trap 'st=$?; cleanup; exit $st' EXIT

ctl()  { printf '%s\n' "$1" | timeout 5 nc 127.0.0.1 "$PORT" 2>/dev/null | head -1; }
hold() { DISPLAY=$DISP xdotool mousedown 1; sleep 0.7; DISPLAY=$DISP xdotool mouseup 1; }
pos()  { ./.venv/bin/python tools/objdump.py --port "$PORT" --table 2>/dev/null \
         | grep "^$SARGE" | sed -n 's/.*pos=\([0-9]*\),\([0-9]*\).*/\1 \2/p'; }

drive_in() {
    ctl "cursor 306 143" >/dev/null; hold; sleep 8
    ctl "key RETURN tap" >/dev/null; sleep 5
    ctl "cursor 476 224" >/dev/null; hold; sleep 5
    ctl "cursor 300 300" >/dev/null; hold; sleep 6
}

# One side: start, drive in, hold two keys through REAL input, report the
# distance walked.  Echoes "<dist> <substate>".
run_side() {
    extra=$1
    cleanup; sleep 3
    if [ -n "$extra" ]; then
        AM2_DISPLAY=$DISP tools/drive.sh start 25 "$extra" >"$WORK/$2.out" 2>&1 &
    else
        AM2_DISPLAY=$DISP tools/drive.sh start >"$WORK/$2.out" 2>&1 &
    fi
    sleep 48
    drive_in
    sub=$(ctl "peek 0x00511DBC 1" | awk '{print $2}')
    set -- $(pos); ax=${1:-0}; ay=${2:-0}
    for k in Up Left Down; do
        DISPLAY=$DISP xdotool keydown $k; sleep 2
        DISPLAY=$DISP xdotool keyup $k; sleep 1
    done
    set -- $(pos); bx=${1:-0}; by=${2:-0}
    dist=$(awk -v a=$ax -v b=$ay -v c=$bx -v d=$by \
           'BEGIN{printf "%d", sqrt((c-a)^2+(d-b)^2)}')
    printf '%s %s' "$dist" "$sub"
}

command -v xdotool >/dev/null || { say "movecheck: xdotool is required"; exit 1; }

say "movecheck: reconstruction"
set -- $(run_side "" recon); RD=$1; RS=$2
say "movecheck:   sub-state $RS, walked $RD world units"

say "movecheck: original (AM2_NOPATCH=1)"
set -- $(run_side "AM2_NOPATCH=1" orig); OD=$1; OS=$2
say "movecheck:   sub-state $OS, walked $OD world units"

rc=0
[ "$RS" = "$OS" ] || { say "movecheck: FAIL -- sub-states differ ($RS vs $OS)"; rc=1; }
if [ "$OD" -lt "$MIN" ]; then
    say "movecheck: VOID -- the ORIGINAL walked only $OD units, so the drive"
    say "           never reached live play and this run compares nothing."
    rc=1
elif [ "$RD" -lt "$MIN" ]; then
    say "movecheck: FAIL -- the reconstruction walked $RD units against the"
    say "           original's $OD. Real input is not reaching gameplay."
    rc=1
fi

[ $rc -eq 0 ] && say "movecheck: the player moves under real input ($RD vs $OD)" \
              || say "movecheck: FAILED"
exit $rc
