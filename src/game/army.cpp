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

typedef void (__cdecl *AM2_GuardedActionFn2)(void *obj, int32_t amount,
                                             int32_t kind, uint32_t uid,
                                             int32_t a, int32_t b);

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header because this module is on the flat side of the
 * split and audio.h names Win32 types -- the same reason air.cpp and
 * commmsg.cpp do it, and spelled the same way so the three cannot drift. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int32_t x, int32_t y);

/* BoatExitPoint is reconstructed, in win32/mapdraw.cpp -- it clips with
 * IntersectRect, so it is on the platform side of the split. Declared here
 * rather than by including that header, for the reason script.cpp declares
 * PreloadSprite: army.cpp is flat and must name no Win32 type. This signature
 * has none. */
extern "C" int32_t __cdecl BoatExitPoint(void *vehicle, uint32_t *out);


/* ExitOneFromVehicle -- original 0x0045AC90, four callers. Empty ONE seat:
 * look the occupant up, choose a spot beside the vehicle, unlink the seat, put
 * the occupant on the ground, and move the selection if that emptied it.
 * Answers 1 when somebody came out and 0 otherwise.
 *
 * IT WAS ADDR_VEHICLE_SEAT_BLOCKED, and the address's own comment predicted
 * the correction. ExitAllFromVehicle names its three still-original callees
 * from that one call site, and orig.h says of them: "read their bodies before
 * relying on the names. What is evidenced is only what ExitAllFromVehicle does
 * with them: the first decides whether a seat is emptied at all". This is the
 * first, and it does not decide anything -- it does the emptying. Renamed.
 *
 * FOUR WAYS OUT BEFORE ANY WORK, and each answers 0: a null vehicle; a
 * multiplayer session where CommMustBroadcast refuses this army; a seat index
 * past the count; and a uid that no longer resolves. The middle one means a
 * client cannot empty a seat on its own account -- the host tells it to.
 *
 * WHERE THE OCCUPANT LANDS IS THE ONE INTERESTING CHOICE. A LIVING BOAT --
 * VEHICLE_OFF_KIND 5 with OBJ_OFF_HEALTH still non-zero -- asks
 * ADDR_BOAT_EXIT_POINT, and if that finds nowhere the function plays a sound
 * and refuses, which is the only refusal that makes a noise. Everything else,
 * including a DEAD boat, goes AM2_VEHICLE_EXIT_OFFSET up and left of the
 * vehicle with no check at all. So the boat is the only vehicle that can be
 * too surrounded to get out of, and sinking it removes that protection.
 *
 * The seat list is a sub-list header, so the count and the uid array are
 * SUBREC_OFF_COUNT and SUBREC_OFF_ROWS off VEHICLE_OFF_PTR_LIST rather than
 * two more offsets on the vehicle.
 *
 * THE SELECTION MOVES ONLY IF THE VEHICLE WAS SELECTED and is now empty:
 * SelectUnit on the occupant, DeselectUnit on the vehicle. With seats still
 * occupied it is SetObjContext on the VEHICLE instead, which keeps the
 * selection where it was and just refreshes what the HUD is looking at.
 */
