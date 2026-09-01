"""Check DirtyCollect's 81-case table against the original, exhaustively.

The function's whole decision is a base-3 classification of the new rectangle's
four edges against an existing one -- 27*left + 9*top + 3*right + bottom, each
digit 0/1/2 for less/equal/greater -- and an 81-entry byte table mapping those
onto twenty-four arms. So the input space that matters is 81 cases, and it can
be ENUMERATED: for each code, build a pair of rectangles that produces it,
seed the list with the second, run the ORIGINAL over the first, and compare the
whole array.

Nothing else can see this. A wrong arm changes which pixels are repainted, and
the frame is repainted either way -- `tools/ab.sh` compares a settled screen,
where a rectangle that was merged when it should have been split leaves no
trace at all. The A/B would only catch an arm that drops coverage AND lands
under the cursor in the same frame.

Like tools/dirtycheck.py this is a STATEFUL oracle: the answer is the array
afterwards, not a return value. Unlike it, one import has to be hooked --
IntersectRect, which does not exist under emulation -- and the hook implements
the SDK's rule, including that an empty intersection zeroes the output and
answers false.

    tools/collectcheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

ADDR_COLLECT        = 0x0041DD90
ADDR_ADD_DIRTY_RECT = 0x0041DD00
ADDR_DIRTY_RECTS    = 0x00508AC4
ADDR_DIRTY_TAIL     = 0x00508AC0
ADDR_FULL_REDRAW    = 0x00512460
ADDR_VIEW_RECT_PREV = 0x00514E24
IAT_INTERSECT_RECT  = 0x0046F258

RECORD_SIZE = 20
OFF_PREV, OFF_NEXT = 0x10, 0x12
DEPTH_MAX = 0x1F4

# A stand-in address for IntersectRect: the IAT slot is filled with this and
# the hook fires on it. Anything unmapped and not otherwise reachable will do.
FAKE_INTERSECT = 0x00F10000

VIEW = (-4096, -4096, 4096, 4096)


def intersect(a, b):
    """The SDK's rule: empty answers false and ZEROES the output."""
    l, t = max(a[0], b[0]), max(a[1], b[1])
    r, bo = min(a[2], b[2]), min(a[3], b[3])
    if l >= r or t >= bo:
        return None
    return (l, t, r, bo)


def code_of(cur, e):
    def d(x, y):
        return 0 if x < y else (1 if x == y else 2)
    return (27 * d(cur[0], e[0]) + 9 * d(cur[1], e[1])
            + 3 * d(cur[2], e[2]) + d(cur[3], e[3]))


def arm_table():
    img = am2.Image()
    return list(img.read(0x0041E10C, 81))


ARM = arm_table()


class Model:
    """win32/mapdraw.cpp's DirtyCollect and dirty.cpp's AddDirtyRect."""

    def __init__(self, recs, tail, full):
        self.r = [list(x) for x in recs]      # [l,t,r,b,prev,next] per slot
        self.tail = tail
        self.full = full

    def add(self, l, t, r, b):
        nxt = (self.tail + 1) & 0xFFFF
        if nxt >= DEPTH_MAX:
            self.full = 1
            return
        self.r[nxt][0:4] = [l, t, r, b]
        self.r[nxt][5] = 0
        self.r[nxt][4] = self.tail
        self.r[self.tail][5] = nxt
        self.tail = nxt

    def collect(self, rect):
        cur = intersect(rect, VIEW)
        if cur is None:
            return
        cur = list(cur)
        if ((self.tail & 0xFFFF) + 1) >= DEPTH_MAX:
            self.full = 1
            return

        at = self.r[0][5]
        while at:
            e = self.r[at]
            nxt = e[5]
            if intersect(tuple(cur), tuple(e[0:4])) is not None:
                arm = ARM[code_of(cur, e)]
                drop = self._arm(arm, cur, e)
                if drop:
                    prev, n2 = e[4], e[5]
                    self.r[prev][5] = n2
                    if not n2:
                        self.tail = prev
                    else:
                        self.r[n2][4] = prev
                if arm == 13:
                    return
            at = nxt

        self.add(cur[0], cur[1], cur[2], cur[3])

    def _arm(self, arm, c, e):
        """True when the entry is unlinked. Mirrors the C, arm for arm."""
        if arm == 0:
            self.add(c[2], e[1], e[2], c[3]); e[1] = c[3]
        elif arm == 1:
            e[0] = c[2]
        elif arm == 2:
            e[1] = c[3]
        elif arm == 3:
            return True
        elif arm == 4:
            c[2] = e[2]; e[1] = c[3]
        elif arm == 5:
            c[2] = e[2]; return True
        elif arm == 6:
            self.add(e[0], c[3], e[2], e[3]); c[2] = e[2]; e[3] = c[1]
        elif arm == 7:
            c[2] = e[2]; e[3] = c[1]
        elif arm == 8:
            self.add(c[2], c[1], e[2], e[3]); e[3] = c[1]
        elif arm == 9:
            self.add(e[0], c[3], e[2], e[3]); e[3] = c[1]
        elif arm == 10:
            e[3] = c[1]
        elif arm == 11:
            c[3] = e[1]
        elif arm == 12:
            c[3] = e[3]; return True
        elif arm == 13:
            pass
        elif arm == 14:
            c[1] = e[3]
        elif arm == 15:
            return True
        elif arm == 16:
            c[1] = e[1]; return True
        elif arm == 17:
            self.add(c[2], e[1], e[2], e[3]); e[2] = c[0]
        elif arm == 18:
            e[2] = c[0]
        elif arm == 19:
            self.add(e[0], e[1], c[0], c[3]); e[1] = c[3]
        elif arm == 20:
            c[0] = e[0]; e[1] = c[3]
        elif arm == 21:
            c[0] = e[0]; return True
        elif arm == 22:
            c[0] = e[2]
        elif arm == 23:
            self.add(e[0], c[1], c[0], e[3]); e[3] = c[1]
        return False


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        # The stand-in has to be MAPPED: a code hook fires before an
        # instruction executes, and Unicorn faults on the fetch first if the
        # page is not there. Every case faulted until this line existed.
        from unicorn import UC_PROT_ALL
        uc.mem_map(FAKE_INTERSECT & ~0xFFF, 0x1000, UC_PROT_ALL)
        uc.mem_write(IAT_INTERSECT_RECT, struct.pack("<I", FAKE_INTERSECT))
        uc.mem_write(ADDR_VIEW_RECT_PREV, struct.pack("<4i", *VIEW))
        uc.hook_add(UC_HOOK_CODE, self._hook)

    def _hook(self, uc, addr, size, _user):
        if addr != FAKE_INTERSECT:
            return
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret, out, a, b = struct.unpack("<4I", bytes(uc.mem_read(esp, 16)))
        ra = struct.unpack("<4i", bytes(uc.mem_read(a, 16)))
        rb = struct.unpack("<4i", bytes(uc.mem_read(b, 16)))
        got = intersect(ra, rb)
        uc.mem_write(out, struct.pack("<4i", *(got or (0, 0, 0, 0))))
        uc.reg_write(UC_X86_REG_EAX, 1 if got else 0)
        uc.reg_write(UC_X86_REG_EIP, ret)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + 12)   # stdcall: callee cleans

    def load(self, recs, tail, full):
        uc = self.emu.uc
        blob = b"".join(struct.pack("<4ihh", *r) for r in recs)
        uc.mem_write(ADDR_DIRTY_RECTS, blob)
        uc.mem_write(ADDR_DIRTY_TAIL, struct.pack("<H", tail))
        uc.mem_write(ADDR_FULL_REDRAW, struct.pack("<i", full))

    def dump(self, n):
        uc = self.emu.uc
        out = []
        for i in range(n):
            out.append(list(struct.unpack(
                "<4ihh", bytes(uc.mem_read(ADDR_DIRTY_RECTS + i * RECORD_SIZE,
                                           RECORD_SIZE)))))
        tail = struct.unpack("<H", bytes(uc.mem_read(ADDR_DIRTY_TAIL, 2)))[0]
        full = struct.unpack("<i", bytes(uc.mem_read(ADDR_FULL_REDRAW, 4)))[0]
        return out, tail, full

    def collect(self, rect, recs, tail, full):
        self.load(recs, tail, full)
        # The rectangle goes in the scratch page the harness already maps.
        from vectors import SCRATCH
        self.emu.uc.mem_write(SCRATCH, struct.pack("<4i", *rect))
        eax, _ = self.emu.call(ADDR_COLLECT, [SCRATCH], b"", count=400000)
        if eax is None:
            return None
        return self.dump(len(recs))


