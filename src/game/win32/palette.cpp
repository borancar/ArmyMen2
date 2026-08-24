/* Display palette calibration -- reconstructed from ArmyMen2.exe.
 *
 *   CalibratePalette   0x0041AFC0   1 call site
 *
 * A Win32 boundary function: it is one of the few places the engine asks the
 * window system a question rather than telling it something. See palette.h for
 * what it measures and why.
 *
 * The colour matcher it calls, 0x0041B7C0, is left in the original image. It
 * has no imports and makes no COM calls -- it is a scan of 256 palette entries
 * for the smallest |dR| + |dG| + |dB|, which is pure arithmetic and not part of
 * the boundary this port is closing.
 *
 * STANDING NOTE -- the GetDC-failed path is a real defect, reproduced.
 *
 *   When GetDC returns NULL the original jumps over the measuring loop, but it
 *   jumps to a label that is *before* the final copy, not after it. So it
 *   still copies its 256-dword stack scratch buffer over the caller's palette,
 *   having never written a single entry of it. The caller gets whatever was on
 *   the stack.
 *
 *   That is reproduced below rather than fixed, as with the AddToItemList
 *   overflow probe and the LockSurface post-Restore publish. GetDC(NULL) on the
 *   screen DC does not realistically fail, which is presumably why it survived.
 *
 * One detail that looks like it matters and provably does not: the original
 * repacks the COLORREF byte by byte into a stack dword whose top byte it never
 * writes, so that byte holds a leftover -- the high byte of the HDC, which
 * occupied the same slot moments earlier. It is dead. The matcher at 0x0041B7C0
 * calls 0x0041B760, which reads bytes 0, 1 and 2 of each colour and nothing
 * else. So masking to 24 bits here is not a simplification of the original's
 * behaviour, it is the whole of it.
 */

#include "palette.h"
#include "../image.h"  /* AM2_IMAGE */
#include "../misc.h"   /* ColourDistance -- pure, so it lives in the flat half */
#include "surface.h"
#include "mapdraw.h"
#include "../../inject/patch.h"

#include <stdint.h>

/* The strip is drawn this far into the surface, on both axes. */
#define PROBE_ORIGIN 0x20
#define PALETTE_ENTRIES 256

#define g_primarySurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_frameBuf       (*(uint8_t **)(uintptr_t)ADDR_FRAMEBUFFER)
#define g_pitch          (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_originDx       (*(const int32_t *)(uintptr_t)ADDR_ORIGIN_DX)
#define g_originDy       (*(const int32_t *)(uintptr_t)ADDR_ORIGIN_DY)

/* 0x0041B7C0. Index of the palette entry nearest `rgb`, searching from
 * `from`. Returns 0 when `from` is already past the end. Not reconstructed --
 * pure colour arithmetic, no calls out of the process.
 *
 * cdecl, checked by hand rather than by tools/checkabi.py, which calls this one
 * thiscall: it opens with `push ecx` to allocate its single 4-byte local, and
 * that reads ecx. The stack offsets settle it -- [esp+0x10] with three pushes
 * down is the first argument (the palette base for `lea esi, [eax+ebx*4]`),
 * [esp+0x18] with four down is the second (its address goes to 0x0041B760),
 * [esp+0x14] with two down is the third (compared against 0x100), and it
 * returns `ret` with no immediate, so the caller cleans. */

