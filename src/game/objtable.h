#ifndef AM2_OBJTABLE_H
#define AM2_OBJTABLE_H

#include <stdint.h>
#include "../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* One registry entry: 12 bytes, sorted ascending by uid.
 *
 * `stamp` is not a serial number, despite looking like one on first reading.
 * It is the iteration marker: FirstItem bumps a global stamp, NextItem skips
 * entries already carrying it and marks each one it returns. That is what makes
 * a walk safe while objects are being added, since an insert memmoves the tail
 * and invalidates any index the caller was holding.
 */
typedef struct {
    uint32_t uid;
    void    *obj;
    uint32_t stamp;
} AM2_ObjEntry;

/* A registered object. Transcribed as a typed struct and accessed through
 * members rather than OBJ_OFF_* casts -- the shape the original was written
 * in. Only the named fields are claimed; the gaps are the other object
 * variants' fields at the same offsets, filled in as functions convert.
 * `owner` is the army byte (OBJ_OFF_ARMY). */
typedef struct {
    uint32_t       type;            /* +0x00  switch selector, 0..8 */
    uint32_t       uid;             /* +0x04  written back on registration */
    uint32_t       flags;           /* +0x08  OBJ_OFF_FLAGS */
    uint8_t        _pad0c[4];       /* +0x0C */
    int8_t         owner;           /* +0x10  OBJ_OFF_ARMY, read with movsx */
    uint8_t        _pad11[0x2F];    /* +0x11 */
    uint8_t        facing;          /* +0x40  OBJ_OFF_FACING */
    uint8_t        _pad41[0x1F];    /* +0x41 */
    int16_t        maxHealth;       /* +0x60  OBJ_OFF_MAX_HEALTH */
    int16_t        health;          /* +0x62  OBJ_OFF_HEALTH */
    int8_t         heightAdj;       /* +0x64  OBJ_OFF_HEIGHT_ADJ */
    uint8_t        _pad65[0x2F];    /* +0x65 */
    const uint8_t *field94;         /* +0x94  OBJ_OFF_FIELD_94, the type record */
    uint32_t       shooterUid;      /* +0x98  OBJ_OFF_RANK, a shot's shooter uid */
    uint8_t        _pad9c[4];       /* +0x9C */
    int32_t        formationSlot;   /* +0xA0  OBJ_OFF_FORMATION_SLOT */
    uint8_t        _pada4[0xC];     /* +0xA4 */
    uint8_t        power;           /* +0xB0  OBJ_OFF_SCRIPT_ID, a shot's power byte */
    uint8_t        _padb1[0x493];   /* +0xB1 */
    int32_t        soldierKind;     /* +0x544 OBJ_OFF_SOLDIER_KIND */
} AM2_Object;

#if defined(__cplusplus) && !defined(_MSC_VER)
#include <stddef.h>   /* offsetof: GCC-side only -- an extra header in front
                         * of VC6 shifts its declaration order and flips the
                         * CheatLine/PlacementScreenClick evaluation-order coin */
/* VC6 is C++98 (no static_assert); GCC shares this POD's i386 layout, so the
 * offsets are guarded on the native build. */
static_assert(offsetof(AM2_Object, flags)         == 0x08,  "flags");
static_assert(offsetof(AM2_Object, owner)         == 0x10,  "owner");
static_assert(offsetof(AM2_Object, facing)        == 0x40,  "facing");
static_assert(offsetof(AM2_Object, maxHealth)     == 0x60,  "maxHealth");
static_assert(offsetof(AM2_Object, health)        == 0x62,  "health");
static_assert(offsetof(AM2_Object, heightAdj)     == 0x64,  "heightAdj");
static_assert(offsetof(AM2_Object, field94)       == 0x94,  "field94");
static_assert(offsetof(AM2_Object, shooterUid)    == 0x98,  "shooterUid");
static_assert(offsetof(AM2_Object, formationSlot) == 0xA0,  "formationSlot");
static_assert(offsetof(AM2_Object, power)         == 0xB0,  "power");
static_assert(offsetof(AM2_Object, soldierKind)   == 0x544, "soldierKind");
#endif

/* Live globals inside the running image. */



/* UID layout: three owner bits above a 29-bit per-owner counter. */
#define AM2_UID_OWNER_SHIFT 29
#define AM2_UID_COUNTER_MASK 0x1FFFFFFFu
#define AM2_UID_COUNTER_MIN  0x3E8u        /* 1000; also the wrap value */
#define AM2_UID_COUNTER_MAX  0x1FFFFC18u   /* 2^29 - 1000 */

/* Original: 0x004277A0. Binary search by uid. Returns the index, or -1 with
 * the insertion position written to *insert_at. */
int32_t __cdecl FindSlot(uint32_t uid, int32_t *insert_at);

/* Original: 0x00427820. Returns the registered object for `uid`, or NULL. */
void *__cdecl LookupByUID(uint32_t uid);
/* The same body expanded in place, which is how several callers have it: the
 * out-slot is uninitialised, so VC6 puts it in a dead argument slot of the
 * caller where there is one (ObjDie) and a stack local where not. */
static inline void *LookupByUIDInline(uint32_t uid)
{
    int32_t slot;
    int32_t i = FindSlot(uid, &slot);

    if (i >= 0)
        return g_objTable[i].obj;
    return 0;
}

/* 0x0044BA60, 14 callers. A cdecl wrapper for the line above and nothing else,
 * passing its one argument straight through. Reconstructed as the wrapper it
 * is: the callers reach it, not the thing it forwards to. */
void *__cdecl ObjByUidAlias(uint32_t uid);

/* Original: 0x00429740. Registers `obj`, allocating a UID when `uid` is 0.
 * Returns the UID it registered under. */
uint32_t __cdecl AddToItemList(AM2_Object *obj, uint32_t uid);

/* Original: 0x00428590. Unregisters `obj` by its uid. 1 if removed, 0 if it
 * was not in the table. */
int32_t __cdecl RemoveFromItemList(AM2_Object *obj);

/* Originals: 0x00427850 and 0x00427880. Walk every registered object.
 * Names are ours. Returns NULL at the end of the walk. */
void *__cdecl FirstItem(void);
void *__cdecl NextItem(void);

/* 0x00427CE0. Add an object's uid to the selected group, unless it is already
 * there or the group is over its cap, and mark the object. */
void __cdecl SelectUnit(void *obj);

/* Original: 0x00427C80, four callers. The counterpart of SelectUnit: drop the
 * object from the selection list and clear OBJ_FLAG_SELECTED. */
void __cdecl DeselectUnit(void *obj);

int objtable_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_OBJTABLE_H */
