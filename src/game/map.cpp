/* map.cpp -- see map.h. */
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "gamedir.h"
#include "misc.h"      /* CompareDword -- reconstructed */
#include "definfo.h"   /* DefParseInfoFile -- reconstructed */
#include "crt.h"       /* am2_log, am2_free */
#include "map.h"
#include "savetag.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kMapSaveBlock ((void *)(uintptr_t)AM2_IMAGE(ADDR_MAP_SAVE_BLOCK))

int32_t __cdecl SaveMapSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_MAP);
    orig_fwrite(kMapSaveBlock, AM2_MAP_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadMapSection(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_MAP,
                      (const char *)AM2_IMAGE(ADDR_STR_MAP_CPP), 0x906))
        return 0;

    orig_fread(kMapSaveBlock, AM2_MAP_SAVE_SIZE, 1, fp);
    return 1;
}

/* 0x0042DBB0. XOR every whole dword of a file together.
 *
 * Named by its own two log lines, which are a matched pair -- "Checksum of %s "
 * with no newline, then "is %x \n" -- so one call produces one line whichever
 * way it goes. Both are unconditional: a file that will not open still gets
 * announced and still reports a checksum, which is 0.
 *
 * A TRAILING PARTIAL DWORD IS DROPPED. fread asks for one 4-byte item and
 * returns 0 for anything short, so a file whose length is not a multiple of
 * four ignores its last one to three bytes. Reproduced.
 *
 * The original reuses its own argument slot as the read buffer; that is a
 * register-allocation detail with no observable side, so this uses a local. */
uint32_t __cdecl Checksum(const char *path)
{
    uint32_t  sum = 0;
    uint32_t  word;
    am2_FILE *fp;

    orig_log("Checksum of %s ", path);

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_MODE_RB));

    if (fp != (am2_FILE *)0) {
        while (orig_fread(&word, 4, 1, fp) != 0)
            sum ^= word;

        orig_fclose(fp);
    }

    orig_log("is %x \n", sum);
    return sum;
}

int32_t __cdecl TileOfPoint(uint32_t packed)
{
    int32_t x = (int16_t)(packed & 0xFFFFu);
    int32_t y = (int16_t)(packed >> 16);

    if (x < 0 || y < 0)
        return 0;
    if (x >= *(const int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X)
        return 0;
    if (y >= *(const int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y)
        return 0;
    return (y >> AM2_TILE_SHIFT) * *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W
           + (x >> AM2_TILE_SHIFT);
}

uint32_t __cdecl PointOfTile(int32_t tile)
{
    int32_t w     = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    int32_t shift = *(const int16_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;
    uint16_t x, y;

    x = (uint16_t)((((uint32_t)tile << AM2_TILE_SHIFT)
                    & (uint32_t)((w << AM2_TILE_SHIFT) - 16)) + 8);
    /* A 16-BIT shift of the tile index, then scaled -- the original does
     * `shr ax, cl` and not `shr eax, cl`. */
    y = (uint16_t)(((uint32_t)((uint16_t)tile >> shift) << AM2_TILE_SHIFT) + 8);
    return (uint32_t)x | ((uint32_t)y << 16);
}

/* The three data checksums a multiplayer session compares, 0x004303B0,
 * 0x00430400 and 0x00430450.
 *
 * They are XOR folds of Checksum over the files each side has to agree on --
 * the rules, the mission script and the map -- and the panel refresh below
 * stores all three in globals for the handshake to send. Nothing here is
 * cryptographic and nothing is meant to be: it catches a different EDITION of
 * the data, not a tampered one.
 *
 * Each one chdirs first, because the game opens data files by bare name. */
uint32_t __cdecl RulesChecksum(void)
{
    uint32_t sum;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_AAI_DIR));

    sum  = Checksum((const char *)AM2_IMAGE(ADDR_STR_GAME_AAI));
    sum ^= Checksum((const char *)AM2_IMAGE(ADDR_STR_OBJECT_AAI));
    sum ^= Checksum((const char *)AM2_IMAGE(ADDR_STR_TROOP_AAI));
    sum ^= Checksum((const char *)AM2_IMAGE(ADDR_STR_VEHICLE_AAI));
    sum ^= Checksum((const char *)AM2_IMAGE(ADDR_STR_WEAPON_AAI));
    return sum;
}

/* 0x00430400. The guard is a lookup in a registry, not a test that the string
 * is non-empty -- a multiplayer script that is not registered gets no checksum
 * at all and the side that has it disagrees with nobody. Note the lookup
 * LOWER-CASES its argument in place; the buffer is the game's own. */
uint32_t __cdecl MpScriptChecksum(void)
{
    char path[0x100];

    if (!orig_script_list_find((char *)AM2_IMAGE(ADDR_MP_SCRIPT_NAME)))
        return 0;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_RULES_DIR));
    sprintf(path, (const char *)AM2_IMAGE(ADDR_FMT_DOT_TXT),
            (const char *)AM2_IMAGE(ADDR_MP_SCRIPT_NAME));
    return Checksum(path);
}

