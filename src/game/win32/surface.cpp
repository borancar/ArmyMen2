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
#include "mapdraw.h"   /* SetDrawTarget */
#include "../rect.h"
#include "winmain.h"
#include "report.h"
#include "palette.h"
#include "../misc.h"
#include "../../inject/patch.h"

#include <stdint.h>

/* Writable views of the globals LockSurface publishes. blit.c takes the same
 * two read-only, which is why it declares its own const versions. */
#define g_surfaceLocked  (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_drawTarget  (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET)
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
        if (surf == g_drawTarget)
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

    g_drawTarget = surf;
    g_surfaceLocked = 1;
    g_frameBuf      = desc.lpSurface;
    g_pitch         = desc.lPitch;
    return 1;
}

int32_t __cdecl UnlockSurface(void)
{
    if (g_surfaceLocked) {
        LPDIRECTDRAWSURFACE surf = g_drawTarget;

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
#define g_backBuffer     (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER)
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
#define g_offscreen   (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_OFFSCREEN_SURFACE)
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
        g_backBuffer = NULL;
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
                RestoreTileSet();   /* the pixels are gone; redraw them */
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

    if (g_drawTarget == g_primarySurface) {
        /* Game coordinates are relative to the client area; the primary is the
         * whole desktop. Shift, or the fill lands somewhere else entirely. */
        dest.left   += g_screenRect.left;
        dest.top    += g_screenRect.top;
        dest.right  += g_screenRect.left;
        dest.bottom += g_screenRect.top;
    }

    fx.dwSize      = sizeof fx;
    fx.dwFillColor = colour;
    IDirectDrawSurface_Blt(g_drawTarget, &dest, NULL, NULL,
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

    if (IDirectDrawSurface_Blt(g_backBuffer, &dest, NULL, NULL,
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

/* Turn a loaded bitmap into something drawable -- 0x0041BE80.
 *
 * The third of the family, after CreateBitmapSurface and ReloadBitmapSurface,
 * and the one that chooses between them: given a bitmap and its palette it
 * either encodes it for the software blitters or puts it in a DirectDraw
 * surface, and records which in the destination's flags.
 *
 * A palette remap comes first unless the caller supplies one. Every source
 * entry is turned into a colour and matched against the active palette, and
 * the search starts at ten rather than zero when the RESERVE10 flag is clear --
 * which is how the first ten entries are kept for something else. With no
 * active palette the table is the identity.
 *
 * The size test picks the encoder: 60,000 pixels or more goes to one routine
 * and less to another, and the flag written afterwards -- 4, 8 or 0x10 -- is
 * how everything downstream knows which shape the result is in.
 *
 * The DirectDraw path is CreateOffscreenSurface's dance again. Video memory is
 * asked for unless the record already says system memory, and it is asked
 * *politely*: GetAvailableVidMem is consulted first and the request downgraded
 * if the bitmap would not fit, which is a better citizen than simply failing
 * and retrying. It still retries once if the create fails anyway.
 *
 * MATCHED ARGUMENT, worth recording. The palette matcher's third argument is
 * read from a slot whose low byte alone is ever written, so its top three bytes
 * are whatever the stack held. That is not a defect: 0x0041B7C0 does `and ebx,
 * 0xff` before using it. Passing the byte is faithful, and it was worth
 * checking rather than reproducing an apparent bug that was not one.
 *
 * Exercised by every sprite the game loads, so the Boot Camp pixel budget is
 * the check that matters here. */
static_assert((DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH) == 7, "the descriptor flags");
static_assert(DDSCAPS_OFFSCREENPLAIN == 0x40, "DDSCAPS_OFFSCREENPLAIN");
static_assert(DDSCAPS_SYSTEMMEMORY == 0x800, "DDSCAPS_SYSTEMMEMORY");

typedef int32_t (__cdecl *am2_encode_fn)(const void *pixels, void *dest,
                                         int32_t w, int32_t h,
                                         const uint8_t *remap);
#define orig_encode_big   (*(am2_encode_fn)ADDR_ENCODE_BIG)
#define orig_encode_small (*(am2_encode_fn)ADDR_ENCODE_SMALL)
#define g_activePalette   (*(const uint32_t **)(uintptr_t)ADDR_ACTIVE_PALETTE)
#define g_ddraw2obj       (*(LPDIRECTDRAW2 *)(uintptr_t)ADDR_DIRECTDRAW2)

int32_t __cdecl MakeBitmap(const uint32_t *src, const void *pixels,
                           uint8_t *dest, const uint8_t *remap)
{
    uint8_t   table[256];
    uint32_t  flags   = *(uint32_t *)(dest + BMP_OFF_FLAGS);
    int32_t   reserve = (flags & BMP_FLAG_RESERVE10) == 0;
    int32_t   w, h, i;

    /* Copy the geometry across first; the original does this before anything
     * can fail, so the record is consistent even on the error paths. */
    *(uint32_t *)(dest + BMP_OFF_WIDTH)  = src[1];
    *(uint32_t *)(dest + BMP_OFF_HEIGHT) = src[2];
    *(uint32_t *)(dest + 0x0C)           = src[6];
    *(uint32_t *)(dest + 0x10)           = src[7];

    if (!remap) {
        int32_t from = 0;

        if (reserve) {
            for (i = 0; i < BMP_RESERVED_ENTRIES; i++)
                table[i] = (uint8_t)i;
            from = BMP_RESERVED_ENTRIES;
        }
        if (g_activePalette) {
            for (i = from; i < 256; i++)
                table[i] = NearestPalIndex(g_activePalette,
                                             SwapColourBytes(src[10 + i], 0),
                                             (uint32_t)from);
        } else {
            for (i = from; i < 256; i++)
                table[i] = (uint8_t)i;
        }
        remap = table;
    }

    w = (int32_t)*(uint32_t *)(dest + BMP_OFF_WIDTH);
    h = (int32_t)*(uint32_t *)(dest + BMP_OFF_HEIGHT);

    if (flags & BMP_FLAG_SOFTWARE) {
        int32_t result;

        flags &= ~BMP_FLAG_SOFTWARE;
        *(uint32_t *)(dest + BMP_OFF_FLAGS) = flags;
        dest[BMP_OFF_KEY] = 0;

        if (w * h >= BMP_SOFTWARE_LIMIT) {
            result = orig_encode_big(pixels, dest, w, h, remap);
            *(uint32_t *)(dest + BMP_OFF_FLAGS) |= 4;
        } else {
            result = orig_encode_small(pixels, dest, w, h, remap);
            *(uint32_t *)(dest + BMP_OFF_FLAGS) |= reserve ? 8 : 0x10;
        }
        return result;
    }

    {
        DDSURFACEDESC       ddsd;
        DDSCAPS             want;
        DWORD               total = 0, freeVid = 0;
        LPDIRECTDRAWSURFACE surf = NULL;
        int32_t             copied;

        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize   = sizeof ddsd;
        ddsd.dwFlags  = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
        ddsd.dwWidth  = (DWORD)w;
        ddsd.dwHeight = (DWORD)h;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        want.dwCaps = DDSCAPS_OFFSCREENPLAIN;

        /* Ask before taking: if the card has not got room, do not make it say
         * so by failing. */
        if (!(*(uint32_t *)(dest + BMP_OFF_FLAGS) & BMP_FLAG_SYSMEM)) {
            IDirectDraw2_GetAvailableVidMem(g_ddraw2obj, &want, &total, &freeVid);
            if (freeVid < (DWORD)(w * h))
                *(uint32_t *)(dest + BMP_OFF_FLAGS) |= BMP_FLAG_SYSMEM;
        }
        if (*(uint32_t *)(dest + BMP_OFF_FLAGS) & BMP_FLAG_SYSMEM)
            ddsd.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;

        if (IDirectDraw2_CreateSurface(g_ddraw2obj, &ddsd, &surf, NULL) != DD_OK) {
            if (*(uint32_t *)(dest + BMP_OFF_FLAGS) & BMP_FLAG_SYSMEM) {
                orig_log((const char *)(uintptr_t)ADDR_STR_BMP_NO_SURF);
                return 0;
            }
            /* Only worth saying when video memory was what we asked for. */
            orig_log((const char *)(uintptr_t)ADDR_STR_BMP_NO_VIDMEM);
            *(uint32_t *)(dest + BMP_OFF_FLAGS) |= BMP_FLAG_SYSMEM;
            ddsd.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
            if (IDirectDraw2_CreateSurface(g_ddraw2obj, &ddsd, &surf, NULL) != DD_OK) {
                orig_log((const char *)(uintptr_t)ADDR_STR_BMP_NO_SURF);
                return 0;
            }
        }

        IDirectDrawSurface_Restore(surf);
        if (IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL) != DD_OK) {
            IDirectDrawSurface_Release(surf);
            orig_log((const char *)(uintptr_t)ADDR_STR_BMP_NO_LOCK);
            return 0;
        }

        copied = orig_blit_bitmap_in(ddsd.lpSurface, ddsd.lPitch, pixels, w, h,
                                     remap, (uint32_t *)(dest + BMP_OFF_KEY));
        if (!copied) {
            IDirectDrawSurface_Release(surf);
            orig_log((const char *)(uintptr_t)ADDR_STR_BMP_NO_LOCK);
            return 0;
        }

        IDirectDrawSurface_Unlock(surf, ddsd.lpSurface);
        *(LPDIRECTDRAWSURFACE *)(dest + BMP_OFF_SURFACE) = surf;

        if (!(*(uint32_t *)(dest + BMP_OFF_FLAGS) & BMP_FLAG_NO_COLORKEY))
            SetSurfaceColorKey(surf, dest[BMP_OFF_KEY]);
        return copied;
    }
}

/* Show or dismiss the menu overlay -- 0x00425AF0.
 *
 * Two jobs in one function, chosen by whether a menu request is pending.
 *
 * With one pending it is a teardown: the paint object is deleted, the mode
 * goes back to 0x21, and presenting is switched back on -- the game resumes.
 *
 * Without one it draws. Presenting is switched off first so the ordinary frame
 * cannot appear underneath, the primary is saved into the back buffer the
 * first time through, the overlay is drawn, and the result is blitted back to
 * the primary at the client origin.
 *
 * The paint object is reached through two ordinary C++ virtuals rather than
 * COM -- `this` in ecx, nothing pushed -- so they do not appear in the DirectX
 * survey at all. Slot 1 takes the object's own rectangle BY VALUE, which is
 * where the original does something worth knowing about: it leaves
 * SetDrawTarget's pushed argument on the stack, subtracts only twelve more
 * bytes, and writes sixteen. The earlier push is the first of the four dwords.
 * Written normally here -- GCC balances its own calls -- because the effect is
 * identical and the trick is not.
 *
 * The saved-primary blit uses the screen RECT and the restore uses the clip
 * RECT; they are different rectangles and swapping them would be invisible in
 * fullscreen and wrong in a window. */
typedef void (__attribute__((thiscall)) *am2_paint_slot1_fn)(void *self, RECT area);
typedef void (__attribute__((thiscall)) *am2_paint_slot2_fn)(void *self);
typedef void (__cdecl *am2_overlay_prep_fn)(int32_t a, int32_t b);
typedef void (__cdecl *am2_overlay_draw_fn)(void);
typedef void (__attribute__((thiscall)) *am2_delete_fn)(void *self, int32_t flag);

#define g_paintObj      (*(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT)
#define g_menuMode      (*(int32_t *)(uintptr_t)ADDR_MENU_MODE)
#define g_menuPending   (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET)
#define g_overlayDirty  (*(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY)
#define g_presenting    (*(int32_t *)(uintptr_t)ADDR_PRESENT_ENABLED)
#define g_screenRectPtr ((LPRECT)(uintptr_t)ADDR_SCREEN_RECT)
#define orig_overlay_prepare (*(am2_overlay_prep_fn)ADDR_OVERLAY_PREPARE)

void __cdecl DrawMenuOverlay(void)
{
    uint8_t *paint = g_paintObj;

    if (g_menuPending) {
        /* Going back to the game: drop the overlay and let frames through. */
        if (paint) {
            void **vt = *(void ***)paint;
            (*(am2_delete_fn)vt[0])(paint, 1);
            g_paintObj = NULL;
        }
        g_menuMode     = MENU_MODE_PLAYING;
        g_menuPending  = 0;
        g_overlayDirty = 0;
        g_presenting   = 1;
        return;
    }

    if (g_overlayDirty) {
        g_presenting = 0;
        if (paint) {
            void **vt = *(void ***)paint;

            SetDrawTarget(g_primarySurface);
            (*(am2_paint_slot1_fn)vt[1])(paint, *(const RECT *)(paint + 0x14));
        }
        /* Keep what was on screen, so the overlay can be drawn over a copy. */
        IDirectDrawSurface_BltFast(g_backBuffer, 0, 0, g_primarySurface,
                                   g_screenRectPtr, DDBLTFAST_WAIT);
        g_overlayDirty = 0;
    }

    SetDrawTarget(g_backBuffer);
    if (paint) {
        void **vt = *(void ***)paint;
        (*(am2_paint_slot2_fn)vt[2])(paint);
    }
    SetDrawTarget(g_backBuffer);

    orig_overlay_prepare(0, 1);
    DrawMenuCursor();

    IDirectDrawSurface_BltFast(g_primarySurface, (DWORD)g_originDx,
                               (DWORD)g_originDy, g_backBuffer,
                               g_screenClip, DDBLTFAST_WAIT);
}

/* The animated menu cursor -- 0x00412FE0.
 *
 * THE LAST FUNCTION IN THE IMAGE WITH ANY COM DISPATCH. 204 of the 207 stdcall
 * COM sites in game code were already inside reconstructed functions; all three
 * that were not are here.
 *
 * It is menu logic and it would have been a fair decline -- an animated pointer
 * with a save-under is not a channel to the outside world. It is reconstructed
 * because it was the only thing standing between the port and "every COM
 * dispatch site the survey can see is ours", and because almost everything it
 * calls is already ours: DrawSprite four times, SetDrawTarget twice,
 * LockSurface, UnlockSurface and RectSet. What is left is three Blts and the
 * bookkeeping between them.
 *
 * THREE PATHS, and which one runs depends on where the menu is:
 *
 *   Save-under. With a paint object and presenting disabled, the frame beneath
 *   the cursor is copied into a corner of the menu surface before the cursor is
 *   drawn over it, and put back on the next tick. That is what the three Blts
 *   do -- restore, save, restore-previous -- and why there are two rectangles
 *   rather than one.
 *
 *   Direct, for rows at or above 0x13. The surface is locked and the cursor and
 *   its two optional overlays drawn straight into it, with no save-under at all.
 *
 *   Bare, for lower rows without a paint object: just the sprite.
 *
 * THE DESTINATION IS OFFSET AND THE SOURCE IS NOT, in the last Blt, and only
 * when the target is the primary. The primary is the whole desktop in a window,
 * so a rectangle in game coordinates lands in the wrong place on it; the back
 * buffer it reads from has no such offset. The original keeps two copies of the
 * rectangle for exactly this and it is the sort of asymmetry that looks like a
 * bug until the windowed case is considered.
 *
 * The animation is a ten-frame cycle on a 200 ms timer. A missing sprite ends
 * the cycle if it is the second frame -- meaning this row has no animation at
 * all -- and otherwise wraps back to the first.
 *
 * Verified where it is sharpest: this draws the title-screen cursor, and the
 * windowed A/B renders the title screen with a budget of ZERO differing pixels.
 */
static_assert(DDBLT_WAIT == 0x01000000, "DDBLT_WAIT");

#define g_menuEnabled   (*(int32_t *)(uintptr_t)ADDR_MENU_ENABLED)
#define g_menuRow       (*(const int32_t *)(uintptr_t)ADDR_MENU_ROW)
#define g_animFrame     (*(int32_t *)(uintptr_t)ADDR_MENU_ANIM_FRAME)
#define g_animNext      (*(uint32_t *)(uintptr_t)ADDR_MENU_ANIM_NEXT)
#define g_menuSprites2  ((uint8_t **)(uintptr_t)ADDR_MENU_SPRITES)
#define g_cursorSprite  (*(uint8_t **)(uintptr_t)ADDR_MENU_SPRITES_END)
#define g_savedValid    (*(int32_t *)(uintptr_t)ADDR_MENU_SAVED_VALID)
#define g_savedRect     ((AM2_Rect *)(uintptr_t)ADDR_MENU_SAVED_RECT)
#define g_saveSlot      ((RECT *)(uintptr_t)ADDR_MENU_SAVE_SLOT)
#define g_cursorRect    ((AM2_Rect *)(uintptr_t)ADDR_MENU_CURSOR_RECT)
#define g_cursorPrev    ((AM2_Rect *)(uintptr_t)ADDR_MENU_CURSOR_PREV)
#define g_menuSurface2  (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MENU_SURFACE)
#define g_paintObj      (*(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT)
#define g_clipRect      ((const RECT *)(uintptr_t)ADDR_SCREEN_CLIP)
#define g_curX          (*(const int32_t *)(uintptr_t)ADDR_CURSOR_X)
#define g_curY          (*(const int32_t *)(uintptr_t)ADDR_CURSOR_Y)
#define g_overlayA      (*(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A)
#define g_overlayB      (*(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_B)
#define g_spriteMode    (*(const int32_t *)(uintptr_t)ADDR_MENU_SPRITE_MODE)
#define g_overlayAFld   (*(const int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_A_FLD)
#define g_overlayBFld   (*(const int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_B_FLD)

typedef uint32_t (__cdecl *am2_ticks_fn)(void);
#define orig_ticks (*(am2_ticks_fn)ADDR_TICKS)

/* The paint object's first virtual, which takes its rectangle BY VALUE. It is
 * a C++ virtual and not COM -- `this` stays in ecx and is never pushed. */
typedef void (__attribute__((thiscall)) *am2_paint_fn)(void *self, AM2_Rect r);

#define sfld16(s, off) (*(const int16_t *)((const uint8_t *)(s) + (off)))
#define sfld32(s, off) (*(const int32_t *)((const uint8_t *)(s) + (off)))
#define swr32(s, off)  (*(int32_t *)((uint8_t *)(s) + (off)))

/* Advance the ten-frame cycle if its 200 ms have elapsed. */
static void TickMenuAnimation(int32_t row)
{
    uint32_t now;
    int32_t  f;

    if (g_animFrame < 0)
        return;
    now = orig_ticks();
    if (now <= g_animNext)
        return;

    f = g_animFrame + 1;
    g_animFrame = f;
    if (f >= MENU_ANIM_FRAMES) {
        g_animFrame = 0;
        g_animNext  = now + MENU_ANIM_PERIOD;
        return;
    }
    if (!g_menuSprites2[row * MENU_ANIM_FRAMES + f]) {
        /* No second frame means this row simply does not animate. */
        if (f == 1) {
            g_animFrame = -1;
            g_animNext  = now + MENU_ANIM_PERIOD;
            return;
        }
        f = 0;
        g_animFrame = 0;
    }
    g_cursorSprite = g_menuSprites2[row * MENU_ANIM_FRAMES + f];
    g_animNext     = now + MENU_ANIM_PERIOD;
}

/* Draw one overlay at the cursor, offset by the cursor sprite's own overlay
 * point and the overlay's. The original writes this out twice. */
static void DrawMenuOverlaySprite(uint8_t *spr, uint32_t dxAddr, int32_t fld)
{
    const uint8_t *cursor = g_cursorSprite;
    int32_t x, y;

    if (!spr)
        return;
    swr32(spr, SPR_OFF_MODE) = fld;
    x = g_curX + sfld16(cursor, SPR_OFF_OVX)
      + *(const int16_t *)(uintptr_t)dxAddr;
    y = g_curY + sfld16(cursor, SPR_OFF_OVY)
      + *(const int16_t *)(uintptr_t)(dxAddr + 2);
    DrawSprite((AM2_Sprite *)spr, x, y, g_spriteMode != 0);
}

void __cdecl DrawMenuCursor(void)
{
    int32_t row;

    if (!g_menuEnabled)
        return;

    row = g_menuRow;
    TickMenuAnimation(row);

    if (g_paintObj && !g_presenting) {
        LPDIRECTDRAWSURFACE target = g_drawTarget;
        AM2_Rect            cur, clipped, shifted;
        uint8_t            *spr;

        SetDrawTarget(g_backBuffer);

        /* Put back whatever the cursor covered last tick. */
        if (g_savedValid)
            IDirectDrawSurface_Blt(g_drawTarget, (LPRECT)g_savedRect,
                                   g_menuSurface2, g_saveSlot,
                                   DDBLT_WAIT, NULL);

        {
            am2_paint_fn *vt = *(am2_paint_fn **)g_paintObj;
            vt[1](g_paintObj, *(AM2_Rect *)(g_paintObj + 0x14));
        }

        spr = g_cursorSprite;
        cur.left   = g_curX - sfld16(spr, SPR_OFF_HOTX);
        cur.top    = g_curY - sfld16(spr, SPR_OFF_HOTY);
        cur.right  = cur.left + sfld32(spr, SPR_OFF_W);
        cur.bottom = cur.top  + sfld32(spr, SPR_OFF_H);

        /* Save what is about to be covered. */
        if (IntersectRect((LPRECT)&clipped, (const RECT *)&cur, g_clipRect)) {
            IDirectDrawSurface_Blt(g_menuSurface2, g_saveSlot, g_backBuffer,
                                   (LPRECT)&clipped, DDBLT_WAIT, NULL);
            *g_savedRect = clipped;
            g_savedValid = 1;
        }

        DrawSprite((AM2_Sprite *)g_cursorSprite, g_curX, g_curY, 0);
        SetDrawTarget(target);

        /* And restore last tick's area, which the redraw above may have left
         * stale. */
        if (IntersectRect((LPRECT)&cur, (const RECT *)g_cursorRect,
                          g_clipRect)) {
            shifted = cur;
            if (g_drawTarget == g_primarySurface) {
                /* The primary is the whole desktop when windowed, so only the
                 * DESTINATION moves; the back buffer it reads from does not. */
                shifted.left   += g_screenRect.left;
                shifted.right  += g_screenRect.left;
                shifted.top    += g_screenRect.top;
                shifted.bottom += g_screenRect.top;
            }
            IDirectDrawSurface_Blt(g_drawTarget, (LPRECT)&shifted, g_backBuffer,
                                   (LPRECT)&cur, DDBLT_WAIT, NULL);
        }
    } else {
        g_savedValid = 0;

        if (row >= MENU_ROW_DIRECT) {
            if (!LockSurface(g_drawTarget))
                return;
            if (g_cursorSprite) {
                uint8_t *spr = g_cursorSprite;

                swr32(spr, SPR_OFF_MODE) = g_spriteMode;
                DrawSprite((AM2_Sprite *)spr,
                           g_curX + *(const int16_t *)(uintptr_t)ADDR_MENU_CURSOR_DX,
                           g_curY + *(const int16_t *)(uintptr_t)(ADDR_MENU_CURSOR_DX + 2),
                           g_spriteMode != 0);
                DrawMenuOverlaySprite(g_overlayA, ADDR_MENU_OVERLAY_A_DX,
                                      g_overlayAFld);
                DrawMenuOverlaySprite(g_overlayB, ADDR_MENU_OVERLAY_B_DX,
                                      g_overlayBFld);
            }
            UnlockSurface();
        } else {
            DrawSprite((AM2_Sprite *)g_cursorSprite, g_curX, g_curY, 0);
        }
    }

    /* This tick becomes last tick. */
    *g_cursorPrev = *g_cursorRect;

    if (g_cursorSprite) {
        const uint8_t *spr = g_cursorSprite;
        AM2_Rect       r;
        int32_t        hx = sfld16(spr, SPR_OFF_HOTX);
        int32_t        hy = sfld16(spr, SPR_OFF_HOTY);

        RectSet(&r, g_curX - hx, g_curY - hy,
                g_curX + sfld32(spr, SPR_OFF_W) - hx,
                g_curY + sfld32(spr, SPR_OFF_H) - hy);
        *g_cursorRect = r;
    }
}

int surface_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DRAW_MENU_CURSOR, (const void *)DrawMenuCursor,
                        "DrawMenuCursor", 0);

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
    rc |= patch_replace(ADDR_MAKE_BITMAP, (const void *)MakeBitmap,
                        "MakeBitmap", 4);
    rc |= patch_replace(ADDR_DRAW_MENU_OVERLAY, (const void *)DrawMenuOverlay,
                        "DrawMenuOverlay", 0);
    rc |= patch_replace(ADDR_FREE_MAP_SURFACES, (const void *)FreeMapSurfaces,
                        "FreeMapSurfaces", 0);
    return rc;
}
