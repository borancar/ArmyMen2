/* region.cpp -- see region.h. */
#include <stdint.h>
#include <string.h>

#include "misc.h"   /* CollapseEqualDeltas -- reconstructed */
#include "rect.h"   /* Clamp -- reconstructed */
#include "dist.h"   /* AngleDelta, RoundTo8 -- reconstructed */
#include "trig.h"   /* Cos8, Sin8 -- reconstructed */
#include "anim.h"   /* AM2_Anim */
#include "army.h"   /* ObjIsOurs -- reconstructed */
#include "air.h"    /* RevealObj -- reconstructed */

#include "region.h"
#include "objtype.h"  /* ObjIsType3, ObjIsType8 */
#include "map.h"      /* TileOfPoint -- reconstructed */
#include "maprow.h"   /* RowAnimField4 -- reconstructed */
#include "msgslot.h"  /* CommMustBroadcast -- the timeout kill's gate */
#include "armymsg.h"  /* DamageBroadcast */
#include "item.h"     /* ObjClearFootprint, ObjClearRoachFootprint */
#include "gameproc.h" /* Call405220 -- the `defend` arm's thunk */
#include "item.h"     /* ObjectsHitByPoint -- reconstructed */
#include "image.h"
#include "crt.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kRegionOfCell (*(uint8_t **)AM2_IMAGE(ADDR_REGION_OF_CELL))
/* Spelled as army.cpp spells it, so checkglobals sees one name per
 * address rather than two. */
#define g_armyObjLists ((void **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)
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
        RegionSolvePair(from, to);
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

/* MoveStepPoint -- original 0x00428E40, six call sites in five functions: the
 * roach steppers, two in the vehicle band and 0x0040DA70. Where would this
 * object be after one frame, heading that way at that speed? It writes the
 * point and moves nothing.
 *
 * THE HEADING IS SNAPPED TO THE ANIMATION'S FACINGS, and that is the whole
 * reason the function needs the object rather than just a point.
 * `AM2_Anim::directionBits` is `Log2Mask(directions)`, so
 * `RoundTo8(heading, bits) << (8 - bits)` rounds an 8-bit heading to one of
 * the animation's evenly spaced facings and puts it back in 8-bit form. A
 * sprite with eight directions therefore moves in eight directions, not 256.
 * Only when OBJ_FLAG_SNAP_HEADING is set; otherwise the heading is used as given.
 *
 * THE ROW POINTER IS DEREFERENCED BEFORE IT IS TESTED. `rows` is left NULL
 * when OBJ_OFF_ROW_COUNT is not positive and then read at
 * ROW_OFF_ANIM_PLAYING regardless -- address 0x44, which faults. The `rows`
 * test inside the snap is therefore vacuous: anything that got that far has
 * already dereferenced it. Reproduced, since the callers evidently guarantee
 * rows and the alternative is a different program.
 *
 * THE STEP IS CLAMPED AWAY FROM ZERO, NOT UP FROM IT. `speed * frameSeconds`
 * is forced to a magnitude of at least AM2_MOVE_MIN_STEP with its sign kept:
 * a value in [0, 2) becomes +2 and one in (-2, 0) becomes -2. So a very slow
 * object still moves a whole step each frame, and a backwards one still backs
 * up. Four x87 compares in the original, which is what makes the two-sided
 * shape easy to miss.
 *
 * OBJ_OFF_SUBPIXEL_X and _Y are added before the truncation, which is what
 * they are for: the fraction a step loses to `_ftol` is carried in them rather
 * than thrown away, so a speed that does not divide the frame still averages
 * out. Nothing here writes them.
 *
 * Two arguments earn a note. The FIFTH is never read -- both the count and the
 * one call site checked push seven, and nothing in the body touches that slot,
 * the same shape as RandomPointAhead's first. And the SIXTH adds 0x80 to the
 * heading, which is half a turn: it is the reverse flag.
 *
 * Arithmetic note, and the same one FormationPoint carries. The original keeps
 * the product in x87 and truncates once through `_ftol`; this uses double,
 * which is exact for these magnitudes, with a C cast for the truncation. The
 * intermediate `speed * frameSeconds` IS stored to a float by the original --
 * `fst dword` -- so that one is a float here too rather than a double.
 */
