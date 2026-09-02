/* map.cpp -- see map.h. */
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "gamedir.h"
#include "misc.h"      /* CompareDword, TitleCaseName -- reconstructed */
#include "definfo.h"   /* DefParseInfoFile -- reconstructed */
#include "crt.h"       /* am2_log, am2_free */
#include "map.h"
#define kMapSep ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))
#include "rect.h"      /* AM2_Rect, RectSet, MakePoint */
#include "dist.h"      /* Log2Mask */
#include "maprow.h"    /* MapDescInit */
#include "region.h"    /* BuildTileDeltas, SettlePointInRegion */
#include "packkey.h"   /* PackKey */
#include "objtype.h"   /* EnsureSpriteAaiRecord */
#include "item.h"      /* CreateItem, ApplyHeightItem */
#include "air.h"      /* ObjConceal */
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
    uint32_t sum = AmmChecksum((const char *)AM2_IMAGE(ADDR_MAP_NAME),
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
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_B),       r + LEVEL_OFF_LOAD_SCREEN);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_A),       r + LEVEL_OFF_LOAD_MUSIC);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_C),       r + LEVEL_OFF_BRIEFING);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_SOUND_NAME),  r + LEVEL_OFF_BRIEF_SFX);
    strcpy((char *)AM2_IMAGE(ADDR_LEVEL_STR_D),       r + LEVEL_OFF_STRAT_MAP);

    *(int32_t *)AM2_IMAGE(ADDR_TILESET_RESERVE) =
        *(const int32_t *)(r + LEVEL_OFF_CYCLE);
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

/* FindLevelByName -- original 0x0043E230, six callers, 144 bytes. The LEVEL
 * table's by-name search, and the name ScriptListFind below could not have.
 *
 * That comment records a rename being refused: I called 0x0043E900
 * `FindLevelByName` on the strength of "loaded from the same .txt by the same
 * reader", and the compiler stopped it because AM2_LEVEL_RECORD_SIZE already
 * meant 0x30C while those records are 0xCC. The name was right and the address
 * was wrong. This is the one that walks the 0x30C records.
 *
 * It is LINEAR where FindLevelRecord's search by ID is a bsearch, which is
 * consistent rather than odd: the table is sorted by the id at
 * LEVEL_OFF_ID, so a name search cannot binary-search it.
 *
 * It lower-cases its argument IN PLACE, the same as ScriptListFind, so every
 * caller must pass a buffer and the table's own names must already be lower
 * case -- which they are, because DefMapLine copies map_name through
 * unchanged and the shipped files spell it lower case.
 *
 * The count is re-read every iteration. Fourth function in this tree to do
 * that, so it is the compiler's habit and not any of them allowing the body to
 * change it.
 */
void *__cdecl FindLevelByName(char *name)
{
    char   *base;
    int32_t i;

    if (!name)
        return (void *)0;

    orig_strlwr(name);

    for (i = 0; i < *(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT); i++) {
        base = *(char **)AM2_IMAGE(ADDR_LEVEL_TABLE);
        if (!strcmp(base + (uint32_t)i * AM2_LEVEL_RECORD_SIZE
                        + LEVEL_OFF_MAP_NAME,
                    name))
            return base + (uint32_t)i * AM2_LEVEL_RECORD_SIZE;
    }

    return (void *)0;
}

/* One column of a MAP line: the next token, with "none" meaning the empty
 * string, copied into the record. Written once because the original writes it
 * eight times.
 *
 * THE SUBSTITUTE FOR "none" IS ADDR_DIR_SCRATCH, which is a .bss char[] and
 * not a string literal. orig.h says of that buffer "whether anything ever
 * writes it is NOT established"; this is a seventh site that only makes sense
 * if it is empty, and the first where being empty is the whole point -- the
 * column means "there isn't one". Evidence, not proof.
 */
static int32_t TakeMapColumn(char *dst)
{
    char *tok = am2_strtok((char *)0,
                           (const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS));

    if (!tok)
        return 0;
    if (!strcmp(tok, (const char *)AM2_IMAGE(ADDR_STR_NONE_LOWER)))
        tok = (char *)AM2_IMAGE(ADDR_DIR_SCRATCH);
    strcpy(dst, tok);
    return 1;
}

/* DefMapLine -- original 0x0043E2C0, 1,520 bytes, and its only reference in
 * the image is the parser table at 0x00477468: it is the handler for the
 * `MAP` command, number 0x60. Same `int32(cmd, line)` shape as DefObjLine,
 * DefLinkParse and ParsePlaceLine, and `cmd` is unused.
 *
 * One line of campaign.txt, bootcamp.txt or mpmaps.txt into a 0x30C level
 * record, appended to the table with AddLevelRecord. Every column is a strtok
 * over ADDR_DEF_SEPARATORS and the columns are the ones the file's own header
 * comment names -- see LEVEL_OFF_* in orig.h, which is now spelled from that
 * header rather than from offsets.
 *
 * THE RETURN IS A COLUMN NUMBER, not a boolean. 0 is success and 2..14 say
 * which column was missing, counting from `map_name` as 2. The caller turns it
 * into "Couldn't parse %s!". Nothing distinguishes "the line ran out" from
 * "the file is a different format"; the number is how far it got.
 *
 * THE FIELDS ARE NOT WRITTEN IN OFFSET ORDER and that is the file's doing:
 * the columns run map_name(0x004), map_text(0x044), folder(0x084),
 * victorycin(0x0C4), load screen(0x204), loadmusic(0x1C4), briefing(0x248),
 * briefsfx(0x288), stratmap(0x2C8), cycle?(0x244), then the three lose movies
 * at 0x104, 0x144, 0x184. Reading the record's layout off this function in
 * order would interleave them wrongly.
 *
 * THE ID IS THE COUNT PLUS ONE, written before anything is parsed, so a line
 * that fails still consumed a number -- except that nothing records it, since
 * the record is discarded. It is the key FindLevelRecord bsearches.
 *
 * `map_text` IS THE DISPLAY NAME AND GETS TITLE CASE. Underscores become
 * spaces, and a lower-case letter is capitalised when it is the first
 * character or follows a space or a full stop. The full stop is in there
 * deliberately -- reproduced, though no shipped line has one.
 *
 * `folder` IS PREFIXED WITH "data\", so the record holds the path and not the
 * name. Both halves are unbounded copies into a 0x40-byte field, which is the
 * original's behaviour and the same class as MovieBuildName's overrun.
 *
 * `cycle?` IS THE ONE COLUMN WITH NO NULL CHECK: strtok's result goes straight
 * into DefParseBoolean, which is expected to reject it. A line that stops
 * there returns 11 by way of that parser failing rather than by a test here.
 *
 * THE LAST COLUMN IS OPTIONAL and the header does not mention it. The field at
 * LEVEL_OFF_MOVIE_INDEX is zeroed and then filled by DefParseNumber only if a
 * token is left; a missing one is not an error.
 *
 * NOT EXERCISED BY A COUNTER, because ReadLevelFile reaches it through
 * DefParseInfoFile's table by ADDRESS rather than by name -- so this is one of
 * the cases where a patch is installed and the counter still cannot move.
 *
 * THE A/B DOES COVER IT, and that is measured in both directions rather than
 * argued. Dropping the "data\" prefix from the folder puts `bootcamp` at
 * 293,671 pixels with four extra log lines -- "Couldn't open bitmap file!",
 * "Unable to load sprite bootcamp.bmp", "ReadScript: Could not open
 * bootcamp1.txt" and a `lines: 0` summary -- so an error in here takes the map
 * down loudly rather than changing nothing.
 *
 * WHAT IT DOES NOT CATCH: swapping the `briefing` and `stratmap` columns moves
 * ZERO pixels and leaves the log identical. Both are .bmp names and neither
 * reaches the frame `bootcamp` grabs, so that pair -- and by the same argument
 * any pair of same-typed columns -- is verified by the file's own header line
 * and by reading, not by this run. Said plainly, because a clean A/B on a
 * function this wide reads like more coverage than it is. */
