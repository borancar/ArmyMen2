#!/usr/bin/env python3
"""Check AiStepFollow against the original, over both routes to its route arm.

AiStepFollow (0x00407C80) is the AI dispatcher's `follow` arm, one of the
functions no drive here reaches.

CHECKS = ("AiStepFollow",)

WHAT MAKES IT DIFFERENT FROM ITS SIBLINGS, and worth its own corpus:

  - it zeroes OBJ_OFF_SCRIPT_STATE UNCONDITIONALLY at the top, where
    AiStepIgnore and the twins do it only once they have arrived
  - the route arm is reached TWO ways: the lead range exceeding
    AM2_AI_FOLLOW_SLACK, or the leader having MOVED since last frame, which
    is a call to PointsDiffer on two packed points inside the leader
  - the route arm's copy into OBJ_OFF_FIELD_C0 comes from the CONTEXT
    (SIGHT_OFF_DEST) rather than from the object's own script state, which
    every sibling uses

That third one is the kind of difference that survives a careless merge: the
instruction is the same shape and the source is not.

The turn test sits BEFORE the second promotion, as in AiStepDefend, so the
route path skips it -- a following unit does not turn toward what it sees
while it is still catching up.

WHAT IS STUBBED: AiRouteToward, ConsiderSighting and PointsDiffer, so what is
compared is which of them runs, in what order, and what the function does
around them. PointsDiffer is a parameter of the corpus rather than a
computation, which is what lets the leader-moved route be reached without
building a leader.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AiStepFollow",)

AI_STEP_FOLLOW = 0x00407C80
AI_ROUTE_TOWARD = 0x00407190
CONSIDER_SIGHTING = 0x004074A0
POINTS_DIFFER = 0x0042E110
ADDR_ZERO_POINT = 0x005125A0
ADDR_GAME_CLOCK_MS = 0x00511E04

SIGHT_OFF_LEADER = 0x00
SIGHT_OFF_LEAD_RANGE = 0x04
SIGHT_OFF_DEST = 0x0A
SIGHT_OFF_OBSERVER = 0x10
SIGHT_OFF_BEARING = 0x18
SIGHT_OFF_FOUND = 0x1C
SIGHT_OFF_FOUND_RANGE = 0x20
SIGHT_OFF_FOUND_BEARING = 0x24

OBJ_OFF_SCRIPT_STATE = 0xB4
OBJ_OFF_FIELD_C0 = 0xC0
OBJ_OFF_TARGET_UID = 0xCC
OBJ_OFF_DEADLINE_D0 = 0xD0
OBJ_OFF_HIT_DIR = 0x104

SLACK = 0xF0
DELAY = 0x82

DATA = 0x6D000000
DATA_SZ = 0x10000
OBJ = DATA + 0x1000
OUT = DATA + 0x2000
CTX = DATA + 0x3000
FOUND = DATA + 0x4000
LEADER = DATA + 0x5000
OBSERVER = DATA + 0x6000

ZERO = 0
SENTINEL = 0xA5
FOUND_UID = 0x1234ABCD
DEST = 0x0BADF00D


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(ADDR_ZERO_POINT, struct.pack("<I", ZERO))
        uc.mem_write(FOUND + 4, struct.pack("<I", FOUND_UID))

        from unicorn import UC_HOOK_CODE
        for a in (AI_ROUTE_TOWARD, CONSIDER_SIGHTING, POINTS_DIFFER):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._route,
                    begin=AI_ROUTE_TOWARD, end=AI_ROUTE_TOWARD)
        uc.hook_add(UC_HOOK_CODE, self._consider,
                    begin=CONSIDER_SIGHTING, end=CONSIDER_SIGHTING)
        uc.hook_add(UC_HOOK_CODE, self._differ,
                    begin=POINTS_DIFFER, end=POINTS_DIFFER)
        self.trace = []
        self.differ = 0

    def _ret(self, uc, val=0):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        uc.reg_write(x86_const.UC_X86_REG_EAX, val)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP,
                     struct.unpack("<I", uc.mem_read(esp, 4))[0])

    def _route(self, uc, a, s, u):
        self.trace.append("route"); self._ret(uc)

    def _consider(self, uc, a, s, u):
        self.trace.append("consider"); self._ret(uc)

    def _differ(self, uc, a, s, u):
        self.trace.append("differ"); self._ret(uc, self.differ)

    def run(self, lead, leader, differ, hit, observer, found, clock, deadline):
        uc = self.emu.uc
        uc.mem_write(OBJ, b"\0" * 0x200)
        uc.mem_write(OUT, b"\0" * 0x20)
        uc.mem_write(CTX, b"\0" * 0x40)
        uc.mem_write(LEADER, b"\0" * 0x40)
        self.trace = []
        self.differ = differ

        uc.mem_write(OBJ + OBJ_OFF_SCRIPT_STATE, struct.pack("<I", 0x55667788))
        uc.mem_write(OBJ + OBJ_OFF_FIELD_C0, struct.pack("<I", 0xDEADBEEF))
        uc.mem_write(OBJ + OBJ_OFF_TARGET_UID, struct.pack("<I", 0xCAFEBABE))
        uc.mem_write(OBJ + OBJ_OFF_DEADLINE_D0, struct.pack("<I", deadline))
        uc.mem_write(OBJ + OBJ_OFF_HIT_DIR, bytes([hit]))

        uc.mem_write(CTX + SIGHT_OFF_LEADER,
                     struct.pack("<I", LEADER if leader else 0))
        uc.mem_write(CTX + SIGHT_OFF_LEAD_RANGE, struct.pack("<i", lead))
        uc.mem_write(CTX + SIGHT_OFF_DEST, struct.pack("<I", DEST))
        uc.mem_write(CTX + SIGHT_OFF_OBSERVER,
                     struct.pack("<I", OBSERVER if observer else 0))
        uc.mem_write(CTX + SIGHT_OFF_BEARING, bytes([0x11]))
        uc.mem_write(CTX + SIGHT_OFF_FOUND,
                     struct.pack("<I", FOUND if found else 0))
        uc.mem_write(CTX + SIGHT_OFF_FOUND_RANGE, struct.pack("<I", 0x999))
        uc.mem_write(CTX + SIGHT_OFF_FOUND_BEARING, bytes([0x77]))
        uc.mem_write(ADDR_GAME_CLOCK_MS, struct.pack("<I", clock))
        uc.mem_write(OUT + 1, bytes([SENTINEL]))

        self.emu.call(AI_STEP_FOLLOW, [OBJ, OUT, CTX], b"\0" * 64,
                      count=100000)
        rd = lambda a: struct.unpack("<I", bytes(uc.mem_read(a, 4)))[0]
        return (bytes(uc.mem_read(OUT + 1, 1))[0],
                rd(OBJ + OBJ_OFF_SCRIPT_STATE),
                rd(OBJ + OBJ_OFF_FIELD_C0),
                rd(OBJ + OBJ_OFF_TARGET_UID),
                bytes(uc.mem_read(OBJ + OBJ_OFF_HIT_DIR, 1))[0],
                rd(CTX + SIGHT_OFF_OBSERVER),
                tuple(self.trace))


def model(lead, leader, differ, hit, observer, found, clock, deadline):
    """src/game/region.cpp's AiStepFollow, in the same terms."""
    out1 = SENTINEL
    state = ZERO                     # zeroed unconditionally, at the top
    c0 = 0xDEADBEEF
    uid = 0xCAFEBABE
    obs = OBSERVER if observer else 0
    bearing = 0x11
    hitleft = hit
    trace = []

    move = lead > SLACK
    if not move and leader:
        trace.append("differ")
        move = bool(differ)

    def promote():
        nonlocal uid, obs, bearing
        if found:
            uid, obs, bearing = FOUND_UID, FOUND, 0x77

    if move:
        c0 = DEST                    # from the CONTEXT, not the object
        trace.append("route")
    else:
        if hit:
            if not obs:
                out1 = hit
            hitleft = 0
        promote()
        if obs and ((clock - deadline) & 0xFFFFFFFF) >= DELAY:
            out1 = bearing

    promote()                        # the tail's promotion, on both paths
    trace.append("consider")
    return (out1, state, c0, uid, hitleft, obs, tuple(trace))


def cases():
    for lead in (0, 0xF0, 0xF1, 0x400):
        for leader in (0, 1):
            for differ in (0, 1):
                for hit in (0, 0x33):
                    for observer in (0, 1):
                        for found in (0, 1):
                            # The third pair is a deadline in the FUTURE,
                            # which the unsigned compare wraps and passes.
                            # Without it a signed comparison fails nothing.
                            for clock, deadline in ((1000, 1000 - 0x82),
                                                    (1000, 1000),
                                                    (1000, 9000)):
                                yield (lead, leader, differ, hit, observer,
                                       found, clock, deadline)


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got, want = h.run(*args), model(*args)
        n += 1
        if got != want:
            bad += 1
            if bad <= 6:
                print("  %s ->\n     %s\n want %s" % (args, got, want))
    print("aifollowcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
