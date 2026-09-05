/* gdi32.cpp -- device contexts, palettes and fonts.
 *
 * A DC draws on an 8-bit surface: the one a DirectDraw surface's GetDC
 * names, or the primary for GetDC(hwnd). Text goes through stb_truetype
 * onto that surface, thresholded to one palette index, which is what GDI
 * produced on an 8-bit DirectDraw surface and what the game's EncodeGlyph
 * reads back -- it treats anything that is not the background key as ink.
 *
 * The game's three fonts are ArialNarrow at 12 and 14 and ArialBlack at
 * 18. Neither ships with a Linux desktop; Liberation Sans is metric-
 * compatible with Arial, and the narrow face is Liberation Sans drawn at
 * 82% width, which is what Arial Narrow is to Arial. AM2_FONT_NARROW,
 * AM2_FONT_BLACK and AM2_FONT_DEFAULT name TTF files to use instead.
 */
#include "platform.h"

/* Vendored as-is from https://github.com/nothings/stb (public domain / MIT).
 * Its unused static helpers are not this file's to prune. */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb/stb_truetype.h"
#pragma GCC diagnostic pop

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ---- GDI objects ---------------------------------------------------------------- */

enum { AM2_OBJ_FONT = 0x464F4E54, AM2_OBJ_PALETTE = 0x50414C45 };

typedef struct AM2_FontFile {
    char     path[512];
    uint8_t *data;
    int32_t  bytes;
    struct AM2_FontFile *next;
} AM2_FontFile;

/* GDI's own rendering of the game's three fonts, read out of the original
 * under Wine by tools/glyphdump.py: one bit per pixel, the cell TextOutA
 * painted and GetTextExtentPoint32A measured. A CreateFontA naming one of
 * those faces at that height gets these back rather than stb_truetype's
 * rasterisation, which differs from FreeType's in about 1,400 pixels of a
 * 640x480 dialog. The game builds its glyph tables from what TextOutA
 * draws, so this is what makes its text come out as the original's. */
typedef struct AM2_GlyphFont {
    const char *face;
    int32_t     height;
    int32_t     style;
    int32_t     first, count;   /* into am2_glyphs */
} AM2_GlyphFont;

typedef struct AM2_Glyph {
    int32_t  ch, width, height;
    int32_t  bits;              /* into am2_glyph_bits, (width+7)/8 bytes a row */
} AM2_Glyph;

#include "glyphs.inc"

typedef struct AM2_Font {
    int32_t        magic;
    stbtt_fontinfo info;
    float          scale;       /* font units to pixels, vertically */
    float          xscale;      /* extra horizontal condensing */
    int32_t        ascent, descent, lineGap;
    int32_t        height;      /* cell height in pixels */
    const AM2_GlyphFont *recorded;  /* Wine's bitmaps for this face, or NULL */
} AM2_Font;

static const AM2_GlyphFont *am2_glyph_font_for(const char *face, int32_t height,
                                               int32_t style)
{
    size_t i;

    if (getenv("AM2_FONT_NORECORD"))
        return NULL;
    for (i = 0; i < sizeof am2_glyph_fonts / sizeof am2_glyph_fonts[0]; i++)
        if (face && !strcasecmp(am2_glyph_fonts[i].face, face) &&
            am2_glyph_fonts[i].height == height && am2_glyph_fonts[i].style == style)
            return &am2_glyph_fonts[i];
    return NULL;
}

static const AM2_Glyph *am2_glyph_of(const AM2_GlyphFont *gf, int32_t ch)
{
    int32_t i;

    for (i = 0; i < gf->count; i++)
        if (am2_glyphs[gf->first + i].ch == ch)
            return &am2_glyphs[gf->first + i];
    return NULL;
}

typedef struct AM2_Palette {
    int32_t      magic;
    int32_t      count;
    PALETTEENTRY entries[256];
} AM2_Palette;

typedef struct AM2_DC {
    AM2_DCTarget target;
    AM2_Font    *font;
    AM2_Palette *palette;
    COLORREF     textColour;
    int32_t      bkMode;
    int32_t      isScreen;
} AM2_DC;

PALETTEENTRY am2_screen_palette[256];

