/* DirectDraw surface lock/unlock -- reconstructed from ArmyMen2.exe.
 *
 *   LockSurface    0x0041B9A0   38 call sites
 *   UnlockSurface  0x0041BA40   34 call sites
 *
 * STANDING NOTE -- the Restore paths are UNTESTED, and there are two of them.
 *
 *   1. Here in LockSurface. On DDERR_SURFACELOST the original calls Restore and
 *      then, if that succeeds, falls straight through to publishing the
 *      descriptor WITHOUT retrying the Lock -- so it stores an uninitialised
 *      lpSurface and lPitch. That is reproduced faithfully below. It is a real
 *      defect, not a misreading.
 *   2. In DrawSpriteClipped, where a BltFast returning DDERR_SURFACELOST calls
 *      the recovery chain at 0x00445EB0.
 *
 * Neither can be reached from a headless Boot Camp run: losing a surface needs
 * an alt-tab or a display mode change. Anyone exercising them should do it
 * deliberately -- windowed, then force a mode switch or minimise/restore -- and
 * should expect the LockSurface path to publish garbage on the first frame
 * after recovery, because that is what the original does. Do not "fix" it
 * without deciding that behavioural fidelity is no longer the goal.
 *
 * See surface.h. All DirectDraw declarations come from ddraw.h rather than
 * being restated here; the values the game hardcodes all match it exactly:
 * dwSize 0x6C == sizeof(DDSURFACEDESC), flags 1 == DDLOCK_WAIT, and the error
 * it compares against, 0x887601C2, == DDERR_SURFACELOST.
 */

#include "surface.h"
#include "rect.h"
#include "winmain.h"
#include "report.h"
#include "../inject/patch.h"

#include <stdint.h>

/* Writable views of the globals LockSurface publishes. blit.c takes the same
 * two read-only, which is why it declares its own const versions. */
#define g_surfaceLocked  (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_lockedSurface  (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_LOCKED_SURFACE)
#define g_primarySurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_frameBuf       (*(void **)(uintptr_t)ADDR_FRAMEBUFFER)
#define g_pitch          (*(int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)

/* The game hardcodes dwSize = 0x6C, which is DDSURFACEDESC and not
 * DDSURFACEDESC2 (124 bytes). Check the SDK agrees rather than assume it. */
typedef char am2_ddsd_is_the_v1_struct[(sizeof(DDSURFACEDESC) == 0x6C) ? 1 : -1];

int32_t __cdecl LockSurface(LPDIRECTDRAWSURFACE surf)
{
    DDSURFACEDESC desc;
    HRESULT       hr;

    /* Locks do not nest. Re-locking the surface already held is a no-op that
     * succeeds; asking for a different one while holding this is refused. */
    if (g_surfaceLocked) {
        if (surf == g_lockedSurface)
            return 1;
        orig_log("another surface already locked!\n");
        return 0;
    }

    desc.dwSize = sizeof desc;
    hr = IDirectDrawSurface_Lock(surf, NULL, &desc, DDLOCK_WAIT, NULL);

    if (hr != DD_OK) {
        if (hr == DDERR_SURFACELOST)
            hr = IDirectDrawSurface_Restore(g_primarySurface);
        if (hr != DD_OK)
            return 0;

        /* Faithful, and wrong: on a successful Restore the original falls
         * straight through to the store below without retrying the Lock, so it
         * publishes an uninitialised descriptor. Reproduced rather than fixed,
         * as with the AddToItemList overflow path -- matching behaviour matters
         * more than intent, and this only fires on a lost surface (alt-tab or a
         * mode change), which is presumably why it was never noticed. */
    }

    g_lockedSurface = surf;
    g_surfaceLocked = 1;
    g_frameBuf      = desc.lpSurface;
    g_pitch         = desc.lPitch;
    return 1;
}

int32_t __cdecl UnlockSurface(void)
{
    if (g_surfaceLocked) {
        LPDIRECTDRAWSURFACE surf = g_lockedSurface;

        /* Unlock takes back the same pointer Lock handed out. */
        IDirectDrawSurface_Unlock(surf, g_frameBuf);

        g_surfaceLocked = 0;
        g_frameBuf      = NULL;
        g_pitch         = 0;
    }
    return 1;
}

/* ---- creating and clearing --------------------------------------------- */

#define g_ddraw2     (*(LPDIRECTDRAW2 *)(uintptr_t)ADDR_DIRECTDRAW2)
#define g_screenRect (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_RECT)

static_assert(DDSD_CAPS + DDSD_HEIGHT + DDSD_WIDTH + DDSD_PIXELFORMAT == 0x1007,
              "the descriptor fields it fills in");
static_assert(DDSCAPS_SYSTEMMEMORY == 0x800, "DDSCAPS_SYSTEMMEMORY");
static_assert(DDCKEY_SRCBLT == 8, "DDCKEY_SRCBLT");
static_assert((DDBLT_COLORFILL | DDBLT_WAIT) == 0x01000400, "colour fill");
static_assert(sizeof(DDBLTFX) == 0x64, "DDBLTFX");

