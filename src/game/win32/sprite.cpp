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
#include "../anim.h"
#include "../blit.h"
#include "../rect.h"
#include "../air.h"        /* RemapSpriteRuns, GrowSpriteList */
#include "palette.h"       /* NearestPalIndex -- reconstructed */
#include "../crt.h"        /* am2_malloc */
#include "../../inject/patch.h"

#include <stdint.h>
#include <string.h>

#define g_screenClip     (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_CLIP)
#define g_surfaceLocked  (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_drawTarget  (*(void *const *)(uintptr_t)ADDR_DRAW_TARGET)
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
    if (g_drawTarget == g_primarySurface) {
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
            IDirectDrawSurface_Unlock((LPDIRECTDRAWSURFACE)g_drawTarget,
                                      g_frameBuf);
            g_surfaceLocked = 0;
        }
        dest = (LPDIRECTDRAWSURFACE)g_drawTarget;

        trans = (spr->flags & 1) ? DDBLTFAST_WAIT
                                 : (DDBLTFAST_WAIT | DDBLTFAST_SRCCOLORKEY);

        hr = IDirectDrawSurface_BltFast(dest, x, y,
                                        (LPDIRECTDRAWSURFACE)spr->image.surface,
                                        (LPRECT)clipped, trans);
        if (hr == DDERR_SURFACELOST)
            RestoreSpriteSurface(spr);
    }
}

/* The three reload routines and the -df switch that picks between two of
 * them. All original; this function only decides which applies. */
typedef int32_t (__cdecl *am2_reload_named_fn)(AM2_Sprite *, const char *, int32_t);
typedef int32_t (__cdecl *am2_rebuild_fn)(AM2_Sprite *, int32_t);
#define orig_sprite_reload_named (*(am2_reload_named_fn)ADDR_SPRITE_RELOAD_NAMED)
#define orig_sprite_rebuild_df   (*(am2_rebuild_fn)ADDR_SPRITE_REBUILD_DF)
#define orig_sprite_rebuild_alt  (*(am2_rebuild_fn)ADDR_SPRITE_REBUILD_ALT)
#define g_optDf                  (*(const int32_t *)(uintptr_t)ADDR_OPT_DF)

/* Put a lost sprite back. See sprite.h.
 *
 * The order matters: Restore first, because a surface that will not come back
 * cannot be filled, and then the pixels, because DirectDraw hands a restored
 * surface back with its contents undefined. Restoring without refilling would
 * leave the sprite drawing garbage rather than nothing.
 *
 * `flags & 0x1D` is passed on rather than the whole word. Those are bit 0 --
 * the colour-key bit -- and bits 2, 3 and 4 of the software-path selector; bit
 * 5 is dropped. Kept exactly, since the reload routines are the original's and
 * expect what the original gave them. */
void __cdecl RestoreSpriteSurface(AM2_Sprite *spr)
{
    int32_t flags;

    if (IDirectDrawSurface_Restore(spr->image.surface) != DD_OK)
        return;

    flags = (int32_t)(spr->flags & 0x1D);

    if (spr->source) {
        if (orig_sprite_reload_named(spr, spr->source, flags))
            return;
        orig_log((const char *)(uintptr_t)ADDR_STR_RESTORE_FAIL_S, spr->source);
        return;
    }

    if (g_optDf) {
        if (orig_sprite_rebuild_df(spr, flags))
            return;
    } else {
        if (orig_sprite_rebuild_alt(spr, flags))
            return;
    }
    orig_log((const char *)(uintptr_t)ADDR_STR_RESTORE_FAIL_X, spr->id);
}

/* ---- sprite lifetime ---------------------------------------------------
 *
 * The record is 64 bytes and the original says so itself: ClearSprite blanks it
 * with `rep stosd` for 0x10 dwords. */
static_assert(sizeof(AM2_Sprite) == 0x40, "AM2_Sprite is 64 bytes");

typedef int32_t (__cdecl *am2_slot_of_fn)(uint32_t id);
#define orig_sprite_slot_of (*(am2_slot_of_fn)ADDR_SPRITE_SLOT_OF)
#define g_spriteTable       (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE)

