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
#endif

/* One entry of the link table at ADDR_DEF_LINKS. Twenty bytes, which is the
 * stride CountLinksWithParent steps and the size the table's own search
 * wrapper is given. */
typedef struct {
    int32_t parent;     /* +0x00, PackKey(parenttype, n1, 0) */
    int32_t siblings;   /* +0x04, links already sharing that parent */
    int32_t child;      /* +0x08, PackKey(childtype, n2, 0) */
    int16_t a;          /* +0x0C, narrowed from a parsed int32 */
    int16_t b;          /* +0x0E, likewise */
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

#ifdef __cplusplus
}
#endif

#endif /* AM2_DEFPARSE_H */