LPDIRECTDRAWSURFACE __cdecl CreateOffscreenSurface(int32_t width, int32_t height,
                                                   int32_t caps,
                                                   int32_t colourKey)
{
    DDSURFACEDESC       ddsd;
    LPDIRECTDRAWSURFACE surf;
    HRESULT             hr;
    /* The only bit of `caps` anyone looks at: "system memory, no argument". */
    const int32_t       forceSystem = caps & DDSCAPS_OFFSCREENPLAIN;

    /* Ask the primary what it looks like, then change only the size and the
     * caps. This is how the new surface ends up in the same pixel format
     * without anyone naming one. */
    memset(&ddsd, 0, sizeof ddsd);
    ddsd.dwSize = sizeof ddsd;
    hr = IDirectDrawSurface_GetSurfaceDesc(g_primarySurface, &ddsd);
    if (hr) {
        ReportError(hr, "GetSurfaceDesc()");
        return NULL;
    }

    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = forceSystem
        ? (DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY)
        : DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth  = (DWORD)width;
    ddsd.dwHeight = (DWORD)height;

    hr = IDirectDraw2_CreateSurface(g_ddraw2, &ddsd, &surf, NULL);
    if (hr) {
        /* Only worth retrying if we had not already asked for system memory. */
        if (forceSystem) {
            ReportError(hr, "CreateSurface()");
            return NULL;
        }
        orig_log("Failed to put surface into video memory!\n");
        ddsd.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
        hr = IDirectDraw2_CreateSurface(g_ddraw2, &ddsd, &surf, NULL);
        if (hr) {
            ReportError(hr, "CreateSurface()");
            return NULL;
        }
    }

    if (colourKey >= 0) {
        DDCOLORKEY ck;

        /* A single colour, so both ends of the range are the same. */
        ck.dwColorSpaceLowValue  = (DWORD)colourKey;
        ck.dwColorSpaceHighValue = (DWORD)colourKey;
        IDirectDrawSurface_SetColorKey(surf, DDCKEY_SRCBLT, &ck);
    }
    return surf;
}

