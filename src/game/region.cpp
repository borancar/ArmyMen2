/* region.cpp -- see region.h. */
#include <stdint.h>

#include "region.h"
#include "objtype.h"  /* ObjIsType3, ObjIsType8 */
#include "map.h"      /* TileOfPoint -- reconstructed */
#include "image.h"
#include "crt.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kRegionOfCell (*(uint8_t **)AM2_IMAGE(ADDR_REGION_OF_CELL))
/* 0x00514EF0 holds a POINTER to the array, not the array. Same shape as the
 * cell map above; getting this wrong took the game down on the first run. */
#define kRegions      (*(uint8_t **)AM2_IMAGE(ADDR_REGIONS))
#define kTracePF      (*(const int32_t *)AM2_IMAGE(ADDR_OPT_TRACE_PF))

#define kNLinks(r)  (*(uint8_t *)(kRegions + (r) * AM2_REGION_SIZE \
                                 + REGION_OFF_NLINKS))
#define kLinks(r)   (*(AM2_RegionLink **)(kRegions + (r) * AM2_REGION_SIZE \
                                          + REGION_OFF_LINKS))

/* 0x0042B860. Record that two cells connect their regions.
 *
 * Both cells are looked up in the per-cell region map, and region 0 means "no
 * region" -- either end being 0 drops the edge silently.
 *
 * The duplicate test compares the destination AND the near cell, not the far
 * one, so two different crossings into the same region are both kept while the
 * same crossing offered twice is not. That asymmetry is the original's.
 *
 * The array grows by exactly one link per call -- a realloc every time, with
 * no capacity -- and the new slot is zeroed before all three of its fields are
 * written, so the zeroing cannot be observed. Both reproduced.
 *
 * The edge is one-way: nothing here adds the reverse. Callers that want both
 * directions call twice.
 *
 * The log is gated on -tracePF, which is what identifies this graph as the
 * pathfinding structure, and it prints the two REGION ids rather than the
 * cells it was given. */
void __cdecl AddRegionLink(int32_t cell, int32_t neighbour)
{
    const uint8_t *map = kRegionOfCell;
    int32_t        a   = map[cell];
    int32_t        b   = map[neighbour];
    AM2_RegionLink *links;
    int32_t         n;

    if (a == 0 || b == 0)
        return;

    n = kNLinks(a);

    for (int32_t i = 0; i < n; i++) {
        if (kLinks(a)[i].to == b && (uint16_t)kLinks(a)[i].from == cell)
            return;
    }

    links = (AM2_RegionLink *)am2_realloc(kLinks(a),
                                          (size_t)(n + 1)
                                          * sizeof(AM2_RegionLink));
    kLinks(a) = links;

    n = kNLinks(a);
    links[n].to   = 0;
    links[n].from = 0;
    links[n].into = 0;

    links[n].to   = (int16_t)b;
    links[n].from = (int16_t)cell;
    links[n].into = (int16_t)neighbour;

    kNLinks(a) = (uint8_t)(n + 1);

    if (kTracePF)
        orig_log("Added Region link from %d to %d\n", a, b);
}

/* 0x00437E00, four callers -- and one of them is ADDR_SETTLE_POINT_IN_REGION
 * itself, which installs the rule and then dispatches through it in the same
 * breath. Choose which of three rules a point gets settled under, from the
 * object doing the asking.
 *
 * FILED HERE RATHER THAN BY BAND, the same call RowInit needed. It sits in
 * pad.cpp..script.cpp, a different translation unit from this file's
 * 0x0042B860 -- but its only consumers are the region walkers, and the
 * settle function it feeds reads ADDR_REGION_OF_CELL. Filing it away from
 * them would cost more than the split does.
 *
 * The three arms come from the tests, not from reading the handlers. A
 * vehicle of kind 5 -- `ptboat` in the unit-type table -- takes one rule; any
 * other vehicle and any roach take another; a null object or anything else
 * takes the third. A unit that moves on water having a rule of its own is
 * exactly what one would expect, and here it is a fact rather than a guess.
 *
 * TWO THINGS THE SHAPE HIDES. The non-vehicle path stores the vehicle rule
 * FIRST and only then asks whether the object is a roach, overwriting with
 * the default when it is not -- so the store happens twice on that path and
 * the intermediate value is observable by nothing. And a NULL object skips
 * the army store entirely, jumping straight to the default rule, so
 * ADDR_POINT_RULE_ARMY keeps whatever the previous object left. Both
 * reproduced.
 */
