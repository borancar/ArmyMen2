#!/usr/bin/env python3
"""Check ShotStrike against the original: who it damages, and when it stops.

ShotStrike (0x0043C000) walks the objects at a point, damages the ones a shot
can hit, and then asks the terrain what it struck.

CHECKS = ("ShotStrike",)

THIS IS THE FUNCTION CLAUDE.md CALLS COVERED AND UNCHECKED. It ran 13,582
times on a driven Boot Camp mission -- the busiest thing in the tree at the
time -- and `ab.sh combat` still could not see a change to it: making a
non-explosive shot PENETRATE instead of stopping at the first thing it
damages left the run clean, 21 identical messages and frames in step. The
reason is the shape of the count rather than its size: almost every call
finds nothing at the point and falls straight through to the terrain test, so
a five-figure counter certifies only the path that does nothing.

That counter is also blind now, its callers being ours, so it cannot even
report the coverage it once did.

WHAT IS COMPARED is the return value, the SEQUENCE of objects handed to
ApplyShotDamage, and the field the explosive arm rewrites. Five callees are
stubbed, and the two predicates among them are driven from per-object flags
rather than from the object's type: what is under test is this function's
control flow, and `ObjIsItem` and `ObjIsType2` are separately covered at 100%
by tests/vectors.h.

THE STOP-VERSUS-CONTINUE DECISION IS THE POINT. A non-explosive shot returns
after the FIRST object it damages; an explosive one carries on through the
list, skipping troopers and flagged items when it decides what to rewrite.
The corpus therefore carries lists where more than one object can be hit,
because a list of one cannot tell stopping from continuing -- which is
exactly why the live drive could not.

THE TILE ARRAYS ARE REACHED THROUGH POINTERS and are `.bss` in the file, so
they are allocated and seeded here; unseeded, every case reads a null pointer
and the terrain test never runs. That is shakecheck's finding again.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("ShotStrike",)

SHOT_STRIKE = 0x0043C000
OBJECTS_AT_POINT = 0x0042A550
SHOT_HITS_OBJ = 0x0043BCD0
APPLY_SHOT_DAMAGE = 0x0043BBE0
OBJ_IS_TYPE2 = 0x00457470
OBJ_IS_ITEM = 0x00433860

ADDR_TILE_ATTRS = 0x00514EBC
ADDR_CELL_WEIGHTS = 0x00514EC0
ADDR_TILE_FLAGS = 0x00514ED0

OBJ_OFF_UID = 0x04
OBJ_OFF_ARMY = 0x10
OBJ_OFF_TILE = 0x1A
OBJ_OFF_FIELD_44 = 0x44
OBJ_OFF_QUERY_NEXT = 0x68
OBJ_OFF_FIELD_94 = 0x94
OBJ_OFF_RANK = 0x98
TYPEREC_OFF_CODE = 0x00
TYPEREC_OFF_FIELD_08 = 0x08
TYPEREC_OFF_FIELD_3C = 0x3C

STRUCK_NOTHING = 0
STRUCK_HARD = 5
STRUCK_GROUND = 6

DATA = 0x7C000000
DATA_SZ = 0x40000
SHOT = DATA + 0x1000
SHOT_REC = DATA + 0x2000
OBJS = DATA + 0x4000            # 0x200 apart
OBJ_REC = DATA + 0x9000         # 0x100 apart, one per object
ATTRS = DATA + 0x20000
WEIGHTS = DATA + 0x24000
FLAGS = DATA + 0x28000
assert FLAGS + 0x10000 < DATA + DATA_SZ

OBJ_STRIDE = 0x200
REC_STRIDE = 0x100
SHOOTER_UID = 0x00AA0001
TILE = 0x40
SHOT_ARMY = 1


def obj_at(i):
    return OBJS + i * OBJ_STRIDE


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        stubs = {
            OBJECTS_AT_POINT: (2, self._atpoint),
            SHOT_HITS_OBJ: (6, self._hits),
            APPLY_SHOT_DAMAGE: (5, self._apply),
            OBJ_IS_TYPE2: (1, self._istype2),
            OBJ_IS_ITEM: (1, self._isitem),
        }
        for a, (n, fn) in stubs.items():
            uc.mem_write(a, b"\xc3")
            uc.hook_add(UC_HOOK_CODE, self._wrap(n, fn), begin=a, end=a)
        self.uc = uc

    def _wrap(self, nargs, fn):
        def hook(uc, addr, size, user):
            from unicorn import x86_const
            esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
            vals = struct.unpack("<%dI" % (nargs + 1),
                                 uc.mem_read(esp, 4 * (nargs + 1)))
            rv = fn(tuple(vals[1:]))
            uc.reg_write(x86_const.UC_X86_REG_EAX, (rv or 0) & 0xFFFFFFFF)
            uc.reg_write(x86_const.UC_X86_REG_ESP, esp + 4)   # all cdecl
            uc.reg_write(x86_const.UC_X86_REG_EIP, vals[0])
        return hook

    def _index(self, ptr):
        return (ptr - OBJS) // OBJ_STRIDE

    def _atpoint(self, a):
        self.trace.append(("atpoint",))
        return obj_at(0) if self.objs else 0

    def _hits(self, a):
        i = self._index(a[0])
        o = self.objs[i]
        self.trace.append(("hits", i))
        self.uc.mem_write(a[5], struct.pack("<i", o["scratch"]))
        return 1 if o["hits"] else 0

    def _apply(self, a):
        self.trace.append(("apply", self._index(a[0]), a[4]))

    def _istype2(self, a):
        return 1 if self.objs[self._index(a[0])]["type2"] else 0

    def _isitem(self, a):
        return 1 if self.objs[self._index(a[0])]["item"] else 0

    def run(self, c):
        uc = self.uc
        self.trace = []
        self.objs = c["objs"]

        uc.mem_write(SHOT, b"\0" * 0x200)
        uc.mem_write(SHOT_REC, b"\0" * 0x80)
        uc.mem_write(SHOT_REC + TYPEREC_OFF_CODE, struct.pack("<i", c["code"]))
        uc.mem_write(SHOT_REC + TYPEREC_OFF_FIELD_08,
                     struct.pack("<i", c["rec8"]))
        uc.mem_write(SHOT + OBJ_OFF_FIELD_94, struct.pack("<I", SHOT_REC))
        uc.mem_write(SHOT + OBJ_OFF_RANK, struct.pack("<I", SHOOTER_UID))
        uc.mem_write(SHOT + OBJ_OFF_ARMY, struct.pack("<b", SHOT_ARMY))
        uc.mem_write(SHOT + OBJ_OFF_TILE, struct.pack("<H", TILE))
        uc.mem_write(SHOT + OBJ_OFF_FIELD_44, struct.pack("<i", 0x7777))

        for i, o in enumerate(c["objs"]):
            base = obj_at(i)
            rec = OBJ_REC + i * REC_STRIDE
            uc.mem_write(base, b"\0" * 0x120)
            uc.mem_write(rec, b"\0" * 0x40)
            uc.mem_write(base + OBJ_OFF_UID, struct.pack("<I", o["uid"]))
            uc.mem_write(base + OBJ_OFF_FIELD_94, struct.pack("<I", rec))
            uc.mem_write(rec + TYPEREC_OFF_FIELD_3C,
                         struct.pack("<B", o["rec3c"]))
            nxt = obj_at(i + 1) if i + 1 < len(c["objs"]) else 0
            uc.mem_write(base + OBJ_OFF_QUERY_NEXT, struct.pack("<I", nxt))

        # The tile arrays are pointers into .bss; unseeded every case reads
        # null and the terrain test never runs.
        uc.mem_write(ATTRS, b"\0" * 0x200)
        uc.mem_write(WEIGHTS, b"\0" * 0x200)
        uc.mem_write(FLAGS, b"\0" * 0x200)
        uc.mem_write(ATTRS + TILE, struct.pack("<b", c["attr"]))
        uc.mem_write(WEIGHTS + TILE, struct.pack("<b", c["weight"]))
        uc.mem_write(FLAGS + TILE, struct.pack("<B", c["flag"]))
        uc.mem_write(ADDR_TILE_ATTRS, struct.pack("<I", ATTRS))
        uc.mem_write(ADDR_CELL_WEIGHTS, struct.pack("<I", WEIGHTS))
        uc.mem_write(ADDR_TILE_FLAGS, struct.pack("<I", FLAGS))

        rc, _ = self.emu.call(SHOT_STRIKE, [SHOT, c["at"], c["height"]],
                              b"\0" * 64, count=400000)
        f44 = struct.unpack("<i", bytes(
            uc.mem_read(SHOT + OBJ_OFF_FIELD_44, 4)))[0]
        return rc, f44, tuple(self.trace)


def model(c):
    """src/game/item.cpp's ShotStrike, in the same terms."""
    t = [("atpoint",)]
    f44 = 0x7777
    for i, o in enumerate(c["objs"]):
        if o["uid"] == SHOOTER_UID:
            continue
        t.append(("hits", i))
        if not o["hits"]:
            continue
        t.append(("apply", i, o["scratch"]))
        if c["code"] != 3:
            return STRUCK_NOTHING, f44, tuple(t)
        if o["type2"]:
            continue
        if o["item"] and o["rec3c"] > 0:
            continue
        f44 = 20 - c["rec8"]

    if c["attr"] < c["height"]:
        return STRUCK_GROUND, f44, tuple(t)
    if c["code"] == 3:
        return STRUCK_GROUND, 20 - c["rec8"], tuple(t)
    if not (c["flag"] & 1):
        return STRUCK_NOTHING, f44, tuple(t)
    if c["weight"] < 0x0F:
        return STRUCK_NOTHING, f44, tuple(t)
    return STRUCK_HARD, f44, tuple(t)


