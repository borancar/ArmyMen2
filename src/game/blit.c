/* RLE glyph blitter -- reconstructed from ArmyMen2.exe 0x0041C710.
 *
 * The bottom of the text path: 904,409 calls in a single Boot Camp session, and
 * the last unknown beneath DrawText. See blit.h for the glyph format and the
 * calling convention.
 *
 * Each row is a stream of alternating byte counts: how many pixels to leave
 * untouched, then how many to fill, repeating. Horizontal clipping is done
 * against src.left and src.right in glyph space while the destination pointer
 * advances in step, so the pointer effectively begins at the pixel
 * corresponding to src.left. Vertical clipping is just the range of rows walked.
 *
 * Written to follow the original's control flow rather than to read well. The
 * clipping arithmetic uses unsigned comparisons throughout, matching the jb/jae
 * the compiler emitted, and the running x advances by different amounts on the
 * skipped and drawn paths -- both are load-bearing.
 */

#include "blit.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

#define g_pitch    (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_frameBuf (*(uint8_t *const *)(uintptr_t)ADDR_FRAMEBUFFER)

void __fastcall BlitGlyph(int32_t x, int32_t y, const uint8_t *glyph,
                          AM2_Rect src, int32_t colour)
{
    uint8_t       *dst    = g_frameBuf + (uint32_t)(y * g_pitch + x);
    const uint8_t *rowPtr = glyph + src.top * 2 + 4;
    const uint8_t *endPtr = glyph + src.bottom * 2 + 4;
    uint32_t       left   = (uint32_t)src.left;
    uint32_t       right  = (uint32_t)src.right;

    while (rowPtr < endPtr) {
        uint8_t       *d   = dst;
        const uint8_t *rle = glyph + *(const uint16_t *)rowPtr;
        uint32_t       px  = 0;

        while (px < right) {
            uint32_t n = *rle++;            /* pixels to leave alone */
            uint32_t end = px + n;

            if (px >= left)
                d += n;                     /* already past the clip edge */
            else if (end >= left)
                d += end - left;            /* only the part beyond it counts */

            px = end;
            if (px >= right)
                break;

            n   = *rle++;                   /* pixels to fill */
            end = px + n;

            if (n != 0 && end > left) {
                uint32_t lead  = (left >= px) ? (left - px) : 0;
                uint32_t start = px + lead;
                uint32_t count;

                if (end > right)
                    end = right;
                count = end - start;

                px += lead + count;
                if (count != 0) {
                    memset(d, (uint8_t)colour, count);
                    d += count;             /* rep stosb advances the pointer */
                }
            } else {
                px += n;                    /* nothing visible in this run */
            }
        }

        rowPtr += 2;
        dst    += g_pitch;
    }
}

int blit_install(void)
{
    return patch_replace(ADDR_BLIT_GLYPH, BlitGlyph, "BlitGlyph", 6);
}
