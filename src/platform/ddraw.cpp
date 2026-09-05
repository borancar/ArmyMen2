/* ddraw.cpp -- DirectDraw over plain buffers.
 *
 * Every surface is an 8-bit buffer in memory. The primary is the one the
 * screen shows: whenever something lands on it -- a Flip from the back
 * buffer, a Blt or BltFast into it, an Unlock or ReleaseDC on it, or a
 * change to the palette it has attached -- its pixels go through that
 * palette to sdl.cpp as one frame. Nothing else in the layer knows how a
 * frame reaches the screen.
 *
 * Cooperative levels and display modes are accepted and not enforced: the
 * game asks for an exclusive fullscreen 8-bit mode and gets a window whose
 * logical size is the mode it asked for. Surfaces are never lost.
 */
#include "platform.h"
#include <ddraw.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AM2_DEFINE_GUID_VALUE(IID_IDirectDraw,        0x6C14DB80, 0xA733, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectDraw2,       0xB3A6F3E0, 0x2B43, 0x11CF, 0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56);
AM2_DEFINE_GUID_VALUE(IID_IDirectDrawSurface, 0x6C14DB81, 0xA733, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectDrawPalette, 0x6C14DB84, 0xA733, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);

/* ---- objects ------------------------------------------------------------------ */

typedef struct AM2_DDPalette {
    const IDirectDrawPaletteVtbl *lpVtbl;
    int32_t      refs;
    PALETTEENTRY entries[256];
} AM2_DDPalette;

typedef struct AM2_DDSurface {
    const IDirectDrawSurfaceVtbl *lpVtbl;
    int32_t        refs;
    int32_t        width, height, pitch;
    uint8_t       *pixels;
    int32_t        isPrimary;
    struct AM2_DDSurface *back;    /* the flip chain's back buffer */
    AM2_DDPalette *palette;
    DDCOLORKEY     srcKey;
    int32_t        hasSrcKey;
    DWORD          caps;
} AM2_DDSurface;

typedef struct AM2_DDraw {
    const IDirectDrawVtbl  *lpVtbl;
    const IDirectDraw2Vtbl *lpVtbl2;    /* the IDirectDraw2 view is this+4 */
    int32_t  refs;
    int32_t  modeW, modeH, modeBpp;
} AM2_DDraw;

static AM2_DDSurface *am2_primary;
/* Set by SetCooperativeLevel: an exclusive fullscreen application owns the
 * display and its primary's palette IS the screen's; a windowed one shares
 * the desktop and shows through the system palette GDI realised, whatever
 * palette it attaches. The game relies on the second: in windowed mode it
 * attaches a DirectDraw palette built from a snapshot that is still zero
 * and expects the RealizePalette it did first to be what shows. */
static int32_t am2_exclusive;
extern const IDirectDrawSurfaceVtbl am2_surface_vtbl;
extern const IDirectDrawPaletteVtbl am2_palette_vtbl;
extern const IDirectDrawVtbl        am2_ddraw_vtbl;
extern const IDirectDraw2Vtbl       am2_ddraw2_vtbl;

/* ---- the frame ---------------------------------------------------------------------- */

static const PALETTEENTRY *am2_primary_palette(void)
{
    return am2_exclusive && am2_primary && am2_primary->palette
               ? am2_primary->palette->entries : am2_screen_palette;
}

static void am2_present(void)
{
    static int32_t presented;

    if (!am2_primary)
        return;
    if (presented < 8) {
        const PALETTEENTRY *pal = am2_primary_palette();
        int32_t i, nonzero = 0, lit = 0;
        for (i = 0; i < 256; i++)
            if (pal[i].peRed | pal[i].peGreen | pal[i].peBlue)
                nonzero++;
        for (i = 0; i < am2_primary->height; i++) {
            const uint8_t *row = am2_primary->pixels + i * am2_primary->pitch;
            int32_t x;
            for (x = 0; x < am2_primary->width; x++)
                lit += row[x] != 0;
        }
        am2_plat_debug("present #%d: %s palette, %d non-black entries, %d non-zero pixels",
                       presented, am2_primary->palette ? "attached" : "screen", nonzero, lit);
        presented++;
    }
    am2_host_present(am2_primary->pixels, am2_primary->pitch, am2_primary->width,
                     am2_primary->height, am2_primary_palette());
}

int32_t am2_ddraw_primary_target(AM2_DCTarget *out)
{
    if (!am2_primary)
        return 0;
    out->pixels = am2_primary->pixels;
    out->pitch = am2_primary->pitch;
    out->width = am2_primary->width;
    out->height = am2_primary->height;
    out->palette = am2_primary_palette();
    out->changed = am2_present;
    return 1;
}

/* ---- surfaces ----------------------------------------------------------------------- */

