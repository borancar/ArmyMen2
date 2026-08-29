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

#define kMapSaveBlock ((void *)(uintptr_t)AM2_IMAGE(ADDR_MAP_BLOCK))

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

/* 0x0042B210, five callers -- PointOfTile's two-pointer twin.
 *
 * The same arithmetic and NOT quite the same code. PointOfTile shifts a
 * 16-bit register for the row (`shr ax, cl`) and packs the result into one
 * dword; this masks the tile to 16 bits first and then shifts 32-bit, and
 * writes two int32 out. The answers agree for every tile a map can hold --
 * the mask is what makes them agree -- and both add 8, which is the centre of
 * a 16-pixel tile.
 *
 * The x mask is `(w << 4) - 16`, which is only a mask at all because the
 * map's width is a power of two; ADDR_MAP_ROW_SHIFT is its log and the y half
 * uses that instead. If the two ever disagreed the x would wrap and the y
 * would not.
 *
 * VERIFIED BY READING, and measured rather than assumed. It runs 63,504 times
 * in one live Boot Camp mission -- so this is not a cold path -- and swapping
 * x for y is invisible anyway: `bootcamp` stays at its usual 22 pixels and
 * `mission` at 287, inside the 281..302 band four clean runs give. The calls
 * happen and the answer goes somewhere the frame does not show. Same standing
 * as the trig tables had before tools/trigdump.py.
 */
