/* item.cpp -- see item.h.
 *
 * Taking a translation unit whole rather than picking leaves off by size: the
 * struct offsets and the names only make sense together. The item half of this
 * unit names itself generously -- "DeployItem(resurrection): uid:%x,
 * health:%d", "DestroyItemObject, %x", "itemGoneMessageSend uid %x item_type
 * %d" -- so the fields have real names to be read off rather than invented.
 */
#include <stdint.h>
#include <string.h>

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
#include "map.h"      /* TileOfPoint */
#include "air.h"      /* RevealNearby */
#include "objscript.h" /* UpdateObjectScript */
#include "msgslot.h"   /* CommMustBroadcast */
#include "packkey.h"  /* KeyLookupTriple */
#include "gameproc.h"  /* SaveOneItem */

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header because this module is on the flat side of the
 * split and audio.h names Win32 types -- the same reason air.cpp and
 * commmsg.cpp do it, and spelled the same way so the three cannot drift.
 * `extern "C"` is correct: audio.h's block spans that declaration, unlike
 * LoadAudioSection below it, which gameproc.cpp declares with C++ linkage. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int32_t x, int32_t y);


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
/* 0x00448880, two callers, 64 bytes. The CODE of whatever weapon is in a
 * unit's hand -- the selected inventory slot's uid, looked up, checked to be a
 * weapon, and the dword its OBJ_OFF_FIELD_C0 record points at. Zero when the
 * slot is empty or holds something that is not a weapon.
 *
 * It is the same three steps SaveType2 takes to build its tag and the same
 * dword ThingCode switches on, so the return is the code a data file carries
 * and not an index into anything. What differs is the middle step: this uses
 * ObjIsType4 where SaveType2 uses WeaponByUid, so a non-weapon here answers 0
 * in silence where WeaponByUid would complain to the log. Two spellings of one
 * check, and the quiet one is the one on this path.
 *
 * FOUR THINGS ARE UNGUARDED and all four are the original's. The unit is
 * dereferenced without a null test; the slot index is used without a range
 * test, so a selection outside 0..5 reads past the array; LookupByUID's answer
 * goes straight into ObjIsType4, which is safe only because that accessor
 * opens on a null test of its own; and the OBJ_OFF_FIELD_C0 pointer is
 * dereferenced without one, which is safe only because a weapon always has the
 * record. Reproduced rather than tidied.
 *
 * Both callers do the same thing with the answer -- compare it against 20 --
 * and neither is reconstructed, so what 20 is stays for whoever reads them.
 * The name claims only what the body computes.
 *
 * MEASURED, and the guess made when this landed was wrong. Both callers are
 * the original's and reach this by address, so the counter can move -- and it
 * was written up as "expected to read 0, nothing here puts a weapon in hand
 * and asks". It reads 2,642 on a Boot Camp mission standing still and 12,293
 * after four rounds of walking and firing. So this is not verified by reading
 * at all: the A/B that passed it ran it thousands of times.
 *
 * The lesson is the one already in CLAUDE.md about counts of 0, one step
 * earlier: predicting a counter is not measuring it, and a prediction costs
 * nothing to check.
 */
int32_t __cdecl HeldWeaponCode(void *unit)
{
    const uint8_t *u = (const uint8_t *)unit;
    uint32_t       uid;
    AM2_Object    *obj;

    uid = *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                              + (size_t)*(const int32_t *)
                                    (u + UNIT_OFF_INVENTORY_SEL) * 4);
    if (!uid)
        return 0;

    obj = (AM2_Object *)LookupByUID(uid);
    if (!ObjIsType4(obj))
        return 0;

    return **(const int32_t *const *)((const uint8_t *)obj
                                      + OBJ_OFF_FIELD_C0);
}

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
        SaveOneItem(fp, obj);
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
        LoadOneItem(fp, 0);
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
    DestroyItemObject(trooper, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC,
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
    DestroyItemObject(vehicle, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC,
                      unlink);
    am2_free(vehicle);
}

void __cdecl DestroyItemCommon(void *item, int32_t unlink)
{
    if (!item)
        return;

    FreeSubrecordRows((uint8_t *)item + OBJ_OFF_SUBRECORD);
    DestroyItemObject(item, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC, unlink);
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
    DestroyItemObject(item, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC, unlink);
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
    DestroyItemObject(weapon, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC, unlink);
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

/* Defined below, beside WeaponByUid. */
void __cdecl SelectInventorySlot(void *unit, int32_t slot);

    sel = *(const int32_t *)(u + UNIT_OFF_INVENTORY_SEL);
    if (sel == slot) {
        /* What was in hand has gone: reset and re-select, if the object
         * agrees it should. */
        *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = 0;
        if (ObjType2Field548((const AM2_Object *)unit))
            SelectInventorySlot(unit, 0);
        return;
    }
    /* Above the hole slides down; below is left alone. Equal was handled
     * above and cannot reach here. */
    if (sel > slot)
        *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = sel - 1;
}

#define g_tileAttrs (*(const uint8_t **)(uintptr_t)ADDR_TILE_ATTRS)
#define g_tileFlags (*(const uint8_t **)(uintptr_t)ADDR_TILE_FLAGS)

int32_t __cdecl ObjTileAttr(const void *obj)
{
    uint32_t tile = *(const uint16_t *)((const uint8_t *)obj + OBJ_OFF_TILE);

    return *(const int8_t *)(g_tileAttrs + tile);
}

int32_t __cdecl TileAttrAt(uint32_t tile)
{
    return *(const int8_t *)(g_tileAttrs + (tile & 0xFFFFu));
}

/* Still original: the cell query HeightAtPoint walks, and the two halves of
 * ObjTileChanged's "something moved" path. */
typedef void (__cdecl *AM2_ObjHookFn)(void *obj);
typedef void (__cdecl *AM2_ObjRemapFn)(void *obj, void *desc, int32_t force);
#define orig_obj_tile_hook ((AM2_ObjHookFn)(uintptr_t)ADDR_OBJ_TILE_HOOK)
#define orig_obj_remap     ((AM2_ObjRemapFn)(uintptr_t)ADDR_OBJ_REMAP)


typedef void *(__cdecl *AM2_ObjectsAtFn)(const uint32_t *pt, void *desc);
#define orig_objects_at_point ((AM2_ObjectsAtFn)(uintptr_t)ADDR_OBJECTS_AT_POINT)

/* Still original. Declared here rather than with the other teardown seams
 * further down, because VehicleDied is above them. */
typedef void (__cdecl *AM2_ObjOnlyFn)(void *obj);
#define orig_obj_clear_footprint \
            ((AM2_ObjOnlyFn)AM2_IMAGE(ADDR_OBJ_CLEAR_FOOTPRINT))

/* Still original: the tail both death handlers share, and the ten-argument
 * maker. The second was already declared further down this file, so it moved
 * up here rather than being written a second time -- its fifth argument is a
 * uid at that other site, which is why it is uint32_t and why this caller
 * passes a plain 0 rather than inventing a name for it. */
typedef void (__cdecl *AM2_DiedTailFn)(void *obj, int32_t a);
typedef void (__cdecl *AM2_SpawnAtFn)(int32_t x, int32_t y, int32_t kind,
                                      int32_t army, uint32_t uid, int32_t extra,
                                      int32_t e, int32_t f, int32_t g,
                                      int32_t h);
#define orig_trooper_died_tail ((AM2_DiedTailFn)(uintptr_t)ADDR_TROOPER_DIED_TAIL)
#define orig_spawn_at          ((AM2_SpawnAtFn)(uintptr_t)ADDR_SPAWN_AT)

/* Still original: types 1 and 4 have their own height handler. */
typedef void (__cdecl *AM2_ApplyHeight14Fn)(void *obj, int32_t height);
#define orig_apply_height_1_4 \
            ((AM2_ApplyHeight14Fn)(uintptr_t)ADDR_APPLY_HEIGHT_1_4)

/* Still original: the teardown PointActionC opens with, and the notify it
 * ends on. */
typedef void (__cdecl *AM2_AfterMoveFn)(void *obj, int32_t a, int32_t b);
#define orig_obj_after_move ((AM2_AfterMoveFn)(uintptr_t)ADDR_OBJ_AFTER_MOVE)
typedef void (__cdecl *AM2_ObjOnlyFn2)(void *obj);
#define orig_item_teardown_early \
            ((AM2_ObjOnlyFn2)AM2_IMAGE(ADDR_ITEM_TEARDOWN))

/* Still original: the common object initialiser, eight callers across the
 * type makers. */
typedef void (__cdecl *AM2_ObjInitFn)(void *obj, void *dir, int32_t type,
                                      uint32_t pt, const char *name,
                                      int32_t e, int32_t f);
#define orig_obj_init_common ((AM2_ObjInitFn)(uintptr_t)ADDR_OBJ_INIT_COMMON)

/* 0x00435550, five callers -- the maker LoadType7 uses, and the one that
 * refuses a thirty-third kind-7 object.
 *
 * THE COUNT IS INCREMENTED BEFORE THE CHECK AND NOT PUT BACK. A refused
 * attempt leaves ADDR_KIND7_COUNT one higher than the number alive, so once
 * the limit is reached every further attempt pushes it further out and only
 * ADDR_FREE_ITEM_KIND7's decrements -- which clamp at zero -- bring it down.
 * Bounded at both ends and not symmetric in between; reproduced, because a
 * reconstruction that decremented on refusal would be tidier and would let a
 * thirty-third through after enough failures.
 *
 * ITS FOURTH ARGUMENT ENDS UP AT OBJ_OFF_FACING, which is exactly the field
 * LoadType7 reads to fill it -- so the two agree about the shape from opposite
 * sides, and that is better than either alone. Its SECOND argument is read
 * nowhere.
 *
 * The object is 0x94 bytes -- the header and nothing else -- cleared whole,
 * then given its army, a flags word of 1, and a deadline of one second from
 * now. The name handed to the initialiser is the empty string.
 */
void *__cdecl MakeKind7(uint32_t pt, int32_t unused, int32_t army,
                        int32_t facing, int32_t e, int32_t f)
{
    int32_t *count = (int32_t *)(uintptr_t)ADDR_KIND7_COUNT;
    uint8_t *o;

    (void)unused;

    if (++*count > AM2_KIND7_MAX)
        return (void *)0;

    o = (uint8_t *)am2_malloc(AM2_ITEM_HEADER_BYTES);
    memset(o, 0, AM2_ITEM_HEADER_BYTES);

    *(o + OBJ_OFF_ARMY)             = (uint8_t)army;
    *(uint32_t *)(o + OBJ_OFF_FLAGS) = 1;

    orig_obj_init_common(o, (void *)(uintptr_t)ADDR_DIR_SCRATCH, 7, pt,
                         (const char *)AM2_IMAGE(ADDR_STR_EMPTY), e, f);

    *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) =
        *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS + AM2_KIND7_FUSE_MS;
    *(o + OBJ_OFF_FACING) = (uint8_t)facing;
    return o;
}