static AM2_DDSurface *am2_surface_new(int32_t w, int32_t h, DWORD caps)
{
    AM2_DDSurface *s = (AM2_DDSurface *)calloc(1, sizeof *s);

    if (!s)
        return NULL;
    s->lpVtbl = &am2_surface_vtbl;
    s->refs = 1;
    s->width = w;
    s->height = h;
    s->pitch = (w + 15) & ~15;
    s->pixels = (uint8_t *)calloc((size_t)s->pitch * (size_t)h + 16, 1);
    s->caps = caps;
    if (!s->pixels) {
        free(s);
        return NULL;
    }
    return s;
}

static void am2_surface_free(AM2_DDSurface *s)
{
    if (s->back)
        IDirectDrawSurface_Release((LPDIRECTDRAWSURFACE)s->back);
    if (s->palette)
        IDirectDrawPalette_Release((LPDIRECTDRAWPALETTE)s->palette);
    if (s == am2_primary)
        am2_primary = NULL;
    free(s->pixels);
    free(s);
}

static HRESULT STDMETHODCALLTYPE Surface_QueryInterface(IDirectDrawSurface *p, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectDrawSurface)) {
        *out = p;
        p->lpVtbl->AddRef(p);
        return DD_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Surface_AddRef(IDirectDrawSurface *p)
{
    return (ULONG)++((AM2_DDSurface *)p)->refs;
}

static ULONG STDMETHODCALLTYPE Surface_Release(IDirectDrawSurface *p)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;
    int32_t        n = --s->refs;

    if (n <= 0) {
        am2_surface_free(s);
        return 0;
    }
    return (ULONG)n;
}

/* The slots the game never calls, each with its own signature: a stdcall
 * callee pops its own arguments, so a stub reached through the wrong
 * prototype would unbalance the stack. */
#define SURFACE_UNSUPPORTED(name, ...) \
    static HRESULT STDMETHODCALLTYPE Surface_##name(IDirectDrawSurface *, ##__VA_ARGS__) \
    { return DDERR_UNSUPPORTED; }
SURFACE_UNSUPPORTED(AddAttachedSurface, LPDIRECTDRAWSURFACE)
SURFACE_UNSUPPORTED(AddOverlayDirtyRect, LPRECT)
SURFACE_UNSUPPORTED(BltBatch, LPVOID, DWORD, DWORD)
SURFACE_UNSUPPORTED(DeleteAttachedSurface, DWORD, LPDIRECTDRAWSURFACE)
SURFACE_UNSUPPORTED(EnumAttachedSurfaces, LPVOID, LPDDENUMSURFACESCALLBACK)
SURFACE_UNSUPPORTED(EnumOverlayZOrders, DWORD, LPVOID, LPDDENUMSURFACESCALLBACK)
SURFACE_UNSUPPORTED(GetBltStatus, DWORD)
SURFACE_UNSUPPORTED(GetCaps, LPDDSCAPS)
SURFACE_UNSUPPORTED(GetClipper, LPDIRECTDRAWCLIPPER *)
SURFACE_UNSUPPORTED(GetFlipStatus, DWORD)
SURFACE_UNSUPPORTED(GetOverlayPosition, LPLONG, LPLONG)
SURFACE_UNSUPPORTED(GetPixelFormat, LPDDPIXELFORMAT)
SURFACE_UNSUPPORTED(Initialize, LPDIRECTDRAW, LPDDSURFACEDESC)
SURFACE_UNSUPPORTED(SetOverlayPosition, LONG, LONG)
SURFACE_UNSUPPORTED(UpdateOverlay, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPVOID)
SURFACE_UNSUPPORTED(UpdateOverlayDisplay, DWORD)
SURFACE_UNSUPPORTED(UpdateOverlayZOrder, DWORD, LPDIRECTDRAWSURFACE)

static int32_t am2_rect_of(const AM2_DDSurface *s, const RECT *rc, RECT *out)
{
    if (rc) {
        *out = *rc;
    } else {
        out->left = out->top = 0;
        out->right = s->width;
        out->bottom = s->height;
    }
    return out->left < out->right && out->top < out->bottom;
}

static void am2_fill(AM2_DDSurface *s, const RECT *rc, uint8_t colour)
{
    RECT    r;
    int32_t y;

    if (!am2_rect_of(s, rc, &r))
        return;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > s->width) r.right = s->width;
    if (r.bottom > s->height) r.bottom = s->height;
    for (y = r.top; y < r.bottom; y++)
        memset(s->pixels + y * s->pitch + r.left, colour, (size_t)(r.right - r.left));
}

/* Copy src's rectangle to (dx, dy) on dst, clipped to both, optionally
 * skipping pixels within src's colour key. Handles overlap by copying
 * through a row buffer when the two surfaces are one. */
