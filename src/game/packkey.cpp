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

typedef struct { uint32_t key; int32_t value; } AM2_KeyEntry;

#define g_keyTableN (*(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT)
#define g_keyTable  (*(const AM2_KeyEntry **)(uintptr_t)ADDR_KEY_TABLE)

int32_t __cdecl KeyLookup(uint32_t key)
{
    int32_t lo = 0;
    int32_t hi = g_keyTableN;

    while (hi > lo) {
        int32_t mid = lo + ((hi - lo) >> 1);

        if (g_keyTable[mid].key == key)
            return g_keyTable[mid].value;
        if (g_keyTable[mid].key > key)
            hi = mid;
        else
            lo = mid + 1;
    }
    return -1;
}

int32_t __cdecl KeyLookupTriple(uint32_t a, uint32_t b, uint32_t c)
{
    /* The original computes this inline rather than calling PackKey; the
     * arithmetic is identical, overflow included. */
    return KeyLookup(PackKey(a, b, c));
}

int packkey_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_KEY_LOOKUP, (const void *)KeyLookup, "KeyLookup", 1);
    rc |= patch_replace(ADDR_KEY_LOOKUP_TRIPLE, (const void *)KeyLookupTriple,
                  "KeyLookupTriple", 3);
    rc |= patch_replace(ADDR_PACK_KEY, (const void *)PackKey, "PackKey", 3);
    rc |= patch_replace(ADDR_KEY_FIELD_A, (const void *)KeyFieldA, "KeyFieldA", 1);
    rc |= patch_replace(ADDR_KEY_FIELD_B, (const void *)KeyFieldB, "KeyFieldB", 1);
    rc |= patch_replace(ADDR_KEY_FIELD_C, (const void *)KeyFieldC, "KeyFieldC", 1);
    return rc;
}
