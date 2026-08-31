#!/usr/bin/env python3
"""Read a registered object's fields out of the running game.

    AM2_DISPLAY=:99 tools/drive.sh start 25
    tools/point.py 306 143 --click ; sleep 30
    tools/drive.sh ctl "key RETURN tap" ; sleep 25
    tools/objdump.py --leader
    tools/objdump.py --uid 3e8 --at 0x60 --len 8

Run it against a build with the reconstruction in and again under
AM2_NOPATCH=1, and the two must agree. That turns "verified by reading" into a
comparison for the whole class of functions that write a field of an object and
return nothing -- which neither the vector harness (it cannot map the game's
globals) nor AM2_SELFCHECK=1 (it compares eax) can check.

It earned its keep on SetMaxHealth. `ab.sh bootcamp` DOES see a wrong
difficulty scale, at 96 differing pixels rather than the usual 22 -- and it
reports A/B clean anyway, because 96 is well inside the budget of 500. This
reads the number: the leader's max health is 140 on a correct build and 280
with the scale index forced to 0, which is exactly 4.0 against 2.0.

The object table is the sorted array src/game/objtable.c describes -- {uid,
obj, serial} records of 12 bytes -- so a uid is found in a dozen reads rather
than by walking it.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2ctl

OBJ_COUNT = 0x00514F04
OBJ_TABLE = 0x00514F0C
LEADER_UID = 0x00511E4C     # our own army's, per src/game/army.h
ENTRY = 12

CHUNK = 96


def read(ctl, addr, size):
    out = bytearray()
    while len(out) < size:
        n = min(CHUNK, size - len(out))
        reply = ctl.send("dump %x %d" % (addr + len(out), n))
        if not reply.startswith("ok "):
            raise SystemExit("dump failed at %#x: %s" % (addr + len(out), reply))
        out += bytes.fromhex(reply.split()[2])
    return bytes(out[:size])


def find(ctl, uid):
    """The object a uid names, by binary search over the sorted table."""
    count, = struct.unpack("<i", read(ctl, OBJ_COUNT, 4))
    base, = struct.unpack("<I", read(ctl, OBJ_TABLE, 4))
    if count <= 0 or not base:
        raise SystemExit("the object table is empty -- has a mission loaded?")
    lo, hi = 0, count - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        u, obj, _serial = struct.unpack("<III", read(ctl, base + mid * ENTRY,
                                                     ENTRY))
        if u == uid:
            return obj, count
        if u < uid:
            lo = mid + 1
        else:
            hi = mid - 1
    raise SystemExit("uid %08x is not in the table of %d" % (uid, count))


# What --table prints per object. Every one of these is a VALUE the game
# computed, never a heap pointer: a dump that carried an address would differ
# between two runs for a reason that is not a defect, which is the rule
# tools/actdiff.py and `ctl widgets` already follow.
TABLE_FIELDS = (
    ("type",   0x000, "<i"),
    ("flags",  0x008, "<I"),
    ("army",   0x010, "<b"),
    ("pos",    0x012, "<hh"),
    ("tile",   0x01A, "<H"),
    ("box",    0x020, "<iiii"),   # OBJ_OFF_BOX_OFFSETS
    ("hit",    0x030, "<iiii"),   # OBJ_OFF_HIT_RECT
    ("health", 0x060, "<hh"),
    ("cells",  0x08C, "<B"),      # OBJ_OFF_CELL_COUNT
)
TABLE_SPAN = 0x90

# The AI band's own state -- dumped SEPARATELY and only for the types whose
# records are big enough to hold it.
#
# Nothing could see this band at all: every function in it (the three per-type
# steps, the three sight builders, the mode arms) writes only fields past 0x90,
# so the table above was blind to all of them. It took a real defect to notice
# -- StepType2 wrote a converging tail as early returns, skipping the call that
# places a trooper's held weapon -- and the ONLY reason that was caught is that
# the weapon's own `pos` is in the table. The step's own writes were invisible.
#
# IT CANNOT BE A WIDER TABLE_SPAN. Records differ in size by type: CLAUDE.md
# records a missile's as 0xB8 bytes and a roach's as 0x560, so reading 0x590
# from every object would run past most of them and return heap contents --
# unstable between runs, which is exactly what the "values only" rule above
# exists to prevent. Types 2 and 3 are known to reach at least 0x5A8, because
# OBJ_OFF_FIELD_5A4 and a vehicle's +0x59C are read on them.
AI_TYPES = (2, 3)
AI_FIELDS = (
    ("destpt", 0x0C0, "<I"),      # OBJ_OFF_FIELD_C0, a packed point
    ("aimode", 0x0E4, "<i"),      # OBJ_OFF_AI_MODE -- which arm ran
    ("pose",   0x538, "<i"),      # OBJ_OFF_POSE / a vehicle's list header
    ("sarge",  0x548, "<i"),      # OBJ_OFF_SARGE
    ("outbr",  0x57C, "<i"),      # StepType2's out+0, StepType3's out+4
    ("outst",  0x580, "<i"),      # and +4 / +8
    ("outhit", 0x584, "<i"),      # and +8 / +0x0C
)
AI_SPAN = 0x588


def dump_table(ctl):
    """Every registered object, as values only. See TABLE_FIELDS.

    This exists because two mutations in two commits went uncaught: the suite
    compares pixels, a log and a widget tree, and is blind to anything whose
    only effect is WHERE something is on the map. Taken at the Boot Camp
    briefing the table is STATIC -- the game composes no frames while a dialog
    is up -- so it diffs exactly between the two sides of an A/B, with no
    budget, the way `ctl widgets` does for the menu layer.
    """
    count, = struct.unpack("<i", read(ctl, OBJ_COUNT, 4))
    base, = struct.unpack("<I", read(ctl, OBJ_TABLE, 4))
    if count <= 0 or not base:
        raise SystemExit("the object table is empty -- has a mission loaded?")

    table = read(ctl, base, count * ENTRY)
    print("registered %d" % count)
    for i in range(count):
        uid, obj, _serial = struct.unpack_from("<III", table, i * ENTRY)
        if not obj:
            print("%08x (null)" % uid)
            continue
        blob = read(ctl, obj, TABLE_SPAN)
        out = []
        for name, off, fmt in TABLE_FIELDS:
            vals = struct.unpack_from(fmt, blob, off)
            out.append("%s=%s" % (name, ",".join(str(v) for v in vals)))
        # The AI block, only for the types whose records reach it. See
        # AI_FIELDS: reading it from a missile or a roach would run past the
        # allocation and print heap.
        otype, = struct.unpack_from("<i", blob, 0)
        if otype in AI_TYPES:
            ai = read(ctl, obj, AI_SPAN)
            for name, off, fmt in AI_FIELDS:
                vals = struct.unpack_from(fmt, ai, off)
                out.append("%s=%s" % (name, ",".join(str(v) for v in vals)))
        print("%08x %s" % (uid, " ".join(out)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=am2ctl.DEFAULT_PORT)
    ap.add_argument("--table", action="store_true",
                    help="every registered object, values only, for an A/B")
    ap.add_argument("--uid", help="hex uid; omit with --leader")
    ap.add_argument("--leader", action="store_true",
                    help="our own army's leader, from 0x00511E4C")
    ap.add_argument("--at", default="0x60", help="offset into the object")
    ap.add_argument("--len", type=int, default=4, dest="length")
    args = ap.parse_args()

    ctl = am2ctl.Control(port=args.port)
    if args.table:
        dump_table(ctl)
        return
    if args.leader:
        uid, = struct.unpack("<I", read(ctl, LEADER_UID, 4))
    elif args.uid:
        uid = int(args.uid, 16)
    else:
        raise SystemExit("give --uid or --leader")

    obj, count = find(ctl, uid)
    at = int(args.at, 0)
    data = read(ctl, obj + at, args.length)
    print("uid %08x  obj %08x  of %d registered" % (uid, obj, count))
    print("  +%#05x %s" % (at, data.hex(" ")))
    if args.length >= 4 and at == 0x60:
        mx, cur = struct.unpack_from("<hh", data, 0)
        print("  max health %d, current %d" % (mx, cur))


if __name__ == "__main__":
    main()
