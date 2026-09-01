#ifndef AM2_PALETTE_H
#define AM2_PALETTE_H

#include <stdint.h>
#include "../../inject/win32.h"  /* BITMAPINFO, for the palette reader */
#include "../../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Display palette calibration -- one of the Win32 boundary functions.
 *
 * The game runs in an 8-bit paletted mode, and cannot assume the colour it
 * asks for is the colour that appears: the display driver, or Windows' own
 * reserved system entries, may substitute something else. So it measures.
 *
 * CalibratePalette paints a 256-pixel strip of every palette index onto the
 * primary surface, then reads those pixels straight back with GDI's GetPixel
 * -- going out through the window system and back -- to learn what each index
 * actually renders as. Each measured colour is matched against the caller's
 * palette, and the palette is rewritten so that entry i holds the colour that
 * will really appear when i is drawn.
 *
 * The caller's table is 512 dwords, in two halves:
 *
 *     [0  .. 255]   in: the wanted palette, packed 0x00BBGGRR
 *                   out: the same entries, corrected to what displays
 *     [256..511]    out: the raw COLORREF GetPixel returned for each index
 *
 * The strip is drawn at (0x20, 0x20) in surface coordinates, offset by the
 * primary surface origin, so it lands on screen and is overwritten by the next
 * repaint. This is measurement by side effect, and it is why the routine is
 * only ever called during mode setup.
 */
void __cdecl CalibratePalette(uint32_t *palette);

/* Original: 0x0041AF00. Hand a 256-entry palette to GDI and make it current.
 *
 * The densest boundary function in the game -- nine import sites in 192 bytes,
 * and nothing else. It copies the caller's 0x00BBGGRR entries into a LOGPALETTE
 * that lives in the image, creates a palette from it, selects it into the
 * screen DC long enough to realize it, and throws it away again.
 *
 * The pair of SetSystemPaletteUse calls at the top is not redundant. Setting
 * NOSTATIC and immediately STATIC again is the documented way to make the
 * driver release and reload its static entries; either call on its own is a
 * no-op. */
void __cdecl RealizeSystemPalette(const uint32_t *palette);

/* Original: 0x00445170. Read the system palette back and mark what is ours.
 *
 * Windows reserves twenty entries, ten at each end. This reads the current 256
 * into the game's own table and then flags the 236 in between PC_NOCOLLAPSE,
 * which tells GDI to give each one a palette slot of its own instead of folding
 * it onto the nearest colour already realized. The twenty at the ends keep
 * flags of 0 and map onto the system colours as usual. */
void __cdecl SnapshotSystemPalette(void);

/* Original: 0x0041B0E0, 5 call sites. Install a palette: create the
 * DirectDraw palette, attach it to the primary, and rebuild the seven remap
 * tables and 23 named colour indices derived from it.
 *
 * The image's only CreatePalette is in here. */
void __cdecl SetGamePalette(uint8_t *palette);

/* 0x0041B7C0. The palette entry nearest `colour`, searching from `from` up --
 * callers use that to protect a reserved block of low entries. `from` is
 * masked to a byte, so 0x100 or more searches nothing and answers 0. */
uint8_t __cdecl NearestPalIndex(const uint32_t *palette, uint32_t colour,
                                uint32_t from);

/* 0x0041B820, 25 callers. NearestPalIndex with the three channels apart. The
 * original packs them into its own first argument slot and passes the dword,
 * so the top byte is whatever the red argument's byte 3 was -- stale, and
 * harmless, because the matcher masks. */
uint8_t __cdecl NearestPalIndexRGB(const uint32_t *pal, uint32_t r, uint32_t g,
                                   uint32_t b, uint32_t from);

/* 0x0040A4B0, one caller. Build every palette remap the game owns: the four
 * army tables at ADDR_OBJ_TABLE_RECORDS, four more ramps beside them, sixty-
 * four randomised variation blocks, and the grey ramp at
 * ADDR_ROW_LUT_DOUBLES. Every one is the identity above AM2_REMAP_COLOURS. */
void __cdecl BuildRemapTables(void);

/* 0x0041B6D0 and the two halves under it. Read an 8-bit .bmp's palette and
 * expand it into the renderer's pair of 256-entry tables. */
int32_t __cdecl ReadBitmapPalette(const char *path, BITMAPINFO *out);
void    __cdecl ExpandPalette(void *dst, const BITMAPINFO *bi);
int32_t __cdecl LoadPaletteFile(const char *path, void *dst);

/* 0x00423C50. Rebuild all three sprite sets' remap tables against the palette
 * now in force -- and the reserved-block copy beside each. */
void __cdecl PaletteLoaded(void);

/* 0x0042B1A0, one caller -- RefreshDraw, once a frame. Animates the eight
 * reserved palette entries by re-uploading one of six tileset palettes on a
 * 160 ms timer, stepping a ping-pong sequence. See the body for the two
 * comparisons and why their signednesses differ. */
void __cdecl CyclePalette(void);

int palette_install(void);

#ifdef __cplusplus
}
#endif

/* 0x0042B120, one caller. Load palette0.bmp .. palette5.bmp into the six
 * tileset palettes, after chdir'ing into the map's own directory. */
void __cdecl LoadTilesetPalettes(void);

#endif /* AM2_PALETTE_H */
