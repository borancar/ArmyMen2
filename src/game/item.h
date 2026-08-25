/* item.cpp -- the item list and the objects on it.
 *
 * Reconstructed from the translation unit the linker placed between the
 * item.cpp and map.cpp save-tag anchors (0x00428C40..0x0042DBB0). The item half
 * runs to about 0x0042B120, where the map code starts; docs/00-recon.md
 * explains why alphabetical link order lets a function be attributed at all.
 */
#ifndef AM2_ITEM_H
#define AM2_ITEM_H

#include <stdint.h>
#include "../inject/orig.h"   /* am2_FILE, for the savegame pair */

#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x00447990, and it names itself -- "RemoveInventoryItem". Take one
 * slot out of a unit's six-entry weapon inventory: shift the entries above it
 * down, clear the sixth, and fix up which slot is in hand.
 *
 * The shift is `memmove` of `0x14 - slot * 4` bytes, so it moves only the
 * entries that exist above `slot` and is skipped entirely for slot 5. The
 * sixth entry is cleared either way, which is what stops the shift leaving a
 * duplicate at the top.
 *
 * The selected slot is fixed in three cases and they are not symmetric. If the
 * removed slot WAS in hand the selection resets to 0 and the unit re-selects,
 * but only when ObjType2Field548 agrees. If the selection was ABOVE the removed
 * slot it slides down by one. If it was below it is left alone -- and the
 * `jle` that decides this makes "equal" go the first way, which is why the
 * equal case is tested first and cannot fall through to the decrement. */
void __cdecl RemoveInventoryItem(void *unit, int32_t slot);

/* A uid carries its owner in the top three bits, over a 29-bit per-owner
 * counter -- the layout objtable.h already describes and AddToItemList already
 * builds. UidArmy is the original's accessor for the owner half. */
uint32_t __cdecl UidArmy(uint32_t uid);

/* Applied to a uid on its way into, or out of, a comm message: all 100 call
 * sites are in the comm code around message construction and parsing, and what
 * they pass is the uid field at +4. It returns its argument unchanged.
 *
 * Two readings fit and both agree on the behaviour. It is the shape of a
 * host/network byte-order conversion, which is identity on x86; and it is the
 * shape of a debug-build validator stubbed out for retail, which this binary
 * demonstrably does elsewhere -- ADDR_LOG is a bare `ret`. Named for what it
 * does rather than for either guess. */
uint32_t __cdecl UidOnWire(uint32_t uid);

/* A 3-bit field packed at bit 18 of the word at +8, with a matched setter --
 * the strongest structural evidence available for a field, since get and set
 * agree on position and width.
 *
 * What it MEANS is not established, so it is named for where it is, the way
 * KeyFieldA/B/C already are in this tree. Two things point at an army or team
 * index and neither is proof: three bits give eight values and objtable.h
 * documents exactly eight uid counters, one per owner; and the only readers,
 * in 0x0041F8B0 and its neighbours, compare it against a parameter that uses
 * -1 for "any", which is how you filter a list by team. Against that,
 * AM2_Object already has an `owner` at +0x10, so if this is also an owner the
 * object carries two, and that wants explaining before it goes in a name. */
uint32_t __cdecl ObjFieldA(const void *obj);
void     __cdecl ObjSetFieldA(void *obj, uint32_t value);

/* Signed byte at +0x64. Read by three callers in 0x00420xxx, each passing it
 * straight to 0x0045F460 -- 3,200 bytes, no strings, unidentified. Sign
 * matters: the original uses movsx, so the field is int8_t and negative values
 * are meaningful. */
int32_t __cdecl ObjFieldB(const void *obj);

