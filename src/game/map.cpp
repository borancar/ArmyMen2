/* map.cpp -- see map.h. */
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>   /* abs */
#include <string.h>

#include "gamedir.h"
#include "misc.h"      /* CompareDword, TitleCaseName -- reconstructed */
#include "definfo.h"   /* DefParseInfoFile -- reconstructed */
#include "crt.h"       /* am2_log, am2_free */
/* Under VC6, map.cpp defines TileOfPoint with the sixteen-bit result the
 * image's `xor ax, ax` shows, while every other file declares it `int` --
 * the original's own mismatch, harmless under C linkage. GCC keeps one
 * int32_t declaration: a 16-bit return read as 32 would be undefined. */
#define AM2_MAP_DEFINES_TILEOFPOINT
#include "map.h"
#define kMapSep (" \t\n;,")
#include "rect.h"      /* AM2_Rect, RectSet, MakePoint */
#include "dist.h"      /* Log2Mask */
#include "maprow.h"    /* MapDescInit */
#include "region.h"    /* BuildTileDeltas, SettlePointInRegion */
#include "packkey.h"   /* PackKey */
#include "spritetype.h" /* AM2_Sprite, its hot spot */
#include "objtype.h"   /* EnsureSpriteAaiRecord */
#include "item.h"      /* CreateItem, ApplyHeightItem */
#include "air.h"      /* ObjConceal */
#include "savetag.h"
#include "image.h"
#include "../inject/orig.h"

/* The map file's field descriptors: {FourCC tag, dword count}[7] -- INDX(4),
 * MOVE, ELOW, TRIG, RESV, ELEV, OWNR (each 1). Transcribed from 0x00485FB8. */
extern "C" uint32_t am2_map_field_descs[14] = {   /* NOT const: LoadMap freads the map's own layout into it */
    0x58444E49u, 4, 0x45564F4Du, 1, 0x574F4C45u, 1, 0x47495254u, 1,
    0x56534552u, 1, 0x56454C45u, 1, 0x524E574Fu, 1,
};
#include "../inject/patch.h"

/* Reconstructed in win32/mapdraw.cpp beside RestoreTileSet, which reads the
 * same .atl container. Forward-declared rather than included: this module
 * is flat. NO extern "C" -- mapdraw.h closes its block at 129 and declares
 * this at 181, outside it. */
int32_t __cdecl LoadAtlFile(const char *path);

int32_t __cdecl SaveMapSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_MAP);
    orig_fwrite(g_mapBlock, AM2_MAP_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadMapSection(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_MAP,
                      "C:\\ArmyMen2\\source\\map.cpp", 0x906))
        return 0;

    orig_fread(g_mapBlock, AM2_MAP_SAVE_SIZE, 1, fp);
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

    fp = orig_fopen(path, "rb");

    if (fp != (am2_FILE *)0) {
        while (orig_fread(&word, 4, 1, fp) != 0)
            sum ^= word;

        orig_fclose(fp);
    }

    orig_log("is %x \n", sum);
    return sum;
}

#if defined(_MSC_VER)
extern "C" uint16_t __cdecl TileOfPoint(AM2_Point pt)
#else
int32_t __cdecl TileOfPoint(AM2_Point pt)
#endif
{
    if (pt.x < 0 || pt.y < 0)
        return 0;
    if (pt.x >= g_mapExtentX)
        return 0;
    if (pt.y >= g_mapExtentY)
        return 0;
    return (pt.y >> AM2_TILE_SHIFT) * g_mapTilesW
           + (pt.x >> AM2_TILE_SHIFT);
}

