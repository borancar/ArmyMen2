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

TWO ORACLES, and the second covers RoachStepAllowed's SPEED -- the head of
the function, up to 0x0043D168, where the answer is final and before it
reaches the object's rows, its animation and the map.  Stop the emulation
where the answer is settled, not where the function returns; that is
formationcheck's rule and it is what makes this one possible at all.

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
        self.speed_seen = None

        from unicorn import UC_HOOK_CODE

        def at_stub(uc, address, size, _user):
            if address == SPEED_SETTLED:
                esp = uc.reg_read(
                    __import__("unicorn").x86_const.UC_X86_REG_ESP)
                self.speed_seen = struct.unpack(
                    "<i", uc.mem_read(esp + 0x10, 4))[0]
                uc.emu_stop()
                return
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

    def speed(self, state, reverse, f44, vels, delta):
        """The speed local as the original leaves it at SPEED_SETTLED."""
        uc = self.emu.uc
        for addr, v in zip((ROACH_FORVEL, ROACH_REVVEL,
                            ROACH_FORACC, ROACH_REVACC), vels):
            uc.mem_write(addr, struct.pack("<i", v))
        uc.mem_write(FRAME_DELTA, struct.pack("<f", delta))
        uc.mem_write(CTRL, b"\0" * 0x40)
        uc.mem_write(CTRL + 0x14, struct.pack("<ii", state, reverse))
        uc.mem_write(OBJ, b"\0" * 0x80)
        uc.mem_write(OBJ + 0x44, struct.pack("<i", f44))

        self.speed_seen = None
        self.emu.call(STEP_ALLOWED, [OBJ, CTRL, SCRATCH + 0x400], b"\0" * 64)
        return self.speed_seen

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


# ---- the second oracle: RoachStepAllowed's speed -------------------------
#
# STOP WHERE THE ANSWER IS FINAL, NOT WHERE THE FUNCTION RETURNS.  Past
# 0x0043D168 it reads the object's row, its playing animation and the map, so
# a whole-function emulation is impossible and a partial one is easy -- the
# formationcheck precedent exactly.  The speed local is settled by then and
# lives at [esp+0x10] throughout that region, which tools/espmap.py confirms
# is the slot all four of its writes use.
#
# ESPMAP IS ALSO WHAT KEPT A FALSE DEFECT OUT.  Hand-counting the prologue
# suggested the original reads the STATE from argument 1 and the speed field
# from argument 2, which is the opposite of the reconstruction -- the
# ADDR_ENTER_VEHICLE shape, and it would have been written up as one.  The
# count had dropped the leading `push ecx`.  espmap puts the three arguments
# at slots +0x18, +0x1C and +0x20 and shows ebx taking the second and edi the
# first, which is what the C already does.
STEP_ALLOWED = 0x0043D0F0
SPEED_SETTLED = 0x0043D168

ROACH_FORVEL = 0x00487BB8
ROACH_REVVEL = 0x00487BBC
ROACH_FORACC = 0x00487BC0
ROACH_REVACC = 0x00487BC4
FRAME_DELTA = 0x00511E10

# CLEAR OF THE SCRATCH Emu.call WRITES.  It lays its scratch bytes down at
# SCRATCH itself, so a record placed there is zeroed AFTER being seeded --
# which is how the first run of this oracle came to report 348 differences:
# every case ran the forward arm because `state` and `reverse` had been wiped.
# The seeds have to outlive the call that uses them.
CTRL = SCRATCH + 0x100
OBJ = SCRATCH + 0x200

# (forvel, revvel, foracc, revacc) -- the second pair is deliberately larger
# than the first so the caps actually bite, and the third has them equal.
VELS = ((40, 30, 200, 150), (5, 5, 4000, 4000), (100, 100, 100, 100))
DELTAS = (0.0, 0.016, 0.05, 0.5)
FIELD44 = (-5000, -100, -1, 0, 1, 7, 100, 5000)


def ftol(x):
    """MSVC _ftol truncates toward zero, and so does a C cast to int32_t."""
    v = int(x)
    return ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def speed_model(state, reverse, f44, vels, delta):
    if state == 1:
        return 0
    forvel, revvel, foracc, revacc = vels
    if reverse:
        v = ftol(float(f44) - float(revacc) * delta)
        return max(-revvel, v)
    v = ftol(float(foracc) * delta + float(f44))
    return min(forvel, v)


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

    print("roachcheck: mask points -- %d cases, %d differ" % (cases, bad))

    cases2 = bad2 = 0
    for vels in VELS:
        for delta in DELTAS:
            for state in (0, 1, 2):
                for reverse in (0, 1):
                    for f44 in FIELD44:
                        got = h.speed(state, reverse, f44, vels, delta)
                        want = speed_model(state, reverse, f44, vels, delta)
                        cases2 += 1
                        if got is None or got != want:
                            bad2 += 1
                            if bad2 <= 6:
                                print("  state %d rev %d f44 %6d vels %s d %.3f"
                                      "  original %s model %s"
                                      % (state, reverse, f44, vels, delta,
                                         got, want))

    print("roachcheck: step speed  -- %d cases, %d differ" % (cases2, bad2))
    return 1 if (bad or bad2) else 0


if __name__ == "__main__":
    raise SystemExit(main())
