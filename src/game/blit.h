#ifndef AM2_BLIT_H
#define AM2_BLIT_H

#include <stdint.h>
#include "../inject/orig.h"
#include "rect.h"

/* BlitGlyph -- original 0x0041C710.
 *
 * Draws one run-length encoded glyph into the framebuffer at (x, y), filling
 * its covered pixels with `colour`. `src` is the clipped source rectangle
 * produced by ClipRect, in glyph space.
 *
 * __fastcall: x and y arrive in ecx and edx, the remaining six dwords on the
 * stack, and the callee cleans them (`ret 0x18`). The argument tracer only
 * reads stack dwords, which is why observing this function appeared to show a
 * list beginning at `glyph`.
 *
 * Glyph format, established here and in DrawText:
 *
 *     +0            uint16   width
 *     +2            uint16   height
 *     +4            uint16   rowOffset[height]   byte offset of each row
 *     rowOffset[r]  uint8    alternating skip and run lengths
 *
 * The rows hold coverage only, never colour: a run is filled with the caller's
 * colour byte. That is what makes DrawText's '^' colour escape possible at all,
 * and it means one font can be drawn in any palette entry.
 */
void __fastcall BlitGlyph(int32_t x, int32_t y, const uint8_t *glyph,
                          AM2_Rect src, int32_t colour);

int blit_install(void);

#endif /* AM2_BLIT_H */