void __cdecl CalibratePalette(uint32_t *palette)
{
    /* Deliberately uninitialised: the GetDC-failure path copies this over the
     * caller's palette without writing it, and that is what the original
     * does. See the standing note above. */
    uint32_t scratch[PALETTE_ENTRIES];
    int32_t  i;

    SetDrawTarget(g_primarySurface);
    if (!LockSurface(g_primarySurface))
        return;

    /* Paint index i at x = i, one row, so every entry is on screen at once. */
    {
        uint8_t *row = g_frameBuf + (int64_t)(g_originDy + PROBE_ORIGIN) * g_pitch;
        for (i = 0; i < PALETTE_ENTRIES; i++)
            row[g_originDx + i + PROBE_ORIGIN] = (uint8_t)i;
    }

    /* The strip has to be on the real display before GDI can be asked about
     * it, so the surface goes back to DirectDraw first. */
    UnlockSurface();

    {
        HDC dc = GetDC(NULL);

        if (dc) {
            for (i = 0; i < PALETTE_ENTRIES; i++) {
                COLORREF c = GetPixel(dc, g_originDx + i + PROBE_ORIGIN,
                                      g_originDy + PROBE_ORIGIN);
                /* COLORREF is already 0x00BBGGRR, which is the packing the
                 * matcher reads. */
                uint8_t idx = NearestPalIndex(palette,
                                                 (uint32_t)c & 0x00FFFFFFu, 0);

                scratch[i] = palette[idx];
                palette[PALETTE_ENTRIES + i] = (uint32_t)c;
            }
            ReleaseDC(NULL, dc);
        }
    }

    /* Reached whether or not the DC was had -- see the standing note. */
    for (i = 0; i < PALETTE_ENTRIES; i++)
        palette[i] = scratch[i];
}

/* ---- the GDI half ------------------------------------------------------ */

static_assert(SYSPAL_NOSTATIC == 2 && SYSPAL_STATIC == 1, "SYSPAL_*");
/* The original writes 4, which is PC_NOCOLLAPSE and not PC_RESERVED -- those
 * two are easy to transpose and mean different things. A static_assert on the
 * wrong one is what caught it. */
static_assert(PC_NOCOLLAPSE == 4, "PC_NOCOLLAPSE");
static_assert(sizeof(PALETTEENTRY) == 4, "PALETTEENTRY");

#define g_hWnd          (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_logPalette    ((LPLOGPALETTE)(uintptr_t)ADDR_LOGPALETTE)
/* Indexed past the LOGPALETTE header rather than through its one-element
 * array member, which is the same address without the out-of-bounds fiction. */
#define g_logEntries    ((LPPALETTEENTRY)(uintptr_t)ADDR_LOGPALETTE_ENTRIES)
#define g_systemPalette ((LPPALETTEENTRY)(uintptr_t)ADDR_SYSTEM_PALETTE)

/* Windows keeps twenty entries of the system palette for itself, ten at each
 * end, and an 8-bit application gets the 236 in between. */
#define SYSTEM_RESERVED 10

void __cdecl RealizeSystemPalette(const uint32_t *palette)
{
    HPALETTE hpal;
    int32_t  i;

    /* Toggling the system palette use to NOSTATIC and straight back is how the
     * static entries are made to let go: the first call releases them, the
     * second takes the default arrangement again, and the display driver
     * reloads its table in between. Neither call on its own does anything. */
    {
        HDC dc = GetDC(NULL);

        SetSystemPaletteUse(dc, SYSPAL_NOSTATIC);
        SetSystemPaletteUse(dc, SYSPAL_STATIC);
        ReleaseDC(NULL, dc);
    }

    /* The caller's entries are 0x00BBGGRR dwords, the same packing
     * CalibratePalette works in, so the three colour bytes transfer straight
     * across. palVersion and palNumEntries are already in the image. */
    for (i = 0; i < PALETTE_ENTRIES; i++) {
        const uint8_t *src = (const uint8_t *)&palette[i];

        g_logEntries[i].peRed   = src[0];
        g_logEntries[i].peGreen = src[1];
        g_logEntries[i].peBlue  = src[2];
        g_logEntries[i].peFlags = 0;
    }

    hpal = CreatePalette(g_logPalette);
    if (!hpal)
        return;

    {
        HDC      dc  = GetDC(NULL);
        HPALETTE old = SelectPalette(dc, hpal, FALSE);

        RealizePalette(dc);
        SelectPalette(dc, old, FALSE);
        DeleteObject(hpal);
        ReleaseDC(NULL, dc);
    }
}

