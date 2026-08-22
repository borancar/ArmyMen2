#!/usr/bin/env python3
"""Check the animation tables in the running game against the `.ani` files.

    AM2_DISPLAY=:99 AM2_MAKEVARS="GAMELOG=1" tools/drive.sh start 30 AM2_DUMP_ANIMS=1
    tools/point.py 306 143 --click     # BOOT CAMP
    sleep 30; tools/drive.sh ctl "key RETURN tap"
    tools/anicheck.py [logfile]

LoadAnimTable is the tail of a `.ani` load: LoadSpriteFile reads the palette,
LoadSpriteSet reads the sprites, and what is left is a list of animations over
them. An A/B cannot see inside that list. A mis-read field would draw the wrong
sprite, which shows up as pixels -- but `bootcamp` screenshots the briefing,
where no soldier is on screen, and `mission` scrolls, so its pixel check is off
by construction. The log is identical either way, because nothing logs.

So this reads the file itself. The format is not guessed: parsing every shipped
`.ani` with this layout consumes each one to its last byte, which a mis-sized
field could not do. AM2_DUMP_ANIMS=1 then prints what the game built, and the
two are compared entry by entry.

Sprite indices are compared as DIFFERENCES between consecutive cells. The
absolute value is the file's index plus however many sprites were already in
the list, which depends on the load order of every earlier file; the deltas do
not.

What it covers and what it does not. The 121 borrowed entries in a Boot Camp
run all resolve to the pointer predicted here, which checks the fallback search
and the final fixup -- explosions.ani passes no fallback at all, so its three
borrowed entries can only come from the fixup. Two paths stay unreached by any
shipped data: no borrowed id is missing from rifleman.ani, so the
`fallback->entries[0]` last resort never fires, and rifleman's 52 ids are all
distinct, so "the last match wins" and "the first match wins" cannot be told
apart. Both stay verified by reading.
"""

import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENTRY = re.compile(r"ANIM\s+(\d+) id=(-?\d+) next=(-?\d+) borrowed=(\d+) "
                   r"anim=(\S+)(.*)")
DETAIL = re.compile(r"\s*f=(-?\d+) fa=(-?\d+) bits=(\d+) w4=(-?\d+) "
                    r"w6=(-?\d+) cells=(-?\d+)(.*)")
CELL = re.compile(r"\((-?\d+),(-?\d+)\)")

# The soldier files, all of which pass rifleman's table as their fallback --
# see the call sites at 0x00446F50.
SOLDIERS = {"bazookaman.ani", "grenadier.ani", "flamer.ani", "mortarman.ani",
            "sweeper.ani", "m80.ani", "zombie.ani", "scientists.ani"}


def parse_ani(path):
    """Every animation entry in a `.ani`, and assert the file ends exactly."""
    with open(path, "rb") as fh:
        d = fh.read()
    off = 256 * 4                                   # the palette
    count, = struct.unpack_from("<i", d, off)       # then the sprite set
    off += 4
    for _ in range(count):
        off += 12                                   # six uint16 of geometry
        size, = struct.unpack_from("<i", d, off)
        off += 4 + size
        overlay, = struct.unpack_from("<i", d, off)
        off += 4
        if overlay > 0:
            off += overlay

    n, = struct.unpack_from("<i", d, off)
    off += 4
    out = []
    for _ in range(n):
        ident, nxt, kind = struct.unpack_from("<hhh", d, off)
        off += 6
        if nxt == 0:
            nxt = -2                                # the file's 0 means none
        if kind != 1:
            out.append((ident, nxt, None))
            continue
        frames, facings, w4, w6 = struct.unpack_from("<hhhh", d, off)
        off += 8
        cells = (frames * facings) & 0xFFFF         # computed 16-bit
        if cells >= 0x8000:
            cells -= 0x10000
        grid = []
        for _ in range(max(0, cells)):
            grid.append(struct.unpack_from("<hh", d, off))
            off += 4
        out.append((ident, nxt, (frames, facings, w4, w6, cells, grid)))
    if off != len(d):
        raise ValueError(f"{path}: {off} of {len(d)} bytes consumed")
    return out


def log2_mask(v):
    """0x0042DFE0: the bit index of a power of two in 1..0x8000, else 0."""
    if 1 <= v <= 0x8000 and (v & (v - 1)) == 0:
        return v.bit_length() - 1
    return 0


def read_tables(path):
    tables, cur = [], None
    with open(path, errors="replace") as fh:
        for line in fh:
            if line.startswith("ANIMS "):
                cur = []
                tables.append(cur)
                continue
            m = ENTRY.match(line)
            if not m or cur is None:
                continue
            e = {"id": int(m.group(2)), "next": int(m.group(3)),
                 "borrowed": int(m.group(4)), "ptr": m.group(5), "det": None}
            d = DETAIL.match(m.group(6))
            if d:
                e["det"] = (int(d.group(1)), int(d.group(2)), int(d.group(3)),
                            int(d.group(4)), int(d.group(5)), int(d.group(6)),
                            [(int(a), int(b)) for a, b in CELL.findall(d.group(7))])
            cur.append(e)
    return tables


