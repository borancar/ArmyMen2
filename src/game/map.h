/* map.cpp -- the map module's savegame section.
 *
 * A module of its own because the image names it: the loader passes
 * "C:\\ArmyMen2\\source\\map.cpp" to CheckSaveTag, which is the same evidence
 * that put script.cpp and objscript.cpp on the original's own dividing line
 * rather than one of ours. Only the save pair is reconstructed so far; the rest
 * of the unit runs from 0x0042B120 up.
 */
#ifndef AM2_MAP_H
#define AM2_MAP_H

#include <stdint.h>
#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

/* 0x0042DB40 and 0x0042DB70. The simplest section in the savegame: a tag and
 * one fixed 236-byte block at 0x00514D90, written and read straight.
 *
 * The saver checks nothing and always answers 1. The loader answers 0 if the
 * tag is wrong -- and, unlike LoadItems, it destroys nothing first, so a
 * failed load here leaves the block as it was.
 *
 * The 236 bytes begin with the map's own directory -- "data\\kitchen" in a
 * campaign save -- and are otherwise mostly zero, 34 non-zero bytes of the
 * 236. So the section identifies which map the save belongs to. Read out of a
 * savegame rather than guessed; what the remaining fields are is still open.
 *
 * There is not a single heap pointer in the block, which matters for checking
 * it: the item section's bytes shift between builds because am2hook.dll moves
 * the heap under the pointers stored there, and this section cannot. A plain
 * byte comparison is valid here. */
int32_t __cdecl SaveMapSection(am2_FILE *fp);
int32_t __cdecl LoadMapSection(am2_FILE *fp);

void map_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MAP_H */
