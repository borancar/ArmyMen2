/* The RLE blitter family -- reconstructed from ArmyMen2.exe.
 *
 *   BlitGlyph      0x0041C710   solid fill,   16-bit row offsets   3 sites
 *   BlitCopy16     0x0041C2B0   copy source,  16-bit row offsets   2 sites
 *   BlitCopy32     0x0041C1C0   copy source,  32-bit row offsets   1 site
 *   BlitRemap16    0x0041C3A0   copy via LUT, 16-bit row offsets   2 sites
 *
 * All four are the same routine in the original -- byte-identical prologue, row
 * walk and clipping arithmetic -- differing only in two respects, which is why
 * they are written here as one core with a policy rather than four near-copies.
 * A diff of 0x0041C1C0 against 0x0041C2B0 shows a single meaningful change, the
 * row-offset width; a diff of 0x0041C3A0 against 0x0041C710 shows only the
 * inner fill.
 *
 * Row offset width. 16-bit offsets can only reach 64KB of encoded data, so the
 * 32-bit variant exists for sprites too large for that.
 *
 * Fill policy, and the format difference it implies:
 *
 *   solid  -- runs carry no pixel data at all, just a length; the run is filled
 *             with the caller's colour byte. This is the font format, and it is
 *             what makes DrawText's '^' colour escape possible: one font can be
 *             drawn in any palette entry.
 *   copy   -- runs are followed by that many source bytes, copied straight out.
 *   remap  -- as copy, but each byte is passed through a 256-entry lookup table
 *             supplied by the caller. Used for recolouring and translucency.
 *
 * The solid variant never advances the source pointer past a run because there
 * is nothing there to skip; the other two must. That asymmetry is the clearest
 * evidence that fonts and sprites use related but distinct encodings.
 *
 * Not part of this family, despite being reached from the same dispatcher:
 *   0x00445EB0 makes a C++ virtual call through the sprite's object at +0x10
 *              (`call [vtable+0x6C]`) and falls back through several drawing
 *              paths with logging. It is a dispatcher, not a blitter.
 *   0x0041C480 is 656 bytes and not yet read.
 */

#include "blit.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

#define g_pitch    (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_frameBuf (*(uint8_t *const *)(uintptr_t)ADDR_FRAMEBUFFER)

enum { FILL_SOLID, FILL_COPY, FILL_REMAP };

/* `param` is the colour byte for FILL_SOLID and the lookup table for
 * FILL_REMAP; it is unused for FILL_COPY.
 *
 * Written to follow the original's control flow rather than to read well: the
 * clipping arithmetic uses unsigned comparisons throughout, matching the
 * jb/jae the compiler emitted, and the running x advances by different amounts
 * on the skipped and drawn paths. Both are load-bearing.
 */
static void blit_core(int32_t x, int32_t y, const uint8_t *data, AM2_Rect src,
                      uintptr_t param, int fill, int offset32)
{
    const int32_t  step   = offset32 ? 4 : 2;
    uint8_t       *dst    = g_frameBuf + (uint32_t)(y * g_pitch + x);
    const uint8_t *rowPtr = data + src.top * step + 4;
    const uint8_t *endPtr = data + src.bottom * step + 4;
    uint32_t       left   = (uint32_t)src.left;
    uint32_t       right  = (uint32_t)src.right;

    while (rowPtr < endPtr) {
        uint32_t rowOff = offset32 ? *(const uint32_t *)rowPtr
                                   : *(const uint16_t *)rowPtr;
        const uint8_t *rle = data + rowOff;
        uint8_t       *d   = dst;
        uint32_t       px  = 0;

        while (px < right) {
            uint32_t n   = *rle++;          /* pixels to leave alone */
            uint32_t end = px + n;

            if (px >= left)
                d += n;
            else if (end >= left)
                d += end - left;

            px = end;
            if (px >= right)
                break;

            n   = *rle++;                   /* pixels to draw */
            end = px + n;

            if (n != 0 && end > left) {
                uint32_t lead  = (left >= px) ? (left - px) : 0;
                uint32_t start = px + lead;
                uint32_t count;

                if (end > right)
                    end = right;
                count = end - start;

                if (fill != FILL_SOLID)
                    rle += lead;            /* skip the clipped-off pixels */
                px += lead + count;

                if (count != 0) {
                    if (fill == FILL_SOLID) {
                        memset(d, (uint8_t)param, count);
                    } else if (fill == FILL_COPY) {
                        memcpy(d, rle, count);
                        rle += count;
                    } else {
                        const uint8_t *lut = (const uint8_t *)param;
                        uint32_t k;
                        for (k = 0; k < count; k++)
                            d[k] = lut[rle[k]];
                        rle += count;
                    }
                    d += count;
                }
            } else {
                px += n;
                if (fill != FILL_SOLID)
                    rle += n;               /* run skipped, but still encoded */
            }
        }

        rowPtr += step;
        dst    += g_pitch;
    }
}

void __fastcall BlitGlyph(int32_t x, int32_t y, const uint8_t *glyph,
                          AM2_Rect src, int32_t colour)
{
    blit_core(x, y, glyph, src, (uintptr_t)(uint8_t)colour, FILL_SOLID, 0);
}

void __fastcall BlitCopy16(int32_t x, int32_t y, const uint8_t *data,
                           AM2_Rect src, int32_t unused)
{
    (void)unused;
    blit_core(x, y, data, src, 0, FILL_COPY, 0);
}

void __fastcall BlitCopy32(int32_t x, int32_t y, const uint8_t *data,
                           AM2_Rect src, int32_t unused)
{
    (void)unused;
    blit_core(x, y, data, src, 0, FILL_COPY, 1);
}

void __fastcall BlitRemap16(int32_t x, int32_t y, const uint8_t *data,
                            AM2_Rect src, const uint8_t *lut)
{
    blit_core(x, y, data, src, (uintptr_t)lut, FILL_REMAP, 0);
}

/* NOT INSTALLED YET -- BlitCopy16/32 and BlitRemap16 crash on their first call.
 *
 * BlitGlyph, the solid-fill variant, is verified and stays installed. The other
 * three are written but disabled, because the first live call to BlitCopy16
 * (an 85x80 sprite, src {0,0,85,80}) faulted immediately.
 *
 * The likely cause, and where to resume: the solid variant never advances the
 * source pointer, so it consumes exactly two bytes per iteration and any
 * mis-accounting is self-limiting. The copy and remap variants advance `rle` by
 * the run length, so if the running x is even slightly out of step with the
 * encoder, the source pointer desynchronises and walks off the sprite -- the
 * destination writes are bounded by the clip, but the source reads are not.
 *
 * The control-flow transcription was checked against the original line by line
 * and matches, so the fault is more likely in an assumption about the stream
 * than in the arithmetic: possibly a row terminator, or a header that is not
 * the width/height pair the glyph format uses. Next step is to dump one real
 * sprite's bytes and decode a row by hand before re-enabling.
 */
int blit_install(void)
{
    return patch_replace(ADDR_BLIT_GLYPH, BlitGlyph, "BlitGlyph", 6);
}
