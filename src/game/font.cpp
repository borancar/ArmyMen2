/* Runtime font generation -- reconstructed from ArmyMen2.exe 0x004464C0.
 *
 * See font.h for the glyph format this defines.
 *
 * The encoder reads the locked surface directly, so it runs between a
 * LockSurface and an UnlockSurface like every other drawing operation -- its
 * caller at 0x004465E0 draws the character with GDI first, then locks, encodes,
 * and unlocks.
 *
 * Two details worth keeping. The background colour is not a constant: it is
 * sampled from the surface itself at `framebuffer[pitch * 100]`, a pixel well
 * below the 25x25 scratch square, so whatever the surface was cleared to counts
 * as transparent. And runs are capped at 254 rather than 255, so a span longer
 * than that is emitted as several pairs with zero-length partners between them.
 */

#include "font.h"
#include "../inject/patch.h"
#include "surface.h"

#include <stdint.h>

#define g_fontSurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_FONT_SURFACE)
#define g_pitch    (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_frameBuf (*(uint8_t *const *)(uintptr_t)ADDR_FRAMEBUFFER)

/* The scratch square the glyph is rendered into, wiped after each encode. */
#define SCRATCH_SIDE 25
/* Sampled this far down the surface, clear of the scratch square. */
#define KEY_ROW      100
#define RUN_MAX      0xFE

/* The row table is a flexible array, so the RLE stream starts at
 * sizeof(AM2_Rle16) + height * sizeof(rowOffset[0]) -- which is the `+4` and
 * `height * 2` the original computes by hand. */
typedef char am2_rle16_header_is_4_bytes[(sizeof(AM2_Rle16) == 4) ? 1 : -1];

uint32_t __cdecl EncodeGlyph(AM2_Rle16 *out, int32_t width, int32_t height,
                             int32_t unused)
{
    uint8_t       *fb   = g_frameBuf;
    uint8_t       *base = (uint8_t *)out;
    uint8_t       *w;
    const uint8_t *rowPix;
    uint32_t       written;
    uint8_t        key;
    int32_t        row;

    (void)unused;                       /* pushed by the caller, never read */

    out->width  = (uint16_t)width;
    out->height = (uint16_t)height;

    written = (uint32_t)(sizeof *out + height * sizeof out->rowOffset[0]);
    w       = base + written;
    rowPix  = fb;

    key = fb[g_pitch * KEY_ROW];

    for (row = 0; row < height; row++) {
        const uint8_t *p   = rowPix;
        const uint8_t *end = rowPix + width;

        out->rowOffset[row] = (uint16_t)(w - base);

        /* Always emits pairs, and always at least one pair per row -- a row
         * that is entirely background still gets its count followed by a zero
         * foreground run. */
        do {
            uint32_t n = 0;

            while (p < end && *p == key && n < RUN_MAX) {
                p++;
                n++;
            }
            *w++ = (uint8_t)n;
            written++;

            n = 0;
            while (p < end && *p != key && n < RUN_MAX) {
                p++;
                n++;
            }
            *w++ = (uint8_t)n;
            written++;
        } while (p != end);

        rowPix += g_pitch;
    }

    /* Wipe the scratch square back to the background colour so the next
     * character starts clean. The original writes 6 dwords plus a byte per
     * row -- 25 bytes -- for 25 rows. */
    {
        uint8_t *dst = g_frameBuf;
        int32_t  i, j;

        for (i = 0; i < SCRATCH_SIDE; i++) {
            for (j = 0; j < SCRATCH_SIDE; j++)
                dst[j] = key;
            dst += g_pitch;
        }
    }

    return written;
}

/* RenderGlyph -- reconstructed from 0x004465E0.
 *
 * Draws one character into the scratch surface with GDI, then locks it and
 * hands it to EncodeGlyph. This is the function actually bracketed by
 * Lock/Unlock; EncodeGlyph is what runs inside the bracket.
 *
 * The argument order was measured rather than derived, because the frame
 * juggles seven Win32 calls and the locals move under it. Observation gave
 * RenderGlyph(1, 0x20, 0x0B0A0057, 0x02B50020, 0x8000) and then 0x21, 0x22,
 * 0x23 ... -- a constant first argument that is never read, the character
 * walking printable ASCII from space, a constant HFONT, and the output buffer
 * and remaining space that reappear as EncodeGlyph's first and fourth
 * arguments.
 *
 * Text is drawn white on a transparent background; EncodeGlyph then treats
 * whatever the surface was cleared to as the background, so the colour itself
 * carries no meaning beyond "not the key".
 */
uint32_t __cdecl RenderGlyph(int32_t unused, char ch, HFONT font,
                             AM2_Rle16 *out, int32_t space)
{
    LPDIRECTDRAWSURFACE surf = g_fontSurface;
    HDC                 hdc;
    SIZE                size;
    char                buf[2];
    uint32_t            written;

    (void)unused;                   /* every observed call passes 1 */

    if (IDirectDrawSurface_GetDC(surf, &hdc) != DD_OK)
        return 0;

    SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0x00FFFFFF);

    /* The character is sign-extended before being formatted, so codes above
     * 0x7F arrive negative. Only 0x20..0x7E are ever passed. */
    wsprintfA(buf, "%c", (int)ch);

    /* lstrlenA is called twice by the original, once for each consumer. */
    GetTextExtentPoint32A(hdc, buf, lstrlenA(buf), &size);
    TextOutA(hdc, 0, 0, buf, lstrlenA(buf));

    IDirectDrawSurface_ReleaseDC(surf, hdc);

    if (!LockSurface(surf))
        return 0;

    written = EncodeGlyph(out, size.cx, size.cy, space);
    UnlockSurface();

    return written;
}

int font_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ENCODE_GLYPH, (const void *)EncodeGlyph, "EncodeGlyph", 4);
    rc |= patch_replace(ADDR_RENDER_GLYPH, (const void *)RenderGlyph, "RenderGlyph", 5);
    return rc;
}