/* 0x00428950 and 0x00428BB0. The item section of a savegame, both named by
 * their own counts -- "Saved %d items" and "Loaded %d items".
 *
 * The wire format is settled by reading both ends independently and finding
 * they agree, which is better evidence than either alone:
 *
 *   0x06660007   opens the section        (checked by the loader)
 *   0x06660000   one before each item     (the loader's continue condition)
 *   0x06660001   closes it                (anything not 0x06660000 stops it)
 *
 * So the terminator is not really a value the loader knows: it stops on the
 * first marker that is not an item marker, and the saver happens to write
 * 0x06660001. A save ending any other way would load identically.
 *
 * The saver walks FirstItem/NextItem, which are the pair the registry
 * invariant in CLAUDE.md is about, and counts as it goes. It checks nothing:
 * a write that fails is not noticed, and it always answers 1.
 *
 * The loader clears the list first, and does it BEFORE reading its argument --
 * so a load that then fails its tag check has already emptied the world. The
 * failing path answers 0; every other path answers 1. */
int32_t __cdecl SaveItems(am2_FILE *fp);
int32_t __cdecl LoadItems(am2_FILE *fp);

/* 0x00429570, six callers. The attribute byte for the tile an object is
 * standing on: the tile index at +0x1A picks an entry of the table at
 * 0x00514EBC, and the byte comes back SIGN EXTENDED. The name is ours.
 *
 * The index is read as a word and used unsigned, so an object with 0xFFFF
 * there reads 64K into the table. Nothing bounds it; the original does not
 * either. */
int32_t __cdecl ObjTileAttr(const void *obj);

/* 0x00429540, three callers. The same byte taken by tile INDEX rather than by
 * object -- masked to 16 bits here where its neighbour reads a word, which is
 * the same value arrived at differently. */
int32_t __cdecl TileAttrAt(uint32_t tile);

/* 0x004355D0, one caller. A second deadline on the mission clock, at +0x58:
 * once the clock is PAST it, bit 1 of the object's flags goes on. Unsigned
 * compare, and nothing clears the bit here. Both names are ours. */
void __cdecl ObjMarkIfOverdue(void *obj);

/* 0x00429590, 24 callers. How high an object stands: the byte at +0x65 is an
 * absolute floor when it is non-zero and otherwise the tile's own attribute
 * byte is used; either way the SIGNED byte at +0x64 is added. So +0x64 is an
 * offset and +0x65 an override -- neither name is the program's, and the
 * override is read unsigned where the offset is read signed. */
int32_t __cdecl ObjHeight(const void *obj);

/* 0x00429CE0, seven callers. A plain cdecl forwarder for 0x0042A0A0, which is
 * still original -- both arguments go straight through. */
void __cdecl ItemPreDestroyAlias(void *obj, int32_t arg);

/* 0x0041D3A0. One row's teardown: unregister it from the map's cell lists and
 * free the buffer it owns, both gated on the single flag at +0x34. */
void __cdecl RowRelease(void *row, void *desc);

/* 0x00428DA0, 22 callers. Destroy an object by type, then broadcast. */
void __cdecl DestroyByType(void *obj);

void item_install(void);

#ifdef __cplusplus
}
#endif

/* 0x004285F0. Destroy one item, dispatching on its kind at +0. `unlink` takes
 * it out of the item list first, and a failed unlink aborts and returns 0. An
 * unknown kind returns 1 having done nothing. Two callers. */
int32_t __cdecl FreeItem(void *item, int32_t unlink);

/* 0x00429450. The object registry's teardown -- FreeItem every entry with
 * `unlink` ZERO, which is what keeps the forward walk safe, then free the
 * array and clear the record. */
void __cdecl ItemsReset(void);

/* 0x00434EC0. Release an object's sub-list: every row's own teardown, then
 * the array, then the capacity -- which is cleared unconditionally where the
 * array is freed only when there is one. */
void __cdecl FreeSubrecordRows(void *subrecord);

/* 0x0042A0A0. Unlink an object from every cell list it is registered in.
 * Each unlink writes -1 back, so a second call returns on the first entry --
 * and entry zero's index is tested BEFORE the loop as well as inside it. */
void __cdecl ItemPreDestroy(void *obj, int32_t cells);