/* 0x00428F80, two callers -- adjacent sites in one function. Move an object to
 * a point and take every one of its rows with it.
 *
 * THE SECONDARY ROWS ARE OFFSET BY THE FIRST SPRITE'S attachX AND attachY, and
 * that is what identifies those two fields. sprite.h had them as fileA and
 * fileB with "what they MEAN is still not established"; this adds them to rows
 * 1..n as an X and a Y, so they are where an attached row sits relative to the
 * one carrying the sprite. A turret on its body. One reader, but an
 * unambiguous one, and the fields are renamed for it.
 *
 * A NULL SPRITE ON ROW 0 ABANDONS THE WHOLE FUNCTION rather than just the row
 * loop -- the branch goes to the epilogue, so ObjTileChanged and the notify
 * are skipped as well. The object has already been moved by then, so it ends
 * up at the new position with its map registration not brought up to date.
 * Reproduced.
 *
 * The row count is re-read at every one of the four places it is tested, and
 * the first sprite is re-read inside the loop through a held pointer to the
 * field rather than a copy of it. Both are the original's shape.
 *
 * The row-1 offsets are added as 16-BIT arithmetic, on top of a position that
 * was just written as a dword -- so an attach offset that carries out of the
 * low half lands in the row's Y rather than wrapping its X. */
void __cdecl PointActionC(void *obj, uint32_t point)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *rows;
    int32_t  i;

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) <= 0)
        return;

    rows = *(uint8_t **)(o + OBJ_OFF_ROWS);
    orig_item_teardown_early(o);

    *(uint32_t *)(o + OBJ_OFF_POS)      = point;
    *(uint32_t *)(rows + ROW_OFF_X)     = point;
    RowUpdate(rows, 1, (void *)(uintptr_t)ADDR_MAP_DESC);

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) <= 1)
        goto done;

    if (!*(void *const *)(rows + ROW_OFF_SPRITE))
        return;                    /* the whole function, not just the loop */

    *(uint32_t *)(rows + ROW_OFF_X) = *(const uint32_t *)(o + OBJ_OFF_POS);
    *(int16_t *)(rows + ROW_OFF_Y_ADJUST) =
        *(const int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST);

    for (i = 1; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        uint8_t *r   = *(uint8_t **)(o + OBJ_OFF_ROWS)
                       + (uint32_t)i * AM2_OBJ_ROW_STRIDE;
        uint8_t *spr;

        *(uint32_t *)(r + ROW_OFF_X) = *(const uint32_t *)(o + OBJ_OFF_POS);

        spr = *(uint8_t **)(rows + ROW_OFF_SPRITE);
        *(int16_t *)(r + ROW_OFF_X) =
            (int16_t)(*(const int16_t *)(r + ROW_OFF_X)
                      + *(const int16_t *)(spr + SPRITE_OFF_ATTACH_X));
        spr = *(uint8_t **)(rows + ROW_OFF_SPRITE);
        *(int16_t *)(r + ROW_OFF_Y) =
            (int16_t)(*(const int16_t *)(r + ROW_OFF_Y)
                      + *(const int16_t *)(spr + SPRITE_OFF_ATTACH_Y));

        RowUpdate(r, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

done:
    ObjTileChanged(o, 0, 0);
    orig_obj_after_move(o, 1, 0);
}

/* 0x004278E0, four callers -- ObjTileChanged's tail among them. Give an object
 * a height and push it into the depth sort.
 *
 * A ZERO HEIGHT MEANS "TAKE THE TILE'S OWN", read through ADDR_TILE_ATTRS at
 * OBJ_OFF_TILE. So 0 is not a height, it is a request -- and a caller that
 * genuinely wants a height of zero cannot say so.
 *
 * Four arms over `type - 1` and only three distinct bodies: the jump table has
 * 0x0042790C twice, for types 1 and 4, which is the same pairing SaveType4 and
 * ADDR_STEP_TYPE1_4 show. Those two do not touch a row at all.
 *
 * TYPE 3 IS THE ONE WITH TWO ROWS. It writes the first row's depth layer and
 * then the SECOND's when the count is above one -- the same second row
 * VehicleDied hides. Everything else writes the first only.
 *
 * TYPE 2 IS THE ONE THAT DOES NOT CHECK. The default arm tests the row count
 * before touching a row; the type-2 arm jumps into the same tail without
 * testing, so a type 2 with no rows writes through a null. Reproduced, because
 * it is the original's and every type 2 in a mission has a row.
 *
 * The second call re-reads OBJ_OFF_HEIGHT_SET rather than reusing the value it
 * just stored. Same number either way; written as the original has it. */
void __cdecl ApplyObjHeight(void *obj, int32_t height)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *rows;

    if (height == 0)
        height = TileAttrAt(*(const uint16_t *)(o + OBJ_OFF_TILE));

    switch (*(const int32_t *)o) {
    case 1:
    case 4:
        orig_apply_height_1_4(o, height);
        return;

    case 2:
        *(o + OBJ_OFF_HEIGHT_SET) = (uint8_t)height;
        break;

    case 3:
        *(o + OBJ_OFF_HEIGHT_SET) = (uint8_t)height;
        rows = *(uint8_t **)(o + OBJ_OFF_ROWS);
        *(int16_t *)(rows + OBJ_OFF_DEPTH_LAYER) =
            (int16_t)ScaleBy32Blocks((int8_t)height);
        if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 1)
            *(int16_t *)(rows + AM2_OBJ_ROW_STRIDE + OBJ_OFF_DEPTH_LAYER) =
                (int16_t)ScaleBy32Blocks(
                    *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET));
        return;

    default:
        *(o + OBJ_OFF_HEIGHT_SET) = (uint8_t)height;
        if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) <= 0)
            return;
        break;
    }

    rows = *(uint8_t **)(o + OBJ_OFF_ROWS);
    *(int16_t *)(rows + OBJ_OFF_DEPTH_LAYER) =
        (int16_t)ScaleBy32Blocks((int8_t)height);
}

/* 0x00447E50, one caller -- VehicleDied's twin, and the same first move: take
 * the object off the map if flag bit 0 is set, then clear it.
 *
 * WHAT IT SPAWNS BELONGS TO THE KILLER. `by` is a uid; the army the spawn gets
 * is that object's OBJ_OFF_ARMY, or AM2_ARMY_NEUTRAL when the uid no longer
 * resolves. So a kill by someone who has since died is credited to nobody
 * rather than to the victim -- which is the sort of thing a reconstruction
 * would quietly get wrong by defaulting to the victim's own army.
 *
 * The spawn is gated on OBJ_OFF_FIELD_5A4 being non-zero. orig.h has that
 * field as "structural until something says what it counts"; this does not
 * settle it either, but it adds a second reader to ADDR_TYPE2_FIELD5A4_SET's
 * and both treat it as a permission rather than a quantity.
 *
 * The middle argument is read nowhere here and passed straight to the tail --
 * unlike VehicleDied's, which is read nowhere at all. */
void __cdecl TrooperDied(void *obj, int32_t a, uint32_t by)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & MAPOBJ_FLAG_VISIBLE) {
        ItemPreDestroyAlias(o, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC);
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)MAPOBJ_FLAG_VISIBLE;
    }

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_5A4)) {
        const uint8_t *killer = (const uint8_t *)LookupByUID(by);
        int32_t        army   = killer
                                ? *(const int8_t *)(killer + OBJ_OFF_ARMY)
                                : AM2_ARMY_NEUTRAL;

        orig_spawn_at(*(const int16_t *)(o + OBJ_OFF_X),
                      *(const int16_t *)(o + OBJ_OFF_Y),
                      AM2_SPAWN_KIND_95, army, 0,
                      *(const int32_t *)(uintptr_t)ADDR_SPAWN_EXTRA_6628D4,
                      0, 0, 0, 0);
    }

    orig_trooper_died_tail(o, a);
}

/* 0x0045B630, one caller. A vehicle has died: mark it, hide its second row,
 * clear its footprint and take it off the map.
 *
 * ITS SECOND ARGUMENT IS UNUSED. The body never reads it, and the name kept it
 * because the trooper twin at ADDR_TROOPER_DIED does use one. Reproduced with
 * the parameter present and ignored -- dropping it would change the shape the
 * one caller uses, and a `(void)` says more than a missing argument would.
 *
 * THE ROW IT HIDES IS THE SECOND, and only when there is one: the guard is
 * `rowCount > 1` and the row it clears is `rows + AM2_OBJ_ROW_STRIDE`. A
 * vehicle carries two map objects and this puts out the second; nothing read
 * so far says which is which, only that the first survives.
 *
 * The unregister is conditional on flag bit 0 and clears it afterwards, so a
 * vehicle that dies twice is taken off the map once. That idempotence is the
 * flag's, not the caller's.
 */
void __cdecl VehicleDied(void *obj, uint32_t by)
{
    uint8_t *o = (uint8_t *)obj;

    (void)by;

    *(int32_t *)(o + VEHICLE_OFF_DEATH_STATE) = 5;
    *(int32_t *)(o + VEHICLE_OFF_DEAD)        = 1;

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 1)
        ObjFlagClear0(*(uint8_t **)(o + OBJ_OFF_ROWS) + AM2_OBJ_ROW_STRIDE);

    orig_obj_clear_footprint(o);

    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & MAPOBJ_FLAG_VISIBLE) {
        ItemPreDestroyAlias(o, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC);
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)MAPOBJ_FLAG_VISIBLE;
    }
}

/* 0x004294C0, fifteen callers. Recompute an object's tile from its position
 * and, if anything moved, put it back on the map and re-apply its height.
 *
 * THREE THINGS HAVE TO BE TRUE TO SKIP THE WORK -- the position equal to
 * OBJ_OFF_PREV_POS, the tile equal to what it was, and `force` zero. Written
 * as the original's three-branch chain rather than as one `if`, because the
 * middle test reads the tile the first half of this function has just saved
 * and the order is what makes that safe.
 *
 * The hook runs whenever OBJ_OFF_FLAGS bit 3 is CLEAR, and it runs BEFORE the
 * early exit -- so it is not part of the "something moved" path however much
 * its position in the body suggests it. Reproduced in place.
 *
 * OBJ_OFF_PREV_TILE is written as a dword from a zero-extended uint16 and
 * compared as one, which is why it is not `int16_t` here. It is also not the
 * map object's ROW_OFF_X: the same offset in a different structure, and the
 * two had a name each under different prefixes until this arrived.
 *
 * CHECKED WITH objdump.py, WHICH IS WHAT IT IS FOR. 146 calls before the
 * briefing and 24,938 in one live mission, and the pixels still see nothing:
 * adding 1 to the tile leaves `bootcamp` at 76. Reading the field instead is
 * exact -- the leader's OBJ_OFF_TILE is 0x407C on a correct build and 0x407D
 * with that mutation, and OBJ_OFF_PREV_TILE follows it as 0x0000407C against
 * 0x0000407D, which also confirms the dword width.
 *
 * The `force` arm has no such check. Ignoring it entirely leaves `mission` at
 * 281 and `bootcamp` at 76, and it writes no field to read back, so that one
 * stays verified by reading. */
void __cdecl ObjTileChanged(void *obj, int32_t height, int32_t force)
{
    uint8_t *o = (uint8_t *)obj;

    *(uint32_t *)(o + OBJ_OFF_PREV_TILE) = *(const uint16_t *)(o + OBJ_OFF_TILE);
    *(uint16_t *)(o + OBJ_OFF_TILE) =
        (uint16_t)TileOfPoint(*(const uint32_t *)(o + OBJ_OFF_POS));

    if (!(*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_NO_TILE_HOOK))
        orig_obj_tile_hook(o);

    if (PointsEqual(*(const uint32_t *)(o + OBJ_OFF_PREV_POS),
                    *(const uint32_t *)(o + OBJ_OFF_POS))
        && *(const uint32_t *)(o + OBJ_OFF_PREV_TILE)
               == (uint32_t)*(const uint16_t *)(o + OBJ_OFF_TILE)
        && force == 0)
        return;

    orig_obj_remap(o, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC), force);
    ApplyObjHeight(o, height);
}

