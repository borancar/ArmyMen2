#ifndef AM2_OBJTABLE_H
#define AM2_OBJTABLE_H

#include <stdint.h>
#include "../inject/orig.h"
#include "dist.h"     /* AM2_Point */

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
struct AM2_MapRow;

/* The sub-list header an object carries at OBJ_OFF_SUBRECORD: the record list
 * its rows were built from, the row count (OBJ_OFF_ROW_COUNT) and the rows
 * themselves (OBJ_OFF_ROWS), AM2_OBJ_ROW_STRIDE apart. */
typedef struct {
    const void        *list;      /* +0x00 SUBREC_OFF_LIST */
    int32_t            count;     /* +0x04 SUBREC_OFF_COUNT */
    struct AM2_MapRow *rows;      /* +0x08 */
    int32_t            capacity;  /* +0x0C */
} AM2_RowSet;

typedef struct {
    uint32_t       type;            /* +0x00  switch selector, 0..8 */
    uint32_t       uid;             /* +0x04  written back on registration */
    uint32_t       flags;           /* +0x08  OBJ_OFF_FLAGS */
    uint8_t        _pad0c[4];       /* +0x0C */
    int8_t         owner;           /* +0x10  OBJ_OFF_ARMY, read with movsx */
    uint8_t        _pad11[1];       /* +0x11 */
    AM2_Point      pos;             /* +0x12 OBJ_OFF_POS */
    uint8_t        _pad16[0x2A];    /* +0x16 */
    uint8_t        facing;          /* +0x40  OBJ_OFF_FACING */
    uint8_t        _pad41[1];       /* +0x41 */
    int16_t        row0YAdjust;     /* +0x42  OBJ_OFF_ROW0_Y_ADJUST */
    int32_t        field44;         /* +0x44  OBJ_OFF_FIELD_44, row 0's field3C */
    uint8_t        _pad48[0x10];    /* +0x48 */
    uint32_t       deadline58;      /* +0x58  OBJ_OFF_DEADLINE_58, a clock stamp */
    uint8_t        _pad5c[4];       /* +0x5C */
    int16_t        maxHealth;       /* +0x60  OBJ_OFF_MAX_HEALTH */
    int16_t        health;          /* +0x62  OBJ_OFF_HEALTH */
    int8_t         heightAdj;       /* +0x64  OBJ_OFF_HEIGHT_ADJ */
    int8_t         heightSet;       /* +0x65  OBJ_OFF_HEIGHT_SET */
    uint8_t        _pad66[6];       /* +0x66 */
    AM2_RowSet     rowSet;          /* +0x6C OBJ_OFF_SUBRECORD */
    uint8_t        _pad7c[0x18];    /* +0x7C */
    const uint8_t *field94;         /* +0x94  OBJ_OFF_FIELD_94, the type record */
    uint32_t       shooterUid;      /* +0x98  OBJ_OFF_RANK, a shot's shooter uid */
    uint8_t        _pad9c[4];       /* +0x9C */
    int32_t        formationSlot;   /* +0xA0  OBJ_OFF_FORMATION_SLOT */
    uint8_t        _pada4[0xC];     /* +0xA4 */
    uint8_t        power;           /* +0xB0  OBJ_OFF_SCRIPT_ID, a shot's power byte */
    uint8_t        _padb1[0x5B];    /* +0xB1 */
    int32_t        field10C;        /* +0x10C OBJ_OFF_FIELD_10C */
    uint8_t        _pad110[0x41C];  /* +0x110 */
    int32_t        vehicleKind;     /* +0x52C VEHICLE_OFF_KIND */
    uint8_t        _pad530[8];      /* +0x530 */
    int32_t        pose;            /* +0x538 OBJ_OFF_POSE */
    uint8_t        _pad53c[8];      /* +0x53C */
    int32_t        soldierKind;     /* +0x544 OBJ_OFF_SOLDIER_KIND */
    int32_t        sarge;           /* +0x548 OBJ_OFF_SARGE */
    uint32_t       inventory[6];    /* +0x54C UNIT_OFF_INVENTORY, weapon uids */
    uint32_t       _pad564;         /* +0x564 */
    int32_t        inventorySel;    /* +0x568 UNIT_OFF_INVENTORY_SEL, the slot in hand */
    uint32_t       _pad56c;         /* +0x56C */
    uint32_t       riding;          /* +0x570 OBJ_OFF_RIDING, the uid ridden */
    uint8_t        _pad574[0x40];   /* +0x574 */
    AM2_Point      sentPos;         /* +0x5B4 TROOPER_OFF_SENT_POS */
    uint32_t       sentPosSeq;      /* +0x5B8 TROOPER_OFF_SENT_POS_T */
    uint8_t        sentFacing;      /* +0x5BC TROOPER_OFF_SENT_FACING */
    uint8_t        _pad5bd[3];
    uint32_t       sentFacingSeq;   /* +0x5C0 TROOPER_OFF_SENT_FACING_T */
    int32_t        sentPose;        /* +0x5C4 TROOPER_OFF_SENT_POSE */
    uint32_t       sentPoseSeq;     /* +0x5C8 TROOPER_OFF_SENT_POSE_T */
    uint32_t       lastSeq;         /* +0x5CC TROOPER_OFF_LAST_SEQ */
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
static_assert(offsetof(AM2_Object, row0YAdjust)   == 0x42,  "row0YAdjust");
static_assert(offsetof(AM2_Object, field44)       == 0x44,  "field44");
static_assert(offsetof(AM2_Object, deadline58)    == 0x58,  "deadline58");
static_assert(offsetof(AM2_Object, heightSet)     == 0x65,  "heightSet");
static_assert(offsetof(AM2_Object, field10C)      == 0x10C, "field10C");
static_assert(offsetof(AM2_Object, vehicleKind)   == 0x52C, "vehicleKind");
static_assert(offsetof(AM2_Object, pos)           == 0x12,  "pos");
static_assert(offsetof(AM2_Object, rowSet.count)  == 0x70,  "rowSet.count");
static_assert(offsetof(AM2_Object, rowSet.rows)   == 0x74,  "rowSet.rows");
static_assert(offsetof(AM2_Object, pose)          == 0x538, "pose");
static_assert(offsetof(AM2_Object, sarge)         == 0x548, "sarge");
static_assert(offsetof(AM2_Object, sentPos)       == 0x5B4, "sentPos");
static_assert(offsetof(AM2_Object, lastSeq)       == 0x5CC, "lastSeq");
static_assert(offsetof(AM2_Object, inventory)     == 0x54C, "inventory");
static_assert(offsetof(AM2_Object, inventorySel)  == 0x568, "inventorySel");
static_assert(offsetof(AM2_Object, riding)        == 0x570, "riding");
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
