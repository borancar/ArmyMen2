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

#include <stdint.h>

#define g_pitch    (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_frameBuf (*(uint8_t *const *)(uintptr_t)ADDR_FRAMEBUFFER)

/* The scratch square the glyph is rendered into, wiped after each encode. */
#define SCRATCH_SIDE 25
/* Sampled this far down the surface, clear of the scratch square. */
#define KEY_ROW      100
#define RUN_MAX      0xFE

uint32_t __cdecl EncodeGlyph(uint8_t *out, int32_t width, int32_t height,
                             int32_t unused)
{
    uint8_t       *fb  = g_frameBuf;
    uint16_t      *rowTab;
    uint8_t       *w;
    const uint8_t *rowPix;
    uint32_t       written;
    uint8_t        key;
    int32_t        row;

    (void)unused;                       /* pushed by the caller, never read */

    *(uint16_t *)(out + 0) = (uint16_t)width;
    *(uint16_t *)(out + 2) = (uint16_t)height;

    rowTab  = (uint16_t *)(out + 4);
    w       = out + 4 + height * 2;
    written = (uint32_t)(4 + height * 2);
    rowPix  = fb;

    key = fb[g_pitch * KEY_ROW];

    for (row = 0; row < height; row++) {
        const uint8_t *p   = rowPix;
        const uint8_t *end = rowPix + width;

        rowTab[row] = (uint16_t)(w - out);

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

int font_install(void)
{
    return patch_replace(ADDR_ENCODE_GLYPH, EncodeGlyph, "EncodeGlyph", 4);
}