int32_t __cdecl ExitOneFromVehicle(int32_t seat, void *vehicle)
{
    uint8_t  *v = (uint8_t *)vehicle;
    uint8_t  *seats;
    uint8_t  *occupant;
    uint32_t  at;

    if (!vehicle)
        return 0;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              *(const int8_t *)(v + OBJ_OFF_ARMY)))
        return 0;

    seats = v + VEHICLE_OFF_PTR_LIST;
    if (*(const int32_t *)(seats + SUBREC_OFF_COUNT) < seat)
        return 0;

    occupant = (uint8_t *)LookupByUID(
        (*(uint32_t *const *)(seats + SUBREC_OFF_ROWS))[seat]);
    if (!occupant)
        return 0;

    if (*(const int32_t *)(v + VEHICLE_OFF_KIND) == AM2_VEHICLE_KIND_BOAT
        && *(const int16_t *)(v + OBJ_OFF_HEALTH) != 0) {
        if (!BoatExitPoint(v, &at)) {
            PlaySoundAt(3, 0, 0,
                        *(const int16_t *)(v + OBJ_OFF_POS),
                        *(const int16_t *)(v + OBJ_OFF_POS + 2));
            return 0;
        }
    } else {
        at = (uint32_t)(uint16_t)(*(const int16_t *)(v + OBJ_OFF_POS)
                                  - AM2_VEHICLE_EXIT_OFFSET)
             | ((uint32_t)(uint16_t)(*(const int16_t *)(v + OBJ_OFF_POS + 2)
                                     - AM2_VEHICLE_EXIT_OFFSET) << 16);
    }

    *(int32_t *)(occupant + OBJ_OFF_RIDING) = 0;
    ListRemoveAt(seats, seat);

    if (CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                          *(const int8_t *)(v + OBJ_OFF_ARMY)))
        SendVehicleExit(v, occupant);

    DeployItem(occupant, at, 0, 0);

    if (*(const uint32_t *)(v + OBJ_OFF_FLAGS) & OBJ_FLAG_SELECTED) {
        if (*(const int32_t *)(seats + SUBREC_OFF_COUNT) <= 0) {
            SelectUnit(occupant);
            DeselectUnit(v);
            return 1;
        }
        SetObjContext(v);
    }

    return 1;
}

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

        if (!ExitOneFromVehicle(seat, vehicle)
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
                    SendVehicleExit(vehicle, out);
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

/* ObjAttachTo -- original 0x00458070, 640 bytes, twenty callers. Detach an
 * object from whatever it is attached to, and then attach it to a target if
 * one is given. The three per-type destroy handlers pass a NULL target, so for
 * them this is purely a detach.
 *
 * IT WAS READ A WHILE AGO AND DEFERRED FOR A LIST OF PREREQUISITES THAT HAVE
 * ALL SINCE ARRIVED. orig.h said it "would need names for nine fields, three
 * comm methods and four 0x100-byte blocks". Every one of those was named by
 * some other unit in the meantime -- the fields one at a time, the comm
 * queries with misc.cpp, the blocks as ADDR_OBJ_TABLE_RECORDS -- and nobody
 * re-read the note. It needed no new names at all. A decline is worth
 * re-testing against the tree rather than against the reason it was written.
 *
 * TWO CORRECTIONS TO THAT NOTE, both from the body.
 *
 * The stance is 0, 3 or 6 and NOT "0, 3, 6 or 7". Only three instructions
 * store OBJ_OFF_AI_MODE. The 7 is `mov esi, 7`, which is what soldier kind is
 * COMPARED against, and it is never written anywhere.
 *
 * +0xA8 and +0xAC are the SUB-LIST HEADER at OBJ_OFF_PTR_LIST, not the item
 * chain. `lea ecx,[edi+0xa4]` hands ListRemoveAt that header directly, and
 * 0xA4 + SUBREC_OFF_COUNT is 0xA8 and + SUBREC_OFF_ROWS is 0xAC. Spelling
 * them that way dissolves the type-overload the note warned about instead of
 * working around it: for types 2, 3 and 8 this is a list of member uids.
 *
 * THE DETACH LOOP DOES NOT ADVANCE ON A REMOVAL, which is what makes it
 * correct rather than a bug: after ListRemoveAt the following element has
 * shifted down into the same index, so the jump goes to the loop CONDITION and
 * not to the increment. It removes the subject's own uid and any uid that no
 * longer resolves, and renumbers every survivor's OBJ_OFF_FORMATION_SLOT as it
 * passes. Reading it as a break, or as an ordinary for, gets both wrong.
 *
 * THE THREE STANCE VALUES ARE LEFT AS LITERALS ON PURPOSE. 0, 3 and 6 go
 * into OBJ_OFF_AI_MODE, and the script vocabulary this file records elsewhere
 * already calls 6 `attack`; a new name here would be a SECOND name for that
 * value in the same field, which is the alias mistake one level down. Nothing
 * establishes what 3 and 0 mean beyond the branches that write them -- 3 when
 * the two are allied or the same side, 0 when the subject's own army is
 * ADDR_DEFAULT_OWNER -- so they stay numbers until something reads them.
 * Soldier kind 7 is a literal for the same reason.
 *
 * THE ecx LOADS BEFORE AllyFlag ARE DEAD and the prototype is right as it
 * stands. Both sites do `mov ecx, [comm]` immediately before the call, which
 * reads exactly like thiscall -- but AllyFlag is `mov eax,[esp+8];
 * mov ecx,[esp+4]; ...; ret 8`, so ecx is an input to nothing and is clobbered
 * as scratch. Checked in the callee rather than inferred from the call site,
 * which is the only reason a correct declaration was not "corrected". */
void __cdecl ObjAttachTo(void *subject, void *target)
{
    uint8_t *s = (uint8_t *)subject;
    uint8_t *t = (uint8_t *)target;
    void    *comm;
    int32_t  mp;
    uint32_t old;
    int32_t  army;

    if (subject == target || !subject || !ObjIsTypeIn238((const AM2_Object *)s))
        return;

    old = *(const uint32_t *)(s + OBJ_OFF_FOLLOW_UID);
    *(uint32_t *)(s + OBJ_OFF_TARGET_UID) = 0;

    if (old) {
        uint8_t *h = (uint8_t *)LookupByUID(old);

        if (h && ObjIsTypeIn238((const AM2_Object *)h)) {
            uint8_t *list = h + OBJ_OFF_PTR_LIST;
            int32_t  i = 0;

            while (i < *(const int32_t *)(list + SUBREC_OFF_COUNT)) {
                uint32_t uid = (*(uint32_t *const *)
                                (list + SUBREC_OFF_ROWS))[i];
                uint8_t *other;

                if (uid == ((const AM2_Object *)s)->uid) {
                    ListRemoveAt(list, i);
                    continue;           /* the next one shifted into i */
                }

                other = (uint8_t *)LookupByUID(uid);
                if (!other) {
                    ListRemoveAt(list, i);
                    continue;
                }

                if (ObjIsTypeIn238((const AM2_Object *)other))
                    *(int32_t *)(other + OBJ_OFF_FORMATION_SLOT) = i;
                i++;
            }
        }
    }

    if (!target) {
        *(int32_t *)(s + OBJ_OFF_FORMATION_SLOT) = 0;
        *(uint32_t *)(s + OBJ_OFF_FOLLOW_UID) = 0;
        return;
    }

    /* TWO DIFFERENT GLOBALS, and reading them as one was a real mistake in an
     * earlier draft of this. `mov ecx,[0x511da0]` at the top is
     * ADDR_MP_SESSION and it gates both kind-7 arms; the comm object is loaded
     * separately at 0x4581E6 as the `this` for CommArmyOfSlot. ObjsAreAllied
     * four hundred lines up gates the same arms on `mp` too, which is what
     * exposed it -- and NO A/B COULD HAVE: ADDR_MP_SESSION is 0 on every drive
     * this project has, so both arms are unreachable and a wrong global there
     * is invisible. */
    mp   = *(const int32_t *)(uintptr_t)ADDR_MP_SESSION;
    comm = *(void *const *)(uintptr_t)ADDR_COMM_OBJECT;
    army = *(const int8_t *)(s + OBJ_OFF_ARMY);

    if (mp
        && *(const int32_t *)s == AM2_OBJ_TYPE_TROOPER
        && *(const int32_t *)(s + OBJ_OFF_SOLDIER_KIND) == 7) {
        if (*(const int32_t *)t == AM2_OBJ_TYPE_TROOPER
            && *(const int32_t *)(t + OBJ_OFF_SOLDIER_KIND)
               == 7)
            goto attach_and_join;
        goto mode_only;
    }

    /* The subject's army is movsx and the target's is a plain `cmp byte`, so
     * one is signed and the other is not -- ObjsAreAllied spells the pair the
     * same way. It cannot matter at 4, and it is written as the original reads
     * it rather than made uniform. */
    if (army == AM2_ARMY_ALL
        || *(const uint8_t *)(t + OBJ_OFF_ARMY) == AM2_ARMY_ALL)
        goto attach_and_join;

    if (*(const int32_t *)t == AM2_OBJ_TYPE_TROOPER) {
        const uint8_t *kind;
        int32_t        idx;

        if (comm
            && *(const int32_t *)(t + OBJ_OFF_SOLDIER_KIND)
               == 7)
            goto mode_only;

        kind = *(const uint8_t *const *)(t + SAVED_OFF_TABLE_REC2);
        /* Spelled exactly as ObjsAreAllied spells it, four hundred lines up
         * in this same file -- that function is the SAME inlined block and is
         * already verified, so a second spelling here would be a second place
         * to be wrong. */
        if ((const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS
                + (uint32_t)(CommArmyOfSlot(comm, army) << 8)) == kind)
            goto attach_and_join;

        if (kind == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS))
            idx = 0;
        else if (kind == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x100))
            idx = 1;
        else if (kind == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x200))
            idx = 2;
        else if (kind == (const uint8_t *)(uintptr_t)(ADDR_OBJ_TABLE_RECORDS + 0x300))
            idx = 3;
        else
            goto mode_only;

        if (AllyFlag(army, CommSlotForArmy(comm, idx)))
            goto attach_and_join;
    }

    if (AllyFlag(army, *(const int8_t *)(t + OBJ_OFF_ARMY)))
        goto attach_and_join;

