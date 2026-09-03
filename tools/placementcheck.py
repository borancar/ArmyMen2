#!/usr/bin/env python3
"""Check CanPlaceAt's refusals against the original, over a built corpus.

CanPlaceAt (0x0043A6D0) asks whether the thing in key-table slot `slot` can
stand at world point `at`: build its tile mask there, and answer 0 the moment
any cell of it fails.  CLAUDE.md lists it among the functions no drive here
reaches, and its counter is blind, so neither a run nor `counts` can speak
for it.

FOUR REFUSALS AND AN ORDER.  A cell is skipped unless bit 0 of its mask byte
is set; a cell whose weight has reached AM2_CELL_WEIGHT_STEP refuses; a cell
whose kind is not the one asked for refuses; and a cell with any object on it
refuses.  The ORDER matters, because the first failure returns -- so a case
that would fail two tests must fail on the earlier one, and swapping the
weight and kind tests is a mutation this corpus is built to catch.

THE MASK BUILDER IS ASSEMBLED, NOT STUBBED WITH `ret`.  ListMaskAction and
ListBoxAction WRITE the mask into a stack buffer belonging to CanPlaceAt,
whose address is not known until the call happens -- so a bare `ret` would
leave the buffer uninitialised and the scan would read whatever the stack
held.  Both are replaced with a short copier that takes the destination from
its own third argument and copies a prepared mask over it, preserving esi and
edi as cdecl requires.

WHICH KIND OF ORACLE THIS IS.  All four callees are reconstructed, so
CLAUDE.md's rule would allow replaying the cases against our C -- but that
would test something else.  Here the mask is SUPPLIED; in the game it is
computed by ListMaskAction and ListBoxAction from map globals.  So this is a
model-versus-original check of the SCAN, and the two mask builders keep
whatever standing they had.

WHAT IT DOES NOT COVER: how a real mask is built from a record, which is
those two functions' own job, and PointOfTile's arithmetic.  Both are stubbed.

MUTATIONS, AND THE ONE THAT IS A THEOREM RATHER THAN A GAP.  Of the model's
decisions, the weight boundary fails 60 cases, dropping the kind test 24,
dropping the occupancy test 24, reading the cell byte as "any bit set"
instead of bit 0 fails 48, and shifting each row's origin by one fails 30.
The last two failed NOTHING until the corpus was extended, and both gaps were
the corpus rather than the harness: every blocker filled the whole grid, so
an index error still landed on a blocked cell, and every cell byte was 0 or
1, so the original's "test al,al" and "test al,1" were the same predicate.
Position-dependent blockers and a cell value of 2 close both.

What stays uncaught is the `and eax,0xffff` the original applies to the tile
index: widening it to 24 bits changes no case.  That is not reachable rather
than untested -- with AM2_TILE_SHIFT of 4 the index only exceeds 16 bits
above y = 4096, and no map in the game has a thousandth of that many tile
rows.  Say which of a tool's gaps are gaps and which are theorems.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CAN_PLACE_AT = 0x0043A6D0
LIST_MASK_ACTION = 0x004385A0
LIST_BOX_ACTION = 0x00438F10
OBJECTS_HIT_BY_POINT = 0x0042A1B0
POINT_OF_TILE = 0x0042B250

KEY_TABLE_COUNT = 0x00516148
AAI_RECORDS = 0x0051614C
RECORD_LISTS = 0x00516140
CELL_WEIGHTS = 0x00514EC0
TILE_KIND = 0x00514ED4
MAP_ROW_SHIFT = 0x00514DE4

AAIREC_LIST_SLOT = 0x10
LISTHDR_MASK = 0x10
OBJMASK_BITS = 0x0C
WEIGHT_STEP = 0x0F

DATA, DATA_SZ = 0x64000000, 0x40000
AAI_ARRAY = DATA + 0x100
AAI_REC = DATA + 0x200
LIST_ARRAY = DATA + 0x300
LIST_HDR = DATA + 0x400
# The two grids are 0x10000 bytes EACH, and the first layout put them 0x8000
# apart -- so the kind grid ran over the mask source and zeroed it before
# every call.  The mask then read as all-zero, every cell was skipped, and the
# tool reported the original accepting placements it refuses.  A harness whose
# buffers overlap produces confident disagreements, which is the same failure
# shape as mprowcheck's cached stub.
WEIGHTS = DATA + 0x1000
KINDS = DATA + 0x11000
MASKSRC = DATA + 0x21000
HITCELL = DATA + 0x22000

MASK_BYTES = 0x90          # a 16-byte rect and 128 cells is ample here
SHIFT = 4                  # a 16-tile-wide map keeps the indices small


def copier(src, n):
    """push edi/esi; edi = arg3; esi = src; rep movsb; eax = 0; pop; ret."""
    return (b"\x57\x56"
            + b"\x8b\x7c\x24\x14"
            + b"\xbe" + struct.pack("<I", src)
            + b"\xb9" + struct.pack("<I", n)
            + b"\xf3\xa4"
            + b"\x31\xc0"
            + b"\x5e\x5f\xc3")


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        for a in (LIST_MASK_ACTION, LIST_BOX_ACTION):
            uc.mem_write(a, copier(MASKSRC, MASK_BYTES))
        # PointOfTile answers its argument; nothing here reads the point
        # except the hit test, which is itself a cell lookup.
        uc.mem_write(POINT_OF_TILE, b"\x8b\x44\x24\x04\xc3")
        # ObjectsHitByPoint: read the tile's own flag out of a parallel array.
        uc.mem_write(OBJECTS_HIT_BY_POINT,
                     b"\x8b\x44\x24\x04"              # mov eax,[esp+4] (pt*)
                     b"\x8b\x00"                      # mov eax,[eax]
                     b"\x25\xff\x00\x00\x00"          # and eax,0xFF
                     b"\x0f\xb6\x80" + struct.pack("<I", HITCELL)  # movzx
                     + b"\xc3")
        uc.mem_write(MAP_ROW_SHIFT, struct.pack("<i", SHIFT))
        uc.mem_write(AAI_ARRAY, struct.pack("<I", AAI_REC))
        uc.mem_write(AAI_RECORDS, struct.pack("<I", AAI_ARRAY))
        uc.mem_write(LIST_ARRAY, struct.pack("<I", LIST_HDR))
        uc.mem_write(RECORD_LISTS, struct.pack("<I", LIST_ARRAY))
        uc.mem_write(AAI_REC, b"\0" * 0x40)
        uc.mem_write(AAI_REC + AAIREC_LIST_SLOT, struct.pack("<i", 0))
        uc.mem_write(CELL_WEIGHTS, struct.pack("<I", WEIGHTS))
        uc.mem_write(TILE_KIND, struct.pack("<I", KINDS))

    def run(self, count, slot, rect, cells, weights, kinds, hits, kind,
            wide_mask):
        uc = self.emu.uc
        uc.mem_write(KEY_TABLE_COUNT, struct.pack("<i", count))
        uc.mem_write(LIST_HDR, b"\0" * 0x40)
        uc.mem_write(LIST_HDR + LISTHDR_MASK + OBJMASK_BITS,
                     struct.pack("<I", 0x1234 if wide_mask else 0))

        mask = bytearray(MASK_BYTES)
        struct.pack_into("<4i", mask, 0, *rect)
        for i, v in enumerate(cells):
            mask[16 + i] = v
        uc.mem_write(MASKSRC, bytes(mask))

        uc.mem_write(WEIGHTS, bytes(weights))
        uc.mem_write(KINDS, bytes(kinds))
        uc.mem_write(HITCELL, bytes(hits))

        eax, _ = self.emu.call(CAN_PLACE_AT, [0, slot, kind], b"\0" * 64,
                               count=2000000)
        return eax


def model(count, slot, rect, cells, weights, kinds, hits, kind):
    if slot >= count:
        return 1
    left, top, right, bottom = rect
    n = 0
    for y in range(top, bottom + 1):
        tile = (y << SHIFT) + left
        for x in range(left, right + 1):
            index = tile & 0xFFFF
            if cells[n] & 1:
                if weights[index] >= WEIGHT_STEP:
                    return 0
                if kinds[index] != kind:
                    return 0
                if hits[index & 0xFF]:
                    return 0
            tile += 1
            n += 1
    return 1


def cases():
    """Rects, cell masks and grids built so each refusal is reached alone."""
    grid = 0x10000
    for rect in ((0, 0, 0, 0), (0, 0, 2, 0), (0, 0, 1, 1), (2, 1, 4, 3)):
        wide = (rect[2] - rect[0] + 1) * (rect[3] - rect[1] + 1)
        for pattern in (0, 1, 2, 3, 4):
            # 0: no cell participates.  1: all do.  2: only the first.
            # 3: only the last -- which is what distinguishes a scan that
            # stops early from one that stops late.  4: every cell NON-ZERO
            # with bit 0 CLEAR, which is the one case that tells the
            # original's two tests apart: it does "test al,al; je" and then
            # "test al,1; je", and a corpus of only 0 and 1 makes those the
            # same predicate.  Without this, reading bit 0 as "any bit set"
            # passes every case.
            cells = [0] * 128
            if pattern == 1:
                cells = [1] * 128
            elif pattern == 2:
                cells[0] = 1
            elif pattern == 3:
                cells[wide - 1] = 1
            elif pattern == 4:
                cells = [2] * 128
            # The first four blockers fill the whole grid, so they say
            # nothing about WHICH index the scan reads -- an origin off by
            # one still lands on a blocked cell.  The last two block a
            # single index apiece, which is what makes the walk's
            # arithmetic observable.
            first = ((rect[1] << SHIFT) + rect[0]) & 0xFFFF
            last = ((rect[3] << SHIFT) + rect[2]) & 0xFFFF
            for blocker in ("none", "weight", "kind", "hit", "weight+kind",
                            "weight@first", "weight@last"):
                weights = bytearray(grid)
                kinds = bytearray(grid)
                hits = bytearray(0x100)
                if blocker in ("weight", "weight+kind"):
                    for i in range(grid):
                        weights[i] = WEIGHT_STEP
                if blocker == "weight@first":
                    weights[first] = WEIGHT_STEP
                if blocker == "weight@last":
                    weights[last] = WEIGHT_STEP
                if blocker in ("kind", "weight+kind"):
                    for i in range(grid):
                        kinds[i] = 9
                if blocker == "hit":
                    for i in range(0x100):
                        hits[i] = 1
                yield rect, cells, weights, kinds, hits, blocker, pattern


def main():
    h = Harness()
    n = bad = 0

    for wide_mask in (0, 1):
        for slot, count in ((0, 1), (5, 1)):        # in range, and past the end
            for rect, cells, weights, kinds, hits, blocker, pat in cases():
                got = h.run(count, slot, rect, cells, weights, kinds, hits,
                            0, wide_mask)
                want = model(count, slot, rect, cells, weights, kinds, hits, 0)
                n += 1
                if got != want:
                    bad += 1
                    if bad <= 6:
                        print("  rect %s pat %d %s slot %d/%d wide %d: "
                              "%s vs %s" % (rect, pat, blocker, slot, count,
                                            wide_mask, got, want))

    print("placementcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
