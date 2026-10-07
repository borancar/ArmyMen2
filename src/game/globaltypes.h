/* globaltypes.h -- the record types of the original's global tables.
 *
 * Shared by the layout (origstate.h, which declares the storage), by the
 * per-build name map (globals.h) and by the relink's typed externs
 * (src/vc6/include/am2_relink_os.h), so a global has one type everywhere it
 * is declared. C-compatible: plain structs, fixed-width fields. */
#ifndef AM2_GLOBALTYPES_H
#define AM2_GLOBALTYPES_H

#include <stdint.h>
#include "point.h"   /* AM2_Point, for AM2_MaskRec */

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
    AM2_Point pts[40];
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

/* One player's record inside the player message, 0x60 bytes. SendPlayerMsg
 * fills it from the comm object's slot of the same index. */
typedef struct AM2_PlayerRec {
    uint32_t id;            /* +0x00 MSGREC_OFF_ID, the player id */
    int32_t  colour;        /* +0x04 REC_PLAYER_COLOUR */
    int32_t  zero;          /* +0x08 MSGREC_OFF_ZERO, written 0 */
    int32_t  pad0c;         /* +0x0C */
    int32_t  setting;       /* +0x10 REC_PLAYER_SETTING, the army's points */
    char     name[0x40];    /* +0x14 MSGREC_OFF_NAME */
    int32_t  team;          /* +0x54 REC_PLAYER_TEAM */
    int32_t  readyToLoad;   /* +0x58 REC_PLAYER_F270 */
    int32_t  wasHere;       /* +0x5C REC_PLAYER_WAS_HERE */
} AM2_PlayerRec;

/* The player message, ADDR_PLAYER_MSG, 0x238 bytes: the host's view of the
 * session, one record per player slot. */
typedef struct AM2_PlayerMsg {
    uint32_t      header[2];        /* +0x00 kind and size */
    int32_t       count;            /* +0x08 MSG_PLAYER_COUNT */
    uint32_t      connected;        /* +0x0C MSG_PLAYER_CONNECTED */
    uint32_t      pad10;            /* +0x10 */
    int32_t       hasMap;           /* +0x14 MSG_PLAYER_HAS_MAP */
    int32_t       scoreLimit;       /* +0x18 MSG_PLAYER_SCORE_LIMIT */
    char          mapName[0x40];    /* +0x1C MSG_PLAYER_MAP_NAME */
    char          levelName[0x40];  /* +0x5C MSG_PLAYER_LEVEL_NAME */
    uint32_t      mapSum;           /* +0x9C MSG_PLAYER_MAP_SUM */
    uint32_t      ruleSum;          /* +0xA0 MSG_PLAYER_RULE_SUM */
    uint32_t      ruleArg;          /* +0xA4 MSG_PLAYER_RULE_ARG */
    AM2_PlayerRec recs[4];          /* +0xA8 MSG_PLAYER_RECORDS */
    int32_t       overFlags;        /* +0x228 MSG_PLAYER_OVER_FLAGS */
    int32_t       setting22c;       /* +0x22C MSG_PLAYER_SETTING_22C */
    int32_t       version;          /* +0x230 MSG_PLAYER_VERSION */
    int32_t       checksum;         /* +0x234 MSG_PLAYER_CHECKSUM */
} AM2_PlayerMsg;

/* The scenario table, ADDR_SCENARIOS[ADDR_SCENARIO_COUNT]: one 0x40-byte
 * record per "Scenario<n>" header in the map file, its four parts one per
 * army slot, each part an array of rows ParseScenarioPart reads off the wire
 * and PlaceScenario turns into units. See SCEN_ROW_OFF_* in orig.h. */
typedef struct AM2_ScenRow {
    int32_t  kind;                  /* +0x00 SCEN_ROW_OFF_KIND, 0x8005 the leader */
    struct { int16_t x, y; } pos;   /* +0x04 SCEN_ROW_OFF_POS */
    uint8_t  _pad08[8];             /* +0x08 */
    uint8_t  amount;                /* +0x10 SCEN_ROW_OFF_AMOUNT */
    uint8_t  flag;                  /* +0x11 SCEN_ROW_OFF_FLAG, set when the wire byte was ZERO */
    uint8_t  field12;               /* +0x12 SCEN_ROW_OFF_FIELD_12, forced to 1 if zero */
    char     name[0x51];            /* +0x13 SCEN_ROW_OFF_NAME, unbounded on the wire */
    uint32_t at;                    /* +0x64 SCEN_ROW_OFF_AT, a packed point */
    uint8_t  field68;               /* +0x68 SCEN_ROW_OFF_FIELD_68 */
    uint8_t  _pad69[3];
} AM2_ScenRow;

typedef struct AM2_ScenPart {
    int32_t      field0;            /* +0x00 cleared, never filled */
    int16_t      count;             /* +0x04 SCENARIO_PART_OFF_COUNT */
    uint16_t     _pad06;
    AM2_ScenRow *rows;              /* +0x08 SCENARIO_PART_OFF_ROWS */
} AM2_ScenPart;

/* The record's header as it comes off the wire, AM2_SCENARIO_HDR_BYTES. */
typedef struct AM2_ScenHdr {
    char         tag[8];            /* +0x00 "Scenario" */
    char         digit;             /* +0x08 SCENARIO_HDR_OFF_DIGIT, '1'..'4' */
    char         _pad09[7];
} AM2_ScenHdr;

typedef struct AM2_Scenario {
    AM2_ScenHdr  hdr;               /* +0x00 */
    AM2_ScenPart parts[4];          /* +0x10 SCENARIO_OFF_PARTS, AM2_SCENARIO_PARTS */
} AM2_Scenario;

/* One entry of the dirty-rectangle list, ADDR_DIRTY_RECTS[], 0x14 bytes
 * (AM2_DIRTY_RECORD_SIZE): a RECT, then the links of the doubly linked list
 * ADDR_DIRTY_HEAD and ADDR_DIRTY_TAIL hold the ends of. Index 0 is the
 * terminator, so the links are indices, not pointers. */
typedef struct AM2_DirtyRec {
    int32_t  left, top, right, bottom;  /* +0x00, a RECT */
    uint16_t prev;                      /* +0x10 DIRTY_OFF_PREV */
    uint16_t next;                      /* +0x12 DIRTY_OFF_NEXT */
} AM2_DirtyRec;

/* One built font, ADDR_GLYPH_SIZE[] at ADDR_FONT_STRIDE (0x20C) apart: the
 * size of its glyph block, each character's offset into it, the block. The
 * image indexes it as `font * 0x20C` with the field folded into the
 * displacement -- an array of these, not three parallel tables. */
typedef struct AM2_FontRec {
    uint32_t  size;                     /* +0x000 ADDR_GLYPH_SIZE */
    uint16_t  offsets[256];             /* +0x004 ADDR_GLYPH_OFFSETS */
    uint8_t  *base;                     /* +0x204 ADDR_FONT_BASES */
    uint32_t  field208;                 /* +0x208 */
} AM2_FontRec;

#endif /* AM2_GLOBALTYPES_H */
