/* definfo.cpp -- see definfo.h. */
#include <stdint.h>
#include <string.h>

#include "definfo.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* strtol reached through the game's own thunk. Unlike strtok, nothing here
 * depends on shared state -- this is for consistency with the rest of the def
 * parsing, which must use the game's tokeniser. */
typedef int32_t (__cdecl *AM2_StrtolFn)(const char *s, char **end, int32_t base);
#define orig_strtol (*(AM2_StrtolFn)AM2_IMAGE(ADDR_CRT_STRTOL))

#define kDefKeywords ((const AM2_DefKeyword *)AM2_IMAGE(ADDR_DEF_NAME_TABLE))

typedef am2_FILE *(__cdecl *AM2_FopenFn)(const char *path, const char *mode);
typedef char *(__cdecl *AM2_FgetsFn)(char *buf, int32_t n, am2_FILE *fp);
typedef char *(__cdecl *AM2_StrlwrFn)(char *s);
typedef char *(__cdecl *AM2_StrtokFn)(char *s, const char *sep);
#define orig_def_fopen  (*(AM2_FopenFn)AM2_IMAGE(ADDR_FOPEN))
#define orig_fgets      (*(AM2_FgetsFn)AM2_IMAGE(ADDR_CRT_FGETS))
#define orig_strlwr     (*(AM2_StrlwrFn)AM2_IMAGE(ADDR_CRT_STRLWR))
#define orig_strtok     (*(AM2_StrtokFn)AM2_IMAGE(ADDR_CRT_STRTOK))
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

    *out = orig_strtol(tok, &end, 0);

    if (end == tok) {
        orig_log("Bad or missing number\n");
        return 0;
    }

    return 1;
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

        tok = orig_strtok(work, kSep);
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

int definfo_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DEF_PARSE_NUMBER, (const void *)DefParseNumber,
                        "DefParseNumber", 2);
    rc |= patch_replace(ADDR_DEF_NAME_INDEX, (const void *)DefFindKeyword,
                        "DefFindKeyword", 1);
    rc |= patch_replace(ADDR_DEF_DISPATCH_LINE, (const void *)DefDispatchFile,
                        "DefDispatchFile", 1);
    rc |= patch_replace(ADDR_DEF_PARSE_INFO_FILE,
                        (const void *)DefParseInfoFile, "DefParseInfoFile", 1);
    return rc;
}
