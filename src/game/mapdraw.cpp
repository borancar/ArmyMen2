/* Map repainting -- reconstructed from ArmyMen2.exe.
 *
 *   SetDrawTarget     0x0041AC40   28 call sites
 *   RedrawMapRegion   0x0041CF90    7 call sites
 *
 * RedrawMapRegion is the dirty-rectangle repaint, and the fourth function found
 * bracketed by Lock/Unlock. It is mostly glue: the work is in the two callees
 * it still shares with the original image.
 *
 * The order matters and is worth stating, because it is not the obvious one.
 * The screen-space setup at 0x0042D9B0 takes the rectangle BY VALUE, so it
 * cannot hand a transformed rectangle back -- and indeed the tile walker is
 * given the original world-space rectangle, not a converted one. The two
 * callees therefore work in different coordinate spaces on purpose: one
 * prepares the screen area, the other walks tiles in world space.
 *
 * All three calls are cdecl and the original cleans all 24 bytes of their
 * arguments in one `add esp, 0x18` after the last of them, rather than after
 * each.
 */

#include "mapdraw.h"
#include "surface.h"
#include "../inject/patch.h"

#include <stdint.h>

#define g_drawTarget (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_LOCKED_SURFACE)
#define g_backBuffer (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_SURFACE)
#define g_mapDesc    ((void *)(uintptr_t)ADDR_MAP_DESC)

/* 0x0041E440: the recursive tile walker. Shifts the rectangle's edges right by
 * 8 to get tile indices and bounds-checks them against the map descriptor.
 * Not reconstructed. */
typedef void (__cdecl *am2_draw_map_tiles_fn)(const AM2_Rect *world,
                                              void *mapDesc, int32_t flag);
#define orig_draw_map_tiles (*(am2_draw_map_tiles_fn)ADDR_DRAW_MAP_TILES)

/* The map is painted once into a cache surface and the visible part copied out
 * of it a rectangle at a time; this is that copy. Both surfaces and both
 * origins come from globals, so the whole of the caller's rectangle is consumed
 * as geometry.
 *
 * Two different origins are subtracted, which is the only subtle thing here.
 * The SOURCE rectangle is measured from the camera, in tiles scaled by 16 --
 * that is where the region sits on the painted map. The DESTINATION point is
 * measured from the screen origin instead. They are not the same offset and
 * using one for both would slide the backdrop against the sprites drawn over
 * it.
 *
 * Nothing checks the result. A failed BltFast leaves whatever was in the back
 * buffer and the tile walker draws over it regardless, as in the original. */
static_assert(DDBLTFAST_WAIT == 0x10, "DDBLTFAST_WAIT");

#define g_mapCache (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)
#define g_cameraX  (*(const int32_t *)(uintptr_t)ADDR_CAMERA_X)
#define g_cameraY  (*(const int32_t *)(uintptr_t)ADDR_CAMERA_Y)
#define g_viewX  (*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X)
#define g_viewY  (*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y)

/* Tiles are 16 pixels, so the camera scales by 16 to reach pixels. */
#define TILE_SHIFT 4

void __cdecl BlitMapBackdrop(AM2_Rect world)
{
    RECT src;

    src.left   = world.left   - (g_cameraX << TILE_SHIFT);
    src.top    = world.top    - (g_cameraY << TILE_SHIFT);
    src.right  = world.right  - (g_cameraX << TILE_SHIFT);
    src.bottom = world.bottom - (g_cameraY << TILE_SHIFT);

    IDirectDrawSurface_BltFast(g_backBuffer,
                               (DWORD)(world.left - g_viewX),
                               (DWORD)(world.top  - g_viewY),
                               g_mapCache, &src, DDBLTFAST_WAIT);
}

void __cdecl SetDrawTarget(LPDIRECTDRAWSURFACE surf)
{
    /* The compare is redundant -- storing unconditionally would behave
     * identically -- but it is what the original does. */
    if (g_drawTarget != surf)
        g_drawTarget = surf;
}

void __cdecl RedrawMapRegion(const AM2_Rect *world)
{
    /* Degenerate regions are rejected before any work, and the test is for
     * equality rather than ordering: an inverted rectangle would slip through
     * here exactly as it does in the original. */
    if (world->top == world->bottom)
        return;
    if (world->left == world->right)
        return;

    BlitMapBackdrop(*world);

    SetDrawTarget(g_backBuffer);
    if (!LockSurface(g_backBuffer))
        return;

    orig_draw_map_tiles(world, g_mapDesc, 0);
    UnlockSurface();
}

