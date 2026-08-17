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
#include "sprite.h"
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

#define g_mapCache      (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)
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
    RestoreIfLost(g_mapCache);

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

typedef void (__cdecl *am2_gate_fn)(int32_t);
#define orig_refresh_gate (*(am2_gate_fn)ADDR_REFRESH_GATE)
#define orig_refresh_draw (*(am2_void_fn)ADDR_REFRESH_DRAW)
#define g_presentEnabledRW (*(int32_t *)(uintptr_t)ADDR_PRESENT_ENABLED)

void __cdecl RefreshScreen(void)
{
    /* Saved rather than assumed: presenting may already have been off. */
    const int32_t wasEnabled = g_presentEnabledRW;

    orig_refresh_gate(0);
    g_presentEnabledRW = 0;

    /* Twice, because the scene is double buffered and one pass would leave the
     * other buffer holding whatever was there before. */
    orig_refresh_draw();
    orig_refresh_draw();

    IDirectDrawSurface_BltFast(g_primarySurface,
                               (DWORD)g_screenRect.left,
                               (DWORD)g_screenRect.top,
                               g_backBuffer, g_screenClip, DDBLTFAST_WAIT);

    orig_refresh_gate(1);
    g_presentEnabledRW = wasEnabled;
}

void __cdecl ReleasePalette(void *holder)
{
    LPDIRECTDRAWPALETTE *slot =
        (LPDIRECTDRAWPALETTE *)((uint8_t *)holder + PALETTE_HOLDER_OFF);

    if (!*slot)
        return;
    IDirectDrawPalette_Release(*slot);
    *slot = NULL;
}

void __cdecl SetPaletteRange(PALETTEENTRY *entries, uint32_t first,
                             uint32_t last)
{
    LPDIRECTDRAWPALETTE pal;

    /* Windowed, the desktop owns the palette; rewriting it would recolour
     * every other window on the screen. */
    if (g_windowed)
        return;
    if (!g_palHolder)
        return;

    pal = *(LPDIRECTDRAWPALETTE *)(g_palHolder + PALETTE_HOLDER_OFF);
    if (!pal)
        return;

    /* Inclusive range, hence the +1, and the entries are passed from `first`
     * rather than from the start of the array. */
    IDirectDrawPalette_SetEntries(pal, 0, first, last - first + 1,
                                  &entries[first]);
}

void __cdecl SetSurfaceColorKey(LPDIRECTDRAWSURFACE surf, uint8_t key)
{
    DDCOLORKEY ck;

    /* One index, so both ends of the range are the same -- as in
     * CreateOffscreenSurface, which sets its own key the same way. */
    ck.dwColorSpaceLowValue  = key;
    ck.dwColorSpaceHighValue = key;
    IDirectDrawSurface_SetColorKey(surf, DDCKEY_SRCBLT, &ck);
}

/* STANDING NOTE -- the two paths after the Lock return the surface they have
 * just Released, so the caller is handed a dangling interface pointer and told
 * it succeeded. As in ReloadBitmapSurface neither Unlocks first, and both blame
 * the Lock because the copy-failed path jumps into the Lock handler rather than
 * carrying its own message. Reproduced as the original has it.
 *
 * The descriptor-failure path in between leaks instead: it returns NULL without
 * releasing the surface it created a moment earlier. Also reproduced. */
LPDIRECTDRAWSURFACE __cdecl CreateBitmapSurface(am2_FILE *fp, uint32_t nbytes,
                                                int32_t width, int32_t height,
                                                const uint8_t *remap, uint32_t flags)
{
    DDSURFACEDESC        ddsd;
    LPDIRECTDRAWSURFACE  surf;
    uint8_t             *pixels;

    pixels = (uint8_t *)orig_malloc(nbytes);
    if (orig_fread(pixels, nbytes, 1, fp) != 1) {
        orig_log("Error sprite from stream in CreateBitmapSurface().\n");
        orig_free(pixels);
        return NULL;
    }

    /* No caps and no colour key yet -- the key is not known until the copy has
     * run, and is applied at the end from the same slot the count came in on. */
    surf = CreateOffscreenSurface(width, height, 0, -1);
    if (!surf) {
        orig_log("Unable to create directdraw surface in CreateBitmapSurface()");
        orig_free(pixels);
        return NULL;
    }

    memset(&ddsd, 0, sizeof ddsd);
    ddsd.dwSize = sizeof ddsd;
    if (IDirectDrawSurface_GetSurfaceDesc(surf, &ddsd) != DD_OK) {
        orig_log("Unable to get surface desc in CreateBitmapSurface()\n");
        orig_free(pixels);
        return NULL;
    }

    if (IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL) != DD_OK) {
        IDirectDrawSurface_Release(surf);
        orig_log("Error on Lock in CreateBitmapSurface()");
        orig_free(pixels);
        return surf;
    }

    if (!orig_blit_bitmap_in(ddsd.lpSurface, ddsd.lPitch, pixels,
                             width, height, remap, &nbytes)) {
        IDirectDrawSurface_Release(surf);
        orig_log("Error on Lock in CreateBitmapSurface()");
        orig_free(pixels);
        return surf;
    }

    IDirectDrawSurface_Unlock(surf, ddsd.lpSurface);
    if (!(flags & 1))
        SetSurfaceColorKey(surf, (uint8_t)nbytes);
    orig_free(pixels);
    return surf;
}