int32_t __cdecl DefMapLine(int32_t cmd, char *line)
{
    uint8_t  rec[AM2_LEVEL_RECORD_SIZE];
    char    *tok;
    int32_t  i;
    int32_t  col;

    (void)cmd;

    memset(rec, 0, sizeof rec);
    *(int32_t *)(rec + LEVEL_OFF_ID) =
        *(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT) + 1;

    tok = am2_strtok(line, (const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS));
    if (!tok)
        return 2;
    strcpy((char *)rec + LEVEL_OFF_MAP_NAME, tok);

    tok = am2_strtok((char *)0,
                     (const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS));
    if (!tok)
        return 3;
    strcpy((char *)rec + LEVEL_OFF_NAME, tok);

    /* The original INLINES the title-caser here rather than calling it, and
     * the body it inlines is TitleCaseName instruction for instruction --
     * underscore replaced first, a lower-case letter raised when it opens the
     * string or follows a space or a full stop, strlen recomputed every turn.
     * It was about to be written a second time; misc.cpp has had it under its
     * own address all along. Fourth near-miss of this kind, and the first
     * caught by reading rather than by a tool. */
    TitleCaseName((char *)rec + LEVEL_OFF_NAME);

    tok = am2_strtok((char *)0,
                     (const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS));
    if (!tok)
        return 4;
    strcpy((char *)rec + LEVEL_OFF_FOLDER,
           (const char *)AM2_IMAGE(ADDR_STR_DATA_BACKSLASH));
    strcat((char *)rec + LEVEL_OFF_FOLDER, tok);

    if (!TakeMapColumn((char *)rec + LEVEL_OFF_WIN_MOVIE))
        return 5;
    if (!TakeMapColumn((char *)rec + LEVEL_OFF_LOAD_SCREEN))
        return 6;
    if (!TakeMapColumn((char *)rec + LEVEL_OFF_LOAD_MUSIC))
        return 7;
    if (!TakeMapColumn((char *)rec + LEVEL_OFF_BRIEFING))
        return 8;
    if (!TakeMapColumn((char *)rec + LEVEL_OFF_BRIEF_SFX))
        return 9;
    if (!TakeMapColumn((char *)rec + LEVEL_OFF_STRAT_MAP))
        return 10;

    /* No null check -- see above. */
    if (!DefParseBoolean((int32_t *)(rec + LEVEL_OFF_CYCLE),
                         am2_strtok((char *)0,
                                    (const char *)
                                        AM2_IMAGE(ADDR_DEF_SEPARATORS))))
        return 11;

    col = 12;
    for (i = 0; i < 3; i++, col++)
        if (!TakeMapColumn((char *)rec + LEVEL_OFF_LOSE_MOVIES
                           + (uint32_t)i * AM2_LEVEL_MOVIE_BYTES))
            return col;

    *(int32_t *)(rec + LEVEL_OFF_MOVIE_INDEX) = 0;
    tok = am2_strtok((char *)0,
                     (const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS));
    if (tok)
        DefParseNumber((int32_t *)(rec + LEVEL_OFF_MOVIE_INDEX), tok);

    AddLevelRecord(rec);
    return 0;
}

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

/* ParseScenarioPart -- original 0x0043DAA0, one caller, which is
 * ParseScenarios below.
 *
 * It fills one 0x0C-byte part and answers how many bytes of the buffer it
 * took, which is how the caller's cursor advances. The part is
 * {?, uint16 count, rows *}: three dwords cleared on entry, the count written
 * as a HALF word into the middle one, and a malloc'd array of SCEN_ROW_BYTES
 * records in the third. FreeScenarios frees exactly that third dword for each
 * of the four parts -- `[rec + 0x18 + n*0x0C]`, four times -- which is what
 * settles that it is an array and not, as orig.h had it, a name.
 *
 * IT SKIPS THREE VARIABLE-LENGTH RUNS BEFORE IT READS ANYTHING IT KEEPS.
 * The buffer opens with a dword, then a run of that many dwords; then two
 * more dwords, each followed by its own run. All three counts are tested with
 * a SIGNED `jle`, so a negative one skips nothing rather than stepping
 * backwards, and none of the skipped dwords is looked at. Only the fourth
 * count is kept.
 *
 * A NEGATIVE COUNT REACHES THE ALLOCATION. Zero returns before it; the
 * `count <= 0` test that guards the loop comes AFTER the malloc, so a
 * negative one asks for `count * 0x6C` bytes as a size_t and then copies
 * nothing into whatever comes back. Written in that order deliberately.
 *
 * The row on the wire is four fixed dwords -- a kind, then two counts each
 * padded to a dword, then a quad of bytes -- followed by the name. Only the
 * LOW HALF of the two middle dwords is read, so the wire pads a uint16 to
 * four bytes each time, and 0x10 goes on the byte total per row whatever the
 * name does.
 *
 * TWO OF THE FOUR BYTES ARE REWRITTEN RATHER THAN STORED. The second is
 * stored NEGATED -- a wire zero becomes 1 and anything else 0 -- and the
 * third is stored as it stands and then forced to 1 if it was zero. So the
 * wire's "0" means "yes" for one of them and "default" for the other, and a
 * reconstruction that stored either straight would be wrong in a way no
 * length check could see.
 *
 * THE NAME'S LENGTH BYTE IS SIGNED, and that is the original's, not a
 * transcription: `movsx eax, bl` before both the cursor advance and the byte
 * total. A length of 0x80 or more therefore moves the cursor BACKWARDS and
 * takes the total down with it. The copy itself is a strlen-and-copy off the
 * NUL, so the byte only advances the cursor -- it never bounds the copy, and
 * a name longer than the 0x51 bytes between SCEN_ROW_OFF_NAME and
 * SCEN_ROW_OFF_AT would run into the fields above it. Both reproduced.
 *
 * The two trailing fields are seeded rather than read: SCEN_ROW_OFF_AT takes
 * ADDR_ZERO_POINT, which is the packed origin, and SCEN_ROW_OFF_FIELD_68 is
 * cleared. Neither comes off the wire.
 */
int32_t __cdecl ParseScenarioPart(void *part, const void *at)
{
    uint8_t       *p    = (uint8_t *)part;
    const uint8_t *cur  = (const uint8_t *)at + 8;
    int32_t        took = 8;
    int32_t        n;
    int32_t        a;
    int32_t        b;
    int32_t        count;
    uint8_t       *rows;
    int32_t        i;

    *(int32_t *)(p + 0) = 0;
    *(int32_t *)(p + 4) = 0;
    *(int32_t *)(p + 8) = 0;

    n = *(const int32_t *)at;
    if (n > 0) {
        cur  += (uint32_t)n * 4;
        took  = n * 4 + 8;
    }

    a     = *(const int32_t *)cur;
    b     = *(const int32_t *)(cur + 4);
    cur  += 8;
    took += 8;
    if (a > 0) {
        cur  += (uint32_t)a * 4;
        took += a * 4;
    }
    if (b > 0) {
        cur  += (uint32_t)b * 4;
        took += b * 4;
    }

    count = *(const int32_t *)cur;
    cur  += 4;
    took += 4;
    *(uint16_t *)(p + SCENARIO_PART_OFF_COUNT) = (uint16_t)count;
    if (count == 0)
        return took;

    rows = (uint8_t *)orig_malloc((size_t)((uint32_t)count * SCEN_ROW_BYTES));
    *(uint8_t **)(p + SCENARIO_PART_OFF_ROWS) = rows;
    memset(rows, 0, (size_t)((uint32_t)count * SCEN_ROW_BYTES));
    if (count <= 0)
        return took;

    for (i = 0; i < count; i++) {
        uint8_t *row = rows + (uint32_t)i * SCEN_ROW_BYTES;
        uint8_t  amount;
        uint8_t  flag;
        uint8_t  mode;
        uint8_t  len;

        *(uint32_t *)(row + SCEN_ROW_OFF_KIND) = *(const uint32_t *)cur;
        cur += 4;
        *(uint16_t *)(row + SCEN_ROW_OFF_POS)     = *(const uint16_t *)cur;
        cur += 4;
        *(uint16_t *)(row + SCEN_ROW_OFF_POS + 2) = *(const uint16_t *)cur;
        cur += 4;

        amount = cur[0];
        flag   = cur[1];
        mode   = cur[2];
        len    = cur[3];

        row[SCEN_ROW_OFF_AMOUNT]   = amount;
        row[SCEN_ROW_OFF_FLAG]     = (uint8_t)(flag == 0 ? 1 : 0);
        row[SCEN_ROW_OFF_FIELD_12] = mode;
        row[SCEN_ROW_OFF_FIELD_68] = 0;
        *(uint32_t *)(row + SCEN_ROW_OFF_AT) =
            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
        if (mode == 0)
            row[SCEN_ROW_OFF_FIELD_12] = 1;

        if (len != 0) {
            strcpy((char *)(row + SCEN_ROW_OFF_NAME),
                   (const char *)(cur + 4));
            took += (int32_t)(int8_t)len;
            cur   = cur + 4 + (int32_t)(int8_t)len;
        } else {
            row[SCEN_ROW_OFF_NAME] = 0;
            cur += 4;
        }

        took += 0x10;
    }

    return took;
}

