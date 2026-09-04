#!/usr/bin/env python3
"""Check ExitOneFromVehicle against the original: which arm runs, and where.

ExitOneFromVehicle (0x0045AC90) empties ONE seat of a vehicle.  CLAUDE.md
lists it among the functions no drive here reaches -- nothing in either
drivable mission puts a unit into a vehicle and takes it out again.

CHECKS = ("ExitOneFromVehicle",)

ITS OUTPUT IS NOT ITS RETURN VALUE, which is the trap this tree already
records for SelectFirePose.  The function answers 0 or 1, and five separate
refusals answer 0 -- so an oracle comparing eax would pass with four of them
deleted.  What it actually does is CALL things: it removes the seat,
broadcasts, deploys the occupant at a point it computed, and then takes one
of three selection arms.  So all ten callees are stubbed and the compared
value is the TRACE, with the deploy point in it.

Three things the trace is needed to separate, none visible in eax:

  - CommMustBroadcast is asked TWICE with the same arguments, and the first
    ask is behind the multiplayer session while the second is not.  Two calls
    to one predicate are two branches, as this file says of CreateTrooper
  - the boat arm computes its exit point through BoatExitPoint and every
    other kind subtracts a constant from the vehicle's own position, in
    SIXTEEN BITS -- a position near the origin wraps, and the corpus puts one
    there on purpose
  - the selection tail has THREE outcomes (select the occupant and deselect
    the vehicle, re-context the vehicle, or neither) and all three return 1

ListRemoveAt is stubbed to DECREMENT the count rather than to do nothing,
because the count is read again below it and the `<= 0` arm is unreachable
otherwise.  A stub that does nothing would have made that arm dead and the
mutation covering it would have failed nothing.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("ExitOneFromVehicle",)

EXIT_ONE = 0x0045AC90
COMM_MUST_BROADCAST = 0x0040F560
LOOKUP_BY_UID = 0x00427820
BOAT_EXIT_POINT = 0x0045AAF0
PLAY_SOUND_AT = 0x0040C040
LIST_REMOVE_AT = 0x0042A750
SEND_VEHICLE_EXIT = 0x0045E3C0
DEPLOY_ITEM = 0x00428CA0
SELECT_UNIT = 0x00427CE0
DESELECT_UNIT = 0x00427C80
SET_OBJ_CONTEXT = 0x00457A60

ADDR_MP_SESSION = 0x00511DA0
ADDR_COMM_OBJECT = 0x004751B0

OBJ_OFF_FLAGS = 0x08
OBJ_OFF_ARMY = 0x10
OBJ_OFF_POS = 0x12
OBJ_OFF_HEALTH = 0x62
VEHICLE_OFF_KIND = 0x52C
VEHICLE_OFF_PTR_LIST = 0x538
SUBREC_OFF_COUNT = 0x04
SUBREC_OFF_ROWS = 0x08
OBJ_OFF_RIDING = 0x570
OBJ_FLAG_SELECTED = 0x400
KIND_BOAT = 5
EXIT_OFFSET = 0x30

DATA = 0x76000000
DATA_SZ = 0x20000
VEH = DATA + 0x1000
OCC = DATA + 0x3000
ROWS = DATA + 0x5000

RIDING_SENTINEL = 0x5A5A5A5A
BOAT_POINT = 0x00110022
UID = 0x00ABCDEF


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE
        # (arg count, thiscall?, handler).  TWO of the ten are thiscall, and
        # that is not cosmetic: `this` arrives in ECX rather than on the
        # stack, and the callee pops the stack arguments itself.  A stub that
        # gets either half wrong shifts every frame below it -- which is
        # exactly what the first run of this tool did, reporting 71 of 85
        # cases differing with the arm sequence correct throughout.
        stubs = {
            COMM_MUST_BROADCAST: (1, True, self._broadcast),
            LOOKUP_BY_UID: (1, False, self._lookup),
            BOAT_EXIT_POINT: (2, False, self._boat),
            PLAY_SOUND_AT: (5, False, self._sound),
            LIST_REMOVE_AT: (1, True, self._remove),
            SEND_VEHICLE_EXIT: (2, False, self._send),
            DEPLOY_ITEM: (4, False, self._deploy),
            SELECT_UNIT: (1, False, self._select),
            DESELECT_UNIT: (1, False, self._deselect),
            SET_OBJ_CONTEXT: (1, False, self._context),
        }
        for a, (n, this, fn) in stubs.items():
            uc.mem_write(a, b"\xc3")
            uc.hook_add(UC_HOOK_CODE, self._wrap(n, this, fn), begin=a, end=a)
        self.uc = uc
        self.trace = []

    def _wrap(self, nargs, thiscall, fn):
        """One stub.  `nargs` counts STACK arguments, `this` excluded."""
        def hook(uc, addr, size, user):
            from unicorn import x86_const
            esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
            vals = struct.unpack("<%dI" % (nargs + 1),
                                 uc.mem_read(esp, 4 * (nargs + 1)))
            args = list(vals[1:])
            if thiscall:
                args.insert(0, uc.reg_read(x86_const.UC_X86_REG_ECX))
            rv = fn(tuple(args)) or 0
            uc.reg_write(x86_const.UC_X86_REG_EAX, rv & 0xFFFFFFFF)
            # thiscall is CALLEE-cleanup, so the stub pops its own arguments
            uc.reg_write(x86_const.UC_X86_REG_ESP,
                         esp + 4 + (4 * nargs if thiscall else 0))
            uc.reg_write(x86_const.UC_X86_REG_EIP, vals[0])
        return hook

    def _broadcast(self, a):
        self.trace.append(("broadcast", a[1] & 0xFF))
        return self.bcast

    def _lookup(self, a):
        self.trace.append(("lookup", a[0]))
        return 0 if self.occ_null else OCC

    def _boat(self, a):
        self.trace.append(("boatpoint",))
        if self.boat_ok:
            self.uc.mem_write(a[1], struct.pack("<I", BOAT_POINT))
        return self.boat_ok

    def _sound(self, a):
        self.trace.append(("sound", a[0], a[3] & 0xFFFF, a[4] & 0xFFFF))

    def _remove(self, a):
        self.trace.append(("remove", a[1]))
        n = struct.unpack("<i", bytes(self.uc.mem_read(
            VEH + VEHICLE_OFF_PTR_LIST + SUBREC_OFF_COUNT, 4)))[0]
        self.uc.mem_write(VEH + VEHICLE_OFF_PTR_LIST + SUBREC_OFF_COUNT,
                          struct.pack("<i", n - 1))

    def _send(self, a):
        self.trace.append(("send",))

    def _deploy(self, a):
        self.trace.append(("deploy", a[1]))

    def _select(self, a):
        self.trace.append(("select",))

    def _deselect(self, a):
        self.trace.append(("deselect",))

    def _context(self, a):
        self.trace.append(("context",))

    def run(self, c):
        uc = self.uc
        uc.mem_write(VEH, b"\0" * 0x600)
        uc.mem_write(OCC, b"\0" * 0x600)
        uc.mem_write(ROWS, struct.pack("<8I", *([UID] * 8)))
        self.bcast, self.occ_null = c["bcast"], c["occ_null"]
        self.boat_ok = c["boat_ok"]
        self.trace = []

        uc.mem_write(VEH + OBJ_OFF_FLAGS,
                     struct.pack("<I", OBJ_FLAG_SELECTED if c["sel"] else 0))
        uc.mem_write(VEH + OBJ_OFF_ARMY, struct.pack("<b", 3))
        uc.mem_write(VEH + OBJ_OFF_POS, struct.pack("<2h", c["x"], c["y"]))
        uc.mem_write(VEH + OBJ_OFF_HEALTH, struct.pack("<h", c["health"]))
        uc.mem_write(VEH + VEHICLE_OFF_KIND, struct.pack("<i", c["kind"]))
        uc.mem_write(VEH + VEHICLE_OFF_PTR_LIST + SUBREC_OFF_COUNT,
                     struct.pack("<i", c["count"]))
        uc.mem_write(VEH + VEHICLE_OFF_PTR_LIST + SUBREC_OFF_ROWS,
                     struct.pack("<I", ROWS))
        uc.mem_write(OCC + OBJ_OFF_RIDING, struct.pack("<I", RIDING_SENTINEL))
        uc.mem_write(ADDR_MP_SESSION, struct.pack("<i", c["mp"]))
        uc.mem_write(ADDR_COMM_OBJECT, struct.pack("<I", DATA))

        veh = 0 if c["null_veh"] else VEH
        rc, _ = self.emu.call(EXIT_ONE, [c["seat"], veh], b"\0" * 64,
                              count=200000)
        riding = struct.unpack("<I", bytes(
            uc.mem_read(OCC + OBJ_OFF_RIDING, 4)))[0]
        return rc, riding, tuple(self.trace)


def model(c):
    """src/game/army.cpp's ExitOneFromVehicle, in the same terms."""
    trace = []
    army = 3
    if c["null_veh"]:
        return 0, RIDING_SENTINEL, ()

    if c["mp"]:
        trace.append(("broadcast", army))
        if not c["bcast"]:
            return 0, RIDING_SENTINEL, tuple(trace)

    if c["count"] < c["seat"]:
        return 0, RIDING_SENTINEL, tuple(trace)

    trace.append(("lookup", UID))
    if c["occ_null"]:
        return 0, RIDING_SENTINEL, tuple(trace)

    if c["kind"] == KIND_BOAT and c["health"] != 0:
        trace.append(("boatpoint",))
        if not c["boat_ok"]:
            trace.append(("sound", 3, c["x"] & 0xFFFF, c["y"] & 0xFFFF))
            return 0, RIDING_SENTINEL, tuple(trace)
        at = BOAT_POINT
    else:
        at = (((c["x"] - EXIT_OFFSET) & 0xFFFF)
              | (((c["y"] - EXIT_OFFSET) & 0xFFFF) << 16))

    trace.append(("remove", c["seat"]))
    trace.append(("broadcast", army))
    if c["bcast"]:
        trace.append(("send",))
    trace.append(("deploy", at))

    if c["sel"]:
        if c["count"] - 1 <= 0:
            trace.append(("select",))
            trace.append(("deselect",))
            return 1, 0, tuple(trace)
        trace.append(("context",))
    return 1, 0, tuple(trace)