static void am2_copy(AM2_DDSurface *dst, int32_t dx, int32_t dy,
                     const AM2_DDSurface *src, const RECT *srcRect, int32_t keyed)
{
    RECT    r;
    int32_t w, h, y, x;
    uint8_t row[4096];

    if (!am2_rect_of(src, srcRect, &r))
        return;
    if (r.left < 0) { dx -= r.left; r.left = 0; }
    if (r.top < 0) { dy -= r.top; r.top = 0; }
    if (r.right > src->width) r.right = src->width;
    if (r.bottom > src->height) r.bottom = src->height;
    if (dx < 0) { r.left -= dx; dx = 0; }
    if (dy < 0) { r.top -= dy; dy = 0; }
    w = r.right - r.left;
    h = r.bottom - r.top;
    if (dx + w > dst->width) w = dst->width - dx;
    if (dy + h > dst->height) h = dst->height - dy;
    if (w <= 0 || h <= 0 || w > (int32_t)sizeof row)
        return;
    /* A surface scrolled onto itself: when the destination lies below the
     * source the rows must go bottom-up, or each row reads the one the
     * previous step just overwrote -- and with a one-pixel scroll every row
     * becomes a copy of the first, which is what the vertical stripes over
     * the map were. memmove settles the horizontal case within a row. */
    for (int32_t i = 0; i < h; i++) {
        const uint8_t *s;
        uint8_t       *d;
        y = (dst == src && dy > r.top) ? h - 1 - i : i;
        s = src->pixels + (r.top + y) * src->pitch + r.left;
        d = dst->pixels + (dy + y) * dst->pitch + dx;
        if (!keyed) {
            memmove(d, s, (size_t)w);
            continue;
        }
        memcpy(row, s, (size_t)w);
        for (x = 0; x < w; x++) {
            uint32_t v = row[x];
            if (v >= src->srcKey.dwColorSpaceLowValue &&
                v <= src->srcKey.dwColorSpaceHighValue)
                continue;
            d[x] = row[x];
        }
    }
}

/* Stretch src's rectangle onto dst's, nearest neighbour. */
static void am2_stretch(AM2_DDSurface *dst, const RECT *dstRect,
                        const AM2_DDSurface *src, const RECT *srcRect, int32_t keyed)
{
    RECT    d, s;
    int32_t dw, dh, sw, sh, x, y;

    if (!am2_rect_of(dst, dstRect, &d) || !am2_rect_of(src, srcRect, &s))
        return;
    dw = d.right - d.left;
    dh = d.bottom - d.top;
    sw = s.right - s.left;
    sh = s.bottom - s.top;
    for (y = 0; y < dh; y++) {
        int32_t sy = s.top + y * sh / dh;
        int32_t dy = d.top + y;
        if (dy < 0 || dy >= dst->height || sy < 0 || sy >= src->height)
            continue;
        for (x = 0; x < dw; x++) {
            int32_t sx = s.left + x * sw / dw;
            int32_t dx = d.left + x;
            uint8_t v;
            if (dx < 0 || dx >= dst->width || sx < 0 || sx >= src->width)
                continue;
            v = src->pixels[sy * src->pitch + sx];
            if (keyed && v >= src->srcKey.dwColorSpaceLowValue &&
                v <= src->srcKey.dwColorSpaceHighValue)
                continue;
            dst->pixels[dy * dst->pitch + dx] = v;
        }
    }
}

