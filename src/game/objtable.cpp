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
#include "item.h"     /* OnSelectionChanged -- reconstructed */
#include "misc.h"      /* PtrListPush -- reconstructed */
#include "image.h"
#include "crt.h"       /* am2_realloc */
#include "../inject/patch.h"

#include <stdint.h>

int32_t __cdecl FindSlot(uint32_t uid, int32_t *insert_at)
{
    int32_t hi, mid, lo;

    /* With an empty table `mid` stays 0 and the tail below reports insertion
     * position 0, which is what the original does. The midpoint is a shift
     * -- `sar`, no rounding fix-up. */
    hi  = g_objCount;
    mid = 0;
    lo  = 0;
    while (hi > lo) {
        mid = ((hi - lo) >> 1) + lo;
        if (g_objTable[mid].uid == uid)
            return mid;
        if (g_objTable[mid].uid < uid)
            lo = mid + 1;
        else
            hi = mid;
    }

    /* Not found. Report where it would belong, so callers that are inserting
     * do not have to search twice. Three separate exits in the original,
     * distinguished by where the search landed. */
    if (mid >= g_objCount)
        *insert_at = g_objCount;
    else if (g_objTable[mid].uid < uid)
        *insert_at = mid + 1;
    else
        *insert_at = mid;

    return -1;
}

AM2_Object *__cdecl LookupByUID(uint32_t uid)
{
    int32_t slot;
    int32_t i;

    /* The original passes the address of its own `uid` argument slot as the
     * out-param, reusing one stack dword for both -- safe because FindSlot
     * reads the key into a register before it ever writes through the pointer.
     * The same trick appears in CheckSaveTag. Left uninitialised, VC6 puts
     * it in that slot too. */
    i = FindSlot(uid, &slot);
    if (i >= 0)
        return g_objTable[i].obj;
    return 0;
}

/* The records must stay exactly 12 bytes or every index computation above is
 * wrong. Checked at compile time rather than trusted. */
typedef char am2_objentry_is_12_bytes[(sizeof(AM2_ObjEntry) == 12) ? 1 : -1];

/* AddToItemList -- reconstructed from 0x00429740.
 *
 * Registers an object and returns its UID, allocating one when `uid` is 0.
 * A UID is (owner << 29) | counter, giving eight owners with a 29-bit counter
 * each, kept in the per-owner array at 0x00511DE0 and starting at 1000.
 *
 * Which field supplies the owner depends on the object's type, dispatched
 * through a 9-entry jump table at 0x0042991C: types 0, 5, 6 and 7 take it from
 * the global at 0x004F9FDC, everything else from the object's own byte at
 * +0x10.
 */