/* STANDING NOTE -- both failure paths after the Lock return 1, which is the
 * value the caller reads as success. The surface is Released, the buffer freed
 * and an error logged, and then the function claims it worked. Worse, neither
 * path Unlocks first, so a surface that was locked successfully is Released
 * while still locked. That is what the original does and it is reproduced here.
 *
 * The two paths also share one message. A copy that fails reports "Error on
 * Lock", because the original jumps into the middle of the Lock handler rather
 * than writing a second string -- so do not trust that message to mean the Lock
 * is what failed.
 *
 * Reaching any of this needs a surface that is lost or a malformed sprite in a
 * stream, so none of it is exercised by an ordinary run. */
int32_t __cdecl ReloadBitmapSurface(LPDIRECTDRAWSURFACE surf, am2_FILE *fp,
                                    uint32_t nbytes, int32_t width, int32_t height,
                                    const uint8_t *remap, uint32_t flags)
{
    DDSURFACEDESC ddsd;
    uint8_t      *pixels;

    /* The game's heap: the buffer is handed to the game's own fread and freed
     * by the game's own free on every path out of here. */
    pixels = (uint8_t *)orig_malloc(nbytes);
    if (orig_fread(pixels, nbytes, 1, fp) != 1) {
        orig_log("Error sprite from stream in ReloadBitmapSurface().\n");
        orig_free(pixels);
        return 0;
    }

    memset(&ddsd, 0, sizeof ddsd);
    ddsd.dwSize = sizeof ddsd;
    if (IDirectDrawSurface_GetSurfaceDesc(surf, &ddsd) != DD_OK) {
        orig_log("Unable to get surface desc in ReloadBitmapSurface()\n");
        orig_free(pixels);
        return 0;
    }

    if (IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL) != DD_OK) {
        IDirectDrawSurface_Release(surf);
        orig_log("Error on Lock in ReloadBitmapSurface()");
        orig_free(pixels);
        return 1;
    }

    /* `nbytes` goes in as the byte count and is read back afterwards as the
     * transparent index, so it has to be passed by address and re-read -- see
     * ADDR_BLIT_BITMAP_IN. Caching it across this call would silently key the
     * surface on the wrong colour. */
    if (!orig_blit_bitmap_in(ddsd.lpSurface, ddsd.lPitch, pixels,
                             width, height, remap, &nbytes)) {
        IDirectDrawSurface_Release(surf);
        orig_log("Error on Lock in ReloadBitmapSurface()");
        orig_free(pixels);
        return 1;
    }

    IDirectDrawSurface_Unlock(surf, ddsd.lpSurface);
    if (!(flags & 1))
        SetSurfaceColorKey(surf, (uint8_t)nbytes);
    orig_free(pixels);
    return 1;
}

void __cdecl ClearRegion(const RECT *r, uint8_t colour)
{
    RECT    dest;
    DDBLTFX fx;

    /* A locked surface cannot be Blt'd to. Silently nothing, as the original. */
    if (g_surfaceLocked)
        return;
    if (!IntersectRect(&dest, r, g_screenClip))
        return;

    if (g_lockedSurface == g_primarySurface) {
        /* Game coordinates are relative to the client area; the primary is the
         * whole desktop. Shift, or the fill lands somewhere else entirely. */
        dest.left   += g_screenRect.left;
        dest.top    += g_screenRect.top;
        dest.right  += g_screenRect.left;
        dest.bottom += g_screenRect.top;
    }

    fx.dwSize      = sizeof fx;
    fx.dwFillColor = colour;
    IDirectDrawSurface_Blt(g_lockedSurface, &dest, NULL, NULL,
                           DDBLT_COLORFILL | DDBLT_WAIT, &fx);
}

