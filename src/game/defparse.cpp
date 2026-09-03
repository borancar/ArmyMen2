/* defparse.cpp -- see defparse.h. */
#include <stdint.h>

#include "defparse.h"
#include "image.h"
#include "packkey.h"
#include "definfo.h"
#include "misc.h"      /* ComparePair, FreeIfNotNull */
#include "objtype.h"   /* FreeRecordList -- reconstructed */
#include "crt.h"       /* the game's allocator -- this table is its memory */
#include "../inject/orig.h"
#include "../inject/patch.h"

/* strtok now comes through crt.h, which is the same seam under one name
 * -- see there for why the cursor has to be shared. */

#define kSep ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))

#define kDefObjRecCap  (*(int32_t *)AM2_IMAGE(ADDR_DEF_OBJ_REC_CAP))

typedef void (__cdecl *AM2_QsortFn)(void *base, uint32_t n, uint32_t size,
                                    const void *cmp);
#define orig_qsort (*(AM2_QsortFn)AM2_IMAGE(ADDR_CRT_QSORT))
#define orig_bsearch (*(AM2_BsearchFn)AM2_IMAGE(ADDR_CRT_BSEARCH))
#define kDefLinkCap    (*(int32_t *)AM2_IMAGE(ADDR_DEF_LINK_CAP))
#define kDefObjRecs      (*(void **)AM2_IMAGE(ADDR_DEF_OBJ_RECS))
#define kDefTrooperRecs  (*(void **)AM2_IMAGE(ADDR_DEF_TROOPER_RECS))
#define kDefTrooperCount (*(int32_t *)AM2_IMAGE(ADDR_DEF_TROOPER_COUNT))
#define kDefTrooperCap   (*(int32_t *)AM2_IMAGE(ADDR_DEF_TROOPER_CAP))
#define kDefObjRecCount  (*(int32_t *)AM2_IMAGE(ADDR_DEF_OBJ_REC_COUNT))

#define kDefLinks      (*(AM2_DefLink **)AM2_IMAGE(ADDR_DEF_LINKS))
#define kDefLinkCount  (*(int32_t *)AM2_IMAGE(ADDR_DEF_LINK_COUNT))

/* Read the next whitespace-delimited field. Only the first call passes the
 * line; the rest continue from strtok's own state. */
static int32_t NextType(char *line)
{
    return DefObjParse(DefFindKeyword(am2_strtok(line, kSep)));
}