void __cdecl SnapshotSystemPalette(void)
{
    HDC     dc = GetDC(g_hWnd);
    int32_t i;

    GetSystemPaletteEntries(dc, 0, PALETTE_ENTRIES, g_systemPalette);

    /* Mark which entries the game means to own outright. The twenty Windows
     * keeps are left at flags 0, so they map onto the system colours as usual;
     * everything between them is PC_NOCOLLAPSE, which tells GDI not to fold the
     * entry onto an existing colour but to give it a slot of its own. That is
     * what makes the game's 236 colours come out as asked for rather than
     * approximated to whatever is already realized. */
    for (i = 0; i < SYSTEM_RESERVED; i++)
        g_systemPalette[i].peFlags = 0;
    for (i = SYSTEM_RESERVED; i < PALETTE_ENTRIES - SYSTEM_RESERVED; i++)
        g_systemPalette[i].peFlags = PC_NOCOLLAPSE;
    for (i = PALETTE_ENTRIES - SYSTEM_RESERVED; i < PALETTE_ENTRIES; i++)
        g_systemPalette[i].peFlags = 0;

    ReleaseDC(g_hWnd, dc);
}

/* Install a palette and build every table derived from it -- 0x0041B0E0.
 *
 * THE LAST DIRECTX OBJECT THE PORT DID NOT CREATE. 0x0041B132 is the only
 * CreatePalette in the image, so while this stayed original the claim that
 * every DirectX object is made by reconstructed code was false -- the display
 * palette was the exception, and nobody had checked.
 *
 * Windowed and fullscreen ask for different things, which is the only part of
 * the DirectDraw section that is not boilerplate. Fullscreen passes the game's
 * own palette with DDPCAPS_ALLOW256, because it owns the display. Windowed
 * passes a fixed table and only DDPCAPS_8BIT, because the desktop owns the
 * palette and the game may not set 256 entries under it -- and windowed is also
 * the only path that then calls CalibratePalette to find out what actually
 * displays.
 *
 * Entry 0 is forced to black before anything else, so index 0 is dependably
 * transparent-black whatever the file said.
 *
 * The rest is table building, and it is the bulk of the function: seven remap
 * tables of 256 bytes and 23 named colour indices, all resolved through the
 * nearest-colour matcher. Nothing in it touches the outside world.
 *
 * THE ARITHMETIC IS x87 AND HAS TO STAY x87. The shade tables multiply an
 * integer channel by a double and truncate, and the original does that on the
 * FPU at 80-bit precision. i686 GCC evaluates double expressions the same way
 * by default, so `(uint8_t)(c * 0.7)` matches; it would not if this were built
 * with SSE math, where 0.7 * 100 rounds differently at the truncation boundary.
 * Recorded because nothing in the build makes that dependency visible.
 *
 * Verified where it is sharpest: it runs on every palette load, and the
 * windowed A/B has a budget of ZERO differing pixels. A wrong entry anywhere in
 * any of these tables shows up there immediately. */
static_assert(DDPCAPS_8BIT == 0x04, "DDPCAPS_8BIT");
static_assert(DDPCAPS_ALLOW256 == 0x40, "DDPCAPS_ALLOW256");

#define PALETTE_ENTRIES   256
#define REMAP_DARK_LIMIT  0x30   /* at or below this, darkening is a no-op */
#define REMAP_LIFT_LIMIT  0xA0   /* at or below this, brighten instead */
#define REMAP_LIFT        0x80
#define DARK_SCALE        0.7

#define g_ddraw        (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)
#define g_windowedMode (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)
typedef void (__cdecl *am2_realize_fn)(const uint32_t *palette);
#define g_remapIdent   (*(uint8_t **)(uintptr_t)ADDR_REMAP_IDENTITY)
#define g_remapDark    (*(uint8_t **)(uintptr_t)ADDR_REMAP_DARK)
#define g_remapBright  (*(uint8_t **)(uintptr_t)ADDR_REMAP_BRIGHT)
#define g_remapTint    (*(uint8_t **)(uintptr_t)ADDR_REMAP_TINT)
#define g_remapShades  ((uint8_t **)(uintptr_t)ADDR_REMAP_SHADES)

typedef void (__cdecl *am2_void_fn)(void);
#define orig_palette_loaded (*(am2_void_fn)ADDR_PALETTE_LOADED)

/* Was a private helper here with the reserved-block threshold hardcoded at 9.
 * 0x0041B820 is that function and takes the threshold as a fifth argument; the
 * 23 call sites in this file all pass 9. */
