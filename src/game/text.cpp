/* Text rendering -- reconstructed from ArmyMen2.exe 0x00446930.
 *
 * The first substantial drawing routine handled, rather than a primitive: 384
 * bytes, 34 call sites, and everything it stands on -- ClipRect, AM2_Rect --
 * was reconstructed first. It draws a string one glyph at a time, clipping each
 * against the global text rectangle and blitting whatever survives.
 *
 * Glyph lookup uses two parallel tables with awkward strides, reproduced as the
 * original computes them rather than tidied:
 *
 *     offset = glyphOffsets[(int8_t)ch + font * 262]      uint16
 *     base   = fontBases[font * 131]                      uint8_t *
 *     glyph  = base + offset
 *     width  = *(uint16 *)(glyph + 0)
 *     height = *(uint16 *)(glyph + 2)
 *
 * The character index is sign-extended (movsx), so bytes above 0x7F index
 * backwards from the font's base. That is what the original does; whether any
 * caller relies on it is unknown.
 *
 * THE BASE INDEX WAS 133 AND IT IS 131. Both strides are 524 bytes -- 262
 * uint16 and 131 dwords -- and this file had one of them right and the other
 * eight bytes long, which is the tell: two indexes into parallel tables that
 * disagree about the record size cannot both be correct. Confirmed three
 * ways: orig.h says 131, DrawTextClipped at 0x00446AB0 computes `edi*131`
 * through the same `shl 6 / add / lea` chain, and the running game has the
 * font bases at 0x02B621D8, 0x02B58028 and 0x02B5C890, exactly 524 apart --
 * where 133 dwords reads 0x00005936, a size field rather than a pointer.
 *
 * Nothing had caught it because font 0 is the only font these drives reach
 * through here; font 1 would have dereferenced 0x5936.
 *
 * Two behaviours worth knowing before calling it:
 *
 *   - A glyph that clips away entirely does not get skipped, it ENDS THE
 *     STRING. The original jumps straight to the epilogue. Text running off the
 *     right edge therefore truncates, which is sensible; text starting off the
 *     left edge draws nothing at all, which is probably not intended but is
 *     the behaviour.
 *   - The pen only advances after a successful blit, which is moot given the
 *     above but is reproduced anyway.
 */

#include "text.h"
#include "blit.h"
#include "rect.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

#define g_surfaceLocked   (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_glyphOffset ((const uint16_t *)(uintptr_t)ADDR_GLYPH_OFFSETS)
#define g_fontBase    ((uint8_t *const *)(uintptr_t)ADDR_FONT_BASES)
#define g_screenClip  (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_CLIP)
/* Compared for identity only, never dereferenced, so a flat module can
 * hold it without naming a COM type. It was int32_t here, which is the
 * width hazard this project has fixed-width types to avoid: on the
 * native build a pointer is not 32 bits and the compare would truncate. */
#define g_drawTarget     (*(void *const *)(uintptr_t)ADDR_DRAW_TARGET)
#define g_primarySurface (*(void *const *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_originDX    (*(int32_t *)(uintptr_t)ADDR_ORIGIN_DX)
#define g_originDY    (*(int32_t *)(uintptr_t)ADDR_ORIGIN_DY)

void __cdecl DrawText(int32_t x, int32_t y, const char *str,
                      int32_t font, int32_t arg4, int32_t colour)
{
    int32_t len, i, j, penX;

    (void)arg4;                     /* every observed call passes 0 */

    if (!g_surfaceLocked)
        return;

    len = (int32_t)strlen(str);
    if (len <= 0)
        return;

    penX = x;

    for (i = 0, j = 1; i < len; i++, j++) {
        const AM2_Rle16 *glyph;
        AM2_Rect src, out;
        int32_t  dstX, dstY, gw, gh;
        uint16_t offset;
        uint8_t  ch = (uint8_t)str[i];

        /* '^' escape: the next character becomes the colour. Both are
         * consumed. A '^' in the final position is drawn literally. */
        if (ch == 0x5E && j < len) {
            colour = (int8_t)str[i + 1];
            i++;
            j++;
            continue;
        }

        offset = g_glyphOffset[(int8_t)ch + font * AM2_FONT_OFFSET_STEP];
        glyph  = (const AM2_Rle16 *)(g_fontBase[font * AM2_FONT_BASE_STEP]
                                     + offset);
        gw     = glyph->width;
        gh     = glyph->height;

        src.left = 0;
        src.top  = 0;
        src.right  = gw;
        src.bottom = gh;

        dstX = penX;
        dstY = y;

        if (!ClipRect(&src, &g_screenClip, &dstX, &dstY, &out))
            return;                 /* not `continue` -- see the header note */

        if (g_drawTarget == g_primarySurface) {
            dstX += g_originDX;
            dstY += g_originDY;
        }

        /* Our own BlitGlyph, called directly. Going through 0x0041C710 would
         * work -- it holds a jmp back into this DLL -- but it would bounce
         * reconstruction through the original image for no reason. The side
         * effect is that BlitGlyph's traced call count now only counts the
         * game's own call sites, as with FindSlot. */
        BlitGlyph(dstX, dstY, glyph, out, colour);

        penX += gw;
    }
}

/* 0x00446E00, three callers, all in the HUD. TextExtent's vertical twin: the
 * same walk, summing the glyph record's SECOND uint16 minus three instead of
 * its first.
 *
 * THE FIELD IS THE HEIGHT, and a probe settles that rather than a reading.
 * Across "SARGE" the first field is 7, 7, 8, 9, 7 and the second is 12 for
 * every one of them -- 14 for every one in font 1. A per-glyph width varies
 * with the glyph; a line height does not. TextExtent already treats the same
 * field as a line height, for the space glyph alone. And the caller confirms
 * it: the result feeds a running vertical coordinate in a HUD panel.
 *
 * ITS ESCAPE HANDLING DIFFERS FROM TextExtent'S, which is the thing most
 * likely to be tidied away by someone writing the two from one template.
 * Here `^` consumes the character after it as well; there the character after
 * it counts.
 *
 * The loop tests the NEXT byte and then advances, so a string ending in `^`
 * reads one byte past its terminator -- the escape has already advanced once
 * and the tail advances again. Reproduced; the value is only tested against
 * zero, and no caller passes a string ending that way. */
int32_t __cdecl TextStackHeight(const char *text, int32_t font)
{
    const uint8_t  *base  = g_fontBase[font * AM2_FONT_BASE_STEP];
    const uint16_t *table = &g_glyphOffset[font * AM2_FONT_OFFSET_STEP];
    const uint8_t  *p     = (const uint8_t *)text;
    int32_t         total = 0;

    if (!*p)
        return 0;

    for (;;) {
        uint8_t ch = *p;

        if (ch == 0x5E) {                     /* '^', and its argument */
            p++;
        } else if ((int8_t)ch >= 0x1F) {
            total += *(const uint16_t *)(base + table[ch] + 2) - 3;
        }

        ch = p[1];
        p++;
        if (!ch)
            break;
    }

    return total;
}

int text_install(void)
{
    /* Two now, so not a bare `return patch_replace` any more -- that shape is
     * how an addition ends up compiling, passing every check and patching
     * nothing. See region_install. */
    int rc = 0;

    rc |= patch_replace(ADDR_DRAW_TEXT, (const void *)DrawText, "DrawText", 6);
    rc |= patch_replace(ADDR_TEXT_STACK_HEIGHT, (const void *)TextStackHeight,
                        "TextStackHeight", 3);
    return rc;
}