def compare(table, ref):
    """Every disagreement between one dumped table and one parsed file."""
    bad = []
    if len(table) != len(ref):
        return [f"count {len(table)} vs {len(ref)}"]
    for i, (e, (ident, nxt, det)) in enumerate(zip(table, ref)):
        if e["id"] != ident or e["next"] != nxt:
            bad.append(f"[{i}] id/next {e['id']},{e['next']} "
                       f"vs {ident},{nxt}")
            continue
        # An entry with no animation of its own prints no detail, and is
        # exactly the one the loader marks borrowed.
        if (det is None) != (e["det"] is None):
            bad.append(f"[{i}] id {ident}: kind disagrees")
            continue
        if det is None:
            if e["borrowed"] != 1:
                bad.append(f"[{i}] id {ident}: borrowed={e['borrowed']}")
            continue
        if e["borrowed"] != 0:
            bad.append(f"[{i}] id {ident}: borrowed={e['borrowed']} on own")
        frames, facings, w4, w6, cells, grid = det
        got = e["det"]
        for name, a, b in (("frames", got[0], frames),
                           ("facings", got[1], facings),
                           ("bits", got[2], log2_mask(facings)),
                           ("field4", got[3], w4),
                           ("field6", got[4], w6),
                           ("cells", got[5], cells)):
            if a != b:
                bad.append(f"[{i}] id {ident}: {name} {a} vs {b}")
        want = grid[:len(got[6])]
        if [c[0] for c in want] != [c[0] for c in got[6]]:
            bad.append(f"[{i}] id {ident}: cell field0 differs")
        deltas = [b[1] - a[1] for a, b in zip(want, want[1:])]
        if deltas != [b[1] - a[1] for a, b in zip(got[6], got[6][1:])]:
            bad.append(f"[{i}] id {ident}: sprite deltas differ")
    return bad


def check_borrows(table, fallback):
    """Where each borrowed entry's animation came from: last match, else 0."""
    bad = 0
    for e in table:
        if not e["borrowed"]:
            continue
        want = None
        if fallback:
            for f in fallback:              # no break: the LAST match wins
                if f["id"] == e["id"]:
                    want = f["ptr"]
            if want is None:
                want = fallback[0]["ptr"]
        if want is None:
            want = table[0]["ptr"]          # the loader's own final fixup
        if e["ptr"] != want:
            bad += 1
            print(f"  borrow id {e['id']}: {e['ptr']}, expected {want}")
    return bad


def make_config():
    out = subprocess.run(["make", "-s", "config"], cwd=REPO,
                         capture_output=True, text=True).stdout
    cfg = {}
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip().strip("'\"")
    return cfg


def main():
    cfg = make_config()
    gamedir = cfg.get("GAMEDIR")
    if not gamedir or not os.path.isdir(gamedir):
        sys.exit("anicheck: no GAMEDIR -- run with DISPLAY set")
    log = (sys.argv[1] if len(sys.argv) > 1
           else os.path.join(gamedir, cfg.get("LOGFILE", "")))
    if not os.path.isfile(log):
        sys.exit(f"anicheck: no log at {log}")

    anidir = os.path.join(gamedir, "data", "ani")
    refs = {}
    for f in sorted(os.listdir(anidir)):
        if f.lower().endswith(".ani"):
            refs[f] = parse_ani(os.path.join(anidir, f))
    print(f"{len(refs)} .ani file(s) parsed, each consumed to its last byte")

    tables = read_tables(log)
    if not tables:
        print("no ANIM lines in the log -- was AM2_DUMP_ANIMS=1 set, and did\n"
              "the run reach a mission? Nothing loads a `.ani` on the title "
              "screen.")
        return 1

    named, unmatched = [], 0
    for t in tables:
        hits = [n for n, r in refs.items() if not compare(t, r)]
        named.append(hits)
        if not hits:
            unmatched += 1
            print(f"  NO MATCH for a table of {len(t)} entries:")
            # Report against whichever file has the same length, if any.
            for n, r in refs.items():
                if len(r) == len(t):
                    for line in compare(t, r)[:6]:
                        print(f"    vs {n}: {line}")
                    break

    rifle = next((t for t, n in zip(tables, named) if n == ["rifleman.ani"]),
                 None)
    borrowed = bad = 0
    for t, n in zip(tables, named):
        if not n:
            continue
        borrowed += sum(e["borrowed"] for e in t)
        bad += check_borrows(t, rifle if set(n) & SOLDIERS else None)

    print(f"{len(tables)} table(s) in the log, {unmatched} matching no file")
    print(f"{borrowed} borrowed entry/entries, {bad} resolved wrongly")
    return 1 if (unmatched or bad) else 0


if __name__ == "__main__":
    sys.exit(main())
