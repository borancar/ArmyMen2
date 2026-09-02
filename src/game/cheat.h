/* cheat.h -- the typed-cheat console.
 *
 * The game's debug console reads a line, matches it against a table of
 * phrases in the image, and runs one of thirty-nine arms.  Anything it does
 * not recognise is handed to the script-line runner, so `trigger greenwins`
 * typed at the console does what the same line does in a mission file.
 *
 * NOTHING ON ANY DRIVE THIS PROJECT HAS TYPES A CHEAT, so every A/B over this
 * is a no-regression check and nothing more.  What stands behind it instead
 * is that the whole function was DECODED rather than read: the arms, the
 * dispatch table, the phrase table's bounds and every callee's convention.
 * Four claims made by reading a subset of the arms were wrong and each was
 * corrected by decoding all of them; see the block above ADDR_CHEAT_ENTRY. */
#ifndef AM2_CHEAT_H
#define AM2_CHEAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x00417B80. Match one typed line against the cheat table and run its arm. */
void __cdecl CheatLine(const char *line);

int cheat_install(void);

#ifdef __cplusplus
}
#endif

#endif
