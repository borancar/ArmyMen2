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
/* OBJ_OFF_SOLDIER_KIND now lives in orig.h -- item.cpp needs it too. */
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
        && *(const int32_t *)((const uint8_t *)o + OBJ_OFF_SOLDIER_KIND) == 7)
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
                 && *(int32_t *)(raw + OBJ_OFF_SOLDIER_KIND) == 7))
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

void __cdecl SetLeadsAndAct(void *obj)
{
    *(int32_t *)((uint8_t *)obj + OBJ_OFF_LEADS) = 1;
    Type238Action(obj, AM2_LEADS_ACTION_ARG);
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

/* 0x004574D0, eleven callers. Whether two objects are on the same side.
 *
 * It is AllyFlag above with four exceptions layered over it, and the layering
 * order is the function: each exception answers outright rather than adjusting
 * what comes after.
 *
 * KIND 7 IS ALLIED ONLY TO KIND 7, and only in a multiplayer session. If the
 * FIRST object is a type 2 whose OBJ_OFF_SOLDIER_KIND is 7, the answer is
 * whether the second is one too -- nothing else is consulted, not even the
 * neutral army. That is the same exception ObjIsFriendly makes one object at a
 * time, but with the opposite sense for a pair: ObjIsFriendly says a kind 7 is
 * never friendly, and this says two of them are friendly to each other.
 *
 * ARMY 4 IS ALLIED TO EVERYTHING, tested on both objects. Note the two reads
 * are not the same width: the first object's army is sign-extended and the
 * second's is compared as a plain byte. Transcribed as written; for the values
 * a game ships it makes no difference, and inventing the symmetry would hide
 * that the original does not have it.
 *
 * SHARING AN OBJECT-TABLE RECORD IS ALLIANCE. The second object's record
 * pointer -- one of two fields, chosen by the third argument and by its
 * OBJ_OFF_FIELD_530 not being 5, which is the same 5 ObjConceal tests -- is
 * compared against the record for the FIRST object's army. Equal means allied.
 * Otherwise the pointer is decoded back to an index by matching it against the
 * four record addresses in turn, and that index goes through CommSlotForArmy
 * into AllyFlag. A pointer that is none of the four ends it: not allied.
 *
 * The decode is written as the four comparisons the original makes rather than
 * as a subtract and shift. A division would accept records 4 through 255 as
 * well, and the original accepts exactly four.
 *
 * BOTH NULL TESTS ARE USELESS AND BOTH ARE REPRODUCED. `if (!a)` jumps to code
 * whose first act is to read a's army; `if (!b)` jumps past the type-2 block
 * to a tail that reads b's army. So neither test decides whether a null is
 * dereferenced, only which dereference happens first. No caller passes null,
 * and the shape is worth leaving visible rather than tidying into a guard the
 * original does not have.
 *
 * The original loads the comm object into ecx before each AllyFlag call, which
 * is stdcall and reads two stack arguments and no `this` -- so those loads are
 * dead. Not written; there is nothing to reproduce.
 *
 * MEASURED AT TWO CALLS on a driven Boot Camp mission, and the two exceptions
 * that matter most are NOT among what those two cover: ADDR_MP_SESSION is 0 on
 * every drive this project has, so both kind-7 arms are unreachable here and
 * are verified by reading. What the two calls do reach is the army-4 pair, the
 * record comparison and the tail. Landing this also took AllyFlag's counter to
 * 0, since it is now called by name from here and from ArmiesAllied, which
 * reads 104 on the same run.
 */
int32_t __cdecl ObjsAreAllied(void *a, void *b, int32_t useRec3)
{
    const uint8_t *oa   = (const uint8_t *)a;
    const uint8_t *ob   = (const uint8_t *)b;
    void          *comm = *(void **)(uintptr_t)ADDR_COMM_OBJECT;
    int32_t        mp   = *(const int32_t *)(uintptr_t)ADDR_MP_SESSION;
    const uint8_t *rec;
    int32_t        army, idx;

    if (mp && a && *(const int32_t *)oa == 2
        && *(const int32_t *)(oa + OBJ_OFF_SOLDIER_KIND) == 7)
        return (b && *(const int32_t *)ob == 2
                && *(const int32_t *)(ob + OBJ_OFF_SOLDIER_KIND) == 7);

    army = *(const int8_t *)(oa + OBJ_OFF_ARMY);
    if (army == 4)
        return 1;
    if (*(const uint8_t *)(ob + OBJ_OFF_ARMY) == 4)
        return 1;

    if (b && *(const int32_t *)ob == 2) {
        if (mp && *(const int32_t *)(ob + OBJ_OFF_SOLDIER_KIND) == 7)
            return 0;

        rec = (useRec3
               && *(const int32_t *)(ob + OBJ_OFF_FIELD_530) != 5)
              ? *(const uint8_t *const *)(ob + SAVED_OFF_TABLE_REC3)
              : *(const uint8_t *const *)(ob + SAVED_OFF_TABLE_REC2);

        if ((const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS
                + (uint32_t)(CommArmyOfSlot(comm, army) << 8)) == rec)
            return 1;

        if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS))
            idx = 0;
        else if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x100))
            idx = 1;
        else if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x200))
            idx = 2;
        else if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x300))
            idx = 3;
        else
            return 0;

        if (AllyFlag(army, CommSlotForArmy(comm, idx)))
            return 1;
    }

    return AllyFlag(army, *(const int8_t *)(ob + OBJ_OFF_ARMY));
}

