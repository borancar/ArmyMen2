/* standin.cpp -- NOT reconstructions. What the CRT modules reach that is
 * not read: the locale layer's wrappers over CompareStringA,
 * GetStringTypeA and LCMapStringA, and the wide-environment conversion
 * this ANSI image never needs. Each reaches the kernel32 call its original
 * would with the arguments it would, and nothing more. Nothing here is
 * verified against anything, which is why it is one file with this name. */
#include "crt.h"
#include "../../inject/win32.h"
#include <stdlib.h>

int32_t __cdecl crt_get_string_type_a(uint32_t info, const char *src, int32_t n, uint16_t *out,
                                      uint32_t codepage, uint32_t lcid, int32_t error)
{
    (void)codepage; (void)error;
    return GetStringTypeA(lcid, info, src, n, out);
}

int32_t __cdecl crt_lcmapstring_a(uint32_t lcid, uint32_t flags, const char *src, int32_t n,
                                  char *dst, int32_t dstn, uint32_t codepage, int32_t error)
{
    (void)codepage; (void)error;
    return LCMapStringA(lcid, flags, src, n, dst, dstn);
}

int32_t __cdecl crt_compare_string_a(uint32_t lcid, uint32_t flags, const char *a, int32_t na,
                                     const char *b, int32_t nb, uint32_t codepage)
{
    (void)codepage;
    return CompareStringA(lcid, flags, a, na, b, nb);
}

int32_t __cdecl crt_wtomb_environ(void) { return -1; }