static int32_t NextNumber(int32_t *out)
{
    return DefParseNumber(out, am2_strtok((char *)0, kSep));
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
    link.dx       = (int16_t)a;
    link.dy       = (int16_t)b;
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

/* 0x0044CCC0 and 0x0044CF70, the append and the free of a THIRD def table.
 *
 * It is the same machinery as DefAddObjRec above and the same two constants --
 * fifty records to begin with, 0x640 being 50 * 32, then twenty more at a time
 * -- over 32-byte records instead of 56. What identifies it is the parser that
 * fills it, 0x0044CD70, which is still original: it splits lines on
 * ADDR_DEF_SEPARATORS, reads fields with DefParseNumber, writes a small
 * integer into the record's first dword, and rejects a line it cannot classify
 * with "Bad Trooper Type". That first dword is also the whole key -- the table
 * is sorted and searched with ADDR_COMPARE_DWORD, which subtracts one dword
 * from one dword and looks no further.
 *
 * THE THREE GLOBALS ARE data, count, capacity, WHICH IS NOT THE ORDER THE UID
 * REMAP TABLE USES. That one is capacity, count, data at 0x00513080. Two
 * growable arrays in one image with the fields the other way round is the sort
 * of thing that reads as a transcription slip when it is the original; the
 * order here is fixed by `cmp [0x659F50], ecx` deciding the grow, exactly as
 * it was fixed there by the opposite compare.
 *
 * UNLIKE DefAddObjRec THERE IS NO DUPLICATE SCAN. It appends whatever it is
 * given. The parser does the searching instead -- it bsearches the table for
 * the key before deciding to append -- so the refusal lives one level up, in
 * code that is still original.
 *
 * Neither checks its allocator, and the capacity is raised before the realloc
 * and kept either way, which is the same accepted hazard as the uid remap
 * pair. The free zeroes count and capacity BEFORE releasing the pointer, which
 * is DefFreeTables' order below rather than the uid remap's.
 *
 * THE SORT IS DELIBERATELY LEFT ORIGINAL. 0x0044CD40 qsorts this table and
 * then TAIL-JUMPS to 0x0045CAA0, which is ADDR_LOG -- an address
 * src/inject/gamelog.c patches, so naming it here is a seam checkseams
 * refuses. It is a bare `ret` that identical-COMDAT folding has merged with
 * the stubbed logger, the same case CLAUDE.md records for a widget vtable's
 * slot 2, and reproducing the jump would mean deciding what the folded
 * function was. Not decided, so not written.
 *
 * BOTH RUN AT STARTUP, unlike the other two tables' halves. A Boot Camp drive
 * reads DefAddTrooperRec 8 and DefFreeTrooperRecs 1, while DefAddObjRec and
 * DefAddLink beside them read 0 for the usual reason -- their callers are
 * ours. So these two are compared by the A/B rather than verified by reading,
 * and the eight appends exercise the initial malloc but not the grow: fifty
 * records is the initial capacity and eight is well inside it.
 */
void __cdecl DefAddTrooperRec(const void *rec)
{
    uint8_t *tab = (uint8_t *)kDefTrooperRecs;
    int32_t  cap;
    int32_t  n;

    if (tab == (uint8_t *)0) {
        tab = (uint8_t *)am2_malloc(AM2_DEF_LINK_INITIAL
                                    * AM2_DEF_TROOPER_REC_SIZE);
        cap = AM2_DEF_LINK_INITIAL;
        kDefTrooperRecs = tab;
        kDefTrooperCap  = cap;
    } else {
        cap = kDefTrooperCap;
    }

    if (kDefTrooperCount >= cap) {
        cap += AM2_DEF_LINK_GROW;
        kDefTrooperCap = cap;
        tab = (uint8_t *)am2_realloc(
                  tab, (size_t)cap * AM2_DEF_TROOPER_REC_SIZE);
        kDefTrooperRecs = tab;
    }

    n = kDefTrooperCount;
    for (int32_t i = 0; i < (int32_t)(AM2_DEF_TROOPER_REC_SIZE / 4); i++)
        ((int32_t *)(tab + n * AM2_DEF_TROOPER_REC_SIZE))[i] =
            ((const int32_t *)rec)[i];

    kDefTrooperCount = n + 1;
}

/* DefTrooperLine -- original 0x0044CDA0, 464 bytes, NINE exits.
 *
 * The `trooperlevel1` .. `trooperlevel8` lines of a .def file, which is how
 * the eight-entry RANK table is declared: seven numbers per line, keyed by
 * which of the eight keywords opened it. Its fields already had names --
 * RANK_SRC_OFF_MAX_HEALTH, _SIGHT_RANGE, _FIRE_PCT, _THRESHOLD and _XP --
 * from the side that consumes the record.
 *
 * NOTHING IN .text CALLS IT. Its eight callers are eight entries of the
 * keyword table in .rdata, each {const char *word, int32_t id, void *fn},
 * and dumping that table is what named the function: the words are
 * trooperlevel1..8 and the ids 0x2D..0x34. A function reached only from a
 * data table is invisible to a call-graph and obvious from the table.
 *
 * ITS EIGHT-ARM JUMP TABLE IS THE IDENTITY, and that was checked rather than
 * assumed. Each arm does nothing but store 0..7, so the whole switch is
 * `cmd - 0x2D` -- but WeaponClassOf lays four arms out in one order and
 * dispatches them in another, so an eight-arm table that looks like a
 * counted sequence is exactly the case this project has been burned by.
 * Decoded: entry k really is arm k. Written as the subtraction it is.
 *
 * The RETURN IS AN ERROR CODE, not a boolean: 0 when the record is added,
 * 1 for a keyword outside the eight, and 2..8 to say which of the seven
 * numbers failed to parse. That is where nine exits come from, and it
 * matches DefObjLine, whose numbering runs to 12 for the same reason.
 *
 * IT DOES NOT ZERO THE RECORD, where DefObjLine does. It has no need to --
 * all eight dwords are written before DefAddTrooperRec sees them, one from
 * the keyword and seven from the line -- and the difference is reproduced
 * rather than tidied into a shared shape.
 *
 * am2_strtok rather than the host's, for the reason crt.h gives: the cursor
 * is shared with DefParseInfoFile, which is mid-file when this is called. */
int32_t __cdecl DefTrooperLine(int32_t cmd, char *line)
{
    int32_t rec[AM2_DEF_TROOPER_REC_DWORDS];

    if (cmd < AM2_DEF_TROOPERLEVEL1
        || cmd > AM2_DEF_TROOPERLEVEL1 + AM2_RANK_COUNT - 1) {
        orig_log("Bad Trooper Type\n");
        return 1;
    }
    rec[0] = cmd - AM2_DEF_TROOPERLEVEL1;

    if (!DefParseNumber(&rec[1], am2_strtok(line, kSep)))
        return 2;

    for (int32_t i = 2; i < AM2_DEF_TROOPER_REC_DWORDS; i++)
        if (!DefParseNumber(&rec[i], am2_strtok((char *)0, kSep)))
            return i + 1;

    DefAddTrooperRec(rec);
    return 0;
}

void __cdecl DefFreeTrooperRecs(void)
{
    uint8_t *recs = (uint8_t *)kDefTrooperRecs;

    kDefTrooperCount = 0;
    kDefTrooperCap   = 0;

    if (recs != (uint8_t *)0)
        am2_free(recs);
    kDefTrooperRecs = (void *)0;
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

    if (!DefParseNumber(&rec[1], am2_strtok(line, kSep)))
        return 2;

    for (int32_t i = 2; i <= 11; i++) {
        if (!DefParseNumber(&rec[i], am2_strtok((char *)0, kSep)))
            return i + 1;

        if (i == 3)
            rec[3] = 0;      /* parsed, checked, discarded -- see above */
    }

    if (DefParseNumber(&rec[12], am2_strtok((char *)0, kSep)))
        (void)DefParseNumber(&rec[13], am2_strtok((char *)0, kSep));

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

/* DefFindTrooperRec -- original 0x0044CD70, one caller: LoadDefTables, which
 * walks levels 0..7 filling ADDR_RANK_RECORDS from what this finds.
 *
 * The find half of the quartet DefAddTrooperRec, DefSortTrooperRecs and
 * DefFreeTrooperRecs already make up, and one bsearch is the whole of it --
 * the key is a single dword in a stack record, and CompareDword compares only
 * the first, which is what makes a sorted table searchable on its level.
 *
 * IT RESERVES A WHOLE RECORD ON THE STACK AND FILLS ONE FIELD. `sub esp, 0x20`
 * is AM2_DEF_TROOPER_REC_SIZE, the same size it hands bsearch, and only the
 * first dword is written -- the other seven are whatever the frame held.
 * CompareDword never reads them, so it cannot matter, and it is written the
 * same way rather than as a bare int32: the size the original reserves is
 * evidence about what the key IS.
 *
 * `TROOPER` HERE IS THE PROGRAM'S WORD, not a role name taken from a caller.
 * ADDR_DEF_NAME_TABLE has eight .aai keywords pointing at the parser above
 * this one -- `trooperlevel1` through `trooperlevel8` -- which is also why
 * DefAddTrooperRec reads exactly 8 on a drive. See orig.h; the name this
 * function had, ADDR_RANK_DEF_FIND, was the odd one of the four.
 *
 * ITS ENTRY IN docs/functions.tsv IS MERGED: 512 bytes covering this 46-byte
 * bsearch and the 450-byte `trooperlevel` line parser after it, which is
 * still original and which the keyword table points at.
 */
void *__cdecl DefFindTrooperRec(int32_t level)
{
    int32_t key[AM2_DEF_TROOPER_REC_SIZE / 4];

    key[0] = level;

    return orig_bsearch(key, kDefTrooperRecs, (uint32_t)kDefTrooperCount,
                        AM2_DEF_TROOPER_REC_SIZE,
                        (const void *)CompareDword);
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

typedef void (__cdecl *AM2_DefLogFn)(void);
#define orig_def_log ((AM2_DefLogFn)(uintptr_t)ADDR_LOG)

/* DefSortObjRecs and DefSortTrooperRecs -- originals 0x00435A50 and
 * 0x0044CD40, one caller each, and both from DefFinish.
 *
 * The same function with different tables: qsort the record array by its
 * count and stride, then TAIL-JUMP to the logger with no arguments. The
 * object records go through ADDR_COMPARE_TRIPLE and the trooper records
 * through ADDR_COMPARE_DWORD, which is the only thing that distinguishes
 * them apart from the three globals.
 *
 * They use the game's own qsort, not ours, for the same reason the file
 * functions do: the comparator is an address in the image and the array is
 * on the game's heap. Nothing here would break with libc's, but nothing here
 * has any reason to prefer it either.
 *
 * The trooper one had been read before and left original -- orig.h described
 * it exactly and said so. It is the whole of a 48-byte function, so what was
 * left was the writing.
 */
void __cdecl DefSortObjRecs(void)
{
    orig_qsort(*(void **)(uintptr_t)ADDR_DEF_OBJ_RECS,
               *(const uint32_t *)(uintptr_t)ADDR_DEF_OBJ_REC_COUNT,
               AM2_DEF_OBJ_REC_SIZE,
               (const void *)CompareTriple);
    orig_def_log();
}

/* 0x00460290. The third of the three sorters, and the same function again --
 * this file's note above the other two applies unchanged: same shape, a
 * different table, and the comparator is the only thing that distinguishes
 * them. This one uses CompareDword like the trooper sorter.
 *
 * The table is the missile defs' lookup array, named from its READER:
 * ADDR_MISSILE_DEF_FIND reads it, and a find over something another function
 * qsorts is a binary search. */
void __cdecl DefSortMissileRecs(void)
{
    orig_qsort(*(void **)(uintptr_t)ADDR_DEF_MISSILE_RECS,
               *(const uint32_t *)(uintptr_t)ADDR_DEF_MISSILE_COUNT,
               AM2_MISSILE_DEF_BYTES,
               (const void *)CompareDword);
    orig_def_log();
}

void __cdecl DefSortTrooperRecs(void)
{
    orig_qsort(*(void **)(uintptr_t)ADDR_DEF_TROOPER_RECS,
               *(const uint32_t *)(uintptr_t)ADDR_DEF_TROOPER_COUNT,
               AM2_DEF_TROOPER_REC_SIZE,
               (const void *)CompareDword);
    orig_def_log();
}

/* FreeAaiTables -- original 0x00434B60, two callers.
 *
 * Take down the two record tables the `.aai` parser builds and the two index
 * arrays beside them: free each record list, free the array of them, free the
 * sorted key index, free each AAI record, free the array of those, free the
 * key table. Four blocks, and every pointer is cleared as it goes.
 *
 * THE FIRST BLOCK IS GATED ON ITS ARRAY AND THE THIRD ON ITS COUNT, which is
 * not symmetry and is worth reading twice. `ADDR_RECORD_LISTS` non-null runs
 * the loop and the free; `ADDR_KEY_TABLE_COUNT` non-zero does the same for
 * `ADDR_AAI_RECORDS`. So a record-list array with a zero count is freed
 * correctly -- the loop simply does not run -- while a NULL AAI array with a
 * non-zero count would be indexed. Nothing sets one without the other, and
 * the original takes the risk either way round. Reproduced.
 *
 * Both inner frees are ours already -- FreeRecordList in objtype.cpp and
 * FreeIfNotNull in misc.cpp -- and are called by name. checkseams caught the
 * two orig_ macros that went in first; fifth and sixth this session, and the
 * picker now prints what orig.h already knows about a candidate for exactly
 * this reason.
 *
 * THE TWO INNER FREES ARE DIFFERENT FUNCTIONS. `FreeRecordList` is a real
 * teardown; `FreeIfNotNull` is the one-line guard its name says. So the record
 * lists are structures and the AAI records are plain allocations, which is the
 * only thing here that says the two tables hold different kinds of thing.
 *
 * The array pointer and the count are re-read from their globals on every
 * iteration of both loops. Nothing in either callee can touch them -- they
 * free an element, not the table -- so that is the compiler, and it is written
 * as the plain loop it means.
 *
 * The capacities are cleared with the counts, but only in the two blocks that
 * have one; the key table and the sorted index are single pointers with no
 * count of their own.
 */
void __cdecl FreeAaiTables(void)
{
    int32_t i;

    if (*(void *const *)(uintptr_t)ADDR_RECORD_LISTS) {
        for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT;
             i++)
            FreeRecordList(
                (*(void *const *const *)(uintptr_t)ADDR_RECORD_LISTS)[i]);

        am2_free(*(void **)(uintptr_t)ADDR_RECORD_LISTS);
        *(void **)(uintptr_t)ADDR_RECORD_LISTS         = (void *)0;
        *(int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT  = 0;
        *(int32_t *)(uintptr_t)ADDR_RECORD_LIST_CAP    = 0;
    }

    if (*(void *const *)(uintptr_t)ADDR_RECORD_LIST_INDEX) {
        am2_free(*(void **)(uintptr_t)ADDR_RECORD_LIST_INDEX);
        *(void **)(uintptr_t)ADDR_RECORD_LIST_INDEX = (void *)0;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT) {
        for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT; i++)
            FreeIfNotNull(
                (*(void *const *const *)(uintptr_t)ADDR_AAI_RECORDS)[i]);

        am2_free(*(void **)(uintptr_t)ADDR_AAI_RECORDS);
        *(void **)(uintptr_t)ADDR_AAI_RECORDS       = (void *)0;
        *(int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT = 0;
        *(int32_t *)(uintptr_t)ADDR_AAI_RECORD_CAP  = 0;
    }

    if (*(void *const *)(uintptr_t)ADDR_KEY_TABLE) {
        am2_free(*(void **)(uintptr_t)ADDR_KEY_TABLE);
        *(void **)(uintptr_t)ADDR_KEY_TABLE = (void *)0;
    }
}


/* DefVehicleLine -- original 0x0045EC20, and it is registered FOUR times in
 * the .aai keyword table at 0x00477258: for `tank` (0x36), `half_track`
 * (0x37), `convoy` (0x38) and one more. One function serves every vehicle
 * keyword and the TABLE supplies the difference, which is why its first
 * argument is the keyword's own code.
 *
 * `jeep` (0x35) is the exception -- it gets 0x004602F0, a much larger
 * function, rather than this one. So the switch below covers 0x35 even though
 * nothing in the table routes 0x35 here; reading the arms without reading the
 * table would make that arm look reachable.
 *
 * Nine dwords, which is AM2_VEHICLE_DEF_REC_SIZE exactly: the kind from the
 * keyword, then eight numbers off the line.
 *
 * EVERY FIELD HAS ITS OWN ERROR CODE, 2 through 9 in order, and the caller
 * gets the index of the field that failed rather than a flag. The default arm
 * answers 1 and is the only one that says anything -- "Bad Vehicle Type",
 * through the game's own logger, which here is a REAL call with a format
 * string rather than the empty-frame tail jump that SortVehicleDefs ends on.
 * The two look alike in a disassembly and are not the same thing.
 *
 * Only the first token comes from the line; the rest continue from strtok's
 * own state, which is what NextNumber above is for. */
int32_t __cdecl DefVehicleLine(int32_t code, char *line)
{
    int32_t rec[9];

    switch (code) {
    case 0x35: rec[0] = 0; break;
    case 0x36: rec[0] = 1; break;
    case 0x37: rec[0] = 2; break;
    case 0x38: rec[0] = 3; break;
    case 0x39: rec[0] = 4; break;
    case 0x3A: rec[0] = 5; break;
    default:
        orig_log("Bad Vehicle Type\n");
        return 1;
    }

    if (!DefParseNumber(&rec[1], am2_strtok(line, kSep)))  return 2;
    if (!NextNumber(&rec[2]))                              return 3;
    if (!NextNumber(&rec[3]))                              return 4;
    if (!NextNumber(&rec[4]))                              return 5;
    if (!NextNumber(&rec[5]))                              return 6;
    if (!NextNumber(&rec[6]))                              return 7;
    if (!NextNumber(&rec[7]))                              return 8;
    if (!NextNumber(&rec[8]))                              return 9;

    AddVehicleDef(rec);
    return 0;
}


/* THE KEYWORD-TO-KIND MAP, GENERATED FROM THE IMAGE'S JUMP TABLE RATHER THAN
 * TRANSCRIBED, and it is not the identity.
 *
 * Entry k is the arm for code k+1, and every arm does one thing: store a small
 * integer. Forty-two of them store k. TWO DO NOT -- entry 31 stores 32 and
 * entry 32 stores 31 -- so `vehicleammo` (code 32) and `vehiclearmor` (code
 * 33) get each other's kind index. Writing this as `cmd - 1`, which is what
 * forty-two of the forty-four arms say, would have been wrong for exactly
 * those two and invisible to every check this project has: the def tables are
 * loaded before any A/B takes its first screenshot, and nothing compares them.
 *
 * Decoded by reading each table entry's target and its immediate, which is the
 * rule DirtyCollect's eighty-one arms established -- emit the mapping from the
 * table, never from the order the arms are laid out in. */
static const int32_t kWeaponKind[44] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 31,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
};

/* DefWeaponLine -- original 0x004602F0, and the .aai keyword table registers
 * it for forty-four weapon keywords. Thirteen dwords, which is
 * AM2_MISSILE_DEF_REC_SIZE: the kind from the keyword, then twelve numbers,
 * and the record goes to AddMissileDef.
 *
 * TWO OF THE TWELVE FIELDS ARE BOOLEANS, not numbers -- rec[7] and rec[10] go
 * through DefParseBoolean, the rest through DefParseNumber. That is not
 * visible from reading a couple of the parse blocks, which are otherwise
 * identical; only the call TARGETS separate them, 0x0041A2D0 against
 * 0x0041A250. Written first as twelve numbers, and the A/B caught it at once:
 * "Invalid table entry: line 4, token #8" and a dumped record reading
 * `0 0 0 0 0 0 False 0 0 False 0 0`, with the False in exactly those two
 * columns. Where the original repeats itself, diff the calls before believing
 * the repetition -- this file says so and it was still worth relearning.
 *
 * The return is an error code like its two siblings' -- 0 on success, 1 for a
 * keyword outside the range, and 2..13 to say which field failed. That is
 * the numbering DefTrooperLine's comment already points at as "runs to 12".
 *
 * `jeep` IS REGISTERED HERE AND CANNOT WORK. Its code is 53, outside the
 * switch, so it takes the default arm and is refused with "Bad Weapon Type".
 * The vehicle keywords beside it go to DefVehicleLine; this one entry points
 * at the wrong handler. Reproduced -- it is the table's mistake, not ours,
 * and correcting it would diverge from the original for a `jeep` line. */
int32_t __cdecl DefWeaponLine(int32_t cmd, char *line)
{
    int32_t rec[AM2_MISSILE_DEF_REC_SIZE / 4];
    int32_t i;

    if (cmd < 1 || cmd > 44) {
        orig_log("Bad Weapon Type\n");
        return 1;
    }
    rec[0] = kWeaponKind[cmd - 1];

    if (!DefParseNumber(&rec[1], am2_strtok(line, kSep)))
        return 2;

    for (i = 2; i < (int32_t)(AM2_MISSILE_DEF_REC_SIZE / 4); i++) {
        const char *tok = am2_strtok((char *)0, kSep);
        int32_t     ok  = (i == 7 || i == 10)
                          ? DefParseBoolean(&rec[i], tok)
                          : DefParseNumber(&rec[i], tok);

        if (!ok)
            return i + 1;
    }

    AddMissileDef(rec);
    return 0;
}

int defparse_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_FREE_AAI_TABLES,
                        (const void *)FreeAaiTables,
                        "FreeAaiTables", 2);

    rc |= patch_replace(ADDR_DEF_SORT_OBJ_RECS, (const void *)DefSortObjRecs,
                        "DefSortObjRecs", 1);
    rc |= patch_replace(ADDR_DEF_STEP_460290,
                        (const void *)DefSortMissileRecs,
                        "DefSortMissileRecs", 1);
    rc |= patch_replace(ADDR_DEF_SORT_TROOPER_RECS,
                        (const void *)DefSortTrooperRecs,
                        "DefSortTrooperRecs", 1);
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
    rc |= patch_replace(ADDR_DEF_TROOPER_LINE,
                        (const void *)DefTrooperLine, "DefTrooperLine", 2);
    rc |= patch_replace(ADDR_DEF_ADD_TROOPER_REC,
                        (const void *)DefAddTrooperRec, "DefAddTrooperRec", 1);
    rc |= patch_replace(ADDR_DEF_FIND_TROOPER_REC,
                        (const void *)DefFindTrooperRec,
                        "DefFindTrooperRec", 1);
    rc |= patch_replace(ADDR_DEF_FREE_TROOPER_RECS,
                        (const void *)DefFreeTrooperRecs,
                        "DefFreeTrooperRecs", 2);
    rc |= patch_replace(ADDR_DEF_FREE_TABLES, (const void *)DefFreeTables,
                        "DefFreeTables", 3);
    rc |= patch_replace(ADDR_DEF_VEHICLE_LINE, (const void *)DefVehicleLine,
                        "DefVehicleLine", 2);
    rc |= patch_replace(ADDR_DEF_WEAPON_LINE, (const void *)DefWeaponLine,
                        "DefWeaponLine", 2);
    return rc;
}