/* Paint the map into its cache surface, one tile at a time -- 0x0042D580.
 *
 * This is where the map actually becomes pixels. Everything else in this file
 * moves the result about; this makes it.
 *
 * The caller asks for a rectangle in tile coordinates, it is clipped against
 * the visible area, and every surviving tile is blitted from the tile sheet
 * into the cache. Two things are worth knowing.
 *
 * The visible area is the camera. The four dwords from ADDR_CAMERA_X are read
 * as a RECT here and handed straight to IntersectRect, so the camera position
 * and the visible-tile rectangle are the same four words seen two ways.
 *
 * And the tile index is the source coordinates. The low five bits are the
 * column, scaled by sixteen, so the sheet is 32 tiles across and the index
 * decodes to a position in it with no lookup table at all.
 *
 * The row is `(idx >> 5) * 16`, and the original writes it as `sar eax,1` then
 * `and al,0xF0` -- which masks only the LOW BYTE and leaves everything above
 * bit 7 alone. Reading that as `& 0xF0` caps the row at 15 and puts every tile
 * with an index of 512 or more in the wrong place; the Boot Camp A/B went from
 * 22 differing pixels to 33,137 and said so immediately. `& ~0xF` is the
 * faithful reading, and is exactly `(idx >> 5) * 16`.
 *
 * Exercised by every map repaint, so an error here is visible immediately --
 * which is the point of doing it. */
static_assert(DDBLT_WAIT == 0x01000000, "DDBLT_WAIT");

#define g_mapCacheSurf (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)
#define g_mapSprite    (*(uint8_t **)(uintptr_t)ADDR_MAP_SURFACE)
#define g_mapTiles     (*(const uint16_t **)(uintptr_t)ADDR_MAP_TILES)
#define g_mapRowShift  (*(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT)
#define g_visibleTiles ((const AM2_Rect *)(uintptr_t)ADDR_VISIBLE_TILES)

void __cdecl PaintMapTiles(const AM2_Rect *tiles)
{
    LPDIRECTDRAWSURFACE dest = g_mapCacheSurf;
    LPDIRECTDRAWSURFACE sheet;
    AM2_Rect            clipped;
    int32_t             row, top;

    if (!dest)
        return;
    sheet = *(LPDIRECTDRAWSURFACE *)(g_mapSprite + 0x10);
    if (!sheet)
        return;

    if (!IntersectRect((LPRECT)&clipped, (const RECT *)tiles,
                       (const RECT *)g_visibleTiles))
        return;

    /* Tile coordinates become pixels relative to the camera. */
    top = (clipped.top - g_visibleTiles->top) * MAP_TILE_SIZE;

    for (row = clipped.top; row < clipped.bottom; row++) {
        const uint16_t *cell = g_mapTiles
                             + ((row << g_mapRowShift) + tiles->left);
        int32_t         left = (clipped.left - g_visibleTiles->left)
                               * MAP_TILE_SIZE;
        int32_t         col;

        for (col = clipped.left; col < clipped.right; col++) {
            uint16_t idx = *cell;
            RECT     to, from;

            to.left   = left;
            to.top    = top;
            to.right  = left + MAP_TILE_SIZE;
            to.bottom = top + MAP_TILE_SIZE;

            from.left   = (idx & MAP_SHEET_COLUMNS) * MAP_TILE_SIZE;
            from.top    = (int32_t)((idx >> 1) & ~0xFu);
            from.right  = from.left + MAP_TILE_SIZE;
            from.bottom = from.top + MAP_TILE_SIZE;

            IDirectDrawSurface_Blt(dest, &to, sheet, &from, DDBLT_WAIT, NULL);

            left += MAP_TILE_SIZE;
            cell++;
        }
        top += MAP_TILE_SIZE;
    }
}

int mapdraw_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_SET_DRAW_TARGET, (const void *)SetDrawTarget, "SetDrawTarget", 1);
    rc |= patch_replace(ADDR_REDRAW_MAP_REGION, (const void *)RedrawMapRegion,
                        "RedrawMapRegion", 1);
    rc |= patch_replace(ADDR_BLIT_MAP_BACKDROP, (const void *)BlitMapBackdrop,
                        "BlitMapBackdrop", 4);
    rc |= patch_replace(ADDR_PAINT_MAP_TILES, (const void *)PaintMapTiles,
                        "PaintMapTiles", 1);
    return rc;
}
