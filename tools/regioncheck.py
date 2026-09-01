"""Check RegionSolvePair's table filling against the original.

orig.h says of this neighbourhood that "region routing feeds the AI, which no
drive reaches, so a wrong next-hop table would pass every configuration in
tools/ab.sh", and that what it wants is an oracle. This is that oracle.

WHAT IT CHECKS IS THE TABLE FILLING AND NOTHING ELSE, which is the part the
warning is about. RegionFindPath -- 1,168 bytes of A* over the region graph --
is still the original's and is HOOKED here to hand back a path chosen by the
corpus, so the search is not under test and does not need a graph seeded for
it. What is under test is the two nested double loops that turn one path into
next-hop entries for every prefix and suffix of it, in both directions, and the
asymmetric no-path exit that writes cost[from][to] TWICE and never touches
cost[to][from].

That asymmetry is exactly the kind of thing this catches: it is one line of C
that looks like a typo and is not.

The corpus enumerates path lengths 0 through 8 over a stride of 12, with the
path visiting distinct regions, plus the no-path answer and the inactive-target
refusal. Every case records the WHOLE next and cost matrices, so a single wrong
index anywhere fails.

WHAT IT DOES NOT CLOSE, and the line is a principled one. It checks the MODEL
below against the original, not region.cpp's C. tools/fireposevec.h can replay
its cases against the C because SelectFirePose's callees are all reconstructed
or pure; this one's are not -- RegionSolvePair calls the ORIGINAL RegionFindPath,
and giving the C the same hook would mean putting test scaffolding into
production code for a function the game never calls that way. So this has the
standing posecheck, moviecheck, boolcheck, ringcheck, shakecheck, explcheck and
collectcheck have, and the replay is available exactly when a function reaches
nothing that is still the image's.

    tools/regioncheck.py [--emit tests/regionvec.h]
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH, SCRATCH_SZ
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP

ADDR       = 0x00438300      # RegionSolvePair
FIND_PATH  = 0x00437E70      # hooked

G_REGIONS  = 0x00514EF0
G_STRIDE   = 0x00514EEC
G_NEXT     = 0x00514EF4
G_STAMP    = 0x00514EF8
G_COST     = 0x00514EFC

REGION_SIZE = 44
OFF_ACTIVE  = 4

STRIDE = 12
MATRIX = STRIDE * STRIDE

REGIONS = SCRATCH
NEXT    = SCRATCH + 0x1000
COST    = SCRATCH + 0x2000
PATHBUF = SCRATCH + 0x3000   # where the hook writes its path

CASES = [
    [],                       # no path at all
    [3],
    [3, 5],
    [1, 4, 7],
    [0, 2, 5, 9],
    [11, 8, 6, 3, 1],
    [0, 1, 2, 3, 4, 5],
    [9, 7, 5, 3, 1, 0, 2],
    [2, 4, 6, 8, 10, 11, 9, 7],
]


def run(emu, path, active, stamp):
    """One emulated RegionSolvePair, with RegionFindPath hooked."""
    uc = emu.uc
    buf = bytearray(SCRATCH_SZ)

    for i in range(STRIDE):
        struct.pack_into("<i", buf, REGIONS - SCRATCH + i * REGION_SIZE
                         + OFF_ACTIVE, 1)
    to = path[-1] if path else 1
    if not active:
        struct.pack_into("<i", buf, REGIONS - SCRATCH + to * REGION_SIZE
                         + OFF_ACTIVE, 0)

    # Distinguishable starting contents, so a cell the original leaves alone
    # is visibly different from one it writes.
    for i in range(MATRIX):
        buf[NEXT - SCRATCH + i] = 0xAA
        buf[COST - SCRATCH + i] = 0xBB
    for i, r in enumerate(path):
        struct.pack_into("<h", buf, PATHBUF - SCRATCH + i * 2, r)

    uc.mem_write(SCRATCH, bytes(buf))
    uc.mem_write(G_STRIDE, struct.pack("<h", STRIDE))
    uc.mem_write(G_NEXT, struct.pack("<I", NEXT))
    uc.mem_write(G_COST, struct.pack("<I", COST))
    uc.mem_write(G_STAMP, bytes([stamp]))
    uc.mem_write(G_REGIONS, struct.pack("<I", REGIONS))

    def on_find_path(u, address, size, _):
        """`int32(from, to, int16 *path, int32 *len)` -- answer the corpus."""
        sp = u.reg_read(UC_X86_REG_ESP)
        out, plen = struct.unpack("<II", u.mem_read(sp + 12, 8))
        if path:
            u.mem_write(out, bytes(u.mem_read(PATHBUF, len(path) * 2)))
            u.mem_write(plen, struct.pack("<i", len(path)))
        u.reg_write(UC_X86_REG_EAX, 1 if path else 0)
        u.reg_write(UC_X86_REG_EIP, struct.unpack("<I", u.mem_read(sp, 4))[0])
        u.reg_write(UC_X86_REG_ESP, sp + 4)

    h = uc.hook_add(0x1 << 2, on_find_path, begin=FIND_PATH, end=FIND_PATH)
    try:
        frm = path[0] if path else 0
        got, after = emu.call(ADDR, [frm, to], bytes(buf))
    finally:
        uc.hook_del(h)

    if after is None:
        return None
    return (bytes(after[NEXT - SCRATCH:NEXT - SCRATCH + MATRIX]),
            bytes(after[COST - SCRATCH:COST - SCRATCH + MATRIX]))


def model(path, active, stamp):
    """What region.cpp's RegionSolvePair does, in Python."""
    nxt  = bytearray([0xAA] * MATRIX)
    cost = bytearray([0xBB] * MATRIX)
    frm  = path[0] if path else 0
    to   = path[-1] if path else 1

    if not active:
        return bytes(nxt), bytes(cost)

    if not path:
        nxt[frm * STRIDE + to] = 0
        nxt[to * STRIDE + frm] = 0
        cost[frm * STRIDE + to] = stamp
        return bytes(nxt), bytes(cost)

    n = len(path)
    for i in range(1, n):
        for j in range(i, n):
            nxt[path[i - 1] * STRIDE + path[j]] = path[i] & 0xFF
            cost[path[i - 1] * STRIDE + path[j]] = stamp
    for i in range(n - 1, 0, -1):
        for j in range(i, 0, -1):
            nxt[path[i] * STRIDE + path[i - j]] = path[i - 1] & 0xFF
            cost[path[i] * STRIDE + path[i - j]] = stamp
    return bytes(nxt), bytes(cost)


