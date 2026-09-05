#!/bin/sh
# enterlevel.sh -- enter a mission from a save, frozen, on any build.
#
#     tools/enterlevel.sh [-b dev|orig|recon] [-f FOLDER] SAVE.sav
#     tools/enterlevel.sh --stop
#
# The game's own SAVE GAME is the one snapshot that crosses runs and builds
# (tools/savecheck.sh is the proof), and a load of it is how a level is
# entered at will. Two harness pieces turn that into a fixed point:
#
#   `loadgame FOLDER FILE` on the control socket makes the LOAD button's
#   four writes -- folder into the game-proc block, file name, the load
#   pending flag, a request for state 2 -- so no menu is driven and no
#   coordinate is trusted. It takes AM2_NOPATCH=1 unchanged.
#
#   AM2_PAUSE_ON_ENTER=1 raises the -dbg pause reason the moment the game
#   state becomes 2, which the game does on a fresh start and not after a
#   load, so the mission arrives FROZEN before its first frame.
#
# Measured: one campaign save loaded twice by the dev binary and twice by
# the original gives 327 objects and one table md5; Boot Camp's save --
# which the dev binary's F5 writes into save\bootcamp, the folder string
# being empty there and F5 filling it with the level's name -- loaded at
# the title screen by all three builds gives 1,612 objects and one md5.
#
# FOLDER is what the game itself would put in the block: the player's name
# for a campaign save, `bootcamp` for Boot Camp. It defaults from SAVE's
# parent directory. SAVE is copied to save/FOLDER/enter.sav, and anything
# already in that folder is left alone -- no list is driven, so nothing
# needs to be the only row. --stop kills the game and removes enter.sav.
#
#   -b dev     build/armymen2-dev on the display (the default)
#   -b orig    the original under Wine, AM2_NOPATCH=1
#   -b recon   the injected reconstruction under Wine
#
# All three take `-nointro -dbg`, drive.sh's default; docs/saveload.md
# records what mismatched halves cost.
set -e

DISP=${AM2_DISPLAY:-:99}
REPO=$(cd "$(dirname "$0")/.." && pwd)
G="$REPO/.wine/drive_c/GOG Games/Army Men II"
PY="$REPO/.venv/bin/python"
[ -x "$PY" ] || PY=python3
BUILD=dev
FOLDER=
STASH="${TMPDIR:-/tmp}/enterlevel.stash"

say() { printf '%s\n' "$*"; }

if [ "$1" = --stop ]; then
    [ -f "$STASH/pid" ] && kill "$(cat "$STASH/pid")" 2>/dev/null || true
    AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" stop >/dev/null 2>&1 || true
    [ -f "$STASH/placed" ] && rm -f "$(cat "$STASH/placed")"
    rm -rf "$STASH"
    exit 0
fi

while [ $# -gt 1 ]; do
    case "$1" in
        -b) BUILD=$2; shift 2 ;;
        -f) FOLDER=$2; shift 2 ;;
        *)  say "enterlevel: unknown option $1"; exit 2 ;;
    esac
done
SAVE=$1
[ -f "$SAVE" ] || { say "enterlevel: no such save: $SAVE"; exit 2; }
[ -n "$FOLDER" ] || FOLDER=$(basename "$(cd "$(dirname "$SAVE")" && pwd)")
SAVEDIR="$G/save/$FOLDER"

case "$BUILD" in
    dev)   PORT=31337 ;;
    orig|recon) PORT=31436 ;;
    *) say "enterlevel: -b takes dev, orig or recon"; exit 2 ;;
esac
[ -d "$STASH" ] && { say "enterlevel: a previous run is still up; run --stop first"; exit 2; }
mkdir -p "$STASH" "$SAVEDIR"
cp "$SAVE" "$SAVEDIR/enter.sav"
printf '%s' "$SAVEDIR/enter.sav" >"$STASH/placed"

ctl()  { "$PY" "$REPO/tools/am2ctl.py" --port "$PORT" "$@" 2>/dev/null | head -1; }
peek() { ctl "peek $1 1" | awk '{print $2}'; }

case "$BUILD" in
    dev)
        [ -x "$REPO/build/armymen2-dev" ] || { say "enterlevel: run make native-dev first"; exit 1; }
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

r=$(ctl "loadgame $FOLDER enter.sav")
case "$r" in
    ok*) ;;
    *)   say "enterlevel: $r"; exit 1 ;;
esac
i=0
while [ $i -lt 15 ]; do
    sleep 3
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
say "enterlevel: $BUILD loaded $(basename "$SAVE") as save/$FOLDER: $n objects, table md5 $(md5sum "$STASH/table" | cut -c1-12), clock $(peek 0x00511E04), paused on port $PORT"
say "enterlevel: table in $STASH/table; tools/enterlevel.sh --stop when done"
