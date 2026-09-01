"""Check CreateExplosion's thirty-arm kind table against the original.

The function's whole decision is a two-level jump table -- a byte index at
0x00422B70 selecting one of five arms at 0x00422B5C -- over kinds 0x78..0x95,
and each arm writes a different blast rect, a different BLAST_OFF_MODE and a
different secondary kind-7 object. That is 30 cases, so it can be ENUMERATED
rather than sampled, which is the argument tools/moviecheck.py and
tools/posecheck.py already make.

It matters because NO A/B CAN SEE IT. Nothing in the suite reaches combat, so
the function is cold on every configuration; and even in a live mission a wrong
arm changes a blast radius and a sound, not a log line. A single wrong byte in
that 30-entry index table is exactly what this catches.

Everything below the constructor is HOOKED rather than run: the CRT's malloc
reaches HeapAlloc, which does not exist under emulation, and the five game
callees read map globals that are .bss zeros here. The hooks record their
arguments, so MakeKind7's are compared too -- which is the half of each arm
that the object's own fields do not carry.

    tools/explcheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn import UC_HOOK_CODE, UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
from vectors import Emu, SCRATCH

ADDR = 0x00422860

MALLOC       = 0x004647F8
MAKE_KIND7   = 0x00435550
OBJ_INIT     = 0x00429940
TILE_OF_POINT= 0x0042B290
BUILD_ROWSET = 0x00434DA0
HIDE_ROWS    = 0x00434E60
SET_ANIM     = 0x0040A1A0

TILE_ATTRS     = 0x00514EBC
GAME_CLOCK_MS  = 0x00511E04
EXPLOSION_ANIMS= 0x00510228

AREA_16 = 0x004788F0
AREA_24 = 0x00478900
AREA_32 = 0x00478910

HEAP, HEAP_SZ = 0x50000000, 0x100000

KIND_FIRST, KIND_LAST = 0x78, 0x95

BLAST_DAMAGE = 0x98
FIELD_94     = 0x94
BLAST_RECT   = 0x9C
BLAST_DUE    = 0xAC
BLAST_SOUND  = 0xB0
BLAST_MODE   = 0xB4
BLAST_SRC    = 0xB8
OBJ_ARMY     = 0x10
OBJ_HEIGHT   = 0x65
OBJ_ROWS     = 0x74

# What item.cpp's CreateExplosion does, as a model of the arms alone.
ARM_16 = (0x78, 0x82, 0x8C, 0x94)
ARM_24 = (0x81, 0x95)
ARM_32 = (0x83, 0x8A, 0x8B)
ARM_SCATTER = (0x7B,)


def model_arm(kind, x, facing):
    """(mode, area base, sound pending, MakeKind7 args or None)."""
    if kind in ARM_16:
        return 5, AREA_16, 1, (4, 0, facing)
    if kind in ARM_24:
        return 7, AREA_24, 1, (4, 0, facing)
    if kind in ARM_32:
        return 9, AREA_32, 1, (4, 0, facing)
    if kind in ARM_SCATTER:
        hit = (x & 0x0B) == 1
        return 0, AREA_16, 0, ((1, 0, facing) if hit else None)
    return 0, AREA_16, 0, None


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(HEAP, HEAP_SZ, UC_PROT_ALL)
        self.next = HEAP + 0x10
        self.kind7 = []
        # TileOfPoint is hooked to 0, so this only has to be readable.
        uc.mem_write(TILE_ATTRS, struct.pack("<I", SCRATCH + 0x4000))
        uc.mem_write(GAME_CLOCK_MS, struct.pack("<i", 1000))
        uc.hook_add(UC_HOOK_CODE, self._hook)

    def _ret(self, uc, eax, argc):
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack("<I", bytes(uc.mem_read(esp, 4)))[0]
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EIP, ret)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)   # cdecl: the caller cleans

    def _args(self, uc, n):
        esp = uc.reg_read(UC_X86_REG_ESP)
        return struct.unpack("<%dI" % n, bytes(uc.mem_read(esp + 4, 4 * n)))

    def _hook(self, uc, addr, size, _user):
        if addr == MALLOC:
            n = self._args(uc, 1)[0]
            p = self.next
            self.next += (n + 15) & ~15
            self._ret(uc, p, 1)
        elif addr == MAKE_KIND7:
            a = self._args(uc, 6)
            self.kind7.append(a)
            self._ret(uc, 0, 6)
        elif addr == BUILD_ROWSET:
            # Leave a row buffer where the caller will look for it: the set is
            # obj + 0x6C and the rows pointer is set + 8, which is obj + 0x74.
            st = self._args(uc, 6)[0]
            uc.mem_write(st + 8, struct.pack("<I", SCRATCH + 0x2000))
            self._ret(uc, 0, 6)
        elif addr in (OBJ_INIT, TILE_OF_POINT, HIDE_ROWS, SET_ANIM):
            self._ret(uc, 0, 0)

    def run(self, x, y, kind, army, src, damage, delay, unused, uid, facing):
        self.kind7 = []
        eax, _ = self.emu.call(ADDR, [x, y, kind, army, src, damage, delay,
                                      unused, uid, facing], b"", count=200000)
        if eax is None:
            return None
        o = eax
        rd = lambda off, n=4: struct.unpack(
            "<i" if n == 4 else "<h", bytes(self.emu.uc.mem_read(o + off, n)))[0]
        rows = struct.unpack("<I", bytes(self.emu.uc.mem_read(o + OBJ_ROWS, 4)))[0]
        return {
            "kind":   rd(FIELD_94),
            "damage": rd(BLAST_DAMAGE),
            "rect":   struct.unpack("<4i",
                                    bytes(self.emu.uc.mem_read(o + BLAST_RECT, 16))),
            "due":    rd(BLAST_DUE),
            "sound":  rd(BLAST_SOUND),
            "mode":   rd(BLAST_MODE),
            "src":    rd(BLAST_SRC) & 0xFFFFFFFF,
            "army":   struct.unpack("<b", bytes(self.emu.uc.mem_read(o + OBJ_ARMY, 1)))[0],
            "anim":   struct.unpack("<I",
                                    bytes(self.emu.uc.mem_read(rows + 0x40, 4)))[0],
            "depth":  struct.unpack("<h",
                                    bytes(self.emu.uc.mem_read(rows + 0x26, 2)))[0],
            "kind7":  list(self.kind7),
        }


def expected(h, x, y, kind, army, src, damage, delay, facing):
    mode, area, sound, k7 = model_arm(kind, x, facing)
    rect = list(struct.unpack("<4i", bytes(h.emu.uc.mem_read(area, 16))))
    rect[0] += x
    rect[2] += x
    rect[1] += y
    rect[3] += y
    if kind == 0x85:
        depth = 999
    elif kind == 0x86:
        depth = 1001
    else:
        depth = 10000
    return {
        "kind": kind, "damage": damage, "rect": tuple(rect),
        "due": (1000 + delay) if delay > 0 else 0,
        "sound": sound, "mode": mode, "src": src & 0xFFFFFFFF,
        "army": army, "anim": EXPLOSION_ANIMS, "depth": depth,
        "k7": k7,
    }


def main():
    h = Harness()
    bad = 0
    cases = 0

    for kind in list(range(KIND_FIRST, KIND_LAST + 1)) + [0x77, 0x96, 0, 0xFF]:
        for x, y in ((100, 200), (101, 200), (0, 0), (1, 3)):
            for delay in (0, 250):
                cases += 1
                got = h.run(x, y, kind, 2, 0xABCD, 7, delay, 0, 0x1234, 3)
                if got is None:
                    print("kind %#x at (%d,%d): the original faulted"
                          % (kind, x, y))
                    bad += 1
                    continue

                want = expected(h, x, y, kind, 2, 0xABCD, 7, delay, 3)

                # The 0x85 pair makes TWO objects; only the outer one's fields
                # come back, and its own kind-7 calls are the tail of the list.
                k7 = got["kind7"][-1] if got["kind7"] else None
                k7 = (k7[1], k7[2], k7[3]) if k7 else None
                if kind == 0x85 and got["kind7"]:
                    k7 = None if want["k7"] is None else k7

                for key in ("kind", "damage", "rect", "due", "sound", "mode",
                            "src", "army", "anim", "depth"):
                    if got[key] != want[key]:
                        bad += 1
                        print("kind %#04x (%d,%d) delay=%d: %s original %r "
                              "ours %r" % (kind, x, y, delay, key,
                                           got[key], want[key]))
                if k7 != want["k7"]:
                    bad += 1
                    print("kind %#04x (%d,%d): MakeKind7 original %r ours %r"
                          % (kind, x, y, k7, want["k7"]))

    if bad:
        print("explcheck: %d disagreement(s) over %d cases" % (bad, cases))
        return 1
    print("explcheck: %d cases over %d kinds, all identical"
          % (cases, KIND_LAST - KIND_FIRST + 1 + 4))
    return 0


if __name__ == "__main__":
    sys.exit(main())
