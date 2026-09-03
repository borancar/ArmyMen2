#!/usr/bin/env python3
"""Check AiStep's mode dispatch, guard and tail against the original.

AiStep (0x00407F80) is one frame of AI for one object: build a context, run
the arm its mode selects, then record which region the object stands in.
CLAUDE.md records the whole AI band as unreachable in this environment --
`counts Ai` returns every counter at 0 on a driven Boot Camp mission -- so
no drive can compare it and no A/B can fail on it.

WHY THIS FUNCTION AND NOT ONE OF ITS ARMS.  The arms reach the sighting
layer, pathfinding and the object model, none of which emulates outside the
game.  The DISPATCH does not: it is a bound, a jump table and a tail, and
this project's own rule says a family of cold functions is best attacked at
its dispatcher.  So the arms are stubbed and what is compared is which one
ran -- the question a jump table answers and a reading of the arms cannot.

WHAT MAKES IT WORTH A TOOL.  The mapping is not the order the arms are laid
out in.  Six distinct arms serve eight indices: modes 1, 4 and 5 share one,
and the `ja` above 7 sends every other value to that same arm, so it is the
default as well as three modes.  CLAUDE.md lists reading such a table off
the bodies as the commonest way this project gets a switch wrong, and lists
"slots sharing an arm" as its second failure mode.  Both are here at once.

THE TAIL IS PART OF THE ANSWER, NOT A SIDE EFFECT.  AiStep returns nothing;
its observable output is the region halfword it writes into the object.  A
check comparing a return value would pass with the whole body deleted, which
is the trap tools/firepose.py records.  So the region field is seeded with a
sentinel before every case and read back after -- "wrote nothing", which the
null guard really does, is distinguishable from "wrote zero".

WHAT IT DOES NOT COVER: everything inside the six arms, and the context
builder, all seven of which are stubbed.  They keep whatever standing they
had, which for now is reading.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

# What this tool actually verifies.  It is declared rather than inferred
# because the file below CONTAINS the addresses of six arms it only STUBS --
# an address search would read those as coverage, which is the same
# "a mention is not a test" mistake tools/checkclaims.py already strips
# docstrings to avoid, one level in: here the address is in CODE.
CHECKS = ("AiStep",)

AI_STEP = 0x00407F80
BUILD_CONTEXT = 0x00407D70

# The six arms, in the order the jump table reaches them rather than the
# order they are laid out in.  Read from the image at 0x0040803C.
ARMS = {
    "AiAttackBody": 0x00407710,
    "AiStepTrack": 0x00407560,
    "AiStepIgnore": 0x00407BF0,
    "AiStepFollow": 0x00407C80,
    "AiStepAttack": 0x00407BD0,
    "AiStepDefend": 0x00407640,
}

OBJ_OFF_TILE = 0x1A
OBJ_OFF_REGION = 0xDC
OBJ_OFF_AI_MODE = 0xE4

ADDR_REGION_OF_CELL = 0x00514ECC

DATA = 0x66000000
DATA_SZ = 0x00040000
OBJ = DATA + 0x1000
REGIONS = DATA + 0x2000        # the cell -> region byte array
MARK = DATA + 0x100            # which arm ran

SENTINEL = 0xBEEF              # what the region field holds before the call


def stub(mark_value):
    """mov dword [MARK], value; ret -- cdecl, and it clobbers no register."""
    return (b"\xc7\x05" + struct.pack("<I", MARK)
            + struct.pack("<I", mark_value) + b"\xc3")


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)

        self.ids = {}
        for n, (name, addr) in enumerate(sorted(ARMS.items()), start=1):
            self.ids[name] = n
            uc.mem_write(addr, stub(n))
        # The context builder writes 0x44 bytes of stack we never read.
        uc.mem_write(BUILD_CONTEXT, b"\xc3")

        uc.mem_write(ADDR_REGION_OF_CELL, struct.pack("<I", REGIONS))
        # A position-dependent region table, so a wrong tile index shows up
        # as a wrong region rather than as the same byte everywhere.
        uc.mem_write(REGIONS, bytes((i * 7 + 11) & 0xFF for i in range(0x10000)))

    def run(self, mode, tile, null=False):
        uc = self.emu.uc
        uc.mem_write(MARK, struct.pack("<I", 0))
        uc.mem_write(OBJ, b"\0" * 0x200)
        uc.mem_write(OBJ + OBJ_OFF_AI_MODE, struct.pack("<I", mode & 0xFFFFFFFF))
        uc.mem_write(OBJ + OBJ_OFF_TILE, struct.pack("<H", tile))
        uc.mem_write(OBJ + OBJ_OFF_REGION, struct.pack("<H", SENTINEL))

        self.emu.call(AI_STEP, [0 if null else OBJ, 0], b"\0" * 64,
                      count=200000)
        arm = struct.unpack("<I", bytes(uc.mem_read(MARK, 4)))[0]
        region = struct.unpack("<H", bytes(uc.mem_read(OBJ + OBJ_OFF_REGION,
                                                       2)))[0]
        return arm, region


def model(h, mode, tile, null):
    """What src/game/region.cpp's AiStep does, in the same terms."""
    if null:
        return 0, SENTINEL                      # returns before the context

    if mode == 0:
        arm = "AiAttackBody"
    elif mode == 2:
        arm = "AiStepIgnore"
    elif mode == 3:
        arm = "AiStepFollow"
    elif mode == 6:
        arm = "AiStepAttack"
    elif mode == 7:
        arm = "AiStepDefend"
    else:
        arm = "AiStepTrack"                     # 1, 4, 5 and every other value

    return h.ids[arm], (tile * 7 + 11) & 0xFF


def cases():
    modes = list(range(-2, 12)) + [0x7FFFFFFF, -0x80000000, 0xFFFF, 0x10000]
    for mode in modes:
        for tile in (0, 1, 0x100, 0x1234, 0xFFFF):
            yield mode, tile, False
    for tile in (0, 0x1234):
        yield 0, tile, True                     # the null guard


def main():
    h = Harness()
    n = bad = 0
    for mode, tile, null in cases():
        got = h.run(mode, tile, null)
        want = model(h, mode, tile, null)
        n += 1
        if got != want:
            bad += 1
            if bad <= 8:
                print("  mode %-12s tile 0x%04x%s: arm %s region 0x%x "
                      "vs arm %s region 0x%x"
                      % (mode, tile, " NULL" if null else "",
                         got[0], got[1], want[0], want[1]))
    print("aicheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
