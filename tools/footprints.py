#!/usr/bin/env python3
"""Read the roach collision footprints out of a running game and hash them.

    AM2_DISPLAY=:99 tools/drive.sh start 25
    tools/point.py 306 143 --click ; sleep 30
    tools/drive.sh ctl "key RETURN tap" ; sleep 25
    tools/footprints.py

BuildRoachFootprints runs once, right after roach.ani loads, and nothing writes
the table again -- so a hash of it is a complete comparison. Run this against a
build with the reconstruction in and again under AM2_NOPATCH=1, and the two
must agree byte for byte.

That is the only check available. The table is a list of grid squares where the
roach sprite is opaque; nothing about it reaches the log, and whether it is
right shows up, if at all, as a collision that lands slightly wrong somewhere
much later. `bootcamp` passes whether it is built correctly or not -- measured, not
assumed: with the sample step doubled from 2 to 4, all 32 records change and
the point total drops from 237 to 25, and the A/B is still clean at the usual
22 pixels and an identical log.

One record per facing, {int32_t count; AM2_Point pts[40]}, 0xA4 apart, and the
facing count is a separate global just below. Everything between them is
hashed, including the points a record did not fill: those are the previous
facing's leftovers rather than output, but they are deterministic, and
including them is what caught the reconstruction writing the whole table one
dword early. Every point was right; only where they sat was wrong, and both
A/Bs were clean on it.
"""
import argparse
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2ctl

FACINGS = 0x00654CA0     # int32_t, written by the builder
TABLE = 0x00654CA8       # the first record's count; its points follow
STRIDE = 0xA4
MAX_POINTS = 40

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=am2ctl.DEFAULT_PORT)
    ap.add_argument("--points", action="store_true",
                    help="print every record's points, not just the hash")
    ap.add_argument("--save", help="write the raw bytes here too")
    args = ap.parse_args()

    ctl = am2ctl.Control(port=args.port)
    facings, = struct.unpack("<i", read(ctl, FACINGS, 4))
    if facings <= 0 or facings > 256:
        raise SystemExit("facings reads %d -- has a mission loaded? Nothing "
                         "builds this on the title screen." % facings)

    # From the facing count through the last record, and INCLUDING the dword
    # between them and the points a record did not fill. Both are leftovers
    # rather than output, and hashing them is deliberate: a table written one
    # dword early has every point right and only the surroundings wrong, which
    # is exactly the mistake this found the first time it ran.
    blob = read(ctl, FACINGS, (TABLE - FACINGS) + facings * STRIDE)
    print("facings %d" % facings)

    total = 0
    for k in range(facings):
        at = (TABLE - FACINGS) + k * STRIDE
        count, = struct.unpack_from("<i", blob, at)
        if count < 0 or count > MAX_POINTS:
            raise SystemExit("record %d has count %d -- is the base right?"
                             % (k, count))
        total += count
        if args.points:
            pts = struct.unpack_from("<%dh" % (count * 2), blob, at + 4)
            print("  %2d  %2d  %s" % (k, count,
                                      " ".join("(%d,%d)" % (pts[i], pts[i + 1])
                                               for i in range(0, len(pts), 2))))
    print("%d record(s), %d point(s), %d bytes  %s"
          % (facings, total, len(blob), hashlib.sha256(blob).hexdigest()))
    if args.save:
        open(args.save, "wb").write(blob)


if __name__ == "__main__":
    main()