/* One little vertical meter, drawn straight onto the back buffer.
 *
 * 0x004624A0. Three pixels wide, sixteen tall, and three colour fills: the
 * whole column in the background colour, the filled part of it in the caller's
 * colour, and a single-pixel row somewhere along it as a marker.
 *
 * The filled height is `(value - base) * 16 / 60`, capped at the bar's own
 * sixteen. The divisor is not obvious from the code -- the original divides by
 * multiplying by 0x88888889 and shifting -- so it was worked out by running
 * that sequence over sample values rather than read off: 60, 120 and 300 give
 * 1, 2 and 5.
 *
 * The marker sits at `base % 16` above the bottom, and is drawn in the
 * caller's colour when it is above the filled part and in the background colour
 * when it is not, which is what makes it read as a mark rather than a gap.
 *
 * A failed Blt is logged and otherwise ignored, three separate times with the
 * same message. */
static_assert(sizeof(DDBLTFX) == 0x64, "DDBLTFX");
static_assert((DDBLT_COLORFILL | DDBLT_WAIT) == 0x01000400, "colour fill");

#define g_backBufferSurf (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_FONT_SURFACE)
#define g_seqBarBg       (*(const uint8_t *)(uintptr_t)ADDR_SEQ_BAR_BG)

#define SEQ_BAR_WIDTH  3
#define SEQ_BAR_HEIGHT 16
#define SEQ_BAR_SCALE  60

/* Every fill is the same shape: a rectangle and a colour. */
static void SeqBarFill(int32_t left, int32_t top, int32_t right, int32_t bottom,
                       uint32_t colour)
{
    RECT    dest;
    DDBLTFX fx;

    dest.left   = left;
    dest.top    = top;
    dest.right  = right;
    dest.bottom = bottom;

    /* Only dwSize and the colour are set, as elsewhere in this file:
     * DDBLT_COLORFILL reads nothing else. */
    fx.dwSize      = sizeof fx;
    fx.dwFillColor = colour;

    if (IDirectDrawSurface_Blt(g_backBufferSurf, &dest, NULL, NULL,
                               DDBLT_COLORFILL | DDBLT_WAIT, &fx) != DD_OK)
        orig_log((const char *)(uintptr_t)ADDR_STR_SEQ_BLT_FAIL);
}

void __cdecl DrawSeqBar(int32_t x, int32_t bottom, uint32_t colour,
                        int32_t value, int32_t base)
{
    int32_t filled = ((value - base) * SEQ_BAR_HEIGHT) / SEQ_BAR_SCALE;
    int32_t mark;

    if (filled > SEQ_BAR_HEIGHT)
        filled = SEQ_BAR_HEIGHT;

    /* The empty bar. */
    SeqBarFill(x, bottom - (SEQ_BAR_HEIGHT - 1), x + SEQ_BAR_WIDTH, bottom + 1,
               g_seqBarBg);
    /* However much of it is full. */
    SeqBarFill(x, bottom - filled, x + SEQ_BAR_WIDTH, bottom + 1,
               colour & 0xFF);

    /* And the marker, which disappears into the fill once the fill reaches it. */
    mark = base % SEQ_BAR_HEIGHT;
    SeqBarFill(x, bottom - mark, x + SEQ_BAR_WIDTH, bottom - mark + 1,
               (mark > filled) ? (colour & 0xFF) : g_seqBarBg);
}

/* Let the map go -- 0x0042D390.
 *
 * Two objects, released two different ways, which is the whole content: the
 * map is an AM2_Sprite and goes through ReleaseSprite, while the cache surface
 * it was painted into is a raw IDirectDrawSurface and takes a plain Release.
 *
 * The sprite pointer lives at ADDR_MAP_SURFACE, whose comment in orig.h --
 * "desc; +8 a flag, +0x10 the surface" -- was describing AM2_Sprite's layout
 * without knowing it: +0x10 is `image`, exactly where ReleaseSprite looks. */
