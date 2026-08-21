/* defparse.cpp -- see defparse.h. */
#include <stdint.h>

#include "defparse.h"
#include "image.h"
#include "packkey.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* All four still original and reached by address. strtok in particular MUST be
 * the game's: DefObjParse below tokenises from the same state, and libc's
 * would be a second, unrelated cursor. */
typedef char *(__cdecl *AM2_StrtokFn)(char *s, const char *sep);
typedef int32_t (__cdecl *AM2_DefNameIndexFn)(const char *name);
typedef int32_t (__cdecl *AM2_DefObjParseFn)(int32_t nameindex);
typedef int32_t (__cdecl *AM2_DefParseNumberFn)(int32_t *out, const char *tok);
typedef int32_t (__cdecl *AM2_DefCountLinksFn)(int32_t parentkey);
typedef void (__cdecl *AM2_DefAddLinkFn)(const AM2_DefLink *link);

#define orig_strtok        (*(AM2_StrtokFn)AM2_IMAGE(ADDR_CRT_STRTOK))
#define orig_def_name_index \
    (*(AM2_DefNameIndexFn)AM2_IMAGE(ADDR_DEF_NAME_INDEX))
#define orig_def_obj_parse (*(AM2_DefObjParseFn)AM2_IMAGE(ADDR_DEF_OBJ_PARSE))
#define orig_def_number    (*(AM2_DefParseNumberFn)AM2_IMAGE(ADDR_DEF_PARSE_NUMBER))
#define orig_def_count_links \
    (*(AM2_DefCountLinksFn)AM2_IMAGE(ADDR_DEF_COUNT_LINKS))
#define orig_def_add_link  (*(AM2_DefAddLinkFn)AM2_IMAGE(ADDR_DEF_ADD_LINK))

#define kSep ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))

/* Read the next whitespace-delimited field. Only the first call passes the
 * line; the rest continue from strtok's own state. */
static int32_t NextType(char *line)
{
    return orig_def_obj_parse(orig_def_name_index(orig_strtok(line, kSep)));
}

static int32_t NextNumber(int32_t *out)
{
    return orig_def_number(out, orig_strtok((char *)0, kSep));
}

/* 0x004360C0. See defparse.h for the line's shape.
 *
 * Three things reproduced rather than corrected. The LAST field's parse result
 * is not checked -- every other one aborts the line, but a bad seventh field
 * is stored as whatever the parser left behind. The `a` and `b` fields are
 * parsed as int32 and stored as int16, so anything above 65535 is truncated
 * silently. And `siblings` is filled with a COUNT taken before this link is
 * added, so the first link for a parent stores 0.
 *
 * The failure codes are ordinal, 1..7 in field order, and none of them is
 * logged except the three that have their own string. */
int32_t __cdecl DefLinkParse(int32_t cmd, char *line)
{
    AM2_DefLink link;
    int32_t     parent, child;
    int32_t     n1, n2, a, b, c;

    if (cmd != AM2_DEF_CMD_LINK) {
        orig_log("DefLinkParse: 'LINK' command not found.\n");
        return 1;
    }

    parent = NextType(line);
    if (parent < 0) {
        orig_log("DefLinkParse: Parent object type not found.\n");
        return 2;
    }

    if (!NextNumber(&n1))
        return 3;

    child = NextType((char *)0);
    if (child < 0) {
        orig_log("DefLinkParse: Child object type not found.\n");
        return 4;
    }

    if (!NextNumber(&n2))
        return 5;

    if (!NextNumber(&a))
        return 6;

    if (!NextNumber(&b))
        return 7;

    c = 0;
    (void)NextNumber(&c);          /* the one result the original ignores */

    link.parent   = (int32_t)PackKey((uint32_t)parent, (uint32_t)n1, 0);
    link.child    = (int32_t)PackKey((uint32_t)child, (uint32_t)n2, 0);
    link.siblings = orig_def_count_links(link.parent);
    link.a        = (int16_t)a;
    link.b        = (int16_t)b;
    link.c        = c;

    orig_def_add_link(&link);
    return 0;
}

int defparse_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DEF_LINK_PARSE, (const void *)DefLinkParse,
                        "DefLinkParse", 1);
    return rc;
}
