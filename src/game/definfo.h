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
int32_t __cdecl DefParseNumber(int32_t *out, const char *tok);

/* 0x0041A640. The index of `name` in the vocabulary table, or -1. That index
 * is the command id the handlers are given. */
int32_t __cdecl DefFindKeyword(const char *name);

int definfo_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_DEFINFO_H */
