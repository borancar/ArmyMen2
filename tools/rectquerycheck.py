#!/usr/bin/env python3
"""Check AllObjectsInRect against the original, over a built map.

AllObjectsInRect (0x0042A3D0) answers every object whose hit rectangle meets
a world rectangle, threading them through OBJ_OFF_QUERY_NEXT.  CLAUDE.md puts
it among the functions no drive here reaches and says both existing harnesses
refuse it: tools/vectors.py because it reads two globals, and AM2_SELFCHECK=1
because its map descriptor is null before install() and dereferencing it
would take the process down the way LookupOwnerObj did.  So it stood on
reading alone.

WHY IT IS WORTH ONE.  It is the sibling of ObjectsInRect, which runs 112
times on a drive, and the two were transcribed within an hour of each other.
Where they DIFFER is four bytes of jump apiece, and both differences are
invisible to any run that only exercises the other:

  - the entry clip is the OPPOSITE TEST.  ObjectsInRect rejects a rectangle
    entirely off the map and accepts one that overlaps; this one refuses
    unless the WHOLE rectangle is on the map.  A query straddling the edge is
    answered by one and refused outright by the other.
  - the home-cell rule has TWO arms here and three there.  This one lacks
    "the object's home cell is still ahead of us", so it answers such an
    object early where the sibling waits.

The corpus is built to reach both: rectangles that straddle each of the four
edges, and objects chained into a cell later than their own top-left.

INTERSECTRECT IS SHARED, NOT MODELLED TWICE.  The one call is an import
through the IAT, which does not exist under emulation.  The slot is pointed
at a mapped stub and the stub is implemented in Python -- and the MODEL calls
that same Python, so the import is not a second source of truth.  This
project already learned that lesson the other way round, in
tools/formationcheck.py, where a hand-written model of AngleDelta reported
256 mismatches that were all the model's; there the cure was to call the
image's own function, and here, where that is impossible, it is to have one
implementation serve both sides.

The stub address must be MAPPED even though every instruction at it is
hooked: a Unicorn code hook fires BEFORE the instruction executes, so an
unmapped page faults on the fetch and the hook never runs -- which reads
exactly like the original faulting.  tools/collectcheck.py records the same.

MUTATIONS, AND THE THREE THINGS THE CORPUS HAD TO LEARN.  Of 312 cases:
taking the sibling's entry clip fails 120, its third home-cell arm 20 by row
and 16 by column, the two clamps 6 and 4, reversing the chain 98, inverting
the destroyed test 158 and shifting the cell index 152.

Three of those failed NOTHING at first, and each gap was the corpus:

  - the home-cell guard only applies where `y > y0`, so an object in ROW 0
    cannot reach it however its hit rect is placed.  The first attempt at
    covering the dropped arm put such objects in row 0 and passed.
  - a cell index past the reported width ALIASES a later row's slot, and
    the column guard then skips whatever it finds there -- unless that
    object's own left lies in the out-of-range column.  So the clamp needs
    the descriptor to under-report the map AND an object beyond it.
  - with the world extent equal to `cols << CELL_SHIFT`, the entry clip
    already guarantees `right >> CELL_SHIFT <= cols - 1`, so both clamps are
    vacuous and neither can be mutated observably.  Nothing says a real
    map's descriptor and its extents agree, and the second map
    configuration is there because the original's author did not assume it.

The general shape is the one this project keeps re-learning: a guard is not
compared until something takes the arm that needs it.

WHAT IT DOES NOT COVER: the sibling's predicate arm, and whatever fills a
real map's cells.  The chains here are built by this tool.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AllObjectsInRect",)

ALL_OBJECTS_IN_RECT = 0x0042A3D0
IAT_INTERSECT_RECT = 0x0046F258

ADDR_MAP_EXTENT_X = 0x00514DD0
ADDR_MAP_EXTENT_Y = 0x00514DD4

CELL_SHIFT = 8
MAPDESC_COLS, MAPDESC_ROWS, MAPDESC_SHIFT, MAPDESC_CELLS = 0x00, 0x04, 0x08, 0x0C
NODE_OBJ, NODE_NEXT = 0x00, 0x08
OBJ_FLAGS, OBJ_HIT_RECT, OBJ_QUERY_NEXT = 0x08, 0x30, 0x68
OBJ_FLAG_DESTROYED = 0x04

DATA = 0x67000000
DATA_SZ = 0x00040000
MAPDESC = DATA + 0x100
CELLARRAY = DATA + 0x1000
NODES = DATA + 0x4000
OBJS = DATA + 0x8000
RECTBUF = DATA + 0x200
STUB = DATA + 0x30000

NODE_SZ = 0x10
OBJ_SZ = 0x80

SHIFT = 2

# (cols, rows, extent).  The second map reports FEWER cells than its world
# extent covers, and that configuration is not decoration: with extent equal
# to cols << CELL_SHIFT the entry clip already guarantees right >> 8 <=
# cols - 1, so the two clamps below it can never bite and a mutation to
# either passes every case.  Nothing says a real map's descriptor and its
# extents agree, and the clamps exist because the original's author did not
# assume it.
MAPS = ((4, 4, 4 << CELL_SHIFT), (4, 4, 8 << CELL_SHIFT))


def intersects(a, b):
    """Win32 IntersectRect's answer, as a bool.  ONE implementation, used by
    the emulated import and by the model alike."""
    left, top = max(a[0], b[0]), max(a[1], b[1])
    right, bottom = min(a[2], b[2]), min(a[3], b[3])
    return left < right and top < bottom


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(IAT_INTERSECT_RECT, struct.pack("<I", STUB))
        uc.mem_write(STUB, b"\xc3")          # mapped; every fetch is hooked

        from unicorn import UC_HOOK_CODE
        uc.hook_add(UC_HOOK_CODE, self._intersect_hook,
                    begin=STUB, end=STUB + 1)


        # MAPDESC_OFF_CELLS holds the ARRAY, not a pointer to it: the
        # original is `mov eax,[edx+0xc]` then `mov edi,[eax+ebx*4]`, two
        # loads.  The first version of this harness put a third level in and
        # every case came back empty, which reads as the original refusing
        # the query rather than as the map being malformed.


    def _intersect_hook(self, uc, addr, size, user):
        """IntersectRect(dst, a, b) -- stdcall, so the callee pops its three."""
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        ret, dst, pa, pb = struct.unpack("<4I", uc.mem_read(esp, 16))
        a = struct.unpack("<4i", uc.mem_read(pa, 16))
        b = struct.unpack("<4i", uc.mem_read(pb, 16))
        hit = intersects(a, b)
        if hit:
            uc.mem_write(dst, struct.pack("<4i", max(a[0], b[0]), max(a[1], b[1]),
                                          min(a[2], b[2]), min(a[3], b[3])))
        else:
            uc.mem_write(dst, struct.pack("<4i", 0, 0, 0, 0))
        uc.reg_write(x86_const.UC_X86_REG_EAX, 1 if hit else 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 16)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def run(self, objs, rect, cols, rows, extent):
        """objs: list of (cell, hit_rect, destroyed).  Returns the chain."""
        uc = self.emu.uc
        uc.mem_write(ADDR_MAP_EXTENT_X, struct.pack("<i", extent))
        uc.mem_write(ADDR_MAP_EXTENT_Y, struct.pack("<i", extent))
        uc.mem_write(MAPDESC, struct.pack("<4i", cols, rows, SHIFT, CELLARRAY))
        uc.mem_write(CELLARRAY, b"\0" * (2 << (SHIFT + SHIFT + 2)))
        uc.mem_write(NODES, b"\0" * (NODE_SZ * (len(objs) + 1)))
        uc.mem_write(OBJS, b"\0" * (OBJ_SZ * (len(objs) + 1)))

        heads = {}
        for i, (cell, hit, dead) in enumerate(objs):
            o = OBJS + i * OBJ_SZ
            uc.mem_write(o + OBJ_FLAGS,
                         struct.pack("<I", OBJ_FLAG_DESTROYED if dead else 0))
            uc.mem_write(o + OBJ_HIT_RECT, struct.pack("<4i", *hit))
            uc.mem_write(o + OBJ_QUERY_NEXT, struct.pack("<I", 0))

            n = NODES + i * NODE_SZ
            uc.mem_write(n + NODE_OBJ, struct.pack("<I", o))
            uc.mem_write(n + NODE_NEXT, struct.pack("<I", heads.get(cell, 0)))
            heads[cell] = n
        for cell, head in heads.items():
            uc.mem_write(CELLARRAY + cell * 4, struct.pack("<I", head))

        uc.mem_write(RECTBUF, struct.pack("<4i", *rect))
        eax, _ = self.emu.call(ALL_OBJECTS_IN_RECT, [RECTBUF, MAPDESC],
                               b"\0" * 64, count=2000000)

        out, seen = [], set()
        p = eax
        while p and p not in seen:
            seen.add(p)
            out.append((p - OBJS) // OBJ_SZ)
            p = struct.unpack("<I", bytes(uc.mem_read(p + OBJ_QUERY_NEXT, 4)))[0]
        return out


def model(objs, rect, cols, rows, extent):
    """src/game/win32/mapdraw.cpp's AllObjectsInRect, in the same terms."""
    left, top, right, bottom = rect
    if left < 0 or right >= extent or top < 0 or bottom >= extent:
        return []

    x0 = left >> CELL_SHIFT if left >= 0 else 0
    y0 = top >> CELL_SHIFT if top >= 0 else 0
    x1 = min(right >> CELL_SHIFT, cols - 1)
    y1 = min(bottom >> CELL_SHIFT, rows - 1)

    chains = {}
    for i, (cell, hit, dead) in enumerate(objs):
        chains.setdefault(cell, []).insert(0, i)   # newest first, as built

    head = []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            cell = (y << SHIFT) + x
            for i in chains.get(cell, []):
                _, hit, dead = objs[i]
                if dead:
                    continue
                if (hit[1] >> CELL_SHIFT) < y and y > y0:
                    continue
                if (hit[0] >> CELL_SHIFT) < x and x > x0:
                    continue
                if not intersects(hit, rect):
                    continue
                head.insert(0, i)
    return head