mode_only:
    *(int32_t *)(s + OBJ_OFF_AI_MODE) =
        (army == (int32_t)g_defaultOwner)
            ? 0 : 6;
    *(uint32_t *)(s + OBJ_OFF_FOLLOW_UID) =
        ((const AM2_Object *)t)->uid;
    return;

attach_and_join:
    *(int32_t *)(s + OBJ_OFF_AI_MODE) = 3;
    *(uint32_t *)(s + OBJ_OFF_FOLLOW_UID) =
        ((const AM2_Object *)t)->uid;

    if (ObjIsTypeIn238((const AM2_Object *)t)) {
        uint8_t *list = t + OBJ_OFF_PTR_LIST;

        *(int32_t *)(s + OBJ_OFF_FORMATION_SLOT) =
            *(const int32_t *)(list + SUBREC_OFF_COUNT);
        PtrListPush(list, (void *)(uintptr_t)
                    ((const AM2_Object *)s)->uid);
    }
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

/* Three target predicates that share one functions.tsv entry -- 0x00403600,
 * 0x00403660 and 0x004036C0, at 0x60, 0x60 and 0x30 bytes. Patching any one
 * of them would have marked all three reconstructed, so all three are here.
 *
 * ALL THREE OPEN THE SAME WAY on OBJ_FLAG_BIT8: answer yes, without looking
 * at health, the destroyed flag or the army. It is an override, and what sets
 * it is not established -- only that these three agree about it.
 */

