#ifndef AM2_PACKKEY_H
#define AM2_PACKKEY_H

#include <stdint.h>
#include "../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* A packed 26-bit key built from three fields, used throughout the map code.
 *
 *   bit  25 .. 19   field A   7 bits    PackKey's first argument
 *   bit  18 .. 17   unused
 *   bit  16 ..  7   field B  10 bits    second argument
 *   bit   6 ..  0   field C   7 bits    third argument
 *
 * The two-bit gap is real, not a mistake in reading it. PackKey shifts the
 * first argument left by 12 and then the whole thing by 7, which leaves room
 * for twelve bits of B, but every reader masks only ten (`& 0x3FF`). So either
 * B is guaranteed below 1024 by its callers, or values above that are silently
 * truncated on the way back out. Worth remembering if a map ever comes back
 * wrong: it is exactly the kind of latent limit that would only show up on a
 * large map.
 *
 * What the three fields *mean* is not established, so they are named
 * structurally. The widths are suggestive -- 7 bits is 0..127, which would suit
 * a tile coordinate -- but that is not evidence and nothing here depends on it.
 */

/* Original: 0x00433810. */
uint32_t __cdecl PackKey(uint32_t a, uint32_t b, uint32_t c);

/* Originals: 0x00433830, 0x00433840, 0x00433850. */
uint32_t __cdecl KeyFieldA(uint32_t key);
uint32_t __cdecl KeyFieldB(uint32_t key);
uint32_t __cdecl KeyFieldC(uint32_t key);

int packkey_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PACKKEY_H */
