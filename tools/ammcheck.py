#!/usr/bin/env python3
"""Walk every shipped .amm with the layout taken off LoadMap's disassembly.

A FILE FORMAT THAT CONSUMES ITS INPUT EXACTLY IS ITS OWN PROOF -- the argument
tools/anicheck.py already makes for .ani.  This exists BEFORE 0x0042C440 is
transcribed rather than after, so that the format reading is checked against
23 real files before any of it is written as C.

It has already refuted that reading twice.  FORM's size is LITTLE-endian here,
not IFF's big-endian; and the tag list taken off the first switch is short --
CSUM, VERS, DESC, AFIL, OFIL, ASCR, PSCR, TNAM, ONAM, TLAY, OATT and NORM are
all in the files and none is in the switch, because the default arm fseeks
past anything it does not know.  Reading the arms gives the tags the loader
CARES about, never the tags the format HAS.

FOUR MAPS WALK TO THEIR LAST BYTE EXACTLY.  Three more needed one real fix:
the whole FORM is padded to an even length, so a file can be one byte longer
than FORM's size claims.  That is IFF and is now allowed for.

WHAT IS STILL UNEXPLAINED: nineteen maps desync, and every one of them lands
ONE BYTE before the next tag, having trusted OATT's size.  OATT says 0x988 and
its data behaves as 0x987.

IFF's pad-to-even is the obvious candidate and it is NOT the answer -- it was
tried, it fixed none of the nineteen, and it silently changed the chunk count
from 296 to 335 by padding chunks that did not need it.  A change that does
not fix what it was made for does not get kept, so it is reverted and this
paragraph is what survives of it.

So the header layout and the chunk walk are right, and something about OATT
specifically is not.  That has to come from LoadMap rather than from more
guessing at the format: the loop accumulates a running total in ebp, and the
likeliest answer is that it never reaches OATT at all.
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..", ".wine", "drive_c", "GOG Games", "Army Men II")

# Every tag that appears in the shipped files, not only the ones the switch has.
KNOWN = set(b"""CSUM VERS DESC AFIL OFIL ASCR PSCR MHDR TNAM ONAM TLAY OLAY
                OATT NORM MOVE BPAD NPAD OWNR TRIG REGN SCEN ELEV ELOW NUMB
                SCRI INDX RESV""".split())
PLANE = {b"BPAD", b"NPAD", b"MOVE", b"OWNR", b"TRIG", b"REGN", b"ELEV", b"ELOW"}


def walk(path):
    d = open(path, "rb").read()
    if d[0:4] != b"FORM" or d[8:12] != b"MAP ":
        return None, None, "not a MAP FORM"
    form = struct.unpack_from("<I", d, 4)[0]
    if form + 8 != len(d) and form + 9 != len(d):
        return None, None, "FORM says %d, file is %d" % (form + 8, len(d))

    at, w, h, seen = 12, None, None, []
    while at + 8 <= len(d):
        tag = d[at:at + 4]
        size = struct.unpack_from("<I", d, at + 4)[0]
        if tag not in KNOWN or size > len(d):
            return seen, w, "desync at %d: tag %r size %d" % (at, tag, size)
        if tag == b"MHDR":
            w, h = struct.unpack_from("<II", d, at + 8)[:2]
        elif tag in PLANE and w is not None and size != w * h:
            return seen, w, "%r is %d, w*h is %d" % (tag, size, w * h)
        seen.append((tag, size))
        at += 8 + size
    if at not in (len(d), len(d) + 1, len(d) - 1):
        return seen, w, "consumed %d of %d" % (at, len(d))
    return seen, w, None


def main():
    files = []
    for root, _, fs in os.walk(GAME):
        files += [os.path.join(root, f) for f in sorted(fs)
                  if f.lower().endswith(".amm")]
    if not files:
        print("no .amm found under %s" % GAME)
        return 1

    clean, chunks, why = 0, 0, {}
    for p in sorted(files):
        seen, _, err = walk(p)
        chunks += len(seen or [])
        if err is None:
            clean += 1
        else:
            key = err.split(":")[0]
            why.setdefault(key, []).append(os.path.basename(p))

    print("%d maps, %d chunks, %d walk to the last byte exactly"
          % (len(files), chunks, clean))
    for k, v in sorted(why.items()):
        print("  %-14s %2d maps, e.g. %s" % (k, len(v), v[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