int32_t __cdecl ClearSurface(LPDIRECTDRAWSURFACE surf, uint32_t colour)
{
    RECT    dest;
    DDBLTFX fx;
    HRESULT hr;

    dest.left   = g_screenRect.left;
    dest.top    = g_screenRect.top;
    dest.right  = g_screenRect.right;
    dest.bottom = g_screenRect.bottom;

    /* Only dwSize and the fill colour are written; the rest of the DDBLTFX is
     * left as it lies, as in the original, because DDBLT_COLORFILL is the only
     * flag and it reads only that one field. */
    fx.dwSize      = sizeof fx;
    fx.dwFillColor = colour;

    /* The primary is the entire desktop in windowed mode, so it is filled
     * through the screen rectangle. Everything else is ours entirely. */
    hr = IDirectDrawSurface_Blt(surf, (surf == g_primarySurface) ? &dest : NULL,
                                NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    return hr == DD_OK;
}

static_assert(DDBLTFAST_WAIT == 0x10, "DDBLTFAST_WAIT");
static_assert(PM_NOREMOVE == 0, "PM_NOREMOVE");

#define g_presentEnabled (*(const int32_t *)(uintptr_t)ADDR_PRESENT_ENABLED)
#define g_backBuffer     (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_FONT_SURFACE)
#define g_screenClip     ((LPRECT)(uintptr_t)ADDR_SCREEN_CLIP)
#define g_hWnd           (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_windowed       (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)

void __cdecl PresentFrame(void)
{
    MSG     msg;
    HRESULT hr;

    if (!g_presentEnabled)
        return;

    if (g_windowed) {
        /* No flipping chain in a window, so the back buffer is copied up to
         * the primary at wherever the client area happens to be. */
        IDirectDrawSurface_BltFast(g_primarySurface,
                                   (DWORD)g_screenRect.left,
                                   (DWORD)g_screenRect.top,
                                   g_backBuffer, g_screenClip, DDBLTFAST_WAIT);
        return;
    }

    hr = IDirectDrawSurface_Flip(g_primarySurface, NULL, 0);
    while (hr != DD_OK) {
        if (hr == DDERR_SURFACELOST) {
            IDirectDrawSurface_Restore(g_primarySurface);
            return;
        }

        /* Stay answerable while the previous flip finishes. Peeked without
         * removing, so this consumes nothing -- it is only here to notice a
         * WM_QUIT arriving mid-wait. */
        if (PeekMessageA(&msg, g_hWnd, 0, 0, PM_NOREMOVE) && !PumpMessage(&msg)) {
            PostQuitMessage(0);
            return;
        }

        if (hr != DDERR_WASSTILLDRAWING)
            return;
        hr = IDirectDrawSurface_Flip(g_primarySurface, NULL, 0);
    }
}

#define g_ddraw       (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)
#define g_clipper     (*(LPDIRECTDRAWCLIPPER *)(uintptr_t)ADDR_DD_CLIPPER)
#define g_offscreen   (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_SURFACE)
#define g_backBuffer2 (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_FONT_SURFACE)
#define g_palHolder   (*(uint8_t **)(uintptr_t)ADDR_MOVIE_PALETTE_OWNER)
#define MOVIE_PALETTE_OFF 0x800u

void __cdecl ShutdownDirectDraw(void)
{
    /* Nothing was ever created, so there is nothing to undo. */
    if (!g_ddraw)
        goto release_v2;

    if (g_clipper) {
        IDirectDrawClipper_Release(g_clipper);
        g_clipper = NULL;
    }
    if (g_offscreen) {
        IDirectDrawSurface_Release(g_offscreen);
        g_offscreen = NULL;
    }

    /* The palette is not a global of its own -- it hangs off the same holder
     * the movie player reads it from. */
    if (g_palHolder) {
        LPDIRECTDRAWPALETTE pal =
            *(LPDIRECTDRAWPALETTE *)(g_palHolder + MOVIE_PALETTE_OFF);

        if (pal) {
            IDirectDrawPalette_Release(pal);
            *(LPDIRECTDRAWPALETTE *)(g_palHolder + MOVIE_PALETTE_OFF) = NULL;
        }
        g_palHolder = NULL;
    }

    if (g_primarySurface) {
        IDirectDrawSurface_Release(g_primarySurface);
        g_primarySurface = NULL;
        /* Cleared rather than released: fullscreen it was attached to the
         * primary and never separately owned, and windowed it is the offscreen
         * surface already let go above. */
        g_backBuffer2 = NULL;
    }

    IDirectDraw_RestoreDisplayMode(g_ddraw);
    IDirectDraw_Release(g_ddraw);
    g_ddraw = NULL;

release_v2:
    /* Outside the guard, so the v2 interface is released even when the v1 was
     * already gone -- QueryInterface can have succeeded and everything after it
     * failed. */
    if (g_ddraw2) {
        IDirectDraw2_Release(g_ddraw2);
        g_ddraw2 = NULL;
    }
}

#define g_surface514e94 (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_SURFACE_514E94)
#define g_mapSurfaceDesc (*(uint8_t **)(uintptr_t)ADDR_MAP_SURFACE)
#define MAP_DESC_FLAG    0x08u
#define MAP_DESC_SURFACE 0x10u

typedef void (__cdecl *am2_void_fn)(void);
#define orig_on_map_restored (*(am2_void_fn)ADDR_ON_MAP_RESTORED)

/* IsLost answers DD_OK when the surface is fine, so a non-zero result is the
 * thing to act on. */
static void RestoreIfLost(LPDIRECTDRAWSURFACE surf)
{
    if (surf && IDirectDrawSurface_IsLost(surf) != DD_OK)
        IDirectDrawSurface_Restore(surf);
}

void __cdecl RestoreLostSurfaces(void)
{
    RestoreIfLost(g_primarySurface);
    RestoreIfLost(g_offscreen);
    RestoreIfLost(g_surface514e94);

    /* The map keeps its surface in a descriptor, and only wants it restored
     * while the flag at +8 is clear. */
    if (g_mapSurfaceDesc &&
        *(int32_t *)(g_mapSurfaceDesc + MAP_DESC_FLAG) == 0) {
        LPDIRECTDRAWSURFACE surf =
            *(LPDIRECTDRAWSURFACE *)(g_mapSurfaceDesc + MAP_DESC_SURFACE);

        if (surf && IDirectDrawSurface_IsLost(surf) != DD_OK) {
            /* Reload through the descriptor rather than the local: the restore
             * is what the original re-reads it for. */
            surf = *(LPDIRECTDRAWSURFACE *)(g_mapSurfaceDesc + MAP_DESC_SURFACE);
            if (IDirectDrawSurface_Restore(surf) == DD_OK)
                orig_on_map_restored();   /* the pixels are gone; redraw them */
        }
    }
}

int surface_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_LOCK_SURFACE, (const void *)LockSurface, "LockSurface", 1);
    rc |= patch_replace(ADDR_UNLOCK_SURFACE, (const void *)UnlockSurface, "UnlockSurface", 0);
    rc |= patch_replace(ADDR_CREATE_OFFSCREEN, (const void *)CreateOffscreenSurface,
                        "CreateOffscreenSurface", 4);
    rc |= patch_replace(ADDR_CLEAR_SURFACE, (const void *)ClearSurface,
                        "ClearSurface", 2);
    rc |= patch_replace(ADDR_PRESENT_FRAME, (const void *)PresentFrame,
                        "PresentFrame", 0);
    rc |= patch_replace(ADDR_SHUTDOWN_DDRAW, (const void *)ShutdownDirectDraw,
                        "ShutdownDirectDraw", 0);
    rc |= patch_replace(ADDR_RESTORE_LOST, (const void *)RestoreLostSurfaces,
                        "RestoreLostSurfaces", 0);
    return rc;
}