static uint8_t MatchRGB(const void *pal, uint32_t r, uint32_t g, uint32_t b)
{
    return NearestPalIndexRGB((const uint32_t *)pal, r, g, b, 9);
}

/* Every colour the engine looks up by name, in the original's order. */
static const struct { uint32_t addr, r, g, b; } kNamedColours[] = {
    { 0x00502AD9, 0x00, 0x00, 0x00 }, { 0x004FD768, 0xFF, 0xFF, 0xFF },
    { 0x004FE090, 0xC0, 0xC0, 0xC0 }, { 0x004FE089, 0x00, 0xFF, 0x00 },
    { 0x00507234, 0xDA, 0x4E, 0x00 }, { 0x004FDF7C, 0x00, 0x00, 0xFF },
    { 0x0050712C, 0xAC, 0xE8, 0x00 }, { 0x00502ACC, 0x88, 0x88, 0x88 },
    { 0x00502AD8, 0x00, 0x84, 0x00 }, { 0x00502CE5, 0xAC, 0x00, 0x00 },
    { 0x004FD760, 0x00, 0x00, 0xAC }, { 0x005022C0, 0x88, 0xC0, 0x00 },
    { 0x004FE092, 0xFF, 0xFF, 0x00 }, { 0x004FE091, 0x69, 0xA9, 0x52 },
    { 0x004FDF74, 0x32, 0x71, 0x26 }, { 0x004FDF75, 0xEF, 0xD9, 0xA0 },
    { 0x004FE1AE, 0x65, 0x57, 0x30 }, { 0x004FE1AC, 0x92, 0xB8, 0xDF },
    { 0x004FE088, 0x32, 0x5D, 0x8A }, { 0x004FE093, 0xAB, 0xAB, 0xAB },
    { 0x00502CE4, 0x54, 0x54, 0x54 }, { 0x004FE1AD, 0xFF, 0xFF, 0xFF },
    { 0x004FE094, 0x00, 0x00, 0x00 },
};

void __cdecl SetGamePalette(uint8_t *pal)
{
    static const double kShade[4] = { 0.4, 0.5, 0.6, 0.85 };
    LPDIRECTDRAWPALETTE *held;
    uint32_t             i, k;

    if (!pal)
        return;

    ReleasePalette(pal);
    pal[0] = pal[1] = pal[2] = 0;
    RealizeSystemPalette((const uint32_t *)pal);

    held = (LPDIRECTDRAWPALETTE *)(pal + PALETTE_HOLDER_OFF);
    if (g_windowedMode)
        IDirectDraw_CreatePalette(g_ddraw, DDPCAPS_8BIT,
                                  (LPPALETTEENTRY)(uintptr_t)ADDR_GDI_PALETTE,
                                  held, NULL);
    else
        IDirectDraw_CreatePalette(g_ddraw, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
                                  (LPPALETTEENTRY)pal, held, NULL);

    IDirectDrawSurface_SetPalette(g_primarySurface, *held);

    memcpy((void *)(uintptr_t)ADDR_PALETTE_COPY, pal, 0x201 * 4);

    if (g_windowedMode)
        CalibratePalette((uint32_t *)pal);

    /* Four 256-byte blocks, back to back. */
    g_remapShades[0] = (uint8_t *)(uintptr_t)ADDR_REMAP_SHADE_STORE;
    for (k = 1; k < 4; k++)
        g_remapShades[k] = g_remapShades[k - 1] + PALETTE_ENTRIES;

    for (i = 0; i < PALETTE_ENTRIES; i++) {
        uint32_t r = pal[i * 4 + 0], g = pal[i * 4 + 1], b = pal[i * 4 + 2];
        uint8_t  cr, cg, cb;

        g_remapIdent[i] = (uint8_t)i;

        /* Already dark enough that 70% would not change the match. */
        if (r <= REMAP_DARK_LIMIT && g <= REMAP_DARK_LIMIT
            && b <= REMAP_DARK_LIMIT)
            g_remapDark[i] = (uint8_t)i;
        else
            g_remapDark[i] = MatchRGB(pal, (uint32_t)(r * DARK_SCALE),
                                      (uint32_t)(g * DARK_SCALE),
                                      (uint32_t)(b * DARK_SCALE));

        /* Dark colours are lifted, light ones darkened -- one table doing
         * "make this stand out" in both directions. */
        if (r <= REMAP_LIFT_LIMIT && g <= REMAP_LIFT_LIMIT
            && b <= REMAP_LIFT_LIMIT) {
            cr = (r + REMAP_LIFT < 0xFF) ? (uint8_t)(r + REMAP_LIFT) : 0xFF;
            cg = (g + REMAP_LIFT < 0xFF) ? (uint8_t)(g + REMAP_LIFT) : 0xFF;
            cb = (b + REMAP_LIFT < 0xFF) ? (uint8_t)(b + REMAP_LIFT) : 0xFF;
        } else {
            cr = (uint8_t)(r * DARK_SCALE);
            cg = (uint8_t)(g * DARK_SCALE);
            cb = (uint8_t)(b * DARK_SCALE);
        }
        g_remapBright[i] = MatchRGB(pal, cr, cg, cb);

        /* Two colours by parity, so a run of indices alternates. */
        g_remapTint[i] = (i & 1) ? MatchRGB(pal, 0xD6, 0xEF, 0x29)
                                 : MatchRGB(pal, 0xFF, 0xFF, 0x00);

        for (k = 0; k < 4; k++)
            g_remapShades[k][i] = MatchRGB(pal, (uint32_t)(r * kShade[k]),
                                           (uint32_t)(g * kShade[k]),
                                           (uint32_t)(b * kShade[k]));
    }

    for (i = 0; i < sizeof kNamedColours / sizeof kNamedColours[0]; i++)
        *(uint8_t *)(uintptr_t)kNamedColours[i].addr =
            MatchRGB(pal, kNamedColours[i].r, kNamedColours[i].g,
                     kNamedColours[i].b);

    orig_palette_loaded();
}

