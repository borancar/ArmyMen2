/* item.cpp -- see item.h.
 *
 * Taking a translation unit whole rather than picking leaves off by size: the
 * struct offsets and the names only make sense together. The item half of this
 * unit names itself generously -- "DeployItem(resurrection): uid:%x,
 * health:%d", "DestroyItemObject, %x", "itemGoneMessageSend uid %x item_type
 * %d" -- so the fields have real names to be read off rather than invented.
 */
#include <stdint.h>

#include "item.h"
#include "misc.h"      /* ClearPtrList */
#include "objtable.h"
#include "objtype.h"   /* ObjType2Field548 */
#include "objflag.h"   /* ObjFlagClear0 -- reconstructed */
#include "savetag.h"
#include "image.h"
#include "crt.h"
#include "armymsg.h"  /* SendObjDestroyed -- reconstructed */
#include "army.h"     /* LookupOwnerObj -- reconstructed */
#include "event.h"    /* EventNotify -- reconstructed */
#include "msgslot.h"  /* CommMustBroadcast -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "maprow.h"   /* RowUpdate -- reconstructed */
#include "script.h"   /* AM2_Pad */

/* 0x0042A7A0, 18 call sites.
 *
 *     mov eax,[esp+4] / shr eax,0x1d / ret
 *
 * Two independent confirmations that this is the owner and not an arbitrary
 * bitfield: objtable.h already carries AM2_UID_OWNER_SHIFT as 29 because
 * AddToItemList composes uids that way, and 0x0042A930 logs this function's
 * result as "Send Death Message: uid %x, army %d". Owner and army are the same
 * field.
 */
uint32_t __cdecl UidArmy(uint32_t uid)
{
    return uid >> AM2_UID_OWNER_SHIFT;
}

/* 0x0042A7B0, 100 call sites.
 *
 *     mov eax,[esp+4] / ret
 *
 * See the note in item.h. Kept as a function rather than folded into its
 * callers because it is one in the original, and because a later port to a
 * big-endian target is exactly where the distinction would start to matter.
 */
uint32_t __cdecl UidOnWire(uint32_t uid)
{
    return uid;
}

/* 0x0042A810 and 0x0042A7F0. A getter and setter for the same three bits, so
 * the mask is confirmed from both directions rather than inferred from one:
 * the setter clears 0x001C0000 and ors in (value & 7) << 18, and the getter
 * reads (word >> 18) & 7.
 *
 * The setter masks its argument to three bits BEFORE shifting. A value of 8
 * therefore sets the field to 0 rather than spilling into bit 21, which is
 * the kind of detail that only shows up when the vectors try it.
 */
#define OBJ_FIELDA_SHIFT 18
#define OBJ_FIELDA_MASK  7u
/* OBJ_OFF_FLAGS moved to orig.h, where air.cpp needs it too -- a local
 * copy of a shared offset is how two definitions of one field start. */

uint32_t __cdecl ObjFieldA(const void *obj)
{
    const uint32_t *flags = (const uint32_t *)((const uint8_t *)obj + OBJ_OFF_FLAGS);

    return (*flags >> OBJ_FIELDA_SHIFT) & OBJ_FIELDA_MASK;
}

void __cdecl ObjSetFieldA(void *obj, uint32_t value)
{
    uint32_t *flags = (uint32_t *)((uint8_t *)obj + OBJ_OFF_FLAGS);

    *flags = (*flags & ~(OBJ_FIELDA_MASK << OBJ_FIELDA_SHIFT))
           | ((value & OBJ_FIELDA_MASK) << OBJ_FIELDA_SHIFT);
}

/* 0x00429560. movsx, so signed. */
int32_t __cdecl ObjFieldB(const void *obj)
{
    return *(const int8_t *)((const uint8_t *)obj + 0x64);
}

/* The three below stay original: writing one item, reading one item, and the
 * reset the loader opens with. Each is a target of its own. */
typedef void (__cdecl *am2_save_one_fn)(am2_FILE *fp, void *obj);
typedef void (__cdecl *am2_load_one_fn)(am2_FILE *fp, int32_t flag);
typedef void (__cdecl *am2_void_fn)(void);

#define orig_save_one_item (*(am2_save_one_fn)ADDR_SAVE_ONE_ITEM)
#define orig_load_one_item (*(am2_load_one_fn)ADDR_LOAD_ONE_ITEM)

/* 0x00429450. The object registry's teardown: FreeItem every entry, free the
 * array, and clear all three fields of the {capacity, count, table} record.
 *
 * It passes 0 for FreeItem's `unlink`, which is the whole reason it can walk
 * forward without the table moving under it -- unlinking is what memmoves the
 * tail. It re-reads the count every iteration anyway. */
void __cdecl ItemsReset(void)
{
    int32_t i;

    for (i = 0; i < g_objCount; i++)
        FreeItem(g_objTable[i].obj, 0);

    if (g_objTable)
        am2_free(g_objTable);
    g_objCap   = 0;
    g_objCount = 0;
    g_objTable = (AM2_ObjEntry *)0;
}

/* 0x0045EE80. A weapon by uid, and it COMPLAINS rather than just refusing: a
 * uid that resolves to something whose type is not 4 logs "uid wasn't a
 * weapon!" and returns null, while a uid of zero or one that resolves to
 * nothing returns null in silence. Three ways to fail and only one of them is
 * worth a line. */
void *__cdecl WeaponByUid(uint32_t uid)
{
    uint32_t *obj;

    if (!uid)
        return (void *)0;
    obj = (uint32_t *)LookupByUID(uid);
    if (!obj)
        return (void *)0;
    if (obj[0] == AM2_OBJ_TYPE_WEAPON)
        return obj;
    am2_log((const char *)AM2_IMAGE(ADDR_STR_NOT_A_WEAPON));
    return (void *)0;
}

int32_t __cdecl SaveItems(am2_FILE *fp)
{
    int32_t  count = 0;
    void    *obj;

    WriteSaveTag(fp, AM2_SAVE_TAG_ITEMS);

    for (obj = FirstItem(); obj; obj = NextItem()) {
        orig_save_one_item(fp, obj);
        count++;
    }

    WriteSaveTag(fp, AM2_SAVE_TAG_END);
    am2_log("Saved %d items\n", count);
    return 1;
}

int32_t __cdecl LoadItems(am2_FILE *fp)
{
    uint32_t mark;
    int32_t  count = 0;

    /* Before the tag check, and before `fp` is even read. */
    ItemsReset();

    if (!CheckSaveTag(fp, AM2_SAVE_TAG_ITEMS,
                      (const char *)AM2_IMAGE(ADDR_STR_ITEM_CPP), 0x4A8))
        return 0;

    orig_fread(&mark, 4, 1, fp);
    while (mark == AM2_SAVE_RECORD_MARK) {
        orig_load_one_item(fp, 0);
        count++;
        orig_fread(&mark, 4, 1, fp);
    }

    am2_log("Loaded %d items\n", count);
    return 1;
}

/* 0x0042A0A0. Unlink an object from every cell list it is registered in,
 * before the storage goes back.
 *
 * The table is a BYTE count at 0x8C and 0x10-byte entries at 0x90, each
 * holding the index of the list it is linked into or -1. Each unlink writes
 * -1 back, so calling this twice is harmless -- the second pass returns on
 * the first entry.
 *
 * Three things reproduced rather than tidied. The count is re-read every
 * iteration though nothing here changes it. Entry ZERO's index is tested
 * BEFORE the loop as well as inside it, so an object whose first entry is
 * already unlinked leaves the rest linked. And the second `test al, al`
 * cannot be taken: the first one already returned on zero, and `jbe` on a
 * byte asks the same question. */
void __cdecl ItemPreDestroy(void *obj, int32_t cells)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *entries;
    int32_t  i;

    if (!*(const uint8_t *)(o + OBJ_OFF_CELL_COUNT))
        return;
    entries = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES);
    if (*(const int32_t *)(entries + CELL_ENTRY_OFF_INDEX) < 0)
        return;

    for (i = 0; i < (int32_t)*(const uint8_t *)(o + OBJ_OFF_CELL_COUNT); i++) {
        uint8_t *e = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES)
                     + (uint32_t)i * AM2_CELL_ENTRY_STRIDE;
        int32_t  at = *(const int32_t *)(e + CELL_ENTRY_OFF_INDEX);

        if (at < 0)
            return;
        ListUnlink(e, (void **)(*(uint8_t **)((uint8_t *)(uintptr_t)cells
                                              + CELLS_OFF_HEADS)
                                + (uint32_t)at * 4));
        *(int32_t *)(e + CELL_ENTRY_OFF_INDEX) = -1;
    }
}

#define kCommDbg \
    (*(const int32_t *)((const uint8_t *)*(void **)AM2_IMAGE(ADDR_COMM_OBJECT) \
                        + COMM_OFF_EVENT_DEBUG))

/* 0x00429C80. Release an item object's allocation, by its own log line.
 *
 * The byte at +0x8C is the guard and the record: nothing happens if it is
 * already clear, and it is cleared at the end, so calling this twice is safe
 * and the second call is silent. That makes it idempotent by construction
 * rather than by a caller's discipline.
 *
 * The third argument gates a call to 0x0042A0A0 -- four callers, unnamed --
 * which runs BEFORE the free, so it is a chance to look at the allocation
 * rather than a notification that it is gone.
 *
 * Note the log prints the object's uid at +4 and is gated on the comm object's
 * debug field, the same one the event functions read. */
