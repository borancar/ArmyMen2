#!/usr/bin/env python3
"""Check CheckSaveTag against the original, including the short-read case.

CheckSaveTag (0x004235D0) reads four bytes from a savegame and compares them
against the tag the caller expected, logging the caller's file and line when
they differ.  CLAUDE.md lists it among the functions no drive here reaches.

CHECKS = ("CheckSaveTag",)

WHY IT NEEDS AN ORACLE AT ALL, being nine lines of C: the destination of the
read IS THE FUNCTION'S OWN FIRST ARGUMENT SLOT.  `lea ecx, [esp+4]` takes the
address of the `FILE *` and hands it to fread, so the pointer is overwritten
by the bytes read and the comparison is against what landed there.  Our C
reproduces that by initialising the local FROM fp, which looks like a
pointless cast until you ask what happens when fread returns SHORT: the
untouched bytes still hold the old pointer, and the tag compared is a mixture
of the file's bytes and the caller's stack.

That case cannot be produced by any drive -- a savegame either has four bytes
or the read fails -- so it is verified here or nowhere.  It is also the only
reason the initialisation is not dead code.

THE ORACLE FORCES IT.  fread is hooked in Python rather than stubbed in
assembly, because the destination is a stack address not known until the call
happens; the hook writes exactly `n` of the four bytes and answers `n / 4` as
fread does.  With n = 4 the tag is the file's; with n < 4 the top bytes are
whatever the fp argument was, which is what makes the two readings differ.

WHAT IT COMPARES: the return value, whether the logger fired, and the two
arguments it was handed -- the caller's file pointer and line -- because a
mismatch that logs the wrong line is a defect no return value shows.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("CheckSaveTag",)

CHECK_SAVE_TAG = 0x004235D0
ADDR_FREAD = 0x004645C1
ADDR_LOG = 0x0045CAA0

DATA = 0x69000000
DATA_SZ = 0x10000
FP = DATA + 0x40           # a stand-in FILE *, and the residue on a short read
FILEBUF = DATA + 0x100     # the four bytes "in the file"
NAME = DATA + 0x200        # the caller's source-file string


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(NAME, b"loadgame.cpp\0")

        from unicorn import UC_HOOK_CODE
        uc.mem_write(ADDR_FREAD, b"\xc3")
        uc.mem_write(ADDR_LOG, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._fread, begin=ADDR_FREAD,
                    end=ADDR_FREAD)
        uc.hook_add(UC_HOOK_CODE, self._log, begin=ADDR_LOG, end=ADDR_LOG)
        self.nread = 4
        self.logged = None

    def _fread(self, uc, addr, size, user):
        """fread(dst, size, count, fp) -- cdecl, the CALLER cleans up."""
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        ret, dst, sz, cnt = struct.unpack("<4I", uc.mem_read(esp, 16))
        data = bytes(uc.mem_read(FILEBUF, 4))[:self.nread]
        if data:
            uc.mem_write(dst, data)
        uc.reg_write(x86_const.UC_X86_REG_EAX,
                     self.nread // sz if sz else 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def _log(self, uc, addr, size, user):
        """The logger, cdecl and varargs: record what it was handed."""
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        ret, fmt, a1, a2 = struct.unpack("<4I", uc.mem_read(esp, 16))
        self.logged = (fmt, a1, a2)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def run(self, filebytes, expected, line, nread):
        uc = self.emu.uc
        uc.mem_write(FILEBUF, struct.pack("<I", filebytes))
        self.nread = nread
        self.logged = None
        eax, _ = self.emu.call(CHECK_SAVE_TAG, [FP, expected, NAME, line],
                               b"\0" * 64, count=100000)
        return eax, self.logged


def model(filebytes, expected, line, nread):
    """src/game/savetag.cpp's CheckSaveTag, in the same terms.

    The local starts as the fp value, and only `nread` of its bytes are
    replaced -- which is the whole point of the initialisation."""
    tag = FP & 0xFFFFFFFF
    raw = struct.pack("<I", tag)
    new = struct.pack("<I", filebytes)
    tag = struct.unpack("<I", new[:nread] + raw[nread:])[0]

    if tag == expected:
        return 1, None
    return 0, ("fmt", NAME, line)


def cases():
    for nread in (4, 3, 2, 1, 0):
        for filebytes in (0x00000000, 0xFFFFFFFF, 0x12345678, FP & 0xFFFFFFFF,
                          0xDEADBEEF):
            for expected in (0x12345678, 0x00000000, FP & 0xFFFFFFFF,
                             0xDEADBEEF):
                for line in (1, 4242):
                    yield filebytes, expected, line, nread


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got_rc, got_log = h.run(*args)
        want_rc, want_log = model(*args)
        n += 1

        ok = (got_rc == want_rc) and ((got_log is None) == (want_log is None))
        if ok and got_log is not None:
            ok = (got_log[1], got_log[2]) == (want_log[1], want_log[2])
        if not ok:
            bad += 1
            if bad <= 8:
                print("  file=%08X expected=%08X line=%d nread=%d -> "
                      "rc=%s log=%s, want rc=%s log=%s"
                      % (args[0], args[1], args[2], args[3], got_rc, got_log,
                         want_rc, want_log))

    print("savetagcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
