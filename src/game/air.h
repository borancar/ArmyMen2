/* air.cpp -- the comm transport's savegame section.
 *
 * A module of its own because the image names it: the loader hands
 * "C:\\ArmyMen2\\source\\air.cpp" to CheckSaveTag, which is proof for THIS pair
 * and for nothing else.
 *
 * Worth stating because it is not tidy. msgslot.cpp holds nine functions from
 * the same `<=air.cpp` band -- the message-slot writers, the latency ring, the
 * keyed removal, the flow masks and the two Send*Msg broadcasts -- under a name
 * of ours. That band label means "at or below air.cpp", so those functions may
 * belong to this unit or to an earlier one; nothing so far settles it. If one
 * of them ever names its own file the way this pair does, the two modules
 * should be merged under whichever name the image gives.
 */
#ifndef AM2_AIR_H
#define AM2_AIR_H

#include <stdint.h>
#include "rect.h"   /* AM2_Point */
#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x00408F80, 0x00408FD0 and 0x00408FF0 -- the air-support queue's
 * head, and all three names are ours.
 *
 * The log line that looks like a name is not one. 0x00408FF0 prints
 * "EndMission  AirSupport.count decreasing to: %d" and DoAirSupport prints
 * "EndMission  AirSupport.count increasing to: %d" from a different function
 * entirely, so "EndMission" is a prefix these two share -- a section label in
 * the original's source, most likely. DoAirSupport names ITSELF on the line
 * above its own. A self-naming sweep would pair the prefix with whichever
 * function it found first.
 *
 * AirSupportBegin looks at the head entry's `extra` field: zero means play
 * sound 0x2E and raise the active flag with both others clear, non-zero means
 * raise both others and leave the active flag alone. AirSupportClear is the
 * three-line reset. AirSupportPop shifts all four arrays down one, decrements
 * the count, and tail-calls Begin if anything is left or Clear if not -- the
 * original really does tail-JUMP to both, which is why they are separate
 * functions rather than arms. */
void __cdecl AirSupportBegin(void);
void __cdecl AirSupportClear(void);
void __cdecl AirSupportPop(void);

/* Original: 0x00409680, and the name is ours. Answer the uid of an enemy
 * within five hundred units of `where`, or zero.
 *
 * "Enemy" is two tests: the object's OWNER differs from the asking uid's army,
 * and its health is above zero. Both have to hold, and the owner is the byte
 * objtable.h already calls `owner` -- read here with a signed load, as it is
 * everywhere else.
 *
 * UidArmy is called once per CANDIDATE rather than once before the loop, so a
 * query returning forty objects calls it forty times. Reproduced; hoisting it
 * would be a different program. */
uint32_t __cdecl FindEnemyNear(uint32_t where, uint32_t from);

/* Original: 0x00409710, "DoAirSupport paratroopers where: %d, from %d, army
 * %d, count: %d" -- this one really does name itself, on its own line, which
 * is what makes the "EndMission" prefix on the line below it a prefix.
 *
 * Queue one air-support request. Kind 2 is taken as given; anything else asks
 * FindEnemyNear first and becomes kind 3 if there is one -- so the caller's
 * kind is a floor, not a decision.
 *
 * It refuses at thirty and answers 0. Otherwise it fills the four arrays,
 * starts the queue if this is the first entry, and answers 1.
 *
 * Two details of the order are the original's. AirSupportBegin is called with
 * the entry WRITTEN but the count still zero, so Begin reads an entry the
 * count says is not there -- harmless, because Begin only looks at slot 0. And
 * the count is re-read from memory before each of the four stores rather than
 * held in a register.
 *
 * The `where` field is written here as one dword and copied by AirSupportPop
 * as two words. Same field, two access widths, and both are reproduced. */
int32_t __cdecl DoAirSupport(int32_t kind, uint32_t where, uint32_t from);

/* Original: 0x004097D0, and the name is ours. Walk every registered object and
 * reveal the ones near `where`, each staying visible until the game clock
 * plus `delayMs` -- an air strike lighting up what it flies over.
 *
 * Three tests, in the original's order and all three needed: the object is a
 * type 2, 3 or 8; it is not ALREADY revealed, which is the 0x0800 flag
 * RevealObj sets; and ApproxDist from `where` is no more than `radius`.
 *
 * The distance is ApproxDist, not a true one -- so the "radius" is that
 * function's diamond-ish approximation and not a circle. Reproduced, since it
 * is what decides who is caught.
 *
 * The point arrives BY VALUE and its address is taken to pass to ApproxDist,
 * which is why the parameter cannot become a pointer. */
void __cdecl RevealNearby(AM2_Point where, int32_t radius,
                              int32_t delayMs);

/* Originals: 0x004098B0 and 0x00409930, the sprite list's teardown and its
 * growth. The list itself is 0x004F96C0..0x004F96C8 -- capacity, count, array.
 *
 * FreeSpriteList is what winmain.cpp's FreeSpriteListAlias has been jumping to
 * since it was written; the alias at 0x00409920 was already reconstructed, and
 * this closes the seam under it rather than adding anything beside it.
 *
 * It releases every sprite, frees the array and clears all three globals. Its
 * EMPTY path clears the count and the capacity anyway, so a null array with a
 * stale count is tidied rather than trusted -- and the count is re-read from
 * memory on every iteration of the release loop rather than held.
 *
 * GrowSpriteList adds a HUNDRED to the capacity and reallocs to that without
 * looking at the count, so it is "make room", called by whoever is about to
 * need it, not "grow if full". Nothing checks the realloc. */
