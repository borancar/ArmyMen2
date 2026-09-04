#!/usr/bin/env python3
"""Check AiStepTrack and AiStepDefend, and that they still DIFFER.

CHECKS = ("AiStepTrack", "AiStepDefend")

These two are 224 and 208 bytes of what reads as one function written out
twice, and CLAUDE.md records the near-miss: noticing the resemblance,
factoring the shared tail into a helper both call, and flattening a real
difference in silence.  Normalised disassembly gives sixty-nine instructions
each, the same instructions in the same order, with ONE structural
difference -- a ten-instruction turn test sits BEFORE the second promotion in
one and AFTER it in the other.

WHAT THAT COSTS, stated as behaviour rather than as layout.  The still-moving
path jumps straight to the second promotion in both, so the turn test is
BEHIND that jump in AiStepDefend and AHEAD of it in AiStepTrack:

    routing (dist > AM2_AI_ARRIVED_DIST)   Track turns    Defend does NOT
    arrived                                both turn

So a tracking unit turns toward what it sees while still walking, and a
defending one only once it has arrived.  That is visible in the game and
invisible in a summary -- and it is the whole of the difference, which is why
this tool checks BOTH functions against one model parameterised by which they
are, and then asserts that the two disagree on the routing path.  A check
that only confirmed each against itself would still pass if one were
rewritten into the other.

THE PROMOTION IS IDEMPOTENT, which is why running it twice on the arrived
path -- both functions do -- changes nothing, and why the order matters only
where the turn test sits between the jump and the promotion.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AiStepTrack", "AiStepDefend")

AI_STEP_TRACK = 0x00407560
AI_STEP_DEFEND = 0x00407640
AI_ROUTE_TOWARD = 0x00407190
CONSIDER_SIGHTING = 0x004074A0
ADDR_ZERO_POINT = 0x005125A0
ADDR_GAME_CLOCK_MS = 0x00511E04

SIGHT_OFF_OBSERVER = 0x10
SIGHT_OFF_RANGE = 0x14
SIGHT_OFF_BEARING = 0x18
SIGHT_OFF_FOUND = 0x1C
SIGHT_OFF_FOUND_RANGE = 0x20
SIGHT_OFF_FOUND_BEARING = 0x24
SIGHT_OFF_DEST_DIST = 0x28

OBJ_OFF_SCRIPT_STATE = 0xB4
OBJ_OFF_FIELD_C0 = 0xC0
OBJ_OFF_TARGET_UID = 0xCC
OBJ_OFF_DEADLINE_D0 = 0xD0
OBJ_OFF_HIT_DIR = 0x104

ARRIVED = 0x20
DELAY = 0x82

DATA = 0x6B000000
DATA_SZ = 0x10000
OBJ = DATA + 0x1000
OUT = DATA + 0x2000
CTX = DATA + 0x3000
FOUND = DATA + 0x4000      # the object the context found; +4 is its uid
OBSERVER = DATA + 0x5000

ZERO = 0
SENTINEL = 0xA5
FOUND_UID = 0x1234ABCD


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(ADDR_ZERO_POINT, struct.pack("<I", ZERO))
        uc.mem_write(FOUND + 4, struct.pack("<I", FOUND_UID))

        from unicorn import UC_HOOK_CODE
        for a in (AI_ROUTE_TOWARD, CONSIDER_SIGHTING):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._route,
                    begin=AI_ROUTE_TOWARD, end=AI_ROUTE_TOWARD)
        uc.hook_add(UC_HOOK_CODE, self._consider,
                    begin=CONSIDER_SIGHTING, end=CONSIDER_SIGHTING)
        self.trace = []

    def _ret(self, uc):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        uc.reg_write(x86_const.UC_X86_REG_EAX, 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP,
                     struct.unpack("<I", uc.mem_read(esp, 4))[0])

    def _route(self, uc, addr, size, user):
        self.trace.append("route")
        self._ret(uc)

    def _consider(self, uc, addr, size, user):
        self.trace.append("consider")
        self._ret(uc)

    def run(self, fn, dist, hit, observer, found, clock, deadline, bearing,
            fbearing):
        uc = self.emu.uc
        uc.mem_write(OBJ, b"\0" * 0x200)
        uc.mem_write(OUT, b"\0" * 0x20)
        uc.mem_write(CTX, b"\0" * 0x40)
        self.trace = []

        uc.mem_write(OBJ + OBJ_OFF_SCRIPT_STATE, struct.pack("<I", 0x55667788))
        uc.mem_write(OBJ + OBJ_OFF_FIELD_C0, struct.pack("<I", 0xDEADBEEF))
        uc.mem_write(OBJ + OBJ_OFF_TARGET_UID, struct.pack("<I", 0xCAFEBABE))
        uc.mem_write(OBJ + OBJ_OFF_DEADLINE_D0, struct.pack("<I", deadline))
        uc.mem_write(OBJ + OBJ_OFF_HIT_DIR, bytes([hit]))

        uc.mem_write(CTX + SIGHT_OFF_DEST_DIST, struct.pack("<i", dist))
        uc.mem_write(CTX + SIGHT_OFF_OBSERVER,
                     struct.pack("<I", OBSERVER if observer else 0))
        uc.mem_write(CTX + SIGHT_OFF_BEARING, bytes([bearing]))
        uc.mem_write(CTX + SIGHT_OFF_FOUND,
                     struct.pack("<I", FOUND if found else 0))
        uc.mem_write(CTX + SIGHT_OFF_FOUND_RANGE, struct.pack("<I", 0x999))
        uc.mem_write(CTX + SIGHT_OFF_FOUND_BEARING, bytes([fbearing]))

        uc.mem_write(ADDR_GAME_CLOCK_MS, struct.pack("<I", clock))
        uc.mem_write(OUT + 1, bytes([SENTINEL]))

        self.emu.call(fn, [OBJ, OUT, CTX], b"\0" * 64, count=100000)

        rd = lambda a, n=4: struct.unpack("<I", bytes(uc.mem_read(a, 4)))[0]
        return (bytes(uc.mem_read(OUT + 1, 1))[0],
                rd(OBJ + OBJ_OFF_SCRIPT_STATE),
                rd(OBJ + OBJ_OFF_TARGET_UID),
                bytes(uc.mem_read(OBJ + OBJ_OFF_HIT_DIR, 1))[0],
                rd(CTX + SIGHT_OFF_OBSERVER),
                bytes(uc.mem_read(CTX + SIGHT_OFF_BEARING, 1))[0],
                tuple(self.trace))


def model(defend, dist, hit, observer, found, clock, deadline, bearing,
          fbearing):
    """One model, parameterised by WHICH of the twins this is."""
    out1 = SENTINEL
    state = 0x55667788
    uid = 0xCAFEBABE
    ctx_obs = OBSERVER if observer else 0
    ctx_bearing = bearing
    hitleft = hit
    trace = []

    def promote():
        nonlocal uid, ctx_obs, ctx_bearing
        if found:
            uid = FOUND_UID
            ctx_obs = FOUND
            ctx_bearing = fbearing

    def turn():
        nonlocal out1
        if ctx_obs and ((clock - deadline) & 0xFFFFFFFF) >= DELAY:
            out1 = ctx_bearing

    if dist > ARRIVED:
        trace.append("route")
        promote()                 # both jump INTO the second promotion
        if not defend:
            turn()                # ...which Track follows with the turn test
    else:
        state = ZERO
        if hit:
            if not ctx_obs:
                out1 = hit
            hitleft = 0
        promote()                 # the first promotion
        if defend:
            turn()
            promote()
        else:
            promote()
            turn()

    trace.append("consider")
    return (out1, state, uid, hitleft, ctx_obs, ctx_bearing, tuple(trace))


def cases():
    for dist in (0, 0x20, 0x21, 0x400):
        for hit in (0, 0x33):
            for observer in (0, 1):
                for found in (0, 1):
                    for clock, deadline in ((1000, 1000 - 0x82), (1000, 1000)):
                        yield dist, hit, observer, found, clock, deadline, \
                              0x11, 0x77


def main():
    h = Harness()
    n = bad = diverge = 0
    for args in cases():
        for defend, fn, name in ((0, AI_STEP_TRACK, "AiStepTrack"),
                                 (1, AI_STEP_DEFEND, "AiStepDefend")):
            got = h.run(fn, *args)
            want = model(defend, *args)
            n += 1
            if got != want:
                bad += 1
                if bad <= 6:
                    print("  %s %s ->\n     %s\n want %s"
                          % (name, args, got, want))
        # The two must still DISAGREE somewhere, or one has become the other.
        if h.run(AI_STEP_TRACK, *args) != h.run(AI_STEP_DEFEND, *args):
            diverge += 1

    print("aitwincheck: %d cases, %d differ; the twins disagree on %d input(s)"
          % (n, bad, diverge))
    if diverge == 0:
        print("  FAIL -- the two are behaving identically, so the structural"
              " difference has been flattened")
        return 1
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