def O(uid, hits=1, type2=0, item=0, rec3c=0, scratch=7):
    return dict(uid=uid, hits=hits, type2=type2, item=item, rec3c=rec3c,
                scratch=scratch)


def cases():
    base = dict(code=1, rec8=4, at=0x00320064, height=5, attr=20,
                weight=0x20, flag=1, objs=[])

    def var(**kw):
        c = dict(base); c.update(kw); return c

    lists = [
        [],
        [O(SHOOTER_UID)],                                   # only the shooter
        [O(0x11, hits=0)],
        [O(0x11)],
        [O(SHOOTER_UID), O(0x11), O(0x12)],                 # skip, then two
        [O(0x11, hits=0), O(0x12), O(0x13)],
        [O(0x11), O(0x12, type2=1), O(0x13)],
        [O(0x11, type2=1), O(0x12, item=1, rec3c=2), O(0x13)],
        [O(0x11, item=1, rec3c=0), O(0x12)],                # item, flag clear
        [O(0x11, scratch=3), O(0x12, scratch=9)],
    ]
    for objs in lists:
        for code in (1, 3):
            yield var(objs=objs, code=code)
    # THE TERRAIN TEST IS ONLY REACHED WHEN THE WALK DOES NOT RETURN, and a
    # non-explosive shot returns the moment it damages anything. So the attr
    # sweep has to run over an EMPTY list too -- with one hittable object and
    # code 1 the function is gone before the ground test, and turning `<`
    # into `<=` failed not one of 55 cases.
    for attr in (-1, 4, 5, 6, 40):
        for height in (5, 40):
            for code in (1, 3):
                for objs in ([], [O(0x11)], [O(0x11, hits=0)]):
                    yield var(attr=attr, height=height, code=code, objs=objs)

    # AND THE LOOP'S WRITE TO FIELD_44 IS OBSERVABLE ONLY ON THE EARLY GROUND
    # RETURN. For an explosive shot the `code == 3` arm below rewrites that
    # field with the same value, so unless `attr < height` sends the function
    # out first, what the loop decided is overwritten and the two SKIPS -- the
    # trooper and the flagged item -- change nothing anyone can see. Both
    # mutations passed all 55 cases before these existed.
    for objs in ([O(0x11, type2=1)],
                 [O(0x11, item=1, rec3c=2)],
                 [O(0x11, item=1, rec3c=0)],
                 [O(0x11, type2=1), O(0x12, type2=1)],
                 [O(0x11, type2=1), O(0x12)],
                 [O(0x11), O(0x12, type2=1)]):
        for rec8 in (4, 25):
            yield var(code=3, rec8=rec8, attr=-5, height=5, objs=objs)
    for flag in (0, 1, 3):
        for weight in (0x00, 0x0E, 0x0F, 0x7F):
            yield var(flag=flag, weight=weight, attr=90, height=5)
    for rec8 in (0, 4, 25):
        yield var(code=3, rec8=rec8, objs=[O(0x11), O(0x12)])


def main():
    h = Harness()
    n = bad = 0
    for c in cases():
        got, want = h.run(c), model(c)
        n += 1
        if got != want:
            bad += 1
            if bad <= 4:
                print("  code=%d attr=%d h=%d flag=%d w=%d objs=%d"
                      % (c["code"], c["attr"], c["height"], c["flag"],
                         c["weight"], len(c["objs"])))
                print("     got  %s" % (got,))
                print("     want %s" % (want,))
    print("shotcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