/* ObjIsOurs -- original 0x00403600, five callers.
 *
 * Is this object on our side? Its own army against the local player's, and
 * optionally an alliance as well.
 *
 * THE MULTIPLAYER GUARD COMES FIRST AND IS A FLAT REFUSAL. In a session, a
 * type 2 whose soldier kind is 7 is never ours -- not even when the armies
 * match, because the test runs before the comparison. Kind 7 is the one
 * SetSoldierKind gives 1.5x health and a name from its own table, so this is
 * a special unit being held at arm's length. Outside a session the guard does
 * not run at all and such a unit is ours like any other.
 *
 * THE SECOND ARGUMENT ONLY WIDENS. A matching army answers 1 whatever it is;
 * it decides only whether an ALLIED army counts too. So a caller passing 0
 * gets "mine", and one passing non-zero gets "mine or my ally's".
 *
 * The army is sign-extended for the comparison against ADDR_DEFAULT_OWNER and
 * passed to AllyFlag as that same widened value.
 */
int32_t __cdecl ObjIsOurs(void *obj, int32_t allies)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  army;
    int32_t  mine;

    if (*(void *const *)(uintptr_t)ADDR_MP_SESSION
        && ObjIsType2((const AM2_Object *)o)
        && *(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == 7)
        return 0;

    army = *(const int8_t *)(o + OBJ_OFF_ARMY);
    mine = (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER;

    if (army == mine)
        return 1;
    if (!allies)
        return 0;

    return AllyFlag(army, mine) ? 1 : 0;
}

/* ObjIsLiveTarget -- original 0x00403660, one call site (which the aligned
 * scan reports twice).
 *
 * Is this object worth shooting at? Five tests after the override, and the
 * order is the whole content: type 7 is always yes; zero health is always no;
 * an ITEM with NEGATIVE health is no; destroyed is no; and finally the army
 * must not be the neutral one.
 *
 * THE ZERO AND NEGATIVE HEALTH TESTS ARE SEPARATE AND MEAN DIFFERENT THINGS.
 * Health of exactly zero is refused for everything. Health BELOW zero is
 * refused only for items -- a type 2 or 3 at negative health is still a live
 * target here. That is the third reading of this field in as many files:
 * SelectIfOwn takes `!= 0`, ObjToAI takes `> 0`, and this takes both, on
 * different objects.
 *
 * THE CONSTANT 4 IS USED TWICE, AS A FLAG MASK AND AS AN ARMY. The original
 * loads `al = 4`, tests it against the flags for OBJ_FLAG_DESTROYED, and then
 * compares the army byte against the same register. Two unrelated meanings in
 * one constant, which is a compiler folding and not a fact about either --
 * written out as the two constants they are.
 */
int32_t __cdecl ObjIsLiveTarget(void *obj)
{
    uint8_t *o     = (uint8_t *)obj;
    uint32_t flags = *(const uint32_t *)(o + OBJ_OFF_FLAGS);

    if (flags & OBJ_FLAG_BIT8)
        return 1;
    if (*(const int32_t *)o == 7)
        return 1;
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0)
        return 0;
    if (ObjIsItem((const AM2_Object *)o)
        && *(const int16_t *)(o + OBJ_OFF_HEALTH) < 0)
        return 0;
    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return 0;

    return *(const uint8_t *)(o + OBJ_OFF_ARMY) != AM2_ARMY_NEUTRAL;
}

