/* maprow.h -- the map's cell grid and its depth-sorted draw list. See
 * maprow.cpp for why these four live on the flat side. */
#ifndef AM2_MAPROW_H
#define AM2_MAPROW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x0041D740. Order two objects for drawing: layer first when both
 * have one, then a slope-weighted screen position. */
int32_t __cdecl DepthCompare(void *a, void *b);

/* Original: 0x0041D8F0. Link a node that is not yet in the list into its
 * sorted place. The primitive under DepthInsert, a different address. */
void __cdecl DepthLink(void *node, void **head);

/* Original: 0x0041DB90. Put one node back into depth order after the object it
 * points at has moved. Walks outward in one direction and re-links once. */
void __cdecl DepthResort(void *node, void **head);

/* Original: 0x0041D2B0, six callers. Give a row its entry buffer, sized from a
 * width and height in world units, and put it on the map. Returns the buffer's
 * size in bytes. */
int32_t __cdecl RowAlloc(int32_t w, int32_t h, void *row, void *desc);

/* Original: 0x0041D980. Link every cell the row's current rectangle covers,
 * from entries assumed not to be in any list yet. */
void __cdecl RowRegisterAll(void *row, void *desc);

/* Original: 0x0041D480, thirty-seven callers. Bring one row's membership of
 * the map's cell grid up to date with where it now is. */
void __cdecl RowUpdate(void *row, int32_t force, void *desc);

/* Original: 0x0041D270 and 0x0041D210. The map descriptor's grid, freed and
 * rebuilt. Both are called on both descriptors when a map loads. */
void __cdecl MapDescFree(void *desc);
void __cdecl MapDescInit(void *desc, int32_t w, int32_t h);

/* Original: 0x0040A050. Clear a map object and give it a sprite, a position
 * and the default palette. See maprow.cpp for why it is filed here. */
void __cdecl RowInit(void *row, void *sprite, int32_t x, int32_t y);

/* 0x00434DA0. One block of `count` rows, each placed at its spec's (x, y)
 * offset from (dx, dy) and given a buffer sized by the spec's (w, h), with a
 * bounding rect copied into the 0x20-byte header. */
void __cdecl BuildRowSet(void *set, int32_t count, const void *specs,
                         int32_t dx, int32_t dy, const void *rect);

/* Original: 0x0041D3D0, three callers. Put a new sprite on a row, rebuilding
 * its cell buffer only when the new one needs more cells. */
void __cdecl RowSetSprite(void *row, void *sprite, void *desc);

/* 0x0040A1A0. Put a row on an animation frame. -2 does nothing, -1 means the
 * frame it is already on, 0 clears bit 0 of the row and returns; anything else
 * is an entry id, and one not in the table takes entry 0. */
void __cdecl SetAnimFrame(void *row, int16_t frame, int32_t force);

int maprow_install(void);

#ifdef __cplusplus
}
#endif

/* 0x0040A130, two callers. The animation's field4 for an id, doubled when the
 * row's lut is the one that doubles. */
int16_t __cdecl RowAnimField4(const void *row, uint16_t id);

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

#endif
