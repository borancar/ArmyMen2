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

int savetag_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SAVETAG_H */