int32_t __cdecl MoveStepPoint(void *obj, int32_t heading, int32_t turn,
                              int32_t speed, int32_t unused, int32_t flip,
                              void *outPt)
{
    uint8_t         *o    = (uint8_t *)obj;
    AM2_Point       *out  = (AM2_Point *)outPt;
    uint8_t         *rows = (uint8_t *)0;
    AM2_Anim        *anim;
    uint8_t          head;
    float            step;

    (void)unused;

    if (!*(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS)
        return 0;

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 0)
        rows = *(uint8_t **)(o + OBJ_OFF_ROWS);

    anim = *(AM2_Anim **)(rows + ROW_OFF_ANIM_PLAYING);
    if (!anim)
        return 0;

    head = (uint8_t)heading;
    if ((*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SNAP_HEADING) && rows) {
        uint8_t bits = anim->directionBits;

        head = (uint8_t)((uint8_t)RoundTo8(head, bits) << (8 - bits));
    }

    if (flip)
        head = (uint8_t)(head + 0x80);
    head = (uint8_t)(head + (uint8_t)turn);

    *(uint32_t *)out = *(const uint32_t *)(o + OBJ_OFF_POS);
    if (!speed)
        return 1;

    step = (float)speed * *(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC;
    if (step >= 0.0f) {
        if (step < AM2_MOVE_MIN_STEP)
            step = AM2_MOVE_MIN_STEP;
    } else if (step > -AM2_MOVE_MIN_STEP) {
        step = -AM2_MOVE_MIN_STEP;
    }

    out->x = (int16_t)(*(const int16_t *)(o + OBJ_OFF_POS)
                       + (int32_t)((double)Cos8(head) * (double)step
                                   + (double)*(const float *)
                                       (o + OBJ_OFF_SUBPIXEL_X)));
    out->y = (int16_t)(*(const int16_t *)(o + OBJ_OFF_POS + 2)
                       + (int32_t)((double)Sin8(head) * (double)step
                                   + (double)*(const float *)
                                       (o + OBJ_OFF_SUBPIXEL_Y)));
    return 1;
}

/* AnimStepPoint -- original 0x0040DA70, three callers, all in 0x0044AFB0.
 * MoveStepPoint with the speed taken from the object's CURRENT ANIMATION
 * rather than passed in. The out point is filled with the object's own
 * position first, so every exit leaves it valid.
 *
 * THE POSE INDEX IS TWO TABLE LOOKUPS DEEP. `pose` indexes
 * ADDR_WEAPON_POSE_FRAMES for an animation ID; that ID is searched for in the
 * row's own ROW_OFF_ANIM_CUR table by AM2_AnimEntry::id; and the same ID indexes
 * ADDR_FRAME_HEADING_BIAS for a byte added to the heading. So the pose decides
 * both how fast the object moves and which way it faces while doing it.
 *
 * A MISS FALLS BACK TO ENTRY ZERO rather than failing. The search runs to the
 * end and then the index is forced to 0 -- and the same forcing happens when
 * the id IS found at the last entry, because the original re-tests `i < count`
 * after the loop and that is false either way. So a match on the LAST
 * animation of a table is treated as a miss. Written out as the original has
 * it; it is a real off-by-one and not a transcription of one.
 *
 * THE SPEED IS THE ANIMATION'S, DOUBLED FOR ONE PARTICULAR REMAP. It is
 * AM2_Anim::field4, and the doubling is `ROW_OFF_FIELD_2C ==
 * ADDR_ROW_LUT_DOUBLES` -- a row whose colour remap is that specific table
 * moves twice as fast, which is a stranger coupling than it looks and is
 * reproduced without explanation.
 *
 * `fast` replaces all of that with AM2_ANIM_FAST_STEP divided by the frame's
 * seconds, which MoveStepPoint then multiplies by the same number: eight units
 * this frame, whatever the frame rate.
 *
 * Two exits before the step. No animation id at all answers 1 with the point
 * at the object's position, and a row with no animation table answers 0 with
 * the same point. The difference matters to the caller and not to the point.
 */
int32_t __cdecl AnimStepPoint(void *obj, int32_t heading, int32_t pose,
                              void *outPt, int32_t fast)
{
    uint8_t   *o    = (uint8_t *)obj;
    AM2_Point *out  = (AM2_Point *)outPt;
    uint8_t   *rows = *(uint8_t **)(o + OBJ_OFF_ROWS);
    int32_t    animId =
        ((const int32_t *)AM2_IMAGE(ADDR_WEAPON_POSE_FRAMES))[pose];
    const AM2_AnimTable *table;
    int32_t              i;
    int32_t              speed;

    out->x = *(const int16_t *)(o + OBJ_OFF_POS);
    out->y = *(const int16_t *)(o + OBJ_OFF_POS + 2);

    if (!animId)
        return 1;

    table = *(const AM2_AnimTable *const *)(rows + ROW_OFF_ANIM_CUR);
    if (!table)
        return 0;

    i = 0;
    if (table->count > 0) {
        while (i < table->count && table->entries[i].id != animId)
            i++;
    }
    if (i >= table->count)
        i = 0;

    if (fast) {
        speed = (int32_t)(AM2_ANIM_FAST_STEP
                          / *(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC);
    } else {
        speed = table->entries[i].anim->field4;
    }

    if (*(const uint32_t *)(rows + ROW_OFF_FIELD_2C)
        == (uint32_t)ADDR_ROW_LUT_DOUBLES)
        speed += speed;

    return MoveStepPoint(obj, heading, ((const uint8_t *)
                             AM2_IMAGE(ADDR_FRAME_HEADING_BIAS))[animId],
                         speed, 0, 0, out);
}

typedef int32_t (__cdecl *AM2_FindPathFn)(int32_t from, int32_t to,
                                          uint16_t *route, int32_t *n,
                                          int32_t arg);
#define orig_find_path ((AM2_FindPathFn)(uintptr_t)ADDR_FIND_PATH)

/* PlanPathTo -- original 0x00439D60, three callers: the trooper AI's common
 * step, the vehicle AI's, and 0x00408210. Find a route from where the object
 * is to a point, and write it onto the object as a list of waypoints.
 * Answers 1 on success and 0 when there is no route.
 *
 * IT IS BeginMoveTo's GENERAL CASE, and putting the two side by side is what
 * named five fields. That one traces a straight line and, if nothing refuses,
 * writes the from and to points at +0x120 and +0x124 and seeds three small
 * fields with 0, 1 and 2 -- which orig.h recorded as three unknowns. This one
 * writes as many waypoints as the route needs, at the same +0x120 with the
 * same stride, a zero word after the last, the index of the one in hand at
 * +0x520 and the count at +0x522. So BeginMoveTo's "seeded 0, 1 and 2" is a
 * terminator, an index and a count for a list of exactly two, and the five
 * offsets are one structure.
 *
 * The route comes back in ADDR_TILE_LINE_BUF -- the same uint16 tile buffer
 * TraceTileLine fills for the straight-line case, which is the other half of
 * the evidence that these two are one mechanism.
 *
 * THE LIST IS CLEARED BEFORE THE SEARCH, not after it. The three words go to
 * zero between NearestAllowedTile and the pathfinder, so a failure leaves the
 * object with an empty list rather than the previous route -- and the failure
 * exit writes nothing else but the retry time.
 *
 * TWO DEADLINES ON ONE FIELD, and the difference is the point.
 * OBJ_OFF_MOVE_UNTIL gets the clock plus ADDR_PATH_RETRY_MS (500) when there
 * is no route and plus AM2_MOVE_VALID_MS (3000) when there is. So a failure
 * costs half a second before anything tries again and a success is good for
 * three.
 *
 * THE TARGET IS SNAPPED FIRST. NearestAllowedTile is given the point's tile
 * and may rewrite the point through the caller's pointer, and the tile handed
 * to the pathfinder is taken from the point AFTERWARDS -- so the route is to
 * where the object may actually stand, not to where it was asked to go. The
 * caller sees the rewritten point too, since the pointer is its own.
 */
int32_t __cdecl PlanPathTo(void *obj, uint32_t *at, int32_t arg)
{
    uint8_t  *o = (uint8_t *)obj;
    uint16_t *route = (uint16_t *)AM2_IMAGE(ADDR_TILE_LINE_BUF);
    int32_t   n = 0;
    int32_t   i;

    SetPointRule(obj);
    NearestAllowedTile(obj, TileOfPoint(*at), at);

    *(uint16_t *)(o + OBJ_OFF_MOVE_FROM)  = 0;
    *(uint16_t *)(o + OBJ_OFF_MOVE_AT)    = 0;
    *(uint16_t *)(o + OBJ_OFF_MOVE_COUNT) = 0;

    if (!orig_find_path((int32_t)*(const uint16_t *)(o + OBJ_OFF_TILE),
                        TileOfPoint(*at), route, &n, arg)) {
        *(int32_t *)(o + OBJ_OFF_MOVE_UNTIL) =
            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
            + *(const int32_t *)AM2_IMAGE(ADDR_PATH_RETRY_MS);
        return 0;
    }

    CollapseEqualDeltas(route, &n);

    for (i = 0; i < n; i++) {
        int32_t x, y;

        TileToXY((int32_t)route[i], &x, &y);
        *(uint16_t *)(o + OBJ_OFF_MOVE_FROM + (uint32_t)i * AM2_MOVE_STEP_BYTES)
            = (uint16_t)x;
        *(uint16_t *)(o + OBJ_OFF_MOVE_FROM + (uint32_t)i * AM2_MOVE_STEP_BYTES
                      + 2) = (uint16_t)y;
    }

    *(uint16_t *)(o + OBJ_OFF_MOVE_FROM
                  + (uint32_t)n * AM2_MOVE_STEP_BYTES) = 0;
    *(uint16_t *)(o + OBJ_OFF_MOVE_AT)    = 0;
    *(uint16_t *)(o + OBJ_OFF_MOVE_COUNT) = (uint16_t)n;

    *(int32_t *)(o + OBJ_OFF_MOVE_UNTIL) =
        *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS + AM2_MOVE_VALID_MS;
    return 1;
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
    *(uint16_t *)(o + OBJ_OFF_MOVE_END) = 0;
    *(uint16_t *)(o + OBJ_OFF_MOVE_AT) = 1;
    *(uint16_t *)(o + OBJ_OFF_MOVE_COUNT) = 2;

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

/* 0x004389D0 stays original and is reached by name so checkseams can see it. */
typedef int32_t (__cdecl *AM2_HitMaskActionFn)(void *obj, void *mask);
#define orig_obj_hit_mask_action \
    ((AM2_HitMaskActionFn)(uintptr_t)AM2_IMAGE(ADDR_OBJ_HIT_MASK_ACTION))


/* The original inlines this twice -- once to add cover and once to remove it
 * -- with the same bounds test and the same walk over ADDR_TILE_NEIGHBOURS,
 * differing only in `inc` against `dec`. Written once with a delta. The bounds
 * keep two tiles clear of every edge, and the x and y are recovered from the
 * cell SEPARATELY: x by masking with the width minus one and y by shifting
 * down ADDR_MAP_ROW_SHIFT, which only agree if the map's width is a power of
 * two -- which is presumably why that shift is stored beside the width. */
static void ShiftTileCover(int32_t cell, int32_t delta)
{
    int32_t  w = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    int32_t  tx = cell & (w - 1);
    int32_t  ty = cell >> *(const uint8_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;
    uint8_t *cover;
    const int32_t *n;
    int32_t  i;

    if (tx < AM2_TILEMASK_MARGIN || tx >= w - AM2_TILEMASK_MARGIN)
        return;
    if (ty < AM2_TILEMASK_MARGIN
        || ty >= *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H
                 - AM2_TILEMASK_MARGIN)
        return;

    cover = *(uint8_t *const *)(uintptr_t)ADDR_TILE_COVER;
    cover[cell] = (uint8_t)(cover[cell] + delta);

    n = (const int32_t *)(uintptr_t)ADDR_TILE_NEIGHBOURS;
    for (i = 0; i < AM2_TILE_NEIGHBOUR_COUNT; i++) {
        uint8_t *p = *(uint8_t *const *)(uintptr_t)ADDR_TILE_COVER
                     + cell + n[i];

        *p = (uint8_t)(*p + delta);
    }
}

/* ItemTeardown -- original 0x00439320, 656 bytes, six callers. Take an
 * object's footprint back off the map. orig.h had it as "the item-only half of
 * the common teardown, and the only callee of DestroyObjCommon without a
 * name" -- true, and it says nothing about what the function does, which is
 * maintain the terrain cover map.
 *
 * THE FLAG NAME IS THE WHOLE STORY: it refuses anything without
 * OBJ_FLAG_FOOTPRINT_ON and clears that flag on the way out, so it is
 * idempotent and the name already said what the body does.
 *
 * TYPES 3 AND 8 ARE ONE-CALL TAILS -- ObjClearFootprint and
 * ObjClearRoachFootprint, the vehicle and roach versions of this. The dispatch
 * is the chained `dec / sub 2 / sub 5` idiom, so the set is {1, 3, 8} and
 * reading only the first test gives type 1 alone.
 *
 * `__chkstk` IS NOT CODE. The opening `mov eax, 0x1020; call ADDR_CRT_CHKSTK`
 * is MSVC's page-walking stack probe, a compiler artifact; what it reserves is
 * one tile mask. ObjBoxAction fills it, or ADDR_OBJ_HIT_MASK_ACTION when the
 * object has an OBJ_OFF_HIT_MASK -- and region.cpp records that on every map
 * this project can drive, everything has one, so the else arm is the cold half
 * of a cold function.
 *
 * THE LOOP subtracts OBJ_OFF_HEIGHT_SET from each masked cell's
 * ADDR_CELL_WEIGHTS entry and moves ADDR_TILE_COVER for that cell and its
 * twenty ADDR_TILE_NEIGHBOURS only when the weight CROSSES 15. This is the +1
 * half of the pair CLAUDE.md credits with settling those two globals.
 *
 * THE CELL INDEX STARTS AT rect.top AND THAT LOOKS LIKE A BUG. Both fillers
 * index zero-based over the rect -- ADDR_OBJ_HIT_MASK_ACTION computes
 * `cells + row * width + 2` and BoxAction `cells + (y - top) * stride - left`
 * -- while this walks `cells[top ...]`. Reproduced exactly, not corrected:
 * this is the original's behaviour, the same standing as LockSurface's
 * uninitialised descriptor, and correcting it on a function no drive reaches
 * would be an unverifiable divergence from the binary.
 *
 * THE INCREMENT ARM NEEDS A NEGATIVE HEIGHT. `h <= 15 && h - height >= 15`
 * collapses to `h == 15 && height == 0` for any non-negative height, so it is
 * reachable only if OBJ_OFF_HEIGHT_SET goes negative -- and every compare here
 * is a SIGNED byte compare, so it can. Written as found rather than pruned.
 *
 * COLD, AND SAID PLAINLY. It is reached from DestroyObjCommon, and no drive
 * this project has destroys an item: 325 are added during load and none dies
 * in the window observed. So a clean A/B says only that nothing else
 * regressed, exactly as region.cpp says of ObjBoxAction. Verified by reading.
 */
void __cdecl ItemTeardown(void *obj)
{
    uint8_t       *o = (uint8_t *)obj;
    uint8_t        mask[AM2_TILEMASK_BYTES];
    const int32_t *rect = (const int32_t *)(mask + TILEMASK_OFF_RECT);
    const uint8_t *cells = mask + TILEMASK_OFF_CELLS;
    int32_t        height;
    int32_t        idx;
    int32_t        row;
    int32_t        x;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return;
    if (!(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_FOOTPRINT_ON))
        return;

    switch (*(const int32_t *)o) {
    case AM2_OBJ_TYPE_ROACH:
        ObjClearRoachFootprint(obj);
        return;
    case AM2_OBJ_TYPE_VEHICLE:
        ObjClearFootprint(obj);
        return;
    case AM2_OBJ_TYPE_ITEM:
        break;
    default:
        return;
    }

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) < 1)
        return;
    if (!*(const void *const *)(*(const uint8_t *const *)(o + OBJ_OFF_ROWS)
                                + ROW_OFF_SPRITE))
        return;
    if (!ObjIsItem((const AM2_Object *)obj))
        return;

    height = *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET);
    if ((int8_t)height
        > (int8_t)(*(const uint8_t *const *)(uintptr_t)ADDR_TILE_ATTRS)
              [*(const uint16_t *)(o + OBJ_OFF_TILE)])
        return;
    if (!*(const int8_t *)(o + OBJ_OFF_RANK))
        return;

    if (*(const void *const *)(o + OBJ_OFF_HIT_MASK))
        orig_obj_hit_mask_action(obj, mask);
    else
        ObjBoxAction(obj, mask);

    idx = rect[1];      /* the original starts it at rect.top -- see above */

    for (row = rect[1]; row <= rect[3]; row++) {
        int32_t cell = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W * row
                       + rect[0];

        for (x = rect[0]; x <= rect[2]; x++, idx++, cell++) {
            uint8_t *weights =
                *(uint8_t *const *)(uintptr_t)ADDR_CELL_WEIGHTS;
            int8_t   h;
            int8_t   nh;

            if (!(cells[idx] & 1))
                continue;

            h  = (int8_t)weights[cell & 0xFFFF];
            nh = (int8_t)(h - height);

            if (h <= 15 && nh >= 15)
                ShiftTileCover(cell, 1);
            else if (h >= 15 && nh < 15)
                ShiftTileCover(cell, -1);

            weights[cell & 0xFFFF] = (uint8_t)nh;
        }
    }

    *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;
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

/* The bitmask twin of ListBoxAction, still original: LISTHDR_OFF_HIT_MASK
 * chooses between the two and this is the arm taken when it is SET. */
typedef int32_t (__cdecl *AM2_ListMaskFn)(uint32_t at, void *hdr, void *out);
#define orig_list_mask_action \
    ((AM2_ListMaskFn)(uintptr_t)ADDR_LIST_MASK_ACTION)

/* UnitWeaponInfo -- original 0x004045E0, three callers. Fill the six fields of
 * a sight context that describe the weapon a unit is HOLDING: the object, its
 * kind, its damage, the two ends of its range band, and whether its cooldown
 * has elapsed.
 *
 * IT NAMED FOUR FIELDS AND RE-BASED A TABLE. SIGHTC_OFF_WEAPON and
 * SIGHTC_OFF_READY were ENABLED_40 and ENABLED_54, off the one site that tests
 * them -- where a pointer and a flag both read as "enabled". +0x48 had no name
 * at all. And ADDR_RANK_RECORDS was four bytes late: this reads the field
 * BEFORE the one AiHitReact reads, which is the second toucher that makes the
 * record's real start visible. See orig.h.
 *
 * THE RANGE BAND IS PLUS OR MINUS TEN PERCENT, in three arms. Kind 0x2B takes
 * a flat (r - 4, r + 2) and no float at all; a record with no range at all
 * answers AM2_WEAPON_RANGE_NONE both ways, so an unranged weapon is treated as
 * reaching everywhere rather than nowhere; everything else multiplies by 0.9
 * and 1.1, with kind 3 scaling the range up by 1.2 first.
 *
 * The original loads the range once and multiplies the COPY, so both ends come
 * from the same rounded intermediate -- `fild; fld st(0); fmul; ftol; fmul;
 * ftol`. Written the same way round.
 *
 * THE COOLDOWN IS SCALED BY RANK EXCEPT FOR KIND 3. A kind-3 weapon compares
 * the record's cooldown directly; every other kind multiplies it by
 * RANK_REC_OFF_FIRE_SCALE first, which runs 2.5 at rank 0 down to 1.0 at rank
 * 7 -- so a raw recruit waits two and a half times as long between shots as a
 * veteran. That is the whole of what the rank table's first field does here.
 *
 * The scaled compare loads the cooldown as a 64-bit `fild qword` over a pair
 * whose high half is a zero the function has just written, which makes it
 * UNSIGNED; the kind-3 compare is a plain signed one. Both are then written as
 * `cmp; sbb; neg`, a comparison spelled as arithmetic.
 */
void __cdecl UnitWeaponInfo(void *unit, void *out)
{
    const uint8_t *u = (const uint8_t *)unit;
    uint8_t       *c = (uint8_t *)out;
    const uint8_t *w;
    const uint8_t *rec;
    int32_t        kind, range;
    uint32_t       elapsed;

    w = (const uint8_t *)WeaponByUid(*(const uint32_t *)
            (u + UNIT_OFF_INVENTORY
             + (uint32_t)*(const int32_t *)(u + UNIT_OFF_INVENTORY_SEL) * 4));

    *(const void **)(c + SIGHTC_OFF_WEAPON) = w;
    if (!w) {
        *(int32_t *)(c + SIGHTC_OFF_KIND)  = 0;
        *(int32_t *)(c + SIGHTC_OFF_READY) = 0;
        return;
    }

    rec  = *(const uint8_t *const *)(w + OBJ_OFF_FIELD_C0);
    kind = *(const int32_t *)(rec + ITEMTYPE_OFF_KIND);

    *(int32_t *)(c + SIGHTC_OFF_KIND)   = kind;
    *(int32_t *)(c + SIGHTC_OFF_DAMAGE) =
        *(const int32_t *)(rec + ITEMTYPE_OFF_DAMAGE);

    range = *(const int32_t *)(rec + ITEMTYPE_OFF_RANGE);

    if (kind == AM2_WEAPON_KIND_FIXED) {
        *(int32_t *)(c + SIGHTC_OFF_WANT_RANGE) = range - 4;
        *(int32_t *)(c + SIGHTC_OFF_MAX_RANGE)  =
            *(const int32_t *)(rec + ITEMTYPE_OFF_RANGE) + 2;
    } else if (range == 0) {
        *(int32_t *)(c + SIGHTC_OFF_WANT_RANGE) = AM2_WEAPON_RANGE_NONE;
        *(int32_t *)(c + SIGHTC_OFF_MAX_RANGE)  = AM2_WEAPON_RANGE_NONE;
    } else {
        double v;

        if (kind == AM2_WEAPON_KIND_TIMED)
            range = (int32_t)((double)range
                              * *(const double *)AM2_IMAGE(ADDR_WEAPON_RANGE_K3));

        v = (double)range;
        *(int32_t *)(c + SIGHTC_OFF_WANT_RANGE) =
            (int32_t)(v * *(const double *)AM2_IMAGE(ADDR_WEAPON_RANGE_LO));
        *(int32_t *)(c + SIGHTC_OFF_MAX_RANGE) =
            (int32_t)(v * *(const double *)AM2_IMAGE(ADDR_WEAPON_RANGE_HI));
    }

    elapsed = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
              - *(const uint32_t *)(w + ITEM_OFF_LAST_USE);

    if (kind == AM2_WEAPON_KIND_TIMED) {
        *(int32_t *)(c + SIGHTC_OFF_READY) =
            ((uint32_t)*(const int32_t *)(rec + ITEMTYPE_OFF_COOLDOWN)
             < elapsed) ? 1 : 0;
    } else {
        const uint8_t *rank = (const uint8_t *)AM2_IMAGE(ADDR_RANK_RECORDS)
            + (uint32_t)*(const int32_t *)(u + OBJ_OFF_RANK) * RANK_REC_BYTES;
        uint32_t wait = (uint32_t)(double)
            ((double)(uint32_t)*(const int32_t *)(rec + ITEMTYPE_OFF_COOLDOWN)
             * (double)*(const float *)(rank + RANK_REC_OFF_FIRE_SCALE));

        *(int32_t *)(c + SIGHTC_OFF_READY) = (wait < elapsed) ? 1 : 0;
    }
}


/* CanPlaceAt -- original 0x0043A6D0, six callers. Could the thing described by
 * key-table slot `slot` stand at world point `at`? Build its tile mask there
 * and answer 0 the moment any cell of it fails; 1 if none does.
 *
 * THE SLOT IS A KEY-TABLE SLOT, and that is the one thing here that had to be
 * derived rather than read. It is bounds-checked against ADDR_KEY_TABLE_COUNT
 * and then used to index ADDR_AAI_RECORDS -- so those two arrays are PARALLEL,
 * one count over both, which nothing in the file said before.
 *
 * The mask comes from whichever of the two markers LISTHDR_OFF_HIT_MASK
 * selects, and BoxAction underneath fills the margin with
 * AM2_TILEMASK_PAD_CELL and the box itself with AM2_TILEMASK_BOX_CELL. So
 * `cell & 1` is exactly "inside the box rather than the padding", which is
 * what orig.h already records as the bit that is read -- this is the reader.
 *
 * THREE WAYS TO FAIL, in the order the original tests them: the cell is
 * already covered, its ADDR_TILE_KIND byte is not the one asked for, or an
 * object is standing on it. The first two are array reads and the third is a
 * call, which is presumably why they are in that order.
 *
 * The original tests the cell byte TWICE -- `test al,al; je` then
 * `test al,1; je` -- and the first is subsumed by the second, since a zero
 * byte has bit 0 clear. Written once. Nothing can observe the difference:
 * neither test has a side effect and both skip to the same place.
 *
 * The tile mask is 0x1010 bytes on the stack and the original reserves 0x1018,
 * the extra eight being the loop's own row counter and the point it hands the
 * hit test. Written as the record it is rather than as a byte array.
 */
int32_t __cdecl CanPlaceAt(uint32_t at, int32_t slot, int32_t kind)
{
    struct {
        AM2_Rect r;
        uint8_t  cells[AM2_TILEMASK_CELLS];
    } mask;

    const uint8_t *rec;
    void          *hdr;
    int32_t        x, y, n = 0;
    int32_t        shift = *(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;

    if (slot >= *(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT)
        return 1;

    rec = ((const uint8_t *const *)
              *(void *const *)(uintptr_t)ADDR_AAI_RECORDS)[slot];
    hdr = ((void *const *)*(void *const *)(uintptr_t)ADDR_RECORD_LISTS)
              [*(const int32_t *)(rec + AAI_OFF_DEF_INDEX)];

    if (*(const int32_t *)((const uint8_t *)hdr + LISTHDR_OFF_HIT_MASK))
        orig_list_mask_action(at, hdr, &mask);
    else
        ListBoxAction(at, hdr, &mask);

    for (y = mask.r.top; y <= mask.r.bottom; y++) {
        int32_t tile = (y << shift) + mask.r.left;

        for (x = mask.r.left; x <= mask.r.right; x++, tile++, n++) {
            uint32_t index = (uint32_t)tile & 0xFFFFu;
            uint32_t pt;

            if (!(mask.cells[n] & 1))
                continue;

            if (((const uint8_t *)*(void *const *)(uintptr_t)ADDR_CELL_WEIGHTS)
                    [index] >= AM2_CELL_WEIGHT_STEP)
                return 0;

            if (((const uint8_t *)*(void *const *)(uintptr_t)ADDR_TILE_KIND)
                    [index] != (uint32_t)kind)
                return 0;

            pt = PointOfTile(tile);
            if (ObjectsHitByPoint(&pt,
                    (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC))
                return 0;
        }
    }

    return 1;
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

typedef void (__cdecl *AM2_AiBodyFn)(void *obj, void *out, void *ctx);

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

    AiBuildContext(obj, ctx);

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

/* AiHitReact -- original 0x00405050, TEN call sites across the trooper AI
 * band. What a unit does about having been hit: choose a pose, turn toward the
 * hit if it is not already watching something, and consume OBJ_OFF_HIT_DIR.
 *
 * IT IS WHERE out[8] COMES FROM, which settles what that field is. AiKeepRange
 * writes 7 or 5 there and this writes 0x2B, 7, or one of six values 12..17 out
 * of ADDR_HIT_POSE_BY_CLASS -- so `out + 8` is a requested POSE, the same
 * vocabulary ADDR_SET_POSE and tools/posecheck.py use, and not a mode or a
 * flag. Reading either function alone would not have said so.
 *
 * THE POSE LADDER HAS THREE BANDS AND THE TOP ONE DOES NOTHING. The unit's
 * OBJ_OFF_RANK selects a threshold from ADDR_RANK_RECORDS -- 32, 48, 56, 64,
 * 80, 96, 112, 128, rising with rank -- and SIGHTC_OFF_SEED is compared
 * against half of it and then against all of it:
 *
 *     below half        a pose from ADDR_HIT_POSE_BY_CLASS
 *     half to full      AM2_POSE_HIT_HEAVY
 *     at or above full  no pose at all, the field is left as it was
 *
 * So a higher rank needs a bigger value to reach the same band, and past the
 * threshold the unit does not react with a pose at all.
 *
 * Three ways to skip the ladder entirely, and they are tested in this order:
 * OBJ_OFF_SOLDIER_KIND 7 takes AM2_POSE_KIND7 and nothing else; kind 8 takes
 * no pose; and SIGHTC_OFF_KIND 3 takes no pose. Only the first writes.
 *
 * THE TURN IS GATED ON NOT ALREADY WATCHING SOMETHING -- SIGHTC_OFF_OBSERVER
 * null -- exactly as the vehicle arms gate theirs, but the CONSUME is outside
 * that test and outside the whole ladder. So a unit that is watching an enemy
 * forgets it was hit without turning, and one whose kind or class skipped the
 * pose still forgets. Reproduced; it is the same asymmetry AiStepIgnore has.
 *
 * The two-entry table index is `class * 2 + (SIGHTC_OFF_SEED >= 0x80)`,
 * computed in the original with `cmp cl, 0x80; sbb edx, edx; inc edx` -- the
 * borrow-flag idiom for an unsigned comparison as 0 or 1, written here as the
 * comparison it is.
 */
void __cdecl AiHitReact(void *obj, void *out, void *ctx)
{
    uint8_t       *o = (uint8_t *)obj;
    uint8_t       *w = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)ctx;
    uint8_t        hit = *(const uint8_t *)(o + OBJ_OFF_HIT_DIR);
    int32_t        kind;

    if (!hit)
        return;

    kind = *(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND);
    if (kind == 7) {
        *(int32_t *)(w + 8) = AM2_POSE_KIND7;
    } else if (kind != 8
               && *(const int32_t *)(c + SIGHTC_OFF_KIND) != 3) {
        int32_t limit = *(const int32_t *)
            ((const uint8_t *)AM2_IMAGE(ADDR_RANK_RECORDS)
             + (uint32_t)*(const int32_t *)(o + OBJ_OFF_RANK)
               * RANK_REC_BYTES
             + RANK_REC_OFF_THRESHOLD);
        uint8_t v = *(const uint8_t *)(c + SIGHTC_OFF_SEED);

        if ((int32_t)v < (limit >> 1)) {
            int32_t slot = *(const int32_t *)(c + SIGHTC_OFF_FIELD_00) * 2
                           + (v >= 0x80 ? 1 : 0);

            *(int32_t *)(w + 8) =
                ((const int32_t *)AM2_IMAGE(ADDR_HIT_POSE_BY_CLASS))[slot];
        } else if ((int32_t)v < limit) {
            *(int32_t *)(w + 8) = AM2_POSE_HIT_HEAVY;
        }
    }

    if (!*(const void *const *)(c + SIGHTC_OFF_OBSERVER))
        w[4] = hit;

    *(o + OBJ_OFF_HIT_DIR) = 0;
}

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
 * for one dword, and 7 or 5 by whether SIGHTC_OFF_SEED is under 4.
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
                   && *(const uint8_t *)(c + SIGHTC_OFF_SEED) < 0x10
                   && *(const int32_t *)(c + SIGHTC_OFF_FIELD_00) == 0) {
            *(int32_t *)(w + 8) =
                *(const uint8_t *)(c + SIGHTC_OFF_SEED) < 4 ? 7 : 5;
        }
    }

    if (now - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0)
        >= AM2_AI_TURN_DELAY_MS)
        w[4] = *(const uint8_t *)(c + SIGHTC_OFF_BEARING);
}

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

    AiHitReact(obj, out, ctx);

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
 *
 * ITS FIRST TWO GUARDS ARE ABOUT THE WEAPON, which the field names used to
 * hide. They were SIGHT_OFF_ENABLED_30 and _ENABLED_40, taken from THIS
 * function, which only tests them -- and a pointer and a flag both read as
 * "enabled". 0x00407D70 is the writer and settles both: +0x30 is the weapon
 * object and +0x40 is whether its cooldown has elapsed. So the rule is "no
 * weapon, no sighting" and "not while it is reloading", and the range it
 * compares against is that weapon's range times 1.1 -- the same named
 * constant UnitWeaponInfo uses for SIGHTC_OFF_MAX_RANGE.
 *
 * Nothing here changed. The code is identical and the names now say what it
 * was always doing, which is the whole of the difference.
 */
void __cdecl ConsiderSighting(void *seen, void *out, const void *sight)
{
    uint8_t       *s = (uint8_t *)seen;
    uint8_t       *o = (uint8_t *)out;
    const uint8_t *c = (const uint8_t *)sight;
    const uint8_t *observer;
    int32_t        range;
    int32_t        delta;

    if (!*(const int32_t *)(c + SIGHT_OFF_WEAPON))
        return;
    if (!*(const int32_t *)(c + SIGHT_OFF_READY))
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

    if (*(const int32_t *)(c + SIGHTC_OFF_WEAPON)
        && *(const int32_t *)(c + SIGHTC_OFF_READY)) {
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

/* The search itself, still original: 1,168 bytes, and the only thing this
 * function drives. It answers non-zero with the path written into the caller's
 * array and its length through the fourth argument. */
typedef int32_t (__cdecl *AM2_RegionFindPathFn)(int32_t from, int32_t to,
                                                int16_t *path, int32_t *len);
#define orig_region_find_path \
    ((AM2_RegionFindPathFn)(uintptr_t)ADDR_REGION_FIND_PATH)

/* RegionSolvePair -- original 0x00438300, four callers. Solve the route from
 * one region to another and record it in the two all-pairs byte matrices, so
 * that RegionHops can walk it a hop at a time afterwards.
 *
 * IT REFUSES OUTRIGHT WHEN THE DESTINATION IS INACTIVE, before it searches and
 * before it stamps -- so an inactive `to` leaves the pair unanswered and the
 * next caller tries again. Every other exit stamps.
 *
 * THE NO-PATH EXIT IS NOT SYMMETRIC AND ORIG.H SAID IT WAS. It clears
 * next[from][to] and next[to][from], which really are a pair, and then writes
 * the stamp into cost[from][to] TWICE -- two separate reloads of both globals,
 * `imul ecx, edi` and `imul eax, edi`, the same register both times, checked
 * in the bytes rather than in the mnemonics. cost[to][from] is never touched.
 *
 * So an unreachable pair is answered one way round only and the reverse gets
 * solved again from the other side. Reproduced, including the duplicated
 * store: a reconstruction that wrote it once would be the same program, and
 * one that "fixed" the symmetry would not.
 *
 * THE FOUND EXIT FILLS BOTH DIRECTIONS, in two nested double loops that are
 * mirror images. Forward: for every i along the path and every j at or after
 * it, the next hop from path[i-1] toward path[j] is path[i]. Backward: the
 * same with the ends swapped. That is the whole point of solving one pair --
 * it answers every pair the path passes through, in both directions, for the
 * price of one search.
 *
 * The path buffer is AM2_REGION_PATH_MAX int16 on the stack and nothing bounds
 * the search against it here; that is the callee's business.
 */
void __cdecl RegionSolvePair(int32_t from, int32_t to)
{
    int32_t  len;
    int16_t  path[AM2_REGION_PATH_MAX];
    uint8_t *next  = *(uint8_t *const *)(uintptr_t)ADDR_REGION_NEXT;
    uint8_t *cost  = *(uint8_t *const *)(uintptr_t)ADDR_REGION_COST;
    int32_t  stride;
    uint8_t  stamp;
    int32_t  i, j;

    if (!*(const int32_t *)((const uint8_t *)
              *(void *const *)(uintptr_t)ADDR_REGIONS
              + (uint32_t)to * AM2_REGION_SIZE + REGION_OFF_ACTIVE))
        return;

    if (!orig_region_find_path(from, to, path, &len)) {
        stride = *(const int16_t *)(uintptr_t)ADDR_REGION_STRIDE;
        stamp  = *(const uint8_t *)(uintptr_t)ADDR_REGION_STAMP;

        next[from * stride + to] = 0;
        next[to * stride + from] = 0;

        /* Both of these are cost[from][to]; the original writes it twice. */
        cost[from * stride + to] = stamp;
        cost[from * stride + to] = stamp;
        return;
    }

    stride = *(const int16_t *)(uintptr_t)ADDR_REGION_STRIDE;
    stamp  = *(const uint8_t *)(uintptr_t)ADDR_REGION_STAMP;

    for (i = 1; i < len; i++)
        for (j = i; j < len; j++) {
            next[path[i - 1] * stride + path[j]] = (uint8_t)path[i];
            cost[path[i - 1] * stride + path[j]] = stamp;
        }

    for (i = len - 1; i > 0; i--)
        for (j = i; j > 0; j--) {
            next[path[i] * stride + path[i - j]] = (uint8_t)path[i - 1];
            cost[path[i] * stride + path[i - j]] = stamp;
        }
}


/* The three arms and the context builder these two dispatchers share with
 * nothing else, all left original and reached by address. None is patched, so
 * naming the address here is a seam checkseams allows. */
typedef void (__cdecl *AM2_AiArmFn)(void *obj, void *out, void *ctx);
typedef void (__cdecl *AM2_AiFillFn)(void *obj, void *ctx, int32_t sarge);
#define orig_ai_arm0   ((AM2_AiArmFn)(uintptr_t)AM2_IMAGE(ADDR_BIG_4057D0))
#define orig_ai_arm3   ((AM2_AiArmFn)(uintptr_t)AM2_IMAGE(ADDR_AI_405DB0))
#define orig_ai_arm6   ((AM2_AiArmFn)(uintptr_t)AM2_IMAGE(ADDR_AI_406B30))
#define orig_ai_deflt  ((AM2_AiArmFn)(uintptr_t)AM2_IMAGE(ADDR_BIG_405220))

/* The middle both dispatchers share, to the instruction. Kind 7 reacts to
 * being hit and can finish the step outright -- AM2_POSE_KIND7 in the output
 * state means there is nothing further to decide this frame -- while kind 8
 * reacts and carries on. Every other kind skips the reaction entirely.
 *
 * THE SHORTCUT SKIPS THE DISPATCH AND NOTHING ELSE, in both callers. It is a
 * `jmp` to the region write, which is where the dispatch arms fall to anyway
 * -- so in Sarge that is the last thing either path does, and in the trooper
 * the 0x540 tail below runs either way. Written as a void helper for that
 * reason: a flag returned to the callers would invite exactly the wrong
 * reading, which is the mistake this comment exists to prevent. The first
 * version of this file had the trooper RETURN on the shortcut and skip its
 * tail; the original falls through.
 *
 * The original has this inline in both, which is what a `jmp` into a shared
 * tail compiles to; one helper says it once instead of twice. */
static void AiStepReactAndDispatch(void *obj, void *out, uint8_t *ctx)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == 7) {
        AiHitReact(obj, out, ctx);
        if (*(const int32_t *)((uint8_t *)out + SIGHTCOUT_OFF_STATE)
            == AM2_POSE_KIND7)
            return;
    } else if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == 8) {
        AiHitReact(obj, out, ctx);
    }

    switch (*(const int32_t *)(o + OBJ_OFF_AI_MODE)) {
    case 0:  orig_ai_arm0(obj, out, ctx);  break;
    case 2:  AiWalkStep(obj, out, ctx);    break;
    case 3:  orig_ai_arm3(obj, out, ctx);  break;
    case 6:  orig_ai_arm6(obj, out, ctx);  break;
    case 7:  Call405220((int32_t)(intptr_t)obj, (int32_t)(intptr_t)out,
                        (int32_t)(intptr_t)ctx);
             break;
    /* 1, 4, 5 and everything above 7 -- `evade` is 5 and takes this arm. */
    default: orig_ai_deflt(obj, out, ctx); break;
    }
}

