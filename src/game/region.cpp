/* region.cpp -- see region.h. */
#include <stdint.h>
#include <string.h>

#include "rect.h"   /* Clamp -- reconstructed */
#include "dist.h"   /* AngleDelta -- reconstructed */
#include "army.h"   /* ObjIsOurs -- reconstructed */
#include "air.h"    /* RevealObj -- reconstructed */

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

typedef int32_t (__cdecl *AM2_PointRuleFn)(int32_t tile);

/* 0x00439F40, five callers. NearestAllowedTile's TWIN, and reading them
 * together is the only way to see what either one is for. Same square spiral,
 * same four directions out of ADDR_SPIRAL_DX/DY, same three tests on a
 * candidate, same give-up at the end of a ring. Three differences, all of them
 * small and none of them cosmetic:
 *
 *  - It installs the rule for a NULL object, so it always searches under
 *    ADDR_POINT_RULE_DEFAULT. The sibling takes an object and gets that
 *    object's arm -- boat, other vehicle, or default. So this is the "what
 *    would anything be allowed to stand on" question and the sibling is
 *    "what would THIS unit be allowed to stand on".
 *
 *  - When the starting tile is already accepted it writes NOTHING through the
 *    caller's pointer and simply answers the tile. The sibling writes the
 *    point on that path too, conditionally. So a caller here keeps whatever
 *    point it arrived with unless the search actually moved, which is exactly
 *    what FormationPoint below relies on: it has just computed a point and
 *    only wants it snapped if the tile it lands on is refused.
 *
 *  - Its first argument is the tile rather than an object, so there are two
 *    parameters and not three.
 *
 * THE `push 0` SERVES TWO CALLS. The original pushes it for SetPointRule and
 * then does not clean up, so the following `push tile; call rule; add esp, 8`
 * cleans both. That is a deferred cleanup and not a two-argument rule -- the
 * rule call inside the loop pushes one dword and cleans four, which is what
 * settles the arity.
 *
 * It returns a UINT16, and the failure exit is `xor ax, ax` -- sixteen bits,
 * leaving the rest of eax holding whatever the spiral left there. Log2Mask's
 * problem again: read the low word.
 */
