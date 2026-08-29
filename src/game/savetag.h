#ifndef AM2_SAVETAG_H
#define AM2_SAVETAG_H

#include <stdint.h>
#include "../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x004235D0. Returns 1 when the next dword of `fp` equals
 * `expected`, otherwise logs against (file, line) and returns 0. */
int32_t __cdecl CheckSaveTag(am2_FILE *fp, uint32_t expected,
                             const char *file, int32_t line);

/* 0x00423680. The other half: write a four-byte section tag. 46 callers.
 *
 * CheckSaveTag reads into its own `fp` argument slot so that a short read is
 * still compared against something defined; nothing like that is needed here,
 * because the bytes are going out and the value is already in a slot of its
 * own. It is one fwrite and no error check -- a save that cannot write is not
 * noticed here. */
void __cdecl WriteSaveTag(am2_FILE *fp, uint32_t tag);

int savetag_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00428760, two callers. Write a script-name reference into a savegame:
 * the "no name" tag alone, or the name tag, the length and the string. */
int32_t __cdecl SaveScriptName(am2_FILE *fp, const void *rec);

/* 0x004287E0, one caller. The load half of SaveScriptName: read the tag, and
 * for a real name a length and that many bytes, then bind the record. */
int32_t __cdecl LoadScriptName(am2_FILE *fp, void *rec);

#endif /* AM2_SAVETAG_H */
