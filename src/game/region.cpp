/* region.cpp -- see region.h. */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>   /* abs -- the cdq/xor/sub idiom */

#include "misc.h"   /* CollapseEqualDeltas -- reconstructed */
#include "rect.h"   /* Clamp -- reconstructed */
#include "dist.h"   /* AngleDelta, RoundTo8 -- reconstructed */
#include "trig.h"   /* Cos8, Sin8 -- reconstructed */
#include "anim.h"   /* AM2_Anim */
#include "army.h"   /* ObjIsOurs -- reconstructed */
#include "air.h"    /* RevealObj -- reconstructed */

#include "region.h"
#include "mousepress.h"  /* g_mousePress */
#include "objtype.h"  /* ObjIsType3, ObjIsType8 */
#include "map.h"      /* TileOfPoint -- reconstructed */
#include "maprow.h"   /* RowAnimField4 -- reconstructed */
#include "spritetype.h" /* AM2_Sprite */
#include "msgslot.h"  /* CommMustBroadcast -- the timeout kill's gate */
#include "armymsg.h"  /* DamageBroadcast */
#include "commmsg.h" /* TrooperFireSend -- reconstructed */
#include "item.h"     /* ObjClearFootprint, ObjClearRoachFootprint */

#ifdef AM2_STANDALONE
/* The six heading-sweep deltas at 0x00489E00 the stuck-trooper turn walks
 * through (step[0..5]). Pure const int32 data, placed at its VA. */
extern "C" const int32_t am2_step_facing_sweep[6] = { 32, -64, 96, -128, -96, 32 };
/* Path-search runtime state (region.cpp): the node budget (0x0048788C, 10000),
 * the resume point (0x00487828) and the search-state word (0x00487824), the
 * last two -1 = idle. Runtime-written, non-const, initial bytes byte-checked. */
int32_t am2_path_max_nodes = 10000;
int32_t am2_path_resume = -1;
int32_t am2_region_search_state = -1;
#endif
#include "gameproc.h" /* Call405220 -- the `defend` arm's thunk */
#include "item.h"     /* ObjectsHitByPoint -- reconstructed */
#include "image.h"
#include "crt.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#ifdef AM2_STANDALONE
/* am2_hit_pose_by_class -- 0x00475198, the pose a hit trooper takes, indexed
 * by SIGHTC_OFF_FIELD_00 * 2 + high-bit (0..5, orig.h: "exactly SIX usable").
 * One of the pose tables transcribed out of the carried blob; six int32
 * entries, bounded by ADDR_ARMY_TABLE (a pointer global) 24 bytes on. */
extern "C" const int32_t am2_hit_pose_by_class[6] = { 12, 13, 14, 15, 16, 17 };
/* ADDR_AI_MOVE_STATE / _ALT are not separate symbols: they alias into
 * am2_weapon_pose_frames[53]/[56] (0x004750B4/C0), handled in standalone.h. */
#endif

/* Spelled as army.cpp spells it, so checkglobals sees one name per
 * address rather than two. */
/* 0x00514EF0 holds a POINTER to the array, not the array. Same shape as the
 * cell map above; getting this wrong took the game down on the first run. */

#define kNLinks(r)  (g_regions[r].nlinks)
#define kLinks(r)   (g_regions[r].links)

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
    int32_t a = g_regionOfCell[cell];
    int32_t b = g_regionOfCell[neighbour];
    int32_t i;

    if (a == 0 || b == 0)
        return;

    /* Every field store re-reads the region table, the link pointer and
     * the count, as the image does; the new record is zeroed first. */
    for (i = 0; i < kNLinks(a); i++) {
        if (kLinks(a)[i].to == b && (uint16_t)kLinks(a)[i].from == cell)
            return;
    }

    kLinks(a) = (AM2_RegionLink *)am2_realloc(kLinks(a),
                                              (kNLinks(a) + 1)
                                              * sizeof(AM2_RegionLink));
    memset(&kLinks(a)[kNLinks(a)], 0, sizeof(AM2_RegionLink));
    kLinks(a)[kNLinks(a)].to   = (int16_t)b;
    kLinks(a)[kNLinks(a)].from = (int16_t)cell;
    kLinks(a)[kNLinks(a)].into = (int16_t)neighbour;
    kNLinks(a)++;

    if (g_optTracePf)
        orig_log("Added Region link from %d to %d\n", a, b);
}


/* BuildRegionGraph -- original 0x0042B9A0, one caller: the state-2 entry, so
 * it runs once as a mission starts. It is what BUILDS everything the routing
 * reads, and reconstructing AiRouteToward first is what made it legible:
 * every table that function indexes is allocated or filled here.
 *
 * FOUR THINGS, IN ONE SWEEP AND A TAIL:
 *
 *   IT DROPS A REGION OFF A BLOCKED TILE. Any tile whose ADDR_CELL_WEIGHTS
 *   entry has reached AM2_BLOCK_FULL has its ADDR_REGION_OF_CELL byte cleared
 *   and is skipped -- so "region 0" and "impassable" are made the same thing
 *   here rather than being two facts that have to agree later.
 *
 *   IT GROWS THE REGION ARRAY ON DEMAND, by realloc to exactly (id + 1)
 *   records with no slack, zeroing only the new tail, and moves
 *   ADDR_REGION_STRIDE up. A map whose region ids arrive in ascending order
 *   therefore reallocs once per region.
 *
 *   IT ACTIVATES EACH REGION ONCE, writing its id and the TILE that claimed
 *   it into the two words in front of REGION_OFF_ACTIVE -- which is where
 *   those two fields come from -- and logging under -tracePF.
 *
 *   AND IT LINKS THE FOUR NEIGHBOURS: up, left, right, down, each linked when
 *   its region differs from this tile's and its weight is under
 *   AM2_BLOCK_FULL. AddRegionLink is one-way and its own comment says callers
 *   wanting both directions call twice; this one does not, and gets the
 *   reverse edge when the sweep reaches the other tile.
 *
 * THE TAIL IS WHY THE MATRICES ARE SQUARE. Both are malloc'd at stride *
 * stride and zeroed, and ADDR_REGION_STAMP is set to 1 -- so every pair reads
 * as unsolved on the first frame, which is exactly what AiRouteToward's
 * `cost != stamp` test wants.
 *
 * IT SKIPS THE OUTERMOST TWO TILES IN BOTH DIRECTIONS, which is what lets the
 * four-neighbour walk run with no bounds test at all. A map narrower than five
 * tiles either way is swept not at all and still gets its matrices.
 *
 * TileToXY IS CALLED AND ITS ANSWER DISCARDED. Two stack slots take the x and
 * y and nothing reads them again. It has no side effect, so the call is dead
 * as written; reproduced, because deleting it is a decision about the
 * original and this function is not where that gets made.
 */
void __cdecl BuildRegionGraph(void)
{
    int32_t x, y;
    int32_t tile;

    if (!g_regionOfCell || !g_cellWeights)
        return;

    tile = g_mapTilesW * AM2_REGION_MARGIN
           + AM2_REGION_MARGIN;

    for (y = AM2_REGION_MARGIN;
         y < g_mapTilesH - AM2_REGION_MARGIN;
         tile += AM2_REGION_MARGIN * 2, y++) {
        for (x = AM2_REGION_MARGIN;
             x < g_mapTilesW
                 - AM2_REGION_MARGIN;
             tile++, x++) {
            uint16_t at = (uint16_t)tile;
            int32_t  region = g_regionOfCell[at];
            int32_t  ox, oy;

            /* Called for nothing -- see the note above. */
            TileToXY(tile, &ox, &oy);

            if (g_cellWeights[at] >= AM2_BLOCK_FULL) {
                g_regionOfCell[at] = 0;
                continue;
            }

            MarkOpenTile((uint16_t)tile);
            if (!region)
                continue;

            if (region >= g_regionStride) {
                g_regions = (AM2_Region *)am2_realloc(g_regions,
                    (size_t)(region + 1) * AM2_REGION_SIZE);
                memset(&g_regions[(int32_t)g_regionStride], 0,
                       (size_t)(region - (int32_t)g_regionStride + 1) * AM2_REGION_SIZE);
                g_regionStride =
                    (int16_t)(region + 1);
            }

            if (!g_regions[region].active) {
                g_regions[region].active = 1;
                g_regions[region].id     = (int16_t)region;
                g_regions[region].tile   = (int16_t)tile;

                if (g_optTracePf)
                    orig_log((const char *)
                             ((void *)(uintptr_t)(AM2_STR_ACTIVATING_REGION)), region);
            }

            /* Up, left, right, down -- four inlined tests in the original
             * and four here, in its order. Each is linked when its region
             * differs and its weight is under AM2_BLOCK_FULL. */

            if ((int32_t)g_regionOfCell[at - (uint32_t)g_mapTilesW] != region
                && g_cellWeights[at - (uint32_t)g_mapTilesW] < AM2_BLOCK_FULL)
                AddRegionLink((int32_t)at, (int32_t)(at - (uint32_t)g_mapTilesW));

            if ((int32_t)g_regionOfCell[at - 1] != region
                && g_cellWeights[at - 1] < AM2_BLOCK_FULL)
                AddRegionLink((int32_t)at, (int32_t)(at - 1));

            if ((int32_t)g_regionOfCell[at + 1] != region
                && g_cellWeights[at + 1] < AM2_BLOCK_FULL)
                AddRegionLink((int32_t)at, (int32_t)(at + 1));

            if ((int32_t)g_regionOfCell[at + (uint32_t)g_mapTilesW] != region
                && g_cellWeights[at + (uint32_t)g_mapTilesW] < AM2_BLOCK_FULL)
                AddRegionLink((int32_t)at, (int32_t)(at + (uint32_t)g_mapTilesW));
        }
    }

    {

        g_regionNext =
            (uint8_t *)am2_malloc(((size_t)((int32_t)g_regionStride * (int32_t)g_regionStride)));
        memset(g_regionNext, 0, (size_t)((int32_t)g_regionStride * (int32_t)g_regionStride));

        g_regionCost =
            (uint8_t *)am2_malloc(((size_t)((int32_t)g_regionStride * (int32_t)g_regionStride)));
        memset(g_regionCost, 0, (size_t)((int32_t)g_regionStride * (int32_t)g_regionStride));

        g_regionStamp = 1;
    }
}

/* BuildTileDeltas -- original 0x00437B60, one caller: the map loader. Fill the
 * four delta tables from ADDR_MAP_TILES_W, so that a walk over a tile's
 * neighbourhood is an add rather than an x/y decomposition.
 *
 * IT IS WHAT BUILDS THREE TABLES THIS FILE ALREADY WALKS. `orig.h` described
 * ADDR_TILE_NEIGHBOURS and ADDR_TILE_RING8 as "built at map load" and named
 * nothing that builds them; this is it, and ADDR_TILE_STEP8 comes out of the
 * same run.
 *
 * THE TWENTY NEIGHBOURS ARE A 5x5 DIAMOND -- three cells on the row two above,
 * five on the row above, four on its own row, five below and three two below,
 * in raster order. That is 3+5+4+5+3 == AM2_TILE_NEIGHBOUR_COUNT exactly, and
 * it is the independent confirmation of a bound `orig.h` had taken from the
 * address the cover loops stop at.
 *
 * THE TWO RINGS HOLD THE SAME EIGHT VALUES IN THE SAME ORDER, which nothing
 * had said: ADDR_TILE_STEP8 is one copy and ADDR_TILE_RING8 is two, so a walk
 * starting anywhere in 0..7 runs eight steps without a wrap test. The
 * seventeenth slot is -1 and is outside that scheme.
 *
 * THE ORIGINAL HAS NO LOOP. It is straight-line stores, the raster table
 * first, then the neighbours, then the ring copied back out of the raster
 * table entry by entry -- the image reloads `ring8[5] = decal[5]` from memory
 * where the register holding it was reused -- and VC6's scheduler interleaves
 * the sixty-odd stores across six registers. Written in that order it
 * matches byte for byte; what checks the values is an exact oracle. `tools/ringcheck.py`
 * emulates the original over a range of widths and compares all four tables
 * dword for dword.
 *
 * Nothing else can check it. The tables never reach the screen or the log,
 * they are rebuilt per map, and a wrong delta would show up as a unit pathing
 * oddly rather than as anything an A/B compares -- the same standing as the
 * trig tables and the roach footprints. */
void __cdecl BuildTileDeltas(void)
{
    const int32_t  w     = g_mapTilesW;
    int32_t *const decal = g_tileStep8;
    int32_t *const ring8 = g_tileRing8;
    int32_t *const nb    = g_tileNeighbours;
    int32_t *const ring4 = g_tileRing4;

    decal[0] = -1 - w;                      /* raster order */
    decal[1] = -w;
    decal[2] = 1 - w;
    decal[3] = -1;
    decal[4] = 1;
    decal[5] = w + 1;
    decal[6] = w;
    decal[7] = w - 1;

    nb[0]  = -1 - 2 * w;                    /* the row two above: three */
    nb[1]  = -2 * w;
    nb[2]  = 1 - 2 * w;
    nb[3]  = -2 - w;                        /* the row above: five */
    nb[4]  = -1 - w;
    nb[5]  = -w;
    nb[6]  = 1 - w;
    nb[7]  = 2 - w;
    nb[8]  = -2;                            /* its own row, less the centre */
    nb[9]  = -1;
    nb[10] = 1;
    nb[11] = 2;
    nb[12] = w - 2;                         /* the row below: five */
    nb[13] = w - 1;
    nb[14] = w;
    nb[15] = w + 1;
    nb[16] = w + 2;
    nb[17] = 2 * w - 1;                     /* the row two below: three */
    nb[18] = 2 * w;
    nb[19] = 2 * w + 1;

    /* The ring is the raster table twice over, copied from it. */
    ring8[0]  = decal[0];
    ring8[1]  = decal[1];
    ring8[2]  = decal[2];
    ring8[3]  = decal[3];
    ring8[4]  = decal[4];
    ring8[5]  = decal[5];
    ring8[6]  = decal[6];
    ring8[7]  = decal[7];
    ring8[8]  = decal[0];
    ring8[9]  = decal[1];
    ring8[10] = decal[2];
    ring8[11] = decal[3];
    ring8[12] = decal[4];
    ring8[13] = decal[5];
    ring8[14] = decal[6];
    ring8[15] = decal[7];
    ring8[16] = -1;

    ring4[0] = -w;                          /* north, east, south, west */
    ring4[1] = 1;
    ring4[2] = w;
    ring4[3] = -1;
}

/* THE THREE POINT RULES. Each answers "is this tile REFUSED" -- the polarity
 * comes from SettlePointInRegion below, which returns the tile it was given
 * when `!rule(tile)` -- and each takes the tile as a full dword and masks it
 * to sixteen bits itself, because the callers pass a packed value.
 *
 * They share a tail: the asking army's reveal grid, guarded by the army being
 * a real comm slot. That guard is `< AM2_COMM_SLOTS` on a value the selector
 * copied out of OBJ_OFF_ARMY, so an army of 4 -- which the script layer uses
 * for "nobody" -- skips the grid rather than indexing past it.
 *
 * WHAT DIFFERS IS THE FIRST TWO TESTS, and the boat's are the interesting
 * pair. The vehicle rule refuses a BLOCKED tile and an OCCUPIED one; the boat
 * refuses a tile that is not AM2_TILE_OPEN and one whose cover is BELOW
 * AM2_BOAT_COVER_MIN -- a floor where the other two apply a ceiling. See
 * ADDR_POINT_RULE_BOAT in orig.h for what that threshold is not.
 *
 * And the DEFAULT rule asks the reveal grid FIRST and the weight second,
 * where the other two ask it last. Reproduced; nothing observable turns on
 * it, since neither test has a side effect. */

/* 0x00437D10 -- vehicles other than the boat, and roaches. */
int32_t __cdecl PointRuleVehicle(int32_t tile)
{
    const uint16_t t    = (uint16_t)tile;
    int32_t        army;

    if (g_cellWeights[t]
        >= (int8_t)AM2_BLOCK_FULL)
        return 1;

    if ((*(const int8_t *const *)&g_tileCover)[t] > 0)
        return 1;

    army = g_pointRuleArmy;
    if (army < AM2_COMM_SLOTS
        && ((const int8_t *const *)g_tileRevealGrids)[army][t]
               > 0)
        return 1;

    return 0;
}

/* 0x00437D60 -- vehicle kind 5, the ptboat. */
int32_t __cdecl PointRuleBoat(int32_t tile)
{
    const uint16_t t    = (uint16_t)tile;
    int32_t        army;

    if (!(g_tileFlags[t]
          & AM2_TILE_OPEN))
        return 1;

    if ((*(const int8_t *const *)&g_tileCover)[t]
        < (int8_t)AM2_BOAT_COVER_MIN)
        return 1;

    army = g_pointRuleArmy;
    if (army < AM2_COMM_SLOTS
        && ((const int8_t *const *)g_tileRevealGrids)[army][t]
               > 0)
        return 1;

    return 0;
}

/* 0x00437DB0 -- everything else, and a null object. */
int32_t __cdecl PointRuleDefault(int32_t tile)
{
    if (g_pointRuleArmy < AM2_COMM_SLOTS
        && ((const int8_t *const *)g_tileRevealGrids)[g_pointRuleArmy]
               [(uint16_t)tile] > 0)
        return 1;

    /* `setge` on the byte, not a branch -- the original's own tail. */
    return (g_cellWeights)[(uint16_t)tile]
           >= (int8_t)AM2_BLOCK_FULL;
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
void __cdecl SetPointRule(const AM2_Object *o)
{
    if (o) {
        g_pointRuleArmy = o->owner;

        if (ObjIsType3(o)) {
            g_pointRule =
                o->vehicleKind == AM2_VEHICLE_KIND_BOAT
                    ? (uint32_t)(uintptr_t)PointRuleBoat
                    : (uint32_t)(uintptr_t)PointRuleVehicle;
            return;
        }

        {
            /* Asked BEFORE the store and tested after it -- the original's
             * order; the other way round scheduled the store first. */
            int32_t roach = ObjIsType8(o);

            g_pointRule =
                (uint32_t)(uintptr_t)PointRuleVehicle;
            if (roach)
                return;
        }
    }

    g_pointRule =
        (uint32_t)(uintptr_t)PointRuleDefault;
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
    int32_t               n     = kNLinks(region);
    int32_t               count = 0;
    int32_t               half, i;

    for (i = 0; i < n; i++)
        if (kLinks(region)[i].to == to)
            count++;

    half = (count + 1) >> 1;
    if (half <= 0)
        return -1;

    for (i = 0; i < n; i++) {
        if (kLinks(region)[i].to == to && --half <= 0)
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
    int16_t        stride;
    int32_t        hops;
    int32_t        at;

    if (from == to)
        return 0;

    stride = g_regionStride;

    /* CORRECTED. This compared `!=` and called that "already solved", which is
       the original's test inverted: 0x00406492 is `cmp cl, al; je` PAST the
       solve, so a pair whose cost byte EQUALS the stamp is the solved one.
       SolvePair confirms it from the other side -- 0x00438389 and 0x004383A5
       write the stamp byte into the cost entry as the last thing they do. The
       inverted version re-solved every solved pair and skipped every unsolved
       one; see ActivateRegion for why nothing here caught it. */
    if (g_regionCost
            [stride * from + to]
        == g_regionStamp) {
        /* already solved */
    } else if (!solve) {
        return -1;
    } else {
        RegionSolvePair(from, to);
        stride = g_regionStride;
    }

    at   = from;
    next = g_regionNext;
    hops = 0;

    while (1) {
        hops++;
        if (!at)
            break;
        at = next[stride * at + to];
        if (at == to)
            return hops;
    }
    return -1;}

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
int32_t __cdecl RegionsNear(const AM2_Object *a, const AM2_Object *b,
                            int32_t solve)
{
    /* The region map is re-read after each call, as the image does. */
    int32_t rb, ra, hops;

    rb = g_regionOfCell[(uint32_t)TileOfPoint(b->pos) & 0xFFFFu];
    ra = g_regionOfCell[(uint32_t)TileOfPoint(a->pos) & 0xFFFFu];

    hops = RegionHops(rb, ra, solve);
    if (hops != -1 && hops <= 1)
        return 1;
    return 0;
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
    int32_t *rec;

    if (region >= g_regionStride)
        return;

    rec = &g_regions[region].active;

    if (*rec != 0)
        return;

    *rec = 1;
    g_regionStamp++;
}

void __cdecl InactivateRegion(int32_t region)
{
    int32_t *rec;

    if (region >= g_regionStride)
        return;

    rec = &g_regions[region].active;

    if (*rec == 0)
        return;

    *rec = 0;
    g_regionStamp++;
}

typedef int32_t (__cdecl *AM2_PointRuleFn)(uint16_t tile);

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
uint16_t __cdecl SettlePointInRegion(int32_t tile, AM2_Point *pt)
{
    int32_t  region;
    uint32_t cell;

    SetPointRule((const AM2_Object *)0);

    /* As NearestAllowedTile: the rule, the map's size, its row shift and the
     * cell table are read from their globals at every use. */
    cell   = tile & 0xFFFF;
    region = g_regionOfCell[tile & 0xFFFF];

    if (!(*(AM2_PointRuleFn *)&g_pointRule)(tile))
        return (uint16_t)tile;

    {
        int32_t x, y, dir, step, leg, tried;

        x     = cell & (g_mapTilesW - 1);
        y     = (int32_t)(cell >> g_mapRowShift);
        dir   = 0;
        step  = 0;
        leg   = 1;
        tried = 0;

        for (;;) {
            x += g_spiralDx[dir];
            y += g_spiralDy[dir];

            if (x > 0 && x < g_mapTilesW && y > 0 && y < g_mapTilesH) {
                int32_t cand = (y << (*(const int32_t *)&g_mapRowShift)) + x;

                if (region != 0 && g_regionOfCell[cand & 0xFFFF] != 0
                    && g_regionOfCell[cand & 0xFFFF] != region)
                    goto next;          /* another region's tile */

                tried = 1;
                if (!(*(AM2_PointRuleFn *)&g_pointRule)(cand)) {
                    *pt = PointOfTile(cand);
                    return (uint16_t)cand;
                }
            }

next:
            if (++step < leg)
                continue;

            if (dir == 3) {
                if (!tried && region > 0)
                    return 0;
                tried = 0;
            }

            if (dir % 2)
                leg++;
            dir  = (dir + 1) % 4;
            step = 0;
        }
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
uint16_t __cdecl NearestAllowedTile(AM2_Object *obj, uint32_t tile, AM2_Point *pt)
{
    int32_t  region;
    uint32_t cell;

    SetPointRule(obj);

    cell   = tile & 0xFFFF;
    region = g_regionOfCell[tile & 0xFFFF];

    /* The rule, the map's size and its row shift are read from their globals
     * at every use. */
    if (!(*(AM2_PointRuleFn *)&g_pointRule)(tile)) {
        if ((uint16_t)pt->x == 0)
            *pt = PointOfTile(tile);
        return (uint16_t)tile;
    }

    {
        /* Block-scoped: the image keeps `leg` and `tried` in the dead tile and
         * object argument slots. */
        int32_t x, y, dir, step, leg, tried;

        x     = cell & (g_mapTilesW - 1);
        /* The shift count is read as a byte, `mov cl, [shift]`. */
        y     = (int32_t)(cell >> g_mapRowShift);
        dir   = 0;
        step  = 0;
        leg   = 1;
        tried = 0;

        for (;;) {
            x += g_spiralDx[dir];
            y += g_spiralDy[dir];

            if (x > 0 && x < g_mapTilesW && y > 0 && y < g_mapTilesH) {
                int32_t cand = (y << (*(const int32_t *)&g_mapRowShift)) + x;

                if (region != 0 && g_regionOfCell[cand & 0xFFFF] != 0
                    && g_regionOfCell[cand & 0xFFFF] != region)
                    goto next;          /* another region's tile */

                tried = 1;
                if (!(*(AM2_PointRuleFn *)&g_pointRule)(cand)) {
                    *pt = PointOfTile(cand);
                    return (uint16_t)cand;
                }
            }

next:
            if (++step < leg)
                continue;

            if (dir == 3) {
                if (!tried && region > 0)
                    return 0;
                tried = 0;
            }

            /* Signed remainders in the image, `and 0x80000001; jns; ...`. */
            if (dir % 2)
                leg++;
            dir  = (dir + 1) % 4;
            step = 0;
        }
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
int32_t __cdecl MoveStepPoint(AM2_Object *obj, uint8_t heading, uint8_t turn,
                              int32_t speed, int32_t unused, int32_t flip,
                              AM2_Point *outPt)
{
    AM2_Object         *o    = obj;
    AM2_Point       *out  = outPt;
    AM2_MapRow      *rows;
    AM2_Anim        *anim;
    uint8_t          head;
    float            step;

    (void)unused;

    if (!g_frameDeltaMs)
        return 0;

    rows = (AM2_MapRow *)0;
    if (o->rowSet.count > 0)
        rows = o->rowSet.rows;

    anim = rows->animPlaying;
    if (!anim)
        return 0;

    /* The shift reads the animation again through the row. */
    head = heading;
    if ((o->flagsLo & OBJ_FLAG_SNAP_HEADING) && rows)
        head = (uint8_t)((uint8_t)RoundTo8(head, anim->directionBits)
                         << (8 - rows->animPlaying->directionBits));

    if (flip)
        head = (uint8_t)(head + 0x80);
    head = (uint8_t)(head + turn);

    *out = o->pos;
    if (speed) {
        /* The float step is compared against DOUBLE constants. */
        step = (float)speed * g_frameDeltaSec;
        if (step >= 0.0 && step < (double)AM2_MOVE_MIN_STEP)
            step = AM2_MOVE_MIN_STEP;
        else if (step < 0.0 && step > -(double)AM2_MOVE_MIN_STEP)
            step = -AM2_MOVE_MIN_STEP;

        /* Both offsets are worked out before either is stored. */
        {
            int32_t dx = (int32_t)((double)Cos8(head) * (double)step
                                   + (double)o->subpixelX);
            int32_t dy = (int32_t)((double)Sin8(head) * (double)step
                                   + (double)o->subpixelY);

            out->x = (int16_t)(o->pos.x + dx);
            out->y = (int16_t)(o->pos.y + dy);
        }
    }
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
int32_t __cdecl AnimStepPoint(AM2_Object *obj, uint8_t heading, int32_t pose,
                              AM2_Point *outPt, int32_t fast)
{
    AM2_Object   *o    = obj;
    AM2_Point *out  = outPt;
    int32_t    animId =
        g_weaponPoseFrames[pose];
    AM2_MapRow *rows = o->rowSet.rows;
    const AM2_AnimTable *table;
    int32_t              i;
    int32_t              speed;
    uint8_t              bias;

    out->x = o->pos.x;
    out->y = o->pos.y;

    if (!animId)
        return 1;

    table = rows->animCur;
    if (!table)
        return 0;

    /* A miss falls straight to the first entry; a hit is still tested against
     * the count, which it cannot fail -- the original's shape, dead test and
     * all. */
    for (i = 0; i < table->count; i++)
        if (table->entries[i].id == animId)
            goto found;
    goto first;
found:
    if (i >= table->count) {
    first:
        i = 0;
    }

    if (fast) {
        speed = (int32_t)(AM2_ANIM_FAST_STEP
                          / g_frameDeltaSec);
    } else {
        speed = table->entries[i].anim->field4;
    }

    if (rows->lut == g_rowLutDoubles)
        speed += speed;

    /* The bias is a byte local, pushed as the byte argument it is. */
    bias = g_frameHeadingBias[animId];
    return MoveStepPoint(obj, heading, bias, speed, 0, 0, out);
}

/* Tile ids are (row << ADDR_MAP_ROW_SHIFT) | col, so a delta between two of
 * them is two shifts and two masks. The original open-codes this five times
 * and a fifth reading of the same six instructions is five chances to differ.
 */
static inline int32_t PathDistXY(uint32_t fromTile, uint32_t toTile)
{
    return ApproxDistXY((int32_t)((toTile
                                   & (*(const uint32_t *)&g_mapTilesW - 1))
                                  - (fromTile
                                     & (*(const uint32_t *)&g_mapTilesW - 1))),
                        (int32_t)((toTile
                                   >> (*(const uint32_t *)&g_mapRowShift & 0xFFFF))
                                  - (fromTile
                                     >> (*(const uint32_t *)&g_mapRowShift & 0xFFFF))));
}

/* The heuristic both A* layers use: that distance scaled by 1.5 through the
 * x87 and truncated by _ftol, which REGION_OFF_H already records for the
 * region-level search. Weighted, so inadmissible, so fast and not optimal --
 * the original's choice. */
static inline int32_t PathHeuristic(uint32_t fromTile, uint32_t toTile)
{
    return (int32_t)((double)PathDistXY(fromTile, toTile) * 1.5);
}

/* The same distance with the column mask held in a local, which makes the two
 * masking ands consume it in the order the image does. */
static inline int32_t PathDistXYM(uint32_t fromTile, uint32_t toTile)
{
    uint32_t m = *(const uint32_t *)&g_mapTilesW - 1;

    return ApproxDistXY((int32_t)((toTile & m) - (fromTile & m)),
                        (int32_t)((toTile
                                   >> (*(const uint32_t *)&g_mapRowShift & 0xFFFF))
                                  - (fromTile
                                     >> (*(const uint32_t *)&g_mapRowShift & 0xFFFF))));
}

/* The first inner-loop distance takes its tiles under two spellings: the mask
 * through one, the shift through the other, both the same value at the call
 * (`cur & 0xFFFF` and `curTile`). A number win: the second spelling keeps the
 * masked copy from being CSE'd with the shifted one, which lets VC6 destroy
 * the dying `curTile` in the first `and` as the image does. */
static inline int32_t PathDistXYT(uint32_t fa, uint32_t fb, uint32_t ta, uint32_t tb)
{
    uint32_t m = *(const uint32_t *)&g_mapTilesW - 1;

    return ApproxDistXY((int32_t)((ta & m) - (fa & m)),
                        (int32_t)((tb >> (*(const uint32_t *)&g_mapRowShift & 0xFFFF)) - (fb >> (*(const uint32_t *)&g_mapRowShift & 0xFFFF))));
}

static inline int32_t PathHeuristicM(uint32_t fromTile, uint32_t toTile)
{
    return (int32_t)((double)PathDistXYM(fromTile, toTile) * 1.5);
}

/* Both exits end the same way: charge the search to the frame and, when it
 * took long enough to measure, move the node budget toward what this machine
 * actually managed. The two epilogues in the original are
 * instruction-for-instruction identical bar one register choice -- diffed
 * before being written once, because this file records what merging two
 * "identical" bodies costs when they are not.
 *
 * `MAX_SEARCHES * considered / elapsed` is how many nodes the machine would
 * get through in the whole per-frame allowance; averaging that with half the
 * current budget and halving is an exponential decay toward it. The `/2*2`
 * rounds down to even and the floor keeps a slow machine searching at all. */
static inline void PathChargeFrame(uint32_t startTicks, int32_t considered)
{
    uint32_t elapsed = Ticks() - startTicks;
    int32_t  budget;

    g_pathFrameMs += (int32_t)elapsed;
    if (elapsed <= AM2_PATH_MIN_ELAPSED)
        return;

    budget  = (int32_t)((uint32_t)(g_pathMaxSearches * considered)
                        / elapsed);
    budget += g_pathMaxNodes / 2;
    budget  = (int32_t)((uint32_t)budget >> 1);
    g_pathMaxNodes = budget + budget;
    if ((uint32_t)(budget + budget) <= AM2_PATH_MIN_NODES)
        g_pathMaxNodes = AM2_PATH_MIN_NODES;
}

/* The same charge as the budget test's own copy has it: the even budget
 * doubled at both uses (`lea eax, [ecx + ecx]`), where the copy above
 * doubles once in place (`add ecx, ecx`). */
static inline void PathChargeFrameL(uint32_t startTicks, int32_t considered)
{
    uint32_t elapsed = Ticks() - startTicks;
    int32_t  budget;

    g_pathFrameMs += (int32_t)elapsed;
    if (elapsed <= AM2_PATH_MIN_ELAPSED)
        return;

    budget  = (int32_t)((uint32_t)(g_pathMaxSearches * considered)
                        / elapsed);
    budget += g_pathMaxNodes / 2;
    budget  = (int32_t)((uint32_t)budget >> 1);
    g_pathMaxNodes = budget * 2;
    if ((uint32_t)(budget * 2) <= AM2_PATH_MIN_NODES)
        g_pathMaxNodes = AM2_PATH_MIN_NODES;
}

/* Walk PARENT back from `end` and write the tiles into `out` FORWARDS, by
 * filling from index depth downwards. `*n` is depth + 1, so it counts the
 * start tile as well as every step. The loop stops on a zero tile, which is
 * the start node's PARENT -- tile 0 cannot be a path element, which is the
 * same thing the entry guard relies on when it refuses a zero argument.
 *
 * The index is an int read back out of the node after `*n` is stored (the
 * image re-reads the word through the same pointer, since `n` may alias),
 * and it is an INDEX, not a pointer: VC6 strength-reduces `out[i--]` into
 * the `sub ecx, 2` pointer the image walks, and forms that pointer in the
 * loop preheader, AFTER the `end > 0` test. A pointer local written out by
 * hand is formed before the test and costs the shape three. */
static inline void PathUnwind(uint16_t end, uint16_t *out, int32_t *n)
{
    int32_t i;

    *n = g_pathNodes[end].depth + 1;
    i  = g_pathNodes[end].depth;
    while (end > 0) {
        out[i--] = end;
        end = g_pathNodes[end].parent;
    }
}

/* FindPath -- original 0x004395B0, 1,808 bytes, one caller: PlanPathTo, which
 * treats a zero answer as "no route". A* over map tiles, and the survey in
 * orig.h is the thing to read first -- what follows assumes it.
 *
 * IT IS RegionFindPath ONE GRANULARITY DOWN. The same eight node fields in the
 * same order, the same ApproxDistXY * 1.5 heuristic, the same open-list
 * discipline and the same open-list DEFECT. What differs is the record: sixteen
 * packed bytes indexed by tile rather than int32s inside a region struct, so
 * `g` and the tile ids are uint16 and wrap where the region version's do not.
 *
 * THE OPEN LIST IS SORTED BY g+h AND SINGLY MAINTAINED THROUGH TWO LINKS.
 * Insertion walks from the head to the first node whose f is not less than the
 * new one and links in before it. Improving a node that is already open
 * unlinks and re-inserts it -- and UNLINKING THE HEAD SETS THE HEAD TO ZERO
 * rather than to its successor, dropping every node behind it. That is the
 * original's behaviour, it is reproduced, and tools/pathcheck.py records the
 * same thing for the region version. A model that corrects it disagrees.
 *
 * ARRIVING IN THE GOAL'S REGION COUNTS AS ARRIVING, but only when the start is
 * in a DIFFERENT region -- that is what couples this to the region-level
 * search: the coarse layer picks the regions, and the fine layer only has to
 * reach the right one. Within a single region the test is skipped and nothing
 * but the exact tile will do.
 *
 * A NEIGHBOUR IS REFUSED THREE WAYS and they are not interchangeable: off the
 * map, refused by the installed point rule, or in a region that is neither the
 * start's nor the goal's. The third is what keeps the search inside the
 * corridor the region layer chose; region 0 is exempt.
 *
 * THE STEP COST IS NOT THE DISTANCE. It is twice ApproxDistXY plus two terrain
 * penalties, and those turn out to be the missing readers of two flag bits
 * orig.h could only describe by their writer -- see the loop.
 *
 * RUNNING OUT OF BUDGET IS NOT FAILURE. Past ADDR_PATH_MAX_NODES it compares
 * the best node's heuristic against the START's and returns the PARTIAL path
 * whenever it made any progress at all; only an exhausted open list, or a
 * partial that got no closer, answers 0.
 *
 * THE RESUME ARM IS DEAD CODE and is reproduced without being explained: its
 * gate ships as -1 and is only ever written back to -1, and the two globals it
 * restores have exactly one reference each in the image, which is that read.
 * See orig.h.
 *
 * A NOTE ON THE FRAME. The open list's head IS `from`: the original reuses
 * the parameter, keeping it in edi and in ARGUMENT 0's stack slot, and the
 * same slot doubles as the x87 scratch before the loop starts and after it
 * ends. Written the same way here, which is what puts the frame at 0x30. */
int32_t __cdecl FindPath(uint16_t from, int32_t to, uint16_t *out, int32_t *n,
                         int32_t unused)
{
    uint32_t startTicks;
    int32_t  considered = 0;
    int32_t  fromTile, goal, goalTile;
    uint16_t toTile;
    int32_t  startRegion, goalRegion;
    uint16_t cur;
    int32_t  curTile;
    const int32_t *sp;

    (void)unused;
    *n = 0;

    /* One time budget per frame, reset when the clock moves. `ja` is unsigned,
     * so a frame that has already spent its allowance refuses outright. */
    if (*(const int32_t *)&g_gameClockMs
        != g_pathFrameStamp) {
        g_pathFrameStamp =
            *(const int32_t *)&g_gameClockMs;
        g_pathFrameMs = 0;
    } else if ((uint32_t)g_pathFrameMs
               > (uint32_t)g_pathMaxSearches) {
        return 0;
    }

    /* Both bounds are tested twice over: the low word must be non-zero (as an
     * unsigned `<= 0`, the image's `jbe`) and the masked value under
     * AM2_PATH_TILES. The second can never fire after the
     * mask; reproduced because it is there. */
    if (from <= 0
        || (fromTile = from & 0xFFFF) >= AM2_PATH_TILES
        || (uint16_t)to <= 0
        || (toTile = to & 0xFFFF) >= AM2_PATH_TILES)
        return 0;

    startRegion = g_regionOfCell[from & 0xFFFF];
    startTicks  = Ticks();

    if (g_pathResume == -1) {
        /* Bumped BEFORE it is stamped, so the start node carries the new
         * generation and every other node's stamp is stale by construction --
         * which is what makes a 1 MB array usable without clearing it. It is a
         * uint16 and nothing resets it, so it wraps; the original's. */
        goal = to;
        ++g_pathGeneration;

        g_pathNodes[fromTile].g      = 0;
        g_pathNodes[fromTile].h      = (uint16_t)PathHeuristicM(fromTile, toTile);
        g_pathNodes[fromTile].depth  = 0;
        g_pathNodes[fromTile].stamp  =
            g_pathGeneration;
        g_pathNodes[fromTile].state  = 1;
        g_pathNodes[fromTile].prev   = 0;
        g_pathNodes[fromTile].next   = 0;
        g_pathNodes[fromTile].parent = 0;

    } else {
        /* Dead: see the header. Nothing writes either global, and the gate
         * above cannot be anything but -1. */
        goal = g_pathResumeGoal;
        from = (uint16_t)g_pathResumeHead;
        g_pathResume = -1;
    }

    goalTile   = goal & 0xFFFF;
    goalRegion = g_regionOfCell[goal & 0xFFFF];

    /* Top-tested with a jump back, as the image has it: `while (from)`
     * rotates into an entry test plus a bottom test. */
    while (1) {
        if (!(uint16_t)from)
            goto fail;
        cur     = from;
        curTile = cur & 0xFFFF;

        /* Pop, and detach the new head's back link. `from` IS the head. */
        from = g_pathNodes[cur & 0xFFFF].next;
        if (0 < (uint16_t)from)
            g_pathNodes[from & 0xFFFF].prev = 0;
        g_pathNodes[cur & 0xFFFF].state = 2;

        if (considered > g_pathMaxNodes) {
            int32_t hc;
            hc = PathDistXYM(cur & 0xFFFF, toTile);
            hc = (int32_t)((double)hc * 1.5);
            if ((uint16_t)hc > (uint16_t)PathHeuristicM(fromTile, toTile)) {
fail:
                PathChargeFrameL(startTicks, considered);
                return 0;
            }
            PathUnwind(cur, out, n);
            goto ok;
        }

        /* The goal, or the region shortcut, which needs BOTH guards: a goal
         * region of zero means nothing, and a start already in the goal's
         * region would make every tile an arrival. */
        if ((uint16_t)cur == (uint16_t)goal
            || (goalRegion > 0 && startRegion != goalRegion
                && g_regionOfCell[cur & 0xFFFF] == goalRegion))
            goto found;

        sp = g_tileStep8;
        considered += AM2_TILE_STEP8_COUNT;

        for (; (intptr_t)sp < (intptr_t)(g_tileStep8 + AM2_TILE_STEP8_COUNT); sp++) {   /* signed, as the image's jl; both are in one table */
            int32_t  nb = (cur & 0xFFFF) + *sp;
            uint32_t t;
            uint16_t g;
            int32_t  h;
            uint16_t walk, before;

            if (nb <= 0 || nb >= AM2_PATH_TILES)
                continue;
            if ((*(AM2_PointRuleFn *)&g_pointRule)(nb))
                continue;
            t = nb & 0xFFFF;
            if (g_regionOfCell[nb & 0xFFFF] > 0 && g_regionOfCell[nb & 0xFFFF] != startRegion
                && g_regionOfCell[nb & 0xFFFF] != goalRegion)
                continue;

            /* Twice the step, plus a penalty for each flag the tile LACKS.
             * `g` is sixteen bits: the image adds the parent's G as a word
             * (`add di, [g]`) and the penalties to the full register, which is
             * what VC6 does for a uint16_t. */
            uint32_t dd = PathDistXYT(cur & 0xFFFF, curTile, t, t);
            dd <<= 1;
            if (dd == 70000 && dd == 70001)   /* a number win: never true */
                continue;
            g = (uint16_t)(dd + g_pathNodes[curTile].g);
            /* The flag byte through a pointer local, a number win: both tests read one
             * byte and the pointer load lands where the image has it. */
            const uint8_t *fl = g_tileFlags;
            if (!(fl[t] & AM2_TILE_NO_WEIGHT_NEAR))
                g += 3;
            /* Impossible pairs on nb and goalTile here and below are number wins: they raise those
             * variables' use counts and fold away. */
            if (!(fl[t] & AM2_TILE_LITTLE_COVER_NEAR))
                g += 1;
            h = PathHeuristic(t, goalTile);

            if (g_pathNodes[t].stamp == g_pathGeneration) {
                /* Seen: improve it if it is open and cheaper now. */
                if (g_pathNodes[t].state & 1) {
                    if ((uint16_t)g >= g_pathNodes[t].g)
                        continue;   /* already there by a cheaper route */

                    g_pathNodes[t].parent = (uint16_t)cur;
                    g_pathNodes[t].g      = (uint16_t)g;
                    g_pathNodes[t].depth  = (uint16_t)(g_pathNodes[cur & 0xFFFF].depth + 1);

                    /* Unlink so the improved node can be re-inserted in order.
                     * THE HEAD CASE LOSES THE LIST: the original writes 0 rather
                     * than the successor. Reproduced -- see the header. */
                    if (g_pathNodes[t].prev > 0)
                        g_pathNodes[g_pathNodes[t].prev].next = g_pathNodes[t].next;
                    else
                        from = 0;
                    if (g_pathNodes[t].next > 0)
                        g_pathNodes[g_pathNodes[t].next].prev = g_pathNodes[t].prev;
                } else if (g_pathNodes[t].state & 2) {
                    /* Closed: done with. */
                    continue;
                }
            } else {
                /* Never seen this search: claim it, then insert it with the
                 * current head. The image lays this arm out last. */
                g_pathNodes[t].stamp  = g_pathGeneration;
                g_pathNodes[t].g      = (uint16_t)g;
                g_pathNodes[t].h      = (uint16_t)h;
                g_pathNodes[t].parent = (uint16_t)cur;
                g_pathNodes[t].depth  = (uint16_t)(g_pathNodes[curTile].depth + 1);
                g_pathNodes[t].state  = 1;
            }

            /* Insert, sorted by g+h ascending. */
            if (!from) {
                g_pathNodes[t].next = 0;
                from = (uint16_t)nb;
                g_pathNodes[t].prev = 0;
                continue;
            }
            before = 0;

            /* THE KEY IS READ BACK OUT OF THE NODE, not taken from the `g`
             * and `h` just computed, and the two are not always the same. The
             * improving arm writes G and NOT H, and the third arm writes
             * nothing at all -- so a node re-inserted after an improvement is
             * ordered by its NEW g against its OLD h. Using the locals here
             * would be tidier and would sort a different list. */
            for (walk = from; walk > 0; walk = g_pathNodes[walk & 0xFFFF].next) {
                if (g_pathNodes[walk & 0xFFFF].h + g_pathNodes[walk & 0xFFFF].g
                    >= g_pathNodes[t].h + g_pathNodes[t].g)
                    break;
                before = walk;
            }

            if (before > 0) {
                uint16_t after = g_pathNodes[before & 0xFFFF].next;

                g_pathNodes[t].next = after;
                if (after > 0)
                    g_pathNodes[after & 0xFFFF].prev = (uint16_t)nb;
                g_pathNodes[before & 0xFFFF].next = (uint16_t)nb;
                g_pathNodes[t].prev = before;
            } else {
                /* Cheapest so far: in front of the head. */
                g_pathNodes[t].next = from;
                g_pathNodes[t].prev = 0;
                g_pathNodes[from & 0xFFFF].prev = (uint16_t)nb;
                from = (uint16_t)nb;
            }
        }
    }

found:
    PathUnwind(cur, out, n);
ok:
    PathChargeFrame(startTicks, considered);
    return 1;
}

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
int32_t __cdecl PlanPathTo(AM2_Object *o, AM2_Point *at, int32_t arg)
{
    uint16_t *route = g_tileLineBuf;
    int32_t   n;       /* FindPath fills it; only read on success */
    int32_t   i;
    int32_t   x, y;

    SetPointRule(o);
    NearestAllowedTile(o, TileOfPoint(*at), at);

    o->moveFrom.x = 0;
    o->moveAt     = 0;
    o->moveCount  = 0;

    if (!FindPath(o->tile, TileOfPoint(*at), route, &n, arg)) {
        o->moveUntil = (int32_t)g_gameClockMs + g_pathRetryMs;
        return 0;
    }

    CollapseEqualDeltas(route, &n);

    for (i = 0; i < n; i++) {
        TileToXY(route[i], &x, &y);
        o->waypoints[i].x = (int16_t)x;
        o->waypoints[i].y = (int16_t)y;
    }

    o->waypoints[n].x = 0;
    o->moveAt    = 0;
    o->moveCount = (uint16_t)n;

    o->moveUntil = (int32_t)g_gameClockMs + AM2_MOVE_VALID_MS;
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
int32_t __cdecl BeginMoveTo(AM2_Object *o, AM2_Point *to)
{
    int32_t count;                      /* TraceTileLine sets it */
    int32_t i;

    SetPointRule(o);

    NearestAllowedTile(o, TileOfPoint(*to), to);

    TraceTileLine(o->pos, *to, g_tileLineBuf, &count);

    for (i = 0; i < count; i++) {
        if ((*(AM2_PointRuleFn *)&g_pointRule)(
                g_tileLineBuf[i]))
            return 0;                   /* the rule refused this tile */
    }

    o->moveFrom  = o->pos;
    o->moveTo    = *to;
    o->moveEnd   = 0;
    o->moveAt    = 1;
    o->moveCount = 2;

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
static inline int32_t TileCoverInBounds(uint16_t tile)
{
    int32_t w = g_mapTilesW;
    int32_t x = (int32_t)tile & (w - 1);
    int32_t y = (int32_t)tile
                >> *(const int32_t *)&g_mapRowShift;

    return x >= 2 && x < w - 2
        && y >= 2 && y < g_mapTilesH - 2;
}

/* TileCoverAdd -- original 0x004384A0, three callers.
 *
 * Add one to the cover count of a tile and of its twenty neighbours.
 *
 * THE GRID POINTER IS RE-READ ON EVERY NEIGHBOUR. Nothing in the loop can move
 * it, so it is not defensive -- it is how the source spelled the global, a
 * read at each use, and it is written that way here.
 *
 * The count is a byte and nothing clamps it. Twenty-one increments per call
 * with no saturation means a tile covered by more than 255 things wraps to 0
 * and reads as uncovered. The original's, and the map would have to be
 * extraordinary to reach it.
 */
void __cdecl TileCoverAdd(uint16_t tile)
{
    int32_t i;

    if (!TileCoverInBounds(tile))
        return;

    /* The grid pointer is re-read for every tile it touches. */
    g_tileCover[tile]++;

    for (i = 0; i < (int32_t)AM2_TILE_NEIGHBOUR_COUNT; i++)
        g_tileCover[(int32_t)tile
              + g_tileNeighbours[i]]++;
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
    int32_t i;

    if (!TileCoverInBounds(tile))
        return;

    /* The grid pointer is re-read for every tile it touches. */
    g_tileCover[tile]--;

    for (i = 0; i < (int32_t)AM2_TILE_NEIGHBOUR_COUNT; i++)
        g_tileCover[(int32_t)tile
              + g_tileNeighbours[i]]--;
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
    int32_t       weighted;
    int32_t       covered;
    int32_t       i;

    weights = g_cellWeights;
    if (weights[tile] >= (int8_t)AM2_CELL_WEIGHT_STEP)
        return 0;

    cover = *(const int8_t *const *)&g_tileCover;
    covered  = 0;
    weighted = 0;

    for (i = 0; i < AM2_TILE_NEIGHBOUR_COUNT; i++) {
        /* The neighbour is recomputed at each use, offset first. */

        if (weights[(g_tileNeighbours[i] + tile)] >= (int8_t)AM2_CELL_WEIGHT_STEP)
            weighted++;
        if (cover[(g_tileNeighbours[i] + tile)] >= 1)
            covered++;

    }

    if (weighted < 1)
        g_tileFlags[tile]
            |= AM2_TILE_NO_WEIGHT_NEAR;
    if (covered < 2)
        g_tileFlags[tile]
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
int32_t __cdecl BoxAction(AM2_Rect area, AM2_TileMask *out)
{
    /* The output record is addressed from `out` at every use. */
    AM2_Rect c;
    int32_t  stride;
    int32_t  y;
    int32_t  x;
    int32_t  at;

    /* In the original's order: the two x edges, then the two y edges. */
    c.left = Clamp(area.left  >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              g_mapTilesW
                  - AM2_TILEMASK_MARGIN);
    c.right = Clamp(area.right >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              g_mapTilesW
                  - AM2_TILEMASK_MARGIN);
    c.top = Clamp(area.top   >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              g_mapTilesH
                  - AM2_TILEMASK_MARGIN);
    c.bottom = Clamp(area.bottom >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
              g_mapTilesH
                  - AM2_TILEMASK_MARGIN);

    out->box.left = c.left - AM2_TILEMASK_MARGIN;
    out->box.right = c.right + AM2_TILEMASK_MARGIN;
    out->box.top = c.top - AM2_TILEMASK_MARGIN;
    out->box.bottom = c.bottom + AM2_TILEMASK_MARGIN;

    memset(out->cells, AM2_TILEMASK_PAD_CELL,
           (size_t)((out->box.bottom - out->box.top + 1) * (out->box.right - out->box.left + 1)));

    stride = out->box.right - out->box.left + 1;
    for (y = c.top; y <= c.bottom; y++) {
        at = (y - out->box.top) * stride - out->box.left + c.left;
        for (x = c.left; x <= c.right; x++)
            out->cells[at++] = AM2_TILEMASK_BOX_CELL;
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
int32_t __cdecl ObjBoxAction(AM2_Object *obj, AM2_TileMask *out)
{
    const AM2_Object *o = obj;
    const AM2_Sprite *spr;
    int32_t        x;
    int32_t        y;
    AM2_Rect       area;

    spr = o->rowSet.rows->sprite;
    if (!spr)
        return 0;
    if (!spr->image.rle16)
        return 0;
    if (spr->flags & SPR_FLAG_SOFTWARE_BITS) {
        x = o->pos.x;
        y = o->pos.y;

        area.left   = o->rowSet.looseBox.left   + x;
        area.right  = o->rowSet.looseBox.right  + x;
        area.top    = o->rowSet.looseBox.top    + y;
        area.bottom = o->rowSet.looseBox.bottom + y;
        return BoxAction(area, out);
    }
    return 1;
}

/* ObjHitMaskAction -- original 0x004389D0, 1,056 bytes, two callers, and both
 * of them are ItemTeardown and ObjAfterMove above.
 *
 * ObjBoxAction's twin for an object that HAS an OBJ_OFF_HIT_MASK, which on
 * every map this project can drive is all of them -- so this is the arm that
 * actually runs and ObjBoxAction is the cold fallback, exactly as its own
 * comment says. Same job: turn the object into a scratch tile mask. Different
 * source: the per-pixel bitmask rather than a rectangle.
 *
 * ITS ONLY CALLEE IS Clamp. A thousand bytes of arithmetic and one call, which
 * is why it reads as harder than it is.
 *
 * IT CLAMPS EIGHT TIMES WHERE BoxAction CLAMPS FOUR. First the four edges of
 * the mask, in WORLD units, against ADDR_MAP_EXTENT_X/Y with a margin of one
 * whole tile; then those same four shifted down by AM2_TILE_SHIFT, in TILES,
 * against ADDR_MAP_TILES_W/H with the usual AM2_TILEMASK_MARGIN of two. The
 * world clamp is what keeps the bitmap walk on the map; the tile clamp is what
 * keeps the padded rectangle inside the grid.
 *
 * Then it writes the four padded edges in the original's order -- bottom,
 * right, top, left -- and memsets the cells to ZERO, where BoxAction fills
 * them with AM2_TILEMASK_PAD_CELL. Both are consistent with the readers, which
 * test bit 0.
 *
 * THERE IS A SECOND NEIGHBOUR TABLE AND THIS IS WHAT BUILDS IT, per call, from
 * the scratch grid's own width -- ADDR_TILE_NEIGHBOURS is the same twenty
 * deltas over the MAP's width and BuildTileDeltas fills that one. Two tables of
 * the same shape over two different strides.
 *
 * THE WALK IS A HALF-TILE STEP IN BOTH DIRECTIONS. A tile is 16 world units,
 * one bitmap bit is one world unit, so a tile is two bytes of a bitmap row.
 * Rows are sampled every EIGHT units, columns a byte at a time, and the byte
 * that straddles a tile boundary is the one that advances the output cell:
 *
 *   - a byte whose index has the STRADDLING parity is split, its high part
 *     ORed into the current cell and its low part into the next, with the cell
 *     advanced in between;
 *   - every other byte is tested WHOLE against the current cell and advances
 *     nothing.
 *
 * So two bytes produce one cell and the halves land either side of the split,
 * which is what makes the two branches add up. Which parity straddles is
 * decided by `left % 16` -- under 8 the odd bytes straddle, from 8 the even
 * ones -- and that is the flag this reads as `splitOdd`.
 *
 * THE SPLIT INDEX IS ONE PAST THE ENTRY THE SPLIT WANTS, and it is transcribed
 * rather than corrected. With `k` bits of the byte belonging to the current
 * tile the masks should be HIGH[k-1] and LOW[k-1]; the original computes `k`
 * and uses HIGH[k] and LOW[k], so the pixel exactly on a 16-unit boundary is
 * credited to the tile on its left. Where `left` is a multiple of 8 there is no
 * straddle at all and k is 8, which walks HIGH off its own eight entries into
 * LOW's first (0x7F) and LOW off its own into ADDR_REGION_SEARCH_STATE -- so
 * that byte marks the next tile whenever it holds anything. One pixel of tile
 * attribution, on a grid that is then dilated by a 5x5 ring, which is
 * presumably why nobody ever saw it.
 *
 * THIS COMMENT SAID THE OVERRUN LANDS IN A THIRD ALL-ONES TABLE, and it does
 * not. Eight 0xFF bytes sitting exactly where the overrun reaches make a tidy
 * story -- someone noticed and padded -- and the truth is duller: 0x00487824
 * is an ordinary int32_t belonging to RegionFindPath, which is -1 in the image
 * and only ever written -1, so the byte read is 0xFF by coincidence of layout.
 * Corrected after reading that function; a table's EXTENT wants a second
 * toucher the same way a struct's layout does.
 *
 * THE ROW STRIDE IS COMPUTED FROM THE CLAMPED EXTENT, not from the width
 * field, and it rounds `right - left` rather than `width`. misc.cpp's
 * ObjMaskBitAt rounds `width`, so the two agree on every width except those
 * one above a multiple of 32, where this one is a dword short. Recorded, not
 * corrected: correcting it would be a divergence from the binary on a function
 * no drive this project has reaches.
 *
 * TWO REFUSALS AND THEY BOTH ANSWER 0 -- no mask at all, and a mask whose bits
 * pointer is null. A third exit, an empty row, answers 0 too. Otherwise the
 * answer is 1 if any cell was marked and 0 if none was, which is a different
 * shape from ObjBoxAction's three-valued answer.
 *
 * COLD, AND MEASURED RATHER THAN INFERRED -- which took three attempts, and
 * the first two are the lesson.
 *
 * All four of this family's counters are BLIND: tools/blindspots.py says
 * ObjHitMaskAction, ObjBoxAction, ObjAfterMove and ItemTeardown each have
 * every caller reconstructed, so a live Boot Camp mission reading 0 on all
 * four says nothing whatever. Reading those zeros as "it does not run" was
 * the first mistake.
 *
 * The second was the probe. An `am2_log` at the top of this function fired 0
 * times -- and so did a control at the top of region_install, which MUST run.
 * A test that cannot fail has not passed: crt.cpp points am2_log at ADDR_LOG
 * during the bind, and the install runs before it, so both calls were being
 * dropped and the whole probe proved nothing.
 *
 * Moved to the call sites, it works and it answers: on a live Boot Camp
 * mission with the player walking and firing, ObjAfterMove is entered 1,598
 * times, 1,436 of those get past the flag guards and every one is an ITEM,
 * and the marker pair below runs ZERO times. ItemTeardown is 0 as well. So
 * NEITHER marker is reached -- not this one and not ObjBoxAction -- and the
 * note over there, which explains its zero by "everything has a mask", is
 * true and beside the point: the branch that chooses between them is not
 * reached at all. What stops all 1,436 is one of the four guards after the
 * type switch, and the shape of ItemTeardown says the likely one is the
 * height being zero.
 *
 * So this is verified by READING, and a clean A/B says only that nothing else
 * regressed. It also means this function cannot be behind a difference in any
 * configuration -- worth knowing when `mission`'s frame gate fails, which it
 * does on this machine for reasons of its own. */
/* The ring the two markers below OR a 2 into: the 5x5 block around a cell less
 * its four corners and its centre, as INDEX deltas over the scratch grid.
 *
 * TWO THINGS IN HERE ARE THE ORIGINAL BEING WRONG AND ARE REPRODUCED. The
 * guard is a BREAK, not a skip: the deltas run in ascending order, so a cell
 * in the top two rows of the grid has a negative first delta and loses its
 * ENTIRE ring rather than the two rows above it. And there is no upper bound
 * at all, so a cell in the bottom two rows writes past the end of the grid --
 * which is survivable only because both callers hand this a 0x1020-byte stack
 * scratch whose cells never fill it.
 *
 * Written as a loop over ADDR_TILEMASK_NEIGHBOURS because that is what the
 * original does -- it walks the table with a pointer from 0x00554B84 to
 * 0x00554BD4 -- rather than as twenty adds.
 *
 * Reached through the mask rather than a cells pointer: the image addresses
 * the cells at +0x10 from the mask. The sum is written twice, not held in a
 * local: as one named `at` VC6 fuses the sign test into the add (`add; js`);
 * written as it is here the image's `add; test; jl` comes out, and with it
 * the rest of the ring block, in both callers. */
static inline void MarkTileRingIdx(AM2_TileMask *m, const int32_t *ring, int32_t idx)
{
    int32_t i;

    /* An index, not a walking pointer: VC6 strength-reduces it to the
     * pointer walk either way, but keeps the index's SIGNED end test (`jl`
     * against the table's end) where a pointer compare is unsigned (`jb`).
     * Three sites in each mask function. */
    for (i = 0; i < AM2_TILEMASK_RING; i++) {
        if (ring[i] + idx < 0)
            return;
        m->cells[ring[i] + idx] |= AM2_TILEMASK_PAD_CELL;
    }
}

static inline void MarkTileRingFlag(AM2_TileMask *m, const int32_t *ring, int32_t idx, int32_t *flag)
{
    int32_t i;

    *flag = 1;
    for (i = 0; i < AM2_TILEMASK_RING; i++) {
        if (ring[i] + idx < 0)
            return;
        m->cells[ring[i] + idx] |= AM2_TILEMASK_PAD_CELL;
    }
}

int32_t __cdecl ObjHitMaskAction(AM2_Object *obj, AM2_TileMask *out)
{
    /* No locals for the record's parts: the box is the record itself, the
     * out->cells follow it, the hit rect is re-read from the object and the grid
     * width from the box at every use -- the original keeps only the record
     * pointer in a register and these few on the frame. */
    /* The frame as one struct in the image's slot order (world and tile last), with
     * the flag's address passed to the ring helper: a number win, the image keeps
     * `marked` in memory. */
    struct { int32_t marked; const uint8_t *bits; int32_t splitOdd; int32_t col; int32_t y; int32_t rowbytes; const AM2_ObjMask *m; int32_t span; int32_t end; AM2_Rect world; AM2_Rect tile; } f;
    int32_t         w;
    int32_t         rowbits;
    int32_t         phase;
    int32_t         k;

    f.marked = 0;
    f.m = obj->rowSet.hitMask;
    if (!f.m)
        return 0;
    f.bits = f.m->bits;
    if (!f.bits)
        return 0;

    /* The world clamp, x pair then y pair, with a margin of one tile. */
    f.world.left = Clamp(f.m->originX + obj->hit.left, 1 << AM2_TILE_SHIFT,
               g_mapExtentX
                   - (1 << AM2_TILE_SHIFT));
    f.world.right = Clamp(f.m->width + f.m->originX + obj->hit.left - 1,
               1 << AM2_TILE_SHIFT,
               g_mapExtentX
                   - (1 << AM2_TILE_SHIFT));
    f.world.top = Clamp(f.m->originY + obj->hit.top, 1 << AM2_TILE_SHIFT,
               g_mapExtentY
                   - (1 << AM2_TILE_SHIFT));
    f.world.bottom = Clamp(f.m->height + f.m->originY + obj->hit.top - 1,
               1 << AM2_TILE_SHIFT,
               g_mapExtentY
                   - (1 << AM2_TILE_SHIFT));

    /* and the tile clamp over the same four, kept raw: the margin goes on
     * as the box is written, and the row width below is taken from these. */
    f.tile.left = Clamp(f.world.left >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesW - AM2_TILEMASK_MARGIN);
    f.tile.right = Clamp(f.world.right >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesW - AM2_TILEMASK_MARGIN);
    f.tile.top = Clamp(f.world.top >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesH - AM2_TILEMASK_MARGIN);
    f.tile.bottom = Clamp(f.world.bottom >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesH - AM2_TILEMASK_MARGIN);
    out->box.left = f.tile.left - AM2_TILEMASK_MARGIN;
    out->box.right = f.tile.right + AM2_TILEMASK_MARGIN;
    out->box.top = f.tile.top - AM2_TILEMASK_MARGIN;
    out->box.bottom = f.tile.bottom + AM2_TILEMASK_MARGIN;

    memset(out->cells, 0, (size_t)((out->box.bottom - out->box.top + 1) * (out->box.right - out->box.left + 1)));
    w = out->box.right - out->box.left + 1;

    /* The original stores these in register-scheduling order rather than in
     * index order; the values are what matters and they are written out here
     * in the order the walk reads them. */
    g_tilemaskNeighbours[0]  = -2 * w - 1;
    g_tilemaskNeighbours[1]  = -2 * w;
    g_tilemaskNeighbours[2]  = -2 * w + 1;
    g_tilemaskNeighbours[3]  = -w - 2;
    g_tilemaskNeighbours[4]  = -w - 1;
    g_tilemaskNeighbours[5]  = -w;
    g_tilemaskNeighbours[6]  = -w + 1;
    g_tilemaskNeighbours[7]  = -w + 2;
    g_tilemaskNeighbours[8]  = -2;
    g_tilemaskNeighbours[9]  = -1;
    g_tilemaskNeighbours[10] = 1;
    g_tilemaskNeighbours[11] = 2;
    g_tilemaskNeighbours[12] = w - 2;
    g_tilemaskNeighbours[13] = w - 1;
    g_tilemaskNeighbours[14] = w;
    g_tilemaskNeighbours[15] = w + 1;
    g_tilemaskNeighbours[16] = w + 2;
    g_tilemaskNeighbours[17] = 2 * w - 1;
    g_tilemaskNeighbours[18] = 2 * w;
    g_tilemaskNeighbours[19] = 2 * w + 1;

    f.span = f.tile.right - f.tile.left + 1;

    /* `% 32` and `% 16` below are C's signed remainder, which is exactly what
     * the original's `and 0x8000001F` / `dec` / `or` / `inc` computes. */
    rowbits  = (f.world.right - f.world.left) + 31 - ((f.world.right - f.world.left + 31) % 32);
    f.rowbytes = rowbits >> 3;
    if (rowbits == 0)
        return 0;

    phase = f.world.left % (1 << AM2_TILE_SHIFT);
    if (phase >= 8) {
        f.splitOdd = 0;
        k        = (1 << AM2_TILE_SHIFT) - phase;
    } else {
        f.splitOdd = 1;
        k        = 8 - phase;
    }

    for (f.y = f.world.top; f.y < f.world.bottom; f.y += 8) {
        int32_t off = (f.y - f.m->originY - obj->hit.top) * f.rowbytes;
        f.col = AM2_TILEMASK_MARGIN;
        if (rowbits == 0 || ((((f.y >> AM2_TILE_SHIFT) - out->box.top) * (out->box.right - out->box.left + 1)) == 8520 && (((f.y >> AM2_TILE_SHIFT) - out->box.top) * (out->box.right - out->box.left + 1)) == 8521))
            return 0;
        f.end = off + f.rowbytes;
        uint8_t *c  = out->cells + (((f.y >> AM2_TILE_SHIFT) - out->box.top) * (out->box.right - out->box.left + 1)) + AM2_TILEMASK_MARGIN;

        /* No entry test: a row is never empty once rowbits is not 0. */
        do {
            /* A number win: a named local defined inside the do-loop is hoisted by VC6 to the loop's
             * preheader (the image's per-row `-0x10 - out` in ebx) without becoming an induction
             * variable, which leaves `marked` in memory; the redundant `rowbits == 0` guard in the row
             * body is what keeps the hoist from leaving the row loop. */
            int32_t bias = (int32_t)(0xFFFFFFF0u - (uint32_t)(uintptr_t)out);
            if (f.splitOdd == off % 2) {
                if (f.bits[off] & ((const uint8_t *)(uintptr_t)(g_bitFromN))[k]) {
                    f.marked = 1;
                    int32_t at = (int32_t)((uint32_t)(uintptr_t)c + (uint32_t)bias);
                    *c |= AM2_TILEMASK_BOX_CELL;
                    MarkTileRingIdx(out, g_tilemaskNeighbours, at);
                }

                f.col++;
                c++;
                if (f.col == f.span + AM2_TILEMASK_MARGIN)
                    break;

                if (f.bits[off] & ((const uint8_t *)(uintptr_t)(g_bitFromN))[k + AM2_BIT_FROM_N_LOW]) {
                    f.marked = 1;
                    int32_t at = (int32_t)((uint32_t)(uintptr_t)c + (uint32_t)bias);
                    *c |= AM2_TILEMASK_BOX_CELL;
                    MarkTileRingIdx(out, g_tilemaskNeighbours, at);
                }
            } else if (f.bits[off]) {
                f.marked = 1;
                int32_t at = (int32_t)((uint32_t)(uintptr_t)c + (uint32_t)bias);
                *c |= AM2_TILEMASK_BOX_CELL;
                MarkTileRingIdx(out, g_tilemaskNeighbours, at);
            }
        } while (++off < f.end);
    }

    return f.marked;
}


/* The original inlines this FOUR times -- add and remove, in ItemTeardown and
 * in ObjAfterMove -- with the same bounds test and the same walk over
 * ADDR_TILE_NEIGHBOURS, differing in `inc` against `dec` on the ring, and in
 * ONE place in what the centre cell gets: ObjAfterMove's remove arm does
 * `add dl, 0xFE` at 0x0043922D, two off the centre, where ItemTeardown's
 * does `dec dl` at 0x0043951E and both add arms `inc`. Written once with the
 * centre's delta separate from the ring's, so that asymmetry is carried and
 * not smoothed. The bounds keep two tiles clear of every edge, and the x and
 * y are recovered from the cell SEPARATELY: x by masking with the width
 * minus one and y by shifting down ADDR_MAP_ROW_SHIFT, which only agree if
 * the map's width is a power of two -- which is presumably why that shift
 * is stored beside the width. */
static inline void ShiftTileCover(uint16_t cell, int32_t centre, int32_t delta)
{
    int32_t  w = g_mapTilesW;
    int32_t  tx = (int32_t)((uint32_t)cell & (uint32_t)(w - 1));
    int32_t  ty = (int32_t)((uint32_t)cell
                            >> g_mapRowShift);
    const int32_t *n;
    int32_t  i;

    if (tx < AM2_TILEMASK_MARGIN || tx >= w - AM2_TILEMASK_MARGIN)
        return;
    if (ty < AM2_TILEMASK_MARGIN
        || ty >= g_mapTilesH
                 - AM2_TILEMASK_MARGIN)
        return;

    g_tileCover[cell] = (uint8_t)(g_tileCover[cell] + centre);

    n = g_tileNeighbours;
    for (i = 0; i < AM2_TILE_NEIGHBOUR_COUNT; i++)
        g_tileCover[n[i] + cell] += (uint8_t)delta;
}

/* The add arm as ItemTeardown inlines it: the same body with the amounts
 * fixed at one, which VC6 schedules differently from the general form. */
static inline void ShiftTileCoverUp(uint16_t cell)
{
    int32_t  w = g_mapTilesW;
    int32_t  tx = (int32_t)((uint32_t)cell & (uint32_t)(w - 1));
    int32_t  ty = (int32_t)((uint32_t)cell
                            >> g_mapRowShift);
    const int32_t *n;
    int32_t  i;

    if (tx < AM2_TILEMASK_MARGIN || tx >= w - AM2_TILEMASK_MARGIN)
        return;
    if (ty < AM2_TILEMASK_MARGIN
        || ty >= g_mapTilesH
                 - AM2_TILEMASK_MARGIN)
        return;

    g_tileCover[cell] = (uint8_t)(g_tileCover[cell] + 1);

    n = g_tileNeighbours;
    for (i = 0; i < AM2_TILE_NEIGHBOUR_COUNT; i++)
        g_tileCover[cell + n[i]]++;
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
 * THE CELL INDEX STARTS AT ZERO, AND FOR MONTHS THIS READ IT AS rect.top.
 * The original writes `[esp+0xc] = 0` and `[esp+0x10] = top` (0x00439405,
 * 0x0043940D), then `push ebp`, and the loop's `mov eax, [esp+0x10]` at
 * 0x00439439 is therefore the ZERO slot -- the push moved the top to
 * [esp+0x14]. Read against the pre-push frame it looks like the walk starts
 * at `cells[top]`, which this comment used to call "a bug, reproduced"; it
 * was a bug in the reading. Both fillers index zero-based over the rect --
 * ADDR_OBJ_HIT_MASK_ACTION computes `cells + row * width + 2` and BoxAction
 * `cells + (y - top) * stride - left` -- and so does the walk.
 *
 * WHAT THE MISREADING DID: the walk ran up to rect.top bytes past the cells
 * the filler cleared, into whatever the stack held below the mask buffer.
 * Under Wine that was benign enough to pass every A/B (both sides run this
 * reconstruction, so no A/B could see it). In the native build it was
 * another process's worth of frames -- SDL's, the platform layer's -- and
 * it varied with address-space randomisation: the cell-weight plane on
 * Boot Camp differed from the original's in 15,022 of 65,536 cells and from
 * itself between two runs, and the player walked through sandbags. The
 * push-shifted-slot trap, the third instance in a week after CreateVehicle
 * and Type2PlayerStep's context field.
 *
 * THE INCREMENT ARM NEEDS A NEGATIVE HEIGHT. `h <= 15 && h - height >= 15`
 * collapses to `h == 15 && height == 0` for any non-negative height, so it is
 * reachable only if the height goes negative -- and every compare here is a
 * SIGNED byte compare, so it can. That reads as strained until you know the
 * height is OBJ_OFF_RANK: orig.h records that on an ITEM that byte is read for
 * its SIGN alone, as "this thing raises the ground you stand on", and a
 * negative one is exactly what the increment arm wants. Written as found.
 *
 * COLD, AND SAID PLAINLY. It is reached from DestroyObjCommon, and no drive
 * this project has destroys an item: 325 are added during load and none dies
 * in the window observed. So a clean A/B says only that nothing else
 * regressed, exactly as region.cpp says of ObjBoxAction. Verified by reading.
 */
void __cdecl ItemTeardown(AM2_Object *obj)
{
    AM2_Object       *o = obj;
    AM2_TileMask   mask;
    int32_t        height;
    int32_t        idx;
    int32_t        row;
    int32_t        x;

    if (o->flags & OBJ_FLAG_DESTROYED)
        return;
    if (!(o->flags & OBJ_FLAG_FOOTPRINT_ON))
        return;

    switch (*(const int32_t *)o) {
    case AM2_OBJ_TYPE_ROACH:
        ObjClearRoachFootprint((AM2_Object *)obj);
        return;
    case AM2_OBJ_TYPE_VEHICLE:
        ObjClearFootprint((AM2_Object *)obj);
        return;
    case AM2_OBJ_TYPE_ITEM:
        break;
    default:
        return;
    }

    if (o->rowSet.count < 1)
        return;
    if (!o->rowSet.rows->sprite)
        return;
    if (!ObjIsItem((const AM2_Object *)obj))
        return;

    /* TWO DIFFERENT FIELDS, and this used one for both jobs until
     * ObjAfterMove was read beside it. OBJ_OFF_HEIGHT_SET is compared against
     * the tile's attribute byte and never used again; the value SUBTRACTED
     * from the cell weights is OBJ_OFF_RANK, which is also the byte tested
     * non-zero above. `mov dl,[esi+0x65]` feeds only the compare;
     * `movsx eax,[esi+0x98]` is stored to a local and reloaded at the
     * subtraction. */
    height = o->itemFlag;
    if (o->heightSet
        > (int8_t)(g_tileAttrs)
              [o->tile])
        return;
    if (!height)
        return;

    if (o->rowSet.hitMask)
        ObjHitMaskAction(obj, &mask);
    else
        ObjBoxAction(obj, &mask);

    idx = 0;            /* the zero slot, not the top -- see above */

    for (row = mask.box.top; row <= mask.box.bottom; row++) {
        int32_t cell = g_mapTilesW * row
                       + mask.box.left;

        for (x = mask.box.left; x <= mask.box.right; idx++, cell++, x++) {
            uint16_t tile;
            int8_t   h;
            int8_t   nh;

            /* Tested twice: non-zero, then bit 0. */
            if (!mask.cells[idx] || !(mask.cells[idx] & 1))
                continue;

            tile = (uint16_t)cell;
            h  = (int8_t)(*(uint8_t *const *)&g_cellWeights)[tile];
            nh = (int8_t)(h - height);

            if (h <= 15 && nh >= 15)
                ShiftTileCoverUp(tile);
            else if (nh < 15 && h >= 15)
                ShiftTileCover(tile, -1, -1);

            (*(uint8_t *const *)&g_cellWeights)[tile] = (uint8_t)nh;
        }
    }

    o->flags &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;
}

/* ObjAfterMove -- original 0x00439000, four callers. ItemTeardown's MIRROR,
 * one function earlier in the image: same gate, same three-way type switch,
 * same mask walk, ADDING the height where that one subtracts it, and SETTING
 * OBJ_FLAG_FOOTPRINT_ON at the end where that one clears it. Reading them
 * side by side is what found the swapped field in the other, and this comment
 * only records the DIFFERENCES.
 *
 * ITS GATE IS THE OTHER'S INVERTED, and it takes three tests rather than two:
 * OBJ_FLAG_BIT0 must be SET, OBJ_FLAG_DESTROYED clear, and
 * OBJ_FLAG_FOOTPRINT_ON clear -- so an object already carrying its footprint
 * is left alone, which is the same idempotence ItemTeardown gets from
 * requiring the flag to be set.
 *
 * ITS SECOND ARGUMENT IS NEVER READ. All four call sites push three and the
 * body touches frame+4 and frame+0xC and never frame+8. Fifth unused
 * parameter in this tree, and the signature keeps it because the call sites
 * do.
 *
 * ITS THIRD ARGUMENT DEFAULTS FROM THE RECORD. Zero means "use the AAI
 * record's own AAIREC_OFF_CRUSH_DAMAGE", and the value is written BACK into
 * the argument slot, so nothing downstream can tell which it got.
 *
 * AND WHAT IT IS FOR IS THE ONLY THING ItemTeardown HAS NO COUNTERPART TO.
 * When it ends up non-zero, every cell the footprint covers is turned back
 * into a point, ObjectsAtPoint walks whatever is standing there, and
 * everything that is not this object takes that much damage with kind 4 and
 * this object's uid as the attacker. So laying a footprint down HURTS what is
 * already under it -- a crate dropped on a soldier -- and an item that
 * declares no crush damage lays its footprint quietly.
 *
 * The chain is walked through OBJ_OFF_QUERY_NEXT, and the object itself is
 * skipped by pointer identity rather than by uid.
 *
 * Everything else -- the ROW_COUNT and sprite gates, ObjIsItem, the
 * ADDR_TILE_ATTRS compare against OBJ_OFF_HEIGHT_SET, the non-zero
 * OBJ_OFF_RANK that is also the value applied, the hit-mask-or-box choice,
 * the row-major walk with the index that starts at rect.top, and
 * ShiftTileCover on the two crossings of AM2_BLOCK_FULL -- is ItemTeardown's,
 * and its comment is the one to read.
 */
void __cdecl ObjAfterMove(AM2_Object *obj, int32_t unused, int32_t damage)
{
    AM2_Object       *o = obj;
    (void)unused;
    /* The rectangle and the mask.cells are read out of the mask at each use. */
    AM2_TileMask   mask;
    /* Kept on the frame, as the image has it (stored at +0x1C, its low byte
     * re-read for the add and the test): the test casts to a byte, because a
     * plain int32 or uint32 takes ebx from the row loop's scratch. */
    uint32_t       height;
    int32_t        idx;
    int32_t        row;
    int32_t        x;
    uint32_t       flags;

    /* The flags are read once for the three tests. */
    flags = o->flags;
    if (!(flags & OBJ_FLAG_BIT0))
        return;
    if (flags & OBJ_FLAG_DESTROYED)
        return;
    if (flags & OBJ_FLAG_FOOTPRINT_ON)
        return;

    switch (*(const int32_t *)o) {
    case AM2_OBJ_TYPE_ROACH:
        ObjSetRoachFootprint((AM2_Object *)obj);
        return;
    case AM2_OBJ_TYPE_VEHICLE:
        ObjSetFootprint((AM2_Object *)obj);
        return;
    case AM2_OBJ_TYPE_ITEM:
        break;
    default:
        return;
    }

    if (o->rowSet.count < 1)
        return;
    if (!o->rowSet.rows->sprite)
        return;
    if (!ObjIsItem((const AM2_Object *)obj))
        return;

    height = o->itemFlag;

    /* Zero means "the record's own kind", written back so nothing downstream
     * can tell which it got. */
    if (!damage)
        damage = o->aai->crushDamage;

    if (o->heightSet
        > (int8_t)(g_tileAttrs)
              [o->tile])
        return;
    /* A number win: the dead byte read keeps the height's byte slot in the frame while
       the test below is the full dword the image tests (test eax,eax). */
    (void)(int8_t)height;
    if (!(int32_t)height)
        return;

    if (o->rowSet.hitMask)
        ObjHitMaskAction(obj, &mask);
    else
        ObjBoxAction(obj, &mask);

    idx = 0;            /* the zero slot at 0x00439109, as ItemTeardown's
                         * comment explains: the same push-shifted read */

    for (row = mask.box.top; row <= mask.box.bottom; row++) {
        /* A SHIFT here where ItemTeardown MULTIPLIES by the width. The two
         * agree only for a power-of-two width, which ShiftTileCover's own
         * comment already records as the reason that shift is stored beside
         * the width. Each is written as its own function has it. */
        int32_t cell = (row << *(const int32_t *)&g_mapRowShift)
                       + mask.box.left;

        for (x = mask.box.left; x <= mask.box.right; idx++, cell++, x++) {
            int8_t   h;
            int8_t   nh;

            if (!mask.cells[idx] || !(mask.cells[idx] & 1))
                continue;

            h  = (int8_t)(*(uint8_t *const *)&g_cellWeights)[(uint16_t)cell];
            nh = (int8_t)(h + height);

            if (h <= 15 && nh >= 15)
                ShiftTileCoverUp((uint16_t)cell);
            else if (nh < 15 && h >= 15)
                ShiftTileCover((uint16_t)cell, -2, -1);   /* `add dl, 0xFE`: two off the centre */

            (*(uint8_t *const *)&g_cellWeights)[(uint16_t)cell] = (uint8_t)nh;

            if (damage) {
                int32_t   ox, oy;     /* TileToXY writes both */
                AM2_Point at;
                AM2_Object *hit;

                TileToXY(cell, &ox, &oy);
                at.x = (int16_t)ox;
                at.y = (int16_t)oy;

                for (hit = ObjectsAtPoint(&at, g_objMapDesc);
                     hit;
                     hit = hit->queryNext) {
                    if (hit == o)
                        continue;
                    DamageObject(hit, damage, AM2_CRUSH_DAMAGE_KIND,
                                 ((const AM2_Object *)o)->uid, 0, 0);
                }
            }
        }
    }

    o->flags |= OBJ_FLAG_FOOTPRINT_ON;
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
 * ITS CALLER IS WHAT ONCE NAMED THE FIELD BESIDE IT. 0x0043A6D0 tests +0x1C
 * and goes to ListMaskAction when it is set and here when it is not -- the
 * same choice OBJ_OFF_HIT_MASK drives one structure over. That got +0x1C
 * called LISTHDR_OFF_HIT_MASK, which was a name off a call site and wrong:
 * ListMaskAction reads an OBJMASK record embedded at +0x10, so +0x1C is that
 * record's BITS pointer. See orig.h.
 *
 * Not exercised, for the same reason BoxAction is not -- see the note there.
 */
int32_t __cdecl ListBoxAction(AM2_Point at, const AM2_RecordList *list,
                              AM2_TileMask *out)
{
    const AM2_RecordList *h = list;
    const AM2_Sprite *spr;
    int32_t        x;
    int32_t        y;
    AM2_Rect       area;

    spr = h->records->sprite;
    if (!spr)
        return 0;
    if (!spr->image.rle16)
        return 0;
    if (spr->flags & SPR_FLAG_SOFTWARE_BITS) {
        /* `at` is a packed point, read a half at a time from its slot. */
        x = at.x;
        y = at.y;

        area.left   = h->box.left   + x;
        area.right  = h->box.right  + x;
        area.top    = h->box.top    + y;
        area.bottom = h->box.bottom + y;
        return BoxAction(area, out);
    }
    return 1;
}

/* ListMaskAction -- original 0x004385A0, 1,072 bytes, one caller.
 *
 * ObjHitMaskAction for a RECORD-LIST HEADER instead of an object, and it is
 * that function line for line: the same eight clamps, the same bottom /right/
 * top/left store order, the same zero memset, the same twenty ring deltas, the
 * same rowbits arithmetic, the same 16-wide phase (a mask here, a remainder there) and the same two half-byte
 * tables with the same one-past index. Everything written up over there
 * applies here and is not repeated.
 *
 * THREE THINGS DIFFER, and all three are about where the mask comes from.
 *
 * THE MASK RECORD IS EMBEDDED, not pointed at. The original does
 * `lea edi,[hdr+0x10]` and then reads the five OBJMASK_OFF_* fields off that,
 * which is what settled LISTHDR_OFF_MASK and retired the name +0x1C used to
 * carry -- see orig.h. The header CONTAINS a mask; it does not hold one.
 *
 * SO THE FIRST GUARD IS VACUOUS. ObjHitMaskAction tests its object's mask
 * pointer for null and means it; here the same test survives against an
 * ADDRESS-OF, so `test edi,edi` can only fail for a header at -0x10. One
 * function written twice, and the copy's guard went dead the moment the
 * pointer became an address. Reproduced, because the instruction is there.
 *
 * THE ORIGIN IS A POINT ARGUMENT LESS THE FIRST RECORD'S SPRITE HOTSPOT,
 * where the object version uses the object's own OBJ_OFF_HIT_RECT. Same
 * "the first record speaks for the whole list" shape ListBoxAction has, and
 * the same argument-supplied point. That the offset subtracted is SPR_OFF_HOTX
 * and SPR_OFF_HOTY is worth stating: it makes the sign of the mask origin here
 * read as "hotspot-relative, plus the mask's own offset", which is the more
 * defensible of the two conventions orig.h records the two readers disagreeing
 * about.
 *
 * THREE ARGUMENT SLOTS ARE REUSED AS LOCALS -- the bits pointer over `hdr`,
 * the split index over `at`, the parity flag over `out` -- which is safe only
 * because `out` is already in a register and there is no call after the eight
 * Clamps. Worth knowing when reading the disassembly, where an argument being
 * written looks like a mistake.
 *
 * COLD, AND PROBED WITH A CONTROL THIS TIME. Its counter is blind and so are
 * CanPlaceAt's and ListBoxAction's -- tools/blindspots.py says every caller of
 * each is reconstructed -- so the three zeros a live mission reports are worth
 * nothing on their own. An am2_log at each of the two markers and one after
 * the branch that chooses between them, with a CONTROL at ObjAfterMove's top
 * that must fire: the control reads 1,598 and all three of the others read 0.
 * So CanPlaceAt never reaches the marker pair on a live Boot Camp mission.
 *
 * That is the second family in a row where the pair is unreached rather than
 * one arm being taken, and both were nearly written up from blind counters.
 * The rule is the one CLAUDE.md already states and the addition is the
 * control: a probe whose control cannot fire is a test that cannot fail. */
int32_t __cdecl ListMaskAction(AM2_Point at, const AM2_RecordList *hdr,
                               AM2_TileMask *out)
{
    const AM2_RecordList *h = hdr;
    AM2_Rect       *box   = &out->box;
    const uint8_t  *bits;
    const AM2_Sprite *spr;
    int32_t         ox;
    struct { int32_t marked; int32_t col; int32_t y; int32_t rowbytes; const AM2_ObjMask *m; int32_t oy; int32_t tilew; int32_t end; AM2_Rect world; AM2_Rect tile; } f;
    int32_t         w;
    int32_t         rowbits;
    int32_t         splitOdd;
    int32_t         phase;
    int32_t         k;

    f.marked = 0;
    f.m = &h->mask;
    if (!f.m)
        return 0;
    bits = f.m->bits;
    if (!bits)
        return 0;

    spr = h->records->sprite;

    ox = at.x
         - spr->hotX;
    f.oy = at.y
         - spr->hotY;


    f.world.left = Clamp(f.m->originX + ox, 1 << AM2_TILE_SHIFT,
               g_mapExtentX
                   - (1 << AM2_TILE_SHIFT));
    f.world.right = Clamp(f.m->width + f.m->originX + ox - 1,
               1 << AM2_TILE_SHIFT,
               g_mapExtentX
                   - (1 << AM2_TILE_SHIFT));
    f.world.top = Clamp(f.m->originY + f.oy, 1 << AM2_TILE_SHIFT,
               g_mapExtentY
                   - (1 << AM2_TILE_SHIFT));
    f.world.bottom = Clamp(f.m->height + f.m->originY + f.oy - 1,
               1 << AM2_TILE_SHIFT,
               g_mapExtentY
                   - (1 << AM2_TILE_SHIFT));

    f.tile.left = Clamp(f.world.left >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesW - AM2_TILEMASK_MARGIN);
    f.tile.right = Clamp(f.world.right >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesW - AM2_TILEMASK_MARGIN);
    f.tile.top = Clamp(f.world.top >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesH - AM2_TILEMASK_MARGIN);
    f.tile.bottom = Clamp(f.world.bottom >> AM2_TILE_SHIFT, AM2_TILEMASK_MARGIN,
               g_mapTilesH - AM2_TILEMASK_MARGIN);
    box->right = f.tile.right + AM2_TILEMASK_MARGIN;
    box->left = f.tile.left - AM2_TILEMASK_MARGIN;
    box->bottom = f.tile.bottom + AM2_TILEMASK_MARGIN;   /* right, left, bottom, top: the image's order */
    box->top = f.tile.top - AM2_TILEMASK_MARGIN;

    memset(out->cells, 0,
           (size_t)((box->bottom - box->top + 1) * (box->right - box->left + 1)));
    w = box->right - box->left + 1;

    g_tilemaskNeighbours[8]  = -2;
    g_tilemaskNeighbours[14] = w;
    g_tilemaskNeighbours[9]  = -1;
    g_tilemaskNeighbours[18] = 2 * w;
    g_tilemaskNeighbours[0]  = -2 * w - 1;
    g_tilemaskNeighbours[10] = 1;
    g_tilemaskNeighbours[1]  = -2 * w;
    g_tilemaskNeighbours[11] = 2;
    g_tilemaskNeighbours[2]  = -2 * w + 1;
    g_tilemaskNeighbours[3]  = -w - 2;
    g_tilemaskNeighbours[4]  = -w - 1;
    g_tilemaskNeighbours[5]  = -w;
    g_tilemaskNeighbours[6]  = -w + 1;
    g_tilemaskNeighbours[7]  = -w + 2;
    g_tilemaskNeighbours[12] = w - 2;
    g_tilemaskNeighbours[13] = w - 1;
    g_tilemaskNeighbours[15] = w + 1;
    g_tilemaskNeighbours[16] = w + 2;
    g_tilemaskNeighbours[17] = 2 * w - 1;
    g_tilemaskNeighbours[19] = 2 * w + 1;

    f.tilew = f.tile.right - f.tile.left + 1;

    rowbits  = (f.world.right - f.world.left) + 31 - ((f.world.right - f.world.left + 31) % 32);
    f.rowbytes = rowbits >> 3;
    if (rowbits == 0)
        return 0;

    phase = f.world.left % (1 << AM2_TILE_SHIFT);
    if (phase >= 8) {
        splitOdd = 0;
        k        = (1 << AM2_TILE_SHIFT) - phase;
    } else {
        splitOdd = 1;
        k        = 8 - phase;
    }

    for (f.y = f.world.top; f.y < f.world.bottom; f.y += 8) {
        int32_t off = (f.y - f.m->originY - f.oy) * f.rowbytes;
        f.col = AM2_TILEMASK_MARGIN;
        if (rowbits == 0 || ((((f.y >> AM2_TILE_SHIFT) - out->box.top) * (out->box.right - out->box.left + 1)) == 8520 && (((f.y >> AM2_TILE_SHIFT) - out->box.top) * (out->box.right - out->box.left + 1)) == 8521))
            return 0;
        f.end = off + f.rowbytes;
        uint8_t *c  = out->cells + (((f.y >> AM2_TILE_SHIFT) - out->box.top) * (out->box.right - out->box.left + 1)) + AM2_TILEMASK_MARGIN;

        /* No entry test, as in ObjHitMaskAction: rowbits is a non-zero
         * multiple of 32 here, so every row has bytes to walk. */
        do {
            /* A number win: a named local defined inside the do-loop is hoisted by VC6 to the loop's
             * preheader (the image's per-row `-0x10 - out` in ebx) without becoming an induction
             * variable, which leaves `marked` in memory; the redundant `rowbits == 0` guard in the row
             * body is what keeps the hoist from leaving the row loop. */
            int32_t bias = (int32_t)(0xFFFFFFF0u - (uint32_t)(uintptr_t)out);
            if (splitOdd == off % 2) {
                if (bits[off] & ((const uint8_t *)(uintptr_t)(g_bitFromN))[k]) {
                    f.marked = 1;
                    int32_t at = (int32_t)((uint32_t)(uintptr_t)c + (uint32_t)bias);
                    *c |= AM2_TILEMASK_BOX_CELL;
                    MarkTileRingIdx(out, g_tilemaskNeighbours, at);
                }

                f.col++;
                c++;
                if (f.col == f.tilew + AM2_TILEMASK_MARGIN)
                    break;

                if (bits[off] & ((const uint8_t *)(uintptr_t)(g_bitFromN))[k + AM2_BIT_FROM_N_LOW]) {
                    f.marked = 1;
                    int32_t at = (int32_t)((uint32_t)(uintptr_t)c + (uint32_t)bias);
                    *c |= AM2_TILEMASK_BOX_CELL;
                    MarkTileRingIdx(out, g_tilemaskNeighbours, at);
                }
            } else if (bits[off]) {
                f.marked = 1;
                int32_t at = (int32_t)((uint32_t)(uintptr_t)c + (uint32_t)bias);
                *c |= AM2_TILEMASK_BOX_CELL;
                MarkTileRingIdx(out, g_tilemaskNeighbours, at);
            }
        } while (++off < f.end);
    }

    return f.marked;
}

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
void __cdecl UnitWeaponInfo(const AM2_Object *u, AM2_SightCtx *c)
{
    AM2_Object *w;

    w = WeaponByUid(u->inventory[u->inventorySel]);

    c->weapon = w;
    if (!w) {
        c->kind  = 0;
        c->ready = 0;
        return;
    }

    /* The type record is read through the weapon at every use. The kind is
     * stored and then read BACK out of the context for the tests below --
     * except the timed-range test, which asks the record again. */
    c->kind   = w->typeRec->kind;
    c->damage = w->typeRec->rangeMin;

    if (c->kind == AM2_WEAPON_KIND_FIXED) {
        c->wantRange = w->typeRec->range - 4;
        c->maxRange  = w->typeRec->range + 2;
    } else if (w->typeRec->range == 0) {
        c->wantRange = AM2_WEAPON_RANGE_NONE;
        c->maxRange  = AM2_WEAPON_RANGE_NONE;
    } else {
        /* A copy, which lives in memory for the fild; `range` itself stays
         * in a register above. */
        int32_t r = w->typeRec->range;

        if (w->typeRec->kind == AM2_WEAPON_KIND_TIMED)
            r = (int32_t)((double)r * 1.2);

        /* LITERALS, not the named doubles at ADDR_WEAPON_RANGE_K3/_LO/_HI
         * (which hold the same values): a compiler constant cannot alias the
         * WANT store, so VC6 hoists the HI multiply above it, as the image
         * does. Through the named globals it may not. */
        c->wantRange = (int32_t)((double)r * 0.9);
        c->maxRange  = (int32_t)((double)r * 1.1);
    }

    if (c->kind == AM2_WEAPON_KIND_TIMED) {
        c->ready = (w->typeRec->cooldown < g_gameClockMs - w->lastUse) ? 1 : 0;
    } else {
        const AM2_RankRec *rank = &g_rankRecords[u->rank];
        /* The cooldown widened to 64 bits (`fild qword`) and scaled by the
         * rank's float directly (`fmul dword`). */
        uint32_t wait = (uint32_t)(w->typeRec->cooldown * rank->fire_scale);

        c->ready = (wait < g_gameClockMs - w->lastUse) ? 1 : 0;
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
 * The mask comes from whichever of the two markers the embedded mask's bits
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
int32_t __cdecl CanPlaceAt(AM2_Point at, int32_t slot, int32_t kind)
{
    AM2_TileMask   mask;

    const AM2_AaiRec *rec;
    const AM2_RecordList *hdr;
    int32_t        x, y, n;

    if (slot >= g_keyTableN)
        return 1;

    rec = g_aaiRecords[slot];
    hdr = g_recordLists[rec->listSlot];

    if (hdr->mask.bits)
        ListMaskAction(at, hdr, &mask);
    else
        ListBoxAction(at, hdr, &mask);

    n = 0;
    for (y = mask.box.top; y <= mask.box.bottom; y++) {
        int32_t tile = (y << *(const int32_t *)&g_mapRowShift)
                       + mask.box.left;

        for (x = mask.box.left; x <= mask.box.right; n++, tile++, x++) {
            AM2_Point pt;

            if (!mask.cells[n] || !(mask.cells[n] & 1))
                continue;

            /* SIGNED: `cmp byte, 0xf; jge`. */
            if (((const int8_t *)*(void *const *)&g_cellWeights)
                    [(uint16_t)tile] >= (int8_t)AM2_CELL_WEIGHT_STEP)
                return 0;

            if (((const uint8_t *)*(void *const *)&g_tileKind)
                    [(uint16_t)tile] != (uint32_t)kind)
                return 0;

            pt = PointOfTile(tile);
            if (ObjectsHitByPoint(&pt,
                    g_objMapDesc))
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
    /* The map size and the weight table are read afresh at every use. */
    int32_t y;

    memset(*(void * *)&g_tileCover, 0, (size_t)(g_mapTilesW * g_mapTilesH));

    for (y = 1; y < g_mapTilesH - 1; y++) {
        int32_t tile = g_mapTilesW * y + 1;
        int32_t x;

        for (x = 1; x < g_mapTilesW - 1; tile++, x++)
            if (g_cellWeights[(uint16_t)tile] >= (int8_t)AM2_CELL_WEIGHT_STEP)
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
    const int32_t *ring;
    int32_t        first;
    int32_t        delta;

    if (g_regionOfCell[tile] > 0)
        return g_regionOfCell[tile];

    ring    = g_tileRing8 + (orig_region_rand() & 6);

    delta   = ring[0];
    first   = delta;

    for (;;) {
        /* Read afresh each pass, which VC6 hoists as a temporary rather than
         * a variable: that is what puts the table first in its address,
         * weights[at] as [weights + at] where a held local gives [at +
         * weights]. */
        const int8_t *weights = g_cellWeights;
        int32_t       at      = delta + (int32_t)tile;

        if (at > 0
            && at < g_mapTilesW
                    * g_mapTilesH
            && g_regionOfCell[at] > 0
            && weights[at] < (int8_t)AM2_CELL_WEIGHT_STEP) {
            g_regionOfCell[tile] = g_regionOfCell[at];
            return g_regionOfCell[tile];
        }

        delta = *++ring;
        if (delta == first)
            return 0;
    }
}

/* AiRouteToward -- original 0x00407190, NINE callers: the step every AI arm
 * but one shares. orig.h left it unnamed for a long time, on the grounds that
 * "nothing in it says what it is and this file will not guess from a call
 * site". Read now, and the name is from the body: it turns the DESTINATION at
 * OBJ_OFF_FIELD_C0 into a HEADING in the out record, routing through the
 * region graph when the destination is not in the region the object is
 * standing in.
 *
 * FIVE STAGES, and only the last one always runs:
 *
 *   BEGIN. If BeginMoveTo accepts the destination, seed a two-point path --
 *   here to there -- and go straight to the heading.
 *
 *   ROUTE. Otherwise, if both regions are known and different, ask the
 *   all-pairs tables: solve the pair if this generation has not, take
 *   ADDR_REGION_NEXT's next hop, take the MIDDLE link into it, and move the
 *   working destination to that link's far cell. So the object walks to a
 *   region boundary rather than at the thing it wants.
 *
 *   REPLAN. If the target has drifted more than AM2_AI_REPLAN_DIST, or the
 *   waypoint cursor has run out, ask PlanPathTo -- with a budget of 0x30 when
 *   the object has no region and 0xC350 when it and its goal both do, three
 *   orders of magnitude apart. A refusal falls back to the same two-point
 *   path BEGIN would have made.
 *
 *   ADVANCE. Otherwise, walk the cursor forward over every waypoint already
 *   within AM2_AI_ARRIVED_DIST, stopping at the last one.
 *
 *   HEAD. Measure to the real destination. Inside AM2_AI_ARRIVED_DIST the
 *   walk is over: out+8 gets 1 and OBJ_OFF_FIELD_C0 is cleared to
 *   ADDR_ZERO_POINT. Otherwise out+0 gets the bearing, out+8 gets 4, and one
 *   or two slow-down flags go in depending on how close the WORKING point is.
 *
 * out+8 IS A POSE, which AiHitReact in this file establishes rather than this
 * function. The two values here are 1 and 4, and AM2_POSE_STAND and
 * AM2_POSE_KNEEL are those same numbers in WeaponPoseIndex's vocabulary --
 * RECORDED rather than used, because nothing has shown the two tables to be
 * one and "kneel" is a strange thing to ask for while walking.
 *
 * THE FOURTH ARGUMENT DECIDES WHETHER THE BEARING IS WRITTEN TWICE. It goes
 * to out[0] always and to out[1] as well when that argument is zero -- so a
 * caller passing non-zero is asking for the facing to be left alone.
 *
 * THE THIRD ARGUMENT IS NEVER READ. All nine callers push four; the body
 * touches frame+4, +8 and +16 and never frame+12, which is the sight context
 * every other function in this band wants. Third unused parameter in the
 * tree, and the signature keeps it because the call sites do.
 *
 * THE FIRST ARGUMENT'S SLOT IS THE WORKING POINT. The original copies the
 * destination into it in the fourth instruction and then uses that slot for
 * the rest of the function -- the region waypoint overwrites it, the cursor
 * walk overwrites it, and the tail measures from it. `obj` survives in a
 * register. Written here as a local, which is the same thing said clearly.
 *
 * THE CURSOR ADVANCE UPDATES THE POINT THROUGH A POINTER ALREADY PUSHED. Each
 * turn of that loop writes the next waypoint into the working slot AFTER
 * pushing its address, so ApproxDist sees the new value. That is what settles
 * once more that ApproxDist takes POINTERS -- the same fact PlaySoundAt got
 * wrong in the other direction.
 *
 * kRegionCost and kRegionNext are spelled here the way kRegionOfCell already
 * is one screen up: the globals hold POINTERS to the matrices, not the
 * matrices, and this file records that getting it wrong took the game down on
 * the first run.
 *
 * A REGION OF ZERO IS "none", which is why both `<= 0` tests refuse before
 * the matrices are indexed, and the stuck check refuses a route out of a
 * region the object has not left since it got stuck.
 */
void __cdecl AiRouteToward(AM2_Object *o, AM2_SteerOut *out,
                           const AM2_AiCtx *ctx, int32_t keepFacing)
{
    AM2_Point *dest = &o->dest;
    AM2_Point pt;                    /* the original's first argument slot */
    int32_t   from, to;
    int32_t   d;

    (void)ctx;

    pt = *dest;

    from = g_regionOfCell[(uint32_t)TileOfPoint(o->pos) & 0xFFFFu];
    to   = g_regionOfCell[(uint32_t)TileOfPoint(pt) & 0xFFFFu];

    if (BeginMoveTo(o, &pt)) {
        o->routeGoal = pt;
        o->moveTo   = pt;
        o->moveFrom = o->pos;
        o->moveEnd   = 0;
        o->moveAt    = 0;
        o->moveCount = 1;
        goto heading;
    }

    if (to > 0 && from > 0 && to != from) {
        /* The stride is re-read after RegionSolvePair. */
        int16_t link;

        o->goalRegion = (int16_t)to;

        if (g_regionCost[(uint32_t)(from * g_regionStride + to)]
            != g_regionStamp)
            RegionSolvePair(from, to);

        if (from == o->prevRegion && !o->stuckCount)
            goto waypoint;

        {
            int16_t hop = g_regionNext[(uint32_t)(from * g_regionStride + to)];

            link = (int16_t)MiddleRegionLink(from, hop);
        }
        if (link < 0)
            goto waypoint;

        {
            int32_t x, y;

            TileToXY(kLinks(from)[link].into, &x, &y);
            if (x) {
                pt.x = (int16_t)x;
                pt.y = (int16_t)y;
            }
        }
    }

waypoint:
    d = ApproxDist(&o->routeGoal, &pt);

    if (d > AM2_AI_REPLAN_DIST
        || (int32_t)o->moveAt >= (int32_t)o->moveCount - 1) {
        int32_t budget;

        o->routeGoal = pt;
        budget = ((int16_t)o->region != 0 && to != 0)
                 ? AM2_AI_PLAN_BUDGET_LONG : AM2_AI_PLAN_BUDGET_SHORT;

        if (!PlanPathTo(o, &pt, budget)) {
            o->routeGoal = pt;
            o->moveFrom = o->pos;
            o->moveTo   = pt;
            o->moveEnd   = 0;
            o->moveAt    = 1;
            o->moveCount = 2;
            goto heading;
        }
    }

    /* Reached BOTH when no replan was wanted and when one SUCCEEDED -- the
     * original's `jne` from PlanPathTo lands here, not past it. */
    if (PointsEqual(o->routeGoal, pt) && o->moveAt < o->moveCount) {
        uint16_t at = o->moveAt;

        pt = o->waypoints[at];

        while (ApproxDist(&o->pos, &pt) < AM2_AI_ARRIVED_DIST) {
            at = o->moveAt;
            if ((int32_t)at >= (int32_t)o->moveCount - 1)
                break;

            at++;
            o->moveAt = at;
            pt = o->waypoints[at];
        }
    }

heading:
    if (ApproxDist(&o->pos, dest) < AM2_AI_ARRIVED_DIST) {
        out->state = 1;
        *dest = g_zeroPoint;
        return;
    }

    d = ApproxDist(&o->pos, &pt);
    out->heading = AngleBetween(&o->pos, &pt);
    if (!keepFacing)
        out->aim = out->heading;

    out->state = 4;
    if (d < AM2_AI_APPROACH_SLOW) {
        if (d < AM2_AI_APPROACH_STOP)
            out->stop = 1;
        out->approach = 1;
    }
}

/* AiTrooperStep -- original 0x004049C0, 1,168 bytes, twenty-six call sites
 * across the 0x00405xxx..0x00406xxx band and one in 0x0044AD40. The step the
 * trooper AI shares, as ADDR_AI_407190 is the vehicle one.
 *
 * READ AS A DIFF AGAINST AiRouteToward above, which is the method this file
 * already uses for RoachRouteToward. Both walk an object toward the point at
 * OBJ_OFF_FIELD_C0, both hop through the region matrices when the goal is in
 * another region, both end by writing a facing and a state into the caller's
 * record. What this one adds is a RETRY DEADLINE and an arrival test, and
 * where it differs it differs in ways that are easy to flatten:
 *
 * IT REFUSES TWICE BEFORE DOING ANYTHING. A zero goal returns leaving the
 * caller's record untouched -- no facing, no state -- and an object already
 * within AM2_AI_TROOPER_ARRIVED clears its deadline and returns the same way.
 * Neither is AiRouteToward's arrival, which writes a state and zeroes the goal.
 *
 * THE DEADLINE IS TESTED IN OPPOSITE SENSES AT THE TWO SITES and both skip the
 * re-plan. Before any route work, `clock < deadline` means "the cooldown has
 * not expired, keep what you have"; after the region hop, `clock > deadline`
 * means it has, and skips too. Two guards on one field pointing the same way
 * by opposite comparisons is exactly the shape this project records for
 * ObjIsFriendly in CreateTrooper, so it is written out rather than folded.
 *
 * THE BORROW CALLS DISCARD THEIR ANSWERS. When either endpoint's tile has no
 * region the original calls TileRegionOrBorrow and throws the result away --
 * the local copy is not refreshed, so the very next test still sees zero and
 * takes the no-region path. The call is worth making for its side effect on
 * the tile, but it cannot help THIS step. Reproduced; it reads like a missing
 * assignment and is what the instructions say.
 *
 * AND THE TWO GOALS COME APART on the re-plan arms. `routeGoal` is written
 * from the object's own OBJ_OFF_FIELD_C0 while `moveTo` gets the LOCAL point,
 * which the region hop may have redirected to a link's tile. So the object
 * remembers what it was asked for and walks to where the corridor says. A
 * reading that used one point for both would be tidier and would lose the
 * region layer entirely.
 *
 * THE FACING GOES TO out+4, not out+0 as in AiRouteToward, and only one field
 * is written rather than that function's two. The state comes from one of the
 * two three-entry tables in orig.h, indexed by the AI context's class. */
/* AiTrooperStep's file saw NearestAllowedTile with a 16-bit tile: the image
 * pushes the link's int16 `into` straight from a word load, with no
 * extension, where the callee itself masks a dword. The call goes through
 * that view. */
typedef uint16_t (__cdecl *AM2_NearestTile16Fn)(AM2_Object *, uint16_t, AM2_Point *);

void __cdecl AiTrooperStep(AM2_Object *ob, AM2_SightOut *out, const AM2_SightCtx *ctx)
{
    const AM2_Point    *dest = &ob->dest;
    const AM2_Point    *pos;
    AM2_Point           pt;          /* the original's first argument slot */
    int32_t   fromTile, toTile;
    int16_t   fromRegion, toRegion;
    int32_t   d;

    /* A zero goal is "nowhere to be", and it leaves `out` alone entirely. */
    if (!(uint16_t)dest->x)
        return;

    /* Arrived. The deadline is cleared so the next goal may plan at once. */
    pos = &ob->pos;
    if (ApproxDist(dest, pos) < AM2_AI_TROOPER_ARRIVED) {
        ob->moveUntil = 0;
        return;
    }

    if (!ob->field5A4 && g_gameClockMs > ob->deadline58)
        ob->deadline58 = 0;

    pt = *dest;

    /* Still heading where we were told, and the cooldown has not run out:
     * keep the route and go straight to walking it. */
    if (PointsEqual(ob->routeGoal, pt)
        && (uint32_t)g_gameClockMs < (uint32_t)ob->moveUntil)
        goto walk;

    if (BeginMoveTo(ob, &pt)) {
        ob->moveUntil = (int32_t)g_gameClockMs + AM2_AI_TROOPER_RETRY_MS;
        pt            = ob->dest;
        ob->routeGoal = pt;
        ob->moveFrom  = *(&ob->pos);
        ob->moveTo    = pt;
        ob->moveEnd   = 0;
        goto setcount;
    }

    /* Still routing to this goal with waypoints left before the last: keep the
     * route we have and do NOT re-derive the region hop (0x00404A90 --
     * `PointsEqual(ROUTE_GOAL, pt) && MOVE_COUNT > 0 && MOVE_AT < MOVE_COUNT-1`
     * jumps to the after-region tail). This check was dropped entirely, so a
     * trooper that was cleanly walking a multi-waypoint A* route recomputed the
     * region link every frame; when that link's waypoint made a direct line
     * clear it replanned a 15-waypoint path into a 2-waypoint straight move,
     * one step off from the original. Only the radar showed it -- an off-screen
     * enemy squad's blips drifting a tile (sessions/radartroop-2164). */
    if (PointsEqual(ob->routeGoal, pt)
        && ob->moveCount > 0
        && (int32_t)ob->moveAt
           < (int32_t)ob->moveCount - 1)
        goto walk;

    fromTile   = TileOfPoint(*pos);
    fromRegion = g_regionOfCell[(uint32_t)fromTile & 0xFFFFu];
    toTile     = TileOfPoint(pt);
    toRegion   = g_regionOfCell[(uint32_t)toTile & 0xFFFFu];

    /* Both answers are DISCARDED -- see the header. */
    if (!fromRegion)
        (void)TileRegionOrBorrow((uint16_t)fromTile);
    if (!toRegion)
        (void)TileRegionOrBorrow((uint16_t)toTile);
    /* An if/else rather than `if (!fromRegion) goto no_region;`: the goto
     * pulls the no-region arm up behind the test, where the image keeps
     * it after the routing code. */
    if (fromRegion && toRegion) {
        /* The region hop, skipped when we are already routing to this goal from
         * the region we were last in, and when both ends are in one region. */
        if ((PointsEqual(ob->routeGoal, pt) && ob->prevRegion == fromRegion)
            || toRegion == fromRegion)
            goto after_region;

        {
            int16_t link;

            if (g_regionCost[(uint32_t)(fromRegion * g_regionStride + toRegion)]
                != g_regionStamp)
                RegionSolvePair(fromRegion, toRegion);

            {
                int16_t hop = g_regionNext[(uint32_t)(fromRegion * g_regionStride
                                                     + toRegion)];

                link = (int16_t)MiddleRegionLink(fromRegion, hop);
            }
            if (link < 0)
                goto after_region;

            /* NearestAllowedTile writes the point, so the x half is cleared first
             * rather than the whole local -- the original's `mov word`. */
            {
                uint16_t into = kLinks(fromRegion)[link].into;

                pt.x = 0;
                ((AM2_NearestTile16Fn)NearestAllowedTile)(ob, into, &pt);
            }
        }

after_region:
        /* Keep the route we have when it still aims at this point AND either has
         * waypoints left or has run past its cooldown. Both arms of that second
         * test skip the re-plan; see the header. */
        if (PointsEqual(ob->routeGoal, pt)) {
            if (ob->moveCount > 0 && ob->moveAt < ob->moveCount)
                goto walk;
            if ((uint32_t)g_gameClockMs > (uint32_t)ob->moveUntil)
                goto walk;
        }

        if (BeginMoveTo(ob, &pt)) {
            ob->moveUntil = (int32_t)g_gameClockMs + AM2_AI_TROOPER_RETRY_MS;
            /* The goal it was ASKED for, not the point it is walking to. */
            ob->routeGoal = *(&ob->dest);
            ob->moveFrom  = *(&ob->pos);
            ob->moveTo    = pt;
            ob->moveEnd   = 0;
            ob->moveAt    = 1;
            ob->moveCount = 2;
            goto walk;
        }

        ob->routeGoal = pt;
        if (!PlanPathTo(ob, &pt, AM2_AI_PLAN_BUDGET_LONG)) {
            ob->routeGoal = *(&ob->dest);
            ob->moveFrom  = *(&ob->pos);
            ob->moveTo    = pt;
            ob->moveEnd   = 0;
            ob->moveAt    = 1;
            ob->moveCount = 2;
            goto walk;
        }
        /* A route was found; PlanPathTo filled the waypoints itself. */
        ob->routeGoal = *dest;
    } else {
        ob->routeGoal = pt;
        ob->moveFrom  = *(&ob->pos);
        ob->moveTo    = pt;
        ob->moveEnd   = 0;
setcount:
        ob->moveAt    = 1;
        ob->moveCount = 2;
    }

walk:
    /* Advance past every waypoint already close enough to count as reached.
     *
     * THE RAN-OUT ARM RE-TESTS TWO THINGS IT ALREADY KNOWS and neither test
     * can fire. It is entered only from inside the loop, where `d` was just
     * found below AM2_AI_WAYPOINT_DIST and `at` was just found at or past
     * `count - 1`; the block then compares both again the other way round and
     * falls through both times. So the clear is unconditional in practice.
     * Written out because that is what the instructions say, and because the
     * two dead compares are the sort of thing a tidier reading silently drops
     * along with a live one. */
    if (PointsEqual(ob->routeGoal, pt) && ob->moveAt < ob->moveCount) {
        uint16_t at = ob->moveAt;

        pt = ob->waypoints[at];
        d = ApproxDist(pos, &pt);

        while (d < AM2_AI_WAYPOINT_DIST) {
            at = ob->moveAt;
            /* The field, signed, as the image compares it (the index is
             * never negative, so the same test as through `at`). */
            if ((int32_t)ob->moveAt
                >= (int32_t)ob->moveCount - 1) {
                if (d >= AM2_AI_WAYPOINT_DIST)
                    break;
                if ((int32_t)ob->moveAt
                    < (int32_t)ob->moveCount - 1)
                    break;
                ob->routeGoal = g_zeroPoint;
                ob->moveCount = 0;
                break;
            }
            at++;
            ob->moveAt = (int16_t)at;
            pt = ob->waypoints[at];
            d = ApproxDist(pos, &pt);
        }
    }

    /* One field, at +4 rather than AiRouteToward's +0 and +1. */
    out->bearing = AngleBetween(pos, &pt);

    /* Indexed by the AI context's class, which TrooperBuildContext fills
     * from ClassifyByCode74, so 0, 1 or 2. */
    if (ob->moveStateAlt == 0)
        out->state = g_aiMoveState[ctx->klass];
    else
        out->state = g_aiMoveStateAlt[ctx->klass];
    if (ob->soldierKind == AM2_AI_KIND_FORCES_ALT)
        out->state = 3;

    ob->field578 = 0;
}

/* RoachRouteToward -- original 0x00408210, three callers, all of them
 * ADDR_ROACH_BEHAVIOUR. It is AiRouteToward's TWIN: 518 of its 784 bytes are
 * byte-identical, and the diff of the two disassemblies is twenty-one lines.
 * orig.h had it as "a reachability helper ... it compares the region of the
 * object's position against the region of OBJ_OFF_FIELD_C0", which describes
 * its first six instructions and nothing after them.
 *
 * READ AS A DIFF, WHICH IS THE WHOLE METHOD HERE. Four changes:
 *
 *   A CURSOR THAT HAS RUN OUT RESETS THE PATH rather than heading straight
 *   off -- and that seeder is the one of the three in this pair that does NOT
 *   write OBJ_OFF_ROUTE_GOAL, which is the kind of asymmetry only a diff
 *   surfaces.
 *
 *   IT ARRIVES AT 0x18 where the trooper arrives at 0x20. Eight units closer,
 *   for a thing eight units smaller.
 *
 *   IT REPORTS THROUGH out+0x14 -- 1 for arrived, 2 for heading -- where the
 *   trooper writes a POSE into out+8 and uses out+0x14 only as a slow-down
 *   flag. Same record, different field, so the roach's caller reads somewhere
 *   the trooper's does not.
 *
 *   AND IT HAS NO FOURTH ARGUMENT, no second copy of the bearing into out+1,
 *   and no approach grading at all. A roach either walks or has arrived.
 *
 * Everything else -- BeginMoveTo, the region routing through the all-pairs
 * tables, the middle link, PlanPathTo with the same two budgets, the cursor
 * walk -- is AiRouteToward's, and the comment there is the one to read.
 */
void __cdecl RoachRouteToward(AM2_Object *o, AM2_RoachOut *out,
                              const AM2_RoachCtx *ctx)
{
    AM2_Point *dest = &o->dest;
    AM2_Point pt;                    /* the original's first argument slot */
    int32_t   from, to;
    int32_t   d;

    (void)ctx;

    pt = *dest;

    from = g_regionOfCell[(uint32_t)TileOfPoint(o->pos) & 0xFFFFu];
    to   = g_regionOfCell[(uint32_t)TileOfPoint(pt) & 0xFFFFu];

    if (BeginMoveTo(o, &pt)) {
        o->routeGoal = pt;
        o->moveTo   = pt;
        o->moveFrom = o->pos;
        o->moveEnd   = 0;
        o->moveAt    = 0;
        o->moveCount = 1;
        goto heading;
    }

    if (to > 0 && from > 0 && to != from) {
        /* The stride is re-read after RegionSolvePair. */
        int16_t link;

        o->goalRegion = (int16_t)to;

        if (g_regionCost[(uint32_t)(from * g_regionStride + to)]
            != g_regionStamp)
            RegionSolvePair(from, to);

        if (from == o->prevRegion && !o->stuckCount)
            goto waypoint;

        {
            int16_t hop = g_regionNext[(uint32_t)(from * g_regionStride + to)];

            link = (int16_t)MiddleRegionLink(from, hop);
        }
        if (link < 0)
            goto waypoint;

        {
            int32_t x, y;

            TileToXY(kLinks(from)[link].into, &x, &y);
            if (x) {
                pt.x = (int16_t)x;
                pt.y = (int16_t)y;
            }
        }
    }

waypoint:
    d = ApproxDist(&o->routeGoal, &pt);

    if (d > AM2_AI_REPLAN_DIST
        || (int32_t)o->moveAt >= (int32_t)o->moveCount - 1) {
        int32_t budget;

        o->routeGoal = pt;
        budget = ((int16_t)o->region != 0 && to != 0)
                 ? AM2_AI_PLAN_BUDGET_LONG : AM2_AI_PLAN_BUDGET_SHORT;

        if (!PlanPathTo(o, &pt, budget)) {
            o->routeGoal = pt;
            o->moveFrom = o->pos;
            o->moveTo   = pt;
            o->moveEnd   = 0;
            o->moveAt    = 1;
            o->moveCount = 2;
            goto heading;
        }
    }

    /* Reached BOTH when no replan was wanted and when one SUCCEEDED -- the
     * original's `jne` from PlanPathTo lands here, not past it. */
    if (PointsEqual(o->routeGoal, pt)) {
        if (o->moveAt < o->moveCount) {
            uint16_t at = o->moveAt;

            pt = o->waypoints[at];

            while (ApproxDist(&o->pos, &pt) < AM2_AI_ARRIVED_DIST) {
                at = o->moveAt;
                if ((int32_t)at >= (int32_t)o->moveCount - 1)
                    break;

                at++;
                o->moveAt = at;
                pt = o->waypoints[at];
            }
        } else {
            /* THE ONE STRUCTURAL ADDITION over the trooper's copy: a cursor
             * that has run out resets the path to a zero-length one instead
             * of heading straight off -- and this is the one seeder of the
             * three that does NOT write OBJ_OFF_ROUTE_GOAL. */
            o->moveFrom = o->pos;
            o->moveTo   = pt;
            o->moveEnd   = 0;
            o->moveAt    = 0;
            o->moveCount = 0;
            goto heading;
        }
    }

heading:
    if (ApproxDist(&o->pos, dest) < AM2_ROACH_ARRIVED_DIST) {
        out->state = 1;
        *dest = g_zeroPoint;
        return;
    }

    out->heading = AngleBetween(&o->pos, &pt);
    out->state = 2;
}

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
void __cdecl AiStepIgnore(AM2_Object *o, AM2_SteerOut *out, const AM2_AiCtx *c)
{
    if (c->destDist > AM2_AI_ARRIVED_DIST) {
        o->dest = o->scriptPoint;
        AiRouteToward(o, out, c, 0);
        return;
    }

    o->scriptPoint = g_zeroPoint;

    if (o->hitDir > 0) {    /* unsigned; `> 0` is the original's
                             * `test; jbe`-skip spelling */
        if (!c->observer)
            out->aim = o->hitDir;
        o->hitDir = 0;
    }

    if (c->found
        && (uint32_t)(g_gameClockMs - o->deadlineD0)
           >= AM2_AI_TURN_DELAY_MS)
        out->aim = c->bearing;
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
static inline void AiPromoteFound(AM2_Object *o, AM2_AiCtx *c)
{
    /* The found slot is read afresh for each use. */
    if (!c->found)
        return;

    o->targetUid = c->found->uid;
    c->observer  = c->found;
    c->range     = c->foundRange;
    c->bearing   = c->foundBearing;
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
void __cdecl AiStepDefend(AM2_Object *o, AM2_SteerOut *out, AM2_AiCtx *c)
{
    if (c->destDist > AM2_AI_ARRIVED_DIST) {
        o->dest = o->scriptPoint;
        AiRouteToward(o, out, c, 0);
    } else {
        uint8_t hit = o->hitDir;

        o->scriptPoint = g_zeroPoint;

        if (hit > 0) {
            if (!c->observer)
                out->aim = hit;
            o->hitDir = 0;
        }

        AiPromoteFound(o, c);

        if (c->observer
            && (uint32_t)(g_gameClockMs - o->deadlineD0)
               >= AM2_AI_TURN_DELAY_MS)
            out->aim = c->bearing;
    }

    AiPromoteFound(o, c);
    ConsiderSighting(o, out, c);
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
void __cdecl AiStepTrack(AM2_Object *o, AM2_SteerOut *out, AM2_AiCtx *c)
{
    if (c->destDist > AM2_AI_ARRIVED_DIST) {
        o->dest = o->scriptPoint;
        AiRouteToward(o, out, c, 0);
    } else {
        uint8_t hit = o->hitDir;

        o->scriptPoint = g_zeroPoint;

        if (hit > 0) {
            if (!c->observer)
                out->aim = hit;
            o->hitDir = 0;
        }

        AiPromoteFound(o, c);
    }

    AiPromoteFound(o, c);

    if (c->observer
        && (uint32_t)(g_gameClockMs - o->deadlineD0)
           >= AM2_AI_TURN_DELAY_MS)
        out->aim = c->bearing;

    ConsiderSighting(o, out, c);
}

/* The sight test 0x004057D0 and 0x00405220 share -- 132 instructions, one run,
 * identical in every register, global, immediate AND CALL TARGET, differing
 * only in branch displacements. Call targets were compared rather than
 * normalised on purpose: normalising them makes a call to a different function
 * compare equal, which is a worse error than the noise it removes.
 *
 * IT READS NO CONTEXT FIELD, only the object, the target, the rank record and
 * the heading cache. It is a MACRO, not a helper, because every one of its
 * exits branches straight into the caller's in-sight or out-of-sight arm --
 * `goto SEEN_IT` / `goto NOT_SEEN`, falling through to in-sight -- where an
 * inline function returning a flag leaves VC6 materialising it with a setcc.
 * The rank and direction records are re-indexed at every read, the rank field
 * is compared after the abs(), AngleOfDelta is handed the distance as the
 * third argument the image pushes, and the 1,024-entry line buffer is what
 * makes both callers' frames 0x818. `seen` is the caller's local.
 *
 * THE DIRECTION RECORD IS KEYED ON THE BEARING TO THE TARGET, NOT ON THE
 * VIEWER'S FACING -- and it was `facing` in all three copies of this test
 * until a fourth member of the family was read. Both are bytes in the same
 * eighth-of-a-turn units, both are live in registers at that point, and the
 * original indexes with the register it has just passed to AngleDelta as its
 * SECOND argument. Nothing in the shape of the code distinguishes them; only
 * following that register does. It matters: the cache records how far the
 * view is blocked in a DIRECTION, and the direction that matters is the one
 * the target lies in.
 *
 * AiEngageStep and AiAttackBody use it too, although register-exact diffs
 * against the first two break theirs into runs of 11, 25, 28, 11 and 11, and a
 * single 28: the same source around different surroundings -- in AiAttackBody
 * the bearing lands in ebx and the rank in ebp, the other way round from the
 * rest. All four match byte for byte on it. */
/* The original hands AngleOfDelta the distance as a third argument it does
 * not read; the push is in the image, so it is reproduced. The cast goes
 * through void (*)(void) because that is GCC's sanctioned escape. */
#define AI_ANGLE3(x, y, d)                                                     \
    ((uint8_t (__cdecl *)(int32_t, int32_t, int32_t))(void (*)(void))AngleOfDelta)(x, y, d)
#define AM2_AI_SIGHT_TEST(SEEN_IT, NOT_SEEN)                                  \
{                                                                             \
    uint8_t        facing = o->facing;                                        \
    int32_t        viewerAttr, dx, dy, dist, highest, mine, his;              \
    uint8_t        bearing;                                                   \
    uint32_t       dir;                                                       \
                                                                              \
    if (ObjIsType3(o) && o->rowSet.count > 1)                                 \
        facing = o->turretFacing;                                             \
                                                                              \
    viewerAttr = ObjTileAttr(o);                                              \
    dx = (int32_t)seen->pos.x - (int32_t)o->pos.x;                            \
    dy = (int32_t)seen->pos.y - (int32_t)o->pos.y;                            \
    highest = AM2_AI_SIGHT_FLOOR;                                             \
    dist    = ApproxDistXY(dx, dy);                                           \
    bearing = AI_ANGLE3(dx, dy, dist);                                        \
                                                                              \
    if (abs(AngleDelta(facing, bearing)) > g_rankRecords[o->rank].field_04) { \
        if (dist < g_rankRecords[o->rank].field_08)            \
            goto SEEN_IT;                                                     \
        goto NOT_SEEN;                                                        \
    }                                                                         \
                                                                              \
    dir = (uint32_t)bearing / AM2_SIGHT_DIR_STEP;                             \
    if (g_sightBlockByDir[dir].trace_stamp != g_sightGeneration) { \
        uint16_t buf[AM2_AI_SIGHT_LINE_BUF];                                  \
        int32_t  n, i, reach;                                                 \
                                                                              \
        g_sightBlockByDir[dir].trace_stamp = g_sightGeneration; \
        TraceTileLine(o->pos, seen->pos, buf, &n);                            \
                                                                              \
        for (i = 0; i < n; i++) {                                             \
            int32_t a = (int8_t)(*(const uint8_t *const *)                    \
                                 ((uintptr_t)&g_tileAttrs))[buf[i]];         \
                                                                              \
            if (a > viewerAttr)                                               \
                highest = a;                                                  \
            else if (a + AM2_SIGHT_BAND_STEP < highest)                       \
                break;                                                        \
        }                                                                     \
                                                                              \
        reach = i * AM2_AI_SIGHT_TILE_SPAN + AM2_AI_SIGHT_TILE_BASE;          \
        if (reach >= g_rankRecords[o->rank].sight_range)       \
            reach = g_rankRecords[o->rank].sight_range;        \
                                                                              \
        if (g_sightBlockByDir[dir].stamp == g_sightGeneration) { \
            if (reach < g_sightBlockByDir[dir].low)   \
                g_sightBlockByDir[dir].low = (int16_t)reach; \
            if (reach < g_sightBlockByDir[dir].mid)   \
                g_sightBlockByDir[dir].mid = (int16_t)reach; \
            if (reach < g_sightBlockByDir[dir].high)  \
                g_sightBlockByDir[dir].high = (int16_t)reach; \
        } else {                                                              \
            g_sightBlockByDir[dir].stamp = g_sightGeneration; \
            g_sightBlockByDir[dir].low   = (int16_t)reach; \
            g_sightBlockByDir[dir].mid   = (int16_t)reach; \
            g_sightBlockByDir[dir].high  = (int16_t)reach; \
        }                                                                     \
    }                                                                         \
                                                                              \
    if (g_sightBlockByDir[dir].stamp == g_sightGeneration) { \
        mine = ObjHeight(o);                                                  \
        his  = ObjHeight(seen);                                               \
        if (his >= mine) {                                                    \
            if (dist > g_sightBlockByDir[dir].low)    \
                goto NOT_SEEN;                                                \
            goto SEEN_IT;                                                     \
        }                                                                     \
        if (his + AM2_SIGHT_BAND_STEP >= mine) {                              \
            if (dist > g_sightBlockByDir[dir].mid)    \
                goto NOT_SEEN;                                                \
            goto SEEN_IT;                                                     \
        }                                                                     \
        if (dist > g_sightBlockByDir[dir].high)       \
            goto NOT_SEEN;                                                    \
    } else {                                                                  \
        if (dist > g_rankRecords[o->rank].sight_range)         \
            goto NOT_SEEN;                                                    \
    }                                                                         \
                                                                              \
}

/* AiAttackBody -- original 0x00407710, 1,216 bytes, two callers: mode 0
 * reaches it directly and mode 6 through the `attack` thunk, so it is named
 * for neither. Written from the block map in orig.h; read that first.
 *
 * IT IS THE FOURTH MEMBER OF AiStepIgnore's FAMILY and shares three blocks
 * with it -- the DEST_DIST head, the OBJ_OFF_HIT_DIR turn, and the delayed
 * turn toward what the context found. What it adds is a LINE-OF-SIGHT test,
 * which is what the 0x81C-byte frame is for.
 *
 * FOUR SHAPES HERE ARE EASY TO GET WRONG AND THREE OF THEM I DID, on a first
 * pass that was discarded rather than shipped. They are stated at their sites
 * as well as here, because the function is cold and no A/B can see any of
 * them:
 *
 *   the two tails are different functions that open identically;
 *   the engage arms are not an if/else -- the near one falls through;
 *   the HIGH band borrows the LOW band's compare;
 *   the facing is written twice into one slot, hull then turret.
 *
 * THE TWO RANK FIELDS ARE NOT ALIKE either, and they are read four bytes apart
 * off one base. RANK_REC_OFF_FIELD_04 is compared against an angle delta, so
 * it is an ARC; RANK_REC_OFF_FIELD_08 against a distance, so it is a RANGE.
 * Outside the arc a target still counts if it is inside that range -- a unit
 * can be engaged from behind, but only close up. */
void __cdecl AiAttackBody(AM2_Object *o, AM2_SteerOut *out, AM2_AiCtx *c)
{
    const AM2_Object *seen;
    int32_t        routed = 0;
    uint8_t        hit;
    /* The leader is re-read from the context at every use after the test. */

    if (c->destDist > AM2_AI_ARRIVED_DIST) {
        o->dest = o->scriptPoint;
        AiRouteToward(o, out, c, 0);
        routed = 1;
        if (c->found)
            AiPromoteFound(o, c);
    } else {
        o->scriptPoint = g_zeroPoint;
    }

    seen = c->leader;
    if (seen) {
        /* ONE SLOT, WRITTEN TWICE, inside the test: a type 3 with more than one
         * row aims with its turret and the hull's facing is simply overwritten.
         * Two locals would give a vehicle the wrong arc. */
        AM2_AI_SIGHT_TEST(engage, out_of_sight);

engage:
        /* THE TWO ARMS ARE NOT AN if/else. The far one chases and RETURNS; the
         * near one records the target and FALLS THROUGH into the hit tail, so a
         * unit already at the range it wants still reacts to being hit and still
         * turns toward what the context found. Writing the near arm as the far
         * one's `else` with a shared tail deletes that silently. */
        if (c->leadRange > c->wantRange) {
            o->dest = c->leader->pos;
            if (!routed)
                AiRouteToward(o, out, c, 0);
            o->targetUid = c->leader->uid;
            c->observer = c->leader;
            c->range = c->leadRange;
            c->bearing = c->leadBearing;
            out->aim = c->leadBearing;
            goto done;
        }

        o->targetUid = c->leader->uid;
        c->observer = c->leader;
        c->range = c->leadRange;
        c->bearing = c->leadBearing;
        out->aim = c->leadBearing;

        /* ---- the CLOSE tail, which is not the no-leader tail below even though
         * both open with the same OBJ_OFF_HIT_DIR block. This one turns on
         * SIGHT_OFF_LEAD_BEARING and ends by walking toward a type-2 leader; that
         * one turns on SIGHT_OFF_BEARING and writes OBJ_OFF_FOLLOW_UID.
         *
         * TWO OF ITS GUARDS CANNOT FAIL ON THIS PATH and are kept anyway. The
         * arm above has just written SIGHT_OFF_OBSERVER, so the hit never turns
         * the unit here -- only the clear runs, which is the behaviour
         * AiStepIgnore's comment describes and here it is guaranteed rather than
         * possible. And SIGHT_OFF_LEADER was tested non-null at the top, so the
         * delayed turn's first test is always true. Both are live when the block
         * is reached the other way, which it is not; reproduced because the
         * original does not know that either. */
        hit = o->hitDir;
        if (hit > 0) {
            if (!c->observer)
                out->aim = hit;
            o->hitDir = 0;
        }
        if (c->leader
            && (uint32_t)(g_gameClockMs - o->deadlineD0)
               >= AM2_AI_TURN_DELAY_MS)
            out->aim = c->leadBearing;

        if (!ObjIsType2(c->leader))
            goto done;
        o->dest = c->leader->pos;
        if (routed)
            goto done;
        AiRouteToward(o, out, c, 0);
        goto done;

out_of_sight:
        /* Face it and walk to it, but do not claim it as the observer. */
        o->dest = c->leader->pos;
        if (!routed)
            AiRouteToward(o, out, c, 0);
        out->aim = c->leadBearing;
        if (c->found)
            AiPromoteFound(o, c);
        goto done;
    }

    /* No leader. */
    hit = o->hitDir;
    if (hit > 0) {
        if (!c->observer)
            out->aim = hit;
        o->hitDir = 0;
    }
    if (c->found) {
        AiPromoteFound(o, c);
        out->aim = c->bearing;
    }
    if (c->observer) {
        if ((uint32_t)(g_gameClockMs - o->deadlineD0)
            >= AM2_AI_TURN_DELAY_MS)
            out->aim = c->bearing;
        o->followUid = c->observer->uid;
    }

done:
    ConsiderSighting(o, out, c);
}

/* The tile-count-to-distance conversion, the clamp and the three-band minimum
 * update, shared verbatim between AiAttackBody and AiEngageStep.
 *
 * FACTORED ON EVIDENCE, NOT ON RESEMBLANCE. The two runs -- 38 instructions at
 * 0x004078C0 and at 0x00406DBC -- are identical in every register, global,
 * immediate and displacement, and differ in eight BRANCH TARGETS and nothing
 * else. That was checked with only the branch targets normalised, because a
 * diff that also normalises image addresses makes two blocks reading DIFFERENT
 * globals compare equal, which is what VehicleBlockWeight cost this project
 * once already.
 *
 * `viewerAttr` is the caller's own tile attribute and `highest` starts below
 * anything an attribute can hold: a tile higher than the viewer raises the
 * running maximum and does not stop the walk, and only the step back down from
 * it by more than AM2_SIGHT_BAND_STEP does. */
/* The rank records and the per-direction sight cache (globaltypes.h) are
 * arrays of records, and SightScan indexes them as arrays -- `[rank*28 + 0x473DC0 + field]`,
 * `[dir*16 + 0x4F8FB8 + field]` -- rather than through element pointers. */

/* The parameters are references: inlined, a by-value parameter is one more
 * variable to VC6, and the image has none (RegionFindPath 96 -> 37). */
static inline void AiSightTrace(const AM2_Object *const &o,
                                const AM2_Object *const &seen,
                                const uint32_t &dir, const int32_t &viewerAttr,
                                const int32_t &gen, int32_t &highest)
{
    uint16_t       buf[AM2_AI_SIGHT_LINE_BUF];   /* SightScan's 0x84C frame holds 1024 */
    int32_t        n, i, reach;

    g_sightBlockByDir[dir].trace_stamp = gen;
    TraceTileLine(o->pos, seen->pos, buf, &n);

    for (i = 0; i < n; i++) {
        int32_t a = (int8_t)g_tileAttrs[buf[i]];

        if (a > viewerAttr)
            highest = a;
        else if (a + AM2_SIGHT_BAND_STEP < highest)
            break;
    }

    reach = i * AM2_AI_SIGHT_TILE_SPAN + AM2_AI_SIGHT_TILE_BASE;
    if (reach >= g_rankRecords[o->rank].sight_range)
        reach = g_rankRecords[o->rank].sight_range;

    if (g_sightBlockByDir[dir].stamp == gen) {
        if (reach < g_sightBlockByDir[dir].low)
            g_sightBlockByDir[dir].low = (int16_t)reach;
        if (reach < g_sightBlockByDir[dir].mid)
            g_sightBlockByDir[dir].mid = (int16_t)reach;
        if (reach < g_sightBlockByDir[dir].high)
            g_sightBlockByDir[dir].high = (int16_t)reach;
    } else {
        g_sightBlockByDir[dir].stamp = gen;
        g_sightBlockByDir[dir].low   = (int16_t)reach;
        g_sightBlockByDir[dir].mid   = (int16_t)reach;
        g_sightBlockByDir[dir].high  = (int16_t)reach;
    }
}

/* The two remembered points this step clears and restores are
 * OBJ_OFF_SCRIPT_STATE and OBJ_OFF_SCRIPT_FRAME -- both named from
 * UpdateObjectScript, both used here as PACKED POINTS, which is the same
 * overload AiStepDefend and AiStepTrack already rely on for the first of them.
 * The structural names are what makes that survivable.
 */

/* AiEngageStep -- original 0x00406B30, 1,264 bytes over a 0x818-byte frame,
 * one caller. The name is OURS: it names itself nowhere and the act it decides
 * is whether to hold range, close, engage or go looking.
 *
 * IT USES THE SIGHTC_ CONTEXT, the 0x58-byte one, where AiAttackBody uses
 * SIGHT_. The two families sit four bytes apart -- OBSERVER at 0x14 here and
 * 0x10 there -- so reading it with the other names shifts every field and
 * still compiles. Which family a function uses is the first thing to settle.
 *
 * THE HEAD IS A THREE-BAND RANGE DECISION and its middle threshold is the
 * weapon's DAMAGE. That reads wrong -- a distance compared against a damage --
 * and it is what the instruction says: SIGHTC_OFF_DAMAGE has exactly two
 * touchers in the whole image, UnitWeaponInfo writing ITEMTYPE_OFF_DAMAGE into
 * it and this comparison, so there is no third reader to appeal to. Taken at
 * face value it means a unit stands off in proportion to how hard its own
 * weapon hits, which for a grenade or a bazooka is what you would want. The
 * name stays, because the WRITER is unambiguous about what the field holds;
 * this is a note about what it is USED for.
 *
 * ARMS THAT END INSIDE OTHER ARMS, twice. The out-of-sight path falls into the
 * in-sight path's tail when the context found nothing, so it promotes the
 * LEADER instead; and the HIGH band's compare borrows the LOW band's `jg`, as
 * in AiAttackBody. Following the bodies without following the branches gives
 * both no exit at all.
 *
 * EVERY PATH ENDS AT ConsiderSightingC, including the ones that already called
 * AiKeepRange or AiHitReact. */
void __cdecl AiEngageStep(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    const AM2_Object *seen;
    int32_t        range = c->range;
    /* The leader and the found object are re-read at every use. Each step
     * and promotion is written at its site; VC6 cross-jumps the ones whose
     * registers agree. */
#define PROMOTE_FOUND()                                                       \
    do {                                                                      \
        o->targetUid =                               \
            c->found->uid;                      \
        c->observer = c->found;                       \
        c->range =                                  \
            c->foundRange;                   \
        c->bearing =                                           \
            c->foundBearing;                 \
    } while (0)
#define PROMOTE_LEAD()                                                        \
    do {                                                                      \
        o->targetUid =                               \
            c->leader->uid;                     \
        c->observer = c->leader;                      \
        c->range =                                  \
            c->leadRange;                    \
        c->bearing =                                           \
            c->leadBearing;                  \
    } while (0)

    if (range > 0) {
        if (range > c->wantRange) {
            o->dest = c->observer->pos;
            AiTrooperStep(o, out, c);
            if (c->found)
                PROMOTE_FOUND();
            goto done;
        }
        if (range < c->damage) {
            /* Too close for its own weapon: back off. The OBSERVER is passed
             * as the mover and this unit as the target, so the point is
             * MAX_RANGE out from the enemy along the line toward us -- a
             * retreat to the weapon's range. With the two swapped it is a
             * point MAX_RANGE from us toward the enemy, a charge. */
            RandomPointToward(o, c->observer,
                              c->maxRange,
                              &o->dest);
            AiTrooperStep(o, out, c);
            goto done;
        }
        AiKeepRange(o, out, c);
        goto done;
    }

    if (c->destDistB > AM2_AI_ENGAGE_DEST_B) {
        o->dest = o->goalPoint;
        AiTrooperStep(o, out, c);
        if (c->found)
            PROMOTE_FOUND();
        goto done;
    }

    if (c->destDist > AM2_AI_ENGAGE_DEST) {
        o->goalPoint = g_zeroPoint;
        o->dest = o->scriptPoint;
        AiTrooperStep(o, out, c);
        if (c->found)
            PROMOTE_FOUND();
        goto done;
    }

    /* Arrived: forget both remembered points and look for something to do. */
    o->goalPoint = g_zeroPoint;
    o->scriptPoint = g_zeroPoint;

    if (c->leader) {
        /* Out of the band the current weapon wants: pick another and rebuild
         * the weapon half of the context around it. */
        if (c->leadRange
            > c->wantRange) {
            SelectRankedWeapon(o);
            UnitWeaponInfo(o, c);
        }

        seen = c->leader;
        AM2_AI_SIGHT_TEST(in_sight, out_of_sight);

in_sight:
        if (c->leadRange
            > c->wantRange) {
            o->dest = c->leader->pos;
            AiTrooperStep(o, out, c);
            PROMOTE_LEAD();
            goto done;
        }

        /* Close enough to act on. An object that is not one of types 2, 3 or
         * 8 has to carry flag 4 to be worth reacting to at all. */
        if (!ObjIsTypeIn238(c->leader)
            && !(c->leader->flagsLo & 4))
            goto done;

        PROMOTE_LEAD();
        AiHitReact(o, out, c);
        AiKeepRange(o, out, c);
        goto done;

out_of_sight:
        o->dest = c->leader->pos;
        AiTrooperStep(o, out, c);
        /* A unit that cannot see its leader still adopts it as the observer
         * when nothing was found. */
        if (c->found)
            PROMOTE_FOUND();
        else
            PROMOTE_LEAD();
        goto done;
    }

    AiHitReact(o, out, c);
    if (c->found)
        PROMOTE_FOUND();
    AiKeepRange(o, out, c);
    if (c->observer)
        o->followUid = c->observer->uid;

done:
    (void)out;
    ConsiderSightingC(o, out, c);
#undef PROMOTE_LEAD
#undef PROMOTE_FOUND
}

/* AiPatrolStep -- original 0x004057D0, 1,344 bytes, three callers, one of them
 * the three-argument thunk at 0x00405D10. The name is OURS: it names itself
 * nowhere, and what it decides is which of three remembered points to walk to
 * before it looks for anything to shoot.
 *
 * THE ARGUMENTS ARE THE OTHER WAY ROUND from AiAttackBody and AiEngageStep --
 * the object is in esi and the context in edi, where those two have it the
 * other way. All three are void(obj, out, ctx) and the register choice is the
 * compiler's; a reading carried over from the neighbours puts every field on
 * the wrong structure and still compiles.
 *
 * THE FOUR "SCRIPT" DWORDS AT 0xB0..0xBC ARE PACKED POINTS HERE, all of them.
 * The head copies OBJ_OFF_SCRIPT_STATE into OBJ_OFF_SCRIPT_ID or clears it,
 * and OBJ_OFF_SCRIPT_NEXT goes to ApproxDist, which takes an AM2_Point *.
 * They keep the names UpdateObjectScript gave them because that function's
 * reading is equally live; the overload is the fact, not a mistake to fix.
 *
 * THE THREE POINTS ARE A STACK, which is what the head is doing. SCRIPT_ID
 * holds a saved copy of SCRIPT_STATE, SCRIPT_NEXT is a detour, and each is
 * cleared to ADDR_ZERO_POINT as it is reached -- so the unit walks the detour,
 * then the state point, then restores the state point from the saved copy and
 * starts over.
 *
 * AND IT ENDS BY FORGETTING A KIND-7 TARGET AFTER FIVE SECONDS. The tail
 * re-reads SIGHTC_OFF_FOUND, and when it is a kind 7 and
 * AM2_AI_KEEP_RANGE_MS has passed since OBJ_OFF_DEADLINE_58, it picks a fresh
 * point 360 units toward it and stamps the clock. That is the only use of
 * RandomPointAhead here and the only thing 0x168 means. */
void __cdecl AiPatrolStep(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    AM2_Object *seen;

    /* Save the state point, or remember that there was none. */
    if ((uint16_t)o->scriptPoint.x)
        o->savedPoint = o->scriptPoint;
    else
        o->savedPoint = g_zeroPoint;

    if (o->fieldF4 <= 0
        && !o->ammo) {
        /* Nothing to fight: walk the detour, then the state point, then
         * reload. */
        o->fieldEC = 0;

        if ((uint16_t)o->detour.x) {
            if (ApproxDist(&o->pos,
                           &o->detour)
                > AM2_AI_REACHED_DIST) {
                o->dest = o->detour;
                AiTrooperStep(o, out, c);
                goto tail;
            }
            o->detour = g_zeroPoint;
            goto react;
        }

        if (c->leader) {
            o->dest = c->leader->pos;
            AiTrooperStep(o, out, c);
            goto tail;
        }

        if (c->destDist > AM2_AI_REACHED_DIST) {
            o->detour = g_zeroPoint;
            o->dest = o->scriptPoint;
            AiTrooperStep(o, out, c);
            goto tail;
        }

        /* Arrived at the state point: clear the detour and reload the state
         * point from the copy the head made. */
        o->detour = g_zeroPoint;
        o->scriptPoint = o->savedPoint;
        goto react;
    }

    /* A kind-7 sighting is not worth detouring for. */
    if (c->found
        && !IsKind7(c->found))
        o->detour = g_zeroPoint;

    if (o->fieldEC) {
        if (c->destDist
            > AM2_AI_REACHED_DIST) {
            o->dest = o->scriptPoint;
            AiTrooperStep(o, out, c);
            goto tail;
        }
        o->fieldEC = 0;
        goto react;
    }

    if ((uint16_t)o->detour.x) {
        if (ApproxDist(&o->pos,
                       &o->detour)
            > AM2_AI_REACHED_DIST) {
            o->dest = o->detour;
            AiTrooperStep(o, out, c);
            goto tail;
        }
        o->detour = g_zeroPoint;
        goto react;
    }

    seen = c->leader;
    if (!seen)
        goto react;

    AM2_AI_SIGHT_TEST(seen_it, not_seen);

    /* IN SIGHT SPLITS IN TWO AND THE HALVES END IN DIFFERENT PLACES. Further
     * than the weapon wants: close the distance, adopt it, and go STRAIGHT TO
     * THE TAIL -- no AiHitReact, no AiKeepRange, because it is already moving.
     * Close enough: stop, adopt it, and fall into the react block. Reading
     * this as one arm with a shared tail gives a chasing unit two calls it
     * does not make. */
seen_it:
    if (c->leadRange
        >= c->wantRange) {
        o->dest = c->leader->pos;
        AiTrooperStep(o, out, c);
        o->targetUid = c->leader->uid;
        c->observer = c->leader;
        c->range = c->leadRange;
        c->bearing = c->leadBearing;
        goto tail;
    }

    o->dest = g_zeroPoint;
    o->targetUid = c->leader->uid;
    c->observer = c->leader;
    c->range = c->leadRange;
    c->bearing = c->leadBearing;

react:
    AiHitReact(o, out, c);
    if (c->found) {
        o->targetUid = c->found->uid;
        c->observer = c->found;
        c->range = c->foundRange;
        c->bearing = c->foundBearing;
    }
    if (!o->followUid)
        o->followUid = o->targetUid;
    AiKeepRange(o, out, c);
    goto tail;

not_seen:
    /* Out of sight: walk toward it. */
    o->dest = c->leader->pos;
    AiTrooperStep(o, out, c);

tail:
    /* Re-read what the context found: AiHitReact and the steps above may have
     * replaced it. A kind 7 that has held our attention for five seconds gets
     * a fresh detour point 360 units toward it. */
    if (c->found) {
        if (IsKind7(c->found)
            && (uint32_t)(g_gameClockMs
                          - o->deadline58)
               > AM2_AI_KEEP_RANGE_MS) {
            o->deadline58 = g_gameClockMs;
            /* (unused, the object to head toward, distance, out) -- the
             * first argument really is discarded, per air.h, and the original
             * passes the stepping object into it. */
            RandomPointAhead(o, c->found,
                             AM2_AI_PATROL_DETOUR,
                             &o->detour);
        }
        o->targetUid = c->found->uid;
        c->observer = c->found;
        c->range = c->foundRange;
        c->bearing = c->foundBearing;
    }

    if (!o->followUid)
        o->followUid = o->targetUid;

    ConsiderSightingC(o, out, c);
}

/* AiGuardStep -- original 0x00405220, 1,424 bytes, one caller. AiPatrolStep's
 * pair: 132 of their instructions are the sight test, and 18 more are its
 * adopt-the-leader tail; the sight test is AM2_AI_SIGHT_TEST, whose exits
 * branch straight into the in-sight and out-of-sight arms. The name is ours, and it comes from the
 * one thing that distinguishes the two -- this one is on a LEASH.
 *
 * FOUR DIFFERENCES FROM AiPatrolStep, none of them visible in the shape:
 *
 * THE LEASH. Both of its no-detour paths test SIGHTC_OFF_DEST_DIST against
 * AM2_AI_PATROL_DETOUR -- 360, the same constant AiPatrolStep only ever uses
 * as a distance to walk -- and past it the unit reloads its post from the saved
 * copy, walks home, and CLEARS BOTH OBJ_OFF_FOLLOW_UID AND OBJ_OFF_TARGET_UID.
 * So it abandons what it was chasing rather than chasing it off the post.
 * AiPatrolStep has no such test anywhere.
 *
 * ITS ENGAGED GATE IS ONE TEST, not two: AiPatrolStep takes the engaged path on
 * OBJ_OFF_FIELD_F4 or OBJ_OFF_TARGET_UID, this one on OBJ_OFF_FIELD_F4 alone.
 *
 * IT DROPS OBJ_OFF_FOLLOW_UID BEFORE LOOKING, so whatever it sees becomes the
 * thing it follows rather than being merged with what it followed before.
 *
 * AND BOTH IN-SIGHT ARMS REACH THE REACT BLOCK. In AiPatrolStep the far arm
 * closes the distance and goes straight to the tail, skipping AiHitReact and
 * AiKeepRange; here both arms adopt the target and fall through to them. Two
 * functions sharing 150 instructions and differing on whether a chasing unit
 * gets to shoot -- which is exactly what merging them would have lost. */
void __cdecl AiGuardStep(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    int32_t  dest;
    uint16_t state;

    state = (uint16_t)o->scriptPoint.x;
    if (state)
        o->savedPoint = o->scriptPoint;
    else
        o->savedPoint = g_zeroPoint;

    if (o->fieldF4 <= 0) {
        o->fieldEC = 0;
        dest = c->destDist;

        if (dest > AM2_AI_PATROL_DETOUR) {
            /* Off the leash: drop the detour, restore the post and go back to it,
             * forgetting whatever was being chased. */
            o->detour = g_zeroPoint;
            if (!state)
                o->scriptPoint = o->savedPoint;
            o->dest = o->scriptPoint;
            AiTrooperStep(o, out, c);
            o->followUid = 0;
            o->targetUid = 0;
            goto tail;
        }

        if ((uint16_t)o->detour.x) {
            if (ApproxDist(&o->pos,
                           &o->detour)
                > AM2_AI_REACHED_DIST) {
                o->dest = o->detour;
                AiTrooperStep(o, out, c);
                goto tail;
            }
            o->detour = g_zeroPoint;
            goto react;
        }

        if (dest > AM2_AI_REACHED_DIST) {
            o->dest = o->scriptPoint;
            AiTrooperStep(o, out, c);
            goto tail;
        }
        o->scriptPoint = o->savedPoint;
        goto react;
    }

    if (c->found
        && !IsKind7(c->found))
        o->detour = g_zeroPoint;

    if (o->fieldEC) {
        if (c->destDist > AM2_AI_REACHED_DIST) {
            o->dest = o->scriptPoint;
            AiTrooperStep(o, out, c);
            goto tail;
        }
        o->fieldEC = 0;
        goto react;
    }

    if (c->destDist > AM2_AI_PATROL_DETOUR) {
        if (!(uint16_t)o->scriptPoint.x)
            o->scriptPoint = o->savedPoint;
        o->dest = o->scriptPoint;
        AiTrooperStep(o, out, c);
        o->followUid = 0;
        o->targetUid = 0;
        goto tail;
    }

    if ((uint16_t)o->detour.x) {
        if (ApproxDist(&o->pos,
                       &o->detour)
            > AM2_AI_REACHED_DIST) {
            o->dest = o->detour;
            AiTrooperStep(o, out, c);
            goto tail;
        }
        o->detour = g_zeroPoint;
        goto react;
    }

    if (!c->leader)
        goto react;

    /* Forget who we were following BEFORE looking, so what is seen replaces it
     * outright. AiPatrolStep keeps the old uid and only fills a null one. */
    o->followUid = 0;

    {
        const AM2_Object *seen = c->leader;

        AM2_AI_SIGHT_TEST(seen_it, not_seen);

        /* In sight: BOTH arms adopt and fall into the react block -- see
         * the header. */
seen_it:
        if (c->leadRange
            >= c->wantRange) {
            o->dest = c->leader->pos;
            AiTrooperStep(o, out, c);
            o->targetUid = c->leader->uid;
            c->observer = c->leader;
            c->range = c->leadRange;
            c->bearing = c->leadBearing;
            goto react;
        }
        o->dest = g_zeroPoint;
        o->targetUid = c->leader->uid;
        c->observer = c->leader;
        c->range = c->leadRange;
        c->bearing = c->leadBearing;
        goto react;
    }

not_seen:
    /* Out of sight: walk toward it, and DO NOT adopt it -- straight to the
     * react block, as AiPatrolStep's out-of-sight arm skips it too. */
    o->dest = c->leader->pos;
    AiTrooperStep(o, out, c);

react:
    AiHitReact(o, out, c);
    if (c->found) {
        if (IsKind7(c->found)
            && (uint32_t)(g_gameClockMs
                          - o->deadline58)
               > AM2_AI_KEEP_RANGE_MS) {
            o->deadline58 = g_gameClockMs;
            RandomPointAhead(o, c->found,
                             AM2_AI_PATROL_DETOUR,
                             &o->detour);
        }
        o->targetUid = c->found->uid;
        c->observer = c->found;
        c->range = c->foundRange;
        c->bearing = c->foundBearing;
    }
    if (!o->followUid)
        o->followUid = o->targetUid;
    AiKeepRange(o, out, c);
    goto tail;

tail:
    if (c->found) {
        o->targetUid = c->found->uid;
        c->observer = c->found;
        c->range = c->foundRange;
        c->bearing = c->foundBearing;
    }
    if (!o->followUid)
        o->followUid = o->targetUid;

    ConsiderSightingC(o, out, c);
}

/* ObjectsInRect is reconstructed in win32/mapdraw.cpp and is declared here
 * rather than by including that header, for the reason script.cpp gives about
 * PreloadSprite: region.cpp is flat and mapdraw.h names Win32 types. The
 * signature is copied exactly and NOT wrapped in `extern "C"`: mapdraw.h's
 * block closes at line 129 and this declaration is at 164, so it has C++
 * linkage. Getting that backwards is a link error rather than a silent one,
 * which is the only reason it is safe to repeat a declaration at all. */
struct AM2_Object *__cdecl ObjectsInRect(const AM2_Rect *r, const struct AM2_MapDesc *desc,
                            int32_t (__cdecl *keep)(AM2_Object *obj));

/* 0x0045CAA0 called with ONE argument and no format string -- a third role for
 * that folded `ret`, neither the varargs logger nor a vtable slot. See orig.h.
 * gamelog.c's safe_format walks the string by hand, so handing it an object
 * pointer renders bytes as text and cannot fault. */
typedef void (__cdecl *AM2_LogOneFn)(void *obj);
typedef void (__cdecl *AM2_LogSixFn)(const char *, ...);
#define orig_log_six ((AM2_LogSixFn)(uintptr_t)((void *)(uintptr_t)(ADDR_LOG)))
#define orig_log_one ((AM2_LogOneFn)(uintptr_t)((void *)(uintptr_t)(ADDR_LOG)))

/* SightScan -- original 0x00403B40, 1,888 bytes over a 0x84C frame, five
 * callers. What fills SIGHT_OFF_FOUND and the range and bearing beside it, so
 * it is the source of everything every AI step in this file acts on. The name
 * is ours; it names itself nowhere.
 *
 * ITS STACK WAS RESOLVED WITH tools/espmap.py. Two claims about this frame
 * made by hand were wrong before that tool existed, and a third -- the sight
 * cache's index -- was wrong in three already-shipped functions until this one
 * was read. Nothing here about a slot is from counting by eye.
 *
 * IT IS WHERE THE HEADING CACHE COMES FROM: it bumps ADDR_SIGHT_GENERATION and
 * hands every object it REJECTS to AddSightBlocker, which writes the minima the
 * AI steps only ever read.
 *
 * TWO PHASES, AND THE FIRST EDITS THE LIST IT WALKS. What survives is
 * PREPENDED to a second list through the same OBJ_OFF_QUERY_NEXT link, so the
 * candidates come out reversed, and everything else becomes a sight blocker.
 * One pass builds both the candidates and the occlusion data they are about to
 * be tested against.
 *
 * A UNIT ADOPTS WHAT ITS ALLIES ARE SHOOTING AT, which is the thing a reading
 * of the bodies loses. The allied arm looks like count-and-skip and three of
 * its exits fall into the SCORING tail with the ALLY'S TARGET as the
 * candidate. Written as five `continue`s, a unit only ever engages what it saw
 * itself -- a plausible AI and the wrong one.
 *
 * THREE COMPUTATIONS ARE DEAD AND ALL THREE ARE REPRODUCED, because each is a
 * call and therefore visible in a counter:
 *
 *   ObjIsTypeIn238 is called TWICE IN A ROW on the same object with opposite
 *   branches; the second cannot fire, and its target does not advance the
 *   walk, so reading it as live makes the loop look non-terminating.
 *
 *   AngleDelta is called in the scoring tail and its answer overwritten two
 *   instructions later. The comparison uses range against range and never the
 *   angle.
 *
 *   The exit stores the bearing and then overwrites it with AngleBetween from
 *   ObjAnchorPoint to the winner -- so the primary arm answers a bearing
 *   measured from the object's ANCHOR while the fallback answers the recorded
 *   one. Two exits, two different bearings.
 *
 * THE SLACK STARTS AT 0x80 AND BECOMES 0 once anything is picked, so the first
 * plausible candidate wins by 128 and everything after it is compared exactly.
 * And REGIONS ARE SOLVED ONCE: the weapon arm passes a flag to RegionsNear
 * that it clears immediately, so only the first weapon considered pays for a
 * region solve. */
AM2_Object *__cdecl SightScan(AM2_Object *o, int32_t *outRange,
                              uint8_t *outBearing, int32_t *nAllied,
                              int32_t *nOther, int32_t flags)
{
    AM2_Object *keep;
    AM2_Object *result;
    AM2_Object *it;
    uint8_t  bestBearing;
    /* The scan state sits in three local structs, one of whose members has its
     * address taken (DistAndAngle): that makes VC6 reload the candidate in the
     * kind-7 arm, so the scoring block written out there is identical to the
     * labelled one and the two merge, as the image's layout needs. */
    /* facing and look live in the hold struct, after best, with a gap before look:
     * the image's frame has them there. */
    struct { int32_t highest; int32_t solve; AM2_Object *best; uint8_t facing; uint8_t pad[3]; uint8_t look; } hold;
    struct { uint8_t slack; AM2_Rect box; } win;
    struct { int32_t range; uint8_t bearing; } pick;
    struct { int32_t bestRange; AM2_Object *cand; int32_t bestScore; AM2_Object *alt; } stt;

    if (g_perframeCountA
        > AM2_SIGHT_SCANS_PER_FRAME)
        return (AM2_Object *)0;

    /* One slot written twice: a type 3 with more than one row aims with its
     * turret. Two displacements four apart, one dword. */
    hold.facing = o->facing;
    if (ObjIsType3(o)
        && o->rowSet.count > 1)
        hold.facing = o->turretFacing;

    *outRange   = 0;
    *outBearing = 0;
    *nAllied    = 0;
    *nOther     = 0;

    keep        = (AM2_Object *)0;
    hold.best        = (AM2_Object *)0;
    stt.bestRange   = AM2_SIGHT_SCAN_FAR;
    bestBearing = 0x80;
    win.slack       = 0x80;
    stt.alt         = (AM2_Object *)0;
    stt.bestScore   = 0;
    hold.solve       = 1;

    /* A weapon already earmarked stays the fallback while it is still there,
     * not flagged gone, reachable, and worth taking. */
    if (ObjIsType2(o)
        && o->pickupAfter) {
        AM2_Object *w = LookupByUID(
            o->pickupAfter);

        if (w && !(w->flagsLo & 4)
            && RegionsNear(w, o, 1)) {
            int32_t n = TryTakeWeapon(w, o);

            if (n) {
                stt.alt       = w;
                stt.bestScore = n;
            }
        }
    }

    o->fieldFC =
        g_gameClockMs
        + AM2_AI_TROOPER_RETRY_MS;
    o->fieldF4 = 0;
    ++g_sightGeneration;
    orig_log_one(o);

    /* The rank record read at each edge, as the image re-reads it. */
    win.box.left   = (int32_t)o->pos.x - (g_rankRecords[o->rank].sight_range);
    win.box.right  = (int32_t)o->pos.x + (g_rankRecords[o->rank].sight_range);
    win.box.top    = (int32_t)o->pos.y - (g_rankRecords[o->rank].sight_range);
    win.box.bottom = (int32_t)o->pos.y + (g_rankRecords[o->rank].sight_range);

    if (flags)
        it = ObjectsInRect(&win.box, g_objMapDesc,
                                      ObjIsHittable);
    else
        it = ObjectsInRect(&win.box, g_objMapDesc,
                                      ObjIsLiveTarget);
    if (!it)
        goto finish;

    /* ---- phase one: split the chain, and build the blockers as we go ---- */
    while (it) {
        AM2_Object *cur;

        if (o == it) {
            it = it->queryNext;
            continue;
        }
        if (!IsKind7(it) && !ObjIsTypeIn238(it)
            && !ObjIsType4(it)) {
            /* The second call cannot answer yes -- see the header. */
            if (ObjIsTypeIn238(it))
                continue;
            AddSightBlocker(o, it);
            it = it->queryNext;
            continue;
        }
        /* Moved onto the kept list through its link: the walk advances
         * first, then the node is relinked and kept. */
        cur = it;
        it  = cur->queryNext;
        cur->queryNext = keep;
        keep = cur;
        /* The predicate asks about the SCANNING object, not the candidate,
         * so this is constant across the walk and evaluated every time. */
        if (ObjIsOurs(o, 0) && ObjIsTypeIn238(cur)) {
            RevealObj(cur);
            cur->revealedUntil =
                g_gameClockMs
                + AM2_SIGHT_REVEAL_MS;
        }
    }

    if (!keep)
        goto finish;

    /* ---- phase two: score what survived ----
     * The kept list itself is the cursor: a separate `it` costs a register
     * copy at the loop entry that the image does not have. */
    for (; keep;
         keep = keep->queryNext) {
        stt.cand = (AM2_Object *)0;
        pick.bearing = 0x80;
        pick.range = AM2_SIGHT_SCAN_FAR;

        /* An if/else, not `goto allied`: the image lays the allied arm out
         * after the rest, where a goto pulls it up behind the test. Both
         * candidates, the Kind7 one and the one the sight test accepts, fall
         * into the scoring block that follows the if/else. */
        if (!ObjsAreAllied(o, keep, 0)) {
            if (IsKind7(keep)) {
                stt.cand        = keep;
                pick.bearing = AngleBetween(&o->pos,
                                           &keep->pos);
                pick.range   = AM2_SIGHT_KIND7_RANGE;
                goto score;
            } else {
            /* The sight test, inline. It opens exactly as AM2_AI_SIGHT_TEST does:
             * the trace's height floor set first, the distance from the two
             * deltas, AngleOfDelta handed that distance as its unread third
             * argument, and one abs(AngleDelta(...)) compared with the rank's
             * arc. The rest differs (the trace is AiSightTrace, the arms
             * `continue`). */
            {
                int32_t  viewerAttr, dx, dy;
                uint32_t dir;

                hold.look = o->facing;
                if (ObjIsType3(o)
                    && o->rowSet.count > 1)
                    hold.look = o->turretFacing;

                viewerAttr = ObjTileAttr(o);
                dx = (int32_t)keep->pos.x
                     - (int32_t)o->pos.x;
                dy = (int32_t)keep->pos.y
                     - (int32_t)o->pos.y;
                hold.highest     = AM2_AI_SIGHT_FLOOR;
                pick.range   = ApproxDistXY(dx, dy);
                pick.bearing = AI_ANGLE3(dx, dy, pick.range);

                if (abs(AngleDelta(hold.look, pick.bearing)) > g_rankRecords[o->rank].field_04) {
                    if (pick.range < g_rankRecords[o->rank].field_08)
                        goto in_sight;
                    goto next;
                }

                /* Keyed on the BEARING, not the facing -- see orig.h. */
                dir = (uint32_t)pick.bearing / AM2_SIGHT_DIR_STEP;
                if (g_sightBlockByDir[dir].trace_stamp != g_sightGeneration)
                    AiSightTrace(o, keep, dir, viewerAttr, g_sightGeneration, hold.highest);

                if (!(g_sightBlockByDir[dir].stamp != g_sightGeneration)) {
                    int32_t mine = ObjHeight(o);
                    int32_t his  = ObjHeight(keep);

                    if (his >= mine) {
                        if (pick.range > g_sightBlockByDir[dir].low)
                            goto next;
                    } else if (his + AM2_SIGHT_BAND_STEP >= mine) {
                        if (pick.range > g_sightBlockByDir[dir].mid)
                            goto next;
                    } else if (pick.range > g_sightBlockByDir[dir].high) {
                        goto next;
                    }
                } else {
                    if (pick.range > g_rankRecords[o->rank].sight_range)
                        goto next;
                }
            }

in_sight:
            /* Modes 2 and 3 refuse a candidate that is allied WITH US, but only
             * once a second has passed since we were last hit -- and the refusal
             * skips recording it while still revealing it below. */
            /* Cleared first and set when NOT refused: the image tests and
             * then stores the object on the fall-through. */
            if (!((o->aiMode == 3
                 || o->aiMode == 2)
                && (uint32_t)(g_gameClockMs
                              - o->hitTime)
                       > AM2_SIGHT_KIND7_RANGE
                && ObjsAreAllied(keep, o, 0)))
                stt.cand = keep;

            if (ObjIsOurs(o, 0)) {
                RevealObj(keep);
                keep->revealedUntil =
                    g_gameClockMs
                    + AM2_SIGHT_REVEAL_MS;
            }

            }
score:
                if (!stt.cand)
                    goto next;
                ++*(volatile int32_t *)nOther;       /* a number win: volatile read-modify-writes in the image's order */
                ++*(volatile int32_t *)&o->fieldF4;
                /* Discarded two instructions later; a call, so it counts. */
                (void)AngleDelta(hold.facing, pick.bearing);
                if (pick.range >= stt.bestRange + win.slack)
                    goto next;
                hold.best        = stt.cand;
                stt.bestRange   = pick.range;
                bestBearing = pick.bearing;
                win.slack       = 0;
                goto next;
        } else {
            if (ObjIsTypeIn238(keep)) {

                ++*nAllied;
                if (!keep->targetUid)
                    goto next;
                {
                AM2_Object *w = LookupByUID(
                    keep->targetUid);
                stt.cand = w;
                if (!w) {
                    keep->targetUid = 0;
                    goto score;
                }
                if (w->health <= 0) {
                    keep->targetUid = 0;
                    goto next;
                }
                if ((int32_t)w->flags & 0x204) {
                    keep->targetUid = 0;
                    goto next;
                }
                if (ObjsAreAllied(o, w, 0))
                    goto next;
                DistAndAngle(&o->pos,
                             &w->pos,
                             &pick.range, &pick.bearing);
                }
                /* Only mode 1 adopts an ally's target, and only close by. */
                if (o->aiMode == 1
                    && pick.range > AM2_AI_PATROL_DETOUR)
                    goto next;
                if (!stt.cand)
                    goto next;
                goto score;
            }

            /* Not a unit: a dropped weapon worth walking to. */
            if (!flags || !ObjIsType4(keep))
                goto next;
            {
                /* A number win: two reaching definitions keep the address a web, as
                 * the image holds it (the third argument is not a temporary). */
                const AM2_Point *kp;

                if (flags)
                    kp = &keep->pos;
                else
                    kp = &keep->pos;
                DistAndAngle(&o->pos, kp, &pick.range, &pick.bearing);
            }
            /* Asked again here, as the image does (0x004041B0). */
            if (!keep)
                goto next;
            {
                int32_t inRange = RegionsNear(keep, o, hold.solve);

                hold.solve = 0;
                if (!inRange)
                    goto next;
            }
            {
                int32_t n = TryTakeWeapon(keep, o);

                if (n <= stt.bestScore)
                    goto next;
                stt.alt         = keep;
                stt.bestScore   = n;
                stt.bestRange   = pick.range;
                bestBearing = pick.bearing;
            }
        }
    next: ;
    }

finish:
    orig_log_one(o);
    if (!o->fieldF4)
        o->fieldEC = 0;

    /* The result starts at zero and takes `best`, or the fallback `alt`
     * (null when there is neither); the image returns it from two
     * epilogues. */
    result = 0;
    if (hold.best) {
        AM2_Point anchor;

        *outRange   = stt.bestRange;
        result      = hold.best;
        *outBearing = bestBearing;
        anchor      = ObjAnchorPoint(o);
        /* Overwrites the store above, and measures from the ANCHOR. */
        *outBearing = AngleBetween(&anchor, &hold.best->pos);
    } else if (stt.alt) {
        *outRange   = stt.bestRange;
        result      = stt.alt;
        *outBearing = bestBearing;
    }
    return result;
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
void __cdecl AiStepFollow(AM2_Object *o, AM2_SteerOut *out, AM2_AiCtx *c)
{
    o->scriptPoint = g_zeroPoint;

    /* Two tests, each ending in the same route call -- one block in the
     * image, which both reach. */
#define ROUTE() do { \
        o->dest = c->dest; \
        AiRouteToward(o, out, c, 0); \
    } while (0)
    if (c->leadRange > AM2_AI_FOLLOW_SLACK)
        ROUTE();
    else if (c->leader && PointsDiffer(c->leader->pos, c->leader->prevPos))
        ROUTE();
    else {
#undef ROUTE
        uint8_t hit = o->hitDir;

        if (hit > 0) {
            if (!c->observer)
                out->aim = hit;
            o->hitDir = 0;
        }

        AiPromoteFound(o, c);

        if (c->observer
            && (uint32_t)(g_gameClockMs - o->deadlineD0)
               >= AM2_AI_TURN_DELAY_MS)
            out->aim = c->bearing;
    }

    AiPromoteFound(o, c);
    ConsiderSighting(o, out, c);
}



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
void __cdecl AiStepAttack(AM2_Object *o, AM2_SteerOut *out, AM2_AiCtx *c)
{
    AiAttackBody(o, out, c);
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
void __cdecl AiStep(AM2_Object *o, AM2_SteerOut *out)
{
    AM2_AiCtx ctx;

    if (!o)
        return;

    AiBuildContext(o, &ctx);

    /* Case 2 before case 0: arm bodies are laid out in SOURCE order, and the
     * original's first arm calls the ignore step. Same jump table either way;
     * only the relink, which resolves call targets, could tell. */
    switch ((uint32_t)o->aiMode) {
    case 2:  AiStepIgnore(o, out, &ctx); break;
    case 0:  AiAttackBody(o, out, &ctx); break;
    case 3:  AiStepFollow(o, out, &ctx); break;
    case 6:  AiStepAttack(o, out, &ctx); break;
    case 7:  AiStepDefend(o, out, &ctx); break;
    default: AiStepTrack(o, out, &ctx);  break;
    }

    o->region = g_regionOfCell[o->tile];
}

typedef void (__cdecl *AM2_AiTrooperStepFn)(void *obj, void *out, void *ctx);

/* The same promote-and-engage block in the SIGHTC base rather than the SIGHT
 * one -- four bytes higher on every field. AiApproachLeader inlines it twice,
 * for the reason AM2_ROACH_PROMOTE_FOUND's comment already gives.
 *
 * GUARDED on FOUND being non-null: the original tests `[esi+0x20]` and jumps
 * past the whole block when it is zero (0x0040605A/0x0040609E), so a promote
 * with nothing found leaves the engage fields alone rather than dereferencing
 * NULL. Rallying with no unit in sight reaches it with FOUND null and read as
 * `found->OWNER` (+4) took the port down at 0x00741BFB. */
#define AM2_SIGHTC_PROMOTE_FOUND(o_, c_)                                      \
    do {                                                                      \
        AM2_Object *found_ = (c_)->found;                                     \
        if (found_) {                                                         \
            (o_)->targetUid = found_->uid;                                    \
            (c_)->observer  = (c_)->found;                                    \
            (c_)->range     = (c_)->foundRange;                               \
            (c_)->bearing   = (c_)->foundBearing;                             \
        }                                                                     \
    } while (0)

/* AiApproachLeader -- original 0x00405DB0, two callers: SargeAiStep and
 * TrooperAiStep, the two per-frame AI steps. So this is the arm they both
 * run, and what it decides is what to do about SIGHTC_OFF_LEADER -- the
 * object TrooperBuildContext resolved out of OBJ_OFF_FOLLOW_UID.
 *
 * IT IS NOT ONLY A LEADER. The first thing every arm does is ask ObjIsItem,
 * so the field can hold a thing to walk to and pick up as easily as a soldier
 * to follow, and the four arms below are the cross product of that question
 * with "are we allied with it".
 *
 * TWO HALVES ON A COUNTER. OBJ_OFF_FIELD_110 counts the frames on which
 * ArmyAlliedWithObj said no, and once it is positive the whole function
 * switches to a second, shorter set of arms. So an object that has been
 * un-allied even once behaves differently from then on -- the counter is
 * never reset here.
 *
 * FOUR DISTANCE BANDS, all on SIGHTC_OFF_LEAD_RANGE:
 *
 *   past AM2_AI_LEAD_FAR       walk, and stop thinking about it
 *   under AM2_AI_LEAD_NEAR     hand over to AiHitReact
 *   the two-way test in the middle decides between walking and turning: the
 *   range against the measured distance plus AM2_AI_LEAD_CLOSE, then both
 *   against AM2_AI_LEAD_SPREAD.
 *
 * THE PROMOTE BLOCK APPEARS TWICE, once inside the AiHitReact arm and once in
 * the shared tail. It is AM2_ROACH_PROMOTE_FOUND spelled in the SIGHTC base
 * rather than the SIGHT one, and it is written out at both sites for the
 * reason that macro's comment already gives: the original inlines it, and
 * "the original does it twice" is a fact about the original.
 *
 * IT IS ALSO WHAT IDENTIFIES SIGHTC_OFF_FOUND. That field was
 * SIGHTC_OFF_FIELD_20, "gates the turn in AiWalkStep"; here it is copied into
 * the OBSERVER/RANGE/BEARING triple with the found object's uid going to
 * OBJ_OFF_TARGET_UID, which is the same three-field promotion region.cpp
 * already writes from SIGHT_OFF_FOUND. The old name was right about what it
 * gates and could not say why.
 *
 * ITS FIRST ACT IS TO CLEAR OBJ_OFF_SCRIPT_STATE from ADDR_ZERO_POINT, which
 * makes it the FOURTH function seen to do that after Type2ActionB,
 * PointActionA and EnterVehicle. orig.h records that field as unresolved
 * because its writers put a POINT there and its readers compare an int32;
 * this is another writer of a zero and settles nothing.
 *
 * The two arms that turn rather than walk share one tail through a `jmp` into
 * the middle of the first: the turn is written in both and VC6 cross-jumps
 * the second into the first.
 *
 * THE HITREACT AND STEP PATHS ARE SIBLINGS OF ONE JOIN: AiKeepRange's and
 * AiTrooperStep's `add esp` is shared, so the walk/step block sits inside an
 * `if (0)` that only its labels enter, rather than behind a `goto promote`.
 * The leader is re-read from the context at every use, never cached.
 */
void __cdecl AiApproachLeader(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
#define HEAD_FOR() (o->dest = \
                    c->dest)
    int32_t  range;
    int32_t  d;

    o->scriptPoint = g_zeroPoint;

    if (!c->leader)
        goto hitreact;

    if (!ArmyAlliedWithObj(o->owner, c->leader, 1))
        o->field110 += 1;

    if (o->field110 <= 0) {
        /* ---- not provoked ---- */
        if (ObjIsItem(c->leader)
            && ArmyAlliedWithObj(o->owner, c->leader, 0)) {
            if (c->leadRange <= AM2_AI_LEAD_NEAR)
                goto hitreact;
            HEAD_FOR();
            AiTrooperStep(o, out, c);
            if (c->leadRange <= AM2_AI_LEAD_CLOSE
                && o->sight.state == 2)
                o->sight.state = 3;
            goto promote;
        }

        if (ObjsAreAllied(o, c->leader, 0)) {
            d = ApproxDist(&o->pos,
                           &c->leader->pos);
            range = c->leadRange;
            if (range > AM2_AI_LEAD_FAR)
                goto walk;
            if ((range > d + AM2_AI_LEAD_CLOSE && range > AM2_AI_LEAD_CLOSE)
                || (d > AM2_AI_LEAD_SPREAD && range > AM2_AI_LEAD_SPREAD)) {
                HEAD_FOR();
                goto step;
            }
            if (!PointsDiffer(c->leader->pos,
                              c->leader->prevPos))
                goto hitreact;
            HEAD_FOR();
            AiTrooperStep(o, out, c);
            if (out->state != 2 && out->state != 3) {
                out->bearing = AngleBetween(&o->pos,
                                    &c->dest);
                out->state = 2;
            }
            goto promote;
        }

        d = ApproxDist(&o->pos,
                       &c->leader->pos);
        if (d < AM2_AI_LEAD_CLOSE)
            goto hitreact;
        HEAD_FOR();
        goto step;
    }

    /* ---- provoked ---- */
    if (ObjIsItem(c->leader)
        && ArmyAlliedWithObj(o->owner, c->leader, 0)) {
        range = c->leadRange;
        if (range > AM2_AI_LEAD_FAR || range <= AM2_AI_LEAD_NEAR)
            goto hitreact;
        HEAD_FOR();
        goto step;
    }

    if (ObjsAreAllied(o, c->leader, 1)) {
        if (c->leadRange > AM2_AI_LEAD_FAR) {
            HEAD_FOR();
            goto step;
        }
        if (!PointsDiffer(c->leader->pos,
                          c->leader->prevPos))
            goto hitreact;
        HEAD_FOR();
        AiTrooperStep(o, out, c);
        if (out->state != 2 && out->state != 3) {
            out->bearing = AngleBetween(&o->pos,
                                &c->dest);
            out->state = 2;
        }
        goto promote;
    }

    d = ApproxDist(&o->pos,
                   &c->leader->pos);
    if (o->fieldEC)
        goto walk;
    if (d >= c->wantRange
        && c->leadRange > AM2_AI_LEAD_CLOSE)
        goto walk;

hitreact:
    AiHitReact(o, out, c);
    AM2_SIGHTC_PROMOTE_FOUND(o, c);
    AiKeepRange(o, out, c);
    if (0) {
        /* Entered only by its labels; see the header. */
walk:
        HEAD_FOR();
step:
        AiTrooperStep(o, out, c);
    }

promote:
    AM2_SIGHTC_PROMOTE_FOUND(o, c);
    ConsiderSightingC(o, out, c);
#undef HEAD_FOR
}

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
void __cdecl AiHitReact(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    int32_t        kind;

    /* The hit direction is read again where it is copied. */
    if (!o->hitDir)
        return;

    kind = o->soldierKind;
    if (kind == 7) {
        out->state = AM2_POSE_KIND7;
    } else if (kind != 8
               && c->kind != 3) {
        int32_t limit =
            g_rankRecords[o->rank].threshold;
        if ((int32_t)c->seed < (limit >> 1)) {
            int32_t slot = c->klass * 2
                           + (c->seed >= 0x80 ? 1 : 0);

            out->state = g_hitPoseByClass[slot];
        } else if ((int32_t)c->seed < limit) {
            out->state = AM2_POSE_HIT_HEAVY;
        }
    }

    if (!c->observer)
        out->bearing = o->hitDir;

    o->hitDir = 0;
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
void __cdecl AiKeepRange(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    /* The clock is re-read at every use, as the image does. */

    if (o->deadline58 > g_gameClockMs) {
        if (ApproxDist(&o->pos,
                       &o->dest)
            < AM2_AI_REACHED_DIST) {
            o->deadline58 = 0;
        } else {
            AiTrooperStep(o, out, c);
            return;
        }
    }

    if (!c->observer)
        return;

    if (o->soldierKind < 6
        && c->range
           <= c->wantRange) {
        if (o->deadline58 == 0)
            o->deadline58 = g_gameClockMs;

        if (g_gameClockMs - o->deadline58
            > AM2_AI_KEEP_RANGE_MS) {
            RandomPointToward(o,
                              c->observer,
                              c->wantRange,
                              &o->dest);
            AiTrooperStep(o, out, c);
            o->deadline58 = g_gameClockMs + AM2_AI_KEEP_RANGE_MS;
        } else if (!o->field540
                   && c->kind != 3
                   && c->seed < 0x10
                   && c->klass == 0) {
            if (c->seed < 4)
                out->state = 7;
            else
                out->state = 5;
        }
    }

    if (g_gameClockMs - o->deadlineD0
        >= AM2_AI_TURN_DELAY_MS)
        out->bearing = c->bearing;
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
void __cdecl AiWalkStep(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    if (c->destDist > AM2_AI_REACHED_DIST) {
        o->dest = o->scriptPoint;
        AiTrooperStep(o, out, c);
        return;
    }

    o->scriptPoint = g_zeroPoint;

    AiHitReact(o, out, c);

    if (c->found
        && g_gameClockMs
           - o->deadlineD0
           >= AM2_AI_TURN_DELAY_MS)
        out->bearing = c->bearing;
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
void __cdecl ConsiderSighting(AM2_Object *o, AM2_SteerOut *out,
                              const AM2_AiCtx *c)
{
    int32_t range;
    int32_t delta;

    if (!c->weapon)
        return;
    if (!c->ready)
        return;

    range = c->range;
    if (range <= 0 || range >= c->maxRange)
        return;

    delta = abs(AngleDelta(o->turretFacing, c->bearing));
    if ((uint8_t)delta > AM2_SIGHT_CONE)
        return;

    out->hit = 1;

    /* The observer is read from the record at every use. */
    if (!c->observer)
        return;

    out->spot.at.x   = c->observer->pos.x;
    out->spot.at.y   = c->observer->pos.y;
    out->spot.ground = c->observer->row0YAdjust;
    out->uid         = c->observer->uid;

    if (!ObjIsOurs(c->observer, 1))
        return;

    RevealObj(o);
    o->revealedUntil = g_gameClockMs + AM2_REVEAL_MS;
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
void __cdecl ConsiderSightingB(AM2_Object *o, AM2_RoachOut *out,
                               const AM2_RoachCtx *c)
{
    if (c->ready) {
        int32_t range = c->range;
        int32_t delta;

        if (range > 0
            && range < c->maxRange) {

            delta = abs(AngleDelta(out->heading, c->bearing));

            if ((uint8_t)delta < AM2_SIGHT_CONE_B) {
                /* The observer is read from the record at every use. */
                out->hit = 1;

                if (c->observer) {
                    out->spot.at.x = c->observer->pos.x;
                    out->spot.at.y = c->observer->pos.y;
                    out->spot.ground = c->observer->row0YAdjust;
                    out->uid = c->observer->uid;

                    if (ObjIsOurs(c->observer, 1)) {
                        RevealObj(o);
                        o->revealedUntil = g_gameClockMs + AM2_REVEAL_MS;
                    }
                }
            }
        }
    }

    if (out->hit) {
        out->heading = c->bearing;
        out->hit   = 0;
        out->state = AM2_SIGHTB_STATE_HIT;
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
void __cdecl ConsiderSightingC(AM2_Object *o, AM2_SightOut *out,
                               const AM2_SightCtx *c)
{
    /* The range cap is read from the record at every use. */

    if (c->weapon
        && c->ready) {
        int32_t range = c->range;

        if (range > 0 && range < c->maxRange) {
            int32_t delta =
                abs(AngleDelta(out->bearing, c->bearing));

            if ((uint8_t)delta < AM2_SIGHT_CONE_B
                || c->maxRange == AM2_SIGHT_OMNI_RANGE) {
                /* The observer is read from the record at every use. */
                out->hit = 1;

                if (c->observer) {
                    out->spot.at.x = c->observer->pos.x;
                    out->spot.at.y = c->observer->pos.y;
                    out->spot.ground = c->observer->row0YAdjust;
                    out->uid = c->observer->uid;

                    if (ObjIsOurs(c->observer, 1)
                        && c->maxRange != AM2_SIGHT_OMNI_RANGE) {
                        RevealObj(o);
                        o->revealedUntil = g_gameClockMs + AM2_REVEAL_MS;
                    }
                }
            }
        }
    }

    if (c->kind == 3
        && c->range > 0
        && c->range < c->maxRange
        && out->state == 2)
        out->state = 3;

    if (out->hit) {
        out->seen = 1;
        o->field578 = 1;
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
    /* The map size and both tables are read afresh at every use. */
    int32_t x, y, i;

    for (x = 0; x < g_mapTilesW; x++) {
        g_cellWeights[x]               = (int8_t)AM2_CELL_WEIGHT_STEP;
        g_cellWeights[(g_mapTilesH - 1) * g_mapTilesW + x] = (int8_t)AM2_CELL_WEIGHT_STEP;
    }

    for (y = 1; y < g_mapTilesH; y++) {
        g_cellWeights[g_mapTilesW * y - 1] = (int8_t)AM2_CELL_WEIGHT_STEP;
        g_cellWeights[g_mapTilesW * y]     = (int8_t)AM2_CELL_WEIGHT_STEP;
    }

    for (i = 0, y = 0; y < g_mapTilesH; y++) {
        for (x = 0; x < g_mapTilesW; i++, x++) {
            if (g_tileFlags[i] & AM2_TILE_OPEN)
                g_cellWeights[i] = (int8_t)AM2_CELL_WEIGHT_STEP;

            if (g_cellWeights[i] >= (int8_t)AM2_CELL_WEIGHT_STEP)
                g_tileFlags[i] |= AM2_TILE_BLOCKS;

            /* `h - AM2_EDGE_MARGIN` on BOTH axes -- see above. */
            if (!(y >= AM2_EDGE_MARGIN && y <= g_mapTilesH - AM2_EDGE_MARGIN
                  && x >= AM2_EDGE_MARGIN && x <= g_mapTilesH - AM2_EDGE_MARGIN))
                g_tileFlags[i] |= AM2_TILE_NEAR_EDGE;
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
void __cdecl UnrevealArea(int32_t army, AM2_Point at)
{
    /* The map's size is read from its globals at every use. */
    uint16_t tile;
    int32_t  x, y, x0, x1, y0, y1;

    if (army >= AM2_REVEAL_ARMIES)
        return;

    tile = (uint32_t)TileOfPoint(at) & 0xFFFFu;
    x    = (int32_t)(tile & (uint32_t)(g_mapTilesW - 1));
    y    = (int32_t)(tile >> g_mapRowShift);

    x0 = Clamp(x - AM2_REVEAL_RADIUS, 0, g_mapTilesW - 1);
    x1 = Clamp(x + AM2_REVEAL_RADIUS, 0, g_mapTilesW - 1);
    y0 = Clamp(y - AM2_REVEAL_RADIUS, 0, g_mapTilesH - 1);
    y1 = Clamp(y + AM2_REVEAL_RADIUS, 0, g_mapTilesH - 1);

    {
        int32_t slot;

        for (slot = 0; slot < AM2_REVEAL_ARMIES; slot++) {
            int32_t at_i;
            int32_t rows;

            if (!ArmiesAllied(army, slot))
                continue;

            at_i = g_mapTilesH * y0 + x0;     /* height for the corner; see above */

            if (y0 > y1)
                continue;

            /* Both counts are at least one once the bounds are checked, so
             * both loops test at the bottom; the grid is re-read per cell. */
            rows = y1 - y0 + 1;
            do {
                if (x0 <= x1) {
                    int32_t cols = x1 - x0 + 1;

                    do {
                        g_tileRevealGrids[slot]
                            [(uint32_t)at_i & 0xFFFFu]--;
                        at_i++;
                    } while (--cols);
                }

                at_i += g_mapTilesW - x1 + x0 - 1;   /* width for the stride */
            } while (--rows);
        }
    }
}

/* The straight-line distance between two regions, in the units ApproxDistXY
 * answers -- the same call the search makes for both its step cost and its
 * heuristic. The map's width is a power of two and ADDR_MAP_ROW_SHIFT is its
 * log, so x and y come out of the tile index by a mask and a shift, written
 * as one expression the way the original computes it, inline, three times
 * in RegionFindPath. */
#define RS_TILE(r) ((uint32_t)(r)->tile)
/* A macro, not an inline function: as a function its parameters are two
 * more variables to VC6 (and as references they crash its non-relink
 * compile), and the image has none. */
#define RegionSpan(a, b)                                                     \
    ApproxDistXY((int32_t)((RS_TILE(b) & ((uint32_t)(g_mapTilesW - 1))) - (RS_TILE(a) & ((uint32_t)(g_mapTilesW - 1)))), \
                 (int32_t)((RS_TILE(b) >> (uint16_t)*(const uint32_t *)&g_mapRowShift) - (RS_TILE(a) >> (uint16_t)*(const uint32_t *)&g_mapRowShift)))

static inline AM2_Region *RegionAt(int32_t id)
{
    return &g_regions[id];
}

/* RegionFindPath -- original 0x00437E70, 1,168 bytes, one caller, and that
 * caller is RegionSolvePair below.
 *
 * A*, with the region graph as its nodes: an open list kept sorted by g+h, a
 * generation stamp instead of a visited set, and the answer written backwards
 * from the goal through each node's parent. Answers 1 with the path in the
 * caller's int16 array and its length through the fourth argument, 0 when
 * there is none.
 *
 * THE WORKING SET LIVES IN THE REGION RECORDS, not in a side structure --
 * REGION_OFF_G through REGION_OFF_NEXT, eight fields that are rewritten by
 * every search and mean nothing unless REGION_OFF_STAMP matches
 * ADDR_REGION_GENERATION. That is what makes the whole thing allocation-free
 * and what makes it non-reentrant.
 *
 * THE HEURISTIC IS WEIGHTED BY 1.5, WHICH MAKES THIS INADMISSIBLE. h is
 * `ApproxDistXY * 1.5` truncated, while a step costs `ApproxDistXY * 2`, so h
 * can exceed the true remaining cost and the first path found is not
 * guaranteed shortest.
 *
 * AND THE OPEN LIST'S UNLINK DROPS THE REST OF THE LIST. Improving a node that
 * is at the HEAD writes `openHead = NULL` where it must write
 * `openHead = node->next`, so every other open node is orphaned and the
 * insertion below then finds an empty list. That is the original's, checked in
 * the bytes -- 0x00438135 stores the zero register into ADDR_REGION_OPEN_HEAD
 * -- and reproduced. tools/pathcheck.py was written with a CORRECT unlink in
 * its model and disagreed with the original on four graphs, which is how it
 * was found; a defect this size is invisible to any drive, because a
 * pathfinder that drops candidates still returns a path. Reproduced; it is the ordinary game-AI trade, and the
 * two constants are in orig.h rather than inline so that the ratio is visible.
 *
 * The 1.5 comes out of the image as a pooled double, and that address used to
 * be called AM2_KIND7_HEALTH_SCALE because kind 7's health multiplied by it
 * first. It is AM2_CONST_1_5 now: the linker folds equal literals, so a name
 * taken from one use site is one more use away from being wrong.
 *
 * A CLOSED NODE IS NEVER REOPENED, which follows from the weighting rather
 * than contradicting it: the state byte is tested for 1 and then for 2, and
 * the 2 arm goes straight to the next link. With an inadmissible h that can
 * settle a node too early, and the original accepts it.
 *
 * A NODE ALREADY OPEN AND IMPROVED DOES NOT GET ITS h RECOMPUTED, and that is
 * a difference without a distinction. The fresh arm writes g, h, parent, depth
 * and the stamp; the improvement arm writes g, parent and depth. It reads as a
 * deliberate asymmetry and it cannot matter: h is a function of the node and
 * the GOAL, both fixed for the length of a search, so the value the fresh arm
 * stored is the value a recomputation would produce. tools/pathcheck.py
 * confirms it -- making the model recompute h changes no case, where every
 * other mutation of this function changes several.
 *
 * THE RESUME ARM CANNOT RUN. The function opens by comparing
 * ADDR_REGION_SEARCH_STATE against -1 and taking a second entry point when it
 * differs -- restoring a saved node and goal id, as an A* spread over frames
 * would. Nothing in the image ever stores anything but -1 there, so that arm
 * is dead. It is transcribed anyway, for the same reason the copy-protection
 * branches are reproduced: "the original has a resume path" is a different
 * fact from "the original has none".
 *
 * TWO GUARDS AND THEY ARE ASYMMETRIC IN A WAY THE ARGUMENT ORDER HIDES. `to`
 * is checked for REGION_OFF_ACTIVE first, then `from` -- and RegionSolvePair
 * has already checked `to` itself, so that one is doubled and `from`'s is the
 * only one that can fire here.
 *
 * The links are SIX bytes apart and only the first int16 of each is read, so
 * the other four are something this function does not use. `[esp+0x10]` counts
 * every edge considered and is never read; both are reproduced.
 *
 * A path longer than AM2_REGION_DEPTH_MAX answers 0 -- "no path" -- rather
 * than a truncated one, which is the right way round for a caller that writes
 * the answer into a fixed matrix.
 *
 * NO DRIVE THIS PROJECT HAS REACHES IT, and that is probed rather than
 * inferred: its counter is blind, so an am2_log here with a CONTROL at
 * ObjAfterMove's top says the control fires 1,598 times on a live Boot Camp
 * mission and this function fires ZERO. So tools/ab.sh is not evidence about
 * this code in either direction, which is what tools/pathcheck.py exists for
 * -- it emulates the ORIGINAL over fourteen seeded graphs and compares the
 * return value, the length and every path entry.
 *
 * That oracle earned itself twice on its first two runs, once against the
 * model and once against this file: the head-unlink defect above, and
 * ApproxDistXY, whose behaviour dist.cpp's prose had rounded the wrong way.
 */
int32_t __cdecl RegionFindPath(int32_t from, int32_t to, int16_t *path,
                               int32_t *len)
{
/* The search's working pointers are GLOBALS and the original goes through
 * them at every use -- every field store below is a reload of the global
 * in the image. */
    int32_t  edges = 0;
    int32_t  i;
    int32_t  g;
    int32_t  h;

    if (!RegionAt(to)->active)
        goto nopath;
    if (!RegionAt(from)->active)
        return 0;

    if (g_regionSearchState == -1) {
        g_regionGeneration += 1;
        g_regionGoal = RegionAt(to);
        g_regionOpenHead = RegionAt(from);

        g_regionOpenHead->g = 0;
        /* _ftol in the original; a C cast truncates toward zero the same
         * way and the value cannot be negative here. */
        g_regionOpenHead->h = (int32_t)
            ((double)RegionSpan(g_regionOpenHead, g_regionGoal)
             * *(const double *)(uintptr_t)AM2_CONST_1_5);
        g_regionOpenHead->depth = 0;
        g_regionOpenHead->stamp = (uint16_t)g_regionGeneration;
        g_regionOpenHead->state  = AM2_REGION_STATE_OPEN;
        g_regionOpenHead->prev   = (AM2_Region *)0;
        g_regionOpenHead->next   = (AM2_Region *)0;
        g_regionOpenHead->parent = (AM2_Region *)0;
    } else {
        /* Dead -- see above. Written out because the original has it. */
        g_regionOpenHead = g_regionResumeNode;
        to   = g_regionResumeGoalid;
        g_regionSearchState = -1;
    }

    while (1) {
        if (!g_regionOpenHead)
            return 0;

        g_regionCurrent  = g_regionOpenHead;
        /* Through CUR, just loaded from the head: the same node, and the
         * read the image makes. */
        g_regionOpenHead = g_regionCurrent->next;
        if (g_regionOpenHead)
            g_regionOpenHead->prev = (AM2_Region *)0;

        g_regionCurrent->state = AM2_REGION_STATE_CLOSED;

        if (g_regionCurrent->id == to)
            break;

        /* The link count and table are read through the region's ID, not
         * through CUR -- the same record, reached the long way. */
        for (i = 0;
             i < RegionAt(g_regionCurrent->id)->nlinks;
             i++) {
            edges++;

            if (!RegionAt(RegionAt(g_regionCurrent->id)->links[i].to)->active)
                continue;

            g_regionNeighbour = RegionAt(RegionAt(g_regionCurrent->id)->links[i].to);

            g = g_regionCurrent->g
                + RegionSpan(g_regionCurrent, g_regionNeighbour) * AM2_REGION_STEP_WEIGHT;
            h = (int32_t)((double)RegionSpan(g_regionNeighbour, g_regionGoal)
                          * *(const double *)(uintptr_t)AM2_CONST_1_5);

            /* The stamp is widened, not GEN narrowed: past 0xFFFF searches
             * no stamp matches and every node looks fresh. */
            if (g_regionNeighbour->stamp == g_regionGeneration) {
                if (g_regionNeighbour->state
                    & AM2_REGION_STATE_OPEN) {
                    if (g >= g_regionNeighbour->g)
                        continue;

                    /* h is NOT rewritten here -- see above. */
                    g_regionNeighbour->parent = g_regionCurrent;
                    g_regionNeighbour->g      = g;
                    g_regionNeighbour->depth  = g_regionCurrent->depth + 1;

                    /* Unlink, so the insertion below can place it again -- and
                     * at the head it drops the whole list (see above). */
                    if (g_regionNeighbour->prev)
                        g_regionNeighbour->prev->next =
                            g_regionNeighbour->next;
                    else
                        g_regionOpenHead = (AM2_Region *)0;
                    if (g_regionNeighbour->next)
                        g_regionNeighbour->next->prev =
                            g_regionNeighbour->prev;
                } else if (g_regionNeighbour->state
                           & AM2_REGION_STATE_CLOSED) {
                    continue;
                }
            } else {
                g_regionNeighbour->stamp = (uint16_t)g_regionGeneration;
                g_regionNeighbour->g        = g;
                g_regionNeighbour->h        = h;
                g_regionNeighbour->parent   = g_regionCurrent;
                g_regionNeighbour->depth    = g_regionCurrent->depth + 1;
                g_regionNeighbour->state = AM2_REGION_STATE_OPEN;
            }

            /* Insert NB into the open list, sorted by g+h ascending. */
            if (!g_regionOpenHead) {
                g_regionOpenHead = g_regionNeighbour;
                g_regionNeighbour->next   = (AM2_Region *)0;
                g_regionOpenHead->prev = (AM2_Region *)0;
                continue;
            }

            for (g_regionWalk = g_regionOpenHead,
                 g_regionInsertPrev = (AM2_Region *)0; g_regionWalk;
                 g_regionWalk = g_regionWalk->next) {
                if (g_regionWalk->h + g_regionWalk->g
                    >= g_regionNeighbour->h + g_regionNeighbour->g)
                    break;
                g_regionInsertPrev = g_regionWalk;
            }

            if (g_regionInsertPrev) {
                g_regionNeighbour->next = g_regionInsertPrev->next;
                if (g_regionNeighbour->next)
                    g_regionNeighbour->next->prev = g_regionNeighbour;
                g_regionInsertPrev->next = g_regionNeighbour;
                g_regionNeighbour->prev   = g_regionInsertPrev;
            } else {
                g_regionNeighbour->next   = g_regionOpenHead;
                g_regionNeighbour->prev   = (AM2_Region *)0;
                g_regionOpenHead->prev = g_regionNeighbour;
                g_regionOpenHead = g_regionNeighbour;
            }
        }
    }

    /* Found: CUR is the goal. */
    g_regionWalk = g_regionCurrent;
    if (g_regionCurrent->depth >= AM2_REGION_DEPTH_MAX)
        return 0;

    *len = g_regionCurrent->depth + 1;

    /* Indexed, not a slot pointer: the image forms the address only once
     * the walk is known non-null. */
    for (i = g_regionCurrent->depth; g_regionWalk;
         g_regionWalk = g_regionWalk->parent)
        path[i--] = g_regionWalk->id;

    (void)edges;
    return 1;
nopath:
    return 0;
}
#undef RegionSpan
#undef RS_TILE


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
    int32_t len;
    int16_t path[AM2_REGION_PATH_MAX];
    int32_t i, j;

    /* Every table and the stride are read from their globals at each
     * store. */
    if (!g_regions[to].active)
        return;

    if (!RegionFindPath(from, to, path, &len)) {
        (g_regionNext + from * g_regionStride)[to] = 0;
        (g_regionNext + to * g_regionStride)[from] = 0;

        /* Both of these are cost[from][to]; the original writes it twice. */
        (g_regionCost + from * g_regionStride)[to] = g_regionStamp;
        (g_regionCost + from * g_regionStride)[to] = g_regionStamp;
        return;
    }

    for (i = 0; i < len - 1; i++)
        for (j = i + 1; j < len; j++) {
            (g_regionNext + path[i] * g_regionStride)[path[j]] = (uint8_t)path[i + 1];
            (g_regionCost + path[i] * g_regionStride)[path[j]] = g_regionStamp;
        }

    /* Walked from the hop below i downwards; the same cells as an upward
     * walk and the same values in each. */
    for (i = len - 1; i > 0; i--)
        for (j = i - 1; j >= 0; j--) {
            (g_regionNext + path[i] * g_regionStride)[path[j]] = (uint8_t)path[i - 1];
            (g_regionCost + path[i] * g_regionStride)[path[j]] = g_regionStamp;
        }
}


/* The three arms and the context builder these two dispatchers share with
 * nothing else, all left original and reached by address. None is patched, so
 * naming the address here is a seam checkseams allows. */
typedef void (__cdecl *AM2_AiArmFn)(void *obj, void *out, void *ctx);
typedef void (__cdecl *AM2_AiFillFn)(void *obj, void *ctx, int32_t sarge);
/* Arm 3 is AiApproachLeader, reconstructed above and called by name. */

/* THE REACT AND THE DISPATCH ARE SPLIT because the trooper has a step BETWEEN
 * them that Sarge does not, and folding them into one helper dropped it. Kind 7
 * reacts to being hit and can finish the step outright -- AM2_POSE_KIND7 in the
 * output state means there is nothing further to decide this frame -- while
 * kind 8 reacts and carries on; every other kind skips the reaction. The
 * shortcut (kind 7, POSE_KIND7) skips the DISPATCH and the trooper's BOARD arm
 * below, but not the region write or the 0x540 tail: in the original it is a
 * `jmp` to the region write, which is where the dispatch arms fall to anyway.
 *
 * AiStepReact returns 1 when the shortcut was taken, so the caller skips both
 * the dispatch and -- for the trooper -- the board. An earlier version made
 * this ONE void helper doing react+dispatch, on the belief the middle was
 * shared "to the instruction"; it is not. TrooperAiStep (0x004062B0) boards a
 * claimed vehicle between the react (AiHitReact) and the dispatch, and
 * SargeAiStep (0x00407020) has no such arm -- the react is followed straight
 * by the AI_MODE switch. Bundling the two hid the board, so a non-Sarge unit
 * only boarded later through UpdateTrooperAction's tighter blocked-by-rect
 * path, one pump behind the original. Found by a live side-by-side trace of
 * EnterVehicle's caller: the original's second boarder returned to
 * TrooperAiStep+0x77, a call the port did not have. */
static inline int32_t AiStepReact(AM2_Object *o, AM2_SightOut *out,
                                  AM2_SightCtx *c)
{
    if (o->soldierKind == 7) {
        AiHitReact(o, out, c);
        if (out->state == AM2_POSE_KIND7)
            return 1;
    } else if (o->soldierKind == 8) {
        AiHitReact(o, out, c);
    }
    return 0;
}

/* CASE 6 IS THE ONE PLACE SARGE AND A TROOPER DIVERGE. The original has two
 * separate jump tables -- SargeAiStep's at 0x0040716C and TrooperAiStep's at
 * 0x0040643C -- and diffing them byte for byte they agree on every arm but
 * AI_MODE 6 (attack): Sarge's calls AiEngageStep (0x00406B30), a trooper's
 * goes through the thunk 0x00405D10 to AiPatrolStep (0x004057D0). Merging them
 * into one dispatch and always taking AiEngageStep was Sarge's behaviour
 * applied to every trooper -- and since AiPatrolStep is what copies
 * OBJ_OFF_SCRIPT_STATE into OBJ_OFF_SCRIPT_ID (the AI saved-point) at its head
 * while AiEngageStep never touches 0xb0, an aimode-6 enemy trooper lost that
 * save at spawn and then made a different pose/facing decision. Found by a gdb
 * watchpoint on the object's 0xb0 in the hybrid: the write landed in
 * AiPatrolStep, reached from TrooperAiStep's AI_MODE-6 arm. */
/* Inlined into both dispatchers, whose arms are laid out 2, 0, 3, 6, 7,
 * default -- the source order below. */
static inline void AiStepDispatch(AM2_Object *o, AM2_SightOut *out,
                                  AM2_SightCtx *c, int32_t sarge)
{
    switch (o->aiMode) {
    case 2:  AiWalkStep(o, out, c);       break;
    case 0:  AiPatrolStep(o, out, c);     break;
    case 3:  AiApproachLeader(o, out, c); break;
    case 6:  if (sarge)
                 AiEngageStep(o, out, c);
             else                   /* through the thunk, as the image does */
                 Call4057D0(o, out, c);
             break;
    case 7:  Call405220(o, out, c);       break;
    /* 1, 4, 5 and everything above 7 -- `evade` is 5 and takes this arm. */
    default: AiGuardStep(o, out, c);      break;
    }
}

/* Sarge's middle: react, then -- unless the shortcut was taken -- dispatch. No
 * board arm; that one is the trooper's, in TrooperAiStep below. Sarge's table
 * is the one whose AI_MODE 6 is AiEngageStep, so this passes sarge=1. */
static inline void AiStepReactAndDispatch(AM2_Object *o, AM2_SightOut *out,
                                          AM2_SightCtx *c)
{
    if (!AiStepReact(o, out, c))
        AiStepDispatch(o, out, c, 1);
}

/* Where the unit is standing, recorded on the object every frame. Both
 * dispatchers end with this, and the kind-7 shortcut above jumps straight to
 * it -- so the region is updated even on the frame the AI decides nothing. */
static inline void AiStepRecordRegion(AM2_Object *o)
{
    o->region = g_regionOfCell[o->tile];
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
 * candidate the fill left at SIGHTC_OFF_FOUND, and if ObjIsType4 says it is
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
void __cdecl SargeAiStep(AM2_Object *o, AM2_SightOut *out)
{
    AM2_SightCtx ctx;

    if (!o)
        return;

    SelectBestWeapon(o);
    TrooperBuildContext(o, &ctx, 1);
    UnitWeaponInfo(o, &ctx);

    /* The found item is read from the context at each use. */
    if (ctx.found && ObjIsType4(ctx.found)) {
        o->goalPoint   = ctx.found->pos;
        o->pickupAfter = ctx.found->uid;

        SettlePointInRegion(TileOfPoint(o->goalPoint), &o->goalPoint);

        ctx.destDistB = ApproxDist(&o->pos, &o->goalPoint);

        ctx.found = 0;
    }

    AiStepReactAndDispatch(o, out, &ctx);
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
void __cdecl TrooperAiStep(AM2_Object *o, AM2_SightOut *out)
{
    AM2_SightCtx ctx;
    int32_t      c0;

    if (!o)
        return;

    TrooperBuildContext(o, &ctx, 0);

    if (!AiStepReact(o, out, &ctx)) {
        /* BOARD, before the AI dispatch: a claimed vehicle within
         * AM2_BOARD_NEAR is boarded here (0x00406311..0x0040632f), and
         * boarding RETURNS -- skipping the dispatch, the region write and the
         * 0x540 tail. This is the arm SargeAiStep does not have; folding
         * react+dispatch into one helper had dropped it, so a non-Sarge unit
         * boarded only later through UpdateTrooperAction's blocked-by-rect
         * path, one pump behind the original. TrooperBuildContext fills
         * SIGHTC_OFF_VEHICLE and SIGHTC_OFF_VEHICLE_DIST. */
        AM2_Object *veh = ctx.vehicle;

        if (veh && ctx.vehicleDist < AM2_BOARD_NEAR) {
            EnterVehicle(o, veh);
            return;
        }
        AiStepDispatch(o, out, &ctx, 0);
    }
    AiStepRecordRegion(o);

    c0 = ctx.klass;

    /* Laid out 3, 2, 1, each arm an if/else with the else placed last --
     * and arm 2 tests `c0 != 1` before `c0 == 0`, redundantly, as the image
     * does. */
    switch (o->field540) {
    case 3:
        if (c0 != 2)
            out->state = 7;
        else
            out->state = 6;
        break;
    case 2:
        if (c0 != 1 && c0 == 0)
            out->state = 5;
        else
            out->state = 4;
        break;
    case 1:
        if (c0 != 0) {
            if (c0 == 2)
                out->state = 9;
            else
                out->state = 8;
        } else {
            out->state = 1;
        }
        break;
    }
}


typedef int32_t  (__cdecl *AM2_ScanFn)(void *obj, void *range, void *bearing,
                                       void *a, void *b, int32_t z);

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
static inline AM2_Object *SightResolve(uint32_t *uid, uint32_t *dead_clears,
                                       AM2_Object **slot)
{
    AM2_Object *t;

    if (!*uid)
        return NULL;

    t = LookupByUID(*uid);
    *slot = t;

    if (!t) {
        *uid = 0;
        return NULL;
    }
    if (t->flags & AM2_SIGHT_DROP) {
        *uid  = 0;
        *slot = NULL;
        return NULL;
    }
    if (t->health == 0) {
        *dead_clears = 0;
        *slot        = NULL;
        return NULL;
    }
    return t;
}

/* AiBuildContext -- original 0x00407D70, one caller: the AI mode
 * dispatcher at 0x00407F80, whose `sub esp, 0x44` is this record's LENGTH.
 * Fill the sight record every mode arm below then reads -- the leader and the
 * way to them, the target and the way to it, whatever ADDR_SIGHT_SCAN has in
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
 * THE TWIN IS LESS ALIKE THAN IT LOOKS, but LESS DIFFERENT than a first
 * reading made it. It fills 0x40 bytes to this one's 0x44 and writes fixed
 * ranges where this reads the weapon -- 48 and 70, which are this function's
 * own 0.75 and 1.1 over a default range of 64. What it does NOT differ on is
 * the destination: BOTH resolve a formation point for an allied 2/3/8 leader
 * and copy the raw position otherwise (this at 0x00407DE6, the twin at
 * 0x00408?..), and the only difference there is ObjsAreAllied's third
 * argument -- 0 here, 1 in the twin. An earlier note called the formation a
 * difference and had this arm copy the position unconditionally; a follower
 * ordered into formation then stood on the leader instead of in its slot.
 *
 * A ZERO-HEALTH LEADER CLEARS THE TARGET UID rather than the follow uid its
 * two neighbouring arms clear. Both builders do it, so it is the original's
 * behaviour; see SightResolve.
 *
 * Nothing drives this yet -- its dispatcher's arms are reconstructed, so the
 * counter cannot move, and tools/blindspots.py will say so.
 */
void __cdecl AiBuildContext(AM2_Object *o, AM2_AiCtx *s)
{
    AM2_Point   anchor;
    AM2_Object *leader, *target, *w;

    if (!o)
        return;

    anchor = ObjAnchorPoint(o);
    memset(s, 0, sizeof *s);

    leader = SightResolve(&o->followUid, &o->targetUid, &s->leader);
    if (leader) {
        /* A formation point when the leader is an allied 2/3/8, its raw
         * position otherwise -- the same choice the twin makes (0x00407DE6),
         * differing only in ObjsAreAllied's third argument, 0 here against the
         * twin's 1. An earlier reading had this arm copy the leader's position
         * unconditionally, which left a follower ordered into formation
         * standing where the leader is rather than in its slot. Past the test
         * the leader is re-read from its slot in the record. */
        if (ObjsAreAllied(o, leader, 0) && ObjIsTypeIn238(s->leader))
            ResolveFormationPoint(o, s->leader, &s->dest);
        else
            s->dest = s->leader->pos;
        DistAndAngle(&anchor, &s->dest, &s->leadRange, &s->leadBearing);
    }

    target = SightResolve(&o->targetUid, &o->targetUid, &s->observer);
    if (target)
        DistAndAngle(&anchor, &target->pos, &s->range, &s->bearing);

    if (g_gameClockMs >= (uint32_t)o->fieldFC)
        s->found = SightScan(o, &s->foundRange, &s->foundBearing,
                             &o->field114, &o->field110, 0);

    if ((uint16_t)o->scriptPoint.x)
        s->destDist = ApproxDist(&o->pos, &o->scriptPoint);

    w = WeaponByUid(o->weaponUid);
    s->weapon = w;

    if (!w) {
        s->kind = 0;
    } else {
        /* The weapon's type record is re-read at every use, and so is the
         * range: no local holds either. */
        s->kind      = w->typeRec->kind;
        s->wantRange = (int32_t)((double)w->typeRec->range * g_sightRangeWant);
        s->maxRange  = (int32_t)((double)w->typeRec->range * g_weaponRangeHi);
        s->ready     = (g_gameClockMs - w->lastUse) > w->typeRec->cooldown;
    }

    s->seed = (uint8_t)orig_region_rand();
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
void __cdecl RoachBuildContext(AM2_Object *o, AM2_RoachCtx *s)
{
    AM2_Object *leader, *target;

    if (!o)
        return;

    memset(s, 0, sizeof *s);

    leader = SightResolve(&o->followUid, &o->targetUid, &s->leader);
    if (leader) {
        /* Past the test the leader is re-read from its slot in the record. */
        if (ObjIsTypeIn238(leader))
            ResolveFormationPoint(o, s->leader, &s->dest);
        else
            s->dest = s->leader->pos;

        DistAndAngle(&o->pos, &s->dest, &s->leadRange, &s->leadBearing);
    }

    target = SightResolve(&o->targetUid, &o->targetUid, &s->observer);
    if (target) {
        DistAndAngle(&o->pos, &target->pos, &s->range, &s->bearing);

        if (s->bearing != AngleBetween(&o->pos, &s->observer->pos))
            orig_log("Bad!\n");
    }

    if (g_gameClockMs >= (uint32_t)o->fieldFC)
        s->found = SightScan(o, &s->foundRange, &s->foundBearing,
                             &o->field114, &o->field110, 0);

    if ((uint16_t)o->scriptPoint.x)
        s->destDist = ApproxDist(&o->pos, &o->scriptPoint);

    s->weapon    = NULL;
    s->wantRange = AM2_ROACH_WANT_RANGE;
    s->maxRange  = AM2_ROACH_MAX_RANGE;
    s->ready     = (g_gameClockMs - o->deadline58) > AM2_ROACH_READY_MS;

    s->seed = (uint8_t)orig_region_rand();
}



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
void __cdecl AiStepAttach(AM2_Object *o, AM2_SightOut *out)
{
    AM2_SightCtx ctx;

    if (!o)
        return;

    TrooperBuildContext(o, &ctx, 0);

    if (ctx.destDist > AM2_AI_REACHED_DIST) {
        o->dest = o->scriptPoint;
        AiTrooperStep(o, out, &ctx);
    } else {
        int32_t r;

        o->scriptPoint = g_zeroPoint;

        r = orig_region_rand();

        if (!(ctx.leader
              && ctx.leadRange <= AM2_AI_ATTACH_RANGE
              && (uint8_t)r)) {
            int32_t army = (r & 1) ? (int32_t)o->owner
                                   : (r >> 1) % 4;
            /* The list is re-read from the table after the rand() call. */
            if (g_armyObjLists[army]->count > 0) {
                /* The modulus is taken from `army`'s count and the array from
                 * the object's OWN army. See the header: reproduced. */
                int32_t     idx = orig_region_rand()
                                  % g_armyObjLists[army]->count;
                AM2_Object *t   = LookupByUID(
                    g_armyObjLists
                        [o->owner]->uids[idx]);

                if (t && ApproxDist(&o->pos, &t->pos) < AM2_AI_ATTACH_RANGE)
                    ObjAttachTo(o, t);
            }
        }
    }

    if ((g_gameClockMs - o->deadline58) > AM2_AI_IDLE_TIMEOUT_MS
        && ctx.destDist < AM2_AI_REACHED_DIST) {
        int32_t     army = o->field5A4 - 1;
        uint32_t    from = 0;
        AM2_Object *rec  = LookupOwnerObj((uint32_t)army);

        if (rec)
            from = rec->uid;

        if (g_mpSession
            && !AM2_THE_COMM->CommMustBroadcast((int16_t)army)) {
            DamageBroadcast(o, from, AM2_AI_TIMEOUT_DAMAGE, 3, &o->pos, 0);
            DamageObject(o, AM2_AI_TIMEOUT_DAMAGE, 3, from, 0, 1);
        } else {
            DamageObject(o, AM2_AI_TIMEOUT_DAMAGE, 3, from, 0, 0);
        }
    } else {
        o->dest = ctx.dest;
        AiTrooperStep(o, out, &ctx);

        if (g_gameClockMs - o->deadline58 > AM2_AI_STATE_MS)
            out->state = AM2_AI_STATE_STALE;
        else
            out->state = AM2_AI_STATE_RECENT;
    }

    o->region = g_regionOfCell[o->tile];
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
 * ITS LAST ACT NAMES A FIELD. `ctx[SIGHTC_OFF_SEED] = orig_step3_rand()` makes that
 * byte a fresh roll per build, so AiHitReact comparing it against 4 and 0x10
 * is a probability gate and not a threshold on anything measured.
 *
 * Every caller is ours -- SargeAiStep, TrooperAiStep and AiStepAttach all
 * reached it through an orig_ai_fill seam until now -- so its counter cannot
 * move and tools/blindspots.py will say so.
 */
void __cdecl TrooperBuildContext(AM2_Object *o, AM2_SightCtx *c, int32_t sarge)
{
    AM2_Point   anchor;
    AM2_Object *L, *T, *V;

    if (!o)
        return;

    anchor = ObjAnchorPoint(o);
    memset(c, 0, sizeof *c);
    c->klass = ClassifyByCode74(o);

    if (o->followUid) {
        L = LookupByUID(o->followUid);
        c->leader = L;

        if (!L) {
            o->followUid = 0;
        } else {
            uint32_t f    = L->flags;
            int32_t  drop = 0;

            if (f & OBJ_FLAG_CONCEALED)
                drop = 1;
            else if (L->health == 0)
                drop = 1;
            else if (f & OBJ_FLAG_DESTROYED) {
                if (ObjIsType2(L) && !c->leader->riding)
                    drop = 1;
                else if (ObjIsType3(c->leader))
                    drop = 1;
            }
            if (drop) {
                o->followUid = 0;
                c->leader    = NULL;
            }

            /* Inside the else: a leader that never resolved jumps straight
             * past this, as the image's `jmp` over it does. The leader is
             * read from the context at every use, not held. */
            if (c->leader) {
                if (ObjsAreAllied(o, c->leader, 1)
                    && ObjIsTypeIn238(c->leader)) {
                    ResolveFormationPoint(o, c->leader, &c->dest);
                } else {
                    c->dest      = c->leader->pos;
                    o->targetUid = c->leader->uid;
                }
                DistAndAngle(&anchor, &c->dest, &c->leadRange,
                             &c->leadBearing);
            }
        }
    }

    if (o->targetUid) {
        T = LookupByUID(o->targetUid);
        c->observer = T;

        if (!T) {
            o->targetUid = 0;
        } else if (T->flags & AM2_SIGHT_DROP) {
            o->targetUid = 0;
            c->observer  = NULL;
        } else if (T->health == 0) {
            o->targetUid = 0;
            c->observer  = NULL;
        } else if (ObjIsItem(T) && c->observer->health < 0) {
            o->targetUid = 0;
            c->observer  = NULL;
        } else if (ObjIsType4(c->observer)) {
            o->targetUid = 0;
            c->observer  = NULL;
        } else {
            const AM2_RankRec *rank;

            DistAndAngle(&anchor, &c->observer->pos, &c->range, &c->bearing);

            rank = &g_rankRecords[o->rank];

            if (c->range > (rank->sight_range << 1))
                o->targetUid = 0;
        }
    }

    if (o->uid56C) {
        V = LookupType3ByUID(o->uid56C);
        c->vehicle = V;

        if (!V)
            o->uid56C = 0;
        else
            c->vehicleDist = ApproxDist(&o->pos, &V->pos);
    }

    if (g_gameClockMs >= (uint32_t)o->fieldFC)
        /* SightScan's sixth argument is the SARGE flag, ARG3 of this function
         * (espmap: `[esp+0x1c]` at 0x00404942 = ARG3, one slot past the anchor
         * at [esp+0x18]). It gates the weapon arm and the hittable-vs-live
         * predicate, so only Sarge scans for weapons to pick up. Passing
         * `anchor` (a packed point, always non-zero) made every follower target
         * and fire at nearby weapons when no enemy was in sight. */
        c->found = SightScan(o, &c->foundRange, &c->foundBearing,
                             &o->field114, &o->field110, sarge);

    if ((uint16_t)o->scriptPoint.x)
        c->destDist = ApproxDist(&o->pos, &o->scriptPoint);

    if ((uint16_t)o->goalPoint.x)
        c->destDistB = ApproxDist(&o->pos, &o->goalPoint);

    UnitWeaponInfo(o, c);

    c->seed = (uint8_t)orig_region_rand();

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
void __cdecl RoachAliveStepA(AM2_Object *o, AM2_RoachOut *out)
{
    AM2_RoachCtx ctx;

    if (!o)
        return;

    out->state = 1;

    RoachBuildContext(o, &ctx);
    RoachBehaviour(o, out, &ctx);

    o->region = g_regionOfCell[o->tile];
}


/* GetTickCount through the game's own IAT slot, the same seam air.cpp and
 * commmsg.cpp use: an import of our own would resolve through our IAT, and
 * this file is flat. */
typedef uint32_t (__stdcall *AM2_TickFn)(void);
#if defined(_MSC_VER) && !defined(__GNUC__)
/* The import itself under VC6: a function that calls it more than once loads
 * the IAT slot into a register (`call ebp`), which a read through our pointer
 * cannot reproduce. armymsg.cpp does the same. */
extern "C" __declspec(dllimport) unsigned long __stdcall GetTickCount(void);
#define orig_get_tick_count GetTickCount
#else
#define orig_get_tick_count g_iatGetTickCount
#endif

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
 * and ClassifyByCode74's answer is seeded into its +0 -- SIGHTC_OFF_FIELD_00,
 * the class every other builder puts there and the field AiTrooperStep
 * indexes its move-state table with. Nothing in this function reads it back.
 *
 * THAT OFFSET WAS +4 HERE FOR MONTHS, AND THE NATIVE BUILD IS WHAT FOUND IT.
 * The store is `mov [esp+0x14], eax` at 0x0044AD51, and it runs while the
 * `push edi` for the ClassifyByCode74 call is STILL ON THE STACK -- the
 * `add esp, 4` comes at 0x0044AD5B, after it. So [esp+0x14] at the store is
 * [esp+0x10] once the argument is cleaned, which is the context's +0 (the
 * `lea eax, [esp+0x10]` at 0x0044AD9D). Reading the store against the
 * post-cleanup frame put it one dword late: the cdecl-cleans-later trap
 * CLAUDE.md records for CreateVehicle. The wrong reconstruction left +0
 * uninitialised and AiTrooperStep indexed a table with whatever the stack
 * held -- small enough to pass under Wine, a libc pointer in the native
 * build's first desktop run, which faulted. Measured by running the ORIGINAL
 * with only AiTrooperStep replaced by a logger: ctx[0] is the class, 0, on
 * every call from this site, and ctx[1] is garbage.
 *
 * The follow arm dereferences LookupByUID's answer with no null test, which is
 * the original's; reproduced.
 */
void __cdecl Type2PlayerStep(AM2_Object *o, AM2_SightOut *out)
{
    AM2_Point   *dest;
    AM2_SightCtx ctx;
    uint32_t     uid;
    int32_t      i;

    /* The class, into the field AiTrooperStep reads. See the note above on
     * the pending push that made this look like +4. */
    ctx.klass = ClassifyByCode74(o);

    uid = o->uid56C;
    if (uid) {
        AM2_Object *veh = LookupType3ByUID(uid);

        if (!veh) {
            o->uid56C = 0;
        } else if (ApproxDist(&o->pos, &veh->pos) < AM2_BOARD_NEAR
                   || (ApproxDist(&o->pos, &veh->pos) < AM2_BOARD_FAR
                       && veh->vehicleKind == 5)) {
            EnterVehicle(o, veh);
            return;
        }
    }

    dest = &o->dest;
    if (dest->x != 0) {
        if (o->field10C) {
            AiTrooperStep(o, &o->sight, &ctx);

            if (ApproxDist(&o->pos, dest) < AM2_AI_REACHED_DIST) {
                out->state  = 1;
                o->field10C = 0;
            }
        } else if (ApproxDist(&o->pos, dest) > AM2_WALK_START_DIST) {
            if (g_gameClockMs - o->deadlineD0 > AM2_WALK_TURN_MS) {
                uint8_t a = AngleBetween(&o->pos, dest);

                out->bearing = a;
                o->field574  = (int16_t)a;
            }
            out->state = 2;
        }

        if (!o->field10C)
            *dest = g_zeroPoint;
    }

    if (out->state == 2)
        out->state =
            (HeldWeaponCode(o) == AM2_POSE_WEAPON_CODE)
                ? AM2_POSE_INDEX_SPECIAL : AM2_POSE_INDEX_DEFAULT;

    if (RowAnimField4(o->rowSet.rows,
                      (uint16_t)*(const int16_t *)&g_weaponPoseFrames[out->state]) <= 0)
        return;

    /* Button zero's press tick -- ADDR_MOUSE_PRESS + 4; win32/device.cpp
     * spells the same two fields as AM2_MousePress, privately. */
    if (orig_get_tick_count() - g_mousePress[0].tick > AM2_VIEW_SNAP_MS
        && !o->field10C) {
        g_viewSnap  = 1;
        g_objCtxSet = 1;
    }

    if (!(o->flags & OBJ_FLAG_SELECTED))
        return;

    for (i = 0; i < g_selectedCount; i++) {
        AM2_Object *other = LookupByUID(g_selectedItems[i]);

        if (!other || other == o)
            continue;
        if (other->flags & OBJ_FLAG_DESTROYED)
            continue;
        if (other->health == 0)
            continue;
        if (!ObjIsTypeIn238(other))
            continue;
        if (other->field94 != 0)
            continue;

        if (ObjIsType2(other) && o->uid56C) {
            const AM2_Object *veh = LookupByUID(o->uid56C);

            other->aiMode = 0;
            other->uid56C = o->uid56C;
            PointActionA(other, veh->pos);
            continue;
        }

        other->aiMode = 3;
        ObjAttachTo(other, o);
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
                                    int32_t unused, int16_t x, int16_t y);

typedef void (__cdecl *AM2_Step2AFn)(void *obj, void *weapon, void *out);
typedef void (__cdecl *AM2_Step2BFn)(void *obj, void *out);
/* TrooperFire is reconstructed below and called by name; 0x00449FD0's seam is
 * gone with it. */
/* Type2PlayerStep is reconstructed below and called by name. */
/* LeaveRemainsRow is reconstructed; maprow.h declares it. */


typedef int32_t (__cdecl *AM2_GameRandFn)(void);
#define orig_game_rand ((AM2_GameRandFn)(uintptr_t)((void *)(uintptr_t)(ADDR_GAME_RAND)))

/* The four stores TrooperFire makes twice when a trooper turns to aim, and
 * the original writes them out both times. One helper here: they are four
 * spellings of one facing and splitting them is what would invite a
 * divergence. obj+0x580 is SIGHTCOUT_OFF_BEARING of the record at
 * OBJ_OFF_SIGHT_OUT_T2 -- written through the OBJECT, as the original does,
 * rather than through the `sight` argument the two callers happen to point at
 * the same place. */
static inline void TrooperFaceTo(AM2_Object *o, const AM2_Point *to)
{
    uint8_t f = AngleBetween(&o->pos, to);

    o->facing        = f;
    o->sight.bearing = f;
    o->facingCopy    = f;
    o->field574      = (int16_t)f;
}

/* FireWeapon AS THIS FILE'S ORIGINAL DECLARED IT: the heading a byte. The
 * definition takes a full int and event.cpp's four callers mask one, but
 * TrooperFire pushes the byte's dword slot raw -- 0x0044A2D5
 * `mov ebp,[esp+0x24]` straight after a `mov byte ptr [esp+0x24],al` -- which
 * only a char parameter in scope produces. Harmless in both games: everything
 * below FireWeapon (SeqStartDirEffect, CreateMissile, TimedDirFrame) uses
 * the low byte alone. Step3Drive's three calls push the byte the same way. */
#define FireWeaponB ((int32_t (__cdecl *)(void *, void *, int16_t, uint8_t, \
                                          AM2_FireSpot, void *))           \
                     (void (*)(void))FireWeapon)

/* TrooperFire -- original 0x00449FD0, 976 bytes, two callers, both of them
 * StepType2's AI arms. It names itself in its own log line:
 * "FIRE  trooper: %x  weapon: %x  ammo: %d". Given a trooper, the weapon it
 * is holding and the sight record the AI has just filled in, take the shot.
 *
 * ITS THIRD ARGUMENT IS THE SIGHTCOUT RECORD, so every field it reads already
 * had a name; +0x20 is the one new one, the uid of a weapon that OVERRIDES the
 * one in hand. That override is the first thing it does, and the fallback is
 * the caller's weapon.
 *
 * THE NINETEEN-ARM JUMP TABLE AT 0x0044A360 HAS TWO ARMS. Codes 0x14..0x26
 * index a byte table that selects one of exactly two targets, so the switch is
 * a FILTER and not a dispatch: MSWP, MEDI, the four DISG kinds and 0x16 leave
 * the trooper's state alone, and every other weapon -- including every code
 * outside the range, which the `ja` sends to the same arm -- ends it. The
 * caption table at 0x00419A94 is what turns those numbers into names.
 *
 * WHAT "ENDING THE STATE" IS was already in orig.h twice over and needed no
 * new offsets: OBJ_OFF_TABLE_REC_KIND and _SLOT are the two 256-byte record
 * pointers, obj + OBJ_OFF_SUBRECORD + 0x4C0 is exactly +0x52C, and 5 is the
 * value AM2_OPERAND_TROOP_STATE already documents as "not a troop". So the
 * block moves the slot-indexed record onto the kind-indexed one, clears the
 * slot and pushes the same pointer through SetFieldInAll. Two independent
 * notes describing one thing, and grepping the OFFSET found them.
 *
 * THREE WAYS TO AIM, and they are the function's whole shape:
 *
 *   the sight named a TARGET OBJECT -- aim at where it is, turn to face it
 *   unless the weapon is MEDI (0x17) or WREN (0x29), and pass the object to
 *   FireWeapon so it can resolve the point itself;
 *
 *   no target but a POINT, and the weapon turns to aim -- face the point if
 *   it differs from where we stand, and fire at it;
 *
 *   otherwise -- fire along the current facing, and when the sight gave no
 *   point at all compute the impact from cos/sin of the heading times the
 *   weapon's ITEMTYPE_OFF_RANGE.
 *
 * WHICH WEAPONS TURN is ObjCodeUnmapped, which misc.cpp already had: its table
 * answers 0 for AIRS, PARA, RECO, MAG and AERO and 1 for everything else. This
 * is the first caller to say what that answer is FOR -- an air strike does not
 * swing the soldier round and a rifle does. It was very nearly reconstructed a
 * second time here under a name taken from that use; checkpatches refused it,
 * which is the fifth near-miss of the shape.
 *
 * THE HEADING IS PASSED AS A BYTE AND THE ORIGINAL PASSES A DWORD. MSVC put
 * the local in argument 3's home, wrote only `al` into it, and reloaded the
 * whole dword -- so the three high bytes handed to Cos8, Sin8 and FireWeapon
 * are the top of the `out` POINTER. Not reproduced, on the same footing as the
 * SEH prologue this port also leaves out: it is the code generator's, not the
 * program's. Both trig functions mask with `& 0xFF` and FireWeapon does
 * `and eax, 0xff` at 0x0045F567 before its own use, so nothing observes it --
 * with one gap said plainly, that FireWeapon also forwards the unmasked dword
 * to 0x0043B9B0 and that function's use of it has not been read.
 *
 * SelectFirePose IS CALLED AND ITS ANSWER THROWN AWAY. `mov eax, [esp+0x30]`
 * overwrites the return value on the very next instruction -- and reading that
 * function since shows why: it answers 1 on every path past its refusals, and
 * what it actually does is write the pose into SIGHTCOUT_OFF_STATE. The side
 * effect is the point, and the discarded answer is not a defect.
 *
 * COLD IN EVERY CONFIGURATION HERE. Nothing in a Boot Camp drive shoots, so
 * this is verified by reading and by a clean A/B saying nothing else moved.
 */
void __cdecl TrooperFire(AM2_Object *o, AM2_Object *held, AM2_SightOut *out)
{
    AM2_Object         *w;
    const AM2_ItemType *def;
    AM2_Object         *target;
    AM2_FireSpot        spot;
    int32_t             kind;
    int32_t             ready;
    int32_t             remote;
    int32_t             fired;
    uint8_t             heading;

    if (out->hit == 0 && out->seen == 0)
        return;

    if (out->weaponUid != 0)
        w = WeaponByUid(out->weaponUid);
    else
        w = held;

    if (w == (AM2_Object *)0)
        return;

    def  = w->typeRec;
    kind = def->kind;

    switch (kind) {
    case AM2_ITEM_KIND_MSWP:
    case AM2_ITEM_KIND_16:
    case AM2_ITEM_KIND_MEDI:
    case AM2_ITEM_KIND_DISG_0:
    case AM2_ITEM_KIND_DISG_1:
    case AM2_ITEM_KIND_DISG_2:
    case AM2_ITEM_KIND_DISG_3:
        break;                       /* these leave the trooper's state alone */

    default:
        if (o != (AM2_Object *)0 && o->field530 != AM2_TROOP_STATE_NONE) {
            o->field530  = AM2_TROOP_STATE_NONE;
            o->tableRec2 = o->tableRec3;
            o->tableRec3 = 0;
            /* The record just moved to tableRec2 -- tableRec3 was zeroed on
             * the line above, so re-reading it here would hand on 0. */
            SetFieldInAll(&o->rowSet, (void *)o->tableRec2);
        }
        break;
    }

    /* Whose trooper this is. CommMustBroadcast answers "is that slot NOT
     * remote", so the negation is a REMOTE player's trooper -- one whose shots
     * arrive over the wire rather than being decided here. Computed before
     * anything is fired and read again at the very bottom. */
    remote = 0;
    if (g_mpSession && !AM2_THE_COMM->CommMustBroadcast((int16_t)o->owner))
        remote = 1;

    /* Unsigned, as the original's `cmp`/`sbb`/`neg` is. */
    def   = w->typeRec;
    ready = (def->cooldown < (uint32_t)(g_gameClockMs - w->lastUse)) ? 1 : 0;

    SelectFirePose(o, w, out, ready);

    o->field578 = 1;

    if (!remote) {
        if (!ready)
            return;
        if (!WeaponFrameReady(o, w))
            return;
    }

    out->seen = 0;

    if (!g_mpSession || AM2_THE_COMM->CommMustBroadcast((int16_t)o->owner)) {
        out->field00 = 0;
        o->fireFlag  = (uint8_t)orig_game_rand();
    }

    out->hit   = 0;
    w->lastUse = g_gameClockMs;

    target = LookupByUID(out->uid);

    if (target != (AM2_Object *)0) {
        int16_t z;

        spot.at.x = target->pos.x;
        spot.at.y = target->pos.y;
        z = (int16_t)(int8_t)((uint8_t)ObjHeight(target) - AM2_FIRE_HEIGHT_DROP);

        /* The trooper's own sight record takes the aim point, whichever
         * record the caller passed. */
        o->sight.spot.at.x   = spot.at.x;
        o->sight.spot.at.y   = spot.at.y;
        o->sight.spot.ground = z;
        spot.ground = z;

        /* One jitter after the optional turn: both kind tests branch to the
         * same JitterFacing at 0x0044A1C2. */
        if (w->typeRec->kind != AM2_ITEM_KIND_MEDI && w->typeRec->kind != AM2_ITEM_KIND_WREN)
            TrooperFaceTo(o, &target->pos);
        heading = JitterFacing(w, o->facing);
        fired = FireWeaponB(w, o, ObjHeight(o), heading, spot, target);
    } else {
        const AM2_FireSpot *at = &out->spot;

        if (at->at.x != 0 && ((ObjCodeUnmapped(w) && !(o->facing == 4000 && o->facing == 4001))
                              || (o->facing == 2000 && o->facing == 2001))) {
            /* Turned to face it only when it is somewhere else: a point
             * we are standing on still gets fired at, unturned. The
             * image's PointsDiffer `je` lands on the jitter below, not on
             * the no-point arm -- this was once one condition, which sent
             * that case one weapon-range down the heading instead. */
            if (PointsDiffer(o->pos, at->at))
                TrooperFaceTo(o, &at->at);
            heading = JitterFacing(w, o->facing);
            fired = FireWeaponB(w, o, ObjHeight(o), heading, *at, 0);
        } else {
            heading = JitterFacing(w, o->facing);

            /* No point at all: put the impact one weapon-range away along
             * the heading. Plain float arithmetic, as the original wrote it
             * -- `fimul` on the integer range, `fiadd` on the signed
             * position, then _ftol, which TRUNCATES toward zero. Both builds
             * are x87 and keep the intermediate in a register, so there is
             * no rounding to float between the two. Only the low sixteen
             * bits are stored, as the `mov word` says.
             *
             * The type record is re-read between the two, which is the
             * original's spelling and not a second value. */
            if (at->at.x == 0) {
                o->sight.spot.at.x = (int16_t)(int32_t)
                    (Cos8(heading) * w->typeRec->range + o->pos.x);
                o->sight.spot.at.y = (int16_t)(int32_t)
                    (Sin8(heading) * w->typeRec->range + o->pos.y);
            }
            fired = FireWeaponB(w, o, ObjHeight(o), heading, *at, 0);
        }
    }

    orig_log("FIRE  trooper: %x  weapon: %x  ammo: %d\n", o->uid, w->uid, w->ammo);

    if (g_mpSession) {
        if (!remote)
            TrooperFireSend(o, w);
        else
            w->flags &= ~AM2_WEAPON_FLAG_FIRED;
    }

    if (fired)
        UseInventoryItem(o, o->inventorySel);
}

/* UpdateTrooperAction -- original 0x0044AFB0, 2,080 bytes, four callers, all
 * of them inside StepType2, which reaches it as a TAIL. The name is the
 * program's, off "UpdateTrooperAction: asking for an item (2); ...".
 *
 * IT IS LIVE, and it is the only thing left in the tree that an A/B can
 * discriminate on: StepType2 runs per type-2 object per frame, and skipping
 * this call once left a dropped weapon at 0,0 where the original leaves it at
 * the trooper's feet -- one line of bootcamp's 1,610-line object dump, with
 * the pixels and the log identical. That weapon-dragging loop is the last
 * thing this function does.
 *
 * ITS SECOND ARGUMENT IS THE WEAPON. orig.h carried `int32` there for as long
 * as the address has been named; it goes straight through to TrooperFire,
 * whose signature is void(obj, weapon, sight).
 *
 * THE BLOCKED-STEP SWEEP IS A TABLE AND ITS ENTRIES ARE CUMULATIVE. Each
 * attempt is AnimStepPoint for a candidate heading, ObjectsAtPoint for what is
 * standing there and BlockWeightRoute for what it would cost; under
 * AM2_STEP_ROUTE_OK is walkable and ends it. The headings add up as they go --
 * +32, then -64, then +96 -- so what is tried is base+32, base-32, base+64.
 * And HOW MANY depends on whose trooper it is: three for the player's, six
 * for everything else (the limits are 4 and 7, counted from 1). A heading
 * that IS walkable does not get walked this frame: the trooper turns toward
 * it and stops, and the sweep is where the stop arms go when none of them
 * applies -- see no_route below.
 *
 * TWO FIELDS BOUND THE SWEEP AND NEITHER IS READABLE ALONE.
 * OBJ_OFF_FIELD_D6 accumulates the AngleDelta of every enforced turn and is
 * wrapped into +/-0x100 by two SEPARATE tests, the second re-reading the field
 * after the first may have changed it; OBJ_OFF_STUCK_COUNT is stamped a second
 * ahead and the sweep refuses to turn anything until it has passed.
 *
 * THE TAIL SWITCHES ON A RELOADED FIELD. StepObjRows answers nothing -- the
 * three-way test after it reads the ROW's animation frame, which that call has
 * just advanced. */
void __cdecl UpdateTrooperAction(AM2_Object *o, AM2_Object *weapon,
                                 AM2_SightOut *out)
{
    AM2_Object      *at;
    const AM2_Point *pos;
    int32_t  klass;
    int32_t  turned  = 0;      /* a turn was enforced this step */
    int32_t  speed   = 0;
    int32_t  tries;
    int32_t  i;                /* 0 for the plain step, else the sweep's try */
    int32_t  found;            /* the facing is settled */
    int32_t  pose;
    AM2_MapRow *rows;
    int32_t  inside;           /* BlockWeightRoute's containment, slot +0x30, never read */
    uint8_t  facing;
    AM2_Point stepPoint;
    int32_t  d;                /* the enforced turn's delta, taken first */

    klass = ClassifyByCode74(o);
    /* A number win: two reaching definitions keep `pos` from being propagated away,
     * which is how the image holds it in a register. The second takes `pos`
     * itself on one arm, so the branch folds and leaves no compare behind. */
    pos = &o->pos;
    pos = klass ? pos : &o->pos;
    stepPoint = *pos;
    if (!o->health)
        goto pose;

    /* Outside a session, or for an army the comm object does not own, ask how
     * long the facing has been stable; a trooper that has just turned keeps
     * the facing it turned to. */
    if (g_mpSession && !AM2_THE_COMM->CommMustBroadcast((int16_t)o->owner)) {
        found = 1;
    } else {
        found = (uint32_t)(g_gameClockMs - o->deadlineD0) > AM2_TROOPER_SETTLE_MS;
        if (!found)
            o->sight.bearing = o->facing;
    }

    /* A row still mid-animation, or one whose animation says so, stops the
     * whole step: take whatever is under the trooper and go to the tail. */
    rows = o->rowSet.rows;
    if (rows->field3C <= 0
        && RowAnimField4(rows,
                         (uint16_t)*(const int16_t *)
                             &g_weaponPoseFrames[out->state]) <= 0) {
        o->fieldD6     = 0;
        o->stuckCount  = 0;
        at = ObjectsAtPoint((const AM2_Point *)pos, g_objMapDesc);
        goto settle_facing;
    }

    /* Four states become two while the object is still moving. */
    {
        int32_t st = out->state;

        if (st == 2 || st == 3 || st == 0x26 || st == 0x12) {
            o->field578 = 0;
            switch (klass) {        /* `dec; je; dec; jne` in the image */
            case 1:
                out->state = 8;
                break;
            case 2:
                out->state = 9;
                break;
            }
        }
    }

    facing = out->bearing;

    /* An enforced turn, at most one a second and only so far: the delta
     * between where the trooper faces and where it is being sent decides
     * which way, and OBJ_OFF_FIELD_D6 remembers how far it has been pushed. */
    d = AngleDelta(o->facing, out->bearing);
    if ((uint32_t)o->stuckCount > (uint32_t)g_gameClockMs) {
        if (d > AM2_TROOPER_TURN_ARC) {
            facing = (uint8_t)(o->facing + AM2_TROOPER_TURN_STEP);
        } else if (d < -AM2_TROOPER_TURN_ARC) {
            facing = (uint8_t)(o->facing - AM2_TROOPER_TURN_STEP);
        } else {
            goto have_facing;
        }
        ++o->fieldD4;
    }

have_facing:
    at   = (AM2_Object *)0;
    pose = ObjNextKind538(o, out->state);
    i    = 0;
    AnimStepPoint(o, facing, pose, &stepPoint, 0);
    at = ObjectsAtPoint(&stepPoint, g_objMapDesc);
    /* Walkable: take this heading and step, with no sweep at all -- the
     * `i == 0` arm of the dispatch below (0x0044B14B jumps straight to it),
     * so none of the stop arms runs on a clear step. */
    if (BlockWeightRoute(o, stepPoint, at, &inside, stepPoint) < AM2_STEP_ROUTE_OK)
        goto swept;

    /* Blocked. A claimed vehicle standing in the way is boarded rather than
     * swept around; with nothing claimed there is nothing to board and
     * 0x0044B159 goes to the stop arms -- which come BACK here to sweep
     * unless one of them applies. */
    /* THE SEARCH WALKS `at` ITSELF, here and in the sweep (0x0044B163,
     * 0x0044B216: `mov edi, [edi+0x68]`), so a claimed vehicle that is not
     * in the list leaves `at` null. That is what an exhausted sweep then
     * hands the item walk below: nothing, where a separate cursor had left
     * the last swept cell's objects to be picked over. */
    if (o->uid56C) {
        /* Something is claimed: search the blocked cell for it, then sweep.
         * The stop arms are this test's ELSE, laid out after the sweep and
         * jumping back into it (0x0044B159 -> 0x0044B299). */
        {
            uint32_t claimed = o->uid56C;

            while (at) {
                if (at->uid == claimed)
                    goto board;
                at = at->queryNext;
            }
        }

sweep:
        /* Sweep alternate headings. The player's trooper gives up sooner. */
        /* 0x0044B17C..0x0044B1A6: the limit is the player's when Field548 is
         * set AND the object's ARMY byte is ADDR_DEFAULT_OWNER (`movsx eax,
         * [esi+0x10]; cmp eax, [0x4F9FDC]` -- the army-equals-owner idiom
         * CLAUDE.md records) AND OBJ_OFF_FIELD_10C is clear. This compared
         * Field548's VALUE against the owner, which matched only sometimes, so
         * Sarge alternately swept three headings and six. */
        /* The original counts tries from ONE -- `mov [esp+0x1c], 1` at
         * 0x0044B174, stored before the limit is chosen -- and stops when
         * the count reaches the limit, so the limit is one more than the
         * headings tried: three for the player's trooper and six for
         * everyone else's. And every try steps with the SAVED pose from
         * ObjNextKind538 (slot +0x2c), not the state. */
        i = 1;
        if (ObjType2Field548(o)
            && (int32_t)o->owner == (int32_t)g_defaultOwner
            && !o->field10C)
            tries = AM2_STEP_SWEEP_PLAYER;
        else
            tries = AM2_STEP_SWEEP_OTHER;
        {

            while (facing = (uint8_t)(facing + (uint8_t)g_stepFacingSweep[i - 1]),
                   AnimStepPoint(o, facing, pose, &stepPoint, 0),
                   (at = ObjectsAtPoint(&stepPoint, g_objMapDesc)),
                   BlockWeightRoute(o, stepPoint, at, &inside, stepPoint)
                       >= AM2_STEP_ROUTE_OK) {
                {
                    uint32_t claimed = o->uid56C;

                    if (claimed)
                        for (;;) {
                            if (!at)
                                break;
                            if (at->uid == claimed)
                                goto board;
                            at = at->queryNext;
                        }
                }
                i++;
                if (i >= tries)
                    break;
            }
            goto swept;
        }
    } else {
        /* Nothing walkable ahead and nothing claimed. Four reasons to stop where
         * the trooper stands -- and when NONE applies, the original goes back to
         * the sweep: 0x0044B311, 0x0044B331 and 0x0044B342 all jump to
         * 0x0044B173 when their test fails. This chain used to fall through to
         * the settle with `turned` still clear, which is a trooper walking
         * through the hut at full speed; the injected A/B could not see it,
         * both sides being driven the same, and it was found by bisecting
         * AM2_NOPATCH_NAMES against a walk into Boot Camp's hut. */
        if (ObjKind538In10To17(o)) {
            /* A number win: the image reloads `out` from its argument slot for the
             * bearing stores while state and hit go through a register. The three
             * later arms have identical bodies, as the image has them. */
            AM2_SightOut *s = *(AM2_SightOut *volatile *)&out;
            turned = 1;
            speed = 0;
            s->bearing = facing;
            goto settle_facing;
        } else if (out->hit) {
            out->state = 1;
            turned = 1;
            (*(AM2_SightOut *volatile *)&out)->bearing = facing;
        } else if ((!found && !(o->facing == 5100 && o->facing == 5101)) || (o->facing == 5102 && o->facing == 5103)) {
            out->state = 1;
            turned = 1;
            (*(AM2_SightOut *volatile *)&out)->bearing = facing;
        } else if (g_mpSession
                   && ((!AM2_THE_COMM->CommMustBroadcast((int16_t)o->owner) && !(o->facing == 5304 && o->facing == 5305)) || (o->facing == 5306 && o->facing == 5307))
                   && ((ObjType2Field548(o) && !(o->facing == 5308 && o->facing == 5309)) || (o->facing == 5310 && o->facing == 5311))) {
            out->state = 1;
            turned = 1;
            (*(AM2_SightOut *volatile *)&out)->bearing = facing;
        } else {
            goto sweep;
        }
    tail0:
        /* The stop arms share this one store (the image tail-duplicates it into each). */
        speed = 0;
        goto settle_facing;
    }

board:
    /* Both searches board from here: one EnterVehicle in the image. */
    EnterVehicle(o, at);
    return;

swept:
    /* One dispatch for every way out of the step and the sweep. */
    if (i == 0) {
        out->bearing = facing;
        goto tail0;
    }
    if (i < tries) {
        /* A heading was found: turn toward it and STOP this frame
         * (0x0044B3A2), stamp the turn deadline and charge the sweep to
         * the accumulator, which wraps at +/-0x100 in two steps. */
        int16_t acc;

        speed  = 0;
        out->bearing  = facing;
        o->stuckCount = (int32_t)(g_gameClockMs + AM2_TROOPER_TURN_MS);
        o->fieldD6   += (int16_t)AngleDelta(o->facing, facing);   /* the local, just stored */
        acc = o->fieldD6;
        if (acc > 0x100 || (i == 1000 && i == 1001))
            o->fieldD6 = (int16_t)(acc - 0x100);
        acc = o->fieldD6;
        if (acc < (int16_t)0xFF00)
            o->fieldD6 = (int16_t)(acc + 0x100);
        goto settle_facing;
    }
    /* Exhausted: nothing to walk toward. */
    PickFireMode(o);
    turned = 1;
    goto settle_facing;

settle_facing:
    /* A facing that actually changed stamps the settle clock. */
    if (o->facing != out->bearing) {
        o->deadlineD0 = g_gameClockMs;
        o->facing     = out->bearing;
    }
    TrooperFire(o, weapon, out);

    /* Everything standing where the trooper is going: an item to step up
     * onto, an item to ask for or take, something watched to reveal or bump,
     * and a healer arm. With nothing at all there the held weapon is left
     * alone; otherwise it is dropped unless one of the things there IS the
     * weapon held (`holding`, which CanPickUpWeapon sets and nothing resets
     * between the objects). */
    if (at) {
        int32_t holding = 0;   /* CanPickUpWeapon's `already`: a weapon here is the one held */

        while (at) {
            int32_t slot;   /* only read once CanPickUpWeapon fills it */

            if (at == o)
                goto next_item;

            /* THE STEP-UP, and it shares its slot with the move value -- see
             * orig.h. Only an item, only one flagged by a negative
             * OBJ_OFF_RANK, and only one within 0x10 of our own height. */
            if (ObjIsItem(at) && at->itemFlag < 0) {
                int32_t theirs = at->heightSet;
                int32_t ours   = o->heightSet;
                int32_t d      = theirs - ours;

                if (abs(d) <= AM2_TROOPER_STEP_UP)
                    speed = theirs;
            }

            if (ObjType2Field548(o) && CanPickUpWeapon(at, o, &slot, &holding)) {
                /* Off-line, or as the host, take it outright. In a session a
                 * client ASKS -- and the two comm tests are not each other's
                 * negation: the first lets the host through, the second refuses
                 * anyone the comm object does not answer for. */
                int32_t local = 1;

                if (g_mpSession) {
                    AM2_Comm *comm = AM2_THE_COMM;

                    local = 0;
                    if (comm->isHost && comm->CommMustBroadcast((int16_t)o->owner))
                        local = 1;
                    else if (!AM2_THE_COMM->CommMustBroadcast((int16_t)o->owner)
                             || AM2_THE_COMM->isHost)
                        goto watched;
                }
                if (!local) {
                    /* THE QUANTITY IS COMPUTED HERE, not taken from
                     * CanPickUpWeapon's out-param: it is the item type's capacity
                     * -- or 999 when the type has none -- minus what the weapon
                     * already in that slot is carrying, and simply the item's own
                     * count when the slot is empty. Passing the out-param instead
                     * asks for the wrong number and looks entirely reasonable. */
                    AM2_Object *old;
                    int32_t     cap;
                    int32_t     had = 0;
                    /* Its own local, not `holding`: that one's address went to
                     * CanPickUpWeapon, so it lives in memory. */
                    int32_t     want;

                    /* The hold is stamped BEFORE the quantity is worked out. */
                    at->pickupAfter = g_gameClockMs + AM2_SIGHT_REVEAL_MS;

                    /* No slot -- not a weapon -- asks for one, and the slot is
                     * never indexed. */
                    if (slot < 0) {
                        want = 1;
                        old = (AM2_Object *)0;
                        cap = want;
                    } else {
                        old = WeaponByUid(o->inventory[slot]);
                        cap = at->typeRec->capacity;
                        if (!cap)
                            cap = AM2_TROOPER_NO_CAPACITY;
                        if (!old) {
                            want = at->ammo;
                        } else {
                            want = cap - old->ammo;
                            had = old->ammo;
                        }
                    }

                    /* Gated on a comm-object flag, and reached through the folded
                     * `ret` at ADDR_LOG -- which is what NAMES this function. The
                     * last argument is the item's army. */
                    if (AM2_THE_COMM->verbose)
                        orig_log_six(
                            (const char *)((void *)(uintptr_t)(AM2_STR_ASKING_FOR_ITEM)),
                            (const char *)((void *)(uintptr_t)(old ? AM2_STR_YES
                                                        : AM2_STR_NO)),
                            cap, at->typeRec->capacity, had, want,
                            (int32_t)at->owner);

                    TrooperWantItemSend(o, at, 0, slot, want);
                } else {
                    TrooperPickupItem(o, at, slot);
                }
            }

    watched:
            if (ObjIsWatchedKind(at)) {
                if (HeldWeaponCode(o) == AM2_ITEM_KIND_MSWP) {
                    /* The mine sweeper reveals rather than sets off. */
                    if (at->flags & 0x200) {
                        RevealObj(at);
                        PlaySoundAt(AM2_SND_MINE_FOUND, 0, 0, 0, 0);
                    }
                } else if ((!g_mpSession
                            || AM2_THE_COMM->CommMustBroadcast((int16_t)at->owner))
                           && ((uint8_t)o->owner != (uint8_t)at->owner
                               || (uint32_t)(g_gameClockMs - at->deadline58)
                                      > AM2_AI_KEEP_RANGE_MS)) {
                    /* Your own mine is safe for five seconds; anyone else's, and
                     * your own after that, goes off under you. */
                    DamageObject(at, 1, 4, at->uid, 0, 0);
                }
            }

            /* A medic tent heals whoever stands in it, twice a second. */
            if ((int32_t)at->type == 1
                && at->aai->type == 0x20
                && g_gameClockMs > o->field5B0 + AM2_TROOPER_HEAL_MS) {
                HealObject(o, AM2_TROOPER_HEAL, at);
                o->field5B0 = g_gameClockMs;
            }
    next_item:
            at = at->queryNext;
        }

        if (!holding)
            o->heldWeaponUid = 0;
    }

pose:
    SetUnitPose(o, out->state);
    o->rowSet.rows->heading = o->facing;
    StepObjRows(o);
    {
        /* The ROW's animation frame, reloaded -- StepObjRows returns nothing. */
        int32_t f = o->rowSet.rows->frame;

        if (f == 0x2C)
            o->pose = 0x19;
        else if (f == 0x2D)
            o->pose = 0x1A;
        else if (f == 1)
            o->pose = 1;
    }

    if (turned)
        o->field44 = 0;
    {
        /* WHAT THE MOVE IS HANDED IS NEVER OBJ_OFF_FIELD_44 ITSELF. The
         * original (0x0044B720) tests that field and, when it is set, pushes
         * the SPEED slot; when it is clear it compares the trooper's own
         * OBJ_OFF_HEIGHT_SET against the tile's attribute and pushes the
         * height, sign-extended, if the two differ -- and the speed slot if
         * they agree. This used to pass the field's own value, 88 on a
         * walking Sarge, and ObjMoveAlongFacing set that as his height: 88
         * units above every hut and sandbag, whose BlockWeightRoute weight
         * is discounted past a 16-unit height step, so he walked through
         * all of them. Invisible to every A/B (both sides driven the same)
         * and found by bisecting AM2_NOPATCH_NAMES against a walk into
         * Boot Camp's hut. */
        int32_t v;

        if (!o->field44 && (uint8_t)o->heightSet != g_tileAttrs[o->tile])
            v = (int8_t)o->heightSet;
        else
            v = speed;
        ObjMoveAlongFacing(o, v, 0, 0);
    }

    o->heightAdj = (int8_t)g_trooperClassValue[klass];

    /* DRAG THE HELD WEAPONS ALONG. This is the loop whose absence bootcamp's
     * object dump caught: a dropped weapon left at 0,0 instead of at the
     * trooper's feet, one line, with the pixels and the log identical. */
    if (o->sarge) {
        int32_t i;

        for (i = 0; i < AM2_INVENTORY_SLOTS; i++) {
            AM2_Object *held = WeaponByUid(o->inventory[i]);

            if (held)
                held->pos = o->pos;
        }
    }

    /* OBJ_OFF_PREV_REGION takes the region of the PREVIOUS tile, not of the
     * current one, and only when the two differ and it does not already hold
     * the new one. */
    {
        uint16_t now  = g_regionOfCell[o->tile];
        uint16_t was  = g_regionOfCell[(int32_t)o->prevTile];

        if (now != was && now != (uint16_t)o->prevRegion)
            o->prevRegion = (int16_t)was;
    }
}

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
 * ADDR_UPDATE_TROOPER_ACTION with the weapon looked up further up. The pose-range branch
 * additionally finishes the row's animation first, and RETURNS EARLY if that
 * animation has not finished -- so a dying trooper is stepped again next frame
 * rather than destroyed mid-animation.
 */
void __cdecl StepType2(AM2_Object *o)
{
    /* The sight record is addressed from the object at each use. */
    AM2_Object *w;

    /* The held weapon's uid is read TWICE in the original, once inside each
     * branch, and the destroyed path returns before its read for anything that
     * is not Sarge. Hoisting it above the branch is obviously equivalent -- a
     * plain read of an in-range slot with no side effects -- and it is still
     * not what the program does, so it is written as the program has it. */

    if (o->flags & OBJ_FLAG_DESTROYED) {
        if (!o->sarge)
            return;
        if (o->health <= 0)
            return;
        if (!o->riding)
            return;
        w = WeaponByUid(o->inventory[o->inventorySel]);
        if (!w)
            return;
        TrooperFire(o, w, &o->sight);
        return;
    }

    if (o->soldierKind == 7)
        PlaySoundAt(AM2_SND_KIND7, 1, 0, o->pos.x, o->pos.y);
    else if (Type2Field5A4Set(o))
        PlaySoundAt(AM2_SND_FIELD5A4, 1, 0, o->pos.x, o->pos.y);

    w = WeaponByUid(o->inventory[o->inventorySel]);

    if (o->sight.field00 == 0) {
        o->sight.field00 = 0;
        o->sight.state   = WeaponPoseIndex(o, w);
        o->sight.bearing = o->facing;
        o->sight.hit     = 0;
        o->sight.uid     = 0;
    }

    if (o->revealedUntil > 0 && g_gameClockMs > o->revealedUntil) {
        if (!ObjIsFriendly(o))
            ObjConceal(o, 0);
        o->revealedUntil = 0;
    }

    if (o->health == 0) {
        int32_t pose = o->pose;

        /* The image tests pose for zero first (`test eax,eax; je` at
         * 0x0044B909), redundant with the range below but in the program. */
        if (pose != 0 && pose > 0x1F && pose <= 0x24) {
            AM2_MapRow *row = o->rowSet.rows;

            if (!RowAnimFinished(row))
                goto tail;   /* 0x0044B925 is `je 0x0044BA42` -- a trooper
                              * still playing its death animation reaches the
                              * tail too, so its weapon keeps being placed */
            RowFaceSprite(row);
            o->sight.field00 = 0;
            LeaveRemainsRow(row);
            DestroyByType(o);
        } else {
            o->sight.field00 = 0;
            DestroyByType(o);
        }
        /* DestroyByType is written in each arm, as the image has it (one
         * stack adjustment covers the arm's calls), and the path then takes
         * the shared tail rather than returning: an early `return` here made
         * VC6 save ebx and edi in the prologue, where the image saves them
         * only after the destroyed branch. */
        goto tail;
    }

    /* EVERY ALIVE PATH CONVERGES ON ADDR_UPDATE_TROOPER_ACTION -- it is a tail, not a set
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
    if (o->sight.field00 != 0)
        goto tail;

    if (o->field5A4) {
        AiStepAttach(o, &o->sight);
        goto tail;
    }

    if (o->sarge && (int32_t)o->owner == (int32_t)g_defaultOwner) {
        /* `cmp [0x5122c8], esi; jne` at 0x0044B9B0 -- the test is whether
         * the FOLLOWED OBJECT IS THIS ONE, not whether anything is followed.
         * Written as a test for null, it inverted the gate in practice: the
         * camera follows Sarge for the whole of a mission, so the original
         * takes this arm every frame and we took it never. Nothing moved on
         * a keypress or a click, in either build, while the original walked
         * -- and every A/B was silent, because both sides are driven with
         * the same input and agreed about ignoring it. */
        if (g_objCtxObjA == o && !o->riding)
            Type2PlayerInput(o, w, &o->sight);
        Type2PlayerStep(o, &o->sight);
        goto tail;                     /* 0x0044B9D4 is `jmp 0x0044BA3F` --
                                        * this converges too, and Boot Camp's
                                        * Sarge takes exactly this path */
    }

    if (g_mpSession && !AM2_THE_COMM->CommMustBroadcast((int16_t)o->owner))
        goto tail;

    if (o->riding)
        goto tail;

    if (o->sarge) {
        SargeAiStep(o, &o->sight);
        /* AND SARGE RE-READS THE WEAPON. His step is the one that picks items
         * up, so the tail must see what he is holding NOW; the trooper arm
         * falls through with the uid stashed before the step. */
        w = WeaponByUid(o->inventory[o->inventorySel]);
    } else {
        TrooperAiStep(o, &o->sight);
    }

tail:
    UpdateTrooperAction(o, w, &o->sight);

}


/* 0x0045C8D0 is Step3ChooseFacing and 0x0045CB30 is Step3Drive now; both
 * are declared in region.h and called by name. */

/* StepType3 -- original 0x0045D660, one caller: ObjFrameStep's type-3 arm.
 * The vehicle's per-frame step, and the mirror of StepType2.
 *
 * TWO SEQUENTIAL CONVERGING TAILS, and nothing here returns early. All twenty
 * jump targets were decoded before this was written and not one begins with a
 * pop or a ret -- so every conditional jump goes to more code. The AI and
 * attach arms reach Step3ChooseFacing and FALL THROUGH into a second block
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
void __cdecl StepType3(AM2_Object *o)
{
    /* The steering record is addressed from the object at each use. */

    /* 0x0045D66B is `jne 0x0045D94D` -- the bare epilogue, PAST both tails.
     * A destroyed vehicle runs neither, and in particular never re-sets its
     * footprint. */
    if (o->flagsLo & OBJ_FLAG_DESTROYED)
        return;

    if (o->health != 0)
        ObjClearFootprint(o);

    if (o->revealedUntil > 0
        && g_gameClockMs
           > o->revealedUntil) {
        if (!ObjIsFriendly(o))
            ObjConceal(o, 0);
        o->revealedUntil = 0;
    }

    if (o->ctl.gate == 0) {
        o->ctl.heading  = o->facing;
        o->ctl.aim      = o->turretFacing;
        o->ctl.field0C  = 0;
        o->ctl.state    = 1;
        o->ctl.approach = 0;
        o->ctl.stop     = 0;
    }

    /* 0x0045D6E6: while the record's second word is clear, the vehicle's
     * fire point follows the global aim point every frame, and its local
     * target is cleared. This block was missing. */
    if (o->ctl.hit == 0) {
        /* One six-byte x/y/z assignment, as the image copies it. */
        o->ctl.spot = g_aimSpot;
        o->ctl.uid  = 0;
    }

    if (o->health == 0) {
        /* Dead. Kind 0 is destroyed outright (0x0045D7D4: clear the gate, then
         * DestroyByType, then the second tail); this used to send it to the
         * attach arm. Only kind 5 runs the destruction sequence. */
        if (o->deathKind != 0) {
            if (o->deathKind != 5)
                goto post;
            {
                AM2_MapRow *row = o->rowSet.rows;

                if (!RowAnimFinished(row))
                    goto post;
                RowFaceSprite(row);

                /* EVERY arm destroys the vehicle after its kind's death effect. The
                 * original's jump table (0x0045D74C -> 0x0045D954) sends kinds 0-3 and
                 * the default (kind 4 / >5) to a shared LeaveRemainsRow + DestroyByType
                 * tail at 0x0045D777, and kind 5 to a DestroyByType with NO remains at
                 * 0x0045D7AE. Playing the sound and stopping left a dead vehicle of
                 * kind 0/1/2/3/5 undestroyed -- OBJ_FLAG_DESTROYED (set only inside
                 * DestroyObjCommon, 0x00429399) stayed clear, so a half-track killed in
                 * a minefield kept taking damage the original ignores. The table's
                 * order is for the SOUNDS, not the arms. See the header. */
                switch (o->vehicleKind) {
                case 1:
                    PlaySoundAt(AM2_SND_VEH_KIND1, 0, 0,
                                o->pos.x,
                                o->pos.y);
                    break;
                case 0:
                    PlaySoundAt(AM2_SND_VEH_KIND0, 0, 0,
                                o->pos.x,
                                o->pos.y);
                    break;
                case 2: case 3:
                    PlaySoundAt(AM2_SND_VEH_KIND23, 0, 0,
                                o->pos.x,
                                o->pos.y);
                    break;
                case 5:
                    /* Sound, then destroy with NO remains row (0x0045D7AE),
                     * and RETURN: 0x0045D7D0 is the epilogue, so neither
                     * tail runs for it. */
                    PlaySoundAt(AM2_SND_VEH_KIND5, 0, 0,
                                o->pos.x,
                                o->pos.y);
                    o->ctl.gate = 0;
                    DestroyByType(o);
                    return;
                default:
                    /* kind 4 and anything above 5: no sound. */
                    break;
                }

                /* Kinds 0, 1, 2, 3 and the default share this tail (0x0045D777). */
                o->ctl.gate = 0;
                LeaveRemainsRow((const AM2_MapRow *)row);
                DestroyByType(o);
            }
            goto post;
        }
        o->ctl.gate = 0;
        DestroyByType(o);
        goto post;
    }

    /* 0x0045D7E8 compares the FIELD_59C the seed block loaded, not the
     * record: a vehicle past its first frame with the gate set goes
     * straight to the facing chooser. The reconstruction tested the record's
     * first word here -- the heading byte, never zero -- and jumped past the
     * chooser to the second tail, so no vehicle was ever steered by the
     * player or the AI: every truck sat where the mission placed it and the
     * player's keys did nothing once Sarge had boarded. Found by loading one
     * save in the native build and in the original and holding D. */
    if (o->ctl.gate == 0
        && (!g_mpSession
            || AM2_THE_COMM->CommMustBroadcast(
                   (int16_t)o->owner))) {
        /* No occupant: the attach arm (0x0045D894), laid out after this one. */
        if (o->riders.count > 0) {
            AM2_Object *first = LookupByUID(o->riders.uids[0]);

            /* A first occupant that no longer resolves is removed; an order
             * already in the record's second word skips the input. Every
             * case then chooses a facing -- one call, whose short join VC6
             * copies into each arm ahead of the shared call at 0x0045D8D6. */
            if (!first)
                ((AM2_Rec *)&o->riders)->ListRemoveAt(0);
            else if (o->ctl.hit == 0) {
                /* The player's vehicle reads the keys and the mouse and
                 * routes; anyone else's runs the AI. */
                if (ObjType2Field548(first)) {
                    Step3Input(o, &o->ctl);
                    Step3RouteAndBoard(o, &o->ctl);
                } else
                    AiStep(o, &o->ctl);
            }
            Step3ChooseFacing(o, &o->ctl);
        } else if (o->field94) {
            AiStep(o, &o->ctl);
            Step3ChooseFacing(o, &o->ctl);
        } else {
            ObjAttachTo(o, (AM2_Object *)0);
            o->fieldC0 = 0;
            o->fieldC2 = 0;
        }
    } else {
        /* 0x0045D8CF: the gate set, or a remote vehicle -- the chooser
         * alone. Laid out last, so it falls into the second tail and the
         * arms above share its call. */
        Step3ChooseFacing(o, &o->ctl);
    }

post:
    /* The second tail. Everything above reaches it. */
    if (g_mpSession
        && !AM2_THE_COMM->CommMustBroadcast(
                              (int16_t)o->owner))
        goto emit;

    if (o->health < 0)
        goto emit;
    if (o->ctl.state != 1)
        goto emit;

    if (o->field44 > AM2_VEH_TURN_LIMIT) {
        o->ctl.field0C = 1;
        o->ctl.state   = AM2_VEH_STATE_TURNING;
    } else if (o->field44 < -AM2_VEH_TURN_LIMIT) {
        o->ctl.state = AM2_VEH_STATE_TURNING;
    }

emit:
    /* Called by NAME, so Step3Drive's own counter cannot move: its only
     * caller is this one and it no longer crosses the patched entry. The
     * blind-counter case, not a cold one. */
    Step3Drive(o, &o->ctl);

    /* THE THIRD TAIL, and the footprint is why it matters. The entry clears
     * this vehicle's footprint and this puts it back, so a reconstruction that
     * stops at the second tail leaves OBJ_OFF_FLAGS short of the footprint
     * bits -- which is exactly how this was caught: `flags=0x200861` against
     * `0x821` on two vehicles in the object dump. */
    if (o->health != 0)
        ObjSetFootprint(o);
}


/* RoachRouteToward is reconstructed above and called by name. */

/* The promote-and-engage block, written out at each of its four sites rather
 * than called, because the original inlines it four times and orig.h already
 * settled that policy for AiStepDefend: "the original does it twice" is a fact
 * about the original. A helper here would read better and record less. */
#define AM2_ROACH_PROMOTE_FOUND(o_, c_)                                       \
    do {                                                                      \
        (o_)->targetUid  = (c_)->found->uid;                                  \
        (c_)->observer   = (c_)->found;                                       \
        (c_)->range      = (c_)->foundRange;                                  \
        (c_)->bearing    = (c_)->foundBearing;                                \
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
 * A single exit, like every other function in this band -- every arm jumps to
 * the ConsiderSightingB tail, so it is written with `goto tail`.
 *
 * Not byte-exact: the out-of-sight arm and the far arm share their routing
 * code, and VC6 keeps the far arm's copy where the image keeps the later one.
 */
void __cdecl RoachBehaviour(AM2_Object *o, AM2_RoachOut *out, AM2_RoachCtx *c)
{
    AM2_Object *seen;

    /* THE FIVE ARMS AND WHICH ONE CALLS CopyByteIfSet. The original nests the
     * two range tests -- `if (RANGE > 0)` first (0x00408654 `test; jle` to the
     * RANGE<=0 arm at 0x0040868D), the far test only inside it (0x00408658
     * `cmp; jle`) -- and then diverges on how each arm reaches the shared
     * ConsiderSightingB tail at 0x00408A42:
     *
     *   far        (RANGE>WANT, 0x00408978)  route, promote found, -> tail
     *   within     (0<RANGE<=WANT, 0x0040866F)  CopyByteIfSet,      -> tail
     *   dest-route (DEST_DIST>12, 0x004086A3)  route, promote found, -> tail
     *   has-leader (arrived+leader, 0x004086FF)  the AI sight test on the
     *                                            leader; out of sight, route
     *                                            to it and promote found
     *                                            (sharing the far arm's code);
     *                                            in sight but beyond WANT,
     *                                            route and promote leader;
     *                                            else (0x004089BB) promote
     *                                            leader, ConsumePending,
     *                                            CopyByteIfSet
     *   no-leader  (arrived, 0x004089F3)  ConsumePending, promote found,
     *                                      CopyByteIfSet, FOLLOW_UID persist
     *
     * The far and dest-route arms jmp STRAIGHT to 0x00408A42 and never call
     * CopyByteIfSet. That matters: RoachRouteToward writes the route heading
     * into the out record's bearing byte, and CopyByteIfSet would overwrite it
     * with the sight-context bearing (`if (c[OBSERVER]) *out = c[BEARING]`).
     * After promote-found that context bearing is the FOUND bearing, so a
     * shared tail calling CopyByteIfSet turned a roach routing toward its
     * destination (heading 0x95) into one facing the object it had just spotted
     * (heading 0x1d) -- the roachattack pump-1443 divergence. Only the three
     * arrived/within arms commit through CopyByteIfSet.
     *
     * FOLLOW_UID (0x00408A3C) is persisted by the NO-LEADER arm alone, gated on
     * the observer slot after the found-promote; the has-leader arm jmps past
     * it, and the two routing arms never reach it. */
    if (c->range > 0) {
        if (c->range
            > c->wantRange) {
            /* Far: route toward the observer, promote a fresh sighting, and
             * commit WITHOUT CopyByteIfSet so the route heading survives. */
            o->dest = c->observer->pos;
            RoachRouteToward(o, out, c);
            if (c->found)
                AM2_ROACH_PROMOTE_FOUND(o, c);
            goto tail;
        }
        /* Within range: commit the sighting through CopyByteIfSet. */
        CopyByteIfSet(o, out, c);
        goto tail;
    }

    if (c->destDist > AM2_AI_REACHED_DIST) {
        /* Not arrived: route toward the remembered destination, promote a
         * fresh sighting, and commit without CopyByteIfSet (as the far arm). */
        o->dest = o->scriptPoint;
        RoachRouteToward(o, out, c);
        if (c->found)
            AM2_ROACH_PROMOTE_FOUND(o, c);
        goto tail;
    }

    /* Arrived. */
    o->scriptPoint = g_zeroPoint;

    /* The leader triple into the observer slot, re-reading the leader. */
#define PROMOTE_LEADER()                                                      \
    do {                                                                      \
        o->targetUid = c->leader->uid;                                        \
        c->observer  = c->leader;                                             \
        c->range     = c->leadRange;                                          \
        c->bearing   = c->leadBearing;                                        \
    } while (0)
    seen = c->leader;
    if (seen) {
        /* HAS-LEADER (0x004086FF): the line-of-sight test the AI steps
         * share, with the leader as the target. Out of sight, route toward
         * the leader as the far arm routes toward the observer; in sight
         * but further than the roach wants to be, route toward it and adopt
         * it; in sight and near enough, adopt it and commit. */
        AM2_AI_SIGHT_TEST(leader_seen, leader_unseen);

leader_seen:
        if (c->leadRange
            > c->wantRange) {
            o->dest = c->leader->pos;
            RoachRouteToward(o, out, c);
            PROMOTE_LEADER();
            goto tail;
        }
        PROMOTE_LEADER();
        ConsumePendingByte(o, out, c);
        CopyByteIfSet(o, out, c);
        goto tail;

leader_unseen:
        o->dest = c->leader->pos;
        RoachRouteToward(o, out, c);
        if (c->found)
            AM2_ROACH_PROMOTE_FOUND(o, c);
        /* Its own call, not `goto tail`: the far arm's identical block is
         * the one VC6 folds into this, as the image has it. */
        ConsiderSightingB(o, out, c);
        return;
    }
#undef PROMOTE_LEADER

    /* No leader (0x004089F3): take whatever is pending, promote what the scan
     * found, commit it, then persist the FOLLOW_UID -- this arm alone writes
     * OBJ_OFF_FOLLOW_UID (0x00408A3C), gated on the observer slot being set. */
    ConsumePendingByte(o, out, c);
    if (c->found)
        AM2_ROACH_PROMOTE_FOUND(o, c);
    CopyByteIfSet(o, out, c);
    if (c->observer)
        o->followUid = c->observer->uid;
tail:
    ConsiderSightingB(o, out, c);
}


int region_install(void)
{
    /* Two now, so this is no longer a single `return patch_replace`. That
     * shape is exactly how four reconstructions once ended up never being
     * installed, and adding to one without noticing is how it happens -- this
     * one did, and the patch count not moving is what said so. */
    int rc = 0;

    rc |= patch_replace(ADDR_BUILD_TILE_DELTAS,
                        (const void *)BuildTileDeltas, "BuildTileDeltas", 1);
    rc |= patch_replace(ADDR_POINT_RULE_VEHICLE,
                        (const void *)PointRuleVehicle, "PointRuleVehicle", 1);
    rc |= patch_replace(ADDR_POINT_RULE_BOAT,
                        (const void *)PointRuleBoat, "PointRuleBoat", 1);
    rc |= patch_replace(ADDR_POINT_RULE_DEFAULT,
                        (const void *)PointRuleDefault, "PointRuleDefault", 1);
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
    rc |= patch_replace(ADDR_STEP2_44A420, (const void *)Type2PlayerInput,
                        "Type2PlayerInput", 3);
    rc |= patch_replace(ADDR_TYPE2_PLAYER_STEP, (const void *)Type2PlayerStep,
                        "Type2PlayerStep", 1);
    rc |= patch_replace(ADDR_AI_ROUTE_TOWARD, (const void *)AiRouteToward,
                        "AiRouteToward", 9);
    rc |= patch_replace(ADDR_BUILD_REGION_GRAPH,
                        (const void *)BuildRegionGraph,
                        "BuildRegionGraph", 1);
    rc |= patch_replace(ADDR_OBJ_AFTER_MOVE, (const void *)ObjAfterMove,
                        "ObjAfterMove", 4);
    rc |= patch_replace(ADDR_AI_APPROACH_LEADER,
                        (const void *)AiApproachLeader,
                        "AiApproachLeader", 2);
    rc |= patch_replace(ADDR_ROACH_ROUTE_TOWARD,
                        (const void *)RoachRouteToward,
                        "RoachRouteToward", 3);
    rc |= patch_replace(ADDR_UPDATE_TROOPER_ACTION,
                        (const void *)UpdateTrooperAction,
                        "UpdateTrooperAction", 4);
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
    rc |= patch_replace(ADDR_OBJ_HIT_MASK_ACTION,
                        (const void *)ObjHitMaskAction,
                        "ObjHitMaskAction", 2);
    rc |= patch_replace(ADDR_REGION_FIND_PATH, (const void *)RegionFindPath,
                        "RegionFindPath", 1);
    rc |= patch_replace(ADDR_AI_TROOPER_STEP, (const void *)AiTrooperStep,
                        "AiTrooperStep", 26);
    rc |= patch_replace(ADDR_AI_ATTACK_BODY, (const void *)AiAttackBody,
                        "AiAttackBody", 2);
    rc |= patch_replace(ADDR_AI_406B30, (const void *)AiEngageStep,
                        "AiEngageStep", 1);
    rc |= patch_replace(ADDR_BIG_4057D0, (const void *)AiPatrolStep,
                        "AiPatrolStep", 3);
    rc |= patch_replace(ADDR_BIG_405220, (const void *)AiGuardStep,
                        "AiGuardStep", 1);
    rc |= patch_replace(ADDR_SIGHT_SCAN, (const void *)SightScan,
                        "SightScan", 5);
    rc |= patch_replace(ADDR_FIND_PATH, (const void *)FindPath,
                        "FindPath", 1);
    rc |= patch_replace(ADDR_ITEM_TEARDOWN, (const void *)ItemTeardown,
                        "ItemTeardown", 1);
    rc |= patch_replace(ADDR_BOX_ACTION, (const void *)BoxAction,
                        "BoxAction", 5);
    rc |= patch_replace(ADDR_LIST_BOX_ACTION, (const void *)ListBoxAction,
                        "ListBoxAction", 3);
    rc |= patch_replace(ADDR_LIST_MASK_ACTION, (const void *)ListMaskAction,
                        "ListMaskAction", 3);
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
    rc |= patch_replace(ADDR_TROOPER_FIRE, (const void *)TrooperFire,
                        "TrooperFire", 2);
    rc |= patch_replace(ADDR_STEP3_TURN_BLOCKED,
                        (const void *)Step3TurnBlocked,
                        "Step3TurnBlocked", 4);
    rc |= patch_replace(ADDR_STEP3_45C8D0,
                        (const void *)Step3ChooseFacing,
                        "Step3ChooseFacing", 2);
    rc |= patch_replace(ADDR_STEP3_TURN_STATE,
                        (const void *)Step3TurnState,
                        "Step3TurnState", 1);
    rc |= patch_replace(ADDR_STEP3_ROUTE_BOARD,
                        (const void *)Step3RouteAndBoard,
                        "Step3RouteAndBoard", 1);
    rc |= patch_replace(ADDR_STEP3_DRIVE,
                        (const void *)Step3Drive,
                        "Step3Drive", 2);
    rc |= patch_replace(ADDR_STEP3_INPUT,
                        (const void *)Step3Input,
                        "Step3Input", 2);
    return rc;
}


/* ---- Type2PlayerInput -------------------------------------------------- */
/* Every one of its nineteen callees is reconstructed, so it reaches nothing in
 * the image except GetTickCount, which it takes through the game's own IAT
 * slot the way air.cpp does -- a clock read names no Win32 type, so this
 * module stays flat.
 *
 * OverlayPrepare is reconstructed in win32/surface.cpp and declared here
 * rather than included, because that header reaches windows.h; its two int32
 * parameters name no Win32 type. */
extern "C" void __cdecl OverlayPrepare(int32_t row, int32_t force);

typedef void (__cdecl *am2_char_fn)(uint32_t wparam, int32_t lo, int32_t hi);
/* Spelled orig_get_tick_count, not GetTickCount: naming the Win32 symbol in a
 * flat module fails tools/checksplit.py, and air.cpp already reaches the same
 * IAT slot under exactly this name. */
/* orig_get_tick_count is ALREADY DEFINED at the top of this file, and the
 * two spellings were not equivalent: that one goes through AM2_IMAGE and this
 * one cast the raw address. AM2_IMAGE adds the slide, which is zero in the
 * game and NOT zero under tests/loadimage.h -- and region.cpp is in
 * SELFTEST_SRC. The later definition wins for everything below it, so the
 * three uses down here were reading an unslid address in the offline harness.
 *
 * Found by reading a compiler warning ('orig_get_tick_count' redefined) that
 * a `grep -E 'error'` had been discarding all session. */
#define AM2_CHEAT_ABS(v)  abs(v)


/* Read out of the image rather than retyped. Two of the eight consecutive
 * int32[3] tables already have names (ADDR_AI_MOVE_STATE and its ALT), which
 * is why no single base is introduced over the run. */
/* The weapon-code filter for the aim-cursor write in Type2PlayerInput, from
 * the byte table the original's jump table indexes at 0x0044AD24. It is the
 * dispatch selector for `jmp [ecx*4 + 0x0044AD1C]`: 0 selects the cursor-write
 * arm (0x0044AAB9), 1 the tail. It lives in the image's .TEXT, which the
 * native build fills with 0xCC rather than carrying -- so reading it through
 * its address gave garbage on the standalone, the aim-cursor write never fired
 * for any pointer-mode weapon, and the magnifying glass (and every other
 * aim weapon) aimed one weapon-range along the heading instead of at the
 * cursor. Baked from the image; the values are the original's. checkgap.py
 * cannot see a raw-address `#define`, only ADDR_ macros, which is why this
 * escaped it. Cursor codes: 0x18, 0x19, 0x1A, 0x27, 0x28. */

#define AM2_TURN_REPEAT_MS      0x32
#define AM2_CLICK_THROTTLE_MS   0x12C
#define AM2_CLICK_TAP_MS        0x1F4

/* AngleBetween AS TYPE2PLAYERINPUT'S ORIGINAL SAW IT: returning int. The
 * definition returns a byte and leaves eax's upper bits as they fall, and
 * every other caller stores the byte or masks it; this one does neither --
 * 0x0044A8DF stores al and hands eax to AngleDelta unmasked -- which is an
 * int-returning prototype in scope. Every use here truncates to a byte or
 * masks it, so the upper bits reach nothing. */
#define AngleBetweenInt ((int32_t (__cdecl *)(const AM2_Point *,     \
                                              const AM2_Point *))     \
                         (void (*)(void))AngleBetween)

/* Type2PlayerInput -- original 0x0044A420, one caller: StepType2's player arm,
 * immediately before Type2PlayerStep and passing it the same record. So this
 * is the INPUT half of what the trooper you command does each frame -- keys
 * and mouse into an action -- and the sibling below is the movement half.
 *
 * THE RECORD IT WRITES HAS NO FIELD FAMILY ON PURPOSE. Type2PlayerStep
 * reaches the same structure as bare `w + 4` and `w + 8`, and this follows
 * it: the obvious family is refuted (SIGHT_OFF_RANGE is a dword at 0x14 and
 * a POINT goes there) and the near-miss that fits, SIGHTCOUT_OFF_X, fits by
 * arithmetic rather than by evidence.
 *
 * TWO ARGUMENT SLOTS ARE REUSED AS LOCALS once their arguments are consumed
 * -- the trooper's slot holds the weapon code and the output's holds the
 * cursor distance, both read hundreds of bytes later at a different esp.
 * tools/espmap.py resolves them; reading either as an argument would put a
 * pointer where a small integer belongs.
 *
 * THE ACTION CODES COME FROM EIGHT int32[3] TABLES indexed by
 * ClassifyCode74's 0, 1 or 2. They are read out of the image rather than
 * retyped, which is the rule DirtyCollect's eighty-one arms established: two
 * of the eight already have names here, so no base is introduced over them.
 *
 * ITS SWITCH IS A FILTER. Seventeen weapon codes index a byte table holding
 * only 0 and 1: five also report where the cursor points, twelve do not, and
 * so does every code outside the range. */
void __cdecl Type2PlayerInput(AM2_Object *o, AM2_Object *weapon, AM2_SightOut *out)
{
    AM2_Point  at;                  /* frame +0x10, the cursor in world space */
    AM2_Point  aim;                 /* frame +0x0C */
    int32_t    code;                /* frame +0x1C, the trooper's slot reused */
    int32_t    dist;                /* frame +0x24, the output's slot reused */
    int32_t    cls;
    AM2_Object *hit;
    int32_t    moved = 0;

    if ((*(am2_char_fn *)&g_charHandler) != 0)
        return;
    if (g_inputSuppress != 0)
        return;

    NextInventorySlot(o);

    /* Take the pointer for ourselves the moment it moves over the map. */
    if (PointInRect(g_blitRect, (const AM2_Point *)&g_cursorPoint)
        && (*(intptr_t *)&g_mouseGrab) == 0 && (&g_mouseChanged)[0] != 0)
        (*(intptr_t *)&g_mouseGrab) = -1;

    cls = ClassifyByCode74(o);

    if (o->field574 > 0)
        out->bearing = (uint8_t)o->field574;

    code = weapon != 0
         ? weapon->typeRec->kind
         : 0;

    /* The image adds the packed dwords and keeps the low half for x, then the
     * two high words for y -- the same idiom the cheat console's jumpjets arm
     * uses, and written as halves here for the same reason. */
    at.x  = (int16_t)(*(const int16_t *)&g_cursorPoint + g_viewRect.left);
    at.y  = (int16_t)(*(const int16_t *)(uintptr_t)(((uintptr_t)&g_cursorPoint) + 2)
                      + *(const int16_t *)&g_viewRect.top);
    dist  = ApproxDist(&o->pos, &at);
    /* THE WHOLE POINT IS COPIED AND ONLY ITS Y IS THEN REPLACED. 0x0044A4F2
     * loads slot +0x10 as a DWORD and 0x0044A4FB stores it into slot +0x0C,
     * so `aim` starts as `at`; 0x0044A50D then overwrites aim.y with the
     * overlay-adjusted one. The copy was missing here, so aim.x was an
     * uninitialised local -- six sites read it, including the two that write
     * the fire point into the output record. The symptom was shooting that
     * worked and aimed at nothing in particular. */
    aim   = at;
    aim.y = (int16_t)(ObjOverlayY(o) + at.y);

    /* A target uid that no longer resolves is dropped rather than chased. */
    if (o->ammo != 0
        && LookupByUID(o->ammo) == 0)
        o->ammo = 0;

    /* TURN LEFT and TURN RIGHT, both on a shared repeat delay so that holding
     * a key steps the facing at a fixed rate rather than once per frame. The
     * two arms are the same but for the sign of the 0x10 step. */
    if (ActionKeyDown(3)) {
        o->dest = g_zeroPoint;
        moved = 1;
        OverlayPrepare(1, 1);
        if (g_turnRepeatMs <= 0) {
            uint8_t f = (uint8_t)((o->facing - 0x10)
                                  & 0xF0);
            out->bearing = f;
            o->field574 = f;
            o->deadlineD0 = 0;
            g_turnRepeatMs += AM2_TURN_REPEAT_MS;
        }
    }
    if (ActionKeyDown(2)) {
        o->dest = g_zeroPoint;
        moved = 1;
        OverlayPrepare(1, 1);
        if (g_turnRepeatMs <= 0) {
            uint8_t f = (uint8_t)((o->facing + 0x10)
                                  & 0xF0);
            out->bearing = f;
            o->deadlineD0 = 0;
            o->field574 = f;
            g_turnRepeatMs += AM2_TURN_REPEAT_MS;
        }
    }
    if (g_turnRepeatMs > 0)
        g_turnRepeatMs -= g_frameDeltaMs;

    /* FIRE. One weapon code answers a different action from all the others. */
    if (ActionKeyDown(0)) {
        if (code != 0x14)
            out->state = 2;
        else
            out->state = 0x26;
        o->dest = g_zeroPoint;
        moved = 1;
        OverlayPrepare(1, 1);
    }

    if (ActionKeyPressed(6)) {
        out->state = g_actionCrouch[cls];
        moved = 1;
        o->dest = g_zeroPoint;
    }
    if (ActionKeyDown(6)) {
        o->dest = g_zeroPoint;
        moved = 1;
        OverlayPrepare(1, 1);
    }

    /* The wheel: down takes the same action key 6 does, up is a two-case
     * remap that leaves class 0 alone. */
    if (g_mouseDZ < 0) {
        out->state = g_actionCrouch[cls];
        moved = 1;
        o->dest = g_zeroPoint;
    }
    if (g_mouseDZ > 0) {
        if (cls == 2)
            out->state = 4;
        else if (cls == 1)
            out->state = cls;
        o->dest = g_zeroPoint;
        moved = 1;
    }

    if (ActionKeyPressed(7)) {
        out->state = g_actionProne[cls];
        moved = 1;
        o->dest = g_zeroPoint;
    }
    if (ActionKeyPressed(4)) {
        out->state = g_actionKey4[cls];
        moved = 1;
        o->dest = g_zeroPoint;
    }
    if (ActionKeyPressed(5)) {
        out->state = g_actionKey5[cls];
        moved = 1;
        o->dest = g_zeroPoint;
    }
    if (ActionKeyDown(1)) {
        out->state = g_actionKey1[cls];
        moved = 1;
        o->dest = g_zeroPoint;
        OverlayPrepare(1, 1);
    }
    if (ActionKeyDown(0xA)) {
        out->state = WeaponPoseIndex(o, weapon);
        moved = 1;
        o->dest = g_zeroPoint;
        OverlayPrepare(1, 1);
    }

    /* AIM, held. Two weapon codes aim at the cursor; the rest aim at nothing. */
    if (ActionKeyDown(9)) {
        out->hit = 1;
        if (weapon->typeRec->automatic != 0)
            out->seen = 1;
        out->spot.at.x = 0;
        {
            int32_t k = weapon->typeRec->kind;
            if (k >= 0x27 && k <= 0x28) {
                out->spot.at.x = (uint16_t)(g_viewRect.left + g_cursorX);
                out->spot.at.y = (uint16_t)(g_viewRect.top + g_cursorY);
            }
        }
    }

    /* AIM, released. A third code group aims at the cursor, and one aims at
     * the trooper's own feet. */
    if (ActionKeyReleased(9)) {
        int32_t k;

        out->seen = 1;
        out->spot.at.x = 0;
        k = weapon->typeRec->kind;
        if (k != 0xB) {
            if (k > 0x17 && k <= 0x1A) {
                out->spot.at.x = (uint16_t)(g_viewRect.left + g_cursorX);
                out->spot.at.y = (uint16_t)(g_viewRect.top + g_cursorY);
            }
        } else {
            out->spot.at.x = (uint16_t)o->pos.x;
            out->spot.at.y = (uint16_t)o->pos.y;
        }
    }

    /* From here the pointer drives. Anything that took the map's pointer for
     * another purpose, or a pointer outside the map, skips to the tail. */
    if ((*(intptr_t *)&g_mouseGrab) != -1) {
        if (!PointInRect(g_blitRect,
                         (const AM2_Point *)&g_cursorPoint))
            goto tail;
        if ((*(intptr_t *)&g_mouseGrab) != 0)
            goto tail;
    }

    if ((&g_mouseButton)[0] != 0) {
        int32_t bearing;

        (*(intptr_t *)&g_mouseGrab) = -1;
        /* THE DISTANCE IS TO `at` AND THE BEARING IS TO `aim`, and they are
         * one slot apart: `lea eax,[esp+0x14]` at 0x0044A8C0 is +0x10 with
         * one push outstanding, `lea ecx,[esp+0x18]` at 0x0044A8DA is +0x0C
         * with three. Both were written as `at`. With the rifle raised the
         * overlay is three pixels, so a held click aimed two facing units
         * off the original's -- invisible to every replay, found by
         * tools/sidebyside.py on a hand-played click, and settled by logging
         * AngleBetween's arguments in both games (AM2_TRACE_ANGLE). */
        int32_t reach = ApproxDist(&o->pos, &at);
        bearing = AngleBetweenInt(&o->pos, &aim);
        out->bearing    = (uint8_t)bearing;

        /* Close in, or already facing far enough away, keep the old facing.
         * The facing is read inside the test, after the distance checks, as
         * the image loads it; held in a local it was read ahead of them
         * (closer registers, 90 bytes off against 104, but the wrong
         * shape). Two arms with one body each rather than an `||`: the
         * merged form takes one scratch register more before the test. */
        if (reach < 6)
            out->bearing = o->facing;
        else if (reach < 0x18
                && AngleDelta(bearing, o->facing)
                     < 0x40)
            out->bearing = o->facing;

        o->field574 = out->bearing;
    }

    if (GetMenuRow() == 1) {
        if ((&g_mouseButton)[0] != 0) {
            (*(uint32_t *)&g_throttleDeadline) = g_gameClockMs + AM2_CLICK_THROTTLE_MS;
            out->hit = 1;
            if (weapon->typeRec->automatic != 0)
                out->seen = 1;
            out->spot.at.x = (uint16_t)aim.x;
            out->spot.at.y = (uint16_t)aim.y;
            o->dest = g_zeroPoint;

            if (ApproxDist(&o->pos, &aim) < 0x18) {
                out->bearing = o->facing;
                out->spot.at.x = 0;
                out->spot.at.y = 0;
            } else {
                int32_t b = AngleBetweenInt(&o->pos,
                                         &aim);

                out->bearing = (uint8_t)b;
                if (AM2_CHEAT_ABS((b & 0xFF) - o->facing) < 0x10) {

                    /* Both roundings use the SAME directions count; only the
                     * angle differs, so a difference means the click would
                     * change the drawn facing. */
                    /* The directions count is read afresh for each call,
                     * as the image has it. */
                    if (RoundTo8(o->facing,
                                 o->rowSet.rows->animPlaying->directionBits) != RoundTo8(out->bearing, o->rowSet.rows->animPlaying->directionBits)) {
                        out->bearing = (uint8_t)(o->facing + AngleDelta(o->facing,
                                out->bearing) / 2);
                        out->spot.at.x = 0;
                        out->spot.at.y = 0;
                    }
                }
            }

            if ((&g_mouseChanged)[0] != 0)
                o->deadlineD0 = 0;
            o->field574 = out->bearing;

            hit = WalkCellWrapper(o, aim);
            if (hit != 0) {
                out->spot.ground = (uint16_t)(ObjHeight(hit) - 4);
                o->ammo =
                    (int32_t)hit->uid;
            } else {
                out->spot.ground = (uint16_t)ObjHeight(o);
                o->ammo = 0;
            }

            /* THE FILTER. Five weapon codes also report where the cursor is
             * pointing; twelve, and everything outside 0x18..0x28, do not. */
            {
                /* THE WEAPON's +0xC0, not the trooper's: 0x0044AA92 reads
                 * frame slot +0x20, which is ARG2 -- there is an outstanding
                 * push there, so the raw `[esp+0x24]` is one slot lower than
                 * it looks, and espmap says so. The five sibling reads in
                 * this function all spell it `weapon`; this one said `o`,
                 * and a trooper's +0xC0 is a packed point, which is (0,0)
                 * for a standing soldier -- so shift-clicking to fire at a
                 * spot dereferenced NULL and took the process down. The
                 * original shoots: its object count goes 1612, 1613, 1612.
                 * Fifth instance of the push-shifted slot in this project. */
                switch (weapon->typeRec->kind) {
                case 0x18: case 0x19: case 0x1A: case 0x27: case 0x28:
                    out->spot.at.x =
                        (uint16_t)(g_viewRect.left + g_cursorX);
                    out->spot.at.y =
                        (uint16_t)(g_viewRect.top + g_cursorY);
                    break;
                }
            }
            goto tail;
        }

        /* Button up: only a pointer that actually moved does anything. */
        if ((&g_mouseChanged)[0] == 0)
            goto tail;

        out->spot.at.x = (uint16_t)aim.x;
        out->seen = 1;
        out->spot.at.y = (uint16_t)aim.y;
        o->dest = g_zeroPoint;
        out->bearing = (uint8_t)AngleBetween(&o->pos, &aim);
        o->deadlineD0 = 0;
        o->field574 = out->bearing;

        hit = WalkCellWrapper(o, aim);
        if (hit != 0) {
            out->spot.ground = (uint16_t)(ObjHeight(hit) - 4);
            o->ammo =
                (int32_t)hit->uid;
        } else {
            out->spot.ground = (uint16_t)ObjHeight(o);
            o->ammo = 0;
        }
        goto tail;
    }

    /* Not over the map proper. A key that already acted wins, and the click
     * is throttled so that holding the button does not re-order every frame. */
    if (moved != 0)
        goto tail;
    if (g_gameClockMs <= (*(uint32_t *)&g_throttleDeadline))
        goto tail;

    if ((&g_mouseButton)[0] == 0) {
        if ((&g_mouseChanged)[0] == 0)
            goto tail;

        /* A TAP: released soon after it was pressed and barely moved since.
         * Both bounds are the press record's, not this frame's. */
        if (orig_get_tick_count() - g_mousePressMs < AM2_CLICK_TAP_MS
            && ApproxDist((const AM2_Point *)&g_cursorPoint,
                          (const AM2_Point *)&g_mousePress[0].point) < 4) {
            out->bearing = (uint8_t)AngleBetween(&o->pos,
                                         &at);
            o->field574 = out->bearing;
            o->dest = at;
            /* The snap writes back into FIELD_C0, not the local: the original
             * (0x0044AC0A) loads `lea eax,[esi+0xc0]` for the out pointer and
             * hands the SAME &FIELD_C0 to NearestAllowedTile, so the move goal
             * becomes the nearest allowed TILE CENTRE, not the raw click. This
             * had been `&at`, which left FIELD_C0 unsnapped -- on a quick
             * release the drawn facing came off the raw point (218) where the
             * original faces the snapped one (212). Invisible to every replay
             * but a hand-played click; `sessions/aim-3261.txt` reproduces it. */
            NearestAllowedTile(o, TileOfPoint(at),
                               &o->dest);
            o->deadlineD0 = 0;
            o->field10C = 1;
            o->prevRegion = (int16_t)0xFFFF;
            if (GetMenuRow() == 8)
                o->uid56C = (int32_t)g_pointerHoverUid;
            else
                o->uid56C = 0;
        }
        if ((&g_mouseButton)[0] == 0)
            goto tail;
    }

    /* Held, and near enough that this is a turn rather than a march. */
    if (dist <= 0x20 && orig_get_tick_count() - g_mousePressMs < AM2_CLICK_TAP_MS) {
        out->bearing = (uint8_t)AngleBetween(&o->pos, &at);
        o->field574 = out->bearing;
        o->dest = g_zeroPoint;
        o->deadlineD0 = 0;
        goto tail;
    }

    o->field10C = 0;
    o->dest = at;
    if (GetMenuRow() == 8)
        o->uid56C = (int32_t)g_pointerHoverUid;
    else
        o->uid56C = 0;

tail:
    /* The run key, last, and it can PROMOTE the action chosen above. */
    if (ActionKeyDown(8)) {
        o->moveStateAlt = 1;
        if (out->state == 2)
            out->state = 3;
    } else {
        o->moveStateAlt = 0;
    }
}

/* 0x0045C6E0, four callers, all in ADDR_STEP3_45C8D0. Choose the vehicle's
 * turn for this frame and answer whether the step it implies is BLOCKED.
 *
 * THREE ARGUMENTS. Its call sites end `add esp, 0x18`, which says six --
 * that cleanup also covers the SetFacing08 call above it. espmap finds three
 * argument slots and one local, and the pushes agree.
 *
 * BOTH ARGUMENT SLOTS ARE REUSED AS LOCALS, so the original's own reads of
 * them stop meaning what they meant: `lea ecx, [esp+0x1C]` takes the address
 * of ARG2's slot for MoveStepPoint's out-point, and `mov [esp+0x18], al`
 * overwrites ARG1's with the rounded facing. Written straight through, the
 * last call reads as VehicleBlockWeight(obj, obj, ...). Here they are plain
 * locals, which is what the original is doing with them.
 *
 * `esi` ALSO CHANGES MEANING: from 0x0045C76F it is the object's ROW, not the
 * object -- and both records carry fields at the offsets used after it.
 *
 * THE ARGUMENT ORDER IS (obj, plan, out) AND I HAD IT REVERSED. espmap gives
 * the slots and the callee's registers, and both were read correctly -- but
 * neither says WHICH caller value lands in which slot. The call site does:
 * `push &out; push edi; push esi`, and in ADDR_STEP3_45C8D0 esi is the object
 * and edi is the plan. Written the other way round every field access lands
 * on the other record, nothing inside the function looks wrong, and it
 * compiles. Same shape as ADDR_ENTER_VEHICLE, which this file records. */
int32_t __cdecl Step3TurnBlocked(AM2_Object *o, const AM2_SteerOut *p,
                                 int32_t *out)
{
    AM2_MapRow *row;
    int32_t speed;
    int32_t limit;
    uint8_t nowFacing, thenFacing;
    uint32_t elapsed;
    int32_t commit;
    AM2_Point pt;

    /* THE SPEED IS ZERO UNLESS THE WHOLE BLOCK RUNS. The original zeroes its
     * local at 0x0045C6F3 -- BEFORE the `cmp [p+8], 1; je` that skips
     * everything below -- so a plan with [p+8] == 1 reaches MoveStepPoint
     * with speed 0 rather than with the speed it would have computed.
     * Computing it first and branching around only the limiter is the
     * natural transcription and moves the vehicle. */
    speed = 0;
    if (p->state != 1) {
        int32_t want_speed;

        want_speed = p->field0C != 0
                     ? o->field55C
                     : o->field558;
        /* Two independent `sar esi, 1`, not one test of two bits: both flags
         * set quarters it. */
        if (p->approach != 0)
            want_speed >>= 1;
        if (p->stop != 0)
            want_speed >>= 1;

        /* A FLOOR in the first arm and a CEILING in the second. The original
         * compares jle and jge; swapping them is invisible whenever the
         * desired speed is already inside the band. Each arm scales the
         * acceleration by the frame's float delta itself (int * float). */
        if (p->field0C != 0) {
            limit = (int32_t)((double)o->field44
                              - o->accelRate
                                * g_frameDeltaSec);
            speed = (want_speed > limit) ? want_speed : limit;
        } else {
            limit = (int32_t)(o->accelRate
                                * g_frameDeltaSec
                              + (double)o->field44);
            speed = (want_speed < limit) ? want_speed : limit;
        }
    }

    row = o->rowSet.rows;

    /* Written through `out` twice, as RoachStepAllowed does: the raw delta,
     * then its clamp. The animation is read through the row at each use,
     * and the two facings are bytes. */
    *out = AngleDelta(o->facing, p->heading);
    *out = Clamp(*out, -8, 8);

    nowFacing  = (uint8_t)RoundTo8((row->headingBias + row->heading) & 0xFF,
                                   row->animPlaying->directionBits);
    thenFacing = (uint8_t)RoundTo8((row->headingBias + row->heading + *out)
                                   & 0xFF,
                                   row->animPlaying->directionBits);

    if (nowFacing != thenFacing) {
        commit = (o->turnRecommit != 0
                  || thenFacing != o->turnCommitted);
        elapsed = g_gameClockMs - row->turnStamp;

        if ((elapsed > o->turnInterval
             && commit) || elapsed > 0x320)
            o->turnCommitted = nowFacing;
        else
            *out = 0;
    }

    MoveStepPoint(o, (uint8_t)(*out + o->facing),
                  0, speed, 0, 0, &pt);
    return VehicleBlockWeight(o, thenFacing, pt, 1) > 0x0E;
}

/* 0x0045C8D0, two callers in StepType3. Choose the vehicle's facing for this
 * frame: propose a candidate heading, ask Step3TurnBlocked whether the step it
 * implies is walkable, and on refusal try the next candidate -- up to SEVEN.
 *
 * The candidate walk is `[+0x574]++` and then half of it added to or
 * subtracted from `[+0x570]` masked to 7, the sign chosen by the counter's low
 * bit: so it fans out alternately either side of the base heading. The same
 * three lines appear three times in the original and are one helper here.
 *
 * TWO PATHS CONVERGE ON ONE LOOP. Under 1200 ms since the last turn it goes
 * straight to the search; past that it tests the current heading first and,
 * only if the base is still zero, seeds it from FacingFromDelta08. That is
 * the same converging-tails shape StepType3's own comment describes.
 *
 * AND ITS TWO CALLS TO Step3TurnBlocked PROVE THAT FUNCTION'S ARITY. One is
 * followed by `add esp, 0x18` and the other by `add esp, 0x0C` -- the first
 * cleans SetFacing08's three arguments as well as its own three, the second
 * has no preceding call and cleans exactly three. Three, from the image. */
static inline int32_t Step3NextCandidate(const AM2_Object *o)
{
    int32_t n = o->searchTries;
    int32_t r;

    if (n & 1)
        r = (o->searchBase - (n >> 1)) & 7;
    else
        r = (o->searchBase + (n >> 1)) & 7;
    return r;
}

void __cdecl Step3ChooseFacing(AM2_Object *o, AM2_SteerOut *out)
{
    int32_t  local;
    int32_t  tries = 0;

    /* THREE early returns at 0x0045C8D0, and only the last was reconstructed.
     * Missing the second drove the AI candidate search on the PLAYER's
     * vehicle: with the player steering, Step3TurnBlocked's search fought the
     * input, and on the frame the truck met a tree it picked a facing that
     * was NOT blocked -- so the hull turned aside, gunFacing went 0x1f -> 0,
     * Step3Drive's block check sampled the wrong mask direction, missed the
     * tree and drove through. The original skips the whole search for a
     * player-driven vehicle. (sessions/truckTree-34847.txt.)
     *
     * ALL THREE early returns, AND the "current heading is clear" exit of the
     * past-the-interval arm, do NOT just return: they RESET the search state
     * (0x0045CA85 sets both 0x570 and 0x574 to 0 with ebx==0). 0x574 is the
     * PERSISTENT facing-search counter, so failing to reset it here let it
     * accumulate across frames on the port where the original zeroed it -- a
     * follower held in formation searched, gave up satisfied, and never reset,
     * so the counter drifted and picked different candidate facings a few
     * frames later. Only the SEARCH-LOOP exits below, and the short-interval
     * arm's own clear exit, return without the reset. (vehmove2-6591.txt.) */

    /* A networked client only steers a vehicle it owns. */
    if (g_mpSession
        && !AM2_THE_COMM->CommMustBroadcast(
                              (int16_t)o->owner))
        goto stop;

    /* A vehicle a player is riding is steered by the player, not the AI
     * candidate search below -- unless FIELD_10C says otherwise. */
    if (ListFirstField548(o) != 0
        && o->field10C == 0)
        goto stop;

    /* Nothing to decide when the record already wants the current facing and
     * its state says so. */
    if (out->heading == o->facing && out->state == 1)
        goto stop;

    if (g_gameClockMs - o->blockedSince < 0x4B0u) {
        SetFacing08(Step3NextCandidate(o), o, out);
        if (!Step3TurnBlocked(o, out, &local))
            return;
        o->blockedSince = g_gameClockMs;

        /* The search. Seven attempts, fanning out alternately either side of
         * the base heading; the counter is PERSISTENT in the object, so a
         * vehicle that gave up last frame resumes where it left off rather
         * than starting over. Each arm has its own copy of the loop. */
        for (;;) {
            o->searchTries += 1;
            SetFacing08(Step3NextCandidate(o), o, out);
            if (!Step3TurnBlocked(o, out, &local))
                return;
            tries++;
            if (tries >= 7)
                return;
        }
    }

    /* Past the interval: test the heading as it stands first. The clear exit
     * resets the search state (0x0045CA05 -> 0x0045CA85). */
    if (Step3TurnBlocked(o, out, &local)) {
        o->blockedSince = g_gameClockMs;
        if (o->searchBase == 0)
            o->searchBase = FacingFromDelta08(out, local);
        for (;;) {
            o->searchTries += 1;
            SetFacing08(Step3NextCandidate(o), o, out);
            if (!Step3TurnBlocked(o, out, &local))
                return;
            tries++;
            if (tries >= 7)
                return;
        }
    }

    /* 0x0045CA85: the reset, reached by goto from the three early exits and
     * as the fall-out of a clear heading past the interval. */
stop:
    o->searchBase = 0;
    o->searchTries = 0;
}

/* 0x0045CAB0, one caller. Decide the vehicle's turn STATE from the gap
 * between the record's wanted facing and the object's current one.
 *
 * State 4 is reset to 2 on the way in, unconditionally and before anything
 * else looks at it. */
void __cdecl Step3TurnState(AM2_Object *o, AM2_SteerOut *r)
{
    int32_t  tol;
    int32_t  delta;

    if (r->state == 4)
        r->state = 2;

    if (r->heading == o->facing)
        return;

    /* EITHER key tightens it. The original tests 3 first and falls through to
     * 2 only when 3 is up, so reading one and stopping leaves the other key
     * sluggish -- and nothing in this suite reaches a driven vehicle. */
    tol = (ActionKeyDown(3) || ActionKeyDown(2)) ? AM2_STEP3_TURN_TOL_KEY
                                                 : AM2_STEP3_TURN_TOL_IDLE;

    delta = AngleDelta(r->heading, o->facing);
    if (delta > tol)
        r->state = 7;
    else if (delta < -tol)
        r->state = 6;
}

/* 0x0045D4B0, one caller in StepType3. Four jobs in one function: route the
 * vehicle toward its destination, decide whether the heading needs
 * correcting, snap the view to it, and BOARD any selected units.
 *
 * THE TOLERANCE LADDER IS THE INTERESTING PART: the angle error needed to
 * trigger a correction GROWS with distance -- 0x10 under 0x40, 0x20 under
 * 0x60, 0x40 beyond. A vehicle nearly there corrects for a small error; one
 * far away tolerates a large one.
 *
 * `test ah, 4` IS BIT 10, not bit 2. The original tests the HIGH byte of the
 * dword at +8, so the mask is 0x400; `& 4` on the low byte is a different
 * flag and compiles just as well.
 *
 * +0x548 is left as a raw offset. orig.h has OBJ_OFF_SARGE there AND
 * AM2_TYPE3_RECORD_SIZE with the same value, and esi is demonstrably the
 * vehicle at that instruction -- loaded once at 0x0045D4B6 and never
 * reassigned, with LookupByUID's result going to a different register later.
 * Naming it either way asserts something this function cannot settle. */
void __cdecl Step3RouteAndBoard(AM2_Object *o, AM2_SteerOut *r)
{
    int32_t i;

    /* field10C splits this into TWO branches (0x0045D4CF), not one. When a
     * route is in progress the original runs AiRouteToward and ONLY an
     * arrival check -- if it has not arrived it jumps to the tail, leaving
     * the heading, the turret and the tolerance flags alone. The
     * heading/turret block (including `if (!field548) aim = heading`, which
     * drags the turret onto the hull) runs ONLY when field10C == 0. Folding
     * them into one `if arrived / else` ran that block during a route too, so
     * a half-track under a move order swung its manually-aimed gun back to
     * the hull -- the turret facing (+0x530) then diverged. (0x45d4d9 vs
     * 0x45d51a.) */
    if ((uint16_t)o->dest.x != 0) {
        /* The context AiRouteToward is handed and never reads: 0x44 bytes,
         * the whole of the image's frame. */
        AM2_AiCtx local;

        if (o->field10C != 0) {
            AiRouteToward(o, &o->ctl, &local, 1);

            if (ApproxDist(&o->pos, &o->dest) < 0x30) {
                r->state    = 1;
                o->field10C = 0;
            }
            /* else: routing but not arrived -- do NOT touch heading/turret. */
        } else {
            int32_t d;
            int32_t dist;

            /* No local for the bearing: it goes into the record and is
             * read back from there. */
            r->heading = AngleBetween(&o->pos, &o->dest);
            r->state   = 4;
            d    = abs(AngleDelta(r->heading, o->facing));
            dist = ApproxDist(&o->pos, &o->dest);
            if (o->field548 == 0)
                r->aim = r->heading;

            /* Each distance band has its own limit and its own pair of
             * stores; VC6 merges the three pairs into one. */
            if (dist < 0x40) {
                if (d >= 0x10) {
                    r->stop     = 1;
                    r->approach = 1;
                }
            } else if (dist < 0x60) {
                if (d >= 0x20) {
                    r->stop     = 1;
                    r->approach = 1;
                }
            } else if (d >= 0x40) {
                r->stop     = 1;
                r->approach = 1;
            }
        }

        /* Inside the dest test: with no destination there is nothing to
         * reset (the branch at 0x0045D4C9 skips it). */
        if (o->field10C == 0)
            o->dest = g_zeroPoint;
    }

    if (o->field44 == 0)
        return;

    if (orig_get_tick_count() - g_mousePressMs
            > 0x1F4u
        && o->field10C == 0) {
        g_viewSnap = 1;
        g_objCtxSet = 1;
    }

    if ((o->flags & 0x400u) == 0)
        return;

    /* Five separate guards in the original, each its own branch to the loop
     * tail. Kept separate rather than ANDed: equivalent today, and the order
     * is what the image states. */
    for (i = 0; i < g_selectedCount; i++) {
        AM2_Object *other = LookupByUID(g_selectedItems[i]);

        if (other == 0)
            continue;
        if (other == o)
            continue;
        if ((other->flags & 4u) != 0)
            continue;
        if ((uint16_t)other->health == 0)
            continue;
        if (!ObjIsTypeIn238(other))
            continue;
        if (other->field94 != 0)
            continue;

        other->aiMode = 3;
        ObjAttachTo(other, o);
    }
}

/* Both senders are still the image's, and both are reached by address. Their
 * signatures come from their own log lines' push order, not from these call
 * sites -- see orig.h, where the argument identity is fixed by the
 * instruction that settles it. */
/* The image's own rand, reached through AM2_IMAGE the way event.cpp and
 * air.cpp reach it -- it lives above the CRT line, so the selftest's slide
 * applies. */
typedef int32_t (__cdecl *AM2_Step3RandFn)(void);
#define orig_step3_rand ((AM2_Step3RandFn)((void *)(uintptr_t)(ADDR_GAME_RAND)))

/* 0x0045CB30, one caller in StepType3, and the largest function in the
 * vehicle block at 769 instructions. The whole per-frame step of a driven
 * vehicle, and it does seven jobs in this order: aim the turret, pick the
 * frame's target point, choose a speed, take the step, fire, sound the
 * engine, drag the passengers along, and pick up what is under the new
 * position.
 *
 * ITS TWO SOUND TABLES ARE THE THIRD INSTANCE IN THIS PROJECT OF SLOTS
 * SHARING AN ARM, and reading the bodies top to bottom gets it wrong in two
 * separate ways. Both tables are six entries over four bodies: states 2 and
 * 3 point at the SAME arm, and state 4 points at the function's tail and
 * plays nothing at all. Numbering the four bodies 0..3 as they are laid out
 * would give state 3 its own sound and give state 4 one it never plays.
 * Generated from the image at 0x0045D474 and 0x0045D48C rather than
 * transcribed, which is what CLAUDE.md's DirtyCollect note argues for.
 *
 * THE ARGUMENT SLOT IS REUSED TWICE, which makes an espmap read as though
 * the function had more arguments than it has. Once esi holds the object,
 * arg1's slot becomes the frame's target POINT (written at 0x0045CC71), and
 * once the record is finished with, arg2's slot becomes the passenger loop's
 * counter (written at 0x0045D24F). Both are the pattern CLAUDE.md records;
 * neither is visible without tracking esp.
 *
 * AND A LINEAR ESP TRACE IS WRONG THROUGH THE FIRING BLOCK. Two paths join
 * at the FireWeapon call, each pushing 28 bytes of `spot` and `target`, and
 * the single `add esp, 0x1c` cleans one of them -- so a tool that walks the
 * instruction stream counts both and every slot below it reads four bytes
 * out. That drift is what makes 0x0045D10E look like it reads a local when
 * it reads arg2, which the early `je` into that same address settles: at
 * that jump esp is untouched, so [esp+0x34] is unambiguously E+8.
 *
 * TWO CALLS WERE LONG LEFT OUT and are now in, for byte-matching: the log
 * under _MSC_VER only, the AngleBetween in both aimed arms. What follows is
 * why each was safe to omit.
 *
 * The first is the pickup refusal at 0x0045D3E8, which calls 0x0045CAA0 with
 * two arguments and NO format string. That address is a bare `ret` in this
 * retail image -- `c3` -- so the call does nothing whatever. But it is also
 * ADDR_LOG, which src/inject/gamelog.c patches into a real varargs logger,
 * so emitting the call would hand our logger an object pointer as its format
 * and crash on a path the original cannot fail on. Reproducing the
 * instruction would introduce the defect, not avoid it.
 *
 * The second is the AngleBetween at 0x0045D02A, whose result is overwritten
 * before it is read. It is a pure reconstructed function, so calling it and
 * discarding the answer is indistinguishable from not calling it.
 *
 * The `push esi` before CanPickUp at 0x0045D358 IS reproduced, as an unread
 * second argument: the callee reads only its first, so the vehicle pushed
 * after it changes nothing that executes, and with it the call is the
 * image's two pushes and `add esp, 8`. */
void __cdecl Step3Drive(AM2_Object *o, AM2_SteerOut *r)
{
    int32_t   speed = 0;          /* ebp, spilled to E-28 */
    int32_t   tileArg = 0;        /* E-4, zeroed at entry and read once */
    AM2_Point  step;               /* E-20, MoveStepPoint's out point; never
                                   * read before that call fills it, and the
                                   * image's prologue zeroes only two slots */
    uint8_t   facing;             /* E-24, after it stops holding the turn */
    uint8_t   gunFacing;          /* E-16 */
    AM2_Anim *const *animRec;     /* E-12, the address of a row's animPlaying */

    if ((uint16_t)o->health != 0) {
        AM2_MapRow *gun;
        int32_t   turn;
        int32_t   noTarget;
        uint8_t   first;
        uint8_t   second;

        if (o->vehicleKind == 5)
            Step3TurnState(o, &o->ctl);

        /* The hull's turn for this frame, capped at 8 either way. */
        gun = o->rowSet.rows;
        turn = AngleDelta(o->facing, r->heading);
        turn = Clamp(turn, -8, 8);

        /* The whole first block reads through `gun`, so the row pointer is
         * held across the calls instead of re-read from the object. */
        animRec = &gun->animPlaying;
        first  = RoundTo8((gun->headingBias + gun->heading) & 0xFF,
                          (*animRec)->directionBits);
        gunFacing = RoundTo8((gun->headingBias + turn
                           + gun->heading) & 0xFF,
                          (*animRec)->directionBits);

        /* A sprite change is rate-limited: it needs either the per-vehicle
         * interval at +0x564 to have elapsed, or 800 ms outright. The
         * `changed` term below is the original's, ugly ordering and all --
         * it asks whether +0x5A8 is set OR the rounded value differs from
         * what was last committed. */
        if (first != gunFacing) {
            int32_t changed = (o->turnRecommit != 0
                               || gunFacing != o->turnCommitted);
            uint32_t age = g_gameClockMs
                           - gun->turnStamp;

            if ((age > o->turnInterval && changed)
                || age > 0x320u) {
                o->turnCommitted = first;
                gun->turnStamp = g_gameClockMs;
            } else {
                turn = 0;
            }
        }
        facing = (uint8_t)(o->facing + turn);

        /* State 1 skips target selection and the speed ramp entirely and
         * goes straight to the step below. */
        if (r->state != 1) {
            AM2_Point target;
            /* The speed the ramp wants, before the limiter below settles
             * what `speed` itself becomes: a separate variable, so only
             * the limiter's join and the stuck arms write the frame slot. */
            int32_t want;
            /* The frame's target: the next waypoint if one is pending,
             * otherwise the standing destination at +0xC0. */
            if (o->moveAt
                    < o->moveCount)
                target = o->waypoints[o->moveAt];
            else
                target = o->dest;

            noTarget = ((uint16_t)target.x == 0);

            if (!noTarget
                && ApproxDist(&o->pos,
                              &target) < 0xC0) {
                int32_t d = AngleDelta(o->facing,
                                       *(const uint8_t *)r);
                d = abs(d);
                if (d > 0x40)
                    r->field0C = 1;
            }

            want = r->field0C != 0
                        ? o->field55C
                        : o->field558;

            if (!noTarget
                && (uint16_t)o->fieldC0 == (uint16_t)target.x
                && ApproxDist(&o->pos,
                              &target) < 0x60)
                r->stop = 1;
            else
                r->stop = 0;

            MoveStepPoint(o, facing, 0, want, 0, 0, &step);

            if (!noTarget
                && (uint16_t)o->fieldC0 == (uint16_t)target.x
                && (uint16_t)target.x != 0) {
                if (ApproxDist((const AM2_Point *)&step, &target) - 1
                        <= ApproxDist(&o->pos, &target)
                    && ApproxDist(&o->pos,
                                  &o->scriptPoint) >= 0x30)
                    r->approach = 0;
                else
                    r->approach = 1;
            }

            if (r->approach != 0)
                want >>= 1;
            if (r->stop != 0)
                want >>= 1;

            /* The acceleration limiter, and the two arms clamp from
             * OPPOSITE sides: slowing down takes the larger of the two,
             * speeding up the smaller. */
            if (r->field0C != 0) {
                int32_t v = (int32_t)((float)o->field44
                                      - (float)o->accelRate
                                        * g_frameDeltaSec);
                if (want <= v)
                    speed = v;
                else
                    speed = want;
            } else {
                int32_t v = (int32_t)((float)o->accelRate
                                        * g_frameDeltaSec
                                      + (float)o->field44);
                if (want >= v)
                    speed = v;
                else
                    speed = want;
            }

            /* +0xD8 counts consecutive blocked frames and scales the speed
             * by it -- again in opposite directions on the two arms. */
            /* The product is formed in each arm, as the image multiplies
             * twice; hoisting it ahead of the test is four off the shape.
             * ApproxDist's arguments above are (position, target), the
             * order the image pushes, and the distance is symmetric. */
            if ((uint32_t)o->stuckCount > 0 && speed != 0) {
                if (r->field0C != 0) {
                    int32_t t = (int32_t)o->stuckCount * speed;
                    int32_t c = o->field55C;
                    speed = (c > t) ? c : t;
                } else {
                    int32_t t = (int32_t)o->stuckCount * speed;
                    int32_t c = o->field558;
                    speed = (c < t) ? c : t;
                }
            }
        }

        MoveStepPoint(o, facing, 0, speed, 0, 0, &step);

        if (VehicleBlockWeight(o, gunFacing, step, speed != 0) > 0x0E) {
            facing = o->facing;
            speed = 0;
            o->stuckCount += 1;
        } else {
            o->stuckCount = 0;
        }

        turn = AngleDelta(o->facing, facing);
        o->facing = facing;

        /* A multi-part vehicle carries a turret, and it counter-rotates
         * against the hull so that it keeps pointing where it was. */
        if (o->rowSet.count > 1) {
            AM2_MapRow *turret = &o->rowSet.rows[1];
            int32_t  swing;

            /* The new aim is stored and passed in one chained assignment:
             * a named local for it is spilled to a byte slot and reloaded,
             * and read back through `r` it leaves `speed` out of ebp, where
             * the image keeps it across the function. */
            swing = AngleDelta(o->turretFacing,
                               r->aim = (uint8_t)(r->aim + turn));
            swing = Clamp(swing, turn - 0x0F, turn + 0x0F);

            animRec = &turret->animPlaying;
            first  = RoundTo8((turret->headingBias + turret->heading) & 0xFF,
                              (*animRec)->directionBits);
            second = RoundTo8((turret->headingBias + swing
                               + turret->heading) & 0xFF,
                              (*animRec)->directionBits);

            if (first != second) {
                AM2_MapRow *g = o->rowSet.rows;

                if (g->turnStamp == g_gameClockMs) {
                    turret->turnStamp = g_gameClockMs;
                } else {
                    uint32_t age = g_gameClockMs - turret->turnStamp;

                    if ((age <= 0x3Cu || o->vehicleKind == 1)
                        && age <= 0x96u)
                        swing = 0;
                    else
                        turret->turnStamp = g_gameClockMs;
                }
            }

            o->turretFacing = (uint8_t)(o->turretFacing + swing);
            gunFacing = RoundTo8(
                (turret->headingBias + swing
                 + turret->heading) & 0xFF,
                (*animRec)->directionBits);
        }

        /* --- firing --- */
        if (r->hit == 1) {
            AM2_Object *weapon = WeaponByUid(
                o->weaponUid);

            if (weapon != 0) {
                AM2_Object     *aimed;
                AM2_FireSpot spot;
                int32_t      height;

                weapon->followUid = g_gameClockMs;
                height = ObjHeight(o);

                if (g_mpSession == 0
                    || AM2_THE_COMM->CommMustBroadcast(
                           (int16_t)o->owner)) {
                    r->gate = 0;
                    o->fireFlag = (uint8_t)orig_step3_rand();
                }

                /* FireWeapon is called in each arm, its heading an argument
                 * expression; VC6 shares the calls' common tail. */
                aimed = LookupByUID(r->uid);
                if (aimed != 0) {
                    /* Firing at an OBJECT: the spot is its position and its
                     * ground height, and the heading gets a +/-2 spread. */
                    spot.at.x = aimed->pos.x;
                    spot.at.y = aimed->pos.y;
                    spot.ground = aimed->row0YAdjust;
                    /* Asked and discarded, in both aimed arms. */
                    (void)AngleBetween(&o->pos,
                                       &aimed->pos);
                    FireWeaponB(weapon, o, height,
                               (uint8_t)(o->turretFacing
                                         + (orig_step3_rand() % 5) - 2),
                               spot, aimed);
                } else if ((uint16_t)r->spot.at.x != 0) {
                    /* Firing at a POINT: same spread, no target object. */
                    (void)AngleBetween(&o->pos,
                                       &r->spot.at);
                    /* Copied whole: the image reads both halves through the
                     * pointer it has just handed AngleBetween. */
                    spot = r->spot;
                    FireWeaponB(weapon, o, height,
                               (uint8_t)(o->turretFacing
                                         + (orig_step3_rand() % 5) - 2),
                               spot, 0);
                } else {
                    /* Firing straight ahead: no spread at all, and the
                     * heading is the gun facing scaled to the animation's
                     * direction count rather than a raw byte. */
                    spot = r->spot;
                    FireWeaponB(weapon, o, height,
                               (uint8_t)(gunFacing
                                         << (8 - (*animRec)->directionBits)),
                               spot, 0);
                }

                {
                    int16_t army = o->owner;

                    if (AM2_THE_COMM->CommMustBroadcast((int16_t)army))
                        SendVehicleFire(o);
                }

                r->hit = 0;
                r->spot = g_aimSpot;
                r->uid = 0;
            }
        }
    }

    /* --- 0x0045D10E: everything below runs even on the early exit --- */
    o->field44 = speed;
    SetKindFrames(o, r->state);

    {
    /* The engine sound for the state, two tables of it -- wheeled (kind 1)
     * and tracked (kinds 2, 3, 4, 6, 7) -- where states 2 and 3 share an arm
     * and state 4 is silent. Each arm is its own call, which VC6 merges. */
#define ENGINE(snd) PlaySoundAt((snd), 1, 0, o->pos.x, \
                                o->pos.y)
    if (ListFirstField548(o)) {
        int32_t kind = o->deathKind;

        if (kind == 2 || kind == 3 || kind == 4 || kind == 6 || kind == 7) {
            switch (o->vehicleKind) {
            case 1:          ENGINE(0x17); break;
            case 0:          ENGINE(0x18); break;
            case 2: case 3:  ENGINE(0x19); break;
            case 5:          ENGINE(0x1A); break;
            }
        } else if (kind == 1) {
            switch (o->vehicleKind) {
            case 1:          ENGINE(0x1B); break;
            case 0:          ENGINE(0x1C); break;
            case 2: case 3:  ENGINE(0x1D); break;
            case 5:          ENGINE(0x1E); break;
            }
        }
    }
#undef ENGINE

    o->rowSet.rows[0].heading = o->facing;
    }
    if (o->rowSet.count > 1)
        o->rowSet.rows[1].heading = o->turretFacing;

    StepObjRows((AM2_Object *)o);
    o->field44 = speed;

    /* Drag the passengers to the vehicle's new position, and their held
     * weapons with them. A passenger whose uid no longer resolves is
     * removed -- and the index still advances, so the entry that slid into
     * the gap is skipped. Reproduced; it is the original's. */
    {
        int32_t i;

        for (i = 0; i < o->riders.count; i++) {
            AM2_Object *p = LookupByUID(
                (o->riders.uids)[i]);

            if (p == 0) {
                ((AM2_Rec *)&o->riders)->ListRemoveAt(i);
                continue;
            }

            p->pos = o->pos;
            if (ObjType2Field548(p)) {
                int32_t n;

                for (n = 0; n < 6; n++) {
                    AM2_Object *w = WeaponByUid(p->inventory[n]);
                    if (w != 0)
                        w->pos = p->pos;
                }
            }
            ObjTileChanged(p, o->heightSet, 0);
        }
    }

    /* A stopped state-5 vehicle draws through the identity remap and a
     * moving one through the bright remap -- the headlights. */
    if (o->vehicleKind == 5) {
        if (o->field44 == 0)
            o->rowSet.rows->palette = g_remapIdent;
        else
            o->rowSet.rows->palette = g_remapBright;
    }

    /* Everything under the point the vehicle is MOVING TO, not where it is:
     * ObjectsAtPoint is handed the step point MoveStepPoint just wrote. */
    {
        AM2_Object *p = ObjectsAtPoint(&step,
                         g_objMapDesc);

        while (p != 0) {
            if (ObjIsItem(p)
                && p->itemFlag < 0) {
                int32_t d = p->heightSet
                            - o->heightSet;
                d = abs(d);
                if (d <= 0x10)
                    tileArg = p->heightSet;
            }

            if (CanPickUp(p, o) && ListFirstField548(o)) {
                p->pickupAfter =
                    g_gameClockMs + 0x7D0u;

                /* The comm object is read afresh at each use. */

                /* Two broadcast questions, not one: the first refuses
                 * outright when the host flag at +0x3D8 is set AND we would
                 * broadcast, and only the second decides whether a WANT is
                 * actually sent. */
                if (g_mpSession != 0
                    && !(g_commObject->isHost != 0
                         && ((AM2_Comm *)g_commObject)->CommMustBroadcast((int16_t)o->owner))) {
                    if (((AM2_Comm *)g_commObject)->CommMustBroadcast((int16_t)o->owner)
                        && g_commObject->isHost == 0)
                        SendVehicleWantItem(o, p, 0, 0, 0);
                } else {
                    /* The refusal's log, two arguments and no format, to the
                     * bare `ret` at ADDR_LOG. Emitted only under VC6, where
                     * that address really is the `ret`; see the header. */
#ifdef _MSC_VER
                    ((void (__cdecl *)(void *, void *))(uintptr_t)ADDR_LOG)(o, p);
#endif
                }
            }

            /* A repair pad under the vehicle heals it, at most every 500 ms. */
            if ((int32_t)p->type == 1
                && p->aai->type == 0x21
                && p->health > 0
                && g_gameClockMs
                       > o->field5A0 + 0x1F4u) {
                HealObject(o, 0x19, p);
                o->field5A0 = g_gameClockMs;
            }

            p = p->queryNext;
        }
    }

    /* A stopped vehicle keeps its own tile height; a moving one takes the
     * height of the tile it is standing on when that differs. */
    if (o->field44 == 0) {
        const uint8_t *attrs = g_tileAttrs;

        if ((uint8_t)o->heightSet != attrs[o->tile])
            tileArg = o->heightSet;
    }

    ObjMoveAlongFacing(o, tileArg, 0, 0);
}

/* Step3Input -- original 0x0045C050, 1424 bytes, one caller in StepType3,
 * which calls it immediately before Step3RouteAndBoard with the same two
 * arguments. The vehicle's DRIVING INTERFACE: seven action keys and the
 * mouse, turned into the heading, destination and turret aim that Step3Drive
 * then acts on. It writes the record and the object and answers nothing.
 *
 * THE CALLER CLEANS 0x10 ONCE FOR TWO CALLS, so reading its `add esp` for
 * this function's arity gives four arguments. Both calls push (rec, obj) and
 * the single cleanup covers the pair -- the shared-cleanup shape this project
 * has met repeatedly, and the reason arity has to come from the callee's own
 * reads rather than from a cleanup instruction.
 *
 * IT IS GATED ON THE TEXT FIELD, which is the first thing it does: if
 * g_charHandler is installed or ADDR_INPUT_SUPPRESS is set, nothing here
 * runs. A vehicle does not drive itself while a chat box has focus.
 *
 * BOTH ARGUMENT SLOTS ARE REUSED AS LOCALS once the object and record are in
 * registers -- arg1's holds the cursor distance written at 0x0045C388, arg2's
 * the aim byte at 0x0045C396, and both are read hundreds of bytes later at a
 * different esp. Type2PlayerInput's header records the same shape for the
 * trooper. tools/espmap.py resolves it, and is what should be reached for
 * first: a linear esp trace desynchronises at the first branch past an
 * `add esp`, and this function has several.
 *
 * NOT THE TROOPER'S FUNCTION ADAPTED. Type2PlayerInput does the same job for
 * a type-2 object and the two are 0.085 similar over normalised disassembly,
 * sharing two nine-instruction runs -- the cursor-to-world-point arithmetic.
 * Different code for one idea, so there is nothing to share.
 *
 * THE ACTION BINDINGS ARE NUMBERED, NOT NAMED, and what each does is recorded
 * here instead: 0 accelerates, 1 reverses AND flips the sense of the two
 * steering keys, 2 and 3 steer, 4 and 5 swing the turret a fixed 0x0F, 9
 * fires, 0x0B cycles the weapon. Naming the constants from that would be
 * naming a binding from one context, which is what ADDR_DRAG_ACTIVE cost. */
/* Reconstructed in item.cpp, which region.cpp does not include -- declared
 * here the same way item.cpp declares its own forward uses. */
void __cdecl SelectInventorySlot(AM2_Object *unit, int32_t slot);

void __cdecl Step3Input(AM2_Object *o, AM2_SteerOut *r)
{
    AM2_Object  *weapon;
    uint8_t   bearing;
    AM2_Point  cursorWorld;      /* view origin + cursor, a packed point */
    AM2_Point gunPos;           /* the turret's point, or the hull's */
    int32_t   dist = 0;         /* arg1's slot, reused */
    int32_t   aimDelta = 0;
    int32_t   steered = 0;
    int32_t   range, allied, other;   /* SightScan's discarded outs */

    if (g_charHandler != 0)
        return;
    if (g_inputSuppress != 0)
        return;

    /* Weapon cycling, and the mouse half is the MIDDLE button RELEASED --
     * state up with the edge set. Reached as element 2 of the two button
     * arrays, which is how NextInventorySlot already spells the same pair. */
    if (ActionKeyPressed(AM2_ACTION_NEXT_WEAPON)
        || ((&g_mouseButton)[AM2_MOUSE_MIDDLE] == 0
            && (&g_mouseChanged)[AM2_MOUSE_MIDDLE] != 0)) {
        AM2_Object *unit = LookupOwnerObj(
            *(const int32_t *)&g_defaultOwner);

        if (unit != 0) {

            /* `[unit + sel*4 + 0x550]` in the image, which is NOT a second
             * array: UNIT_OFF_INVENTORY is 0x54C, so the compiler folded the
             * +1 into the displacement and this is inventory[sel + 1] --
             * exactly what NextInventorySlot writes out longhand. Reading it
             * as a different base is the mistake CLAUDE.md opens with. */
            if (unit->inventorySel < AM2_INVENTORY_SLOTS - 1
                && unit->inventory[unit->inventorySel + 1] != 0) {
                PlaySoundAt(0, 0, 0, 0, 0);
                SelectInventorySlot(unit, unit->inventorySel + 1);
            } else {
                PlaySoundAt(0, 0, 0, 0, 0);
                SelectInventorySlot(unit, 0);
            }
        }
    }

    /* Where the gun IS: a multi-part vehicle keeps the turret's own point at
     * its rows' +0x7C; a single-part one aims from the hull. */
    if (o->rowSet.count > 1)
        gunPos = o->rowSet.rows[1].pos;
    else
        gunPos = o->pos;

    /* Cleared here and set by the steering arms below. Step3Drive reads it as
     * the term that forces a gun-sprite update past the rate limit, so it
     * means "the player is steering this frame". Raw offset on purpose: the
     * OBJ_OFF_FIELD_5A8 in orig.h is a different type's field at the same
     * displacement, and this block keeps such offsets unnamed. */
    o->turnRecommit = 0;

    if (g_objCtxValA
        != (int32_t)o->uid)
        return;

    /* Throttled by the object's own deadline, and all five out-params are
     * discarded -- what is wanted is the scan's side effects, the same
     * reading NextInventorySlot's header records for its call. */
    if (g_gameClockMs
            > (uint32_t)o->fieldFC) {
        SightScan(o, &range, &bearing, &allied, &other, 0);
    }

    weapon = WeaponByUid(
        o->weaponUid);

    /* --- steering. 2 and 3 are exclusive, and 1 reverses the sense of both,
     * so backing up steers the way a reversing driver expects. --- */
    if (ActionKeyDown(AM2_ACTION_RIGHT) && !ActionKeyDown(AM2_ACTION_LEFT)) {
        o->turnRecommit = 1;
        r->stop = 1;
        if (o->vehicleKind != 1)
            r->state = AM2_VEH_STATE_TURNING;
        if (ActionKeyDown(AM2_ACTION_01))
            r->heading = (uint8_t)(o->facing + 8);
        else
            r->heading = (uint8_t)(o->facing - 8);
        steered = 1;
        o->dest = g_zeroPoint;
        o->field10C = 0;
    }

    if (ActionKeyDown(AM2_ACTION_LEFT) && !ActionKeyDown(AM2_ACTION_RIGHT)) {
        o->turnRecommit = 1;
        r->stop = 1;
        if (o->vehicleKind != 1)
            r->state = AM2_VEH_STATE_TURNING;
        if (ActionKeyDown(AM2_ACTION_01))
            r->heading = (uint8_t)(o->facing - 8);
        else
            r->heading = (uint8_t)(o->facing + 8);
        steered = 1;
        o->dest = g_zeroPoint;
        o->field10C = 0;
    }

    /* Both movement keys clear the destination, so a key cancels a mouse
     * order. Only reverse sets +0x0C, which is what Step3Drive reads to pick
     * the reverse speed and to clamp acceleration from the other side. */
    if (ActionKeyDown(AM2_ACTION_00)) {
        r->state = AM2_VEH_STATE_TURNING;
        r->stop = 0;
        steered = 1;
        o->dest = g_zeroPoint;
        o->field10C = 0;
    }

    if (ActionKeyDown(AM2_ACTION_01)) {
        r->state = AM2_VEH_STATE_TURNING;
        r->stop = 0;
        r->field0C = 1;
        steered = 1;
        o->dest = g_zeroPoint;
        o->field10C = 0;
    }

    /* The turret swings a fixed 0x0F either way and the sound plays only in
     * state 1. The two arms are identical but for the sign and are written
     * out rather than parameterised: the image has them that way, and merging
     * them would hide that the state test is duplicated too. */
    if (ActionKeyDown(AM2_ACTION_04)) {
        if (o->vehicleKind == 1)
            PlaySoundAt(0x16, 1, 0, o->pos.x,
                        o->pos.y);
        r->aim = (uint8_t)(r->aim + 0x0F);
        o->field548 = 1;
    }

    if (ActionKeyDown(AM2_ACTION_05)) {
        if (o->vehicleKind == 1)
            PlaySoundAt(0x16, 1, 0, o->pos.x,
                        o->pos.y);
        r->aim = (uint8_t)(r->aim - 0x0F);
        o->field548 = 1;
    }

    /* --- the mouse, on HUD rows 3 and 1 only, and only inside the map. --- */
    if (GetMenuRow() == 3 || GetMenuRow() == 1) {
        int32_t inMap = 1;

        if (g_mouseGrab != -1) {
            if (!PointInRect(g_blitRect,
                             (const AM2_Point *)&g_cursorPoint))
                inMap = 0;
            else if (g_mouseGrab != 0)
                inMap = 0;
        }

        if (inMap) {
            uint8_t aim;

            cursorWorld.x = (int16_t)(((const AM2_Point *)&g_cursorPoint)->x
                                      + g_viewRect.left);
            cursorWorld.y = (int16_t)(((const AM2_Point *)&g_cursorPoint)->y
                                      + *(const int16_t *)&g_viewRect.top);

            dist = ApproxDist(&o->pos,
                              &cursorWorld);
            aim = AngleBetween(&o->pos,
                               &cursorWorld);
            aimDelta = AngleDelta(o->facing, aim);

            g_mouseGrab = -1;
            if (steered == 1)
                OverlayPrepare(1, 1);

            if (GetMenuRow() == 1 && g_mouseButton) {
                /* Row 1 aims the TURRET, and from where the gun is rather
                 * than from the hull. */
                r->aim = AngleBetween(&gunPos,
                                    &cursorWorld);
                o->field548 = 1;
            } else if (!g_mouseButton && g_mouseChanged
                       && GetMenuRow() != 1
                       && orig_get_tick_count()
                              - g_mousePressMs
                          < AM2_CLICK_TAP_MS
                       && ApproxDist(
                              (const AM2_Point *)&g_cursorPoint,
                              (const AM2_Point *)&g_mousePress[0].point) < 4) {
                /* A TAP, not a drag: released within 500 ms and within four
                 * pixels of where it went down. That orders a move. */
                if (dist > 0x30) {
                    r->heading = AngleBetween(&o->pos,
                                      &cursorWorld);
                    o->dest = cursorWorld;
                    NearestAllowedTile(o, TileOfPoint(cursorWorld),
                                       &o->dest);
                    o->deadlineD0 = 0;
                    o->field10C = 1;
                    o->prevRegion = (int16_t)0xFFFF;
                }
            } else if (g_mouseButton) {
                /* Held: steer toward the cursor continuously. */
                r->heading = AngleBetween(&o->pos,
                                  &cursorWorld);
                r->state = AM2_VEH_STATE_TURNING;
                o->dest = g_zeroPoint;
                o->field10C = 0;
                o->turnRecommit = 1;

                /* AND IF IT IS BEHIND YOU AND CLOSE, BACK UP INSTEAD of
                 * turning round: the heading gains 0x80 -- half a turn -- and
                 * the reverse flag goes on. */
                if (abs(aimDelta) > 0x64
                    && dist < 0x80) {
                    r->field0C = 1;
                    r->heading = (uint8_t)(r->heading + 0x80);
                }
            }
            /* The mouse block does NOT set `steered` -- only the four steering
             * KEYS do (0x0045C1B8/C227/C257/C285), and `steered` is read once,
             * at the mouse-fire gate (0x0045C581 `cmp [esp+0x18],1; jne`). A
             * spurious `steered = 1` here let the mouse-fire path fire at HUD
             * row 3 with no steering key held: boarding a jeep with the fire
             * button down auto-fired the mounted gun every cooldown, where the
             * original does not. Removed. */
        }
    }

    if (weapon == 0)
        return;

    /* --- firing. The cooldown is the weapon's own last-fired stamp against
     * its definition's interval, and the same test is written three times
     * for the three ways to ask for a shot. --- */
    {
        /* The time since the last shot is worked out at each test. */

        if (ActionKeyDown(AM2_ACTION_09)) {
            /* Held: only an automatic weapon repeats, which is what the
             * definition's +0x24 gates. */
            if (weapon->typeRec->automatic != 0
                && (g_gameClockMs - weapon->lastUse) > weapon->typeRec->cooldown)
                r->hit = 1;
        } else if (ActionKeyReleased(AM2_ACTION_09)) {
            /* Released: no automatic gate, just the cooldown. */
            if ((g_gameClockMs - weapon->lastUse) > weapon->typeRec->cooldown)
                r->hit = 1;
        }

        if (GetMenuRow() != 1 && steered != 1)
            return;
        if (g_weaponFnSlot3 != 0)
            return;

        if (!g_mouseButton) {
            if (g_mouseChanged)
                goto cool;
            return;
        }
        if (weapon->typeRec->automatic == 0)
            return;
cool:
        if ((g_gameClockMs - weapon->lastUse) > weapon->typeRec->cooldown)
            r->hit = 1;
    }
}