/* 0x0041B7C0, 11 call sites. The palette entry nearest a colour, searching
 * from `from` upward.
 *
 * `from` exists because the caller may be protecting the low entries -- the
 * tileset loader keeps the first block identity-mapped and only remaps above
 * it -- so this is not "nearest in the palette" but "nearest among the ones
 * you are allowed to use".
 *
 * Two details are the original's rather than choices. `from` is masked to a
 * byte before the range check, so a value of 0x100 or more searches nothing
 * and answers 0 rather than reading off the end. And the running best starts
 * at a sentinel larger than any real distance, so an all-equal palette
 * answers `from` -- the comparison is strict. */
uint8_t __cdecl NearestPalIndex(const uint32_t *palette, uint32_t colour,
                                uint32_t from)
{
    uint32_t i    = from & 0xFFu;
    int32_t  best = AM2_COLOUR_DIST_MAX;
    uint8_t  hit  = 0;

    for (; i < 256; i++) {
        int32_t d = ColourDistance(&palette[i], &colour);

        if (d < best) {
            best = d;
            hit  = (uint8_t)i;
        }
    }
    return hit;
}

uint8_t __cdecl NearestPalIndexRGB(const uint32_t *pal, uint32_t r, uint32_t g,
                                   uint32_t b, uint32_t from)
{
    return NearestPalIndex(pal, (r & 0xFFu) | ((g & 0xFFu) << 8)
                                | ((b & 0xFFu) << 16), from);
}

/* 0x00422F60 -- read an 8-bit .bmp's HEADER and palette and nothing else.
 *
 * The file header is read into a local and discarded: only its 14 bytes of
 * position matter. The info header goes to the caller, and anything that is
 * not 8 bits per pixel is rejected -- this is a palette reader, not a bitmap
 * loader, and the pixels are never touched.
 *
 * biClrUsed of zero means 256, which is what the format says and what the
 * original writes back into the caller's header before using it. */
