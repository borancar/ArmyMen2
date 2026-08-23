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

#include "../crt.h"
#include "font.h"
#include "../../inject/patch.h"
#include "surface.h"
#include <string.h>

#include <stdint.h>

#define g_backBuffer (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER)
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
    LPDIRECTDRAWSURFACE surf = g_backBuffer;
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

/* Original: 0x00446450. Ask GDI for a font to render glyphs from.
 *
 * The only CreateFontA in the game, and the source of every HFONT RenderGlyph
 * is handed. Everything except the face, the height and three style bits is
 * fixed: normal weight, default character set and precisions, draft quality and
 * a variable pitch.
 *
 * `style` is a bitfield the game packs itself -- bit 0 italic, bit 1 underline,
 * bit 2 strikeout -- which is why it arrives as one 16-bit value rather than as
 * three flags. A null face name is refused rather than passed to GDI, where it
 * would have meant "any font you like". */
#define FONT_STYLE_ITALIC     0x1u
#define FONT_STYLE_UNDERLINE  0x2u
#define FONT_STYLE_STRIKEOUT  0x4u

static_assert(FW_NORMAL == 0x190, "FW_NORMAL");
static_assert(DRAFT_QUALITY == 1, "DRAFT_QUALITY");
static_assert(VARIABLE_PITCH == 2, "VARIABLE_PITCH");
/* The charset it passes is 0, which is ANSI_CHARSET -- DEFAULT_CHARSET is 1.
 * Another pair that is easy to transpose, caught the same way as PC_NOCOLLAPSE
 * in palette.cpp. */
static_assert(ANSI_CHARSET == 0, "ANSI_CHARSET");
static_assert(OUT_DEFAULT_PRECIS == 0 && CLIP_DEFAULT_PRECIS == 0,
              "the precisions it passes as 0");

HFONT __cdecl CreateGameFont(const char *face, int32_t height, uint16_t style)
{
    HFONT font;

    if (!face)
        return NULL;

    font = CreateFontA(height, 0, 0, 0, FW_NORMAL,
                       style & FONT_STYLE_ITALIC,
                       (style >> 1) & 1,
                       (style >> 2) & 1,
                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DRAFT_QUALITY, VARIABLE_PITCH, face);
    if (!font)
        orig_log("Error creating font.\n");
    return font;
}

/* Original: 0x004466E0. Build one font's entire glyph set.
 *
 * This is what ties the font pipeline together: it asks GDI for the face,
 * renders all 256 characters through RenderGlyph, and keeps the RLE the
 * encoder produced. Everything it calls is already reconstructed --
 * ClearSurface, CreateGameFont, RenderGlyph, and EncodeGlyph below that.
 *
 * The two-allocation shape is deliberate. How much space 256 encoded glyphs
 * need is not knowable until they are encoded, so it renders into a fixed 32KB
 * scratch, then allocates exactly what was used and copies. Both allocations go
 * through the game's own CRT rather than ours, which matters: the buffer it
 * keeps is freed elsewhere in the game, by the game's free, and orig.h is
 * emphatic about not mixing the two heaps.
 *
 * Rendering happens onto the back buffer, which is cleared first -- glyphs are
 * drawn there with GDI and read back, so whatever the frame left behind would
 * otherwise end up inside the letters. */
#define GLYPH_SCRATCH 0x8000

typedef struct { const char *face; int32_t height; uint16_t style; } AM2_FontDesc;

#define g_fontDescs   ((const AM2_FontDesc *)(uintptr_t)ADDR_FONT_DESCS)
#define g_backgroundColour  (*(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR)

/* Per-font record fields, `f` records apart. */
#define font_size(f)    (*(uint32_t *)(uintptr_t)(ADDR_GLYPH_SIZE + (f) * ADDR_FONT_STRIDE))
#define font_offsets(f) ((uint16_t *)(uintptr_t)(ADDR_GLYPH_OFFSETS + (f) * ADDR_FONT_STRIDE))
#define font_base(f)    (*(uint8_t **)(uintptr_t)(ADDR_FONT_BASES + (f) * ADDR_FONT_STRIDE))


