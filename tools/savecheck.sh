#!/bin/sh
# savecheck.sh -- is the SAVE GAME serialisation the same across builds?
#
#     tools/savecheck.sh
#
# The game's save file is the one snapshot that crosses builds: it stores
# objects by uid, not by pointer, so a file written by one build can be read
# by another. This drives the NATIVE development binary and the ORIGINAL
# under Wine (AM2_NOPATCH=1) to the same point of the same campaign mission,
# has each SAVE through the game's own dialog, and compares:
#
#   the two object tables at the moment of saving (they saved the same state)
#   each file LOADED by each build, four tables   (the readers agree, and
#                                                  a load restores the save)
#   the two save FILES, byte for byte, BY SECTION (for reading, not a gate)
#
# THE FILES CANNOT BE EXPECTED TO MATCH BYTE FOR BYTE, and that is the
# original's design rather than a defect: the object-script, event and item
# sections write their records whole, pointers included, and the loader
# overwrites those -- so every such dword is whatever each process's heap
# gave it. The game-proc block carries the clock and a frame counter. What
# a save MEANS is what a load makes of it, so the verdict rests on the
# object tables: the two saves' tables must agree, and each file loaded by
# each build must give the same state. Moving fields (position, tile, hit
# rectangle, destination) are compared separately from the rest where a
# frame has run between the two: at the briefing the first frame's time
# step differs between the native build and Wine -- two walking troopers
# sit one pixel apart for that reason, and the injected reconstruction under
# Wine matches the original there exactly. The LOADS are compared in every
# field, because AM2_PAUSE_ON_ENTER freezes a loaded mission before its
# first frame.
#
#     tools/savecheck.sh --report DIR     re-run the comparison on artifacts
#
# The drive is the game's: the save dialog is opened by writing the two
# globals the SAVE button writes (sub-state 0x19 and the overlay flag), which
# is what ab.sh's mpoptions does for its screen and what takes AM2_NOPATCH=1
# unchanged. The name is typed into the dialog's field after clearing what it
# pre-filled, and SAVE is clicked.
#
# Both halves run with `-nointro -dbg`, the arguments drive.sh defaults to;
# docs/saveload.md records four commits lost to the halves not matching.
#
# THE SAVE DIRECTORY IS SHARED, since the native binary runs on the same
# prefix. The fixture map1_mission1.sav is moved aside for the run so the
# LOAD list has exactly one row, and put back on exit.
set -e

DISP=${AM2_DISPLAY:-:99}
WORK=${TMPDIR:-/tmp}/savecheck.$$
REPO=$(cd "$(dirname "$0")/.." && pwd)
G="$REPO/.wine/drive_c/GOG Games/Army Men II"
SAVEDIR="$G/save/sarge"
NAME=xbuild
NATIVE_PORT=31337
WINE_PORT=31436
PY="$REPO/.venv/bin/python"
[ -x "$PY" ] || PY=python3

mkdir -p "$WORK"
say() { printf '%s\n' "$*"; }

native_pid=
stop_native() {
    [ -n "$native_pid" ] && kill "$native_pid" 2>/dev/null || true
    native_pid=
    sleep 1
}
restore_fixture() {
    if [ -f "$WORK/fixture.sav" ]; then
        mv -f "$WORK/fixture.sav" "$SAVEDIR/map1_mission1.sav"
    fi
    rm -f "$SAVEDIR/$NAME.sav"
}
cleanup() {
    stop_native
    AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" stop >/dev/null 2>&1 || true
    restore_fixture
    return 0
}
trap 'st=$?; cleanup; exit $st' EXIT

if [ "$1" != --report ]; then
    [ -x "$REPO/build/armymen2-dev" ] || { say "savecheck: run make native-dev first"; exit 1; }
    mkdir -p "$SAVEDIR"
    [ -f "$SAVEDIR/map1_mission1.sav" ] && mv "$SAVEDIR/map1_mission1.sav" "$WORK/fixture.sav"
    rm -f "$SAVEDIR/$NAME.sav"
fi

# ---- the two sides -------------------------------------------------------

# ctl SIDE CMD: one control-socket command to the side's port.
ctl() {
    if [ "$1" = native ]; then p=$NATIVE_PORT; else p=$WINE_PORT; fi
    shift
    "$PY" "$REPO/tools/am2ctl.py" --port "$p" "$@" 2>/dev/null | head -1
}
click() { ctl "$1" "cursor $2 $3" >/dev/null; sleep 0.3; ctl "$1" "mouse left tap" >/dev/null; }
table() {
    if [ "$1" = native ]; then p=$NATIVE_PORT; else p=$WINE_PORT; fi
    "$PY" "$REPO/tools/objdump.py" --port "$p" --table 2>/dev/null
}