/* 0x0042A1B0, five callers -- the precise hit test at a world point.
 *
 * Every object in the cell the point falls in whose own OBJ_OFF_HIT_RECT
 * contains it, chained through OBJ_OFF_QUERY_NEXT and returned newest-first.
 * The sibling ADDR_OBJECTS_AT_POINT asks a looser question of the same cell --
 * it builds a box from four offsets at +0x7C -- so these two are not
 * duplicates however alike the shapes look.
 *
 * IT WENT IN AS "the mouse pick" AND IT IS NOT ONE. That came from three of
 * its callers sitting in the HUD band, which is naming a function from its
 * call site -- and the comment that said so cited the callers as evidence
 * while doing it. Reading them settles it: two build the point from a world
 * origin plus a table offset, one from PointOfTile, one from a float
 * projection. Not one passes a cursor.
 *
 * BOTH BOUNDS ARE COLS. The row coordinate is checked against `cols - 1` and
 * not `rows - 1`, which reads as a slip until MapDescInit is read: the grid is
 * allocated `cols << shift` entries and this is the bound it actually has.
 * Third place in the tree that has to say so.
 *
 * The bitmask test is CONDITIONAL on the object having a mask at all. A null
 * OBJ_OFF_HIT_MASK means the rectangle was the whole question, which is what
 * lets a plain item be picked without one. ObjMaskBitAt is misc.cpp's and was
 * about to be reached through the image under a second name -- the alias
 * ratchet and checkseams both said so, on the same address, in one run.
 *
 * 3,872 CALLS AND NOTHING WATCHES THE ANSWER. Returning null unconditionally
 * -- checked that the edit landed before believing the result -- leaves
 * `mission` at 281 and `bootcamp` at 22, both at their floors. That stands
 * whatever the function is for; what changed is the fix. Clicking on a unit
 * would not have closed it, because no caller here reads the cursor.
 * Verified by reading. */
void *__cdecl ObjectsHitByPoint(const uint32_t *pt, const void *desc)
{
    const uint8_t *d = (const uint8_t *)desc;
    int32_t  cols    = *(const int32_t *)(d + MAPDESC_OFF_COLS);
    int32_t  cx      = (int32_t)*(const int16_t *)pt >> AM2_CELL_SHIFT;
    int32_t  cy      = (int32_t)*((const int16_t *)pt + 1) >> AM2_CELL_SHIFT;
    uint8_t *head    = (uint8_t *)0;
    uint8_t *node;

    if (cx < 0 || cx > cols - 1 || cy < 0 || cy > cols - 1)
        return (void *)0;

    node = ((uint8_t *const *)(*(const uint8_t *const *)
                (d + MAPDESC_OFF_CELLS)))
           [(cy << *(const int32_t *)(d + MAPDESC_OFF_SHIFT)) + cx];

    for (; node; node = *(uint8_t **)(node + CELL_NODE_OFF_NEXT)) {
        uint8_t *o = *(uint8_t **)(node + CELL_NODE_OFF_OBJ);

        if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            continue;
        if (!PointInRect((const AM2_Rect *)(o + OBJ_OFF_HIT_RECT),
                         (const AM2_Point *)pt))
            continue;
        if (*(void *const *)(o + OBJ_OFF_HIT_MASK)
            && !ObjMaskBitAt(o, (const AM2_Point *)pt))
            continue;

        *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT) = head;
        head = o;
    }

    return head;
}

/* 0x00448F00, three callers, 176 bytes. The TOTAL obstruction between an
 * object and a map point, in three parts and in the original's order: every
 * object standing at the point, then the tile's own blocking bit, then a
 * height step between the object's tile and the point's.
 *
 * It walks with ObjectsAtPoint through OBJ_OFF_QUERY_NEXT, the same scratch
 * chain HeightAtPoint below reads, and accumulates ObjBlockWeight per object.
 * REACHING AM2_BLOCK_FULL ENDS IT THERE -- the tile checks are skipped
 * entirely, not merely added to, so a point already blocked by an object never
 * consults the terrain at all. Written as the early return it is.
 *
 * The three arguments it passes down are not the three it was given. Its own
 * point goes into ObjBlockWeight's UNUSED third parameter and the reference
 * point into the fourth, which is the one that gets read; that is why this
 * function has two points and why swapping them would be invisible to a
 * compiler and fatal to the answer.
 *
 * THE OUT-OF-RANGE GUARD CANNOT FIRE, and it is reproduced anyway. The
 * original masks TileOfPoint's answer to 16 bits and then branches on it being
 * negative or above 0xFFFF -- `25 ff ff 00 00` followed by `7c 44` and
 * `3d ff ff 00 00 / 7f 3d`. After the mask neither can be true, so the
 * `return 0xFF` those two branches reach is dead code. It is written out here
 * because a reader comparing against the disassembly should find it, and
 * because "the original checks and the check is vacuous" is a different fact
 * from "the original does not check".
 *
 * The height step is skipped when there is no object, since it needs that
 * object's OBJ_OFF_TILE. Both heights are signed bytes out of ADDR_TILE_ATTRS,
 * which is the table item.cpp already calls a height everywhere else.
 *
 * MEASURED, and it explains the zero underneath it. This runs EIGHT times on
 * a driven Boot Camp mission -- title, briefing, both dialogs, then four
 * rounds of walking and firing -- while ObjBlockWeight stays at 0 on the same
 * run. Both counters were 0 before this landed, and the obvious reading of
 * that pair was that neither ran. It was wrong: the caller ran and its loop
 * body did not, because ObjectsAtPoint found nothing standing at those eight
 * points. So ObjBlockWeight is reached-but-empty rather than unreached, which
 * is a third thing a counter of 0 can mean and is not the blind spot.
 *
 * Eight calls is thin, and worth saying so rather than calling it covered:
 * the tile arm and the height arm are compared by the A/B, the object loop is
 * not entered at all, and the vacuous guard is unreachable by construction.
 */
int32_t __cdecl BlockWeightAt(void *from, uint32_t at, uint32_t ref)
{
    uint8_t *o;
    int32_t  total = 0;
    uint32_t tile;

    o = (uint8_t *)orig_objects_at_point(
            &at, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC));

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        total += ObjBlockWeight(from, o, (int32_t)at, ref);
        if (total >= AM2_BLOCK_FULL)
            return total;
    }

    tile = (uint32_t)TileOfPoint(at) & 0xFFFFu;
    /* The original's range check, kept and vacuous; see above. */
    if ((int32_t)tile < 0 || (int32_t)tile > 0xFFFF)
        return 0xFF;

    if (g_tileFlags[tile] & AM2_TILE_BLOCKS)
        total += AM2_BLOCK_FULL;

    if (from) {
        int32_t d = (int32_t)(int8_t)TileAttrAt(tile)
                  - (int32_t)(int8_t)TileAttrAt(
                        *(const uint16_t *)((const uint8_t *)from
                                            + OBJ_OFF_TILE));

        if (d < 0)
            d = -d;
        if (d > AM2_BLOCK_HEIGHT_STEP)
            total += AM2_BLOCK_FULL;
    }

    return total;
}

/* 0x0045B690, two callers, 112 bytes. The sibling of BlockWeightAt above, and
 * the two differences are the whole of it.
 *
 * THE CHAIN IS GIVEN, NOT QUERIED. Both callers run ObjectsAtPoint themselves
 * and hand the head in, so this does no lookup; it still walks
 * OBJ_OFF_QUERY_NEXT and still stops the moment the total reaches
 * AM2_BLOCK_FULL. That is why it is 112 bytes where its sibling is 176.
 *
 * THE TERRAIN TERM IS THE OTHER BIT, AND THE OTHER WAY ROUND. BlockWeightAt
 * penalises AM2_TILE_BLOCKS being SET; this penalises AM2_TILE_OPEN being
 * CLEAR. Two bits of one table asked opposite questions, which is worth
 * transcribing carefully rather than reading as a copy of the sibling -- a
 * `jne` where the sibling has a `je` is one character in a disassembly and
 * inverts the whole terrain contribution. There is no height step here at all.
 *
 * The argument shuffle is the sibling's: this function's point goes into
 * ObjBlockWeight's unused third parameter and `ref` into its fourth.
 *
 * One caller passes a LITERAL 0 as the object, so ObjBlockWeight's no-viewer
 * arm -- the one that skips the height test and returns AM2_BLOCK_FULL for
 * anything that is not an item -- is genuinely reachable and not merely
 * defensive. The same caller picks between this and 0x0045B7E0 on a value
 * being 5, so there is at least a third member of this family unread.
 *
 * ITS DEAD GUARD IS SPELLED DIFFERENTLY FROM THE SIBLING'S, which is the
 * reason to write it out twice rather than once. BlockWeightAt masks and then
 * tests signed 32-bit; this tests `ax` UNSIGNED against 0xFFFF first and masks
 * afterwards. Neither can fire and both reach a `return 0xFF`. Two spellings
 * of one vacuous check is evidence the compiler produced them from different
 * source, not that one is a transcription slip.
 *
 * MEASURED AT 0, and here that matters more than usual. Both callers are the
 * original's and reach this by address, so the counter is not blind -- they
 * simply do not run on any drive this project has, while BlockWeightAt beside
 * it reads 8 on the same run. So every word above is verified by reading and
 * by nothing else, and the inverted terrain term is precisely the kind of
 * one-character error no A/B here could ever report. That is why the polarity
 * is spelled out rather than left to the code.
 */
int32_t __cdecl BlockWeightChain(void *from, uint32_t at, void *chain,
                                 uint32_t ref)
{
    uint8_t *o     = (uint8_t *)chain;
    int32_t  total = 0;
    uint32_t tile;

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        total += ObjBlockWeight(from, o, (int32_t)at, ref);
        if (total >= AM2_BLOCK_FULL)
            return total;
    }

    tile = (uint32_t)TileOfPoint(at);
    /* The original's guard, on the low 16 bits and before the mask below.
       Vacuous; see above. */
    if ((tile & 0xFFFFu) > 0xFFFFu)
        return 0xFF;
    tile &= 0xFFFFu;

    if (!(g_tileFlags[tile] & AM2_TILE_OPEN))
        total += AM2_BLOCK_FULL;

    return total;
}

/* 0x0042A820, five callers. The ground height at a point, raised by anything
 * standing on it.
 *
 * The walk is ObjectsAtPoint's, still original: it collects every object in
 * the cell the point falls in and chains them through OBJ_OFF_QUERY_NEXT. The
 * chain is scratch and lives only until the next such query, which is why
 * this reads it out in one pass and keeps nothing.
 *
 * Three conditions and all three are needed. ObjIsItem, so types 1 and 4 --
 * a soldier standing on a tile does not raise it. The SIGN of the low byte at
 * OBJ_OFF_RANK, which for those types is not a rank at all: 0..7 could never
 * make it negative, and the object is a union past its header. And the height
 * being higher than what is already there, compared
 * SIGNED as a byte, which is the same int8 the rest of the height family
 * uses.
 *
 * IT RETURNS A BYTE AND ONLY A BYTE. The original ends `mov al, bl` over an
 * eax still holding the masked tile index, so the upper bits carry the tile
 * rather than an answer. Log2Mask's problem exactly.
 *
 * Verified by reading, with the measurement said out loud: 12 calls in one
 * live mission, and adding 40 to the answer leaves `mission` at 299 and
 * `bootcamp` at 76 -- both inside their bands. Twelve calls over a whole
 * mission is a thin path and the frame does not show what they decide. */
