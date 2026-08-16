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
 *     base   = fontBases[font * 133]                      uint8_t *
 *     glyph  = base + offset
 *     width  = *(uint16 *)(glyph + 0)
 *     height = *(uint16 *)(glyph + 2)
 *
 * The character index is sign-extended (movsx), so bytes above 0x7F index
 * backwards from the font's base. That is what the original does; whether any
 * caller relies on it is unknown.
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
#include "rect.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

#define g_textReady   (*(int32_t *)(uintptr_t)ADDR_TEXT_READY)
#define g_glyphOffset ((const uint16_t *)(uintptr_t)ADDR_GLYPH_OFFSETS)
#define g_fontBase    ((uint8_t *const *)(uintptr_t)ADDR_FONT_BASES)
#define g_textClip    (*(const AM2_Rect *)(uintptr_t)ADDR_TEXT_CLIP)
#define g_originSelA  (*(int32_t *)(uintptr_t)ADDR_ORIGIN_SEL_A)
#define g_originSelB  (*(int32_t *)(uintptr_t)ADDR_ORIGIN_SEL_B)
#define g_originDX    (*(int32_t *)(uintptr_t)ADDR_ORIGIN_DX)
#define g_originDY    (*(int32_t *)(uintptr_t)ADDR_ORIGIN_DY)

/* 0x0041C710 is __fastcall: the destination x and y arrive in ecx and edx,
 * everything else on the stack, with the clipped source rectangle passed BY
 * VALUE. This is why the argument tracer -- which only reads stack dwords --
 * showed six arguments starting at the pixel pointer and never saw the
 * destination at all. */
typedef void (__fastcall *am2_blit_glyph_fn)(int32_t x, int32_t y,
                                             const void *pixels,
                                             AM2_Rect src, int32_t colour);
#define orig_blit_glyph (*(am2_blit_glyph_fn)ADDR_BLIT_GLYPH)

void __cdecl DrawText(int32_t x, int32_t y, const char *str,
                      int32_t font, int32_t arg4, int32_t colour)
{
    int32_t len, i, j, penX;

    (void)arg4;                     /* every observed call passes 0 */

    if (!g_textReady)
        return;

    len = (int32_t)strlen(str);
    if (len <= 0)
        return;

    penX = x;

    for (i = 0, j = 1; i < len; i++, j++) {
        const uint8_t *glyph;
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

        offset = g_glyphOffset[(int8_t)ch + font * 262];
        glyph  = g_fontBase[font * 133] + offset;
        gw     = *(const uint16_t *)(glyph + 0);
        gh     = *(const uint16_t *)(glyph + 2);

        src.left = 0;
        src.top  = 0;
        src.right  = gw;
        src.bottom = gh;

        dstX = penX;
        dstY = y;

        if (!ClipRect(&src, &g_textClip, &dstX, &dstY, &out))
            return;                 /* not `continue` -- see the header note */

        if (g_originSelA == g_originSelB) {
            dstX += g_originDX;
            dstY += g_originDY;
        }

        orig_blit_glyph(dstX, dstY, glyph, out, colour);

        penX += gw;
    }
}

int text_install(void)
{
    return patch_replace(ADDR_DRAW_TEXT, DrawText, "DrawText", 6);
}
