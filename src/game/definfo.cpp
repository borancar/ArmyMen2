/* definfo.cpp -- see definfo.h. */
#include <stdint.h>
#include <string.h>

#include "definfo.h"
#include "crt.h"      /* am2_strtok -- the shared tokeniser cursor */
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "defparse.h"  /* DefFreeTables, DefFreeTrooperRecs -- reconstructed */
#include "gameproc.h"  /* DefFinish -- reconstructed */
#include "gamedir.h"   /* SetGameDir -- ADDR_SET_DATA_DIR is it */

/* The seams LoadDefTables needs. All are original and reached by address;
 * none is on the patch list. There were five -- the fifth was
 * ADDR_RANK_DEF_FIND, now DefFindTrooperRec in defparse.cpp and called by
 * name. */
typedef void    (__cdecl *AM2_VoidFn)(void);
/* AM2_DefFindFn is definfo.h's now -- item.cpp's MedkitHeal needs the same
 * lookup, and a second private typedef for one function is where a signature
 * goes wrong unseen. */

#define orig_free_list_662024  ((AM2_VoidFn)AM2_IMAGE(ADDR_FREE_LIST_662024))
#define orig_free_list_662928  ((AM2_VoidFn)AM2_IMAGE(ADDR_FREE_LIST_662928))
#define orig_missile_def_find  ((AM2_DefFindFn)AM2_IMAGE(ADDR_MISSILE_DEF_FIND))
#define orig_log_noargs        ((AM2_VoidFn)(uintptr_t)ADDR_LOG)

/* strtol reached through the game's own thunk. Unlike strtok, nothing here
 * depends on shared state -- this is for consistency with the rest of the def
 * parsing, which must use the game's tokeniser. */
typedef int32_t (__cdecl *AM2_StrtolFn)(const char *s, char **end, int32_t base);
typedef double (__cdecl *AM2_StrtodFn)(const char *s, char **end);
#define orig_strtol (*(AM2_StrtolFn)AM2_IMAGE(ADDR_CRT_STRTOL))
#define orig_strtod (*(AM2_StrtodFn)AM2_IMAGE(ADDR_CRT_STRTOD))

#define kDefKeywords ((const AM2_DefKeyword *)AM2_IMAGE(ADDR_DEF_NAME_TABLE))

typedef am2_FILE *(__cdecl *AM2_FopenFn)(const char *path, const char *mode);
typedef char *(__cdecl *AM2_FgetsFn)(char *buf, int32_t n, am2_FILE *fp);
typedef char *(__cdecl *AM2_StrlwrFn)(char *s);
#define orig_def_fopen  (*(AM2_FopenFn)AM2_IMAGE(ADDR_FOPEN))
#define orig_fgets      (*(AM2_FgetsFn)AM2_IMAGE(ADDR_CRT_FGETS))
#define orig_strlwr     (*(AM2_StrlwrFn)AM2_IMAGE(ADDR_CRT_STRLWR))
#define kSep            ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))
#define kFileMode       ((const char *)AM2_IMAGE(ADDR_STR_DEF_FILE_MODE))

/* 0x0041A250. Parse one token as a number.
 *
 * Base 0, so the .aai files may write 0x.. and 0.. as well as decimal. The
 * test for success is that strtol consumed SOMETHING -- end != tok -- not that
 * it consumed everything, so "12abc" parses as 12 and is accepted.
 *
 * Two failures, one quiet. A null token returns 0 without a word, which is how
 * a line simply running out of fields is handled; a token that is not a number
 * complains first. The value is stored either way: strtol's 0 goes into *out
 * before the check. */
int32_t __cdecl DefParseNumber(int32_t *out, const char *tok)
{
    char *end;

    if (tok == (const char *)0)
        return 0;

    *out = am2_strtol(tok, &end, 0);

    if (end == tok) {
        orig_log("Bad or missing number\n");
        return 0;
    }

    return 1;
}