/* 0x00430450. The map's own contribution is the .amm reader's answer -- which
 * is NOT Checksum over the file, but a walk of its chunks -- xored with the
 * object table the map was authored against. */
uint32_t __cdecl MapChecksum(void)
{
    uint32_t sum = orig_amm_checksum((const char *)AM2_IMAGE(ADDR_MAP_NAME),
                                     (const char *)AM2_IMAGE(ADDR_MAP_FOLDER));

    SetGameDir((const char *)AM2_IMAGE(ADDR_MAP_FOLDER));
    return sum ^ Checksum((const char *)AM2_IMAGE(ADDR_STR_OBJECT_AAI));
}

/* 0x0043ED50, ten callers -- everything that chooses a level goes through it:
 * the Boot Camp button, SELECT MAP's OK, the multiplayer panel and the
 * state-2 entry.
 *
 * Seven strings out of the level record into the globals the loader reads,
 * and one flag. Nothing else: the record stays where it is and nothing is
 * validated. A null record is the only refusal.
 *
 * The copies are unbounded `strcpy` into 0x40-byte globals, exactly as the
 * original writes them -- the record's own field size is the only bound, and
 * the fields are 0x40 apart at both ends. Kept as it is, for the reason
 * SetGameDir's overrun is kept: a bounded copy here would be a different
 * function whose failure mode nothing else expects.
 *
 * The order is the original's, which copies +0x204 before +0x1C4. Nothing
 * observes the difference; it is free to keep and it is one less thing that
 * differs from the disassembly. */
void __cdecl SelectLevel(const void *record)
{
    const char *r = (const char *)record;

    if (!r)
        return;

    strcpy((char *)AM2_IMAGE(ADDR_MAP_NAME),          r + LEVEL_OFF_MAP_NAME);
    strcpy((char *)AM2_IMAGE(ADDR_MAP_FOLDER),        r + LEVEL_OFF_FOLDER);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_B),       r + LEVEL_OFF_STR_204);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_A),       r + LEVEL_OFF_STR_1C4);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_C),       r + LEVEL_OFF_STR_248);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_SOUND_NAME),  r + LEVEL_OFF_SOUND_NAME);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_D),       r + LEVEL_OFF_STR_2C8);

    *(int32_t *)AM2_IMAGE(ADDR_TILESET_RESERVE) =
        *(const int32_t *)(r + LEVEL_OFF_RESERVE10);
}

/* 0x0043E8B0 -- empty both level tables. Two {base, count, capacity} triples
 * side by side: the level RECORDS, and the name registry
 * ADDR_SCRIPT_LIST_FIND searches. The counts and capacities are cleared
 * before the frees and the bases after, which is the order the original
 * writes and costs nothing to keep.
 *
 * That the same function owns both is what identifies the second table: it is
 * loaded from the same `.txt` by the same reader, which the searcher alone
 * could not have said. */
void __cdecl FreeLevelTables(void)
{
    void *levels = *(void **)AM2_IMAGE(ADDR_LEVEL_TABLE);
    void *names;

    *(int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT) = 0;
    *(int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_CAP)   = 0;
    if (levels)
        am2_free(levels);

    names = *(void **)AM2_IMAGE(ADDR_NAME_TABLE_BASE);
    *(void **)AM2_IMAGE(ADDR_LEVEL_TABLE) = (void *)0;
    *(int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_COUNT) = 0;
    *(int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_CAP)   = 0;
    if (names)
        am2_free(names);

    *(void **)AM2_IMAGE(ADDR_NAME_TABLE_BASE) = (void *)0;
}

