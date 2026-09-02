/* definfo.cpp -- reading the object-definition (.aai) files.
 *
 * A name of ours, taken from the one function in this band that names itself:
 * DefParseInfoFile, at 0x0041A5F0. These sit in the audio.cpp..event.cpp band,
 * which is a DIFFERENT translation unit from defparse.cpp's record parsers --
 * so they are a separate module here rather than folded in, even though the
 * two halves only make sense together.
 *
 * The vocabulary lives in one table at ADDR_DEF_NAME_TABLE, twelve bytes an
 * entry, and an entry's INDEX is the command id its handler is passed.
 */
#ifndef AM2_DEFINFO_H
#define AM2_DEFINFO_H

#include <stdint.h>

#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

/* One row of the .aai vocabulary. `handler` is the parser for lines opening
 * with this keyword -- DefObjLine for the object types, DefLinkParse for
 * LINK. */
typedef struct {
    const char *name;     /* +0x00; an empty one ends the table */
    int32_t     value;    /* +0x04 */
    void       *handler;  /* +0x08 */
} AM2_DefKeyword;

/* 0x0041A250. Parse one token as a number into *out. Returns 1 on success and
 * 0 for a null token or a token that is not a number -- complaining only in
 * the second case. 48 callers. */
/* 0x004602C0, still the original's: a bsearch over the definition table by id.
 * The MISSILE in ADDR_MISSILE_DEF_FIND's name is from its first-seen use --
 * MedkitHeal asks it for AM2_ITEM_KIND_MEDKIT. */
typedef void *(__cdecl *AM2_DefFindFn)(int32_t id);

int32_t __cdecl DefParseNumber(int32_t *out, const char *tok);

/* 0x0041A640. The index of `name` in the vocabulary table, or -1. That index
 * is the command id the handlers are given. */
/* 0x0041A290. The float twin: strtod rather than strtol, no base argument,
 * and the value stored before the check either way. */
int32_t __cdecl DefParseFloat(float *out, const char *tok);

/* 0x0041A2D0. The boolean twin: TRUE/true/True/T/t/1 against
 * FALSE/false/False/F/f/0, twelve inlined strcmps in the original. A null
 * token returns 0 in silence and leaves *out alone; anything unrecognised
 * complains and stores 0. */
int32_t __cdecl DefParseBoolean(int32_t *out, const char *tok);

int32_t __cdecl DefFindKeyword(const char *name);

/* 0x0041A6B0. Read an open .aai file, dispatching each line through the
 * vocabulary table. Closes `fp` on every exit. 1 if the whole file parsed. */
int32_t __cdecl DefDispatchFile(am2_FILE *fp);

/* 0x0041A5F0. Open one .aai file and hand it to the dispatcher, which then
 * owns it. Ten callers. */
int32_t __cdecl DefParseInfoFile(const char *path);

/* 0x00403400, one caller. Reload every definition table from the .aai files,
 * letting the tileset directory override Object.aai, then rebuild the missile
 * definitions and the rank records. Both rebuilds permute their fields; see
 * definfo.cpp. */
void __cdecl LoadDefTables(void);

int definfo_install(void);

#ifdef __cplusplus
}
#endif

/* 0x0045EDF0. Free the vehicle def table, zeroing the count before the free
 * and the pointer after it. */
void __cdecl FreeVehicleDefs(void);

#endif /* AM2_DEFINFO_H */
