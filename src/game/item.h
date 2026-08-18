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

void item_install(void);

#ifdef __cplusplus
}
#endif

#endif