typedef void (__cdecl *AM2_DirtyCollectFn)(const void *rect);
#define orig_dirty_collect ((AM2_DirtyCollectFn)(uintptr_t)ADDR_DIRTY_COLLECT)

/* 0x0041DB20, four callers in the image plus RowRelease below. Take one row
 * out of every map cell list it is linked into.
 *
 * Three guards before any work, and the middle one is the interesting one: the
 * FIRST entry's cell index decides whether the whole row is considered linked.
 * A negative there and nothing happens at all, not even the dirty mark.
 *
 * The dirty rectangle is collected BEFORE anything is unlinked, which is the
 * ordering that matters -- the row still knows where it was, so the region it
 * occupied gets marked for repaint.
 *
 * Both the entry count at +0x34 and the buffer pointer at +0x38 are re-read
 * from the row on EVERY iteration rather than held, and the loop breaks the
 * moment an entry's index is negative rather than skipping it. Reproduced as
 * written: an unlink that reallocated or shortened the row would otherwise be
 * a use-after-free here, and it is not this function's business to decide that
 * cannot happen.
 *
 * Exercised, and measured before the claim rather than after: its counter
 * reads 74 on a Boot Camp mission, from the original's own callers -- three of
 * the four are inside ADDR_ROW_UPDATE, which has 37 call sites. ListUnlink
 * reads 38 on the same run, which is consistent with the inner loop doing real
 * work, though that counter is shared with its other call sites and cannot be
 * attributed here on its own. So both the early returns and the loop body run.
 * RowRelease, the caller in this file, reads 0. */
void __cdecl RowUnregisterAll(void *row, void *desc)
{
    uint8_t *r = (uint8_t *)row;
    int32_t  i;
    uint32_t off;

    if (!r[ROW_OFF_OWNS])
        return;
    if (*(const int32_t *)(*(uint8_t *const *)(r + ROW_OFF_BUFFER) + 0x0C) < 0)
        return;

    orig_dirty_collect(r + ROW_OFF_RECT);

    if (!r[ROW_OFF_OWNS])
        return;

    for (i = 0, off = 0; i < (int32_t)r[ROW_OFF_OWNS]; i++, off += 0x10) {
        uint8_t *entry = *(uint8_t *const *)(r + ROW_OFF_BUFFER) + off;
        int32_t  cell  = *(const int32_t *)(entry + 0x0C);

        if (cell < 0)
            return;

        ListUnlink(entry,
                   (void **)(*(uint8_t *const *)((const uint8_t *)desc + 0x0C)
                             + (uint32_t)cell * 4));
        *(int32_t *)(entry + 0x0C) = -1;
    }
}

/* 0x0041D3A0, five callers -- one row's teardown.
 *
 * The +0x34 flag gates everything: a row that does not own a buffer is left
 * entirely alone, and in particular is NOT unregistered. So the flag means
 * "this row is in the map's cell lists AND owns an allocation", the two
 * together, rather than either separately -- which is what makes one test
 * enough for both actions.
 *
 * The order is unregister, then free, then clear both fields. Taking the row
 * out of the cell lists before the buffer goes is the part that matters:
 * anything walking those lists in between would otherwise reach freed
 * memory. */
void __cdecl RowRelease(void *row, void *desc)
{
    uint8_t *r = (uint8_t *)row;

    if (!r[ROW_OFF_OWNS])
        return;

    RowUnregisterAll(r, desc);
    am2_free(*(void **)(r + ROW_OFF_BUFFER));

    *(void **)(r + ROW_OFF_BUFFER) = (void *)0;
    r[ROW_OFF_OWNS] = 0;
}

/* 0x00434EC0. Release an object's sub-list: every row's own teardown, then
 * the array, then the capacity.
 *
 * The header is {?, count, rows, capacity} and the object reads the same two
 * dwords from its own side as OBJ_OFF_ROW_COUNT and OBJ_OFF_ROWS -- one
 * structure seen two ways, which is why the offsets look four bytes apart
 * from the shape RevealObj uses.
 *
 * The count is re-read every iteration, as it is everywhere else in this
 * family, and the array is freed only when there is one -- but the CAPACITY
 * is cleared unconditionally, so an already-empty list still has that written
 * over it. Neither the count nor the header's first dword is touched. */
void __cdecl FreeSubrecordRows(void *subrecord)
{
    uint8_t *rec = (uint8_t *)subrecord;
    void    *rows;
    int32_t  i;

    for (i = 0; i < *(const int32_t *)(rec + SUBREC_OFF_COUNT); i++)
        RowRelease(*(uint8_t **)(rec + SUBREC_OFF_ROWS)
                   + (uint32_t)i * AM2_OBJ_ROW_STRIDE,
                   (void *)(uintptr_t)ADDR_MAP_DESC);

    rows = *(void **)(rec + SUBREC_OFF_ROWS);
    if (rows) {
        am2_free(rows);
        *(void **)(rec + SUBREC_OFF_ROWS) = (void *)0;
    }
    *(int32_t *)(rec + SUBREC_OFF_CAPACITY) = 0;
}

void __cdecl DestroyTrooper(void *trooper, int32_t unlink)
{
    uint8_t *t = (uint8_t *)trooper;
    int32_t  weaponUid;
    uint8_t *weapon;
    void    *alloc;

    if (!trooper)
        return;

    weaponUid = *(const int32_t *)(t + TROOPER_OFF_WEAPON_UID);
    if (weaponUid) {
        /* Answers null, having complained, for anything that is not kind 4. */
        weapon = (uint8_t *)WeaponByUid(weaponUid);
        if (weapon) {
            if (kCommDbg)
                orig_log("DestroyTrooper %x\n",
                         *(const int32_t *)(weapon + 4));
            /* An 8-bit OR on a 32-bit load, stored back as 32 bits. */
            *(int32_t *)(weapon + WEAPON_OFF_FLAGS) |= WEAPON_FLAG_DEAD;
        }
    }

    alloc = *(void **)(t + TROOPER_OFF_ALLOC);
    if (alloc)
        am2_free(alloc);

    FreeSubrecordRows(t + OBJ_OFF_SUBRECORD);
    DestroyItemObject(trooper, (int32_t)(uintptr_t)ADDR_OBJ_TABLE_ARG,
                      unlink);
    am2_free(trooper);
}

typedef void (__attribute__((thiscall)) *AM2_ClearListFn)(void *rec);

void __cdecl DestroyVehicle(void *vehicle, int32_t unlink)
{
    uint8_t *v = (uint8_t *)vehicle;
    int32_t  weaponUid;
    uint8_t *weapon;

    if (!vehicle)
        return;

    weaponUid = *(const int32_t *)(v + VEHICLE_OFF_WEAPON_UID);
    if (weaponUid) {
        weapon = (uint8_t *)WeaponByUid(weaponUid);
        /* A 32-bit OR here, an 8-bit one in DestroyTrooper. Same bit. */
        if (weapon)
            *(int32_t *)(weapon + WEAPON_OFF_FLAGS) |= WEAPON_FLAG_DEAD;
    }

    /* The one step neither of the other two arms has. */
    ClearPtrList(v + VEHICLE_OFF_PTR_LIST);

    FreeSubrecordRows(v + OBJ_OFF_SUBRECORD);
    DestroyItemObject(vehicle, (int32_t)(uintptr_t)ADDR_OBJ_TABLE_ARG,
                      unlink);
    am2_free(vehicle);
}

void __cdecl DestroyItemCommon(void *item, int32_t unlink)
{
    if (!item)
        return;

    FreeSubrecordRows((uint8_t *)item + OBJ_OFF_SUBRECORD);
    DestroyItemObject(item, (int32_t)(uintptr_t)ADDR_OBJ_TABLE_ARG, unlink);
    am2_free(item);
}

void __cdecl DestroyKind7(void *item, int32_t unlink)
{
    int32_t *live = (int32_t *)(uintptr_t)ADDR_KIND7_COUNT;

    if (!item)
        return;

    /* Clamped at zero going down, and refused above 32 going up. */
    *live -= 1;
    if (*live < 0)
        *live = 0;

    FreeSubrecordRows((uint8_t *)item + OBJ_OFF_SUBRECORD);
    DestroyItemObject(item, (int32_t)(uintptr_t)ADDR_OBJ_TABLE_ARG, unlink);
    am2_free(item);
}

void __cdecl DestroyWeapon(void *weapon, int32_t unlink)
{
    uint8_t *w = (uint8_t *)weapon;

    if (!weapon)
        return;

    /* Not gated on the verbosity flag, unlike DestroyTrooper's. */
    orig_log("DestroyWeapon, %x\n", *(const int32_t *)(w + 4));

    FreeSubrecordRows(w + OBJ_OFF_SUBRECORD);
    DestroyItemObject(weapon, (int32_t)(uintptr_t)ADDR_OBJ_TABLE_ARG, unlink);
    am2_free(weapon);
}

void __cdecl DestroyItemObject(void *obj, int32_t arg, int32_t notify)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const uint8_t *)(o + OBJ_OFF_ALLOC_LIVE) == 0)
        return;

    if (kCommDbg)
        orig_log("DestroyItemObject, %x\n", *(const int32_t *)(o + 4));

    if (notify)
        ItemPreDestroy(obj, arg);

    am2_free(*(void **)(o + OBJ_OFF_ALLOC_PTR));

    *(uint8_t *)(o + OBJ_OFF_ALLOC_LIVE) = 0;
}