AM2_Point __cdecl PointOfTile(int32_t tile)
{
    AM2_Point p;   /* built in memory -- the original reuses `tile`'s slot */

    /* The globals are read where they are used, mask before index. */
    p.x = (int16_t)((((uint32_t)g_mapTilesW
                       << AM2_TILE_SHIFT) - 16)
                    & ((uint32_t)tile << AM2_TILE_SHIFT)) + 8;
    /* A 16-BIT shift of the tile index, then scaled -- the original does
     * `shr ax, cl` and not `shr eax, cl`. */
    p.y = (int16_t)((((uint16_t)tile
                      >> *(const int16_t *)&g_mapRowShift)
                     << AM2_TILE_SHIFT) + 8);
    return p;
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
void __cdecl TileToXY(uint16_t tile, int32_t *x, int32_t *y)
{
    uint32_t t = tile;

    /* The globals are read where they are used, mask before index. */
    *x = (int32_t)((((uint32_t)g_mapTilesW
                     << AM2_TILE_SHIFT) - 16)
                   & (t << AM2_TILE_SHIFT)) + 8;
    *y = (int32_t)(((t >> g_mapRowShift)
                    << AM2_TILE_SHIFT) + 8);
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

    SetGameDir("aai");

    sum  = Checksum("game.aai");
    sum ^= Checksum("object.aai");
    sum ^= Checksum("troop.aai");
    sum ^= Checksum("vehicle.aai");
    sum ^= Checksum("weapon.aai");
    return sum;
}

/* 0x00430400. The guard is a lookup in a registry, not a test that the string
 * is non-empty -- a multiplayer script that is not registered gets no checksum
 * at all and the side that has it disagrees with nobody. Note the lookup
 * LOWER-CASES its argument in place; the buffer is the game's own. */
uint32_t __cdecl MpScriptChecksum(void)
{
    char path[0x100];

    if (!ScriptListFind(g_mpScriptName))
        return 0;

    SetGameDir("rules");
    sprintf(path, "%s.txt",
            g_mpScriptName);
    return Checksum(path);
}

/* 0x00430450. The map's own contribution is the .amm reader's answer -- which
 * is NOT Checksum over the file, but a walk of its chunks -- xored with the
 * object table the map was authored against. */
uint32_t __cdecl MapChecksum(void)
{
    uint32_t sum = AmmChecksum(g_mapName_,
                               g_tilesetPath);

    SetGameDir(g_tilesetPath);
    return sum ^ Checksum("object.aai");
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
void __cdecl SelectLevel(const AM2_Level *record)
{
    const AM2_Level *r = record;

    if (!r)
        return;

    strcpy(g_mapName_,          r->mapName);
    strcpy(g_tilesetPath,        r->folder);
    strcpy(g_levelStrB,       r->loadScreen);
    strcpy(g_levelStrA,       r->loadMusic);
    strcpy(&g_levelStrC,       r->briefing);
    strcpy(&g_levelSoundName,  r->briefSfx);
    strcpy(g_levelStrD,       r->stratMap);

    g_tilesetReserve =
        r->cycle;
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
    AM2_Level *levels = g_levelTable;
    AM2_NameRec *names;

    g_levelCount = 0;
    g_levelTableCap   = 0;
    if (levels)
        am2_free(levels);

    names = g_nameTableBase;
    g_levelTable = (AM2_Level *)0;
    g_nameTableCount = 0;
    g_nameTableCap   = 0;
    if (names)
        am2_free(names);

    g_nameTableBase = (AM2_NameRec *)0;
}

/* 0x0043E1F0 -- find a level record by id, with the CRT's own bsearch over
 * the sorted table.
 *
 * The KEY is a whole 0x30C-byte record on the stack with only its first dword
 * set. The rest is never initialised and never read: the comparator at
 * CompareDword looks at that one field. Reproduced as written -- a
 * four-byte key would be a different function if the comparator ever grew. */
AM2_Level *__cdecl FindLevelRecord(int32_t id)
{
    AM2_Level key;

    key.id = id;

    return (AM2_Level *)((AM2_BsearchFn)((void *)(uintptr_t)(ADDR_CRT_BSEARCH)))(
               &key, g_levelTable,
               (uint32_t)g_levelCount,
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
/* `inline` is load-bearing: the original has this body at every call site,
 * and VC6's /O2 (/Ob1) inlines only functions declared inline. Out of line,
 * each caller compiled to push/call/pop -- the relink found it. */
static inline void ReadLevelFile(const char *name)
{
    FreeLevelTables();
    SetGameDir(g_dirScratch);

    if (!DefParseInfoFile(name))
        am2_log("Couldn't parse %s!\n", name);
}

void __cdecl ReadCampaignLevels(void)
{
    ReadLevelFile("campaign.txt");
}

void __cdecl ReadMpMapList(void)
{
    ReadLevelFile("mpmaps.txt");
}

void __cdecl ReadBootcampLevels(void)
{
    ReadLevelFile("bootcamp.txt");
}

/* Still original: the CRT lower-caser. ReadMpMapList beside it is ours, and
 * declared above. */
typedef char *(__cdecl *AM2_StrlwrFn)(char *);
#define orig_strlwr       ((AM2_StrlwrFn)((void *)(uintptr_t)(ADDR_CRT_STRLWR)))

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
AM2_Level *__cdecl FindLevelByName(char *name)
{
    /* The level table, read afresh wherever it is used. */
#define LEVEL(i) (&g_levelTable[i])
    int32_t i;

    if (!name)
        return (AM2_Level *)0;

    orig_strlwr(name);

    for (i = 0; i < g_levelCount; i++)
        if (!strcmp(LEVEL(i)->mapName, name))
            return LEVEL(i);

    return (AM2_Level *)0;
#undef LEVEL
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
/* A macro rather than an inline function: each column's missing-token exit
 * is a `return` in the image, laid out inline after the test, where a helper
 * returning a flag leaves VC6 materialising it. */
#define TAKE_MAP_COLUMN(dst, missing)                                         \
    tok = am2_strtok((char *)0, " \t\n;,");                                   \
    if (!tok)                                                                 \
        return (missing);                                                     \
    if (!strcmp("none", tok))                                                 \
        strcpy((dst), g_dirScratch);       \
    else                                                                      \
        strcpy((dst), tok)

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
    AM2_Level rec;
    char    *tok;
    int32_t  i;
    int32_t  col;

    (void)cmd;

    memset(&rec, 0, sizeof rec);
    rec.id =
        g_levelCount + 1;

    tok = am2_strtok(line, " \t\n;,");
    if (!tok)
        return 2;
    strcpy(rec.mapName, tok);

    tok = am2_strtok((char *)0,
                     " \t\n;,");
    if (!tok)
        return 3;
    strcpy(rec.name, tok);

    /* The original INLINES the title-caser here rather than calling it, and
     * the body it inlines is TitleCaseName instruction for instruction --
     * underscore replaced first, a lower-case letter raised when it opens the
     * string or follows a space or a full stop, strlen recomputed every turn.
     * It was about to be written a second time; misc.cpp has had it under its
     * own address all along. Fourth near-miss of this kind, and the first
     * caught by reading rather than by a tool. */
    TitleCaseNameInline(rec.name);

    tok = am2_strtok((char *)0,
                     " \t\n;,");
    if (!tok)
        return 4;
    strcpy(rec.folder,
           "data\\");
    strcat(rec.folder, tok);

    TAKE_MAP_COLUMN(rec.winMovie, 5);
    TAKE_MAP_COLUMN(rec.loadScreen, 6);
    TAKE_MAP_COLUMN(rec.loadMusic, 7);
    TAKE_MAP_COLUMN(rec.briefing, 8);
    TAKE_MAP_COLUMN(rec.briefSfx, 9);
    TAKE_MAP_COLUMN(rec.stratMap, 10);

    /* No null check -- see above. */
    if (!DefParseBoolean(&rec.cycle,
                         am2_strtok((char *)0,
                                    " \t\n;,")))
        return 11;

    col = 12;
    for (i = 0; i < 3; col++, i++) {
        /* Not the column macro: here each arm has its own strcpy. */
        tok = am2_strtok((char *)0, " \t\n;,");
        if (!tok)
            return col;
        if (!strcmp("none", tok))
            strcpy(rec.loseMovies[i],
                   g_dirScratch);
        else
            strcpy(rec.loseMovies[i], tok);
    }

    rec.movieIndex = 0;
    tok = am2_strtok((char *)0,
                     " \t\n;,");
    if (tok)
        DefParseNumber(&rec.movieIndex, tok);

    AddLevelRecord(&rec);
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
AM2_NameRec *__cdecl ScriptListFind(char *name)
{
    int32_t i;

    if (!name)
        return (AM2_NameRec *)0;

    if (!g_nameTableCount)
        ReadMpMapList();

    orig_strlwr(name);

    /* The name table, read afresh wherever it is used. */
#define REC(i) (&g_nameTableBase[i])
    for (i = 0; i < g_nameTableCount; i++)
        if (!strcmp(REC(i)->name, name))
            return REC(i);

    return (AM2_NameRec *)0;
#undef REC
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
void __cdecl TraceTileLine(AM2_Point from, AM2_Point to,
                           uint16_t *tiles, int32_t *count)
{
    /* The coordinates are signed (`movsx`), so the shift is arithmetic. */
    int32_t tx0 = from.x >> 4;
    int32_t ty0 = from.y >> 4;
    int32_t tx1 = to.x >> 4;
    int32_t ty1 = to.y >> 4;
    int32_t dx  = tx1 - tx0;
    int32_t dy  = ty1 - ty0;
    int32_t adx = abs(dx) * 2;
    int32_t ady = abs(dy) * 2;
    int32_t stepX = dx < 0 ? -1 : 1;
    int32_t stepY = dy < 0 ? -1 : 1;

    int32_t stepDown = g_mapTilesW * stepY;
    /* Called as the `int` every file but this one declares it. */
    int32_t tile = ((int32_t (__cdecl *)(AM2_Point))TileOfPoint)(from);

    int32_t i;

    *count   = 0;
    tiles[0] = (uint16_t)tile;
    i = ++*count;

    if (adx > ady) {
        dy = ady - (adx >> 1);
        while (tx0 != tx1) {
            if (dy >= 0) {
                tile += stepDown;
                dy  -= adx;
            }
            tx0  += stepX;
            tile += stepX;
            dy  += ady;

            tiles[i] = (uint16_t)tile;
            i = ++*count;
        }
        return;
    }

    dy = adx - (ady >> 1);
    while (ty0 != ty1) {
        if (dy >= 0) {
            tile += stepX;
            dy  -= ady;
        }
        tile += stepDown;
        ty0  += stepY;
        dy  += adx;

        tiles[i] = (uint16_t)tile;
        i = ++*count;
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
    return g_levelCount;
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
 * nothing into whatever comes back. That test is the row loop's own entry
 * test; a separate `if (count <= 0) return took;` moved the return out of
 * the place the image keeps it.
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
int32_t __cdecl ParseScenarioPart(AM2_ScenPart *p, const void *at)
{
    const uint8_t *cur;
    int32_t        took;
    int32_t        n;
    int32_t        a;
    int32_t        b;
    int32_t        count;
    AM2_ScenRow   *rows;
    int32_t        i;

    /* VC6's inline memset: three dword stores through a copy of p. */
    memset(p, 0, sizeof *p);

    cur = (const uint8_t *)at;
    n = *(const int32_t *)cur;
    cur += 8;
    took = 8;
    if (n > 0) {
        cur  += (uint32_t)n * 4;
        took  = n * 4 + 8;
    }

    a     = *(const int32_t *)cur;
    cur  += 4;
    took += 8;
    b     = *(const int32_t *)cur;
    cur  += 4;
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
    p->count = (int16_t)count;
    if (count == 0)
        goto done;      /* the one return, as the image has it */

    rows = p->rows = (AM2_ScenRow *)orig_malloc((size_t)((uint32_t)count * SCEN_ROW_BYTES));
    memset(rows, 0, (size_t)((uint32_t)count * SCEN_ROW_BYTES));

    for (i = 0; i < count; i++, rows++) {
        AM2_ScenRow   *row = rows;
        const uint8_t *q;
        int8_t         len;

        row->kind  = *(const int32_t *)cur;
        cur += 4;
        row->pos.x = *(const int16_t *)cur;
        cur += 4;
        row->pos.y = *(const int16_t *)cur;
        cur += 4;

        /* Four bytes read one at a time through a walking pointer. */
        q = cur;
        row->amount  = *q++;
        row->flag    = (uint8_t)(*q++ == 0 ? 1 : 0);
        row->field12 = *q;
        row->field68 = 0;
        q++;
        row->at = g_zeroPoint;
        if (!row->field12)
            row->field12 = 1;

        len = (int8_t)*q++;
        if (len) {
            strcpy(row->name, (const char *)q);
            cur   = q + len;
            took += len;
        } else {
            row->name[0] = 0;
            cur += 4;
        }

        took += 0x10;
    }

done:
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
int32_t __cdecl ParseScenarios(const uint8_t *at, int32_t remaining_in)
{
    int32_t        count;
    AM2_Scenario  *table;
    int32_t        i;

    memset((void *)&g_scenarioUnread, 0, 8);

    count = *(const int32_t *)at;
    at += 4;
    remaining_in -= 4;

    table = (AM2_Scenario *)orig_malloc((size_t)count * AM2_SCENARIO_BYTES);
    g_scenarios = table;
    g_scenarioCount = (uint16_t)count;

    for (i = 0; i < count; i++) {
        AM2_ScenHdr   hdr;
        AM2_Scenario *rec;
        int32_t  which;
        int32_t  part;

        hdr = *(const AM2_ScenHdr *)at;
        at += AM2_SCENARIO_HDR_BYTES;
        remaining_in -= (int32_t)AM2_SCENARIO_HDR_BYTES;

        if (strncmp(hdr.tag, "Scenario",
                    AM2_SCENARIO_TAG_BYTES) != 0)
            return 0;


        {
            uint8_t digit = (uint8_t)hdr.digit;

            which = !digit ? 0 : (int32_t)(int8_t)digit - '1';

        }

        rec = &table[which];
        rec->hdr = hdr;

        for (part = 0; part < (int32_t)AM2_SCENARIO_PARTS; part++) {
            int32_t took = ParseScenarioPart(&rec->parts[part], at);

            at += took;
            remaining_in -= took;
        }
    }

    /* Kept, and said out loud: GCC's own "set but not used" on this parameter
     * is the same finding the disassembly gives, arrived at independently. */
    (void)remaining_in;
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
    /* The table pointer is re-read after every free, as the original does. */
#define PART_ROWS(i, k) (g_scenarios[i].parts[k].rows)
    int32_t i, k;

    if (g_scenarios) {
        for (i = 0; i < g_scenarioCount; i++)
            for (k = 0; k < (int32_t)AM2_SCENARIO_PARTS; k++)
                if (PART_ROWS(i, k))
                    am2_free(PART_ROWS(i, k));
        am2_free(g_scenarios);
    }

    /* Both are cleared with or without a table to free, as one eight-byte
     * memset over the header (ADDR_SCENARIO_UNREAD, the count, the table). */
    memset((void *)&g_scenarioUnread, 0, 8);
#undef PART_ROWS
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
void __cdecl AddLevelRecord(const AM2_Level *record)
{
    AM2_Level *table = g_levelTable;
    int32_t  cap;
    int32_t  count;

    if (!table) {
        table = (AM2_Level *)am2_malloc((size_t)AM2_LEVEL_TABLE_FIRST
                                        * AM2_LEVEL_RECORD_SIZE);
        cap   = AM2_LEVEL_TABLE_FIRST;
        g_levelTable    = table;
        g_levelTableCap = cap;
    } else {
        cap = g_levelTableCap;
    }

    count = g_levelCount;

    if (count >= cap) {
        cap += AM2_LEVEL_TABLE_GROW;
        g_levelTableCap = cap;

        table = (AM2_Level *)am2_realloc(table,
                                         (size_t)cap * AM2_LEVEL_RECORD_SIZE);
        g_levelTable = table;

        count = g_levelCount;
    }

    /* The count is read again for the copy and incremented in place. */
    memcpy(&table[g_levelCount],
           record, AM2_LEVEL_RECORD_SIZE);

    g_levelCount++;
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
void __cdecl NameRecAddMap(AM2_NameRec *rec, const char *entry)
{
    AM2_NameRec *r = rec;

    if (!r->maps) {
        r->maps = (char (*)[0x40])
            am2_malloc((size_t)AM2_LEVEL_TABLE_FIRST * AM2_NAMEREC_FIELD);
        r->cap = AM2_LEVEL_TABLE_FIRST;
    }

    if (r->count
        >= r->cap) {
        int32_t cap = r->cap
                      + AM2_LEVEL_TABLE_GROW;

        r->cap = cap;
        r->maps = (char (*)[0x40])
            am2_realloc(r->maps,
                        (size_t)cap * AM2_NAMEREC_FIELD);
    }

    memcpy(r->maps[r->count],
           entry, AM2_NAMEREC_FIELD);

    r->count += 1;
}

/* 0x0043EAC0. One `rules` line: three tokens into a fresh name record, the
 * third title-cased, and the record appended to the table ScriptListFind
 * searches. */
int32_t __cdecl DefRulesLine(int32_t cmd, char *line)
{
    AM2_NameRec rec;
    char   *tok;

    (void)cmd;
    memset(&rec, 0, sizeof rec);

    tok = am2_strtok(line, kMapSep);
    if (!tok)
        return 2;
    strcpy(rec.name, tok);

    tok = am2_strtok((char *)0, kMapSep);
    if (!tok)
        return 3;
    strcpy(rec.name2, tok);

    tok = am2_strtok((char *)0, kMapSep);
    if (!tok)
        return 4;
    strcpy(rec.name3, tok);

    TitleCaseName(rec.name3);
    AddNameRecord(&rec);
    return 0;
}

/* 0x0043EBD0. One `rulemap` line: a list name and a map name, the second
 * appended to the list the first finds. */
int32_t __cdecl DefRuleMapLine(int32_t cmd, char *line)
{
    char  entry[AM2_NAMEREC_FIELD];
    char *tok;
    AM2_NameRec *rec;

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
void __cdecl AddNameRecord(const AM2_NameRec *record)
{
    AM2_NameRec *table = g_nameTableBase;
    int32_t  cap;
    int32_t  count;

    if (!table) {
        table = (AM2_NameRec *)am2_malloc((size_t)AM2_LEVEL_TABLE_FIRST
                                          * AM2_NAME_RECORD_SIZE);
        cap   = AM2_LEVEL_TABLE_FIRST;
        g_nameTableBase = table;
        g_nameTableCap   = cap;
    } else {
        cap = g_nameTableCap;
    }

    count = g_nameTableCount;

    if (count >= cap) {
        cap += AM2_LEVEL_TABLE_GROW;
        g_nameTableCap = cap;

        table = (AM2_NameRec *)am2_realloc(table,
                                           (size_t)cap * AM2_NAME_RECORD_SIZE);
        g_nameTableBase = table;

        count = g_nameTableCount;
    }

    /* The count is read again for the copy and incremented in place. */
    memcpy(&table[g_nameTableCount],
           record, AM2_NAME_RECORD_SIZE);

    g_nameTableCount++;
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
    uint32_t  formLen;                   /* its own slot in the frame */
    uint32_t  len;
    am2_FILE *fp;

    SetGameDir(g_tilesetPath);

    am2_sprintf(name, "%s.amm", map);

    fp = orig_fopen(name, "rb");
    if (!fp)
        return sum;

    orig_fread(&tag, 4, 1, fp);
    if (tag == AM2_IFF_FORM) {
        orig_fread(&formLen, 4, 1, fp);      /* the FORM length, unread */
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

/* FreeSpriteRegistry is reconstructed in win32/sprite.cpp; declared here (not
 * included) for the same flat-side reason, and its `void(void)` names nothing
 * platform. FreeAaiTables lives in defparse.cpp (flat, C++ linkage). Both are
 * FreeMapLayers's own tail in the original -- see below. */
void __cdecl FreeSpriteRegistry(void);
void __cdecl FreeAaiTables(void);

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
 * TEN POINTERS THEN FOLLOW, then the four TILE_REVEAL_GRIDS as a loop, then
 * TILE_COVER -- each guarded against null, freed and cleared, and the clear is
 * what makes calling this twice safe, which is exactly what the loader relies
 * on. The list was extracted from the disassembly by script; the reveal-grid
 * loop (0x0042D51C, esi from ADDR_TILE_REVEAL_GRIDS up to ADDR_TILE_COVER, four
 * `uint8_t *` freed) was dropped in the first reading -- exactly the "a hand
 * copy skips one of near-identical blocks" hazard, found by an AM2_TRACE_HEAP
 * diff at the level teardown: the original freed four pointers here the port
 * did not, desynchronising the fixed heap from that point on. Order is the
 * original's, so TILE_COVER stays last. */
void __cdecl FreeMapLayers(void)
{
    int32_t i;

    FreeMapSurfaces();

    if (g_regions) {
        int32_t n;

        for (n = 0; n < g_regionStride; n++) {
            AM2_Region *r = &g_regions[n];

            if (r->nlinks > 0)
                am2_free(r->links);
        }

        am2_free(g_regions);
        g_regions = 0;
    }

    /* Each layer written out, as the image has them; only the reveal
     * grids are a loop. */
#define FREE_LAYER(a)                                   \
    if (*(void **)(uintptr_t)(a)) {                     \
        am2_free(*(void **)(uintptr_t)(a));             \
        *(void **)(uintptr_t)(a) = 0;                   \
    }
    FREE_LAYER(((uintptr_t)&g_regionNext))
    FREE_LAYER(((uintptr_t)&g_regionCost))
    FREE_LAYER(((uintptr_t)&g_mapTiles))
    FREE_LAYER(((uintptr_t)&g_tileAttrs))
    FREE_LAYER(((uintptr_t)&g_cellWeights))
    FREE_LAYER(((uintptr_t)&g_mapPadbitLayer))
    FREE_LAYER(((uintptr_t)&g_mapPadLayer))
    FREE_LAYER(((uintptr_t)&g_tileKind))
    FREE_LAYER(((uintptr_t)&g_regionOfCell))
    FREE_LAYER(((uintptr_t)&g_tileFlags))
#undef FREE_LAYER

    for (i = 0; i < AM2_REVEAL_ARMIES; i++) {
        if (g_tileRevealGrids[i]) {
            am2_free(g_tileRevealGrids[i]);
            g_tileRevealGrids[i] = 0;
        }
    }

    {
        void **p = (void * *)&g_tileCover;

        if (*p) {
            am2_free(*p);
            *p = 0;
        }
    }

    /* THE TAIL: the original ends by tearing down the object registry, both
     * map descriptors, the AAI tables and the sprite registry, in this order
     * (0x0042D550..0x0042D571). The first reading dropped all five, which left
     * those frees to happen later -- ItemsReset at the next LoadItems, the rest
     * at their own reset paths -- so the mission-section teardown freed in a
     * different ORDER than the original and the fixed heap drifted from that
     * point on, which an AM2_TRACE_HEAP diff showed as the port running
     * FreeAnimTable where the original was still inside ItemsReset. Every one
     * of the five is idempotent (each guards its pointer and clears it), so
     * running them here as well as at load is safe. */
    ItemsReset();
    MapDescFree(g_objMapDesc);
    MapDescFree(g_mapDesc);
    FreeAaiTables();
    FreeSpriteRegistry();
}



/* ---- LoadMap ----------------------------------------------------------- */
/* PreloadSpriteByKey is reconstructed in win32/sprite.cpp; declared here
 * rather than included, because map.cpp is flat and AM2_Sprite carries an
 * LPDIRECTDRAWSURFACE.  An incomplete type is enough -- only two int16 fields
 * of the returned sprite are read, and those are reached by offset. */
struct AM2_Sprite;
AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a,
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

#define orig_map_fopen   ((am2_map_fopen_fn)(uintptr_t)ADDR_FOPEN)
#define orig_map_fread   ((am2_map_fread_fn)(uintptr_t)ADDR_FREAD)
#define orig_map_fseek   ((am2_map_fseek_fn)(uintptr_t)ADDR_FSEEK)
#define orig_map_fclose  ((am2_map_fclose_fn)(uintptr_t)ADDR_FCLOSE)


/* w*h bytes straight into one global.  Seven of the arms are exactly this. */

/* MHDR's tail.  Three rectangles, the listener, both map descriptors and the
 * tile deltas, all derived from w and h.  ADDR_CAMERA_Y is field +4 of
 * ADDR_VISIBLE_TILES and is written by the same RectSet as the rest of it. */

/* One OLAY batch's records.  Each is described by the field table the FILE
 * just supplied, so the widths come from there and a field with no arm is
 * read for its stated width and dropped -- which is what RESV is.
 *
 * ELOW OVERRIDES ELEV AND OWNR.  When it is non-zero the record takes its
 * elevation from ELOW's high nibble and its owner from the low one, and the
 * two single-byte fields are not used at all.  Three fields, two
 * destinations, and a precedence that is invisible from any one arm. */

typedef int32_t (__cdecl *am2_kind_allowed_fn)(int32_t kind);
typedef void (__cdecl *am2_respawn_pool_fn)(int32_t seed);
/* BuildRespawnPool is reconstructed; maprow.h declares it. */

/* RespawnKindAllowed is reconstructed; maprow.h declares it. */

/* RandomRespawnKind is reconstructed; maprow.h declares it. Its typedef here
 * said `int32_t (int32_t *)` and was right about the return value -- which I
 * re-derived from a call site rather than reading it. */


/* Turn the parse records into objects.  NOT "CreateWeapon per record": each
 * one packs a sprite key, may have its kind SUBSTITUTED from the weighted
 * respawn pool, preloads its sprite and takes that sprite's own offset into
 * its position before anything is created. */

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
/* Allocate and zero a layer the file did not supply; the size is re-read
 * for each of the two uses, as the original does. */
#define DEFAULT_LAYER(layer)                              \
    if ((layer) == 0) {                                   \
        *(void **)&(layer) = am2_malloc(g_mapTilesW * g_mapTilesH); \
        memset((layer), 0, g_mapTilesW * g_mapTilesH);    \
    }

int32_t __cdecl LoadMap(const char *base, const char *folder)
{
    char      path[0x40];
    int32_t v;
    am2_FILE *f;
    int32_t   tag;
    uint32_t  size, formSize;
    int32_t   consumed, plane, i;
    int32_t   tlayDone = 0;
    AM2_MapRec *objs = 0;
    int32_t   objCount = 0, nfields;

    SetGameDir(folder);
    memset(g_mapBlock, 0, 0x1A0);
    strcpy(g_mapBlock, folder);

    sprintf(path, "%s.atl", base);
    if (LoadAtlFile(path) == 0) {
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
    consumed = 0xC;
    if (tag != AM2_IFF_MAP)
        goto bad;

    /* A size mismatch is FATAL: the chunk is skipped and then the whole
     * load fails. Written out at each site, as the image has it. */
#define MISMATCH()                                                          \
    do {                                                                    \
        am2_log("chunksize/targetsize different\n");                        \
        orig_map_fseek(f, (int32_t)size, 1);                                \
        goto bad;                                                           \
    } while (0)

    do {
        orig_map_fread(&tag, 4, 1, f);
        orig_map_fread(&size, 4, 1, f);
        consumed += 8;

        switch (tag) {
        /* Seven planes, one shape.  Only the global differs. */
        case AM2_IFF_MHDR: {
            int32_t hdr[3];

            if (size != 0xC) MISMATCH();
            orig_map_fread(hdr, 0xC, 1, f);
            consumed += 0xC;
            /* Both dimensions first, then the shift, as the image stores them. */
            g_mapTilesW      = hdr[0];
            g_mapTilesH      = hdr[1];
            (*(int32_t *)&g_mapRowShift)    = Log2Mask(hdr[0]) & 0xFF;
            g_mapExtentX     = g_mapTilesW << 4;
            g_mapExtentY     = g_mapTilesH << 4;
            g_mapExtentShift = Log2Mask(g_mapExtentX) & 0xFF;
            if ((*(int32_t *)&g_mapRowShift) < 1) {
                FreeMapLayers();
                goto bad;
            }
            /* Three rects by value, each with its own frame slot -- one
             * reused rect gives one slot, and the whole frame shifts. The
             * inset rect has four field names here, so it is written over
             * them as a whole. */
            {
                AM2_Rect r0 = RectMake(0, 0, g_mapExtentX, g_mapExtentY);
                g_mapBounds = r0;
            }
            {
                AM2_Rect r1 = RectMake(0x40, 0x40, g_mapExtentX - 0x40, g_mapExtentY - 0x40);
                g_mapInset = r1;
            }
            *(uint32_t *)&g_listenerPos = MakePoint(0x10, 0x10);
            g_listenerPosPrev = g_listenerPos;
            {
                AM2_Rect r2 = RectMake(1, 1, g_viewTilesW + 1, g_viewTilesH + 1);
                g_visibleTiles = r2;
            }

            MapDescInit(g_objMapDesc,
                        g_mapExtentX, g_mapExtentY);
            MapDescInit(g_mapDesc, g_mapExtentX, g_mapExtentY);
            BuildTileDeltas();
            break;
        }

        case AM2_IFF_BPAD:
            plane = g_mapTilesW * g_mapTilesH;
            if ((int32_t)size != plane) MISMATCH();
            g_mapPadbitLayer = (uint8_t *)am2_malloc(plane);
            orig_map_fread(g_mapPadbitLayer, plane, 1, f);
            consumed += plane;
            break;
        case AM2_IFF_MOVE:
            plane = g_mapTilesW * g_mapTilesH;
            if ((int32_t)size != plane) MISMATCH();
            *(uint8_t * *)&g_cellWeights = (uint8_t *)am2_malloc(plane);
            orig_map_fread(*(uint8_t * *)&g_cellWeights, plane, 1, f);
            consumed += plane;
            break;
        case AM2_IFF_NPAD:
            plane = g_mapTilesW * g_mapTilesH;
            if ((int32_t)size != plane) MISMATCH();
            g_mapPadLayer = (uint8_t *)am2_malloc(plane);
            orig_map_fread(g_mapPadLayer, plane, 1, f);
            consumed += plane;
            break;

        case AM2_IFF_OWNR:
            plane = g_mapTilesW * g_mapTilesH;
            if ((int32_t)size != plane) MISMATCH();
            g_tileKind = (uint8_t *)am2_malloc(plane);
            orig_map_fread(g_tileKind, plane, 1, f);
            consumed += plane;
            break;
        case AM2_IFF_TRIG:
            plane = g_mapTilesW * g_mapTilesH;
            /* A difference, not a compare: a number win, the image has one more
             * scratch temporary in the rotation here. */
            if (size - (uint32_t)plane) MISMATCH();
            g_tileFlags = (uint8_t *)am2_malloc(plane);
            orig_map_fread(g_tileFlags, plane, 1, f);
            consumed += plane;
            break;
        case AM2_IFF_REGN:
            plane = g_mapTilesW * g_mapTilesH;
            if ((int32_t)size != plane) MISMATCH();
            g_regionOfCell = (uint8_t *)am2_malloc(plane);
            orig_map_fread(g_regionOfCell, plane, 1, f);
            consumed += plane;
            break;
        case AM2_IFF_ELEV:
            plane = g_mapTilesW * g_mapTilesH;
            if ((int32_t)size != plane) MISMATCH();
            g_tileAttrs = (uint8_t *)am2_malloc(plane);
            orig_map_fread(g_tileAttrs, plane, 1, f);
            consumed += plane;
            break;

        /* Owns nothing: a scratch buffer the parser consumes and we free. */
        case AM2_IFF_SCEN: {
            uint8_t *tmp = (uint8_t *)am2_malloc((size_t)size);

            if (tmp == 0)
                goto skip;
            orig_map_fread(tmp, (size_t)size, 1, f);
            consumed += (int32_t)size;
            ParseScenarios(tmp, (int32_t)size);
            am2_free(tmp);
            break;
        }

        /* NOT a plane: a 0x14-byte header first, and the chunk is w*h*2 PLUS
         * that header.  Only the first TLAY in a file is taken. */
        case AM2_IFF_TLAY: {
            int32_t hdr[5], w, h, bytes;

            if (tlayDone > 0) {         /* only the first is taken */
                orig_map_fseek(f, (int32_t)size, 1);
                consumed += (int32_t)size;
                break;
            }
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
            /* Both refusals are FATAL and skip nothing, each with its own
             * line -- not the planes' "chunksize/targetsize different". */
            if (w != g_mapTilesW || h != g_mapTilesH) {
                am2_log("tile layer size different from map size\n");
                goto bad;
            }
            if ((int32_t)size != bytes + 0x14) {
                am2_log("mismatched chunk and target size\n");
                goto bad;
            }
            g_mapTiles = (uint8_t *)am2_malloc((uint32_t)bytes);
            orig_map_fread(g_mapTiles, (uint32_t)bytes, 1, f);
            consumed += bytes;
            tlayDone++;
            break;
        }

        /* NOT a plane either.  The on-disk record is 0x10 and the in-memory
         * one 0x1C, so they are read one at a time; the array is a TEMPORARY
         * that the tail turns into objects and frees. */
        case AM2_IFF_OLAY: {
            int32_t first = 0, n, recCount, item, junk1, junk2;

            /* The record count goes through v, and `first` stays 0 when a
             * chunk declares none -- the indices below are then absolute. */
            orig_map_fread(&v, 4, 1, f);
            consumed += 4;
            if (v > 0) {
                first = objCount;
                objCount += v;
                objs = (AM2_MapRec *)am2_realloc(objs, (size_t)objCount * 0x1C);
                memset(&objs[first], 0, (size_t)v * 0x1C);
                for (n = first; n < objCount; n++) {
                    orig_map_fread(&objs[n], 0x10, 1, f);
                    consumed += 0x10;
                }
            }

            orig_map_fread(&junk1, 4, 1, f);
            orig_map_fread(&junk2, 4, 1, f);
            orig_map_fread(&nfields, 4, 1, f);
            consumed += 0xC;
            i = 0;
            for (;;) {
                if (i >= nfields)
                    break;
                orig_map_fread(&g_mapFieldDescs[i].tag, 4, 1, f);
                orig_map_fread(&g_mapFieldDescs[i].size, 4, 1, f);
                consumed += 8;
                i++;
            }

            orig_map_fread(&recCount, 4, 1, f);
            consumed += 4;
            for (item = 0; item < recCount; item++) {
                int32_t index = -1;
                int32_t move = 0, elow = 0, ownr = 0, elev = 0, trig = 0, numb = 0;
                char    scri[0x40];     /* 0x40 in the image's frame; the longest
                                         * SCRI in the shipped maps is 19 */

                scri[0] = 0;

                /* v is not cleared: a field narrower than four bytes keeps
                 * the upper bytes of the one before, as the image has it. */
                for (i = 0; i < nfields; i++) {
                    orig_map_fread(&v, g_mapFieldDescs[i].size, 1, f);
                    consumed += (int32_t)(g_mapFieldDescs)[i].size;

                    switch ((int32_t)(g_mapFieldDescs)[i].tag) {
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
                            consumed += v;
                        }
                        break;
                    default:
                        break;          /* read for its declared width and dropped */
                    }
                }

                if (index >= 0) {
                    AM2_MapRec *r = &objs[index];

                    r->move = (uint8_t)move;
                    if (elow != 0) {
                        r->elev = (uint8_t)((elow >> 4) & 0xF);
                        r->owner = (uint8_t)(elow & 0xF);
                    } else {
                        r->elev = (uint8_t)elev;
                        r->owner = (uint8_t)ownr;
                    }
                    r->trig = (uint8_t)trig;
                    r->field11 = 0;
                    objs[index].numb = (int8_t)numb;   /* through the array element, as the image does: r is &objs[index] */

                    if (strlen(scri) != 0) {
                        r->script =
                            (char *)am2_malloc(strlen(scri) + 1);
                        strcpy(r->script, scri);
                    }
                } else {
                    am2_log("Missing index attribute for item #%d!\n", item);
                }
            }
            break;
        }

        default:
            /* A chunk this loader does not want: skipped, and the walk
             * carries on. */
        skip:
            orig_map_fseek(f, (int32_t)size, 1);
            consumed += (int32_t)size;
            break;
        }

    } while (consumed < (int32_t)formSize);

    /* Nine layers every consumer may assume exists. */
    DEFAULT_LAYER(g_cellWeights);
    DEFAULT_LAYER(g_tileAttrs);
    DEFAULT_LAYER(g_regionOfCell);
    DEFAULT_LAYER(g_mapPadbitLayer);
    DEFAULT_LAYER(g_mapPadLayer);
    DEFAULT_LAYER(g_tileKind);
    DEFAULT_LAYER(g_tileFlags);
    /* AND A LOOP, WHICH IS WHY ENUMERATING THE EXPLICIT BLOCKS MISSED THESE.
     * The original walks every pointer from ADDR_TILE_REVEAL_GRIDS up to
     * ADDR_TILE_COVER four bytes at a time -- the four per-army reveal grids
     * -- and fills each null one.  Leaving them null does not fault: it makes
     * SettlePointInRegion spiral for a passable tile it can never find. */
    {
        /* A number win: the pointer walk is spelled as a sum of the table and a difference of two reads
         * of one word (which is zero), so c2 loses sight of which object the stores through `slot` land
         * in and keeps them ordered before the reloads of the map size, as the image does. */
        uint8_t **slot = (uint8_t **)((uintptr_t)g_tileRevealGrids
                                      - ((uintptr_t)g_mapTilesW - (uintptr_t)g_mapTilesW));

        while ((intptr_t)slot < (intptr_t)&g_tileCover) {
            /* DEFAULT_LAYER written out through the walking pointer. */
            if (*slot == 0) {
                *slot = (uint8_t *)am2_malloc(g_mapTilesW * g_mapTilesH);
                memset(*slot, 0, g_mapTilesW * g_mapTilesH);
            }
            slot++;
        }
    }
    DEFAULT_LAYER(g_tileCover);

    /* FOUR THINGS THE DEFAULTING IS FOLLOWED BY, and leaving them out is what
     * cost this function four attempts.  ADDR_LOAD_PENDING is a FLAG tested
     * here, not a layer to allocate -- it shares its code shape with an
     * allocate-if-null block and was swallowed by the scan that found those. */
    SealMapEdges();
    RebuildTileCover();
    BuildAaiBuiltins();

    if (g_loadPending == 0) {
        AM2_Object *cam = CreateItem((char *)"camera", 4,
                               g_aaiKey980000,
                               g_zeroPoint,
                               0, 1, 0);

        g_evtId15Uid =
            (int32_t)cam->uid;
    }

    BuildRespawnPool(g_gameSeed);

    /* Only build the map's objects on a FRESH start. On a load the objects
     * come from the save (LoadItems), so the original skips this: after
     * BuildRespawnPool at 0x0042D0B2 it tests ADDR_LOAD_PENDING and `jne`s past
     * the build loop to the free-temp tail (0x0042D325). We were calling it
     * unconditionally, which rebuilt the map's objects on every load -- ~1250
     * extra alloc/free churn that desynced the fixed heap globally (all objects
     * ended up at different addresses between builds), and downstream a roach's
     * sight scan then iterated in a different order and diverged. */
    if (objCount > 0 && objs != 0) {
    if (g_loadPending == 0)
        for (i = 0; i < objCount; i++) {
            AM2_MapRec *r     = &objs[i];
            int32_t   army;
            int32_t   flags = 0;
            void     *obj = 0;
            AM2_Point at;

            if (r->trig & 0x20)
                flags = 4;
            /* One impossible pair on g_gameOverFlags: a number win (it raises its use count). */
            if ((r->trig & 0x08)
                || (g_gameOverFlags == 1000 && g_gameOverFlags == 1001))
                flags |= 0x810;
            army  = r->owner == 0 ? 4 : (int32_t)r->owner - 1;

            /* The point is read from the record after each arm's sprite
             * lookup, not before it. */
            if (r->type != 0x19) {
                /* An ITEM. */
                int32_t key = PackKey(r->type + 0x14, r->kind, 0);
                AM2_Sprite *spr = PreloadSpriteByKey(key, 0x1000, 1);

                at.x = r->x16;
                at.y = r->y16;
                if (spr != 0) {
                    at.x = (int16_t)(at.x + spr->hotX);
                    at.y = (int16_t)(at.y + spr->hotY);
                }
                obj = CreateItem(r->script, army,
                                 PackKey(r->type + 0x14, r->kind, 0),
                                 at, flags, 1, 0);
                /* No null test of its own: ObjIsWatchedKind answers 0 for one. */
                if (ObjIsWatchedKind((const AM2_Object *)obj)
                    && (int32_t)g_defaultOwner != army)
                    ObjConceal((AM2_Object *)obj, 1);
                /* NOT `continue`: the item arm falls into the SHARED HEIGHT TAIL
                 * below, exactly as the original does (0x0042D1C3 jmps to the
                 * ApplyHeightItem at 0x0042D30B that the weapon arm reaches by
                 * falling through). Skipping it left every map item's height at 0
                 * instead of the terrain's, which halved its depth layer and drew
                 * overlapping ground sprites in the wrong order -- the bottom-edge
                 * divergence AM2_TRACE_HEIGHT traced to this call. */
            } else {

            /* A WEAPON, and the branch where a kind of 0x64 or more is either
             * substituted from the respawn pool or reduced by 0x64. */
                /* QUANTITY IS THE RECORD'S NUMB FIELD, read here, before
                 * anything else in the arm -- and on the respawn path
                 * RandomRespawnKind writes the substitute's default count over
                 * it through the same local. (A prior reconstruction passed
                 * zero, which CreateWeapon turns into 3 rounds, and a crate
                 * with NUMB=30 came out under-stocked.) */
                int32_t  numb = r->numb;
                int32_t  aai, key = PackKey(r->type + 0x14, r->kind, 0);
                int32_t  code = r->kind;
                AM2_Sprite    *spr;
                AM2_Point settled;

                if (code >= 0x64) {
                    if (g_gameOverFlags & 2) {
                        code = RandomRespawnKind(&numb);
                        aai  = EnsureSpriteAaiRecord(0x2D, code, 0);
                        flags |= 0x10000;
                        key = (int32_t)g_aaiRecords[aai]->key;
                        goto haveRecord;
                    }
                    if (code == 0x64)
                        continue;
                    code -= 0x64;
                    key = PackKey(r->type + 0x14, code, 0);
                }
                if (RespawnKindAllowed(code) == 0)
                    continue;
                aai = EnsureSpriteAaiRecord(r->type + 0x14, code, 0);

            haveRecord:
                if (aai < 0)
                    continue;

                spr = PreloadSpriteByKey(key, 0x1000, 1);
                at.x = r->x16;
                at.y = r->y16;
                if (spr != 0) {
                    at.x = (int16_t)(at.x + spr->hotX);
                    at.y = (int16_t)(at.y + spr->hotY);
                }

                /* BOTH POINTS: SettlePointInRegion moves a copy, and the
                 * weapon is placed at the point from BEFORE it ran. And the
                 * army is the literal 4, not the record's owner. */
                settled = at;
                flags |= g_aaiRecords[aai]->flags;
                /* Through map.h's int32_t view: the MSVC definition above
                 * returns a word, and its own callers here would widen it. */
                SettlePointInRegion(((int32_t (__cdecl *)(AM2_Point))(void *)TileOfPoint)(at),
                                    &settled);
                obj = CreateWeapon(r->script, 4,
                                      aai, at, flags,
                                      numb, 1, 0);
            }

            /* THE SHARED HEIGHT TAIL (0x0042D2ED), reached by both arms: the map
             * record's own height is the tile's terrain attribute plus the
             * record's elevation byte, applied to whichever object was made. */
            if (obj != 0) {
                const uint8_t *attrs =
                    g_tileAttrs;
                int32_t tile = ((AM2_Object *)obj)->tile;

                ApplyHeightItem((AM2_Object *)obj, (int8_t)attrs[tile] + r->elev);
            }
        }

    am2_log("freeing temporary map load data...\n");
    for (i = 0; i < objCount; i++) {
        char **owned = &objs[i].script;

        if (*owned != 0)
            am2_free(*owned);
    }
    am2_free(objs);
    }

    orig_map_fclose(f);
    SetGameDir(g_mapBlock);
    return 1;

#undef MISMATCH
bad:
    am2_log("Error in loadmap()\n");
    orig_map_fclose(f);
    SetGameDir(g_mapBlock);
    return 0;
#undef DEFAULT_LAYER
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
