#!/usr/bin/env python3
"""Check DamageObject against the original: which arm runs, and the death tail.

DamageObject (0x00428140) is where the damage family converges -- three
refusals, a four-way dispatch on the object's type, and then a death tail
that fires only when the blow was fatal.

CHECKS = ("DamageObject",)

IT IS THE FAMILY'S ORACLE FOR THE REASON `aicheck.py` IS THE AI'S. CLAUDE.md
records that no configuration here reaches combat, and three separate probes
have now failed to build one: Boot Camp holds three enemies 1,330 units from
the player, MAP 01 leaves every one of its 331 objects at full health after
46 seconds, and all four counters on this path are BLIND, every caller being
ours. So this function is checked here or nowhere.

WHAT IS COMPARED IS THE TRACE, because the function returns void and does its
work by calling. Eight of the fourteen callees are stubbed; the six on the
death tail are stubbed too, so a fatal blow is observable as a sequence
rather than as a change to a world that does not exist offline.

Two things the corpus has to reach that a live drive never would:

  - the health test is asked TWICE, before the dispatch and again after it.
    The first sends an already-dead object down the notify-only path; the
    second decides whether this blow killed it. A stub that leaves health
    alone can only reach the first, so DamageTrooper and its siblings are
    stubbed to SUBTRACT, and the corpus carries blows that do and do not kill.
  - `CommMustBroadcast` is asked in THREE places with two different arguments
    -- the attacker's army twice and the VICTIM's once, on the last refusal.
    Answering it from one flag would make those indistinguishable, so the
    stub answers per-army and the corpus varies the two independently.

THE MULTIPLAYER ARMS ARE THE POINT. ADDR_MP_SESSION is 0 on every drive this
project has, so all three of the broadcast refusals are unreachable in play
and unreachable to any A/B -- this is the only thing that exercises them.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("DamageObject",)

DAMAGE_OBJECT = 0x00428140
FIND_SLOT = 0x004277A0
COMM_MUST_BROADCAST = 0x0040F560
NOTIFY_DAMAGED = 0x00427E10
DAMAGE_BROADCAST = 0x0042A880
DAMAGE_ITEM = 0x004356C0
DAMAGE_TROOPER = 0x00447A40
DAMAGE_VEHICLE = 0x0045B4D0
DAMAGE_ROACH = 0x0043D280
TRIGGER_ITEM_DESTROYED = 0x00427FD0
SEND_DEATH_MESSAGE = 0x0042A930
OBJ_DEATH_CLEANUP = 0x00428070
DESELECT_UNIT = 0x00427C80
LOOKUP_OWNER_OBJ = 0x00457750
SELECT_UNIT = 0x00427CE0

ADDR_MP_SESSION = 0x00511DA0
ADDR_COMM_OBJECT = 0x004751B0
ADDR_OBJ_TABLE = 0x00514F0C
ADDR_SELECTED_COUNT = 0x0051230C
ADDR_DEFAULT_OWNER = 0x004F9FDC

OBJ_OFF_FLAGS = 0x08
OBJ_OFF_OWNER = 0x10
OBJ_OFF_POS = 0x12
OBJ_OFF_HEALTH = 0x62
FLAG_DESTROYED = 0x04
FLAG_SELECTED = 0x400
ENTRY_BYTES = 12                 # {uid, obj, stamp}

DATA = 0x7A000000
DATA_SZ = 0x20000
OBJ = DATA + 0x1000
ATTACKER = DATA + 0x3000
LEADER = DATA + 0x5000
TABLE = DATA + 0x7000

ATTACKER_UID = 0x00C0FFEE
DEFAULT_OWNER = 0
ATTACKER_ARMY = 1
VICTIM_ARMY = 2


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        stubs = {
            FIND_SLOT: (2, "cdecl", self._findslot),
            COMM_MUST_BROADCAST: (1, "thiscall", self._broadcast),
            NOTIFY_DAMAGED: (2, "cdecl", self._notify),
            DAMAGE_BROADCAST: (6, "cdecl", self._dbroadcast),
            DAMAGE_ITEM: (6, "cdecl", self._dmg("item")),
            DAMAGE_TROOPER: (5, "cdecl", self._dmg("trooper")),
            DAMAGE_VEHICLE: (5, "cdecl", self._dmg("vehicle")),
            DAMAGE_ROACH: (5, "cdecl", self._dmg("roach")),
            TRIGGER_ITEM_DESTROYED: (2, "cdecl", self._t("destroyed")),
            SEND_DEATH_MESSAGE: (3, "cdecl", self._t("deathmsg")),
            OBJ_DEATH_CLEANUP: (1, "cdecl", self._t("cleanup")),
            DESELECT_UNIT: (1, "cdecl", self._t("deselect")),
            LOOKUP_OWNER_OBJ: (1, "cdecl", self._lookup),
            SELECT_UNIT: (1, "cdecl", self._t("select")),
        }
        for a, (n, mode, fn) in stubs.items():
            uc.mem_write(a, b"\xc3")
            uc.hook_add(UC_HOOK_CODE, self._wrap(n, mode, fn), begin=a, end=a)
        self.uc = uc

    def _wrap(self, nargs, mode, fn):
        def hook(uc, addr, size, user):
            from unicorn import x86_const
            esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
            vals = struct.unpack("<%dI" % (nargs + 1),
                                 uc.mem_read(esp, 4 * (nargs + 1)))
            args = list(vals[1:])
            if mode == "thiscall":
                args.insert(0, uc.reg_read(x86_const.UC_X86_REG_ECX))
            rv = fn(tuple(args))
            uc.reg_write(x86_const.UC_X86_REG_EAX, (rv or 0) & 0xFFFFFFFF)
            pop = 4 + (4 * nargs if mode in ("thiscall", "stdcall") else 0)
            uc.reg_write(x86_const.UC_X86_REG_ESP, esp + pop)
            uc.reg_write(x86_const.UC_X86_REG_EIP, vals[0])
        return hook

    def _t(self, name):
        def fn(a):
            self.trace.append((name,))
        return fn

    def _findslot(self, a):
        self.trace.append(("findslot", a[0]))
        return 0 if self.attacker_known else -1

    def _broadcast(self, a):
        """Answers per ARMY, so the victim's ask is distinguishable."""
        army = a[1] & 0xFFFF
        if army & 0x8000:
            army -= 0x10000
        self.trace.append(("broadcast", army))
        return self.bcast.get(army, 0)

    def _notify(self, a):
        self.trace.append(("notify", a[1] != 0))

    def _dbroadcast(self, a):
        self.trace.append(("dbroadcast", a[2], a[3]))

    def _dmg(self, which):
        def fn(a):
            self.trace.append((which, a[1]))
            # A blow that lands has to be able to KILL, or the second health
            # test below can never be reached.
            hp = struct.unpack("<h", bytes(
                self.uc.mem_read(OBJ + OBJ_OFF_HEALTH, 2)))[0]
            hp -= self.lethal
            self.uc.mem_write(OBJ + OBJ_OFF_HEALTH,
                              struct.pack("<h", max(hp, -0x8000)))
        return fn

    def _lookup(self, a):
        self.trace.append(("lookupowner", a[0]))
        return LEADER

    def run(self, c):
        uc = self.uc
        self.trace = []
        self.attacker_known = c["attacker"]
        self.bcast = {ATTACKER_ARMY: c["bcast_att"],
                      c["victim_army"]: c["bcast_vic"], 4: c["bcast_none"]}
        self.lethal = c["lethal"]

        uc.mem_write(OBJ, b"\0" * 0x200)
        uc.mem_write(ATTACKER, b"\0" * 0x40)
        uc.mem_write(LEADER, b"\0" * 0x40)
        flags = (FLAG_DESTROYED if c["destroyed"] else 0) \
              | (FLAG_SELECTED if c["selected"] else 0)
        uc.mem_write(OBJ, struct.pack("<I", c["type"]))
        uc.mem_write(OBJ + OBJ_OFF_FLAGS, struct.pack("<I", flags))
        uc.mem_write(OBJ + OBJ_OFF_OWNER, struct.pack("<b", c["victim_army"]))
        uc.mem_write(OBJ + OBJ_OFF_POS, struct.pack("<2h", 300, 400))
        uc.mem_write(OBJ + OBJ_OFF_HEALTH, struct.pack("<h", c["health"]))
        uc.mem_write(ATTACKER + OBJ_OFF_OWNER, struct.pack("<b", ATTACKER_ARMY))
        uc.mem_write(LEADER + OBJ_OFF_FLAGS,
                     struct.pack("<I", FLAG_DESTROYED if c["leader_dead"] else 0))
        uc.mem_write(TABLE, struct.pack("<III", ATTACKER_UID, ATTACKER, 0))
        uc.mem_write(ADDR_OBJ_TABLE, struct.pack("<I", TABLE))
        uc.mem_write(ADDR_MP_SESSION, struct.pack("<i", c["mp"]))
        uc.mem_write(ADDR_COMM_OBJECT, struct.pack("<I", DATA))
        uc.mem_write(ADDR_SELECTED_COUNT, struct.pack("<i", c["selcount"]))
        uc.mem_write(ADDR_DEFAULT_OWNER, struct.pack("<I", DEFAULT_OWNER))

        self.emu.call(DAMAGE_OBJECT,
                      [OBJ, c["amount"], c["kind"], ATTACKER_UID,
                       c["extra"], c["suppress"]], b"\0" * 64, count=400000)
        hp = struct.unpack("<h", bytes(
            uc.mem_read(OBJ + OBJ_OFF_HEALTH, 2)))[0]
        return hp, tuple(self.trace)