/* All five per-kind destructors are ours now, and the switch below calls them
 * directly. Each lived in a different translation unit in the original, which
 * is the real content of the dispatch: the kind says whose object this is.
 *
 * The comment that used to sit here said they "stay original and are reached
 * by address", which stopped being true one arm at a time over five commits.
 * A sentence beside a seam goes stale exactly when the seam closes. */

#define kCommDebug \
    (*(const int32_t *)((const uint8_t *)*(void **)AM2_IMAGE(ADDR_COMM_OBJECT) \
                        + COMM_OFF_EVENT_DEBUG))

/* 0x004285F0. Destroy one item, dispatching on its kind.
 *
 * `unlink` gates a call to RemoveFromItemList, and a FAILED unlink aborts the
 * whole thing -- an item that was not in the list is not freed, and 0 comes
 * back. With `unlink` zero the list is not touched at all and the free always
 * happens. Both callers matter here because this is one of the two ways
 * RemoveFromItemList is reached.
 *
 * The eight kinds land on five distinct destructors. Kinds 1, 5, 6 and 8 share
 * one; the compiler emitted four separate arms for them rather than merging,
 * and kind 8's differs only in whether `pop edi` precedes `mov eax,1`. Reading
 * that as a real difference would be a mistake -- comparing the arms BYTE for
 * byte says they differ, because `call rel32` encodes a relative displacement
 * and identical code at four addresses has four encodings.
 *
 * An unknown kind returns 1 -- success -- having done nothing at all, and
 * without complaining. Reproduced.
 *
 * Only kind 4 logs, and it is gated on the comm object's debug flag, the same
 * one the three event functions read. */
int32_t __cdecl FreeItem(void *item, int32_t unlink)
{
    if (unlink && !RemoveFromItemList((AM2_Object *)item))
        return 0;

    switch (*(const int32_t *)item) {
    case 1:
    case 5:
    case 6:
    case 8:
        DestroyItemCommon(item, unlink);
        return 1;

    case 2:
        DestroyTrooper(item, unlink);
        return 1;

    case 3:
        DestroyVehicle(item, unlink);
        return 1;

    case 4:
        if (kCommDebug)
            orig_log("FreeItem %0x\n", ((const int32_t *)item)[1]);
        DestroyWeapon(item, unlink);
        return 1;

    case 7:
        DestroyKind7(item, unlink);
        return 1;

    default:
        return 1;
    }
}

/* 0x00449860: put a slot in hand. It writes the index to UNIT_OFF_INVENTORY_SEL
 * and looks the weapon up; the rest of it is unread, so it stays original and
 * keeps a role name rather than a claim. */
typedef void (__cdecl *AM2_SelectSlotFn)(void *unit, int32_t slot);
#define orig_select_inventory_slot \
    (*(AM2_SelectSlotFn)AM2_IMAGE(ADDR_SELECT_INVENTORY_SLOT))

void __cdecl RemoveInventoryItem(void *unit, int32_t slot)
{
    uint8_t *u = (uint8_t *)unit;
    int32_t  sel;

    /* The same comm field msgslot.cpp calls AM2_COMM_LOG_ENABLED, reached the
     * same way. Logged BEFORE the null check, as the original does. */
    if (*(const int32_t *)((const uint8_t *)*(void **)AM2_IMAGE(ADDR_COMM_OBJECT)
                           + 0x418))
        orig_log("RemoveInventoryItem\n");

    if (!unit)
        return;
    if (slot < 0 || slot >= AM2_INVENTORY_SLOTS)
        return;

    /* Only the entries ABOVE `slot` move, so the last slot shifts nothing. */
    if (slot < AM2_INVENTORY_SLOTS - 1)
        orig_memmove(u + UNIT_OFF_INVENTORY + slot * 4,
                     u + UNIT_OFF_INVENTORY + (slot + 1) * 4,
                     (size_t)(0x14 - slot * 4));

    /* Cleared whether or not anything shifted, which is what stops the shift
     * leaving a duplicate at the top. */
    *(int32_t *)(u + UNIT_OFF_INVENTORY_LAST) = 0;

    sel = *(const int32_t *)(u + UNIT_OFF_INVENTORY_SEL);
    if (sel == slot) {
        /* What was in hand has gone: reset and re-select, if the object
         * agrees it should. */
        *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = 0;
        if (ObjType2Field548((const AM2_Object *)unit))
            orig_select_inventory_slot(unit, 0);
        return;
    }
    /* Above the hole slides down; below is left alone. Equal was handled
     * above and cannot reach here. */
    if (sel > slot)
        *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = sel - 1;
}

#define g_tileAttrs (*(const uint8_t **)(uintptr_t)ADDR_TILE_ATTRS)

int32_t __cdecl ObjTileAttr(const void *obj)
{
    uint32_t tile = *(const uint16_t *)((const uint8_t *)obj + OBJ_OFF_TILE);

    return *(const int8_t *)(g_tileAttrs + tile);
}

int32_t __cdecl TileAttrAt(uint32_t tile)
{
    return *(const int8_t *)(g_tileAttrs + (tile & 0xFFFFu));
}

int32_t __cdecl ObjHeight(const void *obj)
{
    const uint8_t *o   = (const uint8_t *)obj;
    int32_t        adj = *(const int8_t *)(o + OBJ_OFF_HEIGHT_ADJ);
    uint8_t        set = *(o + OBJ_OFF_HEIGHT_SET);

    if (set)
        return adj + (int32_t)(int8_t)set;
    return TileAttrAt(*(const uint16_t *)(o + OBJ_OFF_TILE)) + adj;
}

void __cdecl ObjMarkIfOverdue(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        > *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58))
        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;
}

void __cdecl ItemPreDestroyAlias(void *obj, int32_t arg)
{
    ItemPreDestroy(obj, arg);
}

/* The four teardowns and the broadcast test, all still the original's. */
typedef void (__cdecl *am2_destroy_fn)(void *obj);
#define kItemComm  (*(void *const *)(uintptr_t)ADDR_COMM_OBJECT)

/* 0x00428DA0, 22 callers. Destroy an object, then tell the others.
 *
 * Two halves. The teardown is chosen by object TYPE -- 2, 3 and 8 each have
 * their own, everything else shares one -- and all four arms end in the same
 * tail at ADDR_DESTROY_OBJ_COMMON, which is what actually sets `flags & 4` and
 * makes the destruction idempotent. Types 2, 3 and 8 are still unidentified,
 * so the arms keep structural names, as ObjIsType2/3/8 already do.
 *
 * Then the broadcast, and the question it asks is not "are we in a game" but
 * "must I tell the other players what this army just did" -- see
 * ADDR_COMM_MUST_BROADCAST, which answers no for a single-player game and
 * treats the neutral army specially.
 *
 * The original writes `movsx ax, byte [obj+0x10]` and pushes the whole EAX, so
 * the top half of that argument is whatever the teardown left in the register.
 * It cannot matter -- the callee takes an int16 -- and passing the sign-
 * extended byte is the same function. Noted because the instruction looks like
 * a truncation and is not.
 *
 * Measured: THREE calls in a driven Boot Camp mission, so the type dispatch is
 * compared against the original on live data. The broadcast beneath it is not
 * -- CommMustBroadcast answers no without a multiplayer session, so
 * SendObjDestroyed stays at 0 and is verified by reading. */
void __cdecl DestroyByType(void *obj)
{
    int32_t type = *(const int32_t *)obj;

    switch (type) {
    case 2:  DestroyType2(obj); break;
    case 3:  DestroyType3(obj); break;
    case 8:  DestroyType8(obj); break;
    default: DestroyObjCommon(obj); break;
    }

    if (CommMustBroadcast(kItemComm,
                                 (int16_t)*(const int8_t *)((const uint8_t *)obj
                                                            + OBJ_OFF_ARMY)))
        SendObjDestroyed(obj);
}

#define orig_item_teardown  ((am2_destroy_fn)AM2_IMAGE(ADDR_ITEM_TEARDOWN))

#define orig_obj_attach_to \
            ((void (__cdecl *)(void *, void *))AM2_IMAGE(ADDR_OBJ_ATTACH_TO))

#define orig_obj_clear_roach_footprint \
            ((am2_destroy_fn)AM2_IMAGE(ADDR_OBJ_CLEAR_ROACH_FOOTPRINT))

#define orig_obj_clear_footprint \
            ((am2_destroy_fn)AM2_IMAGE(ADDR_OBJ_CLEAR_FOOTPRINT))

