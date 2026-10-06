/* maprow.h -- the map's cell grid and its depth-sorted draw list. See
 * maprow.cpp for why these four live on the flat side. */
#ifndef AM2_MAPROW_H
#define AM2_MAPROW_H

#include <stdint.h>

#include "dist.h"                /* AM2_Point */
#include "rect.h"                /* AM2_Rect */

#ifdef __cplusplus
extern "C" {
#endif

struct AM2_AnimTable;
struct AM2_Anim;
struct AM2_Sprite;

struct AM2_MapRow;

struct AM2_Object;

/* One entry of a cell list, ROW_ENTRY_BYTES (AM2_CELL_ENTRY_STRIDE): what it
 * stands for, its links in that cell's list, and which cell that is (-1 for
 * none). A map row's buffer holds them for g_mapDesc, where the lists are
 * depth-sorted; an object's OBJ_OFF_CELL_ENTRIES holds them for
 * g_objMapDesc. */
typedef struct AM2_CellEntry {
    union {
        struct AM2_MapRow *obj;         /* +0x00 DEPTH_OFF_OBJ, a row's */
        struct AM2_Object *object;      /*       an object's */
    };
    struct AM2_CellEntry *prev;          /* +0x04 DEPTH_OFF_PREV */
    struct AM2_CellEntry *next;          /* +0x08 DEPTH_OFF_NEXT */
    int32_t              cell;          /* +0x0C ROW_ENTRY_OFF_CELL */
} AM2_CellEntry;

/* A map descriptor: the cell grid the rows register in. g_mapDesc is the
 * drawable one and g_objMapDesc the one objects are found through. */
typedef struct AM2_MapDesc {
    int32_t        cols;                /* +0x00 MAPDESC_OFF_COLS */
    int32_t        rows;                /* +0x04 MAPDESC_OFF_ROWS */
    int32_t        shift;               /* +0x08 MAPDESC_OFF_SHIFT, log2 of cols */
    AM2_CellEntry **cells;               /* +0x0C MAPDESC_OFF_CELLS, one list head each */
} AM2_MapDesc;

/* A map row: the drawable node every map object carries, linked into the
 * cell grid and the depth list. Transcribed as the struct the original
 * accessed by member; only the named fields are claimed. */
typedef struct AM2_MapRow {
    uint32_t              flags;        /* +0x00  bit 0: on the map */
    struct AM2_Sprite    *sprite;       /* +0x04  ROW_OFF_SPRITE */
    struct AM2_Sprite    *prevSprite;   /* +0x08  ROW_OFF_PREV_SPRITE */
    AM2_Rect              rect;         /* +0x0C  ROW_OFF_RECT */
    AM2_Point             pos;          /* +0x1C  ROW_OFF_X, ROW_OFF_Y */
    int16_t               yAdjust;      /* +0x20  ROW_OFF_Y_ADJUST */
    int16_t               prevX;        /* +0x22  ROW_OFF_PREV_X */
    int16_t               prevY;        /* +0x24  ROW_OFF_PREV_Y */
    int16_t               depthLayer;   /* +0x26  ROW_OFF_FIELD_26 / OBJ_OFF_DEPTH_LAYER, only when > 0 on both */
    float                 depthSlope;   /* +0x28  ROW_OFF_FIELD_28 / OBJ_OFF_DEPTH_SLOPE */
    const void           *lut;          /* +0x2C  MAPOBJ_OFF_LUT */
    void                 *palette;      /* +0x30  MAPOBJ_OFF_PALETTE */
    uint8_t               owns;         /* +0x34  ROW_OFF_OWNS */
    uint8_t               _pad35[3];    /* +0x35 */
    AM2_CellEntry         *buffer;       /* +0x38  ROW_OFF_BUFFER, `owns` entries */
    int16_t               field3C;      /* +0x3C  ROW_OFF_FIELD_3C */
    uint8_t               _pad3e[2];    /* +0x3E */
    struct AM2_AnimTable *animCur;      /* +0x40  ROW_OFF_ANIM_CUR */
    struct AM2_Anim      *animPlaying;  /* +0x44  ROW_OFF_ANIM_PLAYING */
    struct AM2_AnimTable *animNext;     /* +0x48  ROW_OFF_ANIM_NEXT */
    int16_t               frame;        /* +0x4C  ROW_OFF_FRAME */
    int16_t               animNextId;   /* +0x4E  ROW_OFF_ANIM_NEXT_ID */
    uint8_t               heading;      /* +0x50  ROW_OFF_HEADING */
    uint8_t               cell;         /* +0x51  ROW_OFF_CELL */
    uint8_t               _pad52[2];    /* +0x52 */
    uint32_t              stamp54;      /* +0x54  ROW_OFF_STAMP_54 */
    uint32_t              turnStamp;    /* +0x58  ROW_OFF_TURN_STAMP, game-clock ms */
    uint8_t               headingBias;  /* +0x5C  ROW_OFF_HEADING_BIAS */
    uint8_t               _pad5d[3];    /* +0x5D, to AM2_OBJ_ROW_STRIDE */
} AM2_MapRow;

#if defined(__cplusplus) && !defined(_MSC_VER)
#include <stddef.h>   /* GCC-side only, as in objtable.h */
static_assert(offsetof(AM2_MapRow, rect)        == 0x0C, "rect");
static_assert(offsetof(AM2_MapRow, pos)         == 0x1C, "pos");
static_assert(offsetof(AM2_MapRow, yAdjust)     == 0x20, "yAdjust");
static_assert(offsetof(AM2_MapRow, depthSlope)  == 0x28, "depthSlope");
static_assert(offsetof(AM2_MapRow, lut)         == 0x2C, "lut");
static_assert(offsetof(AM2_MapRow, buffer)      == 0x38, "buffer");
static_assert(offsetof(AM2_MapRow, field3C)     == 0x3C, "field3C");
static_assert(offsetof(AM2_MapRow, frame)       == 0x4C, "frame");
static_assert(offsetof(AM2_MapRow, stamp54)     == 0x54, "stamp54");
static_assert(offsetof(AM2_MapRow, headingBias) == 0x5C, "headingBias");
static_assert(sizeof(AM2_MapRow)                == 0x60, "AM2_OBJ_ROW_STRIDE");
#endif

/* Original: 0x0041D740. Order two objects for drawing: layer first when both
 * have one, then a slope-weighted screen position. */
int32_t __cdecl DepthCompare(const AM2_MapRow *a, const AM2_MapRow *b);

/* Original: 0x0041D8F0. Link a node that is not yet in the list into its
 * sorted place. The primitive under DepthInsert, a different address. */
void __cdecl DepthLink(AM2_CellEntry *node, AM2_CellEntry **head);

/* Original: 0x0041DB90. Put one node back into depth order after the object it
 * points at has moved. Walks outward in one direction and re-links once. */
void __cdecl DepthResort(AM2_CellEntry *node, AM2_CellEntry **head);

/* Original: 0x0041D2B0, six callers. Give a row its entry buffer, sized from a
 * width and height in world units, and put it on the map. Returns the buffer's
 * size in bytes. */
int32_t __cdecl RowAlloc(int32_t w, int32_t h, AM2_MapRow *row, AM2_MapDesc *desc);

/* Original: 0x0041D980. Link every cell the row's current rectangle covers,
 * from entries assumed not to be in any list yet. */
void __cdecl RowRegisterAll(AM2_MapRow *row, AM2_MapDesc *desc);

/* Original: 0x0041D480, thirty-seven callers. Bring one row's membership of
 * the map's cell grid up to date with where it now is. */
void __cdecl RowUpdate(AM2_MapRow *row, int32_t force, AM2_MapDesc *desc);

/* Original: 0x0041D270 and 0x0041D210. The map descriptor's grid, freed and
 * rebuilt. Both are called on both descriptors when a map loads. */
void __cdecl MapDescFree(void *desc);
void __cdecl MapDescInit(void *desc, int32_t w, int32_t h);

/* Original: 0x0040A050. Clear a map object and give it a sprite, a position
 * and the default palette. See maprow.cpp for why it is filed here. */
void __cdecl RowInit(AM2_MapRow *row, struct AM2_Sprite *sprite, int32_t x, int32_t y);

/* 0x00434DA0. One block of `count` rows, each placed at its spec's (x, y)
 * offset from (dx, dy) and given a buffer sized by the spec's (w, h), with a
 * bounding rect copied into the 0x20-byte header. */
void __cdecl BuildRowSet(void *set, int32_t count, const void *specs,
                         int32_t dx, int32_t dy, const void *rect);

/* Original: 0x0041D3D0, three callers. Put a new sprite on a row, rebuilding
 * its cell buffer only when the new one needs more cells. */
void __cdecl RowSetSprite(AM2_MapRow *row, struct AM2_Sprite *sprite, AM2_MapDesc *desc);

/* 0x0040A1A0. Put a row on an animation frame. -2 does nothing, -1 means the
 * frame it is already on, 0 clears bit 0 of the row and returns; anything else
 * is an entry id, and one not in the table takes entry 0. */
void __cdecl SetAnimFrame(AM2_MapRow *row, int16_t frame, int32_t force);

int maprow_install(void);

#ifdef __cplusplus
}
#endif

