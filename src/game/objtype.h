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

/* Original: 0x00457470. */
int32_t __cdecl ObjIsType2(const AM2_Object *obj);

/* Original: 0x00457490. */
int32_t __cdecl ObjIsType3(const AM2_Object *obj);

/* 0x004574B0, 0x0045EEB0. The remaining single-type predicates. */
int32_t __cdecl ObjIsType8(const AM2_Object *obj);
int32_t __cdecl ObjIsType4(const AM2_Object *obj);

/* Original: 0x00457420. Types 2, 3 and 8 -- the owned non-item types. */
int32_t __cdecl ObjIsTypeIn238(const AM2_Object *obj);

int objtype_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_OBJTYPE_H */