/* 0x0045EE80. A weapon by uid. Null for a zero uid or one that resolves to
 * nothing, and null WITH a log line for one that resolves to a non-weapon. */
void *__cdecl WeaponByUid(uint32_t uid);

/* 0x00429C80. Release an item object's allocation. Idempotent: the byte at
 * +0x8C is both the guard and the record. `notify` gates a call that runs
 * BEFORE the free. Five callers. */
void __cdecl DestroyItemObject(void *obj, int32_t arg, int32_t notify);

/* Original: 0x004478C0, "DestroyTrooper %x" -- and it is the KIND 2 arm of
 * FreeItem's switch, which is why orig.h calls the address
 * ADDR_FREE_ITEM_KIND2. Kind 2 is the trooper, so the two names agree.
 *
 * Take one trooper down: mark its
 * weapon dead, free the allocation at 0x00AC, free the subrecord's rows, hand
 * the object itself to DestroyItemObject, and free it.
 *
 * The weapon is reached by uid through WeaponByUid, which complains and
 * answers null for anything that is not kind 4 -- so a trooper holding
 * something that is not a weapon leaves that step undone and carries on. Both
 * the uid being zero and the lookup failing land on the same path.
 *
 * The flag is set with an 8-bit OR on a 32-bit load and stored back as 32
 * bits, which for bit 1 is the same thing.
 *
 * Note the object is freed here AND handed to DestroyItemObject, which frees
 * its 0x0090 allocation and clears the live byte -- so the order matters and
 * is reproduced: DestroyItemObject first, then the object. */
void __cdecl DestroyTrooper(void *trooper, int32_t unlink);

/* Original: 0x0045B470, the KIND 3 arm, and a near-twin of DestroyTrooper.
 * Three differences and all three are the original's:
 *
 *  - the weapon uid is at 0x0550, not the trooper's 0x054C;
 *  - there is no log at all, where the trooper has one behind the verbosity
 *    flag and the weapon has one in front of it;
 *  - and it empties a pointer list at 0x0538 first, which the other two arms
 *    have nothing corresponding to.
 *
 * The weapon flag is set with a 32-bit OR here and with an 8-bit OR on a
 * 32-bit load in the trooper. For bit 1 the two are the same thing; the
 * difference is the compiler's, from the same source written twice.
 *
 * The name is ours: this arm carries no string, and kind 3 is the vehicle
 * because ReceiveArmyMsg's switch sends kind 3 to the vehicle handler. */
void __cdecl DestroyVehicle(void *vehicle, int32_t unlink);

/* Original: 0x0045F290, the KIND 4 arm, "DestroyWeapon, %x" -- its own name.
 * The shortest of the three: no weapon of its own to mark, no list to empty,
 * just the subrecord rows, DestroyItemObject and the free.
 *
 * Its log is NOT gated on the comm object's verbosity where the trooper's is.
 * Both go to ADDR_LOG, which this build stubs to a single `ret`. */
void __cdecl DestroyWeapon(void *weapon, int32_t unlink);

/* Original: 0x0043BBB0, the arm kinds 1, 5, 6 and 8 share -- and the BARE
 * version of the family: free the subrecord's rows, hand the object to
 * DestroyItemObject, free the object. Every other arm is this plus something.
 *
 * Forty-eight bytes, and four kinds reach it, which is why the family's shared
 * tail is visible at all. */
void __cdecl DestroyItemCommon(void *item, int32_t unlink);

/* Original: 0x004355F0, the KIND 7 arm: the bare one plus a population
 * decrement, clamped at zero.
 *
 * The clamp is not defensive tidying -- 0x00435550 refuses to create a
 * thirty-third kind-7 object, so the counter is bounded at both ends by
 * design. The name is ours; what a kind-7 object is has not been established,
 * beyond its being 0x94 bytes and limited to 32 live. */
void __cdecl DestroyKind7(void *item, int32_t unlink);

#endif