/* 0x0040A130, two callers. The animation's field4 for an id, doubled when the
 * row's lut is the one that doubles. */
int32_t __cdecl RowAnimField4(const void *row, uint16_t id);

/* 0x00434C90, two callers. BuildRowSet's sibling, driven by a def record with
 * twelve-byte specs instead of a count and sixteen-byte ones. */
void __cdecl BuildRowsFromDef(void *set, const void *def, int32_t x, int32_t y,
                              uint32_t objFlags);

/* 0x00460800. Initialise row pool A -- decals and seq step 0. */
void __cdecl RowPoolAInit(void);
/* 0x00460AC0. Initialise row pool B -- troopers, vehicles and roaches. */
void __cdecl RowPoolBInit(void);
/* 0x00460860. Tear down row pool A: the only place its rows are freed. */
void __cdecl RowPoolAFree(void);
/* 0x00460B30. Tear down row pool B: the only place its rows are freed. */
void __cdecl RowPoolBFree(void);

/* 0x004608C0. Release a pool-A entry. ANSWERS THE NEXT INDEX, corrected for
 * the compaction -- every caller but the evictor discards it, so `void` is
 * what this looks like and would break the evictor's second loop. */
int32_t __cdecl RowPoolARelease(void *entry);
/* 0x00460B90. The same for pool B. */
int32_t __cdecl RowPoolBRelease(void *entry);
/* 0x004609D0. Pool A is over capacity: drop off-screen entries, then drop
 * from the head regardless if that did not free enough. */
