#!/usr/bin/env python3
"""Check StateLeave against the original, including its RE-READ.

StateLeave (0x0042E720) tears the movie state down: forget the movie, stop
and delete it, clear the slot, and blank the primary surface.  CLAUDE.md
lists it among the functions no drive here reaches.

CHECKS = ("StateLeave",)

THE RE-READ IS WHY IT NEEDS AN ORACLE.  The slot at ADDR_STATE_MOVIE is read
to enter the block, MovieForget is called, and then the slot is READ AGAIN
before the stop and the delete -- `mov esi, [0x515F98]` after the call, not a
register kept across it.  Our C reproduces that with a second load, which
looks redundant until MovieForget is allowed to clear the slot: then the
original skips the stop and the delete, and a version that kept the pointer
in a register would stop and delete a movie that has already been forgotten.

Nothing in this environment makes MovieForget clear it, so the difference is
unreachable by any drive and invisible to a reading that does not ask what
the callee may do.  The corpus makes the stub clear it in half the cases,
which is the only way to tell the two readings apart.

WHAT ELSE IS COMPARED: the order of the four calls, the argument each is
handed -- the delete must receive the movie, not the slot -- and that the
surface is cleared on BOTH paths, since the clear sits outside the block.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("StateLeave",)

STATE_LEAVE = 0x0042E720
MOVIE_FORGET = 0x00445670
MOVIE_STOP = 0x00445120
ADDR_DELETE = 0x004648F5
CLEAR_SURFACE = 0x0041AD30
ADDR_STATE_MOVIE = 0x00515F98
ADDR_PRIMARY_SURFACE = 0x00502AD4

DATA = 0x70000000
DATA_SZ = 0x10000
MOVIE = DATA + 0x1000
SURFACE = DATA + 0x2000


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        for a in (MOVIE_FORGET, MOVIE_STOP, ADDR_DELETE, CLEAR_SURFACE):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._forget,
                    begin=MOVIE_FORGET, end=MOVIE_FORGET)
        uc.hook_add(UC_HOOK_CODE, self._stop, begin=MOVIE_STOP, end=MOVIE_STOP)
        uc.hook_add(UC_HOOK_CODE, self._delete,
                    begin=ADDR_DELETE, end=ADDR_DELETE)
        uc.hook_add(UC_HOOK_CODE, self._clear,
                    begin=CLEAR_SURFACE, end=CLEAR_SURFACE)
        self.trace = []
        self.forget_clears = 0

    def _ret(self, uc, nargs=0):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        vals = struct.unpack("<%dI" % (nargs + 1),
                             uc.mem_read(esp, 4 * (nargs + 1)))
        uc.reg_write(x86_const.UC_X86_REG_EAX, 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, vals[0])
        return vals[1:]

    def _forget(self, uc, a, s, u):
        self.trace.append(("forget",))
        if self.forget_clears:
            uc.mem_write(ADDR_STATE_MOVIE, struct.pack("<I", 0))
        self._ret(uc)

    def _stop(self, uc, a, s, u):
        from unicorn import x86_const
        # thiscall: the movie arrives in ecx
        self.trace.append(("stop", uc.reg_read(x86_const.UC_X86_REG_ECX)))
        self._ret(uc)

    def _delete(self, uc, a, s, u):
        args = self._ret(uc, 1)
        self.trace.append(("delete", args[0]))

    def _clear(self, uc, a, s, u):
        args = self._ret(uc, 2)
        self.trace.append(("clear", args[0], args[1]))

    def run(self, movie, forget_clears):
        uc = self.emu.uc
        self.trace = []
        self.forget_clears = forget_clears
        uc.mem_write(ADDR_STATE_MOVIE, struct.pack("<I", movie))
        uc.mem_write(ADDR_PRIMARY_SURFACE, struct.pack("<I", SURFACE))
        self.emu.call(STATE_LEAVE, [], b"\0" * 64, count=100000)
        return (struct.unpack("<I", bytes(uc.mem_read(ADDR_STATE_MOVIE, 4)))[0],
                tuple(self.trace))


def model(movie, forget_clears):
    """src/game/win32/movie.cpp's StateLeave, in the same terms."""
    trace = []
    slot = movie
    if slot:
        trace.append(("forget",))
        if forget_clears:
            slot = 0
        if slot:                       # the RE-READ
            trace.append(("stop", slot))
            trace.append(("delete", slot))
        slot = 0
    trace.append(("clear", SURFACE, 0))
    return (slot, tuple(trace))


def cases():
    for movie in (0, MOVIE):
        for forget_clears in (0, 1):
            yield movie, forget_clears


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got, want = h.run(*args), model(*args)
        n += 1
        if got != want:
            bad += 1
            print("  movie=%08X forget_clears=%d ->\n     %s\n want %s"
                  % (args[0], args[1], got, want))
    print("stateleavecheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
