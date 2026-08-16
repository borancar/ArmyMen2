/* The global object registry -- reconstructed from ArmyMen2.exe.
 *
 *   FindSlot     0x004277A0   binary search by UID
 *   LookupByUID  0x00427820   UID -> object pointer
 *
 * Every gameplay subsystem addresses objects by a 32-bit UID: the recovered
 * debug messages are full of "uid=%x" (items, units, vehicles, events), and
 * this table is what turns one back into a pointer. LookupByUID is the hottest
 * game function measured in Boot Camp -- 3,098 calls in the opening seconds.
 *
 * Layout, from the `lea r,[i+i*2]` then `*4` addressing used throughout
 * (index * 12):
 *
 *      +0  uint32_t  uid       search key, kept sorted ascending
 *      +4  void     *obj       the object itself; may be NULL
 *      +8  uint32_t  serial    stamped from the counter at 0x0051308C
 *
 * Supporting globals, confirmed by their use elsewhere in the same band:
 *      0x00514F0C  table base    (set to NULL together with the count on reset)
 *      0x00514F04  entry count   (zeroed on reset at 0x0042949E)
 *      0x00514F00  capacity      (grown in steps of 100 at 0x00429879)
 *
 * UIDs are compared with unsigned branches (ja/jae), so the key is unsigned
 * even though the index arithmetic is signed.
 */

#include "objtable.h"
#include "../inject/patch.h"

#include <stdint.h>

int32_t __cdecl FindSlot(uint32_t uid, int32_t *insert_at)
{
    const AM2_ObjEntry *tab = g_objTable;
    int32_t count = g_objCount;
    int32_t lo = 0, hi = count, mid = 0;

    /* Guarded do/while: with an empty table `mid` stays 0 and the tail below
     * reports insertion position 0, which is what the original does. */
    if (hi > 0) {
        do {
            mid = lo + (hi - lo) / 2;
            if (tab[mid].uid == uid)
                return mid;
            if (tab[mid].uid >= uid)
                hi = mid;
            else
                lo = mid + 1;
        } while (hi > lo);
    }

    /* Not found. Report where it would belong, so callers that are inserting
     * do not have to search twice. Three separate exits in the original,
     * distinguished by where the search landed. */
    if (mid >= count)
        *insert_at = count;
    else if (tab[mid].uid < uid)
        *insert_at = mid + 1;
    else
        *insert_at = mid;

    return -1;
}

void *__cdecl LookupByUID(uint32_t uid)
{
    int32_t slot;
    int32_t i;

    /* The original passes the address of its own `uid` argument slot as the
     * out-param, reusing one stack dword for both -- safe because FindSlot
     * reads the key into a register before it ever writes through the pointer.
     * The same trick appears in CheckSaveTag. Kept separate here; the
     * observable behaviour is identical. */
    slot = 0;
    i = FindSlot(uid, &slot);
    if (i < 0)
        return 0;

    return g_objTable[i].obj;
}

int objtable_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_FIND_SLOT, FindSlot, "FindSlot", 2);
    rc |= patch_replace(ADDR_LOOKUP_BY_UID, LookupByUID, "LookupByUID", 1);
    return rc;
}