void __cdecl RowPoolAEvict(void);
/* 0x00460CA0. The same for pool B. */
void __cdecl RowPoolBEvict(void);

/* 0x00460A60. Take a slot from pool A, evicting first if it is over capacity.
 * Always returns a slot: the cap test is not a refusal. */
void *__cdecl RowPoolAAlloc(void);
/* 0x00460D30. The same for pool B. */
void *__cdecl RowPoolBAlloc(void);

/* 0x00460D90. Initialise a SEQ context: capacity records of 48 bytes, each
 * owning a 0x60-byte row registered at w x h. FIVE arguments -- read off the
 * call site, not the body. */
void __cdecl SeqCtxInit(void *ctx, int32_t capacity, int32_t margin,
                        int32_t w, int32_t h);
/* 0x00460E30. Free every record's row; harmless on an unallocated context. */
void __cdecl SeqCtxFree(void *ctx);
/* 0x00460EC0. Release a record. CONTEXT FIRST. Answers the next index,
 * corrected for the compaction; only the evictor uses the answer. */
int32_t __cdecl SeqRetire(void *ctx, void *rec);
/* 0x00460FD0. Drop off-screen records, then drop from the head regardless. */
void __cdecl SeqEvict(void *ctx);
/* 0x00461070. Take a record, evicting first when count exceeds
 * capacity MINUS margin -- not capacity, which is what the hardcoded
 * allocators test. Always returns a record. */
void *__cdecl SeqAlloc(void *ctx);

/* 0x004600F0. May this weapon kind respawn? Permissive on three of four
 * paths; the refusing one needs a multiplayer session and cannot run here. */
int32_t __cdecl RespawnKindAllowed(int32_t kind);
/* 0x004601D0. Pick a respawn kind at random. TWO OUTPUTS: it answers the
 * chosen kind AND writes the def's +0x30 field through `out`. One of its two
 * callers discards the answer. */
int32_t __cdecl RandomRespawnKind(int32_t *out);

/* 0x004610E0. Bring the row pools and seq contexts up; run from state 2. */
void __cdecl SeqSubsystemInit(void);
/* 0x00461120. Take them down again, LIFO. */
void __cdecl FreeSeqContexts(void);

/* 0x00461EA0. Leave a remains row behind a dying object: four fields off the
 * source row, ROW_OFF_FIELD_26 forced to 3, then RowUpdate. */
void __cdecl LeaveRemainsRow(const void *src);

/* 0x00461F90. Pick a sprite from the grid by direction and elapsed time,
 * write it into the row, and answer whether more frames remain. */
int32_t __cdecl TimedDirFrame(void *rows, int32_t dir);

/* 0x00460120. Build the weighted bag a respawn draws from: two passes over
 * ADDR_SPAWN_KIND_TABLE, the first to size it and the second to fill it. */
void __cdecl BuildRespawnPool(int32_t seed);

/* 0x00464416. MSVC's srand: store the seed ADDR_GAME_RAND's LCG reads. */
void __cdecl GameSrand(int32_t seed);

/* 0x00461950. Drop a decal row at a point, unless that tile or any of its
 * eight neighbours is occupied. */
void __cdecl PlaceGroundDecal(int32_t x, int32_t z, int32_t variant);

/* 0x00461EE0. Start a sequence effect whose frame is chosen by direction out
 * of sprite group C. Six arguments, confirmed from a call site. */
void __cdecl SeqStartDirEffect(int32_t x, int32_t z, int32_t angle,
                               int32_t group, uint32_t ownerUid,
                               int32_t field26);

/* 0x004619F0. Spawn one particle. Argument 6 is a MODE: negative picks a
 * random direction, non-negative is an angle. */
void __cdecl SpawnParticle(const uint32_t *at, int32_t rowField2C,
                           int32_t kind, int32_t speed, int32_t height,
                           int32_t angle);

/* 0x00461B20. The death spray: six particles of six kinds, then 6..10 more. */
void __cdecl DiedEffectA(void *obj);

/* 0x00461BA0. One to three particles where something was hit, the count from
 * a ten-entry weighted table. */
void __cdecl SpawnHitEffect(const uint32_t *at, int32_t rowField2C,
                            int32_t dir, int32_t height);

/* 0x00461C30. The directional death burst: up to seven particles chosen by an
 * 8x7 mask indexed on the facing sector. Both its tables are decoded from the
 * prologue that builds them on the stack. */
void __cdecl DiedEffectB(const uint32_t *at, int32_t facing,
                         int32_t rowField2C, int32_t height);

#endif