/* What both teardowns do to the sprite's contents.
 *
 * `format` is the discriminator, and zero is the interesting value: it means
 * `image` is a DirectDraw surface and has to be Released, where anything else
 * means RLE pixel data on the game's heap and has to be freed. Getting that
 * backwards would either leak a surface or hand a COM object to free(). */
static void FreeSpriteContents(AM2_Sprite *spr)
{
    if (spr->image.surface) {
        if (spr->format == 0)
            IDirectDrawSurface_Release(spr->image.surface);
        else
            orig_free(spr->image.rle16);
        spr->image.surface = NULL;
    }
    if (spr->overlay) {
        orig_free(spr->overlay);
        spr->overlay = NULL;
    }
    /* Freed and not nulled, as the original. Both callers below make the
     * pointer unreachable immediately afterwards. */
    if (spr->source)
        orig_free(spr->source);
}

void __cdecl ClearSprite(AM2_Sprite *spr)
{
    if (!spr)
        return;
    FreeSpriteContents(spr);
    memset(spr, 0, sizeof *spr);
}

void __cdecl ReleaseSprite(AM2_Sprite *spr)
{
    int32_t slot;

    if (!spr)
        return;

    /* 0xFFFFFFFF means it was never registered, so there is no count to keep
     * and nothing to unhook -- it just goes. */
    if (spr->id != 0xFFFFFFFFu) {
        slot = orig_sprite_slot_of(spr->id);
        if (slot < 0) {
            orig_log((const char *)(uintptr_t)ADDR_STR_RELEASE_MISSING);
        } else if (g_spriteTable[slot] != spr) {
            /* The slot holds something other than us. Complain only if it
             * holds SOMETHING -- an empty slot is an ordinary double release
             * and is silent.
             *
             * The condition is on the slot's occupant, not on this sprite.
             * This read `spr->refs != 0` until the shutdown path was driven
             * for the first time and produced "Error in release: Wrong
             * sprite!" where the original produced nothing: the original
             * tests the register still holding `table[slot]` from the compare
             * immediately above, and I had misread it as the reference count
             * and then written a plausible comment explaining the wrong
             * behaviour. Nothing reaches this path before shutdown, which is
             * why it survived every A/B until the teardown was exercised. */
            if (g_spriteTable[slot] != NULL)
                orig_log((const char *)(uintptr_t)ADDR_STR_RELEASE_WRONG);
        } else {
            if (spr->refs > 0)
                spr->refs--;
            if (spr->refs > 0)
                return;              /* still wanted elsewhere */
            g_spriteTable[slot] = NULL;
        }
    }

    FreeSpriteContents(spr);
    orig_free(spr);
}

/* Let the menu's sprites go -- 0x00412F80.
 *
 * 190 of them, and the original walks them as 19 rows of ten rather than as a
 * flat run -- an inner counter of ten inside an outer bound check. The two are
 * exactly equivalent here, so this is written flat; the nesting is what a two
 * dimensional array compiles to, not a difference in behaviour.
 *
 * The slot immediately past the array is cleared too, without being released.
 * That is the original's, and it is a slot rather than a stray write: the loop
 * stops at that address precisely because it is the end of the array. */
#define g_menuSprites  ((AM2_Sprite **)(uintptr_t)ADDR_MENU_SPRITES)
#define g_menuSpritesEnd (*(uint32_t *)(uintptr_t)ADDR_MENU_SPRITES_END)
#define g_menuSurface  (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MENU_SURFACE)

#define MENU_SPRITE_COUNT \
    ((ADDR_MENU_SPRITES_END - ADDR_MENU_SPRITES) / sizeof(AM2_Sprite *))

void __cdecl FreeMenuSprites(void)
{
    uint32_t i;

    for (i = 0; i < MENU_SPRITE_COUNT; i++) {
        if (g_menuSprites[i]) {
            ReleaseSprite(g_menuSprites[i]);
            g_menuSprites[i] = NULL;
        }
    }
    g_menuSpritesEnd = 0;

    if (g_menuSurface) {
        IDirectDrawSurface_Release(g_menuSurface);
        g_menuSurface = NULL;
    }
}