/* 0x00457620, six callers. The same question as ObjsAreAllied with an ARMY on
 * the left instead of an object.
 *
 * IT IS ObjsAreAllied FROM THE `army == 4` TEST ONWARD, instruction for
 * instruction: the neutral-army pair, the type-2 block with its multiplayer
 * kind-7 refusal, the record comparison, the four-way decode, and the same
 * AllyFlag tail. The only difference is that the army arrives as an argument
 * rather than being read out of a first object -- and as an int32 rather than
 * sign-extended from a byte, which is the one place the two bodies could
 * disagree and do not for any army a game ships.
 *
 * So ObjsAreAllied is this function with the kind-7 PAIR rule in front of it.
 * The image holds two bodies and not a call, so this reconstruction holds two
 * as well; the alternative -- having ObjsAreAllied call this -- would produce
 * the same answers and hide a duplication the original has. What that costs is
 * that a correction to one must be made to the other, and no check here would
 * notice. Said in both comments for that reason.
 *
 * Its own null test is the same useless one: `if (!b)` jumps to a tail whose
 * first act is to read b's army, and b has already been dereferenced above it.
 * Reproduced.
 *
 * MEASURED AT 80 CALLS on a driven Boot Camp mission against ObjsAreAllied's
 * 1, so the shared logic is far better exercised through this door than
 * through that one. Be careful what that buys: it catches a transcription
 * error made in ONE of the two bodies, which is the likely kind, and it
 * cannot catch one made in both -- and since the second was written by reading
 * the first, both is exactly how a misreading would land. The multiplayer
 * kind-7 refusal is unreachable here as it is there, ADDR_MP_SESSION being 0
 * on every drive.
 */
int32_t __cdecl ArmyAlliedWithObj(int32_t army, void *b, int32_t useRec3)
{
    const uint8_t *ob   = (const uint8_t *)b;
    void          *comm = *(void **)(uintptr_t)ADDR_COMM_OBJECT;
    int32_t        mp   = *(const int32_t *)(uintptr_t)ADDR_MP_SESSION;
    const uint8_t *rec;
    int32_t        idx;

    if (army == 4)
        return 1;
    if (*(const uint8_t *)(ob + OBJ_OFF_ARMY) == 4)
        return 1;

    if (b && *(const int32_t *)ob == 2) {
        if (mp && *(const int32_t *)(ob + OBJ_OFF_SOLDIER_KIND) == 7)
            return 0;

        rec = (useRec3
               && *(const int32_t *)(ob + OBJ_OFF_FIELD_530) != 5)
              ? *(const uint8_t *const *)(ob + SAVED_OFF_TABLE_REC3)
              : *(const uint8_t *const *)(ob + SAVED_OFF_TABLE_REC2);

        if ((const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS
                + (uint32_t)(CommArmyOfSlot(comm, army) << 8)) == rec)
            return 1;

        if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS))
            idx = 0;
        else if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x100))
            idx = 1;
        else if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x200))
            idx = 2;
        else if (rec == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x300))
            idx = 3;
        else
            return 0;

        if (AllyFlag(army, CommSlotForArmy(comm, idx)))
            return 1;
    }

    return AllyFlag(army, *(const int8_t *)(ob + OBJ_OFF_ARMY));
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
    rc |= patch_replace(ADDR_OBJS_ARE_ALLIED, (const void *)ObjsAreAllied,
                        "ObjsAreAllied", 11);
    rc |= patch_replace(ADDR_ARMY_ALLIED_WITH_OBJ,
                        (const void *)ArmyAlliedWithObj,
                        "ArmyAlliedWithObj", 6);
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
