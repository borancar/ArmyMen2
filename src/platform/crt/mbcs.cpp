/* mbcs.cpp -- the two multibyte-locale functions the layer above reaches,
 * from the bodies at 0x00469DF7 (_mbctoupper) and 0x0046D189
 * (_mbsnbicoll).
 *
 * Both read tables and codes that _setmbcp fills at startup -- _mbctype,
 * _mbcasemap, __mbcodepage, __mblcid -- and this build runs no such
 * startup, so in the native binary they hold their zero-filled .bss
 * values: _mbctoupper answers its argument unchanged, and _mbsnbicoll
 * compares under code page 0. The hybrid, where the original's startup
 * has run, is where they are compared against the original.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#define crt_mbctype    ((const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCTYPE))
#define crt_mbcasemap  ((const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCASEMAP))
#define crt_mbcodepage (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCODEPAGE))
#define crt_mblcid     (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBLCID))

#define CRT_MB_SBLOW 0x20   /* _mbctype: single-byte lower case */

int32_t __cdecl crt_mbctoupper(int32_t c)
{
    /* The original's other arm, for a two-byte character above 0xFF, maps
     * it through LCMapStringA; nothing here can pass one. */
    if ((uint32_t)c > 0xFF)
        return c;
    if ((crt_mbctype[c + 1] & CRT_MB_SBLOW) == CRT_MB_SBLOW)
        return crt_mbcasemap[c];
    return c;
}

int32_t __cdecl crt_mbsnbicoll(const char *a, const char *b, uint32_t n)
{
    int32_t r;

    if (n == 0)
        return 0;
    r = crt_compare_string_a(crt_mblcid, 1 /* NORM_IGNORECASE */, a, (int32_t)n,
                             b, (int32_t)n, crt_mbcodepage);
    if (r == 0)
        return 0x7FFFFFFF;
    return r - 2;
}