/* Where the unit is standing, recorded on the object every frame. Both
 * dispatchers end with this, and the kind-7 shortcut above jumps straight to
 * it -- so the region is updated even on the frame the AI decides nothing. */
static void AiStepRecordRegion(uint8_t *o)
{
    *(uint16_t *)(o + OBJ_OFF_REGION) =
        kRegionOfCell[*(const uint16_t *)(o + OBJ_OFF_TILE)];
}

/* SargeAiStep -- original 0x00407020, one caller, and that caller is what
 * identifies it: 0x0044B9FE tests OBJ_OFF_SARGE and sends the leader here and
 * everyone else to TrooperAiStep. The two bodies are otherwise the same
 * dispatcher, so "armed and unarmed" is what the extra prologue looks like
 * until the branch above it is read.
 *
 * THE 0x58-BYTE FRAME IS A SIGHTC RECORD, not scratch. UnitWeaponInfo fills
 * six of its fields and every arm below takes it as the `ctx` third argument
 * the whole Ai* family already shares. What settles it as SIGHTC rather than
 * the SIGHT_OFF_ record AiStepIgnore's dispatcher uses is that call: the two
 * dispatcher families genuinely carry different layouts.
 *
 * NEITHER OF THESE RUNS ON ANY DRIVE THIS PROJECT HAS, and the A/B that was
 * green when they landed compares them not at all. Measured rather than
 * assumed: both `patch:` lines are in the log and both say `(traced)`, the
 * trace table did not overflow, `tools/blindspots.py` does not list either --
 * their caller is original, so the counters CAN move -- and both stay at 0
 * through a live Boot Camp mission in which ObjectsAtPoint climbs past 21
 * million. Boot Camp does not reach 0x0044B7D0's trooper arm. So these have
 * SaveDefaultCof's standing, verified by reading, and the clean run beside
 * them is about the rest of the tree.
 *
 * SARGE'S PROLOGUE IS AN ITEM PICKUP. Choose the best weapon, take whatever
 * candidate the fill left at SIGHTC_OFF_FIELD_20, and if ObjIsType4 says it is
 * a weapon: copy its position into the goal point, keep its +4 in
 * OBJ_OFF_PICKUP_AFTER, settle that point into a region the unit can stand in,
 * record the distance, and clear the candidate so nothing picks it twice.
 *
 * SettlePointInRegion TAKES TWO ARGUMENTS AND THE SECOND IS HOISTED. The
 * original pushes the goal point eight instructions and two calls before the
 * call that consumes it, and one `add esp, 0x10` cleans four dwords across
 * three calls. Pairing each push with the nearest call reads it as a
 * one-argument call with a stray push -- which compiles, runs, and is wrong.
 * Fourth instance of the MSVC argument shuffle recorded in this tree.
 *
 * 0xB8 IS A UNION AND THE OTHER NAME IS THE RIGHT ONE THERE. objscript.cpp
 * increments the same dword as a script frame number; here it is a packed
 * point handed to two functions that take points. No second spelling was
 * added for it -- checkoffsets' family baseline may only go down.
 */
