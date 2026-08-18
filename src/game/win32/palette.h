#ifndef AM2_PALETTE_H
#define AM2_PALETTE_H

#include <stdint.h>
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

int palette_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PALETTE_H */
