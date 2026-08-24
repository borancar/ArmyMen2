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

/* 0x0042B290, 45 callers. A packed point to a tile index, sixteen pixels to
 * the tile, or 0 for anything off the map -- which is also tile 0's answer,
 * so a caller cannot tell the two apart.
 *
 * Both coordinates are tested SIGNED as int16 and then again against the map's
 * pixel extents, and the failure path clears only AX, so the result is
 * sixteen bits; every caller stores it as one. */
int32_t __cdecl TileOfPoint(uint32_t packed);

/* 0x0042B250, six callers -- the inverse, and it CENTRES: the point it returns
 * is the middle of the tile, eight pixels in on each axis.
 *
 * The column comes out with an AND against `width * 16 - 16`, which is a
 * modulo only because the width is a power of two, and the row with a shift by
 * ADDR_MAP_ROW_SHIFT. Both halves read the map's width, from two globals that
 * have to agree; that is one of the three readings that settle what
 * 0x00514DDC is. */
uint32_t __cdecl PointOfTile(int32_t tile);

void map_install(void);

#ifdef __cplusplus
}
#endif

/* 0x0042DBB0. XOR every whole dword of a file together; a trailing partial
 * dword is dropped. Announces itself in the log either way, and reports 0 for
 * a file it cannot open. Seven callers. */
uint32_t __cdecl Checksum(const char *path);

/* The three totals a multiplayer session compares before it will start. Each
 * chdirs into the directory it reads, so calling one leaves the process
 * somewhere else than it found it. */
uint32_t __cdecl RulesChecksum(void);
uint32_t __cdecl MpScriptChecksum(void);
uint32_t __cdecl MapChecksum(void);

#endif /* AM2_MAP_H */