/* 0x0043E1F0 -- find a level record by id, with the CRT's own bsearch over
 * the sorted table.
 *
 * The KEY is a whole 0x30C-byte record on the stack with only its first dword
 * set. The rest is never initialised and never read: the comparator at
 * CompareDword looks at that one field. Reproduced as written -- a
 * four-byte key would be a different function if the comparator ever grew. */
void *__cdecl FindLevelRecord(int32_t id)
{
    uint8_t key[AM2_LEVEL_RECORD_SIZE];

    *(int32_t *)key = id;

    return ((AM2_BsearchFn)AM2_IMAGE(ADDR_CRT_BSEARCH))(
               key, *(void **)AM2_IMAGE(ADDR_LEVEL_TABLE),
               (uint32_t)*(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT),
               AM2_LEVEL_RECORD_SIZE,
               (const void *)CompareDword);
}

/* 0x0043EC80, 0x0043ECC0 and 0x0043ED00 -- one shape three times, differing
 * only in the filename: empty the tables, chdir to whatever the shared
 * scratch buffer holds, and parse. A failure is logged and nothing else; the
 * tables are simply left empty.
 *
 * The chdir target is ADDR_DIR_SCRATCH, the same eighty-reference char[] the
 * panel's chat line "clears" its field from. Here it holds a directory, which
 * is what the name says and what the other use does not. */
static void ReadLevelFile(const char *name)
{
    FreeLevelTables();
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    if (!DefParseInfoFile(name))
        am2_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE), name);
}

void __cdecl ReadCampaignLevels(void)
{
    ReadLevelFile((const char *)AM2_IMAGE(ADDR_STR_CAMPAIGN_TXT));
}

void __cdecl ReadMpMapList(void)
{
    ReadLevelFile((const char *)AM2_IMAGE(ADDR_STR_MPMAPS_TXT));
}

void __cdecl ReadBootcampLevels(void)
{
    ReadLevelFile((const char *)AM2_IMAGE(ADDR_STR_BOOTCAMP_TXT));
}

void map_install(void)
{
    patch_replace(ADDR_POINT_OF_TILE, (const void *)PointOfTile,
                  "PointOfTile", 1);
    patch_replace(ADDR_TILE_OF_POINT, (const void *)TileOfPoint,
                  "TileOfPoint", 1);
    patch_replace(ADDR_CHECKSUM, (const void *)Checksum, "Checksum", 1);
    patch_replace(ADDR_SAVE_MAP_SECTION, (const void *)SaveMapSection,
                  "SaveMapSection", 1);
    patch_replace(ADDR_LOAD_MAP_SECTION, (const void *)LoadMapSection,
                  "LoadMapSection", 1);
    patch_replace(ADDR_RULES_CHECKSUM, (const void *)RulesChecksum,
                  "RulesChecksum", 1);
    patch_replace(ADDR_MP_SCRIPT_CHECKSUM, (const void *)MpScriptChecksum,
                  "MpScriptChecksum", 1);
    patch_replace(ADDR_MAP_CHECKSUM, (const void *)MapChecksum,
                  "MapChecksum", 1);
    patch_replace(ADDR_SELECT_LEVEL, (const void *)SelectLevel,
                  "SelectLevel", 10);
    patch_replace(ADDR_FREE_LEVEL_TABLES, (const void *)FreeLevelTables,
                  "FreeLevelTables", 3);
    patch_replace(ADDR_FIND_LEVEL_RECORD, (const void *)FindLevelRecord,
                  "FindLevelRecord", 10);
    patch_replace(ADDR_READ_CAMPAIGN_FILE, (const void *)ReadCampaignLevels,
                  "ReadCampaignLevels", 2);
    patch_replace(ADDR_READ_MP_MAPS, (const void *)ReadMpMapList,
                  "ReadMpMapList", 3);
    patch_replace(ADDR_LOAD_BOOTCAMP_LEVELS, (const void *)ReadBootcampLevels,
                  "ReadBootcampLevels", 1);
}