uint8_t __cdecl HeightAtPoint(uint32_t packedPoint)
{
    int8_t  best = (int8_t)TileAttrAt((uint32_t)TileOfPoint(packedPoint));
    uint8_t *o   = (uint8_t *)orig_objects_at_point(
                       &packedPoint, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC));

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        int8_t h;

        if (!ObjIsItem((const AM2_Object *)o))
            continue;
        if (*(const int8_t *)(o + OBJ_OFF_RANK) >= 0)
            continue;

        h = *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET);
        if (h > best)
            best = h;
    }

    return (uint8_t)best;
}

/* 0x00459FB0, four callers. A uid to a UNIT.
 *
 * The accepted set is types 2, 3 and 8 -- trooper, vehicle, roach -- which is
 * exactly what ObjIsType2, ObjIsType3 and ObjIsType8 answer for, so `unit` is
 * the word rather than something structural. Written as the original's two
 * comparisons rather than as three: `t >= 2 && t <= 3` then `t == 8`, because
 * that is one branch fewer and it is what is there.
 *
 * Uid 0 is refused before the lookup and the test is UNSIGNED, so a uid with
 * the top bit set still reaches LookupByUID. */
void *__cdecl UnitByUid(uint32_t uid)
{
    const int32_t *o;

    if (uid == 0)
        return (void *)0;

    o = (const int32_t *)LookupByUID(uid);
    if (!o)
        return (void *)0;

    if (*o < 2)
        return (void *)0;
    if (*o <= 3 || *o == 8)
        return (void *)o;

    return (void *)0;
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
        ItemPreDestroyAlias(obj, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC);

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

/* ChangeObjectFrame is ours now, shared with event.cpp and
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
#define orig_deploy_trooper ((AM2_DeployTypeFn)(uintptr_t)ADDR_DEPLOY_TROOPER)
#define orig_deploy_vehicle ((AM2_DeployTypeFn)(uintptr_t)ADDR_DEPLOY_VEHICLE)
#define orig_place_obj      ((AM2_PlaceObjFn)(uintptr_t)ADDR_PLACE_OBJ)

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

    SendItemDeploy(obj, resurrect);
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
        ObjFrameStep(obj);

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

/* Defined below, beside the rest of the animation family. */
int32_t __cdecl ChangeObjectFrame(void *obj, int32_t frame, int32_t flag);

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
            ChangeObjectFrame(obj, 0, 0);
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
typedef void (__cdecl *AM2_SendDeathMsgFn)(void *obj, uint32_t attacker,
                                            int32_t kind);
typedef void (__cdecl *AM2_ObjOnlyFn)(void *obj);

#define orig_damage_item      ((AM2_DamageItemFn)(uintptr_t)ADDR_DAMAGE_ITEM)
#define orig_damage_trooper   ((AM2_DamageTypeFn)(uintptr_t)ADDR_DAMAGE_TROOPER)
#define orig_damage_vehicle   ((AM2_DamageTypeFn)(uintptr_t)ADDR_DAMAGE_VEHICLE)

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

/* 0x00427EF0, the fourth member of the same template and the same two-party
 * shape as NotifyDamaged above -- three zeros pushed before the branch, both
 * arms sharing them, no delay so the masks always survive. What is new here is
 * only the literal.
 *
 * Kind 7 is `pickedup`. The evidence is the script's vocabulary and the call
 * sites together, and neither alone would do. The five two-party event
 * keywords are `killed`, `hit`, `healed`, `pickedup` and `dropped`; the
 * notifier bodies for kinds 5, 6, 7 and 8 sit consecutively at 0x00427E10,
 * 0x00427E80, 0x00427EF0 and 0x00427F60, so ordering alone would give 7 and 8
 * to `pickedup` and `dropped` in that order -- which is an argument from
 * layout, and CLAUDE.md records what taking a jump table in address order
 * costs. The call sites decide it instead: all three callers of this address
 * are the pickup family, and each names itself in a log line --
 * "TrooperPickupItem %x", "TrooperHostApprovedPickupItem %x" and
 * "TrooperRemotePickupItem %x". Kind 8 is settled the same way rather than by
 * elimination -- see NotifyDropped below.
 *
 * The two objects go in the order the template fixes -- first argument first
 * -- which for `pickedup <a> by <b>` makes the first the item and the second
 * whoever took it. The callers pass them the other way round from their own
 * parameter lists, each pushing its second argument first; the one that
 * arrives here first is the object whose uid the caller logs, whose
 * OBJ_OFF_FLAGS gains bit 1, and which is stamped with a two-second deadline
 * at +0xC8, while the second is the one whose army the AI sweep beside the
 * call is handed.
 *
 * Its counter will read 0 whichever way the pickup path is driven: the three
 * callers are the original's and call by address, so they cross the patched
 * entry -- but this project has no drive that picks anything up. Verified by
 * reading and by being the fourth copy of a template whose other three are
 * already checked, and said plainly rather than left to be assumed.
 */
static void __cdecl NotifyPickedUp(void *item, void *taker)
{
    const uint8_t *o = (const uint8_t *)item;

    if (taker) {
        const uint8_t *t = (const uint8_t *)taker;

        EventNotify(AM2_EVENT_PICKED_UP,
                    *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)item)->uid,
                    ObjEventMask((const AM2_Object *)item),
                    *(const int32_t *)(t + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)taker)->uid,
                    ObjEventMask((const AM2_Object *)taker),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_PICKED_UP,
                *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                ((const AM2_Object *)item)->uid,
                ObjEventMask((const AM2_Object *)item),
                0, 0, 0, 0, 0, 0);
}

/* 0x00427F60, the fifth and last member of the template, and the same body as
 * NotifyPickedUp above with 8 where the 7 is. Nothing else differs -- the two
 * disassemble identically instruction for instruction.
 *
 * Kind 8 is `dropped`, and it is evidenced rather than inferred: the address
 * has exactly one caller and that caller names itself, "TrooperDropItem  %x"
 * and "TrooperDropItem  %x  ammo: %d" (the double space is the original's).
 * Having a real call site here matters, because the alternative was to take
 * `dropped` as the keyword left over once `pickedup` was assigned -- true, and
 * true for a reason that would not have survived either keyword moving.
 *
 * That caller is also what fixes the argument order for the pair, and it is
 * clearer here than on the pickup side: the object passed first is the one
 * whose uid it logs, and the last thing it does after this call is write army
 * 4 -- neutral -- into that object's OBJ_OFF_ARMY. An item going ownerless as
 * it leaves the trooper's hands. So first is the item and second the trooper,
 * `dropped <a> by <b>`, matching NotifyDamaged's victim-then-attacker.
 *
 * Counter 0 on every drive here, for the same reason as NotifyPickedUp: the
 * caller is the original's and calls by address, so the patched entry would be
 * crossed, but nothing in this project drops anything. Verified by reading and
 * by being the fifth copy of a checked template.
 */
