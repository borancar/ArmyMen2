#ifndef AM2_MAPDRAW_H
#define AM2_MAPDRAW_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"
#include "../rect.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Map repainting.
 *
 * Coordinates here are world space in 1/256-tile fixed point: the recursive
 * tile walker shifts all four edges of the rectangle right by 8 to get tile
 * indices before bounds-checking them against the map descriptor at
 * 0x00514F20. The camera origin lives in 0x00514EA8 / 0x00514EAC and is scaled
 * by 16 when converting to screen space.
 */

/* Original: 0x0041AC40, 28 call sites. Designates which surface subsequent
 * drawing targets. It does not lock anything -- LockSurface later locks
 * whatever was designated. The redundant compare before the store is the
 * original's, kept. */
void __cdecl SetDrawTarget(LPDIRECTDRAWSURFACE surf);

/* Original: 0x0041CF90, 7 call sites. Repaints one dirty rectangle of the map:
 * screen-space setup, target the back buffer, lock, recursively draw every tile
 * covering the region, unlock. Empty rectangles are rejected before any of that
 * happens, so a zero-width or zero-height region costs nothing. */
void __cdecl RedrawMapRegion(const AM2_Rect *world);

/* Original: 0x0042D9B0, 1 call site -- RedrawMapRegion, above. Copies one
 * rectangle of the already-painted map cache into the back buffer.
 *
 * Takes the rectangle BY VALUE, which is why it cannot hand a transformed one
 * back and why RedrawMapRegion still passes world space to the tile walker
 * afterwards. */
void __cdecl BlitMapBackdrop(AM2_Rect world);

/* Original: 0x0042C0E0, 1 call site -- RestoreLostSurfaces. Reload the tileset
 * from its `.atl` file into the map surface after DirectDraw took it back.
 *
 * Named from its own error strings rather than from that call site, where it
 * had been recorded as `ADDR_ON_MAP_RESTORED`. The file is IFF and every
 * `DIB ` chunk in it is one tile bitmap.
 *
 * Only reachable when a surface is lost, which does not happen under Xvfb, so
 * this one is verified by reading rather than by running. */
void __cdecl RestoreTileSet(void);

/* Original: 0x0042DA30. Compose one frame into the offscreen surface and
 * BltFast it onto the back buffer, then save this frame's rectangles as last
 * frame's for the dirty-rectangle merge to compare against. */
/* 0x0042B420. The screen shake, one step per frame and the first thing
 * ComposeFrame does: advance both phases, bounce them off the faded
 * amplitude, and shift the view rectangle by the whole-pixel result. */
void __cdecl ScrollDecay(void);

/* 0x0041E440. Walk the map's cell grid over a world rectangle, hand every
 * object to the depth sort, draw the sorted list -- and split the rectangle
 * and recurse when the sort runs out of room. */
void __cdecl DrawMapObjects(const AM2_Rect *world, void *desc, int32_t deep);

/* 0x0041E160. Insert one object into the sorted depth list. 0 means the list
 * is FULL, which is what makes the walker subdivide; an object outside the
 * region is dropped and 1 is returned. */
int32_t __cdecl DepthInsert(void *obj, const AM2_Rect *world);

/* 0x0041D740. Which of two objects is drawn first: layer, then a slope
 * projection, then y, then the two pointers as addresses so the order is
 * total. 0 is a null argument, not a tie. */
int32_t __cdecl DepthCompare(void *a, void *b);

/* 0x0040A090. Draw one map object: clip, convert the clipped rectangle into a
 * destination position and a source rectangle inside the sprite, put the
 * object's own remap table and palette into the sprite, and blit. */
void __cdecl DrawMapObject(void *obj, const AM2_Rect *world);

/* 0x0041D000. Repaint every registered dirty rectangle meeting a region --
 * the clipped intersection, not the whole record. */
void __cdecl RepaintDirtyList(const AM2_Rect *region);

void __cdecl ComposeFrame(void);

/* Original: 0x0041D060, called from ComposeFrame. Move the part of the view
 * that is still valid to where it now belongs and repaint the strips the
 * scroll exposed. */
void __cdecl ScrollView(void);

/* Original: 0x0042D6D0, called from ComposeFrame. Follow the view with the
 * camera, scroll the painted map cache by the tile delta, and repaint the
 * strips that exposed. */
void __cdecl ScrollMapCache(void);

/* 0x0041DCE0, one caller -- ComposeFrame, at the top of every frame. Three
 * uint16 counters beside the dirty list, cleared as a set. What each counts is
 * not established; that they are 16 bits and reset together is. */
void __cdecl ResetDrawCounts(void);

int mapdraw_install(void);

#ifdef __cplusplus
}
#endif

/* Original: 0x0041D8F0. Link a node that is not yet in the list into its
 * sorted place. The primitive under DepthInsert, which is a different
 * address. */
void __cdecl DepthLink(void *node, void **head);

/* Original: 0x0041DB90. Put one node back into depth order after the object it
 * points at has moved. Walks outward in one direction and re-links once. */
void __cdecl DepthResort(void *node, void **head);

#endif /* AM2_MAPDRAW_H */