void __cdecl FreeMapSurfaces(void)
{
    ReleaseSprite(*(AM2_Sprite **)(uintptr_t)ADDR_MAP_SURFACE);
    *(AM2_Sprite **)(uintptr_t)ADDR_MAP_SURFACE = NULL;

    if (g_mapCache) {
        IDirectDrawSurface_Release(g_mapCache);
        g_mapCache = NULL;
    }
}

/* Put a picture in the middle of the screen -- 0x00445500, thiscall.
 *
 * All of it is arithmetic around one Blt: the source is centred in the screen
 * clip rectangle, `(right - left - width) / 2` on each axis, and blitted whole.
 *
 * The primary is special in the way it always is here. When the destination is
 * the primary the client origin is added, because the primary is the whole
 * desktop in a window and a rectangle in game coordinates would otherwise land
 * somewhere else -- the same correction ClearSurface and ClearRegion make.
 *
 * DDBLT_ASYNC alongside DDBLT_WAIT is the original's. The two are less
 * contradictory than they look -- WAIT governs what to do about a busy
 * blitter, ASYNC asks to be queued -- and both are passed as written.
 *
 * FIDELITY NOTE: immediately before the call the original fetches the current
 * movie, reads a field of it and compares it against zero, and never uses the
 * answer. It is genuinely there and genuinely discarded, like the IsIconic in
 * WndProc. There is nothing to write here that would have the same effect, so
 * it is recorded rather than reproduced. */
static_assert((DDBLT_WAIT | DDBLT_ASYNC) == 0x01000200, "the blit flags");

#define g_originDx (*(const int32_t *)(uintptr_t)ADDR_ORIGIN_DX)
#define g_originDy (*(const int32_t *)(uintptr_t)ADDR_ORIGIN_DY)

void __attribute__((thiscall)) BlitCentred(void *self, LPDIRECTDRAWSURFACE dest)
{
    const int32_t *desc =
        *(const int32_t **)((uint8_t *)self + BLIT_SRC_OFF_DESC);
    LPDIRECTDRAWSURFACE src =
        *(LPDIRECTDRAWSURFACE *)((uint8_t *)self + BLIT_SRC_OFF_SURFACE);
    int32_t width  = desc[1];
    int32_t height = desc[2];
    RECT    to, from;

    to.left = (g_screenClip->right - g_screenClip->left - width) / 2;
    to.top  = (g_screenClip->bottom - g_screenClip->top - height) / 2;
    if (dest == g_primarySurface) {
        to.left += g_originDx;
        to.top  += g_originDy;
    }
    to.right  = to.left + width;
    to.bottom = to.top + height;

    from.left   = 0;
    from.top    = 0;
    from.right  = width;
    from.bottom = height;

    IDirectDrawSurface_Blt(dest, &to, src, &from,
                           DDBLT_WAIT | DDBLT_ASYNC, NULL);
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
    rc |= patch_replace(ADDR_REFRESH_SCREEN, (const void *)RefreshScreen,
                        "RefreshScreen", 0);
    rc |= patch_replace(ADDR_CLEAR_REGION, (const void *)ClearRegion,
                        "ClearRegion", 2);
    rc |= patch_replace(ADDR_RELEASE_PALETTE, (const void *)ReleasePalette,
                        "ReleasePalette", 1);
    rc |= patch_replace(ADDR_SET_PALETTE_RANGE, (const void *)SetPaletteRange,
                        "SetPaletteRange", 3);
    rc |= patch_replace(ADDR_SET_SURF_COLORKEY, (const void *)SetSurfaceColorKey,
                        "SetSurfaceColorKey", 2);
    rc |= patch_replace(ADDR_CREATE_BITMAP, (const void *)CreateBitmapSurface,
                        "CreateBitmapSurface", 6);
    rc |= patch_replace(ADDR_RELOAD_BITMAP, (const void *)ReloadBitmapSurface,
                        "ReloadBitmapSurface", 7);
    rc |= patch_replace(ADDR_DRAW_SEQ_BAR, (const void *)DrawSeqBar,
                        "DrawSeqBar", 5);
    rc |= patch_replace(ADDR_BLIT_CENTRED, (const void *)BlitCentred,
                        "BlitCentred", 1);
    rc |= patch_replace(ADDR_FREE_MAP_SURFACES, (const void *)FreeMapSurfaces,
                        "FreeMapSurfaces", 0);
    return rc;
}