void __cdecl SargeAiStep(void *obj, void *out)
{
    uint8_t  ctx[AM2_SIGHTC_BYTES];
    uint8_t *o = (uint8_t *)obj;
    uint8_t *item;

    if (!obj)
        return;

    SelectBestWeapon(obj);
    TrooperBuildContext(obj, ctx, 1);
    UnitWeaponInfo(obj, ctx);

    item = *(uint8_t **)(ctx + SIGHTC_OFF_FIELD_20);
    if (item && ObjIsType4((const AM2_Object *)item)) {
        uint32_t *goal = (uint32_t *)(o + OBJ_OFF_SCRIPT_FRAME);

        *goal = *(const uint32_t *)(item + OBJ_OFF_POS);
        *(uint32_t *)(o + OBJ_OFF_PICKUP_AFTER) =
            *(const uint32_t *)(item + 4);

        SettlePointInRegion(
            TileOfPoint(*(const uint32_t *)(item + OBJ_OFF_POS)), goal);

        *(int32_t *)(ctx + SIGHTC_OFF_DEST_DIST_B) =
            ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                       (const AM2_Point *)goal);

        *(uint32_t *)(ctx + SIGHTC_OFF_FIELD_20) = 0;
    }

    AiStepReactAndDispatch(obj, out, ctx);
    AiStepRecordRegion(o);
}

/* TrooperAiStep -- original 0x004062B0, one caller, the other half of the
 * OBJ_OFF_SARGE branch above.
 *
 * ITS PROLOGUE IS ONE CALL AND THE DIFFERENCE IS AN ARGUMENT. Where Sarge
 * chooses a weapon and looks for something to pick up, this passes 0 rather
 * than 1 to the same context builder and does nothing else -- so the flag that
 * separates the two paths is handed to a shared helper rather than being a
 * difference between these two functions alone.
 *
 * IT HAS A TAIL SARGE DOES NOT, and the tail is where the frame's decision
 * lands. After the region is recorded it maps OBJ_OFF_FIELD_540 and
 * SIGHTC_OFF_FIELD_00 onto SIGHTCOUT_OFF_STATE:
 *
 *     0x540 == 1   ctx0 0 -> 1,  ctx0 2 -> 9,  else 8
 *     0x540 == 2   ctx0 0 -> 5,  else 4
 *     0x540 == 3   ctx0 2 -> 6,  else 7
 *     anything else            no write at all
 *
 * That is a THIRD independent site reading SIGHTC_OFF_FIELD_00 against 0, 1
 * and 2, which is the range orig.h records for it as suggestive rather than
 * settled. Still nothing WRITES it here either.
 *
 * The last arm falls THROUGH into the shared epilogue rather than jumping to
 * it -- invisible to any diff that normalises jump targets, and the reason the
 * default case is written as "no write" and not as a fifth constant.
 *
 * THE KIND-7 SHORTCUT DOES NOT SKIP THIS TAIL, and the first version of this
 * function had it doing so. The `je` lands ON the region write, which is where
 * the dispatch arms fall to anyway, so the only thing it skips is the
 * dispatch. Reading a jump as "go to the end" rather than looking at what is
 * AT the target is how that got in; the target here is the middle.
 */
void __cdecl TrooperAiStep(void *obj, void *out)
{
    uint8_t  ctx[AM2_SIGHTC_BYTES];
    uint8_t *o = (uint8_t *)obj;
    uint8_t *w = (uint8_t *)out;
    int32_t  c0;

    if (!obj)
        return;

    TrooperBuildContext(obj, ctx, 0);

    AiStepReactAndDispatch(obj, out, ctx);
    AiStepRecordRegion(o);

    c0 = *(const int32_t *)(ctx + SIGHTC_OFF_FIELD_00);

    switch (*(const int32_t *)(o + OBJ_OFF_FIELD_540)) {
    case 1:
        *(int32_t *)(w + SIGHTCOUT_OFF_STATE) = c0 == 0 ? 1 : c0 == 2 ? 9 : 8;
        break;
    case 2:
        *(int32_t *)(w + SIGHTCOUT_OFF_STATE) = c0 == 0 ? 5 : 4;
        break;
    case 3:
        *(int32_t *)(w + SIGHTCOUT_OFF_STATE) = c0 == 2 ? 6 : 7;
        break;
    default:
        break;
    }
}


typedef int32_t  (__cdecl *AM2_ScanFn)(void *obj, void *range, void *bearing,
                                       void *a, void *b, int32_t z);
#define orig_scan_403b40  ((AM2_ScanFn)(uintptr_t)AM2_IMAGE(ADDR_SCAN_403B40))
#define kRangeWant (*(const double *)AM2_IMAGE(ADDR_SIGHT_RANGE_WANT))
#define kRangeHi   (*(const double *)AM2_IMAGE(ADDR_WEAPON_RANGE_HI))

/* One of the two reference blocks both builders share: resolve a uid to an
 * object and drop it if it has gone, been destroyed, been concealed, or is at
 * zero health. Returns the object, or null when the caller should skip.
 *
 * THE ZERO-HEALTH ARM CLEARS THE TARGET UID EVEN WHEN IT IS THE LEADER BEING
 * resolved, which is why `dead_clears` is a separate parameter rather than
 * being the same field as `uid`. Both builders in the image do this
 * identically, so it is the original's behaviour rather than a slip in one of
 * them -- a single sighting would have read as a typo. Reproduced.
 */
static uint8_t *SightResolve(uint32_t *uid, uint32_t *dead_clears, void **slot)
{
    uint8_t *t;

    if (!*uid)
        return NULL;

    t = (uint8_t *)LookupByUID(*uid);
    *slot = t;

    if (!t) {
        *uid = 0;
        return NULL;
    }
    if (*(const uint32_t *)(t + OBJ_OFF_FLAGS) & AM2_SIGHT_DROP) {
        *uid  = 0;
        *slot = NULL;
        return NULL;
    }
    if (*(const int16_t *)(t + OBJ_OFF_HEALTH) == 0) {
        *dead_clears = 0;
        *slot        = NULL;
        return NULL;
    }
    return t;
}

/* AiBuildContext -- original 0x00407D70, one caller: the AI mode
 * dispatcher at 0x00407F80, whose `sub esp, 0x44` is this record's LENGTH.
 * Fill the sight record every mode arm below then reads -- the leader and the
 * way to them, the target and the way to it, whatever ADDR_SCAN_403B40 has in
 * view, and a description of the weapon in hand.
 *
 * ITS TAIL IS WHAT NAMED THREE FIELDS CORRECTLY. SIGHT_OFF_WEAPON and
 * SIGHT_OFF_READY were ENABLED_30 and ENABLED_40, taken off ConsiderSighting,
 * which only tests them -- and a pointer and a flag both read as "enabled"
 * there. This is the writer: +0x30 is the weapon object, +0x40 is whether its
 * cooldown has elapsed, and +0x3C really is a maximum range, scaled by the
 * same named ADDR_WEAPON_RANGE_HI that UnitWeaponInfo uses for the SIGHTC
 * record. See orig.h; the identical mistake had already been made and fixed
 * one record over.
 *
 * IT MEASURES FROM THE ANCHOR POINT, NOT THE POSITION. ADDR_OBJ_ANCHOR_POINT
 * is taken once at the top and is the first argument to both DistAndAngle
 * calls -- but NOT to the ApproxDist below them, which uses the raw
 * OBJ_OFF_POS. Two notions of where the object is, in one function, and the
 * twin at 0x00408060 uses the raw position for all three.
 *
 * THE TWIN IS LESS ALIKE THAN IT LOOKS. It fills 0x40 bytes to this one's
 * 0x44, resolves a FORMATION point for the leader where this copies the
 * leader's position, and writes fixed ranges where this reads the weapon --
 * 48 and 70, which are this function's own 0.75 and 1.1 over a default range
 * of 64. Three differences, not one.
 *
 * A ZERO-HEALTH LEADER CLEARS THE TARGET UID rather than the follow uid its
 * two neighbouring arms clear. Both builders do it, so it is the original's
 * behaviour; see SightResolve.
 *
 * Nothing drives this yet -- its dispatcher's arms are reconstructed, so the
 * counter cannot move, and tools/blindspots.py will say so.
 */
void __cdecl AiBuildContext(void *obj, void *out)
{
    uint8_t  *o = (uint8_t *)obj;
    uint8_t  *s = (uint8_t *)out;
    uint32_t  anchor;
    uint8_t  *leader, *target, *w;

    if (!obj)
        return;

    anchor = ObjAnchorPoint(obj);
    memset(out, 0, AM2_AI_CONTEXT_BYTES);

    leader = SightResolve((uint32_t *)(o + OBJ_OFF_FOLLOW_UID),
                          (uint32_t *)(o + OBJ_OFF_TARGET_UID),
                          (void **)(s + SIGHT_OFF_LEADER));
    if (leader) {
        *(uint32_t *)(s + SIGHT_OFF_DEST) =
            *(const uint32_t *)(leader + OBJ_OFF_POS);
        DistAndAngle((const AM2_Point *)&anchor,
                     (const AM2_Point *)(s + SIGHT_OFF_DEST),
                     (int32_t *)(s + SIGHT_OFF_LEAD_RANGE),
                     s + SIGHT_OFF_LEAD_BEARING);
    }

    target = SightResolve((uint32_t *)(o + OBJ_OFF_TARGET_UID),
                          (uint32_t *)(o + OBJ_OFF_TARGET_UID),
                          (void **)(s + SIGHT_OFF_OBSERVER));
    if (target)
        DistAndAngle((const AM2_Point *)&anchor,
                     (const AM2_Point *)(target + OBJ_OFF_POS),
                     (int32_t *)(s + SIGHT_OFF_RANGE),
                     s + SIGHT_OFF_BEARING);

    if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        >= *(const uint32_t *)(o + OBJ_OFF_FIELD_FC))
        *(int32_t *)(s + SIGHT_OFF_FOUND) =
            orig_scan_403b40(obj, s + SIGHT_OFF_FOUND_RANGE,
                             s + SIGHT_OFF_FOUND_BEARING,
                             o + OBJ_OFF_FIELD_114, o + OBJ_OFF_FIELD_110, 0);

    if (*(const uint16_t *)(o + OBJ_OFF_SCRIPT_STATE))
        *(int32_t *)(s + SIGHT_OFF_DEST_DIST) =
            ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                       (const AM2_Point *)(o + OBJ_OFF_SCRIPT_STATE));

    w = (uint8_t *)WeaponByUid(
            *(const uint32_t *)(o + UNIT_OFF_INVENTORY + 4));
    *(void **)(s + SIGHT_OFF_WEAPON) = w;

    if (!w) {
        *(int32_t *)(s + SIGHT_OFF_KIND) = 0;
    } else {
        const uint8_t *rec = *(const uint8_t *const *)(w + OBJ_OFF_FIELD_C0);
        double         v   = (double)*(const int32_t *)(rec + ITEMTYPE_OFF_RANGE);

        *(int32_t *)(s + SIGHT_OFF_KIND) =
            *(const int32_t *)(rec + ITEMTYPE_OFF_KIND);
        *(int32_t *)(s + SIGHT_OFF_WANT_RANGE) = (int32_t)(v * kRangeWant);
        *(int32_t *)(s + SIGHT_OFF_MAX_RANGE)  = (int32_t)(v * kRangeHi);
        *(int32_t *)(s + SIGHT_OFF_READY) =
            (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
             - *(const uint32_t *)(w + ITEM_OFF_LAST_USE))
            > *(const uint32_t *)(rec + ITEMTYPE_OFF_COOLDOWN);
    }

    *(uint8_t *)(s + SIGHT_OFF_SEED) = (uint8_t)orig_region_rand();
}


