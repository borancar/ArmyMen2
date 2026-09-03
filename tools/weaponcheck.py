#!/usr/bin/env python3
"""Check UnitWeaponInfo's range and readiness against the original.

UnitWeaponInfo (0x004045E0) fills the weapon half of a sight context: which
weapon is in hand, its kind and damage, the range band to want and to accept,
and whether the cooldown is up.  CLAUDE.md lists it among the functions no
drive here reaches, and its counter is BLIND besides -- every caller is
reconstructed -- so neither a run nor `counts` can speak for it.

Its decisions are small and enumerable: THREE range arms (a fixed weapon gets
a flat band either side of its nominal range, a zero range means "none", and
everything else scales by two doubles with a third applied first for a timed
weapon) and TWO readiness arms (a timed weapon compares the raw cooldown, and
everything else scales it by the firer's RANK).  So the space is enumerated
rather than sampled.

WeaponByUid is stubbed so "what is in hand" becomes an input.  The stub reads
its answer from a memory cell rather than carrying an immediate: Unicorn
caches translated blocks, and rewriting a stub's immediate between cases
silently returns the first one -- see tools/mprowcheck.py, which was written
wrong that way first.

WHAT IT DOES NOT COVER: WeaponByUid's own lookup, and where the item-type
record and the rank table come from.  Those stay verified by reading.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

UNIT_WEAPON_INFO = 0x004045E0
WEAPON_BY_UID = 0x0045EE80

RANK_RECORDS = 0x00473DC0
RANK_REC_BYTES = 28
GAME_CLOCK_MS = 0x00511E04
RANGE_LO = 0x0046F2E8
RANGE_HI = 0x0046F2E0
RANGE_K3 = 0x0046F2F0

UNIT_INVENTORY = 0x54C
UNIT_INVENTORY_SEL = 0x568
OBJ_RANK = 0x98
OBJ_FIELD_C0 = 0xC0
ITEM_LAST_USE = 0xC4

IT_KIND, IT_COOLDOWN, IT_RANGE, IT_DAMAGE = 0x00, 0x04, 0x10, 0x14

C_WEAPON, C_KIND, C_DAMAGE = 0x40, 0x44, 0x48
C_WANT, C_MAX, C_READY = 0x4C, 0x50, 0x54

KIND_FIXED, KIND_TIMED, RANGE_NONE = 0x2B, 3, 0x1000

DATA, DATA_SZ = 0x62000000, 0x10000
UNIT = DATA + 0x1000
CTX = DATA + 0x2000
WEAPON = DATA + 0x3000
ITEMTYPE = DATA + 0x4000
WEAPON_CELL = DATA + 0x5000

NOW = 500000
LO, HI, K3 = 0.9, 1.1, 1.2
FIRE_SCALE_OFF = 12       # RANK_REC_OFF_FIRE_SCALE, a float in the record


class Harness:
    def __init__(self, fire_off):
        self.emu = Emu()
        self.fire_off = fire_off
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        uc.mem_write(WEAPON_BY_UID,
                     b"\xa1" + struct.pack("<I", WEAPON_CELL) + b"\xc3")
        uc.mem_write(RANGE_LO, struct.pack("<d", LO))
        uc.mem_write(RANGE_HI, struct.pack("<d", HI))
        uc.mem_write(RANGE_K3, struct.pack("<d", K3))
        uc.mem_write(GAME_CLOCK_MS, struct.pack("<I", NOW))

    def run(self, armed, kind, rng, damage, cooldown, elapsed, rank, scale):
        uc = self.emu.uc
        uc.mem_write(UNIT, b"\0" * 0x600)
        uc.mem_write(UNIT + UNIT_INVENTORY_SEL, struct.pack("<i", 0))
        uc.mem_write(UNIT + OBJ_RANK, struct.pack("<i", rank))
        uc.mem_write(CTX, b"\0" * 0x80)
        uc.mem_write(WEAPON, b"\0" * 0x100)
        uc.mem_write(WEAPON + OBJ_FIELD_C0, struct.pack("<I", ITEMTYPE))
        uc.mem_write(WEAPON + ITEM_LAST_USE, struct.pack("<I", NOW - elapsed))
        uc.mem_write(ITEMTYPE, b"\0" * 0x40)
        uc.mem_write(ITEMTYPE + IT_KIND, struct.pack("<i", kind))
        uc.mem_write(ITEMTYPE + IT_COOLDOWN, struct.pack("<I", cooldown))
        uc.mem_write(ITEMTYPE + IT_RANGE, struct.pack("<i", rng))
        uc.mem_write(ITEMTYPE + IT_DAMAGE, struct.pack("<i", damage))
        uc.mem_write(RANK_RECORDS + rank * RANK_REC_BYTES + self.fire_off,
                     struct.pack("<f", scale))
        uc.mem_write(WEAPON_CELL, struct.pack("<I", WEAPON if armed else 0))

        _, _ = self.emu.call(UNIT_WEAPON_INFO, [UNIT, CTX], b"\0" * 64)
        out = uc.mem_read(CTX, 0x60)
        g = lambda o: struct.unpack_from("<i", out, o)[0]
        return (struct.unpack_from("<I", out, C_WEAPON)[0],
                g(C_KIND), g(C_DAMAGE), g(C_WANT), g(C_MAX), g(C_READY))


def model(armed, kind, rng, damage, cooldown, elapsed, rank, scale):
    if not armed:
        return (0, 0, 0, 0, 0, 0)

    want = mx = 0
    if kind == KIND_FIXED:
        want, mx = rng - 4, rng + 2
    elif rng == 0:
        want = mx = RANGE_NONE
    else:
        r = rng
        if kind == KIND_TIMED:
            r = int(float(r) * K3)
        want, mx = int(float(r) * LO), int(float(r) * HI)

    if kind == KIND_TIMED:
        ready = 1 if (cooldown & 0xFFFFFFFF) < elapsed else 0
    else:
        wait = int(float(cooldown & 0xFFFFFFFF) * float(scale))
        ready = 1 if (wait & 0xFFFFFFFF) < elapsed else 0

    return (WEAPON, kind, damage, want, mx, ready)


KINDS = (0, 1, KIND_TIMED, 5, KIND_FIXED)
RANGES = (0, 1, 4, 10, 64, 300)
COOLDOWNS = (0, 100, 1000)
ELAPSEDS = (0, 99, 100, 101, 1000, 2000)
RANKS = (0, 3, 7)
SCALES = (1.0, 0.5, 2.0)


def main():
    off = FIRE_SCALE_OFF
    h = Harness(off)
    cases = bad = 0

    for armed in (0, 1):
        for kind in KINDS:
            for rng in RANGES:
                for cooldown in COOLDOWNS:
                    for elapsed in ELAPSEDS:
                        for rank, scale in zip(RANKS, SCALES):
                            got = h.run(armed, kind, rng, 77, cooldown,
                                        elapsed, rank, scale)
                            want = model(armed, kind, rng, 77, cooldown,
                                         elapsed, rank, scale)
                            cases += 1
                            if got != want:
                                bad += 1
                                if bad <= 6:
                                    print("  armed %d kind %#x rng %d cd %d "
                                          "el %d rank %d s %.1f\n"
                                          "    original %s\n    model    %s"
                                          % (armed, kind, rng, cooldown,
                                             elapsed, rank, scale, got, want))
    print("weaponcheck: %d cases, %d differ" % (cases, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