static AM2_FontFile *am2_font_files;

/* ---- device contexts ------------------------------------------------------------------- */

HDC am2_dc_create(const AM2_DCTarget *target)
{
    AM2_DC *dc = (AM2_DC *)calloc(1, sizeof *dc);

    if (!dc)
        return NULL;
    if (target)
        dc->target = *target;
    dc->textColour = 0;
    dc->bkMode = OPAQUE;
    return (HDC)dc;
}

void am2_dc_destroy(HDC dc)
{
    free(dc);
}

HDC WINAPI GetDC(HWND hwnd)
{
    AM2_DCTarget t;
    AM2_DC      *dc;

    (void)hwnd;
    memset(&t, 0, sizeof t);
    am2_ddraw_primary_target(&t);
    dc = (AM2_DC *)am2_dc_create(&t);
    if (dc)
        dc->isScreen = 1;
    return (HDC)dc;
}

int32_t WINAPI ReleaseDC(HWND hwnd, HDC dc)
{
    AM2_DC *d = (AM2_DC *)dc;

    (void)hwnd;
    if (!d)
        return 0;
    if (d->target.changed)
        d->target.changed();
    am2_dc_destroy(dc);
    return 1;
}

int32_t WINAPI GetDeviceCaps(HDC dc, int32_t index)
{
    (void)dc;
    switch (index) {
    case BITSPIXEL:   return 8;
    case PLANES:      return 1;
    case SIZEPALETTE: return 256;
    case RASTERCAPS:  return RC_PALETTE;
    }
    return 0;
}

static const PALETTEENTRY *am2_dc_palette(const AM2_DC *dc)
{
    return dc->target.palette ? dc->target.palette : am2_screen_palette;
}

COLORREF WINAPI GetPixel(HDC dc, int32_t x, int32_t y)
{
    AM2_DC *d = (AM2_DC *)dc;
    const PALETTEENTRY *pal;
    uint8_t idx;

    if (!d || !d->target.pixels || x < 0 || y < 0 ||
        x >= d->target.width || y >= d->target.height)
        return CLR_INVALID;
    idx = d->target.pixels[y * d->target.pitch + x];
    pal = am2_dc_palette(d);
    return RGB(pal[idx].peRed, pal[idx].peGreen, pal[idx].peBlue);
}

/* ---- palettes ------------------------------------------------------------------------ */

uint8_t am2_palette_nearest(const PALETTEENTRY *pal, uint8_t r, uint8_t g, uint8_t b)
{
    int32_t best = 0, bestDist = 0x7FFFFFFF, i;

    for (i = 0; i < 256; i++) {
        int32_t dr = (int32_t)pal[i].peRed - r;
        int32_t dg = (int32_t)pal[i].peGreen - g;
        int32_t db = (int32_t)pal[i].peBlue - b;
        int32_t dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
            if (dist == 0)
                break;
        }
    }
    return (uint8_t)best;
}