/* ParseScenarios -- original 0x0043DC10, one caller, which is the map loader.
 * Build the scenario table FreeScenarios tears down.
 *
 * The buffer holds a dword count and then that many 0x40-byte records, each
 * introduced by a 0x10-byte header whose first eight bytes are the literal
 * "Scenario". A header that fails that memcmp abandons the whole parse and
 * answers 0 -- with the table already allocated and the globals already
 * pointing at it, so a truncated file leaves a half-filled table behind
 * rather than none.
 *
 * THE RECORD IS CHOSEN BY A DIGIT IN THE HEADER, NOT BY THE LOOP INDEX. Byte
 * 8 -- the character after "Scenario" -- is taken as '1'..'4' and `digit -
 * '1'` indexes the table. So the records may arrive in any order, and two
 * headers naming the same digit overwrite one another silently. A byte of
 * ZERO is treated as index 0 rather than as an error, which is the one case
 * the subtraction is guarded for.
 *
 * The whole 16-byte header is then copied into the record's first sixteen
 * bytes, and the four SCENARIO_PARTS follow it at SCENARIO_OFF_PARTS -- each
 * parsed by the original's part parser, which reports how far it got so the
 * cursor can advance.
 *
 * ITS SECOND ARGUMENT IS A REMAINING-BYTES COUNTER THAT NOTHING EVER READS,
 * and it is passed BY VALUE. Four comes off it for the count, sixteen for
 * each header and the part parser's answer for each part -- in a register
 * that is discarded at the return. Nothing compares it against anything and
 * no caller can see the result. So the parse trusts the buffer from beginning
 * to end and the arithmetic is dead; both are reproduced, because a bound
 * that is computed and ignored is a fact about the original worth keeping.
 *
 * The CURSOR is by value too, and likewise never written back -- so a caller
 * cannot learn how far the parse got except from the return.
 *
 * It answers the COUNT on success and 0 on a bad header. The count is read
 * once into a local and the loop bound comes from that, not from the global
 * the header word was written to.
 */
int32_t __cdecl ParseScenarios(const uint8_t *at, int32_t remaining)
{
    const uint8_t *p = at;
    int32_t        count;
    uint8_t       *table;
    int32_t        i;

    *(int32_t *)(uintptr_t)ADDR_SCENARIO_UNREAD = 0;
    *(uint8_t **)(uintptr_t)ADDR_SCENARIOS      = (uint8_t *)0;

    count = *(const int32_t *)p;
    p += 4;
    remaining -= 4;

    table = (uint8_t *)orig_malloc((size_t)count * AM2_SCENARIO_BYTES);
    *(uint8_t **)(uintptr_t)ADDR_SCENARIOS = table;
    *(uint16_t *)(uintptr_t)ADDR_SCENARIO_COUNT = (uint16_t)count;

    for (i = 0; i < count; i++) {
        uint32_t hdr[AM2_SCENARIO_HDR_BYTES / 4];
        uint8_t *rec;
        int32_t  which;
        int32_t  part;

        hdr[0] = *(const uint32_t *)(p + 0);
        hdr[1] = *(const uint32_t *)(p + 4);
        hdr[2] = *(const uint32_t *)(p + 8);
        hdr[3] = *(const uint32_t *)(p + 12);
        p += AM2_SCENARIO_HDR_BYTES;
        remaining -= (int32_t)AM2_SCENARIO_HDR_BYTES;

        if (memcmp(hdr, (const void *)AM2_IMAGE(ADDR_STR_SCENARIO),
                   AM2_SCENARIO_TAG_BYTES) != 0)
            return 0;

        {
            uint8_t digit = ((const uint8_t *)hdr)[SCENARIO_HDR_OFF_DIGIT];

            which = digit ? (int32_t)(int8_t)digit - '1' : 0;
        }

        rec = table + (uint32_t)which * AM2_SCENARIO_BYTES;
        *(uint32_t *)(rec + 0)  = hdr[0];
        *(uint32_t *)(rec + 4)  = hdr[1];
        *(uint32_t *)(rec + 8)  = hdr[2];
        *(uint32_t *)(rec + 12) = hdr[3];

        for (part = 0; part < (int32_t)AM2_SCENARIO_PARTS; part++) {
            int32_t took = ParseScenarioPart(
                rec + SCENARIO_OFF_PARTS
                    + (uint32_t)part * SCENARIO_PART_BYTES, p);

            p += took;
            remaining -= took;
        }
    }

    /* Kept, and said out loud: GCC's own "set but not used" on this parameter
     * is the same finding the disassembly gives, arrived at independently. */
    (void)remaining;
    return count;
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
                void *rows = *(void **)(rec + SCENARIO_PART_OFF_ROWS);

                if (rows)
                    am2_free(rows);
            }
        }
    }

    am2_free(tab);

    *(int32_t *)(uintptr_t)ADDR_SCENARIO_UNREAD = 0;
    *(uint8_t **)(uintptr_t)ADDR_SCENARIOS      = (uint8_t *)0;
}

/* AddLevelRecord -- original 0x0043E160, one caller.
 *
 * Append one 0x30C-byte level record: allocate the table on first use, grow it
 * when it is full, copy the record in and bump the count.
 *
 * THE FIRST ALLOCATION AND THE GROWTH STEP DISAGREE. The initial malloc is
 * 0x2490 bytes -- room for twelve records -- and the capacity is set to twelve
 * to match; every growth after that adds SIX. So the table goes 12, 18, 24,
 * and the two constants are separate numbers in the original rather than one
 * expressed twice.
 *
 * NEITHER ALLOCATION IS CHECKED. A failed malloc leaves the table pointer NULL
 * and the memcpy that follows writes through it; a failed realloc loses the
 * old pointer as well. VC6's operator new answers NULL rather than throwing
 * and this is the CRT's malloc, so both are reachable in principle. The
 * original's, and reproduced -- it is the same absence of a check every
 * allocation in this subsystem has.
 *
 * THE GROWTH REWRITES THE CAPACITY BEFORE THE REALLOC and does not put it back
 * if the realloc fails. There is no arm that can put it back, because there is
 * no test.
 *
 * The count is re-read from its global after the realloc rather than kept in a
 * register across the call. Nothing can have changed it -- realloc does not
 * reach back into this table -- so that is the compiler, and it is written as
 * the one value it is.
 *
 * The size arithmetic is `capacity * 780` built out of shifts and `lea`, which
 * is AM2_LEVEL_RECORD_SIZE and is written as that.
 */