void __cdecl TileToXY(int32_t tile, int32_t *x, int32_t *y)
{
    int32_t  w     = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W;
    uint8_t  shift = *(const uint8_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;
    uint32_t t     = (uint32_t)tile & 0xFFFFu;

    *x = (int32_t)(((t << AM2_TILE_SHIFT)
                    & (uint32_t)((w << AM2_TILE_SHIFT) - 16)) + 8);
    *y = (int32_t)(((t >> shift) << AM2_TILE_SHIFT) + 8);
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

    if (!ScriptListFind((char *)AM2_IMAGE(ADDR_MP_SCRIPT_NAME)))
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

/* Still original: the CRT lower-caser. ReadMpMapList beside it is ours, and
 * declared above. */
typedef char *(__cdecl *AM2_StrlwrFn)(char *);
#define orig_strlwr       ((AM2_StrlwrFn)AM2_IMAGE(ADDR_CRT_STRLWR))

/* 0x0043E900, five callers. Find a record by name in the name registry --
 * the SECOND of the two triples FreeLevelTables owns, whose records are 0xCC
 * bytes where the level records are 0x30C.
 *
 * I renamed this `FindLevelByName` on the strength of "loaded from the same
 * .txt by the same reader" and the compiler refused it: AM2_LEVEL_RECORD_SIZE
 * already existed as 0x30C. Same file, same reader, two tables, two strides --
 * and the name stays what it was until something says what these records are.
 *
 * THAT NEAR-RENAME COST AN HOUR IN A WAY WORTH RECORDING. Backing it out with
 * a blanket replace of AM2_LEVEL_RECORD_SIZE across this file also rewrote
 * FindLevelRecord's bsearch stride from 0x30C to 0xCC, which broke the whole
 * campaign load -- and the first two bisects blamed this function, because
 * disabling its patch_replace leaves the DIRECT CALL in MpScriptChecksum
 * pointing at it either way. Disabling a patch does not disable a call.
 *
 * THE FIRST SEARCH IS ALSO THE LOAD. A zero count calls ADDR_READ_MP_MAPS
 * before searching, so nothing has to arrange for the table to exist -- and a
 * caller that searches an empty table twice pays for the parse once.
 *
 * It lower-cases its argument IN PLACE. Every caller passes a buffer, which is
 * what makes that safe; a string literal would be written to.
 *
 * The count is re-read every iteration, as it is in ObjTileChanged and
 * StepObjRows -- three functions in this tree now, so it is the compiler's
 * habit rather than any of them allowing the body to change it.
 */
void *__cdecl ScriptListFind(char *name)
{
    char   *base;
    int32_t i;

    if (!name)
        return (void *)0;

    if (!*(const int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_COUNT))
        ReadMpMapList();

    orig_strlwr(name);

    base = *(char **)AM2_IMAGE(ADDR_NAME_TABLE_BASE);
    for (i = 0; i < *(const int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_COUNT); i++) {
        char *rec = base + (uint32_t)i * AM2_NAME_RECORD_SIZE;

        if (!strcmp(rec, name))
            return rec;
    }

    return (void *)0;
}

/* 0x0042E390, seven callers. The tiles a line crosses, from one packed point
 * to another: a Bresenham walk in TILE space writing each tile index into the
 * caller's uint16 array and counting them.
 *
 * THE WALK NEVER RECOMPUTES A TILE. TileOfPoint runs once, for the starting
 * point, and after that the index is stepped by +/-1 across and by
 * +/-ADDR_MAP_TILES_W down. That is the whole reason it is cheap enough to run
 * per line -- and it also means an index that runs off the end of a row wraps
 * into the next one rather than being clipped. Nothing here checks the bounds
 * of the map, and nothing bounds the output array; both are the caller's.
 *
 * BOTH POINT ARGUMENTS' STACK SLOTS BECOME SCRATCH once they are decoded --
 * the original keeps the running output index in the first and the Bresenham
 * error in the second. Written as locals; there is nothing to reproduce in
 * where a value happens to live.
 *
 * THE TWO LOOPS ARE THE SAME LOOP with x and y exchanged, chosen on
 * 2|dx| > 2|dy|, and the doubling is done ONCE before the choice rather than
 * inside either. The error starts at 2|minor| - |major|, which is the ordinary
 * form; what is worth noticing is that the major axis is compared for EQUALITY
 * with its target, not for having passed it. With the steps derived from the
 * sign of the difference that always terminates, but it is why a zero-length
 * line writes one tile and stops: the y-major arm is chosen when both deltas
 * are zero, and its equality test fires before the first step.
 *
 * The starting tile is written before either loop and the count set to 1, so
 * the array always holds at least one entry and the count is never zero.
 *
 * The sign of each step is computed as `setge; dec; and 0xFE; inc`, which is
 * +1 for a non-negative difference and -1 otherwise. Written as the sign it
 * is; the four instructions are what MSVC makes of a ternary.
 *
 * SUPERSEDED: those 35 came from BeginMoveTo, which is reconstructed now and
 * calls this BY NAME -- so the counter reads 0 from here on and the figure
 * below is the last measurement taken through the patched entry.
 *
 * MEASURED AT 35 CALLS on a driven Boot Camp mission, with TileOfPoint at
 * 3.9 million on the same run -- so this is not on a hot path and the 35 are
 * whatever asked for a line. That is enough for the A/B to compare it and not
 * enough to claim either arm: which of the two loops those 35 took is not
 * established, and neither is whether any of them had a zero-length line.
 * Said as the gap it is rather than argued away as likely.
 */
void __cdecl TraceTileLine(uint32_t from, uint32_t to,
                           uint16_t *tiles, int32_t *count)
{
    /* Both points are packed: x in the low half, y in the high, each signed.
       The original reads them with `movsx`, so the shift is arithmetic. */
    int32_t tx0 = (int32_t)(int16_t)(from & 0xFFFFu) >> 4;
    int32_t ty0 = (int32_t)(int16_t)(from >> 16) >> 4;
    int32_t tx1 = (int32_t)(int16_t)(to & 0xFFFFu) >> 4;
    int32_t ty1 = (int32_t)(int16_t)(to >> 16) >> 4;
    int32_t dx  = tx1 - tx0;
    int32_t dy  = ty1 - ty0;
    int32_t adx = (dx < 0 ? -dx : dx) * 2;
    int32_t ady = (dy < 0 ? -dy : dy) * 2;
    int32_t stepX = dx >= 0 ? 1 : -1;
    int32_t stepY = dy >= 0 ? 1 : -1;
    int32_t stepDown = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W * stepY;
    int32_t tile = TileOfPoint(from);
    int32_t err;
    int32_t i;

    *count   = 0;
    tiles[0] = (uint16_t)tile;
    *count   = 1;
    i        = 1;

    if (adx > ady) {
        err = ady - adx / 2;
        while (tx0 != tx1) {
            if (err >= 0) {
                tile += stepDown;
                err  -= adx;
            }
            err  += ady;
            tx0  += stepX;
            tile += stepX;

            tiles[i] = (uint16_t)tile;
            *count   = i + 1;
            i        = *count;
        }
        return;
    }

    err = adx - ady / 2;
    while (ty0 != ty1) {
        if (err >= 0) {
            tile += stepX;
            err  -= ady;
        }
        tile += stepDown;
        ty0  += stepY;
        err  += adx;

        tiles[i] = (uint16_t)tile;
        *count   = i + 1;
        i        = *count;
    }
}

/* 0x0043ED40, two callers, five bytes of body: the number of records in
 * ADDR_LEVEL_TABLE. Both callers are in the menu band beside CloseScreen,
 * which is what makes it the level COUNT rather than some other total sharing
 * the address -- the global was already named from FindLevelRecord's side.
 *
 * Measured at 0 on a driven Boot Camp mission: its two callers are menu code
 * that path does not reach. The counter is not blind. */
int32_t __cdecl LevelCount(void)
{
    return *(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT);
}

/* FreeScenarios -- original 0x0043DD30, one caller.
 *
 * Free every string the scenario table owns, then the table, then clear both
 * globals. The records are 0x40 bytes -- four dwords, then four 0x0C-byte
 * parts each ending in a malloc'd name -- and it is only those four names per
 * record that are owned.
 *
 * A NULL TABLE SKIPS EVERYTHING INCLUDING THE CLEARS, which is the one branch
 * worth noticing: the original tests the pointer first and jumps past the two
 * stores, so a table that was never built leaves the count word alone. It is
 * already 0 in that case, so nothing observable turns on it, and it is
 * reproduced as written rather than tidied into an unconditional clear.
 *
 * A count of 0 still frees the table. The bound is tested with an UNSIGNED
 * compare against a word, and re-read from the global on every iteration --
 * `free` cannot change it, so that is the compiler keeping a register free
 * rather than anything defensive. Written as the plain loop it is.
 *
 * The allocator is the game's, because the strings came from the game's.
 */
void __cdecl FreeScenarios(void)
{
    uint8_t *tab = *(uint8_t **)(uintptr_t)ADDR_SCENARIOS;

    if (!tab)
        return;

    {
        uint32_t i;

        for (i = 0; i < *(const uint16_t *)(uintptr_t)ADDR_SCENARIO_COUNT;
             i++) {
            uint8_t *rec = tab + (size_t)i * AM2_SCENARIO_BYTES
                           + SCENARIO_OFF_PARTS;
            uint32_t k;

            for (k = 0; k < AM2_SCENARIO_PARTS; k++, rec += SCENARIO_PART_BYTES) {
                char *name = *(char **)(rec + SCENARIO_PART_OFF_NAME);

                if (name)
                    am2_free(name);
            }
        }
    }

    am2_free(tab);

    *(int32_t *)(uintptr_t)ADDR_SCENARIO_UNREAD = 0;
    *(uint8_t **)(uintptr_t)ADDR_SCENARIOS      = (uint8_t *)0;
}

void map_install(void)
{
    patch_replace(ADDR_LEVEL_COUNT, (const void *)LevelCount,
                        "LevelCount", 2);
    patch_replace(ADDR_FREE_SCENARIOS, (const void *)FreeScenarios,
                  "FreeScenarios", 1);
    patch_replace(ADDR_TRACE_TILE_LINE, (const void *)TraceTileLine,
                        "TraceTileLine", 7);
    patch_replace(ADDR_SCRIPT_LIST_FIND, (const void *)ScriptListFind,
                  "ScriptListFind", 5);
    patch_replace(ADDR_TILE_TO_XY, (const void *)TileToXY,
                  "TileToXY", 5);
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