def pair_for(code):
    """Two rectangles that classify as `code`, and they must intersect."""
    e = (100, 100, 200, 200)
    L, T, R, B = code // 27, (code // 9) % 3, (code // 3) % 3, code % 3
    lo = {0: 80, 1: 100, 2: 120}
    to = {0: 80, 1: 100, 2: 120}
    ro = {0: 180, 1: 200, 2: 220}
    bo = {0: 180, 1: 200, 2: 220}
    cur = (lo[L], to[T], ro[R], bo[B])
    if cur[0] >= cur[2] or cur[1] >= cur[3]:
        return None
    return cur, e


def main():
    h = Harness()
    bad = 0
    tried = 0

    for code in range(81):
        pair = pair_for(code)
        if pair is None:
            continue
        cur, e = pair
        if intersect(cur, e) is None:
            continue
        if code_of(cur, e) != code:
            continue
        tried += 1

        n = 8
        recs = [[0, 0, 0, 0, 0, 0] for _ in range(n)]
        recs[0][5] = 1                       # head -> entry 1
        recs[1][0:4] = list(e)
        recs[1][4], recs[1][5] = 0, 0
        got = h.collect(cur, recs, 1, 0)
        if got is None:
            print("code %2d: the original faulted" % code)
            bad += 1
            continue

        m = Model([list(r) for r in recs], 1, 0)
        m.collect(cur)
        want = ([r[:] for r in m.r], m.tail, m.full)

        if got[1] != want[1] or got[2] != want[2] or got[0] != want[0]:
            bad += 1
            print("code %2d (arm %d): differ" % (code, ARM[code]))
            print("   original tail=%d full=%d" % (got[1], got[2]))
            print("   ours     tail=%d full=%d" % (want[1], want[2]))
            for i, (a, b) in enumerate(zip(got[0], want[0])):
                if a != b:
                    print("   slot %d: original %s ours %s" % (i, a, b))

    # THE CASE COUNT IS A FAILURE CONDITION, not a statistic. `code_of` both
    # selects the arm and filters which pairs are valid, so a mutation to it
    # SHRINKS the corpus instead of failing it: swapping the 27 and the 9
    # weights ran 27 cases and reported them all identical. Requiring all 81
    # turns that into the failure it is -- the same shape as the corpus
    # tools/boolcheck.py derived from its own model.
    if tried != 81:
        print("collectcheck: only %d of 81 codes were exercised -- the pair"
              " generator or the classifier is wrong, which is a failure"
              " however the comparisons came out" % tried)
        return 1
    if bad:
        print("collectcheck: %d of %d cases differ" % (bad, tried))
        return 1
    print("collectcheck: all 81 classification codes, all identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
