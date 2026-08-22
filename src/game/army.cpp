/* army.cpp -- see army.h. */
#include <stdint.h>

#include "army.h"
#include "objtable.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define g_allyMatrix   ((int32_t *)(uintptr_t)ADDR_ALLY_MATRIX)
#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_mpSession    (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)
#define g_ourLeaderUid (*(uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID)
#define g_armyObjLists ((void **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)

typedef void (__attribute__((thiscall)) *AM2_ListRemoveAtFn)(void *list,
                                                             int32_t i);
#define orig_list_remove_at \
    (*(AM2_ListRemoveAtFn)AM2_IMAGE(ADDR_LIST_REMOVE_AT))

/* Reached by offset rather than through a struct, as item.cpp does: only a
 * handful of an object's fields have names and none of these are near the
 * front. */
/* `type` and `owner` come from objtable.h's AM2_Object; only the two far
 * fields need offsets. OBJ_OFF_OWNER is NOT reused -- orig.h already has that
 * name on 0x04 of a different record. */
#define OBJ_OFF_MP_ROLE  0x544u   /* 7 is the one value anything tests for */
#define OBJ_OFF_LEADS    0x548u   /* ObjType2Field548 reads the same dword */
#define OBJ_FLAG_SKIP    0x04u
#define AM2_ARMY_ALL     4        /* the value that is allied with everybody */
#define AM2_ARMY_COUNT   4
#define AM2_OBJ_TYPE_2   2

int32_t __attribute__((stdcall)) AllyFlag(int32_t a, int32_t b)
{
    return g_allyMatrix[a * AM2_ARMY_COUNT + b];
}

int32_t __cdecl ArmiesAllied(int32_t a, int32_t b)
{
    if (a == AM2_ARMY_ALL || b == AM2_ARMY_ALL)
        return 1;
    /* A tail call in the original, so whatever the matrix holds is the answer
     * -- not normalised to 0 or 1. */
    return AllyFlag(a, b);
}

int32_t __cdecl ObjIsFriendly(const void *obj)
{
    const AM2_Object *o = (const AM2_Object *)obj;
    int32_t           owner;

    if (g_mpSession && obj && o->type == AM2_OBJ_TYPE_2
        && *(const int32_t *)((const uint8_t *)o + OBJ_OFF_MP_ROLE) == 7)
        return 0;

    /* The null test above guards only the block, and this read is on the path
     * a null argument takes. The original does the same. */
    owner = o->owner;
    if ((uint8_t)owner == AM2_ARMY_ALL)
        return 1;
    if (owner == (int32_t)g_defaultOwner)
        return 1;
    /* `neg`/`sbb`/`neg` in the original: the matrix entry normalised to 0 or
     * 1, which ArmiesAllied above does NOT do. */
    return AllyFlag(owner, (int32_t)g_defaultOwner) != 0;
}

void *__cdecl LookupOwnerObj(uint32_t owner)
{
    void   **list;
    int32_t  i;

    int32_t army = (int32_t)owner;

    if (army == (int32_t)g_defaultOwner)
        return LookupByUID(g_ourLeaderUid);
    if (army < 0 || army >= AM2_ARMY_COUNT)
        return 0;

    list = (void **)g_armyObjLists[army];
    for (i = 0; i < *(const int32_t *)((uint8_t *)list + LIST_OFF_COUNT); i++) {
        const uint32_t *uids =
            *(const uint32_t **)((uint8_t *)list + LIST_OFF_UIDS);
        AM2_Object *obj = (AM2_Object *)LookupByUID(uids[i]);

        if (obj && obj->type == AM2_OBJ_TYPE_2
            && *(int32_t *)((uint8_t *)obj + OBJ_OFF_LEADS))
            return obj;
        /* The list pointer is re-read from the global every iteration, as the
         * original does; nothing here can move it. */
        list = (void **)g_armyObjLists[army];
    }
    return 0;
}

void __cdecl ForEachArmyObject(int32_t army, void(__cdecl *fn)(void *obj))
{
    void    *list = g_armyObjLists[army];
    int32_t  i    = 0;

    /* No range check on `army` here, where ArmyLeader has one. */
    while (i < *(const int32_t *)((uint8_t *)list + LIST_OFF_COUNT)) {
        const uint32_t *uids =
            *(const uint32_t **)((uint8_t *)list + LIST_OFF_UIDS);
        AM2_Object     *obj = (AM2_Object *)LookupByUID(uids[i]);
        uint8_t        *raw = (uint8_t *)obj;

        if (!obj) {
            /* Gone: drop it and do NOT advance, since everything after it has
             * just moved down one. */
            orig_list_remove_at(g_armyObjLists[army], i);
            list = g_armyObjLists[army];
            continue;
        }
        if (!(*(uint8_t *)(raw + OBJ_OFF_FLAGS) & OBJ_FLAG_SKIP)
            && !(g_mpSession && obj->type == AM2_OBJ_TYPE_2
                 && *(int32_t *)(raw + OBJ_OFF_MP_ROLE) == 7))
            fn(obj);
        i++;
        list = g_armyObjLists[army];
    }
}

int army_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ALLY_FLAG, (const void *)AllyFlag, "AllyFlag", 2);
    rc |= patch_replace(ADDR_ARMIES_ALLIED, (const void *)ArmiesAllied,
                        "ArmiesAllied", 2);
    rc |= patch_replace(ADDR_OBJ_IS_FRIENDLY, (const void *)ObjIsFriendly,
                        "ObjIsFriendly", 1);
    rc |= patch_replace(ADDR_LOOKUP_OWNER_OBJ, (const void *)LookupOwnerObj,
                        "LookupOwnerObj", 1);
    rc |= patch_replace(ADDR_FOR_EACH_ARMY_OBJECT,
                        (const void *)ForEachArmyObject, "ForEachArmyObject", 2);
    return rc;
}
