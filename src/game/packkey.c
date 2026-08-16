/* Packed map key, reconstructed from ArmyMen2.exe.
 *
 *   PackKey     0x00433810   39 call sites
 *   KeyFieldA   0x00433830    6 call sites
 *   KeyFieldB   0x00433840    6 call sites
 *   KeyFieldC   0x00433850    1 call site
 *
 * See packkey.h for the bit layout and the field-width discrepancy between the
 * packer and the readers.
 */

#include "packkey.h"
#include "../inject/patch.h"

#include <stdint.h>

uint32_t __cdecl PackKey(uint32_t a, uint32_t b, uint32_t c)
{
    /* Written as the original computes it -- ((a << 12) + b) << 7) + c --
     * rather than as an or of three shifted fields. They agree whenever the
     * fields are in range, and this way the overflow behaviour matches too. */
    return (((a << 12) + b) << 7) + c;
}

uint32_t __cdecl KeyFieldA(uint32_t key)
{
    return (key >> 19) & 0x7F;
}

uint32_t __cdecl KeyFieldB(uint32_t key)
{
    return (key >> 7) & 0x3FF;
}

uint32_t __cdecl KeyFieldC(uint32_t key)
{
    return key & 0x7F;
}

int packkey_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_PACK_KEY, PackKey, "PackKey", 3);
    rc |= patch_replace(ADDR_KEY_FIELD_A, KeyFieldA, "KeyFieldA", 1);
    rc |= patch_replace(ADDR_KEY_FIELD_B, KeyFieldB, "KeyFieldB", 1);
    rc |= patch_replace(ADDR_KEY_FIELD_C, KeyFieldC, "KeyFieldC", 1);
    return rc;
}
