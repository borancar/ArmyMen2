/* defparse.cpp -- see defparse.h. */
#include <stdint.h>

#include "defparse.h"
#include "image.h"
#include "packkey.h"
#include "definfo.h"
#include "misc.h"      /* ComparePair */
#include "crt.h"       /* the game's allocator -- this table is its memory */
#include "../inject/orig.h"
#include "../inject/patch.h"

/* All four still original and reached by address. strtok in particular MUST be
 * the game's: DefObjParse below tokenises from the same state, and libc's
 * would be a second, unrelated cursor. */
typedef char *(__cdecl *AM2_StrtokFn)(char *s, const char *sep);

#define orig_strtok        (*(AM2_StrtokFn)AM2_IMAGE(ADDR_CRT_STRTOK))

#define kSep ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))

#define kDefObjRecCap  (*(int32_t *)AM2_IMAGE(ADDR_DEF_OBJ_REC_CAP))

typedef void (__cdecl *AM2_QsortFn)(void *base, uint32_t n, uint32_t size,
                                    const void *cmp);
#define orig_qsort (*(AM2_QsortFn)AM2_IMAGE(ADDR_CRT_QSORT))
typedef void *(__cdecl *AM2_BsearchFn)(const void *key, const void *base,
                                       uint32_t n, uint32_t size,
                                       const void *cmp);
#define orig_bsearch (*(AM2_BsearchFn)AM2_IMAGE(ADDR_CRT_BSEARCH))
#define kDefLinkCap    (*(int32_t *)AM2_IMAGE(ADDR_DEF_LINK_CAP))
#define kDefObjRecs      (*(void **)AM2_IMAGE(ADDR_DEF_OBJ_RECS))
#define kDefObjRecCount  (*(int32_t *)AM2_IMAGE(ADDR_DEF_OBJ_REC_COUNT))

#define kDefLinks      (*(AM2_DefLink **)AM2_IMAGE(ADDR_DEF_LINKS))
#define kDefLinkCount  (*(int32_t *)AM2_IMAGE(ADDR_DEF_LINK_COUNT))

/* Read the next whitespace-delimited field. Only the first call passes the
 * line; the rest continue from strtok's own state. */
static int32_t NextType(char *line)
{
    return DefObjParse(DefFindKeyword(orig_strtok(line, kSep)));
}

static int32_t NextNumber(int32_t *out)
{
    return DefParseNumber(out, orig_strtok((char *)0, kSep));
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

    DefAddLink(&link);
    return 0;
}

/* 0x00435980. Append one object record, refusing a duplicate. The twin of
 * DefAddLink: fifty records to begin with -- 0xAF0 is 50 * 56 -- then twenty
 * MORE RECORDS at a time, and a linear scan before the copy.
 *
 * The comparator here is CompareTriple, not ComparePair, so "duplicate" means
 * the first THREE fields match: the same triple DefFindObjRec searches on.
 * That is what makes the cascade well defined -- one record per (type, a, b).
 *
 * The original computes 56 * cap as ((cap * 8) - cap) << 3, which is the same
 * number and is written plainly here. */
void __cdecl DefAddObjRec(const int32_t *rec)
{
    int32_t *tab = (int32_t *)kDefObjRecs;
    int32_t  cap;
    int32_t  n;

    if (tab == (int32_t *)0) {
        tab = (int32_t *)am2_malloc(AM2_DEF_LINK_INITIAL
                                    * AM2_DEF_OBJ_REC_SIZE);
        cap = AM2_DEF_LINK_INITIAL;
        kDefObjRecs   = tab;
        kDefObjRecCap = cap;
    } else {
        cap = kDefObjRecCap;
    }

    n = kDefObjRecCount;

    if (n >= cap) {
        cap += AM2_DEF_LINK_GROW;
        kDefObjRecCap = cap;
        tab = (int32_t *)am2_realloc(tab,
                                     (size_t)cap * AM2_DEF_OBJ_REC_SIZE);
        kDefObjRecs = tab;
        n = kDefObjRecCount;
    }

    for (int32_t i = 0; i < n; i++) {
        if (CompareTriple(&tab[i * AM2_DEF_OBJ_REC_DWORDS], rec) == 0) {
            orig_log("duplicate record in object.aai file %d-%d\n",
                     rec[0], rec[1]);
            return;
        }
    }

    for (int32_t i = 0; i < AM2_DEF_OBJ_REC_DWORDS; i++)
        tab[n * AM2_DEF_OBJ_REC_DWORDS + i] = rec[i];

    kDefObjRecCount = n + 1;
}

/* 0x00435E60. Drop both def tables. Role name.
 *
 * The order is worth keeping: all four counts and capacities are zeroed FIRST,
 * then each pointer is freed and only then cleared. Reproduced as written. */
