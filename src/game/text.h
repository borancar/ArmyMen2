#ifndef AM2_TEXT_H
#define AM2_TEXT_H

#include <stdint.h>
#include "../inject/orig.h"
#include "rect.h"

/* DrawText -- original 0x00446930, 34 call sites.
 *
 * Draws `str` at (x, y) in `font`, one glyph at a time, clipping each against
 * the global text clip rectangle and blitting what survives.
 *
 * The argument order was measured rather than derived: under observation the
 * HUD stat panel produces DrawText(0x22C, 0x111, "Sarge", 0, 0, 0xFE) and
 * DrawText(0x240, 0x11D, "4.6 cm", 0, 0, 0xFE) -- labels at x=556, values at
 * x=576, y stepping 12 per row, which is exactly the panel on screen and
 * exactly the glyph height seen in the blit calls.
 *
 * A '^' in the string is an escape: the following character replaces `colour`
 * for the rest of the string. Both characters are consumed. A trailing '^' with
 * nothing after it is drawn literally.
 *
 * `arg4` is not yet identified -- every observed call passes 0.
 */
void __cdecl DrawText(int32_t x, int32_t y, const char *str,
                      int32_t font, int32_t arg4, int32_t colour);

int text_install(void);

#endif /* AM2_TEXT_H */
