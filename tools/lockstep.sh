#!/bin/bash
# lockstep.sh -- run two binaries through the same replay under AM2_LOCKSTEP
# and report the first presented frame on which they differ.
#
#     tools/lockstep.sh [-a BIN] [-b BIN] [-o DIR] SCRIPT
#
# A is build/armymen2-hybrid (the original over src/platform) and B is
# build/armymen2 (the reconstruction over the same) unless said otherwise.
# Both run headless -- SDL's dummy video and audio drivers -- with the
# platform's deterministic clock, timers, sound and input (see platform.h,
# AM2_LOCKSTEP), so their frame logs are comparable line for line. On a
# divergence both are run again to dump that frame, and the pixel count and
# bounding box of the difference are printed. Exit 0 when identical, 1 on a
# divergence, 2 when a run failed.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
A="$REPO/build/armymen2-hybrid"; B="$REPO/build/armymen2"; OUT=""
while getopts a:b:o: opt; do
    case $opt in
    a) A=$OPTARG ;; b) B=$OPTARG ;; o) OUT=$OPTARG ;;
    *) echo "usage: $0 [-a BIN] [-b BIN] [-o DIR] SCRIPT" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
SCRIPT=${1:-}
[ -f "$SCRIPT" ] || { echo "usage: $0 [-a BIN] [-b BIN] [-o DIR] SCRIPT" >&2; exit 2; }
# Absolute, because the game chdirs into its data directory before the
# platform opens the script; a relative name was an infinite run once.
SCRIPT=$(realpath "$SCRIPT")
[ -n "$OUT" ] || OUT=$(mktemp -d /tmp/am2-lockstep-XXXXXX)
mkdir -p "$OUT"
GAMEDIR=${AM2_GAMEDIR:-"$REPO/.wine/drive_c/GOG Games/Army Men II"}

run() { # tag binary [extra env...]
    tag=$1; bin=$2; shift 2
    env SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy AM2_GAMEDIR="$GAMEDIR" \
        AM2_LOCKSTEP=1 AM2_REPLAY="$SCRIPT" AM2_FRAMELOG="$OUT/$tag.frames" \
        AM2_LOG="$OUT/$tag.game.log" "$@" timeout 600 "$bin" -nointro -dbg \
        > "$OUT/$tag.err" 2>&1
    rc=$?
    if [ $rc -ne 0 ] || ! [ -s "$OUT/$tag.frames" ]; then
        echo "lockstep: $tag ($bin) failed, exit $rc; see $OUT/$tag.err"
        return 2
    fi
    return 0
}

run a "$A" || exit 2
run b "$B" || exit 2
na=$(wc -l < "$OUT/a.frames"); nb=$(wc -l < "$OUT/b.frames")
if cmp -s "$OUT/a.frames" "$OUT/b.frames"; then
    echo "lockstep: IDENTICAL over $na presented frames ($SCRIPT)"
    echo "artifacts in $OUT"
    exit 0
fi
# The first line that differs; the present index is the first field.
first=$(diff <(cut -d' ' -f1,3 "$OUT/a.frames") <(cut -d' ' -f1,3 "$OUT/b.frames") | grep -m1 '^[<>]' | cut -d' ' -f2)
pump=$(awk -v f="$first" '$1==f{print $2; exit}' "$OUT/a.frames")
echo "lockstep: DIVERGE at presented frame $first (pump $pump); $na vs $nb frames presented"
run ad "$A" AM2_FRAMEDUMP_AT="$first" AM2_FRAMEDUMP_TO="$OUT/a-$first.ppm" >/dev/null
run bd "$B" AM2_FRAMEDUMP_AT="$first" AM2_FRAMEDUMP_TO="$OUT/b-$first.ppm" >/dev/null
if [ -s "$OUT/a-$first.ppm" ] && [ -s "$OUT/b-$first.ppm" ]; then
    "$REPO/.venv/bin/python" - "$OUT/a-$first.ppm" "$OUT/b-$first.ppm" <<'PY'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGB"); b = Image.open(sys.argv[2]).convert("RGB")
pts = [(x, y) for y in range(a.height) for x in range(a.width) if a.getpixel((x, y)) != b.getpixel((x, y))]
if pts:
    print("lockstep: %d of %d pixels differ, box %d,%d-%d,%d" % (len(pts), a.width * a.height,
          min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)))
else:
    print("lockstep: the dumped frames are identical (the hash differs in the palette only)")
PY
fi
echo "artifacts in $OUT"
exit 1
