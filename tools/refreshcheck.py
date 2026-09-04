#!/usr/bin/env python3
"""Check RefreshScreen against the original: the flag, the double draw, the Blt.

RefreshScreen (0x0044D6D0) forces the screen out of band.  CLAUDE.md lists it
among the functions no drive here reaches, and says why: six of its seven
callers are the in-mission dialog openers and every one of them needs
ADDR_GAME_STATE to be 2, so opening AUDIO from the title screen does not
reach it; the seventh needs an alt-tab.

CHECKS = ("RefreshScreen",)

FOUR THINGS TO GET WRONG, and the corpus reaches each:

  - the present flag is SAVED and RESTORED, not cleared -- presenting may
    already have been off, and a version that sets it back to 1 turns it on
    for a caller that had turned it off
  - RefreshDraw is called TWICE, which a reading naturally collapses to once;
    the scene is double buffered and one pass leaves the other buffer holding
    whatever was there
  - the gate is called with 0 and then with 1, in that order, around the lot
  - and the blit is BltFast -- vtable slot 0x1C -- with six arguments, of
    which the destination coordinates come from the screen RECT and the
    source rectangle from the screen CLIP: two different globals sixteen
    bytes apart, which is exactly the kind of pair this file warns about.

The surface is a fake object with a fake vtable, so the call through slot
0x1C lands on a stub that records what it was handed.  That is the only way
to check the argument order of a COM call: nothing else in the process knows
what a BltFast was asked to do.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("RefreshScreen",)

REFRESH_SCREEN = 0x0044D6D0
REFRESH_GATE = 0x00412DE0
REFRESH_DRAW = 0x00424BF0

ADDR_PRESENT_ENABLED = 0x004FA030
ADDR_BACK_BUFFER = 0x004FE08C
ADDR_PRIMARY_SURFACE = 0x00502AD4
ADDR_SCREEN_CLIP = 0x00485310
ADDR_SCREEN_RECT_LEFT = 0x00485330
ADDR_SCREEN_RECT_TOP = 0x00485334

DDBLTFAST_WAIT = 0x10
BLTFAST_SLOT = 0x1C

DATA = 0x71000000
DATA_SZ = 0x10000
PRIMARY = DATA + 0x1000
VTABLE = DATA + 0x2000
BACKBUF = DATA + 0x3000
BLTSTUB = DATA + 0x4000

LEFT = 0x0000002A
TOP = 0x00000037


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(PRIMARY, struct.pack("<I", VTABLE))
        uc.mem_write(VTABLE + BLTFAST_SLOT, struct.pack("<I", BLTSTUB))
        uc.mem_write(BLTSTUB, b"\xc3")

        from unicorn import UC_HOOK_CODE
        for a in (REFRESH_GATE, REFRESH_DRAW):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._gate, begin=REFRESH_GATE,
                    end=REFRESH_GATE)
        uc.hook_add(UC_HOOK_CODE, self._draw, begin=REFRESH_DRAW,
                    end=REFRESH_DRAW)
        uc.hook_add(UC_HOOK_CODE, self._blt, begin=BLTSTUB, end=BLTSTUB)
        self.trace = []

    def _take(self, uc, n):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        vals = struct.unpack("<%dI" % (n + 1), uc.mem_read(esp, 4 * (n + 1)))
        uc.reg_write(x86_const.UC_X86_REG_EAX, 0)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, vals[0])
        return vals[1:]

    def _gate(self, uc, a, s, u):
        args = self._take(uc, 1)
        self.trace.append(("gate", args[0]))

    def _draw(self, uc, a, s, u):
        self._take(uc, 0)
        self.trace.append(("draw",))

    def _blt(self, uc, a, s, u):
        # stdcall under CINTERFACE: this, dx, dy, src, srect, flags
        args = self._take(uc, 6)
        self.trace.append(("bltfast",) + tuple(args))

    def run(self, enabled):
        uc = self.emu.uc
        self.trace = []
        uc.mem_write(ADDR_PRESENT_ENABLED, struct.pack("<i", enabled))
        uc.mem_write(ADDR_PRIMARY_SURFACE, struct.pack("<I", PRIMARY))
        uc.mem_write(ADDR_BACK_BUFFER, struct.pack("<I", BACKBUF))
        uc.mem_write(ADDR_SCREEN_RECT_LEFT, struct.pack("<I", LEFT))
        uc.mem_write(ADDR_SCREEN_RECT_TOP, struct.pack("<I", TOP))
        self.emu.call(REFRESH_SCREEN, [], b"\0" * 64, count=100000)
        return (struct.unpack("<i", bytes(uc.mem_read(ADDR_PRESENT_ENABLED, 4)))[0],
                tuple(self.trace))


def model(enabled):
    """src/game/win32/surface.cpp's RefreshScreen, in the same terms."""
    return (enabled, (
        ("gate", 0),
        ("draw",),
        ("draw",),
        ("bltfast", PRIMARY, LEFT, TOP, BACKBUF, ADDR_SCREEN_CLIP,
         DDBLTFAST_WAIT),
        ("gate", 1),
    ))


def cases():
    return (0, 1, -1, 0x7FFFFFFF)


def main():
    h = Harness()
    n = bad = 0
    for enabled in cases():
        got, want = h.run(enabled), model(enabled)
        n += 1
        if got != want:
            bad += 1
            print("  enabled=%d ->\n     %s\n want %s" % (enabled, got, want))
    print("refreshcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