/* RoachBuildContext -- original 0x00408060, one caller:
 * ADDR_ROACH_ALIVE_STEP_A, whose `sub esp, 0x40` is this record's length. The
 * roach's half of the sight-context idea, and AiBuildContext's twin.
 *
 * THE RECORDS DIVERGE AT 0x34, NOT AT THEIR ENDS. The vehicle record carries a
 * weapon KIND at 0x34 that this one has no use for, so its want range, max
 * range and cooldown flag all sit one dword lower -- which is the whole of the
 * 0x40-against-0x44 difference. Four independent things say so and orig.h
 * lists them; the prettiest is that this function's 48 and 70 are the vehicle
 * builder's own 0.75 and 1.1 over a default range of 64.
 *
 * SO A ROACH IS SIMPLY ALWAYS UNARMED. It writes a null weapon and the ranges
 * a weapon of range 64 would give, which is why the constants are here rather
 * than read from an item record.
 *
 * IT DIFFERS FROM ITS TWIN IN THREE WAYS. It measures from the raw
 * OBJ_OFF_POS, not from ObjAnchorPoint; it resolves a FORMATION point when the
 * leader is one of the owned types, where the twin copies the leader's
 * position; and it writes those fixed ranges instead of reading a weapon.
 *
 * IT CARRIES THE ORIGINAL'S OWN ASSERTION AND IT IS KEPT. Having taken the
 * target's bearing from DistAndAngle it recomputes the same bearing with
 * AngleBetween and logs "Bad!" when they disagree. The two cannot disagree --
 * AngleBetween is the second half of DistAndAngle -- so this is dead code that
 * somebody left in, and reproducing it costs one comparison.
 *
 * IT RUNS 221,220 TIMES IN A LIVE MAP 01 AND NOTHING COMPARES IT. Measured,
 * with the prediction written down first: kitchen1.txt declares nine roaches
 * and bootcamp1.txt none, so this should read zero on one drive and a large
 * number on the other. It does -- 0 under `ab.sh bootcamp` with StepType8 also
 * at 0, and 221,220 with CreateRoach at 9 on MAP 01 once the briefing is
 * cleared.
 *
 * But `ab.sh campaign` STOPS at that briefing, and deliberately: its own
 * comment records that dismissing it makes MAP 01 hostile and puts two dozen
 * FIRE lines into the log in a non-deterministic order. So the clean run this
 * landed with does not compare a line of this function. It is not cold, it is
 * unwatched -- which is the standing CLAUDE.md already gives the whole roach
 * layer, now with a number on it.
 */
void __cdecl RoachBuildContext(void *obj, void *out)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *s = (uint8_t *)out;
    uint8_t *leader, *target;

    if (!obj)
        return;

    memset(out, 0, AM2_ROACH_CONTEXT_BYTES);

    leader = SightResolve((uint32_t *)(o + OBJ_OFF_FOLLOW_UID),
                          (uint32_t *)(o + OBJ_OFF_TARGET_UID),
                          (void **)(s + SIGHT_OFF_LEADER));
    if (leader) {
        if (ObjIsTypeIn238((const AM2_Object *)leader))
            ResolveFormationPoint(obj, leader,
                                  (AM2_Point *)(s + SIGHT_OFF_DEST));
        else
            *(uint32_t *)(s + SIGHT_OFF_DEST) =
                *(const uint32_t *)(leader + OBJ_OFF_POS);

        DistAndAngle((const AM2_Point *)(o + OBJ_OFF_POS),
                     (const AM2_Point *)(s + SIGHT_OFF_DEST),
                     (int32_t *)(s + SIGHT_OFF_LEAD_RANGE),
                     s + SIGHT_OFF_LEAD_BEARING);
    }

    target = SightResolve((uint32_t *)(o + OBJ_OFF_TARGET_UID),
                          (uint32_t *)(o + OBJ_OFF_TARGET_UID),
                          (void **)(s + SIGHT_OFF_OBSERVER));
    if (target) {
        DistAndAngle((const AM2_Point *)(o + OBJ_OFF_POS),
                     (const AM2_Point *)(target + OBJ_OFF_POS),
                     (int32_t *)(s + SIGHT_OFF_RANGE),
                     s + SIGHT_OFF_BEARING);

        if (*(const uint8_t *)(s + SIGHT_OFF_BEARING)
            != AngleBetween((const AM2_Point *)(o + OBJ_OFF_POS),
                            (const AM2_Point *)
                                (*(uint8_t **)(s + SIGHT_OFF_OBSERVER)
                                 + OBJ_OFF_POS)))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_BAD));
    }

    if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        >= *(const uint32_t *)(o + OBJ_OFF_FIELD_FC))
        *(int32_t *)(s + SIGHT_OFF_FOUND) =
            orig_scan_403b40(obj, s + SIGHT_OFF_FOUND_RANGE,
                             s + SIGHT_OFF_FOUND_BEARING,
                             o + OBJ_OFF_FIELD_114, o + OBJ_OFF_FIELD_110, 0);

    if (*(const uint16_t *)(o + OBJ_OFF_SCRIPT_STATE))
        *(int32_t *)(s + SIGHT_OFF_DEST_DIST) =
            ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                       (const AM2_Point *)(o + OBJ_OFF_SCRIPT_STATE));

    *(void **)(s + SIGHT_OFF_WEAPON)          = NULL;
    *(int32_t *)(s + ROACHCTX_OFF_WANT_RANGE) = AM2_ROACH_WANT_RANGE;
    *(int32_t *)(s + ROACHCTX_OFF_MAX_RANGE)  = AM2_ROACH_MAX_RANGE;
    *(int32_t *)(s + ROACHCTX_OFF_READY) =
        (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
         - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)) > AM2_ROACH_READY_MS;

    *(uint8_t *)(s + SIGHT_OFF_SEED) = (uint8_t)orig_region_rand();
}


typedef void (__cdecl *AM2_AiTrooperFn)(void *obj, void *out, void *ctx);
#define orig_ai_trooper ((AM2_AiTrooperFn)(uintptr_t)AM2_IMAGE(ADDR_AI_TROOPER_STEP))

/* AiStepAttach -- original 0x004060D0, one caller, inside the trooper step
 * chooser at 0x0044B990. The name is OURS and describes what distinguishes it
 * from the rest of the family, the way SettlePointInRegion's does.
 *
 * WALK, THEN ATTACH. While the destination is further than AM2_AI_REACHED_DIST
 * it copies the destination into OBJ_OFF_FIELD_C0 and hands off to the trooper
 * step -- AiStepIgnore's shape exactly. On arrival it clears the destination,
 * picks an object out of an army's list, and attaches to it if it is within
 * AM2_AI_ATTACH_RANGE.
 *
 * THE PICK READS THE WRONG LIST, AND THAT IS THE ORIGINAL'S. The count and the
 * modulus come from `army`, which is the object's own half the time and a
 * random 0..3 the other half; the array actually indexed is obj's own army's.
 * When they differ and the chosen army holds more objects, this reads past the
 * end of the uid list. Kept, for the same reason surface.cpp keeps
 * LockSurface's Restore path: it is what the program does, and nothing this
 * project can drive would reach it -- it needs a coin flip, two army
 * populations of different size, and an out-of-bounds uid that resolves.
 *
 * IT KILLS THE UNIT ON A TIMEOUT. Fifteen seconds after OBJ_OFF_DEADLINE_58,
 * with the destination reached, the object takes 10,000 damage from its own
 * army's owner object. The multiplayer arm is the interesting one and reads
 * backwards until you see it: when a session is up and CommMustBroadcast says
 * NO, it broadcasts by hand and passes suppress=1 so the automatic one does
 * not fire; otherwise it damages plainly with suppress=0.
 *
 * THE SIGNED %4 IS DEAD CODE. The `sar/and 0x80000003/jns/dec/or/inc` is
 * MSVC's signed modulo, but GameRand cannot return a negative, so the
 * correction arm never runs and `& 3` would be equivalent. Written as `% 4`
 * because that is what the source said, not because it matters.
 *
 * THE BOUNDARY AT EXACTLY 12 IS EXCLUDED BY BOTH TESTS. The entry walks only
 * when the distance is strictly GREATER than AM2_AI_REACHED_DIST, and the
 * timeout kill fires only when it is strictly LESS. So a unit at exactly 12 is
 * "arrived" and also exempt from the kill. The two comparisons look
 * inconsistent and are not; do not normalise one of them to `<=`.
 *
 * IT DRAWS FROM GameRand TWICE, once for the branch and the army and once for
 * the list index. Reusing the first value would be the obvious simplification
 * and would desynchronise every later draw in the frame.
 *
 * THE COMM OBJECT IS REACHED BY VALUE, NOT BY ADDRESS. ADDR_COMM_OBJECT points
 * AT the object -- ADDR_COMM_GLOBAL is the object itself -- and the original
 * dereferences it. The first version of this function passed the address, the
 * way an AM2_IMAGE macro reads at a use site; army.cpp had the right form in
 * three places. Nothing could have caught it: the guard above is
 * ADDR_MP_SESSION, which is zero in single player, so no A/B reaches the call
 * at all. Grep an existing call site before passing a global to a function.
 *
 * ONE READ IS UNALIGNED. The original takes a dword at ctx+0x0E, across the
 * two-byte gap after the packed destination point. Reproduced at 0x0E rather
 * than tidied to a neighbouring field, which is what it would look like if
 * somebody assumed the compiler would not do that.
 *
 * ITS COUNTER CAN MOVE -- the caller is original -- but nothing has driven it
 * yet, and the trooper band's coldness is the open question recorded in
 * STATUS.md rather than a property of this function.
 */
void __cdecl AiStepAttach(void *obj, void *out)
{
    uint8_t  ctx[AM2_SIGHTC_BYTES];
    uint8_t *o = (uint8_t *)obj;
    uint8_t *w = (uint8_t *)out;

    if (!obj)
        return;

    TrooperBuildContext(obj, ctx, 0);

    if (*(const int32_t *)(ctx + SIGHTC_OFF_DEST_DIST) > AM2_AI_REACHED_DIST) {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(o + OBJ_OFF_SCRIPT_STATE);
        orig_ai_trooper(obj, out, ctx);
    } else {
        int32_t r;

        *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
            *(const uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_ZERO_POINT);

        r = orig_region_rand();

        if (!(*(const int32_t *)(ctx + SIGHTC_OFF_LEADER)
              && *(const int32_t *)(ctx + SIGHTC_OFF_LEAD_RANGE)
                 <= AM2_AI_ATTACH_RANGE
              && (uint8_t)r)) {
            int32_t army = (r & 1) ? (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY)
                                   : (r >> 1) % 4;
            uint8_t *list = (uint8_t *)g_armyObjLists[army];

            if (*(const int32_t *)(list + LIST_OFF_COUNT) > 0) {
                /* The modulus is taken from `army`'s count and the array from
                 * the object's OWN army. See the header: reproduced. */
                int32_t idx = orig_region_rand()
                              % *(const int32_t *)(list + LIST_OFF_COUNT);
                uint8_t *own =
                    (uint8_t *)g_armyObjLists[*(const int8_t *)(o + OBJ_OFF_ARMY)];
                uint8_t *t = (uint8_t *)LookupByUID(
                    ((const uint32_t *)*(void **)(own + LIST_OFF_UIDS))[idx]);

                if (t
                    && ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                                  (const AM2_Point *)(t + OBJ_OFF_POS))
                       < AM2_AI_ATTACH_RANGE)
                    ObjAttachTo(obj, t);
            }
        }
    }

    if ((*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
         - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)) > AM2_AI_IDLE_TIMEOUT_MS
        && *(const int32_t *)(ctx + SIGHTC_OFF_DEST_DIST) < AM2_AI_REACHED_DIST) {
        int32_t   army = *(const int32_t *)(o + OBJ_OFF_FIELD_5A4) - 1;
        uint8_t  *rec  = (uint8_t *)LookupOwnerObj((uint32_t)army);
        uint32_t  from = rec ? *(const uint32_t *)(rec + 4) : 0;

        if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
            && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                  (int16_t)army)) {
            DamageBroadcast(obj, from, AM2_AI_TIMEOUT_DAMAGE, 3,
                            o + OBJ_OFF_POS, 0);
            DamageObject(obj, AM2_AI_TIMEOUT_DAMAGE, 3, from, 0, 1);
        } else {
            DamageObject(obj, AM2_AI_TIMEOUT_DAMAGE, 3, from, 0, 0);
        }
    } else {
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(ctx + SIGHTC_OFF_DEST);
        orig_ai_trooper(obj, out, ctx);

        *(int32_t *)(w + SIGHTCOUT_OFF_STATE) =
            (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
             - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58))
            > AM2_AI_STATE_MS ? AM2_AI_STATE_STALE : AM2_AI_STATE_RECENT;
    }

    *(uint16_t *)(o + OBJ_OFF_REGION) =
        kRegionOfCell[*(const uint16_t *)(o + OBJ_OFF_TILE)];
}



/* TrooperBuildContext -- original 0x00404730, the THIRD sight builder and the
 * trooper's. Its 0x58 record is the one orig.h calls SIGHTC, and reading it
 * explains the whole family rather than just this member.
 *
 * WHY SIGHTC IS 0x14 BYTES LONGER THAN THE VEHICLE RECORD. Its head shifts by
 * four, because ClassifyByCode74's answer sits at +0 where the SIGHT record
 * keeps the leader. Its weapon block shifts by sixteen, because it also
 * carries the vehicle, that vehicle's distance, and a second destination
 * distance. 0x44 + 0x14 = 0x58, exactly -- so the difference is derived and
 * not merely observed.
 *
 * IT VALIDATES FAR MORE THAN ITS TWO SIBLINGS, and a shared helper would have
 * erased that. They test `flags & 0x204` as one composite and drop; this one
 * splits the bits. A concealed or dead leader goes; a DESTROYED leader goes
 * only when it is a non-riding trooper or a vehicle, so a destroyed
 * anything-else is KEPT.
 *
 * THE VEHICLE TEST READS BACKWARDS AND I HAD IT BACKWARDS. `je` after
 * ObjIsType3 fires when the predicate is FALSE, so it is the vehicle that
 * falls through into the drop. Reading a conditional jump after a predicate
 * call is where that goes wrong: the jump means "not a vehicle", which skims
 * as "is a vehicle".
 *
 * IT TESTS THE SAME TWO FLAGS TWO DIFFERENT WAYS. The leader block splits
 * them -- `test ch,2` then `test cl,4` -- and gives concealed and destroyed
 * different consequences; the target block tests `flags & 0x204` as one
 * composite and drops on either. Two adjacent blocks, one pair of bits, two
 * treatments. It reads as an inconsistency, and tidying it either way changes
 * behaviour in one of the two.
 *
 * THE TARGET BLOCK REJECTS TWO MORE THINGS: an item whose health is negative,
 * and a weapon outright -- and the order matters. An item whose health is NOT
 * negative falls THROUGH to the weapon test, so a healthy weapon is still
 * rejected, by the second test rather than the first. Nesting the health
 * check inside the item check would let it escape both. Then it forgets the
 * target altogether if it is further than the per-rank range in
 * ADDR_RANK_RECORDS, doubled.
 *
 * THAT LOOKUP IS WHAT MOVED THE RANK TABLE. It indexes from 0x00473DC0, twelve
 * bytes before the base orig.h carried, and the record tiles from there
 * exactly into ADDR_FORMATION_SLOTS. See the header; the field is
 * RANK_REC_OFF_SIGHT_RANGE now.
 *
 * ITS LAST ACT NAMES A FIELD. `ctx[SIGHTC_OFF_SEED] = GameRand()` makes that
 * byte a fresh roll per build, so AiHitReact comparing it against 4 and 0x10
 * is a probability gate and not a threshold on anything measured.
 *
 * Every caller is ours -- SargeAiStep, TrooperAiStep and AiStepAttach all
 * reached it through an orig_ai_fill seam until now -- so its counter cannot
 * move and tools/blindspots.py will say so.
 */