uint16_t __cdecl SettlePointInRegion(int32_t tile, uint32_t *pt)
{
    AM2_PointRuleFn rule;
    const uint8_t  *cells;
    int32_t         region;
    int32_t         w, h, shift;
    int32_t         x, y, dir, step, leg, tried;

    SetPointRule((void *)0);

    cells  = *(const uint8_t *const *)(uintptr_t)ADDR_REGION_OF_CELL;
    region = cells[tile & 0xFFFF];

    rule = *(AM2_PointRuleFn *)(uintptr_t)ADDR_POINT_RULE;
    if (!rule(tile))
        return (uint16_t)tile;

    w     = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    h     = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H;
    shift = *(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;

    x     = (tile & 0xFFFF) & (w - 1);
    y     = (int32_t)((uint32_t)(tile & 0xFFFF) >> shift);
    dir   = 0;
    step  = 0;
    leg   = 1;
    tried = 0;

    for (;;) {
        x += ((const int32_t *)AM2_IMAGE(ADDR_SPIRAL_DX))[dir];
        y += ((const int32_t *)AM2_IMAGE(ADDR_SPIRAL_DY))[dir];

        if (x > 0 && x < w && y > 0 && y < h) {
            int32_t cand = (y << shift) + x;
            int32_t ok   = 1;

            if (region != 0) {
                int32_t r = cells[cand & 0xFFFF];

                if (r != 0 && r != region)
                    ok = 0;
            }

            if (ok) {
                tried = 1;
                rule  = *(AM2_PointRuleFn *)(uintptr_t)ADDR_POINT_RULE;
                if (!rule(cand)) {
                    *pt = PointOfTile(cand);
                    return (uint16_t)cand;
                }
            }
        }

        if (++step < leg)
            continue;

        if (dir == 3) {
            if (!tried && region > 0)
                return 0;
            tried = 0;
        }

        if (dir & 1)
            leg++;
        dir  = (dir + 1) & 3;
        step = 0;
    }
}

/* 0x0043A0A0, six callers. The nearest tile the object's point rule will
 * accept, with the corresponding point written back through the caller's
 * pointer.
 *
 * THE COMMON CASE IS A NO-OP. The given tile is put to the rule first, and
 * when it passes the function only fills in the point -- and even that is
 * conditional: the point is written ONLY if its low word is already zero. So
 * a caller that arrives with a point set keeps it, which is why the two
 * readers in item.cpp see a point that is sometimes snapped and sometimes not.
 *
 * WHEN THE TILE IS REFUSED IT SPIRALS. Four directions from ADDR_SPIRAL_DX and
 * ADDR_SPIRAL_DY -- up, right, down, left -- with the leg length starting at
 * one and growing every SECOND direction, which is the ordinary square spiral.
 * The origin tile is never retried: the first step is taken before the first
 * test, and it has already failed.
 *
 * A CANDIDATE MUST PASS THREE THINGS. It must be strictly inside the map --
 * `x > 0`, `x < width`, `y > 0`, `y < height`, so the whole outer border is
 * excluded rather than clamped. It must be in the same REGION as the starting
 * tile, unless the start has no region (0) or the candidate has none, either
 * of which waives the test. Only then is the rule asked.
 *
 * THE GIVE-UP IS AT THE END OF A FULL RING and it is not a distance limit. At
 * the end of direction 3, if no candidate was PUT TO THE RULE during that ring
 * and the starting tile had a region, it returns 0. So a ring that was
 * entirely out of bounds or entirely in other regions ends the search, and a
 * search from a region-less tile never gives up at all -- it spirals until the
 * bounds test stops producing candidates, which for a tile near the middle of
 * the map means a very long walk. Reproduced; it is the original's.
 *
 * The x and y are recovered from the tile with `& (width - 1)` and a shift by
 * ADDR_MAP_ROW_SHIFT, which is a modulo only because the width is a power of
 * two -- the same pair PointOfTile uses in the other direction.
 *
 * It returns the tile as a UINT16, which the old signature had as void.
 * Every caller ignores it, which is why nobody noticed.
 *
 * MEASURED AT 1, AND THAT 1 IS NOT THE COUNT. Four of its six callers are
 * ours now -- BeginMoveTo above and one in item.cpp among them -- and they
 * call by name, so only the remaining original caller crosses the patched
 * entry. BeginMoveTo alone reads 51 on the same run, so this runs at least
 * fifty-two times and the counter can see one of them.
 *
 * WHETHER THE SPIRAL EVER RUNS IS NOT ESTABLISHED, and it is most of the
 * function. The common path is the first rule test passing and returning
 * at once; nothing measured here distinguishes that from a search. So the
 * bounds test, the region waiver, the leg growth and the give-up rule are
 * all verified by reading, and the A/B covers them only if some call was
 * refused -- which is exactly what is not known.
 */
uint16_t __cdecl NearestAllowedTile(void *obj, int32_t tile, uint32_t *pt)
{
    AM2_PointRuleFn rule;
    const uint8_t  *cells;
    int32_t         region;
    int32_t         w, h, shift;
    int32_t         x, y, dir, step, leg, tried;

    SetPointRule(obj);

    cells  = *(const uint8_t *const *)(uintptr_t)ADDR_REGION_OF_CELL;
    region = cells[tile & 0xFFFF];

    rule = *(AM2_PointRuleFn *)(uintptr_t)ADDR_POINT_RULE;
    if (!rule(tile)) {
        if (*(const uint16_t *)pt == 0)
            *pt = PointOfTile(tile);
        return (uint16_t)tile;
    }

    w     = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    h     = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H;
    shift = *(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;

    x     = (tile & 0xFFFF) & (w - 1);
    y     = (int32_t)((uint32_t)(tile & 0xFFFF) >> shift);
    dir   = 0;
    step  = 0;
    leg   = 1;
    tried = 0;

    for (;;) {
        x += ((const int32_t *)AM2_IMAGE(ADDR_SPIRAL_DX))[dir];
        y += ((const int32_t *)AM2_IMAGE(ADDR_SPIRAL_DY))[dir];

        if (x > 0 && x < w && y > 0 && y < h) {
            int32_t cand = (y << shift) + x;
            int32_t ok   = 1;

            if (region != 0) {
                int32_t r = cells[cand & 0xFFFF];

                if (r != 0 && r != region)
                    ok = 0;
            }

            if (ok) {
                tried = 1;
                rule  = *(AM2_PointRuleFn *)(uintptr_t)ADDR_POINT_RULE;
                if (!rule(cand)) {
                    *pt = PointOfTile(cand);
                    return (uint16_t)cand;
                }
            }
        }

        if (++step < leg)
            continue;

        if (dir == 3) {
            if (!tried && region > 0)
                return 0;
            tried = 0;
        }

        if (dir & 1)
            leg++;
        dir  = (dir + 1) & 3;
        step = 0;
    }
}

/* 0x00439E90, four callers. Can this object reach that point in a straight
 * line, and if so, record the move on it.
 *
 * Four steps and the order is the whole of it. SetPointRule installs the arm
 * that suits this object -- boat, other vehicle, or the default.
 * ResolvePointForTile is given the target's tile and may rewrite the target.
 * TraceTileLine fills ADDR_TILE_LINE_BUF with every tile between the object's
 * position and that point. Then each tile is put to the installed rule, and
 * ANY refusal ends it: the function answers 0 and writes nothing at all.
 *
 * THE RULE IS ASKED THROUGH A POINTER AND THE ARGUMENT IS HALF JUNK. The
 * original loads the tile into `cx` alone, leaving the upper half of ecx
 * holding whatever the previous call left there, and pushes the whole dword.
 * That is safe only because every handler opens with `and eax, 0xFFFF` --
 * checked in 0x00437D10 and 0x00437D60 rather than assumed -- so the tile is
 * passed zero-extended here and the two agree. A handler that read all 32 bits
 * would not be reproducible at all.
 *
 * THE BOAT RULE CONFIRMS SOMETHING FROM ANOTHER SUBSYSTEM. ADDR_POINT_RULE_BOAT
 * is vehicle kind 5, which the unit-type table calls `ptboat`, and it refuses a
 * tile whose AM2_TILE_OPEN bit is CLEAR. That is exactly the terrain test
 * BlockWeightChain makes -- and BlockWeightChain is the variant MaskBlockWeight
 * selects for kind 5 and no other. So the bit polarity that looked like the odd
 * one out among three blocking variants is the boat's rule, asked the same way
 * in two places that share no code. Three sides agreeing beats any of them.
 *
 * On success the object gets its CURRENT position as the from, the resolved
 * point as the to, and three small fields seeded 0, 1 and 2. The 1 is the
 * return value reused -- the original sets eax before the stores and writes it
 * as a word into +0x520 -- which is register allocation and not a claim that
 * the two are the same thing, so they are written separately here.
 *
 * MEASURED AT 41 CALLS on a driven Boot Camp mission, and landing it
 * explains a number recorded two commits ago. TraceTileLine read 35 there
 * and reads 0 now: this was its caller, and it calls by name. So the two
 * figures are one drive apart and the same work -- and TraceTileLine is
 * blind from here on, which its own comment should be read against.
 * SetPointRule reads 92 on the same run, all of it from its other callers.
 *
 * What 41 calls cover is the trace, the rule loop and the success stores.
 * Whether any of them was REFUSED -- the early exit that writes nothing --
 * is not established, and neither is which of the three rule arms was
 * installed. Boot Camp has no boats.
 */
int32_t __cdecl BeginMoveTo(void *obj, uint32_t *to)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  count = 0;
    int32_t  i;

    SetPointRule(obj);

    NearestAllowedTile(obj, TileOfPoint(*to), to);

    TraceTileLine(*(const uint32_t *)(o + OBJ_OFF_POS), *to,
                  (uint16_t *)(uintptr_t)ADDR_TILE_LINE_BUF, &count);

    for (i = 0; i < count; i++) {
        AM2_PointRuleFn rule =
            *(AM2_PointRuleFn *)(uintptr_t)ADDR_POINT_RULE;

        if (rule((int32_t)((const uint16_t *)
                     (uintptr_t)ADDR_TILE_LINE_BUF)[i]))
            return 0;                   /* the rule refused this tile */
    }

    *(uint32_t *)(o + OBJ_OFF_MOVE_FROM) =
        *(const uint32_t *)(o + OBJ_OFF_POS);
    *(uint32_t *)(o + OBJ_OFF_MOVE_TO)   = *to;
    *(uint16_t *)(o + OBJ_OFF_MOVE_F128) = 0;
    *(uint16_t *)(o + OBJ_OFF_MOVE_F520) = 1;
    *(uint16_t *)(o + OBJ_OFF_MOVE_F522) = 2;

    return 1;
}

/* The three tile-cover functions -- TileCoverAdd (0x004384A0), TileCoverSub
 * (0x00438520) and MarkOpenTile (0x0043A4F0) -- share one bounds test and one
 * neighbourhood, so they are written together and the shared parts are stated
 * once here.
 *
 * A TILE INDEX IS PACKED, NOT A PAIR. The map's width is a power of two and
 * ADDR_MAP_ROW_SHIFT is its log, so x is `tile & (width - 1)` and y is
 * `tile >> shift`. That is why the neighbour deltas can be plain additions on
 * the index.
 *
 * THE MARGIN IS TWO AND IT IS WHAT MAKES THE NEIGHBOUR WALK SAFE. The two
 * writers refuse a tile outside `2 <= x < width - 2` and `2 <= y < height - 2`
 * and then check no individual neighbour -- the twenty deltas are known to
 * stay inside the map once the centre is two in from every edge. Get the
 * margin wrong and the errors are silent writes past the grid.
 *
 * THE READER DOES NOT HAVE IT. MarkOpenTile walks the same twenty deltas with
 * no margin test of any kind, so a tile near an edge reads bytes outside the
 * grid. That is the original's and it is reproduced; it was nearly written
 * with the test, on the assumption that three functions sharing a
 * neighbourhood share its precondition. They do not -- read each one.
 *
 * The argument arrives as a dword and is masked to sixteen bits before use.
 */
static int32_t TileCoverInBounds(uint16_t tile)
{
    int32_t w = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    int32_t x = (int32_t)tile & (w - 1);
    int32_t y = (int32_t)tile
                >> *(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;

    return x >= 2 && x < w - 2
        && y >= 2 && y < *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H - 2;
}

/* TileCoverAdd -- original 0x004384A0, three callers.
 *
 * Add one to the cover count of a tile and of its twenty neighbours.
 *
 * THE GRID POINTER IS RE-READ ON EVERY NEIGHBOUR. Nothing in the loop can move
 * it, so that is the compiler keeping a register rather than anything
 * defensive; written as one load, which is what it means.
 *
 * The count is a byte and nothing clamps it. Twenty-one increments per call
 * with no saturation means a tile covered by more than 255 things wraps to 0
 * and reads as uncovered. The original's, and the map would have to be
 * extraordinary to reach it.
 */
void __cdecl TileCoverAdd(uint16_t tile)
{
    uint8_t *cover;
    uint32_t i;

    if (!TileCoverInBounds(tile))
        return;

    cover = *(uint8_t **)(uintptr_t)ADDR_TILE_COVER;
    cover[tile]++;

    for (i = 0; i < AM2_TILE_NEIGHBOURS; i++)
        cover[(int32_t)tile
              + ((const int32_t *)(uintptr_t)ADDR_TILE_NEIGHBOURS)[i]]++;
}

/* TileCoverSub -- original 0x00438520, two callers, and TileCoverAdd with
 * `dec` where the other has `inc` -- the same bounds test, the same twenty
 * deltas, the same re-read of the grid pointer. The pair is what identifies
 * the table: nothing else writes it.
 *
 * ObjClearFootprint calls this immediately after taking fifteen off
 * ADDR_CELL_WEIGHTS for the same point, which is how the two tables are known
 * to move together.
 *
 * IT DOES NOT CHECK FOR ZERO. A subtract without a matching add wraps the byte
 * to 255, which reads as heavily covered rather than as empty -- the failure
 * is the loud kind rather than the quiet one, but it is not guarded.
 */
void __cdecl TileCoverSub(uint16_t tile)
{
    uint8_t *cover;
    uint32_t i;

    if (!TileCoverInBounds(tile))
        return;

    cover = *(uint8_t **)(uintptr_t)ADDR_TILE_COVER;
    cover[tile]--;

    for (i = 0; i < AM2_TILE_NEIGHBOURS; i++)
        cover[(int32_t)tile
              + ((const int32_t *)(uintptr_t)ADDR_TILE_NEIGHBOURS)[i]]--;
}

/* MarkOpenTile -- original 0x0043A4F0, one caller.
 *
 * The reader for the two tables the pair above writes. Over the same twenty
 * neighbours, count how many carry a full cell weight and how many carry any
 * cover at all, and set a tile flag for each count that comes up short.
 *
 * ITS ONLY GUARD IS THAT THE TILE IS NOT ITSELF WEIGHTED -- there is nothing
 * to say about a cell something already stands on. It does NOT test the
 * margin the two writers do, so the neighbour walk can read outside the grid
 * near an edge. Reproduced.
 *
 * THE TWO THRESHOLDS ARE DIFFERENT AND THAT IS THE POINT: fewer than ONE
 * weighted neighbour sets 0x04, fewer than TWO covered neighbours sets 0x08.
 * Reading them as the same test loses the distinction the function exists for.
 *
 * IT ALWAYS ANSWERS 0. The original zeroes the register on every path
 * including the early one, and the caller does not look. Kept because the
 * prototype is the original's.
 *
 * The counts are compared with SIGNED tests against a byte read as signed, so
 * a weight of 0x80 or more reads as negative and does NOT count as weighted.
 * A cell would need nine footprint points to get there. Reproduced.
 */
int32_t __cdecl MarkOpenTile(uint16_t tile)
{
    const int8_t *weights;
    const int8_t *cover;
    int32_t       weighted = 0;
    int32_t       covered  = 0;
    uint32_t      i;

    weights = *(const int8_t *const *)(uintptr_t)ADDR_CELL_WEIGHTS;
    if (weights[tile] >= (int8_t)AM2_CELL_WEIGHT_STEP)
        return 0;

    cover = *(const int8_t *const *)(uintptr_t)ADDR_TILE_COVER;

    for (i = 0; i < AM2_TILE_NEIGHBOURS; i++) {
        int32_t at = (int32_t)tile
                     + ((const int32_t *)(uintptr_t)ADDR_TILE_NEIGHBOURS)[i];

        if (weights[at] >= (int8_t)AM2_CELL_WEIGHT_STEP)
            weighted++;
        if (cover[at] >= 1)
            covered++;
    }

    if (weighted < 1)
        (*(uint8_t **)(uintptr_t)ADDR_TILE_FLAGS)[tile]
            |= AM2_TILE_NO_WEIGHT_NEAR;
    if (covered < 2)
        (*(uint8_t **)(uintptr_t)ADDR_TILE_FLAGS)[tile]
            |= AM2_TILE_LITTLE_COVER_NEAR;

    return 0;
}

/* BoxAction -- original 0x00438DF0, two callers, both of them the box markers
 * above and below it.
 *
 * Turn a rectangle in PIXELS into a scratch tile mask: shift each edge down by
 * AM2_TILE_SHIFT, clamp it into the map, pad the result by two tiles on every
 * side, and then write the padded rectangle into the caller's record followed
 * by one byte per tile it covers -- 2 everywhere, then 3 over the box.
 *
 * THE FIFTH ARGUMENT IS A POINTER AND THIS FILE USED TO SAY int32_t. The
 * typedef that reached it through the image declared `int32_t arg`, and
 * ObjBoxAction passed its own `arg` straight through, because both callers of
 * ObjBoxAction are original code handing it a stack buffer and nothing on this
 * side ever had to name the type. It is a 16-byte rectangle and a byte grid;
 * see TILEMASK_OFF_RECT in orig.h.
 *
 * The x pair is clamped against the map WIDTH and the y pair against its
 * HEIGHT, which is what tells the four arguments apart -- they are pushed as
 * plain dwords and nothing else distinguishes them.
 *
 * THE CLAMP IS TO 2 .. size-2 AND THE PAD IS 2, so the padded rectangle can
 * run from 0 to size exactly: the margin is what the clamp leaves room for,
 * and neither bound is a coincidence. What it does NOT guarantee is that the
 * caller's buffer is big enough -- the area is (w+1)*(h+1) over whatever the
 * box spans, and both callers use a fixed stack scratch.
 *
 * The row loop re-reads the rectangle out of the record every turn rather than
 * keeping it, and tests `left <= right` inside the loop although nothing in it
 * can change either. Reproduced; it is one comparison and hoisting it would be
 * a claim about the original that costs more to make than to leave.
 *
 * Always answers 1.
 *
 * NOT EXERCISED BY ANY DRIVE THIS PROJECT HAS, and the counter says so rather
 * than being assumed: a Boot Camp combat run past both dialogs, walking and
 * firing, with TileOfPoint at ten million, leaves BoxAction and ObjBoxAction
 * both at 0. The counter exists -- an unknown name answers "(nothing traced)"
 * and these answer 0 -- so that is a measurement.
 *
 * The reason is one branch above. Both of ObjBoxAction's callers test
 * OBJ_OFF_HIT_MASK first and go to 0x004389D0 when it is set; ObjBoxAction is
 * the no-mask fallback, and everything on this map has a mask. BoxAction's
 * other caller, 0x00438F10, does not run here either. So this is verified by
 * READING, and the clean A/B says only that nothing regressed. */
int32_t __cdecl BoxAction(int32_t left, int32_t top, int32_t right,
                          int32_t bottom, void *out)
{
    uint8_t *rec = (uint8_t *)out;
    int32_t *box = (int32_t *)(rec + TILEMASK_OFF_RECT);
    uint8_t *cells = rec + TILEMASK_OFF_CELLS;
    int32_t  l;
    int32_t  r;
    int32_t  t;
    int32_t  b;
    int32_t  stride;
    int32_t  y;

    /* In the original's order: the two x edges, then the two y edges. */
    l = Clamp(left  >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W
                  - AM2_TILEMASK_MARGIN);
    r = Clamp(right >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W
                  - AM2_TILEMASK_MARGIN);
    t = Clamp(top   >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H
                  - AM2_TILEMASK_MARGIN);
    b = Clamp(bottom >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H
                  - AM2_TILEMASK_MARGIN);

    box[2] = r + AM2_TILEMASK_MARGIN;   /* right, written before left */
    box[3] = b + AM2_TILEMASK_MARGIN;   /* bottom */
    box[0] = l - AM2_TILEMASK_MARGIN;
    box[1] = t - AM2_TILEMASK_MARGIN;

    memset(cells, AM2_TILEMASK_PAD_CELL,
           (size_t)((box[2] - box[0] + 1) * (box[3] - box[1] + 1)));

    stride = box[2] - box[0] + 1;
    for (y = t; y <= b; y++) {
        if (l > r)
            continue;
        memset(cells + (y - box[1]) * stride - box[0] + l,
               AM2_TILEMASK_BOX_CELL, (size_t)(r - l + 1));
    }
    return 1;
}

/* ObjBoxAction -- original 0x00438F80, two callers.
 *
 * Offset the object's box by its position and hand the four edges to
 * 0x00438DF0 -- but only when the object is actually drawn with a software
 * sprite. Three ways out come before that, and they do NOT answer the same
 * thing: no sprite and no image both answer 0, while a sprite whose flags say
 * "not a software blit" answers 1.
 *
 * SO 0 AND 1 ARE NOT SUCCESS AND FAILURE HERE. 1 is "nothing to do, carry on"
 * and 0 is "this object has no sprite at all"; the real answer, when there is
 * one, is whatever 0x00438DF0 returns. Reading the two constants as a boolean
 * gets the middle case backwards.
 *
 * THE FLAGS TESTED ARE 0x1C, WHICH IS NOT THE WHOLE SOFTWARE MASK. sprite.h
 * documents bits 2..5 (0x3C) as selecting the software path; this pair tests
 * only 2..4. Bit 5 alone therefore takes the "nothing to do" arm. Reproduced,
 * and stated because 0x3C is the number a reader arrives with.
 *
 * The box is four separate dwords on the object -- OBJ_OFF_BOX_LEFT and the
 * three after it -- and the x pair and y pair take different offsets, so they
 * cannot be written as one loop.
 *
 * The sprite is reached as raw offsets rather than as AM2_Sprite, because this
 * is the FLAT half and that structure names LPDIRECTDRAWSURFACE. The same
 * reason item.cpp reads the sprite list as void **.
 */
int32_t __cdecl ObjBoxAction(void *obj, void *out)
{
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *spr;
    int32_t        x;
    int32_t        y;

    spr = *(const uint8_t *const *)
              (*(const uint8_t *const *)(o + OBJ_OFF_ROWS) + ROW_OFF_SPRITE);
    if (!spr)
        return 0;
    if (!*(const void *const *)(spr + SPR_OFF_IMAGE))
        return 0;
    if (!(*(const uint8_t *)(spr + SPR_OFF_FLAGS) & SPR_FLAG_SOFTWARE_BITS))
        return 1;

    x = *(const int16_t *)(o + OBJ_OFF_X);
    y = *(const int16_t *)(o + OBJ_OFF_Y);

    return BoxAction(*(const int32_t *)(o + OBJ_OFF_BOX_LEFT)   + x,
                     *(const int32_t *)(o + OBJ_OFF_BOX_TOP)    + y,
                     *(const int32_t *)(o + OBJ_OFF_BOX_RIGHT)  + x,
                     *(const int32_t *)(o + OBJ_OFF_BOX_BOTTOM) + y,
                     out);
}

/* ListBoxAction -- original 0x00438F10, one caller.
 *
 * ObjBoxAction for a RECORD-LIST HEADER instead of an object, and the same
 * function line for line apart from where the two halves come from: the sprite
 * is the first LIST RECORD's rather than the first ROW's, the box is the
 * header's four edges rather than the object's, and the point they are offset
 * by is an ARGUMENT here rather than a field.
 *
 * So the three exits mean what they mean there. No sprite and no image both
 * answer 0; a sprite whose flags say "not a software blit" answers 1, meaning
 * nothing to do; and the real answer, when there is one, is BoxAction's.
 *
 * ITS CALLER IS WHAT NAMED LISTHDR_OFF_HIT_MASK. 0x0043A6D0 tests that field
 * and goes to the bitmask walker at 0x004385A0 when it is set and here when it
 * is not -- the same choice OBJ_OFF_HIT_MASK drives one structure over. The
 * field had been LISTHDR_OFF_EXTRA, named from the only reader anyone had
 * found, which was the free; a field named from one of its two readers is a
 * field named from a call site.
 *
 * Not exercised, for the same reason BoxAction is not -- see the note there.
 */
int32_t __cdecl ListBoxAction(uint32_t at, void *list, void *out)
{
    const uint8_t *h = (const uint8_t *)list;
    const uint8_t *spr;
    int32_t        x;
    int32_t        y;

    spr = *(const uint8_t *const *)
              (*(const uint8_t *const *)(h + LISTHDR_OFF_RECORDS)
               + LISTREC_OFF_SPRITE);
    if (!spr)
        return 0;
    if (!*(const void *const *)(spr + SPR_OFF_IMAGE))
        return 0;
    if (!(*(const uint8_t *)(spr + SPR_OFF_FLAGS) & SPR_FLAG_SOFTWARE_BITS))
        return 1;

    x = (int32_t)(int16_t)(at & 0xFFFFu);
    y = (int32_t)(int16_t)(at >> 16);

    return BoxAction(*(const int32_t *)(h + LISTHDR_OFF_BOX_LEFT)   + x,
                     *(const int32_t *)(h + LISTHDR_OFF_BOX_TOP)    + y,
                     *(const int32_t *)(h + LISTHDR_OFF_BOX_RIGHT)  + x,
                     *(const int32_t *)(h + LISTHDR_OFF_BOX_BOTTOM) + y,
                     out);
}

/* RebuildTileCover -- original 0x0042BE10, one caller.
 *
 * Clear the whole cover grid and rebuild it from the cell weights: every
 * interior tile carrying a full weight is fed to TileCoverAdd, which puts one
 * back on it and on its twenty neighbours.
 *
 * ITS MARGIN IS ONE AND TileCoverAdd's IS TWO. This scans x and y over
 * 1 .. size-2 and TileCoverAdd refuses anything outside 2 .. size-3, so the
 * outermost scanned ring is passed to a function that rejects every tile in
 * it. That is not a bug and it is not a coincidence worth tidying: the two
 * bounds were written independently and the stricter one wins. Reproduced,
 * and worth knowing before reading the loop as "every tile that can be
 * covered".
 *
 * THE CLEAR IS width * height BYTES and the walk is over the interior only, so
 * the border keeps the zeros rather than any older value.
 *
 * The weight is read through a SIXTEEN-BIT mask of the tile index, and
 * compared SIGNED against fifteen -- the same reading MarkOpenTile does, so a
 * weight of 0x80 or more counts as unweighted here too.
 *
 * Both dimensions are re-read from their globals inside the loops rather than
 * hoisted. Nothing can change them; it is the compiler, and it is written as
 * the plain loop it means.
 */
void __cdecl RebuildTileCover(void)
{
    int32_t w = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    int32_t h = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H;
    const int8_t *weights;
    int32_t       y;

    memset(*(void **)(uintptr_t)ADDR_TILE_COVER, 0, (size_t)h * (size_t)w);

    weights = *(const int8_t *const *)(uintptr_t)ADDR_CELL_WEIGHTS;

    for (y = 1; y < h - 1; y++) {
        int32_t tile = w * y + 1;
        int32_t x;

        for (x = 1; x < w - 1; x++, tile++)
            if (weights[(uint16_t)tile] >= (int8_t)AM2_CELL_WEIGHT_STEP)
                TileCoverAdd((uint16_t)tile);
    }
}

typedef int32_t (__cdecl *AM2_RegionRandFn)(void);
#define orig_region_rand ((AM2_RegionRandFn)(uintptr_t)ADDR_GAME_RAND)

/* TileRegionOrBorrow -- original 0x0043A450, two callers.
 *
 * Which region a tile belongs to. If it already has one, answer that. If it
 * does not, walk the eight-neighbour ring from a random even start looking for
 * a neighbour that has one and is not blocked, CACHE that region on this tile,
 * and answer it. Nothing found answers 0 and caches nothing.
 *
 * THE RING TABLE IS DOUBLED AND THE LOOP STOPS ON A VALUE, NOT AN INDEX. The
 * eight deltas are followed by a copy of themselves, so the walk runs forward
 * from wherever it started for eight steps with no wrap test, and terminates
 * when the delta it reads equals the one it began with. That is why there is
 * no counter here: the table's own layout is the bound. It also means a table
 * whose eight deltas were not distinct would stop early, and one that was not
 * doubled would run off the end.
 *
 * THE RANDOM START IS `rand() & 6`, so it is 0, 2, 4 or 6 -- four of the eight
 * positions, never an odd one. The walk still covers all eight from any of
 * them; only where it begins is restricted, which decides WHICH neighbour wins
 * when several qualify.
 *
 * THE TWO NEIGHBOUR TESTS ARE READ DIFFERENTLY AND THAT IS NOT A SLIP. The
 * region byte is compared UNSIGNED against zero -- any non-zero value counts,
 * including 0x80 and above -- while the weight is compared SIGNED against
 * fifteen, so a weight of 0x80 or more reads as negative and counts as
 * unblocked. The same signed reading MarkOpenTile and RebuildTileCover use.
 *
 * The bounds test is on the RESULTING index rather than on the tile's x and y,
 * so a neighbour that wraps around a row edge is accepted as long as it lands
 * inside the grid. That is the difference from the cover functions, which
 * refuse a margin instead; here a tile on the left edge can borrow its region
 * from the right edge of the row above.
 *
 * The map dimensions are multiplied inside the loop on every neighbour rather
 * than once. Reproduced as the plain expression; nothing can change them.
 */
uint16_t __cdecl TileRegionOrBorrow(uint16_t tile)
{
    uint8_t       *regions = *(uint8_t **)(uintptr_t)ADDR_REGION_OF_CELL;
    const int8_t  *weights;
    const int32_t *ring;
    int32_t        first;
    int32_t        delta;

    if (regions[tile])
        return regions[tile];

    weights = *(const int8_t *const *)(uintptr_t)ADDR_CELL_WEIGHTS;
    regions = *(uint8_t **)(uintptr_t)ADDR_REGION_OF_CELL;

    ring  = (const int32_t *)(uintptr_t)ADDR_TILE_RING8
            + (orig_region_rand() & 6);
    first = ring[0];
    delta = first;

    for (;;) {
        int32_t at = delta + (int32_t)tile;

        if (at > 0
            && at < *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H
                    * *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W
            && regions[at] != 0
            && weights[at] < (int8_t)AM2_CELL_WEIGHT_STEP) {
            regions[tile] = regions[at];
            return (*(uint8_t **)(uintptr_t)ADDR_REGION_OF_CELL)[tile];
        }

        delta = *++ring;
        if (delta == first)
            return 0;
    }
}

typedef void (__cdecl *AM2_AiCommonFn)(void *obj, void *out, const void *ctx,
                                       int32_t flag);
#define orig_ai_common ((AM2_AiCommonFn)(uintptr_t)ADDR_AI_407190)

/* AiStepIgnore -- original 0x00407BF0, one caller, which is the AI mode
 * dispatcher at 0x00407F80. THIS IS THE `ignore` ARM, and the identification
 * is the point of the commit rather than the eighty bytes of body.
 *
 * The dispatcher switches on OBJ_OFF_AI_MODE through an eight-entry jump table
 * at 0x0040803C, and the table is read rather than the layout, which matters
 * here as much as it ever has: three of the eight indices share one arm.
 *
 *     0        -> 0x00407710 directly
 *     1, 4, 5  -> 0x00407560          (5 is `evade`)
 *     2        -> this                (`ignore`)
 *     3        -> 0x00407C80
 *     6        -> 0x00407BD0, which forwards to 0x00407710   (`attack`)
 *     7        -> 0x00407640          (`defend`)
 *
 * The mode numbers are not this file's guess. tests/actions-reference.txt
 * settled them from the shipped scripts -- attack 6, defend 7, ignore 2,
 * evade 5, "neither sequential nor in keyword order" -- and the table lands
 * them on arms that make sense of that: `attack` reaches the largest handler
 * in the band through a pass-through thunk orig.h had already noticed and
 * could not explain, and `ignore` gets the smallest.
 *
 * What `ignore` does. If the unit is still more than AM2_AI_ARRIVED_DIST from
 * the destination it remembers, it keeps the destination -- copying it into
 * OBJ_OFF_FIELD_C0 first -- and hands off to the common step. Otherwise it has
 * arrived: the destination is cleared, and the only two things that will turn
 * it are a hit it has not yet reacted to and, after a delay, the bearing of
 * whatever the context found. Which is what ignoring an order looks like.
 *
 * THE HIT ONLY TURNS IT WHEN THE CONTEXT HAS NO OBJECT AT SIGHT_OFF_OBSERVER,
 * but OBJ_OFF_HIT_DIR is consumed either way -- the clear is outside that
 * test. So a unit hit while the context holds that object forgets the hit
 * without acting on it. Reproduced.
 *
 * The delay is compared UNSIGNED (`jb`), so a deadline in the future wraps to
 * a huge number and passes. Written as the original has it.
 */
void __cdecl AiStepIgnore(void *obj, void *out, const void *ctx)
{
    uint8_t       *o = (uint8_t *)obj;
    uint8_t       *w = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)ctx;

    if (*(const int32_t *)(c + SIGHT_OFF_DEST_DIST) > AM2_AI_ARRIVED_DIST) {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(o + OBJ_OFF_SCRIPT_STATE);
        orig_ai_common(obj, out, ctx, 0);
        return;
    }

    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

    if (*(const uint8_t *)(o + OBJ_OFF_HIT_DIR)) {
        if (!*(const void *const *)(c + SIGHT_OFF_OBSERVER))
            w[1] = *(const uint8_t *)(o + OBJ_OFF_HIT_DIR);
        *(o + OBJ_OFF_HIT_DIR) = 0;
    }

    if (*(const int32_t *)(c + SIGHT_OFF_FOUND)
        && (uint32_t)(*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                      - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0))
           >= AM2_AI_TURN_DELAY_MS)
        w[1] = *(const uint8_t *)(c + SIGHT_OFF_BEARING);
}