int32_t __cdecl BuildFont(int32_t font)
{
    uint8_t  *scratch, *out, *kept;
    uint16_t *offsets;
    HFONT     hfont;
    int32_t   ch;
    uint32_t  total;

    if (font_base(font))
        return 1;                       /* already built */

    ClearSurface(g_backBuffer, g_backgroundColour);

    scratch = (uint8_t *)orig_malloc(GLYPH_SCRATCH);
    out     = scratch;

    hfont = CreateGameFont(g_fontDescs[font].face, g_fontDescs[font].height,
                           g_fontDescs[font].style);
    if (!hfont) {
        orig_free(scratch);
        return 0;
    }

    /* The first 32 characters are never rendered, so their offsets are zeroed
     * rather than written; the loop starts at space. */
    memset(font_offsets(font), 0, 32 * sizeof(uint16_t));
    offsets = font_offsets(font) + 32;

    for (ch = 0x20; ch < 0x100; ch++) {
        uint32_t used = (uint32_t)(out - scratch);
        uint32_t n;

        *offsets = (uint16_t)used;
        n = RenderGlyph(font, (char)ch, hfont, (AM2_Rle16 *)out,
                        GLYPH_SCRATCH - used);
        if (!n) {
            orig_free(scratch);
            DeleteObject(hfont);
            return 0;
        }
        out += n;
        offsets++;
    }
    DeleteObject(hfont);

    /* Now the size is known, so take exactly that much and let the scratch go. */
    total = (uint32_t)(out - scratch);
    font_size(font) = total;
    kept = (uint8_t *)orig_malloc(total);
    font_base(font) = kept;
    memcpy(kept, scratch, total);
    orig_free(scratch);
    return 1;
}

int32_t __cdecl BuildFontAlias(int32_t fontIndex)
{
    return BuildFont(fontIndex);
}

void __cdecl FreeFont(int32_t fontIndex)
{
    uint32_t off = (uint32_t)fontIndex * ADDR_FONT_STRIDE;
    uint8_t **base = (uint8_t **)((uintptr_t)ADDR_FONT_BASES + off);

    if (!*base)
        return;
    am2_free(*base);
    *base = 0;
    *(uint32_t *)((uintptr_t)ADDR_GLYPH_SIZE + off) = 0;
}

void __cdecl FreeAllFonts(void)
{
    int32_t i;

    for (i = 0; i < AM2_FONT_COUNT; i++)
        FreeFont(i);
}

int32_t __cdecl TextExtent(const char *text, int32_t font, int32_t out[2])
{
    uint32_t        off   = (uint32_t)font * ADDR_FONT_STRIDE;
    const uint8_t  *base  = *(const uint8_t **)((uintptr_t)ADDR_FONT_BASES + off);
    const uint16_t *table = (const uint16_t *)((uintptr_t)ADDR_GLYPH_OFFSETS + off);
    int32_t         width = 0;
    const uint8_t  *p;

    for (p = (const uint8_t *)text; *p; p++) {
        if (*p == '^')          /* an escape; the character after it counts */
            continue;
        if ((int8_t)*p < 0x1F)  /* a control, and SIGNED -- so 0x80 and up
                                 * are skipped too */
            continue;
        width += *(const uint16_t *)(base + table[*p]);
    }

    if (!out)
        return width;
    out[0] = width;
    /* The line height is the space glyph's, whatever the string was. */
    out[1] = *(const uint16_t *)(base
                                 + *(const uint16_t *)
                                       ((uintptr_t)ADDR_GLYPH_OFFSET_SPACE + off)
                                 + 2);
    return width;
}

int font_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ENCODE_GLYPH, (const void *)EncodeGlyph, "EncodeGlyph", 4);
    rc |= patch_replace(ADDR_RENDER_GLYPH, (const void *)RenderGlyph, "RenderGlyph", 5);
    rc |= patch_replace(ADDR_CREATE_GAME_FONT, (const void *)CreateGameFont,
                        "CreateGameFont", 3);
    rc |= patch_replace(ADDR_BUILD_FONT, (const void *)BuildFont, "BuildFont", 1);
    rc |= patch_replace(ADDR_TEXT_EXTENT, (const void *)TextExtent,
                        "TextExtent", 3);
    rc |= patch_replace(ADDR_FREE_FONT, (const void *)FreeFont, "FreeFont", 1);
    rc |= patch_replace(ADDR_FREE_ALL_FONTS, (const void *)FreeAllFonts,
                        "FreeAllFonts", 0);
    rc |= patch_replace(ADDR_BUILD_FONT_ALIAS, (const void *)BuildFontAlias,
                        "BuildFontAlias", 1);
    return rc;
}