typedef int32_t (__cdecl *am2_sprite_load_fn)(AM2_Sprite *spr, int32_t a,
                                              int32_t b, int32_t c,
                                              int32_t flags);
typedef void (__cdecl *am2_sprite_register_fn)(AM2_Sprite *spr, uint32_t id);

#define orig_sprite_load_triple (*(am2_sprite_load_fn)ADDR_SPRITE_LOAD_TRIPLE)
#define orig_sprite_register    (*(am2_sprite_register_fn)ADDR_SPRITE_REGISTER)

/* Allocate and fill one, or give the record back for free()ing. */
static AM2_Sprite *LoadSpriteRecord(int32_t set, int32_t index, int32_t frame,
                                    int32_t flags)
{
    AM2_Sprite *spr = (AM2_Sprite *)orig_malloc(sizeof(AM2_Sprite));

    /* The original zeroes with `rep stosd` for 0x10 dwords and does not check
     * the allocation, so neither does this. */
    memset(spr, 0, sizeof(AM2_Sprite));

    if (!orig_sprite_load_triple(spr, set, index, frame, flags)) {
        orig_free(spr);
        return 0;
    }
    return spr;
}

AM2_Sprite *__cdecl PreloadSprite(int32_t set, int32_t index, int32_t frame,
                                  int32_t flags, int32_t addref)
{
    uint32_t id = (uint32_t)(((((uint32_t)set << 12) + (uint32_t)index) << 7)
                             + (uint32_t)frame);
    int32_t slot = orig_sprite_slot_of(id);

    if (slot >= 0) {
        AM2_Sprite *spr = g_spriteTable[slot];

        /* A registered slot holding no record. Nothing reaches this in any
         * run so far, and the original's handling of it is odd enough to be
         * worth keeping rather than tidying: it builds the record and hands it
         * back without ever storing it in the slot, so the next lookup finds
         * the same empty slot again. Reproduced, not corrected. */
        if (!spr) {
            spr = LoadSpriteRecord(set, index, frame, flags);
            if (!spr)
                return 0;
            spr->id = id;
        }

        if (addref) {
            spr->refs++;
            return spr;
        }
        /* Not an increment: a floor. An existing count is left as it is. */
        if (spr->refs == 0)
            spr->refs = 1;
        return spr;
    }

    AM2_Sprite *spr = LoadSpriteRecord(set, index, frame, flags);
    if (!spr)
        return 0;

    spr->id = id;
    spr->refs = 1;
    orig_sprite_register(spr, id);
    return spr;
}

/* orig_fread comes from orig.h, where map.cpp and air.cpp already reach it --
 * a second definition here is how one name becomes two. */

#define g_spriteList    (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_LIST)
#define g_spriteListN   (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_COUNT)
#define g_spriteListCap (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_CAP)

void __cdecl LoadSpriteSet(am2_FILE *fp, const uint8_t *table, int32_t from,
                           uint32_t flags)
{
    int32_t count;
    int32_t i;

    if (orig_fread(&count, 4, 1, fp), count <= 0)
        return;

    for (i = 0; i < count; i++) {
        AM2_Sprite *spr;
        int16_t     w;
        int32_t     size;

        if (g_spriteListN >= g_spriteListCap)
            GrowSpriteList();

        spr = (AM2_Sprite *)am2_malloc(0x40);
        g_spriteList[g_spriteListN] = spr;
        /* Re-read from the list rather than reusing the pointer, as the
         * original does -- twice, once for each of the next two uses. */
        spr = g_spriteList[g_spriteListN];
        memset(spr, 0, 0x40);

        /* The original reads this one into the FILE pointer's own argument
         * slot, `fp` being live in a register by then. */
        orig_fread(&w, 2, 1, fp);
        spr->id          = 0xFFFFFFFFu;
        spr->bounds.top  = 0;
        spr->bounds.left = 0;
        spr->bounds.right = w;
        orig_fread(&w, 2, 1, fp);
        spr->bounds.bottom = w;

        orig_fread(&w, 2, 1, fp);
        spr->hotX = w;
        orig_fread(&w, 2, 1, fp);
        spr->hotY = w;

        orig_fread(&w, 2, 1, fp);
        spr->fileA = w;
        orig_fread(&w, 2, 1, fp);
        spr->fileB = w;

        /* The image, then the overlay -- each a size and that many bytes, and
         * the overlay only if its size is positive. Neither malloc nor either
         * read is checked. */
        orig_fread(&size, 4, 1, fp);
        spr->image.rle16 = (AM2_Rle16 *)am2_malloc((size_t)size);
        orig_fread(spr->image.rle16, (size_t)size, 1, fp);

        {
            int32_t overlaySize;

            orig_fread(&overlaySize, 4, 1, fp);
            if (overlaySize > 0) {
                spr->overlay = (AM2_Rle16 *)am2_malloc((size_t)overlaySize);
                orig_fread(spr->overlay, (size_t)overlaySize, 1, fp);
            }
        }

        /* The image's byte count goes to the remapper, which does not read it
         * -- the RLE header already says how far to walk. */
        RemapSpriteRuns(spr->image.rle16, size, table, from);

        spr->flags = flags;
        /* Decided from the CALLER's flags, not from the file. Neither bit
         * leaves it at zero, which sprite.h reads as "image is a surface". */
        if (flags & 0x10)
            spr->format = 3;
        else if (flags & 8)
            spr->format = 2;

        g_spriteListN += 1;
    }
}