/* The promotion the `defend` arm does twice. Whatever 0x00403B40 found becomes
 * the object this unit is engaging: its uid onto the object, and its
 * {object, range, bearing} triple over the record's own at +0x10/+0x14/+0x18,
 * which is where ConsiderSighting reads them from.
 *
 * The original inlines this at BOTH sites rather than calling it, and on the
 * arrived path both run -- the second immediately after the first, with
 * nothing between them that can change SIGHT_OFF_FOUND. So the repeat is
 * idempotent and writing it once would be indistinguishable. Kept as two calls
 * because "the original does it twice" is a fact about the original, and a
 * reader diffing against the disassembly should find both. */
static void AiPromoteFound(uint8_t *o, uint8_t *c)
{
    uint8_t *found = *(uint8_t **)(c + SIGHT_OFF_FOUND);

    if (!found)
        return;

    *(uint32_t *)(o + OBJ_OFF_TARGET_UID) =
        ((const AM2_Object *)found)->uid;
    *(uint8_t **)(c + SIGHT_OFF_OBSERVER) = found;
    *(int32_t *)(c + SIGHT_OFF_RANGE) =
        *(const int32_t *)(c + SIGHT_OFF_FOUND_RANGE);
    *(c + SIGHT_OFF_BEARING) = *(const uint8_t *)(c + SIGHT_OFF_FOUND_BEARING);
}