static HRESULT STDMETHODCALLTYPE Surface_Blt(IDirectDrawSurface *p, LPRECT dstRect,
                                             LPDIRECTDRAWSURFACE srcp, LPRECT srcRect,
                                             DWORD flags, LPDDBLTFX fx)
{
    AM2_DDSurface *dst = (AM2_DDSurface *)p;
    AM2_DDSurface *src = (AM2_DDSurface *)srcp;

    if (dst->isPrimary) {
        static int32_t logged;
        if (logged < 3) {
            logged++;
            am2_plat_debug("Blt to primary rect %d,%d-%d,%d flags 0x%x src %p",
                           dstRect ? (int)dstRect->left : -1, dstRect ? (int)dstRect->top : -1,
                           dstRect ? (int)dstRect->right : -1, dstRect ? (int)dstRect->bottom : -1,
                           (unsigned)flags, (void *)src);
        }
    }
    if (flags & DDBLT_COLORFILL) {
        if (!fx)
            return DDERR_INVALIDPARAMS;
        am2_fill(dst, dstRect, (uint8_t)fx->dwFillColor);
    } else if (src) {
        RECT    d, s;
        int32_t keyed = (flags & DDBLT_KEYSRC) && src->hasSrcKey;
        am2_rect_of(dst, dstRect, &d);
        am2_rect_of(src, srcRect, &s);
        if (d.right - d.left == s.right - s.left && d.bottom - d.top == s.bottom - s.top)
            am2_copy(dst, d.left, d.top, src, &s, keyed);
        else
            am2_stretch(dst, &d, src, &s, keyed);
    }
    if (dst->isPrimary)
        am2_present();
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_BltFast(IDirectDrawSurface *p, DWORD x, DWORD y,
                                                 LPDIRECTDRAWSURFACE srcp, LPRECT srcRect,
                                                 DWORD flags)
{
    AM2_DDSurface *dst = (AM2_DDSurface *)p;
    AM2_DDSurface *src = (AM2_DDSurface *)srcp;

    if (!src)
        return DDERR_INVALIDPARAMS;
    if (dst->isPrimary) {
        static int32_t logged;
        if (logged < 3) {
            logged++;
            am2_plat_debug("BltFast to primary at %d,%d from %dx%d rect %d,%d-%d,%d",
                           (int)x, (int)y, src->width, src->height,
                           srcRect ? (int)srcRect->left : -1, srcRect ? (int)srcRect->top : -1,
                           srcRect ? (int)srcRect->right : -1, srcRect ? (int)srcRect->bottom : -1);
        }
    }
    am2_copy(dst, (int32_t)x, (int32_t)y, src, srcRect,
             (flags & DDBLTFAST_SRCCOLORKEY) && src->hasSrcKey);
    if (dst->isPrimary)
        am2_present();
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_Flip(IDirectDrawSurface *p, LPDIRECTDRAWSURFACE to, DWORD flags)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;
    uint8_t       *t;

    (void)to; (void)flags;
    if (!s->isPrimary || !s->back)
        return DDERR_UNSUPPORTED;
    /* A real flip exchanges which memory the display scans out, so the
     * buffers swap places; what was the front is now the back. */
    t = s->pixels;
    s->pixels = s->back->pixels;
    s->back->pixels = t;
    am2_present();
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_GetAttachedSurface(IDirectDrawSurface *p, LPDDSCAPS caps,
                                                            LPDIRECTDRAWSURFACE *out)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;

    if (caps && (caps->dwCaps & DDSCAPS_BACKBUFFER) && s->back) {
        s->back->refs++;
        *out = (LPDIRECTDRAWSURFACE)s->back;
        return DD_OK;
    }
    *out = NULL;
    return DDERR_NOTFOUND;
}

static HRESULT STDMETHODCALLTYPE Surface_GetColorKey(IDirectDrawSurface *p, DWORD flags, LPDDCOLORKEY out)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;

    if ((flags & DDCKEY_SRCBLT) && s->hasSrcKey) {
        *out = s->srcKey;
        return DD_OK;
    }
    return DDERR_NOTFOUND;
}

static void am2_surface_changed_primary(void)
{
    am2_present();
}

static HRESULT STDMETHODCALLTYPE Surface_GetDC(IDirectDrawSurface *p, HDC *out)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;
    AM2_DCTarget   t;

    t.pixels = s->pixels;
    t.pitch = s->pitch;
    t.width = s->width;
    t.height = s->height;
    t.palette = s->palette ? s->palette->entries : am2_primary_palette();
    t.changed = s->isPrimary ? am2_surface_changed_primary : NULL;
    *out = am2_dc_create(&t);
    return *out ? DD_OK : DDERR_NODC;
}

static HRESULT STDMETHODCALLTYPE Surface_ReleaseDC(IDirectDrawSurface *p, HDC dc)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;

    am2_dc_destroy(dc);
    if (s->isPrimary)
        am2_present();
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_GetPalette(IDirectDrawSurface *p, LPDIRECTDRAWPALETTE *out)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;

    if (!s->palette) {
        *out = NULL;
        return DDERR_NOPALETTEATTACHED;
    }
    s->palette->refs++;
    *out = (LPDIRECTDRAWPALETTE)s->palette;
    return DD_OK;
}

static void am2_describe(const AM2_DDSurface *s, LPDDSURFACEDESC d, int32_t withPointer,
                         const RECT *rc)
{
    DWORD size = d->dwSize;

    memset(d, 0, size < sizeof *d ? size : sizeof *d);
    d->dwSize = size;
    d->dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
    d->dwHeight = (DWORD)s->height;
    d->dwWidth = (DWORD)s->width;
    d->lPitch = s->pitch;
    d->dwBackBufferCount = s->back ? 1 : 0;
    d->ddpfPixelFormat.dwSize = sizeof d->ddpfPixelFormat;
    d->ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
    d->ddpfPixelFormat.dwRGBBitCount = 8;
    d->ddsCaps.dwCaps = s->caps;
    if (withPointer) {
        d->dwFlags |= DDSD_LPSURFACE;
        d->lpSurface = s->pixels;
        if (rc)
            d->lpSurface = s->pixels + rc->top * s->pitch + rc->left;
    }
}

