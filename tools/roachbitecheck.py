#!/usr/bin/env python3
"""Check RoachBite against the original: the reach, the box, the damage loop.

RoachBite (0x0043D330) is what a roach does when it reaches something: pick a
point ahead of itself, play a bite there, and damage every object in a box
around it that is not an ally.

CHECKS = ("RoachBite",)

IT IS COLD FOR A REASON THAT IS MEASURED, not assumed.  CLAUDE.md records a
live MAP 01 run with nine roaches alive -- CreateRoach=9, RoachMaskWeight past
half a million -- leaving this at 0 after two minutes: the roaches run and
simply never attack anything, because nothing walks into one.  A longer wait
was tried.  So it is verified here or nowhere.

THE TRIG TABLES ARE SEEDED, which is the whole reason this can be checked at
all.  Cos8 and Sin8 are `table[h & 0xFF]` over two float[256] that
BuildTrigTables fills at startup; in the file they are .bss zeros, so an
unseeded run would compute the same bite point for every facing and the
geometry would go unchecked.  tools/shakecheck.py records exactly this trap
one subsystem over -- its first version passed because the state it read was
all zeros.

WHAT IS COMPARED: the point handed to PlaySoundAt, the RECT handed to
AllObjectsInRect, every DamageObject call with all six of its arguments, and
that OBJ_OFF_DEADLINE_58 is cleared.  The chain is walked through
OBJ_OFF_QUERY_NEXT, so the corpus hands back chains of one, two and three
objects with the allied test answering differently along them -- an ally in
the middle must be skipped without stopping the walk.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("RoachBite",)

ROACH_BITE = 0x0043D330
PLAY_SOUND_AT = 0x0040C040
ALL_OBJECTS_IN_RECT = 0x0042A3D0
OBJS_ARE_ALLIED = 0x004574D0
DAMAGE_OBJECT = 0x00428140

ADDR_TRIG_COS = 0x00515784
ADDR_TRIG_SIN = 0x00514F80
ADDR_ROACH_REACH = 0x0046FAA8
ADDR_ROACH_BITE_BOX = 0x00487BD8
ADDR_ROACH_DAMAGE = 0x00487BB4

OBJ_OFF_POS = 0x12
OBJ_OFF_FACING = 0x40
OBJ_OFF_DEADLINE_58 = 0x58
OBJ_OFF_QUERY_NEXT = 0x68

BITE_SOUND = 0x31
DAMAGE_KIND = 5

DATA = 0x73000000
DATA_SZ = 0x10000
ROACH = DATA + 0x1000
VICTIMS = DATA + 0x2000      # three of them, 0x100 apart


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)

        # Seeded, position-dependent, and small enough that reach * value
        # stays inside an int16 for every facing.
        cos = b"".join(struct.pack("<f", ((i % 17) - 8) / 4.0)
                       for i in range(256))
        sin = b"".join(struct.pack("<f", ((i % 13) - 6) / 4.0)
                       for i in range(256))
        uc.mem_write(ADDR_TRIG_COS, cos)
        uc.mem_write(ADDR_TRIG_SIN, sin)

        self.reach = struct.unpack("<f", bytes(uc.mem_read(ADDR_ROACH_REACH, 4)))[0]
        self.box = struct.unpack("<4i", bytes(uc.mem_read(ADDR_ROACH_BITE_BOX, 16)))
        self.damage = struct.unpack("<i", bytes(uc.mem_read(ADDR_ROACH_DAMAGE, 4)))[0]
        self.cos = struct.unpack("<256f", cos)
        self.sin = struct.unpack("<256f", sin)

        from unicorn import UC_HOOK_CODE
        for a in (PLAY_SOUND_AT, ALL_OBJECTS_IN_RECT, OBJS_ARE_ALLIED,
                  DAMAGE_OBJECT):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._sound, begin=PLAY_SOUND_AT,
                    end=PLAY_SOUND_AT)
        uc.hook_add(UC_HOOK_CODE, self._query, begin=ALL_OBJECTS_IN_RECT,
                    end=ALL_OBJECTS_IN_RECT)
        self.uc = uc
        uc.hook_add(UC_HOOK_CODE, self._allied, begin=OBJS_ARE_ALLIED,
                    end=OBJS_ARE_ALLIED)
        uc.hook_add(UC_HOOK_CODE, self._damage, begin=DAMAGE_OBJECT,
                    end=DAMAGE_OBJECT)
        self.trace = []
        self.chain = []
        self.allied = ()

    def _take(self, uc, n):
        from unicorn import x86_const
        esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
        vals = struct.unpack("<%dI" % (n + 1), uc.mem_read(esp, 4 * (n + 1)))
        return esp, vals[0], vals[1:]

    def _done(self, uc, esp, ret, val=0):
        from unicorn import x86_const
        uc.reg_write(x86_const.UC_X86_REG_EAX, val & 0xFFFFFFFF)
        uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)
        uc.reg_write(x86_const.UC_X86_REG_EIP, ret)

    def _sound(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 5)
        self.trace.append(("sound", args[0], s16(args[3]), s16(args[4])))
        self._done(uc, esp, ret)

    def _query(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 2)
        rect = struct.unpack("<4i", uc.mem_read(args[0], 16))
        self.trace.append(("rect",) + rect)
        self._done(uc, esp, ret, self.chain[0] if self.chain else 0)

    def _allied(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 3)
        i = self.chain.index(args[1]) if args[1] in self.chain else 0
        self._done(uc, esp, ret, self.allied[i] if i < len(self.allied) else 0)

    def _damage(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 6)
        # THE FIFTH ARGUMENT'S UPPER BYTES ARE UNINITIALISED STACK. The
        # original computes `facing + 0x80` into AL, stores it as a BYTE into
        # a frame slot, and then pushes that slot as a DWORD -- so the top
        # three bytes are whatever the frame held. Only the low byte is the
        # value, and our C passes it zero-extended. Comparing all 32 bits
        # would be comparing leftovers, which is the rule CLAUDE.md already
        # states for the widget field the constructor never writes: an
        # uninitialised field cannot be part of an exact oracle.
        args = list(args)
        args[4] &= 0xFF
        self.trace.append(("damage",) + tuple(args))
        self._done(uc, esp, ret)

    def run(self, x, y, facing, nvictims, allied):
        uc = self.uc
        uc.mem_write(ROACH, b"\0" * 0x100)
        self.trace = []
        self.allied = allied
        self.chain = [VICTIMS + i * 0x100 for i in range(nvictims)]

        for i, v in enumerate(self.chain):
            uc.mem_write(v, b"\0" * 0x100)
            nxt = self.chain[i + 1] if i + 1 < len(self.chain) else 0
            uc.mem_write(v + OBJ_OFF_QUERY_NEXT, struct.pack("<I", nxt))

        uc.mem_write(ROACH + OBJ_OFF_POS, struct.pack("<2h", x, y))
        uc.mem_write(ROACH + OBJ_OFF_FACING, bytes([facing]))
        uc.mem_write(ROACH + 4, struct.pack("<I", 0xABCD1234))
        uc.mem_write(ROACH + OBJ_OFF_DEADLINE_58, struct.pack("<I", 0x77777777))

        self.emu.call(ROACH_BITE, [ROACH], b"\0" * 64, count=200000)
        return (struct.unpack("<I", bytes(uc.mem_read(ROACH + OBJ_OFF_DEADLINE_58, 4)))[0],
                tuple(self.trace))


def model(h, x, y, facing, nvictims, allied):
    """src/game/item.cpp's RoachBite, in the same terms."""
    import ctypes
    to_i32 = lambda f: int(ctypes.c_int32(int(f)).value)
    toX = s16(to_i32(h.cos[facing & 0xFF] * h.reach + float(x)))
    toY = s16(to_i32(h.sin[facing & 0xFF] * h.reach + float(y)))

    trace = [("sound", BITE_SOUND, toX, toY),
             ("rect", toX + h.box[0], toY + h.box[1],
              toX + h.box[2], toY + h.box[3])]

    chain = [VICTIMS + i * 0x100 for i in range(nvictims)]
    for i, v in enumerate(chain):
        if not (allied[i] if i < len(allied) else 0):
            trace.append(("damage", v, h.damage, DAMAGE_KIND, 0xABCD1234,
                          (facing + 0x80) & 0xFF, 0))
    return (0, tuple(trace))


def cases():
    for x, y in ((0, 0), (1000, 2000), (-500, 300)):
        for facing in (0, 1, 0x40, 0x7F, 0x80, 0xFF):
            for nvictims, allied in ((0, ()), (1, (0,)), (1, (1,)),
                                     (2, (0, 0)), (3, (0, 1, 0))):
                yield x, y, facing, nvictims, allied


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got, want = h.run(*args), model(h, *args)
        n += 1
        if got != want:
            bad += 1
            if bad <= 5:
                print("  x=%d y=%d facing=%02X n=%d ->\n     %s\n want %s"
                      % (args[0], args[1], args[2], args[3], got, want))
    print("roachbitecheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