/* AiStepDefend -- original 0x00407640, one caller. The `defend` arm of the AI
 * mode dispatcher, mode 7.
 *
 * AiStepIgnore's shape with two things added, and the two are what `defend`
 * means. Both arms walk to the remembered destination while it is further than
 * AM2_AI_ARRIVED_DIST, and both, on arrival, clear it and turn for an unreacted
 * hit. What this one does that `ignore` does not:
 *
 *  - it PROMOTES whatever 0x00403B40 found into the slot ConsiderSighting
 *    reads, and records that object's uid on the unit at OBJ_OFF_TARGET_UID.
 *    `ignore` reads SIGHT_OFF_FOUND and does nothing with it but take a
 *    bearing;
 *  - and it ends by calling ConsiderSighting on every path, including the one
 *    where it did not arrive. So a defending unit keeps looking while it
 *    moves, which is the whole difference from ignoring.
 *
 * The turn is gated on the OBSERVER rather than on the found object, and that
 * matters because the promotion above may have just installed one. So a unit
 * that finds something this frame can turn to it this frame; a unit with
 * nothing found keeps whatever observer it had.
 *
 * The delay is compared UNSIGNED, as in AiStepIgnore, so a deadline in the
 * future wraps and passes. Written as the original has it.
 */
void __cdecl AiStepDefend(void *obj, void *out, void *ctx)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *w = (uint8_t *)out;
    uint8_t *c = (uint8_t *)ctx;

    if (*(const int32_t *)(c + SIGHT_OFF_DEST_DIST) > AM2_AI_ARRIVED_DIST) {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(o + OBJ_OFF_SCRIPT_STATE);
        orig_ai_common(obj, out, ctx, 0);
    } else {
        uint8_t hit = *(const uint8_t *)(o + OBJ_OFF_HIT_DIR);

        *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

        if (hit) {
            if (!*(const void *const *)(c + SIGHT_OFF_OBSERVER))
                w[1] = hit;
            *(o + OBJ_OFF_HIT_DIR) = 0;
        }

        AiPromoteFound(o, c);

        if (*(const void *const *)(c + SIGHT_OFF_OBSERVER)
            && (uint32_t)(*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                          - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0))
               >= AM2_AI_TURN_DELAY_MS)
            w[1] = *(const uint8_t *)(c + SIGHT_OFF_BEARING);
    }

    AiPromoteFound(o, c);
    ConsiderSighting(obj, out, ctx);
}

