#!/bin/sh
# enterlevel.sh -- enter a mission from a save, frozen, on any build.
#
#     tools/enterlevel.sh [-b dev|orig|recon] [-p PLAYER] SAVE.sav
#
# The game's own SAVE GAME is the one snapshot that crosses runs and builds
# (tools/savecheck.sh is the proof), and a LOAD of it is how a level is
# entered at will. What made that a verification point rather than a
# starting line is AM2_PAUSE_ON_ENTER: the harness raises the -dbg pause
# reason the moment the game state becomes 2, so the loaded mission arrives
# FROZEN before its first frame steps anything -- two loads of one file give
# object tables identical in every field. Measured: 327 objects, 0 lines
# differing between two fresh runs, and 0 five seconds later.
#
# This puts SAVE in the player's save folder as the only file there (the
# others are moved aside and put back by the trap), drives SINGLE PLAYER ->
# the player -> SELECT -> the row -> LOAD, waits for the frozen mission, and
# prints the registered count and an md5 of the object table. The game is
# left RUNNING and paused for whatever comes next -- objdump.py, a snap save,
# a drive -- on the build's control port: 31337 for dev, 31436 for the Wine
# builds. Stop it with `tools/enterlevel.sh --stop` (or drive.sh stop / the
# pid the stash holds).
#
#   -b dev     build/armymen2-dev on the display (the default)
#   -b orig    the original under Wine, AM2_NOPATCH=1
#   -b recon   the injected reconstruction under Wine
#   -p PLAYER  the save folder under save/ (default: sarge)
#
# Both Wine builds and the dev binary take `-nointro -dbg`, drive.sh's
# default; docs/saveload.md records what mismatched halves cost.
set -e

DISP=${AM2_DISPLAY:-:99}
REPO=$(cd "$(dirname "$0")/.." && pwd)
G="$REPO/.wine/drive_c/GOG Games/Army Men II"
PY="$REPO/.venv/bin/python"
[ -x "$PY" ] || PY=python3
BUILD=dev
PLAYER=sarge
STASH="${TMPDIR:-/tmp}/enterlevel.stash"

say() { printf '%s\n' "$*"; }

if [ "$1" = --stop ]; then
    [ -f "$STASH/pid" ] && kill "$(cat "$STASH/pid")" 2>/dev/null || true
    AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" stop >/dev/null 2>&1 || true
    if [ -d "$STASH/saves" ]; then
        rm -f "$(cat "$STASH/dir")/enter.sav"
        for f in "$STASH"/saves/*; do [ -e "$f" ] && mv -f "$f" "$(cat "$STASH/dir")/"; done
        rm -rf "$STASH"
    fi
    exit 0
fi

while [ $# -gt 1 ]; do
    case "$1" in
        -b) BUILD=$2; shift 2 ;;
        -p) PLAYER=$2; shift 2 ;;
        *)  say "enterlevel: unknown option $1"; exit 2 ;;
    esac
done
SAVE=$1
[ -f "$SAVE" ] || { say "enterlevel: no such save: $SAVE"; exit 2; }
SAVEDIR="$G/save/$PLAYER"
mkdir -p "$SAVEDIR"

case "$BUILD" in
    dev)   PORT=31337 ;;
    orig|recon) PORT=31436 ;;
    *) say "enterlevel: -b takes dev, orig or recon"; exit 2 ;;
esac

# Put SAVE alone in the folder, remembering what was there.
[ -d "$STASH" ] && { say "enterlevel: a previous run is still placed; run --stop first"; exit 2; }
mkdir -p "$STASH/saves"
printf '%s' "$SAVEDIR" >"$STASH/dir"
for f in "$SAVEDIR"/*.sav; do [ -e "$f" ] && mv "$f" "$STASH/saves/"; done
cp "$SAVE" "$SAVEDIR/enter.sav"

ctl()   { "$PY" "$REPO/tools/am2ctl.py" --port "$PORT" "$@" 2>/dev/null | head -1; }
click() { ctl "cursor $1 $2" >/dev/null; sleep 0.3; ctl "mouse left tap" >/dev/null; }
peek()  { ctl "peek $1 1" | awk '{print $2}'; }

case "$BUILD" in
    dev)
        cd "$REPO"
        DISPLAY=$DISP SDL_AUDIO_DRIVER=${SDL_AUDIO_DRIVER:-dummy} AM2_GAMEDIR="$G" \
            AM2_PAUSE_ON_ENTER=1 setsid "$REPO/build/armymen2-dev" -nointro -dbg \
            >"$STASH/game.out" 2>&1 &
        sleep 1
        pgrep -n -x armymen2-dev >"$STASH/pid"
        sleep 8 ;;
    orig)
        AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" start 25 AM2_NOPATCH=1 AM2_PAUSE_ON_ENTER=1 \
            >"$STASH/game.out" 2>&1
        sleep 4 ;;
    recon)
        AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" start 25 AM2_PAUSE_ON_ENTER=1 \
            >"$STASH/game.out" 2>&1
        sleep 4 ;;
esac

click 306 182; sleep 6          # SINGLE PLAYER
click 240 177; sleep 4          # the player row
click 455 221; sleep 6          # SELECT
click 240 173; sleep 3          # the one save row
click 455 221                   # LOAD
i=0
while [ $i -lt 12 ]; do
    sleep 4
    [ "$(peek 0x00511DA4)" = 00000002 ] && [ "$(peek 0x00511DBC)" = 00000021 ] && break
    i=$((i + 1))
done
sleep 2
pause=$(peek 0x005122FC)
"$PY" "$REPO/tools/objdump.py" --port "$PORT" --table >"$STASH/table" 2>/dev/null || true
n=$(sed -n 's/^registered \([0-9]*\)$/\1/p' "$STASH/table")
if [ -z "$n" ] || [ "$n" -lt 1 ] 2>/dev/null; then
    say "enterlevel: VOID -- no object table after the load (state $(peek 0x00511DA4), sub-state $(peek 0x00511DBC))"
    exit 1
fi
case "$pause" in
    *8) ;;
    *)  say "enterlevel: NOTE -- the pause reason is not set ($pause); the mission is running" ;;
esac
say "enterlevel: $BUILD loaded $(basename "$SAVE"): $n objects, table md5 $(md5sum "$STASH/table" | cut -c1-12), clock $(peek 0x00511E04), paused on port $PORT"
say "enterlevel: table in $STASH/table; tools/enterlevel.sh --stop when done"
