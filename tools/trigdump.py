#!/usr/bin/env python3
"""Read the four trig tables out of a running game and hash them.

They are built once at startup and never written again, so a hash is a
complete comparison: run this against a build with src/game/trig.cpp patched in
and again under AM2_NOPATCH=1, and the two must agree byte for byte.

That is a stronger check than the A/B can give here. The tables are floats
produced by fsin and fcos, and a single ulp of disagreement -- which is exactly
what calling libm instead of the x87 instructions would cost -- moves one byte
in a table the renderer reads for every sprite. It might well not be visible.
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2ctl

TABLES = [
    ("cos",    0x00515784, 256 * 4),
    ("sin",    0x00514F80, 256 * 4),
    # The two reverse tables are indexed by a signed ratio, so the addresses
    # the code carries are their centres; these are the real starts.
    ("atanC",  0x00515D84 - 512, 1025),
    ("atanS",  0x00515580 - 512, 1025),
]

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
    ap.add_argument("--save", help="write the raw bytes here too")
    args = ap.parse_args()

    ctl = am2ctl.Control(port=args.port)
    blob = b""
    for name, addr, size in TABLES:
        data = read(ctl, addr, size)
        blob += data
        print("%-6s %#010x %5d bytes  %s"
              % (name, addr, size, hashlib.sha256(data).hexdigest()[:16]))
    print("all    %5d bytes  %s" % (len(blob), hashlib.sha256(blob).hexdigest()))
    if args.save:
        open(args.save, "wb").write(blob)


if __name__ == "__main__":
    main()
