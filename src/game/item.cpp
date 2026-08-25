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
#include "msgslot.h"  /* CommMustBroadcast -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"

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
typedef void (__cdecl *AM2_RowReleaseFn)(void *row, void *desc);
#define orig_row_unregister_all \
            ((AM2_RowReleaseFn)(uintptr_t)ADDR_ROW_UNREGISTER_ALL)

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

    orig_row_unregister_all(r, desc);
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
 * from the shape TakeOffMap uses.
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
#define orig_destroy_type2      ((am2_destroy_fn)AM2_IMAGE(ADDR_DESTROY_TYPE2))
#define orig_destroy_type3      ((am2_destroy_fn)AM2_IMAGE(ADDR_DESTROY_TYPE3))
#define orig_destroy_type8      ((am2_destroy_fn)AM2_IMAGE(ADDR_DESTROY_TYPE8))
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
    case 2:  orig_destroy_type2(obj); break;
    case 3:  orig_destroy_type3(obj); break;
    case 8:  orig_destroy_type8(obj); break;
    default: DestroyObjCommon(obj); break;
    }

    if (CommMustBroadcast(kItemComm,
                                 (int16_t)*(const int8_t *)((const uint8_t *)obj
                                                            + OBJ_OFF_ARMY)))
        SendObjDestroyed(obj);
}

typedef void (__cdecl *am2_row_unregister_fn)(void *row, int32_t a, void *desc);
#define orig_row_unregister \
            ((am2_row_unregister_fn)AM2_IMAGE(ADDR_ROW_UNREGISTER))
#define orig_item_teardown  ((am2_destroy_fn)AM2_IMAGE(ADDR_ITEM_TEARDOWN))

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
                       + (uint32_t)i * AM2_ROW_STRIDE;

        ObjFlagClear0(row);
        orig_row_unregister(row, 0, (void *)AM2_IMAGE(ADDR_MAP_DESC));
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