/* AiStepTrack -- original 0x00407560, one caller. The arm modes 1, 4 and 5
 * SHARE, mode 5 being `evade`; 1 and 4 have no keyword in the shipped scripts
 * and are set by code. The name is ours, from the body, because naming it
 * after one of the three modes it serves would be naming it from a call site.
 *
 * IT IS AiStepDefend WITH TEN INSTRUCTIONS ON THE OTHER SIDE OF A BLOCK.
 * Measured rather than eyeballed: disassembling both with branch targets
 * normalised to displacements gives SIXTY-NINE instructions each, the same
 * instructions in the same order, except that the turn test sits BEFORE the
 * second promotion in AiStepDefend and AFTER it here. The rest of the diff is
 * eax/edx swapped by the register allocator.
 *
 * That position is the whole behavioural difference, because the still-moving
 * path jumps to the second promotion in both. Landing there puts the turn test
 * behind you in one and ahead of you in the other:
 *
 *   AiStepDefend   turns toward what it sees only once it has ARRIVED
 *   this one       turns while it is still walking
 *
 * Anyone watching the game would see it, and a transcription that noticed the
 * two functions were "the same" and shared a tail between them would flatten
 * it in silence. Which is the argument for diffing the disassembly rather
 * than trusting the resemblance.
 *
 * Everything else is AiStepDefend's, including the promotion appearing twice
 * with nothing between the two that can change what it reads. See that
 * function for the rest.
 */
void __cdecl AiStepTrack(void *obj, void *out, void *ctx)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *w = (uint8_t *)out;
    uint8_t *c = (uint8_t *)ctx;

    if (*(const int32_t *)(c + SIGHT_OFF_DEST_DIST) > AM2_AI_ARRIVED_DIST) {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(o + OBJ_OFF_SCRIPT_STATE);
        orig_ai_common(obj, out, ctx, 0);
    } else {
        uint8_t hit = *(const uint8_t *)(o + OBJ_OFF_HIT_DIR);

        *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

        if (hit) {
            if (!*(const void *const *)(c + SIGHT_OFF_OBSERVER))
                w[1] = hit;
            *(o + OBJ_OFF_HIT_DIR) = 0;
        }

        AiPromoteFound(o, c);
    }

    AiPromoteFound(o, c);

    if (*(const void *const *)(c + SIGHT_OFF_OBSERVER)
        && (uint32_t)(*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                      - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0))
           >= AM2_AI_TURN_DELAY_MS)
        w[1] = *(const uint8_t *)(c + SIGHT_OFF_BEARING);

    ConsiderSighting(obj, out, ctx);
}

/* AiStepFollow -- original 0x00407C80, one caller. Mode 3, and the name comes
 * from what the context builder puts in front of it rather than from a
 * keyword: the record's SIGHT_OFF_LEADER is the object at OBJ_OFF_FOLLOW_UID,
 * and SIGHT_OFF_DEST is what ResolveFormationPoint answered for this unit
 * behind that leader. Nothing else in the family reads either field.
 *
 * AiStepDefend's shape with the GATE replaced, which is the only interesting
 * part. The other arms ask "am I still far from the place I remember"; this
 * asks two questions about the formation:
 *
 *   - is the unit further than AM2_AI_FOLLOW_SLACK from its formation slot;
 *   - or, if not, has the LEADER moved this frame -- PointsDiffer between its
 *     OBJ_OFF_POS and its OBJ_OFF_PREV_POS.
 *
 * Either sends it walking. So a follower that has caught up stands still only
 * while the leader is also standing still, and starts again the moment the
 * leader does, without waiting to fall out of formation first. That second
 * test is the whole of what makes a column move together.
 *
 * TWO POINTS RATHER THAN ONE, and it is further evidence about a field this
 * file records as unresolved. The other arms clear OBJ_OFF_SCRIPT_STATE on
 * arrival and copy it into OBJ_OFF_FIELD_C0 when they move; this clears it
 * UNCONDITIONALLY at the top -- a follower has no destination of its own --
 * and copies the FORMATION POINT into OBJ_OFF_FIELD_C0 instead. So 0xC0 takes
 * a packed point from two different sources, which is one more reading under
 * which 0xB4 is a point too.
 */
void __cdecl AiStepFollow(void *obj, void *out, void *ctx)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *w = (uint8_t *)out;
    uint8_t *c = (uint8_t *)ctx;
    int32_t  move;

    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

    move = *(const int32_t *)(c + SIGHT_OFF_LEAD_RANGE) > AM2_AI_FOLLOW_SLACK;
    if (!move) {
        const uint8_t *leader =
            *(const uint8_t *const *)(c + SIGHT_OFF_LEADER);

        if (leader
            && PointsDiffer(*(const uint32_t *)(leader + OBJ_OFF_POS),
                            *(const uint32_t *)(leader + OBJ_OFF_PREV_POS)))
            move = 1;
    }

    if (move) {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(c + SIGHT_OFF_DEST);
        orig_ai_common(obj, out, ctx, 0);
    } else {
        uint8_t hit = *(const uint8_t *)(o + OBJ_OFF_HIT_DIR);

        if (hit) {
            if (!*(const void *const *)(c + SIGHT_OFF_OBSERVER))
                w[1] = hit;
            *(o + OBJ_OFF_HIT_DIR) = 0;
        }

        AiPromoteFound(o, c);

        if (*(const void *const *)(c + SIGHT_OFF_OBSERVER)
            && (uint32_t)(*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                          - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0))
               >= AM2_AI_TURN_DELAY_MS)
            w[1] = *(const uint8_t *)(c + SIGHT_OFF_BEARING);
    }

    AiPromoteFound(o, c);
    ConsiderSighting(obj, out, ctx);
}

typedef void (__cdecl *AM2_AiBuildCtxFn)(void *obj, void *ctx);
typedef void (__cdecl *AM2_AiBodyFn)(void *obj, void *out, void *ctx);

#define orig_ai_build_ctx ((AM2_AiBuildCtxFn)(uintptr_t)ADDR_AI_BUILD_CONTEXT)
#define orig_ai_attack_body ((AM2_AiBodyFn)(uintptr_t)ADDR_AI_ATTACK_BODY)

/* AiStepAttack -- original 0x00407BD0, one caller. Mode 6, and it forwards its
 * three arguments to ADDR_AI_ATTACK_BODY and does nothing else.
 *
 * It lived in gameproc.cpp among four one-line pass-throughs grouped by shape,
 * as `Call407710`, with orig.h calling it "void(int32, int32, int32)" because
 * nothing said otherwise. Reading the dispatcher said otherwise: it is the
 * attack arm and its three arguments are the family's (obj, out, ctx). Moved
 * here and retyped. The body it forwards to is shared with mode 0, which
 * reaches it directly, so that address is named for neither mode.
 */
void __cdecl AiStepAttack(void *obj, void *out, void *ctx)
{
    orig_ai_attack_body(obj, out, ctx);
}

/* AiStep -- original 0x00407F80, two callers, both in ADDR_STEP_TYPE3. One
 * frame of AI for one object: build the context, run the arm its mode selects,
 * then record which region the object is standing in.
 *
 * THE TABLE IS THE FACT. Eight entries at ADDR_AI_JUMP_TABLE, six distinct
 * arms, and the mapping is not the order the arms are laid out in:
 *
 *     0        ADDR_AI_ATTACK_BODY, called directly
 *     1, 4, 5  AiStepTrack                            (5 is `evade`)
 *     2        AiStepIgnore                           (`ignore`)
 *     3        AiStepFollow                           (`follow`, from the body)
 *     6        AiStepAttack -> the same body as 0     (`attack`)
 *     7        AiStepDefend                           (`defend`)
 *
 * The keyword names come from tests/actions-reference.txt, which settled them
 * from the shipped scripts before any of this was read.
 *
 * THE BOUND IS UNSIGNED. `cmp eax, 7; ja` sends anything above 7 -- and any
 * NEGATIVE mode, which is the same thing to `ja` -- to the arm 1, 4 and 5
 * already share. So that arm is the default as well as three modes, and a
 * mode field left as garbage lands there rather than faulting.
 *
 * The context is 0x44 bytes of stack, built fresh every frame and never kept.
 * Both callers pass `&obj[OBJ_OFF_FIELD_578]` as `out`, so what every arm
 * writes as `out[1]` is a byte inside the object itself -- the heading it
 * wants -- and not an output parameter in any useful sense.
 *
 * THE TAIL RUNS ONLY IF AN ARM DID. A null object returns before the context
 * is built and skips the region write with it; that is the original's one
 * guard and its only early exit.
 */
void __cdecl AiStep(void *obj, void *out)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t  ctx[AM2_AI_CONTEXT_BYTES];

    if (!obj)
        return;

    orig_ai_build_ctx(obj, ctx);

    switch (*(const uint32_t *)(o + OBJ_OFF_AI_MODE)) {
    case 0:  orig_ai_attack_body(obj, out, ctx); break;
    case 2:  AiStepIgnore(obj, out, ctx);        break;
    case 3:  AiStepFollow(obj, out, ctx);        break;
    case 6:  AiStepAttack(obj, out, ctx);        break;
    case 7:  AiStepDefend(obj, out, ctx);        break;
    default: AiStepTrack(obj, out, ctx);         break;
    }

    *(uint16_t *)(o + OBJ_OFF_REGION) =
        (*(const uint8_t *const *)(uintptr_t)ADDR_REGION_OF_CELL)
            [*(const uint16_t *)(o + OBJ_OFF_TILE)];
}

typedef void (__cdecl *AM2_AiTrooperStepFn)(void *obj, void *out, void *ctx);
#define orig_ai_trooper_step \
    ((AM2_AiTrooperStepFn)(uintptr_t)ADDR_AI_TROOPER_STEP)