def cases():
    base = dict(null_veh=0, mp=0, bcast=1, seat=1, count=4, occ_null=0,
                kind=1, health=100, boat_ok=1, sel=0, x=0x0400, y=0x0500)

    def var(**kw):
        c = dict(base)
        c.update(kw)
        return c

    yield var(null_veh=1)
    for mp in (0, 1):
        for bcast in (0, 1):
            for sel in (0, 1):
                for count, seat in ((4, 1), (1, 1), (0, 0), (0, 1), (2, 3)):
                    yield var(mp=mp, bcast=bcast, sel=sel, count=count,
                              seat=seat)
    for kind in (1, KIND_BOAT):
        for health in (0, 100):
            for boat_ok in (0, 1):
                for sel in (0, 1):
                    for count in (1, 4):
                        yield var(kind=kind, health=health, boat_ok=boat_ok,
                                  sel=sel, count=count)
    for x, y in ((0x0010, 0x0020), (0x0000, 0x0000), (0x0030, 0x0030),
                 (0x7FF0, 0x7FF0), (-0x10, -0x20)):
        for kind in (1, KIND_BOAT):
            yield var(x=x, y=y, kind=kind)
    yield var(occ_null=1)
    yield var(occ_null=1, mp=1, bcast=1)


def main():
    h = Harness()
    n = bad = 0
    for c in cases():
        got, want = h.run(c), model(c)
        n += 1
        if got != want:
            bad += 1
            if bad <= 4:
                keys = "mp=%d b=%d seat=%d n=%d kind=%d hp=%d ok=%d sel=%d" % (
                    c["mp"], c["bcast"], c["seat"], c["count"], c["kind"],
                    c["health"], c["boat_ok"], c["sel"])
                print("  %s\n     got  %s\n     want %s" % (keys, got, want))
    print("vehexitcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
