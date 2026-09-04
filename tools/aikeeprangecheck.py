#!/usr/bin/env python3
"""Check AiKeepRange against the original, over all eight of its decisions.

AiKeepRange (0x00405100) keeps a trooper at a distance from what it is
watching: it walks to a chosen point, and every so often chooses a new one.
One of the AI functions no drive here reaches.

CHECKS = ("AiKeepRange",)

EIGHT DECISIONS, and the corpus is built to reach each:

  - a deadline in OBJ_OFF_DEADLINE_58 compared UNSIGNED against the clock,
    which is what says "still walking to the point I chose"
  - the distance to that point against AM2_AI_REACHED_DIST, SIGNED
  - the observer, whose absence returns EARLY -- skipping the turn test that
    every other arm of the family still runs
  - the soldier kind against 6
  - the current range against the wanted one
  - a five-second timer, again unsigned
  - four separate refusals before the pose is set
  - and the seed against 4 and 0x10, which chooses BETWEEN two poses and
    whether to set one at all

THE EARLY RETURN IS THE INTERESTING ONE. Every sibling falls through to the
turn test; this one does not when there is no observer, so a unit with
nothing in sight does not even turn. That is one `je` to the epilogue rather
than to the tail, and it is invisible in a summary of what the function does.

WHAT IS STUBBED: ApproxDist, RandomPointToward and AiTrooperStep. The
distance is a parameter of the corpus rather than a computation, which is
what lets both sides of AM2_AI_REACHED_DIST be reached without building a
position; ApproxDist has its own vectors in tests/vectors.h.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AiKeepRange",)

AI_KEEP_RANGE = 0x00405100
APPROX_DIST = 0x0042DDE0
RANDOM_POINT_TOWARD = 0x00404E50
AI_TROOPER_STEP = 0x004049C0
ADDR_GAME_CLOCK_MS = 0x00511E04

SIGHTC_OFF_FIELD_00 = 0x00
SIGHTC_OFF_OBSERVER = 0x14
SIGHTC_OFF_RANGE = 0x18
SIGHTC_OFF_BEARING = 0x1C
SIGHTC_OFF_SEED = 0x3C
SIGHTC_OFF_KIND = 0x44
SIGHTC_OFF_WANT_RANGE = 0x4C

OBJ_OFF_DEADLINE_58 = 0x58
OBJ_OFF_FIELD_C0 = 0xC0
OBJ_OFF_DEADLINE_D0 = 0xD0
OBJ_OFF_FIELD_540 = 0x540
OBJ_OFF_SOLDIER_KIND = 0x544

REACHED = 12
KEEP_MS = 0x1388
DELAY = 0x82
CLOCK = 100000

DATA = 0x6E000000
DATA_SZ = 0x10000
OBJ = DATA + 0x1000
OUT = DATA + 0x2000
CTX = DATA + 0x3000
OBSERVER = DATA + 0x4000

SENT4 = 0xA5
SENT8 = 0x5EED5EED


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        for a in (APPROX_DIST, RANDOM_POINT_TOWARD, AI_TROOPER_STEP):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._dist, begin=APPROX_DIST,
                    end=APPROX_DIST)
        uc.hook_add(UC_HOOK_CODE, self._random,
                    begin=RANDOM_POINT_TOWARD, end=RANDOM_POINT_TOWARD)
        uc.hook_add(UC_HOOK_CODE, self._step,
                    begin=AI_TROOPER_STEP, end=AI_TROOPER_STEP)
        self.trace = []
        self.dist = 0

    def _ret(self, uc, val=0):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        uc.reg_write(x86_const.UC_X86_REG_EAX, val & 0xFFFFFFFF)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP,
                     struct.unpack("<I", uc.mem_read(esp, 4))[0])

    def _dist(self, uc, a, s, u):
        self.trace.append("dist"); self._ret(uc, self.dist)

    def _random(self, uc, a, s, u):
        self.trace.append("random"); self._ret(uc)

    def _step(self, uc, a, s, u):
        self.trace.append("step"); self._ret(uc)

    def run(self, dl58, dist, observer, kind, rng, want, f540, ctxkind,
            seed, ctx0, dlD0):
        uc = self.emu.uc
        uc.mem_write(OBJ, b"\0" * 0x600)
        uc.mem_write(OUT, b"\0" * 0x20)
        uc.mem_write(CTX, b"\0" * 0x60)
        self.trace = []
        self.dist = dist

        uc.mem_write(OBJ + OBJ_OFF_DEADLINE_58, struct.pack("<I", dl58))
        uc.mem_write(OBJ + OBJ_OFF_DEADLINE_D0, struct.pack("<I", dlD0))
        uc.mem_write(OBJ + OBJ_OFF_FIELD_540, struct.pack("<i", f540))
        uc.mem_write(OBJ + OBJ_OFF_SOLDIER_KIND, struct.pack("<i", kind))

        uc.mem_write(CTX + SIGHTC_OFF_FIELD_00, struct.pack("<i", ctx0))
        uc.mem_write(CTX + SIGHTC_OFF_OBSERVER,
                     struct.pack("<I", OBSERVER if observer else 0))
        uc.mem_write(CTX + SIGHTC_OFF_RANGE, struct.pack("<i", rng))
        uc.mem_write(CTX + SIGHTC_OFF_BEARING, bytes([0x66]))
        uc.mem_write(CTX + SIGHTC_OFF_SEED, bytes([seed]))
        uc.mem_write(CTX + SIGHTC_OFF_KIND, struct.pack("<i", ctxkind))
        uc.mem_write(CTX + SIGHTC_OFF_WANT_RANGE, struct.pack("<i", want))

        uc.mem_write(ADDR_GAME_CLOCK_MS, struct.pack("<I", CLOCK))
        uc.mem_write(OUT + 4, bytes([SENT4]))
        uc.mem_write(OUT + 8, struct.pack("<I", SENT8))

        self.emu.call(AI_KEEP_RANGE, [OBJ, OUT, CTX], b"\0" * 64,
                      count=100000)
        return (bytes(uc.mem_read(OUT + 4, 1))[0],
                struct.unpack("<I", bytes(uc.mem_read(OUT + 8, 4)))[0],
                struct.unpack("<I", bytes(uc.mem_read(OBJ + OBJ_OFF_DEADLINE_58, 4)))[0],
                tuple(self.trace))


def model(dl58, dist, observer, kind, rng, want, f540, ctxkind, seed, ctx0,
          dlD0):
    """src/game/region.cpp's AiKeepRange, in the same terms."""
    out4, out8 = SENT4, SENT8
    trace = []
    now = CLOCK

    if dl58 > now:                      # unsigned
        trace.append("dist")
        if dist >= REACHED:             # signed
            trace.append("step")
            return (out4, out8, dl58, tuple(trace))
        dl58 = 0

    if not observer:                    # EARLY return: no turn test
        return (out4, out8, dl58, tuple(trace))

    if kind < 6 and rng <= want:
        if dl58 == 0:
            dl58 = now
        if ((now - dl58) & 0xFFFFFFFF) > KEEP_MS:
            trace.append("random")
            trace.append("step")
            dl58 = (now + KEEP_MS) & 0xFFFFFFFF
        elif (not f540) and ctxkind != 3 and seed < 0x10 and ctx0 == 0:
            out8 = 7 if seed < 4 else 5

    if ((now - dlD0) & 0xFFFFFFFF) >= DELAY:
        out4 = 0x66
    return (out4, out8, dl58, tuple(trace))


def cases():
    for dl58 in (0, CLOCK - 1, CLOCK + 1, CLOCK - KEEP_MS - 1):
        for dist in (0, 11, 12, 13):
            for observer in (0, 1):
                for kind in (5, 6):
                    for rng, want in ((10, 20), (20, 10), (10, 10)):
                        for f540 in (0, 1):
                            for ctxkind in (0, 3):
                                for seed in (0, 3, 4, 0x0F, 0x10):
                                    yield (dl58, dist, observer, kind, rng,
                                           want, f540, ctxkind, seed, 0,
                                           CLOCK - DELAY)


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
    print("aikeeprangecheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