ARMS = {1: "item", 2: "trooper", 3: "vehicle", 8: "roach"}


def model(c):
    """src/game/item.cpp's DamageObject, in the same terms."""
    t = []
    hp = c["health"]
    if c["destroyed"]:
        return hp, ()

    t.append(("findslot", ATTACKER_UID))
    att = c["attacker"]
    owner = ATTACKER_ARMY if att else 4
    bc = {ATTACKER_ARMY: c["bcast_att"], c["victim_army"]: c["bcast_vic"],
          4: c["bcast_none"]}

    if c["mp"] and c["suppress"] == 0:
        t.append(("broadcast", owner))
        if not bc[owner]:
            return hp, tuple(t)

    if hp <= 0:
        t.append(("notify", bool(att)))
        if c["mp"] and c["suppress"] == 0:
            t.append(("broadcast", owner))
            if bc[owner]:
                t.append(("dbroadcast", c["amount"], c["kind"]))
        return hp, tuple(t)

    arm = ARMS.get(c["type"])
    if arm:
        t.append((arm, c["amount"]))
        hp -= c["lethal"]

    t.append(("notify", bool(att)))
    if c["mp"] and c["suppress"] == 0:
        t.append(("broadcast", owner))
        if bc[owner]:
            t.append(("dbroadcast", c["amount"], c["kind"]))

    if hp > 0:
        return hp, tuple(t)

    if c["mp"]:
        t.append(("broadcast", c["victim_army"]))
        if not bc[c["victim_army"]]:
            return hp, tuple(t)

    t.append(("destroyed",)); t.append(("deathmsg",)); t.append(("cleanup",))
    if not c["selected"]:
        return hp, tuple(t)
    t.append(("deselect",))
    if c["victim_army"] != DEFAULT_OWNER or c["selcount"] != 0:
        return hp, tuple(t)
    t.append(("lookupowner", DEFAULT_OWNER))
    if not c["leader_dead"]:
        t.append(("select",))
    return hp, tuple(t)


