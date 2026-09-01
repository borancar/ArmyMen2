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
/* 0x00437B60, one caller -- the map loader. Fill the four tile-delta tables
 * from ADDR_MAP_TILES_W: the 5x5 diamond, the eight-ring twice over, one copy
 * of the ring for the decals and the four orthogonals. */
void __cdecl BuildTileDeltas(void);

void __cdecl SetPointRule(void *obj);

/* 0x00437D10, 0x00437D60 and 0x00437DB0, the three rules SetPointRule chooses
 * between. Each answers "is this tile REFUSED"; see the note in region.cpp. */
int32_t __cdecl PointRuleVehicle(int32_t tile);
int32_t __cdecl PointRuleBoat(int32_t tile);
int32_t __cdecl PointRuleDefault(int32_t tile);

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
/* 0x00428E40, six call sites in five functions. Where would this object be
 * after one frame at that heading and speed? It writes the point and moves
 * nothing. `outPt` is an AM2_Point. */
int32_t __cdecl MoveStepPoint(void *obj, int32_t heading, int32_t turn,
                              int32_t speed, int32_t unused, int32_t flip,
                              void *outPt);

/* 0x0040DA70, three callers. MoveStepPoint with the speed taken from the
 * object's current animation rather than passed in. `outPt` is an AM2_Point
 * and is filled with the object's own position on every exit. */
int32_t __cdecl AnimStepPoint(void *obj, int32_t heading, int32_t pose,
                              void *outPt, int32_t fast);

int32_t __cdecl PlanPathTo(void *obj, uint32_t *at, int32_t arg);

void __cdecl AiHitReact(void *obj, void *out, void *ctx);

void __cdecl AiKeepRange(void *obj, void *out, void *ctx);

/* 0x00405D30, two callers. The trooper family's minimal arm, and the exact
 * counterpart of AiStepIgnore: advance the walk, or on arrival forget the
 * destination, react to a hit and face what it sees. */
void __cdecl AiWalkStep(void *obj, void *out, void *ctx);

/* 0x00407190, NINE callers -- the step every AI arm but one shares. Turn the
 * destination at OBJ_OFF_FIELD_C0 into a heading in `out`, routing through the
 * region graph when it is not in the region the object stands in. The third
 * argument is never read; the fourth suppresses the second copy of the
 * bearing. */
void __cdecl AiRouteToward(void *obj, void *out, const void *ctx,
                           int32_t keepFacing);

/* 0x00408210, three callers, all ADDR_ROACH_BEHAVIOUR. The same function for
 * a roach: 518 of 784 bytes identical, arriving eight units closer, reporting
 * through out+0x14 rather than out+8, and resetting the path when its
 * waypoint cursor runs out. */
void __cdecl RoachRouteToward(void *obj, void *out, const void *ctx);

/* 0x00407020, one caller. Sarge's per-frame AI step: build a SIGHTC record on
 * the stack, pick up a weapon if one is in reach, react to being hit, then
 * dispatch on OBJ_OFF_AI_MODE and record the region. */
void __cdecl SargeAiStep(void *obj, void *out);

/* 0x004062B0, one caller. The same for every other trooper -- no pickup, and
 * a tail that maps OBJ_OFF_FIELD_540 onto the output state. The caller at
 * 0x0044B9FE chooses between the two on OBJ_OFF_SARGE. */
void __cdecl TrooperAiStep(void *obj, void *out);

/* 0x00407D70, one caller -- the AI mode dispatcher at 0x00407F80, whose
 * `sub esp, 0x44` is this record's length. Build the sight record every mode
 * arm reads: the leader, the target, what is in view, and the held weapon.
 * Its twin 0x00408060 fills a 0x40-byte record and differs in three ways;
 * see the body. */
void __cdecl AiBuildContext(void *obj, void *out);

/* 0x00408060, one caller -- ADDR_ROACH_ALIVE_STEP_A, whose `sub esp, 0x40` is
 * this record's length. AiBuildContext's twin for a roach: the same structure
 * one dword shorter, with a null weapon and the ranges a weapon of range 64
 * would give. */
void __cdecl RoachBuildContext(void *obj, void *out);

/* 0x004060D0, one caller, inside the trooper step chooser. Walk toward the
 * destination, and on arrival attach to a nearby object; a unit that lingers
 * fifteen seconds is killed. The name is ours. Its object pick reads the
 * count from one army's list and the array from another -- the original's
 * defect, reproduced; see the body. */
void __cdecl AiStepAttach(void *obj, void *out);

/* 0x00404730, three callers, all of them ours. The THIRD sight builder and
 * the trooper's: it fills the 0x58 SIGHTC record every trooper AI step reads.
 * Longer than its two siblings by exactly the fields it adds -- the class at
 * the front, the vehicle pair and a second destination distance in the middle
 * -- and it validates the leader and the target far more strictly than they
 * do. Its counter cannot move; every caller is reconstructed. */
void __cdecl TrooperBuildContext(void *obj, void *ctx, int32_t sarge);