/* 0x00449460, one caller -- DestroyByType's type-2 arm.
 *
 * The simplest of the three per-type handlers and the only one with no
 * type-specific step before the shared work: clear the script id and the field
 * beside it, detach from whatever holds this object, then fall into the common
 * tail. Its two siblings, 0x0045A9C0 and 0x0043CF30, are the same four lines
 * with one extra call in front; they wait until 0x0045A770 and 0x0043CA00 have
 * been READ, rather than being written around a name guessed from their heads.
 *
 * ObjAttachTo with a null target is a pure detach -- see its note in orig.h --
 * so this is "forget where you were, then be destroyed".
 *
 * The four stores are 16-bit in the original, two per dword, and are kept that
 * way. The value is zero so a single dword store would be indistinguishable in
 * effect; matching the width costs nothing and means the next reader compares
 * like with like.
 *
 * Its counter reads 0 and this one really IS the blind spot, unlike the last
 * zero I explained in this file: the only caller is DestroyByType, which is
 * ours and calls this by name, so nothing crosses a patch stub and the counter
 * cannot move whether the arm is taken or not. What is NOT known is whether a
 * type-2 destroy happens in a Boot Camp mission at all -- DestroyByType runs
 * three times there and which arms those take would need a probe. Said plainly
 * rather than left to read as coverage. */
void __cdecl DestroyType2(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    *(uint16_t *)(o + OBJ_OFF_FIELD_C0)     = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_C0 + 2) = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID)     = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID + 2) = 0;

    orig_obj_attach_to(obj, 0);
    DestroyObjCommon(obj);
}

/* 0x0045A9C0, one caller -- DestroyByType's type-3 arm.
 *
 * DestroyType2 with one step in front: take the object's footprint back out of
 * the map's cell weights before forgetting where it was. See
 * ADDR_OBJ_CLEAR_FOOTPRINT, which is read but not reconstructed -- it is
 * reached by address here.
 *
 * That callee is also why type 3 is probably a VEHICLE: it indexes the vehicle
 * mask with obj->[0x52C] as the kind. Recorded in orig.h with the caveat that
 * it has six other callers. */
void __cdecl DestroyType3(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    orig_obj_clear_footprint(obj);

    *(uint16_t *)(o + OBJ_OFF_FIELD_C0)     = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_C0 + 2) = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID)     = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID + 2) = 0;

    orig_obj_attach_to(obj, 0);
    DestroyObjCommon(obj);
}

/* 0x0043CF30, one caller -- DestroyByType's type-8 arm, and DestroyType3 with
 * the roach footprint clearer in place of the vehicle one. The two callees are
 * the same function with one table swapped; see
 * ADDR_OBJ_CLEAR_ROACH_FOOTPRINT.
 *
 * That callee is the evidence that type 8 is a ROACH, exactly as its twin is
 * the evidence that type 3 is a vehicle. Both readings carry the same caveat,
 * recorded in orig.h: the clearer has other callers. */
void __cdecl DestroyType8(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    orig_obj_clear_roach_footprint(obj);

    *(uint16_t *)(o + OBJ_OFF_FIELD_C0)     = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_C0 + 2) = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID)     = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID + 2) = 0;

    orig_obj_attach_to(obj, 0);
    DestroyObjCommon(obj);
}

/* 0x00429320, five callers. The shared tail of every per-type destroy, and the
 * thing that actually marks an object gone.
 *
 * Five steps, and the ORDER is the content: unlink the rows, run the item-only
 * teardown, run the pre-destroy, set the flag, then walk the chain. The flag
 * going on before the chain walk is what stops the recursion below -- every
 * chained object re-enters here and an object already marked returns at once.
 *
 * The rows at OBJ_OFF_ROWS are taken out of the map descriptor's cell lists one
 * at a time. The count is re-read every iteration though nothing in the loop
 * changes it, which is the same shape ItemPreDestroy has and is reproduced for
 * the same reason.
 *
 * The chain is by UID, not by pointer: each link goes through FindSlot and the
 * object comes back out of g_objTable. Note this reading holds for an ITEM,
 * which is what the guard above has just established -- for types 2, 3 and 8
 * the same two dwords are a count and an array pointer instead. See
 * OBJ_OFF_CHAIN_UID.
 *
 * That the chain is by uid matters because the table moves --
 * an insert memmoves the tail -- so holding a pointer across the recursion
 * would be wrong and holding a uid is not.
 *
 * Two exits that look like `continue` and are not. A chain entry that is not an
 * item RETURNS, abandoning the rest of the chain rather than skipping one; and
 * so does an object that is not an item before the walk starts. Reproduced.
 *
 * The broadcast is per chained object, after its own teardown, and asks
 * CommMustBroadcast rather than testing a session -- so a single-player game
 * sends nothing. */
void __cdecl DestroyObjCommon(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint32_t next;
    int32_t  insertAt;
    int32_t  i;

    if (!obj)
        return;
    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG8_BLOCKED)
        return;

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        uint8_t *row = *(uint8_t **)(o + OBJ_OFF_ROWS)
                       + (uint32_t)i * AM2_OBJ_ROW_STRIDE;

        ObjFlagClear0(row);
        RowUpdate(row, 0, (void *)AM2_IMAGE(ADDR_MAP_DESC));
    }

    if (ObjIsItem((const AM2_Object *)obj))
        orig_item_teardown(obj);

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & 1u)
        ItemPreDestroyAlias(obj, (int32_t)(uintptr_t)ADDR_OBJ_TABLE_ARG);

    *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG8_BLOCKED;

    if (!ObjIsItem((const AM2_Object *)obj))
        return;

    next = *(const uint32_t *)(o + OBJ_OFF_CHAIN_UID);
    while (next) {
        int32_t  slot = FindSlot(next, &insertAt);
        uint8_t *cur  = (slot >= 0) ? (uint8_t *)g_objTable[slot].obj : 0;

        if (!ObjIsItem((const AM2_Object *)cur))
            return;

        DestroyObjCommon(cur);

        next = *(const uint32_t *)(cur + OBJ_OFF_CHAIN_NEXT_UID);

        if (CommMustBroadcast(kItemComm,
                              (int16_t)*(const int8_t *)(cur + OBJ_OFF_ARMY)))
            SendObjDestroyed(cur);
    }
}

/* orig_change_object_frame is orig.h's, shared with event.cpp and
 * objscript.cpp -- one definition, not a fourth private copy. */

/* AM2_Object names `owner` at +0x10; orig.h's OBJ_OFF_OWNER is 0x04 and
 * belongs to a different structure, as air.cpp already records. */
#define AM2_OBJ_OWNER_OFF  0x10u
/* The dword EventNotify takes as `num1`. Not an OBJ_OFF_ name on purpose:
 * orig.h already has OBJ_OFF_BOUNDS on 0x0C for a different structure, and a
 * second one there would be a family alias the ratchet is right to refuse. */
#define AM2_OBJ_EVENT_NUM_OFF  0x0Cu

#define g_mpSession     (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)

typedef void (__cdecl *AM2_DeployTypeFn)(void *obj, int32_t x, int32_t y,
                                         int32_t resurrect);
typedef void (__cdecl *AM2_PlaceObjFn)(void *obj, uint32_t where);
typedef void (__cdecl *AM2_ItemDeployMsgFn)(void *obj, int32_t resurrect);
#define orig_deploy_trooper ((AM2_DeployTypeFn)(uintptr_t)ADDR_DEPLOY_TROOPER)
#define orig_deploy_vehicle ((AM2_DeployTypeFn)(uintptr_t)ADDR_DEPLOY_VEHICLE)
#define orig_place_obj      ((AM2_PlaceObjFn)(uintptr_t)ADDR_PLACE_OBJ)
#define orig_item_deploy_msg \
    ((AM2_ItemDeployMsgFn)(uintptr_t)ADDR_ITEM_DEPLOY_MSG)

/* 0x00428CA0, seven callers, and it names itself in the resurrection log line.
 * Put an object into the world at `where`, then tell the other machines.
 *
 * `resurrect` turns it into the revive path, and that path is more suspicious
 * of its caller than anything else here. It logs the uid and health; if the
 * health is NOT zero it logs a second complaint and then gives up UNLESS the
 * object is flagged destroyed -- so an item that is alive and not marked dead
 * is refused, while one whose health survived its destruction is allowed
 * through and healed to full. Both log lines are the original's text.
 *
 * The type dispatch has only two arms and a default. Types 2 and 3 get their
 * own deployers and are handed the point as two separate int16; everything
 * else is placed generically with the point still packed, and on the
 * resurrection path prints "check if resurrect command works with this object
 * type!" -- a warning the original leaves in and this keeps.
 *
 * `suppress` non-zero skips both multiplayer tests, the guard on entry and the
 * message on the way out, which is the same convention DamageObject uses for
 * its sixth argument: a machine acting on something it was told about does not
 * tell anyone back.
 *
 * Exercised once per Boot Camp mission, through EvtDeployItem, with
 * `resurrect` and `suppress` both zero -- so the type dispatch runs and the
 * multiplayer guards fall through on g_mpSession. The counter is blind, that
 * caller being ours, but the LOG settles the rest: not one
 * "DeployItem(resurrection)" line appears in a whole run, which is direct
 * evidence that the revive path is untaken rather than an inference from the
 * caller. An absent log line is evidence when the line is unconditional on the
 * path you are asking about. */
