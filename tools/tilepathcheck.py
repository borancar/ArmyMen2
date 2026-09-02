"""Check FindPath's tile-level A* against the original, with no game running.

tools/pathcheck.py does this for the layer ABOVE -- RegionFindPath, over a
region graph -- and everything it says about why applies here twice over.
FindPath's one caller is PlanPathTo, which CLAUDE.md records as unexercised;
now that the seam is closed its counter is blind as well, so no configuration
in tools/ab.sh can see this function run, let alone see it answer differently.
This tool is its verification or there is none.

IT IS THE SAME ALGORITHM AND SO THIS IS ALMOST THE SAME TOOL, which is the
finding rather than a coincidence: the node carries the same eight fields in
the same order, the heuristic is the same ApproxDistXY * 1.5, and the open list
has the same unlink-the-head defect.  What differs is that the node array is a
1 MB table in .data indexed by TILE, so the graph is seeded as arrays rather
than as structs, and `g` and every tile id are sixteen bits.

WHAT HAS TO BE HOOKED, and each of these was a way to get nothing:

  Ticks (0x00426CD0) is patched to `xor eax,eax; ret`.  Left alone it finds a
  zero performance frequency and falls through to GetTickCount, which is an
  import and faults.  A frozen clock is also what makes the run deterministic:
  elapsed is 0, which is below AM2_PATH_MIN_ELAPSED, so the adaptive node
  budget never moves and case N+1 starts where case N did.

  ADDR_POINT_RULE is a FUNCTION POINTER, so refusing a tile needs code.  Six
  bytes of it, indexing a byte array this file owns -- which is better than
  seeding impassable terrain, because the rule is the only refusal the original
  consults for arbitrary tiles and a stub that always allows would leave it
  entirely unchecked.

  ADDR_TILE_STEP8 must hold the eight neighbour deltas for the seeded width.
  It is built at map load by BuildTileDeltas and is zeros in the image, and
  eight zero deltas make every neighbour the current tile -- which does not
  fault, does not hang, and quietly answers "no route" for every case.

  ADDR_PATH_GENERATION need NOT be seeded, unlike its region counterpart: this
  function increments it itself.  It must not be RESET between cases, though,
  or the second case reads the first's stamps.

THE NODE ARRAY IS NOT SEEDED AT ALL and that is deliberate.  It is a megabyte
of .data the image already carries as zeros, and the generation counter is what
makes stale entries invisible.  Zeroing it between cases would hide exactly the
bug that mechanism exists to prevent.

A NEIGHBOUR CAN WRAP AROUND A ROW and the original does not care: it checks
only `0 < nb < 0x10000`, never that the column stayed adjacent, so stepping
right from the last column lands on the next row's first tile.  The model
reproduces that rather than clipping, and the `wrap` case exists to pin it.

WHAT THE CORPUS CATCHES, measured by mutating the model and re-running. The
counts are the useful part -- a mutation that fails everything says less than
one that fails exactly the cases built to reach it:

    invert a penalty's polarity                10 of 24
    insert after equal f rather than before    10
    drop the 1.5 heuristic weight               9
    step weight 1 instead of 2                  7
    drop the NO_WEIGHT_NEAR penalty             6
    drop the LITTLE_COVER_NEAR penalty          2
    drop the region shortcut                    2
    swap the two penalty constants              1
    correct the head-unlink                     1

THE LAST TWO ARE THE POINT OF THE LAST TWO CASES, and both were zero until
those cases existed. Neither gap was found by mutating -- mutating only said
"no difference", which reads exactly like a redundant term.

The penalties were invisible because a single penalised tile costs 3 where
going round it costs two diagonal steps, which is 4: the route never moved, so
`g` never reached the output. A RUN of penalised tiles fixes that, and telling
the two constants APART needs a case where two routes are priced by different
flags -- `two-gaps`, whose two openings are symmetric so geometry cannot
choose.

The head-unlink needed counting rather than mutating. Across every other case
the open list is improved 49 times and NOT ONCE is the node being improved the
HEAD, so the defect that is the most interesting thing about this function was
entirely unexercised. Instrumenting the branch said so in one run. Then even a
map that REACHES it was not enough -- `rough` takes it once and the search ends
before the discarded nodes would have mattered -- so `rough-head` was searched
for on the only criterion that makes the mutation fail: the model with the
defect and the model without give different routes. Reaching a branch is not
observing it.

WHAT IT DOES NOT REACH, said plainly. The adaptive node budget never runs: the
clock is frozen so elapsed is 0, which is below AM2_PATH_MIN_ELAPSED. Nor does
the give-up arm, since no case approaches 10,000 expansions. Nor the dead
resume arm, which nothing can reach. Those three stay verified by reading.

    tools/tilepathcheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH, SCRATCH_SZ

ADDR = 0x004395B0

G_CLOCK      = 0x00511E04
G_FRAME_MS   = 0x00654C34
G_FRAME_ST   = 0x00654C38
G_MAX_SRCH   = 0x00487890
G_MAX_NODES  = 0x0048788C
G_RESUME     = 0x00487828
G_TILES_W    = 0x00514DDC
G_ROW_SHIFT  = 0x00514DE4
G_REGION_OF  = 0x00514ECC
G_TILE_FLAGS = 0x00514ED0
G_STEP8      = 0x00523DA0
G_RULE       = 0x00523DDC
G_NODES      = 0x00554BD8
A_TICKS      = 0x00426CD0

OFF_G, OFF_H, OFF_DEPTH, OFF_STAMP = 0, 2, 4, 6
OFF_PARENT, OFF_PREV, OFF_NEXT, OFF_STATE = 8, 0x0A, 0x0C, 0x0E

TILES_W    = 16
ROW_SHIFT  = 4
NO_WEIGHT  = 0x04       # AM2_TILE_NO_WEIGHT_NEAR
LITTLE_COV = 0x08       # AM2_TILE_LITTLE_COVER_NEAR

REGION_OF = SCRATCH
TILE_FLAG = SCRATCH + 0x0400
BLOCKED   = SCRATCH + 0x0800
RULE_CODE = SCRATCH + 0x0C00
PATHOUT   = SCRATCH + 0x1000
LENOUT    = SCRATCH + 0x2000
ARRAY_N   = 0x400

# mov eax,[esp+4] ; movzx eax, byte [eax + BLOCKED] ; ret
RULE_STUB = b"\x8b\x44\x24\x04" + b"\x0f\xb6\x80" + struct.pack("<I", BLOCKED) \
            + b"\xc3"


def approx_dist(dx, dy):
    """ApproxDistXY: `dx + dy - (min >> 1)` on the absolute values, which is
    max + CEIL(min/2) and NOT the "max + min/2" the prose used to say. That
    error cost pathcheck.py a run; it is written out here rather than imported
    so this file states its own model."""
    a, b = abs(dx), abs(dy)
    lo = a if a < b else b
    return a + b - (lo >> 1)


def dist(a, b):
    return approx_dist((b & (TILES_W - 1)) - (a & (TILES_W - 1)),
                       (b >> ROW_SHIFT) - (a >> ROW_SHIFT))


def heur(a, b):
    return int(dist(a, b) * 1.5)


class Map(object):
    """The seeded world: a region per tile, flags per tile, blocked per tile."""

    def __init__(self, n=ARRAY_N):
        self.region = [0] * n
        self.flags = [NO_WEIGHT | LITTLE_COV] * n      # no penalty by default
        self.blocked = [0] * n


def model(m, frm, to):
    """What region.cpp's FindPath does, in Python.

    The open list is kept as explicit prev/next links for the same reason
    pathcheck.py keeps them: the original's unlink-on-improvement writes
    head = 0 when the node being improved IS the head, discarding everything
    behind it. A model built on list.remove() would be modelling a correct A*.
    """
    if not (frm & 0xFFFF) or not (to & 0xFFFF):
        return 0, None
    frm &= 0xFFFF
    to &= 0xFFFF

    st = {}

    def node(i):
        if i not in st:
            st[i] = {"g": 0, "h": 0, "depth": 0, "state": 0, "parent": 0,
                     "prev": 0, "next": 0, "seen": False}
        return st[i]

    start_region = m.region[frm]
    goal_region = m.region[to]

    s = node(frm)
    s.update(g=0, h=heur(frm, to), depth=0, state=1, prev=0, next=0, parent=0,
             seen=True)
    head = [frm]

    def unlink(n):
        p, nx = st[n]["prev"], st[n]["next"]
        if p:
            st[p]["next"] = nx
        else:
            head[0] = 0                 # the original's, not a correct unlink
        if nx:
            st[nx]["prev"] = p

    def insert(n):
        f = st[n]["g"] + st[n]["h"]
        if not head[0]:
            head[0] = n
            st[n]["next"] = 0
            st[n]["prev"] = 0
            return
        before = 0
        walk = head[0]
        while True:
            q = node(walk)
            if q["g"] + q["h"] >= f:
                break
            before = walk
            walk = q["next"]
            if not walk:
                break
        if not before:
            st[n]["next"] = head[0]
            st[n]["prev"] = 0
            node(head[0])["prev"] = n
            head[0] = n
        else:
            after = st[before]["next"]
            st[n]["next"] = after
            if after:
                node(after)["prev"] = n
            st[before]["next"] = n
            st[n]["prev"] = before

    considered = 0
    steps = [-1 - TILES_W, -TILES_W, 1 - TILES_W, -1,
             1, TILES_W + 1, TILES_W, TILES_W - 1]

    def unwind(end):
        depth = st[end]["depth"]
        out = [0] * (depth + 1)
        i = depth
        while end:
            out[i] = end
            end = st[end]["parent"]
            i -= 1
        return depth + 1, out

    while head[0]:
        cur = head[0]
        c = node(cur)
        head[0] = c["next"]
        if head[0]:
            node(head[0])["prev"] = 0
        c["state"] = 2

        if considered > 10000:
            if heur(cur, to) <= heur(frm, to):
                return 1, unwind(cur)[1]
            return 0, None
        if cur == to:
            return 1, unwind(cur)[1]
        if goal_region > 0 and start_region != goal_region \
                and m.region[cur] == goal_region:
            return 1, unwind(cur)[1]

        considered += 8
        for d in steps:
            nb = cur + d
            if nb <= 0 or nb >= 0x10000:
                continue
            if m.blocked[nb]:
                continue
            r = m.region[nb]
            if r != 0 and r != start_region and r != goal_region:
                continue

            g = dist(cur, nb) * 2
            g = (g & 0xFFFF0000) | ((g + c["g"]) & 0xFFFF)
            f = m.flags[nb]
            if not (f & NO_WEIGHT):
                g += 3
            if not (f & LITTLE_COV):
                g += 1
            h = heur(nb, to)

            b = node(nb)
            if not b["seen"]:
                b.update(seen=True, g=g & 0xFFFF, h=h & 0xFFFF, parent=cur,
                         depth=(c["depth"] + 1) & 0xFFFF, state=1)
            elif not (b["state"] & 1):
                if b["state"] & 2:
                    continue
            else:
                if (g & 0xFFFF) >= b["g"]:
                    continue
                b.update(parent=cur, g=g & 0xFFFF,
                         depth=(c["depth"] + 1) & 0xFFFF)
                unlink(nb)
            insert(nb)

    return 0, None


def seed(emu, m):
    uc = emu.uc
    uc.mem_write(G_TILES_W, struct.pack("<i", TILES_W))
    uc.mem_write(G_ROW_SHIFT, struct.pack("<i", ROW_SHIFT))
    uc.mem_write(G_REGION_OF, struct.pack("<I", REGION_OF))
    uc.mem_write(G_TILE_FLAGS, struct.pack("<I", TILE_FLAG))
    uc.mem_write(G_RULE, struct.pack("<I", RULE_CODE))
    uc.mem_write(G_RESUME, struct.pack("<i", -1))
    uc.mem_write(G_MAX_SRCH, struct.pack("<i", 10))
    uc.mem_write(G_MAX_NODES, struct.pack("<i", 10000))
    # A clock that never matches the stamp, so the per-frame charge resets
    # every case and no case can refuse because of the one before it.
    uc.mem_write(G_CLOCK, struct.pack("<i", 1))
    uc.mem_write(G_FRAME_ST, struct.pack("<i", 0))
    uc.mem_write(G_FRAME_MS, struct.pack("<i", 0))
    uc.mem_write(G_STEP8, struct.pack("<8i",
                                      -1 - TILES_W, -TILES_W, 1 - TILES_W, -1,
                                      1, TILES_W + 1, TILES_W, TILES_W - 1))
    uc.mem_write(A_TICKS, b"\x31\xc0\xc3")


def scratch_for(m):
    buf = bytearray(SCRATCH_SZ)
    for i in range(ARRAY_N):
        buf[REGION_OF - SCRATCH + i] = m.region[i] & 0xFF
        buf[TILE_FLAG - SCRATCH + i] = m.flags[i] & 0xFF
        buf[BLOCKED - SCRATCH + i] = m.blocked[i] & 0xFF
    buf[RULE_CODE - SCRATCH:RULE_CODE - SCRATCH + len(RULE_STUB)] = RULE_STUB
    return bytes(buf)


def run(emu, m, frm, to):
    seed(emu, m)
    got, after = emu.call(ADDR, [frm, to, PATHOUT, LENOUT, 0],
                          scratch_for(m), count=8000000)
    if after is None:
        return None
    if got == 0:
        return (0, None)
    n = struct.unpack_from("<i", after, LENOUT - SCRATCH)[0]
    if n < 0 or n > 512:
        return (1, "length %d" % n)
    return (1, [struct.unpack_from("<H", after, PATHOUT - SCRATCH + i * 2)[0]
                for i in range(n)])


def open_map(regions=0):
    m = Map()
    for i in range(ARRAY_N):
        m.region[i] = regions
    return m


def walled(cols):
    """Every tile in `cols` refused by the point rule, on every row."""
    m = open_map()
    for t in range(ARRAY_N):
        if (t & (TILES_W - 1)) in cols:
            m.blocked[t] = 1
    return m


def band(bits, cols, row=1):
    """Clear `bits` on a run of tiles in one row, so crossing them costs."""
    m = open_map()
    for c in cols:
        m.flags[(row << ROW_SHIFT) | c] &= ~bits
    return m


def cases():
    yield "straight", open_map(), 0x11, 0x15
    yield "diagonal", open_map(), 0x11, 0x55
    yield "back", open_map(), 0x55, 0x11
    yield "self", open_map(), 0x33, 0x33
    yield "from-zero", open_map(), 0x00, 0x33
    yield "to-zero", open_map(), 0x33, 0x00
    # A wall down column 4 with a gap the search has to find.
    m = walled([4])
    m.blocked[0x74] = 0
    yield "wall-gap", m, 0x11, 0x18
    # And no gap at all, so the open list drains.
    yield "wall-solid", walled([4]), 0x11, 0x18

    # THE PENALTIES HAVE TO BE WORTH A DETOUR OR THEY ARE NOT OBSERVABLE.
    # The first version penalised a single column and no mutation to either
    # term changed any case: one penalised tile costs 3 where going round it
    # costs two diagonal steps, which is 4 more. So the route never moved and
    # `g` never reaches the output. A RUN of penalised tiles is what flips it --
    # eight tiles at +3 is 24 against a fixed 8 for leaving the row and coming
    # back. Measured by mutating, not reasoned: see the header.
    yield "weight-band", band(NO_WEIGHT, range(2, 10)), 0x11, 0x1A
    yield "cover-band", band(LITTLE_COV, range(2, 10)), 0x11, 0x1A
    yield "both-band", band(NO_WEIGHT | LITTLE_COV, range(2, 10)), 0x11, 0x1A
    # A band of each, adjacent, so swapping the two costs tells them apart.
    m = band(NO_WEIGHT, range(2, 6))
    for c in range(6, 10):
        m.flags[(1 << ROW_SHIFT) | c] &= ~LITTLE_COV
    yield "mixed-band", m, 0x11, 0x1A
    # Penalties on the row BELOW as well, so neither row is free and the
    # search improves nodes it has already opened.
    m = band(NO_WEIGHT, range(2, 10))
    for c in range(3, 9):
        m.flags[(2 << ROW_SHIFT) | c] &= ~NO_WEIGHT
    yield "two-bands", m, 0x11, 0x1A
    # A pocket: cheap ground reachable only through expensive ground, which
    # is where a node already on the open list gets a better parent.
    m = walled([5])
    for c in (2, 3, 4):
        for r in (1, 2, 3):
            m.flags[(r << ROW_SHIFT) | c] &= ~(NO_WEIGHT | LITTLE_COV)
    m.blocked[0x35] = 0
    yield "pocket", m, 0x11, 0x18

    # Regions. Start and goal in different ones, so arriving ANYWHERE in the
    # goal's region ends the search -- and a third region the search may not
    # enter at all.
    m = open_map(1)
    for t in range(ARRAY_N):
        if (t & (TILES_W - 1)) >= 8:
            m.region[t] = 2
        elif (t >> ROW_SHIFT) >= 8:
            m.region[t] = 3
    yield "regions", m, 0x11, 0x1A
    yield "regions-back", m, 0x1A, 0x11
    # Same region both ends: the shortcut must not fire.
    yield "one-region", open_map(1), 0x11, 0x15
    # Region 0 is exempt from the corridor test, which nothing else reaches.
    m = open_map(1)
    for t in range(ARRAY_N):
        if (t >> ROW_SHIFT) == 2:
            m.region[t] = 0
    yield "region-zero", m, 0x11, 0x18

    # Row wrap: stepping right off the last column lands on the next row, and
    # the original never checks the column stayed adjacent.
    yield "wrap", open_map(), 0x1F, 0x20
    yield "wrap-back", open_map(), 0x20, 0x1F
    # Long enough that the open list carries real length.
    yield "long", open_map(), 0x01, 0xEE

    # TWO GAPS IN ONE WALL, priced with DIFFERENT flags, which is the only
    # thing here that can tell the two penalties apart. They are symmetric
    # about the start's row, so geometry does not choose: the row-2 gap costs 3
    # through NO_WEIGHT and the row-4 pair costs 2 through LITTLE_COVER, so the
    # search takes row 4. Swap the two constants and row 2 costs 1 while row 4
    # costs 6, and it takes the other gap. Without this case, swapping them
    # changes nothing anywhere -- measured, which is why it exists.
    m = walled([5])
    m.blocked[0x25] = 0
    m.blocked[0x45] = 0
    m.flags[0x25] &= ~NO_WEIGHT
    m.flags[0x45] &= ~LITTLE_COV
    m.flags[0x44] &= ~LITTLE_COV
    yield "two-gaps", m, 0x31, 0x38

    # ROUGH GROUND, and this one is here for a single branch. Across every
    # case above, the open list is improved 49 times and NOT ONCE is the node
    # being improved the HEAD -- so the unlink-the-head defect, which is the
    # most interesting thing about this function, went entirely unexercised
    # and correcting it in the model changed nothing. That was measured by
    # counting the branch rather than by mutating, which only said "no
    # difference". This map came out of a randomised search for one that
    # reaches it; the tiles are written out rather than kept as a seed so the
    # case cannot move under anyone's random number generator.
    m = open_map()
    for t in (0x14, 0x16, 0x25, 0x2C, 0x32, 0x33, 0x35, 0x38, 0x3A, 0x42,
              0x46, 0x54, 0x57, 0x5A, 0x62, 0x69, 0x6B, 0x73, 0x75, 0x81):
        m.blocked[t] = 1
    for t in (0x1A, 0x1B, 0x21, 0x23, 0x24, 0x28, 0x34, 0x36, 0x44, 0x4B,
              0x58, 0x5C, 0x65, 0x66, 0x68, 0x6A, 0x78, 0x84, 0x86, 0x89,
              0x8B):
        m.flags[t] &= ~NO_WEIGHT
    for t in (0x12, 0x17, 0x18, 0x1C, 0x27, 0x2B, 0x31, 0x37, 0x41, 0x43,
              0x51, 0x63, 0x67, 0x76, 0x7A, 0x7B, 0x88):
        m.flags[t] &= ~LITTLE_COV
    yield "rough", m, 0x11, 0x8C

    # AND ONE WHERE CORRECTING THE DEFECT CHANGES THE ANSWER, which `rough`
    # does not. Reaching the head-unlink branch is not the same as observing
    # it: `rough` takes it once and the search finishes before the discarded
    # nodes would have mattered, so correcting the model still agreed on every
    # case. This map was searched for on the right criterion -- the model with
    # the defect and the model without give different routes -- which is the
    # only criterion that makes the mutation fail. Same tiles, written out.
    m = open_map()
    for t in (0x13, 0x15, 0x19, 0x1A, 0x1C, 0x22, 0x23, 0x24, 0x44, 0x46,
              0x52, 0x5A, 0x5D, 0x62, 0x69, 0x6D, 0x76, 0x87, 0x89, 0x8B,
              0x8C, 0x8D, 0x91):
        m.blocked[t] = 1
    for t in (0x14, 0x16, 0x1D, 0x21, 0x25, 0x28, 0x2A, 0x2C, 0x31, 0x37,
              0x3D, 0x47, 0x48, 0x4A, 0x4B, 0x4D, 0x51, 0x53, 0x5B, 0x64,
              0x67, 0x6B, 0x6C, 0x72, 0x7A, 0x7D, 0x83, 0x85, 0x88, 0x93,
              0x94, 0x9A, 0x9C):
        m.flags[t] &= ~NO_WEIGHT
    for t in (0x11, 0x12, 0x27, 0x2B, 0x33, 0x35, 0x39, 0x3A, 0x3B, 0x3C,
              0x42, 0x43, 0x45, 0x49, 0x4C, 0x54, 0x55, 0x56, 0x71, 0x75,
              0x79, 0x7B, 0x82, 0x86, 0x92, 0x97, 0x9D):
        m.flags[t] &= ~LITTLE_COV
    yield "rough-head", m, 0x11, 0x9D


def main():
    emu = Emu()
    bad = 0
    n = 0

    for name, m, frm, to in cases():
        got = run(emu, m, frm, to)
        want = model(m, frm, to)
        n += 1
        if got is None:
            bad += 1
            print("  %-16s -> FAULTED" % name)
            continue
        if got != want:
            bad += 1
            print("  %-16s -> original %r, model %r" % (name, got, want))

    print("tilepathcheck: %d cases, %d disagree" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