static void __cdecl NotifyDropped(void *item, void *dropper)
{
    const uint8_t *o = (const uint8_t *)item;

    if (dropper) {
        const uint8_t *d = (const uint8_t *)dropper;

        EventNotify(AM2_EVENT_DROPPED,
                    *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)item)->uid,
                    ObjEventMask((const AM2_Object *)item),
                    *(const int32_t *)(d + AM2_OBJ_EVENT_NUM_OFF),
                    ((const AM2_Object *)dropper)->uid,
                    ObjEventMask((const AM2_Object *)dropper),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_DROPPED,
                *(const int32_t *)(o + AM2_OBJ_EVENT_NUM_OFF),
                ((const AM2_Object *)item)->uid,
                ObjEventMask((const AM2_Object *)item),
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
        && *(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) != AM2_MP_ROLE_SEVEN
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

/* Defined below, beside the rest of the damage family. */
void __cdecl DamageRoach(void *obj, int32_t amount, int32_t dir,
                         int32_t kind, uint32_t attackerUid);

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
            DamageBroadcast(obj, attackerUid, amount, kind,
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
        DamageRoach(obj, amount, extra, kind, attackerUid);
        break;
    default:
        break;                  /* types 4..7 have no handler */
    }

    NotifyDamaged(obj, attacker);

    if (g_mpSession && suppress == 0
        && CommMustBroadcast(kItemComm, attackerOwner))
        DamageBroadcast(obj, attackerUid, amount, kind,
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

typedef void (__cdecl *AM2_UnitActionFn)(void *obj, int32_t action);
#define orig_unit_action      ((AM2_UnitActionFn)(uintptr_t)ADDR_UNIT_ACTION)

/* Defined below, beside the rest of the object family. */
void __cdecl SetSoldierKind(void *obj, int32_t kind);

/* 0x00448220, two callers. Three effects, and together they are what a unit
 * giving up looks like: it changes to soldier kind 8, its AI mode goes to 2 --
 * `ignore`, per the action-parser oracle -- and it loses its weapon.
 *
 * Losing the weapon is two separate writes and only one of them is on the
 * unit. The weapon object is looked up by uid and marked OBJ_FLAG_OVERDUE, so
 * FreeOverdueItems collects it later; only then is the unit's uid field
 * cleared. Doing it the other way round would lose the uid before anything
 * could find the object, and the weapon would leak.
 *
 * The zero written to OBJ_OFF_SCRIPT_STATE comes from ADDR_ZERO_POINT rather
 * than from an immediate, which is what the original does -- that global is
 * .bss, nothing in the image writes it, and 103 sites read it for "no
 * position". Reproduced as the read rather than folded to 0, because the field
 * being assigned a POINT is the more likely reading of the source and folding
 * it would hide that.
 *
 * The guard is on the same field ADDR_SET_SOLDIER_KIND writes, refusing at 6
 * and above. What that field IS is not settled -- see OBJ_OFF_SOLDIER_KIND in
 * orig.h, where the evidence that it is a soldier kind is recorded along with
 * what stops that being a rename yet.
 *
 * The name is orig.h's and deliberately neutral. The three effects read as a
 * surrender, but nothing in the image says so and there is no string to ask.
 *
 * VERIFIED BY READING. Its counter is live -- blindspots.py does not list it,
 * so a 0 here means the code did not run -- and it reads 0 through a full Boot
 * Camp mission while WeaponByUid, one of its own callees, reads 201,368 on the
 * same drive. Both its callers are event handlers, so reaching it needs a
 * scripted event no mission this project drives fires. That is the same wall
 * FreeItem and RemoveFromItemList are behind: nothing in the observed window
 * makes anything give up or die. */
void __cdecl Type2ActionB(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    void    *weapon;

    if (!o)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= 6)
        return;

    *(int32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const int32_t *)(uintptr_t)ADDR_ZERO_POINT;
    *(int32_t *)(o + OBJ_OFF_FIELD_E4) = 2;

    SetSoldierKind(o, 8);

    weapon = WeaponByUid(*(const uint32_t *)(o + TROOPER_OFF_WEAPON_UID));
    if (weapon)
        *(uint32_t *)((uint8_t *)weapon + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;

    *(int32_t *)(o + TROOPER_OFF_WEAPON_UID) = 0;
    orig_unit_action(o, 0);
}

/* 0x00449860, eight callers. Puts an inventory slot in the unit's hand: it
 * records the slot, installs the weapon's four HANDLERS into the globals the
 * HUD and the input layer call through, and tells the network.
 *
 * The handlers are the substance. Each record of ADDR_WEAPON_HANDLERS is four
 * function pointers, indexed by the first dword of the weapon's
 * OBJ_OFF_FIELD_C0 -- so that field is a pointer to a type record, which is
 * something no reader had established before. The readers settle that these
 * are functions rather than data: they all do `test eax,eax; call eax`.
 *
 * THE FOUR STORES ARE NOT IN ORDER. Slot 2 goes to ADDR_WEAPON_FN_SLOT2 at
 * 0x005122F0 and slot 3 to ADDR_WEAPON_FN_SLOT3 at 0x005122DC, because the
 * globals are not contiguous -- 0x005122E0..EC sit between them, and at least
 * 0x005122E0 is a fifth handler this function never writes. Transcribing the
 * four stores in address order swaps the last two, and nothing about the
 * result would look wrong. Named by SLOT so the swap cannot happen silently.
 *
 * A missing weapon is not an error: the slot is still recorded and everything
 * below the lookup is skipped, so selecting an empty slot empties the hand
 * rather than refusing.
 *
 * THE SWAP IS CHECKED AGAINST THE ORIGINAL, not just asserted. Every record in
 * the image reads {0, 0, -1, 0}, so the only datum that distinguishes the two
 * mappings is WHICH global ends up holding the -1 -- which is exactly the
 * thing a slot-2/slot-3 swap changes. Driving into a Boot Camp mission and
 * reading the globals over the control socket:
 *
 *   ours    D4=0 D8=0 DC=0 F0=ffffffff
 *   orig    D4=0 D8=0 DC=0 F0=ffffffff
 *
 * byte for byte, under AM2_NOPATCH=1 for the second. Transcribed in address
 * order the two would read DC=ffffffff and F0=0, and the sides would differ.
 * No pixel could have shown this; the globals are the evidence, the same way
 * they are for the multiplayer checksums.
 *
 * 0x005122E0 reads 0x00459420 on both sides and neither this function nor the
 * order question touches it -- it is the fifth handler, written elsewhere.
 *
 * The counter reads 2 on that drive, so the coverage is small but real. */
void __cdecl SelectInventorySlot(void *unit, int32_t slot)
{
    uint8_t *u = (uint8_t *)unit;
    void    *w;
    int32_t  kind;
    const uint32_t *rec;

    *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = slot;

    w = WeaponByUid(*(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                        + (uint32_t)slot * 4));
    if (!w)
        return;

    kind = **(int32_t *const *)((uint8_t *)w + OBJ_OFF_FIELD_C0);
    rec  = (const uint32_t *)(uintptr_t)ADDR_WEAPON_HANDLERS + kind * 4;

    *(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT0 = rec[0];
    *(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT1 = rec[1];
    *(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT2 = rec[2];
    *(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT3 = rec[3];

    *(uint32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID =
        ((const AM2_Object *)unit)->uid;
    *(int32_t *)(uintptr_t)ADDR_WEAPON_SLOT = slot;

    orig_unit_action(unit, kind);
    SendTrooperSetWeapon(unit, ((const AM2_Object *)w)->uid, slot);
}

typedef void (__cdecl *AM2_StepFn)(void *obj);
#define orig_step_type2   ((AM2_StepFn)(uintptr_t)ADDR_STEP_TYPE2)
#define orig_step_type3   ((AM2_StepFn)(uintptr_t)ADDR_STEP_TYPE3)
#define orig_step_type5   ((AM2_StepFn)(uintptr_t)ADDR_STEP_TYPE5)
#define orig_step_type6   ((AM2_StepFn)(uintptr_t)ADDR_STEP_TYPE6)
#define orig_step_type8   ((AM2_StepFn)(uintptr_t)ADDR_STEP_TYPE8)

/* Defined below, beside the rest of the object stepping. */
void __cdecl StepType1And4(void *obj);

/* 0x004284D0, one caller -- ObjFrameSweep, for every registered object every
 * frame. Copies the position aside and hands the object to its type's stepper.
 *
 * The position copy happens FIRST and unconditionally, ahead of every guard,
 * so OBJ_OFF_PREV_POS is where the object was last frame even for an object
 * whose stepper never runs. Anything that moves it does so afterwards.
 *
 * TYPES 1 AND 4 SHARE A HANDLER. The jump table at 0x00428564 holds 0x004284F9
 * twice, so reading the eight bodies top to bottom and numbering as you go
 * gets every arm after type 3 wrong -- the same trap the sub-state table set.
 * Written as a shared `case` so the sharing is a fact of the source rather
 * than a coincidence of two identical calls.
 *
 * WHICH ARMS ACTUALLY RUN, measured rather than assumed. A histogram probe
 * over one Boot Camp mission -- 25,000,000 calls, so this is the hottest
 * function in the tree by some margin:
 *
 *   type 1  23,896,810      type 5  0
 *   type 2     108,766      type 6  0
 *   type 3      62,152      type 7  0
 *   type 4     932,272      type 8  0
 *   out of range 0
 *
 * So HALF the dispatch is unexercised here, including type 7's
 * ObjMarkIfOverdue, and the `type > 7` guard never fires at all. Worth knowing
 * before reading a clean A/B as covering this function: it covers four arms.
 *
 * The shared arm IS covered from both sides -- type 1 and type 4 between them
 * account for 24.8M of the calls -- so the one detail most likely to be
 * transcribed wrong is the one best exercised.
 *
 * TYPE 2 IS THE ONLY ARM WITHOUT THE DESTROYED GUARD. In the original the
 * check sits INSIDE seven of the eight arms and type 2 simply does not have
 * it. Here it is hoisted to one test before the switch, written as
 * `type != 1 && destroyed` -- equivalent, because the seven arms that have it
 * all do the same thing with it and return, and it is the only difference
 * between them.
 *
 * So the ASYMMETRY is reproduced while the duplication is not. That is worth
 * being exact about: one `test` missing from one arm out of eight is precisely
 * the shape of thing a tidy rewrite unifies away, and no frame comparison
 * would ever notice. Hoisting keeps it in one place where it can be read;
 * dropping the `type != 1` would lose it.
 *
 * That asymmetry is NOT exercised, though: it only shows when a destroyed
 * type 2 is stepped, and nothing in these drives destroys anything. Verified
 * by reading, like the arms above that never run.
 *
 * The ADDR_EVT_ID15_FLAG gate is the other way round from how it reads: when
 * the flag is SET, only objects carrying OBJ_FLAG8_BIT40 are stepped at all.
 * A clear flag steps everything. */
void __cdecl ObjFrameStep(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint32_t type;

    *(uint32_t *)(o + OBJ_OFF_PREV_POS) = *(const uint32_t *)(o + OBJ_OFF_POS);

    if (*(const int32_t *)(uintptr_t)ADDR_EVT_ID15_FLAG
        && !(*(const uint8_t *)(o + OBJ_OFF_FLAGS8) & OBJ_FLAG8_BIT40))
        return;

    type = *(const uint32_t *)o - 1u;
    if (type > 7u)
        return;

    if (type != 1u && (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED))
        return;

    switch (type) {
    case 0: case 3: StepType1And4(obj);     break;   /* ours already */
    case 1:         orig_step_type2(obj);   break;
    case 2:         orig_step_type3(obj);   break;
    case 4:         orig_step_type5(obj);   break;
    case 5:         orig_step_type6(obj);   break;
    case 6:         ObjMarkIfOverdue(obj);  break;   /* ours already */
    default:        orig_step_type8(obj);   break;
    }
}

/* 0x0043D280, one caller -- DamageObject's type 8 arm. A roach takes a hit.
 *
 * The third argument is a DIRECTION and not a second amount, which is the
 * thing most likely to be got wrong here: ADDR_RECV_DAMAGE computes it from
 * the message position and the object's own and masks it to a byte, and the
 * message's trace line calls it "dir". It is clamped UP to 1 before being
 * stored -- `cmp al,1; jae` on the low byte -- so OBJ_OFF_HIT_DIR is never 0,
 * and the clamp reads the byte while the rest of the function reads the full
 * dword. Both are reproduced.
 *
 * Armour is a subtraction, not a scale: damage is `max(0, amount - 2)`, spelt
 * as `amount - min(amount, armour)` so it cannot go negative, then clamped to
 * the health left so health cannot go below zero. The image ships 2.
 *
 * Death picks a state from the KIND: 1 and 3 give 5, everything else 6. The
 * original spells that `dec; je` then `sub 2; je`, which is 1 and 3 -- not a
 * range, and not consecutive.
 *
 * The fifth argument, the attacker's uid, is never read. Kept in the signature
 * because DamageObject passes it and the other three type handlers take it.
 *
 * VERIFIED BY READING. Its counter is blind, but the interesting number is its
 * CALLER's: DamageObject reads 0 through a full Boot Camp mission, so the
 * whole damage path is cold on every drive this project has -- nothing in the
 * observed window shoots anything, which is the same wall FreeItem,
 * RemoveFromItemList and Type2ActionB sit behind. Reaching it needs a mission
 * driven long enough for something to die.
 *
 * So the checks here are the two constants read out of the image rather than
 * guessed -- the armour is 2 and the wave is 0x32 -- and the direction
 * argument, which ADDR_RECV_DAMAGE independently confirms by computing it from
 * two positions and calling it "dir" in its own trace line. */
void __cdecl DamageRoach(void *obj, int32_t amount, int32_t dir, int32_t kind,
                         uint32_t attackerUid)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  armour;
    int32_t  hurt;
    int16_t  health;

    (void)attackerUid;

    *(uint8_t *)(o + OBJ_OFF_HIT_DIR) =
        ((uint8_t)dir >= 1u) ? (uint8_t)dir : (uint8_t)1;
    *(uint32_t *)(o + OBJ_OFF_HIT_TIME) =
        *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    armour = *(const int32_t *)(uintptr_t)ADDR_ROACH_ARMOUR;
    if (amount < armour)
        armour = amount;
    hurt = amount - armour;

    health = *(const int16_t *)(o + OBJ_OFF_HEALTH);
    if (hurt > (int32_t)health)
        hurt = (int32_t)health;
    *(int16_t *)(o + OBJ_OFF_HEALTH) = (int16_t)(health - (int16_t)hurt);

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0)
        return;

    *(int32_t *)(o + OBJ_OFF_DEATH_STATE) = (kind == 1 || kind == 3) ? 5 : 6;

    PlaySoundAt(AM2_ROACH_DEATH_SOUND, 0, 0,
                (int32_t)*(const int16_t *)(o + OBJ_OFF_POS),
                (int32_t)*(const int16_t *)(o + OBJ_OFF_POS + 2));

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_BIT0) {
        ItemPreDestroyAlias(obj, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC);
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_BIT0;
    }
}

typedef void (__cdecl *AM2_ResolvePtFn)(void *obj, int32_t tile, uint32_t *pt);
#define orig_resolve_point  ((AM2_ResolvePtFn)(uintptr_t)ADDR_RESOLVE_POINT_FOR_TILE)

/* 0x004582F0, nine callers. Points an object at a place: detach it from
 * whatever it was attached to, clear its script id, resolve the tile the
 * point falls in back to a point, and store that.
 *
 * The type guard is the familiar 2/3/8 set, spelt as a RANGE plus an equality
 * -- `< 2` refuse, `<= 3` accept, `!= 8` refuse -- so it is two comparisons
 * and not the ObjIsTypeIn238 call other sites use. Reproduced as the original
 * spells it rather than routed through that predicate: they agree today, and
 * an inlined test that stops agreeing is a fact about this function.
 *
 * THE POINT MAKES A ROUND TRIP. It goes to TileOfPoint by VALUE to get a tile,
 * then the caller's own argument slot is passed to
 * ADDR_RESOLVE_POINT_FOR_TILE by ADDRESS, and what comes back out of that slot
 * is what gets stored -- not the point that came in. So a caller's point is
 * snapped to whatever that resolver decides, and reading the store as "save
 * the argument" would be wrong whenever the two differ.
 *
 * ADDR_OBJ_ATTACH_TO is given a NULL target, which is the detach case of a
 * two-argument attach rather than a function of its own.
 *
 * The tail is two small writes: a flag set from whether OBJ_OFF_FIELD_F4 is
 * positive -- `setg`, so strictly greater, and stored as 0 or 1 rather than
 * the value -- and an AI mode of 3 demoted to 1, which is the only mode this
 * touches.
 *
 * VERIFIED BY READING, and this one is NOT a blind counter: blindspots.py
 * does not list it, so its 0 through a full Boot Camp mission means the code
 * did not run. TileOfPoint, which it calls, reads 2,312,945 on the same drive,
 * so the module around it is thoroughly live.
 *
 * Two attempts to reach it are recorded so they are not repeated: clicking on
 * the map, and selecting then right-clicking a destination at three places.
 * Both leave it at 0. Nine callers and none of them fires here, so whatever
 * orders an object to a point is something this drive does not do. Checked on
 * the CAMPAIGN map as well, which is a different map with different scripted
 * content: also 0. So this is not a Boot Camp peculiarity. */
void __cdecl PointActionA(void *obj, uint32_t point)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  type;
    int32_t  tile;

    if (!obj)
        return;

    type = *(const int32_t *)o;
    if (type < 2)
        return;
    if (type > 3 && type != 8)
        return;

    orig_obj_attach_to(obj, 0);

    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID) = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_B2)  = 0;

    tile = TileOfPoint(point);
    orig_resolve_point(obj, tile, &point);

    *(int32_t *)(o + OBJ_OFF_FIELD_EC) =
        (*(const int32_t *)(o + OBJ_OFF_FIELD_F4) > 0) ? 1 : 0;
    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) = point;

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_E4) == 3)
        *(int32_t *)(o + OBJ_OFF_FIELD_E4) = 1;
}

