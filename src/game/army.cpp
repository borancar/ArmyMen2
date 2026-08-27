/* army.cpp -- see army.h. */
#include <stdint.h>

#include "army.h"
#include "objtable.h"
#include "item.h"     /* DamageObject -- reconstructed */
#include "misc.h"      /* ListRemoveAt */
#include "msgslot.h"   /* CommMustBroadcast */
#include "objtype.h"   /* ObjType2Field548 */
#include "crt.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "armymsg.h"  /* DamageBroadcast */

#define g_allyMatrix   ((int32_t *)(uintptr_t)ADDR_ALLY_MATRIX)
#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_mpSession    (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)
#define g_ourLeaderUid (*(uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID)
#define g_armyObjLists ((void **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)


/* Reached by offset rather than through a struct, as item.cpp does: only a
 * handful of an object's fields have names and none of these are near the
 * front. */
/* `type` and `owner` come from objtable.h's AM2_Object; only the two far
 * fields need offsets. OBJ_OFF_OWNER is NOT reused -- orig.h already has that
 * name on 0x04 of a different record. */
/* OBJ_OFF_MP_ROLE now lives in orig.h -- item.cpp needs it too. */
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
            ListRemoveAt(g_armyObjLists[army], i);
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

#define g_difficulty     (*(int32_t *)(uintptr_t)ADDR_DIFFICULTY)
#define g_levelAttempt   (*(const int32_t *)(uintptr_t)ADDR_LEVEL_ATTEMPT)
/* Both constants come out of the image rather than being written here: same
 * bits, and nothing to mistype. The table is FLOATS and the share is a
 * DOUBLE, which is what `fmul dword` and `fmul qword` say. */
#define g_difficultyScale ((const float *)AM2_IMAGE(ADDR_DIFFICULTY_SCALE))
#define g_enemyHealthShare (*(const double *)AM2_IMAGE(ADDR_ENEMY_HEALTH_SHARE))

void __cdecl SetMaxHealth(void *obj, int32_t amount)
{
    AM2_Object *o   = (AM2_Object *)obj;
    int16_t    *max = (int16_t *)((uint8_t *)o + OBJ_OFF_MAX_HEALTH);
    int32_t     scaled;
    int32_t     floor;

    if (*max > AM2_MAX_HEALTH_CAP)
        return;

    if (g_mpSession) {
        *max = (int16_t)amount;
        return;
    }

    if (o->owner == (int32_t)g_defaultOwner) {
        *max = (int16_t)(int32_t)((long double)amount
                                  * (long double)g_difficultyScale[g_difficulty]);
        return;
    }

    /* Hard leaves an enemy's health exactly as the caller passed it -- which
     * is to say, untouched: this writes nothing at all. */
    if (g_difficulty > 1)
        return;

    scaled = (int32_t)((long double)amount * (long double)g_enemyHealthShare);
    floor  = amount - AM2_HEALTH_PER_ATTEMPT
                      * (g_levelAttempt / (g_difficulty * 2 + 2));
    *max = (int16_t)(scaled > floor ? scaled : floor);
}

typedef void (__cdecl *AM2_Type238ActionFn)(void *obj, int32_t arg);
#define orig_type238_action \
    (*(AM2_Type238ActionFn)AM2_IMAGE(ADDR_TYPE238_ACTION))

void __cdecl SetLeadsAndAct(void *obj)
{
    *(int32_t *)((uint8_t *)obj + OBJ_OFF_LEADS) = 1;
    orig_type238_action(obj, AM2_LEADS_ACTION_ARG);
}

void *__cdecl ListFirstObj(const void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (!obj)
        return 0;
    if (*(const int32_t *)(o + VEHICLE_OFF_PTR_LIST + 4) < 1)
        return 0;
    return LookupByUID(**(const uint32_t **)(o + VEHICLE_OFF_PTR_LIST + 8));
}

uint32_t __cdecl ListFirstField548(const void *obj)
{
    return ObjType2Field548((const AM2_Object *)ListFirstObj(obj));
}

typedef int32_t (__cdecl *AM2_SeatBlockedFn)(int32_t seat, void *vehicle);
typedef void (__cdecl *AM2_DropOccupantFn)(void *vehicle, void *occupant);
typedef void (__cdecl *AM2_GuardedActionFn2)(void *obj, int32_t amount,
                                             int32_t kind, uint32_t uid,
                                             int32_t a, int32_t b);
