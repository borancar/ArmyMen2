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
#include "surface.h"
#include "mapdraw.h"
#include "../inject/patch.h"

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
typedef uint8_t (__cdecl *am2_nearest_index_fn)(const uint32_t *palette,
                                                uint32_t rgb, uint32_t from);
#define orig_nearest_index (*(am2_nearest_index_fn)ADDR_NEAREST_PAL_INDEX)

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
                uint8_t idx = orig_nearest_index(palette,
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

int palette_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_CALIBRATE_PALETTE, (const void *)CalibratePalette,
                        "CalibratePalette", 1);
    rc |= patch_replace(ADDR_REALIZE_PALETTE, (const void *)RealizeSystemPalette,
                        "RealizeSystemPalette", 1);
    rc |= patch_replace(ADDR_SNAPSHOT_PALETTE, (const void *)SnapshotSystemPalette,
                        "SnapshotSystemPalette", 0);
    return rc;
}