void __cdecl FreeSpriteList(void);

/* Original: 0x00404400, two callers. Write the formation position for `slot`
 * into `out`, relative to `leader`. See ADDR_FORMATION_SLOTS for the table. */
void __cdecl FormationPoint(void *follower, void *leader, AM2_Point *out,
                            int32_t slot);

/* Original: 0x00404580, three callers, and the name is ours. Place `follower`
 * in formation on `leader`, redirecting to the vehicle when the leader is a
 * type 2 that is riding one, then calling FormationPoint. */
void __cdecl ResolveFormationPoint(void *follower, void *leader,
                                   AM2_Point *out);

/* Original: 0x004035F0. Zero the two per-frame counters, both of which are
 * vestigial -- see the source for the whole-image reference count. */
void __cdecl ClearFrameCounts(void);

/* Original: 0x00403AF0, three callers, and the name is ours. The object's
 * position with its sprite's second anchor pair subtracted. Picks row 1 when
 * there is more than one row and row 0 when there is exactly one. */
uint32_t __cdecl ObjAnchorPoint(const void *obj);

/* Original: 0x004296E0, eight callers. Reveal one object through the fog. Two
 * flags and they are not symmetric: OBJ_FLAG_REVEALED goes up
 * unconditionally and is what callers test, while OBJ_FLAG_CONCEALED gates the
 * row work and is lowered by it. ADDR_OBJ_CONCEAL is the exact inverse. */
void __cdecl RevealObj(void *obj);

/* 0x004295C0. Sets the fog flag from an INVERTED argument -- non-zero turns
 * fog ON -- and, when turning it OFF, reveals every type 2/3/8 object. Not a
 * call to RevealObj: it omits that function's OBJ_FLAG_REVEALED write,
 * deliberately. */
void __cdecl SetFogOfWar(int32_t fogOn);

/* 0x00429650. RevealObj's inverse -- sets OBJ_FLAG_CONCEALED and unlinks the
 * rows. Declines unless ADDR_FOG_OF_WAR is zero (fog ON) or `force` is set. */
void __cdecl ObjConceal(void *obj, int32_t force);

/* 0x0041A1B0, two callers -- both cheat arms. Invert the fog flag and bring
 * every enemy object into line with it. See air.cpp for the double write. */
void __cdecl ToggleFogOfWar(void);

/* 0x004064E0, four callers. Is `who` standing at the named army's flag base?
 * Answers 0 for YES -- see air.cpp. */
int32_t __cdecl AtFlagBase(const void *who, const void *owner, int32_t army,
                           const char *name);

/* 0x00406550, two callers. A thing's own code to a result code -- mostly a
 * constant, four flag-base tests and one health comparison. See air.cpp. */
int32_t __cdecl ThingCode(const void *who, const void *owner);

/* 0x00448E60. How much `obj` obstructs, as seen from `from` and measured
 * against `at`. 0, an item's own byte, or AM2_BLOCK_FULL. The third argument
 * is not read; it is in the signature because the callers pass it. */
int32_t __cdecl ObjBlockWeight(void *from, void *obj, int32_t unused,
                               uint32_t at);
void __cdecl GrowSpriteList(void);

/* Original: 0x00409960, and the name is ours. Remap a run-length-encoded
 * sprite image in place: every pixel index at or above `from` is replaced by
 * `table[index]`, and everything below it is left alone -- the same
 * reserved-block convention NearestPalIndex's `from` argument follows.
 *
 * The image is {uint16 width, uint16 height} then one uint16 per row, so the
 * pixel data starts at 4 + height*2. A row is pairs of {skip, run} bytes and
 * ends when skip and run together have covered the width; only the run bytes
 * are pixels.
 *
 * The SECOND parameter is never read. Four are pushed and three are used;
 * whatever it was for, this function does not want it.
 *
 * A null table returns at once. A height of zero returns too -- the test is on
 * the height, not the width, so a zero-width image with rows would walk them.
 * Reproduced.
 *
 * It sits in this module by position, like the rest of the `<=air.cpp` band;
 * a sprite remapper may well belong to an earlier translation unit. */
void __cdecl RemapSpriteRuns(void *img, int32_t unused, const uint8_t *table,
                             int32_t from);

/* 0x00409840 and 0x00409870. A tag and one fixed 584-byte block at
 * 0x004F945C, written and read straight -- the simplest section in the file
 * and the same shape map.cpp's is.
 *
 * The saver checks nothing and always answers 1. The loader answers 0 on a bad
 * tag and reads nothing, so a foreign save leaves the block as it was. That
 * puts it with LoadEventBlock and LoadScriptSection rather than with LoadItems
 * and LoadPadSection, which destroy before they check. */
int32_t __cdecl SaveAirSection(am2_FILE *fp);
int32_t __cdecl LoadAirSection(am2_FILE *fp);

void air_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00404ED0, two callers. Pick a point `dist` away from an object on a
 * heading within +/-32 of the way it is facing. The first argument is unused. */
void __cdecl RandomPointAhead(void *, const void *obj, int32_t dist,
                              AM2_Point *out);

#endif /* AM2_AIR_H */
