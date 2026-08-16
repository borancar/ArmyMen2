#ifndef AM2_BLIT_H
#define AM2_BLIT_H

#include <stdint.h>
#include "../inject/orig.h"
#include "rect.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

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
/* A run-length encoded image, as both the blitters and EncodeGlyph see it.
 *
 * The row table is a flexible array of `height` entries, and the RLE stream
 * begins immediately after it -- `rowOffset[r]` is a byte offset from the start
 * of the structure to that row's stream. Each stream is alternating counts:
 * pixels to leave alone, then pixels to draw, repeating to the row's width.
 *
 * Whether a run is followed by that many pixel bytes depends on the consumer,
 * not on the format: BlitCopy16/32 and BlitRemap16 read them, BlitGlyph and
 * BlitOverlay do not, because fonts and shadow masks encode coverage only.
 *
 * Two widths exist because a 16-bit offset can only reach 64KB of encoded data.
 * Only the 16-bit header has been confirmed against real data; the 32-bit
 * variant is assumed to match, since both blitters take the table from +4.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t rowOffset[];       /* [height], then the RLE stream */
} AM2_Rle16;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t rowOffset[];       /* [height], then the RLE stream */
} AM2_Rle32;

void __fastcall BlitGlyph(int32_t x, int32_t y, const AM2_Rle16 *glyph,
                          AM2_Rect src, int32_t colour);

/* The plain copy variants take FIVE stack dwords, not six: they need neither a
 * colour nor a lookup table, and the originals clean up with `ret 0x14` where
 * BlitGlyph and BlitRemap16 use `ret 0x18`. Adding a sixth parameter here makes
 * the compiler over-pop by four bytes and corrupts the caller's stack. */
void __fastcall BlitCopy16(int32_t x, int32_t y, const AM2_Rle16 *data,
                           AM2_Rect src);
void __fastcall BlitCopy32(int32_t x, int32_t y, const AM2_Rle32 *data,
                           AM2_Rect src);
void __fastcall BlitRemap16(int32_t x, int32_t y, const AM2_Rle16 *data,
                            AM2_Rect src, const uint8_t *lut);

/* 0x0041C480. Transforms the destination through the table at 0x004FE1A4
 * rather than drawing source pixels -- the shadow and translucency layer.
 * Five stack dwords like the copy variants. */
void __fastcall BlitOverlay(int32_t x, int32_t y, const AM2_Rle16 *data,
                            AM2_Rect src);

int blit_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_BLIT_H */
