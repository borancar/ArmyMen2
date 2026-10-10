/* pointerfn.h -- the pointer layer's handler types: g_pointerPick,
 * g_pointerAction, g_weaponFnSlot0/1 and the ADDR_WEAPON_HANDLERS records
 * SelectInventorySlot copies them from.
 *
 * Its own header rather than globaltypes.h, for the reason mousepress.h
 * gives: declared in every translation unit, these types turned a register
 * tie in CheatLine and moved FlowRecvMessage. */
#ifndef AM2_POINTERFN_H
#define AM2_POINTERFN_H

#include <stdint.h>
#include "point.h"
/* The pointer layer's two handler shapes: the pick that finds what is under
 * the cursor, and the action that is applied to it. The four globals that
 * hold the current mode's and weapon's handlers have these types. */
struct AM2_Object;
typedef int32_t (__cdecl *AM2_PointerPickFn)(void *obj);
typedef void (__cdecl *AM2_PointerActionFn)(struct AM2_Object *obj, AM2_Point at);

/* One record of ADDR_WEAPON_HANDLERS, per weapon kind; SelectInventorySlot
 * copies it into ADDR_WEAPON_FN_SLOT0..3. */
typedef struct AM2_WeaponHandlers {
    AM2_PointerPickFn   pick;       /* +0x00 -> ADDR_WEAPON_FN_SLOT0 */
    AM2_PointerActionFn action;     /* +0x04 -> ADDR_WEAPON_FN_SLOT1 */
    int32_t             slot2;      /* +0x08 -> ADDR_WEAPON_FN_SLOT2 */
    int32_t             slot3;      /* +0x0C -> ADDR_WEAPON_FN_SLOT3 */
} AM2_WeaponHandlers;

#endif
