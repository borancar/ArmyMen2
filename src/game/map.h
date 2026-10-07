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
#include "dist.h"             /* AM2_Point */

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
#if !(defined(_MSC_VER) && defined(AM2_MAP_DEFINES_TILEOFPOINT))
int32_t __cdecl TileOfPoint(AM2_Point pt);
#endif

/* 0x0043ED40. How many records ADDR_LEVEL_TABLE holds. */
int32_t __cdecl LevelCount(void);

/* 0x0042E390. The tiles a line crosses, stepped in tile space from the first
 * point's tile. Writes `tiles` and `count`; neither is bounds-checked. */
void __cdecl TraceTileLine(AM2_Point from, AM2_Point to,
                           uint16_t *tiles, int32_t *count);

/* 0x0042B250, six callers -- the inverse, and it CENTRES: the point it returns
 * is the middle of the tile, eight pixels in on each axis.
 *
 * The column comes out with an AND against `width * 16 - 16`, which is a
 * modulo only because the width is a power of two, and the row with a shift by
 * ADDR_MAP_ROW_SHIFT. Both halves read the map's width, from two globals that
 * have to agree; that is one of the three readings that settle what
 * 0x00514DDC is. */
AM2_Point __cdecl PointOfTile(int32_t tile);

/* 0x0042B210. The same answer as PointOfTile, written back as two int32
 * rather than packed into one dword. */
void __cdecl TileToXY(uint16_t tile, int32_t *x, int32_t *y);

/* One placed object of a map's OLAY chunk (AM2_MAPREC_BYTES in memory, 0x10
 * of it from the file): a type, a kind and a position, then the attributes
 * the ATTR chunk fills in by index. The position is two dwords on disk and
 * the builder reads each as a short. */
typedef struct AM2_MapRec {
    int32_t type;                   /* +0x00 MAPREC_OFF_TYPE */
    int32_t kind;                   /* +0x04 MAPREC_OFF_KIND */
    union { int32_t x; int16_t x16; };  /* +0x08 MAPREC_OFF_X */
    union { int32_t y; int16_t y16; };  /* +0x0C MAPREC_OFF_Y */
    uint8_t trig;                   /* +0x10 MAPREC_OFF_TRIG */
    uint8_t field11;                /* +0x11 written 0 */
    uint8_t elev;                   /* +0x12 MAPREC_OFF_ELEV */
    uint8_t owner;                  /* +0x13 MAPREC_OFF_OWNER, 0 = none */
    uint8_t move;                   /* +0x14 MAPREC_OFF_MOVE */
    int8_t  numb;                   /* +0x15 MAPREC_OFF_NUMB */
    uint8_t _pad16[2];
    char   *script;                 /* +0x18 MAPREC_OFF_SCRIPT, owned */
} AM2_MapRec;

/* One map of the level table (ADDR_LEVEL_TABLE, AM2_LEVEL_RECORD_SIZE):
 * a line of campaign.txt, bootcamp.txt or mpmaps.txt, whose header comment
 * names the columns. Every string is 0x40 bytes (AM2_LEVEL_STR_BYTES). */
typedef struct AM2_Level {
    int32_t id;                     /* +0x000 LEVEL_OFF_ID, the bsearch key */
    char    mapName[0x40];          /* +0x004 LEVEL_OFF_MAP_NAME, map_name */
    char    name[0x40];             /* +0x044 LEVEL_OFF_NAME, map_text: the
                                     *        display name */
    char    folder[0x40];           /* +0x084 LEVEL_OFF_FOLDER */
    char    winMovie[0x40];         /* +0x0C4 LEVEL_OFF_WIN_MOVIE, victorycin */
    char    loseMovies[3][0x40];    /* +0x104 LEVEL_OFF_LOSE_MOVIES */
    char    loadMusic[0x40];        /* +0x1C4 LEVEL_OFF_LOAD_MUSIC */
    char    loadScreen[0x40];       /* +0x204 LEVEL_OFF_LOAD_SCREEN */
    int32_t cycle;                  /* +0x244 LEVEL_OFF_CYCLE, `cycle?` */
    char    briefing[0x40];         /* +0x248 LEVEL_OFF_BRIEFING */
    char    briefSfx[0x40];         /* +0x288 LEVEL_OFF_BRIEF_SFX */
    char    stratMap[0x40];         /* +0x2C8 LEVEL_OFF_STRAT_MAP */
    int32_t movieIndex;             /* +0x308 LEVEL_OFF_MOVIE_INDEX */
} AM2_Level;

