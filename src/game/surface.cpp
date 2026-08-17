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

typedef int32_t (__cdecl *am2_report_error_fn)(HRESULT hr, const char *fmt, ...);
#define orig_report_error (*(am2_report_error_fn)ADDR_REPORT_ERROR)

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
        orig_report_error(hr, "GetSurfaceDesc()");
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
            orig_report_error(hr, "CreateSurface()");
            return NULL;
        }
        orig_log("Failed to put surface into video memory!\n");
        ddsd.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
        hr = IDirectDraw2_CreateSurface(g_ddraw2, &ddsd, &surf, NULL);
        if (hr) {
            orig_report_error(hr, "CreateSurface()");
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

int surface_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_LOCK_SURFACE, (const void *)LockSurface, "LockSurface", 1);
    rc |= patch_replace(ADDR_UNLOCK_SURFACE, (const void *)UnlockSurface, "UnlockSurface", 0);
    rc |= patch_replace(ADDR_CREATE_OFFSCREEN, (const void *)CreateOffscreenSurface,
                        "CreateOffscreenSurface", 4);
    rc |= patch_replace(ADDR_CLEAR_SURFACE, (const void *)ClearSurface,
                        "ClearSurface", 2);
    return rc;
}
