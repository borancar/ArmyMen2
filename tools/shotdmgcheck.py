#!/usr/bin/env python3
"""Check ApplyShotDamage against the original: the scaling, and the reaction.

ApplyShotDamage (0x0043BBE0) sits between ShotStrike and DamageObject: it
turns a shot's type record into an amount and a damage KIND, hands both to
DamageObject, and then makes the shooter react to what it hit.

CHECKS = ("ApplyShotDamage",)

It is the middle of a chain whose ends are already checked here --
tools/shotcheck.py above it and tools/damagecheck.py below -- and it is
unreachable the same way both of them are: no drive here reaches combat, and
its one caller is ours, so its counter is blind and reads 0 whatever happens.

WHAT IS COMPARED is the trace, and the ARGUMENTS to DamageObject are the
substance of it. The amount is scaled by a switch over the shot's code with
four distinct behaviours, and the KIND differs on exactly one arm -- the
random one answers 1 where every other answers 2, which no A/B could ever
see because the number never reaches the screen.

TWO OPERANDS ARE EASY TO GET WRONG AND BOTH ARE COMPARED:

  - the random arm is `rand() % (damage + 1) + 1`, so it spans 1..damage+1
    inclusive and can never be 0. `rand` is stubbed to a value the case
    chooses, which makes the modulus observable rather than incidental --
    with a real generator the same case gives a different answer each run and
    nothing could be compared at all.
  - the fifth argument is a BYTE, `facing + 0x80`, and only the low byte of
    it is the function's: the original leaves whatever the register held
    above, observed here as 0x00BB0090 with the shooter's uid showing
    through. tools/roachbitecheck.py found exactly this at the same argument
    of the same callee, which is what makes it DamageObject's contract
    rather than one caller's quirk. The corpus carries facings either side
    of the fold so the wrap is still compared.

The anti-troop arm doubles TWICE against a trooper -- once for `doubled` and
again for the type -- and that compounding is the thing a single flag cannot
distinguish, so both inputs vary independently.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("ApplyShotDamage",)

APPLY_SHOT_DAMAGE = 0x0043BBE0
GAME_RAND = 0x00464420
OBJ_IS_TYPE2 = 0x00457470
DAMAGE_OBJECT = 0x00428140
OBJS_ARE_ALLIED = 0x004574D0
LOOKUP_BY_UID = 0x00427820
OBJ_IS_TYPE_IN_238 = 0x00457420
SHOOTER_REACT = 0x00457DA0

OBJ_OFF_UID = 0x04
OBJ_OFF_FACING = 0x40
OBJ_OFF_FIELD_94 = 0x94
OBJ_OFF_RANK = 0x98
OBJ_OFF_TARGET_UID = 0xCC
TYPEREC_OFF_CODE = 0x00
TYPEREC_OFF_DAMAGE = 0x1C

KIND = 2
KIND_RAND = 1
CODE_RANDOM = 3
CODE_ANTI_TROOP = 30
DIR_BIAS = 0x80
DOUBLING_CODES = (1, 7, 8, 9, 10, 29)

DATA = 0x7E000000
DATA_SZ = 0x20000
SHOT = DATA + 0x1000
SHOT_REC = DATA + 0x2000
TARGET = DATA + 0x4000
OWNER = DATA + 0x6000

SHOOTER_UID = 0x00BB0007
TARGET_UID = 0x00CC0009
SENTINEL = 0xDEADBEEF


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        stubs = {
            GAME_RAND: (0, self._rand),
            OBJ_IS_TYPE2: (1, self._istype2),
            DAMAGE_OBJECT: (6, self._damage),
            OBJS_ARE_ALLIED: (3, self._allied),
            LOOKUP_BY_UID: (1, self._lookup),
            OBJ_IS_TYPE_IN_238: (1, self._in238),
            SHOOTER_REACT: (2, self._react),
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

    def _rand(self, a):
        self.trace.append(("rand",))
        return self.randval

    def _istype2(self, a):
        self.trace.append(("istype2",))
        return 1 if self.type2 else 0

    def _damage(self, a):
        amount = a[1] if a[1] < 0x80000000 else a[1] - 0x100000000
        # ONLY THE LOW BYTE OF THE FIFTH ARGUMENT IS THE FUNCTION'S. The
        # original leaves whatever was in the register above it -- observed
        # here as 0x00BB0090, the shooter's uid showing through under the
        # biased facing. tools/roachbitecheck.py found the same thing at the
        # same argument of the same callee, which is what makes it the
        # CALLEE's contract rather than a quirk of one caller.
        self.trace.append(("damage", amount, a[2], a[3], a[4] & 0xFF, a[5]))

    def _allied(self, a):
        self.trace.append(("allied",))
        return 1 if self.allied else 0

    def _lookup(self, a):
        self.trace.append(("lookup", a[0]))
        return OWNER if self.owner_found else 0

    def _in238(self, a):
        self.trace.append(("in238",))
        return 1 if self.in238 else 0

    def _react(self, a):
        self.trace.append(("react",))

    def run(self, c):
        uc = self.uc
        self.trace = []
        self.randval = c["rand"]
        self.type2 = c["type2"]
        self.allied = c["allied"]
        self.owner_found = c["owner_found"]
        self.in238 = c["in238"]

        uc.mem_write(SHOT, b"\0" * 0x120)
        uc.mem_write(SHOT_REC, b"\0" * 0x40)
        uc.mem_write(TARGET, b"\0" * 0x120)
        uc.mem_write(OWNER, b"\0" * 0x120)
        uc.mem_write(SHOT_REC + TYPEREC_OFF_CODE, struct.pack("<i", c["code"]))
        uc.mem_write(SHOT_REC + TYPEREC_OFF_DAMAGE,
                     struct.pack("<i", c["damage"]))
        uc.mem_write(SHOT + OBJ_OFF_FIELD_94, struct.pack("<I", SHOT_REC))
        uc.mem_write(SHOT + OBJ_OFF_RANK, struct.pack("<I", SHOOTER_UID))
        uc.mem_write(SHOT + OBJ_OFF_FACING, struct.pack("<B", c["facing"]))
        uc.mem_write(TARGET + OBJ_OFF_UID, struct.pack("<I", TARGET_UID))
        uc.mem_write(OWNER + OBJ_OFF_TARGET_UID, struct.pack("<I", SENTINEL))

        self.emu.call(APPLY_SHOT_DAMAGE, [TARGET, SHOT, 0x1111, 0x2222,
                                          c["doubled"]], b"\0" * 64,
                      count=200000)
        tgt = struct.unpack("<I", bytes(
            uc.mem_read(OWNER + OBJ_OFF_TARGET_UID, 4)))[0]
        return tgt, tuple(self.trace)


def model(c):
    """src/game/item.cpp's ApplyShotDamage, in the same terms."""
    t = []
    amount = c["damage"]
    kind = KIND

    if c["code"] == CODE_RANDOM:
        t.append(("rand",))
        amount = c["rand"] % (amount + 1) + 1
        kind = KIND_RAND
    elif c["code"] in DOUBLING_CODES:
        if c["doubled"]:
            amount += amount
    elif c["code"] == CODE_ANTI_TROOP:
        if c["doubled"]:
            amount += amount
        t.append(("istype2",))
        if c["type2"]:
            amount += amount

    extra = (c["facing"] + DIR_BIAS) & 0xFF
    t.append(("damage", amount, kind, SHOOTER_UID, extra, 0))

    t.append(("allied",))
    if c["allied"]:
        return SENTINEL, tuple(t)
    t.append(("lookup", SHOOTER_UID))
    t.append(("in238",))
    if not c["in238"]:
        return SENTINEL, tuple(t)
    t.append(("react",))
    return TARGET_UID, tuple(t)


