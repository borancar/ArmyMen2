/* defparse.cpp -- the object-definition (.aai) parsers.
 *
 * A name of ours for a family the image names one by one: DefParseInfoFile,
 * DefGameParse, DefObjParse and DefLinkParse all appear in the binary's own
 * strings, but no source filename does, and the band they sit in is
 * map.cpp..objscript.cpp -- between two modules rather than inside either.
 *
 * These files carry their own command vocabulary, which is NOT the script
 * token table in docs/scripttokens.md. LINK is 0x5F here; 95 in the script
 * table is `sniper`.
 */
#ifndef AM2_DEFPARSE_H
#define AM2_DEFPARSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
/* 0x00435A50 and 0x0044CD40, one caller each and both from DefFinish. The
 * same qsort with different tables and comparators, each ending in a
 * no-argument tail jump to the logger. */
void __cdecl DefSortObjRecs(void);
void __cdecl DefSortTrooperRecs(void);

/* 0x0044CD70, one caller. The find half of that quartet: a bsearch of the
 * `trooperlevel` records on their level, with CompareDword on the first
 * dword. NULL when the .aai declared no such level. */
void *__cdecl DefFindTrooperRec(int32_t level);

#endif

/* One entry of the link table at ADDR_DEF_LINKS. Twenty bytes, which is the
 * stride CountLinksWithParent steps and the size the table's own search
 * wrapper is given. */
typedef struct {
    int32_t parent;     /* +0x00, PackKey(parenttype, n1, 0) */
    int32_t siblings;   /* +0x04, links already sharing that parent */
    int32_t child;      /* +0x08, PackKey(childtype, n2, 0) */
    /* Where the child is drawn relative to the parent. Both were "narrowed
     * from a parsed int32" and nothing more until PlaceCursorPrepare was
     * read: it hands the pair straight to ADDR_MENU_CURSOR_DX and its two
     * siblings, which are int16 pairs, so a building's second and third
     * pieces are offset by these. Read as a PAIR by that consumer -- one
     * int32 load -- which is also why they are adjacent int16s.
     *
     * CONFIRMED BY A SECOND CONSUMER, and the compiler is what found it:
     * renaming these broke CreateItem, which adds the first to a point's x
     * and the second to its y. Two independent readers agreeing on which is
     * which is better evidence than either alone, and it is the rule about
     * preferring a second toucher arriving for free. */
    int16_t dx;         /* +0x0C, narrowed from a parsed int32 */
    int16_t dy;         /* +0x0E, likewise */
    int32_t c;          /* +0x10 */
} AM2_DefLink;

/* 0x004360C0. Parse one LINK line:
 *
 *     LINK <parenttype> <n1> <childtype> <n2> <a> <b> <c>
 *
 * Returns 0 on success and 1..7 for the seven ways it can fail, in the order
 * the fields appear. NOTE that `line` is only used for the first strtok --
 * every field after the first comes from strtok(NULL, ...), so this shares
 * the CRT's strtok state with whatever else is parsing. */
int32_t __cdecl DefLinkParse(int32_t cmd, char *line);

/* 0x00435FA0. How many links already name this parent key. */
int32_t __cdecl DefCountLinks(int32_t parentkey);

/* 0x00435B60. Map an .aai object keyword (tokens 0x4F..0x5E) to its object
 * type constant, or -1. Logs nothing -- its caller does that. */
int32_t __cdecl DefObjParse(int32_t token);

/* 0x00435C20. Parse one OBJ line into a 56-byte record and append it. Returns
 * 0 on success, 1 for a bad keyword, 2..12 for a bad field. The thirteenth
 * field is optional and the twelfth failing is NOT an error. A role name. */
int32_t __cdecl DefObjLine(int32_t cmd, char *line);

/* 0x00435980. Append one 56-byte object record, refusing a duplicate on the
 * (type, a, b) triple DefFindObjRec searches. */
void __cdecl DefAddObjRec(const int32_t *rec);

/* 0x00435E60. Free both def tables and zero all six globals. */
void __cdecl DefFreeTables(void);

/* 0x00435FD0. Sort the link table, then check each distinct parent against the
 * object records -- storing its link count, or complaining that the record is
 * missing. A role name; it names itself nowhere. */
void __cdecl DefCheckLinks(void);

/* 0x00435EE0. Append one link, refusing a duplicate on (parent, siblings). */
void __cdecl DefAddLink(const AM2_DefLink *link);

/* 0x00435AC0. Find the object record for (type, a, b), falling back through
 * (type, 0, b) and (type, 0, 0). NULL if none matches. */
void *__cdecl DefFindObjRec(int32_t type, int32_t a, int32_t b);

/* 0x00436080. bsearch the table for (parent, siblings). Only correct after
 * DefCheckLinks has sorted it. NOT DefLinkParse, which this address was
 * called until the bodies were read. */
AM2_DefLink *__cdecl DefFindLink(int32_t parent, int32_t siblings);

int defparse_install(void);


/* 0x0044CCC0 and 0x0044CF70. The third def table -- 32-byte records keyed on
 * their first dword, filled by the still-original parser at 0x0044CD70 whose
 * rejection message is "Bad Trooper Type". Append one, and drop the lot. */
void __cdecl DefAddTrooperRec(const void *rec);
void __cdecl DefFreeTrooperRecs(void);

#ifdef __cplusplus
}
#endif

/* 0x00434B60, two callers. Take down the two `.aai` record tables and the two
 * index arrays beside them. */
void __cdecl FreeAaiTables(void);

/* 0x00460290. Sort the missile-def lookup array so ADDR_MISSILE_DEF_FIND can
 * binary-search it. The third of this file's three identical sorters. */
void __cdecl DefSortMissileRecs(void);

#endif /* AM2_DEFPARSE_H */