/* 0x004480E0, three callers. Type2ActionB's sibling and the same shape: the
 * same guard on OBJ_OFF_SOLDIER_KIND, refusing at 7 here rather than at 6, then a
 * set of writes and a change of soldier kind -- 6 rather than 8.
 *
 * What is new is the SELECTION handover, and it is a two-step. If the object
 * is selected it is deselected; then, only if it belonged to the player AND
 * nothing is selected any more, the player's own object is selected instead --
 * unless that object is already destroyed. So losing your selected unit moves
 * the cursor to you, and losing somebody else's does not.
 *
 * The count test is what makes the second step conditional rather than
 * automatic: ADDR_SELECTED_COUNT is read AFTER the deselect, so it is asking
 * "did that empty the selection", not "was anything selected".
 *
 * The argument is INCREMENTED before it is stored, so the caller passes a
 * previous value rather than the value to set. Both Type2Action siblings write
 * OBJ_OFF_SCRIPT_STATE from ADDR_ZERO_POINT; see the note there about what
 * that field probably is.
 *
 * VERIFIED BY READING. Same wall as Type2ActionB -- the callers are event
 * handlers, and no drive this project has fires them. */
void __cdecl Type2ActionC(void *obj, int32_t prev)
{
    uint8_t *o = (uint8_t *)obj;

    if (!o)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= 7)
        return;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SELECTED) {
        DeselectUnit(obj);

        if (*(const int8_t *)(o + OBJ_OFF_ARMY) ==
                (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER
            && *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT == 0) {
            void *mine = LookupOwnerObj(
                *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);

            if (!(*(const uint8_t *)((uint8_t *)mine + OBJ_OFF_FLAGS)
                  & OBJ_FLAG_DESTROYED))
                SelectUnit(mine);
        }
    }

    *(int32_t *)(o + OBJ_OFF_FIELD_5A4)  = prev + 1;
    *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) =
        *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
    *(int32_t *)(o + OBJ_OFF_FIELD_94)   = 1;
    *(int32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const int32_t *)(uintptr_t)ADDR_ZERO_POINT;

    SetSoldierKind(obj, 6);
}

typedef void (__cdecl *AM2_MoveFacingFn)(void *obj, int32_t a, int32_t b,
                                         int32_t c);
#define orig_move_along_facing ((AM2_MoveFacingFn)(uintptr_t)ADDR_OBJ_MOVE_ALONG_FACING)

/* 0x00433EC0, and the jump table gives it to types 1 AND 4 -- 24.8 million
 * calls between them in one Boot Camp mission, which makes this the
 * best-exercised reconstruction in the tree and the worst place to be sloppy.
 *
 * Four parts, and only the first runs for every object.
 *
 * The script update always runs. Then a ONE-SHOT: flags bit 7 asks for a move
 * along the object's facing, and the bit is cleared after, so whoever sets it
 * gets exactly one.
 *
 * The next two blocks are both behind `record->code == 0x2A && health > 0`,
 * where the record is the pointer at OBJ_OFF_FIELD_94 -- see the note there,
 * that field is a pointer for THIS object type and a scalar for others.
 *
 *   - Only the PLAYER's own such object reveals its surroundings, and only
 *     every 0x76C ms. The deadline compare is UNSIGNED (`jb`), so a clock
 *     wrap makes the difference enormous and the reveal simply happens.
 *   - The frame advance is timed off the ROW's stamp, not the object's, and
 *     the cycle SKIPS 1: n+1, and if that is 1 use 2, if it is past 16 wrap to
 *     0. Reproduced exactly; a plain modulo would be wrong twice per cycle.
 *
 * The last block is a countdown and it is independent of the two above --
 * different guard, different clock field. When the record's +8 matches
 * ADDR_WATCHED_TYPE_ID the object shows `9 - elapsed_seconds` as its frame,
 * clamped at 9, and after ten seconds it spawns something and marks itself
 * OBJ_FLAG_OVERDUE.
 *
 * TWO DETAILS IN THAT SPAWN ARE EASY TO GET BACKWARDS. The uid passed is the
 * OWNER's object's uid when the owner has one and the object's OWN uid when it
 * does not -- not the other way round. And the extra argument is
 * ADDR_SPAWN_EXTRA_6622BC when there is NO multiplayer session, or when there
 * is one and the comm object says this army must broadcast; it is 0 only in
 * the remaining case. A single-session game therefore takes the same arm as a
 * broadcasting host.
 *
 * The seconds division is the compiler's reciprocal for /1000, written as an
 * ordinary unsigned divide.
 *
 * NOW THE PART THAT MATTERS FOR ANYONE READING A CLEAN A/B HERE. This
 * function is called 24.8 MILLION times a mission and every guard in it is
 * FALSE on every drive this project has. Counted over 36,000,000 calls:
 *
 *   flags bit 7 set          0
 *   record code == 0x2A      0
 *   record +8 == watched id  0
 *
 * So the only line that executes is the UpdateObjectScript call at the top.
 * The reveal, the frame cycle, the countdown and the spawn are all dead here,
 * and a clean bootcamp/mission/campaign run says nothing whatever about them.
 *
 * Measured rather than suspected, and the suspicion came from a MUTATION that
 * should have failed and did not: dropping the `skip frame 1` rule from the
 * cycle above changes not one pixel on either drive. That is what sent me
 * looking, and it is the ObjFrameStep histogram one level further in -- a hot
 * function whose hot path is its first instruction.
 *
 * Everything below that first call is therefore VERIFIED BY READING, at the
 * same standing as the multiplayer functions, despite sitting on the busiest
 * code path in the game. */
void __cdecl StepType1And4(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *rec;
    uint32_t now;

    UpdateObjectScript(obj);

    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_BIT7) {
        orig_move_along_facing(obj, 0, 0, 0);
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_BIT7;
    }

    rec = *(uint8_t **)(o + OBJ_OFF_FIELD_94);
    now = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    if (*(const int32_t *)rec == 0x2A
        && *(const int16_t *)(o + OBJ_OFF_HEALTH) > 0) {

        if ((int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY)
                == (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER
            && now >= *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)
                      + AM2_REVEAL_PERIOD_MS) {
            RevealNearby(*(const AM2_Point *)(o + OBJ_OFF_POS),
                         AM2_REVEAL_NEAR, AM2_REVEAL_FAR);
            *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) =
                *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
        }

        {
            uint8_t *row = *(uint8_t **)(o + OBJ_OFF_ROWS);

            if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                    - *(const uint32_t *)(row + ROW_OFF_STAMP_54)
                > AM2_FRAME_PERIOD_MS) {
                int32_t n = *(const int32_t *)(o + OBJ_OFF_FORMATION_SLOT) + 1;

                if (n == 1)
                    n = 2;
                else if (n > 0x10)
                    n = 0;

                ChangeObjectFrame(obj, n, 1);
                *(uint32_t *)(row + ROW_OFF_STAMP_54) =
                    *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
            }
        }
    }

    if (*(const int32_t *)(rec + 8)
            != *(const int32_t *)(uintptr_t)ADDR_WATCHED_TYPE_ID)
        return;

    {
        uint32_t secs = (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                         - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)) / 1000u;

        if (secs >= 9u)
            secs = 9u;
        ChangeObjectFrame(obj, (int32_t)(9u - secs), 1);
    }

    if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        <= *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58) + AM2_FUSE_MS)
        return;

    {
        int32_t   army = (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY);
        void     *mine = LookupOwnerObj((uint32_t)army);
        uint32_t  uid  = mine ? ((const AM2_Object *)mine)->uid
                              : ((const AM2_Object *)obj)->uid;
        int32_t   extra = 0;

        if (!*(void *const *)(uintptr_t)ADDR_MP_SESSION
            || CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                 (int16_t)army))
            extra = *(const int32_t *)(uintptr_t)ADDR_SPAWN_EXTRA_6622BC;

        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;
        *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) =
            *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

        orig_spawn_at((int32_t)*(const int16_t *)(o + OBJ_OFF_POS),
                      (int32_t)*(const int16_t *)(o + OBJ_OFF_POS + 2),
                      AM2_SPAWN_KIND_8B, army, uid, extra, 0, 0, 0, 0);
    }
}

typedef void *(__cdecl *AM2_MakeWeaponFn)(const char *name, int32_t army,
                                          int32_t kind, uint32_t where,
                                          int32_t a, int32_t b, int32_t c,
                                          uint32_t uid);
#define orig_make_weapon  ((AM2_MakeWeaponFn)(uintptr_t)ADDR_CREATE_WEAPON)