/* 0x0041A290. DefParseNumber's float twin, sixty-four bytes further along.
 *
 * The same three-part shape: a null token returns 0 in silence, a token that
 * is not a number complains first, and success means strtod consumed
 * SOMETHING rather than everything.
 *
 * Two differences from the integer form, both reproduced. There is no base
 * argument, so 0x and 0 prefixes mean nothing here. And the value is stored
 * before the check either way -- the original's `fstp` writes *out and only
 * then compares the end pointer -- so a failed parse still leaves 0.0 behind
 * rather than the caller's previous value. */
int32_t __cdecl DefParseFloat(float *out, const char *tok)
{
    char *end;

    if (tok == (const char *)0)
        return 0;

    *out = (float)orig_strtod(tok, &end);

    if (end == tok) {
        orig_log("Bad or missing number\n");
        return 0;
    }

    return 1;
}

/* DefParseBoolean -- original 0x0041A2D0, three callers. The third of
 * DefParseNumber's family, and it sits between the integer form and
 * DefParseInfoFile in the image as well as here.
 *
 * TWELVE INLINED strcmp's AND NOTHING ELSE. Eight hundred bytes of the same
 * six-byte compare loop repeated twelve times, against TRUE/true/True/T/t/1
 * and then FALSE/false/False/F/f/0. VC6 inlines strcmp at /O2 and this is what
 * it costs; there is no table in the image to read the words out of, so they
 * are written out here as the two arrays the original does not have.
 *
 * ITS SIGNATURE IS (out, text) AND THE CALL SITES READ BACKWARDS. Both push
 * the string LAST -- `push eax` straight from strtok, then the out pointer --
 * so the string is argument two, and the body agrees: [esp+0x14] before any
 * further push is the text, [esp+0x10] the destination. Same order as
 * DefParseNumber and DefParseFloat above, which is the check that matters.
 *
 * AND THE `add esp, 0x10` AT EACH CALL SITE IS NOT FOUR ARGUMENTS. It cleans
 * this call's two AND the strtok above it, which is the same merged cleanup
 * PlacementScreenClick's by-value RECT turned on. Counting the cleanup alone
 * gives a four-argument function with two arguments never read, which is what
 * a first pass here produced.
 *
 * TWO FAILURES, ONE QUIET -- the family's shape exactly. A null token returns
 * 0 without a word and leaves *out alone, which is how a line simply running
 * out of fields is handled; a token that is not a boolean complains and stores
 * 0. The two siblings above store their parse result either way and this one
 * has nothing to store, so the arms differ in form and not in effect. */
int32_t __cdecl DefParseBoolean(int32_t *out, const char *tok)
{
    static const uint32_t kTrue[] = {
        ADDR_STR_TRUE, ADDR_STR_BOOL_TRUE_LOW, ADDR_STR_BOOL_TRUE_CAP,
        ADDR_STR_BOOL_UT, ADDR_STR_BOOL_LT, ADDR_STR_BOOL_1
    };
    static const uint32_t kFalse[] = {
        ADDR_STR_FALSE, ADDR_STR_BOOL_FALSE_LOW, ADDR_STR_BOOL_FALSE_CAP,
        ADDR_STR_BOOL_UF, ADDR_STR_BOOL_LF, ADDR_STR_BOOL_0
    };
    uint32_t i;

    if (tok == (const char *)0)
        return 0;

    for (i = 0; i < sizeof kTrue / sizeof kTrue[0]; i++)
        if (strcmp(tok, (const char *)AM2_IMAGE(kTrue[i])) == 0) {
            *out = 1;
            return 1;
        }

    for (i = 0; i < sizeof kFalse / sizeof kFalse[0]; i++)
        if (strcmp(tok, (const char *)AM2_IMAGE(kFalse[i])) == 0) {
            *out = 0;
            return 1;
        }

    orig_log((const char *)AM2_IMAGE(ADDR_STR_BAD_BOOLEAN));
    *out = 0;
    return 0;
}