/* orig_fopen and orig_fclose come from orig.h, like orig_fread. */
#define g_activePalette   (*(const uint32_t **)(uintptr_t)ADDR_ACTIVE_PALETTE)

int32_t __cdecl LoadSpriteFile(const char *path, AM2_AnimTable *anims,
                               const AM2_AnimTable *fallback,
                               int32_t from, uint32_t flags)
{
    am2_FILE *fp;
    uint32_t  palette[AM2_SPRITE_PALETTE_SIZE];
    uint8_t   table[AM2_SPRITE_PALETTE_SIZE];
    int32_t   i;
    int32_t   before;

    fp = orig_fopen(path, "rb");
    if (!fp)
        return 0;   /* eax still holds the null, which is what it returns. */

    orig_fread(palette, 4, AM2_SPRITE_PALETTE_SIZE, fp);

    /* Each of the file's colours becomes the closest ACTIVE palette index at
     * or above `from` -- the same threshold LoadSpriteSet then refuses to
     * rewrite below, so the reserved block is respected twice over. */
    for (i = 0; i < AM2_SPRITE_PALETTE_SIZE; i++)
        table[i] = NearestPalIndex(g_activePalette, palette[i],
                                   (uint32_t)from);

    /* Taken before the load, so the tail gets the index the new sprites start
     * at rather than the count after them. */
    before = g_spriteListN;

    LoadSpriteSet(fp, table, from, flags);
    LoadAnimTable(fp, anims, before, fallback);
    orig_fclose(fp);
    return 1;
}

int sprite_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DRAW_SPRITE, (const void *)DrawSprite, "DrawSprite", 4);
    rc |= patch_replace(ADDR_DRAW_SPRITE_CLIPPED, (const void *)DrawSpriteClipped,
                        "DrawSpriteClipped", 5);
    rc |= patch_replace(ADDR_RESTORE_CHAIN, (const void *)RestoreSpriteSurface,
                        "RestoreSpriteSurface", 1);
    rc |= patch_replace(ADDR_CLEAR_SPRITE, (const void *)ClearSprite,
                        "ClearSprite", 1);
    rc |= patch_replace(ADDR_LOAD_SPRITE_FILE, (const void *)LoadSpriteFile,
                        "LoadSpriteFile", 6);
    rc |= patch_replace(ADDR_LOAD_SPRITE_SET, (const void *)LoadSpriteSet,
                        "LoadSpriteSet", 1);
    rc |= patch_replace(ADDR_RELEASE_SPRITE, (const void *)ReleaseSprite,
                        "ReleaseSprite", 1);
    rc |= patch_replace(ADDR_FREE_MENU_SPRITES, (const void *)FreeMenuSprites,
                        "FreeMenuSprites", 0);
    rc |= patch_replace(ADDR_PRELOAD_SPRITE, (const void *)PreloadSprite,
                        "PreloadSprite", 37);
    return rc;
}