void __cdecl SetPointRule(void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (o) {
        *(int32_t *)(uintptr_t)ADDR_POINT_RULE_ARMY =
            *(const int8_t *)(o + OBJ_OFF_ARMY);

        if (ObjIsType3((const AM2_Object *)o)) {
            *(uint32_t *)(uintptr_t)ADDR_POINT_RULE =
                *(const int32_t *)(o + VEHICLE_OFF_KIND) == AM2_VEHICLE_KIND_BOAT
                    ? ADDR_POINT_RULE_BOAT : ADDR_POINT_RULE_VEHICLE;
            return;
        }

        *(uint32_t *)(uintptr_t)ADDR_POINT_RULE = ADDR_POINT_RULE_VEHICLE;
        if (ObjIsType8((const AM2_Object *)o))
            return;
    }

    *(uint32_t *)(uintptr_t)ADDR_POINT_RULE = ADDR_POINT_RULE_DEFAULT;
}

/* 0x0042B7F0, three callers. Of all the links `region` has to `to`, the index
 * of the MIDDLE one; -1 when there are none.
 *
 * Two regions usually touch along a RUN of cells, so a caller that wants to
 * get from one to the other has a choice of crossings and this makes it --
 * the middle of the run rather than the first cell that happens to be listed.
 * That is the whole content of the function and the reason for the name.
 *
 * IT WALKS THE LIST TWICE and the second walk is not a search for the same
 * thing: the first counts matches, the second stops at the
 * `(count + 1) / 2`-th. Written out that way rather than collapsed into one
 * pass with a saved index, because the original really does make two passes
 * and a single pass would have to decide what "middle" means before it knows
 * the count.
 *
 * The halving is an arithmetic shift of `count + 1`, so an odd run takes the
 * upper middle and an even one the lower -- 3 links give the 2nd, 4 give the
 * 2nd as well.
 *
 * A region with a link count of zero returns -1 from the SECOND guard, having
 * already computed a half of zero from the first. Both exits are the same
 * answer by different routes and both are reproduced.
 *
 * UNEXERCISED, and not for the usual reason -- it is not blind. Its three
 * callers sit inside 0x004049C0 and 0x00407190, the unit movement code, and
 * a Boot Camp drive that walks nobody anywhere never reaches them: the
 * counter reads 0 while AddRegionLink beside it reads 2,109 from the map
 * load. Verified by reading. It needs the same thing the combat path does,
 * which is a drive that gives a unit somewhere to go.
 */
int32_t __cdecl MiddleRegionLink(int32_t region, int32_t to)
{
    const AM2_RegionLink *links;
    int32_t               n     = kNLinks(region);
    int32_t               count = 0;
    int32_t               half, i;

    if (n > 0) {
        links = kLinks(region);
        for (i = 0; i < n; i++)
            if (links[i].to == (int16_t)to)
                count++;
    }

    half = (count + 1) >> 1;
    if (half <= 0)
        return -1;
    if (n <= 0)
        return -1;

    links = kLinks(region);
    for (i = 0; i < n; i++) {
        if (links[i].to == (int16_t)to && --half <= 0)
            return i;
    }

    return -1;
}

/* Still original: solving one pair of the routing tables. */
typedef void (__cdecl *AM2_SolvePairFn)(int32_t from, int32_t to);
#define orig_region_solve_pair \
            ((AM2_SolvePairFn)AM2_IMAGE(ADDR_REGION_SOLVE_PAIR))