/* 0x0041A640. Find a keyword's index in the .aai vocabulary.
 *
 * Walks entries until one has an empty name; there is no count. The original
 * inlines a strcmp that yields -1/0/1 through `sbb`, but only its zero is ever
 * tested, so a plain equality comparison is exactly the same function.
 *
 * The index it returns IS the command id -- 0x4F..0x5E reach DefObjLine and
 * 0x5F reaches DefLinkParse through the handler in the same row. */
int32_t __cdecl DefFindKeyword(const char *name)
{
    const AM2_DefKeyword *e = kDefKeywords;
    int32_t               i = 0;

    while (e->name[0] != '\0') {
        if (strcmp(name, e->name) == 0)
            return i;
        e++;
        i++;
    }

    return -1;
}

/* 0x0041A6B0. Read an already-open .aai file and dispatch each line.
 *
 * Two buffers of 320 bytes: the line as read, kept only so an error message
 * can quote it, and a lowercased working copy that strtok is allowed to cut
 * up. The vocabulary is lower case, which is why the copy exists.
 *
 * `rest` is the character after the token's terminator -- strtok has already
 * written a NUL over the delimiter -- so a handler receives the remainder of
 * the LOWERCASED line and re-enters strtok with it.
 *
 * The handler is given the entry's VALUE, not the index the lookup returned.
 * See orig.h: those agree for every keyword that has a handler, and do not in
 * general.
 *
 * The line counter is incremented before the blank test, so it counts every
 * line read rather than every line dispatched -- which is what makes the two
 * error messages quote a number a person can find in the file. And the second
 * of them prints the handler's return as "token #%d", which is the far side of
 * DefObjLine's and DefLinkParse's ordinal failure codes being FIELD positions.
 *
 * Every exit closes the file, including the two failures. */
int32_t __cdecl DefDispatchFile(am2_FILE *fp)
{
    char    raw[AM2_DEF_LINE_MAX];
    char    work[AM2_DEF_LINE_MAX];
    int32_t lineno = 0;
    int32_t rc     = 0;

    if (fp == (am2_FILE *)0)
        return 0;

    while (orig_fgets(raw, AM2_DEF_LINE_MAX, fp) != (char *)0) {
        const AM2_DefKeyword *e;
        char                 *tok;
        char                 *rest;
        int32_t               idx;
        int32_t (__cdecl *handler)(int32_t, char *);

        strcpy(work, raw);
        orig_strlwr(work);
        lineno++;

        if (work[0] == '\0')
            continue;

        tok = am2_strtok(work, kSep);
        if (tok == (char *)0 || tok[0] == '\0' || tok[0] == '#')
            continue;

        rest = tok + strlen(tok) + 1;

        idx = DefFindKeyword(tok);
        if (idx < 0) {
            orig_log("Invalid Command: line %d token %s \n%s\n",
                     lineno, tok, raw);
            orig_fclose(fp);
            return 0;
        }

        e       = &kDefKeywords[idx];
        handler = (int32_t (__cdecl *)(int32_t, char *))e->handler;

        if (handler != (int32_t (__cdecl *)(int32_t, char *))0)
            rc = handler(e->value, rest);

        if (rc != 0) {
            orig_log("Invalid table entry: line %d, token #%d\n%s\n",
                     lineno, rc, raw);
            orig_fclose(fp);
            return 0;
        }
    }

    orig_fclose(fp);
    return 1;
}

/* 0x0041A5F0. Open one .aai file and hand it to the dispatcher. Ten callers.
 *
 * It does NOT close the file -- the dispatcher owns it from here, on every
 * path. Mode "rt", so the CRT strips carriage returns and fgets sees plain
 * lines. */
int32_t __cdecl DefParseInfoFile(const char *path)
{
    am2_FILE *fp;

    if (path == (const char *)0 || path[0] == '\0') {
        orig_log("Missing Filename (filename NULL or empty)\n");
        return 0;
    }

    fp = orig_def_fopen(path, kFileMode);
    if (fp == (am2_FILE *)0) {
        orig_log("Failed to open %s for reading in DefParseInfoFile\n", path);
        return 0;
    }

    return DefDispatchFile(fp);
}