/* 0x00448170, five callers, and the last of the three Type2Action siblings.
 * Where B disarms and C hands the selection on, A RE-ARMS: the unit becomes
 * soldier kind 7, loses whatever it was holding, and is given a freshly
 * created weapon which it then holds.
 *
 * The three guards are not the same as its siblings'. It refuses at
 * OBJ_OFF_SOLDIER_KIND >= 6, as B does and C does not, and it has one they do not:
 * ADDR_TYPE2_FIELD5A4_SET, which is false unless the object is a type 2 with
 * a positive OBJ_OFF_FIELD_5A4. So whatever that counter tracks is a reason
 * NOT to re-arm.
 *
 * The old weapon is marked OBJ_FLAG_OVERDUE and simply abandoned -- unlike
 * Type2ActionB, the uid field is not cleared first, because the very next
 * thing overwrites it with the new weapon's. Same two writes, opposite order,
 * and both are correct for what their own function is doing.
 *
 * 0x2B APPEARS TWICE and that is what ties the two halves together: it
 * selects the weapon through KeyLookupTriple and it is the action code run
 * afterwards. Reading either as a coincidence would let them drift.
 *
 * The new weapon is created with an EMPTY name -- ADDR_DIR_SCRATCH, which is
 * a scratch buffer, not a literal -- and its army is copied from the unit
 * AFTER creation rather than passed in, even though the creator takes an
 * army. Reproduced; the creator is given the unit's army too, so the second
 * write is redundant unless the creator ignores it.
 *
 * VERIFIED BY READING. Same wall as its siblings: the callers are event
 * handlers no drive fires. */
void __cdecl Type2ActionA(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    void    *old;
    void    *made;

    if (!o)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= 6)
        return;
    if (Type2Field5A4Set((const AM2_Object *)obj))
        return;

    SetSoldierKind(obj, 7);

    old = WeaponByUid(*(const uint32_t *)(o + TROOPER_OFF_WEAPON_UID));
    if (old)
        *(uint32_t *)((uint8_t *)old + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;

    made = orig_make_weapon((const char *)(uintptr_t)ADDR_DIR_SCRATCH,
                            (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY),
                            KeyLookupTriple(AM2_WEAPON_KEY_KIND,
                                            AM2_WEAPON_KEY_2B, 0),
                            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT,
                            4, -1, 0, 0);
    if (!made)
        return;

    *(uint8_t *)((uint8_t *)made + OBJ_OFF_ARMY) =
        *(const uint8_t *)(o + OBJ_OFF_ARMY);
    *(uint32_t *)(o + TROOPER_OFF_WEAPON_UID) =
        ((const AM2_Object *)made)->uid;

    orig_unit_action(obj, AM2_WEAPON_KEY_2B);
    SendTrooperSetWeapon(obj, ((const AM2_Object *)made)->uid, 0);
}

typedef void (__cdecl *AM2_TrooperDiedFn)(void *obj, int32_t kind, uint32_t by);
typedef void (__cdecl *AM2_VehicleDiedFn)(void *obj, uint32_t by);

/* 0x00428450, one caller -- RecvDeath, when the wire says an object died.
 *
 * Health is zeroed FIRST, before any handler runs, so a per-type handler
 * cannot see the object as alive. Only types 2 and 3 have one; every other
 * type falls straight through to the common tail, which always runs.
 *
 * THE ATTACKER IS LOOKED UP BEFORE THE TYPE IS EVEN READ, and the lookup has
 * a trap in it. FindSlot's second parameter is an out-pointer, and the
 * original passes the address of ITS OWN THIRD ARGUMENT -- the attacker uid --
 * so the call may overwrite it. That is safe only because the value was
 * already copied into a register on the line before, and it is that copy
 * every later use reads.
 *
 * Written with a separate local for the scratch, which is what the original
 * means; sharing the argument slot the way it does would work here too, but
 * only by accident of the copy, and nothing would say so.
 *
 * A slot below zero gives a NULL attacker rather than an error, so a death
 * attributed to a uid this side has never seen still cleans up.
 *
 * The original emits the two-call tail TWICE, once per exit; one copy in C is
 * the same function.
 *
 * VERIFIED BY READING. Its one caller needs a peer to send a death message. */
void __cdecl ObjDie(void *obj, int32_t kind, uint32_t by)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  insertAt = 0;
    int32_t  slot     = FindSlot(by, &insertAt);
    void    *attacker = NULL;

    if (slot >= 0)
        attacker = ((AM2_ObjEntry *)(uintptr_t)
                        *(void *const *)(uintptr_t)ADDR_OBJ_TABLE)[slot].obj;

    *(int16_t *)(o + OBJ_OFF_HEALTH) = 0;

    switch (*(const int32_t *)o) {
    case 2:  TrooperDied(obj, kind, by);       break;
    case 3:  VehicleDied(obj, by);             break;
    default:                                   break;
    }

    TriggerItemDestroyed(obj, attacker);
    ObjDeathCleanup(obj);
}

typedef void (__cdecl *AM2_SetFrameFn)(void *row, int16_t frame, int32_t force);
typedef int32_t (__cdecl *AM2_PoseIndexFn)(void *obj, void *weapon);
#define orig_set_anim_frame  ((AM2_SetFrameFn)(uintptr_t)ADDR_SET_ANIM_FRAME)
/* Still original: 272 bytes of per-row animation advance, one caller and that
 * caller is StepObjRows below. */
typedef void (__cdecl *AM2_StepRowFn)(void *row);
#define orig_step_row_anim   ((AM2_StepRowFn)(uintptr_t)ADDR_STEP_ROW_ANIM)
#define orig_weapon_pose     ((AM2_PoseIndexFn)(uintptr_t)ADDR_WEAPON_POSE_INDEX)
/* The game's own rand, spelled as event.cpp spells it -- through AM2_IMAGE,
 * because it is CRT code the offline test maps as data. */
typedef int32_t (__cdecl *AM2_RandFn)(void);
#define orig_rand ((AM2_RandFn)AM2_IMAGE(ADDR_GAME_RAND))

/* 0x00449570, ten callers -- the only writer of OBJ_OFF_SOLDIER_KIND, and the
 * function that settles what that field is. The value it stores is the same
 * value it uses to index ADDR_SOLDIER_ANIMS, whose entry it hangs off the
 * object's first row. A kind, not a role; see the note in orig.h.
 *
 * THE ANIMATION SWAP IS SKIPPED WHEN THE SET IS ALREADY CURRENT -- and the
 * test is against ROW_OFF_ANIM_CUR while the write goes to ROW_OFF_ANIM_NEXT.
 * Two different fields, four bytes apart, and reading them as one would make
 * the guard compare against what it had just written.
 *
 * The frame chosen depends on what the soldier is holding, and there are
 * three cases rather than two: a live soldier with a weapon takes the pose
 * table's entry for that weapon, a live soldier WITHOUT one takes frame 1, and
 * a dead one takes whatever frame the row already had. The last is a
 * no-change that still goes through the setter, so the force flag is what
 * makes it do anything at all.
 *
 * KIND 7 IS A SPECIAL UNIT and gets three things nothing else does: flag
 * 0x8000, a random name, and one and a half times its MAXIMUM health -- the
 * scale is applied to OBJ_OFF_MAX_HEALTH, not to what the unit currently has. The random index is MSVC's signed
 * modulo -- `and 0x8000003F` with a fixup for negatives -- then +1, so it is
 * 1..64 and never 0. The health scale is a double in the image and it is
 * exactly 1.5, read rather than guessed.
 *
 * OBJ_OFF_FIELD_578 is cleared for EVERY kind, before the kind 7 test, so it
 * is not part of that special case however much it looks like it.
 *
 * VERIFIED BY READING. Its counter is blind -- every caller that runs here is
 * ours -- and the Type2Action siblings that call it are event handlers no
 * drive fires. */
