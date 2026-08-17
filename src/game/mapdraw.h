#ifndef AM2_MAPDRAW_H
#define AM2_MAPDRAW_H

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"
#include "rect.h"

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
void __cdecl ComposeFrame(void);

/* Original: 0x0041D060, called from ComposeFrame. Move the part of the view
 * that is still valid to where it now belongs and repaint the strips the
 * scroll exposed. */
void __cdecl ScrollView(void);

/* Original: 0x0042D6D0, called from ComposeFrame. Follow the view with the
 * camera, scroll the painted map cache by the tile delta, and repaint the
 * strips that exposed. */
void __cdecl ScrollMapCache(void);

int mapdraw_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MAPDRAW_H */