/* 0x00406460, one caller. How many hops from one region to another.
 *
 * THIS FUNCTION IS WHAT MAKES THE TWO MATRICES LEGIBLE. Both are indexed
 * `m[from * stride + to]`; the first is tested against a sentinel byte and the
 * second is walked one hop at a time until it arrives. So ADDR_REGION_COST
 * records whether a pair has been solved and ADDR_REGION_NEXT is a next-hop
 * table -- all-pairs routing stored as bytes, which is why a map has fewer
 * than 256 regions.
 *
 * THREE ANSWERS, NOT TWO. Same region is 0, a reachable one is the hop count,
 * and -1 means either that the pair is unsolved and `solve` was clear, or that
 * the walk stepped into region 0. Both -1 paths share an exit and a caller
 * cannot tell them apart -- which is worth knowing before reading -1 as
 * "unreachable".
 *
 * The stride is RE-READ after solving, because solving can rebuild the tables
 * and the caller's copy would be stale. Reproduced; that reload is the only
 * hint here that ADDR_REGION_SOLVE_PAIR does more than fill one cell.
 */
int32_t __cdecl RegionHops(int32_t from, int32_t to, int32_t solve)
{
    const uint8_t *next;
    int32_t        stride;
    int32_t        hops;
    int32_t        at;

    if (from == to)
        return 0;

    stride = *(const int16_t *)AM2_IMAGE(ADDR_REGION_STRIDE);

    /* CORRECTED. This compared `!=` and called that "already solved", which is
       the original's test inverted: 0x00406492 is `cmp cl, al; je` PAST the
       solve, so a pair whose cost byte EQUALS the stamp is the solved one.
       SolvePair confirms it from the other side -- 0x00438389 and 0x004383A5
       write the stamp byte into the cost entry as the last thing they do. The
       inverted version re-solved every solved pair and skipped every unsolved
       one; see ActivateRegion for why nothing here caught it. */
    if ((*(const uint8_t *const *)AM2_IMAGE(ADDR_REGION_COST))
            [from * stride + to]
        == *(const uint8_t *)AM2_IMAGE(ADDR_REGION_STAMP)) {
        /* already solved */
    } else if (!solve) {
        return -1;
    } else {
        orig_region_solve_pair(from, to);
        stride = *(const int16_t *)AM2_IMAGE(ADDR_REGION_STRIDE);
    }

    next = *(const uint8_t *const *)AM2_IMAGE(ADDR_REGION_NEXT);
    at   = from;
    hops = 0;

    for (;;) {
        hops++;
        if (at == 0)
            return -1;
        at = next[at * stride + to];
        if (at == to)
            return hops;
    }
}

/* 0x004066B0, two callers. Whether two objects are in the same region or in
 * neighbouring ones.
 *
 * THE ARGUMENTS ARE READ IN THE OPPOSITE ORDER TO THE CALL. The second
 * object's region is computed first and becomes RegionHops' `from`; the
 * first's becomes `to`. Nothing here is symmetric enough for that to be
 * harmless -- a next-hop table need not agree in both directions -- so the
 * order is reproduced rather than tidied.
 *
 * True for a hop count of 0 or 1, and false for -1, which folds "unreachable"
 * and "unsolved" together with "far away". */
int32_t __cdecl RegionsNear(const void *a, const void *b, int32_t solve)
{
    const uint8_t *cells = *(const uint8_t *const *)AM2_IMAGE(ADDR_REGION_OF_CELL);
    int32_t        rb, ra, hops;

    rb = cells[(uint32_t)TileOfPoint(
                   *(const uint32_t *)((const uint8_t *)b + OBJ_OFF_POS))
               & 0xFFFFu];
    ra = cells[(uint32_t)TileOfPoint(
                   *(const uint32_t *)((const uint8_t *)a + OBJ_OFF_POS))
               & 0xFFFFu];

    hops = RegionHops(rb, ra, solve);
    if (hops == -1 || hops > 1)
        return 0;
    return 1;
}

