/* script.cpp -- the mission-script interpreter.
 *
 * The game ships its missions as readable text under data/<map>/, so this is
 * the one subsystem whose names come from the program's own vocabulary rather
 * than from us. docs/scripttokens.md lists all 185 keywords and the seven
 * token kinds; orig.h carries the call chain from LoadLevelScript down.
 */
#ifndef AM2_SCRIPT_H
#define AM2_SCRIPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x0043EEE0. The id for a keyword, or -1.
 *
 * A linear walk of the 185-entry table at 0x00487C90, comparing with an inlined
 * strcmp. Linear rather than sorted or hashed, and the table is not in id
 * order, so nothing cleverer is available without changing behaviour on a tie
 * -- and there are ties: `mine` and `yours` are both id 88.
 *
 * The comparison is case-SENSITIVE and every entry in the table is lower case.
 * The caller lower-cases the word first -- _strlwr at 0x0046D7D6, whose ASCII
 * path is `cmp 0x41 / cmp 0x5A / add 0x20` -- which is why the scripts can
 * write `Pad`, `pad` and `PAD` interchangeably and all three resolve. */
int32_t __cdecl ScriptLookupToken(const char *word);

int script_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SCRIPT_H */