/* AiKeepRange -- original 0x00405100, six call sites in five functions across
 * the trooper AI band. Keep the unit at the range it wants from what it can
 * see: walk to a spot at that range, re-picked every five seconds, choose a
 * pose while it waits, and face the target.
 *
 * IT ONLY REPOSITIONS WHEN THE ENEMY IS TOO CLOSE. `SIGHTC_OFF_RANGE` greater
 * than `SIGHTC_OFF_WANT_RANGE` skips the whole middle and goes straight to the
 * turn, so this backs a unit OFF and never closes. Which is what makes the
 * argument order below matter.
 *
 * RandomPointToward IS PASSED THE ENEMY AS THE MOVER. Its parameters are
 * (target, obj, dist, out): it takes the heading from `obj` to `target` and
 * steps `dist` from `obj`. This call passes the unit as `target` and the
 * OBSERVER as `obj`, so the point is `want` away from the ENEMY, on the side
 * the unit is already on, with the +/-32 spread that function applies. Reading
 * the two the other way round gives a point near the unit heading at the enemy
 * -- a unit that charges instead of backing off -- and air.cpp's own comment
 * warns about exactly this, both arguments being the same type.
 *
 * TWO DEADLINES ON ONE FIELD. OBJ_OFF_DEADLINE_58 is used as "when the current
 * walk expires" at the top and as "when this unit last settled" in the middle,
 * and the middle sets it to `now + AM2_AI_KEEP_RANGE_MS` after ordering a
 * walk, which is what makes the top's test fire for the next five seconds. So
 * a walk in progress short-circuits everything until the unit is within
 * AM2_AI_REACHED_DIST of OBJ_OFF_FIELD_C0, at which point the field is cleared
 * and the middle starts the clock again from zero.
 *
 * The pose is written only in the gap: not while a walk is outstanding, not
 * for a soldier kind of 6 or more, not when OBJ_OFF_FIELD_540 is set, not for
 * SIGHTC_OFF_KIND 3, and not when SIGHTC_OFF_FIELD_00 is non-zero. Five gates
 * for one dword, and 7 or 5 by whether SIGHTC_OFF_FIELD_3C is under 4.
 *
 * AND `out` IS NOT THE VEHICLE FAMILY'S. Those arms write a heading at out[1];
 * this writes one at out[4] and a pose at out[8]. Same three-argument shape,
 * different record -- worth checking before carrying an offset across.
 */
void __cdecl AiKeepRange(void *obj, void *out, void *ctx)
{
    uint8_t       *o   = (uint8_t *)obj;
    uint8_t       *w   = (uint8_t *)out;
    const uint8_t *c   = (const uint8_t *)ctx;
    uint32_t       now = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    if (*(const uint32_t *)(o + OBJ_OFF_DEADLINE_58) > now) {
        if (ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                       (const AM2_Point *)(o + OBJ_OFF_FIELD_C0))
            >= AM2_AI_REACHED_DIST) {
            orig_ai_trooper_step(obj, out, ctx);
            return;
        }
        *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) = 0;
    }

    if (!*(const void *const *)(c + SIGHTC_OFF_OBSERVER))
        return;

    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) < 6
        && *(const int32_t *)(c + SIGHTC_OFF_RANGE)
           <= *(const int32_t *)(c + SIGHTC_OFF_WANT_RANGE)) {
        if (*(const uint32_t *)(o + OBJ_OFF_DEADLINE_58) == 0)
            *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) = now;

        if (now - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)
            > AM2_AI_KEEP_RANGE_MS) {
            RandomPointToward(obj,
                              *(const void *const *)(c + SIGHTC_OFF_OBSERVER),
                              *(const int32_t *)(c + SIGHTC_OFF_WANT_RANGE),
                              (AM2_Point *)(o + OBJ_OFF_FIELD_C0));
            orig_ai_trooper_step(obj, out, ctx);
            *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) =
                now + AM2_AI_KEEP_RANGE_MS;
        } else if (!*(const int32_t *)(o + OBJ_OFF_FIELD_540)
                   && *(const int32_t *)(c + SIGHTC_OFF_KIND) != 3
                   && *(const uint8_t *)(c + SIGHTC_OFF_FIELD_3C) < 0x10
                   && *(const int32_t *)(c + SIGHTC_OFF_FIELD_00) == 0) {
            *(int32_t *)(w + 8) =
                *(const uint8_t *)(c + SIGHTC_OFF_FIELD_3C) < 4 ? 7 : 5;
        }
    }

    if (now - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0)
        >= AM2_AI_TURN_DELAY_MS)
        w[4] = *(const uint8_t *)(c + SIGHTC_OFF_BEARING);
}

typedef void (__cdecl *AM2_AiHitReactFn)(void *obj, void *out, void *ctx);
#define orig_ai_hit_react ((AM2_AiHitReactFn)(uintptr_t)ADDR_AI_HIT_REACT)

/* AiWalkStep -- original 0x00405D30, two callers. The trooper family's minimal
 * arm, and the exact counterpart of AiStepIgnore on the vehicle side: while the
 * unit is further than AM2_AI_REACHED_DIST from where it is going, advance the
 * walk and do nothing else; once it is there, forget the destination, react to
 * anything that hit it, and face what it can see. The name is ours.
 *
 * The two families line up field for field and neither shares a constant:
 *
 *     vehicle                          trooper
 *     SIGHT_OFF_DEST_DIST  0x28        SIGHTC_OFF_DEST_DIST  0x34
 *     AM2_AI_ARRIVED_DIST  0x20        AM2_AI_REACHED_DIST   0x0C
 *     out[1] is the heading            out[4] is the heading
 *     the hit is handled INLINE        ADDR_AI_HIT_REACT does it, ten sites
 *
 * So the resemblance is at the level of the design and not of the code, which
 * is the opposite of AiStepTrack and AiStepDefend -- those were one function
 * emitted twice. Worth keeping the two cases apart: one calls for a diff of
 * the disassembly, the other for a table like the one above.
 *
 * OBJ_OFF_FIELD_C0 IS WRITTEN BEFORE THE WALK AND CLEARED AFTER IT, and it is
 * OBJ_OFF_SCRIPT_STATE that supplies it -- `obj[0xC0] = obj[0xB4]` on the way
 * out and `obj[0xB4] = ADDR_ZERO_POINT` on arrival, the same pair the vehicle
 * arms perform. A sixth reading under which both fields hold packed points;
 * still not renamed, for the reason recorded at 0xB4.
 */
void __cdecl AiWalkStep(void *obj, void *out, void *ctx)
{
    uint8_t       *o = (uint8_t *)obj;
    uint8_t       *w = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)ctx;

    if (*(const int32_t *)(c + SIGHTC_OFF_DEST_DIST) > AM2_AI_REACHED_DIST) {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(o + OBJ_OFF_SCRIPT_STATE);
        orig_ai_trooper_step(obj, out, ctx);
        return;
    }

    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

    orig_ai_hit_react(obj, out, ctx);

    if (*(const int32_t *)(c + SIGHTC_OFF_FIELD_20)
        && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
           - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0)
           >= AM2_AI_TURN_DELAY_MS)
        w[4] = *(const uint8_t *)(c + SIGHTC_OFF_BEARING);
}

/* ConsiderSighting -- original 0x004074A0, four callers.
 *
 * One observer against one object. Four gates -- both of the record's enable
 * flags, a positive range, and a range below the maximum -- then a bearing
 * within three of the observer's own, and only then does the output record get
 * filled and the reveal considered.
 *
 * THE OUTPUT IS FILLED BEFORE THE OWNERSHIP TEST AND THE REVEAL AFTER IT. So a
 * caller always gets the sighting's numbers when the geometry passes, and the
 * two-second reveal happens only when the OBSERVER belongs to us or an ally.
 * Splitting those two would be the obvious tidy-up and would change what the
 * caller sees.
 *
 * IT READS THE OBSERVER OUT OF THE RECORD FOUR SEPARATE TIMES, once per field
 * it copies, rather than holding it. Nothing between can change it; written as
 * one load, which is what it means.
 *
 * THE BEARING COMPARISON IS `|AngleDelta| <= 3` ON A BYTE. AngleDelta wraps in
 * both directions, so this is a cone of about eight degrees either side --
 * narrow, and the reason a unit does not reveal everything around it.
 *
 * A SECOND READER OF OBJ_OFF_FIELD_530, AND IT DISAGREES WITH THE FIRST.
 * SetObjField530 treats that field as a small state, 0..6, indexing a table of
 * animation frames; this reads its low byte as an 8-BIT BEARING and hands it
 * to AngleDelta. Both cannot be describing the same quantity unless the states
 * double as headings. The field is named for its offset precisely because of
 * this kind of disagreement -- recorded rather than resolved, the way
 * OBJ_OFF_CHAIN_UID's two readings are.
 *
 * The absolute value is the original's `cdq; xor; sub` and the comparison is
 * on `al`, so a delta above 255 could not arise -- AngleDelta answers
 * -128..128.
 */
void __cdecl ConsiderSighting(void *seen, void *out, const void *sight)
{
    uint8_t       *s = (uint8_t *)seen;
    uint8_t       *o = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)sight;
    const uint8_t *observer;
    int32_t        range;
    int32_t        delta;

    if (!*(const int32_t *)(c + SIGHT_OFF_ENABLED_30))
        return;
    if (!*(const int32_t *)(c + SIGHT_OFF_ENABLED_40))
        return;

    range = *(const int32_t *)(c + SIGHT_OFF_RANGE);
    if (range <= 0 || range >= *(const int32_t *)(c + SIGHT_OFF_MAX_RANGE))
        return;

    delta = AngleDelta(*(const uint8_t *)(s + OBJ_OFF_FIELD_530),
                       *(const uint8_t *)(c + SIGHT_OFF_BEARING));
    if (delta < 0)
        delta = -delta;
    if ((uint8_t)delta > AM2_SIGHT_CONE)
        return;

    *(int32_t *)(o + SIGHTOUT_OFF_HIT) = 1;

    observer = *(const uint8_t *const *)(c + SIGHT_OFF_OBSERVER);
    if (!observer)
        return;

    *(int16_t *)(o + SIGHTOUT_OFF_X) =
        *(const int16_t *)(observer + OBJ_OFF_X);
    *(int16_t *)(o + SIGHTOUT_OFF_Y) =
        *(const int16_t *)(observer + OBJ_OFF_Y);
    *(int16_t *)(o + SIGHTOUT_OFF_YADJ) =
        *(const int16_t *)(observer + OBJ_OFF_ROW0_Y_ADJUST);
    *(uint32_t *)(o + SIGHTOUT_OFF_UID) =
        ((const AM2_Object *)observer)->uid;

    if (!ObjIsOurs((void *)observer, 1))
        return;

    RevealObj(s);
    *(int32_t *)(s + OBJ_OFF_REVEALED_UNTIL) =
        *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS + AM2_REVEAL_MS;
}

