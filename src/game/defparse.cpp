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
typedef void (__cdecl *AM2_DefAddLinkFn)(const AM2_DefLink *link);

#define orig_strtok        (*(AM2_StrtokFn)AM2_IMAGE(ADDR_CRT_STRTOK))
#define orig_def_name_index \
    (*(AM2_DefNameIndexFn)AM2_IMAGE(ADDR_DEF_NAME_INDEX))
#define orig_def_obj_parse (*(AM2_DefObjParseFn)AM2_IMAGE(ADDR_DEF_OBJ_PARSE))
#define orig_def_number    (*(AM2_DefParseNumberFn)AM2_IMAGE(ADDR_DEF_PARSE_NUMBER))
#define orig_def_add_link  (*(AM2_DefAddLinkFn)AM2_IMAGE(ADDR_DEF_ADD_LINK))

#define kSep ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))

typedef void (__cdecl *AM2_QsortFn)(void *base, uint32_t n, uint32_t size,
                                    const void *cmp);
typedef void *(__cdecl *AM2_DefFindObjRecFn)(int32_t a, int32_t b, int32_t c);
#define orig_qsort (*(AM2_QsortFn)AM2_IMAGE(ADDR_CRT_QSORT))
#define orig_def_find_obj_rec \
    (*(AM2_DefFindObjRecFn)AM2_IMAGE(ADDR_DEF_FIND_OBJ_REC))

#define kDefLinks      (*(AM2_DefLink **)AM2_IMAGE(ADDR_DEF_LINKS))
#define kDefLinkCount  (*(int32_t *)AM2_IMAGE(ADDR_DEF_LINK_COUNT))

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
    link.siblings = DefCountLinks(link.parent);
    link.a        = (int16_t)a;
    link.b        = (int16_t)b;
    link.c        = c;

    orig_def_add_link(&link);
    return 0;
}

/* 0x00435FA0. How many links already name this parent key. Walks the whole
 * table; the caller is the validator below and DefLinkParse, which uses it to
 * stamp each new link with how many siblings preceded it. */
int32_t __cdecl DefCountLinks(int32_t parentkey)
{
    const AM2_DefLink *p = kDefLinks;
    int32_t            n = kDefLinkCount;
    int32_t            hits = 0;

    if (n <= 0)
        return 0;

    do {
        if (p->parent == parentkey)
            hits++;
        p++;
    } while (--n);

    return hits;
}

/* 0x00435FD0. Sort the link table and check every parent against the object
 * records. A role name -- it names itself nowhere.
 *
 * The comparator is ComparePair, which this port already owns, handed to the
 * game's own qsort. Sorting is what makes the `key <= last` test a
 * duplicate-skip: each distinct parent is looked at once, however many links
 * share it.
 *
 * Unpacking with KeyFieldA and KeyFieldB is the far side of DefLinkParse's
 * PackKey(parenttype, n1, 0), and the pair is what the complaint prints, so
 * "link 33-1" is object type 33, link number 1.
 *
 * The bare Log() is the original's: it pushes no format at all and the callee
 * reads whatever the stack slot above the qsort arguments held. Every observed
 * run has that slot at 0, and the logger is a `ret` in this build, so a
 * literal 0 is passed here -- it reproduces the trace deterministically where
 * reading our own stack garbage would not. */
void __cdecl DefCheckLinks(void)
{
    uint32_t last = 0;

    orig_qsort(kDefLinks, (uint32_t)kDefLinkCount, sizeof(AM2_DefLink),
               (const void *)AM2_IMAGE(ADDR_COMPARE_PAIR));

    orig_log((const char *)0);

    for (int32_t i = 0; i < kDefLinkCount; i++) {
        uint32_t key = (uint32_t)kDefLinks[i].parent;
        uint32_t number, type;
        uint8_t *rec;

        if (key <= last)
            continue;
        last = key;

        number = KeyFieldB(key);
        type   = KeyFieldA(key);
        rec    = (uint8_t *)orig_def_find_obj_rec((int32_t)type,
                                                  (int32_t)number, 0);

        if (rec != (uint8_t *)0)
            *(int32_t *)(rec + DEF_OBJ_REC_OFF_LINKS) = DefCountLinks((int32_t)key);
        else
            orig_log("Object AAI record not found for link %02d-%-3d\n",
                     type, number);
    }
}

int defparse_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DEF_LINK_PARSE, (const void *)DefLinkParse,
                        "DefLinkParse", 1);
    rc |= patch_replace(ADDR_DEF_COUNT_LINKS, (const void *)DefCountLinks,
                        "DefCountLinks", 1);
    rc |= patch_replace(ADDR_DEF_CHECK_LINKS, (const void *)DefCheckLinks,
                        "DefCheckLinks", 1);
    return rc;
}
