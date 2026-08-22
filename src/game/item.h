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

void item_install(void);

#ifdef __cplusplus
}
#endif

/* 0x004285F0. Destroy one item, dispatching on its kind at +0. `unlink` takes
 * it out of the item list first, and a failed unlink aborts and returns 0. An
 * unknown kind returns 1 having done nothing. Two callers. */
int32_t __cdecl FreeItem(void *item, int32_t unlink);

/* 0x00429C80. Release an item object's allocation. Idempotent: the byte at
 * +0x8C is both the guard and the record. `notify` gates a call that runs
 * BEFORE the free. Five callers. */
void __cdecl DestroyItemObject(void *obj, int32_t arg, int32_t notify);

#endif