/* 0x0042BC70 and 0x0042BCB0, one caller each and adjacent in the image: the
 * script's `activateregion` (token 149) and `inactivateregion` (150). That one
 * caller is an action handler which picks between them on a boolean in the
 * action record.
 *
 * The name is the game's own twice over -- the token table has both words, and
 * 0x0042BAFB logs "Activating Region %d" immediately after storing 1 into the
 * same REGION_OFF_ACTIVE these write. Neither alone would do: the log line
 * names the field and the tokens name the pair.
 *
 * WHAT MAKES THEM MORE THAN FLAG SETTERS is the last two instructions. Each
 * increments ADDR_REGION_STAMP, and only when the flag ACTUALLY CHANGED --
 * activating an already-active region writes nothing and bumps nothing. That
 * byte is the generation the all-pairs routing cache is stamped with, so one
 * increment invalidates every solved pair without touching the matrix, and a
 * redundant activate does not throw the cache away. It wraps at 256, which is
 * fine: what matters is that the new stamp differs from the one already
 * stored, not that it is monotonic.
 *
 * THAT IS ALSO HOW A DEFECT IN RegionHops ABOVE WAS FOUND, and it is worth
 * being plain about how badly the evidence was arranged. ADDR_REGION_STAMP was
 * called ADDR_REGION_UNSET and glossed "not solved yet"; RegionHops was
 * written from that name and tested `cost != stamp` for "already solved",
 * which is the original's `cmp cl, al; je` inverted. SolvePair settles it from
 * the far side -- it WRITES the stamp into the cost entry as its last act --
 * so equal means solved and the reconstruction had it exactly backwards.
 *
 * Nothing here caught it and nothing here could have. RegionHops is reached
 * only when something is pathfound rather than driven, which no drive in this
 * project does; its counter and MiddleRegionLink's read 0 on every
 * configuration. The A/B passed because the code never ran. A wrong name
 * propagated into a comparison, and what found it was reading the two
 * functions that write the byte.
 *
 * SolvePair is a SECOND, INDEPENDENT WITNESS to both halves of that, and it
 * was read afterwards rather than assumed. It writes the stamp into
 * cost[from][to] and cost[to][from] on BOTH of its exits -- the one that
 * found a path and the one that did not -- so "stamped" cannot mean anything
 * but "answered". And its first act is to read regions[to] + 4 and return at
 * once when that is zero, which is REGION_OFF_ACTIVE being read by a third
 * function that never mentions the word. Neither fact needed a drive.
 */
void __cdecl ActivateRegion(int32_t region)
{
    uint8_t *rec;

    if (region >= *(const int16_t *)AM2_IMAGE(ADDR_REGION_STRIDE))
        return;

    rec = *(uint8_t *const *)AM2_IMAGE(ADDR_REGIONS)
          + region * AM2_REGION_SIZE + REGION_OFF_ACTIVE;

    if (*(const int32_t *)rec != 0)
        return;

    *(int32_t *)rec = 1;
    (*(uint8_t *)AM2_IMAGE(ADDR_REGION_STAMP))++;
}

void __cdecl InactivateRegion(int32_t region)
{
    uint8_t *rec;

    if (region >= *(const int16_t *)AM2_IMAGE(ADDR_REGION_STRIDE))
        return;

    rec = *(uint8_t *const *)AM2_IMAGE(ADDR_REGIONS)
          + region * AM2_REGION_SIZE + REGION_OFF_ACTIVE;

    if (*(const int32_t *)rec == 0)
        return;

    *(int32_t *)rec = 0;
    (*(uint8_t *)AM2_IMAGE(ADDR_REGION_STAMP))++;
}

int region_install(void)
{
    /* Two now, so this is no longer a single `return patch_replace`. That
     * shape is exactly how four reconstructions once ended up never being
     * installed, and adding to one without noticing is how it happens -- this
     * one did, and the patch count not moving is what said so. */
    int rc = 0;

    rc |= patch_replace(ADDR_SET_POINT_RULE, (const void *)SetPointRule,
                        "SetPointRule", 4);
    rc |= patch_replace(ADDR_MIDDLE_REGION_LINK, (const void *)MiddleRegionLink,
                        "MiddleRegionLink", 3);
    rc |= patch_replace(ADDR_REGION_HOPS, (const void *)RegionHops,
                        "RegionHops", 1);
    rc |= patch_replace(ADDR_REGIONS_NEAR, (const void *)RegionsNear,
                        "RegionsNear", 2);
    rc |= patch_replace(ADDR_ADD_REGION_LINK, (const void *)AddRegionLink,
                        "AddRegionLink", 2);
    rc |= patch_replace(ADDR_ACTIVATE_REGION, (const void *)ActivateRegion,
                        "ActivateRegion", 1);
    rc |= patch_replace(ADDR_INACTIVATE_REGION, (const void *)InactivateRegion,
                        "InactivateRegion", 1);
    return rc;
}