# Both sides run with AM2_PAUSE_ON_ENTER=1, the harness hook that freezes a
# loaded mission before its first frame (see tools/enterlevel.sh), so the
# loaded tables compare in every field rather than only the static ones.
start_side() {
    if [ "$1" = native ]; then
        cd "$REPO"
        DISPLAY=$DISP SDL_AUDIO_DRIVER=dummy AM2_GAMEDIR="$G" AM2_PAUSE_ON_ENTER=1 \
            "$REPO/build/armymen2-dev" -nointro -dbg >"$WORK/native.out" 2>&1 &
        native_pid=$!
        sleep 8
    else
        AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" start 25 AM2_NOPATCH=1 AM2_PAUSE_ON_ENTER=1 \
            >"$WORK/orig.out" 2>&1
        sleep 4
    fi
}
stop_side() {
    if [ "$1" = native ]; then stop_native
    else AM2_DISPLAY=$DISP "$REPO/tools/drive.sh" stop >/dev/null 2>&1 || true; fi
}

# SINGLE PLAYER -> the player row -> SELECT, the shared prefix of both drives.
to_select_player() {
    click "$1" 306 182; sleep 6
    click "$1" 240 177; sleep 4
    click "$1" 455 221; sleep 6
}

substate() { ctl "$1" "peek 0x00511DBC 1" | awk '{print $2}'; }

# ... -> NEW -> the strategic map -> RETURN -> the mission, HELD at its
# briefing (sub-state 0x18: the message dialog, under -dbg's pause). The
# game composes no frames and runs no script while a dialog is up, so what
# is saved from here is the mission's initial state, the same on both
# sides. The briefing is polled for rather than slept for: the start is
# sometimes seen in play at 0x21 with nothing drawn yet, and a SPACE there
# releases -dbg's pause and brings the dialog up.
to_mission() {
    to_select_player "$1"
    click "$1" 455 181; sleep 25
    ctl "$1" "key RETURN tap" >/dev/null
    i=0
    while [ $i -lt 12 ]; do
        sleep 5
        sub=$(substate "$1")
        [ "$sub" = 00000018 ] && return 0
        if [ "$sub" = 00000021 ] && [ $i -ge 4 ]; then
            ctl "$1" "key SPACE tap" >/dev/null
        fi
        i=$((i + 1))
    done
    say "savecheck:   $1 never reached the briefing (sub-state $sub)"
    return 0
}

# A loaded game arrives frozen under AM2_PAUSE_ON_ENTER; this only waits
# for the pause reason to be visible before the dump.
freeze() {
    sleep 2
}

# The SAVE: open the dialog as the SAVE button does, clear the pre-filled
# name (24 backspaces cover "map1_mission1.sav"), type ours, click SAVE.
save_game() {
    ctl "$1" "poke 0x00511DBC 19" >/dev/null
    ctl "$1" "poke 0x00511DC0 1" >/dev/null
    sleep 3
    ctl "$1" 'type \b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b'"$NAME" >/dev/null
    sleep 1
    click "$1" 456 202; sleep 4
}

# ... -> the one save row -> LOAD, then wait for the frozen mission.
load_game() {
    to_select_player "$1"
    click "$1" 240 173; sleep 3
    click "$1" 455 221
    i=0
    while [ $i -lt 10 ]; do
        sleep 4
        sub=$(substate "$1")
        [ "$sub" = 00000021 ] && break
        i=$((i + 1))
    done
    freeze "$1"
}

save_side() {
    say "savecheck: $1 saves"
    start_side "$1"
    to_mission "$1"
    sub=$(ctl "$1" "peek 0x00511DBC 1" | awk '{print $2}')
    table "$1" >"$WORK/$1-saved.table"
    save_game "$1"
    if [ -f "$SAVEDIR/$NAME.sav" ]; then
        cp "$SAVEDIR/$NAME.sav" "$WORK/$1.sav"
        rm -f "$SAVEDIR/$NAME.sav"
        say "savecheck:   sub-state $sub, $(sed -n 's/^registered \([0-9]*\)$/\1/p' "$WORK/$1-saved.table") objects, $(wc -c <"$WORK/$1.sav") bytes written"
    else
        say "savecheck:   FAIL -- $1 wrote no $NAME.sav (sub-state $sub)"
    fi
    stop_side "$1"
}

