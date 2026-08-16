#!/bin/bash
# Drive the game on a headless X display, to exercise code paths that only run
# during real gameplay. Reaching the title screen touches almost none of the
# engine, so any useful survey has to get into a mission first.
#
#   tools/drive.sh start [secs]     launch on :99 and wait for the title screen
#   tools/drive.sh shot NAME        screenshot the display
#   tools/drive.sh click X Y NAME   click, settle, screenshot
#   tools/drive.sh key KEY NAME     send a key, settle, screenshot
#   tools/drive.sh log              tail the game's recovered debug output
#   tools/drive.sh stop             kill the game
#
# Launching goes through `make run` rather than a second copy of the wine
# command line, so there stays exactly one place that knows how to start the
# game. Pass make variables through as usual:
#
#   tools/drive.sh start 25 OBSERVE=1
#
# The game renders 640x480 at the top-left of the 1024x768 root, so root
# coordinates and game coordinates are the same thing.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SHOTS="${AM2_SHOTS:-$REPO/build/shots}"
DISP="${AM2_DISPLAY:-:99}"
SETTLE="${AM2_SETTLE:-3}"
LOG="$REPO/.wine/drive_c/GOG Games/Army Men II/am2.log"

export DISPLAY="$DISP"

mkdir -p "$SHOTS" || exit 1

shot() {
    sleep "$SETTLE"
    import -display "$DISP" -window root "$SHOTS/$1.png" 2>/dev/null
    echo "$SHOTS/$1.png"
}

cmd="${1:-}"
[ $# -gt 0 ] && shift

case "$cmd" in
start)
    wait_for="${1:-20}"
    [ $# -gt 0 ] && shift
    if ! xdpyinfo -display "$DISP" >/dev/null 2>&1; then
        Xvfb "$DISP" -screen 0 1024x768x24 >/dev/null 2>&1 &
        sleep 2
    fi
    rm -f "$LOG"
    ( cd "$REPO" && make -s run "$@" >/dev/null 2>&1 & )
    sleep "$wait_for"
    shot 00-start
    ;;
shot)
    shot "${1:-shot}"
    ;;
click)
    xdotool mousemove --sync "$1" "$2" click 1
    shot "${3:-click-$1-$2}"
    ;;
key)
    xdotool key "$1"
    shot "${2:-key-$1}"
    ;;
log)
    tail -n "${1:-40}" "$LOG"
    ;;
stop)
    # The bracket keeps the pattern from matching this script's own command
    # line -- `pkill -f ArmyMen2.exe` cheerfully kills the shell running it.
    pkill -f 'ArmyMen2[.]exe' 2>/dev/null
    pkill -f 'explorer [/]desktop' 2>/dev/null
    sleep 2
    echo "stopped (remaining: $(pgrep -cf 'ArmyMen2[.]exe'))"
    ;;
*)
    sed -n '2,20p' "$0"
    exit 1
    ;;
esac