HPALETTE WINAPI CreatePalette(const LOGPALETTE *lp)
{
    AM2_Palette *p;

    if (!lp || lp->palNumEntries == 0 || lp->palNumEntries > 256)
        return NULL;
    p = (AM2_Palette *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->magic = AM2_OBJ_PALETTE;
    p->count = lp->palNumEntries;
    memcpy(p->entries, lp->palPalEntry, (size_t)p->count * sizeof(PALETTEENTRY));
    return (HPALETTE)p;
}

HPALETTE WINAPI SelectPalette(HDC dc, HPALETTE pal, BOOL forceBackground)
{
    AM2_DC      *d = (AM2_DC *)dc;
    AM2_Palette *p = (AM2_Palette *)pal;
    HPALETTE     old;

    (void)forceBackground;
    if (!d)
        return NULL;
    old = (HPALETTE)d->palette;
    if (p && p->magic == AM2_OBJ_PALETTE)
        d->palette = p;
    else
        d->palette = NULL;
    return old;
}

/* Realising a palette on the screen DC makes it the system palette, which
 * is what the presented frame is looked up through. The PC_ flags are
 * accepted and ignored: nothing else shares this display. */
UINT WINAPI RealizePalette(HDC dc)
{
    AM2_DC *d = (AM2_DC *)dc;
    int32_t i;

    if (!d || !d->palette)
        return 0;
    am2_plat_debug("RealizePalette: %d entries", d->palette->count);
    for (i = 0; i < d->palette->count; i++) {
        am2_screen_palette[i] = d->palette->entries[i];
        am2_screen_palette[i].peFlags = 0;
    }
    if (d->target.changed)
        d->target.changed();
    return (UINT)d->palette->count;
}

UINT WINAPI SetSystemPaletteUse(HDC dc, UINT use)
{
    (void)dc; (void)use;
    return SYSPAL_STATIC;
}

UINT WINAPI GetSystemPaletteEntries(HDC dc, UINT start, UINT count, LPPALETTEENTRY out)
{
    UINT i;

    (void)dc;
    if (start >= 256)
        return 0;
    if (start + count > 256)
        count = 256 - start;
    if (out)
        for (i = 0; i < count; i++)
            out[i] = am2_screen_palette[start + i];
    return count;
}

/* ---- fonts ----------------------------------------------------------------------------- */

static const AM2_FontFile *am2_font_file(const char *path)
{
    AM2_FontFile *f;
    FILE         *fh;
    long          n;

    for (f = am2_font_files; f; f = f->next)
        if (!strcmp(f->path, path))
            return f;
    fh = fopen(path, "rb");
    if (!fh)
        return NULL;
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    f = (AM2_FontFile *)calloc(1, sizeof *f);
    if (!f || n <= 0) {
        fclose(fh);
        free(f);
        return NULL;
    }
    f->data = (uint8_t *)malloc((size_t)n);
    if (!f->data || fread(f->data, 1, (size_t)n, fh) != (size_t)n) {
        fclose(fh);
        free(f->data);
        free(f);
        return NULL;
    }
    fclose(fh);
    f->bytes = (int32_t)n;
    strncpy(f->path, path, sizeof f->path - 1);
    f->next = am2_font_files;
    am2_font_files = f;
    return f;
}

/* The first of a list of candidate files that exists. */
static const AM2_FontFile *am2_font_first(const char *const *candidates)
{
    for (; *candidates; candidates++) {
        const AM2_FontFile *f = am2_font_file(*candidates);
        if (f)
            return f;
    }
    return NULL;
}

static const char *const am2_fonts_regular[] = {
    "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    NULL
};
static const char *const am2_fonts_italic[] = {
    "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Italic.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Italic.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Oblique.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
    NULL
};
static const char *const am2_fonts_bold[] = {
    "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    NULL
};

HFONT WINAPI CreateFontA(int32_t height, int32_t width, int32_t escapement,
                         int32_t orientation, int32_t weight, DWORD italic,
                         DWORD underline, DWORD strikeout, DWORD charset,
                         DWORD outPrecision, DWORD clipPrecision,
                         DWORD quality, DWORD pitchAndFamily, LPCSTR face)
{
    const AM2_FontFile *file = NULL;
    const char         *env = NULL;
    AM2_Font           *font;
    char                lower[64];
    int32_t             i, narrow = 0, black = 0;

    (void)width; (void)escapement; (void)orientation; (void)underline;
    (void)strikeout; (void)charset; (void)outPrecision; (void)clipPrecision;
    (void)quality; (void)pitchAndFamily;

    for (i = 0; face && face[i] && i < 63; i++)
        lower[i] = (char)tolower((unsigned char)face[i]);
    lower[i] = 0;
    narrow = strstr(lower, "narrow") != NULL;
    black = strstr(lower, "black") != NULL || weight >= FW_BOLD;

    env = narrow ? getenv("AM2_FONT_NARROW")
        : black  ? getenv("AM2_FONT_BLACK")
        :          getenv("AM2_FONT_DEFAULT");
    if (env && *env)
        file = am2_font_file(env);
    if (!file)
        file = am2_font_first(black ? am2_fonts_bold
                              : italic ? am2_fonts_italic
                              : am2_fonts_regular);
    if (!file)
        file = am2_font_first(am2_fonts_regular);
    if (!file) {
        am2_plat_log("no TrueType font found for '%s'; set AM2_FONT_DEFAULT",
                     face ? face : "");
        return NULL;
    }

    font = (AM2_Font *)calloc(1, sizeof *font);
    if (!font)
        return NULL;
    font->magic = AM2_OBJ_FONT;
    if (!stbtt_InitFont(&font->info, file->data,
                        stbtt_GetFontOffsetForIndex(file->data, 0))) {
        free(font);
        return NULL;
    }
    stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->lineGap);
    /* A positive height is the cell height, ascent plus descent; a negative
     * one is the em height. The cell is what GDI's tmHeight reports and
     * what GetTextExtentPoint32 answers as cy. */
    if (height > 0) {
        font->scale = stbtt_ScaleForPixelHeight(&font->info, (float)height);
        font->height = height;
    } else {
        font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, (float)-height);
        font->height = (int32_t)ceilf((font->ascent - font->descent) * font->scale);
    }
    font->xscale = narrow ? 0.82f : 1.0f;
    font->recorded = am2_glyph_font_for(face, height,
                                        (italic ? 1 : 0) | (underline ? 2 : 0) | (strikeout ? 4 : 0));
    return (HFONT)font;
}