/* 0x00403400, one caller -- the level load, which also builds the HUD. It
 * names itself through the five files it parses and its "Couldn't parse %s!".
 *
 * Six steps: free every definition table, SetDataDir("aai") and parse the
 * five files, then SetDataDir(ADDR_MAP_FOLDER) and parse Object.aai AGAIN --
 * a tileset directory may override the global object definitions -- then
 * DefFinish, then rebuild the missile definitions and the rank records.
 *
 * The bare Log() with no arguments is the original's, the same idiom frame.cpp
 * reproduces twice. A failure to parse is logged and otherwise ignored: every
 * one of the six is best-effort.
 *
 * BOTH REBUILDS PERMUTE, and that is what a memcpy would silently destroy.
 * Each destination entry is cleared immediately before it is filled rather
 * than the table being cleared once, so an id the bsearch cannot find reads as
 * zeros instead of as the previous level's data. */
void __cdecl LoadDefTables(void)
{
    uint8_t *dst;
    int32_t  id;

    DefFreeTables();
    orig_free_list_662024();
    orig_free_list_662928();
    DefFreeTrooperRecs();
    orig_log_noargs();

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_AAI_DIR));

    if (!DefParseInfoFile((const char *)AM2_IMAGE(ADDR_STR_DEF_TROOP_AAI)))
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE),
                 AM2_IMAGE(ADDR_STR_DEF_TROOP_AAI));
    if (!DefParseInfoFile((const char *)AM2_IMAGE(ADDR_STR_DEF_WEAPON_AAI)))
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE),
                 AM2_IMAGE(ADDR_STR_DEF_WEAPON_AAI));
    if (!DefParseInfoFile((const char *)AM2_IMAGE(ADDR_STR_VEHICLE_AAI)))
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE),
                 AM2_IMAGE(ADDR_STR_VEHICLE_AAI));
    if (!DefParseInfoFile((const char *)AM2_IMAGE(ADDR_STR_DEF_OBJECT_AAI)))
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE),
                 AM2_IMAGE(ADDR_STR_DEF_OBJECT_AAI));
    if (!DefParseInfoFile((const char *)AM2_IMAGE(ADDR_STR_DEF_GAME_AAI)))
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE),
                 AM2_IMAGE(ADDR_STR_DEF_GAME_AAI));

    /* The override pass. */
    SetGameDir((const char *)AM2_IMAGE(ADDR_MAP_FOLDER));
    if (!DefParseInfoFile((const char *)AM2_IMAGE(ADDR_STR_DEF_OBJECT_AAI)))
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE),
                 AM2_IMAGE(ADDR_STR_DEF_OBJECT_AAI));

    DefFinish();

    /* The missile definitions. Thirteen dwords per entry, five of them moved:
     * 0x10 and 0x14 swap, and 0x1C -> 0x24 -> 0x20 -> 0x1C. */
    dst = (uint8_t *)AM2_IMAGE(ADDR_MISSILE_DEFS);
    for (id = 0; id < AM2_MISSILE_DEF_COUNT;
         id++, dst += AM2_MISSILE_DEF_BYTES) {
        const uint8_t *src;

        memset(dst, 0, AM2_MISSILE_DEF_BYTES);
        src = (const uint8_t *)orig_missile_def_find(id);
        if (src == 0)
            continue;

        *(uint32_t *)(dst + 0x00) = *(const uint32_t *)(src + 0x00);
        *(uint32_t *)(dst + 0x04) = *(const uint32_t *)(src + 0x04);
        *(uint32_t *)(dst + 0x08) = *(const uint32_t *)(src + 0x08);
        *(uint32_t *)(dst + 0x0C) = *(const uint32_t *)(src + 0x0C);
        *(uint32_t *)(dst + 0x10) = *(const uint32_t *)(src + 0x14);
        *(uint32_t *)(dst + 0x14) = *(const uint32_t *)(src + 0x10);
        *(uint32_t *)(dst + 0x18) = *(const uint32_t *)(src + 0x18);
        *(uint32_t *)(dst + 0x1C) = *(const uint32_t *)(src + 0x20);
        *(uint32_t *)(dst + 0x20) = *(const uint32_t *)(src + 0x24);
        *(uint32_t *)(dst + 0x24) = *(const uint32_t *)(src + 0x1C);
        *(uint32_t *)(dst + 0x28) = *(const uint32_t *)(src + 0x28);
        *(uint32_t *)(dst + 0x2C) = *(const uint32_t *)(src + 0x2C);
        *(uint32_t *)(dst + 0x30) = *(const uint32_t *)(src + 0x30);
    }

    /* The rank records. A projection of a 0x20-byte parsed record onto a
     * 28-byte runtime one in which nothing stays in place. The original walks
     * a pointer four bytes INTO each record and writes the first field through
     * [ptr - 4], which is why ADDR_RANK_RECORDS is where it is. Written here
     * against the record base, which is the same addresses. */
    for (id = 0; id < AM2_RANK_COUNT; id++) {
        uint8_t       *rec = (uint8_t *)AM2_IMAGE(ADDR_RANK_RECORDS)
                             + (uint32_t)id * RANK_REC_BYTES;
        const uint8_t *src = (const uint8_t *)DefFindTrooperRec(id);
        float          pct;

        if (src == 0)
            continue;

        *(int32_t *)(rec + RANK_REC_OFF_SIGHT_RANGE) =
            *(const int32_t *)(src + RANK_SRC_OFF_SIGHT_RANGE);
        *(int32_t *)(rec + RANK_REC_OFF_FIELD_04) =
            *(const int32_t *)(src + RANK_SRC_OFF_FIELD_04);
        *(int32_t *)(rec + RANK_REC_OFF_FIELD_08) =
            *(const int32_t *)(src + RANK_SRC_OFF_FIELD_08);

        /* An integer percentage becomes a rate, with a floor of 1.0 -- and
         * this is the record's only float, which is what confirms the whole
         * permutation. */
        pct = (float)((long double)*(const int32_t *)(src + RANK_SRC_OFF_FIRE_PCT)
                      * (long double)*(const float *)AM2_IMAGE(ADDR_F_ONE_HUNDREDTH));
        if (pct < *(const float *)AM2_IMAGE(ADDR_F_ONE))
            pct = 1.0f;
        *(float *)(rec + RANK_REC_OFF_FIRE_SCALE) = pct;

        *(int32_t *)(rec + RANK_REC_OFF_THRESHOLD) =
            *(const int32_t *)(src + RANK_SRC_OFF_THRESHOLD);
        *(int32_t *)(rec + RANK_REC_OFF_MAX_HEALTH) =
            *(const int32_t *)(src + RANK_SRC_OFF_MAX_HEALTH);
        *(int32_t *)(rec + RANK_REC_OFF_XP) =
            *(const int32_t *)(src + RANK_SRC_OFF_XP);
    }
}

int definfo_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_LOAD_DEF_TABLES, (const void *)LoadDefTables,
                        "LoadDefTables", 1);
    rc |= patch_replace(ADDR_DEF_PARSE_BOOLEAN,
                        (const void *)DefParseBoolean, "DefParseBoolean", 3);
    rc |= patch_replace(ADDR_DEF_PARSE_NUMBER, (const void *)DefParseNumber,
                        "DefParseNumber", 2);
    rc |= patch_replace(ADDR_DEF_PARSE_FLOAT, (const void *)DefParseFloat,
                        "DefParseFloat", 1);
    rc |= patch_replace(ADDR_DEF_NAME_INDEX, (const void *)DefFindKeyword,
                        "DefFindKeyword", 1);
    rc |= patch_replace(ADDR_DEF_DISPATCH_LINE, (const void *)DefDispatchFile,
                        "DefDispatchFile", 1);
    rc |= patch_replace(ADDR_DEF_PARSE_INFO_FILE,
                        (const void *)DefParseInfoFile, "DefParseInfoFile", 1);
    return rc;
}
