/* standin.cpp -- NOT reconstructions. What the CRT modules reach that is
 * not read yet: _amsg_exit (0x004665B6), which ends the process with a
 * runtime-error message, and two locale-layer internals named below. These
 * forward to the host so the modules that need them link, and are deleted
 * as each is reconstructed. Nothing here is verified against anything,
 * which is why it is one file with this name. */
#include "crt.h"
#include "../../inject/win32.h"
#include <stdlib.h>

void  __cdecl crt_amsg_exit(int32_t code) { (void)code; abort(); }

int32_t __cdecl crt_compare_string_a(uint32_t lcid, uint32_t flags, const char *a, int32_t na,
                                     const char *b, int32_t nb, uint32_t codepage)
{
    (void)codepage;
    return CompareStringA(lcid, flags, a, na, b, nb);
}

int32_t __cdecl crt_wtomb_environ(void) { return -1; }