void __cdecl DefFreeTables(void)
{
    int32_t *recs  = (int32_t *)kDefObjRecs;
    AM2_DefLink *links;

    kDefObjRecCount = 0;
    kDefLinkCount   = 0;
    kDefObjRecCap   = 0;
    kDefLinkCap     = 0;

    if (recs != (int32_t *)0)
        am2_free(recs);
    kDefObjRecs = (void *)0;

    links = kDefLinks;
    if (links != (AM2_DefLink *)0)
        am2_free(links);
    kDefLinks = (AM2_DefLink *)0;
}

/* 0x00435C20. Parse one OBJ line of an .aai file into a 56-byte record.
 *
 *     <keyword> <f1> <f2> ... <f12> [<f13>]
 *
 * A role name -- it names itself nowhere. The string it logs,
 * "DefObjParse: Bad object Constant Type", names its CALLEE; only
 * docs/functions.tsv merging the two made the sweep attribute it here.
 *
 * The record is zeroed whole, then rec[0] comes from DefObjParse and the rest
 * from thirteen numbers. Three irregularities, all reproduced:
 *
 *  - rec[3] is parsed AND checked -- a bad value returns 4 -- and then thrown
 *    away and forced to 0. That slot is DEF_OBJ_REC_OFF_LINKS, which
 *    DefCheckLinks fills in later with the parent's link count, so whatever
 *    the file says there cannot survive.
 *  - the TWELFTH field failing is not an error. It skips the thirteenth and
 *    appends the record anyway, returning 0, which is what makes that last
 *    field optional.
 *  - the thirteenth field's parse result is never looked at.
 *
 * Failure codes are ordinal: 1 for the keyword, then 2..12 for rec[1]..rec[11]
 * in order. */
int32_t __cdecl DefObjLine(int32_t cmd, char *line)
{
    int32_t rec[AM2_DEF_OBJ_REC_DWORDS];

    for (int32_t i = 0; i < AM2_DEF_OBJ_REC_DWORDS; i++)
        rec[i] = 0;

    rec[0] = DefObjParse(cmd);
    if (rec[0] < 0) {
        orig_log("DefObjParse: Bad object Constant Type\n");
        return 1;
    }

    if (!DefParseNumber(&rec[1], orig_strtok(line, kSep)))
        return 2;

    for (int32_t i = 2; i <= 11; i++) {
        if (!DefParseNumber(&rec[i], orig_strtok((char *)0, kSep)))
            return i + 1;

        if (i == 3)
            rec[3] = 0;      /* parsed, checked, discarded -- see above */
    }

    if (DefParseNumber(&rec[12], orig_strtok((char *)0, kSep)))
        (void)DefParseNumber(&rec[13], orig_strtok((char *)0, kSep));

    DefAddObjRec(rec);
    return 0;
}

/* 0x00435B60. Map one .aai object keyword to its object type constant.
 *
 * A jump table over sixteen consecutive tokens, 0x4F..0x5E, and the mapping is
 * neither contiguous nor monotonic -- 20..23, then 25..27, 29, then 37, 38,
 * then 31..33, then 42, 43, 45. Read from the TABLE at 0x00435BD8 rather than
 * from the order the arms are laid out in, per the rule in CLAUDE.md; here the
 * two happen to agree, which is exactly what makes the check worth doing
 * rather than skipping.
 *
 * Anything else returns -1 and logs nothing. The string that names this
 * function lives in its caller, which complains about the -1. */
int32_t __cdecl DefObjParse(int32_t token)
{
    switch (token) {
    case 0x4F: return 20;
    case 0x50: return 21;
    case 0x51: return 22;
    case 0x52: return 23;
    case 0x53: return 25;
    case 0x54: return 26;
    case 0x55: return 27;
    case 0x56: return 29;
    case 0x57: return 37;
    case 0x58: return 38;
    case 0x59: return 31;
    case 0x5A: return 32;
    case 0x5B: return 33;
    case 0x5C: return 42;
    case 0x5D: return 43;
    case 0x5E: return 45;
    default:   return -1;
    }
}

/* 0x00435AC0. Find the object record for a (type, a, b) triple, falling back
 * to less specific keys. A role name.
 *
 * Three bsearches over the same sorted 56-byte table, each with one more field
 * blanked:
 *
 *     (type, a, b)  ->  (type, 0, b)  ->  (type, 0, 0)
 *
 * so an .aai file can give a record for one exact variant, one for any `a`,
 * and one for the type alone, and the most specific wins. The first hit is
 * returned; a miss after all three gives whatever the last bsearch returned,
 * which is NULL.
 *
 * Note the cascade is REDUNDANT when b is already 0 -- steps two and three
 * search for the same key, and DefCheckLinks calls it exactly that way. The
 * original does the work twice and so does this.
 *
 * The key is a 56-byte partial record and only its first three dwords are ever
 * written; CompareTriple reads no further, so the rest is left uninitialised
 * as in the original. The comparator is passed by ADDRESS rather than as our
 * own symbol, which is what the original does and what keeps CompareTriple's
 * counter meaningful. */
