#!/usr/bin/env python3
"""Check TakeNumberKey against the original, over every key combination.

TakeNumberKey (0x00413A80) latches the 1..8 keys into ADDR_NUMBER_KEY_SLOT as
0..7.  CLAUDE.md lists it among the functions no drive here reaches, and its
counter is blind besides.

CHECKS = ("TakeNumberKey",)

WHY IT IS WORTH CHECKING, being eight identical arms: our C is a LOOP where
the original is eight inlined copies, and that is a deliberate rewrite rather
than a transcription.  The claim it rests on is that the two are
behaviourally identical -- that the `return` after each store makes the arms
an if/else chain so the LOWEST key wins, and that `&&` keeps KeyChanged
behind IsKeyDown exactly as the original's second branch does.  Both halves
of that claim are checkable and neither is checked by reading it twice.

THE OUTPUT IS THE SLOT, NOT THE RETURN VALUE.  The original leaves whatever
the last failed test left in eax and our C returns void, so a comparison of
return values would be comparing the register allocator.  The slot is seeded
with a sentinel before every case, which is what makes "wrote nothing" -- the
no-match path, which really does fall through without storing -- a distinct
answer from "wrote zero".  Without that, a no-match and a key-1 press look
the same.

THE CORPUS IS EXHAUSTIVE IN THE DIMENSION THAT DECIDES THE ANSWER.  The
result depends on the first bit set in `down & changed`, so all 256 down
masks are tried against eight changed patterns -- including `changed = down`,
`changed = ~down` and the alternating ones -- which is what distinguishes an
AND from either test alone.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("TakeNumberKey",)

TAKE_NUMBER_KEY = 0x00413A80
ADDR_IS_KEY_DOWN = 0x00427450
ADDR_KEY_CHANGED = 0x00427470
ADDR_NUMBER_KEY_SLOT = 0x004FCF90

FIRST_DIK = 2          # DIK_1; the eight arms run 2..9
NKEYS = 8
SENTINEL = 0x5EED5EED


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        from unicorn import UC_HOOK_CODE
        uc.mem_write(ADDR_IS_KEY_DOWN, b"\xc3")
        uc.mem_write(ADDR_KEY_CHANGED, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._down,
                    begin=ADDR_IS_KEY_DOWN, end=ADDR_IS_KEY_DOWN)
        uc.hook_add(UC_HOOK_CODE, self._changed,
                    begin=ADDR_KEY_CHANGED, end=ADDR_KEY_CHANGED)
        self.down = 0
        self.changed = 0
        self.calls = 0

    def _answer(self, uc, mask):
        """Both are cdecl and take the scancode; the CALLER cleans up."""
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        ret, dik = struct.unpack("<2I", uc.mem_read(esp, 8))
        bit = dik - FIRST_DIK
        val = 0
        if 0 <= bit < NKEYS:
            val = 0x80 if (mask >> bit) & 1 else 0
        uc.reg_write(x86_const.UC_X86_REG_EAX, val)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def _down(self, uc, addr, size, user):
        self.calls += 1
        self._answer(uc, self.down)

    def _changed(self, uc, addr, size, user):
        self._answer(uc, self.changed)

    def run(self, down, changed):
        uc = self.emu.uc
        self.down, self.changed, self.calls = down, changed, 0
        uc.mem_write(ADDR_NUMBER_KEY_SLOT, struct.pack("<I", SENTINEL))
        self.emu.call(TAKE_NUMBER_KEY, [], b"\0" * 64, count=100000)
        slot = struct.unpack("<I", bytes(uc.mem_read(ADDR_NUMBER_KEY_SLOT,
                                                     4)))[0]
        return slot, self.calls


def model(down, changed):
    """src/game/misc.cpp's TakeNumberKey, in the same terms.

    The second value is how many times IsKeyDown is asked, which is what says
    the scan STOPS at the first match rather than running all eight."""
    for i in range(NKEYS):
        if (down >> i) & 1:
            if (changed >> i) & 1:
                return i, i + 1
    return SENTINEL, NKEYS


def cases():
    for down in range(256):
        for changed in (0x00, 0xFF, down, (~down) & 0xFF, 0x55, 0xAA,
                        0x01, 0x80):
            yield down, changed


def main():
    h = Harness()
    n = bad = 0
    for down, changed in cases():
        got = h.run(down, changed)
        want = model(down, changed)
        n += 1
        if got != want:
            bad += 1
            if bad <= 8:
                print("  down=%02X changed=%02X -> slot=%08X calls=%d, "
                      "want slot=%08X calls=%d"
                      % (down, changed, got[0], got[1], want[0], want[1]))
    print("numberkeycheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