static HRESULT STDMETHODCALLTYPE Surface_GetSurfaceDesc(IDirectDrawSurface *p, LPDDSURFACEDESC d)
{
    if (!d || d->dwSize < sizeof *d)
        return DDERR_INVALIDPARAMS;
    am2_describe((AM2_DDSurface *)p, d, 0, NULL);
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_IsLost(IDirectDrawSurface *p) { (void)p; return DD_OK; }
static HRESULT STDMETHODCALLTYPE Surface_Restore(IDirectDrawSurface *p) { (void)p; return DD_OK; }

static HRESULT STDMETHODCALLTYPE Surface_Lock(IDirectDrawSurface *p, LPRECT rc, LPDDSURFACEDESC d,
                                              DWORD flags, HANDLE ev)
{
    (void)flags; (void)ev;
    if (!d || d->dwSize < sizeof *d)
        return DDERR_INVALIDPARAMS;
    am2_describe((AM2_DDSurface *)p, d, 1, rc);
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_Unlock(IDirectDrawSurface *p, LPVOID ptr)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;

    (void)ptr;
    if (s->isPrimary)
        am2_present();
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_SetClipper(IDirectDrawSurface *p, LPDIRECTDRAWCLIPPER c)
{
    (void)p; (void)c;
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_SetColorKey(IDirectDrawSurface *p, DWORD flags, LPDDCOLORKEY key)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;

    if (!(flags & DDCKEY_SRCBLT))
        return DDERR_UNSUPPORTED;
    if (key) {
        s->srcKey = *key;
        s->hasSrcKey = 1;
    } else {
        s->hasSrcKey = 0;
    }
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Surface_SetPalette(IDirectDrawSurface *p, LPDIRECTDRAWPALETTE palp)
{
    AM2_DDSurface *s = (AM2_DDSurface *)p;
    AM2_DDPalette *pal = (AM2_DDPalette *)palp;

    if (pal)
        pal->refs++;
    if (s->palette)
        IDirectDrawPalette_Release((LPDIRECTDRAWPALETTE)s->palette);
    s->palette = pal;
    am2_plat_debug("SetPalette on %s surface", s->isPrimary ? "the primary" : "an offscreen");
    if (s->isPrimary) {
        if (pal && am2_exclusive)
            memcpy(am2_screen_palette, pal->entries, sizeof am2_screen_palette);
        am2_present();
    }
    return DD_OK;
}

const IDirectDrawSurfaceVtbl am2_surface_vtbl = {
    Surface_QueryInterface, Surface_AddRef, Surface_Release,
    Surface_AddAttachedSurface, Surface_AddOverlayDirtyRect, Surface_Blt,
    Surface_BltBatch, Surface_BltFast, Surface_DeleteAttachedSurface,
    Surface_EnumAttachedSurfaces, Surface_EnumOverlayZOrders, Surface_Flip,
    Surface_GetAttachedSurface, Surface_GetBltStatus, Surface_GetCaps,
    Surface_GetClipper, Surface_GetColorKey, Surface_GetDC, Surface_GetFlipStatus,
    Surface_GetOverlayPosition, Surface_GetPalette, Surface_GetPixelFormat,
    Surface_GetSurfaceDesc, Surface_Initialize, Surface_IsLost, Surface_Lock,
    Surface_ReleaseDC, Surface_Restore, Surface_SetClipper, Surface_SetColorKey,
    Surface_SetOverlayPosition, Surface_SetPalette, Surface_Unlock,
    Surface_UpdateOverlay, Surface_UpdateOverlayDisplay, Surface_UpdateOverlayZOrder,
};

/* ---- palettes ----------------------------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE Palette_QueryInterface(IDirectDrawPalette *p, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectDrawPalette)) {
        *out = p;
        p->lpVtbl->AddRef(p);
        return DD_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Palette_AddRef(IDirectDrawPalette *p)
{
    return (ULONG)++((AM2_DDPalette *)p)->refs;
}

static ULONG STDMETHODCALLTYPE Palette_Release(IDirectDrawPalette *p)
{
    AM2_DDPalette *pal = (AM2_DDPalette *)p;
    int32_t        n = --pal->refs;

    if (n <= 0) {
        free(pal);
        return 0;
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE Palette_GetCaps(IDirectDrawPalette *p, LPDWORD out)
{
    (void)p;
    *out = DDPCAPS_8BIT | DDPCAPS_ALLOW256;
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Palette_GetEntries(IDirectDrawPalette *p, DWORD flags, DWORD start,
                                                    DWORD count, LPPALETTEENTRY out)
{
    AM2_DDPalette *pal = (AM2_DDPalette *)p;

    (void)flags;
    if (start + count > 256)
        return DDERR_INVALIDPARAMS;
    memcpy(out, pal->entries + start, count * sizeof(PALETTEENTRY));
    return DD_OK;
}

static HRESULT STDMETHODCALLTYPE Palette_Initialize(IDirectDrawPalette *p, LPDIRECTDRAW dd, DWORD flags,
                                                    LPPALETTEENTRY entries)
{
    (void)p; (void)dd; (void)flags; (void)entries;
    return DDERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Palette_SetEntries(IDirectDrawPalette *p, DWORD flags, DWORD start,
                                                    DWORD count, LPPALETTEENTRY entries)
{
    AM2_DDPalette *pal = (AM2_DDPalette *)p;

    (void)flags;
    if (start + count > 256 || !entries)
        return DDERR_INVALIDPARAMS;
    memcpy(pal->entries + start, entries, count * sizeof(PALETTEENTRY));
    /* A palette change on the screen shows at once -- the game fades this
     * way, without flipping between steps. */
    if (am2_primary && am2_primary->palette == pal) {
        if (am2_exclusive)
            memcpy(am2_screen_palette, pal->entries, sizeof am2_screen_palette);
        am2_present();
    }
    return DD_OK;
}

const IDirectDrawPaletteVtbl am2_palette_vtbl = {
    Palette_QueryInterface, Palette_AddRef, Palette_Release,
    Palette_GetCaps, Palette_GetEntries, Palette_Initialize, Palette_SetEntries,
};

/* ---- the DirectDraw object --------------------------------------------------------- */

static AM2_DDraw *am2_ddraw_of(void *p, int32_t v2)
{
    return (AM2_DDraw *)((uint8_t *)p - (v2 ? sizeof(void *) : 0));
}

static HRESULT am2_ddraw_qi(AM2_DDraw *dd, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectDraw)) {
        *out = &dd->lpVtbl;
    } else if (IsEqualGUID(iid, IID_IDirectDraw2)) {
        *out = &dd->lpVtbl2;
    } else {
        *out = NULL;
        return E_NOINTERFACE;
    }
    dd->refs++;
    return DD_OK;
}

