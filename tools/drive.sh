#!/bin/bash
# Drive the game on a headless X display, to exercise code paths that only run
# during real gameplay. Reaching the title screen touches almost none of the
# engine, so any useful survey has to get into a mission first.
#
#   tools/drive.sh start [secs] [VAR=VAL...]   launch and wait for the title
#   tools/drive.sh shot NAME                   screenshot
#   tools/drive.sh ctl <command...>            send a control-socket command
#   tools/drive.sh press KEY NAME              tap a key, settle, screenshot
#   tools/drive.sh log [n]                     tail this instance's log
#   tools/drive.sh stop                        kill just this instance
#
# Instances are independent. Everything a concurrent run could collide on --
# desktop name, control port, log file, screenshot directory -- is derived from
# ID by the Makefile, and sourced here rather than re-derived, so the two cannot
# drift apart. ID defaults from $DISPLAY, so a headless run and a desktop run
# are independent automatically:
#
#   AM2_DISPLAY=:99 tools/drive.sh start          # ID 99, port 31436
#   AM2_DISPLAY=:0  tools/drive.sh start          # ID 0,  port 31337
#   AM2_DISPLAY=:99 AM2_ID=7 tools/drive.sh start # a second one on :99
#
# Each run gets its own process group, so `stop` kills that instance alone and
# leaves any other running game untouched.
#
# The game renders 640x480 at the top-left of the root window, so root
# coordinates and game coordinates coincide.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DISP="${AM2_DISPLAY:-:99}"
SETTLE="${AM2_SETTLE:-3}"

export DISPLAY="$DISP"

# These have to reach `make config` as well as `make run`, or the paths this
# script reports would disagree with the ones the game actually uses.
MAKEVARS=()
[ -n "${AM2_ID:-}" ] && MAKEVARS+=("ID=$AM2_ID")
[ -n "${AM2_ISOLATE:-}" ] && MAKEVARS+=("ISOLATE=$AM2_ISOLATE")
[ -n "${AM2_MAKEVARS:-}" ] && MAKEVARS+=($AM2_MAKEVARS)

# Single source of truth: ask the Makefile what this instance is called.
eval "$(cd "$REPO" && make -s config "${MAKEVARS[@]+"${MAKEVARS[@]}"}")"

PIDFILE="$REPO/build/run-$ID.pid"
mkdir -p "$SHOTS" "$REPO/build" || exit 1

shot() {
    sleep "$SETTLE"
    import -display "$DISP" -window root "$SHOTS/$1.png" 2>/dev/null
    echo "$SHOTS/$1.png"
}

ctl() {
    "$REPO/tools/am2ctl.py" --port "$CTLPORT" "$@"
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
    : > "$LOGPATH"
    # setsid gives the run its own process group, so stop can target exactly
    # this instance instead of every ArmyMen2 on the machine.
    ( cd "$REPO" && setsid make -s run "${MAKEVARS[@]+"${MAKEVARS[@]}"}" "$@" \
        >/dev/null 2>&1 & echo $! > "$PIDFILE" )
    echo "instance ID=$ID port=$CTLPORT desktop=$DESKNAME log=$LOGFILE"
    sleep "$wait_for"
    shot 00-start
    ;;
shot)
    shot "${1:-shot}"
    ;;
ctl)
    ctl "$@"
    ;;
press)
    ctl key "$1" tap
    shot "${2:-key-$1}"
    ;;
log)
    tail -n "${1:-40}" "$LOGPATH"
    ;;
stop)
    if [ -f "$PIDFILE" ]; then
        pgid="$(cat "$PIDFILE")"
        kill -TERM -- "-$pgid" 2>/dev/null
        sleep 2
        kill -KILL -- "-$pgid" 2>/dev/null
        rm -f "$PIDFILE"
    fi
    # The desktop is named per instance, so this reaches only our explorer --
    # then walk down to the launcher and the game beneath it. Killing the
    # explorer alone leaves them running, and a surviving game keeps holding
    # ArmyMenMutex, which silently makes the next run in that prefix exit.
    for top in $(pgrep -f "desktop=$DESKNAME" 2>/dev/null); do
        kids="$top"
        for _ in 1 2 3; do
            kids="$kids $(pgrep -P $(echo "$kids" | tr ' ' ',') 2>/dev/null | tr '\n' ' ')"
        done
        kill -KILL $kids 2>/dev/null
    done
    # A dedicated prefix has its own wineserver, so it is safe to take down
    # wholesale. The shared one is not -- another run may be using it.
    if [ "${AM2_ISOLATE:-0}" = "1" ] && [ -d "$REPO/.wine-$ID" ]; then
        WINEPREFIX="$REPO/.wine-$ID" wineserver -k 2>/dev/null
    fi
    sleep 1
    echo "stopped ID=$ID (remaining game processes: $(pgrep -cf 'ArmyMen2[.]exe'))"
    ;;
stop-all)
    # Deliberately blunt: every instance, every prefix under this repo.
    pkill -KILL -f 'ArmyMen2[.]exe' 2>/dev/null
    pkill -KILL -f 'desktop=amii' 2>/dev/null
    for p in "$REPO"/.wine "$REPO"/.wine-*; do
        [ -d "$p" ] && WINEPREFIX="$p" wineserver -k 2>/dev/null
    done
    rm -f "$REPO"/build/run-*.pid
    sleep 2
    echo "stopped all (remaining: $(pgrep -cf 'ArmyMen2[.]exe'))"
    ;;
*)
    sed -n '2,30p' "$0"
    exit 1
    ;;
esac
