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

/* 0x0043ED40. How many records ADDR_LEVEL_TABLE holds. */
int32_t __cdecl LevelCount(void);

/* 0x0042E390. The tiles a line crosses, stepped in tile space from the first
 * point's tile. Writes `tiles` and `count`; neither is bounds-checked. */
void __cdecl TraceTileLine(uint32_t from, uint32_t to,
                           uint16_t *tiles, int32_t *count);

/* 0x0042B250, six callers -- the inverse, and it CENTRES: the point it returns
 * is the middle of the tile, eight pixels in on each axis.
 *
 * The column comes out with an AND against `width * 16 - 16`, which is a
 * modulo only because the width is a power of two, and the row with a shift by
 * ADDR_MAP_ROW_SHIFT. Both halves read the map's width, from two globals that
 * have to agree; that is one of the three readings that settle what
 * 0x00514DDC is. */
uint32_t __cdecl PointOfTile(int32_t tile);

/* 0x0042B210. The same answer as PointOfTile, written back as two int32
 * rather than packed into one dword. */
void __cdecl TileToXY(int32_t tile, int32_t *x, int32_t *y);

/* 0x0043E900. Find a record by name in the name registry; the first search is
 * also the load. */
void *__cdecl ScriptListFind(char *name);

/* 0x0042D3D0, two callers -- the level teardown and the map loader, which
 * clears before it fills. Free every per-map allocation: the region array with
 * each region's own link list first, then twelve pointers in a row, each
 * guarded and each cleared after. */
int32_t __cdecl LoadMap(const char *base, const char *folder);

void __cdecl FreeMapLayers(void);

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

/* 0x0043ED50. Copy the seven strings and one flag out of a level record into
 * the globals the loader reads. Unbounded strcpy, as the original writes it;
 * a null record is the only refusal. */
void __cdecl SelectLevel(const void *record);

/* The level table and the name registry beside it: empty both, look one up by
 * id, and the three readers that fill them from campaign.txt, mpmaps.txt and
 * bootcamp.txt. */
void  __cdecl FreeLevelTables(void);
void *__cdecl FindLevelRecord(int32_t id);
void  __cdecl ReadCampaignLevels(void);
void  __cdecl ReadMpMapList(void);
void  __cdecl ReadBootcampLevels(void);

/* 0x0043DD30, one caller. Free every string the scenario table owns, then the
 * table, then clear both globals. */
void __cdecl FreeScenarios(void);

/* 0x0043E160, one caller. Append one level record, allocating the table on
 * first use and growing it when full. */
void __cdecl AddLevelRecord(const void *record);

/* 0x0043E230, six callers. The LEVEL table's by-name search: lower-cases its
 * argument in place, then walks the 0x30C records comparing LEVEL_OFF_MAP_NAME.
 * Linear, because the table is sorted by id and not by name. */
void *__cdecl FindLevelByName(char *name);

/* 0x0043E2C0. The parser table's handler for the `MAP` command: one line of
 * campaign.txt, bootcamp.txt or mpmaps.txt into a level record. Returns 0, or
 * the number of the column that was missing. */
int32_t __cdecl DefMapLine(int32_t cmd, char *line);

/* 0x0043E9A0, one caller. AddLevelRecord's twin over the 0xCC-byte name
 * records. */
void __cdecl AddNameRecord(const void *record);

/* The `rules` / `rulemap` trio, originals 0x0043EA30, 0x0043EAC0 and
 * 0x0043EBD0 -- one 592-byte entry. The two line handlers are named from the
 * keyword table at 0x00477448, which is the program's own vocabulary; the
 * appender extends the list a name record carries at +0xC0. Both handlers
 * take the line as their SECOND argument and ignore the first, like every
 * other handler in that table. */
void __cdecl NameRecAddMap(void *rec, const void *entry);
int32_t __cdecl DefRulesLine(int32_t cmd, char *line);
int32_t __cdecl DefRuleMapLine(int32_t cmd, char *line);

/* 0x0042C350, one caller. The map's stored checksum, read out of the first
 * CSUM chunk of its `.amm` file. The second argument is pushed by the call
 * site and ignored by the body -- see the definition. */
uint32_t __cdecl AmmChecksum(const char *map, const char *folder);

#endif /* AM2_MAP_H */
