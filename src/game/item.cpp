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
#include "defparse.h"  /* DefFindObjRec -- reconstructed */
#include "pad.h"      /* ObjTileHook -- reconstructed */

/* ShakeAt lives in win32/mapdraw.h and item.cpp is flat, so it is declared
 * here the way script.cpp declares PreloadSprite. Its signature names no Win32
 * type. And the decal helper stays original, reached by address -- it has one
 * caller and that caller is this file. */
/* NOT extern "C": mapdraw.h's block closes at line 129 and ShakeAt is
 * declared at 158, outside it. The linkage has to match the header, not the
 * file it is stubbed in -- audio.h has the same split and selftest.cpp records
 * it. BoatExitPoint needed the wrapper for exactly the opposite reason. */
void __cdecl ShakeAt(const AM2_Point *at, int32_t strength);
typedef void (__cdecl *AM2_DecalFn)(int32_t x, int32_t y, int32_t variant);
#define orig_blast_spin ((AM2_DecalFn)(uintptr_t)ADDR_PLACE_GROUND_DECAL)
#include "objtable.h"
#include "objtype.h"   /* ObjType2Field548 */
#include "objflag.h"   /* ObjFlagClear0 -- reconstructed */
#include "savetag.h"
#include "image.h"
#include "crt.h"
#include "armymsg.h"  /* SendObjDestroyed -- reconstructed */
#include "army.h"     /* LookupOwnerObj -- reconstructed */
#include "region.h"   /* NearestAllowedTile -- reconstructed */
#include "event.h"    /* EventNotify -- reconstructed */
#include "msgslot.h"  /* CommMustBroadcast -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "maprow.h"   /* RowUpdate, SetAnimFrame -- reconstructed */
#include "anim.h"     /* AM2_Anim -- the frame count SetUnitPose waits on */
#include "trig.h"     /* Cos8, Sin8 -- reconstructed */
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

/* The two speech entry points, same file and same reason. BOTH are inside
 * audio.h's extern "C" block, unlike LoadAudioSection, so both get C linkage
 * here. They are not interchangeable: SpeakLine takes a GROUP and
 * SpeakItemPickupLine an item KIND which it maps to a group through its own
 * two dispatch tables. The call sites look identical. */
extern "C" void __cdecl SpeakLine(int32_t group, int32_t owner);
extern "C" void __cdecl SpeakItemPickupLine(int32_t item, int32_t owner);


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

/* 0x0044BAF0, one caller. A unit's class NAME.
 *
 * "Sarge" when OBJ_OFF_SARGE is set; otherwise the entry for the code of the
 * weapon it holds; and entry 0 when it holds none. The same three steps
 * HeldWeaponCode above takes, ending in a table lookup instead of the code.
 *
 * READING THAT TABLE SETTLES A FIELD TWO OTHER COMMENTS GAVE UP ON.
 * ObjType2Field548 says only "the dword at +0x548, but only for a type 2", and
 * LookupOwnerObj's comment says "`ArmyLeader` was the name I nearly gave it.
 * What the +0x548 test means is not established, so the claim is not made."
 * It is established now: the field's only effect here is to make the unit
 * called "Sarge", so LookupOwnerObj really does find the army's leader. A
 * string table names a field that two functions reading it could not.
 *
 * "SARGE" IS THE ENTRY BEFORE THE TABLE, at 0x00489B40 rather than inside it,
 * which is why it is a separate name here rather than an index. Codes 0, 6, 7
 * and 8 point at ADDR_DIR_SCRATCH instead of a literal, so those units have no
 * fixed name and the caller sees whatever that buffer holds.
 *
 * THE CODE IS NOT BOUNDED. Whatever the weapon's OBJ_OFF_FIELD_C0 record says
 * indexes the table, so a code past its end reads whatever follows.
 * Reproduced; the shipped codes are the eleven the table covers.
 *
 * MEASURED AT 0, which is worth a second look because the HUD's SQUAD panel
 * plainly shows "Sarge" during a mission. It does not come from here: this has
 * one caller and that caller is the original's, so the counter is not blind
 * and the panel's name reaches it some other way. The identification does not
 * depend on the function running -- it rests on what the table holds, which is
 * six class names with "Sarge" in the slot before them.
 */
const char *__cdecl UnitClassName(void *unit)
{
    const uint8_t      *u = (const uint8_t *)unit;
    const char *const  *names =
        (const char *const *)AM2_IMAGE(ADDR_UNIT_CLASS_NAMES);
    const uint8_t      *w;

    if (*(const int32_t *)(u + OBJ_OFF_SARGE))
        return *(const char *const *)AM2_IMAGE(ADDR_UNIT_NAME_SARGE);

    w = (const uint8_t *)WeaponByUid(
            *(const uint32_t *)(u + TROOPER_OFF_WEAPON_UID));
    if (!w)
        return names[0];

    return names[**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0)];
}

/* 0x004337C0, one caller. Whether an item can be picked up.
 *
 * Three conditions and all three must hold: it is a weapon (type 4), its
 * OBJ_OFF_PICKUP_AFTER has passed, and the code its OBJ_OFF_FIELD_C0 record
 * holds is 0x1F, 0x20 or 0x21.
 *
 * THAT SECOND CONDITION CLOSES A FIELD FROM THE OTHER SIDE. +0xC8 was
 * identified while reading NotifyPickedUp's callers, where TrooperPickupItem
 * stamps it with the clock plus two seconds and nothing said what read it.
 * This is the reader, and it refuses until the stamp has passed -- so the
 * field is a re-pickup cooldown and the two functions together say so. Neither
 * would have alone.
 *
 * The comparison is UNSIGNED, so a stamp far in the future -- which cannot
 * arise from clock-plus-two-seconds, but the field is a plain dword -- refuses
 * rather than wrapping into the past. Reproduced.
 *
 * The three codes are written by the original as `sub 0x1F` and two `dec`s
 * against zero, which is a three-arm equality chain; written as the three
 * cases it is.
 *
 * MEASURED AT 111,650 CALLS on a driven Boot Camp mission -- something asks
 * this constantly, and the A/B compares it thoroughly. What those calls do not
 * establish is which EXIT they took: a walk over every object would fail the
 * type test on nearly all of them, so the cooldown comparison and the
 * three-code chain may be reached far less often than the count suggests.
 */
int32_t __cdecl CanPickUp(void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;
    int32_t        code;

    if (!ObjIsType4((const AM2_Object *)obj))
        return 0;

    if (*(const uint32_t *)(o + OBJ_OFF_PICKUP_AFTER)
        > *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS)
        return 0;

    code = **(const int32_t *const *)(o + OBJ_OFF_FIELD_C0);

    return code == 0x1F || code == 0x20 || code == 0x21;
}

/* 0x00449200, one caller. Put an object into state 5 and give up its ALTERNATE
 * table record.
 *
 * SAVED_OFF_TABLE_REC3 moves into SAVED_OFF_TABLE_REC2, the alternate is
 * cleared, and the value is handed to SetFieldInAll on the object's
 * OBJ_OFF_SUBRECORD. An object already in state 5 is left alone, so this is
 * idempotent.
 *
 * THE STATE IS WHAT ObjsAreAllied TESTS. That function chooses REC3 only when
 * its third argument is set AND this field is not 5 -- so once this has run,
 * the alternate is never chosen again, which is consistent, because it has
 * just been cleared. Two functions written a dozen commits apart, and the
 * agreement is what makes either reading safe. ObjConceal tests the same 5.
 *
 * ITS ONE CALLER REACHES IT WHEN THE UNIT IS FINISHED -- both OBJ_OFF_HEALTH
 * and OBJ_OFF_MAX_HEALTH at or below zero, and the object a type 2. That is a
 * good description of the occasion and a poor one of the function, so the name
 * is the mechanism. One call site is thin ground for the other kind of name,
 * which is the mistake ADDR_UNIT_ACTION and ADDR_SPRITE_DROP_NAMED both were.
 *
 * MEASURED AT 0, which fits the occasion: nothing dies on a Boot Camp drive,
 * and that is the same wall FreeItem, RemoveFromItemList and ObjDie are behind
 * -- STATUS.md's standing ask for "a mission with something in it to kill".
 * One more function waiting on it.
 */
void __cdecl ObjDropAltRecord(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    void    *rec;

    if (!obj)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_530) == AM2_OBJ_STATE_5)
        return;

    *(int32_t *)(o + OBJ_OFF_FIELD_530) = AM2_OBJ_STATE_5;

    rec = *(void *const *)(o + SAVED_OFF_TABLE_REC3);
    *(void **)(o + SAVED_OFF_TABLE_REC2) = rec;
    *(void **)(o + SAVED_OFF_TABLE_REC3) = (void *)0;

    SetFieldInAll(o + OBJ_OFF_SUBRECORD, rec);
}

/* 0x00447570, one caller. Take an unused soldier name.
 *
 * ADDR_SOLDIER_NAMES is 62 records of {taken, const char *} holding
 * "R. Pavey", "D. Lee", "D. Fruin", "A. Muolic", "J. Wildblood" and the rest
 * of the team -- so a trooper is named after whoever made the game. The index
 * this returns goes into OBJ_OFF_NAME_INDEX.
 *
 * It starts at `rand() % 62` and walks FORWARD to the first free entry,
 * wrapping once. So the names are handed out in table order from a random
 * start, not randomly -- two troopers made in the same moment get adjacent
 * names, which is a different thing from what "random name" suggests.
 *
 * IF EVERY NAME IS TAKEN IT RETURNS THE STARTING INDEX WITHOUT MARKING IT, so
 * the caller gets a name already in use rather than a failure. That is the arm
 * the wrap test reaches, and it is the only exit that does not set the flag --
 * which also means the table's taken-count cannot exceed 62 however many
 * troopers are made.
 *
 * Nothing here ever CLEARS a taken flag, and nothing else in the image writes
 * that column either. So the supply is not returned when a trooper dies: the
 * sixty-third name is a repeat for the rest of the session.
 *
 * The modulo is an unsigned `div`, which matters not at all -- ADDR_GAME_RAND
 * answers 0..0x7FFF -- and is written as the unsigned remainder it is.
 *
 * Measured at 0 on a driven Boot Camp mission: its one caller makes a trooper,
 * and that mission's troopers are placed during load by a different path. The
 * counter is not blind. Verified by reading, and by a table whose contents say
 * unambiguously what it hands out.
 */
int32_t __cdecl TakeSoldierName(void)
{
    uint8_t *tab   = (uint8_t *)AM2_IMAGE(ADDR_SOLDIER_NAMES);
    /* orig_rand's macro is defined further down this file, past here. */
    int32_t  start = (int32_t)((uint32_t)
        ((int32_t (__cdecl *)(void))AM2_IMAGE(ADDR_GAME_RAND))()
        % AM2_SOLDIER_NAMES);
    int32_t  i     = start;

    while (*(const int32_t *)(tab + (size_t)i * AM2_SOLDIER_NAME_BYTES
                              + SOLDIER_NAME_OFF_TAKEN)) {
        if (++i >= AM2_SOLDIER_NAMES)
            i = 0;
        if (i == start)
            return start;               /* all taken; not marked */
    }

    *(int32_t *)(tab + (size_t)i * AM2_SOLDIER_NAME_BYTES
                 + SOLDIER_NAME_OFF_TAKEN) = 1;
    return i;
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

/* 0x00429B60, one caller -- ApplyObjFrame, which calls it only when the four
 * offsets it is about to pass differ from the ones the object already holds.
 * Give the object a new hit box and put it back on the map under it.
 *
 * THE THIRD OF THE ROW/ITEM PAIRS, and this one is maprow.cpp's RowAlloc.
 * Same cell-count arithmetic to the instruction: take 2 off each span first,
 * so a box that exactly fills a cell boundary does not claim the next one,
 * shift down by 8, and add 2 back for the partial cells at either end. Same
 * 8-BIT `imul` with only AL kept, so a box needing more than 255 cells wraps.
 * Same initialisation of every entry to {owner, no links, index -1}, which is
 * the state ItemLinkCells assumes. RowAlloc's comment is the fuller
 * discussion; this note says what differs.
 *
 * WHAT DIFFERS IS THE ORDER AND TWO GUARDS. RowAlloc is called to CREATE a
 * row and takes its spans directly; this is called to CHANGE an object that
 * is already on the map, so it opens with ItemPreDestroy to take the object
 * off every cell list first -- the unlink ItemLinkCells does not do for
 * itself. And it has two exits RowAlloc has no need of: OBJ_FLAG_BIT0 clear
 * returns after writing the box but before sizing anything, and
 * OBJ_FLAG_DESTROYED returns after sizing but before relinking. So a hidden
 * object keeps a stale entry array and a destroyed one keeps a correct array
 * that is in no list.
 *
 * THE BOX IS STORED TWICE, WHICH IS THE FINDING. The four arguments are
 * offsets from the object's own position and go to OBJ_OFF_BOX_OFFSETS
 * verbatim; the same four with the position added go to OBJ_OFF_HIT_RECT.
 * That is what settles MSG_CREATE_OFF_BLOCK, whose sixteen bytes are copied
 * out of the first of those and which orig.h described as unidentified: an
 * item create message carries the sender's box SHAPE, so the receiver can
 * build the same box without knowing the sprite.
 *
 * The grow is a realloc and it only ever grows: the count is compared with
 * `>=` and left alone when it already suffices, so an object whose box
 * shrinks keeps the larger array and the entries past the new box are the
 * ones ItemLinkCells clears.
 */
void __cdecl ItemSetBox(void *obj, int32_t left, int32_t top,
                        int32_t right, int32_t bottom)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  x, y;
    int32_t  w, h, need;
    int32_t  i;

    ItemPreDestroy(o, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC);

    x = *(const int16_t *)(o + OBJ_OFF_POS);
    y = *(const int16_t *)(o + OBJ_OFF_POS + 2);

    *(int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 0)  = left;
    *(int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 4)  = top;
    *(int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 8)  = right;
    *(int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 12) = bottom;

    *(int32_t *)(o + OBJ_OFF_HIT_RECT + 0)  = x + left;
    *(int32_t *)(o + OBJ_OFF_HIT_RECT + 4)  = y + top;
    *(int32_t *)(o + OBJ_OFF_HIT_RECT + 8)  = x + right;
    *(int32_t *)(o + OBJ_OFF_HIT_RECT + 12) = y + bottom;

    if (!(*(const uint8_t *)(o + OBJ_OFF_FLAGS) & MAPOBJ_FLAG_VISIBLE))
        return;

    w = right - left;
    h = bottom - top;
    if (w > 2)
        w -= 2;
    if (h > 2)
        h -= 2;
    need = (int32_t)(uint8_t)(int8_t)((int8_t)((w >> AM2_CELL_SHIFT) + 2)
                                      * (int8_t)((h >> AM2_CELL_SHIFT) + 2));

    if ((int32_t)*(const uint8_t *)(o + OBJ_OFF_CELL_COUNT) < need) {
        *(o + OBJ_OFF_CELL_COUNT) = (uint8_t)need;
        *(void **)(o + OBJ_OFF_CELL_ENTRIES) =
            am2_realloc(*(void **)(o + OBJ_OFF_CELL_ENTRIES),
                        (size_t)((uint32_t)need * AM2_CELL_ENTRY_STRIDE));
    }

    for (i = 0; i < (int32_t)*(const uint8_t *)(o + OBJ_OFF_CELL_COUNT); i++) {
        uint8_t *entry = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES)
                         + (uint32_t)i * AM2_CELL_ENTRY_STRIDE;

        *(void **)(entry + 0) = obj;
        *(void **)(entry + 8) = (void *)0;   /* next */
        *(void **)(entry + 4) = (void *)0;   /* prev */
        *(int32_t *)(entry + CELL_ENTRY_OFF_INDEX) = -1;
    }

    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return;

    ItemLinkCells(o, (void *)(uintptr_t)ADDR_OBJ_MAP_DESC);
}

/* 0x00429F40, two callers. ItemPreDestroy's other half: link the object into
 * every cell list its OBJ_OFF_HIT_RECT covers, and clear the entries it did
 * not need. Link on placement, unlink before the storage goes back.
 *
 * IT IS maprow.cpp's RowRegisterAll, SPECIALISED FOR OBJECTS. Same clip, same
 * four clamps, same stride, same two loops, same "clear the leftovers" tail --
 * and the same `cols - 1` on the bottom edge, which MapDescInit's comment
 * settles: the grid is cols x cols, so cols is the bound the grid actually
 * has and the ROWS in the guard above is the odd one. Reading that sibling
 * first would have saved most of this; the image has several such pairs and
 * the previous commit found another.
 *
 * What differs is what the two operate on -- a row's ROW_OFF_RECT, buffer and
 * DepthLink there, an object's hit rect, OBJ_OFF_CELL_ENTRIES and
 * ListPushFront here -- and ONE GUARD. RowRegisterAll returns early on
 * ROW_FLAG_REMOVED; this has no such test, so a caller must not hand it
 * anything it does not want registered.
 *
 * IT IS ALSO THE ANSWER TO WHY ObjectsInRect NEEDS A DE-DUPLICATION RULE. An
 * object is not in one cell, it is in EVERY cell its hit rect overlaps. So a
 * walk over a block of cells really would answer the same object several
 * times, which is exactly what that function's home-cell rule prevents. The
 * multi-cell registration had been INFERRED there from the de-duplication;
 * this is the writer that confirms it.
 *
 * IT DOES NOT UNLINK FIRST. Every entry is pushed onto a head with no check of
 * where it already is, so calling this on a still-linked object corrupts both
 * lists. The caller has to have unlinked it -- which is the asymmetry with
 * ItemPreDestroy, whose -1 makes it safe to repeat.
 *
 * AND IT DOES NOT BOUND ITSELF BY OBJ_OFF_CELL_COUNT. That count is consulted
 * only by the loop that clears the leftovers; the linking loop writes one
 * entry per cell covered whatever the array holds, so a hit rect spanning
 * more cells than the object has entries writes past the end of it. The
 * original's.
 *
 * The two low clamps are branchless in the original -- `setle` on the sign,
 * `dec`, `and` -- which is max(v, 0) written so that v == 0 takes the same arm
 * as v < 0. The clearing loop re-reads the byte count every iteration although
 * nothing changes it, exactly as ItemPreDestroy's does, and it writes the two
 * links and the index and never the object pointer at +0.
 */
void __cdecl ItemLinkCells(void *obj, void *cells)
{
    uint8_t       *o = (uint8_t *)obj;
    const uint8_t *d = (const uint8_t *)cells;
    int32_t        cl, ct, cr, cb;
    int32_t        cols, rows, shift;
    int32_t        cell, stride, used;

    cl = *(const int32_t *)(o + OBJ_OFF_HIT_RECT + 0)  >> AM2_CELL_SHIFT;
    ct = *(const int32_t *)(o + OBJ_OFF_HIT_RECT + 4)  >> AM2_CELL_SHIFT;
    cr = *(const int32_t *)(o + OBJ_OFF_HIT_RECT + 8)  >> AM2_CELL_SHIFT;
    cb = *(const int32_t *)(o + OBJ_OFF_HIT_RECT + 12) >> AM2_CELL_SHIFT;

    if (cb < 0)
        return;
    rows = *(const int32_t *)(d + MAPDESC_OFF_ROWS);
    if (ct > rows - 1)
        return;
    if (cr < 0)
        return;
    cols = *(const int32_t *)(d + MAPDESC_OFF_COLS);
    if (cl > cols - 1)
        return;

    if (cl <= 0)
        cl = 0;
    if (ct <= 0)
        ct = 0;
    if (cr >= cols - 1)
        cr = cols - 1;
    if (cb >= cols - 1)          /* COLS, not ROWS -- as in RowRegisterAll */
        cb = cols - 1;

    shift  = *(const int32_t *)(d + MAPDESC_OFF_SHIFT);
    used   = 0;
    cell   = (ct << shift) + cl;
    stride = cols - cr + cl - 1;

    for (; ct <= cb; ct++, cell += stride) {
        int32_t x;

        for (x = cl; x <= cr; x++, cell++, used++) {
            uint8_t *entry = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES)
                             + (uint32_t)used * AM2_CELL_ENTRY_STRIDE;

            *(int32_t *)(entry + CELL_ENTRY_OFF_INDEX) = cell;
            ListPushFront(entry,
                          (void **)(*(uint8_t **)(d + MAPDESC_OFF_CELLS)
                                    + (uint32_t)cell * 4));
        }
    }

    for (; used < (int32_t)*(const uint8_t *)(o + OBJ_OFF_CELL_COUNT); used++) {
        uint8_t *entry = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES)
                         + (uint32_t)used * AM2_CELL_ENTRY_STRIDE;

        *(void **)(entry + 4) = (void *)0;   /* prev, as ListPushFront has it */
        *(void **)(entry + 8) = (void *)0;   /* next */
        *(int32_t *)(entry + CELL_ENTRY_OFF_INDEX) = -1;
    }
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


/* ObjectsAtPoint -- original 0x0042A550, fifteen callers.
 *
 * The sibling of ObjectsHitByPoint above, and the two really are different
 * questions of the same cell. That one asks "is the point inside your
 * OBJ_OFF_HIT_RECT, and on a set bit of your mask if you have one". This one
 * asks the rectangle too, and then chooses between THREE further tests:
 *
 *   OBJ_FLAG_ROWS_MASK set -- ask ADDR_OBJ_ROWS_MASK_AT, which walks the
 *   object's rows and tests the point against each row's sprite;
 *
 *   otherwise, no OBJ_OFF_HIT_MASK -- build a looser box from four offsets at
 *   OBJ_OFF_BOX_LEFT.. added to the object's own position, and test that;
 *
 *   otherwise -- ObjMaskBitAt, the same single-bitmask test the sibling uses.
 *
 * So an object with no mask is accepted by a BOX HERE and by its hit
 * rectangle alone in the sibling. That is the whole difference between the
 * two functions and it is worth stating plainly: they are not duplicates, and
 * fifteen callers plus four is not nineteen callers of one thing.
 *
 * The four box offsets are read NOWHERE ELSE in the image, so this function is
 * the only reason they have names.
 *
 * Both bounds are COLS, as in the sibling -- the row coordinate is checked
 * against `cols - 1` and not `rows - 1`, which MapDescInit's allocation makes
 * correct. Fourth place in this tree that has to say so.
 *
 * The answer is chained through OBJ_OFF_QUERY_NEXT and returned NEWEST-FIRST,
 * which reverses the cell's own order. Both siblings do it and both callers'
 * loops assume it.
 *
 * WHAT A CLEAN A/B IS WORTH HERE IS NOT MUCH, and the sibling says why: its
 * own comment records that returning NULL unconditionally left `mission` at
 * 281 and `bootcamp` at 22, both at their floors, across 3,872 calls. Fifteen
 * callers is not evidence that anything watches the answer, and nothing here
 * measures that it does. Verified by reading, with the sibling's structure
 * as the strongest corroboration available -- the two share their bounds,
 * their chaining and their newest-first order, and differ only in the test.
 */
void *__cdecl ObjectsAtPoint(const uint32_t *pt, const void *desc)
{
    const uint8_t *d    = (const uint8_t *)desc;
    int32_t        cols = *(const int32_t *)(d + MAPDESC_OFF_COLS);
    int32_t        cx   = (int32_t)*(const int16_t *)pt >> AM2_CELL_SHIFT;
    int32_t        cy   = (int32_t)*((const int16_t *)pt + 1) >> AM2_CELL_SHIFT;
    uint8_t       *head = (uint8_t *)0;
    uint8_t       *node;

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

        if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_ROWS_MASK) {
            if (!ObjRowsMaskAt(o, pt))
                continue;
        } else if (!*(void *const *)(o + OBJ_OFF_HIT_MASK)) {
            AM2_Rect box;
            int32_t  x = *(const int16_t *)(o + OBJ_OFF_POS);
            int32_t  y = *(const int16_t *)(o + OBJ_OFF_POS + 2);

            box.left   = x + *(const int32_t *)(o + OBJ_OFF_BOX_LEFT);
            box.top    = y + *(const int32_t *)(o + OBJ_OFF_BOX_TOP);
            box.right  = x + *(const int32_t *)(o + OBJ_OFF_BOX_RIGHT);
            box.bottom = y + *(const int32_t *)(o + OBJ_OFF_BOX_BOTTOM);

            if (!PointInRect(&box, (const AM2_Point *)pt))
                continue;
        } else if (!ObjMaskBitAt(o, (const AM2_Point *)pt)) {
            continue;
        }

        *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT) = head;
        head = o;
    }

    return head;
}

typedef void *(__cdecl *AM2_ObjectsAtFn)(const uint32_t *pt, void *desc);
/* Still original. Declared here rather than with the other teardown seams
 * further down, because VehicleDied is above them. */
typedef void (__cdecl *AM2_ObjOnlyFn)(void *obj);

/* Still original: the tail both death handlers share, and the ten-argument
 * maker. The second was already declared further down this file, so it moved
 * up here rather than being written a second time -- its fifth argument is a
 * uid at that other site, which is why it is uint32_t and why this caller
 * passes a plain 0 rather than inventing a name for it. */
/* AM2_DiedTailFn went with its seam: TrooperDiedTail is ours now. */
typedef void (__cdecl *AM2_SpawnAtFn)(int32_t x, int32_t y, int32_t kind,
                                      int32_t army, uint32_t uid, int32_t extra,
                                      int32_t e, int32_t f, int32_t g,
                                      int32_t h);
#define orig_spawn_at          ((AM2_SpawnAtFn)(uintptr_t)ADDR_SPAWN_AT)

/* ApplyHeightItem -- original 0x00433C20, three callers.
 *
 * Types 1 and 4 have their own height handler, and this is it: stamp
 * OBJ_OFF_HEIGHT_SET, recompute the row's depth key from the height, and do
 * the same to every object chained off this one.
 *
 * A THIRD WRITER OF ROW_OFF_FIELD_26, AND IT SETTLES THE READING. The seq
 * adders write 0x3E8 and 1 and "terrain plus 0x3F2", which said only that the
 * field is a small biased number. This one writes `ScaleBy32Blocks(height) -
 * 0x3E8` and then ADDS an int16 from the object's def record. A term that is
 * the object's HEIGHT and a term that is a per-type constant, summed into one
 * field that the renderer reads: that is a depth key, and the record's field
 * is what lets two types at the same height sort against each other.
 *
 * THE CHAIN IS FOLLOWED BY UID AND STOPS AT THE FIRST LINK THAT IS NOT AN
 * ITEM. Not skips -- stops. A chain of {item, trooper, item} applies the
 * height to the first only. The chain walk in ApplyObjFrame two hundred lines
 * up does the same thing, so it is the family's convention rather than this
 * function's accident.
 *
 * AND THE PARENT AND THE CHILDREN LOOK UP THEIR DEF RECORD DIFFERENTLY. Both
 * take the type and the `a` field out of the packed key, but the parent's
 * third argument is OBJ_OFF_REPAIR_FRAME -- or OBJ_OFF_FORMATION_SLOT when
 * that is not positive -- while a child's is the key's own low seven bits.
 * Reproduced; nothing here says why, and the two are not the same lookup.
 *
 * The parent is guarded on having a def record and the children are not: a
 * child with a null OBJ_OFF_FIELD_94 faults. Both the original's.
 */
void __cdecl ApplyHeightItem(void *obj, int32_t height)
{
    uint8_t *o = (uint8_t *)obj;
    uint32_t key;
    int32_t  third;
    void    *rec;
    uint32_t uid;

    if (!obj)
        return;
    if (!*(void **)(o + OBJ_OFF_FIELD_94))
        return;

    third = *(const int32_t *)(o + OBJ_OFF_REPAIR_FRAME);
    if (third <= 0)
        third = *(const int32_t *)(o + OBJ_OFF_FORMATION_SLOT);

    key = *(const uint32_t *)(*(uint8_t **)(o + OBJ_OFF_FIELD_94) + 8);
    rec = DefFindObjRec((int32_t)((key >> AM2_OBJREC_SHIFT_B)
                                  & AM2_OBJREC_MASK_B),
                        (int32_t)((key >> AM2_OBJREC_SHIFT_A)
                                  & AM2_OBJREC_MASK_A),
                        third);

    *(uint8_t *)(o + OBJ_OFF_HEIGHT_SET) = (uint8_t)height;
    *(int16_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS) + ROW_OFF_FIELD_26) =
        (int16_t)(ScaleBy32Blocks(height) - AM2_DEPTH_BASE);

    if (rec)
        *(int16_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS) + ROW_OFF_FIELD_26) +=
            *(const int16_t *)((const uint8_t *)rec + DEF_OBJ_REC_OFF_DEPTH);

    for (uid = *(const uint32_t *)(o + OBJ_OFF_CHAIN_UID); uid; ) {
        uint8_t *link = (uint8_t *)LookupByUID(uid);
        void    *lrec;

        if (!link)
            return;
        if (*(const int32_t *)link != 1 && *(const int32_t *)link != 4)
            return;

        *(uint8_t *)(link + OBJ_OFF_HEIGHT_SET) = (uint8_t)height;
        *(int16_t *)(*(uint8_t **)(link + OBJ_OFF_ROWS) + ROW_OFF_FIELD_26) =
            (int16_t)(ScaleBy32Blocks(height) - AM2_DEPTH_BASE);

        key = *(const uint32_t *)(*(uint8_t **)(link + OBJ_OFF_FIELD_94) + 8);
        lrec = DefFindObjRec((int32_t)((key >> AM2_OBJREC_SHIFT_B)
                                       & AM2_OBJREC_MASK_B),
                             (int32_t)((key >> AM2_OBJREC_SHIFT_A)
                                       & AM2_OBJREC_MASK_A),
                             (int32_t)(key & AM2_OBJREC_MASK_B));
        if (lrec)
            *(int16_t *)(*(uint8_t **)(link + OBJ_OFF_ROWS)
                         + ROW_OFF_FIELD_26) +=
                *(const int16_t *)((const uint8_t *)lrec
                                   + DEF_OBJ_REC_OFF_DEPTH);

        uid = *(const uint32_t *)(link + OBJ_OFF_CHAIN_NEXT_UID);
    }
}

/* Still original: the teardown PointActionC opens with, and the notify it
 * ends on. */
typedef void (__cdecl *AM2_AfterMoveFn)(void *obj, int32_t a, int32_t b);
/* ObjAfterMove is reconstructed, in region.cpp, and called by name. */
/* ItemTeardown had THREE private spellings in this file -- two typedefs and a
 * bare cast, under three names -- for one void(void *). Reconstructing it
 * collapsed all three; the typedef is gone with them. */

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

    ObjInitCommon(o, (char *)AM2_IMAGE(ADDR_DIR_SCRATCH), 7, pt,
                         (const int32_t *)AM2_IMAGE(ADDR_KIND7_BOX), e, f);

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
    ItemTeardown(o);

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
    ObjAfterMove(o, 1, 0);
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
        ApplyHeightItem(o, height);
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

    TrooperDiedTail(o, a);
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

    ObjClearFootprint(o);

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
        ObjTileHook(o);

    if (PointsEqual(*(const uint32_t *)(o + OBJ_OFF_PREV_POS),
                    *(const uint32_t *)(o + OBJ_OFF_POS))
        && *(const uint32_t *)(o + OBJ_OFF_PREV_TILE)
               == (uint32_t)*(const uint16_t *)(o + OBJ_OFF_TILE)
        && force == 0)
        return;

    ObjRemap(o, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC), force);
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

    o = (uint8_t *)ObjectsAtPoint(
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

/* 0x0045EED0, EIGHT callers, 48 bytes. Is this object an ITEM of the type id
 * in ADDR_CREATE_WATCHED_KIND?
 *
 * Three exits and only two answers. A null object RETURNS THE NULL ITSELF
 * rather than a literal zero -- `test eax,eax; jne; ret` with the argument
 * still in eax -- which is the same value and worth reproducing as the
 * constant it is. A non-item answers 0. An item answers the comparison
 * between its OBJ_OFF_FIELD_94 record's +8 dword and that global, which
 * orig.h already records as a type id being matched rather than a count; its
 * neighbour 0x00516164 holds 0xE80609 and is matched against the same field.
 */
int32_t __cdecl ObjIsWatchedKind(const void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (!o)
        return 0;
    if (*(const int32_t *)o != AM2_OBJ_TYPE_ITEM)
        return 0;

    return *(const int32_t *)
               (*(const uint8_t *const *)(o + OBJ_OFF_FIELD_94) + 8)
           == *(const int32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND;
}

/* ObjSetRoachFootprint -- original 0x0043C8D0, six call sites in three
 * functions. Put a roach's footprint ON the map: every cell its mask covers
 * loses AM2_TILE_COVER_STEP from ADDR_CELL_WEIGHTS, and OBJ_FLAG_FOOTPRINT_ON
 * is set.
 *
 * IT WAS ADDR_ROACH_STEP_TAIL_B, and orig.h's own note predicted the
 * correction: the five names in that block are role names taken from one call
 * site, described there as "the weakest kind of naming", with "nothing here
 * reads their bodies". Reading the body makes it the exact partner of
 * ObjClearRoachFootprint below.
 *
 * MEASURED RATHER THAN EYEBALLED. Disassembling both with branch targets
 * normalised gives EIGHTY-SEVEN instructions each and a diff of four lines:
 * the flag gate inverted (`jne` against `je`), `add 0xF` against `add 0xF1`,
 * ADDR_TILE_COVER_ADD against _SUB, and `or 0x200000` against
 * `and ~0x200000`. Everything else -- the stamp, the window, the rounding, the
 * loop -- is the same instruction in the same place.
 *
 * Written out beside its partner rather than folded into one function with a
 * sign and two function pointers, which is what ConsiderSightingB and
 * AiStepTrack are also written out for: a shared body would make those four
 * differences parameters and the next reader would have to trust that the
 * parameters are right.
 *
 * A CELL IS CHARGED ONCE HOWEVER MANY MASK POINTS LAND IN IT, and the
 * mechanism is worth the paragraph. ADDR_ROACH_MARK_STAMP is bumped once per
 * call and written into every cell of a 16 x 16 uint16 window as that cell is
 * done; a cell already carrying this call's stamp is skipped. So the window
 * needs no clearing between calls -- a stamp that has not been written this
 * call cannot match -- which is what makes a per-call scratch array cost
 * nothing. The window's extent is not inferred: the stamp sits 0x200 bytes
 * past the array, which is exactly 256 uint16.
 *
 * THE WINDOW IS IN 16-UNIT CELLS AND SO IS THE MASK, where the cell grid the
 * object registration uses is 256. Both coordinates are shifted by
 * AM2_MASK_CELL_SHIFT and taken relative to the object's own cell less
 * AM2_MASK_WINDOW_HALF, so a mask point further than eight of these from the
 * object indexes outside the window -- and nothing bounds it. Reproduced; the
 * mask is built to fit.
 *
 * The direction is `RoundTo8(row->heading + ROW_OFF_HEADING_BIAS, bits)` with
 * the animation's own directionBits, and it is used AS THE RECORD INDEX --
 * unlike MoveStepPoint, which rounds the same way and then shifts the result
 * back up to eight bits because it wants a heading rather than a slot.
 *
 * ADDR_TILE_COVER_SUB is called for each cell as well, on the tile rather than
 * the cell, and after the weight has already been adjusted.
 */
void __cdecl ObjSetRoachFootprint(void *obj)
{
    uint8_t       *o = (uint8_t *)obj;
    const uint8_t *rows;
    const AM2_Anim *anim;
    int32_t        dir;
    int32_t        baseX, baseY;
    uint32_t       slot;
    int32_t        i;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_FOOTPRINT_ON)
        return;

    ++*(uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP;

    rows = *(const uint8_t *const *)(o + OBJ_OFF_ROWS);
    anim = *(const AM2_Anim *const *)(rows + ROW_OFF_ANIM_PLAYING);

    baseX = (*(const int16_t *)(o + OBJ_OFF_POS) >> AM2_MASK_CELL_SHIFT)
            - AM2_MASK_WINDOW_HALF;
    baseY = (*(const int16_t *)(o + OBJ_OFF_POS + 2) >> AM2_MASK_CELL_SHIFT)
            - AM2_MASK_WINDOW_HALF;

    dir = RoundTo8((*(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)
                    + *(const uint8_t *)(rows + ROW_OFF_HEADING)) & 0xFF,
                   anim->directionBits) & 0xFF;

    slot = (uint32_t)dir * AM2_MASK_STRIDE;

    for (i = 0; i < ((const int32_t *)((const uint8_t *)
             AM2_IMAGE(ADDR_ROACH_MASK_COUNT) + slot))[0]; i++) {
        const int16_t *pts = (const int16_t *)
            ((const uint8_t *)AM2_IMAGE(ADDR_ROACH_MASK) + slot);
        uint32_t  at;
        uint16_t *mark;
        int32_t   tile;

        ((int16_t *)&at)[0] = (int16_t)(pts[i * 2]
                                        + *(const int16_t *)(o + OBJ_OFF_POS));
        ((int16_t *)&at)[1] = (int16_t)(pts[i * 2 + 1]
                                        + *(const int16_t *)(o + OBJ_OFF_POS
                                                             + 2));

        mark = (uint16_t *)(uintptr_t)ADDR_ROACH_MARK
               + (((int32_t)((int16_t *)&at)[0] >> AM2_MASK_CELL_SHIFT) - baseX)
                 * AM2_MASK_WINDOW
               + (((int32_t)((int16_t *)&at)[1] >> AM2_MASK_CELL_SHIFT)
                  - baseY);

        if (*mark == *(const uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP)
            continue;

        tile = TileOfPoint(at);
        ((uint8_t *)(uintptr_t)ADDR_CELL_WEIGHTS)[(uint32_t)tile & 0xFFFF]
            += AM2_TILE_COVER_STEP;
        TileCoverAdd((uint16_t)tile);

        *mark = *(const uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP;
    }

    *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_FOOTPRINT_ON;
}

/* The image's own LCG, and it must be: the roach's stagger draws from the
 * same sequence as everything else in the process that calls it, so libc's
 * would diverge on the first roach.
 *
 * THIS FILE ALREADY HAD THIS MACRO, 1700 lines further down and spelled
 * differently -- AM2_IMAGE where the new one had a plain cast. The compiler
 * caught the redefinition, which nothing else would have: the two expansions
 * agree in the game, where the slide is zero, and differ under
 * tests/loadimage.h, where it is not. Moved up here so there is one. */
typedef int32_t (__cdecl *AM2_GameRandFn)(void);
#define orig_game_rand ((AM2_GameRandFn)AM2_IMAGE(ADDR_GAME_RAND))

/* CreateRoach -- original 0x0043CDD0, two callers: LoadType8, which rebuilds
 * a saved roach, and the spawner at 0x00420B33, which makes a fresh one.
 *
 * ITS EIGHT CONSTANTS ARE NAMED BY THE GAME'S OWN DATA RATHER THAN BY ME.
 * DefGameParse stores the ROACH_* keywords into eight consecutive dwords and
 * aai/game.aai lists them in that order with the image's own default values,
 * so ADDR_ROACH_HEIGHT and ADDR_ROACH_HEALTH are the file's words. Only two
 * of the eight are read here; the armour, damage and the four velocity and
 * acceleration terms are consumed further in.
 *
 * THE TWO CALL SITES DISAGREE ABOUT EVERY ARGUMENT BUT THE NAME, which is
 * what makes the signature readable at all: the spawner passes kind 0, flags
 * 0 and a zero uid where LoadType8 passes the saved record's kind, the saved
 * flags and the saved uid. A parameter that is a literal at one site and a
 * field at the other is a parameter, not a constant.
 *
 * Two writes are redundant and both are reproduced. The flags word is READ
 * before it is OR'd, and the facing byte is written zero, immediately after a
 * memset that has already zeroed all 0x560 bytes. The original does both; a
 * reconstruction that dropped them would be tidier and would differ.
 *
 * The deadline at OBJ_OFF_FIELD_FC is the clock plus a random 0..499, so two
 * roaches created in the same frame do not act in lockstep. It is the game's
 * own rand, not ours -- the LCG in the statically linked CRT -- so the
 * sequence is shared with everything else that draws from it and a
 * reconstruction calling libc's would diverge on the first roach.
 *
 * The footprint is laid down only when ADDR_STATE_ENTERED is clear. On a
 * load that flag is set, and LoadType8 clears OBJ_FLAG_FOOTPRINT_ON on the
 * way out instead so a later pass puts it down -- the two halves of one
 * decision, in two functions. */
void *__cdecl CreateRoach(int32_t kind, char *name, int32_t x, int32_t y,
                          int32_t army, int32_t flags, int32_t a7, int32_t uid)
{
    uint8_t  *o    = (uint8_t *)am2_malloc(AM2_ROACH_BYTES);
    uint32_t  at   = (uint32_t)(uint16_t)(int16_t)x
                   | ((uint32_t)(uint16_t)(int16_t)y << 16);
    uint8_t  *rows;

    memset(o, 0, AM2_ROACH_BYTES);

    /* Both already zero from the memset; the original writes them anyway. */
    *(uint8_t *)(o + OBJ_OFF_FACING) = 0;
    *(uint32_t *)(o + OBJ_OFF_FLAGS) |=
        (uint32_t)flags | OBJ_FLAG_BIT0 | OBJ_FLAG_BIT4 | OBJ_FLAG_SNAP_HEADING;

    *(int8_t *)(o + OBJ_OFF_ARMY)      = (int8_t)army;
    *(int32_t *)(o + VEHICLE_OFF_KIND) = kind;

    /* Index 0 of the block, which aai/game.aai calls ROACH_HEIGHT; read as a
     * byte out of a dword that ships 32. */
    *(int8_t *)(o + OBJ_OFF_HEIGHT_ADJ) =
        *(const int8_t *)AM2_IMAGE(ADDR_GAME_CONSTANTS);
    *(int16_t *)(o + OBJ_OFF_MAX_HEALTH) =
        *(const int16_t *)AM2_IMAGE(ADDR_ROACH_HEALTH);
    SetMaxHealth(o, *(const int32_t *)AM2_IMAGE(ADDR_ROACH_HEALTH));
    *(int16_t *)(o + OBJ_OFF_HEALTH) =
        *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);

    *(int32_t *)(o + OBJ_OFF_RANK) = 7;
    *(int32_t *)(o + OBJ_OFF_FIELD_FC) =
        (int32_t)(orig_game_rand() % AM2_ROACH_STAGGER_MS)
        + *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    ObjInitCommon(o, name, 8, at,
                         (const int32_t *)AM2_IMAGE(ADDR_ROACH_BOX), a7, uid);
    BuildRowSet(o + OBJ_OFF_SUBRECORD, 1,
                (const void *)AM2_IMAGE(ADDR_ROACH_ROW_SPEC), x, y,
                (const void *)AM2_IMAGE(ADDR_ROACH_BOX));

    if ((flags & (int32_t)OBJ_FLAG_DESTROYED) != 0)
        SubrecHideRows(o + OBJ_OFF_SUBRECORD);

    PtrListPush(((void **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)[army],
                *(void **)(o + 4));

    rows = *(uint8_t **)(o + OBJ_OFF_ROWS);
    *(const void **)(rows + ROW_OFF_ANIM_CUR) =
        (const void *)(uintptr_t)ADDR_ROACH_ANIMS;
    *(int16_t *)(rows + ROW_OFF_FIELD_26) = AM2_ROW_FIELD26_INIT;
    *(uint32_t *)rows |= ROW_FLAG_BIT8;

    *(int32_t *)(o + OBJ_OFF_DEATH_STATE) = 1;
    *(uint8_t *)(o + OBJ_OFF_FIELD_540)   = 0;

    SetAnimFrame(rows, *(const int16_t *)AM2_IMAGE(ADDR_ROACH_START_FRAME), 0);
    SetObjField530(o, 1);

    if (*(const int32_t *)(uintptr_t)ADDR_STATE_ENTERED == 0)
        ObjSetRoachFootprint(o);

    return o;
}

/* ObjSetFootprint and ObjClearFootprint -- original 0x0045A620 and 0x0045A770,
 * six and seven callers. The GENERAL footprint pair, of which the roach pair
 * above is the special case: same window, same stamp trick, same cell weights,
 * with the vehicle mask in place of the roach one and a KIND index the roach
 * has no use for.
 *
 * THEY ARE EXACT TWINS AND THE COUNT SAYS SO: 95 instructions each, differing
 * in four places once branch targets are normalised -- the flag test's sense,
 * `add 0xF` against `add 0xF1`, TileCoverAdd against TileCoverSub, and the
 * `or` against the `and`. Nothing else.
 *
 * SO THEY SHARE A BODY HERE, AND THE ROACH PAIR ABOVE DOES NOT. That is an
 * inconsistency inside one file and it is deliberate rather than an oversight:
 * the 95-against-95 diff is evidence that the two differ in exactly four
 * places, and a shared body is that evidence written down where it cannot go
 * stale. The roach pair predates the measurement. If either is changed to
 * match the other it should be the roach pair, and only after the same diff is
 * run on it.
 *
 * The mask record is [kind * 32 + dir], where the kind is VEHICLE_OFF_KIND and
 * the direction comes from RoundTo8 over the row's heading plus its bias,
 * rounded to the animation's own directionBits. Both halves of the slot are
 * needed and the roach version has only the second.
 *
 * The stamp is bumped ONCE PER CALL and compared per cell, so a cell that two
 * of the mask's points land on is weighted once. It is a different array from
 * the roach pair's, which is worth stating because the two look
 * interchangeable: ADDR_OBJ_MARK is at 0x00661E20 and ADDR_ROACH_MARK at
 * 0x00656128.
 */
static void ObjFootprint(void *obj, int32_t set)
{
    uint8_t        *o = (uint8_t *)obj;
    const uint8_t  *rows;
    const AM2_Anim *anim;
    int32_t         dir, i, count;
    int32_t         baseX, baseY;
    uint32_t        slot;
    const int16_t  *pts;

    if (set) {
        if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_FOOTPRINT_ON)
            return;
    } else {
        if (!(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_FOOTPRINT_ON))
            return;
    }

    ++*(uint16_t *)(uintptr_t)ADDR_OBJ_MARK_STAMP;

    rows = *(const uint8_t *const *)(o + OBJ_OFF_ROWS);
    anim = *(const AM2_Anim *const *)(rows + ROW_OFF_ANIM_PLAYING);

    baseX = (*(const int16_t *)(o + OBJ_OFF_POS) >> AM2_MASK_CELL_SHIFT)
            - AM2_MASK_WINDOW_HALF;
    baseY = (*(const int16_t *)(o + OBJ_OFF_POS + 2) >> AM2_MASK_CELL_SHIFT)
            - AM2_MASK_WINDOW_HALF;

    dir = RoundTo8((*(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)
                    + *(const uint8_t *)(rows + ROW_OFF_HEADING)) & 0xFF,
                   anim->directionBits) & 0xFF;

    slot = ((uint32_t)dir
            + (uint32_t)*(const int32_t *)(o + VEHICLE_OFF_KIND) * 32)
           * AM2_MASK_STRIDE;

    count = *(const int32_t *)((const uint8_t *)
                AM2_IMAGE(ADDR_VEHICLE_MASK_COUNT) + slot);
    pts   = (const int16_t *)((const uint8_t *)
                AM2_IMAGE(ADDR_VEHICLE_MASK) + slot);

    for (i = 0; i < count; i++) {
        uint32_t  at;
        uint16_t *mark;
        int32_t   tile;

        ((int16_t *)&at)[0] = (int16_t)(pts[i * 2]
                                        + *(const int16_t *)(o + OBJ_OFF_POS));
        ((int16_t *)&at)[1] = (int16_t)(pts[i * 2 + 1]
                                        + *(const int16_t *)(o + OBJ_OFF_POS
                                                             + 2));

        mark = (uint16_t *)(uintptr_t)ADDR_OBJ_MARK
               + (((int32_t)((int16_t *)&at)[0] >> AM2_MASK_CELL_SHIFT) - baseX)
                 * AM2_MASK_WINDOW
               + (((int32_t)((int16_t *)&at)[1] >> AM2_MASK_CELL_SHIFT)
                  - baseY);

        if (*mark == *(const uint16_t *)(uintptr_t)ADDR_OBJ_MARK_STAMP)
            continue;

        tile = TileOfPoint(at);
        if (set) {
            ((uint8_t *)(uintptr_t)ADDR_CELL_WEIGHTS)[(uint32_t)tile & 0xFFFF]
                += AM2_TILE_COVER_STEP;
            TileCoverAdd((uint16_t)tile);
        } else {
            ((uint8_t *)(uintptr_t)ADDR_CELL_WEIGHTS)[(uint32_t)tile & 0xFFFF]
                -= AM2_TILE_COVER_STEP;
            TileCoverSub((uint16_t)tile);
        }

        *mark = *(const uint16_t *)(uintptr_t)ADDR_OBJ_MARK_STAMP;
    }

    if (set)
        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_FOOTPRINT_ON;
    else
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_FOOTPRINT_ON;
}

void __cdecl ObjSetFootprint(void *obj)
{
    ObjFootprint(obj, 1);
}

void __cdecl ObjClearFootprint(void *obj)
{
    ObjFootprint(obj, 0);
}

/* AllObjectsInRect is reconstructed, in win32/mapdraw.cpp. Declared here
 * rather than by including that header for the reason item.cpp already
 * declares PlaySoundAt above: this file is on the flat side of the split and
 * mapdraw.h reaches win32.h. Neither of its parameters names a Win32 type. */
void *__cdecl AllObjectsInRect(const AM2_Rect *r, const void *desc);

/* Its predicate-taking sibling, same file and same reason for being declared
 * here rather than included. */
void *__cdecl ObjectsInRect(const AM2_Rect *r, const void *desc,
                            int32_t (__cdecl *keep)(void *obj));

/* CreateWeapon, still original -- the type-4 arm of the item-create message,
 * and it names itself in its own log line. Eight arguments. */
typedef void *(__cdecl *AM2_CreateWeaponFn)(const char *name, int32_t type,
                                            int32_t key, uint32_t at,
                                            int32_t flags, int32_t quantity,
                                            int32_t g, int32_t h);
#define orig_create_weapon \
    ((AM2_CreateWeaponFn)(uintptr_t)ADDR_CREATE_WEAPON)

/* The respawn's other half, still original: pick a random eligible kind and
 * hand back one of its ADDR_MISSILE_DEFS fields. */
typedef int32_t (__cdecl *AM2_RandomKindFn)(int32_t *out);
#define orig_random_respawn_kind \
    ((AM2_RandomKindFn)(uintptr_t)ADDR_RANDOM_RESPAWN_KIND)

/* WeaponRespawn -- original 0x00448280, eight callers. When a weapon leaves
 * the map in a multiplayer game, put another one back where it was.
 *
 * FOUR GATES, AND EVERY ONE OF THEM IS WHY IT CANNOT BE EXERCISED HERE. The
 * object must be a type-4 WEAPON; there must be an ADDR_MP_SESSION; bit 20 of
 * ADDR_GAME_OVER_FLAGS must be set; and the weapon's own
 * OBJ_FLAG_8000 must be clear -- which it then SETS, so the whole thing
 * fires at most once per weapon. No DirectPlay session opens on this machine,
 * so the second gate always refuses and every drive stops there.
 *
 * IT IS THE HOST'S JOB. COMM_OFF_IS_HOST gates everything but the flag: a
 * client reaches the end, sets OBJ_FLAG_8000 and creates nothing, so it
 * neither respawns the weapon nor tries again later. The host's copy arrives
 * as an ordinary item-create message.
 *
 * TWO WAYS TO CHOOSE WHAT COMES BACK. With OBJ_FLAG_RESPAWN_RANDOM set it asks
 * ADDR_RANDOM_RESPAWN_KIND for any eligible kind and takes the quantity from
 * that kind's record; without it, the same weapon comes back with the ammo it
 * had, and a type record whose +0x28 is zero refuses outright.
 *
 * THE ARGUMENT SHUFFLE IS THE ONE HARD PART AND IT IS NOT VISIBLE IN THE
 * SOURCE. The original pushes eight dwords, lets KeyLookupTriple consume the
 * top three, cleans exactly those with `add esp, 0xc`, and then puts three
 * fresh pushes on top of the five that are left -- so one call's arguments are
 * built across another call. Reading it by pairing each push with the nearest
 * call gets BOTH calls wrong, which is what made this function look
 * unreadable when it was first declined. Written here as the two calls it is.
 *
 * The event goes out with a delay of AM2_WEAPON_RESPAWN_MS -- five minutes --
 * on the NEW weapon's uid, so whatever consumes it is timing the replacement
 * and not the original.
 */
void __cdecl WeaponRespawn(void *obj)
{
    uint8_t  *o = (uint8_t *)obj;
    uint8_t  *comm;
    uint32_t  flags;
    int32_t   kind;
    int32_t   quantity = 0;
    const char *name = 0;
    void     *made;

    if (!ObjIsType4((const AM2_Object *)obj))
        return;
    if (!*(const int32_t *)(uintptr_t)ADDR_MP_SESSION)
        return;
    if (!(*(const uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS & 0x100000u))
        return;

    flags = *(const uint32_t *)(o + OBJ_OFF_FLAGS);
    if (flags & OBJ_FLAG_8000)
        return;

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        goto mark;

    if (flags & OBJ_FLAG_RESPAWN_RANDOM) {
        kind = orig_random_respawn_kind(&quantity);
    } else {
        const uint8_t *rec = *(const uint8_t *const *)(o + OBJ_OFF_FIELD_C0);

        if (!*(const int32_t *)(rec + 0x28))
            return;
        quantity = *(const int32_t *)(o + ITEM_OFF_AMMO);
        kind     = *(const int32_t *)(rec + ITEMTYPE_OFF_KIND);
    }

    if (*(const int32_t *)(o + ITEM_OFF_NAME_INDEX) > 0)
        name = *(const char *const *)
                   ((const uint8_t *)*(void *const *)
                        (uintptr_t)ADDR_SCRIPT_NAMES
                    + (uint32_t)*(const int32_t *)(o + ITEM_OFF_NAME_INDEX)
                      * AM2_NAME_TABLE_STRIDE);

    made = orig_create_weapon(name, AM2_OBJ_TYPE_WEAPON,
                              KeyLookupTriple(AM2_WEAPON_RESPAWN_KEY,
                                              (uint32_t)kind, 0),
                              *(const uint32_t *)(o + OBJ_OFF_POS),
                              (int32_t)(*(const uint32_t *)(o + OBJ_OFF_FLAGS)
                                        | 4u),
                              quantity, 0, 0);

    if (made)
        EventNotify(0, *(const int32_t *)(uintptr_t)ADDR_RULE_UID_C,
                    *(const uint32_t *)((const uint8_t *)made + 4),
                    0, 0, 0, 0, AM2_WEAPON_RESPAWN_MS, 0, 1);

mark:
    *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_8000;
}

/* RoachAliveStepB -- original 0x0043D5B0, one caller: StepType8, on the alive
 * path. Find a direction the roach can actually move in, by fanning out from
 * the one it settled on last time.
 *
 * THE FAN IS THE WHOLE FUNCTION. ROACH_OFF_FAN counts attempts and the
 * direction comes out of it as `base + n/2` for an even n and `base - n/2` for
 * an odd one, masked to eight -- so the search goes straight ahead, one step
 * clockwise, one anticlockwise, two clockwise, and so on. It stops the moment
 * RoachStepAllowed says yes, which is the ONLY exit that leaves the fan
 * counter where it is; every other path either resets it or runs out.
 *
 * ITS TWO ARMS DIFFER BY WHEN THEY WERE LAST BLOCKED. Under
 * AM2_ROACH_BLOCKED_MS since ROACH_OFF_STAMP it resumes the fan where it left
 * off; past that it starts by trying the current heading, and seeds
 * ROACH_OFF_BASE_DIR from the turn RoachStepAllowed just produced -- but only
 * when the base is still zero. So a roach that has never been blocked adopts
 * the first direction it is refused in as the centre of every later fan.
 *
 * THE EARLY-OUT NEEDS BOTH HALVES. It returns only when the step window's
 * facing already matches the object's AND ROACHSTEP_OFF_STATE is 1 -- and that
 * return is the RESET path, which zeroes the base and the fan. So the roach
 * forgets its search the moment it is pointed where it already faces.
 *
 * FOUR CALLS TO RoachStepAllowed AND ALL FOUR HAND IT THE CALLER'S ARG2 SLOT
 * as the turn output. MSVC reuses the argument slot as a local; the three
 * different `lea` displacements in the original are three stack depths, not
 * three variables, and reading them as separate locals would invent two.
 */
void __cdecl RoachAliveStepB(void *obj, uint8_t *step)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  turn;
    uint32_t now;
    int32_t  tries;

    if (step[ROACHSTEP_OFF_FACING] == *(const uint8_t *)(o + OBJ_OFF_FACING)
        && *(const int32_t *)(step + ROACHSTEP_OFF_STATE) == 1)
        goto reset;

    now = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    if (now - *(const uint32_t *)(o + ROACH_OFF_STAMP)
        >= (uint32_t)AM2_ROACH_BLOCKED_MS) {
        *(uint32_t *)(o + ROACH_OFF_STAMP) = now;

        if (!RoachStepAllowed(obj, step, &turn))
            goto reset;

        *(uint32_t *)(o + ROACH_OFF_STAMP) =
            *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

        if (*(const int32_t *)(o + ROACH_OFF_BASE_DIR) == 0)
            *(int32_t *)(o + ROACH_OFF_BASE_DIR) =
                FacingFromDelta14(step, turn);
    } else {
        int32_t n   = *(const int32_t *)(o + ROACH_OFF_FAN);
        int32_t dir = (n & 1)
            ? ((*(const int32_t *)(o + ROACH_OFF_BASE_DIR) - (n >> 1)) & 7)
            : (((n >> 1) + *(const int32_t *)(o + ROACH_OFF_BASE_DIR)) & 7);

        SetFacing14(dir, obj, step);
        if (!RoachStepAllowed(obj, step, &turn))
            return;

        *(uint32_t *)(o + ROACH_OFF_STAMP) =
            *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
    }

    for (tries = 0; tries < AM2_ROACH_FAN_LIMIT; tries++) {
        int32_t n   = ++*(int32_t *)(o + ROACH_OFF_FAN);
        int32_t dir = (n & 1)
            ? ((*(const int32_t *)(o + ROACH_OFF_BASE_DIR) - (n >> 1)) & 7)
            : (((n >> 1) + *(const int32_t *)(o + ROACH_OFF_BASE_DIR)) & 7);

        SetFacing14(dir, obj, step);
        if (!RoachStepAllowed(obj, step, &turn))
            return;
    }
    return;

reset:
    *(int32_t *)(o + ROACH_OFF_BASE_DIR) = 0;
    *(int32_t *)(o + ROACH_OFF_FAN)      = 0;
}

/* RoachStepAllowed -- original 0x0043D0F0, four callers. May the roach step
 * the way this control record says? Work out the speed it would move at, the
 * turn it would make and the direction it would end up facing, and answer
 * whether the mask weight where it would land is at least what it is here.
 *
 * IT NAMED FOUR OF THE EIGHT ROACH CONSTANTS, which had been left nameless on
 * the argument that an unused name is a second name waiting to happen. Two
 * symmetric arms: ADDR_ROACH_FORVEL and _REVVEL cap the speed, _FORACC and
 * _REVACC are what a frame adds to or takes off it, and the reverse cap is
 * applied NEGATED -- which is what says the two are one signed speed rather
 * than two magnitudes. Only ARMOR is still nameless.
 *
 * IT WRITES THROUGH ITS THIRD ARGUMENT TWICE and the second write is a clamp:
 * the raw AngleDelta goes out first and is then replaced by its sign. A caller
 * reading the pointer between the two would see the delta; nothing does, and
 * the original stores both, so both are stored.
 *
 * THE TURN CAN BE CANCELLED AND THE MEASUREMENT IS NOT. If the row turned
 * within AM2_ROACH_TURN_HOLD_MS the turn is zeroed -- but the direction handed
 * to the second RoachMaskWeight was computed BEFORE that, so the weight is
 * measured for the direction the roach WOULD have turned to. The facing handed
 * to MoveStepPoint uses the cancelled value. Two different answers from one
 * turn, and reproducing it means keeping the order.
 *
 * The here-weight is clamped UP to AM2_ROACH_WEIGHT_FLOOR before the compare,
 * so a completely clear cell still demands 30 of the destination.
 *
 * ITS SECOND ARGUMENT IS A WINDOW INTO THE OBJECT, not a record of its own.
 * StepType8 passes `obj + OBJ_OFF_FIELD_540`, so +0x14 is OBJ_OFF_DEATH_STATE
 * -- 1 while alive, 5 or 6 once DamageRoach has killed it. This was called a
 * "control record" with a STOP field one batch ago, which was a second name
 * for fields that already had one and a meaning guessed off one branch. What
 * the branch does is skip the speed arms when the state is 1; WHY is not
 * established, and the comment no longer pretends it is.
 *
 * That arm leaves the speed at zero and returns 0 -- but only AFTER the turn
 * has been written through the third argument. So the caller still gets a
 * turn out of a call that answers no.
 */
int32_t __cdecl RoachStepAllowed(void *obj, const void *ctrl, int32_t *turn)
{
    const uint8_t  *c = (const uint8_t *)ctrl;
    uint8_t        *o = (uint8_t *)obj;
    const uint8_t  *rows;
    const AM2_Anim *anim;
    int32_t         speed = 0;
    int32_t         cur, next, here;
    uint8_t         facing;
    uint32_t        at;

    if (*(const int32_t *)(c + ROACHSTEP_OFF_STATE) != 1) {
        int32_t reverse = *(const int32_t *)(c + ROACHSTEP_OFF_FLAG18);
        double  delta   = (double)*(const float *)
                              (uintptr_t)ADDR_FRAME_DELTA_SEC;

        if (reverse) {
            int32_t cap = *(const int32_t *)AM2_IMAGE(ADDR_ROACH_REVVEL);
            int32_t v   = (int32_t)((double)*(const int32_t *)
                                        (o + OBJ_OFF_FIELD_44)
                                    - (double)*(const int32_t *)
                                        AM2_IMAGE(ADDR_ROACH_REVACC) * delta);

            speed = (-cap > v) ? -cap : v;
        } else {
            int32_t cap = *(const int32_t *)AM2_IMAGE(ADDR_ROACH_FORVEL);
            int32_t v   = (int32_t)((double)*(const int32_t *)
                                        AM2_IMAGE(ADDR_ROACH_FORACC) * delta
                                    + (double)*(const int32_t *)
                                        (o + OBJ_OFF_FIELD_44));

            speed = (cap < v) ? cap : v;
        }
    }

    rows = *(const uint8_t *const *)(o + OBJ_OFF_ROWS);
    anim = *(const AM2_Anim *const *)(rows + ROW_OFF_ANIM_PLAYING);
    if (!anim)
        return 0;

    *turn = AngleDelta(*(const uint8_t *)(o + OBJ_OFF_FACING),
                       *(const uint8_t *)(c + ROACHSTEP_OFF_FACING));
    *turn = Clamp(*turn, -1, 1);

    cur = RoundTo8((*(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)
                    + *(const uint8_t *)(rows + ROW_OFF_HEADING)) & 0xFF,
                   anim->directionBits) & 0xFF;
    next = RoundTo8((*(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)
                     + *(const uint8_t *)(rows + ROW_OFF_HEADING)
                     + *turn) & 0xFF,
                    anim->directionBits) & 0xFF;

    if (cur != next
        && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
           - *(const uint32_t *)(rows + ROW_OFF_TURN_STAMP)
           <= AM2_ROACH_TURN_HOLD_MS)
        *turn = 0;

    facing = (uint8_t)(*(const uint8_t *)(o + OBJ_OFF_FACING)
                       + (int8_t)*turn);

    if (speed == 0)
        return 0;

    here = RoachMaskWeight(obj, cur, *(const uint32_t *)(o + OBJ_OFF_POS), 0);
    if (here < AM2_ROACH_WEIGHT_FLOOR)
        here = AM2_ROACH_WEIGHT_FLOOR;

    MoveStepPoint(obj, facing, 0, speed, 0, 0, (AM2_Point *)&at);

    return RoachMaskWeight(obj, next, at, 0) >= here;
}

/* RoachStepTailA -- original 0x0043D750, four callers. Commit one roach step:
 * bite if it is in the biting state, work out the speed and the turn, refuse
 * the move if where it would land is no better than where it is, and then
 * write the facing, the speed and the row heading and actually move.
 *
 * IT IS THE COMMITTING TWIN OF RoachStepAllowed, which asks the same question
 * and answers it without touching anything. Reading the two together is what
 * made this one quick, and the two places they DISAGREE are the interesting
 * part, because both are in the image and neither is a misreading:
 *
 *   - The turn is clamped to +-6 here and to +-1 in the predicate.
 *   - The four physics constants are COMPILED IN here -- 130, 80, 100.0, 80.0
 *     as an immediate pair and two .rdata floats -- where the predicate reads
 *     ADDR_ROACH_FORVEL, _REVVEL, _FORACC and _REVACC out of the image. The
 *     values are the same four numbers today, so nothing diverges; they would
 *     diverge if game.aai ever set that block to anything else, and then the
 *     predicate would answer for one roach and this would move a different
 *     one. Reproduced as written rather than unified, which is the whole rule.
 *   - The second weight call passes 1 as its last argument where the predicate
 *     passes 0. The argument is unused in the callee, so this changes nothing
 *     and is kept because the original does it.
 *
 * THE STAMP IS WRITTEN HERE AND ONLY READ THERE, which is the asymmetry that
 * makes the pair make sense: the predicate must not disturb the hysteresis it
 * is measuring against, so ROW_OFF_TURN_STAMP is updated by whichever of the
 * two actually commits.
 *
 * OBJ_OFF_FIELD_44 IS WRITTEN TWICE, once before StepObjRows and once after,
 * with the same value. Nothing here reads it in between, so the second store
 * is only explicable as StepObjRows modifying it -- that is the original's
 * shape and both stores are kept. Dropping either is invisible until the row
 * step starts changing the field.
 *
 * The stuck counter multiplies the speed BEFORE the clamp, so a roach that has
 * been refused ten times asks for ten times the speed and still gets no more
 * than the cap. The guard is `!= 0`, not `> 0` -- the original tests it with
 * `jbe` after `test`, which on a self-test is exactly equality with zero. */
void __cdecl RoachStepTailA(void *obj, uint8_t *ctrl)
{
    uint8_t        *o = (uint8_t *)obj;
    const uint8_t  *c = (const uint8_t *)ctrl;
    uint8_t        *rows;
    const AM2_Anim *anim;
    int32_t         speed = 0;
    int32_t         cap   = 0;
    int32_t         turn, cur, next, here;
    uint8_t         facing;
    uint32_t        at;

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_530) == 4
        && *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)
           > *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS)
        RoachBite(obj);

    if (*(const int32_t *)(c + ROACHSTEP_OFF_STATE) != 1) {
        int32_t reverse = *(const int32_t *)(c + ROACHSTEP_OFF_FLAG18);
        double  delta   = (double)*(const float *)
                              (uintptr_t)ADDR_FRAME_DELTA_SEC;
        int32_t stuck;

        cap = reverse ? 80 : 130;

        if (reverse) {
            int32_t v = (int32_t)((double)*(const int32_t *)
                                      (o + OBJ_OFF_FIELD_44)
                                  - delta * 80.0);

            speed = (-cap > v) ? -cap : v;
        } else {
            int32_t v = (int32_t)(delta * 100.0
                                  + (double)*(const int32_t *)
                                      (o + OBJ_OFF_FIELD_44));

            speed = (cap < v) ? cap : v;
        }

        stuck = *(const int32_t *)(o + OBJ_OFF_STUCK_COUNT);
        if (stuck != 0 && speed != 0) {
            int32_t shove = stuck * speed;

            if (reverse)
                speed = (-cap > shove) ? -cap : shove;
            else
                speed = (cap < shove) ? cap : shove;
        }
    }

    rows = *(uint8_t *const *)(o + OBJ_OFF_ROWS);
    anim = *(const AM2_Anim *const *)(rows + ROW_OFF_ANIM_PLAYING);
    if (!anim)
        return;

    turn = AngleDelta(*(const uint8_t *)(o + OBJ_OFF_FACING),
                      *(const uint8_t *)(c + ROACHSTEP_OFF_FACING));
    turn = Clamp(turn, -6, 6);

    cur = RoundTo8((*(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)
                    + *(const uint8_t *)(rows + ROW_OFF_HEADING)) & 0xFF,
                   anim->directionBits) & 0xFF;
    next = RoundTo8((turn + *(const uint8_t *)(rows + ROW_OFF_HEADING)
                     + *(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)) & 0xFF,
                    anim->directionBits) & 0xFF;

    if (cur != next) {
        uint32_t now = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

        if (now - *(const uint32_t *)(rows + ROW_OFF_TURN_STAMP)
            > AM2_ROACH_TURN_HOLD_MS)
            *(uint32_t *)(rows + ROW_OFF_TURN_STAMP) = now;
        else
            turn = 0;
    }

    facing = (uint8_t)(turn + *(const uint8_t *)(o + OBJ_OFF_FACING));

    here = RoachMaskWeight(obj, cur, *(const uint32_t *)(o + OBJ_OFF_POS), 0);
    if (here < AM2_ROACH_WEIGHT_FLOOR)
        here = AM2_ROACH_WEIGHT_FLOOR;

    MoveStepPoint(obj, facing, 0, speed, 0, 0, (AM2_Point *)&at);

    if (((const AM2_Point *)&at)->x != ((const AM2_Point *)(o + OBJ_OFF_POS))->x
        || ((const AM2_Point *)&at)->y
           != ((const AM2_Point *)(o + OBJ_OFF_POS))->y) {
        int32_t n;

        if (RoachMaskWeight(obj, next, at, 1) >= here) {
            speed = 0;
            if (*(const uint32_t *)(o + OBJ_OFF_STUCK_SINCE) == 0)
                *(uint32_t *)(o + OBJ_OFF_STUCK_SINCE) =
                    *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
            n = *(const int32_t *)(o + OBJ_OFF_STUCK_COUNT) + 1;
        } else {
            *(uint32_t *)(o + OBJ_OFF_STUCK_SINCE) = 0;
            n = 0;
        }
        *(int32_t *)(o + OBJ_OFF_STUCK_COUNT) = n;
    }

    *(uint8_t *)(o + OBJ_OFF_FACING) = facing;
    *(int32_t *)(o + OBJ_OFF_FIELD_44) = speed;
    SetObjField530(obj, *(const int32_t *)(c + ROACHSTEP_OFF_STATE));
    *(uint8_t *)(rows + ROW_OFF_HEADING) = *(const uint8_t *)(o + OBJ_OFF_FACING);
    StepObjRows(obj);
    *(int32_t *)(o + OBJ_OFF_FIELD_44) = speed;
    ObjMoveAlongFacing(obj, 0, 0, 0);
}

/* RoachBite -- original 0x0043D330, one caller: the roach's per-frame step,
 * which reaches it only in state 4. Step AM2_ROACH_REACH along the facing,
 * play a sound at the point stepped to, and damage every object in a 48x48 box
 * around it that is not on the roach's side.
 *
 * THE NAME IS MINE. Nothing in the image names this function -- it pushes no
 * string and its caller pushes none either -- so `RoachBite` describes the
 * body rather than recovering anything, and is written down as such. What IS
 * evidenced is the damage: ADDR_ROACH_DAMAGE is one of the eight constants
 * aai/game.aai names, and it goes in at kind 5.
 *
 * The direction handed to DamageObject is `facing + 0x80`, which is the
 * facing REVERSED -- the direction the blow arrives FROM, as the victim sees
 * it. A byte add wraps, so no masking is needed and none is written.
 *
 * IT CLEARS OBJ_OFF_DEADLINE_58 ON ENTRY, before anything else, so the bite
 * always resets its own timer even if the box turns out to be empty.
 *
 * ONE THING IS DELIBERATELY NOT REPRODUCED, and it is measured rather than
 * waved through. The original hands PlaySoundAt the PACKED point as `x` and
 * the dword starting two bytes into it as `y` -- MSVC put the two int16 in one
 * slot and read it twice at overlapping offsets -- so the upper half of each
 * argument is the other coordinate or whatever the frame held. PlaySoundAt
 * assigns `where.x = (int16_t)x` and `where.y = (int16_t)y`, so both upper
 * halves are discarded and clean arguments cannot differ. Passing the two
 * coordinates plainly.
 */
void __cdecl RoachBite(void *roach)
{
    uint8_t  *o      = (uint8_t *)roach;
    int32_t   facing = *(const uint8_t *)(o + OBJ_OFF_FACING);
    int32_t   x      = *(const int16_t *)(o + OBJ_OFF_POS);
    int32_t   y      = *(const int16_t *)(o + OBJ_OFF_POS + 2);
    float     reach  = *(const float *)(uintptr_t)ADDR_ROACH_REACH;
    const int32_t *box = (const int32_t *)(uintptr_t)ADDR_ROACH_BITE_BOX;
    AM2_Rect  at;
    int16_t   toX, toY;
    uint8_t  *hit;

    *(int32_t *)(o + OBJ_OFF_DEADLINE_58) = 0;

    toX = (int16_t)(int32_t)(Cos8(facing) * reach + (float)x);
    toY = (int16_t)(int32_t)(Sin8(facing) * reach + (float)y);

    PlaySoundAt(AM2_ROACH_BITE_SOUND, 0, 0, toX, toY);

    at.left   = toX + box[0];
    at.top    = toY + box[1];
    at.right  = toX + box[2];
    at.bottom = toY + box[3];

    hit = (uint8_t *)AllObjectsInRect(
              &at, (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC);

    while (hit) {
        if (!ObjsAreAllied(o, hit, 0))
            DamageObject(hit,
                         *(const int32_t *)AM2_IMAGE(ADDR_ROACH_DAMAGE),
                         AM2_ROACH_DAMAGE_KIND,
                         *(const uint32_t *)(o + 4),
                         (uint8_t)(facing + 0x80), 0);
        hit = *(uint8_t **)(hit + OBJ_OFF_QUERY_NEXT);
    }
}

/* ObjClearRoachFootprint -- original 0x0043CA00, three callers, one of them
 * the type-8 destroy handler. Take a roach's footprint back off the map:
 * every cell its mask covers gets AM2_TILE_COVER_STEP added back to
 * ADDR_CELL_WEIGHTS, and the object's OBJ_FLAG_FOOTPRINT_ON is cleared.
 *
 * A CELL IS DECREMENTED ONCE HOWEVER MANY MASK POINTS LAND IN IT, and the
 * mechanism is worth the paragraph. ADDR_ROACH_MARK_STAMP is bumped once per
 * call and written into every cell of a 16 x 16 uint16 window as that cell is
 * done; a cell already carrying this call's stamp is skipped. So the window
 * needs no clearing between calls -- a stamp that has not been written this
 * call cannot match -- which is what makes a per-call scratch array cost
 * nothing. The window's extent is not inferred: the stamp sits 0x200 bytes
 * past the array, which is exactly 256 uint16.
 *
 * THE WINDOW IS IN 16-UNIT CELLS AND SO IS THE MASK, where the cell grid the
 * object registration uses is 256. Both coordinates are shifted by
 * AM2_MASK_CELL_SHIFT and taken relative to the object's own cell less
 * AM2_MASK_WINDOW_HALF, so a mask point further than eight of these from the
 * object indexes outside the window -- and nothing bounds it. Reproduced; the
 * mask is built to fit.
 *
 * The direction is `RoundTo8(row->heading + ROW_OFF_HEADING_BIAS, bits)` with
 * the animation's own directionBits, and it is used AS THE RECORD INDEX --
 * unlike MoveStepPoint, which rounds the same way and then shifts the result
 * back up to eight bits because it wants a heading rather than a slot.
 *
 * ADDR_TILE_COVER_SUB is called for each cell as well, on the tile rather than
 * the cell, and after the weight has already been adjusted.
 */
void __cdecl ObjClearRoachFootprint(void *obj)
{
    uint8_t       *o = (uint8_t *)obj;
    const uint8_t *rows;
    const AM2_Anim *anim;
    int32_t        dir;
    int32_t        baseX, baseY;
    uint32_t       slot;
    int32_t        i;

    if (!(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_FOOTPRINT_ON))
        return;

    ++*(uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP;

    rows = *(const uint8_t *const *)(o + OBJ_OFF_ROWS);
    anim = *(const AM2_Anim *const *)(rows + ROW_OFF_ANIM_PLAYING);

    baseX = (*(const int16_t *)(o + OBJ_OFF_POS) >> AM2_MASK_CELL_SHIFT)
            - AM2_MASK_WINDOW_HALF;
    baseY = (*(const int16_t *)(o + OBJ_OFF_POS + 2) >> AM2_MASK_CELL_SHIFT)
            - AM2_MASK_WINDOW_HALF;

    dir = RoundTo8((*(const uint8_t *)(rows + ROW_OFF_HEADING_BIAS)
                    + *(const uint8_t *)(rows + ROW_OFF_HEADING)) & 0xFF,
                   anim->directionBits) & 0xFF;

    slot = (uint32_t)dir * AM2_MASK_STRIDE;

    for (i = 0; i < ((const int32_t *)((const uint8_t *)
             AM2_IMAGE(ADDR_ROACH_MASK_COUNT) + slot))[0]; i++) {
        const int16_t *pts = (const int16_t *)
            ((const uint8_t *)AM2_IMAGE(ADDR_ROACH_MASK) + slot);
        uint32_t  at;
        uint16_t *mark;
        int32_t   tile;

        ((int16_t *)&at)[0] = (int16_t)(pts[i * 2]
                                        + *(const int16_t *)(o + OBJ_OFF_POS));
        ((int16_t *)&at)[1] = (int16_t)(pts[i * 2 + 1]
                                        + *(const int16_t *)(o + OBJ_OFF_POS
                                                             + 2));

        mark = (uint16_t *)(uintptr_t)ADDR_ROACH_MARK
               + (((int32_t)((int16_t *)&at)[0] >> AM2_MASK_CELL_SHIFT) - baseX)
                 * AM2_MASK_WINDOW
               + (((int32_t)((int16_t *)&at)[1] >> AM2_MASK_CELL_SHIFT)
                  - baseY);

        if (*mark == *(const uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP)
            continue;

        tile = TileOfPoint(at);
        ((uint8_t *)(uintptr_t)ADDR_CELL_WEIGHTS)[(uint32_t)tile & 0xFFFF]
            -= AM2_TILE_COVER_STEP;
        TileCoverSub((uint16_t)tile);

        *mark = *(const uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP;
    }

    *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_FOOTPRINT_ON;
}

/* RoachMaskWeight -- original 0x0043D050, four call sites in two functions,
 * one of them ADDR_ROACH_STEP_TAIL_A. How obstructed is a roach that stands at
 * `at` facing `dir`: the sum of BlockWeightDamaging over every point of its
 * mask for that direction. The name is ours, and it uses the family's own
 * vocabulary -- a MASK indexed by a DIRECTION, which the vehicle builder's
 * "vehicle mask direction: %d" settled for all of them.
 *
 * THE TABLE IS ADDRESSED AS TWO ARRAYS WITH ONE STRIDE, which is how the
 * original does it and worth writing out rather than tidying: the count comes
 * from ADDR_ROACH_MASK_COUNT and the points from ADDR_ROACH_MASK, both indexed
 * by `dir * AM2_MASK_STRIDE`, and the second is four bytes past the first.
 * Folding them into one record pointer would hide the very off-by-one that
 * ADDR_BUILD_ROACH_MASK's comment records as having cost a table.
 *
 * THE DIRECTION IS MASKED TO A BYTE and nothing checks it against
 * ADDR_ROACH_MASK_DIRECTIONS. A direction of 200 indexes 200 * 0xA4 bytes past
 * the table and sums whatever is there. The original's; the callers are
 * trusted.
 *
 * The points are added to `at` SIXTEEN BITS AT A TIME, x into the low word and
 * y into the high, so a mask point that carries the coordinate past 0x7FFF
 * wraps rather than saturating -- the same 16-bit arithmetic
 * NearestClearPoint's spiral does on the same kind of packed point.
 *
 * A count of zero or less answers 0 without looking at anything, which is the
 * only exit that does not walk.
 *
 * Its fourth argument goes straight into BlockWeightDamaging's fifth, which
 * that function never reads. Kept because the call sites are the original's.
 */
int32_t __cdecl RoachMaskWeight(void *from, int32_t dir, uint32_t at,
                                int32_t unused)
{
    uint32_t       slot  = (uint32_t)(dir & 0xFF) * AM2_MASK_STRIDE;
    const int32_t *count = (const int32_t *)
        ((const uint8_t *)AM2_IMAGE(ADDR_ROACH_MASK_COUNT) + slot);
    const int16_t *pts = (const int16_t *)
        ((const uint8_t *)AM2_IMAGE(ADDR_ROACH_MASK) + slot);
    int32_t total = 0;
    int32_t i;

    if (*count <= 0)
        return 0;

    for (i = 0; i < *count; i++) {
        uint32_t pt;
        void    *chain;

        pt = (uint32_t)(uint16_t)((uint16_t)at + (uint16_t)pts[i * 2])
             | ((uint32_t)(uint16_t)((uint16_t)(at >> 16)
                                     + (uint16_t)pts[i * 2 + 1]) << 16);

        chain = ObjectsAtPoint(&pt, (void *)(uintptr_t)ADDR_OBJ_MAP_DESC);
        total += BlockWeightDamaging(from, pt, chain, at, unused);
    }

    return total;
}

/* 0x0043CF70, one caller, 224 bytes. The FOURTH member of the block-weight
 * family, and the only one that CHANGES ANYTHING.
 *
 * The walk is BlockWeightChain's, argument for argument: the same four in the
 * same order, the same accumulation through ObjBlockWeight with the same
 * point-into-the-unused-third-parameter shuffle, and the same stop the moment
 * the total reaches AM2_BLOCK_FULL. Three things differ.
 *
 * IT DAMAGES WHAT IT WALKS PAST. Every object in the chain that
 * ObjIsWatchedKind accepts takes one point of kind-4 damage, WITH ITS OWN UID
 * AS THE ATTACKER. So a query that reads as "how obstructed is this point"
 * wears down the obstruction as a side effect of being asked, and the wear is
 * attributed to the thing being worn rather than to whoever asked. Nothing in
 * the name of a block-weight function suggests that, which is why it is the
 * first thing said here.
 *
 * The damage is gated on being single player -- ADDR_MP_SESSION zero -- or on
 * CommMustBroadcast accepting the OBJECT's army. So in a session only the
 * owner of a thing wears it down, and the others learn about it from the
 * message DamageObject sends.
 *
 * ITS TERRAIN TERM IS THE OTHER POLARITY. This tests AM2_TILE_BLOCKS and adds
 * when the bit is SET, where BlockWeightChain tests AM2_TILE_OPEN and adds
 * when it is CLEAR. Both spellings mean "impassable" over their own bit; they
 * are recorded separately because one of them being wrong is a
 * one-character error no A/B here could report.
 *
 * AND IT HAS THE HEIGHT STEP, which BlockWeightChain does not: the absolute
 * difference between the viewer's own tile height and the target tile's,
 * against AM2_BLOCK_HEIGHT_STEP. That is BlockWeightAt's term, so this
 * function is the chain variant with BlockWeightAt's terrain half bolted on.
 *
 * ITS DEAD GUARD IS SPELLED A THIRD WAY, and the family now has three. This
 * one masks to 16 bits and then tests SIGNED-LESS-THAN and then GREATER-THAN
 * 0xFFFF -- after the mask, neither can fire, and both reach a `return 0xFF`
 * that no input produces. BlockWeightAt masks then tests signed; BlockWeight-
 * Chain tests unsigned then masks. Three spellings of one vacuous check is
 * three different compilations, not one transcription slip repeated.
 *
 * A FIFTH ARGUMENT GOES IN AND IS NEVER READ. The caller pushes five dwords
 * and cleans five; the body reads four. Same shape as ObjBlockWeight's unused
 * third parameter one level down, and reproduced the same way.
 */
int32_t __cdecl BlockWeightDamaging(void *from, uint32_t at, void *chain,
                                    uint32_t ref, int32_t unused)
{
    uint8_t *o     = (uint8_t *)chain;
    int32_t  total = 0;
    uint32_t tile;

    (void)unused;

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        total += ObjBlockWeight(from, o, (int32_t)at, ref);
        if (total >= AM2_BLOCK_FULL)
            return total;

        if (ObjIsWatchedKind(o)
            && (!*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
                || CommMustBroadcast((void *)AM2_IMAGE(ADDR_COMM_OBJECT),
                                     (int16_t)*(const int8_t *)
                                         (o + OBJ_OFF_ARMY))))
            DamageObject(o, AM2_BLOCK_WEAR_AMOUNT, AM2_BLOCK_WEAR_KIND,
                         ((const AM2_Object *)o)->uid, 0, 0);
    }

    tile = (uint32_t)TileOfPoint(at) & 0xFFFFu;
    /* The original's guard, and vacuous after the mask above; see the note. */
    if ((int32_t)tile < 0 || tile > 0xFFFFu)
        return 0xFF;

    if (g_tileFlags[tile] & AM2_TILE_BLOCKS)
        total += AM2_BLOCK_FULL;

    if (from) {
        int32_t here = *(const int8_t *)
            (g_tileAttrs + *(const uint16_t *)((const uint8_t *)from
                                               + OBJ_OFF_TILE));
        int32_t there = *(const int8_t *)(g_tileAttrs + tile);
        int32_t step  = there - here;

        if (step < 0)
            step = -step;
        if (step > AM2_BLOCK_HEIGHT_STEP)
            total += AM2_BLOCK_FULL;
    }

    return total;
}

/* BlockWeightRoute -- original 0x00448FB0, three call sites, all inside
 * ADDR_AI_44AFB0. THE SIXTH MEMBER of the block-weight family and the fifth
 * spelling of one walk; the other five are reconstructed already:
 *
 *   BlockWeightAt        0x00448F00  tile bit and a height step
 *   BlockWeightChain     0x0045B690  pre-collected chain, AM2_TILE_OPEN
 *   BlockWeightTroops    0x0045B7E0  walk INLINED, plus a trooper arm
 *   BlockWeightDamaging  0x0043CF70  and damages what it walks
 *   RoachMaskWeight      0x0043D050  the roach's
 *
 * Written against BlockWeightTroops, which is the same inlined walk, rather
 * than transcribed independently -- a sixth private version of one loop is how
 * a difference stops being visible. What makes this one ITS OWN variant is
 * four things, and every one would be invisible in a from-scratch reading:
 *
 *   - it carries BlockWeightAt's HEIGHT STEP: a hit whose OBJ_OFF_HEIGHT_SET
 *     differs from ours by more than 16 counts nothing. The original takes the
 *     absolute value with `cdq / xor / sub`, so it is symmetric -- something
 *     far below blocks as little as something far above;
 *   - it has NO trooper arm. Where Troops splits type 2 out to ask whether the
 *     hit is an enemy, this asks ObjIsTypeIn238 once and treats 2, 3 and 8
 *     alike;
 *   - the distance test is STRICTLY greater where Troops uses >=. Two
 *     otherwise identical tests, and the boundary case goes the other way;
 *   - it reports CONTAINMENT through an out-parameter, which no other variant
 *     has, and that is what the fourth argument is. The others take `ref`
 *     there by value; this writes through it.
 *
 * THE ROUTE POINT IS NOT THE REFERENCE POINT. `at` is the family's ref -- what
 * ApproxDist measures against and whose tile is charged at the end -- while
 * PointInRect is asked about the current LEG, taken from the planned route at
 * OBJ_OFF_MOVE_FROM indexed by OBJ_OFF_MOVE_AT, or OBJ_OFF_FIELD_C0 when the
 * route is spent. I first read that array as a weapon inventory, having
 * carried +0xC0's meaning across from CreateMissile where it IS a missile def;
 * it is a packed point here, and PointsEqual being handed it BY VALUE is what
 * settled that -- overloading by type, as at 0x52C and 0x538.
 *
 * ON SATURATION the route advances, and only if the leg point was actually
 * inside something: OBJ_OFF_MOVE_AT steps on while it is short of the last
 * waypoint, and at the last it clears OBJ_OFF_FIELD_C0. With no route at all
 * it clears +0xC0 and OBJ_OFF_SCRIPT_STATE together, but only when those two
 * already agree.
 *
 * Two argument slots are recycled as scratch in the original and one has its
 * address taken afterwards; nothing observes the frame, so this uses named
 * locals rather than reproducing that. */
int32_t __cdecl BlockWeightRoute(void *from, uint32_t at, void *chain,
                                 int32_t *inside)
{
    uint8_t *o = (uint8_t *)chain;
    uint8_t *f = (uint8_t *)from;
    int32_t  total = 0;
    uint32_t leg;
    uint32_t tile;

    *inside = 0;

    if (*(const uint16_t *)(f + OBJ_OFF_MOVE_AT)
        < *(const uint16_t *)(f + OBJ_OFF_MOVE_COUNT))
        leg = ((const uint32_t *)(f + OBJ_OFF_MOVE_FROM))
                  [*(const uint16_t *)(f + OBJ_OFF_MOVE_AT)];
    else
        leg = *(const uint32_t *)(f + OBJ_OFF_FIELD_C0);

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        int32_t w = AM2_BLOCK_FULL;
        int32_t ask = 1;
        int32_t dh;

        if (o == f) {
            w = 0;
            ask = 0;
        } else if (f
                   && (dh = *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET)
                            - *(const int8_t *)(f + OBJ_OFF_HEIGHT_SET),
                       (dh < 0 ? -dh : dh) > AM2_BLOCK_HEIGHT_STEP)) {
            w = 0;
            ask = 0;
        } else if (ObjIsItem((const AM2_Object *)o)) {
            w = *(const int8_t *)(o + OBJ_OFF_RANK);
            ask = w >= AM2_BLOCK_FULL;
        } else if (f && ObjIsTypeIn238((const AM2_Object *)o)
                   && ApproxDist((const AM2_Point *)&at,
                                 (const AM2_Point *)(o + OBJ_OFF_POS))
                      > ApproxDist((const AM2_Point *)(f + OBJ_OFF_POS),
                                   (const AM2_Point *)(o + OBJ_OFF_POS))) {
            w = 0;
            ask = 0;
        }

        if (ask)
            *inside = PointInRect((const AM2_Rect *)(o + OBJ_OFF_HIT_RECT),
                                  (const AM2_Point *)&leg);

        total += w;
        if (total >= AM2_BLOCK_FULL) {
            if (*inside) {
                uint16_t cur = *(const uint16_t *)(f + OBJ_OFF_MOVE_AT);
                uint16_t cnt = *(const uint16_t *)(f + OBJ_OFF_MOVE_COUNT);

                if (cur < cnt) {
                    if (cur < (uint16_t)(cnt - 1))
                        *(uint16_t *)(f + OBJ_OFF_MOVE_AT) =
                            (uint16_t)(cur + 1);
                    else
                        *(uint32_t *)(f + OBJ_OFF_FIELD_C0) =
                            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
                } else if (PointsEqual(
                               *(const uint32_t *)(f + OBJ_OFF_FIELD_C0),
                               *(const uint32_t *)(f + OBJ_OFF_SCRIPT_STATE))) {
                    *(uint32_t *)(f + OBJ_OFF_FIELD_C0) =
                            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
                    *(uint32_t *)(f + OBJ_OFF_SCRIPT_STATE) =
                            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
                }
            }
            return total;
        }
    }

    tile = (uint32_t)TileOfPoint(at);
    /* The original's guard, on the low 16 bits and before the mask -- vacuous,
     * exactly as in BlockWeightChain, and kept for the same reason. */
    if ((tile & 0xFFFFu) > 0xFFFFu)
        return 0xFF;
    tile &= 0xFFFFu;

    if (g_tileFlags[tile] & AM2_TILE_BLOCKS)
        total += AM2_BLOCK_FULL;

    /* The second tile term is gated on the MULTIPLAYER session or on this
     * being the player's own army, and it fires only when the destination
     * tile has bit 1 and OUR tile does not -- so crossing into that kind of
     * tile costs, standing in one already does not. */
    if ((*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
         || *(const int8_t *)(f + OBJ_OFF_ARMY) == (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
        && (g_tileFlags[tile] & 2)
        && !(g_tileFlags[*(const uint16_t *)(f + OBJ_OFF_TILE)] & 2))
        total += AM2_BLOCK_FULL;

    if ((uint32_t)TileOfPoint(*(const uint32_t *)(f + OBJ_OFF_FIELD_C0))
        == tile)
        *inside = 1;

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

/* The game's own rand, spelled as event.cpp spells it -- through AM2_IMAGE,
 * because it is CRT code the offline test maps as data. */
typedef int32_t (__cdecl *AM2_RandFn)(void);
#define orig_rand ((AM2_RandFn)AM2_IMAGE(ADDR_GAME_RAND))

/* ShooterReact -- original 0x00457DA0, one caller: ShotStrike, once a shot
 * has damaged something it is not allied with. What it actually does is award
 * the SHOOTER experience, and how much depends entirely on what it hit.
 *
 * ITS ENTRY GUARDS ARE Type238Action's, WORD FOR WORD -- null, the multiplayer
 * broadcast gate on the shooter's own army, and a rank already at
 * AM2_RANK_MAX. Both functions run all three and the callee runs them again a
 * hundred bytes later. The duplication is the original's; nothing here is
 * skipped on the strength of the callee doing it too.
 *
 * FOUR AWARDS, AND THE TABLE IS THE WHOLE FUNCTION:
 *
 *   anything else                            1
 *   a LIVE trooper, vehicle or roach         its rank + 1
 *   the same, KILLED                         (its rank + 1) * 3
 *   a killed SARGE                           100
 *
 * So killing is worth three times wounding, and Sarge is worth more than a
 * rank-7 anything -- 100 against 24. OBJ_OFF_SARGE is what says the last one
 * is Sarge rather than some other trooper flag, and it is only consulted on
 * the dead branch: wounding Sarge pays his rank plus one like any trooper.
 *
 * THE TWO BRANCHES ARE NOT SYMMETRIC IN A SECOND WAY. The killed branch tests
 * `type == 2 && sarge` AFTER computing the triple, so a killed non-Sarge
 * trooper keeps the triple and a killed Sarge overwrites it. The live branch
 * has no type-2 case at all. Written as the two separate arms they are rather
 * than folded, because folding them would need the asymmetry to be an
 * accident and there is no evidence for that.
 *
 * THE TARGET IS DEREFERENCED BEFORE IT IS NULL-CHECKED. `cmp word ptr
 * [eax+0x62], 0` reads the health and only then does `test eax, eax`. Second
 * instance today after ApplyObjFrame's; kept for the same reason, that
 * deleting a test the compiler emitted is a decision about the original.
 *
 * The types it pays for are 2, 3 and 8 -- trooper, vehicle and roach -- which
 * is the same trio ObjIsTypeIn238 answers for and the same one
 * ADDR_TYPE238_ACTION is named after, even though that function admits only
 * type 2 once its own body is read.
 */
void __cdecl ShooterReact(void *shooter, void *target)
{
    uint8_t *s = (uint8_t *)shooter;
    uint8_t *t = (uint8_t *)target;
    int32_t  points = 1;

    if (!s)
        return;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(s + OBJ_OFF_ARMY)))
        return;

    if (*(const int32_t *)(s + OBJ_OFF_RANK) >= AM2_RANK_MAX)
        return;

    if (*(const int16_t *)(t + OBJ_OFF_HEALTH) == 0) {
        /* The original's own null test, after the read above. */
        if (t) {
            int32_t type = *(const int32_t *)t;

            if (type >= 2) {
                if (type <= 3 || type == 8)
                    points = (*(const int32_t *)(t + OBJ_OFF_RANK) + 1)
                             * AM2_KILL_POINT_SCALE;
                if (type == 2 && *(const int32_t *)(t + OBJ_OFF_SARGE))
                    points = AM2_SARGE_KILL_POINTS;
            }
        }
    } else {
        if (t) {
            int32_t type = *(const int32_t *)t;

            if (type >= 2 && (type <= 3 || type == 8))
                points = *(const int32_t *)(t + OBJ_OFF_RANK) + 1;
        }
    }

    Type238Action(s, points);
}

typedef int32_t (__cdecl *AM2_ShotHitsObjFn)(void *target, int32_t code,
                                             int32_t army, int32_t height,
                                             void *shot, int32_t *out);
#define orig_shot_hits_obj ((AM2_ShotHitsObjFn)(uintptr_t)ADDR_SHOT_HITS_OBJ)

/* ShotStrike -- original 0x0043C000, one caller, which is the type-5 stepper.
 * What a shot does when it arrives somewhere: damage everything at that point
 * that it can hit, then ask the terrain whether it stops there. The name is
 * ours; type 5 is the SHOT, which this is the first function in the tree to
 * say plainly.
 *
 * IT TAKES THE ADDRESS OF ITS OWN POINT ARGUMENT and hands it to
 * ObjectsAtPoint -- the point arrives by value and the original takes `&` of
 * the stack slot. It reuses the SHOT argument's slot the same way, as the out
 * parameter for ADDR_SHOT_HITS_OBJ. Both are MSVC reusing what it was given,
 * and both are written out as they are.
 *
 * A NON-EXPLOSIVE SHOT STOPS AT THE FIRST THING IT DAMAGES. After
 * ApplyShotDamage the code branches on the shot's TYPEREC_OFF_CODE, and
 * anything other than 3 RETURNS at once -- out of the walk, with
 * AM2_SHOT_STRUCK_NOTHING. Code 3 carries on down the chain, so it is the one
 * that hits everything at the point rather than one thing. That single `jne`
 * is the difference between a bullet and a blast.
 *
 * Code 3 also keeps a record of what it did: `20 - TYPEREC_OFF_FIELD_08` into
 * OBJ_OFF_FIELD_44, for each object it damaged that is neither a trooper nor
 * an item with TYPEREC_OFF_FIELD_3C set. The same write happens once more in
 * the terrain test below, so the last one wins and the value does not
 * accumulate.
 *
 * THE TERRAIN COMPARE IS SIXTEEN BITS WIDE. The original sign-extends the
 * tile's ADDR_TILE_ATTRS byte into DX and compares it against BP -- the low
 * word of the height argument, not the whole dword. A height above 0xFFFF
 * therefore wraps into the comparison. Reproduced with the casts that say so.
 *
 * The three answers: below the terrain is AM2_SHOT_STRUCK_GROUND, and so is
 * anything of code 3 that got this far; at or above it, a tile that is both
 * flagged in ADDR_TILE_FLAGS and of ADDR_CELL_WEIGHTS 15 or more is
 * AM2_SHOT_STRUCK_HARD; everything else is AM2_SHOT_STRUCK_NOTHING.
 */
int32_t __cdecl ShotStrike(void *shot, uint32_t at, int32_t height)
{
    uint8_t       *s = (uint8_t *)shot;
    const uint8_t *rec = *(const uint8_t *const *)(s + OBJ_OFF_FIELD_94);
    int32_t        code = *(const int32_t *)(rec + TYPEREC_OFF_CODE);
    uint8_t       *o;
    uint16_t       tile;
    int32_t        scratch;

    o = (uint8_t *)ObjectsAtPoint(&at, (void *)(uintptr_t)ADDR_OBJ_MAP_DESC);

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        /* OBJ_OFF_RANK on a SHOT is the shooter's uid -- see orig.h. */
        if (((const AM2_Object *)o)->uid
            == *(const uint32_t *)(s + OBJ_OFF_RANK))
            continue;

        if (!orig_shot_hits_obj(o, code,
                                *(const int8_t *)(s + OBJ_OFF_ARMY),
                                height, shot, &scratch))
            continue;

        ApplyShotDamage(o, shot, (int32_t)at, height, scratch);

        if (code != 3)
            return AM2_SHOT_STRUCK_NOTHING;

        if (ObjIsType2((const AM2_Object *)o))
            continue;
        if (ObjIsItem((const AM2_Object *)o)
            && *(const uint8_t *)(*(const uint8_t *const *)
                                      (o + OBJ_OFF_FIELD_94)
                                  + TYPEREC_OFF_FIELD_3C) > 0)
            continue;

        *(int32_t *)(s + OBJ_OFF_FIELD_44) =
            20 - *(const int32_t *)(rec + TYPEREC_OFF_FIELD_08);
    }

    tile = *(const uint16_t *)(s + OBJ_OFF_TILE);

    if ((int16_t)*(const int8_t *)((*(const uint8_t *const *)
                                        (uintptr_t)ADDR_TILE_ATTRS) + tile)
        < (int16_t)height)
        return AM2_SHOT_STRUCK_GROUND;

    if (code == 3) {
        *(int32_t *)(s + OBJ_OFF_FIELD_44) =
            20 - *(const int32_t *)(rec + TYPEREC_OFF_FIELD_08);
        return AM2_SHOT_STRUCK_GROUND;
    }

    if (!((*(const uint8_t *const *)(uintptr_t)ADDR_TILE_FLAGS)[tile] & 1))
        return AM2_SHOT_STRUCK_NOTHING;
    if ((*(const int8_t *const *)(uintptr_t)ADDR_CELL_WEIGHTS)[tile] < 0x0F)
        return AM2_SHOT_STRUCK_NOTHING;

    return AM2_SHOT_STRUCK_HARD;
}

/* ApplyShotDamage -- original 0x0043BBE0, one caller, 240 bytes. What a shot
 * does to what it hit, and what the shooter does next.
 *
 * The amount starts as the shot's own TYPEREC_OFF_DAMAGE and a THIRTY-ARM
 * JUMP TABLE over TYPEREC_OFF_CODE decides how to scale it. Thirty arms and
 * four bodies -- the table at 0x0043BCB0 is a byte index into four targets at
 * 0x0043BCA0 -- so most codes take the default and change nothing:
 *
 *   - code 3 REPLACES the amount with `rand() % (damage + 1) + 1` and is the
 *     only code whose damage KIND is 1 rather than 2. Two things at once,
 *     which is why it is worth naming rather than leaving as an index;
 *   - codes 1, 7, 8, 9, 10 and 29 DOUBLE when the caller's fifth argument is
 *     set;
 *   - code 30 doubles on that and then doubles AGAIN if the target is a type
 *     2, so it is four times against a trooper and once against anything
 *     else;
 *   - every other code takes the record's amount unchanged.
 *
 * TWO OF ITS FIVE ARGUMENTS ARE NEVER READ. The caller pushes five and cleans
 * five; the body touches the first, the second and the fifth. Third instance
 * of this shape in as many batches -- ObjBlockWeight has one unused
 * parameter, BlockWeightDamaging has one, and this has two.
 *
 * The direction handed to DamageObject is the shot's own OBJ_OFF_FACING plus
 * AM2_SHOT_DIR_BIAS -- a byte facing turned around, since 0x80 is half a
 * turn: the hit comes FROM where the shot was going.
 *
 * AND THE SHOOTER TURNS ON THE TARGET, but only when the two are not allied:
 * the shot's owner uid is resolved, and if that resolves to a type 2, 3 or 8
 * its OBJ_OFF_TARGET_UID takes the target's uid and ShooterReact runs. So
 * shooting something is also how a unit acquires it.
 *
 * OBJ_OFF_RANK IS A UID HERE, which is its THIRD reading. orig.h already
 * records two -- a trooper's rank, and an item's flag byte read for its sign
 * -- and says one name with several readings beats a second name on the
 * offset. On a shot it is the uid of whoever fired it: DamageObject takes it
 * as the attacker and LookupByUID resolves it four instructions later.
 */
void __cdecl ApplyShotDamage(void *target, void *shot, int32_t unusedA,
                             int32_t unusedB, int32_t doubled)
{
    const uint8_t *sh  = (const uint8_t *)shot;
    const uint8_t *rec = *(const uint8_t *const *)(sh + OBJ_OFF_FIELD_94);
    int32_t amount = *(const int32_t *)(rec + TYPEREC_OFF_DAMAGE);
    int32_t code   = *(const int32_t *)(rec + TYPEREC_OFF_CODE);
    int32_t kind   = AM2_SHOT_DAMAGE_KIND;
    uint8_t *owner;

    (void)unusedA;
    (void)unusedB;

    switch (code) {
    case AM2_SHOT_CODE_RANDOM:
        amount = orig_rand() % (amount + 1) + 1;
        kind   = AM2_SHOT_DAMAGE_KIND_RAND;
        break;
    case 1: case 7: case 8: case 9: case 10: case 29:
        if (doubled)
            amount += amount;
        break;
    case AM2_SHOT_CODE_ANTI_TROOP:
        if (doubled)
            amount += amount;
        if (ObjIsType2((const AM2_Object *)target))
            amount += amount;
        break;
    default:
        break;
    }

    DamageObject(target, amount, kind,
                 *(const uint32_t *)(sh + OBJ_OFF_RANK),
                 (int32_t)(int8_t)(*(const uint8_t *)(sh + OBJ_OFF_FACING)
                                   + AM2_SHOT_DIR_BIAS),
                 0);

    if (ObjsAreAllied((void *)sh, target, 0))
        return;

    owner = (uint8_t *)LookupByUID(*(const uint32_t *)(sh + OBJ_OFF_RANK));
    if (!ObjIsTypeIn238((const AM2_Object *)owner))
        return;

    *(uint32_t *)(owner + OBJ_OFF_TARGET_UID) = ((const AM2_Object *)target)->uid;
    ShooterReact(owner, target);
}

/* ObjCollidesWith -- original 0x0045B700, 224 bytes, two callers, both inside
 * 0x0045BC70. Does `from` run into `obj`?
 *
 * A stack of arms, and each one answers on its own; nothing falls through to
 * a default. In order:
 *
 *   - HEIGHT FIRST. If the two OBJ_OFF_HEIGHT_SET bytes differ by more than
 *     AM2_BLOCK_HEIGHT_STEP the answer is no, whatever else is true -- one is
 *     above or below the other and they never meet. The same 16 the whole
 *     block-weight family uses.
 *
 *   - OBJ_FLAG_BIT24 with a vehicle of KIND 1 OR 2 collides unconditionally,
 *     before any question of type or side. What the flag means is not
 *     established; see orig.h.
 *
 *   - FOR A TYPE 2, 3 OR 8: a VEHICLE of your own army does not (you drive
 *     past your own); anything you are not allied with does; and an ALLY does
 *     only under one condition -- the player's own unit is riding `from` and
 *     OBJ_OFF_FIELD_10C is clear.
 *
 *     That last arm is a real game rule and worth stating plainly: a vehicle
 *     the player is driving stops for friendly troops and one the AI is
 *     driving does not. BlockWeightTroops has the same three fields in the
 *     same relation and uses them the OTHER WAY ROUND -- there they are a
 *     reason to SKIP a trooper's weight. Two functions, one condition, two
 *     polarities; both are transcribed rather than reconciled.
 *
 *   - ANYTHING ELSE only collides if ObjIsWatchedKind accepts it, and then
 *     one of YOUR OWN army only after AM2_COLLIDE_OWN_DELAY has passed since
 *     its OBJ_OFF_DEADLINE_58. The caller stamps that field with the clock
 *     plus 100 on whatever it just hit, so the five seconds and the hundred
 *     milliseconds are two cooldowns on one field.
 *
 * MEASURED, see the note on the batch: its two callers are the original's, so
 * the counter is not blind.
 */
int32_t __cdecl ObjCollidesWith(void *from, void *obj)
{
    uint8_t *f = (uint8_t *)from;
    uint8_t *o = (uint8_t *)obj;
    int32_t  step;

    step = (int32_t)*(const int8_t *)(f + OBJ_OFF_HEIGHT_SET)
         - (int32_t)*(const int8_t *)(o + OBJ_OFF_HEIGHT_SET);
    if (step < 0)
        step = -step;
    if (step > AM2_BLOCK_HEIGHT_STEP)
        return 0;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_BIT24) {
        int32_t kind = *(const int32_t *)(f + VEHICLE_OFF_KIND);

        if (kind == 1 || kind == 2)
            return 1;
    }

    if (ObjIsTypeIn238((const AM2_Object *)o)) {
        if (ObjIsType3((const AM2_Object *)o)
            && *(const int8_t *)(f + OBJ_OFF_ARMY)
                   == *(const int8_t *)(o + OBJ_OFF_ARMY))
            return 0;

        if (!ObjsAreAllied(f, o, 0))
            return 1;

        if (*(const uint32_t *)((const uint8_t *)LookupOwnerObj(
                    *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
                + OBJ_OFF_RIDING) != ((const AM2_Object *)f)->uid)
            return 0;
        if (*(const int32_t *)(f + OBJ_OFF_FIELD_10C))
            return 0;
        return 1;
    }

    if (!ObjIsWatchedKind(o))
        return 0;

    if (*(const int8_t *)(f + OBJ_OFF_ARMY) == *(const int8_t *)(o + OBJ_OFF_ARMY)
        && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
               - *(const uint32_t *)(o + OBJ_OFF_DEADLINE_58)
           <= AM2_COLLIDE_OWN_DELAY)
        return 0;

    return 1;
}

/* 0x0045B7E0, three callers, 336 bytes -- the THIRD variant, and the one the
 * other two are simplifications of.
 *
 * Same walk as BlockWeightChain above, over a chain the caller supplies, with
 * the same stop at AM2_BLOCK_FULL and the same AM2_TILE_BLOCKS terrain term
 * that BlockWeightAt uses. What differs is the per-object test: instead of
 * calling ObjBlockWeight it INLINES it, drops the height step entirely, and
 * inserts one arm for a type 2 -- a trooper -- between the item case and the
 * type 3/8 case. That arm is the whole reason this function exists.
 *
 * A TROOPER BLOCKS ONLY IF IT IS AN ENEMY. The inlined arm asks
 * ObjsAreAllied(from, obj, 0) and contributes AM2_BLOCK_FULL when the answer
 * is no and nothing when it is yes -- so friendly troopers are walked through
 * and hostile ones are not. Neither of the other two variants has this; they
 * treat every object the same way whoever owns it.
 *
 * WITH ONE EXCEPTION, AND IT IS THE PLAYER'S OWN UNIT. Before the alliance
 * question, three things are checked together: the viewer's army is the
 * player's ADDR_DEFAULT_OWNER, the owner object for that army is RIDING the
 * viewer -- LookupOwnerObj(owner)->OBJ_OFF_RIDING equals the viewer's uid --
 * and the viewer's OBJ_OFF_FIELD_10C is zero. All three together mean the
 * trooper contributes nothing whatever the alliance says. Any one of them
 * failing falls through to the alliance question. So the unit the player is
 * driving is the only one for which a trooper is never an obstacle.
 *
 * That LookupOwnerObj result is dereferenced without a null check, and it is a
 * function documented to return null when the army is out of range or nothing
 * qualifies. Reproduced; the army has already been compared equal to
 * ADDR_DEFAULT_OWNER, which is what makes it safe in practice.
 *
 * The type 3/8 distance test is ObjBlockWeight's, unchanged: the reference
 * point being closer to the obstacle than the viewer is means it blocks. The
 * original passes that point by copying its own third argument's stack slot
 * over its second and taking the address; written here as a local.
 *
 * ITS TERRAIN BIT IS AM2_TILE_BLOCKS, NOT AM2_TILE_OPEN. So of the three
 * variants, two ask 0x80-is-set and one asks 0x01-is-clear, and this is not
 * the odd one out -- BlockWeightChain is. Worth stating because the three
 * bodies are otherwise close enough to copy from each other.
 *
 * MEASURED AT 2,039,745 CALLS on a driven Boot Camp mission, which settles
 * which of the three the game actually uses: BlockWeightAt reads 8 and
 * BlockWeightChain 0 on the same run. The two cold ones were reconstructed
 * first and read as the general case; this is the general case.
 *
 * What that does NOT tell us is how often the trooper arm fires, and the
 * reason is the usual one: ObjsAreAllied is called from here BY NAME, so its
 * counter cannot see these calls. It reads 2 on the same run and those two are
 * its other callers. The arm is compared by the A/B and counted by nothing.
 */
int32_t __cdecl BlockWeightTroops(void *from, uint32_t at, void *chain,
                                  uint32_t ref)
{
    uint8_t *o     = (uint8_t *)chain;
    uint8_t *f     = (uint8_t *)from;
    int32_t  total = 0;
    uint32_t tile;

    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        int32_t w = AM2_BLOCK_FULL;

        if (o == f) {
            w = 0;
        } else if (ObjIsItem((const AM2_Object *)o)) {
            w = *(const int8_t *)(o + OBJ_OFF_RANK);
        } else if (ObjIsType2((const AM2_Object *)o)) {
            int32_t skip = 0;

            if (!f) {
                w = 0;
            } else {
                if (*(const int8_t *)(f + OBJ_OFF_ARMY)
                    == (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER) {
                    const uint8_t *owner = (const uint8_t *)LookupOwnerObj(
                        *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);

                    if (*(const uint32_t *)(owner + OBJ_OFF_RIDING)
                            == ((const AM2_Object *)f)->uid
                        && *(const int32_t *)(f + OBJ_OFF_FIELD_10C) == 0)
                        skip = 1;
                }
                if (skip)
                    w = 0;
                else
                    w = ObjsAreAllied(f, o, 0) ? 0 : AM2_BLOCK_FULL;
            }
        } else if (f
                   && (ObjIsType3((const AM2_Object *)o)
                       || ObjIsType8((const AM2_Object *)o))) {
            if (ApproxDist((const AM2_Point *)&ref,
                           (const AM2_Point *)(o + OBJ_OFF_POS))
                >= ApproxDist((const AM2_Point *)(f + OBJ_OFF_POS),
                              (const AM2_Point *)(o + OBJ_OFF_POS)))
                w = 0;
        }

        total += w;
        if (total >= AM2_BLOCK_FULL)
            return total;
    }

    tile = (uint32_t)TileOfPoint(at);
    /* The same vacuous guard BlockWeightChain carries, spelled the same way. */
    if ((tile & 0xFFFFu) > 0xFFFFu)
        return 0xFF;
    tile &= 0xFFFFu;

    if (g_tileFlags[tile] & AM2_TILE_BLOCKS)
        total += AM2_BLOCK_FULL;

    return total;
}

/* 0x0045BBB0, five callers. Sum the block weight over a vehicle mask's points.
 *
 * The heading is rounded to one of AM2_VEHICLE_MASK_DIRS with RoundTo8, the
 * kind and that direction index one 0xA4-byte record, and every point in it is
 * offset from the caller's base and weighed. The record's count sits in the
 * dword BELOW ADDR_VEHICLE_MASK, which is how that table was already
 * documented from the builder's side.
 *
 * KIND 5 TAKES BlockWeightChain AND EVERY OTHER KIND TAKES BlockWeightTroops.
 * That is the "value being 5" BlockWeightChain's own comment records without
 * knowing what it was: it is a vehicle kind. So the variant with no trooper
 * arm and the inverted tile bit exists for one kind of vehicle, and the
 * question of which is the odd one out has an answer -- it is whatever kind 5
 * is, and that is still unread.
 *
 * THE CHAIN IS COLLECTED AT THE BASE POINT, NOT AT THE OFFSET ONE, and this is
 * the part to read twice. ObjectsAtPoint is handed the address of the base
 * argument, unchanged, on every iteration -- while the SUMMED point goes into
 * a different slot and is passed as the weigher's `at`. So the objects are the
 * same set every time round and only the terrain term moves with the mask.
 * The query is also repeated rather than hoisted, returning the same chain.
 *
 * Whether that is deliberate is not established and it is not called a bug
 * here. What is certain is the two addresses: `lea edx,[esp+0x1c]` names the
 * base slot and the summed point is written four bytes below it, which is the
 * argument slot the direction byte was parked in earlier. The original reuses
 * both incoming argument slots as scratch, which is why the two are adjacent
 * and easy to conflate.
 *
 * The count is re-read from the record every iteration. It cannot change.
 *
 * MEASURED AT 0. All five call sites sit in one function, 0x0043A860, and
 * that function does not run on any drive here -- BlockWeightTroops reads
 * 1,045,353 on the same run, all of it from its other two callers. So the
 * counter is not blind and this is verified by reading: the mask indexing,
 * the kind-5 split, and the base-versus-offset point above are all
 * unchecked by any test in this project.
 *
 * That last one is the reason to say so loudly. If the chain really should
 * come from the offset point, this reconstruction reproduces a defect
 * exactly, which is the correct outcome -- and if I have misread the two
 * adjacent stack slots, nothing here would tell me.
 */
int32_t __cdecl MaskBlockWeight(int32_t kind, int32_t heading, uint32_t at)
{
    const uint8_t *rec;
    int32_t        dir   = (uint8_t)RoundTo8(heading & 0xFF, 5);
    int32_t        total = 0;
    int32_t        i;

    rec = (const uint8_t *)AM2_IMAGE(ADDR_VEHICLE_MASK - 4)
          + (size_t)(kind * AM2_VEHICLE_MASK_DIRS + dir)
            * AM2_VEHICLE_MASK_STRIDE;

    for (i = 0; i < *(const int32_t *)rec; i++) {
        const int16_t *pt = (const int16_t *)(rec + 4) + i * 2;
        uint32_t       here;
        void          *chain;

        /* Both halves of the packed base, summed with the mask offset. */
        here = (uint32_t)(uint16_t)(int16_t)((int16_t)(at & 0xFFFFu) + pt[0])
             | ((uint32_t)(uint16_t)(int16_t)((int16_t)(at >> 16) + pt[1])
                << 16);

        /* NOTE: the BASE point, not `here`. */
        chain = (uint8_t *)ObjectsAtPoint(&at,
                                      (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC));

        total += (kind == AM2_MASK_CHAIN_KIND)
                 ? BlockWeightChain((void *)0, here, chain, at)
                 : BlockWeightTroops((void *)0, here, chain, at);
    }

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
    uint8_t *o   = (uint8_t *)ObjectsAtPoint(
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

    ObjAttachTo(obj, 0);
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

    ObjClearFootprint(obj);

    *(uint16_t *)(o + OBJ_OFF_FIELD_C0)     = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_C0 + 2) = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID)     = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID + 2) = 0;

    ObjAttachTo(obj, 0);
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

    ObjClearRoachFootprint(obj);

    *(uint16_t *)(o + OBJ_OFF_FIELD_C0)     = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_C0 + 2) = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID)     = 0;
    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID + 2) = 0;

    ObjAttachTo(obj, 0);
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
        ItemTeardown(obj);

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

#define g_mpSession     (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)

/* PlaceObj -- original 0x00429220, one caller.
 *
 * Put an object at a point: move every row it owns, take it off the map and
 * put it back, and re-apply its height. The deploy dispatcher's default arm,
 * so this is what everything that is not a trooper or a vehicle gets.
 *
 * THE EARLY EXIT NEEDS ALL THREE. Same position, same OBJ_OFF_FLAGS bit 2
 * SET -- and it is the flag being set that means "already placed", because
 * the body clears it on the way out. So a second call with the same point
 * does nothing, and a call with a new point does the work even for an object
 * that was already down.
 *
 * AND THE TEARDOWN RUNS FIRST, but only for an ITEM that is not already
 * placed. Moving a type 1 or 4 unregisters it before re-registering, where a
 * type 2 or 3 is simply moved -- which is what makes RemoveFromItemList's
 * caller reachable from a place rather than only from a destroy.
 *
 * ROW 0 IS MOVED TO THE POINT AND THE REST TO THE POINT PLUS THEIR SPRITE'S
 * ATTACH OFFSET, which is the other half of what orig.h records under
 * AM2_Sprite::attachX: "where an attached row sits relative to the one
 * carrying this sprite -- a turret on its body". PointActionC was the one
 * reader that named those fields; this is the second, and it agrees.
 *
 * Row 0 is relinked with force 1 and the rest with force 0. The first has
 * just had its position rewritten and the others may not have moved at all,
 * so the asymmetry is not arbitrary -- but it is the original's either way.
 *
 * HOW OFTEN IT RUNS IS ONCE PER MISSION, and that is worth stating rather
 * than leaving to the clean A/B. Its one caller is the deploy dispatcher
 * below, whose own comment records that a Boot Camp mission reaches it
 * exactly once, through EvtDeployItem. So four clean configurations compare
 * this function on a handful of calls, not on the map load -- objects placed
 * during loading go through the type 2 and 3 arms or through the loader
 * directly, not through here.
 */
typedef void (__cdecl *AM2_ObjOnlyFn3)(void *obj);
typedef void (__cdecl *AM2_AfterMoveFn)(void *obj, int32_t a, int32_t b);
/* ObjAfterMove is reconstructed, in region.cpp, and called by name. */

void __cdecl PlaceObj(void *obj, uint32_t where)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *row;
    int32_t  i;

    if (!obj)
        return;

    if (*(const int16_t *)(o + OBJ_OFF_POS) == (int16_t)where
        && *(const int16_t *)(o + OBJ_OFF_POS + 2) == (int16_t)(where >> 16)
        && (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED))
        return;

    if (!(*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        && ObjIsItem((const AM2_Object *)obj))
        ItemTeardown(obj);

    *(int16_t *)(o + OBJ_OFF_POS)     = (int16_t)where;
    *(int16_t *)(o + OBJ_OFF_POS + 2) = (int16_t)(where >> 16);

    row = *(uint8_t **)(o + OBJ_OFF_ROWS);
    *(int32_t *)(row + ROW_OFF_X) = *(const int32_t *)(o + OBJ_OFF_POS);
    ObjFlagSet0(row);
    RowUpdate(row, 1, (void *)(uintptr_t)ADDR_MAP_DESC);

    for (i = 1; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        const uint8_t *spr;

        row = *(uint8_t **)(o + OBJ_OFF_ROWS) + i * AM2_OBJ_ROW_STRIDE;
        *(int32_t *)(row + ROW_OFF_X) = *(const int32_t *)(o + OBJ_OFF_POS);

        spr = *(const uint8_t *const *)(row + ROW_OFF_SPRITE);
        *(int16_t *)(row + ROW_OFF_X) +=
            *(const int16_t *)(spr + SPRITE_OFF_ATTACH_X);
        spr = *(const uint8_t *const *)(row + ROW_OFF_SPRITE);
        *(int16_t *)(row + ROW_OFF_Y) +=
            *(const int16_t *)(spr + SPRITE_OFF_ATTACH_Y);

        ObjFlagSet0(row);
        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_DESTROYED;

    ObjTileChanged(obj, *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET), 1);

    if (ObjIsItem((const AM2_Object *)obj))
        ObjAfterMove(obj, 1, 0);
}


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
        DeployTrooper(obj, (int16_t)where, (int16_t)(where >> 16),
                            resurrect);
        break;
    case 3:
        DeployVehicle(obj, (int16_t)where, (int16_t)(where >> 16),
                            resurrect);
        break;
    default:
        PlaceObj(obj, where);
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


/* PlayDynamicSound is reconstructed, in win32/audio.cpp. Declared here for the
 * reason this file already declares PlaySoundAt above. */
extern "C" void __cdecl PlayDynamicSound(const char *name, int32_t loop,
                                         int32_t unused, int32_t x, int32_t y,
                                         int32_t slot, int32_t priority,
                                         uint32_t owner);

/* CreateTrooper and CreateWeapon, both still original -- the type-2 and type-4
 * arms of the item-create message. */
typedef void *(__cdecl *AM2_CreateTrooperFn)(const char *name, int32_t x,
                                             int32_t y, int32_t a, int32_t b,
                                             int32_t c, int32_t d, int32_t e,
                                             int32_t f, int32_t g);
#define orig_create_trooper \
    ((AM2_CreateTrooperFn)(uintptr_t)ADDR_CREATE_TROOPER)

/* PortalSpawn -- original 0x00417930, one caller, and that caller is the cheat
 * dispatcher two functions along from the "Flame On!" arms -- so this is a
 * cheat's effect too. Twenty-five armed enemies appear at random points inside
 * the visible view, each with an explosion where it lands.
 *
 * THE FOUR WEAPON CODES ARE A STACK ARRAY, not a table in the image: the
 * original writes 0x1E, 0x0A, 4 and 2 into four locals before the loop and
 * indexes them with `rand() & 3`. The sign fixup around that AND is MSVC's
 * signed-remainder idiom, and it cannot fire -- ADDR_GAME_RAND never answers
 * negative -- but it is reproduced, because a reconstruction that dropped it
 * would be assuming something about the LCG rather than about this function.
 *
 * THE ARGUMENT SHUFFLE IS THE SAME ONE WeaponRespawn HAS. Six dwords go on the
 * stack, KeyLookupTriple consumes the top three -- two pushed for it and one
 * already there -- `add esp, 0xc` cleans exactly those, and three fresh pushes
 * complete CreateWeapon's eight. Written here as the two calls it is.
 *
 * The trooper is created with the one-character name "a" and army 1, and the
 * weapon it gets is stamped with army 1 as well; the pair are then tied
 * together by uid. Each one ends up facing ADDR_LISTENER_POS -- the player's
 * ear, not the player's leader -- which is what makes them all turn inward.
 *
 * Verified by reading: it needs a cheat typed at the keyboard and no
 * configuration in tools/ab.sh types one.
 */
void __cdecl PortalSpawn(void)
{
    static const int32_t kWeapons[4] = { 0x1E, 0x0A, 4, 2 };
    int32_t i;

    PlayDynamicSound((const char *)(uintptr_t)ADDR_STR_PORTAL_WAV,
                     0, 0, 0, 0, 0x10, 3, 0);

    for (i = 0; i < AM2_PORTAL_COUNT; i++) {
        int32_t   x = (int32_t)(orig_game_rand() % AM2_PORTAL_SPAN_X)
                      + *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X;
        int32_t   y = (int32_t)(orig_game_rand() % AM2_PORTAL_SPAN_Y)
                      + *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y;
        uint8_t  *trooper;
        uint8_t  *weapon;
        int32_t   pick;

        orig_spawn_at(x, y, AM2_PORTAL_EFFECT, 0, 0, 0, 0, 0, 0, 0);

        trooper = (uint8_t *)orig_create_trooper(
                      (const char *)(uintptr_t)ADDR_STR_ONE_LETTER,
                      x, y, 1, 1, 0, 0, 0, 1, 0);
        if (!trooper)
            continue;

        pick = orig_game_rand() & 3;

        weapon = (uint8_t *)orig_create_weapon(
                     (char *)AM2_IMAGE(ADDR_DIR_SCRATCH), 1,
                     KeyLookupTriple(AM2_WEAPON_RESPAWN_KEY,
                                     (uint32_t)kWeapons[pick], 0),
                     *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT,
                     4, -1, 0, 0);
        if (!weapon)
            continue;

        *(uint32_t *)(trooper + UNIT_OFF_INVENTORY) =
            *(const uint32_t *)(weapon + 4);
        *(uint8_t *)(weapon + OBJ_OFF_ARMY) = 1;

        SoldierKindForWeapon(
            trooper, **(const int32_t *const *)(weapon + OBJ_OFF_FIELD_C0));
        SendTrooperSetWeapon(trooper, *(const uint32_t *)(weapon + 4), 0);

        *(uint8_t *)(trooper + OBJ_OFF_FACING) =
            AngleBetween((const AM2_Point *)(trooper + OBJ_OFF_POS),
                         (const AM2_Point *)(uintptr_t)ADDR_LISTENER_POS);

        Type238Action(trooper, AM2_PORTAL_ACTION);
    }
}

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
    SeqAddKind7(&pt, (int32_t)((const AM2_Object *)leader)->uid, 0, 0,
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
                    *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)obj)->uid,
                    ObjEventMask((const AM2_Object *)obj),
                    *(const int32_t *)(a + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)src)->uid,
                    ObjEventMask((const AM2_Object *)src),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_HEALED,
                *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
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

/* DamageItem is reconstructed below and called by name.
 *
 * SpawnAt is still original and reached by address -- the same ten-argument
 * creator air.cpp and gameproc.cpp already spell this way. */
typedef void *(__cdecl *AM2_ItemSpawnFn)(int32_t x, int32_t y, int32_t kind,
                                         int32_t army, uint32_t uid,
                                         int32_t extra, int32_t e, int32_t f,
                                         int32_t g, int32_t h);
#define SpawnAt ((AM2_ItemSpawnFn)(uintptr_t)ADDR_SPAWN_AT)
#define orig_damage_trooper   ((AM2_DamageTypeFn)(uintptr_t)ADDR_DAMAGE_TROOPER)

typedef void (__cdecl *AM2_HitEffectFn)(const void *at, int32_t slot,
                                        int32_t dir, int32_t height);

#define orig_spawn_hit_effect \
    ((AM2_HitEffectFn)(uintptr_t)ADDR_SPAWN_HIT_EFFECT)

/* DamageVehicle -- original 0x0045B4D0, one caller, which is DamageObject's
 * type-3 arm. Take `amount` off a vehicle, and if that empties it, empty the
 * vehicle too.
 *
 * ARMOUR IS A THRESHOLD AND NOT A SUBTRACTION, mostly. A hit at or under
 * VEHICLE_OFF_ARMOUR normally does nothing at all -- the function returns
 * before touching the health -- but it first rolls `rand() & 0xFF` against
 * `amount * 8` and, if that comes up, raises the amount to `armour + 1` so
 * that exactly one point gets through. So a rifle round has a chance
 * proportional to its damage of scratching a tank, and eight times the damage
 * is certainty. Only after that is the armour subtracted from what remains.
 *
 * THE HIT MARK IS ROLLED SEPARATELY and only above one point of damage:
 * `rand() % 255 <= AM2_HIT_EFFECT_CHANCE`, roughly a quarter of the time. It
 * is given the position SAVED BEFORE any of this, which matters because
 * ExitAllFromVehicle below moves things.
 *
 * TWO GATES AND THEY ARE NOT SYMMETRIC. In a multiplayer session
 * CommMustBroadcast must accept this army or nothing happens -- a client does
 * not damage on its own account. Outside one, ADDR_CHEAT_INVULNERABLE makes
 * our OWN army immune, which is the "I am the Juggernaut!" cheat, and the
 * multiplayer path skips that test entirely. So the cheat is single-player
 * only, and by construction rather than by a check for it.
 *
 * DEATH IS EXACT EQUALITY WITH ZERO. `health -= amount` and then `!= 0`
 * returns -- and the amount was already clamped to the health above, so it
 * cannot go negative. The clamp is what makes the equality safe, and removing
 * either would leave a vehicle that survives at negative health.
 *
 * The fourth argument -- `kind` in the family's shared signature -- is never
 * read here. DamageObject passes the same five to all three arms and this one
 * wants four of them.
 */
void __cdecl DamageVehicle(void *obj, int32_t amount, int32_t d, int32_t kind,
                           uint32_t attacker)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  armour;
    uint32_t at;

    (void)kind;

    *(o + OBJ_OFF_HIT_DIR) = (uint8_t)((d & 0xFF) < 1 ? 1 : (d & 0xFF));
    *(uint32_t *)(o + OBJ_OFF_HIT_TIME) =
        *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    armour = *(const int32_t *)(o + VEHICLE_OFF_ARMOUR);
    if (amount <= armour
        && (int32_t)(orig_game_rand() & 0xFF) < amount * 8)
        amount = armour + 1;

    if (*(const int32_t *)(o + VEHICLE_OFF_ARMOUR) > amount)
        return;

    amount -= *(const int32_t *)(o + VEHICLE_OFF_ARMOUR);
    if (amount < 0)
        amount = 0;
    if (amount > *(const int16_t *)(o + OBJ_OFF_HEALTH))
        amount = *(const int16_t *)(o + OBJ_OFF_HEALTH);

    at = *(const uint32_t *)(o + OBJ_OFF_POS);

    if (amount > 1 && orig_game_rand() % 255 <= AM2_HIT_EFFECT_CHANCE)
        orig_spawn_hit_effect(&at,
                              *(const int32_t *)(o + OBJ_OFF_TABLE_REC_SLOT),
                              *(const uint8_t *)(o + OBJ_OFF_HIT_DIR),
                              *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET));

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION) {
        if (!CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                               *(const int8_t *)(o + OBJ_OFF_ARMY)))
            return;
    } else if (*(const int32_t *)(uintptr_t)ADDR_CHEAT_INVULNERABLE
               && *(const int8_t *)(o + OBJ_OFF_ARMY)
                  == *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER) {
        return;
    }

    *(int16_t *)(o + OBJ_OFF_HEALTH) =
        (int16_t)(*(const int16_t *)(o + OBJ_OFF_HEALTH) - (int16_t)amount);
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0)
        return;

    ExitAllFromVehicle(obj, attacker);

    *(int32_t *)(o + VEHICLE_OFF_DEATH_STATE) = 5;
    *(int32_t *)(o + VEHICLE_OFF_DEAD)        = 1;

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 1)
        ObjFlagClear0(*(uint8_t **)(o + OBJ_OFF_ROWS) + AM2_OBJ_ROW_STRIDE);

    ObjClearFootprint(obj);

    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & 1) {
        ItemPreDestroyAlias(obj, (int32_t)(uintptr_t)ADDR_OBJ_MAP_DESC);
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~1u;
    }
}


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
                    *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)obj)->uid,
                    ObjEventMask((const AM2_Object *)obj),
                    *(const int32_t *)(a + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)attacker)->uid,
                    ObjEventMask((const AM2_Object *)attacker),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_DAMAGED,
                *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
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
 * THAT DEADLINE HAS A READER NOW. It went in unexplained -- a field the
 * pickup path stamps and nothing was known to consult. CanPickUp below is
 * the consultant: it refuses an item until OBJ_OFF_PICKUP_AFTER has passed.
 * So the two seconds are a re-pickup cooldown, which neither function says
 * on its own.
 *
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
                    *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)item)->uid,
                    ObjEventMask((const AM2_Object *)item),
                    *(const int32_t *)(t + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)taker)->uid,
                    ObjEventMask((const AM2_Object *)taker),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_PICKED_UP,
                *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
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
                    *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)item)->uid,
                    ObjEventMask((const AM2_Object *)item),
                    *(const int32_t *)(d + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)dropper)->uid,
                    ObjEventMask((const AM2_Object *)dropper),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_DROPPED,
                *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
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
                    *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)obj)->uid,
                    ObjEventMask((const AM2_Object *)obj),
                    *(const int32_t *)(a + AM2_OBJ_NAME_IDX_OFF),
                    ((const AM2_Object *)attacker)->uid,
                    ObjEventMask((const AM2_Object *)attacker),
                    0, 0, 0);
        return;
    }

    EventNotify(AM2_EVENT_KILLED,
                *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF),
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
        DamageItem(obj, amount, extra, kind, attackerUid, 0);
        break;
    case 2:
        orig_damage_trooper(obj, amount, extra, kind, attackerUid);
        break;
    case 3:
        DamageVehicle(obj, amount, extra, kind, attackerUid);
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

/* Both defined below, beside the rest of the object family. The image seam
   that used to stand here, on the address this file now patches, is gone
   with the reconstruction. */
void __cdecl SetSoldierKind(void *obj, int32_t kind);
void __cdecl SoldierKindForWeapon(void *unit, uint32_t code);

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
    *(int32_t *)(o + OBJ_OFF_AI_MODE) = 2;

    SetSoldierKind(o, 8);

    weapon = WeaponByUid(*(const uint32_t *)(o + TROOPER_OFF_WEAPON_UID));
    if (weapon)
        *(uint32_t *)((uint8_t *)weapon + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;

    *(int32_t *)(o + TROOPER_OFF_WEAPON_UID) = 0;
    SoldierKindForWeapon(o, 0);
}

/* 0x00446E70. Record a fire request on the unit the weapon globals name --
 * at a target OBJECT when one is given, and at a bare POINT otherwise.
 *
 * IT IS A WEAPON HANDLER, and that is what the name rests on. The address is
 * column 1 of FIVE records in ADDR_WEAPON_HANDLERS -- 0x00489A00, A10, A20,
 * AF0 and B00 -- so several weapon kinds share one implementation. Column 1 is
 * what ADDR_WEAPON_FN_SLOT1 receives, and that global is called as
 * (object, packed point), which is exactly this signature and exactly the
 * shape SetPointerMode's release action has.
 *
 * The weapon lookup here is WeaponByUid, the COMPLAINING one -- an empty slot
 * or a non-weapon writes "uid wasn't a weapon!" to the log on the way out.
 * HeldWeaponCode reaches the same object through ObjIsType4 and says nothing.
 * Two spellings of one check again, and this path is the loud one.
 *
 * Reading that table also corrects orig.h. It said every field of a record is
 * a function pointer; only the first two are. Column 2 defaults to -1 and
 * column 3 to 0, and the readers settle it -- SLOT2 is tested with `jl`, a
 * SIGNED comparison nobody asks of a function pointer, and SLOT3 is a flag.
 * The old claim was generalised from the two readers that had been looked at,
 * which is the same failure as naming a function from one call site.
 *
 * IT REFUSES ON TWO MENU ROWS AND NOWHERE ELSE. GetMenuRow being 3 or 8 ends
 * it -- two separate calls to the same five-byte accessor, not one result
 * compared twice -- so whatever those rows are, the weapon does not aim while
 * they are up. Then the unit must resolve, be a type 2, and hold a weapon in
 * the selected slot; any of those failing returns silently having written
 * nothing.
 *
 * THE TWO ARMS DIFFER IN MORE THAN THE TARGET. Firing at an object zeroes the
 * three coordinate words, stores the target's uid, and puts the unit's own
 * OBJ_OFF_POSE into the mode field. Firing at a point stores the coordinates,
 * zeroes the uid, and puts 0x1F there instead. So the mode field is a pose for
 * one and a constant for the other, which is not a distinction the field's
 * position suggests.
 *
 * The point arrives packed and is unpacked into two separate int16 stores; the
 * third coordinate is always zero. Written as the two stores the original
 * makes rather than as one dword, because the field after them is written
 * separately and a dword store would cover it.
 *
 * MEASURED AT 0, and the reason is one already recorded. Its six call sites
 * are the pointer-mode action paths, and SetPointerMode reads exactly ONE
 * call on every drive here -- mode 0 at mission start. A weapon action is
 * only reachable once a mode above 0 is installed, which needs the
 * order-giving UI no drive reaches. Same wall, measured twice from two
 * sides.
 *
 * The counter is not blind: all six callers are the original's. So every
 * claim above -- the two menu rows, the two arms, the mode field being a
 * pose for one and 0x1F for the other -- is verified by reading.
 * WeaponByUid reads 247,958 on the same run, all of it other callers.
 */
void __cdecl SetWeaponTarget(void *target, uint32_t at)
{
    uint8_t *u;

    if (GetMenuRow() == 3)
        return;
    if (GetMenuRow() == 8)
        return;

    u = (uint8_t *)LookupByUID(
            *(const uint32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID);
    if (!ObjIsType2((const AM2_Object *)u))
        return;

    if (!WeaponByUid(
            *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                + (uint32_t)*(const int32_t *)(uintptr_t)ADDR_WEAPON_SLOT * 4)))
        return;

    *(int32_t *)(u + UNIT_OFF_FIRE_ACTIVE) = 1;
    *(uint8_t *)(u + UNIT_OFF_FIRE_F40)    = *(const uint8_t *)(u + 0x40);
    *(int32_t *)(u + UNIT_OFF_FIRE_F588)   = 1;
    *(int32_t *)(u + UNIT_OFF_FIRE_F58C)   = 1;

    if (target) {
        *(int16_t *)(u + UNIT_OFF_FIRE_X)   = 0;
        *(int16_t *)(u + UNIT_OFF_FIRE_Y)   = 0;
        *(int16_t *)(u + UNIT_OFF_FIRE_Z)   = 0;
        *(uint32_t *)(u + UNIT_OFF_FIRE_UID) =
            ((const AM2_Object *)target)->uid;
        *(int32_t *)(u + UNIT_OFF_FIRE_MODE) =
            *(const int32_t *)(u + OBJ_OFF_POSE);
        return;
    }

    *(int16_t *)(u + UNIT_OFF_FIRE_X)    = (int16_t)(at & 0xFFFFu);
    *(int16_t *)(u + UNIT_OFF_FIRE_Y)    = (int16_t)(at >> 16);
    *(int16_t *)(u + UNIT_OFF_FIRE_Z)    = 0;
    *(uint32_t *)(u + UNIT_OFF_FIRE_UID) = 0;
    *(int32_t *)(u + UNIT_OFF_FIRE_MODE) = AM2_FIRE_MODE_POINT;
}

/* 0x00447950, one caller. Choose a unit's UNIT_OFF_FIRE_MODE between two more
 * values than SetWeaponTarget above writes.
 *
 * The mode is 0x25 when the unit has OBJ_OFF_FIELD_5A4 AND its
 * OBJ_OFF_DEADLINE_58 is more than fifteen seconds behind the game clock, and
 * 1 in every other case -- including a unit with no field at all, which takes
 * the same 1 as one whose deadline is recent. Two different reasons, one
 * answer, and the function cannot be asked which applied.
 *
 * SO THAT FIELD CARRIES AT LEAST FOUR THINGS: a pose, 0x1F for a point target,
 * and these two. That is why it is named for its offset rather than for a
 * meaning -- any name taken from one writer would be wrong at the other three.
 *
 * The comparison is UNSIGNED (`jbe`), so a deadline in the FUTURE -- which
 * makes the subtraction wrap -- reads as enormously stale and takes the 0x25
 * arm. Whether that can happen is not established; OBJ_OFF_DEADLINE_58 is a
 * stamp of clock-plus-fuse, so it can. Reproduced as unsigned.
 *
 * MEASURED AT 0, and for the reason already recorded twice in this file: its
 * one caller is behind the weapon path, and SetPointerMode installs mode 0 and
 * nothing else on every drive here. Third function blocked on the same missing
 * drive.
 */
void __cdecl PickFireMode(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const int32_t *)(o + OBJ_OFF_FIELD_5A4)
        && (uint32_t)(*(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                      - *(const int32_t *)(o + OBJ_OFF_DEADLINE_58))
               > AM2_FIRE_STALE_MS) {
        *(int32_t *)(o + UNIT_OFF_FIRE_MODE) = AM2_FIRE_MODE_STALE;
        return;
    }

    *(int32_t *)(o + UNIT_OFF_FIRE_MODE) = AM2_FIRE_MODE_FRESH;
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

    SoldierKindForWeapon(unit, (uint32_t)kind);
    SendTrooperSetWeapon(unit, ((const AM2_Object *)w)->uid, slot);
}

typedef int32_t (__cdecl *AM2_Scan403B40Fn)(void *obj, void *a, void *b,
                                            void *c, void *d, int32_t e);
#define orig_scan_403b40 ((AM2_Scan403B40Fn)(uintptr_t)ADDR_SCAN_403B40)

/* NextInventorySlot -- original 0x004498F0, one caller. Move a trooper on to
 * its next inventory slot, wrapping to 0, on the weapon-switch action key or
 * on the middle mouse button.
 *
 * THE MOUSE TEST IS A RELEASE, not a press. `!ADDR_MOUSE_BUTTON[2] &&
 * ADDR_MOUSE_CHANGED[2]` -- held is clear, changed is set -- which is this
 * program's idiom for a button coming up, the same one the in-mission ESCAPE
 * handler and WidgetUpdate use. Both globals were already named as the two
 * three-element arrays they belong to, so this needed no new ones; reading
 * them as two loose flags would have hidden that the pair is one gesture.
 *
 * THE SLOT ARRAY IS READ ONE PAST THE SELECTION. `UNIT_OFF_INVENTORY` is six
 * uids and the test is on `inventory[sel + 1]`, so the question is "is there
 * anything in the NEXT slot" and the answer decides between `sel + 1` and 0.
 * The bound is `AM2_INVENTORY_SLOTS - 1` and that is not a tidy-up: the
 * original's literal is 5, and writing it as 5 got a redefinition of the
 * existing 6 from `checkoffsets` within the minute. The constant this wants is
 * "there is a next slot", which is the count less one, and saying so is what
 * keeps the read inside the six.
 *
 * The scan above it is on a timer of its own -- `OBJ_OFF_FIELD_FC` against the
 * game clock -- and runs whether or not anything is pressed, so it is not part
 * of the switch at all. Its answer is discarded here, where AiStepDefend keeps
 * the same function's answer as SIGHT_OFF_FOUND.
 *
 * The sound is PlaySoundAt with five zeros: index 0 at the origin, which the
 * volume model then treats as far away. Written as the original has it.
 */
void __cdecl NextInventorySlot(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  sel;

    if (*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        > *(const uint32_t *)(o + OBJ_OFF_FIELD_FC)) {
        int32_t a, b, c, d;

        orig_scan_403b40(obj, &a, &b, &c, &d, 0);
    }

    if (!ActionKeyPressed(AM2_ACTION_NEXT_WEAPON)) {
        if (((const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)[AM2_MOUSE_MIDDLE])
            return;
        if (!((const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)[AM2_MOUSE_MIDDLE])
            return;
    }

    sel = *(const int32_t *)(o + UNIT_OFF_INVENTORY_SEL);
    if (sel < AM2_INVENTORY_SLOTS - 1
        && *(const int32_t *)(o + UNIT_OFF_INVENTORY
                              + (uint32_t)(sel + 1) * 4) != 0) {
        PlaySoundAt(0, 0, 0, 0, 0);
        SelectInventorySlot(obj, sel + 1);
    } else {
        PlaySoundAt(0, 0, 0, 0, 0);
        SelectInventorySlot(obj, 0);
    }
}


/* The roach step seam is gone -- RoachStepTailA is reconstructed and its one
 * caller below calls it by name. What the seam's comment established is still
 * true of the signature and is kept here: the second argument is
 * `obj + OBJ_OFF_FIELD_540` and the FAMILY reads more than a byte through it
 * -- 0x0043D5B0 tests +0x14 as an int32, which is the object's
 * OBJ_OFF_DEATH_STATE. `uint8_t *facing` was a type off one use, the same
 * shape of mistake as ObjInitCommon's `dir`. */
typedef void (__cdecl *AM2_RoachRowFn)(void *row);
#define orig_roach_row_final \
    ((AM2_RoachRowFn)(uintptr_t)ADDR_ROACH_ROW_FINAL)

typedef void (__cdecl *AM2_StepFn)(void *obj);

/* StepType8 -- original 0x0043D980, one caller. The ROACH's per-frame step.
 *
 * Clear the footprint, refresh the roach's own copy of its facing, and then
 * split on health: alive, it makes a sound and takes two steps; dead, it
 * dispatches on OBJ_OFF_FIELD_530 through three arms and a default. Every
 * path ends with the same two calls.
 *
 * IT DISPATCHES ON 0x530 AND WRITES 0x554, AND THOSE ARE DIFFERENT FIELDS.
 * OBJ_OFF_DEATH_STATE (0x554) is what DamageRoach writes 5 or 6 into when a
 * roach's health reaches zero -- and this stepper does not read it. It reads
 * OBJ_OFF_FIELD_530, whose only other named reader is ObjConceal comparing it
 * to 5, and it takes the SAME value set: 0, 5 and 6. Checked against the
 * bytes rather than assumed, because two fields carrying one vocabulary is
 * exactly the shape a mis-transcribed offset produces. What sets 0x530 is not
 * established; it is not the damage path.
 *
 * THE FACING IS PASSED BY ADDRESS to four of the five callees. The step reads
 * OBJ_OFF_FACING into OBJ_OFF_FIELD_540 at the top and hands the address of
 * the copy on, so a callee that turns the roach writes the copy and the
 * object's own facing is only refreshed on the NEXT frame. Reproduced as the
 * pointer it is.
 *
 * FIVE CALLEES HAD NO NAME and they now have role names taken from where they
 * sit here, which is the weakest kind of naming this project does. Said
 * plainly rather than dressed up: two are what an alive roach runs, two are
 * the common tail, and one runs once as a dead one is destroyed. 704, 416,
 * 560, 304 and 64 bytes, none of them read.
 *
 * The alive path plays AM2_ROACH_ALIVE_SOUND every frame with flag 1, which
 * PlaySoundAt's own comment gives as restart-if-playing or do-not-interrupt.
 * A per-frame call only makes sense under the second reading; the flag's
 * meaning is not settled here and the call is reproduced either way.
 */
void __cdecl StepType8(void *obj)
{
    uint8_t *o      = (uint8_t *)obj;
    uint8_t *facing = o + OBJ_OFF_FIELD_540;

    ObjClearRoachFootprint(obj);
    *facing = *(const uint8_t *)(o + OBJ_OFF_FACING);

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0) {
        *(int32_t *)(o + OBJ_OFF_DEATH_STATE)     = 1;
        *(int32_t *)(o + OBJ_OFF_DEATH_STATE + 4) = 0;

        PlaySoundAt(AM2_ROACH_ALIVE_SOUND, 1, 0,
                    *(const int16_t *)(o + OBJ_OFF_POS),
                    *(const int16_t *)(o + OBJ_OFF_POS + 2));

        RoachAliveStepA(obj, facing);
        RoachAliveStepB(obj, facing);

    } else {
        switch (*(const int32_t *)(o + OBJ_OFF_FIELD_530)) {
        case 0:
            DestroyByType(obj);
            break;

        case 5:
            if (!RowAnimFinished(*(void **)(o + OBJ_OFF_ROWS)))
                break;
            *(int32_t *)(o + OBJ_OFF_DEATH_STATE) = 6;
            break;

        case 6:
            if (!RowAnimFinished(*(void **)(o + OBJ_OFF_ROWS)))
                break;
            RowFaceSprite(*(void **)(o + OBJ_OFF_ROWS));
            orig_roach_row_final(*(void **)(o + OBJ_OFF_ROWS));
            DestroyByType(obj);
            break;

        default:
            break;
        }
    }

    RoachStepTailA(obj, facing);
    ObjSetRoachFootprint(obj);
}

typedef void (__cdecl *AM2_StepFn)(void *obj);typedef void (__cdecl *AM2_StepFn)(void *obj);
#define orig_step_type5   ((AM2_StepFn)(uintptr_t)ADDR_STEP_TYPE5)

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
    case 1:         StepType2(obj);         break;   /* ours now */
    case 2:         StepType3(obj);         break;   /* ours now */
    case 4:         orig_step_type5(obj);   break;
    case 5:         StepType6(obj);         break;   /* ours now */
    case 6:         ObjMarkIfOverdue(obj);  break;   /* ours already */
    default:        StepType8(obj);         break;
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

/* NearestAllowedTile is region.cpp's now; the image seam that stood here
   went with the reconstruction. */

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
 * ADDR_NEAREST_ALLOWED_TILE by ADDRESS, and what comes back out of that slot
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

    ObjAttachTo(obj, 0);

    *(uint16_t *)(o + OBJ_OFF_SCRIPT_ID) = 0;
    *(uint16_t *)(o + OBJ_OFF_FIELD_B2)  = 0;

    tile = TileOfPoint(point);
    NearestAllowedTile(obj, tile, &point);

    *(int32_t *)(o + OBJ_OFF_FIELD_EC) =
        (*(const int32_t *)(o + OBJ_OFF_FIELD_F4) > 0) ? 1 : 0;
    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) = point;

    if (*(const int32_t *)(o + OBJ_OFF_AI_MODE) == 3)
        *(int32_t *)(o + OBJ_OFF_AI_MODE) = 1;
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
        ObjMoveAlongFacing(obj, 0, 0, 0);
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

/* SettlePointInRegion and the two comm functions stay original and are
 * reached by address. The first rewrites the point it is given through a rule
 * chosen from the region; the second is the "<--Trooper Drop Item Send"
 * message. */

/* UseInventoryItem -- original 0x00449760, one caller, and it names itself in
 * both of its log lines.
 *
 * Spend one charge of an inventory slot, and give the slot up when the last
 * one goes. Four things worth stating.
 *
 * ITS SLOT IS CHECKED AT ONE END ONLY. `slot <= 0` is refused and there is NO
 * UPPER BOUND, so a slot of 6 or more indexes past the six-entry
 * UNIT_OFF_INVENTORY and reads whatever follows it. TrooperDropItem, twenty
 * lines away in the same file, checks `>= 6` as well. Reproduced, and the
 * asymmetry is the original's -- the one caller passes a slot it took from
 * the unit itself, so nothing here can reach the gap.
 *
 * THE MULTIPLAYER GUARD IS THE FIRST THING AND IT ONLY APPLIES IN A SESSION.
 * With no session it falls straight through; in one, a unit whose army
 * CommMustBroadcast refuses returns having done nothing -- so a charge is
 * spent by the owner and by nobody else, and everyone else hears about it
 * from the message at the end.
 *
 * IT DECREMENTS FIRST AND DECIDES AFTERWARDS. The ammo is written back
 * whatever it becomes; only when it has reached exactly zero is the slot
 * removed and the message sent. So the common case -- a charge spent with
 * some left -- writes one field and returns, and everything below the
 * decrement is the LAST charge's path.
 *
 * THE SPENT ITEM IS FLAGGED, NOT FREED. Its OBJ_OFF_FLAGS gains bit 1 and it
 * is left for whatever sweeps that flag. orig.h carries two names for that
 * bit -- OBJ_FLAG_OVERDUE and OBJ_FLAG_REPLACED, an alias in the ratchet's
 * baseline -- and REPLACED is the reading here: this is the same flag the two
 * weapon-making paths in this file set on the weapon they supersede.
 *
 * The message goes out with a quantity of ZERO, which is what distinguishes a
 * used-up item from TrooperDropItem's genuine drop: same sender, same slot,
 * no ammo left to hand over.
 */
void __cdecl UseInventoryItem(void *unit, int32_t slot)
{
    uint8_t *u = (uint8_t *)unit;
    uint8_t *comm = (uint8_t *)AM2_IMAGE(ADDR_COMM_OBJECT);
    uint8_t *item;
    int32_t  ammo;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(comm, (int16_t)*(const int8_t *)(u + OBJ_OFF_ARMY)))
        return;

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        am2_log("UseInventoryItem\n");

    if (slot <= 0)
        return;

    item = (uint8_t *)WeaponByUid(
        (int32_t)(*(const uint32_t *)(u + UNIT_OFF_INVENTORY + slot * 4)));
    if (!item)
        return;

    if (!KindInSetA(**(const int32_t *const *)(item + OBJ_OFF_FIELD_C0)))
        return;

    ammo = *(const int32_t *)(item + ITEM_OFF_AMMO);
    if (ammo <= 0)
        return;

    ammo--;
    *(int32_t *)(item + ITEM_OFF_AMMO) = ammo;
    if (ammo != 0)
        return;

    RemoveInventoryItem(u, slot);

    if (CommMustBroadcast(comm,
                          (int16_t)*(const int8_t *)(u + OBJ_OFF_ARMY))) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            am2_log("UseInventoryItem: droping item:%x\n",
                    ((const AM2_Object *)item)->uid);

        TrooperDropItemSend(u, item, slot, 0,
                            *(const uint32_t *)(u + OBJ_OFF_POS));
    }

    *(uint32_t *)(item + OBJ_OFF_FLAGS) |= OBJ_FLAG_REPLACED;
}

/* TrooperDropItem -- original 0x00448D60, and it names itself in both of its
 * log lines: "TrooperDropItem  %x" and "TrooperDropItem  %x  ammo: %d".
 *
 * ITS SLOT RANGE IS 1..5, NOT 0..5. The two guards are `>= 6` and `<= 0`, so
 * slot 0 of the six-entry UNIT_OFF_INVENTORY can never be dropped -- whatever
 * a trooper is holding in that slot stays with it. Reproduced; the array is
 * six long and this reaches five of it.
 *
 * THE POINT IS REWRITTEN IN THE CALLER'S OWN ARGUMENT SLOT. The original
 * passes `&at` to SettlePointInRegion, which moves the point somewhere the
 * region will accept, and every later use is of the moved one. A local here,
 * since nothing observes the difference -- but it is why the drop lands where
 * it lands rather than under the trooper's feet.
 *
 * ONE OF ITS TWO LOG LINES IS GATED AND THE OTHER IS NOT. The bare
 * "TrooperDropItem %x" only prints for a drop that has to be broadcast, and
 * only when COMM_OFF_VERBOSE is set; the one with the ammo prints on every
 * drop. So a single-player log shows the second and never the first.
 *
 * IT DEREFERENCES THE ITEM BEFORE TESTING IT. Both the ammo read for that log
 * line and the uid beside it come out of the item that WeaponByUid returned,
 * and the `if (!item)` guard is four instructions further down -- so a slot
 * holding a uid that no longer resolves faults in the logging, not in the
 * work. Reproduced.
 *
 * THE ITEM TAKES THE TROOPER'S HEIGHT and then the neutral army. The height
 * is copied before the deploy so the thing lands at the height the trooper
 * was standing at; the army is written last and unconditionally, which is an
 * item going ownerless as it leaves the hands that held it.
 *
 * AND THE DEPLOY IS GATED ON AMMO. ITEM_OFF_AMMO zero means the item is spent:
 * it is still removed from the inventory, still recorded in
 * UNIT_OFF_LAST_DROPPED, still made neutral -- but never placed on the map and
 * never notified as dropped. So an empty weapon disappears rather than being
 * droppable.
 */
void __cdecl TrooperDropItem(void *unit, int32_t slot, uint32_t at)
{
    uint8_t *u = (uint8_t *)unit;
    uint8_t *item;

    if (slot >= AM2_INVENTORY_SLOTS || slot <= 0)
        return;

    item = (uint8_t *)WeaponByUid(
        (int32_t)(*(const uint32_t *)(u + UNIT_OFF_INVENTORY + slot * 4)));

    SettlePointInRegion(TileOfPoint(at), &at);

    if (CommMustBroadcast((void *)AM2_IMAGE(ADDR_COMM_OBJECT),
                          (int16_t)*(const int8_t *)(u + OBJ_OFF_ARMY))) {
        if (*(const int32_t *)((const uint8_t *)AM2_IMAGE(ADDR_COMM_OBJECT)
                               + COMM_OFF_VERBOSE))
            am2_log("TrooperDropItem  %x\n", ((const AM2_Object *)item)->uid);

        TrooperDropItemSend(u, item, slot,
                            *(const int32_t *)(item + ITEM_OFF_AMMO), at);
    }

    am2_log("TrooperDropItem  %x  ammo: %d\n",
            ((const AM2_Object *)item)->uid,
            *(const int32_t *)(item + ITEM_OFF_AMMO));

    RemoveInventoryItem(u, slot);

    if (!item)
        return;

    *(uint32_t *)(u + UNIT_OFF_LAST_DROPPED) = ((const AM2_Object *)item)->uid;
    *(uint8_t *)(item + OBJ_OFF_HEIGHT_SET) =
        *(const uint8_t *)(u + OBJ_OFF_HEIGHT_SET);

    if (*(const int32_t *)(item + ITEM_OFF_AMMO)) {
        DeployItem(item, at, 0, 0);
        NotifyDropped(item, u);
    }

    *(uint8_t *)(item + OBJ_OFF_ARMY) = AM2_ARMY_NEUTRAL;
}

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

    made = orig_make_weapon((char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
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

    SoldierKindForWeapon(obj, AM2_WEAPON_KEY_2B);
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

/* WeaponPoseIndex -- original 0x004494A0, three callers.
 *
 * Which POSE a unit takes for the weapon it is holding. The answer is an
 * index into ADDR_WEAPON_POSE_FRAMES, which the caller turns into a frame.
 *
 * TWO TABLES, BOTH INSIDE THE FUNCTION'S OWN 208 BYTES. One byte per weapon
 * code 1..0x2B picks one of four arms, and the arms are a jump table. Written
 * out as a switch over the arm rather than as the jump: the compiler chose the
 * table, the source did not, and three of the four arms are two lines.
 *
 * The 43 arm bytes are reproduced verbatim. They are not derivable -- there is
 * no pattern in {0,3,3,0,3,3,0,0,0,0,3...} beyond "most weapons take the
 * default" -- and inventing an `if` chain that happened to agree on the codes
 * the game ships would be the same mistake as reading a table's bounds from
 * its data.
 *
 * ADDR_POSE_BY_CLASS IS NOT ADDR_WEAPON_POSE_FRAMES, which sits 0x1A0 bytes
 * earlier. The default arm reads the first, not the second, and orig.h's note
 * on the pose field says WeaponPoseIndex "computes an index into"
 * ADDR_WEAPON_POSE_FRAMES -- true of what it RETURNS and not of what it
 * reads. Two tables 416 bytes apart is exactly the sort of thing to check
 * rather than assume when a comment already names one of them.
 *
 * OBJ_OFF_FIELD_578 is the second axis and it is still unnamed. It selects
 * between {1, 4} and {0x19, 0x1A} for one family of weapons and between
 * {1, 4} and {0x1F} for another, and it does nothing at all for class 2,
 * which always answers 6. The original computes it with `neg`/`sbb`/`and`,
 * which is a branchless "non-zero to a constant"; written as a conditional.
 */
int32_t __cdecl WeaponPoseIndex(void *obj, void *weapon)
{
    /* The arm each weapon code selects, verbatim from 0x0044953C. */
    static const uint8_t arm_of_code[AM2_WEAPON_CODE_MAX] = {
        0, 3, 3, 0, 3, 3, 0, 0, 0, 0, 3, 3, 3, 3, 3,
        3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 3, 3, 0, 0,
        3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 3, 3, 2
    };
    const uint8_t *o     = (const uint8_t *)obj;
    int32_t        cls   = ClassifyByCode74(obj);
    int32_t        armed;
    int32_t        code;

    if (!weapon)
        return ((const int32_t *)AM2_IMAGE(ADDR_POSE_BY_CLASS))[cls];

    code = **(const int32_t *const *)((const uint8_t *)weapon
                                      + OBJ_OFF_FIELD_C0) - 1;
    if ((uint32_t)code >= AM2_WEAPON_CODE_MAX)
        return ((const int32_t *)AM2_IMAGE(ADDR_POSE_BY_CLASS))[cls];

    armed = *(const int32_t *)(o + OBJ_OFF_FIELD_578) != 0;

    switch (arm_of_code[code]) {
    case 0:
        if (cls == 1)
            return armed ? AM2_POSE_KNEEL_ARMED_A : AM2_POSE_KNEEL;
        if (cls == 2)
            return AM2_POSE_CLASS2;
        return armed ? AM2_POSE_STAND_ARMED : AM2_POSE_STAND;

    case 1:
        if (cls == 1)
            return armed ? AM2_POSE_KNEEL_ARMED_B : AM2_POSE_KNEEL;
        if (cls == 2)
            return AM2_POSE_CLASS2;
        return AM2_POSE_STAND;

    case 2:
        return AM2_POSE_STAND;

    default:
        return ((const int32_t *)AM2_IMAGE(ADDR_POSE_BY_CLASS))[cls];
    }
}

/* SetAnimFrame is maprow.cpp's now; the image seam that stood here went
   with the reconstruction, and checkseams is what noticed. */
/* StepRowAnim -- original 0x0040A380, one caller.
 *
 * Advance one row's animation by one frame. The per-frame path reaches it once
 * per row of every object in the mission, so it is among the hottest functions
 * in this tree and one of the few in this stretch a pixel comparison can
 * actually see.
 *
 * ROW_OFF_FRAME IS TWO THINGS BY RANGE, and that is what this function shows.
 * Below 1000 the stepper ignores it -- it is the frame id SetAnimFrame matched
 * on. At exactly 1000 it takes up ROW_OFF_ANIM_NEXT_ID. Above 1000 it counts
 * DOWN by one and returns. So anything over 1000 is a delay in FRAMES before a
 * queued animation starts, counting down to the 1000 that starts it. orig.h
 * called the field "int16_t" and nothing more; it can say that now.
 *
 * THE CELL'S HOLD IS HALVED FOR ONE PARTICULAR REMAP TABLE. If the row's
 * MAPOBJ_OFF_LUT is ADDR_ROW_LUT_DOUBLES -- compared by ADDRESS, not by
 * content -- the dwell is shifted right by one, so that animation runs at
 * double speed. orig.h records the same table making a DIFFERENT function
 * double ROW_OFF_FIELD_3C. Two readers, two fields, and both amount to
 * "this one goes faster"; what makes the table special is still not
 * established, but it now has two witnesses instead of one.
 *
 * ANIM_NEXT_ID OF -2 MEANS HOLD. On the last cell the stepper decrements the
 * cell index back, so the animation sits on its final frame forever rather
 * than looping or stopping. Every other value is a frame id to take up.
 *
 * The heading is ROW_OFF_HEADING plus ROW_OFF_HEADING_BIAS, added as BYTES so
 * it wraps at 256, then put through RoundTo8 with the animation's own
 * direction-bit count -- which is what picks the row of cells to use.
 *
 * The sprite is only re-set when it actually changes, and SetRowSprite
 * rebuilds the row's cell buffer when it does; that guard is why a mission
 * does not rebuild every row every frame.
 */
/* The sprite list, as a void ** rather than sprite.cpp's AM2_Sprite *** --
 * this is the FLAT half and must not name that type, which has an
 * LPDIRECTDRAWSURFACE in it. Same address, same arithmetic, and deliberately
 * NOT called g_spriteList: two different expansions under one g_ name is
 * exactly the drift checkglobals refuses. */
#define kSpriteList     (*(void ***)(uintptr_t)ADDR_SPRITE_LIST)

void __cdecl StepRowAnim(void *row)
{
    uint8_t        *r = (uint8_t *)row;
    const AM2_Anim *anim;
    const AM2_AnimCell *cell;
    int32_t         idx;
    int32_t         hold;
    uint32_t        now;
    int32_t         at;

    if (!(*(const uint8_t *)(r + MAPOBJ_OFF_FLAGS) & 1))
        return;

    if (*(const int16_t *)(r + ROW_OFF_FRAME) >= AM2_ROW_DELAY_BASE) {
        if (*(const int16_t *)(r + ROW_OFF_FRAME) == AM2_ROW_DELAY_BASE) {
            SetAnimFrame(r, *(const int16_t *)(r + ROW_OFF_ANIM_NEXT_ID), 0);
            return;
        }
        *(int16_t *)(r + ROW_OFF_FRAME) -= 1;
        return;
    }

    anim = *(const AM2_Anim *const *)(r + ROW_OFF_ANIM_PLAYING);
    if (!anim)
        return;

    idx = anim->frames
          * (uint8_t)RoundTo8((uint8_t)(*(const uint8_t *)(r + ROW_OFF_HEADING_BIAS)
                                        + *(const uint8_t *)(r + ROW_OFF_HEADING)),
                              anim->directionBits)
          + *(const uint8_t *)(r + ROW_OFF_CELL);

    cell = &anim->cells[idx];

    {
        /* TWO dereferences: the global holds the array. Written with one on
         * the first attempt and the game left at the first mission -- the
         * exact failure CLAUDE.md records for `obj -> table -> slot`, which I
         * had read and still made. */
        void *want = kSpriteList[cell->sprite];

        if (*(void *const *)(r + ROW_OFF_SPRITE) != want)
            RowSetSprite(r, want, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    hold = cell->hold;
    if (*(const void *const *)(r + MAPOBJ_OFF_LUT)
            == (const void *)AM2_IMAGE(ADDR_ROW_LUT_DOUBLES))
        hold >>= 1;

    now = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
    if (now < *(const uint32_t *)(r + ROW_OFF_STAMP_54) + (uint32_t)hold)
        return;

    at = (uint8_t)(*(const uint8_t *)(r + ROW_OFF_CELL) + 1);
    *(uint8_t *)(r + ROW_OFF_CELL) = (uint8_t)at;

    if ((int16_t)at < anim->frames) {
        *(uint32_t *)(r + ROW_OFF_STAMP_54) = now;
        return;
    }

    if (*(const int16_t *)(r + ROW_OFF_ANIM_NEXT_ID) == AM2_ROW_ANIM_HOLD) {
        *(uint8_t *)(r + ROW_OFF_CELL) = (uint8_t)(at - 1);
        return;
    }

    *(uint8_t *)(r + ROW_OFF_CELL)      = 0;
    *(uint32_t *)(r + ROW_OFF_STAMP_54) = now;
    SetAnimFrame(r, *(const int16_t *)(r + ROW_OFF_ANIM_NEXT_ID), 0);
}

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
                int32_t pose = WeaponPoseIndex(obj, w);

                SetAnimFrame(*(uint8_t **)(o + OBJ_OFF_ROWS),
                                    (int16_t)((const int32_t *)(uintptr_t)
                                        ADDR_WEAPON_POSE_FRAMES)[pose], 1);
            } else {
                SetAnimFrame(*(uint8_t **)(o + OBJ_OFF_ROWS), 1, 1);
            }
        } else {
            uint8_t *r = *(uint8_t **)(o + OBJ_OFF_ROWS);

            SetAnimFrame(r, *(const int16_t *)(r + ROW_OFF_FRAME), 1);
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

/* 0x00449660, sixteen callers. Set a unit's soldier kind from the code of the
 * weapon now in its hands.
 *
 * IT WAS NAMED FOR NEITHER OF THOSE THINGS. orig.h called it ADDR_UNIT_ACTION,
 * "void(obj, action) -- 44 arms", and both halves were wrong. The argument is
 * the dword an item's OBJ_OFF_FIELD_C0 record points at -- the same value
 * HeldWeaponCode returns, and the same one SelectInventorySlot in this file
 * already computes and indexes ADDR_WEAPON_HANDLERS with before passing it
 * here. Sixteen callers and every one does the same three steps: put a weapon
 * in the hand, read its code, call this.
 *
 * AND THERE ARE SEVEN ARMS, NOT 44. The 44 is the length of a byte index table
 * at 0x00449728 that collapses the code into one of eight jump-table slots at
 * 0x00449708. Six of those differ only in the constant they hand
 * SetSoldierKind, one is the default, and one is a bare `ret`. Counting a
 * dense switch's index table as arms is how 44 was written down; the same
 * mistake would make ThingCode 39 behaviours instead of the seventeen it has.
 *
 * Written as a switch on the CODE, the way air.cpp writes ThingCode, so the
 * numbers here are the ones a data file carries rather than offsets into a
 * table nobody can see. Both tables were read out of the image and the mapping
 * is transcribed from them and nothing else.
 *
 * CODE 0 IS THE ONE ARM THAT WRITES NOTHING, and it is not dead. 0x00448220
 * sets the kind to 8 by hand, clears the weapon uid, and then calls this with
 * 0 -- so "no weapon" has to leave the kind alone, or that sequence would undo
 * itself one line later. A default that fell through to kind 0 would look
 * tidier and would be wrong.
 *
 * Kind 6 is never produced by any code. The bound is UNSIGNED (`cmp eax, 0x2b;
 * ja`), so a negative code takes the default rather than indexing backwards.
 *
 * MEASURED: 6 calls on a Boot Camp mission driven with movement and fire, so
 * the A/B compares it. Note what landing it did to the counter BELOW it --
 * SetSoldierKind now reads 0 on the same run, because its remaining caller is
 * this function and this function calls by name. The reconstruction swallowed
 * a counter, which is the ordinary cost recorded in CLAUDE.md and worth
 * naming here so a later reader does not read that 0 as a regression.
 */
void __cdecl SoldierKindForWeapon(void *unit, uint32_t code)
{
    switch (code) {
    case 0:  return;                        /* writes nothing; see above */
    case 2:  SetSoldierKind(unit, 2); return;
    case 3:  SetSoldierKind(unit, 3); return;
    case 4:  SetSoldierKind(unit, 1); return;
    case 5:  SetSoldierKind(unit, 4); return;
    case 20: SetSoldierKind(unit, 5); return;
    case 43: SetSoldierKind(unit, 7); return;
    default: SetSoldierKind(unit, 0); return;
    }
}

/* ApplyObjFrame is reconstructed, in objtype.cpp, and called by name. The
 * typedef here named its second and third parameters `b` and `a`; reading the
 * body settled them as the sprite SET and INDEX, in that order, which is what
 * the two unpacks below have always been passing. */

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

        if (ApplyObjFrame(obj,
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

            if (ApplyObjFrame(link,
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

/* RankPromote -- original 0x00457BC0, one caller.
 *
 * Bump an object's OBJ_OFF_RANK and, for a plain type 2 at ranks 3, 5 and 7,
 * give it a new weapon.
 *
 * THE PROMOTION AND THE WEAPON HAVE DIFFERENT CONDITIONS, and the rank is
 * written BEFORE the weapon's are tested -- so a vehicle, a Sarge or a
 * soldier of a non-zero kind still gets promoted, it just gets nothing for
 * it. Reading the function as "promote a trooper" would put the write inside
 * the guard and quietly stop ranking everything else.
 *
 * THE THREE WEAPONS ARE 0x0A, 0x08 and 0x1D, in that order for ranks 3, 5 and
 * 7. Not ascending, not consecutive, and ranks 4 and 6 give nothing -- the
 * original writes it as a chain of `sub`/`je`, which is a switch over a
 * sparse set rather than a table, and it is reproduced as one.
 *
 * The outgoing weapon is flagged rather than freed: OBJ_FLAG_REPLACED goes on
 * it and it is left alone. Nothing this function can see reads that bit.
 *
 * The multiplayer guard is the usual one -- CommMustBroadcast on the object's
 * owner, skipped entirely when there is no session, so single player always
 * promotes.
 *
 * CreateWeapon's eight arguments are taken from armymsg.cpp's typedef for the
 * same address; three of them are constants this function supplies and the
 * fourth is ADDR_ZERO_POINT, so the weapon is created at the origin and the
 * caller's own SendTrooperSetWeapon is what places it.
 */
void __cdecl RankPromote(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  rank;
    int32_t  weaponId;
    void    *old;
    uint8_t *made;

    if (!obj)
        return;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
        return;

    rank = *(const int32_t *)(o + OBJ_OFF_RANK);
    if (rank >= AM2_RANK_MAX)
        return;

    rank++;
    *(int32_t *)(o + OBJ_OFF_RANK) = rank;

    if (*(const int32_t *)o != 2)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SARGE))
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND))
        return;

    switch (rank) {
    case 3: weaponId = AM2_RANK_WEAPON_3; break;
    case 5: weaponId = AM2_RANK_WEAPON_5; break;
    case 7: weaponId = AM2_RANK_WEAPON_7; break;
    default: return;
    }

    old = WeaponByUid(*(const uint32_t *)(o + UNIT_OFF_INVENTORY));
    if (old)
        *(uint32_t *)((uint8_t *)old + OBJ_OFF_FLAGS) |= OBJ_FLAG_REPLACED;

    made = (uint8_t *)orig_make_weapon(
        (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
        *(const int8_t *)(o + OBJ_OFF_ARMY),
        KeyLookupTriple(AM2_RANK_WEAPON_GROUP, weaponId, 0),
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT,
        4, -1, 0, 0);

    if (!made)
        return;

    /* +4 is AM2_Object::uid; item.cpp reaches it that way in a dozen places
     * and there is no OBJ_OFF_ macro for it. */
    *(uint32_t *)(o + UNIT_OFF_INVENTORY) =
        ((const AM2_Object *)made)->uid;

    SoldierKindForWeapon(
        o, **(const uint32_t *const *)(made + OBJ_OFF_FIELD_C0));

    SendTrooperSetWeapon(o, ((const AM2_Object *)made)->uid, 0);
}

typedef void (__cdecl *AM2_RankPromoteFn)(void *obj);
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
        const uint8_t *rec = (const uint8_t *)(uintptr_t)ADDR_RANK_RECORDS
                             + (uint32_t)*(const int32_t *)(o + OBJ_OFF_RANK)
                               * RANK_REC_BYTES;
        int16_t health;
        int16_t maxHealth;

        if (*(const int32_t *)(o + OBJ_OFF_REPAIR_FRAME)
            < *(const int32_t *)(rec + RANK_REC_OFF_XP))
            return;

        RankPromote(obj);

        rec = (const uint8_t *)(uintptr_t)ADDR_RANK_RECORDS
              + (uint32_t)*(const int32_t *)(o + OBJ_OFF_RANK) * RANK_REC_BYTES;
        SetMaxHealth(obj, *(const int32_t *)(rec + RANK_REC_OFF_MAX_HEALTH));

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
        StepRowAnim(*(uint8_t **)(o + OBJ_OFF_ROWS)
                           + (uint32_t)i * AM2_OBJ_ROW_STRIDE);

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 0)
        *(int32_t *)(o + OBJ_OFF_FIELD_44) =
            *(const int16_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS)
                               + ROW_OFF_FIELD_3C);
}

/* 0x0040D930, nine callers. Put a unit into a POSE.
 *
 * Two dense switches and a wait, and the order they run in is the whole of it.
 *
 * THE WORD "POSE" IS NOT A GUESS AND IT WAS NOT MINE. This went in calling the
 * argument a state, and the alias ratchet refused the build: the table it
 * indexes, 0x00474FE0, was already ADDR_WEAPON_POSE_FRAMES, named from the
 * other side where ADDR_WEAPON_POSE_INDEX computes an index into it from
 * (object, weapon). Same table, same index, two unrelated callers. The three
 * OBJ_OFF_HEIGHT_ADJ values it goes on to select -- 8, 0x10, 0x18 -- are three
 * heights, which is what stand, kneel and prone look like, and those are the
 * three words the script's `setaipose` takes. A check that exists to stop
 * duplicate names found a better name instead.
 *
 * ASKING FOR THE POSE IT IS ALREADY IN RETURNS AT ONCE, before anything else
 * -- so this is idempotent and callers lean on that.
 *
 * THE FIRST SWITCH DOES NOT CHOOSE THE POSE, it chooses how to get there. Its
 * 37-entry index table collapses to three behaviours: six poses set an
 * "interrupt" flag, nine QUEUE themselves in OBJ_OFF_POSE_PENDING and fall
 * through, and the rest do nothing. A pose above 0x24 skips the switch
 * entirely and lands in that third group. The flag starts as "the pose we are
 * leaving is 1", so the switch can only ever SET it -- there is no arm that
 * clears it, and reading the table as a plain pose-to-action map misses that.
 *
 * THE WAIT IS WHAT THE FLAG IS FOR. Without it, a unit whose current animation
 * has not reached its last cell returns and the new state is dropped -- except
 * that the nine queueing states have already stored themselves, so they are
 * the ones that survive the wait. That is the mechanism: queue, return, and be
 * picked up by the next call that gets past the wait. The pending field is
 * consumed here and nowhere else.
 *
 * THE SECOND SWITCH IS INDEXED BY THE FRAME, NOT THE POSE. The pose picks a
 * frame id out of ADDR_WEAPON_POSE_FRAMES, and that frame id -- offset by ten
 * and bounded to fifty -- picks one of three OBJ_OFF_HEIGHT_ADJ values: 8,
 * 0x10 or 0x18, with 0x18 also the out-of-range default. Written as a switch
 * on the frame id so the numbers are the ones the data carries.
 *
 * NEITHER TABLE READ IS BOUNDED THE WAY IT LOOKS. The first switch bounds the
 * ARGUMENT at 0x24, but ADDR_WEAPON_POSE_FRAMES is indexed AFTER a pending
 * pose may have replaced it, and nothing rechecks. Reproduced.
 *
 * The original reads that table entry twice, once as a dword for the range
 * test and once as a word for SetAnimFrame. Same number; written once here.
 *
 * MEASURED AT 26,057 CALLS on a driven Boot Camp mission, with SetAnimFrame
 * beneath it at 12,399 on the same run -- so rather more than half of these
 * stop at the early-out or the wait, which is what the two guards are for and
 * is the first direct evidence that either fires. Both switches, both guards
 * and the frame lookup are on the compared path; what a run cannot be assumed
 * to cover is any particular ARM of either switch.
 */
void __cdecl SetUnitPose(void *obj, int32_t pose)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *row;
    int32_t  cur = *(const int32_t *)(o + OBJ_OFF_POSE);
    int32_t  interrupt;
    int32_t  pending;
    int32_t  frame;

    if (cur == pose)
        return;

    interrupt = (cur == 1);

    switch (pose) {
    case 0:
    case 32: case 33: case 34: case 35: case 36:
        interrupt = 1;
        break;
    case 5:
    case 8:  case 9:  case 10: case 11:
    case 12: case 13: case 14: case 15:
        *(int32_t *)(o + OBJ_OFF_POSE_PENDING) = pose;
        break;
    default:
        break;                          /* includes every pose above 0x24 */
    }

    row = *(uint8_t *const *)(o + OBJ_OFF_ROWS);

    if (!interrupt) {
        const AM2_Anim *anim =
            *(const AM2_Anim *const *)(row + ROW_OFF_ANIM_PLAYING);

        if (anim
            && *(const uint8_t *)(row + ROW_OFF_CELL) < anim->frames - 1)
            return;                     /* not on the last cell yet */
    }

    pending = *(const int32_t *)(o + OBJ_OFF_POSE_PENDING);
    if (pending) {
        pose = pending;
        *(int32_t *)(o + OBJ_OFF_POSE_PENDING) = 0;
    }

    *(int32_t *)(o + OBJ_OFF_POSE) = pose;

    frame = ((const int32_t *)AM2_IMAGE(ADDR_WEAPON_POSE_FRAMES))[pose];

    switch (frame) {
    case 10: case 12: case 14: case 19:
    case 49: case 50: case 51:
        *(uint8_t *)(o + OBJ_OFF_HEIGHT_ADJ) = 0x10;
        break;
    case 11: case 13: case 15: case 20:
    case 52: case 53: case 59:
        *(uint8_t *)(o + OBJ_OFF_HEIGHT_ADJ) = 8;
        break;
    default:
        *(uint8_t *)(o + OBJ_OFF_HEIGHT_ADJ) = 0x18;
        break;
    }

    SetAnimFrame(row, (int16_t)frame, 0);
}

/* orig_on_selection_changed is orig.h's; objtable.cpp uses the same one. */

/* 0x00427BA0, nine callers. Deselect everything.
 *
 * It walks ADDR_SELECTED_UIDS, and each uid takes one of two paths. One that
 * no longer resolves to an object is REMOVED from the list and the index is
 * NOT advanced, because the list has shifted under it -- the same walk-and-
 * sweep idiom ObjAttachToArmy uses. One that does resolve has
 * OBJ_FLAG_SELECTED cleared and, if it is a type 2, 3 or 8, gets one more
 * step: its OBJ_OFF_FOLLOW_UID is looked up, and if that object exists and is
 * allied with the player's army, ObjAttachTo is called on the pair.
 *
 * THE REMOVAL IS POINTLESS FOR THE LIST AND NOT FOR THE LOOP. Everything the
 * walk leaves is cleared wholesale by ClearPtrList afterwards, so removing an
 * entry cannot change the final state. What it changes is the BOUND: the count
 * is re-read every iteration and ListRemoveAt decrements it, so a dead uid
 * shortens the walk instead of being visited. Written as the original has it
 * rather than simplified away, because the two are only equivalent while
 * nothing else observes the list mid-walk, and ObjAttachTo is called from
 * inside it.
 *
 * THE SELECTED BIT IS CLEARED WITH A BYTE STORE. The original does
 * `and ch, 0xFB` on the flags dword, which is bit 10 -- OBJ_FLAG_SELECTED --
 * and leaves the other three bytes untouched by not writing them. Written as
 * the mask on the whole dword, which is the same result; the byte width is a
 * register allocation, not a claim about which bits are safe to disturb.
 *
 * The tail runs whether or not the walk did: an empty list still clears the
 * record and still reports the change, with ADDR_ZERO_POINT as the position.
 *
 * MEASURED AT 2 CALLS on a driven Boot Camp mission, with SelectUnit at 3
 * on the same run -- so something really was selected and then dropped, and
 * the walk, the flag clear and the tail are compared. What those two
 * cannot have covered is either of the interesting branches: a uid that
 * fails to resolve, which is the one that removes without advancing, and
 * the type-2/3/8 arm reaching ObjAttachTo. Both stay verified by reading,
 * and the first is exactly the branch a walk-and-sweep gets wrong.
 */
void __cdecl DeselectAll(void)
{
    uint8_t *sel = (uint8_t *)(uintptr_t)ADDR_SELECTED_UIDS;
    int32_t  i   = 0;

    while (i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT) {
        int32_t   at;
        int32_t   slot;
        uint8_t  *obj;

        slot = FindSlot((*(const uint32_t *const *)(sel + 8))[i], &at);
        obj  = slot >= 0
               ? (uint8_t *)(*(AM2_ObjEntry *const *)
                                 (uintptr_t)ADDR_OBJ_TABLE)[slot].obj
               : (uint8_t *)0;

        if (!obj) {
            ListRemoveAt(sel, i);       /* and do NOT advance i */
            continue;
        }

        *(uint32_t *)(obj + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_SELECTED;

        if (ObjIsTypeIn238((const AM2_Object *)obj)) {
            int32_t s2 = FindSlot(*(const uint32_t *)(obj + OBJ_OFF_FOLLOW_UID),
                                  &at);

            if (s2 >= 0) {
                void *other = (*(AM2_ObjEntry *const *)
                                   (uintptr_t)ADDR_OBJ_TABLE)[s2].obj;

                if (other
                    && ArmyAlliedWithObj(
                           (int32_t)*(const uint32_t *)
                               (uintptr_t)ADDR_DEFAULT_OWNER, other, 0))
                    ObjAttachTo(obj, (void *)0);
            }
        }

        i++;
    }

    ClearPtrList(sel);
    OnSelectionChanged(*(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
}

/* ADDR_SEND_VEHICLE_ENTER stays original and is reached by address -- it is
 * the twin of armymsg.cpp's SendVehicleExit and names itself the same way,
 * "<--Vehicle Enter Send: Vehicle: %x, item: %x". */
typedef void (__cdecl *AM2_SendVehicleEnterFn)(void *vehicle, void *unit);
#define orig_send_vehicle_enter \
    ((AM2_SendVehicleEnterFn)(uintptr_t)ADDR_SEND_VEHICLE_ENTER)

/* EnterVehicle -- original 0x0045AA00, three callers.
 *
 * Put a unit into a vehicle, all the way. BoardVehicle below is the two-field
 * half of this and is NOT called: the two writes appear here again, inline,
 * which is either the compiler or the author and cannot be told apart from
 * the code. What surrounds them is the rest of the transaction.
 *
 * THE SEAT CHECK IS THE FIRST THING AND THE ONLY REFUSAL. Once the occupant
 * list has as many entries as VEHICLE_OFF_SEATS it returns having done
 * nothing -- no message, no log, no sign at the call site that the unit is
 * still outside.
 *
 * SARGE TAKES SEAT ZERO, AND THE SWAP IS NOT A ROTATION. When the unit that
 * just boarded has OBJ_OFF_SARGE set and there is more than one occupant, the
 * seat-zero uid is copied to the LAST seat -- the one this unit was just
 * pushed into -- and the unit's own uid is written into seat zero. So the two
 * exchange places and nobody else moves. That is what makes seat zero the
 * one ExitAllFromVehicle empties last.
 *
 * The unit's OBJ_OFF_SCRIPT_STATE is written from ADDR_ZERO_POINT, which is
 * the THIRD function seen to do that -- Type2ActionB and PointActionA are the
 * others -- and orig.h already records that field as unresolved because its
 * two writers put a POINT there and its two readers compare it as an int32.
 * This one does not settle it; it is another writer of a zero.
 *
 * THE UNIT IS DESTROYED AT THE END. DestroyByType runs on it after the
 * broadcast, so what rides in the vehicle is a uid in a list and not a live
 * object. Worth knowing before reading the occupant list as a list of things
 * that still exist.
 *
 * The broadcast is gated on the VEHICLE's army rather than the unit's, and
 * the selection move -- deselect the unit, select the vehicle -- happens only
 * when the unit was selected, which is the ordinary "the thing you were
 * commanding became the thing you are now commanding".
 *
 * ITS ARGUMENTS WERE THE WRONG WAY ROUND FOR AS LONG AS IT EXISTED, and every
 * field access in the body was therefore on the other object. The signature
 * was taken from orig.h's `void(vehicle, unit)`, which was a guess, and the
 * body was then written consistently WITH that guess -- so nothing inside it
 * looked wrong and the compiler had nothing to say.
 *
 * The original settles it in two instructions: `mov edi, [esp+0xC]` is the
 * SECOND argument and everything on it is the vehicle's -- +0x53C against
 * VEHICLE_OFF_SEATS, +0x538 for the list, +0x10 for the army the broadcast is
 * gated on -- while `mov esi, [esp+0xC]` one push later is the FIRST and
 * everything on it is the unit's: OBJ_OFF_RIDING, OBJ_OFF_SARGE,
 * OBJ_OFF_UID_56C, and DestroyByType at the end.
 *
 * ALL THREE CALLERS ARE ORIGINAL, so they pass (unit, vehicle) and our code
 * read them as (vehicle, unit): the seat check ran on the unit, the boarding
 * uid went into the vehicle. Found by reading a caller -- 0x0044AD40 hands it
 * the object it was given and the type-3 it looked up, in that order -- and
 * confirmed against 0x004062B0, whose first argument it reads
 * OBJ_OFF_SOLDIER_KIND from.
 *
 * NO A/B COULD HAVE SEEN IT. Boarding a vehicle is in the cold family this
 * file's CLAUDE.md entry now names in one place: nothing in any configuration
 * shoots, dies or gets into anything. Fixed by swapping the two parameter
 * names, which is the whole change -- `v` and `u` are used consistently
 * throughout, so binding them to the right arguments makes every line right
 * at once.
 */
void __cdecl EnterVehicle(void *unit, void *vehicle)
{
    uint8_t  *v = (uint8_t *)vehicle;
    uint8_t  *u = (uint8_t *)unit;
    uint32_t  uid = ((const AM2_Object *)unit)->uid;
    int32_t   n;

    if (*(const int32_t *)(v + VEHICLE_OFF_PTR_LIST + 4)
            >= *(const int32_t *)(v + VEHICLE_OFF_SEATS))
        return;

    *(uint32_t *)(u + OBJ_OFF_RIDING) = ((const AM2_Object *)vehicle)->uid;
    PtrListPush(v + VEHICLE_OFF_PTR_LIST, (void *)(uintptr_t)uid);

    n = *(const int32_t *)(v + VEHICLE_OFF_PTR_LIST + 4);
    if (*(const int32_t *)(u + OBJ_OFF_SARGE) && n > 1) {
        /* Re-read between the two stores, as the original does. */
        (*(uint32_t **)(v + VEHICLE_OFF_PTR_LIST + 8))[n - 1] =
            (*(uint32_t *const *)(v + VEHICLE_OFF_PTR_LIST + 8))[0];
        (*(uint32_t **)(v + VEHICLE_OFF_PTR_LIST + 8))[0] =
            ((const AM2_Object *)unit)->uid;
    }

    *(uint32_t *)(u + OBJ_OFF_SCRIPT_STATE) =
        *(const uint32_t *)AM2_IMAGE(ADDR_ZERO_POINT);
    *(uint32_t *)(u + OBJ_OFF_UID_56C) = 0;

    if (*(const uint32_t *)(u + OBJ_OFF_FLAGS) & OBJ_FLAG_SELECTED) {
        DeselectUnit(u);
        SelectUnit(v);
    }

    if (CommMustBroadcast((void *)AM2_IMAGE(ADDR_COMM_OBJECT),
                          (int16_t)*(const int8_t *)(v + OBJ_OFF_ARMY)))
        orig_send_vehicle_enter(v, u);

    DestroyByType(u);
}

/* 0x0045AAC0, one caller. Put a unit aboard a vehicle.
 *
 * TWO HALVES OF ONE RELATIONSHIP IN ONE FUNCTION, which is what makes the two
 * field names agree rather than each being a guess: the unit's OBJ_OFF_RIDING
 * takes the vehicle's uid, and the unit's uid is pushed onto the vehicle's
 * VEHICLE_OFF_PTR_LIST. Both names were already in orig.h from separate
 * readings -- BlockWeightTroops reads RIDING to decide whether a trooper
 * blocks the unit the player is driving -- and this is the writer that ties
 * them together.
 *
 * The unit is looked up by uid and the result is dereferenced WITHOUT a null
 * test, so a uid that no longer resolves faults here. Reproduced; its one
 * caller has the object in hand already.
 *
 * The list push is PtrListPush on the vehicle's own record, addressed as
 * `vehicle + 0x538` -- the vehicle IS the record, not a pointer to one.
 *
 * MEASURED AT 0. Nothing boards a vehicle on any drive here -- Boot Camp has
 * none to board -- and its one caller is the original's, so the counter is not
 * blind. Verified by reading, and the reading is stronger than most because
 * both field names came from elsewhere and this is where they meet.
 */
void __cdecl BoardVehicle(uint32_t uid, void *vehicle)
{
    uint8_t *unit = (uint8_t *)LookupByUID(uid);

    *(uint32_t *)(unit + OBJ_OFF_RIDING) =
        ((const AM2_Object *)vehicle)->uid;

    PtrListPush((uint8_t *)vehicle + VEHICLE_OFF_PTR_LIST,
                (void *)(uintptr_t)uid);
}

/* 0x00429420, one caller and it is on the level-load path. Reset the item
 * registry, then seed uid counters.
 *
 * IT SEEDS FIVE OF THE EIGHT. ADDR_UID_COUNTERS is uint32_t[8] -- a uid is
 * (owner << 29) | counter, so eight owners -- and this writes 1000 into the
 * first five and leaves 5, 6 and 7 untouched. Five is the four armies plus the
 * neutral one, which is the same five ObjsAreAllied and CommArmyOfSlot treat
 * as real; whatever owners 5..7 are for, a level load does not reset them.
 *
 * The 1000 is written once into a register and stored five times, which is
 * what makes it obvious the five are the same value rather than a coincidence
 * of five separate constants.
 *
 * STARTING AT 1000 RATHER THAN 0 is worth noticing: it means no uid a level
 * ever hands out has a counter below 1000, so a low uid in a save or a packet
 * did not come from here.
 *
 * Runs once on a driven Boot Camp mission, at the level load. ItemsReset drops
 * to 0 on the same run, which is the ordinary blind spot -- this calls it by
 * name and it had no other caller.
 */
void __cdecl ResetItemsAndUids(void)
{
    int32_t i;

    ItemsReset();

    for (i = 0; i < AM2_UID_ARMY_OWNERS; i++)
        ((uint32_t *)(uintptr_t)ADDR_UID_COUNTERS)[i] = AM2_UID_COUNTER_START;
}

/* 0x0045F2D0, one caller. HAS THIS WEAPON RECHARGED -- the difference between
 * the sarge panel's selected-slot flag being 1 and being 2.
 *
 * Two early exits and the second is the interesting one. A null item answers
 * 0 outright; a null TYPE record answers by returning the null it just
 * loaded, which is the same 0 by a different route. Reproduced rather than
 * folded into one `return 0`, because the two are not the same instruction
 * and nothing here establishes they were meant to be.
 *
 * The comparison is unsigned and STRICT: `cooldown < elapsed`, via
 * cmp/sbb/neg. Exactly at the cooldown the weapon is not yet ready.
 */
int32_t __cdecl ItemIsReady(const void *item)
{
    const uint8_t *self = (const uint8_t *)item;
    const uint8_t *type;
    uint32_t       elapsed;

    if (!self)
        return 0;

    type = *(const uint8_t *const *)(self + OBJ_OFF_FIELD_C0);
    if (!type)
        return 0;

    elapsed = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
              - *(const uint32_t *)(self + ITEM_OFF_LAST_USE);

    return *(const uint32_t *)(type + ITEMTYPE_OFF_COOLDOWN) < elapsed;
}

/* 0x004600E0, three instructions and no bounds check at all: the caller is
 * expected to have a real type in hand. The sarge panel's tooltip is the only
 * one there is.
 *
 * The table holds IMAGE addresses, so both the table and what it yields are
 * read through the slide -- zero in the game, and the reason AM2_IMAGE exists
 * at all. */
const char *__cdecl ItemTypeName(uint32_t kind)
{
    const char *const *names = (const char *const *)AM2_IMAGE(ADDR_ITEM_TYPE_NAMES);

    return names[kind];
}

/* Type2ActionAll -- original 0x00417AB0, one caller.
 *
 * Walk a list of uids, drop the entries that no longer resolve, and run
 * ADDR_TYPE2_ACTION_A on every live type 2 that is not destroyed.
 *
 * THIRD LOOP IN THIS TREE THAT DOES NOT ADVANCE OVER A REMOVAL, after
 * DrawSelection and CommReopenSession. The unresolved arm jumps past the
 * increment, so the entry that shifts down into the slot is looked at next --
 * and the bound is re-read from the list every iteration, which is what makes
 * that safe. The other two have the same shape and this one makes it a
 * pattern rather than a pair.
 *
 * The list is a {capacity, count, items} triple, and both the count and the
 * items pointer are re-read inside the loop rather than hoisted -- the
 * removal can move both.
 *
 * Type2ActionA is ours, five hundred lines down in this same file, and is
 * called by name; checkseams caught the orig_ macro that went in first out of
 * habit. Second time in this session.
 */
void __cdecl Type2ActionAll(void)
{
    int32_t i = 0;

    while (i < ((const int32_t *)(uintptr_t)ADDR_TYPE2_ACTION_LIST)[1]) {
        uint8_t *obj = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)((const uint8_t *)(uintptr_t)
                 ADDR_TYPE2_ACTION_LIST + 8))[i]);

        if (!obj) {
            ListRemoveAt((void *)(uintptr_t)ADDR_TYPE2_ACTION_LIST, i);
            continue;   /* no step: the shifted-down entry is next */
        }

        if (!(*(const uint8_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            && ObjIsType2((const AM2_Object *)obj))
            Type2ActionA(obj);

        i++;
    }
}

/* SoldierNameOf -- original 0x004475C0, two callers, both in the HUD.
 *
 * Copy a soldier's personal name into the caller's buffer. Three things have
 * to hold -- an object, a type 2, and a name index inside the table -- and if
 * any of them does not, the buffer is left as an empty string rather than
 * untouched. The clear happens FIRST, before the object is even tested, so a
 * caller that ignores the failure prints nothing rather than the last name.
 *
 * The names are the team's own: "D. DuBois", "J. Wildblood", "One Eye". This
 * is the table TakeSoldierName hands indices out of, and the pair is what
 * settled the record layout -- the name is at +0 and the taken flag at +4, and
 * ADDR_SOLDIER_NAMES had been pointing at the flag. Two functions indexing one
 * table from different offsets is how that kind of error shows itself.
 *
 * The bound is `0 <= n < 62` with the low half a SIGNED test, so a negative
 * index is rejected rather than wrapping -- which matters, because the field
 * is an int32 and nothing guarantees it was ever assigned.
 *
 * The original inlines the copy as strlen-then-rep-movs, which is strcpy, and
 * is written as one. Unbounded, exactly as the original: both callers pass a
 * field of the HUD structure and the longest name is thirteen characters.
 */
void __cdecl SoldierNameOf(char *out, const void *obj)
{
    int32_t n;

    out[0] = '\0';

    if (!obj || !ObjIsType2((const AM2_Object *)obj))
        return;

    n = *(const int32_t *)((const uint8_t *)obj + OBJ_OFF_NAME_INDEX);
    if (n < 0 || (uint32_t)n >= AM2_SOLDIER_NAMES)
        return;

    strcpy(out, *(const char *const *)
        ((const uint8_t *)AM2_IMAGE(ADDR_SOLDIER_NAMES)
         + (size_t)n * AM2_SOLDIER_NAME_BYTES + SOLDIER_NAME_OFF_NAME));
}

/* AwardOwnArmyXp -- original 0x00417B10, one caller, at the tail of it.
 *
 * Award 300 experience to every live type 2 the player's own army owns, and
 * drop the uids that no longer resolve on the way past.
 *
 * FOURTH LOOP IN THIS TREE THAT DOES NOT ADVANCE OVER A REMOVAL, after
 * DrawSelection, CommReopenSession and Type2ActionAll. The unresolved arm
 * jumps past the increment; the bound is re-read from the list at the bottom
 * of every iteration, so the entry that shifts down is seen next.
 *
 * IT IS SLOT 0 AND NOT THE PLAYER'S ARMY. ADDR_ARMY_OBJ_LISTS is one list per
 * comm slot and the original indexes element 0 with no lookup at all, where
 * every other walker in this tree resolves a slot first. In single player
 * those are the same thing; in a multiplayer game they need not be, and this
 * is reproduced rather than corrected.
 *
 * The flag test is bit 2 of OBJ_OFF_FLAGS -- destroyed -- and it is tested
 * before the type is, so a destroyed non-type-2 costs one test rather than
 * two. Immaterial, and the order is the original's.
 *
 * The removal goes through the original's thiscall list helper, as every other
 * caller of it in this tree does.
 */
void __cdecl AwardOwnArmyXp(void)
{
    void   *list = ((void **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)[0];
    int32_t i    = 0;

    while (i < *(const int32_t *)((const uint8_t *)list + LIST_OFF_COUNT)) {
        uint8_t *obj = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)((const uint8_t *)list
                                        + LIST_OFF_UIDS))[i]);

        if (!obj) {
            ListRemoveAt(list, i);
            continue;               /* no step: the shifted-down entry is next */
        }

        if (!(*(const uint8_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            && ObjIsType2((const AM2_Object *)obj))
            Type238Action(obj, AM2_ARMY_XP_AWARD);

        i++;
    }
}

/* WeaponClassOf -- original 0x0042AAE0, one caller, which stores the answer
 * into a word field of a record it is packing.
 *
 * Classify a weapon by uid. Nothing there, not a type 4, or a kind outside
 * 2..5 all answer 0; the four that are in range answer a small code.
 *
 * THE JUMP TABLE'S ARMS ARE NOT IN THE ORDER THEY ARE LAID OUT. Reading the
 * bodies top to bottom gives 1, 2, 3, 4 for kinds 2, 3, 4, 5, which is wrong
 * for two of the four: the table at 0x0042AB38 dispatches to the arms in the
 * order 2, 3, 1, 4. This is the same trap the state-2 sub-state table set,
 * written up in CLAUDE.md, and it is worth one look at the table every time --
 * the linker's layout is not the switch.
 *
 * The kind is the first dword of the record OBJ_OFF_FIELD_C0 points at, which
 * is the same record SelectInventorySlot indexes ADDR_WEAPON_HANDLERS with.
 * So the codes here are a second, smaller classification over the same field.
 */
int32_t __cdecl WeaponClassOf(uint32_t uid)
{
    const uint8_t *obj = (const uint8_t *)LookupByUID(uid);

    if (!obj || !ObjIsType4((const AM2_Object *)obj))
        return 0;

    switch (**(const int32_t *const *)(obj + OBJ_OFF_FIELD_C0)) {
    case 2:  return 2;
    case 3:  return 3;
    case 4:  return 1;
    case 5:  return 4;
    default: return 0;
    }
}

/* DamageItemChain -- original 0x00435650, one caller, and that caller is
 * DamageItem itself, so the two are mutually recursive.
 *
 * Damage an item, then damage every item in the chain hanging off it:
 * OBJ_OFF_CHAIN_UID gives the first, and each link's OBJ_OFF_CHAIN_NEXT_UID
 * gives the one after. Only types 1 and 4 are followed; anything else ends the
 * walk rather than being skipped.
 *
 * THE RECURSION IS BOUNDED BY DamageItem's SIXTH ARGUMENT, NOT BY THIS
 * FUNCTION. Every call from here passes 1, and DamageItem's first test is on
 * that argument -- non-zero takes the arm that does NOT come back here. So the
 * chain is walked once, iteratively, and the mutual recursion is one level
 * deep by construction. Reproduced exactly; changing the constant would make
 * it unbounded.
 *
 * The chain walk reuses the object variable, so the first link's own chain
 * head is never consulted -- it is the NEXT pointer that continues, which is
 * why a linked item's OBJ_OFF_CHAIN_UID does not start a second walk.
 */
void __cdecl DamageItemChain(void *obj, int32_t amount, int32_t d,
                             int32_t kind, uint32_t attacker)
{
    uint8_t *o = (uint8_t *)obj;
    uint32_t uid;

    DamageItem(o, amount, d, kind, attacker, 1);

    for (uid = *(const uint32_t *)(o + OBJ_OFF_CHAIN_UID); uid;
         uid = *(const uint32_t *)(o + OBJ_OFF_CHAIN_NEXT_UID)) {
        int32_t type;

        o = (uint8_t *)LookupByUID(uid);
        if (!o)
            return;

        type = *(const int32_t *)o;
        if (type != 1 && type != 4)
            return;

        DamageItem(o, amount, d, kind, attacker, 1);
    }
}

/* ObjOverlayY -- original 0x0044A3C0, one caller, which ADDS the answer to a
 * y coordinate.
 *
 * The negated SPR_OFF_OVY of the sprite the object's FIRST row is currently
 * showing -- an anchor offset, turned from "how far down the sprite the point
 * is" into "how far up from the point the sprite must go".
 *
 * IT INDEXES THE CELL ARRAY BY DIRECTION ALONE. The cells are laid out
 * direction-major, `frames * directions` of them, so cells[dir] is frame `dir`
 * of direction 0 rather than frame 0 of direction `dir` -- those coincide only
 * when the animation has one frame. Reproduced as written: whether the
 * original meant `dir * frames` is not something the code can be asked, and a
 * "fix" here would change where things are drawn.
 *
 * The direction bits reach RoundTo8 as a dword whose upper three bytes are
 * the animation POINTER's, because the original loads them with a byte move
 * into a register it has just used for the pointer. RoundTo8 masks, so it
 * cannot matter; the same shape as RandomPointAhead's heading.
 *
 * Three ways out answer 0, and the third is a fall-through rather than a
 * written return: with no animation playing the original simply leaves the
 * register holding the null it just tested. Same value, and worth saying,
 * because a reader looking for a `return 0` will not find one.
 */
int32_t __cdecl ObjOverlayY(const void *obj)
{
    const uint8_t   *o = (const uint8_t *)obj;
    const uint8_t   *row;
    const AM2_Anim  *anim;
    int32_t          dir;
    int32_t          sprite;

    if (!o)
        return 0;
    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) < 1)
        return 0;

    row  = *(const uint8_t *const *)(o + OBJ_OFF_ROWS);
    anim = *(const AM2_Anim *const *)(row + ROW_OFF_ANIM_PLAYING);
    if (!anim)
        return 0;

    dir    = RoundTo8(*(const uint8_t *)(o + OBJ_OFF_FACING),
                      anim->directionBits) & 0xFF;
    sprite = anim->cells[dir].sprite;

    return -(int32_t)*(const int16_t *)
        ((const uint8_t *)kSpriteList[sprite] + SPR_OFF_OVY);
}

/* SlotBandHeading -- original 0x00456E20, one caller.
 *
 * Split a slot number into three things the caller wants at once: which of
 * three BANDS it falls in, its index WITHIN that band, and a heading byte off
 * ADDR_SLOT_HEADINGS. Four arguments, three of them out-pointers.
 *
 * The bands are 2, 3 and 4 for slot <= 24, <= 56 and above, and each has its
 * own base -- 9, 25 and 57. The index is `base + (slot - base) / 2`, so two
 * consecutive slots share an index and the pair is told apart by the third
 * output: EVEN adds 16 to the table byte and ODD subtracts 16. That is a
 * heading, and the two members of a pair face 32 apart.
 *
 * `*band = 0` FIRST, THEN OVERWRITTEN ON EVERY PATH. There is no arm that
 * leaves it at zero, so the initial store is dead -- reproduced because it is
 * one instruction and removing it invites the reader to ask which arm needs
 * it.
 *
 * THE PARITY IS TESTED ON THE SLOT, NOT ON THE INDEX. `slot & 1` after the
 * halving, not `index & 1`; the two disagree for exactly the values this
 * function exists to tell apart.
 *
 * NOTHING GUARDS THE BOTTOM. A slot below 9 gives a negative `(slot - 9) / 2`
 * and indexes ADDR_SLOT_HEADINGS backwards. The division is signed -- MSVC's
 * `cdq; sub; sar 1` -- so it truncates toward zero rather than flooring, which
 * is the ordinary C division and is written as one.
 */
void __cdecl SlotBandHeading(int32_t slot, int32_t *band, int32_t *index,
                             uint8_t *heading)
{
    int32_t base;
    int32_t i;
    uint8_t h;

    *band = 0;

    if (slot <= 24) {
        *band = 2;
        base  = 9;
    } else if (slot <= 56) {
        *band = 3;
        base  = 25;
    } else {
        *band = 4;
        base  = 57;
    }

    i = base + (slot - base) / 2;
    *index = i;

    h = ((const uint8_t *)(uintptr_t)ADDR_SLOT_HEADINGS)[i];
    *heading = (uint8_t)((slot & 1) ? h - 0x10 : h + 0x10);
}

/* SelectBestWeapon -- original 0x004069B0, one caller.
 *
 * Walk the unit's six inventory slots, keep the one whose weapon has the
 * highest ADDR_MAP_CODE, record that slot in UNIT_OFF_INVENTORY_SEL and apply
 * the soldier kind the winning code implies.
 *
 * SLOT 0 IS NOT GUARDED AND THE OTHER FIVE ARE. The first is read, looked up
 * and dereferenced unconditionally; slots 1..5 are skipped when the uid is
 * zero. So an empty slot 0 goes through WeaponByUid, which answers NULL having
 * complained, and the dereference that follows takes the process down. The
 * one caller has just put something there. Reproduced -- adding the guard
 * would be inventing a behaviour, and the crash is the original's.
 *
 * It uses WeaponByUid rather than HeldWeaponCode's lookup, and the difference
 * matters: WeaponByUid LOGS for a uid that is not a weapon and then answers
 * NULL, where the other simply answers 0. Same shape, different noise, and
 * only one of them is safe to call speculatively.
 *
 * MapCode is ours, in misc.cpp, and is called by name -- checkseams caught the
 * orig_ macro that went in first out of habit. Third time this session, and
 * the habit is worth naming: an `orig_` for a callee is the reflex, and the
 * question to ask before writing one is whether the callee is already ours.
 *
 * THE COMPARISON IS STRICTLY GREATER, so the LOWEST slot wins a tie. With six
 * slots holding the same weapon the answer is slot 0.
 *
 * The winning weapon is looked up a THIRD time at the end rather than the code
 * being carried out of the loop. Nothing can have changed in between; it is
 * the original's and it is one lookup, so it is written as one.
 */
void __cdecl SelectBestWeapon(void *unit)
{
    uint8_t *u = (uint8_t *)unit;
    int32_t  best;
    int32_t  bestSlot = 0;
    int32_t  i;

    best = MapCode(**(const int32_t *const *)
        ((const uint8_t *)WeaponByUid(
             *(const uint32_t *)(u + UNIT_OFF_INVENTORY)) + OBJ_OFF_FIELD_C0));

    for (i = 1; i < AM2_INVENTORY_SLOTS; i++) {
        uint32_t uid = *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                           + (size_t)i * 4);
        int32_t  code;

        if (!uid)
            continue;

        code = MapCode(**(const int32_t *const *)
            ((const uint8_t *)WeaponByUid(uid) + OBJ_OFF_FIELD_C0));

        if (code > best) {
            bestSlot = i;
            best     = code;
        }
    }

    *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = bestSlot;

    SoldierKindForWeapon(u, (uint32_t)**(const int32_t *const *)
        ((const uint8_t *)WeaponByUid(
             *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                 + (size_t)bestSlot * 4)) + OBJ_OFF_FIELD_C0));
}

/* SetObjField530 -- original 0x0043CD40, two callers.
 *
 * Move an object to a new OBJ_OFF_FIELD_530 and put its first row on the
 * animation frame that goes with it. Named for the field, which is itself
 * named for its offset: nothing here says what the states MEAN, only how they
 * are changed.
 *
 * THE GATE IS "THE CURRENT ANIMATION HAS FINISHED", AND IT IS SKIPPED rather
 * than tested when two conditions hold together -- the transition is one the
 * table below allows to interrupt, and the row's ROW_OFF_FIELD_3C is
 * non-zero. Otherwise RowAnimFinished has to agree or nothing happens at all,
 * including the field write. So a caller cannot assume the state changed.
 *
 * WHICH TRANSITIONS MAY INTERRUPT IS TWO TESTS, NOT ONE, and they are not
 * symmetric. Leaving a state in 3..6 does NOT allow it; entering one in 5..6
 * does, and that second test overrides the first. So 4 -> 5 interrupts and
 * 4 -> 3 does not, though both are inside the same band. Written as the two
 * separate assignments the original makes, because collapsing them into one
 * condition loses the override.
 *
 * NOTHING BOUNDS THE NEW STATE against the seven-entry frame table. An eighth
 * state would read the string data that follows it and hand the result to
 * SetAnimFrame as a frame id. Both callers pass a literal.
 *
 * State 4 alone stamps a deadline 200 ms out into OBJ_OFF_DEADLINE_58. It is
 * the last thing done and only on that state.
 *
 * An unchanged state returns at once -- before the gate, so re-asserting the
 * state an object is already in costs nothing and cannot restart its
 * animation.
 */
void __cdecl SetObjField530(void *obj, int32_t state)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  cur = *(const int32_t *)(o + OBJ_OFF_FIELD_530);
    uint8_t *row;
    int32_t  mayInterrupt;

    if (cur == state)
        return;

    mayInterrupt = (cur >= 3 && cur <= 6) ? 0 : 1;
    if (state >= 5 && state <= 6)
        mayInterrupt = 1;

    row = *(uint8_t **)(o + OBJ_OFF_ROWS);

    if (!(mayInterrupt && *(const int16_t *)(row + ROW_OFF_FIELD_3C) != 0)
        && !RowAnimFinished(row))
        return;

    *(int32_t *)(o + OBJ_OFF_FIELD_530) = state;

    SetAnimFrame(row,
                 (int16_t)((const int32_t *)(uintptr_t)
                     ADDR_FIELD_530_FRAMES)[state],
                 0);

    if (state == 4)
        *(int32_t *)(o + OBJ_OFF_DEADLINE_58) =
            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
            + AM2_FIELD_530_DELAY_MS;
}

/* JitterFacing -- original 0x00449F40, three callers.
 *
 * Wobble a facing by `rand() % 5 - 2` and keep the wobble only if it rounds
 * into the same direction bucket as the original -- so a unit's aim drifts
 * within the sprite it is already drawn with, and never far enough to change
 * which sprite that is.
 *
 * FOR EVERY OBJECT WHOSE RECORD KIND IS NOT 3, THAT TEST IS VACUOUS. The
 * bucket width comes from the kind: 3 for kind 3, and 0x20 otherwise. RoundTo8
 * masks its second argument to a byte and then shifts by `7 - b` and `8 - b`,
 * which x86 masks to five bits -- so b = 0x20 gives `1 << 7` and `>> 8` of a
 * value already masked to a byte, i.e. ZERO for every input. Both sides of the
 * comparison are 0, they always agree, and the wobble is always kept.
 *
 * That is not a reading of undefined behaviour taken on trust. tests/vectors.h
 * carries RoundTo8 vectors with `bits` of 0x2A40, 0x7FFFFFFF and 0xFFFFFFFE --
 * every one of them on the same masked-shift path -- and `make selftest`
 * passes all 6,852 against the original. The behaviour at 0x20 is measured, on
 * this target, by the harness that exists for exactly this.
 *
 * THE JITTER IS COMPUTED BEFORE THE KIND IS LOOKED AT, so rand() is consumed
 * on every call whether the result is used or not. Anything downstream that
 * depends on the sequence would notice a version that returned early.
 *
 * `rand() % 5` is a SIGNED remainder of the image's own LCG, which answers
 * 0..0x7FFF, so it is 0..4 and the bias of 2 makes the wobble -2..+2. The
 * arithmetic is done in a byte and wraps, which is what a facing wants.
 */
uint8_t __cdecl JitterFacing(void *obj, uint8_t facing)
{
    uint8_t jitter = (uint8_t)(orig_rand() % AM2_JITTER_SPREAD
                               + facing - AM2_JITTER_BIAS);
    uint32_t bits;

    bits = (**(const int32_t *const *)((const uint8_t *)obj
                                       + OBJ_OFF_FIELD_C0) == 3)
           ? AM2_JITTER_BITS_3 : AM2_JITTER_BITS_OTHER;

    if (RoundTo8(facing, bits) != RoundTo8(jitter, bits))
        return facing;

    return jitter;
}

/* The row test SetKindFrames applies twice, written once. All three
 * conditions must hold for the row to be left alone: an animation is
 * playing, ROW_OFF_FIELD_3C is zero, and the cell index is short of the
 * last. This is a helper of ours -- the original inlines it both times --
 * and it is one only because writing the conjunction twice is how a
 * polarity gets flipped in one copy. */
static int32_t RowMidAnimation(const uint8_t *row)
{
    const AM2_Anim *anim =
        *(const AM2_Anim *const *)(row + ROW_OFF_ANIM_PLAYING);

    return anim
        && *(const int16_t *)(row + ROW_OFF_FIELD_3C) == 0
        && (int32_t)*(const uint8_t *)(row + ROW_OFF_CELL)
               < (int32_t)anim->frames - 1;
}

/* HeldWeaponObj -- original 0x00459FE0, two callers.
 *
 * The weapon OBJECT a unit or vehicle is holding, or NULL. Type 2 reads the
 * inventory slot UNIT_OFF_INVENTORY_SEL selects; type 3 reads a single fixed
 * slot, UNIT_OFF_INVENTORY + 4 -- not slot 0, and not a selection. Everything
 * else answers NULL.
 *
 * IT SHARES A functions.tsv ENTRY WITH ObjToAI below it. The two run together
 * into one 144-byte entry, so patching either alone would mark both
 * reconstructed -- the merged-entry inflation CLAUDE.md warns about, met head
 * on. Both are written here for that reason.
 *
 * The type tests are each a `sete` into a register that is then tested, which
 * is MSVC materialising a bool it did not need; written as the comparisons
 * they are.
 *
 * It hands the uid to WeaponByUid, which complains and answers NULL for a uid
 * that is not a weapon -- so an empty slot is not silent here. Both callers
 * accept NULL.
 */
void *__cdecl HeldWeaponObj(const void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;

    if (!o)
        return (void *)0;

    if (*(const int32_t *)o == 2)
        return WeaponByUid(*(const uint32_t *)
            (o + UNIT_OFF_INVENTORY
             + (size_t)*(const int32_t *)(o + UNIT_OFF_INVENTORY_SEL) * 4));

    if (*(const int32_t *)o == 3)
        return WeaponByUid(*(const uint32_t *)(o + UNIT_OFF_INVENTORY + 4));

    return (void *)0;
}

/* ObjToAI -- original 0x0045A030, and its two references are one call site: a
 * `push` of the address, which appears twice because the aligned-dword scan
 * and the instruction stream both find it.
 *
 * Hand a unit over to the AI. It is passed BY ADDRESS to the walker at
 * 0x00457820, which calls it for every object an army owns -- the "left, AI
 * takes over" path.
 *
 * ONLY TYPE 2 IS TOUCHED and the two things it does are independently gated:
 * a unit with health above zero stops firing, and a unit that is Sarge gets
 * stance 6. A dead Sarge therefore gets the stance and not the fire clear,
 * which is the arm a single combined `if` would lose.
 *
 * The health test is `> 0` on an int16, so a negative health -- which this
 * game does produce, since damage is not clamped at zero -- takes the same
 * arm as a dead one.
 */
void __cdecl ObjToAI(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (!o || *(const int32_t *)o != 2)
        return;

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) > 0)
        *(int32_t *)(o + UNIT_OFF_FIRE_ACTIVE) = 0;

    if (*(const int32_t *)(o + OBJ_OFF_SARGE))
        *(int32_t *)(o + OBJ_OFF_AI_MODE) = 6;
}

/* SetKindFrames -- original 0x0045B000, three callers, and the other writer of
 * OBJ_OFF_SOLDIER_KIND beside SetSoldierKind.
 *
 * Set the kind and put the object's first two rows on the frames that go with
 * it -- unless a row is mid-animation, in which case that row is left alone.
 *
 * THE "MID-ANIMATION" TEST IS THREE CONDITIONS AND ALL THREE MUST HOLD to skip
 * the row: there is an animation playing, the row's ROW_OFF_FIELD_3C is zero,
 * and the current cell is short of the last. Any one failing means the frame
 * is set. Written as the single `if` the original branches into, because the
 * three are a conjunction and splitting them invites getting the polarity
 * wrong.
 *
 * THE KIND IS WRITTEN INSIDE THAT GUARD, not before it. A call that arrives
 * while row 0 is mid-animation changes NOTHING -- the field keeps its old
 * value -- and yet the second row is still considered, on its own test. So the
 * two rows can disagree about which kind they are showing.
 *
 * ROW 1 GETS A LITERAL FRAME, NOT A TABLE LOOKUP. Row 0 takes
 * ADDR_KIND_FRAMES[kind] and row 1 takes 0x50 whatever the kind is, so only
 * the first row's appearance depends on it.
 *
 * The early exit is on the kind being unchanged, before anything is read, so
 * re-asserting a kind is free.
 *
 * Nothing bounds the kind against the eight-entry table; past it is string
 * data. All three callers pass a small literal.
 */
void __cdecl SetKindFrames(void *obj, int32_t kind)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *row;

    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == kind)
        return;

    row = *(uint8_t **)(o + OBJ_OFF_ROWS);

    if (!RowMidAnimation(row)) {
        *(int32_t *)(o + OBJ_OFF_SOLDIER_KIND) = kind;
        SetAnimFrame(row,
                     (int16_t)((const int32_t *)(uintptr_t)
                         ADDR_KIND_FRAMES)[kind],
                     0);
    }

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) <= 1)
        return;

    row += AM2_OBJ_ROW_STRIDE;

    if (!RowMidAnimation(row))
        SetAnimFrame(row, AM2_SECOND_ROW_FRAME, 0);
}

/* SpawnRandomBarrage -- original 0x00417890, one caller, and that caller
 * prints "Duck and cover!" one instruction before calling it. So this is a
 * CHEAT's effect: two hundred SpawnAt calls scattered across the view, each
 * one of six kinds, each with its own random delay.
 *
 * The name is from the body. The string names the cheat, not the function, and
 * that distinction is the one CLAUDE.md keeps having to make.
 *
 * THE ORDER OF THE FOUR rand() CALLS IS PART OF THE FUNCTION. They draw from
 * the image's own LCG, so which draw becomes the delay and which becomes the x
 * decides every spawn -- and C does not sequence argument evaluation, so
 * writing them inline as arguments would be free for the compiler to reorder.
 * They are four locals, assigned in the original's order: delay, then kind,
 * then y, then x.
 *
 * THE SPAN IS 620 x 480 FROM THE VIEW ORIGIN, which is the screen and not the
 * map -- so the barrage lands where the player is looking rather than
 * somewhere in the level. 620 rather than 640 is the original's number.
 *
 * The six kinds are a table built on the stack every call rather than a
 * constant in the image; reproduced as a local for the same reason, since a
 * static would be a different object with a different lifetime and nothing
 * here needs one.
 *
 * Seven of SpawnAt's ten arguments are constants here -- an army of 0, a uid
 * of 0, 0xC8, then the delay, then three more zeros -- and what most of them
 * mean is not established. They are passed as the literals they are, through
 * the typedef that was already in this file for the other caller.
 */
void __cdecl SpawnRandomBarrage(void)
{
    const int32_t kinds[AM2_BARRAGE_KINDS] =
        { 0x81, 0x82, 0x8C, 0x78, 0x94, 0x95 };
    int32_t i;

    for (i = 0; i < AM2_BARRAGE_COUNT; i++) {
        int32_t delay = orig_rand() % AM2_BARRAGE_DELAY_MAX;
        int32_t kind  = kinds[orig_rand() % AM2_BARRAGE_KINDS];
        int32_t y     = orig_rand() % AM2_BARRAGE_SPAN_Y
                        + *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y;
        int32_t x     = orig_rand() % AM2_BARRAGE_SPAN_X
                        + *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X;

        orig_spawn_at(x, y, kind, 0, 0, AM2_BARRAGE_ARG6, delay, 0, 0, 0);
    }
}

/* SelectIfOwn -- original 0x00458380, four callers.
 *
 * Select one object, if it passes six tests: it exists, its army is the local
 * player's, its health is not zero, it is not destroyed, its type is 2, 3 or
 * 8, and its type record pointer is null. Anything failing answers 0 without
 * touching the selection at all.
 *
 * CONTROL IS THE ADD-TO-SELECTION MODIFIER, and it is checked LAST -- after
 * every test that can refuse. So a click on someone else's unit with CONTROL
 * held leaves the existing selection alone rather than clearing it, which is
 * what a player expects and is not obviously what the code says until the
 * order is read. Both control keys count.
 *
 * THE HEALTH TEST IS `!= 0`, NOT `> 0`. A unit whose health has gone negative
 * -- which this game produces, since damage is not clamped at zero -- is still
 * selectable here, where ObjToAI's `> 0` treats the same value as dead. Two
 * functions in this file reading one field two ways; reproduced, and worth
 * knowing before assuming either is the house rule.
 *
 * THE TYPE TEST IS 2, 3 OR 8 written as a range and an equality, which is the
 * same set ObjIsTypeIn238 answers for -- but this does NOT call it. The
 * original inlines the comparisons, and it is left inlined: routing it through
 * the accessor would change which counter moves and would be a different
 * function.
 *
 * The last test, on OBJ_OFF_FIELD_94, refuses an object that already has a
 * type record. What that means is not established; the field is named for its
 * offset and this is one more reader of it.
 */
int32_t __cdecl SelectIfOwn(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  type;

    if (!o)
        return 0;
    if (*(const int8_t *)(o + OBJ_OFF_ARMY)
        != (int8_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
        return 0;
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0)
        return 0;
    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return 0;

    type = *(const int32_t *)o;
    if (type < 2 || (type > 3 && type != 8))
        return 0;

    if (*(const void *const *)(o + OBJ_OFF_FIELD_94))
        return 0;

    if (!IsKeyDown(AM2_DIK_LCONTROL) && !IsKeyDown(AM2_DIK_RCONTROL))
        DeselectAll();

    SelectUnit(o);
    return 1;
}

/* ResetType2Fields -- original 0x004572A0, two callers.
 *
 * Clear a type 2's working block and stamp its current facing into the copy at
 * +0xF8. Nothing here is conditional: every write happens on every call.
 *
 * WHAT IT CLEARS WAS ALREADY NAMED, and the offset ratchet is what said so. I
 * added eleven `OBJ_OFF_FIELD_<hex>` names for the block and `checkoffsets`
 * refused six of them, because the four script fields, OBJ_OFF_FOLLOW_UID and
 * the OBJ_OFF_HIT_DIR / OBJ_OFF_HIT_TIME pair were named long ago. Taking the
 * existing names turns eleven unknowns into a function with a plain reading:
 * drop the script binding, the weapon type record, the follow target and the
 * last-hit record, wipe the tail block, and re-stamp the facing.
 *
 * That is the rule "grep for the offset before naming it" earning its keep in
 * the direction that matters -- not preventing a duplicate, but SUPPLYING a
 * meaning the new reader did not have.
 *
 * THE FIVE FIELDS AT +0xB0..+0xC0 ARE CLEARED THROUGH ADDR_ZERO_POINT, not
 * with an immediate, and the original RELOADS that global before each store --
 * five separate `mov eax,[0x005125A0]`. A compiler emits that when it cannot
 * prove the stores do not alias the global, which is the tell that the source
 * assigned a named zero rather than a literal. Reproduced as the five loads it
 * is; they are all zero, so nothing observable turns on it, and writing `= 0`
 * would lose the only evidence of what the source said.
 *
 * OBJ_OFF_HIT_DIR IS A BYTE WHERE ITS NEIGHBOURS ARE DWORDS. Clearing it as a
 * dword would take +0x105..+0x107 with it, which the original leaves alone --
 * and orig.h already records that field as `uint8_t, >= 1`, so the width is
 * corroborated from the writing side as well as from this instruction.
 *
 * The order is the original's and the tail block is cleared in the MIDDLE of
 * it: +0xC4 and +0xCC are zeroed, then 0x103 dwords from +0x118, then the four
 * at +0xFC..+0x108, then the facing copy last. Nothing overlaps, so the order
 * cannot matter -- said because a reader checking for overlap should not have
 * to derive it twice.
 */
void __cdecl ResetType2Fields(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    *(uint32_t *)(o + OBJ_OFF_SCRIPT_ID) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
    *(uint32_t *)(o + OBJ_OFF_SCRIPT_STATE) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
    *(uint32_t *)(o + OBJ_OFF_SCRIPT_FRAME) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
    *(uint32_t *)(o + OBJ_OFF_SCRIPT_NEXT) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
    /* objdump.py reads 0xC0 as the destination point; orig.h names it
     * OBJ_OFF_FIELD_C0, and that is the name used here rather than a second
     * one. Deploying cancels wherever the vehicle was headed. */
    *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

    *(uint32_t *)(o + OBJ_OFF_FOLLOW_UID) = 0;
    *(uint32_t *)(o + OBJ_OFF_TARGET_UID) = 0;

    memset(o + OBJ_OFF_TAIL_BLOCK, 0, AM2_OBJ_TAIL_DWORDS * 4);

    *(uint32_t *)(o + OBJ_OFF_FIELD_FC)  = 0;
    *(uint32_t *)(o + OBJ_OFF_FIELD_100) = 0;
    *(uint8_t  *)(o + OBJ_OFF_HIT_DIR) = 0;
    *(uint32_t *)(o + OBJ_OFF_HIT_TIME) = 0;

    *(uint8_t *)(o + OBJ_OFF_FACING_COPY) =
        *(const uint8_t *)(o + OBJ_OFF_FACING);
}

/* ResetObjOnCof -- original 0x00457220, three callers.
 *
 * Clear an object's last-hit record, give it a random phase, detach it from
 * whatever it was attached to, and hand a non-Sarge kind-3 trooper stance 2.
 *
 * THE WHOLE BODY IS BEHIND "default.cof EXISTS", AND THAT FILE DOES NOT SHIP.
 * ADDR_STATE2_ENTER runs `_findfirst("default.cof")` on entering a level and
 * sets the flag only when it is found; the GOG install has no `.cof` at all,
 * so this function returns at its first instruction for the whole of every run
 * this project can drive. It is reconstructed because it is game code below
 * the CRT line, not because anything here exercises it -- **verified by
 * reading, and no A/B can say otherwise**, which is worth stating plainly
 * rather than letting a clean suite imply coverage it does not have.
 *
 * It is not the copy-protection case either: nothing was patched out of the
 * binary, the condition is simply never satisfied by the shipped data.
 *
 * THERE IS A PROBE AND IT WAS NOT TAKEN. Creating an empty `default.cof` in
 * the game directory would set the flag and make this run, which would turn
 * "verified by reading" into a measurement. It is left undone deliberately:
 * 0x00457070 does not merely test for that file, it OPENS and parses it, so an
 * empty one feeds a parser nothing and the outcome is unknown -- and it means
 * writing into the shipped game directory to find out. Worth doing on a
 * throwaway copy of the install; not worth doing on this one.
 *
 * THE NULL CHECK COMES AFTER FOUR STORES THROUGH THE POINTER. The original
 * writes +0xD0, +0x108, +0x104, +0x100 and +0xFC, calls ObjAttachTo, and only
 * then tests the object against zero -- so a null argument has already
 * faulted. That test is dead, and it is reproduced as written because the
 * order is the evidence: it says the second half was added later, or copied
 * from somewhere the pointer really could be null.
 *
 * The random phase is `rand() % 500` from the image's own LCG, so a caller
 * that reconstructed it with libc would desynchronise every later draw.
 *
 * It clears the same +0x100 / +0x104 / +0x108 group ResetType2Fields does,
 * which is the only reason those three are believed to be one record rather
 * than three unrelated slots.
 *
 * The last arm wants a type 2 whose soldier kind is 3 and which is NOT Sarge.
 * ObjToAI gives Sarge stance 6 on the other path, so the two together cover
 * both, from opposite directions.
 */
void __cdecl ResetObjOnCof(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (!*(const int32_t *)(uintptr_t)ADDR_HAVE_DEFAULT_COF)
        return;

    *(uint32_t *)(o + OBJ_OFF_DEADLINE_D0) = 0;
    *(uint32_t *)(o + OBJ_OFF_HIT_TIME)    = 0;
    *(uint8_t  *)(o + OBJ_OFF_HIT_DIR)     = 0;
    *(uint32_t *)(o + OBJ_OFF_FIELD_100)   = 0;
    *(int32_t  *)(o + OBJ_OFF_FIELD_FC)    = orig_rand() % AM2_COF_PHASE_MAX;

    ObjAttachTo(o, (void *)0);

    if (!o)                     /* dead: five stores above already used it */
        return;
    if (*(const int32_t *)o != 2)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) != 3)
        return;
    if (*(const int32_t *)(o + OBJ_OFF_SARGE))
        return;

    *(int32_t *)(o + OBJ_OFF_AI_MODE) = 2;
}

/* SelectRankedWeapon -- original 0x00406AB0, and SelectBestWeapon's twin. Same
 * six slots, same unguarded slot 0, same strictly-greater comparison so the
 * LOWEST slot wins a tie. Two differences, and both matter.
 *
 * IT SCORES WITH MapCode18To28, NOT MapCode. Five weapon codes rank at all
 * and the order is a lookup rather than a formula, so "best" here is a
 * different ordering from the other function's -- the two can disagree about
 * which of two weapons to hold.
 *
 * That scorer was nearly reconstructed a second time while this was being
 * written, under the name WeaponRank, because its address was not grepped
 * first. checkpatches refused the build. **Before reconstructing anything,
 * grep the tree for the ADDRESS as well as for the name** -- fourth time.
 *
 * AND IT ONLY WRITES THE SLOT WHEN THE WINNER IS NOT SLOT 0. `test ebp,ebp;
 * jle` skips the store, so a unit whose best weapon is already the first one
 * keeps whatever selection it had rather than being moved to slot 0. It also
 * does NOT call SoldierKindForWeapon afterwards, where SelectBestWeapon
 * always does. So this one changes the selection and nothing else.
 *
 * Written out beside its twin rather than sharing a helper, for the reason
 * AddLevelRecord and AddNameRecord are: the original has two functions, and
 * the duplication is what makes a later divergence visible.
 */
void __cdecl SelectRankedWeapon(void *unit)
{
    uint8_t *u = (uint8_t *)unit;
    int32_t  best;
    int32_t  bestSlot = 0;
    int32_t  i;

    best = MapCode18To28(**(const int32_t *const *)
        ((const uint8_t *)WeaponByUid(
             *(const uint32_t *)(u + UNIT_OFF_INVENTORY)) + OBJ_OFF_FIELD_C0));

    for (i = 1; i < AM2_INVENTORY_SLOTS; i++) {
        uint32_t uid = *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                           + (size_t)i * 4);
        int32_t  rank;

        if (!uid)
            continue;

        rank = MapCode18To28(**(const int32_t *const *)
            ((const uint8_t *)WeaponByUid(uid) + OBJ_OFF_FIELD_C0));

        if (rank > best) {
            bestSlot = i;
            best     = rank;
        }
    }

    if (bestSlot > 0)
        *(int32_t *)(u + UNIT_OFF_INVENTORY_SEL) = bestSlot;
}

/* ObjRowsMaskAt -- original 0x00435440, one caller.
 *
 * Is a point on a solid pixel of the object's first row? The rectangle is
 * tested first and the sprite's own mask second, so a point inside the box but
 * on a transparent pixel answers 0 -- which is the whole reason this exists
 * rather than a rectangle test at the call site.
 *
 * IT TESTS THE FIRST ROW ONLY, AND orig.h SAID OTHERWISE. That macro's comment
 * described it as walking OBJ_OFF_ROWS and testing every row; there is one
 * load of `rows`, one `[rows + ROW_OFF_SPRITE]`, and no loop. The row count is
 * read only to refuse an object that has none. The description was written
 * from the name and the name was written from the call site -- corrected by
 * reconstructing it, which is the third time that order has been the fix.
 *
 * The name is kept as the established one rather than renamed to fit the
 * reading: the address already had it, and the alias ratchet refused the
 * second. What changed is the comment.
 *
 * WHICH MASK READER IS USED COMES FROM THE SPRITE'S FORMAT, and the mapping is
 * sprite.h's: format 1 has the 32-bit row table and 2 and 3 the 16-bit one.
 * Anything else -- 0, or above 3 -- answers **1**, a HIT, without consulting
 * any mask at all. That is the arm worth noticing: an unrecognised format is
 * treated as solid everywhere, not as empty.
 *
 * Format 0 means the image is a DirectDraw SURFACE rather than a software
 * mask, so there is nothing to read per pixel and answering "solid" is the
 * only thing it could do without a lock. Reproduced, and it is why the four
 * early exits answer 0 while this one answers 1: they mean "no sprite to
 * test", and this means "cannot test, assume yes".
 *
 * The point is made relative to the row's rectangle before the mask sees it,
 * and the two subtractions read the rectangle's left and top from DIFFERENT
 * expressions in the original -- `[ebp]` after `lea ebp,[ebx+0xC]` for the
 * left, and `[ebx+0x10]` for the top. Same two fields either way.
 */
int32_t __cdecl ObjRowsMaskAt(void *obj, const void *ptv)
{
    const AM2_Point *pt = (const AM2_Point *)ptv;
    uint8_t       *o = (uint8_t *)obj;
    uint8_t       *row;
    const uint8_t *spr;
    int32_t        x;
    int32_t        y;

    if (!o)
        return 0;
    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) < 1)
        return 0;

    row = *(uint8_t **)(o + OBJ_OFF_ROWS);
    spr = *(const uint8_t *const *)(row + ROW_OFF_SPRITE);
    if (!spr)
        return 0;
    if (!*(const void *const *)(spr + SPR_OFF_IMAGE))
        return 0;

    if (!PointInRect((const AM2_Rect *)(row + ROW_OFF_RECT), pt))
        return 0;

    x = pt->x - *(const int32_t *)(row + ROW_OFF_RECT);
    y = pt->y - *(const int32_t *)(row + ROW_OFF_RECT + 4);

    switch (*(const int32_t *)(spr + SPR_OFF_FORMAT)) {
    case 1:
        return MaskPixelSolid32((uint32_t)x, (uint32_t)y,
                                *(const void *const *)(spr + SPR_OFF_IMAGE));
    case 2:
    case 3:
        return MaskPixelSolid((uint32_t)x, (uint32_t)y,
                              *(const void *const *)(spr + SPR_OFF_IMAGE));
    default:
        return 1;               /* no software mask: assume solid */
    }
}

/* RemapInventoryUids -- original 0x004276F0, one caller.
 *
 * The savegame uid fixup. Walk every registered object; for each one carrying
 * OBJ_FLAG_NEEDS_REMAP, clear that flag, and if it is a type 2 put all six of
 * its inventory uids through the {old, new} table the loader built.
 *
 * THE FLAG IS CLEARED BEFORE THE TYPE IS TESTED, so an object of any other
 * type still loses it -- the flag means "has been seen by this pass", not
 * "has been remapped".
 *
 * IT DESELECTS EVERY TYPE 2 IT REMAPS. The second flag it clears is
 * OBJ_FLAG_SELECTED, which is right for an object that has just come off
 * disk, and the original spells it `and ch, 0xFB` -- a byte operation on the
 * second byte of the dword -- which is how it stays distinct from the first
 * clear. Only type 2s that reached the loop lose it.
 *
 * That flag went in here as a fresh name, OBJ_FLAG_REMAP_DONE, "cleared,
 * never set". It is OBJ_FLAG_SELECTED and has been named since long before;
 * ToggleSelect is what sets it. **A second name on a flag was the one thing
 * checkoffsets did not watch** -- it tracked `*_OFF_*` families only -- and
 * it watches `*_FLAG_*` now for exactly this.
 *
 * THE TABLE AND ITS COUNT ARE RE-READ AFTER EVERY SUCCESSFUL SUBSTITUTION and
 * not otherwise. Nothing in the loop can move them -- the writes go to the
 * object, not the table -- so this is the compiler reloading across a store it
 * cannot prove disjoint. Written as the plain loop that means.
 *
 * A uid with no entry in the table is LEFT ALONE rather than zeroed, so a save
 * referring to something that no longer exists keeps a dangling uid. The
 * lookups elsewhere answer NULL for it, which is the same thing the empty-slot
 * path produces.
 *
 * The search is linear per slot, so this is O(six slots x table) per object.
 * The table is a load-time artefact and the object count is a few hundred.
 */
void __cdecl RemapInventoryUids(void)
{
    uint8_t *obj;

    for (obj = (uint8_t *)FirstItem(); obj; obj = (uint8_t *)NextItem()) {
        uint32_t flags = *(const uint32_t *)(obj + OBJ_OFF_FLAGS);
        int32_t  slot;

        if (!(flags & OBJ_FLAG_NEEDS_REMAP))
            continue;

        *(uint32_t *)(obj + OBJ_OFF_FLAGS) = flags & ~OBJ_FLAG_NEEDS_REMAP;

        if (*(const int32_t *)obj != 2)
            continue;

        for (slot = 0; slot < AM2_INVENTORY_SLOTS; slot++) {
            uint32_t *here = (uint32_t *)(obj + UNIT_OFF_INVENTORY
                                          + (size_t)slot * 4);
            int32_t   i;

            if (!*here)
                continue;

            for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_UID_REMAP_COUNT;
                 i++) {
                const uint32_t (*map)[2] =
                    *(const uint32_t (**)[2])(uintptr_t)ADDR_UID_REMAP;

                if (map[i][0] == *here) {
                    *here = map[i][1];
                    break;
                }
            }
        }

        *(uint32_t *)(obj + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_SELECTED;
    }
}

/* SetPointerMode is reconstructed, in win32/widget.cpp with the rest of the
 * pointer. Declared here rather than by including that header, for the reason
 * script.cpp declares PreloadSprite: item.cpp is on the flat side of the split
 * and must not reach a win32 header, even transitively. An int32 in and
 * nothing out, so the declaration needs no types this side cannot name. */
extern "C" void __cdecl SetPointerMode(int32_t mode);

/* SelectionClick -- original 0x004137D0, one caller. The whole mouse-selection
 * interface, in one function with four gestures and no arguments: it reads the
 * cursor and the drag state out of globals and answers nothing.
 *
 * FOUR PATHS, and which one runs is decided by two globals before anything
 * else happens:
 *
 *   ADDR_VIEW_RECT_ON  -- a rubber band was being dragged and has been let go:
 *                         clear the flag, sweep ObjectsInRect over
 *                         ADDR_VIEW_RECT, and select what comes back;
 *   ADDR_DRAG_ACTIVE   -- a drag is in progress: renormalise ADDR_VIEW_RECT
 *                         from the anchor and the cursor and return;
 *   neither            -- a plain click: WalkCellAtPoint under the cursor.
 *
 * and inside the last two, CTRL held turns "select" into "toggle".
 *
 * THE CURSOR IS CONVERTED BY ADDING THE VIEW ORIGIN, which is what makes the
 * rectangle world-space while ADDR_CURSOR_POINT stays screen-space. The x term
 * is a full int32 add and the y term a 16-bit one, so they are not written the
 * same way; reproduced as found.
 *
 * CTRL WILL NOT EMPTY THE SELECTION. The toggle removes a uid only when more
 * than one is selected -- `cmp edx, 1; jle` -- so ctrl-clicking your last unit
 * leaves it selected. That is a rule no A/B would ever show, since both sides
 * would refuse identically.
 *
 * THE SELECTION LIST IS A SUB-LIST HEADER and PtrListPush is handed its
 * ADDRESS as `this`: ADDR_SELECTED_UIDS is {capacity, count, items}, so
 * ADDR_SELECTED_COUNT and ADDR_SELECTED_ITEMS are its +4 and +8 and not three
 * separate globals. Spelled through the header, as ObjAttachTo spells an
 * object's own list.
 *
 * The plain-click path is gated on the cursor being inside ADDR_BLIT_RECT --
 * the drawn viewport -- so a click on the HUD reaches none of this. */
void __cdecl SelectionClick(void)
{
    uint32_t  world;
    int16_t   wx = (int16_t)(*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X
                             + *(const int32_t *)(uintptr_t)ADDR_CURSOR_POINT);
    int16_t   wy = (int16_t)(*(const int16_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y
                             + *(const int16_t *)(uintptr_t)(ADDR_CURSOR_POINT
                                                             + 2));
    uint8_t  *e;

    ((int16_t *)&world)[0] = wx;
    ((int16_t *)&world)[1] = wy;

    if (!*(const int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON) {
        /* No rubber band at all: a plain click. The three guards are in the
         * original's order -- inside the viewport, no drag under way, clicking
         * enabled -- and all three are pure tests, so the order is kept for
         * fidelity rather than because it can be observed. */
        if (!PointInRect((const AM2_Rect *)(uintptr_t)ADDR_BLIT_RECT,
                         (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT))
            return;
        if (*(const int32_t *)(uintptr_t)ADDR_DRAG_ACTIVE)
            return;
        if (!*(const int32_t *)(uintptr_t)ADDR_CLICK_ENABLED)
            return;

        e = (uint8_t *)WalkCellAtPoint(
                &world, (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC,
                (int32_t (__cdecl *)(void *))(uintptr_t)ADDR_SELECTABLE_PRED);
        if (!e)
            return;

        if (IsKeyDown(AM2_DIK_LCONTROL) || IsKeyDown(AM2_DIK_RCONTROL)) {
            int32_t n = *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT;
            int32_t i;

            for (i = 0; i < n; i++) {
                if ((*(uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]
                    != ((const AM2_Object *)e)->uid)
                    continue;

                /* Found: drop it, unless it is the only one left. */
                if (n <= 1)
                    return;
                ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
                *(uint32_t *)(e + OBJ_OFF_FLAGS) &=
                    ~(uint32_t)OBJ_FLAG_SELECTED;
                /* DESELECTING DETACHES. `push 0; push esi; call 0x458070` is
                 * ObjAttachTo with a null target -- its pure-detach case -- so
                 * a unit ctrl-clicked out of the selection also leaves
                 * whatever it was following. Omitting it leaves the object
                 * attached with nothing selecting it, which the A/B would show
                 * only if something later walked that list. */
                ObjAttachTo(e, (void *)0);
                OnSelectionChanged(
                    *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
                return;
            }
        } else {
            DeselectAll();
            SetObjContext(e);
        }

        PtrListPush((void *)(uintptr_t)ADDR_SELECTED_UIDS,
                    (void *)(uintptr_t)((const AM2_Object *)e)->uid);
        *(uint32_t *)(e + OBJ_OFF_FLAGS) |= OBJ_FLAG_SELECTED;
        OnSelectionChanged(*(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
        return;
    }

    /* A rubber band exists. While the drag is still under way this only
     * renormalises the rectangle from the anchor and the cursor -- the min of
     * each axis into left/top and the max into right/bottom -- and returns. */
    if (*(const int32_t *)(uintptr_t)ADDR_DRAG_ACTIVE) {
        int16_t ax = *(const int16_t *)(uintptr_t)ADDR_DRAG_ANCHOR;
        int16_t ay = *(const int16_t *)(uintptr_t)(ADDR_DRAG_ANCHOR + 2);
        AM2_Rect *r = (AM2_Rect *)(uintptr_t)ADDR_VIEW_RECT;

        r->left   = (ax < wx) ? ax : wx;
        r->right  = (ax > wx) ? ax : wx;
        r->top    = (ay < wy) ? ay : wy;
        r->bottom = (ay > wy) ? ay : wy;
        return;
    }

    /* The drag has been let go. */
    *(int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON = 0;

    e = (uint8_t *)ObjectsInRect(
            (const AM2_Rect *)(uintptr_t)ADDR_VIEW_RECT,
            (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC,
            (int32_t (__cdecl *)(void *))(uintptr_t)ADDR_SELECTABLE_PRED);
    if (!e)
        return;

    if (!IsKeyDown(AM2_DIK_LCONTROL) && !IsKeyDown(AM2_DIK_RCONTROL))
        DeselectAll();

    for (; e; e = *(uint8_t *const *)(e + OBJ_OFF_QUERY_NEXT)) {
        int32_t n = *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT;
        int32_t i;
        int32_t seen = 0;

        for (i = 0; i < n; i++)
            if ((*(uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]
                == ((const AM2_Object *)e)->uid) {
                seen = 1;
                break;
            }

        if (seen)
            continue;

        /* A type 2/3/8 whose OBJ_OFF_FIELD_94 is set is skipped ENTIRELY --
         * the original's `jne` goes past the push to the loop advance, not
         * merely past the call below it. Reading it as "skip the call and add
         * anyway" selects things the original does not, and no static check
         * would see the difference. */
        if (ObjIsTypeIn238((const AM2_Object *)e)) {
            if (*(const void *const *)(e + OBJ_OFF_FIELD_94))
                continue;
            ObjType2Field548((const AM2_Object *)e);
        }

        PtrListPush((void *)(uintptr_t)ADDR_SELECTED_UIDS,
                    (void *)(uintptr_t)((const AM2_Object *)e)->uid);
        *(uint32_t *)(e + OBJ_OFF_FLAGS) |= OBJ_FLAG_SELECTED;
    }

    OnSelectionChanged(*(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
}

/* TrooperDiedTail's two helpers stay original: both are above the CRT line,
 * so neither is in the 1,239 and reconstructing them would not move it. */
typedef void (__cdecl *AM2_DiedEffectAFn)(void *obj);
typedef void (__cdecl *AM2_DiedEffectBFn)(const AM2_Point *at, int32_t facing,
                                          int32_t kind, int32_t height);
#define orig_died_reset ((AM2_DiedEffectAFn)AM2_IMAGE(ADDR_DIED_EFFECT_A))
#define orig_died_burst ((AM2_DiedEffectBFn)AM2_IMAGE(ADDR_DIED_EFFECT_B))

/* TrooperDiedTail -- original 0x00447EE0, 512 bytes, two callers. The shared
 * tail of TrooperDied: choose the death ANIMATION and the noise that goes
 * with it. Its caller handles the spawn and who gets credited; this is the
 * presentation.
 *
 * ITS ARGUMENT IS THE ONE TrooperDied PASSES STRAIGHT THROUGH. orig.h says of
 * that function "its middle argument is passed straight through to the tail
 * and read nowhere here" -- this is where it is read, and it is a death KIND
 * of 1..5 driving a five-arm jump table.
 *
 * READ THE TABLE AS DATA. The arms are laid out 0x447F12, 0x447F4E, 0x447FAF,
 * 0x447FEA, 0x44800A and the table at 0x004480C8 orders them 1 -> 0x447FAF,
 * 2 -> 0x447F4E, 3 -> 0x447F12, 4 -> 0x447FEA, 5 -> 0x44800A. KINDS 1 AND 3
 * ARE SWAPPED against the layout, so numbering the bodies top to bottom gets
 * two of five wrong. Same failure this file records for the state-2 sub-states
 * and for WeaponClassOf.
 *
 * KIND 3'S ARM IS REACHED TWO WAYS -- through the table, and by falling out of
 * the OBJ_OFF_FIELD_5A4 test above it, which is why that block sits before the
 * dispatch rather than with the other arms.
 *
 * EVERY ARM ANSWERS THE SAME TWO QUESTIONS: which animation goes into
 * OBJ_OFF_FIELD_584, and which sound index survives into the tail. The
 * threshold is OBJ_OFF_SOLDIER_KIND against 6, held in ebx and reused by
 * every arm, which is why the arms look like they end abruptly -- they are
 * all falling into one tail.
 *
 * SARGE CRIES OUT AND EVERYONE ELSE JUST MAKES A NOISE. With OBJ_OFF_SARGE set
 * and the index not 6, the tail speaks AM2_SPEAK_AAH -- four lines -- instead
 * of playing a sound at the body. Kind 1 sets the index to 6 precisely so that
 * arm keeps the sound even for Sarge. Both paths end in the same SetUnitPose.
 *
 * THE ANIMATION TABLE IS THREE ENTRIES, {0x21, 0x22, 0x23}, indexed by
 * ClassifyCode74, which answers 0, 1 or 2 off the row's +0x4C. They are
 * consecutive and it would be shorter to write `0x21 + code`; the original
 * indexes a table, so a table read is what is written.
 *
 * The function ends at 0x004480C4. What tools/disasm.py shows after that --
 * `scasd`, `jg`, `add byte ptr` -- is the jump table decoded as instructions,
 * which is the linear-disassembly desync this file warns about. */
void __cdecl TrooperDiedTail(void *obj, int32_t kind)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  anim;
    int32_t  noise = 5;
    int32_t  big = 6;                     /* ebx, the soldier-kind threshold */

    if (!o)
        return;
    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
        return;

    /* THE CLEAR IS NOT PART OF THE ARM. +0x5A4 being set clears it and falls
     * into kind 3's block at 0x447F12; kind 3 reached through the TABLE enters
     * at 0x447F12 directly and does NOT clear it. Writing the two as one
     * condition clears the field on a path the original leaves alone. */
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_5A4)) {
        *(int32_t *)(o + OBJ_OFF_FIELD_5A4) = 0;
        goto arm3;
    }

    switch (kind) {
    case 3:
    arm3:
        *(int32_t *)(o + OBJ_OFF_FIELD_584) = 0;
        *(int32_t *)(o + OBJ_OFF_SIGHT_OUT_T2) = 1;
        noise = 5;
        orig_died_reset(o);
        goto tail;

    case 1:
        anim = (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= big)
                   ? 0x21 : 0x20;
        noise = big;
        break;
    case 2: {
        anim = ((const int32_t *)AM2_IMAGE(ADDR_DEATH_ANIM_BY_CODE))
                   [ClassifyByCode74(o)];
        noise = 5;
        if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) < big && anim == 0x21)
            orig_died_burst((const AM2_Point *)(o + OBJ_OFF_POS),
                            *(const uint8_t *)(o + OBJ_OFF_FACING),
                            *(const int32_t *)(o + OBJ_OFF_TABLE_REC_KIND),
                            *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET));
        break;
    }
    case 4:
        anim = (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= big)
                   ? 0x21 : 0x24;
        break;
    case 5:
        anim = (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= big)
                   ? 0x21
                   : ((const int32_t *)AM2_IMAGE(ADDR_DEATH_ANIM_BY_CODE))
                         [ClassifyByCode74(o)];
        break;
    default:
        anim = ((const int32_t *)AM2_IMAGE(ADDR_DEATH_ANIM_BY_CODE))
                   [ClassifyByCode74(o)];
        break;
    }

    *(int32_t *)(o + OBJ_OFF_FIELD_584) = anim;
    *(int32_t *)(o + OBJ_OFF_SIGHT_OUT_T2) = 1;

tail:
    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) == 7)
        noise = 0x36;

    if (*(const int32_t *)(o + OBJ_OFF_SARGE) && noise != big)
        SpeakLine(AM2_SPEAK_AAH, *(const int8_t *)(o + OBJ_OFF_ARMY));
    else
        PlaySoundAt(noise, 0, 0,
                    *(const int16_t *)(o + OBJ_OFF_POS),
                    *(const int16_t *)(o + OBJ_OFF_POS + 2));

    SetUnitPose(o, *(const int32_t *)(o + OBJ_OFF_FIELD_584));
}

/* TrooperHostApprovedPickupItem -- original 0x004488C0, 608 bytes, one caller.
 * The HOST half of the pickup pair: what happens on the machine that decided
 * the pickup was allowed. TrooperRemotePickupItem, 0x00448B20, is the other
 * half and this is written against it.
 *
 * IT NAMES ITSELF, seven times, and so does its twin. orig.h had these as
 * ADDR_TROOPER_HOST_PICKUP and ADDR_TROOPER_PAIR_APPLY -- names for which
 * message carries them rather than for what they do.
 *
 * THE HOST CONSUMES AND ANNOUNCES; THE REMOTE RECONCILES AND STAYS QUIET.
 * That is the whole difference and it holds across every arm, which is why
 * reading the two together is worth more than reading either twice:
 *
 *   replace     DestroyByType here, WeaponRespawn there. The displaced weapon
 *               is also marked OBJ_FLAG_OVERDUE here and is not there;
 *   ammo        SpeakLine 0x15 here, and only when the held weapon is NOT
 *               already full -- the test is before the transfer, so a topped
 *               up weapon says nothing. There, nothing is said at all;
 *   new weapon  DestroyByType and a line here, WeaponRespawn there;
 *   medkit      SpeakLine 0x1A here; there, CommMustBroadcast on the ITEM's
 *               army decides whether it leaves the map;
 *   hot target  DestroyByType on both.
 *
 * AND THE HOST SETS NO COOLDOWN AND PLAYS NO SOUND. The twin opens by
 * stamping OBJ_OFF_PICKUP_AFTER and playing AM2_SND_PICKUP; this one goes
 * straight to NotifyPickedUp. On the machine that approved it, the local
 * pickup path has already done both.
 *
 * THE SPEECH GROUPS WERE ALL NAMED ALREADY and they identify the weapon
 * kinds for free: kind 8 speaks AM2_SPEAK_HEAVYMACGUN, kind 10
 * AM2_SPEAK_AUTORIFLE and kind 29 AM2_SPEAK_VULCANGUN, the ammo arm
 * AM2_SPEAK_MOREAMMO and the medkit AM2_SPEAK_HITSSPOT -- "hits the spot",
 * which is what a medkit line would be. Five greps, five existing names.
 *
 * THE THREE SPEECH GROUPS COME FROM A CHAINED COMPARE -- `sub 8 / sub 2 /
 * sub 0x13` -- so the kinds are 8, 10 and 29 and the groups 9, 2 and 0x13.
 * Reading the subtractions as absolute values gives 8, 2, 0x13 for the kinds
 * and is wrong about two of the three. */
void __cdecl TrooperHostApprovedPickupItem(void *troop, void *item,
                                           int32_t slot, int32_t ammo)
{
    uint8_t *t = (uint8_t *)troop;
    uint8_t *w = (uint8_t *)item;
    uint8_t *comm;
    int32_t  kind;
    uint8_t *held;

    NotifyPickedUp(item, troop);

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("TrooperHostApprovedPickupItem %x\n",
                 ((const AM2_Object *)w)->uid);

    kind = **(const int32_t *const *)(w + OBJ_OFF_FIELD_C0);

    if (kind == AM2_ITEM_KIND_HOT_TARGET) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperHostApprovedPickupItem: HotTarget\n");
        DestroyByType(item);
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("TrooperHostApprovedPickupItem %x\n",
                     ((const AM2_Object *)w)->uid);
        return;
    }

    if (kind == AM2_ITEM_KIND_MEDKIT) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperHostApprovedPickupItem:Medkit\n");
        ForEachArmyObject(*(const int8_t *)(t + OBJ_OFF_ARMY),
                          (void (__cdecl *)(void *))(uintptr_t)
                              ADDR_MEDKIT_HEAL_ONE);
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("TrooperHostApprovedPickupItem %x\n",
                     ((const AM2_Object *)w)->uid);
        SpeakLine(AM2_SPEAK_HITSSPOT, *(const int8_t *)(t + OBJ_OFF_ARMY));
        return;
    }

    if (!*(const uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4)) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperHostApprovedPickupItem: new weapon\n");
        *(int32_t *)(w + ITEM_OFF_AMMO) = ammo;
        *(uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4) =
            ((const AM2_Object *)w)->uid;
        *(int8_t *)(w + OBJ_OFF_ARMY) = *(const int8_t *)(t + OBJ_OFF_ARMY);
        DestroyByType(item);
        /* SpeakItemPickup, not SpeakLine: it takes the item KIND and maps it
         * through its own two tables. The swap arm below speaks GROUPS
         * directly instead, which is why the two look alike and are not. */
        SpeakItemPickupLine(**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0),
                        *(const int8_t *)(t + OBJ_OFF_ARMY));
        return;
    }

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("\tTrooperHostApprovedPickupItem: we've got somthing in "
                 "that slot\n");

    held = (uint8_t *)WeaponByUid(
               *(const uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4));

    if (KindInSetB(**(const int32_t *const *)(held + OBJ_OFF_FIELD_C0))
        && KindInSetB(**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))) {
        int32_t k = **(const int32_t *const *)(w + OBJ_OFF_FIELD_C0);

        if (k == 8)
            SpeakLine(AM2_SPEAK_HEAVYMACGUN, *(const int8_t *)(t + OBJ_OFF_ARMY));
        else if (k == 10)
            SpeakLine(AM2_SPEAK_AUTORIFLE, *(const int8_t *)(t + OBJ_OFF_ARMY));
        else if (k == 29)
            SpeakLine(AM2_SPEAK_VULCANGUN, *(const int8_t *)(t + OBJ_OFF_ARMY));

        *(uint32_t *)(held + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;
        *(uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4) =
            ((const AM2_Object *)w)->uid;
        *(int8_t *)(w + OBJ_OFF_ARMY) = *(const int8_t *)(t + OBJ_OFF_ARMY);
        DestroyByType(item);
        return;
    }

    /* Not a swap: take the ammo instead. The line is spoken only when there
     * is room for it, and the test is made BEFORE the transfer. */
    if (*(const int32_t *)(held + ITEM_OFF_AMMO)
        < (*(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))
              [ITEMTYPE_OFF_CAPACITY / 4])
        SpeakLine(AM2_SPEAK_MOREAMMO, *(const int8_t *)(t + OBJ_OFF_ARMY));

    {
        int32_t cap = (*(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))
                          [ITEMTYPE_OFF_CAPACITY / 4];
        int32_t now = *(const int32_t *)(held + ITEM_OFF_AMMO) + ammo;

        *(int32_t *)(held + ITEM_OFF_AMMO) = now;
        if (now > cap)
            *(int32_t *)(held + ITEM_OFF_AMMO) = cap;
    }
}

/* TrooperRemotePickupItem -- original 0x00448B20, 576 bytes, one caller: the
 * kind 0x18 comm message. What a trooper does with an item somebody ELSE told
 * us it picked up.
 *
 * THE NAME IS THE FUNCTION'S OWN, six times over. orig.h called this
 * ADDR_TROOPER_PAIR_APPLY and said so honestly -- "the name is the pairing,
 * not the effect ... none of it is read yet" -- and every one of its log lines
 * opens `TrooperRemotePickupItem`. Renamed from the body, which is what the
 * strings were there for.
 *
 * AND THAT NOTE HAD THE TWO OBJECTS THE WRONG WAY ROUND. It said the function
 * "stamps `now + 2000` into the trooper's +0xC8 and plays sound 0x37 at the
 * weapon's position". It is the other way: the stamp lands on the ITEM and the
 * sound plays where the TROOPER is. NotifyPickedUp(item, taker) is what pins
 * it down -- the original passes (arg2, arg1) -- and the difference matters,
 * because two seconds on the item is a pickup cooldown while two seconds on
 * the trooper would be something else entirely.
 *
 * FIVE ARMS, and the two special kinds leave before the slot logic is reached:
 *
 *   kind 0x0E, HotTarget -- destroy the item outright;
 *   kind 0x16, Medkit    -- apply 0x00458AB0 to EVERY object of the trooper's
 *                           army, so a medkit heals the whole side and not
 *                           just whoever walked over it;
 *   empty slot           -- take it: ammo in, uid into the slot, army copied
 *                           across, and the item respawns;
 *   slot full, both weapons in KindInSetB -- REPLACE: the new uid goes in the
 *                           slot and the item respawns, the old one simply
 *                           dropping out of the array;
 *   slot full, otherwise -- transfer the AMMO instead, capped at the item
 *                           def's +0x18, and respawn the emptied item only if
 *                           it is army 4 and the comm object says so.
 *
 * ADDR_WEAPON_RESPAWN, not a free: an item that has been taken goes back to
 * wherever it spawns rather than being destroyed, which is what makes the
 * replace arm coherent -- the weapon you dropped is not gone.
 *
 * EVERY OFFSET THIS NEEDED WAS ALREADY NAMED, and two of the names are the
 * evidence for the correction above. OBJ_OFF_PICKUP_AFTER is what +0xC8 is
 * called -- the field was named right and only the prose beside this address
 * had it on the wrong object. ITEM_OFF_AMMO is 0xCC, which is
 * OBJ_OFF_TARGET_UID on other types; and item->OBJ_OFF_FIELD_C0 is an ITEMTYPE
 * record, so the kind switched on below is ITEMTYPE_OFF_KIND and the ammo cap
 * is ITEMTYPE_OFF_CAPACITY. Overloading by type, as at 0x52C and 0x538.
 *
 * Every debug line is gated on the comm object's COMM_OFF_VERBOSE, so none of
 * them appears in an ordinary run.
 *
 * checkoffsetuse FOUND CODE I HAD NOT TRANSCRIBED, which is the first time it
 * has caught an omission rather than a wrong name. It reported +0x08 read by
 * the original and named nowhere here: both respawn arms follow
 * WeaponRespawn with `item->flags |= OBJ_FLAG_OVERDUE`, and the medkit arm
 * has a whole tail -- a CommMustBroadcast on the ITEM's army deciding whether
 * the medkit leaves the map at all -- that I had stopped short of. Roughly
 * fifteen instructions, invisible to every other check, and an A/B could not
 * have shown it either: nothing here runs without a second player. */
void __cdecl TrooperRemotePickupItem(void *troop, void *item, int32_t slot,
                                     int32_t ammo)
{
    uint8_t *t = (uint8_t *)troop;
    uint8_t *w = (uint8_t *)item;
    uint8_t *comm;
    int32_t  kind;
    uint8_t *held;

    *(uint32_t *)(w + OBJ_OFF_PICKUP_AFTER) =
        *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS + AM2_PICKUP_HOLD_MS;

    PlaySoundAt(AM2_SND_PICKUP, 0, 0,
                *(const int16_t *)(t + OBJ_OFF_POS),
                *(const int16_t *)(t + OBJ_OFF_POS + 2));
    NotifyPickedUp(item, troop);

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("TrooperRemotePickupItem %x\n",
                 ((const AM2_Object *)w)->uid);

    kind = **(const int32_t *const *)(w + OBJ_OFF_FIELD_C0);

    if (kind == AM2_ITEM_KIND_HOT_TARGET) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperRemotePickupItem: HotTarget\n");
        DestroyByType(item);
        return;
    }

    if (kind == AM2_ITEM_KIND_MEDKIT) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperRemotePickupItem:Medkit\n");
        ForEachArmyObject(*(const int8_t *)(t + OBJ_OFF_ARMY),
                          (void (__cdecl *)(void *))(uintptr_t)
                              ADDR_MEDKIT_HEAL_ONE);

        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperRemotePickupItem %x\n",
                     ((const AM2_Object *)w)->uid);

        /* The medkit is taken off the map only when this side is the one that
         * should say so -- CommMustBroadcast on the ITEM's army, not the
         * trooper's. */
        if (CommMustBroadcast(*(void *const *)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(w + OBJ_OFF_ARMY))) {
            WeaponRespawn(item);
            *(uint32_t *)(w + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;
        }
        return;
    }

    if (!*(const uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4)) {
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("\tTrooperRemotePickupItem: new weapon\n");
        *(int32_t *)(w + ITEM_OFF_AMMO) = ammo;
        *(uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4) =
            ((const AM2_Object *)w)->uid;
        *(int8_t *)(w + OBJ_OFF_ARMY) = *(const int8_t *)(t + OBJ_OFF_ARMY);
        WeaponRespawn(item);
        return;
    }

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("\tTrooperRemotePickupItem: we've got somthing in that slot\n");

    held = (uint8_t *)WeaponByUid(*(const uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4));

    if (KindInSetB(**(const int32_t *const *)(held + OBJ_OFF_FIELD_C0))
        && KindInSetB(**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))) {
        *(uint32_t *)(t + TROOPER_OFF_WEAPON_UID + slot * 4) =
            ((const AM2_Object *)w)->uid;
        *(int8_t *)(w + OBJ_OFF_ARMY) = *(const int8_t *)(t + OBJ_OFF_ARMY);
        WeaponRespawn(item);
        return;
    }

    {
        int32_t cap = (*(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))
                          [ITEMTYPE_OFF_CAPACITY / 4];
        int32_t now = *(const int32_t *)(held + ITEM_OFF_AMMO) + ammo;

        *(int32_t *)(held + ITEM_OFF_AMMO) = now;
        if (now > cap)
            *(int32_t *)(held + ITEM_OFF_AMMO) = cap;
    }

    if (!*(const int32_t *)(w + ITEM_OFF_AMMO)
        && *(const int8_t *)(w + OBJ_OFF_ARMY) == 4   /* the neutral army; item.cpp spells it as the literal */
        && *(const int32_t *)(comm + COMM_OFF_IS_HOST)) {
        WeaponRespawn(item);
        *(uint32_t *)(w + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;
    }
}

/* ToggleSelect -- original 0x00413710, one caller.
 *
 * Add an object to the selection, or take it out if it is already in -- and
 * clear the selection first unless a CONTROL key is held, which is the same
 * modifier SelectIfOwn uses and, unlike there, is tested FIRST here rather
 * than last.
 *
 * YOU CANNOT DESELECT THE LAST OBJECT. The removal path is gated on the
 * selection holding more than one, so clicking the only selected unit with
 * CONTROL held leaves it selected. Reproduced: the guard is explicit in the
 * original, not an accident of the loop.
 *
 * A SELECTED OBJECT WHOSE UID IS NOT IN THE LIST is possible and handled --
 * the flag says selected, the search finds nothing, and the function falls
 * out having only notified. The two can disagree because the flag lives on
 * the object and the list is a separate array.
 *
 * REMOVAL DOES THREE THINGS AND ADDITION DOES TWO. Coming out also detaches
 * the object, through ObjAttachTo with a null target; going in does not
 * attach it to anything. So selection and attachment are not symmetric.
 *
 * Every path that changes anything notifies ADDR_ON_SELECTION_CHANGED with
 * the zero point, including the one that found nothing to remove.
 */
void __cdecl ToggleSelect(void *obj)
{
    AM2_Object *o = (AM2_Object *)obj;
    int32_t     n;
    int32_t     i;

    if (!IsKeyDown(AM2_DIK_LCONTROL) && !IsKeyDown(AM2_DIK_RCONTROL))
        DeselectAll();

    if (!(*(const uint32_t *)((uint8_t *)o + OBJ_OFF_FLAGS)
          & OBJ_FLAG_SELECTED)) {
        PtrListPush((void *)(uintptr_t)ADDR_SELECTED_UIDS,
                    (void *)(uintptr_t)o->uid);
        *(uint32_t *)((uint8_t *)o + OBJ_OFF_FLAGS) |= OBJ_FLAG_SELECTED;
        OnSelectionChanged(
            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
        return;
    }

    n = *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT;

    for (i = 0; i < n; i++)
        if ((*(const uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]
            == o->uid)
            break;

    if (i >= n || n <= 1) {
        OnSelectionChanged(
            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
        return;
    }

    ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
    *(uint32_t *)((uint8_t *)o + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_SELECTED;
    ObjAttachTo(o, (void *)0);
    OnSelectionChanged(
            *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
}

typedef int32_t (__cdecl *AM2_VehicleBlockFn)(void *veh, int32_t facing,
                                              uint32_t at, int32_t unused);
#define orig_vehicle_block_weight \
    ((AM2_VehicleBlockFn)(uintptr_t)ADDR_VEHICLE_BLOCK_WEIGHT)

/* NearestClearVehiclePoint -- original 0x0045B930, one caller. The same square
 * spiral NearestClearPoint above walks, asking a different question at each
 * point: can THIS VEHICLE stand here facing that way?
 *
 * The two are the same loop to the instruction -- the same ADDR_SPIRAL_STEP
 * table, the same sixteen units a step, the same leg growing every second
 * turn, the same sixteen-bit adds onto the packed point, and the same absence
 * of any give-up. What differs is only the test: NearestClearPoint asks
 * BlockWeightAt for a point with no object, and this asks
 * ADDR_VEHICLE_BLOCK_WEIGHT for a vehicle of a particular kind at a particular
 * facing, accepting anything under AM2_VEHICLE_CLEAR_WEIGHT.
 *
 * THE FACING IS ROUNDED TO THIRTY-TWO, NOT TO THE SPRITE'S COUNT.
 * `RoundTo8(facing, 5)` with the 5 a literal -- where MoveStepPoint asks the
 * animation how many directions it has. So the fit is tested against a coarser
 * circle than the thing is drawn on, which is the original's and is worth
 * noticing before assuming the two agree.
 *
 * AND IT CANNOT FAIL EITHER. The bounds test only skips the weight test for a
 * point outside the map; it does not end the walk. What stops this running for
 * ever on a crowded map is the same thing that stops the other -- the step
 * arithmetic is sixteen bits, so a long enough walk wraps the coordinate.
 */
void __cdecl NearestClearVehiclePoint(void *veh, int32_t facing, uint32_t from,
                                      void *outPt)
{
    AM2_Point *out = (AM2_Point *)outPt;
    int32_t    dir = 0;
    int32_t    step = 0;
    int32_t    leg = 1;
    int32_t    grew = 0;
    uint8_t    snapped;

    snapped = (uint8_t)RoundTo8(facing & 0xFF, AM2_VEHICLE_FACING_BITS);
    *(uint32_t *)out = from;

    for (;;) {
        const uint8_t *entry;

        if (PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT), out)
            && orig_vehicle_block_weight(veh, snapped,
                                         *(const uint32_t *)out, 0)
               < AM2_VEHICLE_CLEAR_WEIGHT)
            return;

        if (++step >= leg) {
            step = 0;
            if (++dir > 3)
                dir = 0;
            if (grew) {
                leg++;
                grew = 0;
            } else {
                grew = 1;
            }
        }

        entry = (const uint8_t *)AM2_IMAGE(ADDR_SPIRAL_STEP)
                + (uint32_t)dir * AM2_SPIRAL_STEP_STRIDE;

        out->x = (int16_t)(out->x
                           + (int16_t)((int16_t)*(const int16_t *)entry
                                       << AM2_SPIRAL_STEP_SHIFT));
        out->y = (int16_t)(out->y
                           + (int16_t)((int16_t)*(const int16_t *)(entry + 4)
                                       << AM2_SPIRAL_STEP_SHIFT));
    }
}

/* NearestClearPoint -- original 0x004579C0, two callers. Walk a square spiral
 * out from `from` until a point is both inside the map and passable, and write
 * it back through `out`. The name is ours, from the body.
 *
 * THE THIRD SPIRAL IN THIS TREE and the second table. NearestAllowedTile and
 * SettlePointInRegion walk in TILES with ADDR_SPIRAL_DX/DY, two separate int32
 * arrays; this walks in WORLD UNITS with ADDR_SPIRAL_STEP, an interleaved
 * {dx, dy} array at a different address, sixteen units a step. Same four
 * directions, same leg-grows-every-second-turn rule, three different
 * implementations. Worth knowing before assuming the image has one of either.
 *
 * IT CANNOT FAIL AND SO IT CANNOT STOP. The only exit is finding a point that
 * passes both tests; a start surrounded by nothing acceptable spirals until
 * the arithmetic wraps, because the bounds test does not end the walk -- it
 * only skips the weight test for that point. SettlePointInRegion has the same
 * shape with a give-up on the region, and this one has no give-up at all.
 * Reproduced; it is the original's.
 *
 * The step arithmetic is SIXTEEN BITS throughout: the table's low word into
 * AX, `shl ax, 4`, and `add word ptr` onto each half of the packed point. So a
 * long enough walk wraps the coordinate rather than growing it, which is the
 * only thing that keeps the loop above from running for ever on a real map.
 *
 * The weight comes from BlockWeightAt with a NULL object and the candidate
 * point passed twice -- "how blocked is this point, for nobody in particular".
 */
void __cdecl NearestClearPoint(uint32_t from, void *outPt)
{
    AM2_Point *out = (AM2_Point *)outPt;
    int32_t dir = 0;
    int32_t step = 0;
    int32_t leg = 1;
    int32_t grew = 0;

    *(uint32_t *)out = from;

    for (;;) {
        const uint8_t *entry;

        if (PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT), out)
            && BlockWeightAt((void *)0, *(const uint32_t *)out,
                             *(const uint32_t *)out) < AM2_BLOCK_CLEAR)
            return;

        if (++step >= leg) {
            step = 0;
            if (++dir > 3)
                dir = 0;
            if (grew) {
                leg++;
                grew = 0;
            } else {
                grew = 1;
            }
        }

        entry = (const uint8_t *)AM2_IMAGE(ADDR_SPIRAL_STEP)
                + (uint32_t)dir * AM2_SPIRAL_STEP_STRIDE;

        out->x = (int16_t)(out->x
                           + (int16_t)((int16_t)*(const int16_t *)entry
                                       << AM2_SPIRAL_STEP_SHIFT));
        out->y = (int16_t)(out->y
                           + (int16_t)((int16_t)*(const int16_t *)(entry + 4)
                                       << AM2_SPIRAL_STEP_SHIFT));
    }
}

/* SetObjContext -- original 0x00457A60, three callers.
 *
 * Point the object-context globals at one object and set the pointer mode
 * from what that object is: SARGE gets mode 0, anything else that is a type 2
 * or 3 gets mode 4, and any other type leaves the mode ALONE.
 *
 * SARGE IS REACHED TWO WAYS and that is the shape of the function: directly,
 * when the object is a type 2 with OBJ_OFF_SARGE set; and through a type 3,
 * whose first listed object is checked for the same thing. So selecting the
 * vehicle Sarge is riding gives the same pointer as selecting Sarge.
 *
 * IT WRITES BOTH HALVES OF TWO PARALLEL PAIRS, and re-reads the source for
 * the second of each: `obj` into OBJ_A and OBJ, and `obj->uid` into VAL_A and
 * VAL through two separate loads of the same field. Reproduced as the two
 * loads, because the duplication is the only evidence that the source had two
 * assignments rather than one.
 *
 * THE POINTER MODE IS CLEARED DIRECTLY AND THEN SET THROUGH THE SETTER. The
 * store at the top writes ADDR_POINTER_MODE itself; SetPointerMode later does
 * the real work, including the five companion globals this function has
 * already zeroed by hand. So the by-hand clears are redundant on the two
 * paths that call the setter and are the whole effect on the path that does
 * not.
 *
 * ADDR_OBJ_CTX_SET IS 1 ONLY FOR SARGE. The other path writes 0 before
 * calling the setter, so the flag distinguishes "the context is Sarge" and
 * not "a context is set".
 */
void __cdecl SetObjContext(void *obj)
{
    AM2_Object *o = (AM2_Object *)obj;
    int32_t     type;

    if (!o)
        return;

    *(int32_t *)(uintptr_t)ADDR_POINTER_MODE = 0;

    *(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A = o;
    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A = (int32_t)o->uid;
    *(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ   = o;
    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL = (int32_t)o->uid;

    *(int32_t *)(uintptr_t)ADDR_POINTER_ACTION  = 0;
    *(int32_t *)(uintptr_t)ADDR_POINTER_PICK    = 0;
    *(int32_t *)(uintptr_t)ADDR_POINTER_F14     = 0;
    *(int32_t *)(uintptr_t)ADDR_POINTER_F10     = 0;
    *(int32_t *)(uintptr_t)ADDR_POINTER_OVERLAY = 0;

    type = *(const int32_t *)o;

    if (type == 2 && *(const int32_t *)((uint8_t *)o + OBJ_OFF_SARGE)) {
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 1;
        SetPointerMode(AM2_POINTER_MODE_SARGE);
        return;
    }

    if (type == 3) {
        const uint8_t *inner = (const uint8_t *)ListFirstObj(o);

        if (inner && *(const int32_t *)inner == 2
            && *(const int32_t *)(inner + OBJ_OFF_SARGE)) {
            *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 1;
            SetPointerMode(AM2_POINTER_MODE_SARGE);
            return;
        }
    } else if (type != 2) {
        return;                 /* neither 2 nor 3: the mode is left alone */
    }

    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 0;
    SetPointerMode(AM2_POINTER_MODE_OTHER);
}

/* WalkCellAtPoint -- original 0x0042A110, two callers.
 *
 * ITS OWN MACRO'S COMMENT CALLED IT "a CALLBACK instead of a chain" AND IT IS
 * BOTH. The callback is a FILTER whose answer decides whether the object joins
 * the chain, and the chain is built and returned exactly as
 * ObjectsHitByPoint's is -- the macro even declared the return type `void`.
 * Fourth time reconstructing a function is what corrects its own comment, and
 * the established name is kept because the alias ratchet refused the second.
 *
 * ObjectsHitByPoint above with a caller-supplied predicate: the same cell
 * walk, the same destroyed test, the same OBJ_OFF_HIT_RECT test, then the
 * predicate, then the same optional OBJ_OFF_HIT_MASK test, chaining what
 * survives through OBJ_OFF_QUERY_NEXT.
 *
 * THE PREDICATE SITS BETWEEN THE TWO HIT TESTS, not before or after both.
 * That matters because the mask test is the expensive one -- it is per-pixel
 * -- and the predicate is therefore given a chance to reject an object before
 * the mask is consulted, but only after the cheap rectangle has already
 * accepted it. Moving it either way would change how often each runs, and for
 * a predicate with side effects it would change what it sees.
 *
 * IT IS NOT WHAT THE OTHER TWO ARE BUILT ON. All three members of this family
 * walk the cell themselves; neither of the other two calls this one with a
 * constant predicate. The duplication is the original's and is reproduced
 * rather than factored, for the reason AddLevelRecord and AddNameRecord are:
 * a shared helper would be a fourth thing that is not in the binary.
 *
 * THE BOUND IS `cols` IN BOTH DIRECTIONS, as in its sibling -- the grid is
 * square in cols and MAPDESC_OFF_ROWS is not consulted. See MapDescInit.
 *
 * The chain is built newest-first, so the answer is the LAST qualifying
 * object in the cell's own order and the walk from it runs backwards through
 * that order. Same as the sibling, and worth stating because a caller taking
 * "the first hit" gets the last one added.
 */
void *__cdecl WalkCellAtPoint(const uint32_t *pt, const void *desc,
                              int32_t (__cdecl *keep)(void *obj))
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
        if (!keep(o))
            continue;
        if (*(void *const *)(o + OBJ_OFF_HIT_MASK)
            && !ObjMaskBitAt(o, (const AM2_Point *)pt))
            continue;

        *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT) = head;
        head = o;
    }

    return head;
}

/* WeaponFrameReady -- original 0x004499A0, one caller.
 *
 * Is the object's first row on a frame from which its weapon may act? A dense
 * switch over the weapon's kind picks one of six frame sets; a kind with no
 * rule answers 1 outright, and a frame that matches its set defers to
 * Field51MeetsMin on the row.
 *
 * ITS GUARD IS THE FIRE-MODE FIELD. `UNIT_OFF_FIRE_F58C` is one of the pair
 * PickFireMode writes, so this refuses outright any unit that has not been
 * given a firing target -- which is what puts the whole function on the
 * firing path rather than the animation one.
 *
 * SO THERE ARE THREE ANSWERS, NOT TWO. 1 means "no rule for this kind"; 0
 * means "the rule exists and this frame fails it"; and everything else is
 * whatever Field51MeetsMin says. A caller reading it as a plain boolean gets
 * the right shape and loses why.
 *
 * THE FIRST CALL'S RESULT IS DISCARDED. ClassifyCode74 is called with the
 * object and its answer is immediately overwritten by the row's frame. That
 * is the second such call in this file after ObjIsHittable's ObjIsType4, and
 * the same reading applies: it is there for whatever it does, not for what it
 * returns. Reproduced.
 *
 * MOST KINDS HAVE NO RULE. The 43-entry index table sends 27 of them to the
 * default arm, so the interesting cases are kinds 1, 2, 4, 5, 7..12, 29, 30
 * and 43 -- and the two arms for kinds 11/12 and for kind 5 test the SAME
 * frame, 0x0C, by different routes the compiler did not merge. Written as one
 * case each way round, matching the table rather than the code layout.
 *
 * ARM 0 HAS AN EXTRA GUARD THE OTHERS DO NOT: a positive OBJ_OFF_FIELD_44
 * takes the default answer of 1 before the frame is looked at, so an object
 * with that field set is never refused on frame grounds.
 *
 * The kind is `*(int32 *)weapon->FIELD_C0` -- the same first dword of the type
 * record `WeaponClassOf` and `SelectInventorySlot` index -- and the bound is
 * `(uint32)(kind - 1) > 0x2A`, one comparison covering both ends.
 */
int32_t __cdecl WeaponFrameReady(void *obj, void *weapon)
{
    uint8_t *o   = (uint8_t *)obj;
    uint8_t *row;
    int32_t  kind;
    int32_t  frame;

    if (!*(const int32_t *)(o + UNIT_OFF_FIRE_F58C))
        return 0;

    (void)ClassifyByCode74(o);          /* answer discarded; see above */

    row   = *(uint8_t **)(o + OBJ_OFF_ROWS);
    kind  = **(const int32_t *const *)((uint8_t *)weapon + OBJ_OFF_FIELD_C0);
    frame = *(const int16_t *)(row + ROW_OFF_FRAME);

    switch (kind) {
    case 1: case 7: case 8: case 9: case 10: case 29: case 30:
        if (*(const int32_t *)(o + OBJ_OFF_FIELD_44) > 0)
            return 1;
        if (frame != 6 && frame != 7 && frame != 0x0D)
            return 0;
        break;

    case 2:
        if (frame != 9 && frame != 0x0A && frame != 0x0B)
            return 0;
        break;

    case 4:
        if (frame != 6 && frame != 7 && frame != 0x2C)
            return 0;
        break;

    case 5: case 11: case 12:
        if (frame != 0x0C)
            return 0;
        break;

    case 43:
        if (frame != 6)
            return 0;
        break;

    default:
        return 1;                       /* no rule for this kind */
    }

    return Field51MeetsMin(row);
}

/* SetObjTablePair -- original 0x0044BA70, one caller.
 *
 * Give an object a kind, point it at two of the 256-byte records at
 * ADDR_OBJ_TABLE_RECORDS, propagate one of them through its sub-record, and
 * refresh its rows.
 *
 * THE TWO POINTERS ARE INDEXED DIFFERENTLY AND THAT IS THE WHOLE FUNCTION.
 * `OBJ_OFF_TABLE_REC_KIND` is `records + kind * 0x100`, straight from the
 * argument. `OBJ_OFF_TABLE_REC_SLOT` is `records + slot * 0x100`, where the
 * slot comes from the object's own uid, through UidArmy and then
 * CommArmyOfSlot. So one is what the caller asked for and the other is who
 * owns it, and reading them as a pair of the same thing loses that.
 *
 * IT CONFIRMS ANOTHER FUNCTION'S COMMENT FROM THE OTHER SIDE.
 * `ADDR_SCRIPT_SET_OBJ_TABLE` is documented as writing "+0x4C0 and +0x4C8 of
 * the sub-record at obj+0x6C" -- and obj + 0x6C + 0x4C0 is exactly +0x52C.
 * Two routes to one pair of fields, arrived at independently.
 *
 * ONLY THE KIND POINTER IS PROPAGATED. SetFieldInAll is handed the sub-record
 * and the kind record; the slot record is written to the object and left
 * there. So whatever reads the propagated copy sees the kind's record and
 * never the owner's.
 *
 * THE ARMY LOOKUP GOES THROUGH THE UID, NOT THE OBJECT. UidArmy is given the
 * caller's uid rather than the object that was just resolved from it -- the
 * same value by a longer route, and reproduced, since the two would differ if
 * anything ever resolved a uid to an object of another army.
 *
 * A uid that does not resolve does nothing at all, silently.
 */
void __cdecl SetObjTablePair(uint32_t uid, int32_t kind)
{
    uint8_t *o = (uint8_t *)ObjByUidAlias(uid);
    int32_t  slot;

    if (!o)
        return;

    *(int32_t *)(o + OBJ_OFF_FIELD_530) = kind;

    slot = CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                          (int32_t)UidArmy(uid));

    *(void **)(o + OBJ_OFF_TABLE_REC_SLOT) =
        (uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS
        + (size_t)slot * AM2_OBJ_TABLE_REC_SIZE;
    *(void **)(o + OBJ_OFF_TABLE_REC_KIND) =
        (uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS
        + (size_t)kind * AM2_OBJ_TABLE_REC_SIZE;

    SetFieldInAll(o + OBJ_OFF_SUBRECORD,
                  *(void *const *)(o + OBJ_OFF_TABLE_REC_KIND));

    RowUpdate(*(void **)(o + OBJ_OFF_ROWS), 1,
              (void *)(uintptr_t)ADDR_MAP_DESC);
}

/* PickWeaponSlot -- original 0x00406800, one caller.
 *
 * Which of a unit's six inventory slots should take this weapon, and may it be
 * taken at all? The slot is written to an out-parameter and the permission is
 * the return value, so the two answers are separate and a caller must read
 * both.
 *
 * THE OUT-PARAMETER CARRIES THREE KINDS OF ANSWER. -1 means the candidate's
 * kind needs no slot at all and the function returns at once; -2 means all six
 * slots hold something and none of them settled the question; anything else is
 * the index to use. Only the last is a slot.
 *
 * IT STOPS AT THE FIRST SLOT THAT DECIDES, NOT THE FIRST FREE ONE. The walk
 * ends when it finds an empty slot, a slot whose weapon shares this one's AAI
 * record, or a slot whose weapon is kind-compatible with it -- and the last
 * two RETURN A VERDICT rather than a slot to fill, which is why the return is
 * not simply "found". A caller taking the index without the verdict would put
 * a weapon into an occupied slot.
 *
 * AN UNRESOLVABLE UID IS TREATED AS AN EMPTY SLOT AND REPAIRED IN PASSING:
 * WeaponByUid answering NULL makes the function zero that slot and answer 1
 * with the index still pointing at it. So a stale uid becomes the slot the
 * caller fills, which is tidier than it looks -- it is the only place that
 * clears one.
 *
 * THE SAME-RECORD TEST ANSWERS FROM A THIRD FIELD. When a slot's weapon has
 * the same OBJ_OFF_FIELD_94 as the candidate, the verdict is whether the
 * CANDIDATE's OBJ_OFF_TARGET_UID is not -1 -- nothing about the slot. So "you
 * already carry one of these" is resolved by a property of the thing being
 * offered.
 *
 * The kind-compatibility arm requires BOTH weapons to be in set B before it
 * asks whether the kinds are compatible; a candidate outside that set never
 * reaches TypesCompatible and the walk moves on.
 */
int32_t __cdecl PickWeaponSlot(void *cand, void *unit, int32_t *slot)
{
    uint8_t *c = (uint8_t *)cand;
    uint8_t *u = (uint8_t *)unit;

    if (IsKind14Or22(**(const int32_t *const *)(c + OBJ_OFF_FIELD_C0))) {
        *slot = AM2_SLOT_NONE_NEEDED;
        return 1;
    }

    *slot = 0;

    if (!*(const uint32_t *)(u + UNIT_OFF_INVENTORY))
        return 1;

    for (;;) {
        uint32_t uid = *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                           + (size_t)*slot * 4);
        uint8_t *w   = (uint8_t *)WeaponByUid(uid);

        if (!w) {
            *(uint32_t *)(u + UNIT_OFF_INVENTORY + (size_t)*slot * 4) = 0;
            return 1;
        }

        if (*(void *const *)(w + OBJ_OFF_FIELD_94)
            == *(void *const *)(c + OBJ_OFF_FIELD_94))
            return *(const int32_t *)(c + OBJ_OFF_TARGET_UID) != -1;

        if (KindInSetB(**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))
            && KindInSetB(**(const int32_t *const *)(c + OBJ_OFF_FIELD_C0)))
            return TypesCompatible(
                       **(const int32_t *const *)(w + OBJ_OFF_FIELD_C0),
                       **(const int32_t *const *)(c + OBJ_OFF_FIELD_C0))
                   ? 1 : 0;

        (*slot)++;

        if (*slot >= AM2_INVENTORY_SLOTS) {
            *slot = AM2_SLOT_ALL_FULL;
            return 1;
        }

        if (!*(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                 + (size_t)*slot * 4))
            return 1;
    }
}

/* TryTakeWeapon -- original 0x00406720, two callers, and PickWeaponSlot's only
 * caller.
 *
 * Should this unit take this weapon? Four refusals, then a value; and when the
 * inventory is full, the unit drops its least valuable weapon to make room.
 *
 * IT ANSWERS A VALUE, NOT A BOOLEAN. Every success returns the candidate's
 * ADDR_THING_CODE and every refusal returns 0 -- so a thing whose code is 0 is
 * indistinguishable from a refusal, and both callers get the same answer for
 * either. That is the original's, and it is why the code is computed BEFORE
 * the drop rather than after: the return has to survive the whole tail.
 *
 * THE AMMO TEST IS THE ONE REFUSAL THAT IS NOT ABOUT SLOTS. When
 * PickWeaponSlot points at an occupied slot, the weapon already there is
 * refused only if its ITEM_OFF_AMMO is at least the candidate type's
 * ITEMTYPE_OFF_CAPACITY -- i.e. the one you hold is full. Anything less and
 * the take proceeds. So "already carrying one" is not a refusal by itself.
 *
 * THE FULL-INVENTORY WALK STARTS AT SLOT 1 AND SEEDS ITS BEST WITH SLOT 0's
 * ANSWER -- which is the CANDIDATE's code, not slot 0's. The seed is the value
 * computed a moment earlier for the thing being offered, so a weapon worth
 * less than everything carried finds no victim, `*slot` stays -2, and the
 * function refuses. That is the mechanism by which a unit declines to swap
 * down, and it is invisible unless the seed is read carefully.
 *
 * IT REUSES ITS OWN ARGUMENT SLOT AS THE OUT-PARAMETER. `lea eax, [esp+0xC]`
 * points at where `cand` was pushed; the pointer handed to PickWeaponSlot is
 * that stack slot. Harmless -- `cand` is already in a register -- and the
 * reason this needs a local where the disassembly appears not to.
 *
 * The drop goes through TrooperDropItem at the unit's own position, so what is
 * dropped lands where the unit stands.
 */
int32_t __cdecl TryTakeWeapon(void *cand, void *unit)
{
    uint8_t *c = (uint8_t *)cand;
    uint8_t *u = (uint8_t *)unit;
    int32_t  slot;
    int32_t  code;

    if (!ObjIsType4((const AM2_Object *)c))
        return 0;

    if (!PickWeaponSlot(c, u, &slot))
        return 0;

    if (slot != AM2_SLOT_NONE_NEEDED && slot != AM2_SLOT_ALL_FULL) {
        uint32_t uid = *(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                           + (size_t)slot * 4);

        if (uid) {
            const uint8_t *held = (const uint8_t *)WeaponByUid(uid);

            if (*(const int32_t *)(held + ITEM_OFF_AMMO)
                >= *(const int32_t *)(*(const uint8_t *const *)
                        (c + OBJ_OFF_FIELD_C0) + ITEMTYPE_OFF_CAPACITY))
                return 0;
        }
    }

    code = ThingCode(c, u);

    if (slot == AM2_SLOT_ALL_FULL && code > 0) {
        int32_t best = code;    /* the CANDIDATE's, not slot 0's */
        int32_t i;

        for (i = 1; i < AM2_INVENTORY_SLOTS; i++) {
            int32_t v = ThingCode(
                WeaponByUid(*(const uint32_t *)(u + UNIT_OFF_INVENTORY
                                                + (size_t)i * 4)), u);

            if (v < best) {
                best = v;
                slot = i;
            }
        }

        if (slot == AM2_SLOT_ALL_FULL)
            return 0;           /* nothing carried is worth less */

        TrooperDropItem(u, slot, *(const uint32_t *)(u + OBJ_OFF_POS));
    }

    return code;
}

/* ForEachSelected -- original 0x004578A0, one caller.
 *
 * Call a function for every selected object, dropping the entries that no
 * longer resolve or have been destroyed as it goes.
 *
 * ITS TWO REMOVAL PATHS DISAGREE ABOUT ADVANCING, AND ONE OF THEM IS WRONG.
 * A uid that does not resolve is removed and the index is NOT stepped, so the
 * entry that shifts down is examined next -- the correct shape, and the same
 * one five other loops in this tree use. A DESTROYED object is removed and the
 * index IS stepped, so whatever shifts into that slot is skipped for this
 * pass. The two branches are four instructions apart and end in
 * `jmp 0x004578F9` and `jmp 0x004578F8` -- one instruction apart, the `inc`.
 *
 * The consequence is small and real: two adjacent destroyed selections leave
 * the second still in the list, still flagged, until the next call. Nothing
 * here loops until the list is stable. Reproduced exactly, because a caller
 * that depends on one pass clearing everything is depending on something the
 * original does not do.
 *
 * ONLY THE DESTROYED PATH CLEARS OBJ_FLAG_SELECTED. The unresolvable path
 * cannot -- there is no object to clear it on -- so a uid whose object has
 * been freed leaves no flag behind to worry about, while a destroyed one is
 * both unlisted and unflagged. The original spells that clear `and ah, 0xFB`,
 * a byte operation on the second byte of the dword, which is how it stays
 * distinct from the destroyed-bit test four instructions earlier.
 *
 * THE COUNT IS RE-READ FROM THE GLOBAL AT THE BOTTOM OF EVERY ITERATION, which
 * is what makes removing entries mid-walk safe at all. The items pointer is
 * re-read at the top for the same reason.
 *
 * The callback is called with the object and nothing else, and its answer is
 * discarded.
 */
void __cdecl ForEachSelected(void (__cdecl *fn)(void *obj))
{
    int32_t i = 0;

    while (i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT) {
        uint8_t *obj = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]);

        if (!obj) {
            ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
            continue;                   /* no step: the shifted entry is next */
        }

        if (*(const uint8_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED) {
            ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
            *(uint32_t *)(obj + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_SELECTED;
            i++;                        /* steps -- and so skips one */
            continue;
        }

        fn(obj);
        i++;
    }
}

/* 0x00429040, six callers. Advance an object along its facing for one frame
 * and re-link its map rows.
 *
 * FOUR arguments, and the arity came from the seam this replaces rather than
 * from the body: `unused` is never read, `tileArg` survives untouched to
 * ObjTileChanged at the end, and `reverse` is read once and its stack slot
 * then reused as the angle scratch. A three-argument reading put both live
 * arguments in the wrong places; tools/checkseams.py is what forced the old
 * typedef into view.
 *
 * ADDR_FRAME_DELTA_SEC scales everything, so OBJ_OFF_FIELD_44 is a SPEED and
 * ADDR_GRAVITY's 440.0 is an acceleration: the whole function is a per-frame
 * integrator with three sub-pixel accumulators, one per axis.
 *
 * The heading is not the facing directly. With OBJ_FLAG_SNAP_HEADING set and
 * an animation on row 0 it is rounded to one of that animation's directions
 * and re-expanded to eight bits -- RoundTo8(facing, bits) << (8 - bits), the
 * shift anim.h predicts. Then `reverse` adds half a turn and
 * ROW_OFF_HEADING_BIAS is added last. Note ROW_OFF_HEADING_BIAS is a ROW field
 * at 0x5C, not OBJ_OFF_REVEALED_UNTIL, which is also 0x5C on the other struct.
 *
 * X AND Y ARE NOT ROUNDED ALIKE, and that is reproduced rather than tidied.
 * The new X is stored into its float field and read back before its integer
 * part is taken off, so it rounds to float mid-expression; the new Y stays on
 * the x87 stack from the add right through to the subtraction and rounds once,
 * on the way in. Writing both as plain floats would silently change Y. The
 * `long double` is i386's 80-bit x87 type, which is what makes the difference
 * expressible at all, and _ftol truncates toward zero exactly as a C cast
 * does -- mapdraw.cpp records the same.
 *
 * Zero vertical velocity means NOT FALLING; see AM2_VEL_Z_MIN in orig.h.
 *
 * Two null dereferences of the original's are kept. With no rows the row
 * pointer stays null and ROW_OFF_ANIM_PLAYING is read off it anyway, as is
 * ROW_OFF_HEADING_BIAS. Same standing as LockSurface's descriptor after a
 * successful Restore. */
void __cdecl ObjMoveAlongFacing(void *obj, int32_t tileArg, int32_t unused,
                                int32_t reverse)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *row0 = (uint8_t *)0;
    uint8_t *anim;
    int32_t  i;

    (void)unused;   /* the original never reads its third argument */

    if (*(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS == 0)
        return;

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 0)
        row0 = *(uint8_t **)(o + OBJ_OFF_ROWS);

    /* Read off a null row0 when there are no rows -- the original's. */
    anim = *(uint8_t **)(row0 + ROW_OFF_ANIM_PLAYING);
    if (anim != (uint8_t *)0) {
        const float scale = *(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC;
        uint8_t     angle = *(const uint8_t *)(o + OBJ_OFF_FACING);
        int32_t     speed;

        if ((*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SNAP_HEADING)
            && row0 != (uint8_t *)0) {
            uint32_t bits = ((const AM2_Anim *)anim)->directionBits;

            angle = (uint8_t)RoundTo8((int32_t)angle, bits);
            /* Re-read the animation off the row, as the original does. */
            bits = ((const AM2_Anim *)
                    *(uint8_t **)(row0 + ROW_OFF_ANIM_PLAYING))->directionBits;
            angle = (uint8_t)((uint32_t)angle << (8u - bits));
        }

        if (reverse != 0)
            angle = (uint8_t)(angle + 0x80u);
        angle = (uint8_t)(angle
                          + *(const uint8_t *)(row0 + ROW_OFF_HEADING_BIAS));

        speed = *(const int32_t *)(o + OBJ_OFF_FIELD_44);
        if (speed != 0) {
            float       step;
            long double ny;
            int32_t     ix, iy;

            step = (float)((long double)speed * (long double)scale);

            /* Stored and read back: X rounds to float here. */
            *(float *)(o + OBJ_OFF_SUBPIXEL_X) = (float)
                ((long double)Cos8(angle) * (long double)step
                 + (long double)*(const float *)(o + OBJ_OFF_SUBPIXEL_X));
            /* Never stored: Y stays at 80 bits until its own store below. */
            ny = (long double)Sin8(angle) * (long double)step
                 + (long double)*(const float *)(o + OBJ_OFF_SUBPIXEL_Y);

            ix = (int32_t)*(const float *)(o + OBJ_OFF_SUBPIXEL_X);
            iy = (int32_t)ny;

            *(int16_t *)(o + OBJ_OFF_X) =
                (int16_t)(*(const int16_t *)(o + OBJ_OFF_X) + (int16_t)ix);
            *(int16_t *)(o + OBJ_OFF_Y) =
                (int16_t)(*(const int16_t *)(o + OBJ_OFF_Y) + (int16_t)iy);

            *(float *)(o + OBJ_OFF_SUBPIXEL_X) = (float)
                ((long double)*(const float *)(o + OBJ_OFF_SUBPIXEL_X)
                 - (long double)ix);
            *(float *)(o + OBJ_OFF_SUBPIXEL_Y) =
                (float)(ny - (long double)iy);
        }

        if (*(const float *)(o + OBJ_OFF_VEL_Z) != 0.0f) {
            long double dz = (long double)scale
                             * (long double)*(const float *)
                               (o + OBJ_OFF_VEL_Z);
            int32_t     iz = (int32_t)dz;

            *(int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST) = (int16_t)
                (*(const int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST) + (int16_t)iz);
            *(float *)(o + OBJ_OFF_SUBPIXEL_Z) = (float)
                (dz + (long double)*(const float *)(o + OBJ_OFF_SUBPIXEL_Z)
                 - (long double)iz);

            if (*(const int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST) < 0) {
                *(int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST) = 0;
                *(int32_t *)(o + OBJ_OFF_VEL_Z) = 0;
            } else {
                *(float *)(o + OBJ_OFF_VEL_Z) = (float)
                    ((long double)*(const float *)(o + OBJ_OFF_VEL_Z)
                     - (long double)*(const float *)(uintptr_t)ADDR_GRAVITY
                       * (long double)scale);
                if (*(const float *)(o + OBJ_OFF_VEL_Z) == 0.0f)
                    *(uint32_t *)(o + OBJ_OFF_VEL_Z) = AM2_VEL_Z_MIN;
            }
        }
    }

    if (row0 != (uint8_t *)0) {
        if (*(void **)(row0 + ROW_OFF_SPRITE) == (void *)0)
            return;
        *(int32_t *)(row0 + ROW_OFF_X) = *(const int32_t *)(o + OBJ_OFF_POS);
        *(int16_t *)(row0 + ROW_OFF_Y_ADJUST) =
            *(const int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST);
        RowUpdate(row0, 0, (void *)AM2_IMAGE(ADDR_MAP_DESC));
    }

    for (i = 1; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        uint8_t       *r = *(uint8_t **)(o + OBJ_OFF_ROWS)
                           + (uint32_t)i * AM2_OBJ_ROW_STRIDE;
        const uint8_t *spr;

        *(int32_t *)(r + ROW_OFF_X) = *(const int32_t *)(o + OBJ_OFF_POS);
        spr = *(const uint8_t **)(row0 + ROW_OFF_SPRITE);
        *(int16_t *)(r + ROW_OFF_X) = (int16_t)
            (*(const int16_t *)(r + ROW_OFF_X)
             + *(const int16_t *)(spr + SPRITE_OFF_ATTACH_X));
        spr = *(const uint8_t **)(row0 + ROW_OFF_SPRITE);
        *(int16_t *)(r + ROW_OFF_Y) = (int16_t)
            (*(const int16_t *)(r + ROW_OFF_Y)
             + *(const int16_t *)(spr + SPRITE_OFF_ATTACH_Y));
        RowUpdate(r, 0, (void *)AM2_IMAGE(ADDR_MAP_DESC));
    }

    ObjTileChanged(o, tileArg, 0);
}

/* 0x0045B9F0, one caller -- the type-3 arm of the deploy dispatcher. Place a
 * vehicle at a point.
 *
 * Clear OBJ_FLAG_DESTROYED, take the nearest clear point for the vehicle's
 * facing, stamp the tile height, then relink row 0 and every sub-part. That
 * sub-part loop is the same idiom ObjMoveAlongFacing uses -- stride
 * AM2_OBJ_ROW_STRIDE, each row offset by row 0's sprite attach point -- which
 * is independent corroboration of that reading, since the two functions were
 * read days apart in different families.
 *
 * TWO THINGS READ BACKWARDS IN PROSE AND ARE REPRODUCED AS ENCODED.
 *
 * The ten-dword clear at OBJ_OFF_FIELD_578 covers 0x578..0x5A0, and the facing
 * is stamped into 0x578 and OBJ_OFF_FACING_COPY2 AFTERWARDS. Doing the stamps
 * first -- the natural order to write -- zeroes them.
 *
 * And the attack mode is DEAD CODE. A vehicle whose owner is not the local
 * army gets OBJ_OFF_AI_MODE = 6, and the unconditional `= 1` a few stores
 * later overwrites it with no call or read in between. Kept as the original
 * has it; no A/B could show this, since both sides do the same thing. */
void __cdecl DeployVehicle(void *obj, int32_t x, int32_t y,
                           int32_t resurrect)
{
    uint8_t  *o = (uint8_t *)obj;
    uint8_t  *row0;
    AM2_Point at;
    uint32_t  where;
    uint8_t   height;
    int32_t   i;

    at.x = (int16_t)x;
    at.y = (int16_t)y;
    where = *(const uint32_t *)&at;

    *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_DESTROYED;

    NearestClearVehiclePoint(o, *(const uint8_t *)(o + OBJ_OFF_FACING),
                             where, &at);
    *(uint32_t *)(o + OBJ_OFF_POS) = *(const uint32_t *)&at;

    height = HeightAtPoint(*(const uint32_t *)(o + OBJ_OFF_POS));
    *(o + OBJ_OFF_HEIGHT_SET) = height;
    ObjTileChanged(o, (int32_t)(int8_t)height, 1);

    row0 = *(uint8_t **)(o + OBJ_OFF_ROWS);
    *(uint32_t *)(row0 + ROW_OFF_X) = *(const uint32_t *)(o + OBJ_OFF_POS);
    ObjFlagSet0(row0);
    RowUpdate(row0, 0, (void *)AM2_IMAGE(ADDR_MAP_DESC));

    *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;

    for (i = 1; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        uint8_t       *r = *(uint8_t **)(o + OBJ_OFF_ROWS)
                           + (uint32_t)i * AM2_OBJ_ROW_STRIDE;
        const uint8_t *spr;

        *(uint32_t *)(r + ROW_OFF_X) = *(const uint32_t *)(o + OBJ_OFF_POS);
        spr = *(const uint8_t **)(row0 + ROW_OFF_SPRITE);
        *(int16_t *)(r + ROW_OFF_X) = (int16_t)
            (*(const int16_t *)(r + ROW_OFF_X)
             + *(const int16_t *)(spr + SPRITE_OFF_ATTACH_X));
        spr = *(const uint8_t **)(row0 + ROW_OFF_SPRITE);
        *(int16_t *)(r + ROW_OFF_Y) = (int16_t)
            (*(const int16_t *)(r + ROW_OFF_Y)
             + *(const int16_t *)(spr + SPRITE_OFF_ATTACH_Y));
        ObjFlagSet0(r);
        RowUpdate(r, 0, (void *)AM2_IMAGE(ADDR_MAP_DESC));
    }

    ResetType2Fields(o);
    memset(o + OBJ_OFF_FIELD_578, 0, AM2_DEPLOY_CLEAR_DWORDS * 4u);

    /* Gated on `resurrect`, the fourth argument -- not on the position. The
     * original reads it at [esp+0x24] after two deferred cleanups, which is
     * arg3 and not the packed point that shares a slot with arg0. */
    if (resurrect != 0) {
        uint8_t facing = *(const uint8_t *)(o + OBJ_OFF_FACING);

        *(o + OBJ_OFF_FIELD_530)   = facing;
        *(o + OBJ_OFF_FACING_COPY) = facing;
        *(o + OBJ_OFF_FIELD_578)   = facing;
        *(o + OBJ_OFF_FACING_COPY2) = facing;
        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_BIT0;
        *(int32_t *)(o + OBJ_OFF_RANK) = 0;
        *(int32_t *)(o + OBJ_OFF_REPAIR_FRAME) = 0;

        ObjRemap(o, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC), 1);

        if (!ObjIsFriendly(o))
            ObjConceal(o, 0);

        if ((int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY)
            != *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
            *(int32_t *)(o + OBJ_OFF_AI_MODE) = 6;   /* dead: see above */

        /* Re-stamped after the clear, and this is why the clear must come
         * first: 0x578 and 0x579 are inside it. */
        *(o + OBJ_OFF_FIELD_578)    = *(const uint8_t *)(o + OBJ_OFF_FACING);
        *(o + OBJ_OFF_FACING_COPY2) = *(const uint8_t *)(o + OBJ_OFF_FIELD_530);
        *(int32_t *)(o + OBJ_OFF_FIELD_59C) = 0;
        *(int32_t *)(o + OBJ_OFF_FIELD_580) = 1;
        *(int32_t *)(o + OBJ_OFF_FIELD_584) = 0;
        *(int32_t *)(o + OBJ_OFF_FIELD_58C) = 0;
        *(int32_t *)(o + OBJ_OFF_FIELD_588) = 0;
        *(int32_t *)(o + OBJ_OFF_AI_MODE) = 1;
    }

    SetKindFrames(o, 1);
    ObjSetFootprint(o);
}

/* 0x00449250, one caller -- the type-2 arm of the deploy dispatcher. Place a
 * trooper at a point. It names itself in its first log line.
 *
 * The near-twin of DeployVehicle above, and the three DIFFERENCES are the
 * content: this clears from OBJ_OFF_SIGHT_OUT_T2 rather than
 * OBJ_OFF_FIELD_578 -- each deploy clears its own type's sight-output block,
 * and the two records start four bytes apart -- RowUpdate takes force 1 rather
 * than 0, and an OBJ_OFF_SARGE guard decides whether rank and repair-frame are
 * cleared. Writing this from the sibling erases all three.
 *
 * The `resurrect` gate is the same, and derived the same way: the original
 * reads it at [esp+0x4C] BEFORE an `add esp, 0x2C`, which is arg3 and not the
 * packed point that shares a slot with arg2.
 *
 * Two things reproduced rather than tidied. OBJ_OFF_AI_MODE = 6 for a foreign
 * owner is dead -- the unconditional = 1 below overwrites it with no read
 * between -- and the sibling has the identical dead store, which is what marks
 * it a template artefact rather than a misreading here.
 *
 * The tail is a rule worth knowing: a freshly deployed Sarge of our own army
 * selects himself when nothing else is selected. */
void __cdecl DeployTrooper(void *obj, int32_t x, int32_t y, int32_t resurrect)
{
    uint8_t  *o = (uint8_t *)obj;
    uint8_t  *row0;
    AM2_Point at;
    uint8_t   height;

    if (*(const int32_t *)(*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT
                           + COMM_OFF_VERBOSE) != 0)
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_DEPLOY_TROOPER),
                 ((const AM2_Object *)o)->uid, x, y);

    *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_DESTROYED;

    at.x = (int16_t)x;
    at.y = (int16_t)y;
    NearestClearPoint(*(const uint32_t *)&at, &at);
    *(uint32_t *)(o + OBJ_OFF_POS) = *(const uint32_t *)&at;

    height = HeightAtPoint(*(const uint32_t *)(o + OBJ_OFF_POS));
    *(o + OBJ_OFF_HEIGHT_SET) = height;
    ObjTileChanged(o, (int32_t)(int8_t)height, 1);

    row0 = *(uint8_t **)(o + OBJ_OFF_ROWS);
    *(uint32_t *)(row0 + ROW_OFF_X) = *(const uint32_t *)(o + OBJ_OFF_POS);
    ObjFlagSet0(row0);
    RowUpdate(row0, 1, (void *)AM2_IMAGE(ADDR_MAP_DESC));   /* force 1 here */

    *(uint32_t *)(o + OBJ_OFF_FIELD_C0) =
        *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT;
    ResetType2Fields(o);
    memset(o + OBJ_OFF_SIGHT_OUT_T2, 0, AM2_DEPLOY_CLEAR_DWORDS * 4u);

    if (resurrect != 0) {
        uint8_t  facing = *(const uint8_t *)(o + OBJ_OFF_FACING);
        void    *weapon;

        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_BIT0;

        if (*(const int32_t *)(o + OBJ_OFF_SARGE) == 0) {
            *(int32_t *)(o + OBJ_OFF_RANK) = 0;
            *(int32_t *)(o + OBJ_OFF_REPAIR_FRAME) = 0;
        }

        *(int16_t *)(o + OBJ_OFF_FIELD_574) = (int16_t)facing;
        *(o + OBJ_OFF_FACING_COPY) = facing;
        *(o + OBJ_OFF_FIELD_580)   = facing;
        SetUnitPose(o, 1);

        *(int32_t *)(o + OBJ_OFF_RIDING)    = 0;
        *(int32_t *)(o + OBJ_OFF_FIELD_5A4) = 0;
        ObjRemap(o, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC), 1);
        *(int32_t *)(o + OBJ_OFF_POSE) = 0;

        if (!ObjIsFriendly(o))
            ObjConceal(o, 0);

        if ((int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY)
            != *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
            *(int32_t *)(o + OBJ_OFF_AI_MODE) = 6;   /* dead: see above */

        weapon = WeaponByUid(*(const uint32_t *)(o + OBJ_OFF_WEAPON_UID));
        if (weapon != 0)
            SoldierKindForWeapon(o,
                **(const uint32_t **)((const uint8_t *)weapon
                                      + OBJ_OFF_FIELD_C0));

        *(int32_t *)(o + OBJ_OFF_FIELD_568) = 0;
        SetUnitPose(o, 1);

        if (*(const int32_t *)(o + OBJ_OFF_FIELD_530) != AM2_DEPLOY_KIND_DONE) {
            int32_t slot = *(const int32_t *)(o + OBJ_OFF_TABLE_REC_SLOT);

            *(int32_t *)(o + OBJ_OFF_FIELD_530) = AM2_DEPLOY_KIND_DONE;
            *(int32_t *)(o + OBJ_OFF_TABLE_REC_KIND) = slot;
            *(int32_t *)(o + OBJ_OFF_TABLE_REC_SLOT) = 0;
            SetFieldInAll(o + OBJ_OFF_SUBRECORD, (void *)(uintptr_t)slot);
        }

        *(int32_t *)(o + OBJ_OFF_SIGHT_OUT_T2) = 0;
        *(int32_t *)(o + OBJ_OFF_FIELD_584) = 1;
        *(o + OBJ_OFF_FIELD_580) = *(const uint8_t *)(o + OBJ_OFF_FACING);
        *(int32_t *)(o + OBJ_OFF_FIELD_588) = 0;
        *(int32_t *)(o + OBJ_OFF_FIELD_598) = 0;
        *(int32_t *)(o + OBJ_OFF_AI_MODE) = 1;
    }

    if (*(const int32_t *)(o + OBJ_OFF_SARGE) != 0
        && (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY)
           == *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER
        && *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT <= 0)
        SelectUnit(o);
}

/* 0x00429D00, three callers -- both deploys and the stepper. Recompute an
 * object's hit rect after it has moved, and re-link it into the map's cell
 * lists.
 *
 * Three exits and they mean different things. Unchanged position with `force`
 * clear, or the object not flagged, simply returns. A box that falls off the
 * grid calls ItemPreDestroy first, which unlinks it from every cell it was in
 * -- so leaving the map and being unregistered are the same operation.
 *
 * The cell coordinates come out of an arithmetic that reads oddly and is
 * transcribed rather than simplified: the original reassigns the register
 * holding `right` to the BOTTOM OFFSET partway through, so what looks like
 * `right - topOffset + top` is really `bottomOffset - topOffset + top`, which
 * is the bottom. Simplifying from the register names rather than the values
 * gets it wrong.
 *
 * The clamps are the `xor/setle/dec/and` idiom, which is `max(v, 0)` without a
 * branch. Written as the comparison it is.
 *
 * PointsEqual takes its two points BY VALUE -- rect.h has the signature, and
 * CLAUDE.md records a defect from calling it with pointers instead. */
void __cdecl ObjRemap(void *obj, void *desc, int32_t force)
{
    uint8_t       *o = (uint8_t *)obj;
    const int32_t *grid = (const int32_t *)desc;
    const int32_t *box;
    int32_t       *hit;
    int32_t        x, y, left, top, right, bottom;
    int32_t        cl, ct, cr, cb, row, col, idx;

    if (force == 0
        && PointsEqual(*(const uint32_t *)(o + OBJ_OFF_PREV_POS),
                       *(const uint32_t *)(o + OBJ_OFF_POS)))
        return;

    x = *(const int16_t *)(o + OBJ_OFF_X);
    y = *(const int16_t *)(o + OBJ_OFF_Y);
    box = (const int32_t *)(o + OBJ_OFF_BOX_OFFSETS);
    hit = (int32_t *)(o + OBJ_OFF_HIT_RECT);

    left   = x + box[0];
    top    = y + box[1];
    right  = x + box[2];
    bottom = y + box[3];
    hit[0] = left;
    hit[1] = top;
    hit[2] = right;
    hit[3] = bottom;

    if (!(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_BIT0))
        return;

    cl = left   >> AM2_CELL_SHIFT;
    ct = top    >> AM2_CELL_SHIFT;
    cr = (left + (box[2] - box[0]))   >> AM2_CELL_SHIFT;
    cb = (top  + (box[3] - box[1]))   >> AM2_CELL_SHIFT;

    /* Off the grid in any direction is an unregister, not a clamp. */
    if (cb < 0 || ct > grid[1] - 1 || cr < 0 || cl > grid[0] - 1) {
        ItemPreDestroy(o, (int32_t)(uintptr_t)desc);
        return;
    }

    if (cl < 0) cl = 0;
    if (ct < 0) ct = 0;
    if (cr > grid[0] - 1) cr = grid[0] - 1;
    if (cb > grid[0] - 1) cb = grid[0] - 1;

    idx = 0;
    for (row = ct; row <= cb; row++) {
        int32_t cell = (row << grid[2]) + cl;

        for (col = cl; col <= cr; col++, cell++, idx++) {
            uint8_t *entry = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES)
                             + (uint32_t)idx * AM2_CELL_ENTRY_STRIDE;
            int32_t  was = *(const int32_t *)(entry + CELL_ENTRY_OFF_INDEX);

            if (was == cell)
                continue;
            if (was >= 0)
                ListUnlink(entry,
                           (void **)(*(uint8_t **)((uint8_t *)desc + CELLS_OFF_HEADS)
                                     + (uint32_t)was * 4u));
            *(int32_t *)(entry + CELL_ENTRY_OFF_INDEX) = cell;
            ListPushFront(entry,
                          (void **)(*(uint8_t **)((uint8_t *)desc + CELLS_OFF_HEADS)
                                    + (uint32_t)cell * 4u));
        }
    }

    /* Whatever the object was linked into beyond the cells it now covers is
     * released, and -1 written back so a second pass sees it unlinked. */
    for (; idx < (int32_t)*(const uint8_t *)(o + OBJ_OFF_CELL_COUNT); idx++) {
        uint8_t *entry = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES)
                         + (uint32_t)idx * AM2_CELL_ENTRY_STRIDE;
        int32_t  was = *(const int32_t *)(entry + CELL_ENTRY_OFF_INDEX);

        if (was < 0)
            return;
        ListUnlink(entry, (void **)(*(uint8_t **)((uint8_t *)desc + CELLS_OFF_HEADS)
                                    + (uint32_t)was * 4u));
        *(int32_t *)(entry + CELL_ENTRY_OFF_INDEX) = -1;
    }
}

/* 0x00427990, eight callers. The selection changed: drop what is gone,
 * promote a leader, and set the pointer to match.
 *
 * ITS ARGUMENT IS NEVER READ. Eight callers dutifully push a packed point and
 * nothing in the body touches it -- checked by looking for the parameter slot
 * at every push depth, not by reading past it once. The signature stays
 * because the callers are cdecl and orig.h already declares it.
 *
 * Three things happen to the selection list, which is ADDR_SELECTED_UIDS in
 * the ordinary sub-list shape: a count at +4 and the uid array at +8.
 *
 *   - an entry whose uid no longer resolves through ADDR_OBJ_TABLE is dropped;
 *   - a destroyed one is dropped AND has OBJ_FLAG_DESTROYED cleared on the way
 *     out, which is the only place that bit is taken off a dead unit here;
 *   - what survives is classified, and the winner is SWAPPED TO INDEX 0 -- the
 *     selection's leader is literally its first element.
 *
 * THE AI-MODE ACCUMULATOR IS WHY THE POINTER OFTEN DOES NOT CHANGE, and it
 * explains a note orig.h already carried without a mechanism. It starts at 8
 * and collapses to 0 the moment two selected units disagree, so "the pointer
 * mode follows the selection" is really "follows a selection that AGREES WITH
 * ITSELF". Select three units with different modes and SetPointerMode is never
 * reached.
 *
 * THE FINAL DISPATCH IS A JUMP TABLE AND WAS READ AS ONE. Eight indices,
 * FOUR distinct arms: 6 shares 0's and 7 shares 1's, while 3, 4 and 5 fall to
 * a default that calls nothing. With orig.h's mode names that is attack
 * drawing mode 0's cursor, defend drawing mode 1's, ignore its own, and evade
 * changing nothing. Numbering the arms top to bottom gives 0,1,2,3... and
 * loses the sharing entirely -- the SpriteKeyForKind trap. */
void __cdecl OnSelectionChanged(uint32_t unusedPoint)
{
    uint8_t  *list = (uint8_t *)(uintptr_t)ADDR_SELECTED_UIDS;
    uint8_t  *local;
    uint8_t  *leader = 0;
    int32_t   leaderIdx = 0;
    int32_t   mode = 1;
    int32_t   aiMode = 8;
    int32_t   maxRank = -1;
    int32_t   i, slot;

    (void)unusedPoint;

    local = (uint8_t *)LookupOwnerObj(
                *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);

    if (*(const int32_t *)(list + SUBREC_OFF_COUNT) <= 0)
        return;

    for (i = 0; i < *(const int32_t *)(list + SUBREC_OFF_COUNT); ) {
        const uint32_t *uids = *(const uint32_t **)(list + SUBREC_OFF_ROWS);
        uint8_t        *obj = 0;

        slot = FindSlot(uids[i], &slot);
        if (slot >= 0)
            obj = (uint8_t *)g_objTable[slot].obj;

        if (obj == 0) {
            ListRemoveAt(list, i);
            continue;
        }

        if (*(const uint32_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED) {
            ListRemoveAt(list, i);
            *(uint32_t *)(obj + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_DESTROYED;
            continue;
        }

        if (ObjIsTypeIn238((const AM2_Object *)obj)) {
            if (ObjType2Field548((const AM2_Object *)obj)) {
                leader = obj;
                leaderIdx = i;
                mode = 0;
                break;
            }
            if (ObjIsType3((const AM2_Object *)obj)) {
                if (local != 0
                    && *(const uint32_t *)(local + OBJ_OFF_RIDING)
                       == ((const AM2_Object *)obj)->uid) {
                    leader = obj;
                    leaderIdx = i;
                    mode = 0;
                    break;
                }
                if (maxRank < 8) {
                    maxRank = 8;
                    leaderIdx = i;
                    leader = obj;
                    if (aiMode == 8)
                        aiMode = *(const int32_t *)(obj + OBJ_OFF_AI_MODE);
                    else if (aiMode != *(const int32_t *)(obj + OBJ_OFF_AI_MODE))
                        mode = 0;
                }
            } else {
                if (*(const int32_t *)(obj + OBJ_OFF_RANK) > maxRank) {
                    maxRank = *(const int32_t *)(obj + OBJ_OFF_RANK);
                    leaderIdx = i;
                    leader = obj;
                }
                if (aiMode == 8)
                    aiMode = *(const int32_t *)(obj + OBJ_OFF_AI_MODE);
                else if (aiMode != *(const int32_t *)(obj + OBJ_OFF_AI_MODE))
                    mode = 0;
            }
        }
        i++;
    }

    /* The leader becomes element zero. */
    {
        uint32_t *uids = *(uint32_t **)(list + SUBREC_OFF_ROWS);
        uint32_t  first = uids[0];

        uids[leaderIdx] = first;
        uids = *(uint32_t **)(list + SUBREC_OFF_ROWS);
        uids[0] = first;
    }

    if (leader != 0)
        SetObjContext(leader);

    if (mode == 0 || (uint32_t)aiMode > 7u)
        return;

    switch (aiMode) {
    case 0: case 6: SetPointerMode(4); break;
    case 1: case 7: SetPointerMode(5); break;
    case 2:         SetPointerMode(6); break;
    default:        break;   /* 3, 4, 5 change nothing */
    }
}

/* 0x004335F0, two callers. Can this unit pick this weapon up, and into which
 * of its AM2_WEAPON_SLOTS slots? The slot goes to `slot`; `already` is set to
 * 1 for the one case that means "this is the weapon you are holding".
 *
 * EIGHT SEPARATE EXITS. Its neighbours in this family converge to one tail and
 * this does not -- each exit pops independently with its own side effect, so
 * they are written as returns because they ARE returns. Counting them before
 * writing is what stops eight wrong gotos; StepType2 cost three attempts for
 * exactly the opposite mistake.
 *
 * The two dispatches through 0x00433768 and 0x00433790 look like 29-way jump
 * tables and are not: both have TWO arms and an identical byte index, so they
 * encode a PREDICATE over item types -- ids 1, 7, 8, 9, 10 and 29 on one side
 * -- applied first to the held weapon and then to the candidate. Counting the
 * entries said "two 29-way dispatches"; reading them said "one six-element set
 * used twice".
 *
 * The `target != -1` test is reproduced although it decides nothing: both its
 * arms compute `other->OBJ_OFF_TARGET_UID > 0`. See orig.h. */
int32_t __cdecl CanPickUpWeapon(void *weapon, void *unit, int32_t *slot,
                                int32_t *already)
{
    uint8_t *w = (uint8_t *)weapon;
    uint8_t *u = (uint8_t *)unit;

    if (!ObjIsType4((const AM2_Object *)w))
        return 0;

    if (*(const uint32_t *)(u + OBJ_OFF_HELD_WEAPON_UID)
        == ((const AM2_Object *)w)->uid) {
        *already = 1;
        return 0;
    }

    if (*(const uint32_t *)(w + OBJ_OFF_PICKUP_AFTER)
        > *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS)
        return 0;

    if (IsKind14Or22(**(const int32_t **)(w + OBJ_OFF_FIELD_C0))) {
        *slot = -1;
        return 1;
    }

    *slot = 0;
    if (*(const uint32_t *)(u + OBJ_OFF_WEAPON_UID) == 0)
        return 1;

    for (;;) {
        uint8_t *other = (uint8_t *)WeaponByUid(
            *(const uint32_t *)(u + OBJ_OFF_WEAPON_UID
                                + (uint32_t)*slot * 4u));

        if (other == 0) {
            /* A slot naming a weapon that no longer exists is emptied. */
            *(uint32_t *)(u + OBJ_OFF_WEAPON_UID
                          + (uint32_t)*slot * 4u) = 0;
            return 1;
        }

        if (*(const int32_t *)(other + OBJ_OFF_FIELD_94)
            == *(const int32_t *)(w + OBJ_OFF_FIELD_94)) {
            /* Both arms of the original's test compute this; see orig.h. */
            return *(const int32_t *)(other + OBJ_OFF_TARGET_UID) > 0;
        }

        if (AM2_ITEM_KIND_IS_SPECIAL(**(const int32_t **)
                                     (other + OBJ_OFF_FIELD_C0))
            && AM2_ITEM_KIND_IS_SPECIAL(**(const int32_t **)
                                        (w + OBJ_OFF_FIELD_C0)))
            return TypesCompatible(**(const int32_t **)
                                   (other + OBJ_OFF_FIELD_C0),
                                   **(const int32_t **)
                                   (w + OBJ_OFF_FIELD_C0)) != 0;

        ++*slot;
        if (*slot >= AM2_WEAPON_SLOTS)
            return 0;
        if (*(const uint32_t *)(u + OBJ_OFF_WEAPON_UID
                                + (uint32_t)*slot * 4u) == 0)
            return 1;
    }
}

/* 0x00422B90, one caller -- the per-type step dispatcher's type 6 arm, which
 * is an EXPLOSION. See orig.h for how that was identified and for why its
 * fields carry BLAST_OFF_ names rather than the OBJ_OFF_ ones that share those
 * offsets on other types.
 *
 * Four parts, in this order:
 *
 *   a deadline at BLAST_OFF_DUE_MS -- while the clock has not passed it the
 *     function RETURNS, so nothing below runs; once it has, the deadline is
 *     cleared and row 0 is flagged;
 *   a one-shot sound, AM2_BLAST_SOUND at the blast's own position, cleared
 *     after it plays;
 *   the blast itself, over everything AllObjectsInRect finds in
 *     BLAST_OFF_RECT; and
 *   with BLAST_OFF_MODE at 5 or more, a spawn and a screen shake.
 *
 * The damage is HALVED for one class -- ObjIsType2 and ClassifyByCode74
 * answering 2 -- and the damage KIND is 3 or 1 by a rand roll, which the
 * OBJ_OFF_FIELD_94 == 0x89 test skips entirely. Reading `and eax, 0x255` as a
 * mask over a percentage would be wrong: it is the original's own roll and is
 * transcribed, not tidied.
 *
 * Its two exits are at DIFFERENT STACK DEPTHS because `push edi` and
 * `push ebx` happen inside the flow rather than in the prologue -- the ebx
 * pair brackets the blast loop and the branch that skips the loop jumps past
 * both. That is why the early return is a return and not a goto.
 *
 * The counter for this will read 0: its only caller is reconstructed, so the
 * call never crosses the patched entry. It runs -- the state dump reflects it
 * -- but the counter cannot show that. */
void __cdecl StepType6(void *obj)
{
    uint8_t   *o = (uint8_t *)obj;
    uint8_t   *victim;
    AM2_Point *pos = (AM2_Point *)(o + OBJ_OFF_POS);

    if (*(const uint32_t *)(o + BLAST_OFF_DUE_MS) > 0) {
        if (*(const uint32_t *)(o + BLAST_OFF_DUE_MS)
            > *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS)
            return;
        *(uint32_t *)(o + BLAST_OFF_DUE_MS) = 0;
        ObjFlagSet0(*(void **)(o + OBJ_OFF_ROWS));
    }

    if (*(const int32_t *)(o + BLAST_OFF_SOUND_PENDING) != 0) {
        PlaySoundAt(AM2_BLAST_SOUND, 0, 0, pos->x, pos->y);
        *(int32_t *)(o + BLAST_OFF_SOUND_PENDING) = 0;
    }

    if (*(const uint8_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS) + ROW_OFF_CELL) >= 4
        && *(const int32_t *)(o + BLAST_OFF_DAMAGE) > 0) {

        victim = (uint8_t *)AllObjectsInRect(
                     (const AM2_Rect *)(o + BLAST_OFF_RECT),
                     (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC);

        for (; victim != 0;
             victim = *(uint8_t **)(victim + OBJ_OFF_QUERY_NEXT)) {
            int32_t dmg = *(const int32_t *)(o + BLAST_OFF_DAMAGE);
            int32_t kind = 1;

            if (ObjIsType2((const AM2_Object *)victim)
                && ClassifyByCode74(victim) == 2)
                dmg = *(const int32_t *)(o + BLAST_OFF_DAMAGE) >> 1;

            if (*(const int32_t *)(o + OBJ_OFF_FIELD_94) != 0x89
                && (orig_rand() & 0x255) >= 0x40)
                kind = 3;

            DamageObject(victim, dmg, kind,
                         *(const uint32_t *)(o + BLAST_OFF_SOURCE_UID),
                         orig_rand() % 0xFF + 1, 0);
        }

        *(int32_t *)(o + BLAST_OFF_DAMAGE) = 0;

        if (*(const int32_t *)(o + BLAST_OFF_MODE) >= 5) {
            int32_t  spin = orig_rand() % 6;
            int32_t  tile;

            orig_blast_spin(pos->x, pos->y, spin);
            tile = TileOfPoint(*(const uint32_t *)pos) & 0xFFFF;

            if ((( *(const uint8_t **)(uintptr_t)ADDR_TILE_FLAGS)[tile] & 1)
                && (*(const uint8_t **)(uintptr_t)ADDR_CELL_WEIGHTS)[tile]
                   >= AM2_BLOCK_CLEAR)
                orig_spawn_at(pos->x, pos->y, 0x90,
                              *(const int8_t *)(o + OBJ_OFF_ARMY),
                              *(const uint32_t *)(o + BLAST_OFF_SOURCE_UID),
                              0, 0xC8, 0, 0, 0);

            ShakeAt(pos, *(const int32_t *)(o + BLAST_OFF_MODE));
            /* Once, not once a frame. */
            *(int32_t *)(o + BLAST_OFF_MODE) = 0;
        }
    }

    /* The second exit, and the one this function needs: an explosion whose
     * animation has run out flags itself and stops -- it does NOT step its
     * rows or move. Omitting this leaves a blast that never finishes, and
     * tools/checkoffsetuse.py is what caught it, by reporting OBJ_OFF_FLAGS
     * read by the original and named nowhere in the C. */
    if (RowAnimFinished(*(void **)(o + OBJ_OFF_ROWS))) {
        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;
        return;
    }

    StepObjRows(o);
    ObjMoveAlongFacing(o, *(const int8_t *)(o + OBJ_OFF_HEIGHT_SET), 0, 0);
}

/* CreateItem -- original 0x00433980, 672 bytes, NINE callers. The type-1 arm
 * of the four creators, and the one SendItemCreate's own note names first.
 *
 * IT IS TWO FUNCTIONS SHARING A GATE. DefFindLink decides which: a `key` that
 * the LINK table knows is a COMPOSITE, and this recurses to build a parent and
 * its numbered children; a key with no link is a LEAF, and this allocates the
 * object, fills it from the AAI record and registers its rows. The composite
 * arm never allocates anything itself -- every object it produces comes back
 * out of a recursive call that took the leaf arm.
 *
 * THE AUTHORITY GATE IS THE SAME TEST AT BOTH ENDS, read in opposite senses.
 * On entry: in a multiplayer session, with `remote` zero, a machine that
 * CommMustBroadcast refuses may not create the object at all. On exit: with
 * `remote` zero and CommMustBroadcast agreeing, the object is announced with
 * SendItemCreate. So `remote` means "this creation came from somewhere else",
 * and BOTH surviving call sites in this tree pass 1 -- RecvItemCreate, which
 * is the network, and LoadType1, which is the savegame. Neither is the game
 * deciding to make something.
 *
 * THE POSITION IS OFFSET IN THE ARGUMENT'S OWN SLOT. The original copies the
 * caller's packed point into the fifth argument's stack slot and adds the
 * link record's +0x0C and +0x0E to its two halves there, which is why a linear
 * read makes the flags argument look like it is being modified. It is not:
 * `orFlags` is loaded into ebx BEFORE the slot is scratched and every
 * recursive call gets the caller's value. The adds are 16-bit and wrap, and
 * are written that way.
 *
 * THE PARENT AND THE CHILDREN DIFFER IN EXACTLY TWO ARGUMENTS. A child gets
 * the formatted "%s-%d" name and a uid of ZERO -- so it is allocated a fresh
 * one -- where the parent gets the caller's name and the caller's uid. Every
 * other argument is passed through unchanged, the link record's `child` key
 * standing in for the composite key.
 *
 * A NULL NAME IS AN EMPTY STRING, NOT A NULL. The child arm formats when the
 * caller supplied a name and writes a single NUL when it did not, so the
 * recursive call always gets a valid buffer. The buffer is 64 bytes and
 * unbounded, exactly like MovieBuildName's -- a long enough name in a mission
 * script would smash this frame. That is the original's behaviour and is kept.
 *
 * THE CHAIN IS THREE FIELDS AND THIS IS THEIR WRITER. See
 * OBJ_OFF_CHAIN_PARENT_UID in orig.h: the child points back at the parent, the
 * parent points at its first child, and each child points at the next.
 * Nothing is unlinked here, and the "first" test is on the parent's head field
 * rather than on a counter -- so a child that fails to be created leaves the
 * chain shorter without leaving a hole.
 *
 * THE LEAF UNPACKS THE KEY INLINE rather than through KeyFieldA/B/C, which is
 * what the original does; the three fields are the sprite set, index and
 * frame, and the low one is stored on the object afterwards.
 *
 * A CONCEAL REQUEST ARRIVES AS A FLAG AND LEAVES AS A CALL. OBJ_FLAG_CONCEALED
 * in `orFlags` is OR'd into the object by InitObjFromAai, then CLEARED here and
 * replaced with ObjConceal(obj, 1) -- so the caller cannot set the bit
 * directly and skip the bookkeeping the function does.
 *
 * ItemPostCreate is gated on ObjIsWatchedKind and on the ARMY being under 4,
 * read back off the object rather than from the argument, with a signed
 * compare on the byte.
 */
void *__cdecl CreateItem(char *name, int32_t army, int32_t key, uint32_t at,
                         int32_t orFlags, int32_t remote, uint32_t uid)
{
    AM2_DefLink *rec;
    uint8_t     *parent = (uint8_t *)0;
    uint8_t     *prev   = (uint8_t *)0;
    int32_t      n      = 0;
    uint8_t     *o;
    int32_t      slot;
    int32_t      i;
    int32_t      set, index, frame;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION && remote == 0
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)army))
        return (void *)0;

    rec = DefFindLink(key, 0);
    if (rec) {
        char child[AM2_CHILD_NAME_BUF];

        do {
            uint16_t px = (uint16_t)((uint16_t)at + (uint16_t)rec->a);
            uint16_t py = (uint16_t)((uint16_t)(at >> 16) + (uint16_t)rec->b);
            uint32_t pt = (uint32_t)px | ((uint32_t)py << 16);

            if (!parent) {
                parent = (uint8_t *)CreateItem(name, army, rec->child, pt,
                                               orFlags, remote, uid);
                if (parent && (rec->c & 1))
                    *(uint32_t *)(parent + OBJ_OFF_FLAGS) |= OBJ_FLAG_NO_FRAME;
            } else {
                uint8_t *made;

                if (name)
                    am2_sprintf(child,
                                (const char *)AM2_IMAGE(AM2_STR_CHILD_SUFFIX),
                                name, n);
                else
                    child[0] = '\0';

                made = (uint8_t *)CreateItem(child, army, rec->child, pt,
                                             orFlags, remote, 0);
                if (made) {
                    *(uint32_t *)(made + OBJ_OFF_CHAIN_PARENT_UID) =
                        ((const AM2_Object *)parent)->uid;

                    if (!*(const uint32_t *)(parent + OBJ_OFF_CHAIN_UID))
                        *(uint32_t *)(parent + OBJ_OFF_CHAIN_UID) =
                            ((const AM2_Object *)made)->uid;
                    else if (prev)
                        *(uint32_t *)(prev + OBJ_OFF_CHAIN_NEXT_UID) =
                            ((const AM2_Object *)made)->uid;

                    if (rec->c & 1)
                        *(uint32_t *)(made + OBJ_OFF_FLAGS) |= OBJ_FLAG_NO_FRAME;

                    prev = made;
                }
            }

            n++;
            rec = DefFindLink(key, n);
        } while (rec);

        return parent;
    }

    /* The leaf. KeyFieldA/B/C written out, the way the original has them. */
    set   = (int32_t)(((uint32_t)key >> AM2_OBJREC_SHIFT_B) & AM2_OBJREC_MASK_B);
    index = (int32_t)(((uint32_t)key >> AM2_OBJREC_SHIFT_A) & AM2_OBJREC_MASK_A);
    frame = (int32_t)((uint32_t)key & AM2_OBJREC_MASK_B);

    slot = EnsureSpriteAaiRecord(set, index, frame);
    if (slot < 0)
        return (void *)0;

    o = (uint8_t *)am2_malloc(AM2_ITEM_BYTES);
    memset(o, 0, AM2_ITEM_BYTES);

    if (!InitObjFromAai(o, name, army, slot, at, orFlags, remote,
                        (int32_t)uid, 0)) {
        am2_free(o);
        return (void *)0;
    }

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++)
        RowUpdate(*(uint8_t **)(o + OBJ_OFF_ROWS)
                      + (uint32_t)i * AM2_OBJ_ROW_STRIDE,
                  0, (void *)(uintptr_t)ADDR_MAP_DESC);

    if (orFlags & OBJ_FLAG_CONCEALED) {
        *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_CONCEALED;
        ObjConceal(o, 1);
    }

    if (ObjIsWatchedKind(o) && *(const int8_t *)(o + OBJ_OFF_ARMY) < 4)
        ItemPostCreate((int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY),
                       *(const uint32_t *)(o + OBJ_OFF_POS));

    *(int32_t *)(o + OBJ_OFF_FORMATION_SLOT) = frame;

    if (remote == 0
        && CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                             (int16_t)army))
        SendItemCreate(o);

    return o;
}

/* DamageItem -- original 0x004356C0, three callers. DamageObject's type-1 arm:
 * take armour off the hit, apply what is left, and if that empties the health
 * either advance the item to its next damage frame or destroy it and spawn
 * whatever it leaves behind.
 *
 * ITS ARGUMENT ORDER IS DamageObject'S, not the one the body suggests: the
 * kind is the FOURTH and the attacker's uid the FIFTH, with a third value
 * that this function only passes on. Read off the existing call sites and the
 * depth arithmetic together -- `[esp+0x1C]` at a depth of 12 is frame+16 --
 * after CLAUDE.md's lesson from EnterVehicle earlier today.
 *
 * ITS SIXTH ARGUMENT IS THE RECURSION GUARD, and DamageItemChain is what sets
 * it. Called with it clear, an item that HEADS a chain hands the whole chain
 * to DamageItemChain and returns, and an item that is a CHILD of one returns
 * having done nothing -- so a hit on a composite is delivered once, from the
 * parent, to every piece. DamageItemChain then calls back with a literal 1,
 * which is the only way past that gate.
 *
 * THE ARMOUR IS AAIREC_OFF_ARMOUR AND IS APPLIED AS AN ABSOLUTE VALUE. A
 * negative entry protects exactly as much as its positive twin, and for damage
 * kind 1 it is clamped to zero first -- so kind 1 ignores armour entirely.
 * `cdq; xor; sub` is the abs; written as one.
 *
 * THREE FLAGS AND ONE SEQUENCE. Damage kind 1 is refused outright by
 * OBJ_FLAG_IMMUNE_KIND1 -- the damage becomes zero and the armour subtraction
 * then makes it negative, so the function returns. An object carrying
 * OBJ_FLAG_SEQ_ON_KIND1 and not yet OBJ_FLAG_SEQ_STARTED gets a kind-7
 * sequence at its own position lasting (health + 4) * 250, and the second flag
 * is what stops it starting twice. The names are the MECHANISM; calling them
 * fireproof, flammable and burning would read better and assert more.
 *
 * THE NEXT DAMAGE FRAME IS A LOOKUP, NOT A COUNTER. On reaching zero health it
 * unpacks the record's key, adds one to OBJ_OFF_REPAIR_FRAME, and asks
 * DefFindObjRec for a record with that frame -- and REQUIRES the answer's own
 * key to match, because DefFindObjRec falls back through three searches and
 * can return a near miss. Only then does ApplyObjFrame run, and only if THAT
 * succeeds is the item still alive. So a damaged crate walks through as many
 * frames as its `.aai` declares and is destroyed on the first one missing.
 *
 * THE TWO TEARDOWN ARMS ARE THE SAME CODE TWICE, which is the original's:
 * whether the lookup missed or ApplyObjFrame refused, it tears the subrecord
 * down, marks OBJ_FLAG_OVERDUE, hides the rows and calls the pre-destroy.
 *
 * AND THE WHOLE SPAWN TAIL IS GATED ON ONE SPRITE SET. `cmp edi, 0x1D` on the
 * key's set field: an item from any other set is destroyed silently. Inside
 * it, a WATCHED kind spawns AM2_SPAWN_WATCHED with the owning player's uid --
 * or its own if the army has no owner object -- and unreveals the area first;
 * anything else spawns one of three kinds chosen by the record's INDEX field,
 * with a life to match.
 *
 * A .bss GLOBAL WITH NO WRITER IS THAT SPAWN'S SIXTH ARGUMENT. 0x00662288 has
 * exactly one reference in the image and it is here, so it is always zero --
 * and the multiplayer arm beside it, which passes a literal zero when
 * CommMustBroadcast refuses, therefore cannot be told from the other. Both
 * reproduced; the test is dead as written and it is not ours to remove.
 */
void __cdecl DamageItem(void *obj, int32_t amount, int32_t extra, int32_t kind,
                        uint32_t attacker, int32_t inChain)
{
    uint8_t       *o = (uint8_t *)obj;
    const uint8_t *rec;
    int32_t        armour;
    int32_t        dmg;
    int32_t        set, index, frame;

    if (!inChain) {
        if (*(const uint32_t *)(o + OBJ_OFF_CHAIN_UID)) {
            DamageItemChain(obj, amount, extra, kind, attacker);
            return;
        }
        if (*(const uint32_t *)(o + OBJ_OFF_CHAIN_PARENT_UID))
            return;
    }

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) <= 0)
        return;

    rec    = *(const uint8_t *const *)(o + OBJ_OFF_FIELD_94);
    armour = *(const int16_t *)(rec + AAIREC_OFF_ARMOUR);
    dmg    = amount;

    if (kind == 1) {
        if (armour < 0)
            armour = 0;
        if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_IMMUNE_KIND1)
            dmg = 0;
    }

    dmg -= (armour < 0) ? -armour : armour;
    if (dmg <= 0)
        return;

    if (kind == 1
        && (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SEQ_ON_KIND1)
        && !(*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_SEQ_STARTED)) {
        int32_t life;

        *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_SEQ_STARTED;
        life = (*(const int16_t *)(o + OBJ_OFF_HEALTH) + AM2_SEQ_LIFE_BIAS)
               * AM2_SEQ_LIFE_PER_HP;
        SeqAddKind7((const int32_t *)(o + OBJ_OFF_POS), (int32_t)attacker,
                    0, 0, life);
    }

    if (dmg > *(const int16_t *)(o + OBJ_OFF_HEALTH))
        dmg = *(const int16_t *)(o + OBJ_OFF_HEALTH);

    *(int16_t *)(o + OBJ_OFF_HEALTH) -= (int16_t)dmg;
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) != 0)
        return;

    /* ---- out of health ---- */
    {
        uint32_t       key  = *(const uint32_t *)(rec + AAIREC_OFF_KEY);
        const uint8_t *next;
        int32_t        alive = 0;

        set   = (int32_t)((key >> AM2_OBJREC_SHIFT_B) & AM2_OBJREC_MASK_B);
        index = (int32_t)((key >> AM2_OBJREC_SHIFT_A) & AM2_OBJREC_MASK_A);
        frame = *(const int32_t *)(o + OBJ_OFF_REPAIR_FRAME) + 1;

        next = (const uint8_t *)DefFindObjRec(set, index, frame);

        /* The answer's own key must BE the frame asked for: DefFindObjRec
         * falls back through three searches and can hand back a near miss. */
        if (next && *(const int32_t *)(next + AAIREC_OFF_KEY) == frame)
            alive = ApplyObjFrame(obj, set, index, frame, 0);

        if (!alive) {
            ItemTeardown(obj);
            *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;

            if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 0) {
                ObjFlagClear0(*(void **)(o + OBJ_OFF_ROWS));
                RowUpdate(*(void **)(o + OBJ_OFF_ROWS), 0,
                          (void *)(uintptr_t)ADDR_MAP_DESC);
            }

            ItemPreDestroyAlias(obj, (int32_t)ADDR_OBJ_MAP_DESC);
        }
    }

    if (set != AM2_DAMAGE_ITEM_SET)
        return;

    if (ObjIsWatchedKind(obj)) {
        uint8_t *owner = (uint8_t *)LookupOwnerObj(
            (uint32_t)*(const int8_t *)(o + OBJ_OFF_ARMY));
        uint32_t who = owner ? ((const AM2_Object *)owner)->uid
                             : ((const AM2_Object *)o)->uid;
        int32_t  sixth;

        if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
            && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                  (int16_t)*(const int8_t *)(o + OBJ_OFF_ARMY)))
            sixth = 0;
        else
            sixth = *(const int32_t *)(uintptr_t)ADDR_UNUSED_662288;

        UnrevealArea((int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY),
                     *(const uint32_t *)(o + OBJ_OFF_POS));

        SpawnAt((int32_t)*(const int16_t *)(o + OBJ_OFF_POS),
                (int32_t)*(const int16_t *)(o + OBJ_OFF_Y),
                AM2_SPAWN_WATCHED,
                (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY),
                who, sixth, 0, 0, 0, 0);
        return;
    }

    {
        const uint8_t *r2 = *(const uint8_t *const *)(o + OBJ_OFF_FIELD_94);
        int32_t idx = (int32_t)((*(const uint32_t *)(r2 + AAIREC_OFF_KEY)
                                 >> AM2_OBJREC_SHIFT_A) & AM2_OBJREC_MASK_A);
        int32_t leaves, life;

        if (idx == AM2_SPAWN_INDEX_A) {
            leaves = AM2_SPAWN_KIND_A; life = AM2_SPAWN_LIFE_A;
        } else if (idx == AM2_SPAWN_INDEX_B) {
            leaves = AM2_SPAWN_KIND_B; life = AM2_SPAWN_LIFE_B;
        } else {
            leaves = AM2_SPAWN_KIND_C; life = AM2_SPAWN_LIFE_C;
        }

        SpawnAt((int32_t)*(const int16_t *)(o + OBJ_OFF_POS),
                (int32_t)*(const int16_t *)(o + OBJ_OFF_Y),
                leaves,
                attacker ? (int32_t)UidArmy(attacker)
                      : (int32_t)*(const int8_t *)(o + OBJ_OFF_ARMY),
                ((const AM2_Object *)o)->uid, life, 0, 0, 0, 0);
    }
}

void item_install(void)
{
    patch_replace(ADDR_ITEM_IS_READY, (const void *)ItemIsReady,
                  "ItemIsReady", 1);
    patch_replace(ADDR_CREATE_ITEM, (const void *)CreateItem,
                  "CreateItem", 9);
    patch_replace(ADDR_SHOOTER_REACT, (const void *)ShooterReact,
                  "ShooterReact", 1);
    patch_replace(ADDR_DAMAGE_ITEM, (const void *)DamageItem,
                  "DamageItem", 3);
    patch_replace(ADDR_ITEM_TYPE_NAME, (const void *)ItemTypeName,
                  "ItemTypeName", 1);
    patch_replace(ADDR_ITEM_PRE_DESTROY, (const void *)ItemPreDestroy,
                  "ItemPreDestroy", 2);
    patch_replace(ADDR_ITEM_LINK_CELLS, (const void *)ItemLinkCells,
                  "ItemLinkCells", 2);
    patch_replace(ADDR_ITEM_SET_BOX, (const void *)ItemSetBox,
                  "ItemSetBox", 1);
    patch_replace(ADDR_FREE_SUBRECORD_ROWS, (const void *)FreeSubrecordRows,
                  "FreeSubrecordRows", 1);
    patch_replace(ADDR_ITEMS_RESET, (const void *)ItemsReset,
                  "ItemsReset", 0);
    patch_replace(ADDR_RESET_ITEMS_AND_UIDS, (const void *)ResetItemsAndUids,
                  "ResetItemsAndUids", 1);
    patch_replace(ADDR_STEP_TYPE1_4, (const void *)StepType1And4,
                  "StepType1And4", 1);
    patch_replace(ADDR_TYPE238_ACTION, (const void *)Type238Action,
                  "Type238Action", 2);
    patch_replace(ADDR_CHANGE_OBJECT_FRAME, (const void *)ChangeObjectFrame,
                  "ChangeObjectFrame", 1);
    patch_replace(ADDR_SET_SOLDIER_KIND, (const void *)SetSoldierKind,
                  "SetSoldierKind", 10);
    patch_replace(ADDR_SOLDIER_KIND_FOR_WEAPON,
                  (const void *)SoldierKindForWeapon,
                  "SoldierKindForWeapon", 13);
    patch_replace(ADDR_SET_WEAPON_TARGET, (const void *)SetWeaponTarget,
                  "SetWeaponTarget", 6);
    patch_replace(ADDR_PICK_FIRE_MODE, (const void *)PickFireMode,
                  "PickFireMode", 1);
    patch_replace(ADDR_BOARD_VEHICLE, (const void *)BoardVehicle,
                  "BoardVehicle", 1);
    patch_replace(ADDR_OBJ_IS_WATCHED_KIND, (const void *)ObjIsWatchedKind,
                  "ObjIsWatchedKind", 8);
    patch_replace(ADDR_OBJ_COLLIDES_WITH, (const void *)ObjCollidesWith,
                  "ObjCollidesWith", 2);
    patch_replace(ADDR_APPLY_SHOT_DAMAGE, (const void *)ApplyShotDamage,
                  "ApplyShotDamage", 1);
    patch_replace(ADDR_SHOT_STRIKE, (const void *)ShotStrike,
                  "ShotStrike", 1);
    patch_replace(ADDR_TROOPER_DROP_ITEM, (const void *)TrooperDropItem,
                  "TrooperDropItem", 1);
    patch_replace(ADDR_USE_INVENTORY_ITEM, (const void *)UseInventoryItem,
                  "UseInventoryItem", 1);
    patch_replace(ADDR_BLOCK_WEIGHT_DAMAGING,
                  (const void *)BlockWeightDamaging,
                  "BlockWeightDamaging", 1);
    patch_replace(ADDR_ROACH_MASK_WEIGHT, (const void *)RoachMaskWeight,
                  "RoachMaskWeight", 4);
    patch_replace(ADDR_OBJ_CLEAR_ROACH_FOOTPRINT,
                  (const void *)ObjClearRoachFootprint,
                  "ObjClearRoachFootprint", 3);
    patch_replace(ADDR_OBJ_SET_ROACH_FOOTPRINT,
                  (const void *)ObjSetRoachFootprint,
                  "ObjSetRoachFootprint", 6);
    patch_replace(ADDR_CREATE_ROACH, (const void *)CreateRoach,
                  "CreateRoach", 2);
    patch_replace(ADDR_ROACH_BITE, (const void *)RoachBite, "RoachBite", 1);
    patch_replace(ADDR_WEAPON_RESPAWN, (const void *)WeaponRespawn,
                  "WeaponRespawn", 8);
    patch_replace(ADDR_PORTAL_SPAWN, (const void *)PortalSpawn,
                  "PortalSpawn", 1);
    patch_replace(ADDR_ROACH_STEP_ALLOWED, (const void *)RoachStepAllowed,
                  "RoachStepAllowed", 4);
    patch_replace(ADDR_ROACH_STEP_TAIL_A, (const void *)RoachStepTailA,
                  "RoachStepTailA", 1);
    patch_replace(ADDR_ROACH_ALIVE_STEP_B, (const void *)RoachAliveStepB,
                  "RoachAliveStepB", 1);
    patch_replace(ADDR_OBJ_SET_FOOTPRINT, (const void *)ObjSetFootprint,
                  "ObjSetFootprint", 6);
    patch_replace(ADDR_OBJ_CLEAR_FOOTPRINT, (const void *)ObjClearFootprint,
                  "ObjClearFootprint", 7);
    patch_replace(ADDR_ENTER_VEHICLE, (const void *)EnterVehicle,
                  "EnterVehicle", 3);
    patch_replace(ADDR_DESELECT_ALL, (const void *)DeselectAll,
                  "DeselectAll", 9);
    patch_replace(ADDR_SET_UNIT_POSE, (const void *)SetUnitPose,
                  "SetUnitPose", 9);
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
    patch_replace(ADDR_TROOPER_REMOTE_PICKUP,
                  (const void *)TrooperRemotePickupItem,
                  "TrooperRemotePickupItem", 4);
    patch_replace(ADDR_TROOPER_HOST_APPROVED,
                  (const void *)TrooperHostApprovedPickupItem,
                  "TrooperHostApprovedPickupItem", 4);
    patch_replace(ADDR_TROOPER_DIED_TAIL, (const void *)TrooperDiedTail,
                  "TrooperDiedTail", 2);
    patch_replace(ADDR_NOTIFY_DROPPED, (const void *)NotifyDropped,
                  "NotifyDropped", 1);
    patch_replace(ADDR_WEAPON_POSE_INDEX, (const void *)WeaponPoseIndex,
                  "WeaponPoseIndex", 3);
    patch_replace(ADDR_PLACE_OBJ, (const void *)PlaceObj, "PlaceObj", 1);
    patch_replace(ADDR_APPLY_HEIGHT_1_4, (const void *)ApplyHeightItem,
                  "ApplyHeightItem", 3);
    patch_replace(ADDR_STEP_ROW_ANIM, (const void *)StepRowAnim,
                  "StepRowAnim", 1);
    patch_replace(ADDR_OBJECTS_AT_POINT, (const void *)ObjectsAtPoint,
                  "ObjectsAtPoint", 15);
    patch_replace(ADDR_RANK_PROMOTE, (const void *)RankPromote,
                  "RankPromote", 1);
    patch_replace(ADDR_STEP_TYPE8, (const void *)StepType8, "StepType8", 1);
    patch_replace(ADDR_TYPE2_ACTION_ALL, (const void *)Type2ActionAll,
                  "Type2ActionAll", 1);
    patch_replace(ADDR_SLOT_BAND_HEADING, (const void *)SlotBandHeading,
                  "SlotBandHeading", 1);
    patch_replace(ADDR_SELECT_BEST_WEAPON, (const void *)SelectBestWeapon,
                  "SelectBestWeapon", 1);
    patch_replace(ADDR_SET_OBJ_FIELD_530, (const void *)SetObjField530,
                  "SetObjField530", 2);
    patch_replace(ADDR_JITTER_FACING, (const void *)JitterFacing,
                  "JitterFacing", 3);
    patch_replace(ADDR_HELD_WEAPON_OBJ, (const void *)HeldWeaponObj,
                  "HeldWeaponObj", 2);
    patch_replace(ADDR_OBJ_TO_AI, (const void *)ObjToAI, "ObjToAI", 1);
    patch_replace(ADDR_SET_KIND_FRAMES, (const void *)SetKindFrames,
                  "SetKindFrames", 3);
    patch_replace(ADDR_SPAWN_RANDOM_BARRAGE,
                  (const void *)SpawnRandomBarrage,
                  "SpawnRandomBarrage", 1);
    patch_replace(ADDR_SELECT_IF_OWN, (const void *)SelectIfOwn,
                  "SelectIfOwn", 4);
    patch_replace(ADDR_FOR_EACH_SELECTED, (const void *)ForEachSelected,
                  "ForEachSelected", 1);
    patch_replace(ADDR_RESET_TYPE2_FIELDS, (const void *)ResetType2Fields,
                  "ResetType2Fields", 2);
    patch_replace(ADDR_RESET_OBJ_ON_COF, (const void *)ResetObjOnCof,
                  "ResetObjOnCof", 3);
    patch_replace(ADDR_SELECT_RANKED_WEAPON,
                  (const void *)SelectRankedWeapon,
                  "SelectRankedWeapon", 1);
    patch_replace(ADDR_OBJ_ROWS_MASK_AT, (const void *)ObjRowsMaskAt,
                  "ObjRowsMaskAt", 1);
    patch_replace(ADDR_REMAP_INVENTORY_UIDS,
                  (const void *)RemapInventoryUids,
                  "RemapInventoryUids", 1);
    patch_replace(ADDR_TOGGLE_SELECT, (const void *)ToggleSelect,
                  "ToggleSelect", 1);
    patch_replace(ADDR_SELECTION_CLICK, (const void *)SelectionClick,
                  "SelectionClick", 1);
    patch_replace(ADDR_SET_OBJ_CONTEXT, (const void *)SetObjContext,
                  "SetObjContext", 3);
    patch_replace(ADDR_NEAREST_CLEAR_POINT, (const void *)NearestClearPoint,
                  "NearestClearPoint", 2);
    patch_replace(ADDR_NEAREST_CLEAR_VEHICLE_POINT,
                  (const void *)NearestClearVehiclePoint,
                  "NearestClearVehiclePoint", 1);
    patch_replace(ADDR_WALK_CELL_AT_POINT, (const void *)WalkCellAtPoint,
                  "WalkCellAtPoint", 2);
    patch_replace(ADDR_WEAPON_FRAME_READY, (const void *)WeaponFrameReady,
                  "WeaponFrameReady", 1);
    patch_replace(ADDR_SET_OBJ_TABLE_PAIR, (const void *)SetObjTablePair,
                  "SetObjTablePair", 1);
    patch_replace(ADDR_PICK_WEAPON_SLOT, (const void *)PickWeaponSlot,
                  "PickWeaponSlot", 1);
    patch_replace(ADDR_TRY_TAKE_WEAPON, (const void *)TryTakeWeapon,
                  "TryTakeWeapon", 2);
    patch_replace(ADDR_AWARD_OWN_ARMY_XP, (const void *)AwardOwnArmyXp,
                  "AwardOwnArmyXp", 1);
    patch_replace(ADDR_WEAPON_CLASS_OF, (const void *)WeaponClassOf,
                  "WeaponClassOf", 1);
    patch_replace(ADDR_DAMAGE_ITEM_CHAIN, (const void *)DamageItemChain,
                  "DamageItemChain", 1);
    patch_replace(ADDR_OBJ_OVERLAY_Y, (const void *)ObjOverlayY,
                  "ObjOverlayY", 1);
    patch_replace(ADDR_HELD_WEAPON_CODE, (const void *)HeldWeaponCode,
                  "HeldWeaponCode", 1);
    patch_replace(ADDR_UNIT_CLASS_NAME, (const void *)UnitClassName,
                  "UnitClassName", 1);
    patch_replace(ADDR_CAN_PICK_UP, (const void *)CanPickUp, "CanPickUp", 1);
    patch_replace(ADDR_OBJ_DROP_ALT_RECORD, (const void *)ObjDropAltRecord,
                  "ObjDropAltRecord", 1);
    patch_replace(ADDR_TAKE_SOLDIER_NAME, (const void *)TakeSoldierName,
                  "TakeSoldierName", 1);
    patch_replace(ADDR_SOLDIER_NAME_OF, (const void *)SoldierNameOf,
                  "SoldierNameOf", 2);
    patch_replace(ADDR_BLOCK_WEIGHT_AT, (const void *)BlockWeightAt,
                  "BlockWeightAt", 3);
    patch_replace(ADDR_BLOCK_WEIGHT_CHAIN, (const void *)BlockWeightChain,
                  "BlockWeightChain", 2);
    patch_replace(ADDR_BLOCK_WEIGHT_ROUTE, (const void *)BlockWeightRoute,
                  "BlockWeightRoute", 3);
    patch_replace(ADDR_BLOCK_WEIGHT_TROOPS, (const void *)BlockWeightTroops,
                  "BlockWeightTroops", 3);
    patch_replace(ADDR_MASK_BLOCK_WEIGHT, (const void *)MaskBlockWeight,
                  "MaskBlockWeight", 5);
    patch_replace(ADDR_DAMAGE_OBJECT, (const void *)DamageObject,
                  "DamageObject", 6);
    patch_replace(ADDR_DAMAGE_VEHICLE, (const void *)DamageVehicle,
                  "DamageVehicle", 1);
    patch_replace(ADDR_NEXT_INVENTORY_SLOT, (const void *)NextInventorySlot,
                  "NextInventorySlot", 1);
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
    patch_replace(ADDR_STEP_TYPE6, (const void *)StepType6, "StepType6", 1);
    patch_replace(ADDR_CAN_PICK_UP_WEAPON, (const void *)CanPickUpWeapon,
                  "CanPickUpWeapon", 2);
    patch_replace(ADDR_ON_SELECTION_CHANGED, (const void *)OnSelectionChanged,
                  "OnSelectionChanged", 8);
    patch_replace(ADDR_OBJ_REMAP, (const void *)ObjRemap, "ObjRemap", 3);
    patch_replace(ADDR_DEPLOY_TROOPER, (const void *)DeployTrooper,
                  "DeployTrooper", 1);
    patch_replace(ADDR_DEPLOY_VEHICLE, (const void *)DeployVehicle,
                  "DeployVehicle", 1);
    patch_replace(ADDR_OBJ_MOVE_ALONG_FACING,
                  (const void *)ObjMoveAlongFacing,
                  "ObjMoveAlongFacing", 6);
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
