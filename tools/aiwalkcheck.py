#!/usr/bin/env python3
"""Check AiWalkStep against the original, over both of its arms.

AiWalkStep (0x00405D30) is the trooper's walk step, and one of the AI
functions no drive here reaches -- `counts Ai` returns the whole band at 0 on
a driven Boot Camp mission.

CHECKS = ("AiWalkStep",)

IT IS THE FAMILY'S SHAPE WITH DIFFERENT NUMBERS, and that is the reason to
check it rather than to read it beside its siblings. AiStepIgnore, AiStepTrack
and AiStepDefend all compare a destination distance and then either route on
or settle; this one uses SIGHTC_OFF_DEST_DIST at 0x34 against 12 where they
use SIGHT_OFF_DEST_DIST at 0x28 against 32, reads SIGHTC_OFF_FOUND at 0x20
rather than 0x1C, and writes `out[4]` where every sibling writes `out[1]`.
Four numbers, none of them shared, in functions that otherwise read alike --
which is exactly the shape CLAUDE.md records as costing a rewrite when one is
written from another's outline.

WHAT IS STUBBED AND WHY. AiTrooperStep is the whole of the routing arm and
AiHitReact the whole of the hit handling; both are called for real by the
original and both are stubbed here, so what this compares is WHICH is called
and what the function does around them. AiHitReact has its own oracle --
tools/hitreactcheck.py, 14,594 cases replayed against our C -- so stubbing it
here loses nothing and keeps the two checks from restating each other.

THE DELAY IS UNSIGNED, as it is in AiStepIgnore: a deadline in the future
wraps and passes. The corpus puts the deadline on both sides of the clock.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AiWalkStep",)

AI_WALK_STEP = 0x00405D30
AI_TROOPER_STEP = 0x004049C0
AI_HIT_REACT = 0x00405050
ADDR_ZERO_POINT = 0x005125A0
ADDR_GAME_CLOCK_MS = 0x00511E04

SIGHTC_OFF_FOUND = 0x20
SIGHTC_OFF_BEARING = 0x1C
SIGHTC_OFF_DEST_DIST = 0x34
OBJ_OFF_SCRIPT_STATE = 0xB4
OBJ_OFF_FIELD_C0 = 0xC0
OBJ_OFF_DEADLINE_D0 = 0xD0

REACHED = 12
DELAY = 0x82

DATA = 0x6C000000
DATA_SZ = 0x10000
OBJ = DATA + 0x1000
OUT = DATA + 0x2000
CTX = DATA + 0x3000
FOUND = DATA + 0x4000

ZERO = 0
SENTINEL = 0xA5
STATE = 0x55667788


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(ADDR_ZERO_POINT, struct.pack("<I", ZERO))

        from unicorn import UC_HOOK_CODE
        for a in (AI_TROOPER_STEP, AI_HIT_REACT):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._trooper,
                    begin=AI_TROOPER_STEP, end=AI_TROOPER_STEP)
        uc.hook_add(UC_HOOK_CODE, self._hit,
                    begin=AI_HIT_REACT, end=AI_HIT_REACT)
        self.trace = []

    def _ret(self, uc):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        uc.reg_write(x86_const.UC_X86_REG_EAX, 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP,
                     struct.unpack("<I", uc.mem_read(esp, 4))[0])

    def _trooper(self, uc, addr, size, user):
        self.trace.append("trooper")
        self._ret(uc)

    def _hit(self, uc, addr, size, user):
        self.trace.append("hit")
        self._ret(uc)

    def run(self, dist, found, clock, deadline, bearing):
        uc = self.emu.uc
        uc.mem_write(OBJ, b"\0" * 0x200)
        uc.mem_write(OUT, b"\0" * 0x20)
        uc.mem_write(CTX, b"\0" * 0x60)
        self.trace = []

        uc.mem_write(OBJ + OBJ_OFF_SCRIPT_STATE, struct.pack("<I", STATE))
        uc.mem_write(OBJ + OBJ_OFF_FIELD_C0, struct.pack("<I", 0xDEADBEEF))
        uc.mem_write(OBJ + OBJ_OFF_DEADLINE_D0, struct.pack("<I", deadline))

        uc.mem_write(CTX + SIGHTC_OFF_DEST_DIST, struct.pack("<i", dist))
        uc.mem_write(CTX + SIGHTC_OFF_FOUND,
                     struct.pack("<I", FOUND if found else 0))
        uc.mem_write(CTX + SIGHTC_OFF_BEARING, bytes([bearing]))
        uc.mem_write(ADDR_GAME_CLOCK_MS, struct.pack("<I", clock))
        uc.mem_write(OUT + 4, bytes([SENTINEL]))

        self.emu.call(AI_WALK_STEP, [OBJ, OUT, CTX], b"\0" * 64, count=100000)
        rd = lambda a: struct.unpack("<I", bytes(uc.mem_read(a, 4)))[0]
        return (bytes(uc.mem_read(OUT + 4, 1))[0],
                rd(OBJ + OBJ_OFF_SCRIPT_STATE),
                rd(OBJ + OBJ_OFF_FIELD_C0),
                tuple(self.trace))


def model(dist, found, clock, deadline, bearing):
    """src/game/region.cpp's AiWalkStep, in the same terms."""
    if dist > REACHED:
        return (SENTINEL, STATE, STATE, ("trooper",))

    out4 = SENTINEL
    if found and ((clock - deadline) & 0xFFFFFFFF) >= DELAY:
        out4 = bearing
    return (out4, ZERO, 0xDEADBEEF, ("hit",))


def cases():
    for dist in (0, 11, 12, 13, 200, -3):
        for found in (0, 1):
            for clock, deadline in ((1000, 1000 - 0x81), (1000, 1000 - 0x82),
                                    (1000, 1000), (1000, 9000)):
                for bearing in (0x00, 0x5A, 0xFF):
                    yield dist, found, clock, deadline & 0xFFFFFFFF, bearing


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got, want = h.run(*args), model(*args)
        n += 1
        if got != want:
            bad += 1
            if bad <= 8:
                print("  dist=%d found=%d clock=%d dl=%d bearing=%02X ->\n"
                      "     %s\n want %s" % (args + (got, want)))
    print("aiwalkcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
