/* DirectDraw surface lock/unlock -- reconstructed from ArmyMen2.exe.
 *
 *   LockSurface    0x0041B9A0   38 call sites
 *   UnlockSurface  0x0041BA40   34 call sites
 *
 * See surface.h. All DirectDraw declarations come from ddraw.h rather than
 * being restated here; the values the game hardcodes all match it exactly:
 * dwSize 0x6C == sizeof(DDSURFACEDESC), flags 1 == DDLOCK_WAIT, and the error
 * it compares against, 0x887601C2, == DDERR_SURFACELOST.
 */

#include "surface.h"
#include "../inject/patch.h"

/* DirectDraw's own declarations. CINTERFACE selects the C view of the COM
 * interfaces and COBJMACROS gives the IDirectDrawSurface_* call wrappers, so
 * nothing about DDSURFACEDESC or the vtable layout is restated anywhere here. */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

#include <stdint.h>

/* Writable views of the globals LockSurface publishes. blit.c takes the same
 * two read-only, which is why it declares its own const versions. */
#define g_surfaceLocked  (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_lockedSurface  (*(struct IDirectDrawSurface **)(uintptr_t)ADDR_LOCKED_SURFACE)
#define g_primarySurface (*(struct IDirectDrawSurface **)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_frameBuf       (*(void **)(uintptr_t)ADDR_FRAMEBUFFER)
#define g_pitch          (*(int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)

/* The game hardcodes dwSize = 0x6C, which is DDSURFACEDESC and not
 * DDSURFACEDESC2 (124 bytes). Check the SDK agrees rather than assume it. */
typedef char am2_ddsd_is_the_v1_struct[(sizeof(DDSURFACEDESC) == 0x6C) ? 1 : -1];

int32_t __cdecl LockSurface(struct IDirectDrawSurface *surf)
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
        struct IDirectDrawSurface *surf = g_lockedSurface;

        /* Unlock takes back the same pointer Lock handed out. */
        IDirectDrawSurface_Unlock(surf, g_frameBuf);

        g_surfaceLocked = 0;
        g_frameBuf      = NULL;
        g_pitch         = 0;
    }
    return 1;
}

int surface_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_LOCK_SURFACE, LockSurface, "LockSurface", 1);
    rc |= patch_replace(ADDR_UNLOCK_SURFACE, UnlockSurface, "UnlockSurface", 0);
    return rc;
}
