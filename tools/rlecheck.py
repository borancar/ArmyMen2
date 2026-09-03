#!/usr/bin/env python3
"""Check the RLE sprite encoders against the original, over a built corpus.

EncodeBig (0x0041BBC0) and EncodeSmall (0x0041BD20) turn a bitmap into the
game's own run-length form.  CLAUDE.md records why they have never run here:
they sit behind BMP_FLAG_SOFTWARE, which nothing sets while DirectDraw is
handing out real surfaces, so "the software rasteriser is not a function this
environment misses, it is a layer it never enters".  Both counters are also
BLIND -- every caller is reconstructed -- so a zero from `counts` says nothing
either.  This tool is their verification or there is none.

WHAT IT COMPARES.  The original is emulated with its allocator stubbed to
return a fixed buffer, so the encoded bytes land somewhere readable, and the
whole output is compared against a model of the reconstruction byte for byte
-- header, row table and payload.  The size the function answers is compared
too, because that is what the caller keeps.

TWO THINGS THE EMULATION NEEDS.  The function reserves a 199,000-byte stack
buffer through _chkstk, which is larger than vectors.py's whole stack, so the
harness maps more below it.  And `malloc` reaches HeapAlloc, which does not
exist under emulation -- the same wall scriptcheck hit -- so it is stubbed
with `mov eax, imm32; ret` rather than given a heap.

THE CORPUS IS BUILT FOR THE EDGES, not sampled: runs of exactly 254, 255 and
256 of one colour, to sit either side of the 0xFF cap that ends a skip or a
run; rows that are entirely the key colour and rows with none of it; and both
signs of `h`, which is what chooses between reading the rows bottom-up and
top-down.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, STACK, SCRATCH

ENCODE_BIG = 0x0041BBC0
ENCODE_SMALL = 0x0041BD20
GAME_MALLOC = 0x004647F8

DATA, DATA_SZ = 0x60000000, 0x40000
PIX = DATA + 0x1000
REMAP = DATA + 0x200
OUTBUF = DATA + 0x20000

EXTRA_STACK = 0x60000          # _chkstk reserves 0x30D58; give it room


def build(w, h, pattern):
    """A bitmap with rows padded to a multiple of four, as the encoder reads."""
    stride = (w + 3) & ~3
    rows = abs(h)
    return b"".join(bytes(pattern(x, y) for x in range(w))
                    + b"\0" * (stride - w) for y in range(rows))


def model(pix, w, h, remap, wide):
    """What the reconstruction produces -- header, row table, payload."""
    stride = (w + 3) & ~3
    if h > 0:
        row, step, rows = (h - 1) * stride, -stride, h
    else:
        row, step, rows = 0, stride, -h

    key = pix[row]
    scratch = bytearray(4 + rows * (4 if wide else 2))
    struct.pack_into("<HH", scratch, 0, w & 0xFFFF, rows & 0xFFFF)

    for r in range(rows):
        at = len(scratch)
        if wide:
            struct.pack_into("<I", scratch, 4 + r * 4, at)
        else:
            struct.pack_into("<H", scratch, 4 + r * 2, at & 0xFFFF)

        p, end = row, row + w
        while True:
            skip = 0
            while p != end and pix[p] == key and skip < 0xFF:
                p += 1
                skip += 1
            scratch.append(skip)

            run = 0
            while p != end and pix[p] != key and run < 0xFF:
                p += 1
                run += 1
            scratch.append(run)

            if run:
                scratch += bytes(remap[pix[p - run + i]] for i in range(run))
            if p == end:
                break
        row += step
    return bytes(scratch)


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_map(STACK - EXTRA_STACK, EXTRA_STACK)
        # malloc -> OUTBUF.  The encoder memcpy's its scratch into whatever
        # this answers and returns the SIZE, so a fixed buffer is enough.
        uc.mem_write(GAME_MALLOC, b"\xb8" + struct.pack("<I", OUTBUF) + b"\xc3")

    def original(self, pix, w, h, remap, wide):
        uc = self.emu.uc
        uc.mem_write(PIX, pix)
        uc.mem_write(REMAP, remap)
        uc.mem_write(DATA, b"\0" * 8)
        entry = ENCODE_BIG if wide else ENCODE_SMALL
        eax, _ = self.emu.call(entry, [PIX, DATA, w, h, REMAP],
                               b"\0" * 64, count=4000000)
        if eax is None or eax <= 0 or eax > 0x10000:
            return None, None
        return eax, bytes(uc.mem_read(OUTBUF, eax))


# MOST OF THESE MUST VARY WITH THE ROW, and the first version's did not.
# Inverting the row direction failed only 8 of 240 cases, because five of the
# six patterns were functions of x alone and read identically upside down.
# That is a corpus with no power over the one thing `h`'s sign decides, and a
# mutation count far below what the arm deserves is how it showed.  Every
# pattern below except the two degenerate ones now depends on y.
PATTERNS = {
    "all-key":     lambda x, y: 7,
    "none-key":    lambda x, y: 9 + (x & 3),
    "alternating": lambda x, y: 7 if ((x + y) & 1) else 3,
    "one-run":     lambda x, y: 7 if x < 5 + y else 4,
    "tail-key":    lambda x, y: 4 if x < 5 + 2 * y else 7,
    "per-row":     lambda x, y: 7 if ((x + y) & 7) < 3 else (x & 0xFF),
    "row-shift":   lambda x, y: 7 if ((x + 3 * y) % 11) < 4 else (y + 1) & 0xFF,
}

SIZES = ((1, 1), (4, 1), (5, 3), (16, 2), (254, 1), (255, 1), (256, 1),
         (257, 2), (300, 1), (13, 5))


def main():
    h = Harness()
    remap = bytes((i * 7 + 11) & 0xFF for i in range(256))
    cases = bad = 0

    for wide in (1, 0):
        for w, rows in SIZES:
            for sign in (1, -1):
                for name, pat in PATTERNS.items():
                    pix = build(w, rows, pat)
                    got_size, got = h.original(pix, w, rows * sign,
                                               remap, wide)
                    want = model(pix, w, rows * sign, remap, wide)
                    cases += 1
                    if got is None:
                        bad += 1
                        if bad <= 5:
                            print("  faulted: wide=%d %dx%d %s sign %+d"
                                  % (wide, w, rows, name, sign))
                        continue
                    if got_size != len(want) or got != want:
                        bad += 1
                        if bad <= 5:
                            print("  wide=%d %dx%d %s sign %+d: size %d vs %d"
                                  % (wide, w, rows, name, sign,
                                     got_size, len(want)))
                            for i in range(min(len(got), len(want))):
                                if got[i] != want[i]:
                                    print("    first differing byte %d: "
                                          "%02x vs %02x" % (i, got[i], want[i]))
                                    break

    print("rlecheck: %d cases, %d differ" % (cases, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