static ULONG am2_ddraw_release(AM2_DDraw *dd)
{
    int32_t n = --dd->refs;

    if (n <= 0) {
        free(dd);
        return 0;
    }
    return (ULONG)n;
}

static HRESULT am2_ddraw_create_palette(AM2_DDraw *dd, DWORD flags, LPPALETTEENTRY entries,
                                        LPDIRECTDRAWPALETTE *out)
{
    AM2_DDPalette *pal = (AM2_DDPalette *)calloc(1, sizeof *pal);

    (void)dd; (void)flags;
    if (!pal)
        return DDERR_OUTOFMEMORY;
    pal->lpVtbl = &am2_palette_vtbl;
    pal->refs = 1;
    if (entries)
        memcpy(pal->entries, entries, sizeof pal->entries);
    *out = (LPDIRECTDRAWPALETTE)pal;
    return DD_OK;
}

static HRESULT am2_ddraw_create_surface(AM2_DDraw *dd, LPDDSURFACEDESC d, LPDIRECTDRAWSURFACE *out)
{
    AM2_DDSurface *s;
    DWORD          caps;

    *out = NULL;
    if (!d || d->dwSize < sizeof *d || !(d->dwFlags & DDSD_CAPS))
        return DDERR_INVALIDPARAMS;
    caps = d->ddsCaps.dwCaps;
    am2_plat_debug("CreateSurface caps=0x%x %ux%u", (unsigned)caps,
                   (unsigned)d->dwWidth, (unsigned)d->dwHeight);
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        int32_t w = dd->modeW ? dd->modeW : 640;
        int32_t h = dd->modeH ? dd->modeH : 480;
        if (am2_primary) {
            am2_primary->refs++;
            *out = (LPDIRECTDRAWSURFACE)am2_primary;
            return DD_OK;
        }
        s = am2_surface_new(w, h, caps | DDSCAPS_VIDEOMEMORY);
        if (!s)
            return DDERR_OUTOFMEMORY;
        s->isPrimary = 1;
        if ((d->dwFlags & DDSD_BACKBUFFERCOUNT) && d->dwBackBufferCount > 0) {
            s->back = am2_surface_new(w, h, DDSCAPS_BACKBUFFER | DDSCAPS_FLIP |
                                            DDSCAPS_VIDEOMEMORY);
            if (!s->back) {
                am2_surface_free(s);
                return DDERR_OUTOFMEMORY;
            }
        }
        am2_primary = s;
        am2_host_display_mode(w, h);
        *out = (LPDIRECTDRAWSURFACE)s;
        return DD_OK;
    }
    if (!(d->dwFlags & DDSD_WIDTH) || !(d->dwFlags & DDSD_HEIGHT) ||
        d->dwWidth == 0 || d->dwHeight == 0 || d->dwWidth > 16384 || d->dwHeight > 16384)
        return DDERR_INVALIDPARAMS;
    s = am2_surface_new((int32_t)d->dwWidth, (int32_t)d->dwHeight,
                        caps | ((caps & DDSCAPS_SYSTEMMEMORY) ? 0 : DDSCAPS_VIDEOMEMORY));
    if (!s)
        return DDERR_OUTOFMEMORY;
    *out = (LPDIRECTDRAWSURFACE)s;
    return DD_OK;
}