void __cdecl AddLevelRecord(const void *record)
{
    uint8_t *table = *(uint8_t **)AM2_IMAGE(ADDR_LEVEL_TABLE);
    int32_t  cap;
    int32_t  count;

    if (!table) {
        table = (uint8_t *)am2_malloc((size_t)AM2_LEVEL_TABLE_FIRST
                                      * AM2_LEVEL_RECORD_SIZE);
        cap   = AM2_LEVEL_TABLE_FIRST;
        *(uint8_t **)AM2_IMAGE(ADDR_LEVEL_TABLE)    = table;
        *(int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_CAP) = cap;
    } else {
        cap = *(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_CAP);
    }

    count = *(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT);

    if (count >= cap) {
        cap += AM2_LEVEL_TABLE_GROW;
        *(int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_CAP) = cap;

        table = (uint8_t *)am2_realloc(table,
                                       (size_t)cap * AM2_LEVEL_RECORD_SIZE);
        *(uint8_t **)AM2_IMAGE(ADDR_LEVEL_TABLE) = table;

        count = *(const int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT);
    }

    memcpy(table + (size_t)count * AM2_LEVEL_RECORD_SIZE, record,
           AM2_LEVEL_RECORD_SIZE);

    *(int32_t *)AM2_IMAGE(ADDR_LEVEL_TABLE_COUNT) = count + 1;
}

/* The `rules` / `rulemap` trio -- originals 0x0043EA30, 0x0043EAC0 and
 * 0x0043EBD0, one docs/functions.tsv entry of 592 bytes holding all three.
 *
 * THEIR NAMES ARE THE PROGRAM'S, and finding that out cost one dump. The
 * keyword table at 0x00477448 is {name, value, handler} triples -- the same
 * AM2_DefKeyword shape definfo.h already declares -- and 0x0043EAC0 is
 * `rules` while 0x0043EBD0 is `rulemap`. A first reading of that table was
 * four bytes out and made `map` the LINK parser's keyword; what fixed it was
 * two anchors already in orig.h, `link` landing on ADDR_DEF_LINK_PARSE and
 * `place` on ADDR_PARSE_PLACE_LINE. Second time today that dumping a table
 * settled a name I would otherwise have invented.
 *
 * WHAT THEY BUILD IS THE 0xCC-BYTE NAME RECORD, and between them they account
 * for every byte of it: `rules` writes three 0x40 tokens at +0, +0x40 and
 * +0x80, and the appender uses +0xC0, +0xC4 and +0xC8 as {capacity, count,
 * items} over 0x40-byte entries. 3 * 0x40 + 12 is 0xCC to the byte. That is
 * the tiling argument this project already uses for the trig tables: a
 * mis-sized field could not close.
 *
 * THE APPENDER IS THE THIRD USER OF THE TWELVE-THEN-SIX POLICY. AddLevelRecord
 * and AddNameRecord grow the two TABLES that way; this grows a list INSIDE one
 * record the same way, with the same absence of any allocation check. The
 * initial malloc is 0x300, which is 12 * 0x40, so the constants are the ones
 * already in orig.h rather than new ones.
 *
 * ALL THREE ERROR CODES ARE POSITIONAL. `rules` answers 2, 3 or 4 for a line
 * that runs out of tokens at the first, second or third; `rulemap` answers 2
 * for a missing or unknown list name and 3 for a missing map name. Zero is
 * success. Nothing here logs.
 *
 * BOTH TAKE THE LINE AS THEIR SECOND ARGUMENT and ignore the first, which is
 * the shape every other handler in this keyword table already has --
 * DefLinkParse, DefObjLine and ParsePlaceLine are all `int32(cmd, line)`. The
 * depth arithmetic says so on its own (`[esp+0xe0]` at a depth of 216 is
 * frame+8), and the three siblings agree.
 *
 * `rulemap` LOOKS UP ITS LIST BY NAME AND SILENTLY DROPS A LINE THAT NAMES
 * ONE THAT DOES NOT EXIST -- ScriptListFind's NULL and a missing first token
 * share the return 2, so the caller cannot tell them apart. Reproduced.
 *
 * THE THIRD `rules` TOKEN IS TitleCaseName'd AND THE OTHER TWO ARE NOT, which
 * is what says it is the one shown to a player. ScriptListFind lower-cases
 * what it is given and compares against +0, so the first stays as written.
 */
void __cdecl NameRecAddMap(void *rec, const void *entry)
{
    uint8_t *r = (uint8_t *)rec;

    if (!*(void **)(r + NAMEREC_OFF_MAPS)) {
        *(void **)(r + NAMEREC_OFF_MAPS) =
            am2_malloc((size_t)AM2_LEVEL_TABLE_FIRST * AM2_NAMEREC_FIELD);
        *(int32_t *)(r + NAMEREC_OFF_CAP) = AM2_LEVEL_TABLE_FIRST;
    }

    if (*(const int32_t *)(r + NAMEREC_OFF_COUNT)
        >= *(const int32_t *)(r + NAMEREC_OFF_CAP)) {
        int32_t cap = *(const int32_t *)(r + NAMEREC_OFF_CAP)
                      + AM2_LEVEL_TABLE_GROW;

        *(int32_t *)(r + NAMEREC_OFF_CAP) = cap;
        *(void **)(r + NAMEREC_OFF_MAPS) =
            am2_realloc(*(void **)(r + NAMEREC_OFF_MAPS),
                        (size_t)cap * AM2_NAMEREC_FIELD);
    }

    memcpy(*(uint8_t **)(r + NAMEREC_OFF_MAPS)
               + (uint32_t)*(const int32_t *)(r + NAMEREC_OFF_COUNT)
                 * AM2_NAMEREC_FIELD,
           entry, AM2_NAMEREC_FIELD);

    *(int32_t *)(r + NAMEREC_OFF_COUNT) += 1;
}

/* 0x0043EAC0. One `rules` line: three tokens into a fresh name record, the
 * third title-cased, and the record appended to the table ScriptListFind
 * searches. */
int32_t __cdecl DefRulesLine(int32_t cmd, char *line)
{
    uint8_t rec[AM2_NAME_RECORD_SIZE];
    char   *tok;

    (void)cmd;
    memset(rec, 0, sizeof rec);

    tok = am2_strtok(line, kMapSep);
    if (!tok)
        return 2;
    strcpy((char *)rec + NAMEREC_OFF_NAME, tok);

    tok = am2_strtok((char *)0, kMapSep);
    if (!tok)
        return 3;
    strcpy((char *)rec + NAMEREC_OFF_NAME2, tok);

    tok = am2_strtok((char *)0, kMapSep);
    if (!tok)
        return 4;
    strcpy((char *)rec + NAMEREC_OFF_NAME3, tok);

    TitleCaseName((char *)rec + NAMEREC_OFF_NAME3);
    AddNameRecord(rec);
    return 0;
}

/* 0x0043EBD0. One `rulemap` line: a list name and a map name, the second
 * appended to the list the first finds. */
int32_t __cdecl DefRuleMapLine(int32_t cmd, char *line)
{
    char  entry[AM2_NAMEREC_FIELD];
    char *tok;
    void *rec;

    (void)cmd;
    memset(entry, 0, sizeof entry);

    tok = am2_strtok(line, kMapSep);
    if (!tok)
        return 2;

    rec = ScriptListFind(tok);
    if (!rec)
        return 2;

    tok = am2_strtok((char *)0, kMapSep);
    if (!tok)
        return 3;
    strcpy(entry, tok);

    NameRecAddMap(rec, entry);
    return 0;
}

/* AddNameRecord -- original 0x0043E9A0, one caller, and AddLevelRecord's twin.
 *
 * The same function over the other table DefParseInfoFile fills: 0xCC-byte
 * records instead of 0x30C-byte ones, the same first twelve, the same growth
 * of six, the same two unchecked allocations, the same re-read of the count
 * after the realloc. Only the record size differs, and it appears twice --
 * once in the first malloc's literal and once in the stride.
 *
 * They are written out separately rather than shared, because the original has
 * two functions and a shared helper would be a third thing that is not in the
 * binary. The duplication is the point of comparison: if these two ever stop
 * matching line for line, one of them has been misread.
 */