int32_t __cdecl ReadBitmapPalette(const char *path, BITMAPINFO *out)
{
    BITMAPFILEHEADER  fh;
    am2_FILE         *fp;

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp)
        return 0;

    orig_fread(&fh, sizeof(fh), 1, fp);
    orig_fread(&out->bmiHeader, sizeof(out->bmiHeader), 1, fp);

    if (out->bmiHeader.biBitCount != 8) {
        orig_fclose(fp);
        return 0;
    }

    if (out->bmiHeader.biClrUsed == 0)
        out->bmiHeader.biClrUsed = AM2_PALETTE_ENTRIES;

    orig_fread(out->bmiColors, 4, out->bmiHeader.biClrUsed, fp);
    orig_fclose(fp);
    return 1;
}

/* 0x0041AEB0 -- expand a file palette into the pair of tables the renderer
 * wants: 256 swapped colours, and 256 more 0x400 bytes behind them.
 *
 * **The two tables come out IDENTICAL**, and that is not a misreading. The
 * second is built by reading the first back byte by byte and dropping the top
 * one -- but SwapColourBytes has already zeroed that byte, so the mask
 * removes nothing. What the second table is FOR is the copy that survives:
 * the first is the working palette and gets written over, and this is the
 * pristine one to restore from.
 *
 * The count is a fixed 256 and does NOT consult biClrUsed, so a file that
 * declared fewer entries still expands whatever the reader left behind them.
 *
 * SwapColourBytes takes a second argument that it never reads, and the
 * original pushes only one; zero here, with the same effect. */
void __cdecl ExpandPalette(void *dst, const BITMAPINFO *bi)
{
    uint32_t       *working  = (uint32_t *)dst;
    uint32_t       *pristine = (uint32_t *)((uint8_t *)dst
                                            + AM2_PALETTE_PRISTINE);
    const uint32_t *src = (const uint32_t *)bi->bmiColors;
    int32_t         i;

    for (i = 0; i < AM2_PALETTE_ENTRIES; i++) {
        const uint8_t *b;

        working[i] = SwapColourBytes(src[i], 0);

        /* Read back through bytes, as the original does. */
        b = (const uint8_t *)&working[i];
        pristine[i] = (uint32_t)b[0]
                    | ((uint32_t)b[1] << 8)
                    | ((uint32_t)b[2] << 16);
    }
}

/* 0x0041B6D0, ten callers -- the pair, and the only one of the three anything
 * else calls. Nothing is written to `dst` when the file will not open or is
 * not 8-bit. */
int32_t __cdecl LoadPaletteFile(const char *path, void *dst)
{
    uint8_t buf[sizeof(BITMAPINFOHEADER) + AM2_PALETTE_ENTRIES * 4];

    if (!ReadBitmapPalette(path, (BITMAPINFO *)buf))
        return 0;

    ExpandPalette(dst, (const BITMAPINFO *)buf);
    return 1;
}

int palette_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_NEAREST_PAL_RGB, (const void *)NearestPalIndexRGB,
                        "NearestPalIndexRGB", 5);
    rc |= patch_replace(ADDR_NEAREST_PAL_INDEX, (const void *)NearestPalIndex,
                        "NearestPalIndex", 11);

    rc |= patch_replace(ADDR_SET_GAME_PALETTE, (const void *)SetGamePalette,
                        "SetGamePalette", 1);

    rc |= patch_replace(ADDR_CALIBRATE_PALETTE, (const void *)CalibratePalette,
                        "CalibratePalette", 1);
    rc |= patch_replace(ADDR_REALIZE_PALETTE, (const void *)RealizeSystemPalette,
                        "RealizeSystemPalette", 1);
    rc |= patch_replace(ADDR_SNAPSHOT_PALETTE, (const void *)SnapshotSystemPalette,
                        "SnapshotSystemPalette", 0);
    rc |= patch_replace(ADDR_READ_BMP_PALETTE, (const void *)ReadBitmapPalette,
                        "ReadBitmapPalette", 1);
    rc |= patch_replace(ADDR_EXPAND_PALETTE, (const void *)ExpandPalette,
                        "ExpandPalette", 1);
    rc |= patch_replace(ADDR_LOAD_PALETTE_FILE, (const void *)LoadPaletteFile,
                        "LoadPaletteFile", 10);
    return rc;
}
