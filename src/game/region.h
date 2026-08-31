/* region.cpp -- the region graph.
 *
 * "Region" is the game's own word, from the log lines "Added Region link from
 * %d to %d" and "Activating Region %d". What those lines are FOR is settled by
 * the switch that gates them: -tracePF, already named ADDR_OPT_TRACE_PF. So
 * the region graph is this game's pathfinding structure, and the module is
 * named for the nodes rather than for the algorithm.
 *
 * A name of ours: the band is item.cpp..map.cpp, between two modules the image
 * does name, and it names no source file of its own.
 */
#ifndef AM2_REGION_H
#define AM2_REGION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One edge out of a region. Six bytes, and the two cells are kept as well as
 * the destination -- so a link records not just WHICH region it reaches but
 * where the crossing is. */
typedef struct {
    int16_t to;      /* +0x00, destination region id */
    int16_t from;    /* +0x02, the cell on this side */
    int16_t into;    /* +0x04, the cell on the far side */
} AM2_RegionLink;

/* 0x0042B860. Record that two cells connect their regions. Does nothing if
 * either cell has no region, or if this exact edge is already present. */
void __cdecl AddRegionLink(int32_t cell, int32_t neighbour);

/* 0x0042B7F0. Of all the links a region has to another, the index of the
 * middle one; -1 when there are none. */
int32_t __cdecl MiddleRegionLink(int32_t region, int32_t to);

/* 0x00437E00. Choose which of three rules a point gets settled under, from
 * the object asking. See region.cpp for why it lives here. */
void __cdecl SetPointRule(void *obj);

/* 0x00407BF0, one caller. The `ignore` arm of the AI mode dispatcher at
 * 0x00407F80 -- mode 2. Walk to the remembered destination; on arrival, turn
 * only for an unreacted hit or, after a delay, for what the context found. */
void __cdecl AiStepIgnore(void *obj, void *out, const void *ctx);

/* 0x00407640, one caller. The `defend` arm -- mode 7. AiStepIgnore plus two
 * things: it promotes whatever the search found into the slot ConsiderSighting
 * reads, and it calls ConsiderSighting on every path. */
void __cdecl AiStepDefend(void *obj, void *out, void *ctx);

/* 0x00407560, one caller. The arm modes 1, 4 and 5 share -- 5 is `evade`.
 * AiStepDefend with one branch target moved: it turns toward what it sees
 * while still walking, where defend turns only on arrival. Name ours. */
void __cdecl AiStepTrack(void *obj, void *out, void *ctx);

/* 0x00407C80, one caller. The `follow` arm -- mode 3. AiStepDefend with the
 * gate replaced: walk when out of formation OR when the leader has moved. */
void __cdecl AiStepFollow(void *obj, void *out, void *ctx);

/* 0x00407BD0, one caller. The `attack` arm -- mode 6. Forwards its three
 * arguments to ADDR_AI_ATTACK_BODY and does nothing else. */
void __cdecl AiStepAttack(void *obj, void *out, void *ctx);

/* 0x00407F80, two callers, both in the type-3 stepper. One frame of AI for one
 * object: build the context, run the arm OBJ_OFF_AI_MODE selects through an
 * eight-entry table, then record the object's region. */
void __cdecl AiStep(void *obj, void *out);

/* 0x00405100, six call sites in five functions. Keep the unit at the range it
 * wants from what it sees: a spot re-picked every five seconds, a pose while
 * it waits, and a turn. Its `out` is NOT the vehicle family's -- heading at
 * +4, pose at +8. */
/* 0x00405050, ten call sites. What a unit does about having been hit: choose
 * a pose from its rank and class, turn toward the hit if it is not already
 * watching something, and consume OBJ_OFF_HIT_DIR. */
/* 0x00439D60, three callers. Find a route from where the object is to a point
 * and write it onto the object as a waypoint list. BeginMoveTo's general case:
 * the same five fields, with as many waypoints as the route needs. */
int32_t __cdecl PlanPathTo(void *obj, uint32_t *at, int32_t arg);

void __cdecl AiHitReact(void *obj, void *out, void *ctx);

void __cdecl AiKeepRange(void *obj, void *out, void *ctx);

/* 0x00405D30, two callers. The trooper family's minimal arm, and the exact
 * counterpart of AiStepIgnore: advance the walk, or on arrival forget the
 * destination, react to a hit and face what it sees. */