static HRESULT am2_ddraw_set_mode(AM2_DDraw *dd, DWORD w, DWORD h, DWORD bpp)
{
    if (w == 0 || h == 0 || w > 4096 || h > 4096)
        return DDERR_INVALIDPARAMS;
    dd->modeW = (int32_t)w;
    dd->modeH = (int32_t)h;
    dd->modeBpp = (int32_t)bpp;
    am2_host_display_mode((int32_t)w, (int32_t)h);
    am2_plat_log("display mode %ux%u at %u bpp", (unsigned)w, (unsigned)h, (unsigned)bpp);
    return DD_OK;
}

/* The two vtables share most of their entries, so each method is written
 * once over the shared state and wrapped for whichever view is calling. */
#define DD_METHOD(name, v2, ...) \
    static HRESULT STDMETHODCALLTYPE name##_##v2(__VA_ARGS__)

#define DD_BOTH(name, body, ...) \
    static HRESULT STDMETHODCALLTYPE DD1_##name(IDirectDraw *p, ##__VA_ARGS__) { AM2_DDraw *dd = am2_ddraw_of(p, 0); (void)dd; body } \
    static HRESULT STDMETHODCALLTYPE DD2_##name(IDirectDraw2 *p, ##__VA_ARGS__) { AM2_DDraw *dd = am2_ddraw_of(p, 1); (void)dd; body }

DD_BOTH(QueryInterface, { return am2_ddraw_qi(dd, iid, out); }, REFIID iid, LPVOID *out)
DD_BOTH(Compact, { return DD_OK; })
DD_BOTH(CreateClipper, { (void)flags; (void)outer; *out = NULL; return DDERR_UNSUPPORTED; }, DWORD flags, LPDIRECTDRAWCLIPPER *out, IUnknown *outer)
DD_BOTH(CreatePalette, { (void)outer; return am2_ddraw_create_palette(dd, flags, entries, out); }, DWORD flags, LPPALETTEENTRY entries, LPDIRECTDRAWPALETTE *out, IUnknown *outer)
DD_BOTH(CreateSurface, { (void)outer; return am2_ddraw_create_surface(dd, d, out); }, LPDDSURFACEDESC d, LPDIRECTDRAWSURFACE *out, IUnknown *outer)
DD_BOTH(DuplicateSurface, { (void)s; *out = NULL; return DDERR_UNSUPPORTED; }, LPDIRECTDRAWSURFACE s, LPDIRECTDRAWSURFACE *out)
DD_BOTH(EnumDisplayModes, { (void)flags; (void)d; (void)ctx; (void)cb; return DDERR_UNSUPPORTED; }, DWORD flags, LPDDSURFACEDESC d, LPVOID ctx, LPDDENUMMODESCALLBACK cb)
DD_BOTH(EnumSurfaces, { (void)flags; (void)d; (void)ctx; (void)cb; return DDERR_UNSUPPORTED; }, DWORD flags, LPDDSURFACEDESC d, LPVOID ctx, LPDDENUMSURFACESCALLBACK cb)
DD_BOTH(FlipToGDISurface, { return DD_OK; })
DD_BOTH(GetCaps, { if (drv) { memset(drv, 0, sizeof *drv); drv->dwSize = sizeof *drv; } if (hel) { memset(hel, 0, sizeof *hel); hel->dwSize = sizeof *hel; } return DD_OK; }, LPDDCAPS drv, LPDDCAPS hel)
DD_BOTH(GetDisplayMode, { if (!d || d->dwSize < sizeof *d) return DDERR_INVALIDPARAMS; memset(d, 0, sizeof *d); d->dwSize = sizeof *d; d->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT; d->dwWidth = dd->modeW ? dd->modeW : 640; d->dwHeight = dd->modeH ? dd->modeH : 480; d->ddpfPixelFormat.dwSize = sizeof d->ddpfPixelFormat; d->ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8; d->ddpfPixelFormat.dwRGBBitCount = 8; return DD_OK; }, LPDDSURFACEDESC d)
DD_BOTH(GetFourCCCodes, { *n = 0; (void)codes; return DD_OK; }, LPDWORD n, LPDWORD codes)
DD_BOTH(GetGDISurface, { if (!am2_primary) { *out = NULL; return DDERR_NOTFOUND; } am2_primary->refs++; *out = (LPDIRECTDRAWSURFACE)am2_primary; return DD_OK; }, LPDIRECTDRAWSURFACE *out)
DD_BOTH(GetMonitorFrequency, { *out = 60; return DD_OK; }, LPDWORD out)
DD_BOTH(GetScanLine, { *out = 0; return DD_OK; }, LPDWORD out)
DD_BOTH(GetVerticalBlankStatus, { *out = TRUE; return DD_OK; }, LPBOOL out)
DD_BOTH(Initialize, { (void)g; return DDERR_UNSUPPORTED; }, GUID *g)
DD_BOTH(RestoreDisplayMode, { return DD_OK; })
DD_BOTH(SetCooperativeLevel, { (void)hwnd; am2_exclusive = (flags & DDSCL_EXCLUSIVE) != 0; am2_plat_debug("SetCooperativeLevel 0x%x", (unsigned)flags); return DD_OK; }, HWND hwnd, DWORD flags)
DD_BOTH(WaitForVerticalBlank, { (void)flags; (void)ev; return DD_OK; }, DWORD flags, HANDLE ev)

