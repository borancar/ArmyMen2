/* Object type predicates, reconstructed from ArmyMen2.exe.
 *
 *   ObjIsItem       0x00433860   types 1, 4      33 call sites
 *   ObjIsType2      0x00457470   type 2          47 call sites
 *   ObjIsType3      0x00457490   type 3          28 call sites
 *   ObjIsTypeIn238  0x00457420   types 2, 3, 8   40 call sites
 *
 * and one lookup that ends in a predicate rather than starting from an object:
 *
 *   LookupType3ByUID 0x0045D970  uid -> type 3 or NULL   8 call sites
 *
 * All four accept NULL and answer 0 for it, which is why they are used so
 * freely -- callers do not null-check first. See objtype.h for what is and is
 * not established about the type taxonomy.
 */

#include "objtype.h"
#include "objtable.h"
#include "misc.h"          /* CommArmyOfSlot -- reconstructed */
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

/* The lookup and the test together. The original is branchless -- ObjIsType3's
 * 0-or-1 is turned into an all-ones mask with `neg eax; sbb eax, eax` and
 * ANDed over the pointer -- which is the same function as the conditional
 * written here, because the predicate answers only 0 or 1 and never a third
 * thing. Transcribing the mask would reproduce a compiler's choice rather
 * than the source's.
 *
 * LookupByUID and ObjIsType3 are both reconstructed, so this calls them
 * directly rather than reaching into the image for either. */
AM2_Object *__cdecl LookupType3ByUID(uint32_t uid)
{
    AM2_Object *obj;

    obj = (AM2_Object *)LookupByUID(uid);
    return ObjIsType3(obj) ? obj : 0;
}

#define kEventComm (*(void *const *)(uintptr_t)ADDR_COMM_OBJECT)

/* 0x00427D40, fifteen callers. The event MASK for an object: the top bit
 * always, one more bit for the owner's ARMY, and then a bit per type property.
 * event.h calls EventNotify's third and sixth parameters masks, and this is
 * what fills them.
 *
 * Note the army comes out of CommArmyOfSlot applied to the object's owner
 * byte, so that byte is a SLOT here and the switch is over the army it maps
 * to. Anything above 3 -- including the slot-4 answer that function is
 * documented to give -- leaves the mask with only its top bit, which is the
 * default rather than a special case.
 *
 * The six type tests are independent `if`s and NOT a chain, and their bits
 * deliberately overlap: 0x01C00000 for a type 2 carrying field 548, then
 * 0x01400000 for any type 2, then 0x01000000 for any of types 2, 3 and 8. So
 * an ordinary type 2 accumulates two of them and a type 3 accumulates
 * 0x01000000 | 0x00200000. Reproduced as written; collapsing them into a
 * switch would change the answer.
 *
 * Exercised: its counter reads 1 on a Boot Camp mission, from one of the
 * thirteen callers that are still the original's, on top of six more calls
 * from our own notifiers that the counter cannot see. All six of those are
 * type 2, so they take the 0x01400000 and 0x01000000 bits and no other; the
 * item, type 4 and type 3 bits are unexercised here. */
int32_t __cdecl ObjEventMask(const AM2_Object *obj)
{
    int32_t mask = (int32_t)0x80000000;

    switch (CommArmyOfSlot(kEventComm,
                           *(const int8_t *)((const uint8_t *)obj + 0x10))) {
    case 0:  mask = (int32_t)0xC0000000; break;
    case 1:  mask = (int32_t)0xA0000000; break;
    case 2:  mask = (int32_t)0x90000000; break;
    case 3:  mask = (int32_t)0x88000000; break;
    default: break;
    }

    if (ObjIsItem(obj))
        mask |= 0x04000000;
    if (ObjIsType4(obj))
        mask |= 0x02000000;
    if (ObjType2Field548(obj))
        mask |= 0x01C00000;
    if (ObjIsType2(obj))
        mask |= 0x01400000;
    if (ObjIsTypeIn238(obj))
        mask |= 0x01000000;
    if (ObjIsType3(obj))
        mask |= 0x00200000;

    return mask;
}
/* 0x0044BBA0, four callers. True only when the object is a TYPE 2 and its
 * OBJ_OFF_FIELD_5A4 is positive.
 *
 * Type2ActionA is the reader that says anything about what it means: that
 * function refuses to re-arm a unit when this is true, so the counter is a
 * reason not to. Nothing else read so far narrows it further.
 *
 * The type test comes FIRST and short-circuits, so the field is never read on
 * an object of another type -- which matters, because 0x5A4 is far enough into
 * the record that other types may not have it. */
int32_t __cdecl Type2Field5A4Set(const AM2_Object *obj)
{
    if (!ObjIsType2(obj))
        return 0;

    return *(const int32_t *)((const uint8_t *)obj + OBJ_OFF_FIELD_5A4) > 0;
}



int objtype_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_OBJ_EVENT_MASK, (const void *)ObjEventMask,
                        "ObjEventMask", 1);

    rc |= patch_replace(ADDR_OBJ_IS_ITEM, (const void *)ObjIsItem, "ObjIsItem", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE2, (const void *)ObjIsType2, "ObjIsType2", 1);
    rc |= patch_replace(ADDR_TYPE2_FIELD5A4_SET, (const void *)Type2Field5A4Set,
                        "Type2Field5A4Set", 4);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE3, (const void *)ObjIsType3, "ObjIsType3", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE238, (const void *)ObjIsTypeIn238, "ObjIsTypeIn238", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE8, (const void *)ObjIsType8, "ObjIsType8", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE4, (const void *)ObjIsType4, "ObjIsType4", 1);
    rc |= patch_replace(ADDR_OBJ_TYPE2_FIELD548, (const void *)ObjType2Field548,
                        "ObjType2Field548", 1);
    rc |= patch_replace(ADDR_LOOKUP_TYPE3_BY_UID, (const void *)LookupType3ByUID,
                        "LookupType3ByUID", 1);
    return rc;
}