def cases():
    base = dict(code=1, damage=20, doubled=0, rand=7, type2=0, allied=0,
                owner_found=1, in238=1, facing=0x10)

    def var(**kw):
        c = dict(base); c.update(kw); return c

    for code in (0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 29, 30, 31):
        for doubled in (0, 1):
            for type2 in (0, 1):
                yield var(code=code, doubled=doubled, type2=type2)
    # the random arm's modulus: 1..damage+1 inclusive, never 0
    for damage in (0, 1, 5, 20):
        for r in (0, 1, 6, 19, 20, 21, 1000):
            yield var(code=CODE_RANDOM, damage=damage, rand=r)
    # the biased facing is a SIGNED byte and folds
    for facing in (0x00, 0x01, 0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF):
        yield var(facing=facing)
    for allied in (0, 1):
        for in238 in (0, 1):
            yield var(allied=allied, in238=in238)


def main():
    h = Harness()
    n = bad = 0
    for c in cases():
        got, want = h.run(c), model(c)
        n += 1
        if got != want:
            bad += 1
            if bad <= 4:
                print("  code=%d dmg=%d dbl=%d rand=%d t2=%d facing=%#x"
                      % (c["code"], c["damage"], c["doubled"], c["rand"],
                         c["type2"], c["facing"]))
                print("     got  %s" % (got,))
                print("     want %s" % (want,))
    print("shotdmgcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
