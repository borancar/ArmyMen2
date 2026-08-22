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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=am2ctl.DEFAULT_PORT)
    ap.add_argument("--uid", help="hex uid; omit with --leader")
    ap.add_argument("--leader", action="store_true",
                    help="our own army's leader, from 0x00511E4C")
    ap.add_argument("--at", default="0x60", help="offset into the object")
    ap.add_argument("--len", type=int, default=4, dest="length")
    args = ap.parse_args()

    ctl = am2ctl.Control(port=args.port)
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