void __cdecl DeployItem(void *obj, uint32_t where, int32_t resurrect,
                        int32_t suppress)
{
    uint8_t *o = (uint8_t *)obj;
    int16_t  owner =
        (int16_t)*(const int8_t *)(o + AM2_OBJ_OWNER_OFF);

    if (g_mpSession && suppress == 0
        && !CommMustBroadcast(kItemComm, owner))
        return;

    if (resurrect) {
        am2_log("DeployItem(resurrection): uid:%x, health:%d\n",
                ((const AM2_Object *)obj)->uid,
                *(const int16_t *)(o + OBJ_OFF_HEALTH));

        if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0) {
            am2_log("DeployItem(resurrection): Item Health not zero, "
                    "uid:%x, health:%d\n",
                    ((const AM2_Object *)obj)->uid,
                    *(const int16_t *)(o + OBJ_OFF_HEALTH));
            if (!(o[OBJ_OFF_FLAGS] & OBJ_FLAG_DESTROYED))
                return;
        }

        *(int16_t *)(o + OBJ_OFF_HEALTH) =
            *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);
    }

    switch (*(const int32_t *)o) {
    case 2:
        orig_deploy_trooper(obj, (int16_t)where, (int16_t)(where >> 16),
                            resurrect);
        break;
    case 3:
        orig_deploy_vehicle(obj, (int16_t)where, (int16_t)(where >> 16),
                            resurrect);
        break;
    default:
        orig_place_obj(obj, where);
        if (resurrect)
            am2_log("Warning: check if resurrect command works with this "
                    "object type!\n");
        break;
    }

    if (suppress)
        return;
    if (!CommMustBroadcast(kItemComm, owner))
        return;

    orig_item_deploy_msg(obj, resurrect);
}

typedef void (__cdecl *AM2_ByRefBFn2)(int32_t *slot, int32_t b, int32_t c,
                                      int32_t d, int32_t e);
#define orig_by_ref_b2 ((AM2_ByRefBFn2)(uintptr_t)ADDR_BY_REF_ACTION_B)

/* 0x00417810, one caller, on the per-frame path -- and it is the "Flame On!"
 * cheat's actual effect, which is what identifies every global in it. The
 * cheat arm at 0x00417E20 sets ADDR_FLAME_ON and zeroes the clock; the one at
 * 0x00417EF0 clears the flag.
 *
 * Every 200 ms while the flag is up it points the army leader's weapon field
 * at ADDR_FLAME_RECORD and fires effect 0x14A one tile ABOVE the leader --
 * `y - 1`, not at its feet.
 *
 * The null test is in the wrong place and that is the original's. The leader
 * is dereferenced for its position TWICE, at +0x12 and +0x14, and only then is
 * the pointer tested against zero. So a run with no leader faults before it
 * ever reaches the guard, and the guard protects nothing it is placed to
 * protect. Reproduced exactly, including the order: this reads like the same
 * class of latent fault as LookupOwnerObj's untested result in DamageObject,
 * and neither is ours to fix.
 *
 * The clock advances by 200 from NOW rather than from the previous deadline,
 * so bursts drift with frame timing instead of keeping a fixed cadence.
 *
 * Measured, and the number is misleading on its own: 19,893 calls against
 * ComposeFrame's 19,970, so it runs once a frame -- and EVERY ONE of those
 * returns at the first line, because the cheat is off. Nothing past
 * ADDR_FLAME_ON is exercised, including the misplaced null test. A high call
 * count is coverage of the entry, not of the body, and the two are worth
 * separating whenever a function opens on a flag. */
void __cdecl FlameTick(void)
{
    uint8_t *leader;
    int32_t  pt;
    uint32_t now;

    if (!*(const int32_t *)(uintptr_t)ADDR_FLAME_ON)
        return;

    now = *(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS);
    if (now <= *(const uint32_t *)(uintptr_t)ADDR_FLAME_NEXT_MS)
        return;

    *(uint32_t *)(uintptr_t)ADDR_FLAME_NEXT_MS = now + AM2_FLAME_PERIOD_MS;

    leader = (uint8_t *)LookupOwnerObj(
        *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);

    /* Dereferenced before the test below -- see the note above. */
    *(int16_t *)&pt = *(const int16_t *)(leader + OBJ_OFF_POS);
    *((int16_t *)&pt + 1) =
        (int16_t)(*(const int16_t *)(leader + OBJ_OFF_POS + 2) - 1);

    if (!leader)
        return;

    SetFieldInAll(leader + 0x6C,
                          (void *)(uintptr_t)ADDR_FLAME_RECORD);
    orig_by_ref_b2(&pt, (int32_t)((const AM2_Object *)leader)->uid, 0, 0,
                   AM2_FLAME_EFFECT);
}

/* 0x00437A50, one caller, on the per-frame path. Push every repeating pad's
 * deadline forward once the game clock has passed it.
 *
 * A period of zero or less means the pad does not repeat, and its deadline is
 * left exactly where it is rather than being reset -- so a pad can be stopped
 * by clearing its period without also having to fix up its clock.
 *
 * The deadline advances by ONE period per frame, not to the next future
 * multiple, so a pad whose deadline has fallen far behind catches up one step
 * at a time. Reproduced: it is what the original does, and the difference is
 * observable in how long such a pad takes to fire again.
 *
 * The original walks a bare 0x005161D4 with a stride of 0x48; that resolves as
 * ADDR_PADS plus AM2_Pad's +0x3C, which is what makes the two fields nameable
 * at all.
 *
 * Measured at 18,546 calls against ComposeFrame's 18,699 -- once a frame, as
 * predicted from the caller before the code was written. */
void __cdecl PadAdvanceDeadlines(void)
{
    AM2_Pad *pads = (AM2_Pad *)(uintptr_t)ADDR_PADS;
    uint32_t now  = *(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS);
    int32_t  n    = *(const int32_t *)(uintptr_t)ADDR_PAD_COUNT;
    int32_t  i;

    for (i = 0; i < n; i++) {
        if (pads[i].period <= 0)
            continue;
        if (now > (uint32_t)pads[i].dueAt)
            pads[i].dueAt += pads[i].period;
    }
}

/* 0x00425E70, one caller, on the per-frame path. Re-resolve the three object
 * context slots from the uids they were set with, so a slot whose object has
 * gone becomes null rather than stale.
 *
 * Two things here are the original's and are reproduced. LookupOwnerObj is
 * called on the default owner and its RESULT DISCARDED -- the next call
 * overwrites eax before anything reads it -- so it runs for whatever it does
 * on the way, not for what it returns.
 *
 * And the three slots are not treated alike. The first and the third clear
 * their UID when the lookup fails, so the slot stops being retried; the middle
 * one writes the null back over its own CACHE instead, which it already holds,
 * and leaves the uid alone. That asymmetry has the shape of a copy-paste slip
 * in the original -- the line was edited for the wrong variable -- and its
 * consequence is that a stale ADDR_OBJ_CTX_VAL is looked up again every frame
 * forever. Kept exactly, since fixing it would change how often a dead uid is
 * searched for.
 *
 * Measured at 18,617 calls on the same run. Which of the three slots is ever
 * non-null is NOT measured -- the asymmetry above is read, not observed. */
