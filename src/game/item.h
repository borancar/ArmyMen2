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

#ifdef __cplusplus
extern "C" {
#endif

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

void item_install(void);

#ifdef __cplusplus
}
#endif

#endif