static ULONG STDMETHODCALLTYPE DD1_AddRef(IDirectDraw *p) { return (ULONG)++am2_ddraw_of(p, 0)->refs; }
static ULONG STDMETHODCALLTYPE DD2_AddRef(IDirectDraw2 *p) { return (ULONG)++am2_ddraw_of(p, 1)->refs; }
static ULONG STDMETHODCALLTYPE DD1_Release(IDirectDraw *p) { return am2_ddraw_release(am2_ddraw_of(p, 0)); }
static ULONG STDMETHODCALLTYPE DD2_Release(IDirectDraw2 *p) { return am2_ddraw_release(am2_ddraw_of(p, 1)); }
static HRESULT STDMETHODCALLTYPE DD1_SetDisplayMode(IDirectDraw *p, DWORD w, DWORD h, DWORD bpp)
{
    return am2_ddraw_set_mode(am2_ddraw_of(p, 0), w, h, bpp);
}
static HRESULT STDMETHODCALLTYPE DD2_SetDisplayMode(IDirectDraw2 *p, DWORD w, DWORD h, DWORD bpp,
                                                    DWORD refresh, DWORD flags)
{
    (void)refresh; (void)flags;
    return am2_ddraw_set_mode(am2_ddraw_of(p, 1), w, h, bpp);
}
static HRESULT STDMETHODCALLTYPE DD2_GetAvailableVidMem(IDirectDraw2 *p, LPDDSCAPS caps,
                                                        LPDWORD total, LPDWORD freeMem)
{
    (void)p; (void)caps;
    if (total)
        *total = 64u << 20;
    if (freeMem)
        *freeMem = 64u << 20;
    return DD_OK;
}

const IDirectDrawVtbl am2_ddraw_vtbl = {
    DD1_QueryInterface, DD1_AddRef, DD1_Release, DD1_Compact, DD1_CreateClipper,
    DD1_CreatePalette, DD1_CreateSurface, DD1_DuplicateSurface, DD1_EnumDisplayModes,
    DD1_EnumSurfaces, DD1_FlipToGDISurface, DD1_GetCaps, DD1_GetDisplayMode,
    DD1_GetFourCCCodes, DD1_GetGDISurface, DD1_GetMonitorFrequency, DD1_GetScanLine,
    DD1_GetVerticalBlankStatus, DD1_Initialize, DD1_RestoreDisplayMode,
    DD1_SetCooperativeLevel, DD1_WaitForVerticalBlank, DD1_SetDisplayMode,
};

const IDirectDraw2Vtbl am2_ddraw2_vtbl = {
    DD2_QueryInterface, DD2_AddRef, DD2_Release, DD2_Compact, DD2_CreateClipper,
    DD2_CreatePalette, DD2_CreateSurface, DD2_DuplicateSurface, DD2_EnumDisplayModes,
    DD2_EnumSurfaces, DD2_FlipToGDISurface, DD2_GetCaps, DD2_GetDisplayMode,
    DD2_GetFourCCCodes, DD2_GetGDISurface, DD2_GetMonitorFrequency, DD2_GetScanLine,
    DD2_GetVerticalBlankStatus, DD2_Initialize, DD2_RestoreDisplayMode,
    DD2_SetCooperativeLevel, DD2_WaitForVerticalBlank, DD2_SetDisplayMode,
    DD2_GetAvailableVidMem,
};

HRESULT WINAPI DirectDrawCreate(GUID *guid, LPDIRECTDRAW *out, IUnknown *outer)
{
    AM2_DDraw *dd;

    (void)guid; (void)outer;
    dd = (AM2_DDraw *)calloc(1, sizeof *dd);
    if (!dd)
        return DDERR_OUTOFMEMORY;
    dd->lpVtbl = &am2_ddraw_vtbl;
    dd->lpVtbl2 = &am2_ddraw2_vtbl;
    dd->refs = 1;
    *out = (LPDIRECTDRAW)&dd->lpVtbl;
    return DD_OK;
}
