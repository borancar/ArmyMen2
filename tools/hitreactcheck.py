#!/usr/bin/env python3
"""Check AiHitReact against the original, and record the cases for replay.

AiHitReact (0x00405050) is what a unit does about having been hit: choose a
pose, turn toward the hit if it is not already watching something, and consume
OBJ_OFF_HIT_DIR.  It is one of the nine AI functions CLAUDE.md lists as
unexercised -- `counts Ai` returns the whole band at 0 on a driven Boot Camp
mission, because Boot Camp's enemies never engage.

THIS ONE IS A REPLAY ORACLE, NOT A MODEL COMPARISON, and the distinction is
the one CLAUDE.md draws between tools/firepose.py and tools/regioncheck.py.
AiHitReact CALLS NOTHING -- not one call instruction in its 176 bytes -- and
reads only its three arguments and two constant tables the image ships.  So
nothing it reaches is still the image's, and `--emit` writes the recorded
cases to tests/hitreactvec.h for tests/selftest.cpp to replay against our C.
That proves the transcription, where a model comparison only proves the
model.

THE LADDER IS WHY IT NEEDS A CORPUS RATHER THAN A READING.  The unit's rank
selects a threshold from ADDR_RANK_RECORDS -- 32, 48, 56, 64, 80, 96, 112,
128 -- and the seed is compared against HALF of it and then against ALL of
it, giving three bands: a pose from ADDR_HIT_POSE_BY_CLASS, the heavy pose,
or no pose at all.  Both boundaries move with rank, so a corpus that does not
straddle each of the sixteen values proves nothing about the comparisons; the
seeds here are chosen to sit just below, on, and just above every one.

WHAT THE SENTINELS ARE FOR.  Two of the arms write NOTHING -- the top band
and the kind-8 exit -- so both output fields are seeded before each case and
"wrote nothing" is a distinct answer from "wrote zero".  An oracle comparing
a return value would learn nothing at all: the function returns void.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("AiHitReact",)

AI_HIT_REACT = 0x00405050
ADDR_RANK_RECORDS = 0x00473DC0
ADDR_HIT_POSE_BY_CLASS = 0x00475198
RANK_REC_BYTES = 28
RANK_REC_OFF_THRESHOLD = 16

OBJ_OFF_RANK = 0x98
OBJ_OFF_HIT_DIR = 0x104
OBJ_OFF_SOLDIER_KIND = 0x544

SIGHTC_OFF_FIELD_00 = 0x00
SIGHTC_OFF_OBSERVER = 0x14
SIGHTC_OFF_SEED = 0x3C
SIGHTC_OFF_KIND = 0x44

POSE_KIND7 = 0x2B
POSE_HIT_HEAVY = 7

DATA = 0x68000000
DATA_SZ = 0x00010000
OBJ = DATA + 0x1000
OUT = DATA + 0x3000
CTX = DATA + 0x4000
OBSERVER = DATA + 0x5000

POSE_SENTINEL = 0x5EED5EED         # what out+8 holds before the call
TURN_SENTINEL = 0xA5               # what out+4 holds before the call


class Harness:
    def __init__(self):
        self.emu = Emu()
        self.emu.uc.mem_map(DATA, DATA_SZ)

    def thresholds(self):
        """The eight rank thresholds, read from the image rather than typed."""
        uc = self.emu.uc
        return [struct.unpack("<i", bytes(uc.mem_read(
            ADDR_RANK_RECORDS + r * RANK_REC_BYTES + RANK_REC_OFF_THRESHOLD,
            4)))[0] for r in range(8)]

    def poses(self):
        uc = self.emu.uc
        return list(struct.unpack("<6i", bytes(uc.mem_read(
            ADDR_HIT_POSE_BY_CLASS, 24))))

    def run(self, hit, kind, rank, ctxkind, seed, cls, observer):
        uc = self.emu.uc
        uc.mem_write(OBJ, b"\0" * 0x600)
        uc.mem_write(CTX, b"\0" * 0x60)
        uc.mem_write(OUT, b"\0" * 0x20)

        uc.mem_write(OBJ + OBJ_OFF_HIT_DIR, bytes([hit]))
        uc.mem_write(OBJ + OBJ_OFF_SOLDIER_KIND, struct.pack("<i", kind))
        uc.mem_write(OBJ + OBJ_OFF_RANK, struct.pack("<i", rank))

        uc.mem_write(CTX + SIGHTC_OFF_FIELD_00, struct.pack("<i", cls))
        uc.mem_write(CTX + SIGHTC_OFF_OBSERVER,
                     struct.pack("<I", OBSERVER if observer else 0))
        uc.mem_write(CTX + SIGHTC_OFF_SEED, bytes([seed]))
        uc.mem_write(CTX + SIGHTC_OFF_KIND, struct.pack("<i", ctxkind))

        uc.mem_write(OUT + 8, struct.pack("<I", POSE_SENTINEL))
        uc.mem_write(OUT + 4, bytes([TURN_SENTINEL]))

        self.emu.call(AI_HIT_REACT, [OBJ, OUT, CTX], b"\0" * 64, count=100000)

        pose = struct.unpack("<I", bytes(uc.mem_read(OUT + 8, 4)))[0]
        turn = bytes(uc.mem_read(OUT + 4, 1))[0]
        consumed = bytes(uc.mem_read(OBJ + OBJ_OFF_HIT_DIR, 1))[0]
        return pose, turn, consumed


def model(limits, poses, hit, kind, rank, ctxkind, seed, cls, observer):
    """src/game/region.cpp's AiHitReact, in the same terms."""
    if not hit:
        return POSE_SENTINEL, TURN_SENTINEL, hit

    pose = POSE_SENTINEL
    if kind == 7:
        pose = POSE_KIND7
    elif kind != 8 and ctxkind != 3:
        limit = limits[rank]
        if seed < (limit >> 1):
            pose = poses[cls * 2 + (1 if seed >= 0x80 else 0)] & 0xFFFFFFFF
        elif seed < limit:
            pose = POSE_HIT_HEAVY

    turn = TURN_SENTINEL if observer else hit
    return pose, turn, 0