uint32_t __cdecl AddToItemList(AM2_Object *obj, uint32_t uid)
{
    uint32_t *counter;
    uint32_t  owner;
    uint32_t  id;       /* the uid registered; `uid` is only the request */
    int32_t   n;
#if defined(_MSC_VER) && !defined(__GNUC__)
    int32_t   pos;      /* uninitialised in the original; see the note below */
#else
    int32_t   pos = 0;
#endif

    if (uid == 0) {
        /* Each arm builds the uid itself; the original has no shared
         * owner variable here. */
        switch (obj->type) {
        case 0: case 5: case 6: case 7:
            counter = &g_uidCounter[g_defaultOwner & 7];
            id = (g_defaultOwner << AM2_UID_OWNER_SHIFT) | *counter;
            (*counter)++;
            break;
        case 1: case 2: case 3: case 4: case 8:
        own:
            counter = &g_uidCounter[(uint32_t)(int32_t)obj->owner & 7];
            /* The original shifts the unmasked value; only three bits
             * survive, so this matches. */
            id = ((uint32_t)(int32_t)obj->owner << AM2_UID_OWNER_SHIFT)
               | *counter;
            (*counter)++;
            break;
        default:
            /* The > 8 fall-through. Its own label keeps 1-4 and 8 in the
             * nine-entry jump table the image has. */
            goto own;
        }
    } else {
        uint32_t next;

        owner = (uint32_t)(uint8_t)obj->owner & 7;

        if (g_debugItemList)
            orig_log("AddToItemList: newuid=%x, ownerindex=%d, gCurrentUID=%x\n",
                     uid, owner, g_uidCounter[owner]);
        counter = &g_uidCounter[owner];

        /* Keep the counter ahead of any UID handed to us. */
        id   = uid;
        next = *counter + 1;
        uid &= AM2_UID_COUNTER_MASK;
        *counter = next > uid + 1 ? next : uid + 1;
    }

    if (*counter > AM2_UID_COUNTER_MAX) {
        orig_log("overflow!\n");
        *counter = AM2_UID_COUNTER_MIN;

        /* Probe forward for a counter value whose UID is not already taken.
         *
         * Faithfully reproduced, including its defect: the free UID is built
         * into a different register than the one the insert below uses, so the
         * search advances the counter and is otherwise discarded -- the UID
         * actually registered is still the one computed before the overflow.
         * Unreachable in practice (it needs 2^29 objects for one owner), which
         * is presumably why it was never noticed. Not fixed here: matching the
         * original's behaviour matters more than its intent.
         */
        {
            uint32_t probe = ((uint32_t)(int32_t)obj->owner << AM2_UID_OWNER_SHIFT)
                           | *counter;

            /* Its own insertion point, not the one the insert below uses. */
            int32_t at;

            while (1) {
                int32_t i = FindSlot(probe, &at);

                if (i < 0 || g_objTable[i].obj == 0)
                    break;
                /* The next probe is built from the counter BEFORE it steps,
                 * so the first retry asks for the same uid again. */
                probe = ((uint32_t)(int32_t)obj->owner << AM2_UID_OWNER_SHIFT)
                      | *counter;
                orig_log("searching for free uid!\n");
                (*counter)++;
            }
        }
    }

    obj->uid = id;

    /* Return value ignored by the original: it only wants the insertion point.
     * Note FindSlot writes *pos only when it returns -1, so registering a UID
     * that is already present would insert at a stale position. Callers are
     * expected to supply fresh UIDs. */
    FindSlot(id, &pos);

    if (g_objCount >= g_objCap) {
        g_objCap += 100;
        g_objTable = (AM2_ObjEntry *)am2_realloc(
            g_objTable, (size_t)g_objCap * sizeof(AM2_ObjEntry));
    }

    n = g_objCount - pos;
    if (n > 0)
        orig_memmove(&g_objTable[pos + 1], &g_objTable[pos],
                     n * sizeof(AM2_ObjEntry));

    g_objCount++;
    g_objTable[pos].uid   = id;
    g_objTable[pos].obj   = obj;
    /* Zero, not the current stamp: a newly registered object should be visited
     * by a walk already in progress rather than skipped by it. */
    g_objTable[pos].stamp = 0;

    return id;
}

/* RemoveFromItemList -- reconstructed from 0x00428590.
 *
 * Looks the object up by the UID it carries at +0x04, drops the entry and
 * closes the gap. Returns 1 when it removed something, 0 when the UID was not
 * registered.
 */
int32_t __cdecl RemoveFromItemList(AM2_Object *obj)
{
    int32_t pos;                    /* uninitialised: it lives in obj's slot */
    int32_t i, tail;

    /* Same trick as LookupByUID and CheckSaveTag: the original hands FindSlot
     * the address of its own argument slot. */
    i = FindSlot(obj->uid, &pos);
    if (i < 0)
        return 0;

    g_objCount--;
    tail = g_objCount - i;          /* entries sitting above the removed one */
    if (tail > 0)
        orig_memmove(&g_objTable[i], &g_objTable[i + 1],
                     (size_t)tail * sizeof(AM2_ObjEntry));

    return 1;
}

/* FirstItem / NextItem -- reconstructed from 0x00427850 and 0x00427880.
 *
 * A walk over every registered object. Because an insert memmoves the tail,
 * an index captured by the caller can be invalidated mid-walk, so the table
 * does not rely on index stability: FirstItem bumps a global stamp, and
 * NextItem skips any entry already carrying it and marks each one it hands
 * back. An object can therefore be shifted during a walk without being
 * visited twice.
 */
AM2_Object *__cdecl FirstItem(void)
{
    g_iterCursor = 0;
    if (g_objCount <= 0)
        return 0;

    g_iterStamp++;
    return g_objTable[0].obj;
}