void __cdecl AddNameRecord(const void *record)
{
    uint8_t *table = *(uint8_t **)AM2_IMAGE(ADDR_NAME_TABLE_BASE);
    int32_t  cap;
    int32_t  count;

    if (!table) {
        table = (uint8_t *)am2_malloc((size_t)AM2_LEVEL_TABLE_FIRST
                                      * AM2_NAME_RECORD_SIZE);
        cap   = AM2_LEVEL_TABLE_FIRST;
        *(uint8_t **)AM2_IMAGE(ADDR_NAME_TABLE_BASE) = table;
        *(int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_CAP)   = cap;
    } else {
        cap = *(const int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_CAP);
    }

    count = *(const int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_COUNT);

    if (count >= cap) {
        cap += AM2_LEVEL_TABLE_GROW;
        *(int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_CAP) = cap;

        table = (uint8_t *)am2_realloc(table,
                                       (size_t)cap * AM2_NAME_RECORD_SIZE);
        *(uint8_t **)AM2_IMAGE(ADDR_NAME_TABLE_BASE) = table;

        count = *(const int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_COUNT);
    }

    memcpy(table + (size_t)count * AM2_NAME_RECORD_SIZE, record,
           AM2_NAME_RECORD_SIZE);

    *(int32_t *)AM2_IMAGE(ADDR_NAME_TABLE_COUNT) = count + 1;
}

/* AmmChecksum -- original 0x0042C350, one caller.
 *
 * The map's stored checksum, read out of its `.amm` file. An IFF walk:
 * `FORM`, a length, `MAP `, then a chunk tag, a chunk length, and four bytes
 * of payload.
 *
 * IT DOES NOT SEARCH THE FILE. There is no loop: the `CSUM` chunk has to be
 * the FIRST one after `MAP `, and its length has to be exactly 4. Anything
 * else -- a different first chunk, a longer CSUM, a file that is not IFF at
 * all -- falls straight through to the close and answers 0. So this reads one
 * fixed layout rather than parsing a container.
 *
 * ZERO IS BOTH "no checksum" AND "the checksum is zero", and the function
 * cannot tell a caller which. The sum is initialised to 0 before the file is
 * even opened and every failure path returns it untouched, including the
 * failed open, which reaches the same `return` by way of the fclose being
 * skipped rather than by a branch of its own.
 *
 * NOT ONE OF THE SIX READS IS CHECKED. A truncated file leaves each slot
 * holding whatever the stack had, and the tag comparisons then decide on
 * rubbish -- which will almost always fail and answer 0, but is not
 * guaranteed to. Same absence of a check as every other reader in this
 * subsystem.
 *
 * The FORM length is read into a slot nothing looks at. Reproduced: the read
 * has to happen to advance the stream, and a `fseek` would be a different
 * function.
 *
 * IT TAKES A SECOND ARGUMENT AND IGNORES IT. The caller pushes the map name
 * and the folder; the body reads only the name, and does its chdir with the
 * folder global's address as a LITERAL rather than with the parameter it was
 * handed. Same pointer either way, which is why nothing has ever gone wrong
 * with it -- and why the parameter is kept, unnamed, rather than dropped: the
 * call site pushes two and the signature has to say so.
 *
 * That also means the existing `orig_amm_checksum` macro was right about the
 * arity and my first reading of the body was not. The body alone says one
 * argument; only the call site says two.
 *
 * It chdirs into the folder and does not put it back, as OpenSaveForLoad does
 * with the save directory.
 */
uint32_t __cdecl AmmChecksum(const char *map, const char *)
{
    char      name[AM2_AMM_NAME_BYTES];
    uint32_t  sum = 0;
    uint32_t  tag;
    uint32_t  len;
    am2_FILE *fp;

    SetGameDir((const char *)AM2_IMAGE(ADDR_MAP_FOLDER));

    am2_sprintf(name, (const char *)AM2_IMAGE(ADDR_FMT_DOT_AMM), map);

    fp = orig_fopen(name, (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp)
        return sum;

    orig_fread(&tag, 4, 1, fp);
    if (tag == AM2_IFF_FORM) {
        orig_fread(&len, 4, 1, fp);          /* the FORM length, unread */
        orig_fread(&tag, 4, 1, fp);

        if (tag == AM2_IFF_MAP) {
            orig_fread(&tag, 4, 1, fp);
            orig_fread(&len, 4, 1, fp);

            if (tag == AM2_IFF_CSUM && len == 4)
                orig_fread(&sum, 4, 1, fp);
        }
    }

    orig_fclose(fp);
    return sum;
}

/* FreeMapSurfaces is reconstructed, in win32/surface.cpp. Declared here rather
 * than by including that header because map.cpp is on the flat side of the
 * split; `void(void)` names nothing platform, which is what makes the
 * declaration enough. */
extern "C" void __cdecl FreeMapSurfaces(void);

/* FreeMapLayers -- original 0x0042D3D0, two callers: the level teardown and
 * the map loader, which clears before it fills. Free every per-map allocation.
 *
 * THE REGIONS GO FIRST AND THEY OWN A SECOND ALLOCATION EACH. Every region
 * with a non-zero REGION_OFF_NLINKS has its REGION_OFF_LINKS freed before the
 * array itself is, and the loop is bounded by ADDR_REGION_STRIDE -- the same
 * int16 the routing matrices are square on, which is what says it is the
 * region COUNT and not just their pitch.
 *
 * THE COUNT IS RE-READ EVERY ITERATION AND SO IS THE ARRAY. The original
 * reloads both globals inside the loop, which matters only if `free` could
 * change them; it cannot, and they are reloaded anyway. Reproduced.
 *
 * TWELVE POINTERS THEN FOLLOW, each guarded against null, freed and cleared --
 * and the clear is what makes calling this twice safe, which is exactly what
 * the loader relies on. The list was extracted from the disassembly by script;
 * twelve near-identical blocks is the shape a hand copy skips one of.
 */
void __cdecl FreeMapLayers(void)
{
    static const uint32_t kLayers[] = {
        ADDR_REGION_NEXT, ADDR_REGION_COST, ADDR_MAP_TILES, ADDR_TILE_ATTRS,
        ADDR_CELL_WEIGHTS, ADDR_MAP_PADBIT_LAYER, ADDR_MAP_PAD_LAYER,
        ADDR_TILE_KIND, ADDR_REGION_OF_CELL, ADDR_TILE_FLAGS, ADDR_TILE_COVER,
    };
    uint32_t i;

    FreeMapSurfaces();

    if (*(void *const *)(uintptr_t)ADDR_REGIONS) {
        int32_t n;

        for (n = 0; n < *(const int16_t *)(uintptr_t)ADDR_REGION_STRIDE; n++) {
            uint8_t *r = (uint8_t *)*(void *const *)(uintptr_t)ADDR_REGIONS
                         + (uint32_t)n * AM2_REGION_SIZE;

            if (*(const uint8_t *)(r + REGION_OFF_NLINKS) > 0)
                am2_free(*(void **)(r + REGION_OFF_LINKS));
        }

        am2_free(*(void **)(uintptr_t)ADDR_REGIONS);
        *(void **)(uintptr_t)ADDR_REGIONS = 0;
    }

    for (i = 0; i < sizeof kLayers / sizeof kLayers[0]; i++) {
        void **p = (void **)(uintptr_t)kLayers[i];

        if (*p) {
            am2_free(*p);
            *p = 0;
        }
    }
}



/* ---- LoadMap ----------------------------------------------------------- */
/* PreloadSpriteByKey is reconstructed in win32/sprite.cpp; declared here
 * rather than included, because map.cpp is flat and AM2_Sprite carries an
 * LPDIRECTDRAWSURFACE.  An incomplete type is enough -- only two int16 fields
 * of the returned sprite are read, and those are reached by offset. */
struct AM2_Sprite;
extern "C" AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a,
                                                  int32_t b);

/* The map declares its own per-record field layout, so ADDR_MAP_FIELD_DESCS is
 * a buffer the loader fills rather than a table the image ships. */
typedef struct {
    uint32_t tag;
    uint32_t size;
} AM2_MapFieldDesc;

typedef am2_FILE *(__cdecl *am2_map_fopen_fn)(const char *p, const char *m);
typedef size_t (__cdecl *am2_map_fread_fn)(void *p, size_t sz, size_t n,
                                           am2_FILE *fp);
typedef int32_t (__cdecl *am2_map_fseek_fn)(am2_FILE *fp, int32_t off,
                                            int32_t whence);
