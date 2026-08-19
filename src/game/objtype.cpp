/* Object type predicates, reconstructed from ArmyMen2.exe.
 *
 *   ObjIsItem       0x00433860   types 1, 4      33 call sites
 *   ObjIsType2      0x00457470   type 2          47 call sites
 *   ObjIsType3      0x00457490   type 3          28 call sites
 *   ObjIsTypeIn238  0x00457420   types 2, 3, 8   40 call sites
 *
 * All four accept NULL and answer 0 for it, which is why they are used so
 * freely -- callers do not null-check first. See objtype.h for what is and is
 * not established about the type taxonomy.
 */

#include "objtype.h"
#include "../inject/patch.h"

#include <stdint.h>

int32_t __cdecl ObjIsItem(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 1 || obj->type == 4) ? 1 : 0;
}

int32_t __cdecl ObjIsType2(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 2) ? 1 : 0;
}

int32_t __cdecl ObjIsType3(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 3) ? 1 : 0;
}

int32_t __cdecl ObjIsTypeIn238(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    /* The original tests 2 <= t <= 3 first, then 8 -- a range check plus one
     * outlier, which is what a compiler makes of `t == 2 || t == 3 || t == 8`. */
    return (obj->type == 2 || obj->type == 3 || obj->type == 8) ? 1 : 0;
}

/* 0x004574B0 and 0x0045EEB0. The two remaining single-type tests, in the same
 * shape as the three above: null gives 0, otherwise the type word at +0 is
 * compared and the answer normalised to 0 or 1 with sete.
 *
 * These complete the family -- ObjIsTypeIn238 already tested 2, 3 and 8
 * together, and ObjIsItem tests 1 or 4, so every type either has its own
 * predicate now or appears in a combined one. */
int32_t __cdecl ObjIsType8(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 8) ? 1 : 0;
}

int32_t __cdecl ObjIsType4(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 4) ? 1 : 0;
}

uint32_t __cdecl ObjType2Field548(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    if (obj->type != 2)
        return 0;
    return *(const uint32_t *)((const uint8_t *)obj + 0x548);
}

int objtype_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_OBJ_IS_ITEM, (const void *)ObjIsItem, "ObjIsItem", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE2, (const void *)ObjIsType2, "ObjIsType2", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE3, (const void *)ObjIsType3, "ObjIsType3", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE238, (const void *)ObjIsTypeIn238, "ObjIsTypeIn238", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE8, (const void *)ObjIsType8, "ObjIsType8", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE4, (const void *)ObjIsType4, "ObjIsType4", 1);
    rc |= patch_replace(ADDR_OBJ_TYPE2_FIELD548, (const void *)ObjType2Field548,
                        "ObjType2Field548", 1);
    return rc;
}
