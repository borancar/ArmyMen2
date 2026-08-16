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

int objtype_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_OBJ_IS_ITEM, ObjIsItem, "ObjIsItem", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE2, ObjIsType2, "ObjIsType2", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE3, ObjIsType3, "ObjIsType3", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE238, ObjIsTypeIn238, "ObjIsTypeIn238", 1);
    return rc;
}
