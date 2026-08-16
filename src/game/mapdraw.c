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

/* 0x0042D9B0: screen-space setup for the region, taking the rectangle by
 * value. Not reconstructed. */
typedef void (__cdecl *am2_prepare_screen_rect_fn)(AM2_Rect world);
#define orig_prepare_screen_rect \
    (*(am2_prepare_screen_rect_fn)ADDR_PREPARE_SCREEN_RECT)

/* 0x0041E440: the recursive tile walker. Shifts the rectangle's edges right by
 * 8 to get tile indices and bounds-checks them against the map descriptor.
 * Not reconstructed. */
typedef void (__cdecl *am2_draw_map_tiles_fn)(const AM2_Rect *world,
                                              void *mapDesc, int32_t flag);
#define orig_draw_map_tiles (*(am2_draw_map_tiles_fn)ADDR_DRAW_MAP_TILES)

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

    orig_prepare_screen_rect(*world);

    SetDrawTarget(g_backBuffer);
    if (!LockSurface(g_backBuffer))
        return;

    orig_draw_map_tiles(world, g_mapDesc, 0);
    UnlockSurface();
}

int mapdraw_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_SET_DRAW_TARGET, SetDrawTarget, "SetDrawTarget", 1);
    rc |= patch_replace(ADDR_REDRAW_MAP_REGION, RedrawMapRegion,
                        "RedrawMapRegion", 1);
    return rc;
}