/* One `rules` record of the name registry (AM2_NAME_RECORD_SIZE): three names
 * and the `rulemap` list of map names appended to it. */
typedef struct AM2_NameRec {
    char    name[0x40];             /* +0x00 NAMEREC_OFF_NAME, the searched one */
    char    name2[0x40];            /* +0x40 NAMEREC_OFF_NAME2 */
    char    name3[0x40];            /* +0x80 NAMEREC_OFF_NAME3, title-cased */
    int32_t cap;                    /* +0xC0 NAMEREC_OFF_CAP */
    int32_t count;                  /* +0xC4 NAMEREC_OFF_COUNT */
    char  (*maps)[0x40];            /* +0xC8 NAMEREC_OFF_MAPS */
} AM2_NameRec;

/* 0x0043E900. Find a record by name in the name registry; the first search is
 * also the load. */
AM2_NameRec *__cdecl ScriptListFind(char *name);

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
void __cdecl SelectLevel(const AM2_Level *record);

/* The level table and the name registry beside it: empty both, look one up by
 * id, and the three readers that fill them from campaign.txt, mpmaps.txt and
 * bootcamp.txt. */
void  __cdecl FreeLevelTables(void);
AM2_Level *__cdecl FindLevelRecord(int32_t id);
void  __cdecl ReadCampaignLevels(void);
void  __cdecl ReadMpMapList(void);
void  __cdecl ReadBootcampLevels(void);

/* 0x0043DD30, one caller. Free every string the scenario table owns, then the
 * table, then clear both globals. */
void __cdecl FreeScenarios(void);

/* 0x0043E160, one caller. Append one level record, allocating the table on
 * first use and growing it when full. */
void __cdecl AddLevelRecord(const AM2_Level *record);

/* 0x0043E230, six callers. The LEVEL table's by-name search: lower-cases its
 * argument in place, then walks the 0x30C records comparing LEVEL_OFF_MAP_NAME.
 * Linear, because the table is sorted by id and not by name. */
AM2_Level *__cdecl FindLevelByName(char *name);

/* 0x0043E2C0. The parser table's handler for the `MAP` command: one line of
 * campaign.txt, bootcamp.txt or mpmaps.txt into a level record. Returns 0, or
 * the number of the column that was missing. */
int32_t __cdecl DefMapLine(int32_t cmd, char *line);

/* 0x0043E9A0, one caller. AddLevelRecord's twin over the 0xCC-byte name
 * records. */
void __cdecl AddNameRecord(const AM2_NameRec *record);

/* The `rules` / `rulemap` trio, originals 0x0043EA30, 0x0043EAC0 and
 * 0x0043EBD0 -- one 592-byte entry. The two line handlers are named from the
 * keyword table at 0x00477448, which is the program's own vocabulary; the
 * appender extends the list a name record carries at +0xC0. Both handlers
 * take the line as their SECOND argument and ignore the first, like every
 * other handler in that table. */
void __cdecl NameRecAddMap(AM2_NameRec *rec, const char *entry);
int32_t __cdecl DefRulesLine(int32_t cmd, char *line);
int32_t __cdecl DefRuleMapLine(int32_t cmd, char *line);

/* 0x0042C350, one caller. The map's stored checksum, read out of the first
 * CSUM chunk of its `.amm` file. The second argument is pushed by the call
 * site and ignored by the body -- see the definition. */
uint32_t __cdecl AmmChecksum(const char *map, const char *folder);

#endif /* AM2_MAP_H */
