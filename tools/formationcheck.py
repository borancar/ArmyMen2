"""Check FormationPointFar's ring arithmetic against the original, exhaustively.

Past slot 11 the twelve-entry ADDR_FORMATION_SLOTS table runs out and the
original computes the follower's facing and distance instead: sixteen slots to
a ring, the facing spread round it, the distance stepping 32 per ring, doubled
for a type 3, and then a swing away from the front of a vehicle. That whole
space is decided by the leader's type, the leader's facing and the slot, so it
can be ENUMERATED rather than sampled -- the same argument as
tools/moviecheck.py and tools/posecheck.py.

It matters here because no drive reaches it. Both drivable missions start with
a squad of ONE, so FormationPoint's counter is 0 on a full Boot Camp run and
this arm of it is verified by this tool or by nothing at all.

It emulates the original from its entry to 0x00404354 -- the instruction after
the last write to either value and before the x87 step -- and reads the facing
out of BL and the distance out of EDI. Stopping there is what makes the check
possible: past it the function calls Cos8, Sin8 and a settle that dispatches
through a global function pointer, none of which is set up outside the game.

So this covers the ARITHMETIC and nothing below it. The trig step, the clamp
to ADDR_MAP_BOUNDS_* and the settle are shared verbatim with FormationPoint
and stay verified by reading.

AngleDelta comes from the image too, rather than from a model of it written
here. A first version modelled the wrap by hand and reported 256 mismatches in
the type-3 swing that were entirely the model's -- the original masks and
wraps differently from the obvious `((a - b + 128) & 0xFF) - 128`. One source
of truth, and it is the binary.

    tools/formationcheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH, STACK, STACK_SZ, RET_MAGIC

from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, \
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EDI

ADDR         = 0x004042A0
STOP         = 0x00404354   # after both values are final, before the x87 step
ADDR_ANGLE   = 0x0042DD90

OBJ_OFF_POS    = 0x12
OBJ_OFF_FACING = 0x40

TYPES   = (0, 1, 2, 3, 8)
FACINGS = (0, 1, 63, 64, 127, 128, 200, 255)
SLOTS   = list(range(0, 80)) + [100, 127, 128, 200, 255, 256, 300, 1000]


class Harness:
    def __init__(self):
        self.emu = Emu()

    def _enter(self, args, start, stop, depth=0x1000):
        sp = STACK + STACK_SZ - depth
        for a in reversed(args):
            sp -= 4
            self.emu.uc.mem_write(sp, struct.pack("<I", a & 0xFFFFFFFF))
        sp -= 4
        self.emu.uc.mem_write(sp, struct.pack("<I", RET_MAGIC))
        self.emu.uc.reg_write(UC_X86_REG_ESP, sp)
        self.emu.uc.emu_start(start, stop, count=100000)
        return self.emu.uc.reg_read(UC_X86_REG_EIP) == stop

    def angle_delta(self, a, b):
        """The image's own 0x0042DD90 -- see the module comment."""
        if not self._enter([a, b], ADDR_ANGLE, RET_MAGIC, depth=0x2000):
            raise RuntimeError("AngleDelta did not return")
        v = self.emu.uc.reg_read(UC_X86_REG_EAX) & 0xFFFFFFFF
        return v - 0x100000000 if v >= 0x80000000 else v

    def original(self, type_, facing, slot):
        """(facing, distance) as the original computes them."""
        buf = bytearray(0x80)
        struct.pack_into("<i", buf, 0, type_)
        struct.pack_into("<hh", buf, OBJ_OFF_POS, 1000, 2000)
        buf[OBJ_OFF_FACING] = facing
        self.emu.uc.mem_write(SCRATCH, bytes(buf))
        out = SCRATCH + 0x100
        if not self._enter([SCRATCH, SCRATCH, out, slot], ADDR, STOP):
            return None
        return (self.emu.uc.reg_read(UC_X86_REG_EBX) & 0xFF,
                self.emu.uc.reg_read(UC_X86_REG_EDI) & 0xFFFFFFFF)


def rule(h, type_, facing, slot):
    """What air.cpp's FormationPointFar implements, restated."""
    if type_ == 3:
        leader_facing, shift = facing, 1
    elif type_ in (2, 8):
        leader_facing, shift = facing, 0
    else:
        leader_facing, shift = 0, 0

    f = ((((slot + 4) & 0xFF) << 4) & 0xFF)
    f = (f + leader_facing) & 0xFF

    # C's `/` truncates toward zero, which is what the original's
    # cdq/and 0xF/add/sar 4 does; Python's `//` floors, so it needs saying.
    q = slot - 11
    q = q // 16 if q >= 0 else -((-q) // 16)
    d = ((q + 4) * 32) << shift

    # ObjIsType3 is true for exactly the type-3 leaders here.
    if type_ == 3:
        delta = h.angle_delta(f, leader_facing)
        if abs(delta) < 0x40:
            f = (f + (0xC3 if delta < 0 else 0x3D)) & 0xFF
            d += 0x20

    return f, d & 0xFFFFFFFF


def main():
    h = Harness()
    n = bad = 0

    for type_ in TYPES:
        for facing in FACINGS:
            for slot in SLOTS:
                got  = h.original(type_, facing, slot)
                want = rule(h, type_, facing, slot)
                n += 1
                if got is None:
                    bad += 1
                    print("  type=%d facing=%d slot=%d -> FAULTED"
                          % (type_, facing, slot))
                elif got != want:
                    bad += 1
                    if bad <= 8:
                        print("  type=%d facing=%d slot=%d -> %s, expected %s"
                              % (type_, facing, slot, got, want))

    print("%d case(s), %d disagree" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
