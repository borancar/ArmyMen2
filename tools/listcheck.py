#!/usr/bin/env python3
"""Check ListDropOldest against the original, over its whole input space.

ListDropOldest (0x004539A0) drops the OLDEST row of a widget list.  CLAUDE.md
records it as "the sharpest case of a function that cannot be driven rather
than merely has not been": its one caller is MenuMessage and it fires only
once the menu log passes a hundred lines, which no configuration in ab.sh
produces.  Its counter is blind besides.  So this is its verification or there
is none.

The function is small and its space is small, so it is enumerated: the row
count from 0 upward, and whether the list owns its values.  What is compared
is everything the call leaves behind -- the new count, the rows that survived
and their ORDER, the base pointer having been swapped, and which pointers were
handed to free.

THE ALLOCATOR IS THE WHOLE DIFFICULTY, as it was for scriptcheck: malloc
reaches HeapAlloc, which does not exist under emulation.  Here malloc is a
bump allocator driven from a memory cell -- a FRESH block per call, because a
fixed one would alias the array being copied out of and the copy would be
reading what it had just written -- and free is a bare `ret` with a hook that
records its argument, so "what was freed" becomes an observable rather than a
crash.

THREE THINGS THE CORPUS IS BUILT TO CATCH, each named in the reconstruction's
own comment as a thing reproduced rather than tidied: the count is decremented
FIRST, so the loop copies `count` rows starting at row 1; a list that owns its
values frees the dropped row's pointer and one that does not must not; and at
a count of zero the allocation STILL happens, with a size of zero, and its
pointer is stored.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

LIST_DROP_OLDEST = 0x004539A0
GAME_MALLOC = 0x004647F8
GAME_FREE = 0x004646A9

ROW_STRIDE = 0x104
ROW_VALUE = 0x100
OWNS_VALUES = 8

DATA, DATA_SZ = 0x63000000, 0x80000
LIST = DATA + 0x100
ROWS = DATA + 0x1000
HEAP = DATA + 0x20000
MALLOC_CELL = DATA + 0x10


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(GAME_MALLOC,
                     b"\xa1" + struct.pack("<I", MALLOC_CELL) + b"\xc3")
        uc.mem_write(GAME_FREE, b"\xc3")
        self.freed = []

        from unicorn import UC_HOOK_CODE, x86_const

        def on_free(uc_, address, size, _u):
            if address != GAME_FREE:
                return
            esp = uc_.reg_read(x86_const.UC_X86_REG_ESP)
            self.freed.append(
                struct.unpack("<I", uc_.mem_read(esp + 4, 4))[0])

        uc.hook_add(UC_HOOK_CODE, on_free)

    def run(self, count, owns):
        """Seed the list, call with `this` in ecx, read back what changed.

        Emu.call writes ESP and EBP and nothing else, so ecx set beforehand
        survives into the callee -- which is how a thiscall is reached without
        a second entry point in the harness.
        """
        from unicorn import x86_const
        uc = self.emu.uc

        uc.mem_write(DATA, b"\0" * 0x40)
        uc.mem_write(ROWS, b"\0" * (ROW_STRIDE * (count + 2)))
        for r in range(count):
            at = ROWS + r * ROW_STRIDE
            uc.mem_write(at, bytes([(r + 1) & 0xFF]) * ROW_VALUE)
            uc.mem_write(at + ROW_VALUE, struct.pack("<I", 0x100 + r))

        uc.mem_write(LIST, struct.pack("<i", count))
        uc.mem_write(LIST + 4, struct.pack("<I", ROWS))
        uc.mem_write(LIST + OWNS_VALUES, struct.pack("<i", owns))
        uc.mem_write(MALLOC_CELL, struct.pack("<I", HEAP))
        uc.mem_write(HEAP, b"\xEE" * (ROW_STRIDE * (count + 2)))

        self.freed = []
        uc.reg_write(x86_const.UC_X86_REG_ECX, LIST)
        eax, _ = self.emu.call(LIST_DROP_OLDEST, [], b"\0" * 64,
                               count=2000000)
        if eax is None:
            return None

        new_count = struct.unpack("<i", uc.mem_read(LIST, 4))[0]
        new_base = struct.unpack("<I", uc.mem_read(LIST + 4, 4))[0]
        rows = []
        for r in range(max(new_count, 0)):
            at = new_base + r * ROW_STRIDE
            rows.append((uc.mem_read(at, 1)[0],
                         struct.unpack("<I",
                                       uc.mem_read(at + ROW_VALUE, 4))[0]))
        return new_count, new_base == HEAP, rows, list(self.freed)


def model(count, owns):
    """What the reconstruction leaves behind."""
    new_count = count - 1
    freed = []
    # The owned pointer is freed only when it is NON-NULL, which the corpus
    # reaches because a count of zero leaves row 0 blank: the model was
    # written without that guard and the oracle caught it on the one case
    # that distinguishes them.
    if owns and count >= 1:
        freed.append(0x100 + 0)          # the dropped row's owned value
    rows = [((r + 2) & 0xFF, 0x100 + r + 1) for r in range(max(new_count, 0))]
    freed.append(ROWS)                   # the old array, freed after the copy
    return new_count, True, rows, freed


def main():
    h = Harness()
    cases = bad = 0

    for owns in (0, 1):
        for count in range(0, 7):
            got = h.run(count, owns)
            want = model(count, owns)
            cases += 1
            if got != want:
                bad += 1
                if bad <= 6:
                    print("  count %d owns %d\n    original %s\n    model    %s"
                          % (count, owns, got, want))

    print("listcheck: %d cases, %d differ" % (cases, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
