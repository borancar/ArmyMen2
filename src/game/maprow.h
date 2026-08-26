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

int maprow_install(void);

#ifdef __cplusplus
}
#endif

#endif