void __cdecl TrooperBuildContext(void *obj, void *ctx, int32_t sarge)
{
    uint8_t  *o = (uint8_t *)obj;
    uint8_t  *c = (uint8_t *)ctx;
    uint32_t  anchor;
    uint8_t  *L, *T, *V;

    if (!obj)
        return;

    anchor = ObjAnchorPoint(obj);
    memset(ctx, 0, AM2_SIGHTC_BYTES);
    *(int32_t *)(c + SIGHTC_OFF_FIELD_00) = ClassifyByCode74(obj);

    if (*(const uint32_t *)(o + OBJ_OFF_FOLLOW_UID)) {
        L = (uint8_t *)LookupByUID(*(const uint32_t *)(o + OBJ_OFF_FOLLOW_UID));
        *(void **)(c + SIGHTC_OFF_LEADER) = L;

        if (!L) {
            *(uint32_t *)(o + OBJ_OFF_FOLLOW_UID) = 0;
        } else {
            uint32_t f    = *(const uint32_t *)(L + OBJ_OFF_FLAGS);
            int32_t  drop = 0;

            if (f & OBJ_FLAG_CONCEALED)
                drop = 1;
            else if (*(const int16_t *)(L + OBJ_OFF_HEALTH) == 0)
                drop = 1;
            else if (f & OBJ_FLAG_DESTROYED) {
                if (ObjIsType2((const AM2_Object *)L)
                    && !*(const uint32_t *)(
                           *(uint8_t **)(c + SIGHTC_OFF_LEADER) + OBJ_OFF_RIDING))
                    drop = 1;
                else if (ObjIsType3((const AM2_Object *)
                                    *(uint8_t **)(c + SIGHTC_OFF_LEADER)))
                    drop = 1;
            }
            if (drop) {
                *(uint32_t *)(o + OBJ_OFF_FOLLOW_UID) = 0;
                *(void **)(c + SIGHTC_OFF_LEADER)     = NULL;
            }
        }

        L = *(uint8_t **)(c + SIGHTC_OFF_LEADER);
        if (L) {
            if (ObjsAreAllied(obj, L, 1)
                && ObjIsTypeIn238((const AM2_Object *)L)) {
                ResolveFormationPoint(obj, L,
                                      (AM2_Point *)(c + SIGHTC_OFF_DEST));
            } else {
                *(uint32_t *)(c + SIGHTC_OFF_DEST) =
                    *(const uint32_t *)(L + OBJ_OFF_POS);
                *(uint32_t *)(o + OBJ_OFF_TARGET_UID) =
                    *(const uint32_t *)(L + OBJ_OFF_OWNER);
            }
            DistAndAngle((const AM2_Point *)&anchor,
                         (const AM2_Point *)(c + SIGHTC_OFF_DEST),
                         (int32_t *)(c + SIGHTC_OFF_LEAD_RANGE),
                         c + SIGHTC_OFF_LEAD_BEARING);
        }
    }

    if (*(const uint32_t *)(o + OBJ_OFF_TARGET_UID)) {
        T = (uint8_t *)LookupByUID(*(const uint32_t *)(o + OBJ_OFF_TARGET_UID));
        *(void **)(c + SIGHTC_OFF_OBSERVER) = T;

        if (!T) {
            *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;
        } else if (*(const uint32_t *)(T + OBJ_OFF_FLAGS) & AM2_SIGHT_DROP) {
            *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;
            *(void **)(c + SIGHTC_OFF_OBSERVER)   = NULL;
        } else if (*(const int16_t *)(T + OBJ_OFF_HEALTH) == 0) {
            *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;
            *(void **)(c + SIGHTC_OFF_OBSERVER)   = NULL;
        } else if (ObjIsItem((const AM2_Object *)T)
                   && *(const int16_t *)(
                          *(uint8_t **)(c + SIGHTC_OFF_OBSERVER)
                          + OBJ_OFF_HEALTH) < 0) {
            *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;
            *(void **)(c + SIGHTC_OFF_OBSERVER)   = NULL;
        } else if (ObjIsType4((const AM2_Object *)
                              *(uint8_t **)(c + SIGHTC_OFF_OBSERVER))) {
            *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;
            *(void **)(c + SIGHTC_OFF_OBSERVER)   = NULL;
        } else {
            const uint8_t *rank;

            DistAndAngle((const AM2_Point *)&anchor,
                         (const AM2_Point *)(
                             *(uint8_t **)(c + SIGHTC_OFF_OBSERVER)
                             + OBJ_OFF_POS),
                         (int32_t *)(c + SIGHTC_OFF_RANGE),
                         c + SIGHTC_OFF_BEARING);

            rank = (const uint8_t *)AM2_IMAGE(ADDR_RANK_RECORDS)
                   + (uint32_t)*(const int32_t *)(o + OBJ_OFF_RANK)
                     * RANK_REC_BYTES;

            if (*(const int32_t *)(c + SIGHTC_OFF_RANGE)
                > (*(const int32_t *)(rank + RANK_REC_OFF_SIGHT_RANGE) << 1))
                *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;
        }
    }

    if (*(const uint32_t *)(o + OBJ_OFF_UID_56C)) {
        V = (uint8_t *)LookupType3ByUID(
                *(const uint32_t *)(o + OBJ_OFF_UID_56C));
        *(void **)(c + SIGHTC_OFF_VEHICLE) = V;

        if (!V)
            *(uint32_t *)(o + OBJ_OFF_UID_56C) = 0;
        else
            *(int32_t *)(c + SIGHTC_OFF_VEHICLE_DIST) =
                ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                           (const AM2_Point *)(V + OBJ_OFF_POS));
    }

    if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        >= *(const uint32_t *)(o + OBJ_OFF_FIELD_FC))
        *(int32_t *)(c + SIGHTC_OFF_FIELD_20) =
            orig_scan_403b40(obj, c + SIGHTC_OFF_FOUND_RANGE,
                             c + SIGHTC_OFF_FOUND_BEARING,
                             o + OBJ_OFF_FIELD_114, o + OBJ_OFF_FIELD_110,
                             (int32_t)anchor);

    if (*(const uint16_t *)(o + OBJ_OFF_SCRIPT_STATE))
        *(int32_t *)(c + SIGHTC_OFF_DEST_DIST) =
            ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                       (const AM2_Point *)(o + OBJ_OFF_SCRIPT_STATE));

    if (*(const uint16_t *)(o + OBJ_OFF_SCRIPT_FRAME))
        *(int32_t *)(c + SIGHTC_OFF_DEST_DIST_B) =
            ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                       (const AM2_Point *)(o + OBJ_OFF_SCRIPT_FRAME));

    UnitWeaponInfo(obj, ctx);

    *(uint8_t *)(c + SIGHTC_OFF_SEED) = (uint8_t)orig_region_rand();

    /* The third argument is DEAD in the original and the callers vary it
     * anyway -- SargeAiStep passes 1, the other two pass 0. Proved by frame
     * arithmetic rather than assumed: with five pushes before the body its
     * slot is [esp+0x20], and nothing in the 656 bytes reads [esp+0x20],
     * [esp+0x24] or [esp+0x28]. Kept in the signature because three call
     * sites pass it. */
    (void)sarge;
}



/* RoachAliveStepA -- original 0x00408A60, one caller, and orig.h's own note on
 * it predicted this commit: the five names in that block "are given ROLE names
 * from where they sit in this one function, which is the weakest kind of
 * naming ... Nothing here reads their bodies." Read now.
 *
 * It is AiStep's shape one object type over -- set a field of the output
 * record, build the sight context, run the behaviour that consumes it, record
 * the region -- so the roach has a step/build/behave triple of its own beside
 * the vehicle's and the trooper's.
 *
 * ITS FRAME IS WHAT FIXES THE ROACH RECORD'S LENGTH. `sub esp, 0x40` against
 * RoachBuildContext's `rep stos` of 0x10 dwords: two independent statements of
 * 0x40, which is how the roach and vehicle records were told apart.
 *
 * THE SECOND ARGUMENT IS THE OUTPUT RECORD, NOT A FACING. orig.h called it
 * `facing` from a call site. It is the record 0x0045D660 initialises inside the
 * object at OBJ_OFF_FIELD_578, whose +0x04, +0x08, +0x0C, +0x10 and +0x14 land
 * exactly on the SIGHTCOUT_OFF_ names -- and whose byte 1 is the heading,
 * which is what "facing" was seeing. A name taken from one caller, describing
 * one byte of a structure.
 *
 * ITS COUNTER IS BLIND NOW, AND WRITING IT IS WHAT BLINDED IT. StepType8
 * reached this address through an orig_roach_alive_a seam in item.cpp, so
 * reconstructing it turned a live counter into a dead one -- checkseams
 * required the seam be closed, and closing a seam creates blindness, which
 * CLAUDE.md records as the standing cost of finishing a layer. The comment
 * here said "its counter can move: the caller is original" until the ratchet
 * pointed out that the caller is ours.
 */
void __cdecl RoachAliveStepA(void *obj, void *out)
{
    uint8_t  ctx[AM2_ROACH_CONTEXT_BYTES];
    uint8_t *o = (uint8_t *)obj;

    if (!obj)
        return;

    /* A DWORD 1 at +0x14, so it sets SIGHTCOUT_OFF_X to 1 and
     * SIGHTCOUT_OFF_Y to 0 in one store -- those two are int16 and
     * ConsiderSightingC writes them separately, which is what establishes the
     * names. Written as the single store the original makes. This line said
     * SIGHTCOUT_OFF_SEEN until the bytes were checked against the name: that
     * is +0x10, reads perfectly in context, and is four bytes wrong. */
    *(int32_t *)((uint8_t *)out + SIGHTCOUT_OFF_X) = 1;

    RoachBuildContext(obj, ctx);
    RoachBehaviour(obj, out, ctx);

    *(uint16_t *)(o + OBJ_OFF_REGION) =
        kRegionOfCell[*(const uint16_t *)(o + OBJ_OFF_TILE)];
}


/* GetTickCount through the game's own IAT slot, the same seam air.cpp and
 * commmsg.cpp use: an import of our own would resolve through our IAT, and
 * this file is flat. */
typedef uint32_t (__stdcall *AM2_RegionTickFn)(void);
#define orig_get_tick_count \
    (*(AM2_RegionTickFn *)AM2_IMAGE(ADDR_IAT_GET_TICK_COUNT))

/* Type2PlayerStep -- original 0x0044AD40, one caller: StepType2's player arm,
 * which runs when the object is Sarge AND belongs to the default owner. So
 * this is what the trooper you are commanding does each frame, and none of
 * the AI below it runs for that object.
 *
 * orig.h called it ADDR_STEP2_44AD40 and said outright that the name was "read
 * off that gate, not off the body, and neither has been read". Read now, and
 * the part the gate could not hint at is the last third: IT DRAGS THE REST OF
 * THE SELECTION ALONG.
 *
 * FOUR THINGS IN ORDER:
 *
 *   BOARD. If OBJ_OFF_UID_56C names a live type 3, board it when it is nearer
 *   than AM2_BOARD_NEAR, or nearer than AM2_BOARD_FAR if its
 *   OBJ_OFF_TABLE_REC_KIND is 5. A uid that no longer resolves is cleared and
 *   the walk goes on. Boarding RETURNS -- nothing below it runs that frame.
 *
 *   WALK toward OBJ_OFF_FIELD_C0, which AiKeepRange one file up already reads
 *   as the destination. Two arms, and their thresholds differ by four: an
 *   object already walking (OBJ_OFF_FIELD_10C set) stops when it is within
 *   AM2_AI_REACHED_DIST and reports 1; an object not walking starts only past
 *   AM2_WALK_START_DIST, re-aims at most every AM2_WALK_TURN_MS, and reports
 *   2. Reaching for the existing 0xC in both places would have been wrong in
 *   one of them.
 *
 *   POSE. A report of 2 becomes a pose index -- AM2_POSE_INDEX_SPECIAL for one
 *   weapon code and AM2_POSE_INDEX_DEFAULT for every other -- and that indexes
 *   ADDR_WEAPON_POSE_FRAMES for an animation id the row is asked about.
 *
 *   FOLLOW. If the row has that animation and this object is selected, every
 *   OTHER selected object that is alive, undestroyed and of type 2, 3 or 8
 *   either inherits this object's vehicle claim -- ai mode 0 and a move order
 *   to where the vehicle is -- or is attached to this one with ai mode 3. So
 *   ordering Sarge somewhere orders the squad.
 *
 * THE SAME DISTANCE IS COMPUTED TWICE with the same two arguments, because
 * the compiler did not fold the boarding test's two branches into one call.
 * Written as two calls: ApproxDist is pure, so nothing but its counter can
 * tell, and its counter is blind here anyway -- but the shape is the
 * original's.
 *
 * THE UNNAMED GLOBAL WAS A FIELD OF A NAMED ONE. 0x0048549C is four bytes
 * past ADDR_MOUSE_PRESS, which orig.h already describes as "three {point,
 * tick} pairs, one per button", and the write at 0x00426FD7 stores
 * GetTickCount into it in the instruction after the point. So it is button
 * zero's tick and the test here is "the last press was more than
 * AM2_VIEW_SNAP_MS ago". Grepping the ADDRESS found it; grepping for a name
 * would not have, since it has none.
 *
 * THE CONTEXT IS THE WHOLE 0x58-BYTE FRAME. `sub esp, 0x58` is
 * AM2_SIGHTC_BYTES exactly, the local starts where the four pushes leave it,
 * and ClassifyByCode74's answer is seeded into its +4 before the walker is
 * handed it. Nothing in this function reads that field back.
 *
 * The follow arm dereferences LookupByUID's answer with no null test, which is
 * the original's; reproduced.
 */
