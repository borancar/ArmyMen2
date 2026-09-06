/* standin.cpp -- NOT reconstructions. The CRT modules call the CRT's own
 * heap (malloc 0x004647F8, calloc 0x0046C18E, free 0x004646A9) and, when
 * startup cannot get memory, _amsg_exit (0x004665B6). None of those is read
 * yet; these forward to the host so the modules that need them link, and
 * are deleted as each is reconstructed. Nothing here is verified against
 * anything, which is why it is one file with this name. */
#include "crt.h"
#include "../../inject/win32.h"
#include <stdlib.h>

void *__cdecl crt_malloc(uint32_t n) { return malloc(n); }
void *__cdecl crt_calloc(uint32_t count, uint32_t size) { return calloc(count, size); }
void  __cdecl crt_free(void *p) { free(p); }
void  __cdecl crt_amsg_exit(int32_t code) { (void)code; abort(); }

int32_t __cdecl crt_compare_string_a(uint32_t lcid, uint32_t flags, const char *a, int32_t na,
                                     const char *b, int32_t nb, uint32_t codepage)
{
    (void)codepage;
    return CompareStringA(lcid, flags, a, na, b, nb);
}

int32_t __cdecl crt_wtomb_environ(void) { return -1; }