void __cdecl RefreshObjCtx(void)
{
    LookupOwnerObj(*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);

    *(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A =
        LookupByUID(*(const uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A);
    if (!*(void *const *)(uintptr_t)ADDR_OBJ_CTX_OBJ_A)
        *(uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A = 0;

    *(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ =
        LookupByUID(*(const uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL);
    if (!*(void *const *)(uintptr_t)ADDR_OBJ_CTX_OBJ)
        *(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ = (void *)0;   /* sic */

    *(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ_PREV =
        LookupByUID(*(const uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_PREV);
    if (!*(void *const *)(uintptr_t)ADDR_OBJ_CTX_OBJ_PREV)
        *(uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_PREV = 0;
}

typedef void (__cdecl *AM2_ObjStepFn)(void *obj);
typedef void (__cdecl *AM2_VoidFn)(void);
#define orig_obj_frame_step  ((AM2_ObjStepFn)(uintptr_t)ADDR_OBJ_FRAME_STEP)
#define orig_comm_sync_check ((AM2_VoidFn)(uintptr_t)ADDR_COMM_SYNC_CHECK)

#define g_iterStamp       (*(uint32_t *)(uintptr_t)ADDR_ITER_STAMP)
#define g_secondDeadline  (*(uint32_t *)(uintptr_t)ADDR_SECOND_DEADLINE)

/* 0x00428700, one caller -- the ordinary per-frame path of
 * ADDR_TAKE_MENU_REQUEST, so this runs once a frame for a whole mission.
 *
 * Bump the sweep stamp, step every registered object, and in a multiplayer
 * session tail-jump to the comm check. The stamp is bumped BEFORE the walk and
 * exactly once, which is what lets a per-object step tell "this frame" from
 * "some earlier frame" without carrying a frame number itself.
 *
 * The comm check is a tail JUMP in the original, not a call, so it inherits
 * this function's return -- written here as a call followed by falling off the
 * end, which is the same thing for a void function with no arguments.
 *
 * Measured: 17,716 calls against ComposeFrame's 17,866 on the same run, so it
 * really is once a composed frame. And that was predicted BEFORE the code was
 * written, from Update3DAudioVolumes -- already reconstructed, already known
 * to read five figures, and called from the same path. Checking a hot caller
 * through a counter that already exists costs nothing and is what the last two
 * mis-picks were missing. */
void __cdecl ObjFrameSweep(void)
{
    void *obj;

    g_iterStamp++;

    for (obj = FirstItem(); obj; obj = NextItem())
        orig_obj_frame_step(obj);

    if (g_mpSession)
        orig_comm_sync_check();
}

/* 0x00424FE0, one caller, also on the per-frame path.
 *
 * Once the game clock passes the deadline, push the deadline a second further
 * out. That is the whole function -- and NOTHING READS THE DEADLINE. Below the
 * CRT line 0x005122F8 has exactly three references: the seed in 0x00424E80 and
 * the two here. So this is bookkeeping with no consumer, kept because it is on
 * a path that runs every frame and leaving it out would be a difference even
 * though leaving it in is not.
 *
 * The comparison is UNSIGNED and the clock is milliseconds since startup, so
 * the first call after a wrap would push the deadline out from a small clock
 * rather than skipping -- which no run here is long enough to reach, and which
 * nothing would observe anyway.
 *
 * Measured at 17,791 calls, the same once-a-frame as the sweep above. */
void __cdecl AdvanceSecondDeadline(void)
{
    uint32_t now = *(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS);

    if (now > g_secondDeadline)
        g_secondDeadline =
            now + (uint32_t)*(const int32_t *)AM2_IMAGE(ADDR_TICK_INTERVAL_MS);
}

/* 0x00427E80, and its only caller is HealObject. Event kind 6 -- healed -- and
 * the last of the three notifiers: identical in shape to NotifyDamaged and
 * TriggerItemDestroyed above, differing only in the literal.
 *
 * That the three are the same shape is worth stating, because it is what makes
 * the family readable at all. Each raises one event for the object and, when
 * there is a second party, for that party too; each passes num1, uid and event
 * mask per party; each passes zero for the delay, so none of them can take
 * EventNotify's delayed path, which is the one that would drop the masks. The
 * kinds are 4 killed, 5 damaged, 6 healed.
 *
 * Its counter is 0 and always will be: HealObject is the only caller and calls
 * it by name. Coverage is transitive from HealObject's own probe -- one call
 * in a Boot Camp mission, on the non-item path, which reaches this
 * unconditionally, and with a null `src`, so the two-party arm above does not
 * run.
 *
 * Reconstructing it also took ObjEventMask's counter from 1 to 0, and the
 * cause is exactly this function: that single call came THROUGH the original
 * 0x00427E80, which is now ours and calls ObjEventMask by name. Worth writing
 * down because a counter dropping to zero in the same run as an unrelated-
 * looking change is the shape that gets misread as a regression. */
static void __cdecl NotifyHealed(void *obj, void *src)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (src) {
        const uint8_t *a = (const uint8_t *)src;

        EventNotify(AM2_EVENT_HEALED,
                    *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)obj)->uid,
                    ObjEventMask((const AM2_Object *)obj),
                    *(const int32_t *)(a + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)src)->uid,
                    ObjEventMask((const AM2_Object *)src),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_HEALED,
                *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                ((const AM2_Object *)obj)->uid,
                ObjEventMask((const AM2_Object *)obj),
                0, 0, 0, 0, 0, 0);
}

/* 0x00428370, eight callers. Heal `obj` by `pct` percent of its MAXIMUM
 * health and notify. `src` is the other party, passed straight through to the
 * event and allowed to be null.
 *
 * The percentage is clamped to 0..100 before anything else, so a caller cannot
 * over-heal by asking for 500. Healing never resurrects: every arm gives up on
 * an object already at or below zero health.
 *
 * An ITEM ignores the percentage and goes to full. Which of its two arms runs
 * is decided by OBJ_OFF_REPAIR_FRAME -- positive means it is also stepped back
 * to frame 0 and repaired unconditionally, while zero or less repairs only an
 * item that is alive and not already at full health. Note the ORDER of that
 * second arm's two tests is the original's: alive first, then not-full.
 *
 * Everything else adds `pct` percent of the maximum and clamps. Two details
 * that are the original's and not tidied:
 *
 *   - the sum is formed in 32 bits and then TRUNCATED to 16 before it is
 *     compared against the maximum, so the comparison is on the stored value
 *     rather than on the arithmetic one;
 *   - the health is written BEFORE the clamp and again after it when the clamp
 *     bites, which is two stores to the same field and is what the original
 *     does.
 *
 * The original loads the current health with `mov bx, ...`, leaving the top
 * half of ebx holding whatever the caller left there, and then adds the full
 * 32-bit register. Only the low 16 bits are ever read back, so the garbage
 * cannot reach the result -- worth stating, because the disassembly looks like
 * it depends on an uninitialised value and does not.
 *
 * What is EXERCISED, measured with a temporary probe rather than inferred from
 * the counter: exactly one call in a Boot Camp mission, a real object, and
 * pct = 100. So the non-item path runs, and that is all. The percentage
 * arithmetic below 100, both clamps, and both item arms are reached by nothing
 * this project can drive. The counter itself reads 0 and cannot do otherwise:
 * the one caller that runs is our own EvtObjSet, which calls this by name. */
void __cdecl HealObject(void *obj, int32_t pct, void *src)
{
    uint8_t *o = (uint8_t *)obj;
    int16_t  maxHp;
    int16_t  hp;

    if (!obj)
        return;

    if (pct > 100)
        pct = 100;
    else if (pct < 0)
        pct = 0;

    maxHp = *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);

    if (ObjIsItem((const AM2_Object *)obj)) {
        if (*(const int32_t *)(o + OBJ_OFF_REPAIR_FRAME) > 0) {
            orig_change_object_frame(obj, 0, 0);
            *(int16_t *)(o + OBJ_OFF_HEALTH) = maxHp;
            NotifyHealed(obj, src);
            return;
        }

        hp = *(const int16_t *)(o + OBJ_OFF_HEALTH);
        if (hp <= 0 || hp >= maxHp)
            return;

        *(int16_t *)(o + OBJ_OFF_HEALTH) = maxHp;
        NotifyHealed(obj, src);
        return;
    }

    hp = *(const int16_t *)(o + OBJ_OFF_HEALTH);
    if (hp <= 0)
        return;

    {
        int16_t next = (int16_t)(((int32_t)maxHp * pct) / 100 + hp);

        *(int16_t *)(o + OBJ_OFF_HEALTH) = next;
        if (next > maxHp)
            *(int16_t *)(o + OBJ_OFF_HEALTH) = maxHp;
    }
    NotifyHealed(obj, src);
}

typedef void (__cdecl *AM2_DamageTypeFn)(void *obj, int32_t amount,
                                         int32_t d, int32_t kind,
                                         uint32_t attacker);
typedef void (__cdecl *AM2_DamageItemFn)(void *obj, int32_t amount, int32_t d,
                                         int32_t kind, uint32_t attacker,
                                         int32_t f);
typedef void (__cdecl *AM2_ObjAttackerFn)(void *obj, void *attacker);
typedef void (__cdecl *AM2_DamageBroadcastFn)(void *obj, uint32_t attacker,
                                              int32_t amount, int32_t kind,
                                              const void *where, int32_t f);
typedef void (__cdecl *AM2_SendDeathMsgFn)(void *obj, uint32_t attacker,
                                            int32_t kind);
typedef void (__cdecl *AM2_ObjOnlyFn)(void *obj);

#define orig_damage_item      ((AM2_DamageItemFn)(uintptr_t)ADDR_DAMAGE_ITEM)
#define orig_damage_trooper   ((AM2_DamageTypeFn)(uintptr_t)ADDR_DAMAGE_TROOPER)
#define orig_damage_vehicle   ((AM2_DamageTypeFn)(uintptr_t)ADDR_DAMAGE_VEHICLE)
#define orig_damage_roach     ((AM2_DamageTypeFn)(uintptr_t)ADDR_DAMAGE_ROACH)
#define orig_damage_broadcast \
    ((AM2_DamageBroadcastFn)(uintptr_t)ADDR_DAMAGE_BROADCAST)

#define g_gameOverFlags (*(uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS)
#define g_selectedCount (*(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT)

/* 0x00427E10, and its only two call sites are inside DamageObject. Raise event
 * kind 5 -- damage -- for the object, and for the attacker as well when there
 * is one.
 *
 * The exact mirror of ADDR_NOTIFY_HEALED, which is kind 6 and the same shape.
 * Each party contributes a triple to EventNotify: its `num1` dword, its uid,
 * and its event mask. A null attacker leaves that second triple as three
 * zeros, which the original arranges by pushing the three zeros BEFORE the
 * branch and letting both arms share them. Written here as the `else` it is.
 *
 * The last three arguments are always zero: no delay, so this never takes
 * EventNotify's delayed path, which is the one that would drop the masks.
 *
 * Coverage follows from DamageObject's probe rather than from a new one: all
 * six of its Boot Camp calls take the main path, which reaches this
 * unconditionally, so this runs six times. Every one of them has attacker uid
 * 0 and therefore a NULL attacker -- so the `else` runs and the two-party arm
 * above it does not. Its counter is 0 and always will be: the only caller is
 * DamageObject, calling by name. */
static void __cdecl NotifyDamaged(void *obj, void *attacker)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (attacker) {
        const uint8_t *a = (const uint8_t *)attacker;

        EventNotify(AM2_EVENT_DAMAGED,
                    *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)obj)->uid,
                    ObjEventMask((const AM2_Object *)obj),
                    *(const int32_t *)(a + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)attacker)->uid,
                    ObjEventMask((const AM2_Object *)attacker),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_DAMAGED,
                *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                ((const AM2_Object *)obj)->uid,
                ObjEventMask((const AM2_Object *)obj),
                0, 0, 0, 0, 0, 0);
}

/* 0x00427FD0, and the name is the ORIGINAL's, off its own log line. Event kind
 * 4 -- killed -- and the third member of the family: the same two-party shape
 * as NotifyDamaged above and as the kind-6 heal notify, differing only in the
 * literal and in the log line it can emit first.
 *
 * That log line is why this is not called OnObjDied, which is what it was
 * about to be. A sweep for pushed string literals had reported this function
 * as naming nothing -- the sweep required every byte in 32..127 and so
 * rejected any string ending in a newline, which is what every log message in
 * this image is. Re-run correctly it names this function, names
 * ADDR_SEND_DEATH_MESSAGE, and independently CONFIRMS ADDR_DAMAGE_TROOPER,
 * which had been derived from a jump table index alone.
 *
 * The log is gated on the comm object's COMM_OFF_VERBOSE, so it costs nothing
 * in an ordinary run, and it prints the attacker's uid as 0 when there is no
 * attacker rather than skipping the line.
 *
 * Measured: this runs SIX times in a Boot Camp mission, every one a type 2
 * with health already at 0. That confirms by probe what the previous commit
 * had only inferred from "1000 damage is lethal" -- the death path really is
 * reached, and DamageObject's per-type handler really does take these troopers
 * to zero. Their flags read 0 and 0x800 and never 0x400, which is why
 * DeselectUnit is not reached: none of them was the selected unit.
 *
 * Note what that gate means for
 * checking: an invented message here would NOT have failed the A/B, because
 * the line never prints on any configuration the suite drives. A wrong string
 * behind a debug flag is invisible -- which is the argument for taking the
 * literal off the image rather than writing one that reads plausibly. */
static void __cdecl TriggerItemDestroyed(void *obj, void *attacker)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (*(const int32_t *)((const uint8_t *)kItemComm + COMM_OFF_VERBOSE))
        am2_log("TriggerItemDestroyed, item uid=%x, by uid = %x\n",
                ((const AM2_Object *)obj)->uid,
                attacker ? ((const AM2_Object *)attacker)->uid : 0u);

    if (attacker) {
        const uint8_t *a = (const uint8_t *)attacker;

        EventNotify(AM2_EVENT_KILLED,
                    *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)obj)->uid,
                    ObjEventMask((const AM2_Object *)obj),
                    *(const int32_t *)(a + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)attacker)->uid,
                    ObjEventMask((const AM2_Object *)attacker),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_KILLED,
                *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                ((const AM2_Object *)obj)->uid,
                ObjEventMask((const AM2_Object *)obj),
                0, 0, 0, 0, 0, 0);
}

