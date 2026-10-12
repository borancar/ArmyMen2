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
#include "../rect.h"    /* ClipRect -- reconstructed */
#include "../blit.h"    /* BlitGlyph -- reconstructed */
#include "../image.h"
#include <string.h>

#include <stdint.h>


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
    uint8_t       *w;
    const uint8_t *p;
    uint32_t       written;
    uint8_t        key;
    int32_t        row;

    (void)unused;                       /* pushed by the caller, never read */

    out->width  = (uint16_t)width;
    out->height = (uint16_t)height;

    /* A walking pointer for the row offsets: the image keeps it in a
     * register and steps it by two per row, with `out` on the frame
     * (v3 11 -> 10). */
    w       = (uint8_t *)&out->rowOffset[height];
    written = (uint32_t)(height * sizeof out->rowOffset[0] + sizeof *out);
    p       = g_frameBuf;

    key = p[g_pitch * KEY_ROW];

    for (row = 0; row < height; row++) {
        out->rowOffset[row] = (uint16_t)(w - (uint8_t *)out);
        const uint8_t *start = p;
        const uint8_t *end   = p + width;

        /* Always emits pairs, and always at least one pair per row -- a row
         * that is entirely background still gets its count followed by a zero
         * foreground run. */
        do {
            int32_t n = 0;

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

        p = start + g_pitch;
    }

    /* Wipe the scratch square back to the background colour so the next
     * character starts clean. The original writes 6 dwords plus a byte per
     * row -- 25 bytes -- for 25 rows. The buffer is read afresh here; VC6
     * reuses the register only on the path where the loop never ran. */
    {
        uint8_t *dst = g_frameBuf;
        int32_t  i;

        for (i = 0; i < SCRATCH_SIDE; i++) {
            memset(dst, key, SCRATCH_SIDE);
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
    /* The back buffer is read from its global at every use. */
    HDC                 hdc;
    SIZE                size;
    char                buf[2];
    uint32_t            written;

    (void)unused;                   /* every observed call passes 1 */

    if (IDirectDrawSurface_GetDC(g_backBuffer, &hdc) != DD_OK)
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

    IDirectDrawSurface_ReleaseDC(g_backBuffer, hdc);

    if (!LockSurface(g_backBuffer))
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

/* AM2_FontDesc is in orig.h, shared with the standalone transcription. */

#ifdef AM2_STANDALONE
/* am2_font_descs -- 0x004897E8, the three GDI fonts the game builds (face,
 * pixel height, style). Transcribed out of the blob; the face pointers are
 * verified by dereference, the ints by value (the uint16 style's dword is 0). */
extern "C" const AM2_FontDesc am2_font_descs[3] = {
    { "ArialNarrow", 12, 0 },
    { "ArialNarrow", 14, 0 },
    { "ArialBlack",  18, 0 },
};
#endif


int32_t __cdecl BuildFont(int32_t font)
{
    uint8_t  *scratch, *out, *kept;
    HFONT     hfont;
    int32_t   ch;
    uint32_t  total;
    uint16_t *offs;

    if (g_fonts[font].base)
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
    for (ch = 0; ch < 0x20; ch++)
        g_fonts[font].offsets[ch] = 0;
    offs = &g_fonts[font].offsets[0x20];

    for (ch = 0x20; ch < 0x100; ch++) {
        uint32_t n;

        /* The offset and the room left are two expressions, not one `used`:
         * the image computes `out - scratch` and `scratch - out + size`
         * separately. */
        offs[ch - 0x20] = (uint16_t)(out - scratch);
        n = RenderGlyph(font, (char)ch, hfont, (AM2_Rle16 *)out,
                        (uint32_t)(scratch + GLYPH_SCRATCH - out));
        if (!n) {
            orig_free(scratch);
            DeleteObject(hfont);
            return 0;
        }
        out += n;
    }
    DeleteObject(hfont);

    /* Now the size is known, so take exactly that much and let the scratch go. */
    total = (uint32_t)(out - scratch);
    g_fonts[font].size = total;
    kept = (uint8_t *)orig_malloc(total);
    g_fonts[font].base = kept;
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
    if (!g_fonts[fontIndex].base)
        return;
    am2_free(g_fonts[fontIndex].base);
    g_fonts[fontIndex].base = 0;
    g_fonts[fontIndex].size = 0;
}

void __cdecl FreeAllFonts(void)
{
    int32_t i;

    for (i = 0; i < AM2_FONT_COUNT; i++)
        FreeFont(i);
}

int32_t __cdecl TextExtent(const char *text, int32_t font, int32_t out[2])
{
    int32_t        width = 0;
    const uint8_t *base  = g_fonts[font].base;
    int32_t        i;

    for (i = 0; text[i]; i++) {
        if (text[i] == '^') {                /* an escape; the character after it
                                         * counts */
            i++;
        } else if (text[i] >= 0x1F) {        /* a control, and SIGNED -- so 0x80
                                         * and up are skipped too */
            /* The glyph table indexed afresh, by the signed character. */
            width += *(const uint16_t *)(base
                         + g_fonts[font].offsets[(int32_t)text[i]]);
        }
    }

    if (out) {
        out[0] = width;
        /* The line height is the space glyph's, whatever the string was. */
        out[1] = *(const uint16_t *)(base
                                     + g_fonts[font].offsets[' ']
                                     + 2);
    }
    return width;
}

/* 0x00446AB0, eleven callers -- draw one line of text, clipped twice.
 *
 * The '^' ESCAPE recolours the rest of the line: the character after it
 * becomes the colour, sign-extended from a byte, and the original writes it
 * over its own colour ARGUMENT -- so the change lasts to the end of this call
 * and no further. It is taken only when a character follows; a '^' at the end
 * of the string is drawn as a glyph.
 *
 * Every glyph is clipped TWICE, and the order matters: first against the
 * screen rectangle, then against the caller's. The second clip works on the
 * first one's output, so a caller's rectangle reaching off-screen cannot
 * widen what the screen already refused.
 *
 * The pen advances by the glyph's own width and by nothing else -- no
 * kerning, no letter spacing -- and it advances even when the glyph was
 * clipped away entirely, which is what keeps a partly off-screen line
 * aligned with a fully visible one.
 *
 * The origin shift is applied LAST and only when drawing to the primary
 * surface, which is the windowed offset: everything above works in screen
 * coordinates and the two deltas move the result into the client area.
 *
 * Nothing is drawn at all unless a surface is locked -- the very first test,
 * before even measuring the string. */
/* Spelled as surface.cpp spells them, so the three stay one definition. */

void __cdecl DrawTextClipped(int32_t x, int32_t y, const char *text,
                             int32_t font, RECT clip, int32_t colour)
{
    int32_t         len;
    int32_t         i;
    int32_t         penX;

    if (!g_surfaceLocked)
        return;

    len  = (int32_t)strlen(text);
    penX = x;

    for (i = 0; i < len; i++) {
        const AM2_Rle16 *glyph;
        AM2_Rect         src;
        AM2_Rect         mid;
        int32_t          dx;
        int32_t          dy;
        uint16_t         width;
        uint16_t         height;

        if (text[i] == '^' && i + 1 < len) {
            colour = (int32_t)(int8_t)text[i + 1];
            i++;
            continue;
        }

        glyph =
            (const AM2_Rle16 *)(g_fonts[font].base + g_fonts[font].offsets[(int32_t)(int8_t)text[i]]);
        width  = glyph->width;
        height = glyph->height;

        src.left   = 0;
        src.top    = 0;
        src.right  = width;
        src.bottom = height;

        dx = penX;
        dy = y;

        if (ClipRect(&src, &g_screenClip,
                     &dx, &dy, &mid)
            && ClipRect(&mid, (const AM2_Rect *)&clip, &dx, &dy, &src)) {
            if ((*(LPDIRECTDRAWSURFACE *)&g_drawTarget) == (*(LPDIRECTDRAWSURFACE *)&g_primarySurface)) {
                dx += g_originDX;
                dy += g_originDY;
            }
            BlitGlyph(dx, dy, glyph, src, colour);
        }

        penX += width;
    }
}


/* 0x00446C50, four callers, all of them the edge strip's paint -- the vertical
 * twin of DrawTextClipped above, and deliberately written to the same shape so
 * the two can be read against each other.
 *
 * Identical: the lock test before anything else, the '^' escape recolouring
 * the rest of the line by overwriting the colour ARGUMENT, the double clip
 * screen-then-caller with the second working on the first one's output, and
 * the pen advancing even for a glyph that clipped away entirely.
 *
 * Different, and it is only these two things:
 *
 *   the pen x is `x - width/2` recomputed FOR EACH GLYPH, so every glyph is
 *   centred on the caller's x rather than started at it -- which is what makes
 *   a stack of differently-sized glyphs line up down a 16-pixel bar;
 *
 *   the pen advances DOWN by `height - AM2_GLYPH_STACK_KERN`, which is exactly
 *   what TextStackHeight sums over the same string. Measuring and drawing
 *   agree by construction and not by coincidence, which is worth stating
 *   because the two live in different files.
 *
 * The original reuses its own `font` and `y` argument slots as the in/out pair
 * ClipRect needs, and the first clip's source rectangle as the second clip's
 * output. Both are MSVC placing block-scoped locals, not behaviour: `dx`/`dy`
 * and `src` are ordinary locals here and VC6 lays them out the same way.
 *
 * The glyph index is the PLAIN char, so a byte above 0x7F indexes the offset
 * table backwards. Both painters do it; reproduced.
 */
void __cdecl DrawTextVertical(int32_t x, int32_t y, const char *text,
                              int32_t font, RECT clip, int32_t colour)
{
    int32_t         len;
    int32_t         i;
    int32_t         penY;

    if (!g_surfaceLocked)
        return;

    len  = (int32_t)strlen(text);
    penY = y;

    for (i = 0; i < len; i++) {
        const AM2_Rle16 *glyph;
        AM2_Rect         src;
        AM2_Rect         mid;
        int32_t          dx;
        int32_t          dy;
        uint16_t         width;
        uint16_t         height;

        if (text[i] == '^' && i + 1 < len) {
            colour = (int32_t)(int8_t)text[i + 1];
            i++;
            continue;
        }

        glyph  = (const AM2_Rle16 *)(g_fonts[font].base + g_fonts[font].offsets[(int32_t)(int8_t)text[i]]);
        width  = glyph->width;
        height = glyph->height;

        src.left   = 0;
        src.top    = 0;
        src.right  = width;
        src.bottom = height;

        dx = x - src.right / 2;
        dy = penY;

        if (ClipRect(&src, &g_screenClip,
                     &dx, &dy, &mid)
            && ClipRect(&mid, (const AM2_Rect *)&clip, &dx, &dy, &src)) {
            if ((*(LPDIRECTDRAWSURFACE *)&g_drawTarget) == (*(LPDIRECTDRAWSURFACE *)&g_primarySurface)) {
                dx += g_originDX;
                dy += g_originDY;
            }
            BlitGlyph(dx, dy, glyph, src, colour);
        }

        penY += height - AM2_GLYPH_STACK_KERN;
    }
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
    rc |= patch_replace(ADDR_DRAW_TEXT_CLIPPED, (const void *)DrawTextClipped,
                        "DrawTextClipped", 11);
    rc |= patch_replace(ADDR_DRAW_TEXT_VERTICAL, (const void *)DrawTextVertical,
                        "DrawTextVertical", 4);
    return rc;
}