void __cdecl Type2PlayerStep(void *obj, void *out)
{
    uint8_t *o    = (uint8_t *)obj;
    uint8_t *w    = (uint8_t *)out;
    uint8_t *dest = o + OBJ_OFF_FIELD_C0;
    uint8_t  ctx[AM2_SIGHTC_BYTES];
    uint32_t uid;
    int32_t  frame;
    int32_t  i;

    /* Into SIGHTC_OFF_LEADER, whose name says a pointer and which takes a
     * 0/1/2 class code here -- see orig.h. */
    *(int32_t *)(ctx + SIGHTC_OFF_LEADER) = ClassifyByCode74(obj);

    uid = *(const uint32_t *)(o + OBJ_OFF_UID_56C);
    if (uid) {
        uint8_t *veh = (uint8_t *)LookupType3ByUID(uid);

        if (!veh) {
            *(uint32_t *)(o + OBJ_OFF_UID_56C) = 0;
        } else if (ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                              (const AM2_Point *)(veh + OBJ_OFF_POS))
                       < AM2_BOARD_NEAR
                   || (ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                                  (const AM2_Point *)(veh + OBJ_OFF_POS))
                           < AM2_BOARD_FAR
                       && *(const int32_t *)(veh + OBJ_OFF_TABLE_REC_KIND)
                          == 5)) {
            EnterVehicle(obj, veh);
            return;
        }
    }

    if (*(const int16_t *)dest != 0) {
        if (*(const int32_t *)(o + OBJ_OFF_FIELD_10C)) {
            orig_ai_trooper_step(obj, o + OBJ_OFF_SIGHT_OUT_T2, ctx);

            if (ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                           (const AM2_Point *)dest) < AM2_AI_REACHED_DIST) {
                *(int32_t *)(w + 8) = 1;
                *(int32_t *)(o + OBJ_OFF_FIELD_10C) = 0;
            }
        } else if (ApproxDist((const AM2_Point *)(o + OBJ_OFF_POS),
                              (const AM2_Point *)dest)
                   > AM2_WALK_START_DIST) {
            if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                    - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_D0)
                > AM2_WALK_TURN_MS) {
                uint8_t a = AngleBetween((const AM2_Point *)(o + OBJ_OFF_POS),
                                         (const AM2_Point *)dest);

                *(uint8_t *)(w + 4) = a;
                *(int16_t *)(o + OBJ_OFF_FIELD_574) = (int16_t)a;
            }
            *(int32_t *)(w + 8) = 2;
        }

        if (!*(const int32_t *)(o + OBJ_OFF_FIELD_10C))
            *(uint32_t *)dest =
                *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
    }

    if (*(const int32_t *)(w + 8) == 2)
        *(int32_t *)(w + 8) =
            (HeldWeaponCode(obj) == AM2_POSE_WEAPON_CODE)
                ? AM2_POSE_INDEX_SPECIAL : AM2_POSE_INDEX_DEFAULT;

    frame = *(const int16_t *)((const uint8_t *)AM2_IMAGE(ADDR_WEAPON_POSE_FRAMES)
                               + (uint32_t)*(const int32_t *)(w + 8) * 4);

    if (RowAnimField4(*(const void *const *)(o + OBJ_OFF_ROWS),
                      (uint16_t)frame) <= 0)
        return;

    /* Button zero's press tick -- ADDR_MOUSE_PRESS + 4; win32/device.cpp
     * spells the same two fields as AM2_MousePress, privately. */
    if (orig_get_tick_count()
            - ((const uint32_t *)AM2_IMAGE(ADDR_MOUSE_PRESS))[1]
        > AM2_VIEW_SNAP_MS
        && !*(const int32_t *)(o + OBJ_OFF_FIELD_10C)) {
        *(int32_t *)(uintptr_t)ADDR_VIEW_SNAP  = 1;
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 1;
    }

    if (!(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SELECTED))
        return;

    for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT; i++) {
        uint8_t *other = (uint8_t *)LookupByUID(
            (*(uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]);

        if (!other || other == o)
            continue;
        if (*(const uint32_t *)(other + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            continue;
        if (*(const int16_t *)(other + OBJ_OFF_HEALTH) == 0)
            continue;
        if (!ObjIsTypeIn238((const AM2_Object *)other))
            continue;
        if (*(const int32_t *)(other + OBJ_OFF_FIELD_94) != 0)
            continue;

        if (ObjIsType2((const AM2_Object *)other)
            && *(const uint32_t *)(o + OBJ_OFF_UID_56C)) {
            const uint8_t *veh = (const uint8_t *)LookupByUID(
                *(const uint32_t *)(o + OBJ_OFF_UID_56C));

            *(int32_t *)(other + OBJ_OFF_AI_MODE) = 0;
            *(uint32_t *)(other + OBJ_OFF_UID_56C) =
                *(const uint32_t *)(o + OBJ_OFF_UID_56C);
            PointActionA(other, *(const uint32_t *)(veh + OBJ_OFF_POS));
            continue;
        }

        *(int32_t *)(other + OBJ_OFF_AI_MODE) = 3;
        ObjAttachTo(other, obj);
    }
}

/* PlaySoundAt is reconstructed in win32/audio.cpp, and region.cpp is on the
 * flat side of the split, so it is declared here rather than included -- the
 * same reason script.cpp declares PreloadSprite. Its arguments are all
 * int32_t, so the declaration names no Win32 type.
 *
 * `extern "C"` because audio.h WRAPS its declarations in one, so the symbol
 * has C linkage -- the opposite of gameproc.cpp's LoadAudioSection, which
 * needs no wrapper precisely because audio.h does not cover it. The linker
 * says which: a C++-mangled reference to a C-linkage definition is undefined
 * at link time and compiles perfectly. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int32_t x, int32_t y);

typedef void (__cdecl *AM2_Step2AFn)(void *obj, void *weapon, void *out);
typedef void (__cdecl *AM2_Step2BFn)(void *obj, void *out);
typedef void (__cdecl *AM2_RowFinalFn)(void *row);
#define orig_step2_449fd0 ((AM2_Step2AFn)(uintptr_t)AM2_IMAGE(ADDR_AI_449FD0))
#define orig_step2_44afb0 ((AM2_Step2AFn)(uintptr_t)AM2_IMAGE(ADDR_AI_44AFB0))
#define orig_step2_44a420 ((AM2_Step2AFn)(uintptr_t)AM2_IMAGE(ADDR_STEP2_44A420))
/* Type2PlayerStep is reconstructed below and called by name. */
#define orig_row_final    ((AM2_RowFinalFn)(uintptr_t)AM2_IMAGE(ADDR_ROACH_ROW_FINAL))

/* StepType2 -- original 0x0044B7D0, one caller: ObjFrameStep's type-2 arm. The
 * trooper's per-frame step, and the last piece of the AI band's shape.
 *
 * THE PLAYER'S OWN SARGE NEVER REACHES THE AI. When the object is Sarge AND
 * belongs to the default owner, this runs ADDR_STEP2_44A420 and
 * ADDR_STEP2_44AD40 and returns -- input, not AI. That branch sits upstream of
 * SargeAiStep and TrooperAiStep, and it is why their counters read 0 through a
 * live Boot Camp mission: the one trooper the player commands takes the player
 * path, and it is taken before the AI arms are reached at all.
 *
 * ITS OUTPUT RECORD IS AT +0x57C AND StepType3's IS AT +0x578. Both are the
 * SIGHTCOUT layout, so the SIGHTCOUT_OFF_ names are relative to whichever base
 * the caller passes -- obj+0x580 is this one's BEARING and StepType3's STATE.
 * With +0x57C every write lands coherently: OBJ_OFF_FACING into the BEARING
 * byte, WeaponPoseIndex into STATE, zeros into HIT and UID. Read the base
 * before reading a field.
 *
 * THE PRELUDE'S TWO SOUNDS SHARE ONE CALL SITE. The kind-7 branch pushes its
 * five arguments and JUMPS to the other branch's call, so pairing pushes with
 * the nearest call gives one site five arguments and the other none -- the
 * mirror image of the argument shuffle, and it defeats the same shortcut.
 *
 * ITS COUNTER IS BLIND, AND WRITING IT BLINDED IT. ObjFrameStep is ours in
 * item.cpp and reached this address through an orig_step_type2 seam;
 * checkseams required that be closed, so the counter that could have measured
 * this is dead the moment the function exists. Second time today -- the same
 * happened to RoachAliveStepA. Closing a seam creates blindness, which is the
 * standing cost of finishing a layer.
 *
 * BOTH DEAD BRANCHES END THE SAME WAY: clear the record, DestroyByType, then
 * ADDR_AI_44AFB0 with the weapon looked up further up. The pose-range branch
 * additionally finishes the row's animation first, and RETURNS EARLY if that
 * animation has not finished -- so a dying trooper is stepped again next frame
 * rather than destroyed mid-animation.
 */
void __cdecl StepType2(void *obj)
{
    uint8_t *o   = (uint8_t *)obj;
    uint8_t *out = o + OBJ_OFF_SIGHT_OUT_T2;
    uint8_t *w;

    /* The held weapon's uid is read TWICE in the original, once inside each
     * branch, and the destroyed path returns before its read for anything that
     * is not Sarge. Hoisting it above the branch is obviously equivalent -- a
     * plain read of an in-range slot with no side effects -- and it is still
     * not what the program does, so it is written as the program has it. */
#define AM2_STEP2_HELD_UID                                                    \
    (((const uint32_t *)(o + UNIT_OFF_INVENTORY))                             \
        [*(const int32_t *)(o + UNIT_OFF_INVENTORY_SEL)])

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED) {
        if (!*(const int32_t *)(o + OBJ_OFF_SARGE))
            return;
        if (*(const int16_t *)(o + OBJ_OFF_HEALTH) <= 0)
            return;
        if (!*(const uint32_t *)(o + OBJ_OFF_RIDING))
            return;
        w = (uint8_t *)WeaponByUid(AM2_STEP2_HELD_UID);
        if (!w)
            return;
        orig_step2_449fd0(obj, w, out);
        return;
    }

    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == 7)
        PlaySoundAt(AM2_SND_KIND7, 1, 0,
                    *(const int16_t *)(o + OBJ_OFF_POS),
                    *(const int16_t *)(o + OBJ_OFF_POS + 2));
    else if (Type2Field5A4Set((const AM2_Object *)obj))
        PlaySoundAt(AM2_SND_FIELD5A4, 1, 0,
                    *(const int16_t *)(o + OBJ_OFF_POS),
                    *(const int16_t *)(o + OBJ_OFF_POS + 2));

    w = (uint8_t *)WeaponByUid(AM2_STEP2_HELD_UID);

    if (*(const int32_t *)out == 0) {
        *(int32_t *)out = 0;
        *(int32_t *)(out + SIGHTCOUT_OFF_STATE) = WeaponPoseIndex(obj, w);
        *(uint8_t *)(out + SIGHTCOUT_OFF_BEARING) =
            *(const uint8_t *)(o + OBJ_OFF_FACING);
        *(int32_t *)(out + SIGHTCOUT_OFF_HIT) = 0;
        *(int32_t *)(out + SIGHTCOUT_OFF_UID) = 0;
    }

    if (*(const int32_t *)(o + OBJ_OFF_REVEALED_UNTIL) > 0
        && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
           > *(const uint32_t *)(o + OBJ_OFF_REVEALED_UNTIL)) {
        if (!ObjIsFriendly(obj))
            ObjConceal(obj, 0);
        *(int32_t *)(o + OBJ_OFF_REVEALED_UNTIL) = 0;
    }

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0) {
        int32_t pose = *(const int32_t *)(o + OBJ_OFF_POSE);

        if (pose > 0x1F && pose <= 0x24) {
            void *row = *(void **)(o + OBJ_OFF_ROWS);

            if (!RowAnimFinished(row))
                goto tail;   /* 0x0044B925 is `je 0x0044BA42` -- a trooper
                              * still playing its death animation reaches the
                              * tail too, so its weapon keeps being placed */
            RowFaceSprite(row);
            *(int32_t *)out = 0;
            orig_row_final(row);
        } else {
            *(int32_t *)out = 0;
        }
        DestroyByType(obj);
        orig_step2_44afb0(obj, w, out);
        return;
    }

    /* EVERY ALIVE PATH CONVERGES ON ADDR_AI_44AFB0 -- it is a tail, not a set
     * of returns, and writing it as returns is what the state artifact caught:
     * that call moves the held weapon to its owner's position, so skipping it
     * left a dropped weapon at 0,0 where the original had it at the trooper's
     * feet. One line of a 1,610-line object dump, with the pixels and the log
     * identical on both sides.
     *
     * AND I MADE THE SAME MISTAKE TWICE HERE. The first fix converted the
     * other exits and left the player gate returning, because I had never
     * dumped 0x0044B9D4 -- it is `jmp 0x0044BA3F`, so that path converges too.
     * The A/B came back with the identical one-line difference. The rule
     * "decode the first instruction at every jump target" only works if it is
     * applied to every jump, including the ones already believed understood;
     * an unread address cannot fail a test that was never run on it. */
    if (*(const int32_t *)out != 0)
        goto tail;

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_5A4)) {
        AiStepAttach(obj, out);
        goto tail;
    }

    if (*(const int32_t *)(o + OBJ_OFF_SARGE)
        && (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY)
           == (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER) {
        if (!*(const int32_t *)(uintptr_t)ADDR_OBJ_CTX_OBJ_A
            && !*(const uint32_t *)(o + OBJ_OFF_RIDING))
            orig_step2_44a420(obj, w, out);
        Type2PlayerStep(obj, out);
        goto tail;                     /* 0x0044B9D4 is `jmp 0x0044BA3F` --
                                        * this converges too, and Boot Camp's
                                        * Sarge takes exactly this path */
    }

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
        goto tail;

    if (*(const uint32_t *)(o + OBJ_OFF_RIDING))
        goto tail;

    if (*(const int32_t *)(o + OBJ_OFF_SARGE)) {
        SargeAiStep(obj, out);
        /* AND SARGE RE-READS THE WEAPON. His step is the one that picks items
         * up, so the tail must see what he is holding NOW; the trooper arm
         * falls through with the uid stashed before the step. */
        w = (uint8_t *)WeaponByUid(AM2_STEP2_HELD_UID);
    } else {
        TrooperAiStep(obj, out);
    }

tail:
    orig_step2_44afb0(obj, w, out);

#undef AM2_STEP2_HELD_UID
}


#define orig_step3_45c8d0 ((AM2_Step2BFn)(uintptr_t)AM2_IMAGE(ADDR_STEP3_45C8D0))
#define orig_step3_45cb30 ((AM2_Step2BFn)(uintptr_t)AM2_IMAGE(ADDR_STEP3_45CB30))

