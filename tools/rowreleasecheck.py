#!/usr/bin/env python3
"""Check RowRelease against the original: the guard, the order, the clears.

RowRelease (0x0041D3A0) gives a row's cell registrations back and frees the
buffer it owns.  CLAUDE.md lists it among the functions no drive here
reaches, and its counter is blind besides.

CHECKS = ("RowRelease",)

It is forty-eight bytes and there are four things in it that can be wrong,
none of which a reading settles:

  - the guard is ROW_OFF_OWNS, a BYTE, and a row that does not own its buffer
    must be left entirely alone -- no unregister, no free, no clears
  - the unregister comes BEFORE the free, which is the order that matters:
    the other way round hands the map a buffer that has just been released
  - the pointer freed is the one at ROW_OFF_BUFFER, not the row
  - and BOTH fields are cleared afterwards, so a second call is a no-op
    rather than a double free

THE FREE'S ARGUMENT IS THE POINT.  A double free or a free of the wrong
pointer is exactly the class no A/B can see -- CLAUDE.md's note that "a free
is the weakest possible toucher" is about naming, and this is the same fact
from the other side: the only way to check what was freed is to record it.
So free is hooked and its argument compared, and the order of the two calls
is compared with them.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("RowRelease",)

ROW_RELEASE = 0x0041D3A0
ROW_UNREGISTER_ALL = 0x0041DB20
ADDR_FREE = 0x004646A9

ROW_OFF_OWNS = 0x34
ROW_OFF_BUFFER = 0x38

DATA = 0x6F000000
DATA_SZ = 0x10000
ROW = DATA + 0x1000
DESC = DATA + 0x2000
BUFFER = DATA + 0x3000


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        for a in (ROW_UNREGISTER_ALL, ADDR_FREE):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._unreg,
                    begin=ROW_UNREGISTER_ALL, end=ROW_UNREGISTER_ALL)
        uc.hook_add(UC_HOOK_CODE, self._free, begin=ADDR_FREE, end=ADDR_FREE)
        self.trace = []

    def _args(self, uc, n):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        vals = struct.unpack("<%dI" % (n + 1), uc.mem_read(esp, 4 * (n + 1)))
        return esp, vals[0], vals[1:]

    def _ret(self, uc, esp, ret):
        from unicorn import x86_const
        uc.reg_write(x86_const.UC_X86_REG_EAX, 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def _unreg(self, uc, a, s, u):
        esp, ret, args = self._args(uc, 2)
        self.trace.append(("unreg", args[0], args[1]))
        self._ret(uc, esp, ret)

    def _free(self, uc, a, s, u):
        esp, ret, args = self._args(uc, 1)
        self.trace.append(("free", args[0]))
        self._ret(uc, esp, ret)

    def run(self, owns, buf):
        uc = self.emu.uc
        uc.mem_write(ROW, b"\0" * 0x60)
        self.trace = []
        uc.mem_write(ROW + ROW_OFF_OWNS, bytes([owns]))
        uc.mem_write(ROW + ROW_OFF_BUFFER, struct.pack("<I", buf))
        self.emu.call(ROW_RELEASE, [ROW, DESC], b"\0" * 64, count=100000)
        return (bytes(uc.mem_read(ROW + ROW_OFF_OWNS, 1))[0],
                struct.unpack("<I", bytes(uc.mem_read(ROW + ROW_OFF_BUFFER, 4)))[0],
                tuple(self.trace))


def model(owns, buf):
    """src/game/item.cpp's RowRelease, in the same terms."""
    if not owns:
        return (owns, buf, ())
    return (0, 0, (("unreg", ROW, DESC), ("free", buf)))


def cases():
    for owns in (0, 1, 0x80, 0xFF):
        for buf in (0, BUFFER, 0xFFFFFFFF):
            yield owns, buf


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got, want = h.run(*args), model(*args)
        n += 1
        if got != want:
            bad += 1
            print("  owns=%02X buf=%08X ->\n     %s\n want %s"
                  % (args[0], args[1], got, want))
    print("rowreleasecheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
