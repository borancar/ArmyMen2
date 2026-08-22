#!/usr/bin/env python3
"""Read the collision masks out of a running game and hash them.

    AM2_DISPLAY=:99 tools/drive.sh start 25
    tools/point.py 306 143 --click ; sleep 30
    tools/drive.sh ctl "key RETURN tap" ; sleep 25
    tools/maskdump.py

BuildRoachMask and BuildVehicleMask run once each, right after their `.ani`
loads, and nothing writes the tables again -- so a hash of them is a complete
comparison. Run this against a build with the reconstructions in and again
under AM2_NOPATCH=1, and the two must agree byte for byte.

That is the only check available. A mask is a list of grid squares where the
sprite is opaque; nothing about it reaches the log, and whether it is right
shows up, if at all, as a collision landing slightly wrong much later.
`bootcamp` passes whether they are built correctly or not -- measured, not
assumed: with the sample step doubled from 2 to 4, all 32 roach records change
and the point total drops from 237 to 25, and the A/B is still clean at the
usual 22 pixels with an identical log.

Both tables are {int32_t count; AM2_Point pts[40]} records 0xA4 apart with a
direction count beside them. Everything between is hashed, INCLUDING the points
a record did not fill: those are leftovers rather than output, but they are
deterministic, and including them is what caught the roach table being written
one dword early. Every point was right; only where they sat was wrong.

The vehicle bases are confirmed by tiling: the six turret animation tables end
exactly where the six direction counts begin, those end exactly where the
records begin, and 6 * 32 records of 0xA4 end exactly at the vehicle animation
tables. If a layout does not tile, one of the bases is wrong.
"""
import argparse
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2ctl

STRIDE = 0xA4
MAX_POINTS = 40

# name, directions global, first record's count, how many records
ROACH = ("roach", 0x00654CA0, 0x00654CA8, None)     # records = the direction count
VEHICLE = ("vehicle", 0x0065A2D8, 0x0065A2F0, 6 * 32)

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


def dump(ctl, name, dirs_at, table_at, records, show):
    """Hash one mask table, and report how much of it is real."""
    if records is None:
        n, = struct.unpack("<i", read(ctl, dirs_at, 4))
        if n <= 0 or n > 256:
            raise SystemExit("%s directions reads %d -- has a mission loaded? "
                             "Nothing builds these on the title screen." % (name, n))
        records = n
        ndirs = 1
    else:
        ndirs = (table_at - dirs_at) // 4

    blob = read(ctl, dirs_at, (table_at - dirs_at) + records * STRIDE)
    counts = struct.unpack_from("<%di" % ndirs, blob, 0) if ndirs > 1 else None

    total = 0
    for k in range(records):
        at = (table_at - dirs_at) + k * STRIDE
        count, = struct.unpack_from("<i", blob, at)
        if count < 0 or count > MAX_POINTS:
            raise SystemExit("%s record %d has count %d -- is the base right?"
                             % (name, k, count))
        total += count
        if show and count:
            pts = struct.unpack_from("<%dh" % (count * 2), blob, at + 4)
            print("  %-8s %3d  %2d  %s"
                  % (name, k, count,
                     " ".join("(%d,%d)" % (pts[i], pts[i + 1])
                              for i in range(0, len(pts), 2))))
    print("%-8s %3d record(s), %4d point(s), %6d bytes  %s%s"
          % (name, records, total, len(blob),
             hashlib.sha256(blob).hexdigest(),
             ("  dirs " + ",".join(str(c) for c in counts)) if counts else ""))
    return blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=am2ctl.DEFAULT_PORT)
    ap.add_argument("--points", action="store_true",
                    help="print every record's points, not just the hash")
    ap.add_argument("--save", help="write the raw bytes here too")
    args = ap.parse_args()

    ctl = am2ctl.Control(port=args.port)
    blob = b""
    for name, dirs_at, table_at, records in (ROACH, VEHICLE):
        blob += dump(ctl, name, dirs_at, table_at, records, args.points)
    print("all      %6d bytes  %s" % (len(blob), hashlib.sha256(blob).hexdigest()))
    if args.save:
        open(args.save, "wb").write(blob)


if __name__ == "__main__":
    main()