AM2_Object *__cdecl NextItem(void)
{
    int32_t count = g_objCount;

    if (++g_iterCursor >= count)
        return 0;

    /* Skip whatever this pass has already handed out. */
    while (g_objTable[g_iterCursor].stamp == g_iterStamp)
        if (++g_iterCursor >= count)
            return 0;

    g_objTable[g_iterCursor].stamp = g_iterStamp;
    return g_objTable[g_iterCursor].obj;
}

AM2_Object *__cdecl ObjByUidAlias(uint32_t uid)
{
    return LookupByUID(uid);
}

/* 0x00427CE0, fifteen callers -- the HUD, the script and the unit code all
 * add to the selection through it.
 *
 * Three refusals and then two writes. A null object; a list already OVER
 * sixty-four, which is `> 0x40` and not `>=`, so sixty-five is the real cap;
 * and an object whose uid is already in the list. Past those, the uid is
 * appended and the object is marked.
 *
 * The duplicate scan re-reads the items base once and walks it, comparing
 * UIDS rather than object pointers -- the list holds uids and so does the
 * comparison, which is the part that would be easy to get wrong.
 *
 * The last call is handed ADDR_ZERO_POINT as a packed point, the same
 * global OverlayPrepare seeds its offsets from. */
void __cdecl SelectUnit(AM2_Object *obj)
{
    AM2_Object *o = obj;
    int32_t  i;

    /* The list's count, items and the uid are read again at every use. */
    if (!o)
        return;
    if (g_selectedUids->count > AM2_MAX_SELECTED)
        return;

    for (i = 0; i < g_selectedUids->count; i++)
        if (g_selectedUids->uids[i] == o->uid)
            return;

    ((AM2_Rec *)g_selectedUids)->PtrListPush((void *)(uintptr_t)o->uid);
    o->flags |= OBJ_FLAG_SELECTED;
    OnSelectionChanged(g_zeroPoint);
}

/* 0x00427C80, four callers, and the exact counterpart of SelectUnit above --
 * which is why it lives beside it and uses the same `rec` idiom for the
 * selection list.
 *
 * Two details are the original's rather than tidied. The loop does NOT advance
 * its index after a removal, re-testing the same slot against the shortened
 * list, which is right for a remove-in-place; and OBJ_FLAG_SELECTED is cleared
 * TWICE -- once inside the loop when a match is removed, once unconditionally
 * afterwards -- so an object that was never in the list still comes out with
 * the flag down.
 *
 * The final notify is handed ADDR_ZERO_POINT as a packed point, exactly as
 * SelectUnit's is. */
void __cdecl DeselectUnit(AM2_Object *obj)
{
    AM2_Object *o = obj;
    int32_t  i;

    /* The list's count, items and the uid are read again at every use. */
    if (!o)
        return;

    i = 0;
    while (i < g_selectedUids->count) {
        if (g_selectedUids->uids[i] == o->uid) {
            ((AM2_Rec *)g_selectedUids)->ListRemoveAt(i);
            o->flags &= ~(uint32_t)OBJ_FLAG_SELECTED;
        } else {
            i++;
        }
    }

    o->flags &= ~(uint32_t)OBJ_FLAG_SELECTED;
    OnSelectionChanged(g_zeroPoint);
}

int objtable_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_FIND_SLOT, (const void *)FindSlot, "FindSlot", 2);
    rc |= patch_replace(ADDR_LOOKUP_BY_UID, (const void *)LookupByUID, "LookupByUID", 1);
    rc |= patch_replace(ADDR_ADD_TO_ITEM_LIST, (const void *)AddToItemList, "AddToItemList", 2);
    rc |= patch_replace(ADDR_REMOVE_FROM_ITEM_LIST, (const void *)RemoveFromItemList,
                        "RemoveFromItemList", 1);
    rc |= patch_replace(ADDR_FIRST_ITEM, (const void *)FirstItem, "FirstItem", 0);
    rc |= patch_replace(ADDR_NEXT_ITEM, (const void *)NextItem, "NextItem", 0);
    rc |= patch_replace(ADDR_OBJ_BY_UID_ALIAS, (const void *)ObjByUidAlias,
                        "ObjByUidAlias", 1);
    rc |= patch_replace(ADDR_DESELECT_UNIT, (const void *)DeselectUnit,
                        "DeselectUnit", 1);
    rc |= patch_replace(ADDR_SELECT_UNIT, (const void *)SelectUnit,
                        "SelectUnit", 15);
    return rc;
}