/* ConsiderSightingB -- original 0x00408580, one caller, and
 * ConsiderSighting's sibling. Written out beside it rather than shared,
 * because four things differ and two of them are the record.
 *
 * THE RECORD IS NOT THE SAME LAYOUT. Its enable is one field at +0x3C where
 * the other tests two at +0x30 and +0x40, and its maximum range is at +0x38
 * where the other's is at +0x3C. So +0x3C is a RANGE in one and an ENABLE in
 * the other; both readings are literally what the instructions do, so the two
 * records differ rather than one of them being misread. The three fields they
 * share -- observer, range, bearing -- agree exactly.
 *
 * ITS CONE IS EIGHT, NOT THREE, and the comparison is `>=` on a byte where the
 * other is `>`. Wider, and off by one relative to it.
 *
 * THE BEARING IT COMPARES IS THE OUT RECORD'S, NOT THE SEEN OBJECT'S. The
 * other reads OBJ_OFF_FIELD_530 off the object; this reads a byte the out
 * record carries and, in its tail, WRITES the record's bearing back there. So
 * the out record accumulates a bearing across calls and this one is a step in
 * a sequence rather than a standalone test.
 *
 * THE TAIL RUNS ON EVERY PATH, including the ones that refuse. It commits a
 * recorded hit -- the bearing, the state 4, and clearing the flag -- and the
 * flag it reads is memory rather than a local, so a refusal after a hit that
 * was never committed would still commit it. Within one call the flag is set
 * and cleared by the same call, so that cannot arise from here; it is written
 * as the two branches the original has rather than folded into the success
 * path, because folding would lose that.
 */
void __cdecl ConsiderSightingB(void *seen, void *out, const void *sight)
{
    uint8_t       *s = (uint8_t *)seen;
    uint8_t       *o = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)sight;

    if (*(const int32_t *)(c + SIGHTB_OFF_ENABLED)) {
        int32_t range = *(const int32_t *)(c + SIGHT_OFF_RANGE);
        int32_t delta;

        if (range > 0
            && range < *(const int32_t *)(c + SIGHTB_OFF_MAX_RANGE)) {

            delta = AngleDelta(*(const uint8_t *)(o + SIGHTBOUT_OFF_BEARING),
                               *(const uint8_t *)(c + SIGHT_OFF_BEARING));
            if (delta < 0)
                delta = -delta;

            if ((uint8_t)delta < AM2_SIGHT_CONE_B) {
                const uint8_t *observer =
                    *(const uint8_t *const *)(c + SIGHT_OFF_OBSERVER);

                *(int32_t *)(o + SIGHTOUT_OFF_HIT) = 1;

                if (observer) {
                    *(int16_t *)(o + SIGHTBOUT_OFF_X) =
                        *(const int16_t *)(observer + OBJ_OFF_X);
                    *(int16_t *)(o + SIGHTBOUT_OFF_Y) =
                        *(const int16_t *)(observer + OBJ_OFF_Y);
                    *(int16_t *)(o + SIGHTBOUT_OFF_YADJ) =
                        *(const int16_t *)(observer + OBJ_OFF_ROW0_Y_ADJUST);
                    *(uint32_t *)(o + SIGHTBOUT_OFF_UID) =
                        ((const AM2_Object *)observer)->uid;

                    if (ObjIsOurs((void *)observer, 1)) {
                        RevealObj(s);
                        *(int32_t *)(s + OBJ_OFF_REVEALED_UNTIL) =
                            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                            + AM2_REVEAL_MS;
                    }
                }
            }
        }
    }

    if (*(const int32_t *)(o + SIGHTOUT_OFF_HIT)) {
        *(uint8_t *)(o + SIGHTBOUT_OFF_BEARING) =
            *(const uint8_t *)(c + SIGHT_OFF_BEARING);
        *(int32_t *)(o + SIGHTOUT_OFF_HIT)   = 0;
        *(int32_t *)(o + SIGHTBOUT_OFF_STATE) = AM2_SIGHTB_STATE_HIT;
    }
}

/* ConsiderSightingC -- original 0x00404F40, four callers, and the third member
 * of this family. Same skeleton as the other two; three things are new.
 *
 * THE MAXIMUM RANGE HAS A MAGIC VALUE AND IT CUTS BOTH WAYS. When the record's
 * maximum is exactly 0x1000, a bearing outside the cone is FORGIVEN and the
 * reveal is SUPPRESSED. One constant, two opposite-seeming jobs: it sees in
 * every direction and tells nobody. Writing the two tests as separate ideas
 * would hide that they are the same number.
 *
 * IT HAS A SECOND TAIL THE OTHERS HAVE NOT, and that tail runs on EVERY path,
 * including the ones that never looked at the geometry: a record of kind 3
 * whose range is in bounds bumps the out record's state from 2 to 3. The range
 * is re-tested there rather than reusing the earlier answer, so the bump can
 * happen on a call whose cone test refused.
 *
 * ITS LAST TAIL WRITES THE SEEN OBJECT, not just the out record.
 * `OBJ_OFF_FIELD_578` is set to 1 on any recorded hit -- the field
 * SetSoldierKind clears for every kind -- which makes this the only one of the
 * three that reaches back into the object it was asked about for a reason
 * other than the reveal.
 *
 * A THIRD RECORD LAYOUT that nearly lines up with the first: observer, range
 * and bearing shift by exactly four, but the enables and the maximum do not.
 * So the three are related and distinct rather than one record with a longer
 * header, and the offsets are named per family.
 *
 * As in ConsiderSightingB the bearing compared is the OUT record's -- but
 * unlike B this one never writes it back, so the accumulation is somebody
 * else's job here.
 */
void __cdecl ConsiderSightingC(void *seen, void *out, const void *sight)
{
    uint8_t       *s = (uint8_t *)seen;
    uint8_t       *o = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)sight;
    int32_t        maxRange = *(const int32_t *)(c + SIGHTC_OFF_MAX_RANGE);

    if (*(const int32_t *)(c + SIGHTC_OFF_ENABLED_40)
        && *(const int32_t *)(c + SIGHTC_OFF_ENABLED_54)) {
        int32_t range = *(const int32_t *)(c + SIGHTC_OFF_RANGE);

        if (range > 0 && range < maxRange) {
            int32_t delta =
                AngleDelta(*(const uint8_t *)(o + SIGHTCOUT_OFF_BEARING),
                           *(const uint8_t *)(c + SIGHTC_OFF_BEARING));

            if (delta < 0)
                delta = -delta;

            if ((uint8_t)delta < AM2_SIGHT_CONE_B
                || maxRange == AM2_SIGHT_OMNI_RANGE) {
                const uint8_t *observer =
                    *(const uint8_t *const *)(c + SIGHTC_OFF_OBSERVER);

                *(int32_t *)(o + SIGHTCOUT_OFF_HIT) = 1;

                if (observer) {
                    *(int16_t *)(o + SIGHTCOUT_OFF_X) =
                        *(const int16_t *)(observer + OBJ_OFF_X);
                    *(int16_t *)(o + SIGHTCOUT_OFF_Y) =
                        *(const int16_t *)(observer + OBJ_OFF_Y);
                    *(int16_t *)(o + SIGHTCOUT_OFF_YADJ) =
                        *(const int16_t *)(observer + OBJ_OFF_ROW0_Y_ADJUST);
                    *(uint32_t *)(o + SIGHTCOUT_OFF_UID) =
                        ((const AM2_Object *)observer)->uid;

                    if (ObjIsOurs((void *)observer, 1)
                        && maxRange != AM2_SIGHT_OMNI_RANGE) {
                        RevealObj(s);
                        *(int32_t *)(s + OBJ_OFF_REVEALED_UNTIL) =
                            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                            + AM2_REVEAL_MS;
                    }
                }
            }
        }
    }

    if (*(const int32_t *)(c + SIGHTC_OFF_KIND) == 3
        && *(const int32_t *)(c + SIGHTC_OFF_RANGE) > 0
        && *(const int32_t *)(c + SIGHTC_OFF_RANGE) < maxRange
        && *(const int32_t *)(o + SIGHTCOUT_OFF_STATE) == 2)
        *(int32_t *)(o + SIGHTCOUT_OFF_STATE) = 3;

    if (*(const int32_t *)(o + SIGHTCOUT_OFF_HIT)) {
        *(int32_t *)(o + SIGHTCOUT_OFF_SEEN)  = 1;
        *(int32_t *)(s + OBJ_OFF_FIELD_578)   = 1;
    }
}

/* SealMapEdges -- original 0x0042BCF0, one caller.
 *
 * Seal the map's four edges with a full cell weight, then walk every tile once
 * and settle three things about it.
 *
 * THE THREE PASSES ARE NOT INTERCHANGEABLE. The first two write weights; the
 * third READS the weight it may itself have just written -- a tile marked
 * AM2_TILE_OPEN is given a full weight and then, two instructions later, has
 * AM2_TILE_BLOCKS set because of it. So "open" ends up implying "blocks",
 * which is the opposite of what the two names suggest, and it happens inside
 * one iteration rather than between passes.
 *
 * AM2_TILE_OPEN's polarity is already recorded as inverted against its name --
 * `BlockWeightChain` penalises a tile whose bit 0 is CLEAR. This is the other
 * half of that: the bit being SET is what makes the tile impassable here.
 *
 * THE EDGE MARGIN IS COMPUTED FROM THE HEIGHT ON BOTH AXES. The band is
 * `5 <= y <= height - 5` and `5 <= x <= height - 5` -- the same `height - 5`,
 * not `width - 5`. On a square map the two agree and nothing shows; on a wider
 * one the x band is short and a column of tiles near the right edge would go
 * unflagged, while on a taller one it runs past the width. Reproduced exactly:
 * it is a single register the original computes once and compares twice, so
 * this is what the binary does rather than a transcription slip.
 *
 * THE WEIGHT COMPARISON IS SIGNED, as everywhere else that reads this grid, so
 * a weight of 0x80 or more reads as negative and does NOT get the blocks bit.
 *
 * The first pass walks x and touches two rows; the second walks y and touches
 * two columns, indexing the LEFT edge of row y and the RIGHT edge of row y-1
 * from one multiply. The corners are therefore written more than once, which
 * costs nothing and is why the second pass starts at 1 rather than 0.
 *
 * Every global is re-read on every iteration -- the width, the height, both
 * grids. Nothing here can move them; written as the plain loops that means.
 */
