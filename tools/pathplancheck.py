#!/usr/bin/env python3
"""Check PlanPathTo against the original: the route it writes into the object.

PlanPathTo (0x00439D60) turns a destination into the waypoint list a unit
walks.  CLAUDE.md lists it among the functions no drive here reaches.

CHECKS = ("PlanPathTo",)

IT IS A MODEL-VERSUS-ORIGINAL CHECK AND NOT A REPLAY, and this file already
draws that line: PlanPathTo calls FindPath, 1,168 bytes of A* that is still
the image's, so replaying the cases against our C would mean putting a test
hook into production code for a call the game never makes that way.  Six
callees are stubbed and what is compared is what the function does AROUND
them.

WHAT THAT LEAVES CHECKABLE, and it is the part most likely to be wrong:

  - the route is written at OBJ_OFF_MOVE_FROM with a stride of
    AM2_MOVE_STEP_BYTES, x then y, which is an indexed write into the object
    and the classic place for a stride to be wrong
  - a TERMINATOR goes one step past the last waypoint, so the count and the
    zero must agree
  - the three move fields are cleared BEFORE the search and set after, so a
    failed search leaves them cleared rather than stale
  - and the two deadlines differ: a failure sets the clock plus
    ADDR_PATH_RETRY_MS, a success the clock plus AM2_MOVE_VALID_MS

The route the stub hands back is the corpus's, so lengths of zero, one and
several are all reachable -- including the zero-length success, where the
terminator lands on waypoint 0 and the count is 0.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("PlanPathTo",)

PLAN_PATH_TO = 0x00439D60
SET_POINT_RULE = 0x00437E00
TILE_OF_POINT = 0x0042B290
NEAREST_ALLOWED_TILE = 0x0043A0A0
FIND_PATH = 0x004395B0
COLLAPSE_EQUAL_DELTAS = 0x00439CC0
TILE_TO_XY = 0x0042B210

ADDR_TILE_LINE_BUF = 0x00523DE0
ADDR_PATH_RETRY_MS = 0x00487894
ADDR_GAME_CLOCK_MS = 0x00511E04

OBJ_OFF_TILE = 0x1A
OBJ_OFF_MOVE_FROM = 0x120
OBJ_OFF_MOVE_UNTIL = 0x11C
OBJ_OFF_MOVE_AT = 0x520
OBJ_OFF_MOVE_COUNT = 0x522
STEP = 4
VALID_MS = 0xBB8

DATA = 0x74000000
DATA_SZ = 0x10000
OBJ = DATA + 0x1000
AT = DATA + 0x2000

CLOCK = 50000


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        self.retry = struct.unpack("<i", bytes(uc.mem_read(ADDR_PATH_RETRY_MS, 4)))[0]

        from unicorn import UC_HOOK_CODE
        for a in (SET_POINT_RULE, TILE_OF_POINT, NEAREST_ALLOWED_TILE,
                  FIND_PATH, COLLAPSE_EQUAL_DELTAS, TILE_TO_XY):
            uc.mem_write(a, b"\xc3")
        uc.hook_add(UC_HOOK_CODE, self._rule, begin=SET_POINT_RULE,
                    end=SET_POINT_RULE)
        uc.hook_add(UC_HOOK_CODE, self._tileof, begin=TILE_OF_POINT,
                    end=TILE_OF_POINT)
        uc.hook_add(UC_HOOK_CODE, self._nearest,
                    begin=NEAREST_ALLOWED_TILE, end=NEAREST_ALLOWED_TILE)
        uc.hook_add(UC_HOOK_CODE, self._find, begin=FIND_PATH, end=FIND_PATH)
        uc.hook_add(UC_HOOK_CODE, self._collapse,
                    begin=COLLAPSE_EQUAL_DELTAS, end=COLLAPSE_EQUAL_DELTAS)
        uc.hook_add(UC_HOOK_CODE, self._toxy, begin=TILE_TO_XY, end=TILE_TO_XY)
        self.uc = uc
        self.route = []
        self.found = 1
        self.collapse_to = None
        self.trace = []

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

    def _rule(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 1)
        self.trace.append("rule")
        self._done(uc, esp, ret)

    def _tileof(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 1)
        self._done(uc, esp, ret, (args[0] & 0xFFFF) ^ 0x1234)

    def _nearest(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 3)
        self.trace.append("nearest")
        self._done(uc, esp, ret)

    def _find(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 5)
        self.trace.append("find")
        if self.found:
            uc.mem_write(args[2],
                         b"".join(struct.pack("<H", t) for t in self.route))
            uc.mem_write(args[3], struct.pack("<i", len(self.route)))
        self._done(uc, esp, ret, self.found)

    def _collapse(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 2)
        self.trace.append("collapse")
        if self.collapse_to is not None:
            uc.mem_write(args[1], struct.pack("<i", self.collapse_to))
        self._done(uc, esp, ret)

    def _toxy(self, uc, a, s, u):
        esp, ret, args = self._take(uc, 3)
        tile = args[0] & 0xFFFF
        uc.mem_write(args[1], struct.pack("<i", tile * 3 + 1))
        uc.mem_write(args[2], struct.pack("<i", tile * 5 + 2))
        self._done(uc, esp, ret)

    def run(self, route, found, collapse_to, arg):
        uc = self.uc
        uc.mem_write(OBJ, b"\0" * 0x600)
        self.route, self.found, self.collapse_to = route, found, collapse_to
        self.trace = []
        uc.mem_write(OBJ + OBJ_OFF_TILE, struct.pack("<H", 0x0042))
        uc.mem_write(OBJ + OBJ_OFF_MOVE_UNTIL, struct.pack("<i", 0x7777))
        uc.mem_write(AT, struct.pack("<I", 0x00330044))
        uc.mem_write(ADDR_GAME_CLOCK_MS, struct.pack("<i", CLOCK))

        rc, _ = self.emu.call(PLAN_PATH_TO, [OBJ, AT, arg], b"\0" * 64,
                              count=200000)
        n = (collapse_to if (found and collapse_to is not None)
             else len(route)) if found else 0
        waypoints = []
        for i in range(max(n, 0) + 1):
            raw = uc.mem_read(OBJ + OBJ_OFF_MOVE_FROM + i * STEP, 4)
            waypoints.append(struct.unpack("<2H", raw))
        return (rc,
                struct.unpack("<H", bytes(uc.mem_read(OBJ + OBJ_OFF_MOVE_COUNT, 2)))[0],
                struct.unpack("<H", bytes(uc.mem_read(OBJ + OBJ_OFF_MOVE_AT, 2)))[0],
                struct.unpack("<i", bytes(uc.mem_read(OBJ + OBJ_OFF_MOVE_UNTIL, 4)))[0],
                tuple(waypoints), tuple(self.trace))


def model(h, route, found, collapse_to, arg):
    """src/game/region.cpp's PlanPathTo, in the same terms."""
    trace = ["rule", "nearest", "find"]
    if not found:
        return (0, 0, 0, CLOCK + h.retry, ((0, 0),), tuple(trace))

    trace.append("collapse")
    n = collapse_to if collapse_to is not None else len(route)
    pts = []
    for i in range(n):
        t = route[i] if i < len(route) else 0
        pts.append(((t * 3 + 1) & 0xFFFF, (t * 5 + 2) & 0xFFFF))
    pts.append((0, 0))                    # the terminator, one step past
    return (1, n, 0, CLOCK + VALID_MS, tuple(pts), tuple(trace))


def cases():
    for route, collapse_to in (([], None), ([7], None), ([7, 9], None),
                               ([1, 2, 3, 4], None), ([1, 2, 3, 4], 2),
                               ([5, 6, 7], 0)):
        for found in (0, 1):
            for arg in (0, 1):
                yield route, found, collapse_to, arg


def main():
    h = Harness()
    n = bad = 0
    for args in cases():
        got, want = h.run(*args), model(h, *args)
        n += 1
        if got != want:
            bad += 1
            if bad <= 5:
                print("  route=%s found=%d collapse=%s ->\n     %s\n want %s"
                      % (args[0], args[1], args[2], got, want))
    print("pathplancheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
