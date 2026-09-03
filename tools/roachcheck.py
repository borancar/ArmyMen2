#!/usr/bin/env python3
"""Check RoachMaskWeight's point arithmetic against the original, exhaustively.

THE ROACH LAYER IS HEAVILY EXERCISED AND ENTIRELY UNCOMPARED, which CLAUDE.md
records at length: a live MAP 01 run gives RoachStepAllowed 366,870 calls and
RoachMaskWeight half a million, and `ab.sh campaign` reaches NONE of it -- that
configuration stops at the briefing on purpose, because the mission turns
hostile the moment the dialog clears and the log's 24 FIRE lines land in a
different order between two unsynchronised runs.  So the usual three artifacts
are all unavailable at once, and this function is covered without being checked.

WHAT THIS TOOL CAN AND CANNOT DO.  RoachMaskWeight cannot be emulated whole:
each point goes to ObjectsAtPoint and then to BlockWeightDamaging, which walks
the live object map and DAMAGES what it finds.  So this is a PARTIAL oracle of
the tools/vectors.py `ItemSetBox` shape -- the two callees are stubbed to
`xor eax, eax; ret` in the emulator's memory, and what is compared is the
sequence of POINTS the original asks about.

That is the half most likely to be wrong.  The indexing is
`dir & 0xFF` times a 164-byte stride into a table whose base is four bytes
before the points -- CLAUDE.md records that exact base being got wrong once,
with every point still correct -- and each axis is added in SIXTEEN BITS and
wraps independently.  What it does NOT cover is the accumulation, the
early-out on a zero count reaching the caller, and everything inside the two
stubs; say which half an oracle covers.

THE TABLES ARE SEEDED, and that is the whole design.  They live in .bss and
BuildRoachFootprints fills them at startup, so in the FILE every count is zero
and every case would return immediately -- the shakecheck failure exactly,
where an unseeded run passed while proving nothing.  The seeds here are chosen
to force the wrap: offsets that carry a low half past 0xFFFF and back, and
negative offsets that borrow.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu, SCRATCH

ROACH_MASK_WEIGHT = 0x0043D050
ROACH_MASK_COUNT = 0x00654CA8   # int32 count, then the points, stride 0xA4
STRIDE = 0xA4
OBJECTS_AT_POINT = 0x0042A550
BLOCK_WEIGHT_DAMAGING = 0x0043CF70

# xor eax, eax ; ret -- both callees are cdecl, so the caller cleans and a
# bare `ret` is safe.  ObjectsAtPoint answering null is a chain of nothing,
# which is what makes the walk below it uninteresting and stubbable.
STUB = b"\x31\xc0\xc3"

# Offsets chosen to make the sixteen-bit wrap observable rather than to look
# like a roach: a large positive that carries, a large negative that borrows,
# and small ones either side of zero.
SEED_POINTS = [(0, 0), (1, -1), (-1, 1), (0x4000, 0x4000),
               (-0x4000, -0x4000), (0x7FFF, -0x8000), (3, 7), (-9, 11)]

# `at` values that put each axis near a boundary the wrap crosses.
AT_VALUES = [0x00000000, 0x00010001, 0xFFFFFFFF, 0xFFFF0000, 0x0000FFFF,
             0x80008000, 0x7FFF7FFF, 0xC000C000, 0x40004000, 0x12345678]

DIRS = (0, 1, 2, 7, 31, 128, 200, 255)


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


class Harness:
    def __init__(self):
        self.emu = Emu()
        self.emu.uc.mem_write(OBJECTS_AT_POINT, STUB)
        self.emu.uc.mem_write(BLOCK_WEIGHT_DAMAGING, STUB)
        self.points = []

        from unicorn import UC_HOOK_CODE

        def at_stub(uc, address, size, _user):
            if address != OBJECTS_AT_POINT:
                return
            esp = uc.reg_read(__import__("unicorn").x86_const.UC_X86_REG_ESP)
            ptr = struct.unpack("<I", uc.mem_read(esp + 4, 4))[0]
            self.points.append(struct.unpack("<I", uc.mem_read(ptr, 4))[0])

        self.emu.uc.hook_add(UC_HOOK_CODE, at_stub)

    def seed(self, direction, points):
        """Write one direction's record: the count, then the int16 pairs."""
        base = ROACH_MASK_COUNT + direction * STRIDE
        self.emu.uc.mem_write(base, struct.pack("<i", len(points)))
        blob = b"".join(struct.pack("<hh", x, y) for x, y in points)
        self.emu.uc.mem_write(base + 4, blob)

    def original(self, direction, at):
        self.points = []
        eax, _ = self.emu.call(ROACH_MASK_WEIGHT,
                               [SCRATCH, direction, at, 0], b"\0" * 64)
        return (eax, list(self.points))


def model(points, at):
    """What the points SHOULD be: each axis added in sixteen bits."""
    lo, hi = at & 0xFFFF, (at >> 16) & 0xFFFF
    return [((lo + x) & 0xFFFF) | (((hi + y) & 0xFFFF) << 16)
            for x, y in points]


def main():
    h = Harness()
    cases = bad = 0

    for direction in DIRS:
        for n in range(1, len(SEED_POINTS) + 1):
            pts = SEED_POINTS[:n]
            h.seed(direction, pts)
            for at in AT_VALUES:
                eax, got = h.original(direction, at)
                if eax is None:
                    print("  faulted: dir %d, at %#010x" % (direction, at))
                    bad += 1
                    continue
                want = model(pts, at)
                cases += 1
                if got != want:
                    bad += 1
                    if bad <= 6:
                        print("  dir %3d at %#010x n=%d\n    original %s\n"
                              "    model    %s"
                              % (direction, at, n,
                                 [hex(v) for v in got], [hex(v) for v in want]))

    print("roachcheck: %d cases, %d differ" % (cases, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
