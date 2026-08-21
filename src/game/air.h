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