def cases(limits):
    seeds = set([0, 0xFF, 0x7F, 0x80, 0x81])
    for t in limits:                       # straddle both boundaries of each
        for v in (t >> 1, t):
            seeds.update((v - 1, v, v + 1))
    seeds = sorted(s for s in seeds if 0 <= s <= 0xFF)

    for kind in (0, 7, 8, 9):
        for ctxkind in (0, 3):
            for rank in range(8):
                for seed in seeds:
                    for cls in (0, 1, 2):
                        yield 0x5A, kind, rank, ctxkind, seed, cls, 0
                        yield 0x5A, kind, rank, ctxkind, seed, cls, 1
    for kind in (0, 7):                    # the early return consumes nothing
        yield 0, kind, 0, 0, 0x40, 0, 0


def emit(rows, path):
    with open(path, "w") as fh:
        fh.write("/* Generated by tools/hitreactcheck.py -- do not edit.\n"
                 " *\n"
                 " * One row per case, with what the ORIGINAL at 0x00405050\n"
                 " * left in out+8, out+4 and OBJ_OFF_HIT_DIR.  The two out\n"
                 " * fields are seeded first, so a row carrying the sentinel\n"
                 " * means the original wrote NOTHING there -- which two of\n"
                 " * its arms really do.\n"
                 " */\n"
                 "typedef struct { int32_t hit; int32_t kind; int32_t rank;\n"
                 "                 int32_t ctxkind; int32_t seed; int32_t cls;\n"
                 "                 int32_t observer;\n"
                 "                 uint32_t pose; int32_t turn;\n"
                 "                 int32_t consumed; } AM2_HitReactVector;\n\n"
                 "static const AM2_HitReactVector am2_hitreact_vectors[] = {\n")
        for (hit, kind, rank, ctxkind, seed, cls, obs), (p, t, c) in rows:
            fh.write("  { %d, %d, %d, %d, %d, %d, %d, 0x%08Xu, %d, %d },\n"
                     % (hit, kind, rank, ctxkind, seed, cls, obs, p, t, c))
        fh.write("};\n")


def main():
    h = Harness()
    limits, poses = h.thresholds(), h.poses()
    rows, bad, n = [], 0, 0
    for args in cases(limits):
        got = h.run(*args)
        want = model(limits, poses, *args)
        rows.append((args, got))
        n += 1
        if got != want:
            bad += 1
            if bad <= 8:
                print("  hit %02x kind %d rank %d ctx %d seed %3d cls %d "
                      "obs %d: %s vs %s" % (args + tuple([got, want])[:0]
                                            + (got, want)))
    print("hitreactcheck: %d cases, %d differ  (thresholds %s)"
          % (n, bad, limits))
    if not bad and "--emit" in sys.argv:
        emit(rows, sys.argv[sys.argv.index("--emit") + 1])
        print("  recorded %d cases" % len(rows))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
