/* Sprite drawing -- reconstructed from ArmyMen2.exe.
 *
 *   DrawSprite         0x00445FF0   24 call sites
 *   DrawSpriteClipped  0x00446070   14 call sites
 *
 * DrawSprite is the thin entry point: place by hot spot, clip, hand on.
 * DrawSpriteClipped is the centre of the 2D pipeline -- every large drawing
 * composite reaches it, and it is where the game chooses between rasterising a
 * sprite itself and handing it to DirectDraw.
 *
 * That choice is `flags & 0x3C`. With those bits set the sprite is drawn by the
 * software RLE blitters in blit.c; with them clear it goes to
 * IDirectDrawSurface::BltFast and the hardware does it. Both arms read the same
 * field at +0x10, which is why it is a union -- see sprite.h.
 *
 * Only the software arm needs a locked surface, so the lock check sits inside
 * it rather than at the top: the hardware path must NOT be holding a lock when
 * it calls BltFast, since DirectDraw needs the surface back to blit into it.
 */

#include "sprite.h"
#include "blit.h"
#include "rect.h"
#include "../inject/patch.h"

#include <stdint.h>

#define g_screenClip     (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_CLIP)
#define g_surfaceLocked  (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_lockedSurface  (*(void *const *)(uintptr_t)ADDR_LOCKED_SURFACE)
#define g_primarySurface (*(void *const *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_frameBuf       (*(void *const *)(uintptr_t)ADDR_FRAMEBUFFER)
#define g_originDX       (*(const int32_t *)(uintptr_t)ADDR_ORIGIN_DX)
#define g_originDY       (*(const int32_t *)(uintptr_t)ADDR_ORIGIN_DY)
#define g_overlayPalette (*(void **)(uintptr_t)ADDR_OVERLAY_PALETTE)
#define g_defaultPalette (*(void *const *)(uintptr_t)ADDR_DEFAULT_PALETTE)

/* 0x00445EB0, the surface-lost recovery chain: calls Restore through the
 * sprite's own surface and falls back through several drawing paths. Not
 * reconstructed, so it is called in place. */
typedef void (__cdecl *am2_restore_chain_fn)(AM2_Sprite *spr);
#define orig_restore_chain (*(am2_restore_chain_fn)ADDR_RESTORE_CHAIN)

void __cdecl DrawSprite(AM2_Sprite *spr, int32_t x, int32_t y, int32_t mode)
{
    AM2_Rect clipped;

    if (!spr)
        return;

    /* Either route will do -- a DirectDraw surface or software pixel data.
     * Neither means there is nothing to draw. */
    if (!spr->image.rle16 && !spr->overlay)
        return;

    /* The caller gives the position of the sprite's hot spot, not its corner. */
    x -= spr->hotX;
    y -= spr->hotY;

    if (!ClipRect(&spr->bounds, &g_screenClip, &x, &y, &clipped))
        return;

    DrawSpriteClipped(spr, x, y, &clipped, mode);
}

void __cdecl DrawSpriteClipped(AM2_Sprite *spr, int32_t x, int32_t y,
                               const AM2_Rect *clipped, int32_t mode)
{
    if (!spr)
        return;

    /* Drawing into the primary surface is offset; into anything else it is not.
     * Same test DrawText makes. */
    if (g_lockedSurface == g_primarySurface) {
        x += g_originDX;
        y += g_originDY;
    }

    if (spr->flags & 0x3C) {

        /* Software rasterising writes into the locked bits; without a lock
         * there is nowhere to draw. */
        if (!g_surfaceLocked)
            return;

        if (spr->image.rle16) {
            switch (spr->format) {
            case 1:
                BlitCopy32(x, y, spr->image.rle32, *clipped);
                break;
            case 2:
                /* Remapped only when the caller asks for it AND a table
                 * exists; format 3 remaps whenever a table exists. */
                if (mode && spr->lut)
                    BlitRemap16(x, y, spr->image.rle16, *clipped, spr->lut);
                else
                    BlitCopy16(x, y, spr->image.rle16, *clipped);
                break;
            case 3:
                if (spr->lut)
                    BlitRemap16(x, y, spr->image.rle16, *clipped, spr->lut);
                else
                    BlitCopy16(x, y, spr->image.rle16, *clipped);
                break;
            default:
                break;              /* unknown format: skip to the overlay */
            }
        }

        /* The optional second layer, drawn over whatever went down above. */
        if (!spr->overlay)
            return;

        g_overlayPalette = spr->palette ? spr->palette : g_defaultPalette;
        BlitOverlay(x, y, spr->overlay, *clipped);
        return;
    }

    /* Hardware path. BltFast needs the surface back, so any lock is dropped
     * first -- but this is deliberately NOT UnlockSurface. The original clears
     * only the locked flag here, leaving g_frameBuf and g_pitch still pointing
     * at the old bits, where UnlockSurface clears all three. Reproduced as
     * written; calling UnlockSurface instead would be a behaviour change.
     *
     * AM2_Rect and RECT are both four 32-bit edges in the same order, so the
     * clipped rectangle goes straight to BltFast as an LPRECT, exactly as the
     * original passes it.
     *
     * NOTE -- the DDERR_SURFACELOST branch below is UNTESTED. It needs a lost
     * surface (alt-tab, or a display mode change), which a headless Boot Camp
     * run never produces. It is one of two untested Restore paths; the other is
     * in LockSurface, where the original has a real defect. See the standing
     * note in src/game/surface.c before touching either.
     */
    {
        LPDIRECTDRAWSURFACE dest;
        DWORD               trans;
        HRESULT             hr;

        if (g_surfaceLocked) {
            IDirectDrawSurface_Unlock((LPDIRECTDRAWSURFACE)g_lockedSurface,
                                      g_frameBuf);
            g_surfaceLocked = 0;
        }
        dest = (LPDIRECTDRAWSURFACE)g_lockedSurface;

        trans = (spr->flags & 1) ? DDBLTFAST_WAIT
                                 : (DDBLTFAST_WAIT | DDBLTFAST_SRCCOLORKEY);

        hr = IDirectDrawSurface_BltFast(dest, x, y,
                                        (LPDIRECTDRAWSURFACE)spr->image.surface,
                                        (LPRECT)clipped, trans);
        if (hr == DDERR_SURFACELOST)
            orig_restore_chain(spr);
    }
}

int sprite_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DRAW_SPRITE, (const void *)DrawSprite, "DrawSprite", 4);
    rc |= patch_replace(ADDR_DRAW_SPRITE_CLIPPED, (const void *)DrawSpriteClipped,
                        "DrawSpriteClipped", 5);
    return rc;
}