/* 0x00408A60, one caller. The roach's per-frame step: build the sight
 * context, run the behaviour that consumes it, record the region. Its
 * `sub esp, 0x40` is what fixes RoachBuildContext's record length. The second
 * argument is the output record, not a facing -- see the body. */
void __cdecl RoachAliveStepA(void *obj, void *out);

/* 0x0044B7D0, one caller -- ObjFrameStep's type-2 arm. The trooper's
 * per-frame step. The player's own Sarge never reaches the AI from here: a
 * gate on OBJ_OFF_SARGE and the default owner sends it to input handling
 * instead, which is why SargeAiStep and TrooperAiStep read 0 in Boot Camp.
 * Its output record is at +0x57C where StepType3's is at +0x578, so the
 * SIGHTCOUT_OFF_ names are relative to the base the caller passes. */
/* 0x0044AD40, one caller -- StepType2's player arm. What the trooper you are
 * commanding does each frame: board a claimed vehicle if it is close enough,
 * otherwise walk toward OBJ_OFF_FIELD_C0, pick the pose the held weapon
 * wants, and drag every other selected unit along. */
void __cdecl Type2PlayerStep(void *obj, void *out);

/* 0x00449FD0, two callers -- StepType2's trooper AI arm and 0x0044AFB0. The
 * trooper's shot: pick the weapon (the sight record may override the one in
 * hand), end the trooper's state unless the weapon is one of the seven that
 * do not, check the cooldown, aim, and hand the whole thing to 0x0045F460.
 * It names itself in "FIRE  trooper: %x  weapon: %x  ammo: %d".
 *
 * Cold in every configuration here: nothing in a Boot Camp drive shoots. */
void __cdecl TrooperFire(void *obj, void *held, void *sight);

void __cdecl StepType2(void *obj);

/* 0x0045D660, one caller -- ObjFrameStep's type-3 arm. The vehicle's
 * per-frame step: the same reveal-expiry prologue as StepType2, then either a
 * death sequence keyed on the vehicle's kind or the AI, and TWO sequential
 * converging tails that every path reaches. Its output record is at +0x578
 * where StepType2's is at +0x57C. */
void __cdecl StepType3(void *obj);

/* 0x00408640, one caller -- RoachAliveStepA. The roach's decision half. It
 * promotes one {object, range, bearing} triple into the SIGHT record's
 * observer slot -- the found triple in three arms, the leader triple in the
 * fourth -- which is what ConsiderSighting then reads. */
void __cdecl RoachBehaviour(void *obj, void *out, void *ctx);

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

/* 0x0042B9A0, one caller -- the state-2 entry. Build the whole region graph
 * for the loaded map: clear the region off every blocked tile, grow and
 * activate the region array, link neighbouring tiles in different regions,
 * then allocate the two stride-squared routing matrices AiRouteToward reads
 * and stamp the generation to 1. */
void __cdecl BuildRegionGraph(void);

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

/* 0x004389D0, the same job from the object's per-pixel OBJ_OFF_HIT_MASK rather
 * than from its box. This is the arm that runs; ObjBoxAction is the fallback
 * for an object with no mask, which nothing on a drivable map is. */
int32_t __cdecl ObjHitMaskAction(void *obj, void *out);

/* 0x00439320, six callers. Take an object's footprint back off the map:
 * ObjClearFootprint or ObjClearRoachFootprint for a vehicle or a roach, and
 * for an ITEM, subtract its height from every masked cell's weight and move
 * the tile cover where a weight crosses 15. Clears OBJ_FLAG_FOOTPRINT_ON, so
 * it is idempotent. COLD: no drive this project has destroys an item. */
void __cdecl ItemTeardown(void *obj);

/* 0x00439000, four callers. ItemTeardown's mirror: lay the object's footprint
 * down instead of taking it up, and -- when the third argument or the record
 * supplies a crush damage -- hurt everything already standing on it. The
 * second argument is never read. */
void __cdecl ObjAfterMove(void *obj, int32_t unused, int32_t damage);

/* 0x00438F10, one caller. The same function for a record-list HEADER: the
 * first list record's sprite, the header's own box, and the point as an
 * argument rather than a field. */
int32_t __cdecl ListBoxAction(uint32_t at, void *list, void *out);

/* 0x0043A6D0, six callers. Could the thing described by key-table slot `slot`
 * stand at world point `at`? Build its tile mask there and answer 0 the moment
 * a cell of it is covered, is the wrong ADDR_TILE_KIND, or has an object on
 * it. 1 when none does, and 1 for a slot past the end. */
/* 0x004045E0, three callers. Fill the six sight-context fields that describe
 * the weapon a unit is holding: SIGHTC_OFF_WEAPON, _KIND, _DAMAGE,
 * _WANT_RANGE, _MAX_RANGE and _READY. */
void __cdecl UnitWeaponInfo(void *unit, void *out);

/* 0x00438300, four callers. Solve the route from one region to another and
 * record it in the two all-pairs byte matrices. The found exit fills BOTH
 * directions of the path; the no-path exit answers one way round only. */
void __cdecl RegionSolvePair(int32_t from, int32_t to);

int32_t __cdecl CanPlaceAt(uint32_t at, int32_t slot, int32_t kind);

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
