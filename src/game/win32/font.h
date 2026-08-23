#ifndef AM2_FONT_H
#define AM2_FONT_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"
#include "../blit.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Runtime font generation.
 *
 * The game does not ship its fonts as RLE. It builds them at startup: a
 * character is drawn into a scratch DirectDraw surface with GDI, the surface is
 * locked, and the pixels are run-length encoded into the glyph format the
 * blitters consume. That is where the tables at 0x006598D4 and 0x00659AD4 come
 * from.
 *
 * EncodeGlyph is the half that matters most, because it defines the glyph
 * format from the writing side and confirms independently what BlitGlyph was
 * reverse-engineered to read. It writes an AM2_Rle16 -- the same structure the
 * blitters consume, declared once in blit.h rather than described twice in
 * comments.
 *
 * The runs carry counts only and never pixel values, which is exactly why
 * BlitGlyph never advances its source pointer past a run, and why one font can
 * be drawn in any colour.
 */

/* Original: 0x004465E0. Renders one character into the scratch surface with
 * GDI, then locks it and hands it to EncodeGlyph. Returns the encoded size, or
 * 0 if the DC or the lock could not be had.
 *
 * The first argument is passed by its single caller and never read -- every
 * observed call passes 1. The character walks printable ASCII from 0x20, which
 * is how the whole font gets built: 672 glyphs a session, about seven fonts of
 * 95 characters.
 */
uint32_t __cdecl RenderGlyph(int32_t unused, char ch, HFONT font,
                             AM2_Rle16 *out, int32_t space);

/* Original: 0x004464C0. Encodes the locked surface into `out` and returns the
 * number of bytes written. Also wipes the scratch area ready for the next
 * glyph. The fourth argument is passed by the caller but never read. */
uint32_t __cdecl EncodeGlyph(AM2_Rle16 *out, int32_t width, int32_t height,
                             int32_t unused);

/* Original: 0x00446450. The game's only CreateFontA. `style` is packed --
 * bit 0 italic, bit 1 underline, bit 2 strikeout. Returns NULL for a null face
 * name without asking GDI, and logs if GDI refuses. */
HFONT __cdecl CreateGameFont(const char *face, int32_t height, uint16_t style);

/* Original: 0x004466E0. Build one font's whole glyph set: ask GDI for the face,
 * render all 256 characters, keep the encoded result. Returns 1, including when
 * the font was already built. Renders into a 32KB scratch and then copies to an
 * exact-sized allocation, because the encoded size is not knowable in advance;
 * both allocations use the game's CRT, not ours. */
int32_t __cdecl BuildFont(int32_t font);

/* 0x00446830, four callers. A cdecl wrapper for BuildFont and nothing else. */
int32_t __cdecl BuildFontAlias(int32_t fontIndex);

/* 0x00446840 and 0x00446880. Give one font's glyph bytes back and clear both
 * its table entries, and do that for all three. The second is entry 7 of
 * ShutdownSubsystems' ordered teardown -- another of those that is no longer a
 * guess. Neither touches the offsets table, only the size and the base. */
void __cdecl FreeFont(int32_t fontIndex);
void __cdecl FreeAllFonts(void);

/* 0x004468A0, nine callers. How wide a string is in a font, and how tall a
 * line is, both written through `out`.
 *
 * The width is the sum of each glyph's own first uint16 -- the same
 * AM2_Rle16 header BlitGlyph reads -- reached through the font's offset table.
 * '^' is skipped as an escape and anything below 0x1F as a control, so neither
 * contributes; nothing else is special.
 *
 * The HEIGHT does not depend on the string at all: it is the second uint16 of
 * the SPACE glyph, read through a fixed entry of the offset table. So an empty
 * string still answers a line height, and `out` is left untouched only when it
 * is null.
 *
 * It also RETURNS the width. That is not decoration and was missed when this
 * went in as `void`: the original accumulates into eax and the null-`out`
 * branch falls straight through to the `ret`, so eax is the answer. The
 * typewriter's word-wrap calls it with a null `out` and compares the return,
 * which is what surfaced it -- every earlier caller passes a real `out` and
 * ignores eax, so nothing could see it. */
int32_t __cdecl TextExtent(const char *text, int32_t font, int32_t out[2]);

int font_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_FONT_H */