/* The recorded cell of a string: widths summed, the tallest height. */
static int32_t am2_recorded_extent(const AM2_GlyphFont *gf, LPCSTR text, int32_t n, LPSIZE out)
{
    int32_t i, w = 0, h = 0;

    for (i = 0; i < n; i++) {
        const AM2_Glyph *g = am2_glyph_of(gf, (uint8_t)text[i]);
        if (!g)
            return 0;
        w += g->width;
        if (g->height > h)
            h = g->height;
    }
    out->cx = w;
    out->cy = h;
    return 1;
}

HGDIOBJ WINAPI SelectObject(HDC dc, HGDIOBJ obj)
{
    AM2_DC  *d = (AM2_DC *)dc;
    int32_t *magic = (int32_t *)obj;
    HGDIOBJ  old = NULL;

    if (!d || !magic)
        return NULL;
    if (*magic == AM2_OBJ_FONT) {
        old = (HGDIOBJ)d->font;
        d->font = (AM2_Font *)obj;
    }
    return old;
}

BOOL WINAPI DeleteObject(HGDIOBJ obj)
{
    int32_t *magic = (int32_t *)obj;

    if (!magic)
        return FALSE;
    if (*magic == AM2_OBJ_FONT || *magic == AM2_OBJ_PALETTE) {
        *magic = 0;
        free(obj);
        return TRUE;
    }
    return FALSE;
}

int32_t WINAPI SetBkMode(HDC dc, int32_t mode)
{
    AM2_DC *d = (AM2_DC *)dc;
    int32_t old;

    if (!d)
        return 0;
    old = d->bkMode;
    d->bkMode = mode;
    return old;
}

COLORREF WINAPI SetTextColor(HDC dc, COLORREF colour)
{
    AM2_DC  *d = (AM2_DC *)dc;
    COLORREF old;

    if (!d)
        return CLR_INVALID;
    old = d->textColour;
    d->textColour = colour;
    return old;
}

/* Advance widths summed in font units, then scaled once, so a string's
 * width is the same whether measured or drawn. */
static float am2_text_width(const AM2_Font *f, LPCSTR text, int32_t n)
{
    int32_t i, total = 0;

    for (i = 0; i < n; i++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f->info, (uint8_t)text[i], &adv, &lsb);
        total += adv;
    }
    return (float)total * f->scale * f->xscale;
}

BOOL WINAPI GetTextExtentPoint32A(HDC dc, LPCSTR text, int32_t n, LPSIZE out)
{
    AM2_DC *d = (AM2_DC *)dc;

    if (!d || !out)
        return FALSE;
    if (!d->font) {
        out->cx = n * 8;
        out->cy = 16;
        return TRUE;
    }
    if (d->font->recorded && am2_recorded_extent(d->font->recorded, text, n, out))
        return TRUE;
    out->cx = (LONG)ceilf(am2_text_width(d->font, text, n));
    out->cy = d->font->height;
    return TRUE;
}

