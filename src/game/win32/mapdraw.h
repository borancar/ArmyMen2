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


/* 0x0042B5A0, one caller -- TakeMenuRequest, once a frame. The camera: glides
 * the eye toward ADDR_VIEW_TARGET at a limited speed, honours the two one-shot
 * flags, and derives the three view rectangles the map painter and the HUD
 * both read. See the body for what separates snap from hold. */
void __cdecl ViewUpdate(void);

/* 0x00413610. Locks once, draws the whole view outline, unlocks once -- the
 * matching half for the three line drawers, which each Lock and never Unlock.
 * De-static'd when RefreshDraw needed to call it by name. */
void __cdecl DrawViewRect(void);

/* 0x00462120. The leader's caret and the selected units' health bars, and
 * the pruning of ADDR_SELECTED_UIDS that happens while drawing them. */
void __cdecl DrawSelection(void);

/* 0x00409070. The air-support run: an aircraft on a three-leg path, the
 * gauge that times it, and the frame cycle of the object that called it. */
void __cdecl AirFrameDraw(void);

int mapdraw_install(void);

#ifdef __cplusplus
}
#endif


/* The radar's four drawing primitives and its colour decision. They live here
 * because they are rasterisers over the locked framebuffer, while the radar's
 * PAINT is a HUD widget vtable slot and lives in widget.cpp -- the same split
 * DrawTextVertical already has between font.cpp and its callers.
 *
 * All four blip drawers reject rather than clip, and all of them Lock without
 * Unlocking: the pairing belongs to HudRadarPaint, which Unlocks once at the
 * end. */
void __cdecl DrawRectFast(const AM2_Rect *r, int32_t colour);
void __cdecl DrawBlip3(int32_t x, int32_t y, int32_t colour);
void __cdecl DrawBlipPulse(int32_t x, int32_t y, int32_t colourA,
                           int32_t colourB, int32_t phase);
void __cdecl DrawBlipSquare(int32_t x, int32_t y, int32_t colourA,
                            int32_t colourB, int32_t phase);
/* The object is AM2_Object; taken as void here so mapdraw.h stays includable
 * from src/inject, which is C and has no object model. */
int32_t __cdecl RadarBlipColour(const void *obj, int32_t *blink);

#endif /* AM2_MAPDRAW_H */