#define orig_seat_blocked \
    (*(AM2_SeatBlockedFn)AM2_IMAGE(ADDR_VEHICLE_SEAT_BLOCKED))
#define orig_drop_occupant \
    (*(AM2_DropOccupantFn)AM2_IMAGE(ADDR_VEHICLE_DROP_OCCUPANT))

void __cdecl ExitAllFromVehicle(void *vehicle, uint32_t damageOwner)
{
    uint8_t *v = (uint8_t *)vehicle;
    int32_t  seat;

    for (seat = *(const int32_t *)(v + VEHICLE_OFF_PTR_LIST + 4) - 1;
         seat >= 0; seat--) {
        const uint32_t *uids =
            *(const uint32_t **)(v + VEHICLE_OFF_PTR_LIST + 8);
        void    *rider = LookupByUID(uids[seat]);
        int32_t  kind;

        if (!orig_seat_blocked(seat, vehicle)
            && (!g_mpSession
                || CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                     (int16_t)((const AM2_Object *)v)->owner))) {
            /* Looked up a SECOND time, from a freshly re-read list pointer,
             * before the slot is removed. The original does not reuse the
             * pointer it already has. */
            uint8_t *out = (uint8_t *)LookupByUID(
                (*(const uint32_t **)(v + VEHICLE_OFF_PTR_LIST + 8))[seat]);

            ListRemoveAt(v + VEHICLE_OFF_PTR_LIST, seat);
            if (out) {
                *(int32_t *)(out + OBJ_OFF_RIDING) = 0;
                if (CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                      (int16_t)((const AM2_Object *)v)->owner))
                    orig_drop_occupant(vehicle, out);
            }
        }

        kind = *(const int32_t *)(v + VEHICLE_OFF_KIND);
        if (kind == 3 || kind == 2 || !rider)
            continue;

        {
            AM2_Object *dmg   = (AM2_Object *)LookupByUID(damageOwner);
            int32_t     army  = dmg ? (int32_t)dmg->owner : AM2_ARMY_ALL;

            if (g_mpSession
                && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                      (int16_t)army)) {
                am2_log("ExitAllFromVehicle: I was killed in a vehicle, "
                        "damage owner not me\n");
                DamageBroadcast(rider, damageOwner,
                                      AM2_VEHICLE_DEATH_DAMAGE,
                                      AM2_VEHICLE_DEATH_KIND,
                                      (const uint8_t *)rider + OBJ_OFF_POS, 0);
                DamageObject(rider, AM2_VEHICLE_DEATH_DAMAGE,
                                    AM2_VEHICLE_DEATH_KIND, damageOwner, 0, 1);
            } else {
                am2_log("ExitAllFromVehicle: I was killed in a vehicle, "
                        "damage owner is me\n");
                DamageObject(rider, AM2_VEHICLE_DEATH_DAMAGE,
                                    AM2_VEHICLE_DEATH_KIND, damageOwner, 0, 0);
            }
        }
    }
}

int army_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_EXIT_ALL_FROM_VEHICLE,
                        (const void *)ExitAllFromVehicle,
                        "ExitAllFromVehicle", 2);
    rc |= patch_replace(ADDR_SET_LEADS_AND_ACT, (const void *)SetLeadsAndAct,
                        "SetLeadsAndAct", 1);
    rc |= patch_replace(ADDR_LIST_FIRST_OBJ, (const void *)ListFirstObj,
                        "ListFirstObj", 1);
    rc |= patch_replace(ADDR_LIST_FIRST_FIELD548, (const void *)ListFirstField548,
                        "ListFirstField548", 1);
    rc |= patch_replace(ADDR_ALLY_FLAG, (const void *)AllyFlag, "AllyFlag", 2);
    rc |= patch_replace(ADDR_ARMIES_ALLIED, (const void *)ArmiesAllied,
                        "ArmiesAllied", 2);
    rc |= patch_replace(ADDR_OBJ_IS_FRIENDLY, (const void *)ObjIsFriendly,
                        "ObjIsFriendly", 1);
    rc |= patch_replace(ADDR_LOOKUP_OWNER_OBJ, (const void *)LookupOwnerObj,
                        "LookupOwnerObj", 1);
    rc |= patch_replace(ADDR_SET_MAX_HEALTH, (const void *)SetMaxHealth,
                        "SetMaxHealth", 2);
    rc |= patch_replace(ADDR_FOR_EACH_ARMY_OBJECT,
                        (const void *)ForEachArmyObject, "ForEachArmyObject", 2);
    return rc;
}