def cases():
    base = dict(type=2, health=100, destroyed=0, selected=0, selcount=0,
                mp=0, suppress=0, attacker=1, amount=25, kind=7, extra=3,
                lethal=25, bcast_att=1, bcast_vic=1, bcast_none=1,
                leader_dead=0, victim_army=VICTIM_ARMY)

    def var(**kw):
        c = dict(base); c.update(kw); return c

    yield var(destroyed=1)
    for typ in (0, 1, 2, 3, 4, 5, 6, 7, 8):
        for lethal in (25, 200):            # survives / dies
            for selected in (0, 1):
                yield var(type=typ, lethal=lethal, selected=selected)
    for health in (0, -5, 1):
        for att in (0, 1):
            yield var(health=health, attacker=att)
    for mp in (0, 1):
        for sup in (0, 1):
            for ba in (0, 1):
                for bv in (0, 1):
                    for lethal in (25, 200):
                        yield var(mp=mp, suppress=sup, bcast_att=ba,
                                  bcast_vic=bv, lethal=lethal, selected=1)
    for att in (0, 1):
        for bn in (0, 1):
            yield var(mp=1, attacker=att, bcast_none=bn, lethal=200)
    # THE DEEPEST TAIL NEEDS THE VICTIM TO BE THE PLAYER'S OWN. With the
    # victim on another army the function returns at `owner != defaultOwner`
    # and the selected-count test, the leader lookup and the re-select are
    # all unreachable -- two mutations passed all 83 cases before this
    # dimension existed, which is the corpus driving the thing under test.
    for varmy in (DEFAULT_OWNER, VICTIM_ARMY):
        for selcount in (0, 2):
            for leader_dead in (0, 1):
                for sel in (0, 1):
                    yield var(selected=sel, lethal=200, selcount=selcount,
                              leader_dead=leader_dead, victim_army=varmy)


def main():
    h = Harness()
    n = bad = 0
    for c in cases():
        got, want = h.run(c), model(c)
        n += 1
        if got != want:
            bad += 1
            if bad <= 4:
                print("  type=%d hp=%d mp=%d sup=%d att=%d lethal=%d sel=%d"
                      % (c["type"], c["health"], c["mp"], c["suppress"],
                         c["attacker"], c["lethal"], c["selected"]))
                print("     got  %s" % (got,))
                print("     want %s" % (want,))
    print("damagecheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
