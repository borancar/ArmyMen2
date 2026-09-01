"""Check BuildTileDeltas against the original, over a range of map widths.

The function takes ONE number -- ADDR_MAP_TILES_W -- and writes four tables of
tile-index deltas from it. So the whole input space is a single int32, and a
handful of widths exercises every store: there are no branches at all, sixty-odd
straight-line writes with the compiler juggling six registers.

It matters here because NOTHING ELSE CAN CHECK IT. The tables never reach the
screen or the log; they are rebuilt per map and consumed by the cover pair, the
region walkers and the decal placer. A wrong delta shows up as a unit pathing
oddly or a footprint landing on the wrong cell, which is not something
`tools/ab.sh` compares -- the same standing as the trig tables and the roach
footprints, and the same answer: emulate the original and diff the bytes.

It is also what makes the LOOP in the reconstruction safe. The original has no
loop; the grouping in region.cpp is ours, and this is the evidence that the
grouping produces the same 49 dwords.

    tools/ringcheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

ADDR = 0x00437B60

MAP_TILES_W     = 0x00514DDC
DECAL_RING8     = 0x00523DA0   # int32[8]
TILE_NEIGHBOURS = 0x00654BD8   # int32[20]
TILE_RING8      = 0x0053C480   # int32[17]
TILE_RING4      = 0x00554B70   # int32[4]

# Widths a real map could have, plus small and large ones the arithmetic has no
# reason to care about but which would expose a sign or a shift.
WIDTHS = [7, 32, 64, 96, 128, 160, 200, 256, 512, 1024]


def expected(w):
    """What region.cpp's BuildTileDeltas writes, in the same four tables."""
    ring = [-1 - w, -w, 1 - w, -1, 1, w + 1, w, w - 1]
    return {
        "DECAL_RING8":     ring,
        "TILE_RING8":      ring + ring + [-1],
        "TILE_NEIGHBOURS": [-1 - 2 * w, -2 * w, 1 - 2 * w,
                            -2 - w, -1 - w, -w, 1 - w, 2 - w,
                            -2, -1, 1, 2,
                            w - 2, w - 1, w, w + 1, w + 2,
                            2 * w - 1, 2 * w, 2 * w + 1],
        "TILE_RING4":      [-w, 1, w, -1],
    }


def main():
    emu = Emu()
    bad = 0

    for w in WIDTHS:
        emu.uc.mem_write(MAP_TILES_W, struct.pack("<i", w))
        # Poison the tables first: a store the reconstruction makes and the
        # original does not would otherwise pass by inheriting the last run.
        for base, n in ((DECAL_RING8, 8), (TILE_NEIGHBOURS, 20),
                        (TILE_RING8, 17), (TILE_RING4, 4)):
            emu.uc.mem_write(base, b"\xAA" * (4 * n))

        if emu.call(ADDR, [], b"")[0] is None:
            print("w=%d: the original faulted" % w)
            bad += 1
            continue

        want = expected(w)
        for name, base in (("DECAL_RING8", DECAL_RING8),
                           ("TILE_NEIGHBOURS", TILE_NEIGHBOURS),
                           ("TILE_RING8", TILE_RING8),
                           ("TILE_RING4", TILE_RING4)):
            n   = len(want[name])
            got = list(struct.unpack("<%di" % n, emu.uc.mem_read(base, 4 * n)))
            if got == want[name]:
                continue
            bad += 1
            print("w=%d %s differs:" % (w, name))
            for i, (g, e) in enumerate(zip(got, want[name])):
                if g != e:
                    print("  [%2d] original %-8d ours %-8d" % (i, g, e))

    total = sum(len(v) for v in expected(0).values())
    if bad:
        print("ringcheck: %d table(s) differ" % bad)
        return 1
    print("ringcheck: %d widths x %d dwords, all identical"
          % (len(WIDTHS), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