void __cdecl SetSoldierKind(void *obj, int32_t kind)
{
    uint8_t *o    = (uint8_t *)obj;
    uint8_t *row  = *(uint8_t **)(o + OBJ_OFF_ROWS);
    uint8_t *anim = (uint8_t *)(uintptr_t)ADDR_SOLDIER_ANIMS
                    + (uint32_t)kind * AM2_ANIM_TABLE_BYTES;

    *(int32_t *)(o + OBJ_OFF_SOLDIER_KIND) = kind;

    if (anim != *(uint8_t **)(row + ROW_OFF_ANIM_CUR)) {
        *(uint8_t **)(row + ROW_OFF_ANIM_NEXT) = anim;

        if (*(const int16_t *)(o + OBJ_OFF_HEALTH) > 0) {
            void *w = WeaponByUid(
                *(const uint32_t *)(o + UNIT_OFF_INVENTORY
                                    + (uint32_t)*(const int32_t *)
                                          (o + UNIT_OFF_INVENTORY_SEL) * 4));

            if (w) {
                int32_t pose = orig_weapon_pose(obj, w);

                orig_set_anim_frame(*(uint8_t **)(o + OBJ_OFF_ROWS),
                                    (int16_t)((const int32_t *)(uintptr_t)
                                        ADDR_WEAPON_POSE_FRAMES)[pose], 1);
            } else {
                orig_set_anim_frame(*(uint8_t **)(o + OBJ_OFF_ROWS), 1, 1);
            }
        } else {
            uint8_t *r = *(uint8_t **)(o + OBJ_OFF_ROWS);

            orig_set_anim_frame(r, *(const int16_t *)(r + ROW_OFF_FRAME), 1);
        }
    }

    *(int32_t *)(o + OBJ_OFF_FIELD_578) = 0;
    if (kind != 7)
        return;

    *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_8000;

    {
        int32_t n = orig_rand() % AM2_KIND7_NAME_COUNT + 1;

        *(int32_t *)(o + OBJ_OFF_FIELD_5A8) = n;
        *(int32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
            *(const int32_t *)(uintptr_t)ADDR_ZERO_POINT;

        if (n > 0)
            SetFieldInAll(o + OBJ_OFF_SUBRECORD,
                                  ((void *const *)(uintptr_t)
                                       ADDR_KIND7_NAMES)[n]);
    }

    *(int16_t *)(o + OBJ_OFF_HEALTH) = (int16_t)(int32_t)
        ((double)*(const int16_t *)(o + OBJ_OFF_MAX_HEALTH)
         * *(const double *)(uintptr_t)AM2_KIND7_HEALTH_SCALE);
}

typedef int32_t (__cdecl *AM2_ApplyFrameFn)(void *obj, int32_t b, int32_t a,
                                            int32_t frame, int32_t flag);
#define orig_apply_obj_frame ((AM2_ApplyFrameFn)(uintptr_t)ADDR_APPLY_OBJ_FRAME)

/* 0x004351C0, and the name is the image's own. Changes an object's frame and
 * then every object CHAINED to it -- OBJ_OFF_CHAIN_UID to the first, then
 * OBJ_OFF_CHAIN_NEXT_UID from each to the next.
 *
 * The two numbers it passes down are BITFIELDS unpacked from the type
 * record's +8: bits 7..16 and bits 19..25. That is the same dword
 * StepType1And4 compares whole against ADDR_WATCHED_TYPE_ID, so the field is
 * packed and neither reader is wrong about it.
 *
 * IT HAS TWO EXITS AND THEY DO NOT RETURN THE SAME THING. The normal exit
 * answers whether ANY object's frame actually changed. But both ways of
 * stopping the chain early -- a uid that resolves to nothing, or a link whose
 * type is neither 1 nor 4 -- fall into the DESTROYED exit, which does
 * `xor eax,eax`. So a broken chain answers 0 even when the first object's
 * frame did change.
 *
 * That is deliberate code, not a compiler artefact: the zeroing instruction
 * is there in the epilogue and the two `j` instructions target it. Reproduced,
 * and worth stating, because "return whether anything changed" is what the
 * function looks like it does and is only true when the chain is intact.
 *
 * A chained object carrying OBJ_FLAG_NO_FRAME is SKIPPED -- but the walk goes
 * on past it, so one unchangeable link does not stop the rest.
 *
 * VERIFIED BY READING. Its counter is blind: the callers that reach it here
 * are StepType1And4 and SetSoldierKind, both ours. */
int32_t __cdecl ChangeObjectFrame(void *obj, int32_t frame, int32_t flag)
{
    uint8_t *o   = (uint8_t *)obj;
    int32_t  any = 0;
    uint32_t uid;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return 0;

    {
        uint32_t v = *(const uint32_t *)(*(uint8_t **)(o + OBJ_OFF_FIELD_94) + 8);

        if (orig_apply_obj_frame(obj,
                                 (int32_t)((v >> AM2_OBJREC_SHIFT_B)
                                           & AM2_OBJREC_MASK_B),
                                 (int32_t)((v >> AM2_OBJREC_SHIFT_A)
                                           & AM2_OBJREC_MASK_A),
                                 frame, flag))
            any = 1;
    }

    for (uid = *(const uint32_t *)(o + OBJ_OFF_CHAIN_UID); uid; ) {
        uint8_t *link = (uint8_t *)LookupByUID(uid);
        int32_t  type;

        /* Both of these return 0, not `any` -- see above. */
        if (!link)
            return 0;
        type = *(const int32_t *)link;
        if (type != 1 && type != 4)
            return 0;

        if (!(*(const uint32_t *)(link + OBJ_OFF_FLAGS) & OBJ_FLAG_NO_FRAME)) {
            uint32_t v =
                *(const uint32_t *)(*(uint8_t **)(link + OBJ_OFF_FIELD_94) + 8);

            if (orig_apply_obj_frame(link,
                                     (int32_t)((v >> AM2_OBJREC_SHIFT_B)
                                               & AM2_OBJREC_MASK_B),
                                     (int32_t)((v >> AM2_OBJREC_SHIFT_A)
                                               & AM2_OBJREC_MASK_A),
                                     frame, flag))
                any = 1;
        }

        uid = *(const uint32_t *)(link + OBJ_OFF_CHAIN_NEXT_UID);
    }

    return any;
}

typedef void (__cdecl *AM2_RankPromoteFn)(void *obj);
#define orig_rank_promote ((AM2_RankPromoteFn)(uintptr_t)ADDR_RANK_PROMOTE)

/* 0x00457CD0, two callers. Awards experience and promotes when it is enough.
 *
 * IT LOOPS. One award can carry a unit through more than one rank: after each
 * promotion it re-reads the rank and the total and tests the NEXT threshold,
 * stopping at AM2_RANK_MAX. A single large award skipping two ranks is
 * behaviour, not an edge case.
 *
 * Six refusals before any of that, and the order is the original's. The
 * multiplayer one comes FIRST and is the odd one: outside a session it is
 * skipped entirely, but inside one the unit's army must be one this side
 * broadcasts for -- so experience is awarded by whoever owns the unit, not by
 * whoever caused it.
 *
 * Then rank below 7, a POSITIVE award only, type 2 only, and soldier kind
 * below 6. Note the type test is written as `sete` into a register and then
 * tested, rather than as a branch -- the same value either way.
 *
 * THE TABLE BASE IS 0x00473DD4, not the 0x00473DD8 the threshold comes
 * through. The function reads two fields of one 28-byte record: `+4` for the
 * experience needed and `+0` for what it hands ADDR_RANK_APPLY. Reading the
 * threshold's address as the base would put the table one field late with
 * every value still looking plausible -- the trig-table mistake in another
 * costume.
 *
 * PROMOTION RAISES THE CEILING FIRST. ADDR_SET_MAX_HEALTH is handed the rank
 * record's first field, and only then is current health grown toward the new
 * maximum -- so the cap in the next paragraph is the value this call just set,
 * not the one the unit had a moment ago.
 *
 * The counter is OBJ_OFF_REPAIR_FRAME, which is what that offset is called
 * from the reading HealObject made of it on an ITEM. On a TROOPER it is
 * experience; the object is a union past its header, and this is the third
 * such field after 0xA0 and 0x94. The name is kept rather than aliased.
 *
 * Promotion adds a QUARTER of current health, capped at the maximum:
 * `h + (h >> 2)`, an arithmetic shift, so it is health * 1.25 rounded toward
 * negative infinity rather than toward zero. Health is never negative here,
 * so the two agree; written as the shift regardless.
 *
 * VERIFIED BY READING. Both callers are event handlers, and the multiplayer
 * guard means the whole function is skipped in a session this side does not
 * broadcast for. */
void __cdecl Type238Action(void *obj, int32_t award)
{
    uint8_t *o = (uint8_t *)obj;

    if (!o)
        return;

    if (*(void *const *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
        return;

    if (*(const int32_t *)(o + OBJ_OFF_RANK) >= AM2_RANK_MAX)
        return;
    if (award <= 0)
        return;
    if (*(const int32_t *)o != 2)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= 6)
        return;

    *(int32_t *)(o + OBJ_OFF_REPAIR_FRAME) += award;

    for (;;) {
        const uint8_t *rec = (const uint8_t *)(uintptr_t)ADDR_RANK_TABLE
                             + (uint32_t)*(const int32_t *)(o + OBJ_OFF_RANK)
                               * RANK_REC_BYTES;
        int16_t health;
        int16_t maxHealth;

        if (*(const int32_t *)(o + OBJ_OFF_REPAIR_FRAME)
            < *(const int32_t *)(rec + RANK_REC_OFF_XP))
            return;

        orig_rank_promote(obj);

        rec = (const uint8_t *)(uintptr_t)ADDR_RANK_TABLE
              + (uint32_t)*(const int32_t *)(o + OBJ_OFF_RANK) * RANK_REC_BYTES;
        SetMaxHealth(obj, *(const int32_t *)(rec + RANK_REC_OFF_SCALE));

        health    = *(const int16_t *)(o + OBJ_OFF_HEALTH);
        maxHealth = *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);
        {
            int32_t grown = (int32_t)health + ((int32_t)health >> 2);

            if (grown >= (int32_t)maxHealth)
                grown = (int32_t)maxHealth;
            *(int16_t *)(o + OBJ_OFF_HEALTH) = (int16_t)grown;
        }

        if (*(const int32_t *)(o + OBJ_OFF_RANK) >= AM2_RANK_MAX)
            return;
    }
}

/* 0x00428E00, seven callers. Step every one of the object's rows, then take
 * the FIRST row's +0x3C into the object's +0x44, sign-extended from int16.
 *
 * THE COUNT IS RE-READ EVERY ITERATION and again after the loop, so the
 * original really does allow the stepper below to change it -- the second
 * test is not a redundant copy of the first. Reproduced; nothing read says a
 * stepper does change it.
 *
 * The object's +0x44 and a row's +0x44 are DIFFERENT FIELDS of different
 * structures, and only the row's is an animation. Worth saying because
 * RowAnimFinished, in anim.cpp, reads a row's +0x44 as an AM2_Anim * and the
 * two would otherwise look like one field with two readings.
 */
void __cdecl StepObjRows(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  i;

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++)
        orig_step_row_anim(*(uint8_t **)(o + OBJ_OFF_ROWS)
                           + (uint32_t)i * AM2_OBJ_ROW_STRIDE);

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 0)
        *(int32_t *)(o + OBJ_OFF_FIELD_44) =
            *(const int16_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS)
                               + ROW_OFF_FIELD_3C);
}

void item_install(void)
{
    patch_replace(ADDR_ITEM_PRE_DESTROY, (const void *)ItemPreDestroy,
                  "ItemPreDestroy", 2);
    patch_replace(ADDR_FREE_SUBRECORD_ROWS, (const void *)FreeSubrecordRows,
                  "FreeSubrecordRows", 1);
    patch_replace(ADDR_ITEMS_RESET, (const void *)ItemsReset,
                  "ItemsReset", 0);
    patch_replace(ADDR_STEP_TYPE1_4, (const void *)StepType1And4,
                  "StepType1And4", 1);
    patch_replace(ADDR_TYPE238_ACTION, (const void *)Type238Action,
                  "Type238Action", 2);
    patch_replace(ADDR_CHANGE_OBJECT_FRAME, (const void *)ChangeObjectFrame,
                  "ChangeObjectFrame", 1);
    patch_replace(ADDR_SET_SOLDIER_KIND, (const void *)SetSoldierKind,
                  "SetSoldierKind", 10);
    patch_replace(ADDR_OBJ_DIE, (const void *)ObjDie, "ObjDie", 1);
    patch_replace(ADDR_TYPE2_ACTION_A, (const void *)Type2ActionA,
                  "Type2ActionA", 5);
    patch_replace(ADDR_TYPE2_ACTION_C, (const void *)Type2ActionC,
                  "Type2ActionC", 3);
    patch_replace(ADDR_POINT_ACTION_A, (const void *)PointActionA,
                  "PointActionA", 9);
    patch_replace(ADDR_DAMAGE_ROACH, (const void *)DamageRoach,
                  "DamageRoach", 1);
    patch_replace(ADDR_OBJ_FRAME_STEP, (const void *)ObjFrameStep,
                  "ObjFrameStep", 1);
    patch_replace(ADDR_SELECT_INVENTORY_SLOT, (const void *)SelectInventorySlot,
                  "SelectInventorySlot", 8);
    patch_replace(ADDR_TYPE2_ACTION_B, (const void *)Type2ActionB,
                  "Type2ActionB", 2);
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
    patch_replace(ADDR_NOTIFY_PICKED_UP, (const void *)NotifyPickedUp,
                  "NotifyPickedUp", 1);
    patch_replace(ADDR_NOTIFY_DROPPED, (const void *)NotifyDropped,
                  "NotifyDropped", 1);
    patch_replace(ADDR_HELD_WEAPON_CODE, (const void *)HeldWeaponCode,
                  "HeldWeaponCode", 1);
    patch_replace(ADDR_BLOCK_WEIGHT_AT, (const void *)BlockWeightAt,
                  "BlockWeightAt", 3);
    patch_replace(ADDR_BLOCK_WEIGHT_CHAIN, (const void *)BlockWeightChain,
                  "BlockWeightChain", 2);
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
    patch_replace(ADDR_HEIGHT_AT_POINT, (const void *)HeightAtPoint,
                  "HeightAtPoint", 5);
    patch_replace(ADDR_OBJECTS_HIT_BY_POINT, (const void *)ObjectsHitByPoint,
                  "ObjectsHitByPoint", 5);
    patch_replace(ADDR_OBJ_TILE_CHANGED, (const void *)ObjTileChanged,
                  "ObjTileChanged", 15);
    patch_replace(ADDR_VEHICLE_DIED, (const void *)VehicleDied, "VehicleDied", 1);
    patch_replace(ADDR_TROOPER_DIED, (const void *)TrooperDied, "TrooperDied", 1);
    patch_replace(ADDR_APPLY_OBJ_HEIGHT, (const void *)ApplyObjHeight,
                  "ApplyObjHeight", 4);
    patch_replace(ADDR_POINT_ACTION_C, (const void *)PointActionC,
                  "PointActionC", 2);
    patch_replace(ADDR_MAKE_KIND7, (const void *)MakeKind7, "MakeKind7", 5);
    patch_replace(ADDR_UNIT_BY_UID, (const void *)UnitByUid, "UnitByUid", 4);
    patch_replace(ADDR_ITEM_PRE_DESTROY_ALIAS, (const void *)ItemPreDestroyAlias,
                  "ItemPreDestroyAlias", 2);
    patch_replace(ADDR_ROW_RELEASE, (const void *)RowRelease, "RowRelease", 5);
    patch_replace(ADDR_STEP_OBJ_ROWS, (const void *)StepObjRows,
                  "StepObjRows", 7);
}
