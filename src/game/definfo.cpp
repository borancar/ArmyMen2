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

int definfo_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DEF_PARSE_NUMBER, (const void *)DefParseNumber,
                        "DefParseNumber", 2);
    rc |= patch_replace(ADDR_DEF_NAME_INDEX, (const void *)DefFindKeyword,
                        "DefFindKeyword", 1);
    return rc;
}