BOOL WINAPI TextOutA(HDC dc, int32_t x, int32_t y, LPCSTR text, int32_t n)
{
    AM2_DC   *d = (AM2_DC *)dc;
    AM2_Font *f;
    const PALETTEENTRY *pal;
    uint8_t   ink, paper;
    float     pen;
    int32_t   i, baseline;
    uint8_t  *glyph = NULL;
    int32_t   glyphCap = 0;

    if (!d || !text)
        return FALSE;
    f = d->font;
    if (!f || !d->target.pixels)
        return TRUE;

    pal = am2_dc_palette(d);
    ink = am2_palette_nearest(pal, GetRValue(d->textColour),
                              GetGValue(d->textColour), GetBValue(d->textColour));
    paper = 0;
    baseline = y + (int32_t)(f->ascent * f->scale + 0.5f);

    /* Wine's bitmaps, when this face was recorded: each cell painted at the
     * pen with its top-left at y, which is where the game reads it back
     * from. Only when every character is on record; a string with one
     * missing falls through to the rasteriser whole. */
    if (f->recorded) {
        SIZE    cell;
        int32_t px = x;
        if (am2_recorded_extent(f->recorded, text, n, &cell)) {
            if (d->bkMode == OPAQUE) {
                int32_t cx, cy;
                for (cy = y; cy < y + cell.cy; cy++)
                    for (cx = x; cx < x + cell.cx; cx++)
                        if (cy >= 0 && cy < d->target.height && cx >= 0 && cx < d->target.width)
                            d->target.pixels[cy * d->target.pitch + cx] = paper;
            }
            for (i = 0; i < n; i++) {
                const AM2_Glyph *g = am2_glyph_of(f->recorded, (uint8_t)text[i]);
                int32_t stride = (g->width + 7) / 8, gx, gy;
                for (gy = 0; gy < g->height; gy++) {
                    int32_t py = y + gy;
                    const uint8_t *row = am2_glyph_bits + g->bits + gy * stride;
                    if (py < 0 || py >= d->target.height)
                        continue;
                    for (gx = 0; gx < g->width; gx++) {
                        int32_t cx = px + gx;
                        if (cx < 0 || cx >= d->target.width)
                            continue;
                        if (row[gx >> 3] & (0x80 >> (gx & 7)))
                            d->target.pixels[py * d->target.pitch + cx] = ink;
                    }
                }
                px += g->width;
            }
            return TRUE;
        }
    }

    if (d->bkMode == OPAQUE) {
        int32_t w = (int32_t)ceilf(am2_text_width(f, text, n)), cx, cy;
        for (cy = y; cy < y + f->height; cy++) {
            if (cy < 0 || cy >= d->target.height)
                continue;
            for (cx = x; cx < x + w; cx++)
                if (cx >= 0 && cx < d->target.width)
                    d->target.pixels[cy * d->target.pitch + cx] = paper;
        }
    }

    pen = (float)x;
    for (i = 0; i < n; i++) {
        int      cp = (uint8_t)text[i];
        int      g = stbtt_FindGlyphIndex(&f->info, cp);
        int      adv, lsb, x0, y0, x1, y1, gw, gh, gx, gy;
        float    xs = f->scale * f->xscale;
        int32_t  ox, oy;

        stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
        stbtt_GetGlyphBitmapBox(&f->info, g, xs, f->scale, &x0, &y0, &x1, &y1);
        gw = x1 - x0;
        gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            if (gw * gh > glyphCap) {
                glyphCap = gw * gh;
                glyph = (uint8_t *)realloc(glyph, (size_t)glyphCap);
            }
            stbtt_MakeGlyphBitmap(&f->info, glyph, gw, gh, gw, xs, f->scale, g);
            ox = (int32_t)floorf(pen + 0.5f) + x0;
            oy = baseline + y0;
            for (gy = 0; gy < gh; gy++) {
                int32_t py = oy + gy;
                if (py < 0 || py >= d->target.height)
                    continue;
                for (gx = 0; gx < gw; gx++) {
                    int32_t px = ox + gx;
                    if (px < 0 || px >= d->target.width)
                        continue;
                    if (glyph[gy * gw + gx] >= 128)
                        d->target.pixels[py * d->target.pitch + px] = ink;
                }
            }
        }
        pen += (float)adv * xs;
    }
    free(glyph);
    return TRUE;
}
