#!/usr/bin/env python3
"""Check AiStepIgnore against the original, over every arm it has.

AiStepIgnore (0x00407BF0) is the AI dispatcher's `ignore` arm.  CLAUDE.md
records the whole AI band as unreachable here -- `counts Ai` returns every
counter at 0 on a driven Boot Camp mission -- and tools/aicheck.py checks only
WHICH arm the dispatcher picks, stubbing the arms themselves.  This is the
first of the arms checked from the inside.

CHECKS = ("AiStepIgnore",)

FOUR THINGS DECIDE IT and each is a separate arm to reach:

  - the remembered destination's distance against AM2_AI_ARRIVED_DIST, which
    chooses between routing onward and having arrived
  - a hit not yet reacted to, which turns the unit only when the context
    holds no observer -- but is CONSUMED either way, so a unit hit while
    observing forgets the hit without acting on it
  - the context having found something, and
  - a delay compared UNSIGNED, so a deadline in the FUTURE wraps to a huge
    number and passes

That last one is the reason this needs an oracle rather than a reading.  A
deadline ahead of the clock is not a case any drive produces, and the
difference between `jb` and a signed compare is invisible until it happens.
The corpus puts the deadline on both sides of the clock and on the boundary.

THE OUTPUT IS out[1] AND THE OBJECT, NOT A RETURN VALUE.  Three paths write
out[1], two write nothing, and the function answers void -- so out[1] is
seeded with a sentinel and the object's fields are read back, which is what
tells "wrote nothing" from "wrote zero".  Whether AiRouteToward was called is
compared too: it is the whole of the first arm, and its absence is otherwise
indistinguishable from an arrival.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AiStepIgnore",)

AI_STEP_IGNORE = 0x00407BF0
AI_ROUTE_TOWARD = 0x00407190
ADDR_ZERO_POINT = 0x005125A0
ADDR_GAME_CLOCK_MS = 0x00511E04

SIGHT_OFF_OBSERVER = 0x10
SIGHT_OFF_BEARING = 0x18
SIGHT_OFF_FOUND = 0x1C
SIGHT_OFF_DEST_DIST = 0x28
OBJ_OFF_SCRIPT_STATE = 0xB4
OBJ_OFF_FIELD_C0 = 0xC0
OBJ_OFF_DEADLINE_D0 = 0xD0
OBJ_OFF_HIT_DIR = 0x104

AM2_AI_ARRIVED_DIST = 0x20
AM2_AI_TURN_DELAY_MS = 0x82

DATA = 0x6A000000
DATA_SZ = 0x10000
OBJ = DATA + 0x1000
OUT = DATA + 0x2000
CTX = DATA + 0x3000
OBSERVER = DATA + 0x4000

ZERO_POINT_VALUE = 0x00000000
SENTINEL = 0xA5


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(ADDR_ZERO_POINT, struct.pack("<I", ZERO_POINT_VALUE))

        from unicorn import UC_HOOK_CODE
        uc.mem_write(AI_ROUTE_TOWARD, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._route,
                    begin=AI_ROUTE_TOWARD, end=AI_ROUTE_TOWARD)
        self.routed = 0

    def _route(self, uc, addr, size, user):
        """AiRouteToward(obj, out, ctx, 0) -- cdecl; record and return."""
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        self.routed = 1
        uc.reg_write(x86_const.UC_X86_REG_EAX, 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP,
                     struct.unpack("<I", uc.mem_read(esp, 4))[0])

    def run(self, dist, hit, observer, found, bearing, clock, deadline, state):
        uc = self.emu.uc
        uc.mem_write(OBJ, b"\0" * 0x200)
        uc.mem_write(OUT, b"\0" * 0x20)
        uc.mem_write(CTX, b"\0" * 0x40)
        self.routed = 0

        uc.mem_write(OBJ + OBJ_OFF_SCRIPT_STATE, struct.pack("<I", state))
        uc.mem_write(OBJ + OBJ_OFF_FIELD_C0, struct.pack("<I", 0xDEADBEEF))
        uc.mem_write(OBJ + OBJ_OFF_DEADLINE_D0, struct.pack("<I", deadline))
        uc.mem_write(OBJ + OBJ_OFF_HIT_DIR, bytes([hit]))

        uc.mem_write(CTX + SIGHT_OFF_DEST_DIST, struct.pack("<i", dist))
        uc.mem_write(CTX + SIGHT_OFF_OBSERVER,
                     struct.pack("<I", OBSERVER if observer else 0))
        uc.mem_write(CTX + SIGHT_OFF_FOUND, struct.pack("<i", found))
        uc.mem_write(CTX + SIGHT_OFF_BEARING, bytes([bearing]))

        uc.mem_write(ADDR_GAME_CLOCK_MS, struct.pack("<I", clock))
        uc.mem_write(OUT + 1, bytes([SENTINEL]))

        self.emu.call(AI_STEP_IGNORE, [OBJ, OUT, CTX], b"\0" * 64,
                      count=100000)

        return (bytes(uc.mem_read(OUT + 1, 1))[0],
                struct.unpack("<I", bytes(uc.mem_read(OBJ + OBJ_OFF_SCRIPT_STATE, 4)))[0],
                struct.unpack("<I", bytes(uc.mem_read(OBJ + OBJ_OFF_FIELD_C0, 4)))[0],
                bytes(uc.mem_read(OBJ + OBJ_OFF_HIT_DIR, 1))[0],
                self.routed)


def model(dist, hit, observer, found, bearing, clock, deadline, state):
    """src/game/region.cpp's AiStepIgnore, in the same terms."""
    if dist > AM2_AI_ARRIVED_DIST:
        return (SENTINEL, state, state, hit, 1)

    out1 = SENTINEL
    st = ZERO_POINT_VALUE
    if hit:
        if not observer:
            out1 = hit
    hitleft = 0 if hit else hit

    if found and ((clock - deadline) & 0xFFFFFFFF) >= AM2_AI_TURN_DELAY_MS:
        out1 = bearing

    return (out1, st, 0xDEADBEEF, hitleft, 0)


def cases():
    for dist in (0, 0x1F, 0x20, 0x21, 0x400, -5):
        for hit in (0, 1, 0x7F, 0xFF):
            for observer in (0, 1):
                for found in (0, 1):
                    for clock, deadline in ((1000, 1000), (1000, 1000 - 0x81),
                                            (1000, 1000 - 0x82),
                                            (1000, 1000 - 0x83),
                                            (1000, 5000)):
                        yield (dist, hit, observer, found, 0x40, clock,
                               deadline & 0xFFFFFFFF, 0x11223344)


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got = h.run(*args)
        want = model(*args)
        n += 1
        if got != want:
            bad += 1
            if bad <= 8:
                print("  dist=%d hit=%02X obs=%d found=%d clock=%d dl=%d -> %s"
                      "\n        want %s"
                      % (args[0], args[1], args[2], args[3], args[5], args[6],
                         got, want))
    print("aiignorecheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
