#ifndef AM2_OBJTYPE_H
#define AM2_OBJTYPE_H

#include <stdint.h>
#include "../inject/orig.h"
#include "objtable.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Predicates over the object type at +0x00.
 *
 * What is established about the taxonomy:
 *
 *   - AddToItemList dispatches on the type through a 9-entry jump table, so
 *     valid types are 0..8.
 *   - Types 1, 2, 3, 4 and 8 carry their own owner byte at +0x10; types 0, 5, 6
 *     and 7 take the owner from the global at 0x004F9FDC. So the first group is
 *     army-owned and the second is not.
 *   - Types 1 and 4 are ITEMS. This is evidenced, not guessed: the functions
 *     guarded by that predicate report "ScriptSetObjBitmap was called with %s
 *     which is not an item" and "SetObjScriptState was called with %s which is
 *     not an item" when it fails.
 *   - Types 2, 3 and 8 are therefore the owned non-item types, and 0x00457420
 *     tests exactly that set. They live in unit.cpp, immediately after the
 *     unit.cpp savegame anchor at 0x0045734D.
 *
 * What is NOT established: what 2, 3 and 8 individually mean. The obvious guess
 * given troop.aai / vehicle.aai is troop and vehicle, but the AAI loader passes
 * only filenames and no type constants, so there is no evidence for the mapping
 * and these are named structurally instead. Expect them to be renamed once
 * something pins them down.
 */

/* Original: 0x00433860. Types 1 and 4. */
int32_t __cdecl ObjIsItem(const AM2_Object *obj);

/* 0x00434060, eight callers. Make a list header and a copy of `count`
 * twelve-byte records. Structural name; see objtype.cpp. */
void *__cdecl MakeRecordList(int32_t count, const void *src, void *owner);

/* 0x00434150. Register one of those headers under its owner, keeping an
 * unsorted array of slots and a sorted {owner, slot} index in step. Returns
 * the slot, or -1 when that owner already has one. */
int32_t __cdecl AddRecordList(void *list);

/* 0x00434C40. MakeRecordList's counterpart: free the header's two pointers
 * and then the header. */
void __cdecl FreeRecordList(void *list);

/* 0x00434100. The slot registered for an owner, or -1. AddRecordList's search
 * without the insertion -- note the two disagree about what -1 means. */
int32_t __cdecl FindRecordList(uint32_t owner);

/* 0x004344A0. A 0x40-byte record from seven arguments, seeded from the
 * object.aai record for its (type, key) when the type is not negative. */
void *__cdecl MakeAaiRecord(int32_t type, int32_t key, int32_t slot,
                            int32_t a, int32_t b, int32_t c, int32_t d);

/* 0x004345A0. Register one of those under its key, writing the slot back into
 * the record. AddRecordList's twin on the table KeyLookup searches -- keyed on
 * +8 rather than +0, and growing by 19 rather than 17. */
int32_t __cdecl AddAaiRecord(void *rec);

/* 0x00434E60, three callers. Clear bit 0 on every row the sub-list holds. Its
 * argument is the sub-list header, not the object. */
void __cdecl SubrecHideRows(void *subrec);

/* Original: 0x00457470. */
int32_t __cdecl ObjIsType2(const AM2_Object *obj);

/* 0x0044BBA0. True only for a TYPE 2 whose OBJ_OFF_FIELD_5A4 is positive.
 * Type2ActionA refuses to re-arm a unit when this is true. */
int32_t __cdecl Type2Field5A4Set(const AM2_Object *obj);

/* Original: 0x00457490. */
int32_t __cdecl ObjIsType3(const AM2_Object *obj);

/* 0x004574B0, 0x0045EEB0. The remaining single-type predicates. */
int32_t __cdecl ObjIsType8(const AM2_Object *obj);
int32_t __cdecl ObjIsType4(const AM2_Object *obj);

/* 0x00457450. The dword at +0x548, but only for a type 2 object: null gives 0
 * and so does any other type, so the field is only meaningful on type 2.
 *
 * WHAT THE FIELD MEANS IS SETTLED NOW, and not from here. UnitClassName reads
 * it and, when it is set, answers "Sarge" from the entry before the class-name
 * table -- so OBJ_OFF_SARGE is the name it goes by, and this predicate is
 * "is this the army's Sarge". */
uint32_t __cdecl ObjType2Field548(const AM2_Object *obj);

/* Original: 0x00427D40, fifteen callers. The event mask for an object -- top
 * bit, a bit for its owner's army, and overlapping bits per type property.
 * What EventNotify takes as maskA and maskB. */
int32_t __cdecl ObjEventMask(const AM2_Object *obj);

/* Original: 0x00457420. Types 2, 3 and 8 -- the owned non-item types. */
int32_t __cdecl ObjIsTypeIn238(const AM2_Object *obj);

/* 0x0045D970. The registry lookup and the type test in one: the object
 * registered for `uid` if it is type 3, and NULL for every other answer --
 * no such uid, or one that is some other type.
 *
 * Eight callers, which is the reason it exists: each would otherwise write
 * the pair out, and each would have to decide again what to do with a uid
 * that resolves to the wrong type. */
AM2_Object *__cdecl LookupType3ByUID(uint32_t uid);

int objtype_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_OBJTYPE_H */