/* 0x0042A930, and the name is the original's: "Send Death Message: uid %x,
 * army %d". A 16-byte type-0x23 packet carrying both uids in their on-wire
 * form and the damage kind as one byte, then the log line.
 *
 * The whole body is behind ADDR_MP_SESSION, so in single player this returns
 * at its first instruction. Measured: it runs six times in a Boot Camp mission
 * and does nothing all six, which is the arm the A/B compares. Everything past
 * the gate is verified by reading. */
static void __cdecl SendDeathMessage(void *obj, uint32_t attackerUid,
                                     int32_t kind)
{
    uint8_t msg[AM2_MSG_DEATH_BYTES];

    if (!g_mpSession)
        return;

    *(uint16_t *)(msg + 0) = AM2_MSG_DEATH_BYTES;
    *(uint16_t *)(msg + 2) = AM2_MSG_DEATH;
    *(uint32_t *)(msg + 4) = UidOnWire(((const AM2_Object *)obj)->uid);
    *(uint32_t *)(msg + 8) = UidOnWire(attackerUid);
    msg[12]                = (uint8_t)kind;

    ArmyMessageSend(msg);
    am2_log("Send Death Message: uid %x, army %d\n",
            ((const AM2_Object *)obj)->uid, UidArmy(attackerUid));
}

/* 0x00428070. The last step of the death sequence, and it is entirely
 * multiplayer bookkeeping: two DELAYED events, scheduled 3 seconds and 5
 * minutes out, each gated on a bit of ADDR_GAME_OVER_FLAGS.
 *
 * Three tests come first and the third is the one that matters here: types 2,
 * 3 and 8 only, not one carrying ObjType2Field548, and only in a multiplayer
 * session. So in single player this returns before doing anything, which is
 * what all six of its Boot Camp calls do.
 *
 * Both events pass a RULE uid as EventNotify's num1 and a delay as its eighth
 * argument -- which is the path event.h notes drops the masks and the second
 * pair, and both calls duly pass zeros for those. */
static void __cdecl ObjDeathCleanup(void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return;
    if (ObjType2Field548((const AM2_Object *)obj))
        return;
    if (!g_mpSession)
        return;

    if (ObjIsType2((const AM2_Object *)obj)
        && (g_gameOverFlags & 0x200000u)
        && *(const int32_t *)(o + OBJ_OFF_MP_ROLE) != AM2_MP_ROLE_SEVEN
        && *(const int32_t *)(o + OBJ_OFF_FIELD_94) == 0)
        EventNotify(0, *(const int32_t *)(uintptr_t)ADDR_RULE_UID_B,
                    ((const AM2_Object *)obj)->uid,
                    0, 0, 0, 0, AM2_DEATH_DELAY_SHORT, 0, 1);

    if ((g_gameOverFlags & 0x80000u)
        && !(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_8000))
        EventNotify(0, *(const int32_t *)(uintptr_t)ADDR_RULE_UID_A,
                    ((const AM2_Object *)obj)->uid,
                    0, 0, 0, 0, AM2_DEATH_DELAY_LONG, 0, 1);
}

/* 0x00428140, nineteen callers. Damage one object: dispatch to the handler for
 * its type, tell the world, and run the death sequence if it has just died.
 *
 * The shape is three questions in a row, and the multiplayer rule is the one
 * that shows up three times: in a session, only a machine that MAY broadcast
 * for the relevant army does the work at all -- and the army in question is
 * the ATTACKER's for the damage, and the VICTIM's for the death. `suppress`
 * non-zero skips those tests, which is how a machine applies damage it has
 * been told about rather than damage it decided on.
 *
 * The attacker is resolved from a uid through the object table by hand rather
 * than through LookupByUID -- FindSlot, then the table entry -- and its owner
 * is what the broadcast tests use. A uid that resolves to nothing gives owner
 * 4, which is not a real army and is what makes the test fail closed.
 *
 * An object that is ALREADY at or below zero health takes the early arm: it is
 * notified and broadcast, but no per-type handler runs and it does not die a
 * second time. That is the guard against double-killing, and it is why the
 * health test appears twice.
 *
 * The tail is the selection fix-up and it is worth stating because it is
 * player-visible: if the unit that died was the selected one, it is
 * deselected, and if it belonged to us and nothing else is selected, the
 * army's leader is selected instead. LookupOwnerObj's result is dereferenced
 * with NO null test, which is the original's and is reproduced.
 *
 * What is EXERCISED, measured with a temporary probe because the counter is
 * blind -- both callers that fire are ours and call by name. Six calls in a
 * Boot Camp mission, every one of them type 2 with amount 1000 against 30 or
 * 60 health, attacker uid 0 and suppress 0. So the trooper arm runs, and so
 * does the whole death sequence, since 1000 is lethal to all of them.
 *
 * What does NOT run here: the item, vehicle and roach arms; the types 4 to 7
 * fall-through; the early "already at zero health" arm; and every multiplayer
 * branch, because g_mpSession is 0 in a single-player mission. The attacker
 * lookup is only ever exercised with uid 0, which takes the no-attacker path
 * and the owner-4 default. */
void __cdecl DamageObject(void *obj, int32_t amount, int32_t kind,
                          uint32_t attackerUid, int32_t extra,
                          int32_t suppress)
{
    uint8_t *o = (uint8_t *)obj;
    void    *attacker;
    int16_t  attackerOwner;
    int32_t  slot;
    int32_t  insertAt;

    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return;

    slot = FindSlot(attackerUid, &insertAt);
    if (slot >= 0)
        attacker = g_objTable[slot].obj;
    else
        attacker = (void *)0;

    if (attacker) {
        attackerOwner =
            (int16_t)*(const int8_t *)((const uint8_t *)attacker
                                       + AM2_OBJ_OWNER_OFF);
    } else {
        attacker      = (void *)0;
        attackerOwner = 4;
    }

    if (g_mpSession && suppress == 0
        && !CommMustBroadcast(kItemComm, attackerOwner))
        return;

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) <= 0) {
        NotifyDamaged(obj, attacker);
        if (g_mpSession && suppress == 0
            && CommMustBroadcast(kItemComm, attackerOwner))
            orig_damage_broadcast(obj, attackerUid, amount, kind,
                                  o + OBJ_OFF_POS, 0);
        return;
    }

    switch (*(const int32_t *)o) {
    case 1:
        orig_damage_item(obj, amount, extra, kind, attackerUid, 0);
        break;
    case 2:
        orig_damage_trooper(obj, amount, extra, kind, attackerUid);
        break;
    case 3:
        orig_damage_vehicle(obj, amount, extra, kind, attackerUid);
        break;
    case 8:
        orig_damage_roach(obj, amount, extra, kind, attackerUid);
        break;
    default:
        break;                  /* types 4..7 have no handler */
    }

    NotifyDamaged(obj, attacker);

    if (g_mpSession && suppress == 0
        && CommMustBroadcast(kItemComm, attackerOwner))
        orig_damage_broadcast(obj, attackerUid, amount, kind,
                              o + OBJ_OFF_POS, 0);

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) > 0)
        return;

    if (g_mpSession
        && !CommMustBroadcast(kItemComm,
                              (int16_t)*(const int8_t *)(o + AM2_OBJ_OWNER_OFF)))
        return;

    TriggerItemDestroyed(obj, attacker);
    SendDeathMessage(obj, attackerUid, kind);
    ObjDeathCleanup(obj);

    if (!(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SELECTED))
        return;

    DeselectUnit(obj);

    if ((uint32_t)*(const int8_t *)(o + AM2_OBJ_OWNER_OFF) != g_defaultOwner
        || g_selectedCount != 0)
        return;

    {
        uint8_t *leader = (uint8_t *)LookupOwnerObj(g_defaultOwner);

        if (!(*(const uint8_t *)(leader + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED))
            SelectUnit(leader);
    }
}

