/* globaltypes.h -- the record types of the original's global tables.
 *
 * Shared by the layout (origstate.h, which declares the storage), by the
 * per-build name map (globals.h) and by the relink's typed externs
 * (src/vc6/include/am2_relink_os.h), so a global has one type everywhere it
 * is declared. C-compatible: plain structs, fixed-width fields. */
#ifndef AM2_GLOBALTYPES_H
#define AM2_GLOBALTYPES_H

#include <stdint.h>

/* One A* node of FindPath's tile search, ADDR_PATH_NODES[0x10000], indexed
 * by tile. The original addresses the table as an array of these --
 * `[tile*16 + base + field]` -- not through a pointer to the element. */
typedef struct AM2_PathNode {
    uint16_t g;         /* +0x00 PATHNODE_OFF_G,      cost from the start */
    uint16_t h;         /* +0x02 PATHNODE_OFF_H,      ApproxDistXY * 1.5 */
    uint16_t depth;     /* +0x04 PATHNODE_OFF_DEPTH,  hops from the start */
    uint16_t stamp;     /* +0x06 PATHNODE_OFF_STAMP,  the search generation */
    uint16_t parent;    /* +0x08 PATHNODE_OFF_PARENT, the came-from tile */
    uint16_t prev;      /* +0x0A PATHNODE_OFF_PREV,   open-list links, by g+h */
    uint16_t next;      /* +0x0C PATHNODE_OFF_NEXT */
    uint8_t  state;     /* +0x0E PATHNODE_OFF_STATE,  1 open, 2 closed */
    uint8_t  pad;       /* +0x0F */
} AM2_PathNode;

/* One rank's record, ADDR_RANK_RECORDS[AM2_RANK_COUNT], 28 bytes. The image
 * indexes it `[rank*28 + 0x473DC0 + field]`. DefFillRankRecords fills it from
 * the trooper definitions. */
typedef struct AM2_RankRec {
    int32_t sight_range;    /* +0x00 RANK_REC_OFF_SIGHT_RANGE */
    int32_t field_04;       /* +0x04 RANK_REC_OFF_FIELD_04, the view half-arc */
    int32_t field_08;       /* +0x08 RANK_REC_OFF_FIELD_08, the out-of-arc range */
    float   fire_scale;     /* +0x0C RANK_REC_OFF_FIRE_SCALE */
    int32_t threshold;      /* +0x10 RANK_REC_OFF_THRESHOLD */
    int32_t max_health;     /* +0x14 RANK_REC_OFF_MAX_HEALTH */
    int32_t xp;             /* +0x18 RANK_REC_OFF_XP */
} AM2_RankRec;

/* The per-direction sight cache, ADDR_SIGHT_BLOCK_BY_DIR, one 16-byte record
 * per four headings, indexed `[dir*16 + 0x4F8FB8 + field]`. */
typedef struct AM2_SightDirRec {
    int16_t low;            /* +0x00 SIGHTDIR_OFF_LOW */
    int16_t mid;            /* +0x02 SIGHTDIR_OFF_MID */
    int16_t high;           /* +0x04 SIGHTDIR_OFF_HIGH */
    int16_t pad;
    int32_t stamp;          /* +0x08 SIGHTDIR_OFF_STAMP */
    int32_t trace_stamp;    /* +0x0C SIGHTDIR_OFF_TRACE_STAMP */
} AM2_SightDirRec;

/* One of the twelve squad positions, ADDR_FORMATION_SLOTS[AM2_FORMATION_SLOTS],
 * 6 bytes: a facing relative to the leader and a distance. FormationPoint
 * indexes it `[slot*6 + 0x473EA0 + field]`. */
typedef struct AM2_FormationSlot {
    uint8_t facing;         /* +0x00 */
    uint8_t pad;
    int16_t dist;           /* +0x02 48, 64, 96 or 128 */
    int16_t pad2;
} AM2_FormationSlot;

/* One footprint mask: a count and up to forty point offsets, 0xA4 bytes
 * (AM2_MASK_STRIDE). The roach's table is one record per direction at
 * ADDR_ROACH_MASK_COUNT; the vehicles' is [kind*32 + dir] at
 * ADDR_VEHICLE_MASK_COUNT. The image reaches a point as
 * `[(rec*41 + i)*4 + base + 4]`, which is this record indexed. */
typedef struct AM2_MaskRec {
    int32_t count;
    struct { int16_t x, y; } pts[40];
} AM2_MaskRec;

/* One checkbox of the OPTIONS dialog, ADDR_OPTION_TABLE[AM2_OPTION_COUNT],
 * 0x24 bytes. The dialog is declared by this table; see ADDR_OPTION_TABLE. */
typedef struct AM2_OptionRec {
    int32_t     widget;     /* +0x00 index in the parent's widget array */
    int32_t     x, y;       /* +0x04 */
    int32_t     group;      /* +0x0C non-zero: heads a group */
    int32_t     first;      /* +0x10 the group's first widget */
    int32_t     last;       /* +0x14 and last, inclusive */
    int32_t     bit;        /* +0x18 the bit this checkbox owns */
    int32_t     which;      /* +0x1C non-zero: ADDR_GAME_OVER_FLAGS */
    const char *label;      /* +0x20 */
} AM2_OptionRec;

#endif /* AM2_GLOBALTYPES_H */
