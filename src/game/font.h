#ifndef AM2_FONT_H
#define AM2_FONT_H

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"
#include "blit.h"

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

int font_install(void);

#endif /* AM2_FONT_H */