void __cdecl AiWalkStep(void *obj, void *out, void *ctx);

/* 0x00439F40, five callers. NearestAllowedTile's twin: the same square spiral
 * under the DEFAULT point rule, and writing nothing through `pt` when the
 * starting tile is already accepted. */
uint16_t __cdecl SettlePointInRegion(int32_t tile, uint32_t *pt);

/* 0x00406460 and 0x004066B0. Hops between two regions, and whether two objects
 * are in the same region or neighbouring ones. See region.cpp. */
int32_t __cdecl RegionHops(int32_t from, int32_t to, int32_t solve);
int32_t __cdecl RegionsNear(const void *a, const void *b, int32_t solve);

/* 0x0042BC70 and 0x0042BCB0. The script's `activateregion` and
 * `inactivateregion`: set or clear REGION_OFF_ACTIVE, and bump the routing
 * cache's generation stamp when -- and only when -- the flag changed. */
void __cdecl ActivateRegion(int32_t region);
void __cdecl InactivateRegion(int32_t region);

/* 0x00439E90. Can this object reach that point in a straight line? Traces the
 * tiles between and puts each to the object's installed point rule; on success
 * records the move on the object and returns 1. */
/* 0x0043A0A0. The nearest tile the object's point rule accepts, spiralling
 * outward from the one given. Writes the point back only when its low word is
 * zero; returns the tile, or 0 when a whole ring produced no candidate. */
uint16_t __cdecl NearestAllowedTile(void *obj, int32_t tile, uint32_t *pt);

int32_t __cdecl BeginMoveTo(void *obj, uint32_t *to);

int region_install(void);

#ifdef __cplusplus
}
#endif

/* 0x004384A0 and 0x00438520, five callers between them. Add or subtract one
 * from a tile's cover count and its twenty neighbours'. */
void __cdecl TileCoverAdd(uint16_t tile);
void __cdecl TileCoverSub(uint16_t tile);

/* 0x0043A4F0, one caller. Set a tile flag for each of two neighbourhood counts
 * that comes up short. Always answers 0. */
int32_t __cdecl MarkOpenTile(uint16_t tile);

/* 0x00438DF0, two callers. Turn a rectangle in pixels into a scratch tile
 * mask: clamp each edge into the map, pad by two tiles, and fill the padded
 * rectangle with 2 and the box itself with 3. `out` is a TILEMASK record.
 * Always answers 1. */
int32_t __cdecl BoxAction(int32_t left, int32_t top, int32_t right,
                          int32_t bottom, void *out);

/* 0x00438F80, two callers. Offset the object's box by its position and hand it
 * to BoxAction, when the first row's sprite has a software image. `out` is the
 * same record, and was declared `int32_t arg` here until BoxAction was read. */
int32_t __cdecl ObjBoxAction(void *obj, void *out);

/* 0x00438F10, one caller. The same function for a record-list HEADER: the
 * first list record's sprite, the header's own box, and the point as an
 * argument rather than a field. */
int32_t __cdecl ListBoxAction(uint32_t at, void *list, void *out);

/* 0x0042BE10, one caller. Clear the cover grid and rebuild it from the cell
 * weights. */
void __cdecl RebuildTileCover(void);

/* 0x0043A450, two callers. The region a tile is in, borrowing one from a
 * neighbour and caching it when the tile has none. */
uint16_t __cdecl TileRegionOrBorrow(uint16_t tile);

/* 0x004074A0, four callers. One observer against one object: range, bearing,
 * and a two-second reveal when the observer is ours. */
void __cdecl ConsiderSighting(void *seen, void *out, const void *sight);

/* 0x00408580, one caller. ConsiderSighting's sibling over a different record
 * layout, with a wider cone and a tail that commits the hit. */
void __cdecl ConsiderSightingB(void *seen, void *out, const void *sight);

/* 0x00404F40, four callers. The third sighting variant: a magic maximum range
 * that widens the cone and suppresses the reveal at once. */
void __cdecl ConsiderSightingC(void *seen, void *out, const void *sight);

/* 0x0042BCF0, one caller. Seal the map's four edges, then flag every tile as
 * blocking and/or near the border. */
void __cdecl SealMapEdges(void);

/* 0x0043A330, one caller. Decrement the reveal count over a five-by-five tile
 * block, in the grid of every allied army. */
void __cdecl UnrevealArea(int32_t army, uint32_t at);

#endif /* AM2_REGION_H */