typedef int32_t (__cdecl *am2_map_fclose_fn)(am2_FILE *fp);
typedef int32_t (__cdecl *am2_load_atl_fn)(const char *path);
typedef void *(__cdecl *am2_map_weapon_fn)(const char *name, int32_t army,
                                           int32_t kind, uint32_t where,
                                           int32_t a, int32_t b, int32_t c,
                                           uint32_t uid);

#define orig_map_fopen   ((am2_map_fopen_fn)(uintptr_t)ADDR_FOPEN)
#define orig_map_fread   ((am2_map_fread_fn)(uintptr_t)ADDR_FREAD)
#define orig_map_fseek   ((am2_map_fseek_fn)(uintptr_t)ADDR_FSEEK)
#define orig_map_fclose  ((am2_map_fclose_fn)(uintptr_t)ADDR_FCLOSE)
#define orig_load_atl    ((am2_load_atl_fn)(uintptr_t)ADDR_LOAD_ATL_FILE)
#define orig_map_weapon  ((am2_map_weapon_fn)(uintptr_t)ADDR_CREATE_WEAPON)


/* w*h bytes straight into one global.  Seven of the arms are exactly this. */
static uint8_t *ReadPlane(am2_FILE *f, uint32_t size, int32_t *consumed)
{
    uint8_t *p = (uint8_t *)am2_malloc(size);

    orig_map_fread(p, size, 1, f);
    *consumed += (int32_t)size;
    return p;
}

/* MHDR's tail.  Three rectangles, the listener, both map descriptors and the
 * tile deltas, all derived from w and h.  ADDR_CAMERA_Y is field +4 of
 * ADDR_VISIBLE_TILES and is written by the same RectSet as the rest of it. */