/* StepType3 -- original 0x0045D660, one caller: ObjFrameStep's type-3 arm.
 * The vehicle's per-frame step, and the mirror of StepType2.
 *
 * TWO SEQUENTIAL CONVERGING TAILS, and nothing here returns early. All twenty
 * jump targets were decoded before this was written and not one begins with a
 * pop or a ret -- so every conditional jump goes to more code. The AI and
 * attach arms reach orig_step3_45c8d0 and FALL THROUGH into a second block
 * that ends at orig_step3_45cb30, which every path reaches. StepType2 carried
 * exactly this defect three times; here the shape was established first.
 *
 * ITS OUTPUT RECORD IS AT +0x578 WHERE StepType2's IS AT +0x57C, both
 * SIGHTCOUT. So obj+0x580 is this one's STATE and that one's BEARING, and the
 * SIGHTCOUT_OFF_ names are relative to the base a caller passes.
 *
 * THE DEATH TABLE IS THE JUMP-TABLE TRAP AT ITS WORST. Six indices on
 * OBJ_OFF_TABLE_REC_KIND, five arms, and the sound constants run 0x1F, 0x20,
 * 0x21, 0x22 in the arms' LAYOUT order -- which reads as confirmation that the
 * arms are in index order. The table says 1, 0, {2,3}, 5. Kinds 2 and 3 share
 * an arm and kind 4 makes no sound at all.
 *
 * IT PRUNES DEAD OCCUPANTS BEFORE RUNNING THE AI. OBJ_OFF_POSE is a vehicle's
 * list header -- orig.h already said so -- with the standard layout on top, so
 * the count is at +0x53C and the uids at +0x540. A first occupant that no
 * longer resolves is removed.
 */
void __cdecl StepType3(void *obj)
{
    uint8_t *o   = (uint8_t *)obj;
    uint8_t *out = o + OBJ_OFF_FIELD_578;

    /* 0x0045D66B is `jne 0x0045D94D` -- the bare epilogue, PAST both tails.
     * A destroyed vehicle runs neither, and in particular never re-sets its
     * footprint. */
    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return;

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0)
        ObjClearFootprint(obj);

    if (*(const int32_t *)(o + OBJ_OFF_REVEALED_UNTIL) > 0
        && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
           > *(const uint32_t *)(o + OBJ_OFF_REVEALED_UNTIL)) {
        if (!ObjIsFriendly(obj))
            ObjConceal(obj, 0);
        *(int32_t *)(o + OBJ_OFF_REVEALED_UNTIL) = 0;
    }

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_59C) == 0) {
        *(uint8_t *)(out + 0) = *(const uint8_t *)(o + OBJ_OFF_FACING);
        *(uint8_t *)(out + 1) = *(const uint8_t *)(o + OBJ_OFF_FIELD_530);
        *(int32_t *)(out + SIGHTCOUT_OFF_HIT)   = 0;
        *(int32_t *)(out + SIGHTCOUT_OFF_STATE) = 1;
        *(int32_t *)(out + SIGHTCOUT_OFF_X)     = 0;
        *(int32_t *)(out + SIGHTCOUT_OFF_SEEN)  = 0;
    }

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0)
        goto alive;

    /* Dead: only kind 5 runs the destruction sequence at all. */
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == 0)
        goto attach;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) != 5)
        goto post;
    {
        void *row = *(void **)(o + OBJ_OFF_ROWS);

        if (!RowAnimFinished(row))
            goto post;
        RowFaceSprite(row);

        /* The table's order, not the arms'. See the header. */
        switch (*(const int32_t *)(o + OBJ_OFF_TABLE_REC_KIND)) {
        case 0:
            PlaySoundAt(AM2_SND_VEH_KIND0, 0, 0,
                        *(const int16_t *)(o + OBJ_OFF_POS),
                        *(const int16_t *)(o + OBJ_OFF_POS + 2));
            break;
        case 1:
            PlaySoundAt(AM2_SND_VEH_KIND1, 0, 0,
                        *(const int16_t *)(o + OBJ_OFF_POS),
                        *(const int16_t *)(o + OBJ_OFF_POS + 2));
            break;
        case 2: case 3:
            PlaySoundAt(AM2_SND_VEH_KIND23, 0, 0,
                        *(const int16_t *)(o + OBJ_OFF_POS),
                        *(const int16_t *)(o + OBJ_OFF_POS + 2));
            break;
        case 5:
            PlaySoundAt(AM2_SND_VEH_KIND5, 0, 0,
                        *(const int16_t *)(o + OBJ_OFF_POS),
                        *(const int16_t *)(o + OBJ_OFF_POS + 2));
            break;
        default:
            /* kind 4 and anything above 5: no sound, finish and destroy */
            *(int32_t *)(o + OBJ_OFF_FIELD_59C) = 0;
            orig_row_final(row);
            DestroyByType(obj);
            goto post;
        }
    }
    goto post;

alive:
    if (*(const int32_t *)out != 0)
        goto post;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
        goto post;

    if (*(const int32_t *)(o + OBJ_OFF_POSE + LIST_OFF_COUNT) > 0) {
        uint8_t *first = (uint8_t *)LookupByUID(
            ((const uint32_t *)*(void **)(o + OBJ_OFF_POSE + LIST_OFF_UIDS))[0]);

        if (!first)
            ListRemoveAt(o + OBJ_OFF_POSE, 0);
    }

attach:
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_94)) {
        AiStep(obj, out);
        orig_step3_45c8d0(obj, out);
    } else {
        ObjAttachTo(obj, NULL);
        *(uint16_t *)(o + OBJ_OFF_FIELD_C0)     = 0;
        *(uint16_t *)(o + OBJ_OFF_FIELD_C0 + 2) = 0;
    }

post:
    /* The second tail. Everything above reaches it. */
    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
        goto emit;

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) < 0)
        goto emit;
    if (*(const int32_t *)(out + SIGHTCOUT_OFF_STATE) != 1)
        goto emit;

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_44) > AM2_VEH_TURN_LIMIT) {
        *(int32_t *)(out + SIGHTCOUT_OFF_HIT) = 1;
        *(int32_t *)(out + SIGHTCOUT_OFF_STATE) = AM2_VEH_STATE_TURNING;
    } else if (*(const int32_t *)(o + OBJ_OFF_FIELD_44) < -AM2_VEH_TURN_LIMIT) {
        *(int32_t *)(out + SIGHTCOUT_OFF_STATE) = AM2_VEH_STATE_TURNING;
    }

emit:
    orig_step3_45cb30(obj, out);

    /* THE THIRD TAIL, and the footprint is why it matters. The entry clears
     * this vehicle's footprint and this puts it back, so a reconstruction that
     * stops at the second tail leaves OBJ_OFF_FLAGS short of the footprint
     * bits -- which is exactly how this was caught: `flags=0x200861` against
     * `0x821` on two vehicles in the object dump. */
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0)
        ObjSetFootprint(obj);
}


typedef void (__cdecl *AM2_RegionCmpFn)(void *obj, void *out, void *ctx);
#define orig_ai_408210 ((AM2_RegionCmpFn)(uintptr_t)AM2_IMAGE(ADDR_AI_408210))

/* The promote-and-engage block, written out at each of its four sites rather
 * than called, because the original inlines it four times and orig.h already
 * settled that policy for AiStepDefend: "the original does it twice" is a fact
 * about the original. A helper here would read better and record less. */
#define AM2_ROACH_PROMOTE_FOUND(o_, c_)                                       \
    do {                                                                      \
        uint8_t *found_ = *(uint8_t **)((c_) + SIGHT_OFF_FOUND);              \
        *(uint32_t *)((o_) + OBJ_OFF_TARGET_UID) =                            \
            *(const uint32_t *)(found_ + OBJ_OFF_OWNER);                      \
        *(void **)((c_) + SIGHT_OFF_OBSERVER) =                               \
            *(void **)((c_) + SIGHT_OFF_FOUND);                               \
        *(int32_t *)((c_) + SIGHT_OFF_RANGE) =                                \
            *(const int32_t *)((c_) + SIGHT_OFF_FOUND_RANGE);                 \
        *((c_) + SIGHT_OFF_BEARING) = *((c_) + SIGHT_OFF_FOUND_BEARING);      \
    } while (0)

/* RoachBehaviour -- original 0x00408640, one caller: RoachAliveStepA. The
 * roach's decision half, and the function that shows what the SIGHT record is
 * for.
 *
 * THE RECORD HOLDS {object, range, bearing} THREE TIMES -- leader at +0x00,
 * observer at +0x10, found at +0x1C -- and this PROMOTES one triple into the
 * observer slot depending on what the roach decides to engage. Four
 * promotions: the found triple in the near, far and no-leader arms, and the
 * leader triple in the follow arm. That is why ConsiderSighting reads only
 * observer/range/bearing: the promotion has already chosen for it.
 *
 * ITS OPENING TEST IS "AM I FURTHER THAN I WANT TO BE": SIGHT_OFF_RANGE
 * against ROACHCTX_OFF_WANT_RANGE, which RoachBuildContext writes as 48 --
 * a default weapon range of 64 times 0.75. Far means close the gap; near means
 * decide what to do.
 *
 * 26 jump targets, none an epilogue, counted and untruncated before writing.
 * A single exit, like every other function in this band.
 */
void __cdecl RoachBehaviour(void *obj, void *out, void *ctx)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *c = (uint8_t *)ctx;

    if (*(const int32_t *)(c + SIGHT_OFF_RANGE) > 0
        && *(const int32_t *)(c + SIGHT_OFF_RANGE)
           > *(const int32_t *)(c + ROACHCTX_OFF_WANT_RANGE)) {
        /* Far: head for what we are engaging and look again. */
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(*(uint8_t **)(c + SIGHT_OFF_OBSERVER)
                                + OBJ_OFF_POS);
        orig_ai_408210(obj, out, ctx);
        if (*(void **)(c + SIGHT_OFF_FOUND))
            AM2_ROACH_PROMOTE_FOUND(o, c);
        goto tail;
    }

    if (*(const int32_t *)(c + SIGHT_OFF_DEST_DIST) > AM2_AI_REACHED_DIST) {
        /* Not arrived: keep the destination and look again. */
        *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
            *(const uint32_t *)(o + OBJ_OFF_SCRIPT_STATE);
        orig_ai_408210(obj, out, ctx);
        if (*(void **)(c + SIGHT_OFF_FOUND))
            AM2_ROACH_PROMOTE_FOUND(o, c);
        goto tail;
    }

    /* Arrived. */
    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_ZERO_POINT);

    if (!*(void **)(c + SIGHT_OFF_LEADER)) {
        /* No leader: take whatever is pending, then engage what was found. */
        ConsumePendingByte(obj, out, ctx);
        if (*(void **)(c + SIGHT_OFF_FOUND))
            AM2_ROACH_PROMOTE_FOUND(o, c);
        goto tail;
    }

    /* Following a leader: promote the LEADER triple rather than the found one,
     * which is the only site in the band that promotes from that source. */
    {
        uint8_t *leader = *(uint8_t **)(c + SIGHT_OFF_LEADER);

        *(uint32_t *)(o + OBJ_OFF_TARGET_UID) =
            *(const uint32_t *)(leader + OBJ_OFF_OWNER);
        *(void **)(c + SIGHT_OFF_OBSERVER) = leader;
        *(int32_t *)(c + SIGHT_OFF_RANGE) =
            *(const int32_t *)(c + SIGHT_OFF_LEAD_RANGE);
        *(c + SIGHT_OFF_BEARING) = *(c + SIGHT_OFF_LEAD_BEARING);
        ConsumePendingByte(obj, out, ctx);
    }

tail:
    CopyByteIfSet((uint32_t)(uintptr_t)obj, (uint8_t *)out, ctx);
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
    rc |= patch_replace(ADDR_AI_HIT_REACT, (const void *)AiHitReact,
                        "AiHitReact", 10);
    rc |= patch_replace(ADDR_PLAN_PATH_TO, (const void *)PlanPathTo,
                        "PlanPathTo", 3);
    rc |= patch_replace(ADDR_MOVE_STEP_POINT, (const void *)MoveStepPoint,
                        "MoveStepPoint", 6);
    rc |= patch_replace(ADDR_ANIM_STEP_POINT, (const void *)AnimStepPoint,
                        "AnimStepPoint", 3);
    rc |= patch_replace(ADDR_AI_WALK_STEP, (const void *)AiWalkStep,
                        "AiWalkStep", 2);
    rc |= patch_replace(ADDR_SARGE_AI_STEP, (const void *)SargeAiStep,
                        "SargeAiStep", 1);
    rc |= patch_replace(ADDR_AI_BUILD_CONTEXT, (const void *)AiBuildContext,
                        "AiBuildContext", 1);
    rc |= patch_replace(ADDR_ROACH_BUILD_CONTEXT,
                        (const void *)RoachBuildContext,
                        "RoachBuildContext", 1);
    rc |= patch_replace(ADDR_AI_STEP_ATTACH, (const void *)AiStepAttach,
                        "AiStepAttach", 1);
    rc |= patch_replace(ADDR_TROOPER_BUILD_CONTEXT,
                        (const void *)TrooperBuildContext,
                        "TrooperBuildContext", 1);
    rc |= patch_replace(ADDR_ROACH_ALIVE_STEP_A,
                        (const void *)RoachAliveStepA,
                        "RoachAliveStepA", 1);
    rc |= patch_replace(ADDR_TYPE2_PLAYER_STEP, (const void *)Type2PlayerStep,
                        "Type2PlayerStep", 1);
    rc |= patch_replace(ADDR_STEP_TYPE2, (const void *)StepType2,
                        "StepType2", 1);
    rc |= patch_replace(ADDR_STEP_TYPE3, (const void *)StepType3,
                        "StepType3", 1);
    rc |= patch_replace(ADDR_ROACH_BEHAVIOUR, (const void *)RoachBehaviour,
                        "RoachBehaviour", 1);
    rc |= patch_replace(ADDR_TROOPER_AI_STEP, (const void *)TrooperAiStep,
                        "TrooperAiStep", 1);
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
    rc |= patch_replace(ADDR_ITEM_TEARDOWN, (const void *)ItemTeardown,
                        "ItemTeardown", 1);
    rc |= patch_replace(ADDR_BOX_ACTION, (const void *)BoxAction,
                        "BoxAction", 5);
    rc |= patch_replace(ADDR_LIST_BOX_ACTION, (const void *)ListBoxAction,
                        "ListBoxAction", 3);
    rc |= patch_replace(ADDR_CAN_PLACE_AT, (const void *)CanPlaceAt,
                        "CanPlaceAt", 6);
    rc |= patch_replace(ADDR_UNIT_WEAPON_INFO, (const void *)UnitWeaponInfo,
                        "UnitWeaponInfo", 3);
    rc |= patch_replace(ADDR_REGION_SOLVE_PAIR, (const void *)RegionSolvePair,
                        "RegionSolvePair", 4);
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
