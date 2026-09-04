#!/usr/bin/env python3
"""Check NearestClearVehiclePoint against the original: the SPIRAL itself.

NearestClearVehiclePoint (0x0045B930) walks outward from a point until it
finds one where the vehicle fits.  CLAUDE.md lists it among the functions no
drive here reaches -- and records that its sibling NearestClearPoint runs
EIGHT times on a Boot Camp drive and that returning the start point without
spiralling at all still left the run clean, because nothing the suite
compares mentions where something ends up on a map.

CHECKS = ("NearestClearVehiclePoint",)

SO THE SPIRAL IS THE THING, and what is compared is the SEQUENCE OF POINTS it
asks about -- the same choice tools/roachcheck.py makes for RoachMaskWeight,
and for the same reason: the answer is one point, but the walk that produced
it is the whole function and a wrong turn order gives the right answer from
the wrong place often enough to pass a spot check.

The geometry is four things, none of them checkable by reading:

  - the leg length grows every SECOND turn, not every turn -- the `grew` flag
    is what makes it a spiral rather than a diamond
  - the direction wraps at 3
  - each step is a table entry SHIFTED LEFT by 4, so the table's 1 is sixteen
    world units
  - and the bounds test comes FIRST, so a start point outside the map is
    stepped away from rather than returned

VehicleBlockWeight is stubbed to refuse everything, which is what makes the
walk observable; one case lets it accept at the Nth point, which is what
checks the early return lands on the point that was accepted rather than the
one after it.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("NearestClearVehiclePoint",)

NEAREST_CLEAR_VEH = 0x0045B930
POINT_IN_RECT = 0x0042E1F0
ROUND_TO_8 = 0x0042DFB0
VEHICLE_BLOCK_WEIGHT = 0x0045BC70
ADDR_SPIRAL_STEP = 0x00485340
CLEAR_WEIGHT = 0x1E
SHIFT = 4

DATA = 0x72000000
DATA_SZ = 0x10000
VEH = DATA + 0x1000
OUTPT = DATA + 0x2000

MAX_STEPS = 40


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        for a in (POINT_IN_RECT, ROUND_TO_8, VEHICLE_BLOCK_WEIGHT):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._inrect, begin=POINT_IN_RECT,
                    end=POINT_IN_RECT)
        uc.hook_add(UC_HOOK_CODE, self._round, begin=ROUND_TO_8,
                    end=ROUND_TO_8)
        uc.hook_add(UC_HOOK_CODE, self._weight,
                    begin=VEHICLE_BLOCK_WEIGHT, end=VEHICLE_BLOCK_WEIGHT)
        self.points = []
        self.accept_at = None
        self.inrect = 1
        self.snapped = 0

    def _take(self, uc, n):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        vals = struct.unpack("<%dI" % (n + 1), uc.mem_read(esp, 4 * (n + 1)))
        return esp, vals[0], vals[1:]

    def _done(self, uc, esp, ret, val):
        from unicorn import x86_const
        uc.reg_write(x86_const.UC_X86_REG_EAX, val & 0xFFFFFFFF)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def _inrect(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 2)
        self._done(uc, esp, ret, self.inrect)

    def _round(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 2)
        self.snapped = args[0]
        self._done(uc, esp, ret, args[0])

    def _weight(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 4)
        self.points.append(args[2])          # the packed point asked about
        n = len(self.points)
        clear = self.accept_at is not None and n >= self.accept_at
        self._done(uc, esp, ret, 0 if clear else CLEAR_WEIGHT)

    def run(self, start, facing, inrect, accept_at):
        uc = self.emu.uc
        self.points = []
        self.accept_at = accept_at
        self.inrect = inrect
        uc.mem_write(OUTPT, struct.pack("<I", 0))
        self.emu.call(NEAREST_CLEAR_VEH, [VEH, facing, start, OUTPT],
                      b"\0" * 64, count=2000000)
        return (struct.unpack("<I", bytes(uc.mem_read(OUTPT, 4)))[0],
                tuple(self.points[:MAX_STEPS]))


def spiral_table(emu):
    """The four steps the image ships, read rather than typed."""
    out = []
    for i in range(4):
        raw = emu.uc.mem_read(ADDR_SPIRAL_STEP + i * 8, 8)
        dx, dy = struct.unpack("<2i", raw)
        out.append((dx, dy))
    return out


def model(table, start, facing, inrect, accept_at):
    """src/game/item.cpp's NearestClearVehiclePoint, in the same terms."""
    x = start & 0xFFFF
    y = (start >> 16) & 0xFFFF
    to_s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    x, y = to_s16(x), to_s16(y)

    d = step = grew = 0
    leg = 1
    points = []

    while True:
        packed = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
        if inrect:
            points.append(packed)
            if accept_at is not None and len(points) >= accept_at:
                return packed, tuple(points[:MAX_STEPS])
        if len(points) >= MAX_STEPS and accept_at is None:
            return packed, tuple(points[:MAX_STEPS])
        if not inrect and len(points) == 0 and len(points) >= MAX_STEPS:
            break

        step += 1
        if step >= leg:
            step = 0
            d = 0 if d + 1 > 3 else d + 1
            if grew:
                leg += 1
                grew = 0
            else:
                grew = 1

        dx, dy = table[d]
        x = to_s16((x + (dx << SHIFT)) & 0xFFFF)
        y = to_s16((y + (dy << SHIFT)) & 0xFFFF)


def main():
    h = Harness()
    table = spiral_table(h.emu)
    n = bad = 0
    for start in (0x00100010, 0x00000000, 0x03200258):
        for facing in (0, 0x40, 0xFF):
            for accept_at in (1, 2, 5, 17):
                got = h.run(start, facing, 1, accept_at)
                want = model(table, start, facing, 1, accept_at)
                n += 1
                if got != want:
                    bad += 1
                    if bad <= 5:
                        print("  start=%08X facing=%d accept=%d ->\n"
                              "     %s\n want %s"
                              % (start, facing, accept_at, got, want))
    print("vehpointcheck: %d cases, %d differ  (spiral %s)"
          % (n, bad, table))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