def main():
    emu = Emu()
    rows = []
    bad = 0
    n = 0

    for stamp in (1, 0x5A):
        for active in (1, 0):
            for path in CASES:
                got = run(emu, path, active, stamp)
                want = model(path, active, stamp)
                n += 1
                if got is None:
                    bad += 1
                    print("  path=%s active=%d -> FAULTED" % (path, active))
                    continue
                if got != want:
                    bad += 1
                    print("  path=%s active=%d stamp=%d -> tables differ"
                          % (path, active, stamp))
                    continue
                rows.append((path, active, stamp, got[0], got[1]))

    print("%d case(s), %d disagree" % (n, bad))

    if not bad and "--emit" in sys.argv:
        emit(rows, sys.argv[sys.argv.index("--emit") + 1])
    return 1 if bad else 0


def emit(rows, path):
    with open(path, "w") as fh:
        fh.write("/* Generated by tools/regioncheck.py -- do not edit.\n"
                 " *\n"
                 " * One row per case: the path RegionFindPath was made to\n"
                 " * hand back, and the WHOLE next and cost matrices the\n"
                 " * ORIGINAL RegionSolvePair left behind. Cells it never\n"
                 " * touches keep 0xAA and 0xBB, so a stray write fails too.\n"
                 " */\n"
                 "#define AM2_REGION_TEST_STRIDE 12\n"
                 "typedef struct { int32_t len; int16_t path[8];\n"
                 "                 int32_t active; int32_t stamp;\n"
                 "                 unsigned char next[144];\n"
                 "                 unsigned char cost[144];\n"
                 "               } AM2_RegionVector;\n\n"
                 "static const AM2_RegionVector am2_region_vectors[] = {\n")
        for p, active, stamp, nxt, cost in rows:
            pad = list(p) + [0] * (8 - len(p))
            fh.write("  { %d, {%s}, %d, %d,\n    {%s},\n    {%s} },\n"
                     % (len(p), ",".join(str(x) for x in pad), active, stamp,
                        ",".join("0x%02x" % b for b in nxt),
                        ",".join("0x%02x" % b for b in cost)))
        fh.write("};\n")
    print("wrote %s, %d row(s)" % (path, len(rows)))


if __name__ == "__main__":
    sys.exit(main())
