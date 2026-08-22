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

#endif /* AM2_AIR_H */
