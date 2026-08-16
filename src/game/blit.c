/* The RLE blitter family -- reconstructed from ArmyMen2.exe.
 *
 *   BlitGlyph      0x0041C710   solid fill,   16-bit row offsets   3 sites
 *   BlitCopy16     0x0041C2B0   copy source,  16-bit row offsets   2 sites
 *   BlitCopy32     0x0041C1C0   copy source,  32-bit row offsets   1 site
 *   BlitRemap16    0x0041C3A0   copy via LUT, 16-bit row offsets   2 sites
 *   BlitOverlay    0x0041C480   remap the DESTINATION, 16-bit offsets  1 site
 *
 * All five share a body in the original -- byte-identical prologue, row walk and
 * clipping arithmetic -- differing only in fill policy and row-offset width,
 * which is why they are written here as one core with a policy rather than four
 * near-copies.
 *
 * They do NOT share a signature, though, and that distinction cost a crash. The
 * copy variants take five stack dwords and clean up with `ret 0x14`; BlitGlyph
 * and BlitRemap16 take six and use `ret 0x18`, because only they need a trailing
 * colour or lookup table. Giving the copy variants a sixth `unused` parameter
 * made the compiler over-pop by four bytes and corrupt the caller's stack. A
 * diff that normalises jump targets hides the epilogue, so compare `ret N`
 * explicitly before assuming two functions are the same shape.
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
 *             supplied by the caller. Used for recolouring.
 *   destlut - reads NO source pixels; each covered destination byte is passed
 *             through a table taken from a global. Shadows and translucency,
 *             done by transforming what is already on screen.
 *
 * Solid and destlut never advance the source pointer past a run because there
 * is nothing there to skip; copy and remap must. That asymmetry is the clearest
 * evidence that fonts and sprites use related but distinct encodings -- and it
 * puts the overlay layer on the font side of that divide, carrying coverage
 * only.
 *
 * Not part of this family, despite being reached from the same dispatcher:
 * 0x00445EB0 makes a C++ virtual call through the sprite's surface at +0x10
 * (`call [vtable+0x6C]`, IDirectDrawSurface::Restore) and falls back through
 * several drawing paths with logging. It is recovery, not a blitter.
 */

#include "blit.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

#define g_pitch    (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_frameBuf (*(uint8_t *const *)(uintptr_t)ADDR_FRAMEBUFFER)

enum { FILL_SOLID, FILL_COPY, FILL_REMAP, FILL_DESTLUT };

/* Only the copy and remap policies consume pixel bytes after a run length.
 * Solid and dest-LUT read coverage only, exactly like the font format. */
#define FILL_HAS_PIXELS(f) ((f) == FILL_COPY || (f) == FILL_REMAP)

#define g_overlayPalette (*(const uint8_t *const *)(uintptr_t)ADDR_OVERLAY_PALETTE)

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

                if (FILL_HAS_PIXELS(fill))
                    rle += lead;            /* skip the clipped-off pixels */
                px += lead + count;

                if (count != 0) {
                    if (fill == FILL_SOLID) {
                        memset(d, (uint8_t)param, count);
                    } else if (fill == FILL_COPY) {
                        memcpy(d, rle, count);
                        rle += count;
                    } else if (fill == FILL_REMAP) {
                        const uint8_t *lut = (const uint8_t *)param;
                        uint32_t k;
                        for (k = 0; k < count; k++)
                            d[k] = lut[rle[k]];
                        rle += count;
                    } else {
                        /* FILL_DESTLUT: transform what is already on screen.
                         * No source pixels are read at all -- the stream only
                         * says which destination bytes are covered. */
                        const uint8_t *lut = g_overlayPalette;
                        uint32_t k;
                        for (k = 0; k < count; k++)
                            d[k] = lut[d[k]];
                    }
                    d += count;
                }
            } else {
                px += n;
                if (FILL_HAS_PIXELS(fill))
                    rle += n;               /* run skipped, but still encoded */
            }
        }

        rowPtr += step;
        dst    += g_pitch;
    }
}

void __fastcall BlitGlyph(int32_t x, int32_t y, const AM2_Rle16 *glyph,
                          AM2_Rect src, int32_t colour)
{
    blit_core(x, y, (const uint8_t *)glyph, src, (uintptr_t)(uint8_t)colour, FILL_SOLID, 0);
}

void __fastcall BlitCopy16(int32_t x, int32_t y, const AM2_Rle16 *data,
                           AM2_Rect src)
{
    blit_core(x, y, (const uint8_t *)data, src, 0, FILL_COPY, 0);
}

void __fastcall BlitCopy32(int32_t x, int32_t y, const AM2_Rle32 *data,
                           AM2_Rect src)
{
    blit_core(x, y, (const uint8_t *)data, src, 0, FILL_COPY, 1);
}

void __fastcall BlitRemap16(int32_t x, int32_t y, const AM2_Rle16 *data,
                            AM2_Rect src, const uint8_t *lut)
{
    blit_core(x, y, (const uint8_t *)data, src, (uintptr_t)lut, FILL_REMAP, 0);
}

/* BlitOverlay -- 0x0041C480, the shadow and translucency layer.
 *
 * Unlike the rest of the family this reads nothing from the source: the RLE
 * stream supplies coverage only, and each covered DESTINATION byte is passed
 * through the table at 0x004FE1A4, which the sprite dispatcher sets from the
 * sprite's palette field before calling. That is how the game does shadows and
 * see-through effects -- by remapping what is already on screen.
 *
 * The original is 656 bytes because it aligns the destination to a dword and
 * then transforms four pixels at a time, with separate lead-in cases for each
 * misalignment. Only the effect is reproduced, not the unrolling: the transform
 * is a pure per-byte mapping of the destination, so a plain loop is
 * indistinguishable in result.
 */
void __fastcall BlitOverlay(int32_t x, int32_t y, const AM2_Rle16 *data,
                            AM2_Rect src)
{
    blit_core(x, y, (const uint8_t *)data, src, 0, FILL_DESTLUT, 0);
}

int blit_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_BLIT_GLYPH,   BlitGlyph,   "BlitGlyph", 6);
    rc |= patch_replace(ADDR_BLIT_COPY16,  BlitCopy16,  "BlitCopy16", 5);
    rc |= patch_replace(ADDR_BLIT_COPY32,  BlitCopy32,  "BlitCopy32", 5);
    rc |= patch_replace(ADDR_BLIT_REMAP16, BlitRemap16, "BlitRemap16", 6);
    rc |= patch_replace(ADDR_BLIT_OVERLAY, BlitOverlay, "BlitOverlay", 5);
    return rc;
}