/* ObjIsHittable -- original 0x004036C0, and the shortest of the three.
 *
 * The same question with three tests instead of five: the override, zero
 * health, destroyed, and then ObjIsType4 -- whose ANSWER IS RETURNED and then
 * overwritten.
 *
 * THAT LAST CALL'S RESULT IS DISCARDED. The original calls ObjIsType4, pops
 * the argument, and falls into the same `mov eax, 1` the override arm jumps
 * to -- so the answer is 1 whether the object is a type 4 or not. It is a
 * call for its side effects, and ObjIsType4's only side effect is to LOG for
 * a non-weapon: "uid wasn't a weapon!". So reaching this point with anything
 * else is a complaint in the log and a yes to the caller.
 *
 * Reproduced exactly, including the discarded result, because removing the
 * call would remove the log line -- which is one of the few things an A/B can
 * see.
 */
int32_t __cdecl ObjIsHittable(void *obj)
{
    uint8_t *o     = (uint8_t *)obj;
    uint32_t flags = *(const uint32_t *)(o + OBJ_OFF_FLAGS);

    if (!(flags & OBJ_FLAG_BIT8)) {
        if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0)
            return 0;
        if (flags & OBJ_FLAG_DESTROYED)
            return 0;

        (void)ObjIsType4((const AM2_Object *)o);   /* for the log only */
    }

    return 1;
}

int army_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_EXIT_ALL_FROM_VEHICLE,
                        (const void *)ExitAllFromVehicle,
                        "ExitAllFromVehicle", 2);
    rc |= patch_replace(ADDR_EXIT_ONE_FROM_VEHICLE,
                        (const void *)ExitOneFromVehicle,
                        "ExitOneFromVehicle", 4);
    rc |= patch_replace(ADDR_SET_LEADS_AND_ACT, (const void *)SetLeadsAndAct,
                        "SetLeadsAndAct", 1);
    rc |= patch_replace(ADDR_LIST_FIRST_OBJ, (const void *)ListFirstObj,
                        "ListFirstObj", 1);
    rc |= patch_replace(ADDR_LIST_FIRST_FIELD548, (const void *)ListFirstField548,
                        "ListFirstField548", 1);
    rc |= patch_replace(ADDR_ALLY_FLAG, (const void *)AllyFlag, "AllyFlag", 2);
    rc |= patch_replace(ADDR_OBJ_ATTACH_TO, (const void *)ObjAttachTo,
                        "ObjAttachTo", 2);
    rc |= patch_replace(ADDR_OBJ_IS_OURS, (const void *)ObjIsOurs,
                        "ObjIsOurs", 5);
    rc |= patch_replace(ADDR_OBJ_IS_LIVE_TARGET, (const void *)ObjIsLiveTarget,
                        "ObjIsLiveTarget", 1);
    rc |= patch_replace(ADDR_OBJ_IS_HITTABLE, (const void *)ObjIsHittable,
                        "ObjIsHittable", 0);
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