def cases():
    """Rects that straddle every edge, and objects that test both guards."""
    C = 1 << CELL_SHIFT
    inside = [(0, (10, 10, 60, 60), False),
              (0, (10, 10, 60, 60), True),          # destroyed
              (1, (C + 10, 10, C + 60, 60), False),
              (5, (C + 10, C + 10, C + 60, C + 60), False),
              # chained into cell 5 but its top-left is in cell 0: the arm
              # this function keeps and its sibling refuses.
              (5, (10, 10, C + 60, C + 60), False),
              # chained into cell 4, top-left in cell 0 by ROW only.
              (4, (C + 10, 10, C + 60, C + 60), False),
              # And the other way round: chained into cell 0 while its own
              # top-left is in cell 5, so its home cell is still AHEAD.  That
              # is the arm this function drops and its sibling keeps, and
              # without such an object the difference is unobservable -- a
              # mutation adding the sibling's third arm passed every case
              # until these two were added.
              # The guard only applies where y > y0, so an object in ROW 0
              # can never reach it however its hit rect is placed -- the
              # first two of these were added for that arm and could not
              # reach it.  It takes a cell in a later row than the query
              # starts, holding an object whose top-left is later still.
              (0, (C + 10, C + 10, C + 60, C + 60), False),
              (4, (10, 2 * C + 10, 60, 2 * C + 60), False),   # row ahead
              (1, (2 * C + 10, 10, 2 * C + 60, 60), False),   # column ahead
              # Only this object can make the two clamps observable.  A
              # cell index past the reported width aliases a later row's
              # slot, and the column guard then skips whatever it finds
              # there -- unless that object's own left lies in the
              # out-of-range column.  So the clamp needs the descriptor to
              # under-report the map AND an object beyond it, which is
              # exactly the pair the second map configuration supplies.
              (4, (4 * C + 10, 10, 4 * C + 60, 60), False),
              # The row clamp's mirror image.  A row index past the reported
              # height indexes past the whole cell array rather than
              # aliasing within it, so the array here is twice the map and
              # this object sits in the part beyond it.
              (16, (10, 4 * C + 10, 60, 4 * C + 60), False),
              (15, (3 * C + 10, 3 * C + 10, 3 * C + 60, 3 * C + 60), False)]

    rects = [
             (0, 0, C - 1, C - 1),                  # cell 0 alone
             (C, C, 2 * C - 1, 2 * C - 1),          # cell 5 alone
             (0, 0, 2 * C - 1, 2 * C - 1),          # cells 0,1,4,5
             (C // 2, C // 2, C + C // 2, C + C // 2),
             (-1, 0, C, C),                         # straddles the left edge
             (0, -1, C, C),                         # the top
             (0, 0, 8 * C, C),                      # far past the right
             (0, 0, C, 8 * C),                      # far past the bottom
             (3 * C, 3 * C, 4 * C - 1, 4 * C - 1),
             (-5, -5, 5 * C, 5 * C)]        # off every edge at once

    for cols, rows, extent in MAPS:
        for r in rects + [(0, 0, extent - 1, extent - 1),
                          (0, 0, extent - 1, C - 1),
                          (0, 0, C - 1, extent - 1)]:
            for n in range(1, len(inside) + 1):
                yield inside[:n], r, cols, rows, extent


def main():
    h = Harness()
    n = bad = 0
    for objs, rect, cols, rows, extent in cases():
        got = h.run(objs, rect, cols, rows, extent)
        want = model(objs, rect, cols, rows, extent)
        n += 1
        if got != want:
            bad += 1
            if bad <= 8:
                print("  rect %-26s %dx%d ext %d, %d objs: %s vs %s"
                      % (str(rect), cols, rows, extent, len(objs), got, want))
    print("rectquerycheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
