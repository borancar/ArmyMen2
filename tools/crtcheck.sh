#!/bin/bash
# tools/crtcheck.sh -- the reconstructed CRT's stdio against the original's,
# in one process and with no Wine: build/armymen2-hybrid-dev runs the
# original CRT over src/platform, and with AM2_CRTCHECK set it links the
# reconstruction beside it and runs src/hybrid/crtcheck.cpp instead of the
# game. Headless, seconds, exit 0 when nothing differs.
#
#   tools/crtcheck.sh            # after: make hybrid-dev
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
BIN="$REPO/build/armymen2-hybrid-dev"
GAMEDIR="$REPO/.wine/drive_c/GOG Games/Army Men II"
[ -x "$BIN" ] || { echo "crtcheck: $BIN is missing (make hybrid-dev)" >&2; exit 2; }
[ -f "$GAMEDIR/ArmyMen2.exe" ] || { echo "crtcheck: no ArmyMen2.exe under $GAMEDIR" >&2; exit 2; }
DIR=$(mktemp -d "${TMPDIR:-/tmp}/am2crtcheck.XXXXXX") || exit 2
env SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy AM2_GAMEDIR="$GAMEDIR" \
    AM2_CRTCHECK="$DIR" AM2_LOG=/dev/null "$BIN" -nointro
rc=$?
rm -rf "$DIR"
exit $rc
