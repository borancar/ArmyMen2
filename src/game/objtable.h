#ifndef AM2_OBJTABLE_H
#define AM2_OBJTABLE_H

#include <stdint.h>
#include "../inject/orig.h"
#include "dist.h"     /* AM2_Point */
#include "rect.h"     /* AM2_Rect */

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
struct AM2_Object;

typedef struct {
    uint32_t           uid;
    struct AM2_Object *obj;
    uint32_t           stamp;
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

/* AM2_FireSpot is in point.h, beside AM2_Point. */

/* An item's type record, what OBJ_OFF_FIELD_C0 points at on a weapon. */
typedef struct AM2_ItemType {
    int32_t  kind;                  /* +0x00 ITEMTYPE_OFF_KIND */
    uint32_t cooldown;              /* +0x04 ITEMTYPE_OFF_COOLDOWN, milliseconds */
    uint8_t  _pad08[8];             /* +0x08 */
    int32_t  range;                 /* +0x10 ITEMTYPE_OFF_RANGE */
    int32_t  damage;                /* +0x14 ITEMTYPE_OFF_DAMAGE */
    int32_t  capacity;              /* +0x18 ITEMTYPE_OFF_CAPACITY */
    uint8_t  _pad1c[8];             /* +0x1C */
    int32_t  automatic;             /* +0x24 non-zero: repeats while held */
} AM2_ItemType;

/* The sight record (SIGHTCOUT_OFF_*): what a trooper's sight scan saw and
 * where to fire. A trooper carries one at OBJ_OFF_SIGHT_OUT_T2. */
typedef struct AM2_SightOut {
    int32_t      field00;           /* +0x00 cleared when a shot is decided here */
    uint8_t      bearing;           /* +0x04 SIGHTCOUT_OFF_BEARING */
    uint8_t      _pad05[3];
    int32_t      state;             /* +0x08 SIGHTCOUT_OFF_STATE */
    int32_t      hit;               /* +0x0C SIGHTCOUT_OFF_HIT */
    int32_t      seen;              /* +0x10 SIGHTCOUT_OFF_SEEN */
    AM2_FireSpot spot;              /* +0x14 SIGHTCOUT_OFF_X / _Y / _YADJ */
    uint8_t      _pad1a[2];
    uint32_t     uid;               /* +0x1C SIGHTCOUT_OFF_UID, the target */
    uint32_t     weaponUid;         /* +0x20 SIGHTCOUT_OFF_WEAPON_UID, an override */
} AM2_SightOut;

/* The steering record (SIGHTOUT_OFF_*): what a vehicle's AI (at +0x578) or a
 * roach's (at +0x540) asks of the frame. It sits four bytes BELOW a trooper's
 * AM2_SightOut, so the same absolute bytes mean different things -- +0x580 is
 * this one's state and that one's bearing. */
typedef struct AM2_SteerOut {
    uint8_t      heading;           /* +0x00 the hull heading it wants */
    uint8_t      aim;               /* +0x01 the facing (turret) it wants */
    uint8_t      _pad02[2];
    int32_t      hit;               /* +0x04 SIGHTOUT_OFF_HIT */
    int32_t      state;             /* +0x08 a pose: 1 arrived, 4 under way */
    int32_t      field0C;           /* +0x0C */
    int32_t      stop;              /* +0x10 nearly there: stop */
    int32_t      approach;          /* +0x14 slow down; a roach's 1 arrived / 2 heading */
    AM2_FireSpot spot;              /* +0x18 SIGHTOUT_OFF_X / _Y / _YADJ */
    uint8_t      _pad1e[2];
    uint32_t     uid;               /* +0x20 SIGHTOUT_OFF_UID, the target */
} AM2_SteerOut;

/* The roach's steering record (SIGHTBOUT_OFF_*), at +0x540: a third layout,
 * sharing only the heading byte with AM2_SteerOut. */
typedef struct AM2_RoachOut {
    uint8_t      heading;           /* +0x00 SIGHTBOUT_OFF_BEARING */
    uint8_t      _pad01[3];
    int32_t      hit;               /* +0x04 SIGHTOUT_OFF_HIT */
    uint8_t      _pad08[4];
    AM2_FireSpot spot;              /* +0x0C SIGHTBOUT_OFF_X / _Y / _YADJ */
    uint8_t      _pad12[2];
    int32_t      state;             /* +0x14 SIGHTBOUT_OFF_STATE: 1 arrived, 2 heading, 4 bite */
    int32_t      flag18;            /* +0x18 ROACHSTEP_OFF_FLAG18, picks the arm */
    uint32_t     uid;               /* +0x1C SIGHTBOUT_OFF_UID, the target */
} AM2_RoachOut;

/* The trooper AI's context record (SIGHTC_OFF_*): what the sight scan found
 * around a unit, filled by TrooperBuildContext and read by the AI steps. */
typedef struct AM2_SightCtx {
    int32_t             klass;      /* +0x00 SIGHTC_OFF_FIELD_00, ClassifyByCode74's 0..2 */
    struct AM2_Object  *leader;     /* +0x04 SIGHTC_OFF_LEADER */
    int32_t             leadRange;  /* +0x08 SIGHTC_OFF_LEAD_RANGE */
    uint8_t             leadBearing;/* +0x0C SIGHTC_OFF_LEAD_BEARING */
    uint8_t             _pad0d;
    AM2_Point           dest;       /* +0x0E SIGHTC_OFF_DEST */
    uint8_t             _pad12[2];
    struct AM2_Object  *observer;   /* +0x14 SIGHTC_OFF_OBSERVER */
    int32_t             range;      /* +0x18 SIGHTC_OFF_RANGE */
    uint8_t             bearing;    /* +0x1C SIGHTC_OFF_BEARING */
    uint8_t             _pad1d[3];
    struct AM2_Object  *found;      /* +0x20 SIGHTC_OFF_FOUND */
    int32_t             foundRange; /* +0x24 SIGHTC_OFF_FOUND_RANGE */
    uint8_t             foundBearing; /* +0x28 SIGHTC_OFF_FOUND_BEARING */
    uint8_t             _pad29[3];
    struct AM2_Object  *vehicle;    /* +0x2C SIGHTC_OFF_VEHICLE */
    int32_t             vehicleDist;/* +0x30 SIGHTC_OFF_VEHICLE_DIST */
    int32_t             destDist;   /* +0x34 SIGHTC_OFF_DEST_DIST */
    int32_t             destDistB;  /* +0x38 SIGHTC_OFF_DEST_DIST_B */
    uint8_t             seed;       /* +0x3C SIGHTC_OFF_SEED, thresholds 4 and 0x10 */
    uint8_t             _pad3d[3];
    struct AM2_Object  *weapon;     /* +0x40 SIGHTC_OFF_WEAPON */
    int32_t             kind;       /* +0x44 SIGHTC_OFF_KIND */
    int32_t             damage;     /* +0x48 SIGHTC_OFF_DAMAGE */
    int32_t             wantRange;  /* +0x4C SIGHTC_OFF_WANT_RANGE */
    int32_t             maxRange;   /* +0x50 SIGHTC_OFF_MAX_RANGE */
    int32_t             ready;      /* +0x54 SIGHTC_OFF_READY */
} AM2_SightCtx;

/* The vehicle AI's context record (SIGHT_OFF_*, AM2_AI_CONTEXT_BYTES): built
 * by AiBuildContext on AiStep's stack and read by the mode arms. */
typedef struct AM2_AiCtx {
    struct AM2_Object  *leader;     /* +0x00 SIGHT_OFF_LEADER */
    int32_t             leadRange;  /* +0x04 SIGHT_OFF_LEAD_RANGE, to dest */
    uint8_t             leadBearing;/* +0x08 SIGHT_OFF_LEAD_BEARING */
    uint8_t             _pad09;
    AM2_Point           dest;       /* +0x0A SIGHT_OFF_DEST */
    uint8_t             _pad0e[2];
    struct AM2_Object  *observer;   /* +0x10 SIGHT_OFF_OBSERVER */
    int32_t             range;      /* +0x14 SIGHT_OFF_RANGE */
    uint8_t             bearing;    /* +0x18 SIGHT_OFF_BEARING */
    uint8_t             _pad19[3];
    struct AM2_Object  *found;      /* +0x1C SIGHT_OFF_FOUND */
    int32_t             foundRange; /* +0x20 SIGHT_OFF_FOUND_RANGE */
    uint8_t             foundBearing; /* +0x24 SIGHT_OFF_FOUND_BEARING */
    uint8_t             _pad25[3];
    int32_t             destDist;   /* +0x28 SIGHT_OFF_DEST_DIST */
    uint8_t             seed;       /* +0x2C SIGHT_OFF_SEED */
    uint8_t             _pad2d[3];
    struct AM2_Object  *weapon;     /* +0x30 SIGHT_OFF_WEAPON */
    int32_t             kind;       /* +0x34 SIGHT_OFF_KIND */
    int32_t             wantRange;  /* +0x38 SIGHT_OFF_WANT_RANGE */
    int32_t             maxRange;   /* +0x3C SIGHT_OFF_MAX_RANGE */
    int32_t             ready;      /* +0x40 SIGHT_OFF_READY */
} AM2_AiCtx;

/* The roach's context record (AM2_ROACH_CONTEXT_BYTES): AM2_AiCtx's first
 * 0x34 bytes, then no weapon kind, so the ranges sit one dword lower. */
typedef struct AM2_RoachCtx {
    struct AM2_Object  *leader;     /* +0x00 */
    int32_t             leadRange;  /* +0x04 */
    uint8_t             leadBearing;/* +0x08 */
    uint8_t             _pad09;
    AM2_Point           dest;       /* +0x0A */
    uint8_t             _pad0e[2];
    struct AM2_Object  *observer;   /* +0x10 */
    int32_t             range;      /* +0x14 */
    uint8_t             bearing;    /* +0x18 */
    uint8_t             _pad19[3];
    struct AM2_Object  *found;      /* +0x1C */
    int32_t             foundRange; /* +0x20 */
    uint8_t             foundBearing; /* +0x24 */
    uint8_t             _pad25[3];
    int32_t             destDist;   /* +0x28 */
    uint8_t             seed;       /* +0x2C */
    uint8_t             _pad2d[3];
    struct AM2_Object  *weapon;     /* +0x30, always null */
    int32_t             wantRange;  /* +0x34 ROACHCTX_OFF_WANT_RANGE */
    int32_t             maxRange;   /* +0x38 ROACHCTX_OFF_MAX_RANGE */
    int32_t             ready;      /* +0x3C ROACHCTX_OFF_READY */
} AM2_RoachCtx;

typedef struct AM2_Object {
    uint32_t       type;            /* +0x00  switch selector, 0..8 */
    uint32_t       uid;             /* +0x04  written back on registration */
    union {                         /* +0x08 */
        uint32_t   flags;           /*   OBJ_OFF_FLAGS */
        uint8_t    flagsLo;         /*   its low byte, where a test reads only that */
    };
    int32_t        nameIdx;         /* +0x0C  AM2_OBJ_NAME_IDX_OFF, its kScriptNames slot */
    int8_t         owner;           /* +0x10  OBJ_OFF_ARMY, read with movsx */
    uint8_t        _pad11[1];       /* +0x11 */
    AM2_Point      pos;             /* +0x12 OBJ_OFF_POS */
    AM2_Point      prevPos;         /* +0x16 OBJ_OFF_PREV_POS, ObjFrameStep's copy */
    uint16_t       tile;            /* +0x1A OBJ_OFF_TILE, indexes the tile tables */
    uint32_t       prevTile;        /* +0x1C  OBJ_OFF_PREV_TILE */
    AM2_Rect       box;             /* +0x20 OBJ_OFF_BOX_OFFSETS, the hit box about pos */
    AM2_Rect       hit;             /* +0x30 OBJ_OFF_HIT_RECT, that box in world units */
    uint8_t        facing;          /* +0x40  OBJ_OFF_FACING */
    uint8_t        _pad41[1];       /* +0x41 */
    int16_t        row0YAdjust;     /* +0x42  OBJ_OFF_ROW0_Y_ADJUST */
    int32_t        field44;         /* +0x44  OBJ_OFF_FIELD_44, row 0's field3C */
    float          velZ;            /* +0x48  OBJ_OFF_VEL_Z */
    float          subpixelX;       /* +0x4C  OBJ_OFF_SUBPIXEL_X */
    float          subpixelY;       /* +0x50  OBJ_OFF_SUBPIXEL_Y */
    float          subpixelZ;       /* +0x54  OBJ_OFF_SUBPIXEL_Z */
    uint32_t       deadline58;      /* +0x58  OBJ_OFF_DEADLINE_58, a clock stamp */
    uint32_t       revealedUntil;   /* +0x5C  OBJ_OFF_REVEALED_UNTIL, game-clock ms */
    int16_t        maxHealth;       /* +0x60  OBJ_OFF_MAX_HEALTH */
    int16_t        health;          /* +0x62  OBJ_OFF_HEALTH */
    int8_t         heightAdj;       /* +0x64  OBJ_OFF_HEIGHT_ADJ */
    int8_t         heightSet;       /* +0x65  OBJ_OFF_HEIGHT_SET */
    uint8_t        _pad66[2];       /* +0x66 */
    struct AM2_Object *queryNext;   /* +0x68  OBJ_OFF_QUERY_NEXT, the query result's thread */
    AM2_RowSet     rowSet;          /* +0x6C OBJ_OFF_SUBRECORD */
    uint8_t        _pad7c[0x10];    /* +0x7C */
    uint8_t        cellCount;       /* +0x8C OBJ_OFF_CELL_COUNT */
    uint8_t        _pad8d[3];
    struct AM2_CellEntry *cellEntries; /* +0x90 OBJ_OFF_CELL_ENTRIES, cellCount of them */
    union {                         /* +0x94 OBJ_OFF_FIELD_94, by object type: */
        const uint8_t *field94;     /*   an item's type record */
        int32_t    aiDriven;        /*   types 2 and 3: non-zero, the AI drives it */
    };
    union {                         /* +0x98, by object type: */
        uint32_t   shooterUid;      /*   a shot's shooter uid */
        int32_t    rank;            /*   OBJ_OFF_RANK, a unit's 0..7 */
        int8_t     itemFlag;        /*   an item's: its low byte, signed; negative = step-up */
    };
    int32_t        repairFrame;     /* +0x9C  OBJ_OFF_REPAIR_FRAME */
    int32_t        formationSlot;   /* +0xA0  OBJ_OFF_FORMATION_SLOT */
    union {                         /* +0xA4, by object type: */
        struct {                    /*   an item's frame chain */
            uint32_t _chainA4;
            uint32_t chainUid;      /*   +0xA8 OBJ_OFF_CHAIN_UID, the head */
            uint32_t chainNextUid;  /*   +0xAC OBJ_OFF_CHAIN_NEXT_UID */
        };
        struct {                    /*   a missile's (type 5): */
            float    speedScale;    /*   +0xA4 MISSILE_OFF_SPEED_SCALE, multiplies the step */
            int16_t  ground;        /*   +0xA8 MISSILE_OFF_GROUND */
            uint8_t  _padaa[2];
            int32_t  flown;         /*   +0xAC MISSILE_OFF_FLOWN, units travelled */
        };
        struct {                    /*   types 2, 3 and 8: who follows it, */
            int32_t   capacity;     /*   OBJ_OFF_PTR_LIST, a pointer list */
            int32_t   count;        /*   SUBREC_OFF_COUNT */
            uint32_t *uids;         /*   SUBREC_OFF_ROWS */
        } followers;
    };
    union {                         /* +0xB0, by object type: */
        int32_t    scriptId;        /*   OBJ_OFF_SCRIPT_ID, 1-based; 0 = none */
        uint8_t    power;           /*   a shot's power byte */
        struct { int16_t fieldB0, fieldB2; };  /* types 2, 3, 8: two halves */
        AM2_Point  savedPoint;      /*   a trooper's: scriptPoint saved by the patrol */
    };
    union {                         /* +0xB4, by object type: */
        int32_t    scriptState;     /*   OBJ_OFF_SCRIPT_STATE */
        AM2_Point  scriptPoint;     /*   a trooper's: the point it was scripted to */
    };
    union {                         /* +0xB8, by object type: */
        int32_t    scriptFrame;     /*   OBJ_OFF_SCRIPT_FRAME */
        AM2_Point  goalPoint;       /*   a trooper's: the item it walks to pick up */
    };
    union {                         /* +0xBC, by object type: */
        uint32_t   scriptNext;      /*   OBJ_OFF_SCRIPT_NEXT, a clock deadline */
        AM2_Point  detour;          /*   a trooper's: a detour walked before scriptPoint */
    };
    union {                         /* +0xC0, by object type: */
        const int32_t *typeRec;     /*   OBJ_OFF_FIELD_C0, -> the type record; [0] is its kind */
        const AM2_ItemType *itemType; /* the same pointer, typed, on an item */
        struct { int16_t fieldC0, fieldC2; };  /* types 2, 3, 8: two halves */
        AM2_Point  dest;            /*   a trooper's: where it was told to go */
    };
    union {                         /* +0xC4, by object type: */
        uint32_t   followUid;       /*   OBJ_OFF_FOLLOW_UID */
        uint32_t   lastUse;         /*   an item's: ITEM_OFF_LAST_USE, a clock stamp */
    };
    uint32_t       pickupAfter;     /* +0xC8  OBJ_OFF_PICKUP_AFTER */
    union {                         /* +0xCC, by object type: */
        uint32_t   targetUid;       /*   OBJ_OFF_TARGET_UID */
        int32_t    ammo;            /*   a weapon's: ITEM_OFF_AMMO */
    };
    uint32_t       deadlineD0;      /* +0xD0  OBJ_OFF_DEADLINE_D0 */
    uint8_t        fieldD4;         /* +0xD4  OBJ_OFF_FIELD_D4, incremented per turn */
    uint8_t        _padd5[1];
    int16_t        fieldD6;         /* +0xD6  OBJ_OFF_FIELD_D6, wrapped into +/-0x100 */
    int32_t        stuckCount;      /* +0xD8  OBJ_OFF_STUCK_COUNT, refused moves in a row */
    uint16_t       region;          /* +0xDC  OBJ_OFF_REGION */
    int16_t        prevRegion;      /* +0xDE  OBJ_OFF_PREV_REGION, -1 for none */
    int16_t        goalRegion;      /* +0xE0  OBJ_OFF_GOAL_REGION */
    uint8_t        _pade2[2];
    int32_t        aiMode;          /* +0xE4  OBJ_OFF_AI_MODE */
    int32_t        aiModePrev;      /* +0xE8  OBJ_OFF_AI_MODE_PREV */
    int32_t        fieldEC;         /* +0xEC  OBJ_OFF_FIELD_EC, 0 or 1 */
    int32_t        moveStateAlt;    /* +0xF0  OBJ_OFF_MOVE_STATE_ALT */
    int32_t        fieldF4;         /* +0xF4  OBJ_OFF_FIELD_F4, only its sign is read */
    uint8_t        facingCopy;      /* +0xF8  OBJ_OFF_FACING_COPY */
    uint8_t        _padf9[3];
    int32_t        fieldFC;         /* +0xFC  OBJ_OFF_FIELD_FC */
    int32_t        field100;        /* +0x100 OBJ_OFF_FIELD_100 */
    uint8_t        hitDir;          /* +0x104 OBJ_OFF_HIT_DIR */
    uint8_t        _pad105[3];
    uint32_t       hitTime;         /* +0x108 OBJ_OFF_HIT_TIME, a clock stamp */
    int32_t        field10C;        /* +0x10C OBJ_OFF_FIELD_10C */
    int32_t        field110;        /* +0x110 OBJ_OFF_FIELD_110 */
    int32_t        field114;        /* +0x114 OBJ_OFF_FIELD_114 */
    AM2_Point      routeGoal;       /* +0x118 OBJ_OFF_ROUTE_GOAL */
    int32_t        moveUntil;       /* +0x11C OBJ_OFF_MOVE_UNTIL, game-clock ms */
    union {                         /* +0x120, the route's waypoints */
        AM2_Point  waypoints[256];  /*   indexed by moveAt */
        struct {
            AM2_Point moveFrom;     /*   OBJ_OFF_MOVE_FROM, waypoint 0 */
            AM2_Point moveTo;       /*   OBJ_OFF_MOVE_TO, waypoint 1 */
            int16_t  moveEnd;       /*   OBJ_OFF_MOVE_END, the terminator when there are 2 */
        };
    };
    uint16_t       moveAt;          /* +0x520 OBJ_OFF_MOVE_AT, the waypoint in hand */
    uint16_t       moveCount;       /* +0x522 OBJ_OFF_MOVE_COUNT */
    uint8_t        _pad524[5];      /* +0x524 */
    uint8_t        fireFlag;        /* +0x529 TROOPER_OFF_FIRE_FLAG */
    uint8_t        _pad52a[2];      /* +0x52A */
    union {                         /* +0x52C, by object type: */
        int32_t        vehicleKind; /*   VEHICLE_OFF_KIND */
        const uint8_t *tableRec2;   /*   SAVED_OFF_TABLE_REC2, a type 2's record */
    };
    union {                         /* +0x530 OBJ_OFF_FIELD_530 */
        int32_t    field530;        /*   ObjConceal compares to 5 */
        uint8_t    turretFacing;    /*   a vehicle's gun facing, its low byte */
    };
    union {                         /* +0x534, by object type: */
        const uint8_t *tableRec3;   /*   SAVED_OFF_TABLE_REC3, a type 3's record */
        uint32_t   roachStamp;      /*   ROACH_OFF_STAMP, a roach's: when its fan began */
    };
    union {                         /* +0x538..+0x5AC, by object type: */
        struct {                    /* a unit's (types 2 and 8) */
            int32_t   pose;         /* +0x538 OBJ_OFF_POSE */
            int32_t   posePending;  /* +0x53C OBJ_OFF_POSE_PENDING */
            int32_t   field540;     /* +0x540 OBJ_OFF_FIELD_540 */
            int32_t   soldierKind;  /* +0x544 OBJ_OFF_SOLDIER_KIND */
            int32_t   sarge;        /* +0x548 OBJ_OFF_SARGE */
            uint32_t  inventory[6]; /* +0x54C UNIT_OFF_INVENTORY, weapon uids */
            uint32_t  heldWeaponUid;/* +0x564 OBJ_OFF_HELD_WEAPON_UID */
            int32_t   inventorySel; /* +0x568 UNIT_OFF_INVENTORY_SEL, the slot in hand */
            uint32_t  uid56C;       /* +0x56C OBJ_OFF_UID_56C, a uid the menu row 8 order targets */
            uint32_t  riding;       /* +0x570 OBJ_OFF_RIDING, the uid ridden */
            int16_t   field574;     /* +0x574 OBJ_OFF_FIELD_574, the facing widened */
            uint8_t   _pad576[2];
            int32_t   field578;     /* +0x578 OBJ_OFF_FIELD_578 */
            AM2_SightOut sight;     /* +0x57C OBJ_OFF_SIGHT_OUT_T2 */
            uint32_t  fireStamp;    /* +0x5A0 UNIT_OFF_FIRE_STAMP */
            int32_t   field5A4;     /* +0x5A4 OBJ_OFF_FIELD_5A4 */
            int32_t   field5A8;     /* +0x5A8 OBJ_OFF_FIELD_5A8, the random name index */
        };
        struct {                    /* a vehicle's (type 3) */
            struct {                /* +0x538 VEHICLE_OFF_PTR_LIST, its riders: */
                int32_t   capacity; /*   a pointer list */
                int32_t   count;
                uint32_t *uids;
            } riders;
            int32_t   deathKind;    /* +0x544 0 destroyed outright, 5 the burn-out */
            int32_t   field548;     /* +0x548 0 drags the turret onto the hull */
            int32_t   field54C;     /* +0x54C */
            uint32_t  weaponUid;    /* +0x550 VEHICLE_OFF_WEAPON_UID, its gun */
            int32_t   seats;        /* +0x554 VEHICLE_OFF_SEATS */
            int32_t   field558;     /* +0x558 VEHICLE_OFF_FIELD_558 */
            int32_t   field55C;     /* +0x55C VEHICLE_OFF_FIELD_55C */
            int32_t   accelRate;    /* +0x560 VEHICLE_OFF_ACCEL_RATE, per second */
            uint32_t  turnInterval; /* +0x564 VEHICLE_OFF_TURN_INTERVAL, ms */
            int32_t   field568;     /* +0x568 */
            uint32_t  blockedSince; /* +0x56C the clock when a turn was first refused */
            int32_t   searchBase;   /* +0x570 the facing the turn search fans out from */
            int32_t   searchTries;  /* +0x574 the search's attempts, kept across frames */
            AM2_SteerOut ctl;       /* +0x578 its steering record */
            int32_t   gate;         /* +0x59C OBJ_OFF_FIELD_59C: 0 seeds the record */
            uint32_t  field5A0;     /* +0x5A0 */
            uint8_t   turnCommitted;/* +0x5A4 the row facing last committed */
            uint8_t   _pad5a5[3];
            int32_t   turnRecommit; /* +0x5A8 non-zero: commit whatever the row reads */
        };
        struct {                    /* a roach's (type 8) */
            int32_t      roachBaseDir; /* +0x538 ROACH_OFF_BASE_DIR, 0..7 */
            int32_t      roachFan;     /* +0x53C ROACH_OFF_FAN, the search counter */
            AM2_RoachOut roachCtl;     /* +0x540 its steering record */
        };
    };
    int32_t        nameIndex;       /* +0x5AC OBJ_OFF_NAME_INDEX */
    uint32_t       field5B0;        /* +0x5B0 OBJ_OFF_FIELD_5B0, game-clock ms */
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
static_assert(offsetof(AM2_Object, routeGoal)     == 0x118, "route goal");
static_assert(offsetof(AM2_Object, moveEnd)       == 0x128, "move end");
static_assert(offsetof(AM2_Object, moveAt)        == 0x520, "move at");
static_assert(offsetof(AM2_Object, moveCount)     == 0x522, "move count");
static_assert(offsetof(AM2_Object, maxHealth)     == 0x60,  "maxHealth");
static_assert(offsetof(AM2_Object, health)        == 0x62,  "health");
static_assert(offsetof(AM2_Object, heightAdj)     == 0x64,  "heightAdj");
static_assert(offsetof(AM2_Object, field94)       == 0x94,  "field94");
static_assert(offsetof(AM2_Object, shooterUid)    == 0x98,  "shooterUid");
static_assert(offsetof(AM2_Object, repairFrame)   == 0x9C,  "repairFrame");
static_assert(offsetof(AM2_Object, formationSlot) == 0xA0,  "formationSlot");
static_assert(offsetof(AM2_Object, power)         == 0xB0,  "power");
static_assert(offsetof(AM2_Object, soldierKind)   == 0x544, "soldierKind");
static_assert(offsetof(AM2_Object, row0YAdjust)   == 0x42,  "row0YAdjust");
static_assert(offsetof(AM2_Object, field44)       == 0x44,  "field44");
static_assert(offsetof(AM2_Object, velZ)          == 0x48,  "velZ");
static_assert(offsetof(AM2_Object, subpixelZ)     == 0x54,  "subpixelZ");
static_assert(offsetof(AM2_Object, deadline58)    == 0x58,  "deadline58");
static_assert(offsetof(AM2_Object, heightSet)     == 0x65,  "heightSet");
static_assert(offsetof(AM2_Object, typeRec)       == 0xC0,  "typeRec");
static_assert(offsetof(AM2_Object, followUid)     == 0xC4,  "followUid");
static_assert(offsetof(AM2_Object, targetUid)     == 0xCC,  "targetUid");
static_assert(offsetof(AM2_Object, aiMode)        == 0xE4,  "aiMode");
static_assert(offsetof(AM2_Object, field10C)      == 0x10C, "field10C");
static_assert(offsetof(AM2_Object, vehicleKind)   == 0x52C, "vehicleKind");
static_assert(offsetof(AM2_Object, pos)           == 0x12,  "pos");
static_assert(offsetof(AM2_Object, rowSet.count)  == 0x70,  "rowSet.count");
static_assert(offsetof(AM2_Object, rowSet.rows)   == 0x74,  "rowSet.rows");
static_assert(offsetof(AM2_Object, lastUse)       == 0xC4,  "lastUse");
static_assert(offsetof(AM2_Object, dest)          == 0xC0,  "dest");
static_assert(offsetof(AM2_Object, prevTile)      == 0x1C,  "prevTile");
static_assert(offsetof(AM2_Object, queryNext)     == 0x68,  "queryNext");
static_assert(offsetof(AM2_Object, heldWeaponUid) == 0x564, "heldWeaponUid");
static_assert(offsetof(AM2_Object, revealedUntil) == 0x5C,  "revealedUntil");
static_assert(offsetof(AM2_Object, pickupAfter)   == 0xC8,  "pickupAfter");
static_assert(offsetof(AM2_Object, deadlineD0)    == 0xD0,  "deadlineD0");
static_assert(offsetof(AM2_Object, stuckCount)    == 0xD8,  "stuckCount");
static_assert(offsetof(AM2_Object, prevRegion)    == 0xDE,  "prevRegion");
static_assert(offsetof(AM2_Object, goalRegion)    == 0xE0,  "goalRegion");
static_assert(offsetof(AM2_Object, moveStateAlt)  == 0xF0,  "moveStateAlt");
static_assert(offsetof(AM2_Object, hitTime)       == 0x108, "hitTime");
static_assert(offsetof(AM2_Object, field114)      == 0x114, "field114");
static_assert(offsetof(AM2_Object, waypoints)     == 0x120, "waypoints");
static_assert(offsetof(AM2_Object, field5A4)      == 0x5A4, "field5A4");
static_assert(offsetof(AM2_Object, field5B0)      == 0x5B0, "field5B0");
static_assert(offsetof(AM2_Object, facingCopy)    == 0xF8,  "facingCopy");
static_assert(offsetof(AM2_Object, fireFlag)      == 0x529, "fireFlag");
static_assert(offsetof(AM2_Object, tableRec2)     == 0x52C, "tableRec2");
static_assert(offsetof(AM2_Object, tableRec3)     == 0x534, "tableRec3");
static_assert(offsetof(AM2_Object, field574)      == 0x574, "field574");
static_assert(offsetof(AM2_Object, field578)      == 0x578, "field578");
static_assert(offsetof(AM2_Object, sight)         == 0x57C, "sight");
static_assert(offsetof(AM2_Object, ctl)           == 0x578, "vehicle ctl");
static_assert(offsetof(AM2_Object, roachCtl)      == 0x540, "roach ctl");
static_assert(sizeof(AM2_SightOut) == 0x24, "sight record");
static_assert(sizeof(AM2_SteerOut) == 0x24, "steer record");
static_assert(sizeof(AM2_RoachOut) == 0x20, "roach record");
static_assert(sizeof(AM2_SightCtx) == 0x58, "AM2_SIGHTC_BYTES");
static_assert(sizeof(AM2_AiCtx)    == 0x44, "AM2_AI_CONTEXT_BYTES");
static_assert(sizeof(AM2_RoachCtx) == 0x40, "AM2_ROACH_CONTEXT_BYTES");
static_assert(offsetof(AM2_AiCtx, ready)    == 0x40, "ai ready");
static_assert(offsetof(AM2_RoachCtx, ready) == 0x3C, "roach ready");
static_assert(offsetof(AM2_Object, heldWeaponUid) == 0x564, "held weapon");
static_assert(offsetof(AM2_Object, sentPos)       == 0x5B4, "sentPos");
static_assert(offsetof(AM2_SightOut, spot)        == 0x14,  "sight spot");
static_assert(offsetof(AM2_SightOut, uid)         == 0x1C,  "sight uid");
static_assert(offsetof(AM2_SightOut, weaponUid)   == 0x20,  "sight weaponUid");
static_assert(offsetof(AM2_ItemType, range)       == 0x10,  "itemType range");
static_assert(offsetof(AM2_SightCtx, dest)        == 0x0E,  "ctx dest");
static_assert(offsetof(AM2_SightCtx, ready)       == 0x54,  "ctx ready");
static_assert(offsetof(AM2_Object, tableRec2)     == 0x52C, "tableRec2");
static_assert(offsetof(AM2_Object, tableRec3)     == 0x534, "tableRec3");
static_assert(offsetof(AM2_Object, pose)          == 0x538, "pose");
static_assert(offsetof(AM2_Object, sarge)         == 0x548, "sarge");
static_assert(offsetof(AM2_Object, sentPos)       == 0x5B4, "sentPos");
static_assert(offsetof(AM2_Object, lastSeq)       == 0x5CC, "lastSeq");
static_assert(offsetof(AM2_Object, posePending)   == 0x53C, "posePending");
static_assert(offsetof(AM2_Object, riders.count)  == 0x53C, "riders.count");
static_assert(offsetof(AM2_Object, searchTries)   == 0x574, "searchTries");
static_assert(offsetof(AM2_Object, gate)          == 0x59C, "gate");
static_assert(offsetof(AM2_Object, turnRecommit)  == 0x5A8, "turnRecommit");
static_assert(offsetof(AM2_Object, nameIndex)     == 0x5AC, "nameIndex");
static_assert(offsetof(AM2_Object, field540)      == 0x540, "field540");
static_assert(offsetof(AM2_Object, seats)         == 0x554, "seats");
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
AM2_Object *__cdecl LookupByUID(uint32_t uid);
/* The same body expanded in place, which is how several callers have it: the
 * out-slot is uninitialised, so VC6 puts it in a dead argument slot of the
 * caller where there is one (ObjDie) and a stack local where not. */
static inline AM2_Object *LookupByUIDInline(uint32_t uid)
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
AM2_Object *__cdecl ObjByUidAlias(uint32_t uid);

/* Original: 0x00429740. Registers `obj`, allocating a UID when `uid` is 0.
 * Returns the UID it registered under. */
uint32_t __cdecl AddToItemList(AM2_Object *obj, uint32_t uid);

/* Original: 0x00428590. Unregisters `obj` by its uid. 1 if removed, 0 if it
 * was not in the table. */
int32_t __cdecl RemoveFromItemList(AM2_Object *obj);

/* Originals: 0x00427850 and 0x00427880. Walk every registered object.
 * Names are ours. Returns NULL at the end of the walk. */
AM2_Object *__cdecl FirstItem(void);
AM2_Object *__cdecl NextItem(void);

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