void __cdecl SealMapEdges(void)
{
    int32_t  w = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    int32_t  h = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H;
    int8_t  *weights = *(int8_t **)(uintptr_t)ADDR_CELL_WEIGHTS;
    uint8_t *flags;
    int32_t  x, y, i;

    for (x = 0; x < w; x++) {
        weights[x]                 = (int8_t)AM2_CELL_WEIGHT_STEP;
        weights[(h - 1) * w + x]   = (int8_t)AM2_CELL_WEIGHT_STEP;
    }

    for (y = 1; y < h; y++) {
        weights[w * y - 1] = (int8_t)AM2_CELL_WEIGHT_STEP;
        weights[w * y]     = (int8_t)AM2_CELL_WEIGHT_STEP;
    }

    flags = *(uint8_t **)(uintptr_t)ADDR_TILE_FLAGS;

    for (i = 0, y = 0; y < h; y++) {
        for (x = 0; x < w; x++, i++) {
            if (flags[i] & AM2_TILE_OPEN)
                weights[i] = (int8_t)AM2_CELL_WEIGHT_STEP;

            if (weights[i] >= (int8_t)AM2_CELL_WEIGHT_STEP)
                flags[i] |= AM2_TILE_BLOCKS;

            /* `h - AM2_EDGE_MARGIN` on BOTH axes -- see above. */
            if (!(y >= AM2_EDGE_MARGIN && y <= h - AM2_EDGE_MARGIN
                  && x >= AM2_EDGE_MARGIN && x <= h - AM2_EDGE_MARGIN))
                flags[i] |= AM2_TILE_NEAR_EDGE;
        }
    }
}

/* UnrevealArea -- original 0x0043A330, one caller.
 *
 * Take visibility away over a five-by-five block of tiles, in the reveal grid
 * of every army allied to the one given. Each byte is DECREMENTED, so the
 * grids are counts rather than flags and this is one half of a matched pair.
 *
 * IT INDEXES THE BLOCK'S CORNER BY HEIGHT AND STEPS IT BY WIDTH. The base is
 * `height * y0 + x0` and the per-row advance works out to exactly `width`.
 * Both cannot be right unless the map is square -- and it is: MapDescInit
 * sizes the grid `cols << Log2Mask(cols)`, so width and height agree in every
 * shipped map and the inconsistency never shows. Reproduced as written.
 *
 * That is the SECOND function in this file to measure one axis with the
 * other's extent, after SealMapEdges' border margin. Two independent
 * occurrences make it a habit of the original rather than a slip, and both are
 * invisible for the same reason.
 *
 * THE ARMY IS REFUSED AT 4 AND ABOVE with no lower bound, so a negative army
 * would index the alliance test out of range. Four is the neutral army
 * everywhere else in this tree, so the test reads as "a real army only".
 *
 * THE CLAMPS ARE ASYMMETRIC BETWEEN THE AXES: x is clamped to `width - 1` and
 * y to `height - 1`, which is the one place the two extents are used
 * correctly. So the corner arithmetic and the clamping disagree with each
 * other about which extent belongs to which axis.
 *
 * THE INDEX IS MASKED TO SIXTEEN BITS on every write -- `and ecx, 0xFFFF`
 * inside the inner loop -- so a block near the end of a large grid wraps to
 * the start rather than running off it. The same sixteen-bit tile index the
 * cover functions use.
 *
 * An empty range in either axis is skipped rather than looping backwards; the
 * two guards are separate, so an empty row range skips the whole thing while
 * an empty column range still walks the rows.
 */
void __cdecl UnrevealArea(int32_t army, uint32_t at)
{
    int32_t w = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    int32_t h = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H;
    uint32_t tile = (uint32_t)TileOfPoint(at) & 0xFFFFu;
    int32_t x = (int32_t)(tile & (uint32_t)(w - 1));
    int32_t y = (int32_t)(tile >> *(const uint8_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT);
    int32_t x0, x1, y0, y1, slot;

    if (army >= AM2_REVEAL_ARMIES)
        return;

    x0 = Clamp(x - AM2_REVEAL_RADIUS, 0, w - 1);
    x1 = Clamp(x + AM2_REVEAL_RADIUS, 0, w - 1);
    y0 = Clamp(y - AM2_REVEAL_RADIUS, 0, h - 1);
    y1 = Clamp(y + AM2_REVEAL_RADIUS, 0, h - 1);

    for (slot = 0; slot < AM2_REVEAL_ARMIES; slot++) {
        uint8_t *grid;
        int32_t  at_i;
        int32_t  rows;

        if (!ArmiesAllied(army, slot))
            continue;

        grid = ((uint8_t **)(uintptr_t)ADDR_TILE_REVEAL_GRIDS)[slot];
        at_i = h * y0 + x0;         /* height for the corner; see above */

        if (y0 > y1)
            continue;

        for (rows = y1 - y0 + 1; rows; rows--) {
            if (x0 <= x1) {
                int32_t cols = x1 - x0 + 1;

                for (; cols; cols--, at_i++)
                    grid[(uint32_t)at_i & 0xFFFFu]--;
            }

            at_i += w - x1 + x0 - 1;   /* width for the stride; see above */
        }
    }
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
    rc |= patch_replace(ADDR_AI_STEP_IGNORE, (const void *)AiStepIgnore,
                        "AiStepIgnore", 1);
    rc |= patch_replace(ADDR_AI_STEP_DEFEND, (const void *)AiStepDefend,
                        "AiStepDefend", 1);
    rc |= patch_replace(ADDR_AI_STEP_TRACK, (const void *)AiStepTrack,
                        "AiStepTrack", 1);
    rc |= patch_replace(ADDR_AI_STEP_FOLLOW, (const void *)AiStepFollow,
                        "AiStepFollow", 1);
    rc |= patch_replace(ADDR_AI_STEP_ATTACK, (const void *)AiStepAttack,
                        "AiStepAttack", 1);
    rc |= patch_replace(ADDR_AI_STEP, (const void *)AiStep, "AiStep", 2);
    rc |= patch_replace(ADDR_AI_KEEP_RANGE, (const void *)AiKeepRange,
                        "AiKeepRange", 6);
    rc |= patch_replace(ADDR_AI_WALK_STEP, (const void *)AiWalkStep,
                        "AiWalkStep", 2);
    rc |= patch_replace(ADDR_SETTLE_POINT_IN_REGION,
                        (const void *)SettlePointInRegion,
                        "SettlePointInRegion", 5);
    rc |= patch_replace(ADDR_MIDDLE_REGION_LINK, (const void *)MiddleRegionLink,
                        "MiddleRegionLink", 3);
    rc |= patch_replace(ADDR_REGION_HOPS, (const void *)RegionHops,
                        "RegionHops", 1);
    rc |= patch_replace(ADDR_REGIONS_NEAR, (const void *)RegionsNear,
                        "RegionsNear", 2);
    rc |= patch_replace(ADDR_ADD_REGION_LINK, (const void *)AddRegionLink,
                        "AddRegionLink", 2);
    rc |= patch_replace(ADDR_UNREVEAL_AREA, (const void *)UnrevealArea,
                        "UnrevealArea", 1);
    rc |= patch_replace(ADDR_SEAL_MAP_EDGES, (const void *)SealMapEdges,
                        "SealMapEdges", 1);
    rc |= patch_replace(ADDR_CONSIDER_SIGHTING_C,
                        (const void *)ConsiderSightingC,
                        "ConsiderSightingC", 4);
    rc |= patch_replace(ADDR_CONSIDER_SIGHTING_B,
                        (const void *)ConsiderSightingB,
                        "ConsiderSightingB", 1);
    rc |= patch_replace(ADDR_CONSIDER_SIGHTING,
                        (const void *)ConsiderSighting,
                        "ConsiderSighting", 4);
    rc |= patch_replace(ADDR_TILE_REGION_OR_BORROW,
                        (const void *)TileRegionOrBorrow,
                        "TileRegionOrBorrow", 2);
    rc |= patch_replace(ADDR_REBUILD_TILE_COVER,
                        (const void *)RebuildTileCover,
                        "RebuildTileCover", 1);
    rc |= patch_replace(ADDR_OBJ_BOX_ACTION, (const void *)ObjBoxAction,
                        "ObjBoxAction", 2);
    rc |= patch_replace(ADDR_BOX_ACTION, (const void *)BoxAction,
                        "BoxAction", 5);
    rc |= patch_replace(ADDR_LIST_BOX_ACTION, (const void *)ListBoxAction,
                        "ListBoxAction", 3);
    rc |= patch_replace(ADDR_TILE_COVER_ADD, (const void *)TileCoverAdd,
                        "TileCoverAdd", 3);
    rc |= patch_replace(ADDR_TILE_COVER_SUB, (const void *)TileCoverSub,
                        "TileCoverSub", 2);
    rc |= patch_replace(ADDR_MARK_OPEN_TILE, (const void *)MarkOpenTile,
                        "MarkOpenTile", 1);
    rc |= patch_replace(ADDR_ACTIVATE_REGION, (const void *)ActivateRegion,
                        "ActivateRegion", 1);
    rc |= patch_replace(ADDR_BEGIN_MOVE_TO, (const void *)BeginMoveTo,
                        "BeginMoveTo", 4);
    rc |= patch_replace(ADDR_NEAREST_ALLOWED_TILE,
                        (const void *)NearestAllowedTile,
                        "NearestAllowedTile", 6);
    rc |= patch_replace(ADDR_INACTIVATE_REGION, (const void *)InactivateRegion,
                        "InactivateRegion", 1);
    return rc;
}
