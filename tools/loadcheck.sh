#!/bin/sh
# loadcheck.sh -- does LOADING A SAVED GAME still work, and match the original?
#
#     tools/loadcheck.sh
#
# NOTHING IN THE SUITE LOADS A SAVE, which is how LoadGameProcSection came to
# be missing the two stores that make a load happen at all. `ab.sh campaign`
# drives to a mission by clicking NEW; no configuration ever pressed LOAD. The
# defect was invisible for as long as that was true, and worse than invisible:
# with the load flag left clear the mission started fresh and MissionStartup's
# retry stamp SAVED OVER the file the player asked to load.
#
# So this drives the real thing -- SINGLE PLAYER, the player row, SELECT, the
# save row, LOAD -- on our build and on the original under AM2_NOPATCH=1, from
# the same restored save each time, and compares four things:
#
#   the process is still alive        (a load must not take the game down)
#   the sub-state matches             (both reach the same screen)
#   the registered object count matches
#   the save file's md5 is UNCHANGED  (loading must not write)
#
# THE MD5 IS THE SHARPEST OF THE FOUR and it is the one that catches the
# original defect: a load that silently becomes a fresh start rewrites the
# slot, so the file moving is the whole bug in one line. The fixture is
# restored before each half, because otherwise the second half reads what the
# first wrote -- an earlier comparison was taken minutes apart and the file
# had moved under it, which made a build difference out of nothing.
#
# BOTH HALVES USE THE SAME ARGUMENTS, and that is not decoration. `-dbg` gates
# the pause that decides whether the mission comes up held at the briefing,
# and running one half with it and one without produced four commits of wrong
# conclusions before it was noticed. drive.sh's default is `-nointro -dbg`;
# this passes nothing so both halves take it.
set -e

DISP=${AM2_DISPLAY:-:99}
PORT=31436
WORK=${TMPDIR:-/tmp}/loadcheck.$$
G="$PWD/.wine/drive_c/GOG Games/Army Men II"
SAVE="$G/save/sarge/map1_mission1.sav"
mkdir -p "$WORK"

say() { printf '%s\n' "$*"; }
cleanup() {
    AM2_DISPLAY=$DISP tools/drive.sh stop >/dev/null 2>&1 || true
    return 0
}
trap 'st=$?; cleanup; exit $st' EXIT

ctl()  { printf '%s\n' "$1" | timeout 8 nc 127.0.0.1 "$PORT" 2>/dev/null | head -1; }
hold() { DISPLAY=$DISP xdotool mousedown 1; sleep 0.6; DISPLAY=$DISP xdotool mouseup 1; }
click(){ ctl "cursor $1 $2" >/dev/null; hold; }

[ -f "$SAVE" ] || { say "loadcheck: no save fixture at $SAVE"; exit 1; }
cp "$SAVE" "$WORK/baseline.sav"

# One side. Echoes "<alive> <substate> <objects> <md5>".
run_side() {
    cleanup; sleep 3
    cp "$WORK/baseline.sav" "$SAVE"
    if [ -n "$1" ]; then
        AM2_DISPLAY=$DISP tools/drive.sh start 25 "$1" >"$WORK/$2.out" 2>&1 &
    else
        AM2_DISPLAY=$DISP tools/drive.sh start >"$WORK/$2.out" 2>&1 &
    fi
    sleep 46
    click 306 182; sleep 6
    click 240 177; sleep 4
    click 455 221; sleep 6
    click 240 173; sleep 3
    click 455 221; sleep 26
    alive=$(pgrep -c -f 'ArmyMen2[.]exe' || true)
    sub=$(ctl "peek 0x00511DBC 1" | awk '{print $2}')
    objs=$(./.venv/bin/python tools/objdump.py --port "$PORT" --table 2>/dev/null \
           | sed -n 's/^registered \([0-9]*\)$/\1/p')
    md5=$(md5sum "$SAVE" | cut -c1-32)
    printf '%s %s %s %s' "${alive:-0}" "${sub:-none}" "${objs:-0}" "$md5"
}

command -v xdotool >/dev/null || { say "loadcheck: xdotool is required"; exit 1; }
base=$(md5sum "$WORK/baseline.sav" | cut -c1-32)

say "loadcheck: reconstruction"
set -- $(run_side "" recon); RA=$1; RS=$2; RO=$3; RM=$4
say "loadcheck:   alive=$RA substate=$RS objects=$RO md5=$(echo "$RM" | cut -c1-8)"

say "loadcheck: original (AM2_NOPATCH=1)"
set -- $(run_side "AM2_NOPATCH=1" orig); OA=$1; OS=$2; OO=$3; OM=$4
say "loadcheck:   alive=$OA substate=$OS objects=$OO md5=$(echo "$OM" | cut -c1-8)"

rc=0
# The ORIGINAL half is the control: if it did not load, the drive is broken
# and this run compares nothing -- the VOID arm samission.sh carries.
if [ "$OA" = "0" ] || [ "$OO" -lt 100 ]; then
    say "loadcheck: VOID -- the ORIGINAL did not reach a loaded mission"
    say "           (alive=$OA objects=$OO), so the drive is broken, not the port."
    rc=1
else
    [ "$RA" = "0" ] && { say "loadcheck: FAIL -- our build died loading the save"; rc=1; }
    [ "$RS" = "$OS" ] || { say "loadcheck: FAIL -- sub-states differ: $RS vs $OS"; rc=1; }
    [ "$RO" = "$OO" ] || { say "loadcheck: FAIL -- object counts differ: $RO vs $OO"; rc=1; }
    [ "$RM" = "$base" ] || { say "loadcheck: FAIL -- OUR load REWROTE the save"; rc=1; }
    [ "$OM" = "$base" ] || { say "loadcheck: NOTE -- the original rewrote it too"; }
fi

cp "$WORK/baseline.sav" "$SAVE"
[ $rc -eq 0 ] && say "loadcheck: loading a save matches the original" \
              || say "loadcheck: FAILED"
exit $rc
