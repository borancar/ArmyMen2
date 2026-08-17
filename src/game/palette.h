#ifndef AM2_PALETTE_H
#define AM2_PALETTE_H

#include <stdint.h>
#include "../inject/orig.h"

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

int palette_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PALETTE_H */