void *__cdecl DefFindObjRec(int32_t type, int32_t a, int32_t b)
{
    int32_t key[AM2_DEF_OBJ_REC_SIZE / 4];
    void   *hit;

    key[0] = type;
    key[1] = a;
    key[2] = b;

    hit = orig_bsearch(key, kDefObjRecs, (uint32_t)kDefObjRecCount,
                       AM2_DEF_OBJ_REC_SIZE,
                       (const void *)CompareTriple);
    if (hit != (void *)0)
        return hit;

    key[1] = 0;
    hit = orig_bsearch(key, kDefObjRecs, (uint32_t)kDefObjRecCount,
                       AM2_DEF_OBJ_REC_SIZE,
                       (const void *)CompareTriple);
    if (hit != (void *)0)
        return hit;

    key[1] = 0;
    key[2] = 0;
    return orig_bsearch(key, kDefObjRecs, (uint32_t)kDefObjRecCount,
                        AM2_DEF_OBJ_REC_SIZE,
                        (const void *)CompareTriple);
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
               (const void *)ComparePair);

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
        rec    = (uint8_t *)DefFindObjRec((int32_t)type,
                                                  (int32_t)number, 0);

        if (rec != (uint8_t *)0)
            *(int32_t *)(rec + DEF_OBJ_REC_OFF_LINKS) = DefCountLinks((int32_t)key);
        else
            orig_log("Object AAI record not found for link %02d-%-3d\n",
                     type, number);
    }
}

/* 0x00435EE0. Append one link, refusing a duplicate. Role name.
 *
 * The table starts at fifty records and grows twenty RECORDS at a time, not
 * twenty bytes -- the original computes `(cap + 20) * 20` for the realloc.
 *
 * The duplicate test is a linear scan with ComparePair, which orders on the
 * first two fields: parent, then siblings. Since `siblings` is the count of
 * links already sharing this parent, it is really this link's INDEX among
 * them, and the pair is therefore unique per link -- which is also what makes
 * the bsearch below well defined and what the qsort in DefCheckLinks sorts on.
 * Three uses, one key.
 *
 * A duplicate is complained about and DROPPED; the count does not advance. */
void __cdecl DefAddLink(const AM2_DefLink *link)
{
    AM2_DefLink *tab = kDefLinks;
    int32_t      cap;
    int32_t      n;

    if (tab == (AM2_DefLink *)0) {
        tab = (AM2_DefLink *)am2_malloc(AM2_DEF_LINK_INITIAL
                                        * sizeof(AM2_DefLink));
        cap = AM2_DEF_LINK_INITIAL;
        kDefLinks   = tab;
        kDefLinkCap = cap;
    } else {
        cap = kDefLinkCap;
    }

    n = kDefLinkCount;

    if (n >= cap) {
        cap += AM2_DEF_LINK_GROW;
        kDefLinkCap = cap;
        tab = (AM2_DefLink *)am2_realloc(tab,
                                         (size_t)cap * sizeof(AM2_DefLink));
        kDefLinks = tab;
        n = kDefLinkCount;
    }

    for (int32_t i = 0; i < n; i++) {
        if (ComparePair(&tab[i], link) == 0) {
            orig_log("duplicate link record in object.aai file\n");
            return;
        }
    }

    tab[n] = *link;
    kDefLinkCount = n + 1;
}

/* 0x00436080. Find a link by (parent, siblings) -- a bsearch over the table
 * with the same ComparePair, which only answers correctly because
 * DefCheckLinks sorted it first. NOT DefLinkParse: this address carried that
 * name until the bodies were read, because docs/functions.tsv merges the two.
 *
 * The key is a partial record built on the stack; only its first two fields
 * are ever examined, so the rest is left uninitialised exactly as in the
 * original. */
AM2_DefLink *__cdecl DefFindLink(int32_t parent, int32_t siblings)
{
    AM2_DefLink key;

    key.parent   = parent;
    key.siblings = siblings;

    return (AM2_DefLink *)orig_bsearch(&key, kDefLinks,
                                       (uint32_t)kDefLinkCount,
                                       sizeof(AM2_DefLink),
                                       (const void *)ComparePair);
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
    rc |= patch_replace(ADDR_DEF_ADD_LINK, (const void *)DefAddLink,
                        "DefAddLink", 1);
    rc |= patch_replace(ADDR_DEF_LINK_SEARCH, (const void *)DefFindLink,
                        "DefFindLink", 2);
    rc |= patch_replace(ADDR_DEF_FIND_OBJ_REC, (const void *)DefFindObjRec,
                        "DefFindObjRec", 3);
    rc |= patch_replace(ADDR_DEF_OBJ_PARSE, (const void *)DefObjParse,
                        "DefObjParse", 1);
    rc |= patch_replace(ADDR_DEF_OBJ_LINE, (const void *)DefObjLine,
                        "DefObjLine", 1);
    rc |= patch_replace(ADDR_DEF_ADD_OBJ_REC, (const void *)DefAddObjRec,
                        "DefAddObjRec", 1);
    rc |= patch_replace(ADDR_DEF_FREE_TABLES, (const void *)DefFreeTables,
                        "DefFreeTables", 3);
    return rc;
}
