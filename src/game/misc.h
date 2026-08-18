/* misc.cpp -- small pure helpers that belong to no cluster yet.
 *
 * Each is reconstructed and verified against the original; what they are FOR
 * is in most cases not established, so they are named for what they compute.
 * When the surrounding translation unit is taken whole they should move to it.
 */
#ifndef AM2_MISC_H
#define AM2_MISC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x0045AFA0. The dword at +0x53C. */
uint32_t __cdecl Field53C(const void *p);

/* 0x0045F440. Adds `add` to the low byte of `base`.
 *
 * The saturation is `or al, 0xFF`, NOT a clamp to 255: a sum of 0x150 comes
 * back as 0x1FF, because only the low byte is forced. Reconstructing this as
 * `min(sum, 255)` would be tidier and would not be this function. The compare
 * is signed, so a negative `add` returns the negative sum untouched. */
int32_t __cdecl AddByteSat(uint32_t base, int32_t add);

/* 0x0043E150. Subtracts the dwords two pointers address, which is the shape of
 * a qsort comparator -- and the difference, not the sign, so it overflows for
 * far-apart values exactly as the original does. */
int32_t __cdecl CompareDword(const void *a, const void *b);

/* 0x00408560. Copies the byte at +0x18 of `src` to `*dst`, but only when the
 * dword at +0x10 is non-zero. The first parameter is read by nothing. */
void __cdecl CopyByteIfSet(uint32_t unused, uint8_t *dst, const void *src);

int misc_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MISC_H */