load_side() {   # load_side LOADER FILE-SIDE
    say "savecheck: $1 loads $2's save"
    cp "$WORK/$2.sav" "$SAVEDIR/$NAME.sav"
    start_side "$1"
    load_game "$1"
    alive=1
    if [ "$1" = native ]; then kill -0 "$native_pid" 2>/dev/null || alive=0
    else pgrep -f 'ArmyMen2[.]exe' >/dev/null || alive=0; fi
    sub=$(ctl "$1" "peek 0x00511DBC 1" | awk '{print $2}')
    table "$1" >"$WORK/$1-loads-$2.table"
    md5=$(md5sum "$SAVEDIR/$NAME.sav" | cut -c1-8)
    say "savecheck:   alive $alive, sub-state $sub, $(sed -n 's/^registered \([0-9]*\)$/\1/p' "$WORK/$1-loads-$2.table") objects, file md5 $md5 (was $(md5sum "$WORK/$2.sav" | cut -c1-8))"
    rm -f "$SAVEDIR/$NAME.sav"
    stop_side "$1"
}

# ---- the comparison -------------------------------------------------------

# The file, by section: tags are 0x0666xxxx dwords, and the section order
# is SaveGame's. Prints how many bytes differ in each and what kind.
file_report() {
    "$PY" - "$WORK/native.sav" "$WORK/orig.sav" <<'PYEOF'
import struct, sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
names = {0x0666: 'gameproc', 9: 'map', 5: 'pads', 8: 'objscript', 2: 'script',
         6: 'eventblock', 3: 'conditions', 4: 'events', 7: 'items', 0x10: 'air'}
# Sections are not dword-aligned (the item records are odd sizes), so the
# tags are looked for at every byte.
secs = []
i = 0
while i + 4 <= len(a):
    v = struct.unpack_from('<I', a, i)[0]
    if (v & 0xFFFF0000) == 0x06660000 and (v & 0xFFFF) not in (0, 1) and (not secs or i - secs[-1][0] > 8):
        secs.append((i, names.get(v & 0xFFFF, '%x' % v)))
    i += 1
if len(a) != len(b):
    print("savecheck:   sizes differ: %d against %d bytes" % (len(a), len(b)))
n = min(len(a), len(b))
bounds = [s[0] for s in secs] + [n]
print("savecheck:   %d of %d bytes differ:" % (sum(1 for k in range(n) if a[k] != b[k]), n))
for k, (off, name) in enumerate(secs):
    d = [x for x in range(off, bounds[k + 1]) if a[x] != b[x]]
    if not d:
        continue
    dw = sorted(set(x // 4 * 4 for x in d))
    note = {'objscript': 'raw pointers, one per record',
            'conditions': 'raw pointers in the condition records',
            'items': 'raw pointers in every record, plus any moving field',
            'eventblock': 'frame-relative counters',
            'gameproc': 'the clock and the frame counter',
            'script': 'one word the native build zero-fills'}.get(name, '')
    print("savecheck:     %-10s %5d bytes in %4d dwords  %s" % (name, len(d), len(dw), note))
PYEOF
}

# Static fields only: what a load must reproduce exactly.
static_fields() {
    sed -e 's/ pos=[-0-9,]*//' -e 's/ tile=[0-9]*//' -e 's/ hit=[-0-9,]*//' \
        -e 's/ destpt=[-0-9]*//' -e 's/ outst=[-0-9]*//' -e 's/ outhit=[-0-9]*//' "$1"
}
pair() {   # pair A B LABEL: whole lines, informational
    if diff -q "$WORK/$1.table" "$WORK/$2.table" >/dev/null; then
        say "savecheck:   $3: identical ($(wc -l <"$WORK/$1.table") lines)"
    else
        say "savecheck:   $3: $(diff "$WORK/$1.table" "$WORK/$2.table" | grep -c '^[<>]') lines differ (moving fields included)"
    fi
}
# A load hands the objects whose uids came from the counter fresh ones (the
# four type-6 records at the briefing come back as 3f4..3f7 where the save
# had 3f0..3f3, in both builds), so loaded-against-saved drops the uid
# column; the rows stay in the same order.
pair_gate() {   # pair_gate A B LABEL: every field, the gate for two frozen loads
    if diff -q "$WORK/$1.table" "$WORK/$2.table" >/dev/null; then
        say "savecheck:   $3: identical in every field ($(wc -l <"$WORK/$1.table") lines)"
    else
        say "savecheck:   $3: DIFFER, $(diff "$WORK/$1.table" "$WORK/$2.table" | grep -c '^[<>]') lines"
        rc=1
    fi
}
# Loaded against saved: the save was taken at the briefing, after the
# mission's first frame had created its transient objects (four type-6
# records and their kin, uids from the counter); a frozen load has not run
# that frame yet, so it holds 317 of the 325. The objects present in BOTH
# must agree in every static field; the rest are counted and named.
pair_common() {   # pair_common LOADED SAVED LABEL
    "$PY" - "$WORK/$1.table" "$WORK/$2.table" "$3" <<'PYEOF' || rc=1
import re, sys
def load(path):
    d = {}
    for line in open(path):
        m = re.match(r'^([0-9a-f]{8}) (.*)$', line.rstrip('\n'))
        if m:
            f = re.sub(r' (pos|tile|hit|destpt|outst|outhit)=[-0-9,]*', '', m.group(2))
            d[m.group(1)] = f
    return d
a, b, label = load(sys.argv[1]), load(sys.argv[2]), sys.argv[3]
common = sorted(set(a) & set(b))
bad = [u for u in common if a[u] != b[u]]
only_saved = sorted(set(b) - set(a)); only_loaded = sorted(set(a) - set(b))
extra = ''
if only_saved or only_loaded:
    extra = ' (%d objects only in the save: %s; %d only after the load: %s)' % (
        len(only_saved), ' '.join(only_saved[:6]) or '-', len(only_loaded), ' '.join(only_loaded[:6]) or '-')
if bad:
    print('savecheck:   %s: %d of %d shared objects DIFFER in static fields%s' % (label, len(bad), len(common), extra))
    for u in bad[:4]:
        print('savecheck:     %s\n      loaded %s\n      saved  %s' % (u, a[u], b[u]))
    sys.exit(1)
print('savecheck:   %s: %d shared objects identical in static fields%s' % (label, len(common), extra))
PYEOF
}
pair_static() {   # pair_static A B LABEL [nouid]: static fields, for comparisons across a running frame
    static_fields "$WORK/$1.table" >"$WORK/$1.static"
    static_fields "$WORK/$2.table" >"$WORK/$2.static"
    if [ "$4" = nouid ]; then
        sed -i 's/^[0-9a-f]* //' "$WORK/$1.static" "$WORK/$2.static"
    fi
    if diff -q "$WORK/$1.static" "$WORK/$2.static" >/dev/null; then
        say "savecheck:   $3: static fields identical"
    else
        say "savecheck:   $3: STATIC FIELDS DIFFER, $(diff "$WORK/$1.static" "$WORK/$2.static" | grep -c '^[<>]') lines"
        rc=1
    fi
}

report() {
    rc=0
    for f in native.sav orig.sav native-saved.table orig-saved.table; do
        [ -s "$WORK/$f" ] || { say "savecheck: VOID -- no $f; nothing to compare"; return 1; }
    done
    say "savecheck: the saves"
    pair native-saved orig-saved "tables at the save"
    pair_static native-saved orig-saved "tables at the save"
    say "savecheck: the files"
    file_report
    say "savecheck: the loads"
    for f in orig-loads-native native-loads-native orig-loads-orig native-loads-orig; do
        [ -s "$WORK/$f.table" ] || { say "savecheck: VOID -- no $f.table"; return 1; }
    done
    pair_gate orig-loads-native native-loads-native "native's save, loaded by both"
    pair_gate orig-loads-orig native-loads-orig "the original's save, loaded by both"
    pair_common native-loads-native native-saved "native: loaded against saved"
    pair_common orig-loads-orig orig-saved "the original: loaded against saved"
    return $rc
}

if [ "$1" = --report ]; then
    WORK=$2
    trap - EXIT
    report; rc=$?
    [ $rc -eq 0 ] && say "savecheck: the serialisation matches across builds" \
                  || say "savecheck: DIFFERENCES -- read the artifacts before believing them"
    exit $rc
fi

save_side native
save_side orig
if ! [ -s "$WORK/native.sav" ] || ! [ -s "$WORK/orig.sav" ]; then
    say "savecheck: VOID -- a side wrote no save; nothing to compare"
    exit 1
fi
load_side orig native
load_side native orig
load_side native native
load_side orig orig
report; rc=$?
say "savecheck: artifacts in $WORK"
[ $rc -eq 0 ] && say "savecheck: the serialisation matches across builds" \
              || say "savecheck: DIFFERENCES -- read the artifacts before believing them"
exit $rc
