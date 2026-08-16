/* Sprite drawing entry point -- reconstructed from ArmyMen2.exe 0x00445FF0.
 *
 * The sprite counterpart to DrawText, and built from the same two pieces: clip
 * against the screen rectangle, then hand the clipped source rectangle to a
 * blitter. 24 call sites.
 *
 * The dispatcher it calls, 0x00446070, is not reconstructed yet. It is the
 * busiest thing in the 2D pipeline -- 14 call sites reaching it from every
 * large drawing composite -- and it selects between five different inner
 * blitters (0x0041C1C0, 0x0041C2B0, 0x0041C3A0, 0x0041C480, 0x00445EB0),
 * presumably transparency and blend variants of the RLE loop already
 * reconstructed as BlitGlyph.
 *
 * Worth noting: the clip rectangle at 0x00485310 is the same one DrawText uses.
 * It was named ADDR_TEXT_CLIP when only the text path was known; this function
 * is what showed it to be the general screen clip.
 */

#include "sprite.h"
#include "rect.h"
#include "../inject/patch.h"

#include <stdint.h>

#define g_screenClip (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_CLIP)

/* 0x00446070: (sprite, x, y, clipped source rect, mode). */
typedef void (__cdecl *am2_draw_sprite_clipped_fn)(AM2_Sprite *spr,
                                                   int32_t x, int32_t y,
                                                   const AM2_Rect *clipped,
                                                   int32_t mode);
#define orig_draw_sprite_clipped \
    (*(am2_draw_sprite_clipped_fn)ADDR_DRAW_SPRITE_CLIPPED)

void __cdecl DrawSprite(AM2_Sprite *spr, int32_t x, int32_t y, int32_t mode)
{
    AM2_Rect clipped;

    if (!spr)
        return;

    /* Either route will do -- a DirectDraw surface or software RLE data.
     * Neither means there is nothing to draw. */
    if (!spr->surface && !spr->rle)
        return;

    /* The caller gives the position of the sprite's hot spot, not its corner. */
    x -= spr->hotX;
    y -= spr->hotY;

    if (!ClipRect(&spr->bounds, &g_screenClip, &x, &y, &clipped))
        return;

    orig_draw_sprite_clipped(spr, x, y, &clipped, mode);
}

int sprite_install(void)
{
    return patch_replace(ADDR_DRAW_SPRITE, DrawSprite, "DrawSprite", 4);
}