static void DeriveMapGeometry(void)
{
    AM2_Rect r;

    (*(AM2_Rect *)(uintptr_t)ADDR_MAP_BOUNDS)    = *RectSet(&r, 0, 0, (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X), (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y));
    /* The inset rect already has four field names in this tree rather than
     * one rect name, so it is written a field at a time -- adding a second
     * base over storage that is already named is the mistake orig.h opens
     * with. */
    RectSet(&r, 0x40, 0x40, (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X) - 0x40, (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y) - 0x40);
    *(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_LEFT   = r.left;
    *(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_TOP    = r.top;
    *(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_RIGHT  = r.right;
    *(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_BOTTOM = r.bottom;
    (*(uint32_t *)(uintptr_t)ADDR_LISTENER_POS)  = MakePoint(0x10, 0x10);
    (*(uint32_t *)(uintptr_t)ADDR_LISTENER_POS_PREV) = (*(uint32_t *)(uintptr_t)ADDR_LISTENER_POS);
    (*(AM2_Rect *)(uintptr_t)ADDR_VISIBLE_TILES) = *RectSet(&r, 1, 1, (*(const int32_t *)(uintptr_t)ADDR_VIEW_TILES_W) + 1, (*(const int32_t *)(uintptr_t)ADDR_VIEW_TILES_H) + 1);

    MapDescInit((void *)(uintptr_t)ADDR_OBJ_MAP_DESC,
                (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X), (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y));
    MapDescInit((void *)(uintptr_t)ADDR_MAP_DESC, (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X), (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y));
    BuildTileDeltas();
}

/* One OLAY batch's records.  Each is described by the field table the FILE
 * just supplied, so the widths come from there and a field with no arm is
 * read for its stated width and dropped -- which is what RESV is.
 *
 * ELOW OVERRIDES ELEV AND OWNR.  When it is non-zero the record takes its
 * elevation from ELOW's high nibble and its owner from the low one, and the
 * two single-byte fields are not used at all.  Three fields, two
 * destinations, and a precedence that is invisible from any one arm. */
static void LoadMapRecords(am2_FILE *f, uint8_t *objs, int32_t first,
                           int32_t nfields, int32_t count, int32_t *consumed)
{
    int32_t rec;

    for (rec = 0; rec < count; rec++) {
        int32_t index = -1;
        int32_t move = 0, numb = 0, trig = 0, ownr = 0, elev = 0, elow = 0;
        char    scri[0x80];
        int32_t i;

        scri[0] = 0;

        for (i = 0; i < nfields; i++) {
            uint32_t tag  = ((AM2_MapFieldDesc *)(uintptr_t)ADDR_MAP_FIELD_DESCS)[i].tag;
            uint32_t size = ((AM2_MapFieldDesc *)(uintptr_t)ADDR_MAP_FIELD_DESCS)[i].size;
            int32_t  v    = 0;

            orig_map_fread(&v, size, 1, f);
            *consumed += (int32_t)size;

            switch (tag) {
            case AM2_IFF_MOVE: move = (int8_t)v; break;
            case AM2_IFF_NUMB: numb = (int8_t)v; break;
            case AM2_IFF_TRIG: trig = (int8_t)v; break;
            case AM2_IFF_OWNR: ownr = (int8_t)v; break;
            case AM2_IFF_ELEV: elev = (int8_t)v; break;
            case AM2_IFF_ELOW: elow = (int8_t)v; break;
            case AM2_IFF_INDX: index = v + first; break;
            case AM2_IFF_SCRI:
                if (v > 0) {
                    orig_map_fread(scri, (size_t)v, 1, f);
                    *consumed += v;
                }
                break;
            default:
                break;          /* read for its declared width and dropped */
            }
        }

        if (index < 0) {
            am2_log("Missing index attribute for item #%d!\n", rec);
            continue;
        }

        {
            uint8_t *r = objs + (size_t)index * 0x1C;
            uint8_t  owner;

            r[0x14] = (uint8_t)move;
            if (elow != 0) {
                r[0x12] = (uint8_t)((elow >> 4) & 0xF);
                owner   = (uint8_t)(elow & 0xF);
            } else {
                r[0x12] = (uint8_t)elev;
                owner   = (uint8_t)ownr;
            }
            r[0x13] = owner;
            r[0x10] = (uint8_t)trig;
            r[0x11] = 0;
            r[0x15] = (uint8_t)numb;

            if (scri[0] != 0) {
                size_t n = strlen(scri) + 1;

                *(char **)(r + 0x18) = (char *)am2_malloc(n);
                memcpy(*(char **)(r + 0x18), scri, n);
            }
        }
    }
}

/* Allocate and zero a layer the file did not supply. */
static void DefaultLayerPtr(void **slot, int32_t bytes)
{
    if (*slot == 0) {
        *slot = am2_malloc(bytes);
        memset(*slot, 0, bytes);
    }
}

typedef int32_t (__cdecl *am2_kind_allowed_fn)(int32_t kind);
typedef void (__cdecl *am2_respawn_pool_fn)(int32_t seed);
#define orig_build_respawn_pool \
    ((am2_respawn_pool_fn)(uintptr_t)ADDR_BUILD_RESPAWN_POOL)

#define orig_respawn_kind_allowed \
    ((am2_kind_allowed_fn)(uintptr_t)ADDR_RESPAWN_KIND_ALLOWED)

typedef int32_t (__cdecl *am2_respawn_kind_fn)(int32_t *out);
#define orig_random_respawn_kind \
    ((am2_respawn_kind_fn)(uintptr_t)ADDR_RANDOM_RESPAWN_KIND)


/* Turn the parse records into objects.  NOT "CreateWeapon per record": each
 * one packs a sprite key, may have its kind SUBSTITUTED from the weighted
 * respawn pool, preloads its sprite and takes that sprite's own offset into
 * its position before anything is created. */
static void BuildMapObjects(uint8_t *objs, int32_t count)
{
    int32_t i;

    for (i = 0; i < count; i++) {
        uint8_t  *r     = objs + (size_t)i * AM2_MAPREC_BYTES;
        int32_t   type  = *(const int32_t *)(r + MAPREC_OFF_TYPE);
        int32_t   kind  = *(const int32_t *)(r + MAPREC_OFF_KIND);
        uint8_t   trig  = r[MAPREC_OFF_TRIG];
        uint8_t   owner = r[MAPREC_OFF_OWNER];
        int32_t   army  = owner != 0 ? (int32_t)owner - 1 : 4;
        int32_t   flags = 0;
        AM2_Point at;
        void     *obj;

        if (trig & 0x20)
            flags = 4;
        if (trig & 0x08)
            flags |= 0x810;

        at.x = *(const int16_t *)(r + MAPREC_OFF_X);
        at.y = *(const int16_t *)(r + MAPREC_OFF_Y);

        if (type != 0x19) {
            /* An ITEM. */
            void *spr = PreloadSpriteByKey(PackKey(type + 0x14, kind, 0),
                                           0x1000, 1);

            if (spr != 0) {
                at.x = (int16_t)(at.x + *(const int16_t *)((uint8_t *)spr + 0x24));
                at.y = (int16_t)(at.y + *(const int16_t *)((uint8_t *)spr + 0x26));
            }
            obj = CreateItem(*(char **)(r + MAPREC_OFF_SCRIPT), army,
                             PackKey(type + 0x14, kind, 0),
                             *(const uint32_t *)&at, flags, 1, 0);
            if (obj != 0 && ObjIsWatchedKind(obj)
                && (int32_t)g_defaultOwner != army)
                ObjConceal(obj, 1);
            continue;
        }

        /* A WEAPON, and the branch where a kind of 0x64 or more is either
         * substituted from the respawn pool or reduced by 0x64. */
        {
            int32_t  aai = -1, key = PackKey(type + 0x14, kind, 0);
            uint32_t settled;
            int32_t  code = kind;
            void    *spr;
            uint8_t *aaiRec;

            if (code >= 0x64) {
                if (*(const uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS & 2) {
                    int32_t sub = 0;

                    code = orig_random_respawn_kind(&sub);
                    aai  = EnsureSpriteAaiRecord(0x2D, code, 0);
                    flags |= 0x10000;
                    key = *(const int32_t *)((*(uint8_t ***)(uintptr_t)
                                              ADDR_AAI_RECORDS)[aai] + 8);
                    goto haveRecord;
                }
                if (code == 0x64)
                    continue;
                code -= 0x64;
                key = PackKey(type + 0x14, code, 0);
            }
            if (orig_respawn_kind_allowed(code) == 0)
                continue;
            aai = EnsureSpriteAaiRecord(type + 0x14, code, 0);

        haveRecord:
            if (aai < 0)
                continue;

            spr = PreloadSpriteByKey(key, 0x1000, 1);
            if (spr != 0) {
                at.x = (int16_t)(at.x + *(const int16_t *)((uint8_t *)spr + 0x24));
                at.y = (int16_t)(at.y + *(const int16_t *)((uint8_t *)spr + 0x26));
            }

            aaiRec = (*(uint8_t ***)(uintptr_t)ADDR_AAI_RECORDS)[aai];
            flags |= *(const int32_t *)(aaiRec + 4);

            /* BOTH POINTS ARE PASSED: the one before SettlePointInRegion and
             * the one it may have moved.  The original keeps the first in a
             * register and hands the slot Settle writes as a separate
             * argument, so they are two arguments and not one. */
            settled = *(const uint32_t *)&at;
            SettlePointInRegion(TileOfPoint(settled), (uint32_t *)&at);

            /* AND THE ARMY IS THE LITERAL 4, not the record's owner: the
             * register that carried the owner has been reused twice by here,
             * first for the key and then for the point. */
            obj = orig_map_weapon(*(char **)(r + MAPREC_OFF_SCRIPT), 4,
                                  aai, settled, flags,
                                  (int32_t)*(const uint32_t *)&at, 1, 0);
            if (obj != 0) {
                const uint8_t *attrs =
                    *(const uint8_t **)(uintptr_t)ADDR_TILE_ATTRS;
                int32_t tile = *(const uint16_t *)((uint8_t *)obj + 0x1A);

                ApplyHeightItem(obj, (int8_t)attrs[tile] + r[MAPREC_OFF_ELEV]);
            }
        }
    }
}

/* 0x0042C440. Load one map: its tileset, its layers and its objects.
 *
 * TWO ARGUMENTS, and orig.h said one until its caller was read: `base` feeds
 * both "%s.atl" and "%s.amm", `folder` goes to SetGameDir and is kept in
 * ADDR_MAP_BLOCK so the directory can be restored on the way out.
 *
 * THE FORMAT IS SELF-DESCRIBING, which is the thing to hold on to. The chunk
 * tags decide which layer is filled; the map then declares its own per-record
 * field layout and the record loop obeys whatever widths it was given.
 * Nothing here may assume a layout.
 *
 * AND A MISSING CHUNK IS NOT AN ERROR: nine layers are allocated and zeroed
 * after the walk if the file did not supply them, so every consumer
 * downstream may assume its layer exists. Two of the nine have no chunk arm
 * at all and are created only here. */
int32_t __cdecl LoadMap(const char *base, const char *folder)
{
    char      path[0x104];
    am2_FILE *f;
    uint32_t  tag, size, formSize;
    int32_t   consumed, plane, i;
    int32_t   objCount = 0, nfields = 0, tlayDone = 0;
    uint8_t  *objs = 0;

    SetGameDir(folder);
    memset(((char *)(uintptr_t)ADDR_MAP_BLOCK), 0, 0x1A0);
    strcpy(((char *)(uintptr_t)ADDR_MAP_BLOCK), folder);

    sprintf(path, "%s.atl", base);
    if (orig_load_atl(path) == 0) {
        return 0;
    }

    sprintf(path, "%s.amm", base);
    f = orig_map_fopen(path, "rb");
    if (f == 0)
        return 0;

    orig_map_fread(&tag, 4, 1, f);
    if (tag != AM2_IFF_FORM)
        goto bad;
    orig_map_fread(&formSize, 4, 1, f);
    orig_map_fread(&tag, 4, 1, f);
    if (tag != AM2_IFF_MAP)
        goto bad;
    consumed = 0xC;

    do {
        int32_t bad_size = 0;

        orig_map_fread(&tag, 4, 1, f);
        orig_map_fread(&size, 4, 1, f);
        consumed += 8;
        plane = (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_W) * (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_H);

        switch (tag) {
        case AM2_IFF_MHDR: {
            int32_t hdr[3];

            if (size != 0xC) { bad_size = 1; break; }
            orig_map_fread(hdr, 0xC, 1, f);
            consumed += 0xC;
            (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_W)      = hdr[0];
            (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_H)      = hdr[1];
            (*(int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT)    = Log2Mask(hdr[0]) & 0xFF;
            (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X)     = (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_W) << 4;
            (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y)     = (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_H) << 4;
            (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_SHIFT) = Log2Mask((*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X)) & 0xFF;
            if ((*(int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT) < 1) {
                FreeMapLayers();
                goto bad;
            }
            DeriveMapGeometry();
            break;
        }

        /* Seven planes, one shape.  Only the global differs. */
        case AM2_IFF_NPAD:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_MAP_PAD_LAYER = ReadPlane(f, size, &consumed);
            break;
        case AM2_IFF_BPAD:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_MAP_PADBIT_LAYER = ReadPlane(f, size, &consumed);
            break;
        case AM2_IFF_MOVE:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_CELL_WEIGHTS = ReadPlane(f, size, &consumed);
            break;
        case AM2_IFF_OWNR:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_TILE_KIND = ReadPlane(f, size, &consumed);
            break;
        case AM2_IFF_TRIG:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_TILE_FLAGS = ReadPlane(f, size, &consumed);
            break;
        case AM2_IFF_REGN:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_REGION_OF_CELL = ReadPlane(f, size, &consumed);
            break;
        case AM2_IFF_ELEV:
            if ((int32_t)size != plane) { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_TILE_ATTRS = ReadPlane(f, size, &consumed);
            break;

        /* Owns nothing: a scratch buffer the parser consumes and we free. */
        case AM2_IFF_SCEN: {
            uint8_t *tmp = (uint8_t *)am2_malloc(size);

            if (tmp == 0) { bad_size = 2; break; }
            orig_map_fread(tmp, size, 1, f);
            consumed += (int32_t)size;
            ParseScenarios(tmp, (int32_t)size);
            am2_free(tmp);
            break;
        }

        /* NOT a plane: a 0x14-byte header first, and the chunk is w*h*2 PLUS
         * that header.  Only the first TLAY in a file is taken. */
        case AM2_IFF_TLAY: {
            int32_t hdr[5], w, h, bytes;

            if (tlayDone > 0) { bad_size = 2; break; }
            orig_map_fread(hdr, 0x14, 1, f);
            consumed += 0x14;
            /* The two the original reads are hdr[1] and hdr[2].  Its `lea`
             * for the buffer happens two pushes in and the reads two more,
             * so the displacements are four dwords further out than the
             * fields they name -- taking them at face value gives hdr[3] and
             * hdr[4], which are zero, and the tile plane is silently never
             * read. */
            w     = hdr[1];
            h     = hdr[2];
            bytes = (w * h) << 1;
            if (bytes == 0)
                break;
            if (w != (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_W) || h != (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_H)) { bad_size = 1; break; }
            if ((int32_t)size != bytes + 0x14)        { bad_size = 1; break; }
            *(uint8_t **)(uintptr_t)ADDR_MAP_TILES = ReadPlane(f, (uint32_t)bytes, &consumed);
            tlayDone++;
            break;
        }

        /* NOT a plane either.  The on-disk record is 0x10 and the in-memory
         * one 0x1C, so they are read one at a time; the array is a TEMPORARY
         * that the tail turns into objects and frees. */
        case AM2_IFF_OLAY: {
            int32_t count = 0, first = objCount, n, recCount = 0, junk;
            uint8_t *rec;

            orig_map_fread(&count, 4, 1, f);
            consumed += 4;
            if (count > 0) {
                objCount += count;
                objs = (uint8_t *)am2_realloc(objs, (size_t)objCount * 0x1C);
                rec  = objs + (size_t)first * 0x1C;
                memset(rec, 0, (size_t)count * 0x1C);
                consumed += count * 0x10;
                for (n = 0; n < count; n++, rec += 0x1C)
                    orig_map_fread(rec, 0x10, 1, f);
            }

            orig_map_fread(&junk, 4, 1, f);
            orig_map_fread(&junk, 4, 1, f);
            orig_map_fread(&nfields, 4, 1, f);
            consumed += 0xC;
            for (i = 0; i < nfields; i++) {
                orig_map_fread(&((AM2_MapFieldDesc *)(uintptr_t)ADDR_MAP_FIELD_DESCS)[i].tag, 4, 1, f);
                orig_map_fread(&((AM2_MapFieldDesc *)(uintptr_t)ADDR_MAP_FIELD_DESCS)[i].size, 4, 1, f);
                consumed += 8;
            }

            orig_map_fread(&recCount, 4, 1, f);
            consumed += 4;
            if (recCount > 0)
                LoadMapRecords(f, objs, first, nfields, recCount, &consumed);
            break;
        }

        default:
            bad_size = 2;
            break;
        }

        if (bad_size != 0) {
            if (bad_size == 1)
                am2_log("chunksize/targetsize different\n");
            orig_map_fseek(f, (int32_t)size, 1);
            consumed += (int32_t)size;
        }
    } while (consumed < (int32_t)formSize);

    /* Nine layers every consumer may assume exists. */
    plane = (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_W) * (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_H);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_CELL_WEIGHTS, plane);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_TILE_ATTRS, plane);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_REGION_OF_CELL, plane);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_MAP_PADBIT_LAYER, plane);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_MAP_PAD_LAYER, plane);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_TILE_KIND, plane);
    DefaultLayerPtr((void **)(uintptr_t)ADDR_TILE_FLAGS, plane);
    /* AND A LOOP, WHICH IS WHY ENUMERATING THE EXPLICIT BLOCKS MISSED THESE.
     * The original walks every pointer from ADDR_TILE_REVEAL_GRIDS up to
     * ADDR_TILE_COVER four bytes at a time -- the four per-army reveal grids
     * -- and fills each null one.  Leaving them null does not fault: it makes
     * SettlePointInRegion spiral for a passable tile it can never find. */
    {
        void **slot = (void **)(uintptr_t)ADDR_TILE_REVEAL_GRIDS;

        while (slot < (void **)(uintptr_t)ADDR_TILE_COVER) {
            DefaultLayerPtr(slot, plane);
            slot++;
        }
    }
    DefaultLayerPtr((void **)(uintptr_t)ADDR_TILE_COVER, plane);

    /* FOUR THINGS THE DEFAULTING IS FOLLOWED BY, and leaving them out is what
     * cost this function four attempts.  ADDR_LOAD_PENDING is a FLAG tested
     * here, not a layer to allocate -- it shares its code shape with an
     * allocate-if-null block and was swallowed by the scan that found those. */
    SealMapEdges();
    RebuildTileCover();
    BuildAaiBuiltins();

    if (*(const int32_t *)(uintptr_t)ADDR_LOAD_PENDING == 0) {
        void *cam = CreateItem((char *)"camera", 4,
                               *(const int32_t *)(uintptr_t)ADDR_AAI_KEY_980000,
                               *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT,
                               0, 1, 0);

        *(int32_t *)(uintptr_t)ADDR_EVT_ID15_UID =
            *(const int32_t *)((const uint8_t *)cam + 4);
    }

    orig_build_respawn_pool(*(const int32_t *)(uintptr_t)ADDR_GAME_SEED);

    BuildMapObjects(objs, objCount);

    am2_log("freeing temporary map load data...\n");
    for (i = 0; i < objCount; i++) {
        char **owned = (char **)(objs + (size_t)i * 0x1C + 0x18);

        if (*owned != 0)
            am2_free(*owned);
    }
    am2_free(objs);

    orig_map_fclose(f);
    SetGameDir(((char *)(uintptr_t)ADDR_MAP_BLOCK));
    return 1;

bad:
    am2_log("Error in loadmap()\n");
    orig_map_fclose(f);
    SetGameDir(((char *)(uintptr_t)ADDR_MAP_BLOCK));
    return 0;
}

void map_install(void)
{
    patch_replace(ADDR_LEVEL_COUNT, (const void *)LevelCount,
                        "LevelCount", 2);
    patch_replace(ADDR_FIND_LEVEL_BY_NAME, (const void *)FindLevelByName,
                  "FindLevelByName", 6);
    patch_replace(ADDR_DEF_MAP_LINE, (const void *)DefMapLine,
                  "DefMapLine", 0);
    patch_replace(ADDR_FREE_MAP_LAYERS, (const void *)FreeMapLayers,
                  "FreeMapLayers", 2);
    patch_replace(ADDR_LOAD_MAP, (const void *)LoadMap,
                  "LoadMap", 1);
    patch_replace(ADDR_PARSE_SCENARIOS, (const void *)ParseScenarios,
                  "ParseScenarios", 1);
    patch_replace(ADDR_PARSE_SCENARIO_PART, (const void *)ParseScenarioPart,
                  "ParseScenarioPart", 1);
    patch_replace(ADDR_FREE_SCENARIOS, (const void *)FreeScenarios,
                  "FreeScenarios", 1);
    patch_replace(ADDR_ADD_LEVEL_RECORD, (const void *)AddLevelRecord,
                  "AddLevelRecord", 1);
    patch_replace(ADDR_ADD_NAME_RECORD, (const void *)AddNameRecord,
                  "AddNameRecord", 1);
    patch_replace(ADDR_TRACE_TILE_LINE, (const void *)TraceTileLine,
                        "TraceTileLine", 7);
    patch_replace(ADDR_NAMEREC_ADD_MAP, (const void *)NameRecAddMap,
                  "NameRecAddMap", 1);
    patch_replace(ADDR_DEF_RULES_LINE, (const void *)DefRulesLine,
                  "DefRulesLine", 1);
    patch_replace(ADDR_DEF_RULEMAP_LINE, (const void *)DefRuleMapLine,
                  "DefRuleMapLine", 1);
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
    patch_replace(ADDR_AMM_CHECKSUM, (const void *)AmmChecksum,
                  "AmmChecksum", 1);
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