/* 0x00428C40, one caller. Free every item that is past its deadline.
 *
 * Measured: SIXTY-NINE calls in a driven Boot Camp window, so this is
 * thoroughly exercised and the A/B compares it on live data.
 *
 * I first wrote the opposite here -- that it runs only on LEAVING a level --
 * from the caller's name rather than its body. 0x00425EE0 consumes a pending
 * menu request and RETURNS on that branch, so the teardown is the short arm
 * and this sweep is on the ordinary per-frame path that follows it. Second
 * time this session I have taken a function's role from a caller's summary
 * instead of reading it; the rule is in CLAUDE.md and it is about callees just
 * as much as callers.
 *
 * OBJ_FLAG_OVERDUE is set by ADDR_OBJ_MARK_IF_OVERDUE when an object passes
 * the deadline at OBJ_OFF_DEADLINE_58, so the sweep is a deferred free: mark
 * during play, collect on the way out. Bit 27 exempts an object from it; what
 * that means is not established and the name says so.
 *
 * `next` is taken BEFORE the free, which is the only thing here that has to be
 * right -- FreeItem unlinks, so reading the walk after it would follow a
 * pointer through freed memory. The original is careful about this and the
 * order is worth preserving deliberately rather than by luck.
 *
 * The broadcast asks CommMustBroadcast rather than testing a session directly,
 * so a single-player game sends nothing and the neutral army is special-cased;
 * see that function. */
void __cdecl FreeOverdueItems(void)
{
    void *obj = FirstItem();

    if (!obj)
        return;

    do {
        void   *next  = NextItem();
        uint8_t *o    = (uint8_t *)obj;
        uint32_t flags = *(const uint32_t *)(o + OBJ_OFF_FLAGS);

        if ((flags & OBJ_FLAG_OVERDUE) && !(flags & OBJ_FLAG_NO_SWEEP)) {
            if (CommMustBroadcast(kItemComm,
                                  (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
                ItemGoneMessageSend(obj);
            FreeItem(obj, 1);
        }

        obj = next;
    } while (obj);
}

void item_install(void)
{
    patch_replace(ADDR_ITEM_PRE_DESTROY, (const void *)ItemPreDestroy,
                  "ItemPreDestroy", 2);
    patch_replace(ADDR_FREE_SUBRECORD_ROWS, (const void *)FreeSubrecordRows,
                  "FreeSubrecordRows", 1);
    patch_replace(ADDR_ITEMS_RESET, (const void *)ItemsReset,
                  "ItemsReset", 0);
    patch_replace(ADDR_WEAPON_BY_UID, (const void *)WeaponByUid,
                  "WeaponByUid", 1);
    patch_replace(ADDR_REMOVE_INVENTORY_ITEM,
                  (const void *)RemoveInventoryItem,
                  "RemoveInventoryItem", 4);
    patch_replace(ADDR_UID_ARMY, (const void *)UidArmy, "UidArmy", 1);
    patch_replace(ADDR_FREE_ITEM, (const void *)FreeItem, "FreeItem", 2);
    patch_replace(ADDR_DESTROY_TYPE2, (const void *)DestroyType2,
                  "DestroyType2", 1);
    patch_replace(ADDR_DESTROY_TYPE3, (const void *)DestroyType3,
                  "DestroyType3", 1);
    patch_replace(ADDR_DESTROY_TYPE8, (const void *)DestroyType8,
                  "DestroyType8", 1);
    patch_replace(ADDR_DESTROY_OBJ_COMMON, (const void *)DestroyObjCommon,
                  "DestroyObjCommon", 5);
    patch_replace(ADDR_FREE_OVERDUE_ITEMS, (const void *)FreeOverdueItems,
                  "FreeOverdueItems", 1);
    patch_replace(ADDR_DESTROY_BY_TYPE, (const void *)DestroyByType,
                  "DestroyByType", 22);
    patch_replace(ADDR_FREE_ITEM_KIND2, (const void *)DestroyTrooper,
                  "DestroyTrooper", 1);
    patch_replace(ADDR_FREE_ITEM_KIND3, (const void *)DestroyVehicle,
                  "DestroyVehicle", 1);
    patch_replace(ADDR_FREE_ITEM_KIND4, (const void *)DestroyWeapon,
                  "DestroyWeapon", 1);
    patch_replace(ADDR_FREE_ITEM_COMMON, (const void *)DestroyItemCommon,
                  "DestroyItemCommon", 4);
    patch_replace(ADDR_FREE_ITEM_KIND7, (const void *)DestroyKind7,
                  "DestroyKind7", 1);
    patch_replace(ADDR_DESTROY_ITEM_OBJECT, (const void *)DestroyItemObject,
                  "DestroyItemObject", 5);
    patch_replace(ADDR_UID_ON_WIRE, (const void *)UidOnWire, "UidOnWire", 1);
    patch_replace(ADDR_FLAME_TICK, (const void *)FlameTick,
                  "FlameTick", 0);
    patch_replace(ADDR_PAD_ADVANCE_DEADLINES,
                  (const void *)PadAdvanceDeadlines,
                  "PadAdvanceDeadlines", 0);
    patch_replace(ADDR_REFRESH_OBJ_CTX, (const void *)RefreshObjCtx,
                  "RefreshObjCtx", 0);
    patch_replace(ADDR_OBJ_FRAME_SWEEP, (const void *)ObjFrameSweep,
                  "ObjFrameSweep", 0);
    patch_replace(ADDR_ADVANCE_SECOND, (const void *)AdvanceSecondDeadline,
                  "AdvanceSecondDeadline", 0);
    patch_replace(ADDR_DEPLOY_ITEM, (const void *)DeployItem,
                  "DeployItem", 4);
    patch_replace(ADDR_NOTIFY_HEALED, (const void *)NotifyHealed,
                  "NotifyHealed", 2);
    patch_replace(ADDR_ROW_UNREGISTER_ALL, (const void *)RowUnregisterAll,
                  "RowUnregisterAll", 2);
    patch_replace(ADDR_SEND_DEATH_MESSAGE, (const void *)SendDeathMessage,
                  "SendDeathMessage", 3);
    patch_replace(ADDR_OBJ_DEATH_CLEANUP, (const void *)ObjDeathCleanup,
                  "ObjDeathCleanup", 1);
    patch_replace(ADDR_TRIGGER_ITEM_DESTROYED, (const void *)TriggerItemDestroyed,
                  "TriggerItemDestroyed", 2);
    patch_replace(ADDR_NOTIFY_DAMAGED, (const void *)NotifyDamaged,
                  "NotifyDamaged", 2);
    patch_replace(ADDR_DAMAGE_OBJECT, (const void *)DamageObject,
                  "DamageObject", 6);
    patch_replace(ADDR_HEAL_OBJECT, (const void *)HealObject,
                  "HealObject", 3);
    patch_replace(ADDR_OBJ_FIELD_A, (const void *)ObjFieldA, "ObjFieldA", 1);
    patch_replace(ADDR_OBJ_SET_FIELD_A, (const void *)ObjSetFieldA,
                  "ObjSetFieldA", 2);
    patch_replace(ADDR_OBJ_FIELD_B, (const void *)ObjFieldB, "ObjFieldB", 1);
    patch_replace(ADDR_SAVE_ITEMS, (const void *)SaveItems, "SaveItems", 1);
    patch_replace(ADDR_LOAD_ITEMS, (const void *)LoadItems, "LoadItems", 1);
    patch_replace(ADDR_OBJ_TILE_ATTR, (const void *)ObjTileAttr,
                  "ObjTileAttr", 1);
    patch_replace(ADDR_TILE_ATTR_AT, (const void *)TileAttrAt, "TileAttrAt", 1);
    patch_replace(ADDR_OBJ_MARK_IF_OVERDUE, (const void *)ObjMarkIfOverdue,
                  "ObjMarkIfOverdue", 1);
    patch_replace(ADDR_OBJ_HEIGHT, (const void *)ObjHeight, "ObjHeight", 1);
    patch_replace(ADDR_ITEM_PRE_DESTROY_ALIAS, (const void *)ItemPreDestroyAlias,
                  "ItemPreDestroyAlias", 2);
    patch_replace(ADDR_ROW_RELEASE, (const void *)RowRelease, "RowRelease", 5);
}
