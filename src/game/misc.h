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

/* 0x0042E4F0. (v / 32 + 1) * 1000, with the division rounding toward zero --
 * the original adds 31 before shifting when v is negative. The *1000 is three
 * lea *5 chains and a shl 3, which is 125 * 8. */
int32_t __cdecl ScaleBy32Blocks(int32_t v);

/* 0x0042E510. Tidies a name in place: '_' becomes ' ', and a lowercase letter
 * is capitalised when it starts the string or follows a space or a period.
 *
 * It recomputes strlen on every iteration, so it is quadratic in the length.
 * That is the original's and is kept -- the strings are short and matching the
 * instruction count is worth more here than the better loop. */
void __cdecl TitleCaseName(char *text);

/* 0x0042F120. Stores -1 through both pointers, then clears three bits of the
 * first: *a ends as 0xFFDBFFFD, *b as 0xFFFFFFFF. Written as two stores and an
 * and, which is what the original does rather than one constant each. */
void __cdecl ResetPairMask(uint32_t *a, uint32_t *b);

/* 0x00435640. The dword at +0 equals 7. No null check, unlike the ObjIsTypeN
 * family, so it is not one of them however much it looks like one. */
int32_t __cdecl IsKind7(const void *p);

/* 0x0043EE80. Space, tab or carriage return. NOT newline -- a reconstruction
 * that reached for isspace() would accept '\n' and this does not. */
int32_t __cdecl IsBlank(uint8_t c);

/* 0x0043EEA0. One of ) ( , < = > { } & +. It lives between the pad.cpp and
 * script.cpp anchors, so these are the script language's delimiters. */
int32_t __cdecl IsScriptDelim(uint8_t c);

/* 0x0041AE90. Reverses the low three bytes and zeroes the fourth: 0x00BBGGRR
 * becomes 0x00RRGGBB. Sits beside the palette expander, so it is the channel
 * order swap between the file's colours and the display's. The second
 * parameter is read by nothing. */
uint32_t __cdecl SwapColourBytes(uint32_t colour, uint32_t unused);

/* Four that do nothing but return. Reconstructed because they are functions in
 * the original and something calls them; there is nothing else to say. */
void     __stdcall NullStub4(uint32_t arg);   /* 0x004170E0, `ret 4` */
void     __cdecl   NullStub(void);            /* 0x0042E170, bare `ret` */
int32_t  __cdecl   ReturnZero(void);          /* 0x0042E980 */
int32_t  __cdecl   ReturnOne(void);           /* 0x004354F0 */

int misc_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MISC_H */
