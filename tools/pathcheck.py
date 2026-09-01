"""Check RegionFindPath's A* against the original, with no game running.

tools/regioncheck.py checks the layer ABOVE this one and says why it had to
stop here: RegionFindPath was still the image's, so it was hooked away and the
search itself went unchecked.  orig.h's warning about this neighbourhood --
"region routing feeds the AI, which no drive reaches, so a wrong next-hop table
would pass every configuration in tools/ab.sh" -- applies at least as much to
the search as to the table filling.  Nothing in tools/ab.sh can see either.

So this emulates the ORIGINAL 0x00437E70 over a seeded region graph and
compares its whole answer -- the return value, the length and every entry of
the path -- against a Python model of what region.cpp now does.

WHAT HAS TO BE SEEDED, and each of these was a way to get nothing.  The region
table and its link arrays; ADDR_MAP_TILES_W and ADDR_MAP_ROW_SHIFT, because
every heuristic goes through them to turn a tile index into x and y;
ADDR_REGION_GENERATION, which must MOVE between cases or the second case reads
the first's stamps and treats every node as already visited; and
ADDR_REGION_SEARCH_STATE, which selects the dead resume arm when it is not -1.

The corpus is graphs rather than arguments, because a pathfinder's input is its
graph: a chain, a grid with two equal-cost routes, a diamond where the
inadmissible 1.5 weighting can pick the longer arm, a disconnected pair, an
inactive target, an inactive node in the middle of the only route, from == to,
and a chain long enough to reach AM2_REGION_DEPTH_MAX.

WHAT THE CORPUS CATCHES, measured by mutating the model and re-running:

    correct the head-unlink (see model)      4 of 14
    max + min/2 instead of max + ceil(min/2) 4
    drop the 1.5 heuristic weight            5
    step weight 1 instead of 2               2
    insert after equal-f rather than before  3
    no AM2_REGION_DEPTH_MAX cap              1
    reopen closed nodes                      hangs rather than fails

The first two are not hypotheticals: they are the two errors this found on its
first runs, one in the C's reading of ApproxDistXY and one in the model.

WHAT IT DOES NOT CATCH, and the reason is better than the gap. Recomputing h
on the improvement arm -- the asymmetry region.cpp used to write out as a
deliberate one -- changes nothing on any case, and cannot: h is a function of
the node and the GOAL, both fixed for the length of a search, so the value
recomputed is always the value already there. That is not a corpus gap; the
term is redundant by construction.

Not emitted as vectors yet. A C replay would have to seed ADDR_REGIONS and the
two map globals through the image slide before calling in, which is more
scaffolding than the other replays need; this has the standing regioncheck,
posecheck and moviecheck have until then.

    tools/pathcheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH, SCRATCH_SZ

ADDR = 0x00437E70

G_REGIONS    = 0x00514EF0
G_TILES_W    = 0x00514DDC
G_ROW_SHIFT  = 0x00514DE4
G_GENERATION = 0x00654C2C
G_SEARCH     = 0x00487824
G_GOAL       = 0x00523DC0
G_OPEN       = 0x00523DC4

REGION_SIZE = 44
LINK_SIZE   = 6

OFF_ID, OFF_TILE, OFF_ACTIVE, OFF_NLINKS, OFF_LINKS = 0, 2, 4, 8, 0x0C
OFF_G, OFF_H, OFF_DEPTH, OFF_STAMP, OFF_STATE = 0x10, 0x14, 0x18, 0x1C, 0x1E
OFF_PARENT, OFF_PREV, OFF_NEXT = 0x20, 0x24, 0x28

TILES_W   = 16
ROW_SHIFT = 4
WEIGHT    = 2          # AM2_REGION_STEP_WEIGHT
DEPTH_MAX = 0xFE       # AM2_REGION_DEPTH_MAX

# 256 regions * 44 bytes is 0x2C00, so the link arrays cannot start at
# 0x2000 -- the first version had them overlapping and the long chains faulted.
REGIONS = SCRATCH
LINKS   = SCRATCH + 0x3000
PATHOUT = SCRATCH + 0x6000
LENOUT  = SCRATCH + 0x7000


def approx_dist(dx, dy):
    """ApproxDistXY, which the original calls and dist.cpp reconstructs.

    `dx + dy - (min >> 1)` on the absolute values.  Written as "max + min/2"
    in prose everywhere, which is NOT the same function: max + min - min/2 is
    max + CEIL(min/2), so the two differ for every odd min.  Modelled as
    max + min/2 here first, and it disagreed with the original on 40 of 81
    small deltas -- and on four of the grid graphs below, where it moved
    enough ties to pick a different route of the same length."""
    a, b = abs(dx), abs(dy)
    lo = a if a < b else b
    return a + b - (lo >> 1)


def span(g, a, b):
    ax, ay = g[a]["tile"] & (TILES_W - 1), g[a]["tile"] >> ROW_SHIFT
    bx, by = g[b]["tile"] & (TILES_W - 1), g[b]["tile"] >> ROW_SHIFT
    return approx_dist(bx - ax, by - ay)


def model(g, frm, to):
    """What region.cpp's RegionFindPath does, in Python.

    The open list is kept as explicit prev/next links rather than a Python
    list, and that is not fussiness.  The original's unlink-on-improvement
    writes `OPEN_HEAD = 0` when the node being improved is the HEAD, instead of
    `OPEN_HEAD = node->next` -- so improving the head DISCARDS every other node
    on the open list.  A model that used list.remove() would be modelling a
    correct A*, and the first version did: it disagreed with the original on
    four of the five grids, which is how the defect was found.  region.cpp
    reproduces it; so does this.
    """
    if not g[to]["active"] or not g[frm]["active"]:
        return 0, None

    st = {i: {"g": 0, "h": 0, "depth": 0, "state": 0, "parent": None,
              "prev": None, "next": None, "stamp": 0} for i in range(len(g))}
    gen = 1
    head = [None]

    def unlink(n):
        p, nx = st[n]["prev"], st[n]["next"]
        if p is not None:
            st[p]["next"] = nx
        else:
            head[0] = None          # the original's, not a correct unlink
        if nx is not None:
            st[nx]["prev"] = p

    def insert(n):
        f = st[n]["g"] + st[n]["h"]
        if head[0] is None:
            head[0] = n
            st[n]["next"] = None
            st[n]["prev"] = None
            return
        prev = None
        walk = head[0]
        while walk is not None:
            if st[walk]["h"] + st[walk]["g"] >= f:
                break
            prev = walk
            walk = st[walk]["next"]
        if prev is not None:
            after = st[prev]["next"]
            st[n]["next"] = after
            if after is not None:
                st[after]["prev"] = n
            st[prev]["next"] = n
            st[n]["prev"] = prev
        else:
            old = head[0]
            st[n]["next"] = old
            st[n]["prev"] = None
            st[old]["prev"] = n
            head[0] = n

    st[frm].update(g=0, h=int(span(g, frm, to) * 1.5), depth=0, state=1,
                   parent=None, prev=None, next=None, stamp=gen)
    head[0] = frm

    while head[0] is not None:
        cur = head[0]
        nxt = st[cur]["next"]
        head[0] = nxt
        if nxt is not None:
            st[nxt]["prev"] = None
        st[cur]["state"] = 2

        if cur == to:
            if st[cur]["depth"] >= DEPTH_MAX:
                return 0, None
            path = []
            walk = cur
            while walk is not None:
                path.append(walk)
                walk = st[walk]["parent"]
            path.reverse()
            return 1, path

        for nb in g[cur]["links"]:
            if not g[nb]["active"]:
                continue
            gg = st[cur]["g"] + span(g, cur, nb) * WEIGHT
            hh = int(span(g, nb, to) * 1.5)

            if st[nb]["stamp"] != gen:
                st[nb].update(stamp=gen, g=gg, h=hh, parent=cur,
                              depth=st[cur]["depth"] + 1, state=1)
            elif st[nb]["state"] & 1:
                if gg >= st[nb]["g"]:
                    continue
                st[nb].update(g=gg, parent=cur, depth=st[cur]["depth"] + 1)
                unlink(nb)
            elif st[nb]["state"] & 2:
                continue

            insert(nb)

    return 0, None


def build(g):
    """The region table and its link arrays, as bytes in the scratch."""
    buf = bytearray(SCRATCH_SZ)
    linkat = LINKS
    for i, r in enumerate(g):
        base = REGIONS - SCRATCH + i * REGION_SIZE
        struct.pack_into("<h", buf, base + OFF_ID, i)
        struct.pack_into("<h", buf, base + OFF_TILE, r["tile"])
        struct.pack_into("<i", buf, base + OFF_ACTIVE, 1 if r["active"] else 0)
        buf[base + OFF_NLINKS] = len(r["links"])
        struct.pack_into("<I", buf, base + OFF_LINKS, linkat)
        for k, nb in enumerate(r["links"]):
            struct.pack_into("<h", buf, linkat - SCRATCH + k * LINK_SIZE, nb)
        linkat += max(1, len(r["links"])) * LINK_SIZE
    return buf


def run(emu, gen, g, frm, to):
    uc = emu.uc
    buf = build(g)
    uc.mem_write(G_REGIONS, struct.pack("<I", REGIONS))
    uc.mem_write(G_TILES_W, struct.pack("<i", TILES_W))
    uc.mem_write(G_ROW_SHIFT, struct.pack("<i", ROW_SHIFT))
    uc.mem_write(G_GENERATION, struct.pack("<i", gen))
    uc.mem_write(G_SEARCH, struct.pack("<i", -1))
    uc.mem_write(G_GOAL, struct.pack("<I", 0))
    uc.mem_write(G_OPEN, struct.pack("<I", 0))

    got, after = emu.call(ADDR, [frm, to, PATHOUT, LENOUT], bytes(buf),
                          count=4000000)
    if after is None:
        return None
    if got == 0:
        return (0, None)
    n = struct.unpack_from("<i", after, LENOUT - SCRATCH)[0]
    if n < 0 or n > 256:
        return (1, "length %d" % n)
    return (1, [struct.unpack_from("<h", after, PATHOUT - SCRATCH + i * 2)[0]
                for i in range(n)])


def R(tile, links, active=1):
    return {"tile": tile, "links": links, "active": active}


def chain(n, active_at=None):
    g = []
    for i in range(n):
        links = [j for j in (i - 1, i + 1) if 0 <= j < n]
        g.append(R((i % TILES_W) | ((i // TILES_W) << ROW_SHIFT), links,
                   0 if i == active_at else 1))
    return g


def grid(w, h):
    g = []
    for y in range(h):
        for x in range(w):
            i = y * w + x
            links = []
            if x:         links.append(i - 1)
            if x < w - 1: links.append(i + 1)
            if y:         links.append(i - w)
            if y < h - 1: links.append(i + w)
            g.append(R(x | (y << ROW_SHIFT), links))
    return g


def cases():
    yield "chain-8", chain(8), 0, 7
    yield "chain-8-back", chain(8), 7, 0
    yield "chain-8-self", chain(8), 3, 3
    yield "chain-8-cut", chain(8, active_at=4), 0, 7
    yield "chain-8-goal-off", chain(8, active_at=7), 0, 7
    yield "chain-8-start-off", chain(8, active_at=0), 0, 7
    yield "grid-4x4", grid(4, 4), 0, 15
    yield "grid-4x4-corner", grid(4, 4), 3, 12
    yield "grid-5x5", grid(5, 5), 0, 24
    yield "grid-6x4", grid(6, 4), 5, 18
    # A diamond whose two arms have different hop counts but similar spans,
    # which is where the 1.5 weighting can prefer the longer one.
    yield "diamond", [R(0x00, [1, 2]), R(0x01, [0, 3]), R(0x20, [0, 4]),
                      R(0x12, [1, 5]), R(0x30, [2, 5]), R(0x33, [3, 4])], 0, 5
    # Two components: nothing joins 0..2 to 3..5.
    yield "split", [R(0x00, [1]), R(0x01, [0, 2]), R(0x02, [1]),
                    R(0x40, [4]), R(0x41, [3, 5]), R(0x42, [4])], 0, 5
    yield "chain-250", chain(250), 0, 249
    yield "chain-256", chain(256), 0, 255


def main():
    emu = Emu()
    rows = []
    bad = 0
    n = 0
    gen = 1

    for name, g, frm, to in cases():
        gen += 1
        got = run(emu, gen, g, frm, to)
        want = model(g, frm, to)
        n += 1
        if got is None:
            bad += 1
            print("  %-18s -> FAULTED" % name)
            continue
        if got != want:
            bad += 1
            print("  %-18s -> original %s, model %s" % (name, got, want))
            continue
        rows.append((name, len(g), frm, to,
                     [r["tile"] for r in g],
                     [r["active"] for r in g],
                     [r["links"] for r in g],
                     got))

    print("%d case(s), %d disagree" % (n, bad))

    if not bad and "--emit" in sys.argv:
        emit(rows, sys.argv[sys.argv.index("--emit") + 1])
    return 1 if bad else 0


def emit(rows, path):
    with open(path, "w") as fh:
        fh.write("/* Generated by tools/pathcheck.py -- do not edit.\n"
                 " *\n"
                 " * One row per graph: the region table to seed, and the\n"
                 " * WHOLE answer the ORIGINAL RegionFindPath gave for it --\n"
                 " * return value, length and every path entry.\n"
                 " */\n"
                 "#define AM2_PATH_MAX_REGIONS 256\n"
                 "#define AM2_PATH_MAX_LINKS   4\n"
                 "typedef struct {\n"
                 "    const char *name;\n"
                 "    int32_t     count, from, to, ret, len;\n"
                 "    int16_t     tile[AM2_PATH_MAX_REGIONS];\n"
                 "    int8_t      active[AM2_PATH_MAX_REGIONS];\n"
                 "    int8_t      nlinks[AM2_PATH_MAX_REGIONS];\n"
                 "    int16_t     link[AM2_PATH_MAX_REGIONS]"
                 "[AM2_PATH_MAX_LINKS];\n"
                 "    int16_t     path[AM2_PATH_MAX_REGIONS];\n"
                 "} AM2_PathVector;\n\n"
                 "static const AM2_PathVector am2_path_vectors[] = {\n")
        for name, cnt, frm, to, tiles, act, links, (ret, p) in rows:
            p = p or []
            fh.write("  { \"%s\", %d, %d, %d, %d, %d,\n" %
                     (name, cnt, frm, to, ret, len(p)))
            fh.write("    {%s},\n" % ",".join(str(t) for t in tiles))
            fh.write("    {%s},\n" % ",".join(str(a) for a in act))
            fh.write("    {%s},\n" % ",".join(str(len(l)) for l in links))
            fh.write("    {%s},\n" %
                     ",".join("{%s}" % ",".join(str(x) for x in
                                                (l + [0] * 4)[:4])
                              for l in links))
            fh.write("    {%s} },\n" % ",".join(str(x) for x in p))
        fh.write("};\n")
    print("wrote %s, %d row(s)" % (path, len(rows)))


if __name__ == "__main__":
    sys.exit(main())
