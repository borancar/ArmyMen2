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
#include "../dist.h"   /* RoundTo8 */
#include "../packkey.h"  /* the three key fields */
#include "../image.h"  /* AM2_IMAGE */
#include "../misc.h"   /* MaskPixelSolid */
#include "../blit.h"
#include "../rect.h"
#include "../air.h"        /* RemapSpriteRuns, GrowSpriteList */
#include "palette.h"       /* NearestPalIndex -- reconstructed */
#include "surface.h"       /* CreateBitmapSurface -- reconstructed */
#include "../crt.h"        /* am2_malloc */
#include "../gamedir.h"    /* SetGameDir -- reconstructed */
#include "winmain.h"       /* Ticks -- reconstructed */
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
        if (SpriteReloadNamed(spr, spr->source, flags))
            return;
        orig_log((const char *)(uintptr_t)ADDR_STR_RESTORE_FAIL_S, spr->source);
        return;
    }

    if (g_optDf) {
        if (SpriteRebuildDf(spr, flags))
            return;
    } else {
        if (SpriteRebuildAlt(spr, flags))
            return;
    }
    orig_log((const char *)(uintptr_t)ADDR_STR_RESTORE_FAIL_X, spr->id);
}

/* ---- sprite lifetime ---------------------------------------------------
 *
 * The record is 64 bytes and the original says so itself: ClearSprite blanks it
 * with `rep stosd` for 0x10 dwords. */
static_assert(sizeof(AM2_Sprite) == 0x40, "AM2_Sprite is 64 bytes");

#define g_spriteTable       (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE)
#define g_spriteRegCount    (*(const int32_t *)(uintptr_t)ADDR_SPRITE_REG_COUNT)
#define g_spriteRegPairs    (*(const uint32_t *const *)(uintptr_t)ADDR_SPRITE_REG_PAIRS)

/* 0x00445990. The sprite id to its SLOT in g_spriteTable, or -1.
 *
 * A binary search over a second table of {id, slot} pairs kept sorted by id
 * -- so the registry is two arrays, not one, and this one exists purely to
 * make the lookup logarithmic. The comparison is UNSIGNED (`jae`), which
 * matters because an id is (((set << 12) + index) << 7) + frame and the top
 * bit is reachable. */
int32_t __cdecl SpriteSlotOf(uint32_t id)
{
    const uint32_t *tab = g_spriteRegPairs;
    int32_t         lo  = 0;
    int32_t         hi  = g_spriteRegCount;

    if (hi <= 0)
        return -1;
    do {
        int32_t mid = lo + ((hi - lo) >> 1);

        if (tab[(uint32_t)mid * 2] == id)
            return (int32_t)tab[(uint32_t)mid * 2 + 1];
        if (tab[(uint32_t)mid * 2] > id)
            hi = mid;
        else
            lo = mid + 1;
    } while (hi > lo);
    return -1;
}

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
        slot = SpriteSlotOf(spr->id);
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

/* InitMenuScreen -- original 0x00412E00, two callers. It was
 * ADDR_INIT_DIGIT_TABLE, "fills 0x004FCDF8", which is one of the five things it
 * does: free the old menu sprites, build two byte tables, load the digit
 * sprites, reset the cursor's two rects and the animation clock, and make the
 * menu's offscreen surface.
 *
 * THE FIRST TABLE SHARES ITS 256 BYTES WITH ADDR_FLAME_RECORD, which the
 * alias ratchet found and reading both confirmed: the "Flame On!" cheat hands
 * that address to SetFieldInAll as a weapon record. Two subsystems, one
 * buffer, and neither is up while the other is.
 *
 * THE TWO TABLES ARE BOTH GLYPH MAPS AND THE BIAS IS THE SAME. The first is
 * the identity except for its first ten entries, which come out 0x50 lower --
 * a digit turned into the font's glyph for it. The second walks the LIVE
 * palette: largest of the three channels, divided by 25, taken from ten,
 * clamped to 0..9, same bias. So a palette index becomes the digit glyph that
 * stands for how dark it is, and the two tables are read the same way by
 * whatever draws them.
 *
 * The division is the original's `imul 0x51EB851F; sar edx, 3` -- a signed
 * divide by 25 without a `div`. Written as the division it is.
 *
 * THE SPRITE LOOP RUNS ONE ROW PAST THE ARRAY. Its bound is inclusive and the
 * count is 0x13, so it walks rows 0..19 of a table FreeMenuSprites frees as 19
 * rows of ten. Row 19 lands on ADDR_MENU_SPRITES_END -- the cursor slot -- and
 * the six dwords after it. Two statements later this function overwrites the
 * cursor slot anyway, so the only trace an overrun could leave is in those six
 * dwords, and only if PreloadSprite answers non-null for set 0 index 19, which
 * ends the row the moment it does not. Reproduced exactly as written; the
 * bound is the original's and shortening it would be a difference.
 *
 * NOTHING LOADS UNLESS THE GAME IS IN STATE 2. Outside it the count is 0 and
 * the loop still runs ONE row -- inclusive again -- so row 0 is loaded on
 * every call and the other nineteen only in a mission.
 */
void __cdecl InitMenuScreen(void)
{
    /* The same buffer ADDR_FLAME_RECORD names -- see orig.h. Two subsystems,
     * one 256 bytes, and neither is up while the other is. */
    uint8_t       *digits  = (uint8_t *)(uintptr_t)ADDR_FLAME_RECORD;
    uint8_t       *shades  = (uint8_t *)(uintptr_t)ADDR_PALETTE_GLYPHS;
    const uint8_t *palette;
    AM2_Rect       r;
    int32_t        i, row, frame, rows;

    FreeMenuSprites();

    for (i = 0; i < 10; i++)
        digits[i] = (uint8_t)(i - AM2_GLYPH_DIGIT_BIAS);
    for (i = 10; i < 256; i++)
        digits[i] = (uint8_t)i;

    palette = *(const uint8_t *const *)(uintptr_t)ADDR_ACTIVE_PALETTE;

    for (i = 0; i < 256; i++) {
        int32_t hi = palette[i * 4];

        if (palette[i * 4 + 2] > hi)
            hi = palette[i * 4 + 2];
        if (palette[i * 4 + 1] > hi)
            hi = palette[i * 4 + 1];

        shades[i] = (uint8_t)(Clamp(10 - (hi + 4) / AM2_GLYPH_SHADE_STEP, 0, 9)
                              - AM2_GLYPH_DIGIT_BIAS);
    }

    rows = (*(const int32_t *)(uintptr_t)ADDR_GAME_STATE == 2)
           ? AM2_MENU_SPRITE_ROWS : 0;

    for (row = 0; row <= rows; row++) {
        AM2_Sprite **dst = (AM2_Sprite **)(uintptr_t)ADDR_MENU_SPRITES + row * 10;

        for (frame = 0; frame < 10; frame++) {
            dst[frame] = PreloadSprite(0, row, frame, 0, 1);
            if (!dst[frame])
                break;
        }
    }

    *(int32_t *)(uintptr_t)ADDR_MENU_ANIM_FRAME = 0;
    g_menuSpritesEnd = *(const uint32_t *)(uintptr_t)ADDR_MENU_SPRITES;
    *(uint32_t *)(uintptr_t)ADDR_MENU_ANIM_NEXT = Ticks() + MENU_ANIM_PERIOD;

    RectSet(&r, 0, 0, 0, 0);
    *(AM2_Rect *)(uintptr_t)ADDR_MENU_CURSOR_PREV = r;
    *(AM2_Rect *)(uintptr_t)ADDR_MENU_CURSOR_RECT = r;

    g_menuSurface = CreateOffscreenSurface(0x20, 0x20, 0x40, -1);
}

/* LoadBitmap is defined below; the image seam that stood here went with the
   reconstruction. */
AM2_Sprite *__cdecl LoadBitmap(const char *name, int32_t flags);

/* 0x00445CF0, fourteen callers -- get a sprite by NAME rather than by number.
 *
 * If the name is in the loader's own "%02d_%03d_%02d_*.bmp" convention it is
 * read back into the three integers and PreloadSprite does the rest, so a
 * named lookup and a numbered one land on the same sprite.
 *
 * If it is NOT, the bitmap is loaded by filename and registered under a
 * SYNTHESISED id -- PreloadSprite's own packing with set 99, frame 0, and an
 * index that is the registry count plus one. So every bitmap has an id in the
 * same key space whether or not its name carries one, and set 99 is reserved
 * for the ones that do not.
 *
 * The original computes that id as `(count + 0x63001) << 7`, which is the
 * same arithmetic with the shifts already folded.
 *
 * A sprite arriving by this route is given a reference count of ONE directly
 * rather than through the addref path, because it has just been created and
 * has exactly one holder. */
AM2_Sprite *__cdecl PreloadSpriteName(const char *name, int32_t flags,
                                      int32_t addref)
{
    int32_t     set;
    int32_t     index;
    int32_t     frame;
    uint32_t    id;
    AM2_Sprite *spr;

    if (ParseSpriteName(name, &set, &index, &frame))
        return PreloadSprite(set, index, frame, flags, addref);

    id  = (uint32_t)(((AM2_SPRITE_SET_BY_NAME << 12)
                      + *(const int32_t *)(uintptr_t)ADDR_SPRITE_REG_COUNT + 1)
                     << 7);
    spr = LoadBitmap(name, flags);
    if (!spr)
        return (AM2_Sprite *)0;

    spr->id   = id;
    spr->refs = 1;
    SpriteRegister(spr, id);
    return spr;
}

/* 0x004459E0, two callers -- put a sprite in the registry.
 *
 * The registry is two arrays that grow together: the SPRITES are appended,
 * and a table of {id, slot} PAIRS is kept sorted so a lookup can binary
 * search it. So a sprite's slot is the order it was registered in and its
 * position among the pairs is the order of its id -- two orders, one object.
 *
 * Registering an id that is already there does NOTHING: the search finds it
 * and returns, without replacing the sprite or complaining. The caller cannot
 * tell, because the function answers nothing.
 *
 * Both arrays grow by fifty at a time and only when the count has caught the
 * capacity, which is one realloc each rather than one per sprite.
 *
 * The original computes the move length as `((lo << 29) - lo + count) << 3`,
 * which is `8 * (count - lo)` exactly: `lo << 29 << 3` is `lo << 32` and
 * vanishes. Written here as what it means, with the note that the original
 * arrived at it by wraparound rather than by a multiply. */
void __cdecl SpriteRegister(AM2_Sprite *spr, uint32_t id)
{
    AM2_SpritePair **pairsp  = (AM2_SpritePair **)(uintptr_t)ADDR_SPRITE_REG_PAIRS;
    AM2_Sprite    ***slotsp  = (AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE;
    int32_t         *countp  = (int32_t *)(uintptr_t)ADDR_SPRITE_REG_COUNT;
    int32_t         *capp    = (int32_t *)(uintptr_t)ADDR_SPRITE_REG_CAP;
    int32_t          lo      = 0;
    int32_t          hi      = *countp;
    int32_t          count;

    while (hi > lo) {
        int32_t mid = lo + (hi - lo) / 2;

        if ((*pairsp)[mid].id == id)
            return;
        if ((*pairsp)[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }

    count = *countp;
    if (count >= *capp) {
        int32_t grown = *capp + AM2_SPRITE_REG_GROW;

        *slotsp = (AM2_Sprite **)am2_realloc(*slotsp, (size_t)grown * 4);
        *pairsp = (AM2_SpritePair *)am2_realloc(*pairsp, (size_t)grown * 8);
        count   = *countp;
        *capp   = grown;
    }

    (*slotsp)[count] = spr;

    if (lo < count)
        memmove(&(*pairsp)[lo + 1], &(*pairsp)[lo],
                (size_t)(count - lo) * sizeof(AM2_SpritePair));

    (*pairsp)[lo].id   = id;
    (*pairsp)[lo].slot = count;
    *countp = count + 1;
}

typedef void (__cdecl *am2_load_shadow_fn)(const char *file, AM2_Sprite *spr);
typedef int32_t (__cdecl *am2_sprintf_fn)(char *, const char *, ...);
typedef int32_t (__cdecl *am2_findfirst_fn)(const char *pattern, void *data);
typedef int32_t (__cdecl *am2_findclose_fn)(int32_t handle);

#define orig_sprintf        ((am2_sprintf_fn)AM2_IMAGE(ADDR_GAME_SPRINTF))
/* LoadShadowBmp is reconstructed above and called by name. */
#define orig_findfirst      ((am2_findfirst_fn)AM2_IMAGE(ADDR_CRT_FINDFIRST))
#define orig_findclose      ((am2_findclose_fn)AM2_IMAGE(ADDR_CRT_FINDCLOSE))
#define g_spriteSetDirs     ((const char *const *)AM2_IMAGE(ADDR_SPRITE_SET_DIRS))

/* LoadShadowBmp -- original 0x00423300, one caller: SpriteLoadTriple's loose
 * `.sha` half, so it runs only under -df. orig.h had it as "a 1-bit DIB read
 * into spr->overlay", which is where it ENDS; what it does on the way is
 * RUN-LENGTH ENCODE the bitmap.
 *
 * THE STREAM IT BUILDS, and every part of it is invented by this function:
 *
 *   int16  width
 *   int16  height
 *   int16  offset[height]   -- from the stream's own start to that row's runs
 *   uint8  runs[]           -- alternating counts of clear and set pixels,
 *                              each capped at AM2_RLE_RUN_MAX
 *
 * Every row starts with a CLEAR run, which may be zero, so the parity is
 * fixed and no run needs a tag. A row ends when the cursor reaches width - 1,
 * not width, which is the sort of off-by-one worth reproducing exactly.
 *
 * IT IS ALL BUILT IN A 32 KB STACK BUFFER and then copied into a malloc of
 * exactly the length used, which is also what it returns. Nothing bounds the
 * encoder against that buffer: a bitmap whose rows alternate every pixel
 * needs width bytes per row, so a sprite wider than about 32,000 pixels total
 * would smash the frame. The original's, and kept.
 *
 * THE DIB HEADER IS READ AS RAW OFFSETS, the way LoadDibFlipped reads its
 * own, so nothing here names a Win32 structure. Two of the fields are not
 * used for what a bitmap reader would expect:
 *
 *   THE STRIDE IS biSizeImage / biHeight, computed rather than derived from
 *   the width -- so a .sha whose biSizeImage is wrong reads garbage rather
 *   than failing, and a biSizeImage of zero is what the "invalid file size"
 *   message catches.
 *
 *   THE HOT SPOT COMES OUT OF biXPelsPerMeter AND biYPelsPerMeter, low word
 *   of each. AM2_Sprite's own comment already records that smuggling from the
 *   reader's end -- "it has only biXPelsPerMeter and biYPelsPerMeter to
 *   smuggle them through and splits them by axis" -- and this is the writer.
 *
 * THE HOT SPOT IS CLAMPED AND THE CLAMP IS SIGNED. Outside +/-0x800 on either
 * axis the value is dropped to zero, per axis, with word compares -- so a
 * negative hot spot is fine and a wild one is discarded rather than refused.
 *
 * IT ONLY TOUCHES THE REST OF THE SPRITE WHEN THERE IS NO IMAGE YET. With
 * spr->image already set, the overlay and its palette go in and the bounds,
 * hot spot, format and flag are left alone -- so a .sha loaded over a sprite
 * that already has its bitmap adds a layer without redefining it.
 *
 * ftell IS CALLED ON A FILE JUST OPENED and its answer added to bfOffBits.
 * That is zero for a plain fopen; it is reproduced because the sum is what
 * the seek uses and a reader who drops it would be surprised by an archive.
 */
int32_t __cdecl LoadShadowBmp(const char *path, AM2_Sprite *spr)
{
    uint8_t   fileHdr[AM2_BMPFILE_HDR_BYTES];
    uint8_t   infoHdr[AM2_BMPINFO_HDR_BYTES];
    uint8_t   palette[0x400];
    uint8_t   stream[0x8000];
    am2_FILE *fp;
    int32_t   base;
    uint8_t  *pixels;
    uint8_t  *out;
    uint8_t  *rowTable;
    int32_t   width, height, stride, bytes;
    int32_t   row, x, len;

    if (!spr)
        return 0;

    spr->overlay = (AM2_Rle16 *)0;

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp)
        return 0;

    base = (int32_t)orig_ftell(fp);
    orig_fread(fileHdr, AM2_BMPFILE_HDR_BYTES, 1, fp);
    orig_fread(infoHdr, AM2_BMPINFO_HDR_BYTES, 1, fp);

    if (*(const int16_t *)(infoHdr + BMPINFO_OFF_BITCOUNT) != 1) {
        orig_fclose(fp);
        orig_log((const char *)AM2_IMAGE(AM2_STR_SHA_NOT_1BIT), path);
        return 0;
    }

    orig_fread(palette, (size_t)*(const int32_t *)(infoHdr + BMPINFO_OFF_HEIGHT)
                        * 4, 1, fp);
    orig_fseek(fp, *(const int32_t *)(fileHdr + BMPFILE_OFF_BITS) + base, 0);

    bytes = *(const int32_t *)(infoHdr + BMPINFO_OFF_SIZEIMAGE);
    if (bytes <= 0) {
        orig_fclose(fp);
        orig_log((const char *)AM2_IMAGE(AM2_STR_SHA_BAD_SIZE), path);
        return 0;
    }

    pixels = (uint8_t *)am2_malloc((size_t)bytes);
    orig_fread(pixels, (size_t)bytes, 1, fp);
    orig_fclose(fp);

    height = *(const int32_t *)(infoHdr + BMPINFO_OFF_HEIGHT);
    width  = *(const int32_t *)(infoHdr + BMPINFO_OFF_WIDTH);
    stride = bytes / height;

    *(int16_t *)(stream + 0) = (int16_t)width;
    *(int16_t *)(stream + 2) = (int16_t)height;

    rowTable = stream + 4;
    out      = stream + 4 + (uint32_t)height * 2;

    for (row = 0; row < height; row++, rowTable += 2) {
        *(int16_t *)rowTable = (int16_t)(out - stream);

        x = 0;
        do {
            uint8_t n = 0;

            while (x < width && n < AM2_RLE_RUN_MAX
                   && !BitmapBitSet(pixels, x, row, height, stride)) {
                x++;
                n++;
            }
            *out++ = n;

            n = 0;
            while (x < width && n < AM2_RLE_RUN_MAX
                   && BitmapBitSet(pixels, x, row, height, stride)) {
                x++;
                n++;
            }
            *out++ = n;
        } while (x < width - 1);
    }

    am2_free(pixels);

    len = (int32_t)(out - stream);
    spr->overlay = (AM2_Rle16 *)am2_malloc((size_t)len);
    memcpy(spr->overlay, stream, (size_t)len);
    spr->palette = *(void *const *)(uintptr_t)ADDR_DEFAULT_PALETTE;

    if (!spr->image.surface) {
        int16_t hx = *(const int16_t *)(infoHdr + BMPINFO_OFF_XPELS);
        int16_t hy = *(const int16_t *)(infoHdr + BMPINFO_OFF_YPELS);

        spr->bounds.left   = 0;
        spr->bounds.top    = 0;
        spr->bounds.right  = width;
        spr->bounds.bottom = height;

        spr->hotX = hx;
        spr->hotY = hy;
        if (!(hx <= AM2_SHA_HOT_LIMIT && hx >= -AM2_SHA_HOT_LIMIT))
            spr->hotX = 0;
        if (!(hy <= AM2_SHA_HOT_LIMIT && hy >= -AM2_SHA_HOT_LIMIT))
            spr->hotY = 0;

        spr->format = AM2_SPR_FORMAT_SHADOW;
        spr->flags |= SPR_FLAG_HAS_OVERLAY;
    }

    return len;
}

/* 0x004457E0. Fill a sprite record from {set, index, frame}.
 *
 * Two halves, and the FIRST decides whether the rest of the function runs at
 * all: with ADDR_OPT_DF set -- which is every run that does not pass -df --
 * the whole thing is a tail call into the packed-data-file loader, and the
 * loose-file body below is unreachable. That is the shipped configuration, so
 * what this reconstruction mostly does in practice is choose.
 *
 * The loose half chdirs to the set's own directory and then globs for the two
 * files that make one sprite: a `.bmp` of pixels and a `.sha` of shadow. Both
 * are optional individually -- a missing one clears its field and the sprite
 * is still good -- and only losing BOTH is a failure. The names carry the
 * numbers, so the glob is `%02d_%03d_%02d_*` with anything after the third
 * number free text: `01_000_00_screen.bmp` is set 1, index 0, frame 0.
 *
 * A set of 20 or above is the map's own art and gets the loaded map directory
 * in front of it; below that the directory stands alone. Both formats and the
 * table are the original's -- see ADDR_SPRITE_SET_DIRS.
 *
 * The two patterns share one buffer, which is visible in the failure message:
 * it names the `.sha` pattern, never the `.bmp` one, because by then the
 * second sprintf has overwritten the first. Reproduced rather than tidied.
 *
 * What `tools/ab.sh df` covers and what it does not, measured both ways. The
 * install ships exactly one loose sprite, so a run reaches the found arm once
 * and the missing arm twenty times, and clearing the wrong field on a missing
 * `.sha` costs an extra log line and 303,757 pixels. The `set >= 20` arm is
 * NOT covered: only sets 0, 1 and 3 arrive, because there is no menu under
 * -df and so no map, and replacing the test with `if (0)` passes the
 * configuration unchanged. That branch and the map directory in front of it
 * stay verified by reading. */
int32_t __cdecl SpriteLoadTriple(AM2_Sprite *spr, int32_t set, int32_t index,
                                 int32_t frame, int32_t flags)
{
    char    pattern[0x100];
    uint8_t found[0x118];
    char    dir[0x100];
    int32_t handle;

    if (g_optDf)
        return SpriteLoadFromDataFile(spr, set, index, frame, flags);

    if (set >= 20)
        orig_sprintf(dir, (const char *)AM2_IMAGE(ADDR_STR_FMT_DIR_SUB),
                     (const char *)AM2_IMAGE(ADDR_MAP_BLOCK),
                     g_spriteSetDirs[set]);
    else
        orig_sprintf(dir, (const char *)AM2_IMAGE(ADDR_STR_FMT_S),
                     g_spriteSetDirs[set]);
    SetGameDir(dir);

    orig_sprintf(pattern, (const char *)AM2_IMAGE(ADDR_STR_GLOB_BMP),
                 set, index, frame);
    handle = orig_findfirst(pattern, found);
    if (handle == -1) {
        spr->image.rle16 = 0;
    } else {
        SpriteReloadNamed(spr, (const char *)(found + AM2_FIND_OFF_NAME),
                                 flags);
        orig_findclose(handle);
    }

    orig_sprintf(pattern, (const char *)AM2_IMAGE(ADDR_STR_GLOB_SHA),
                 set, index, frame);
    handle = orig_findfirst(pattern, found);
    if (handle == -1) {
        spr->overlay = 0;
    } else {
        LoadShadowBmp((const char *)(found + AM2_FIND_OFF_NAME), spr);
        orig_findclose(handle);
    }

    if (!spr->image.rle16 && !spr->overlay) {
        orig_log((const char *)(uintptr_t)ADDR_STR_SPRITE_MISSING, pattern);
        return 0;
    }
    return 1;
}

/* 0x004230F0. A bitmap record from a file NAME.
 *
 * Open, read one DIB chunk, hand the header and the pixels to MakeBitmap, free
 * the pixels. Everything below it is already ours -- ReadDibChunk fills the
 * 0x428-byte header (ten dwords then a 256-entry palette) and MakeBitmap turns
 * it into the 0x1C-byte record the sprite fields come out of.
 *
 * The file is closed BEFORE the pixels are checked, which is the order the
 * original writes and the only order that does not leak the handle on a failed
 * read. `remap` is passed as NULL, so MakeBitmap builds its own table from the
 * display palette. */
int32_t __cdecl LoadBitmapDescriptor(const char *name, void *out)
{
    uint8_t   header[0x428];
    am2_FILE *fp;
    void     *pixels;
    int32_t   rc;

    if (!name || !name[0])
        return 0;

    fp = orig_fopen(name, (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp) {
        orig_log((const char *)(uintptr_t)ADDR_STR_BITMAP_OPEN_FAIL);
        return 0;
    }

    pixels = ReadDibChunk(fp, header);
    orig_fclose(fp);
    if (!pixels)
        return 0;

    rc = MakeBitmap((const uint32_t *)header, pixels, (uint8_t *)out, 0);
    orig_free(pixels);
    return rc;
}

/* Keep a hot spot only if it is plausible.
 *
 * The two dwords it comes out of are the DIB header's biXPelsPerMeter and
 * biYPelsPerMeter, so an ordinary paint program's output lands here as a real
 * resolution: every bitmap in this install carries 2834 (72 dpi) or 5038 (128
 * dpi), read out of the files themselves. Both are outside this range, so the
 * clamp fires on every sprite this path loads and they all get (0,0).
 *
 * That it FIRES is measured; that it MATTERS is not, and the difference was
 * worth finding out rather than assuming. Deleting the clamp outright changes
 * not one pixel of `ab.sh df`, the only configuration that runs this code --
 * so whatever consumes the hot spot, it is not what places the backdrop that
 * configuration draws. Making the function fail instead costs 300,424 pixels
 * and a log line, so the code around it is thoroughly observed and this is a
 * term that does not matter rather than a path that does not run. The clamp
 * stays verified by reading. */
static int16_t ClampHotSpot(int16_t v)
{
    return (v > 2048 || v < -2048) ? 0 : v;
}

/* 0x004456B0. Reload a sprite from a named bitmap.
 *
 * The record's flags word is an IN parameter as well as an out one: the
 * caller's flags go in before the load, because MakeBitmap reads
 * BMP_FLAG_RESERVE10 and the software-path bits out of it to decide what to
 * build. What comes back is merged into the sprite one way -- `(record & 0x1C)
 * | (caller & 1)` is OR'd IN and never clears a bit, the same as the data-file
 * path.
 *
 * The hot spot and the two file fields share two dwords, split by AXIS rather
 * than in struct order: x carries {hotX, attachX} and y carries {hotY, attachY}.
 * That is not how the data file packs the same four values -- see sprite.h --
 * and it is a consequence of having only the two resolution fields to put them
 * in. All four are clamped; the archive's are not, because the archive is the
 * game's own file and a .bmp on disk is whatever a tool wrote. */
int32_t __cdecl SpriteReloadNamed(AM2_Sprite *spr, const char *name,
                                  int32_t flags)
{
    uint8_t  rec[AM2_BMP_RECORD_SIZE];
    int32_t  x, y;
    uint32_t merged;

    *(uint32_t *)(rec + BMP_OFF_FLAGS) = (uint32_t)flags;

    if (!LoadBitmapDescriptor(name, rec)) {
        spr->image.rle16 = 0;
        return 0;
    }

    spr->image.rle16 = *(AM2_Rle16 **)(rec + BMP_OFF_SURFACE);
    spr->keyIndex    = rec[BMP_OFF_KEY];

    spr->bounds.top    = 0;
    spr->bounds.left   = 0;
    spr->bounds.right  = *(const int32_t *)(rec + BMP_OFF_WIDTH);
    spr->bounds.bottom = *(const int32_t *)(rec + BMP_OFF_HEIGHT);

    x = *(const int32_t *)(rec + BMP_OFF_HOT_X);
    y = *(const int32_t *)(rec + BMP_OFF_HOT_Y);

    spr->hotX  = ClampHotSpot((int16_t)x);
    spr->hotY  = ClampHotSpot((int16_t)y);
    spr->attachX = ClampHotSpot((int16_t)(x >> 16));
    spr->attachY = ClampHotSpot((int16_t)(y >> 16));

    merged = (*(const uint32_t *)(rec + BMP_OFF_FLAGS) & 0x1Cu)
             | ((uint32_t)flags & 1u);
    spr->flags |= merged;

    if (spr->flags & 4)
        spr->format = 1;
    else if (spr->flags & 8)
        spr->format = 2;
    else if (spr->flags & 0x10)
        spr->format = 3;
    else
        spr->format = 0;

    return 1;
}

/* ---- the packed data file ----------------------------------------------
 *
 * Three sets -- title, shared and the map's own -- each a palette, two remap
 * tables, an open file and a directory of {key, offset} sorted by key. This is
 * where every sprite in a shipped run comes from; the loose-file path above is
 * only reachable under -df.
 *
 * The key is the sprite id, and that is what identifies PackKey's three fields
 * as {set, index, frame}. See packkey.h.
 */
#define SET_FILE(s)  (*(am2_FILE **)((uint8_t *)(s) + SPRITE_SET_OFF_FILE))
#define SET_DIR(s)   (*(AM2_SpriteDirEntry **)((uint8_t *)(s) + \
                                               SPRITE_SET_OFF_DIR))
#define SET_COUNT(s) (*(int32_t *)((uint8_t *)(s) + SPRITE_SET_OFF_DIR_COUNT))

/* 0x00423940. Which of the three sets holds a key.
 *
 * Sets 1..9 are the title archive, 20 and up the map's own, and everything
 * else -- 0, and 10..19 -- the shared one. The same bands ADDR_SPRITE_SET_DIRS
 * splits on: one directory name per set on the loose side, one open archive
 * per BAND on this one. */
void *__cdecl SpriteSetForKey(uint32_t key)
{
    int32_t set = (int32_t)KeyFieldA(key);

    if (set >= 20)
        return (void *)AM2_IMAGE(ADDR_SPRITE_SET_THIRD);
    if (set < 1 || set > 9)
        return (void *)AM2_IMAGE(ADDR_SPRITE_SET_SHARED);
    return (void *)AM2_IMAGE(ADDR_SPRITE_SET_TITLE);
}

/* 0x00423D50. The directory index for a key, or -1.
 *
 * A binary search by halving. The compare is SIGNED (`jge`) where the sprite
 * registry's own search in SpriteSlotOf is UNSIGNED (`jae`) -- two tables
 * holding the same kind of id, disagreeing about it. Reproduced: a key with
 * bit 31 set would be searched differently by the two, and PackKey only
 * produces 26 bits, so nothing can tell them apart. */
int32_t __cdecl SpriteDirIndex(void *set, uint32_t key)
{
    const AM2_SpriteDirEntry *dir;
    int32_t lo = 0;
    int32_t hi = SET_COUNT(set);

    if (hi <= 0)
        return -1;

    dir = SET_DIR(set);
    do {
        int32_t mid = lo + ((hi - lo) >> 1);

        if (dir[mid].key == key)
            return mid;
        if ((int32_t)dir[mid].key < (int32_t)key)
            lo = mid + 1;
        else
            hi = mid;
    } while (hi > lo);

    return -1;
}

#define SET_FOLDER(s) ((char *)((uint8_t *)(s) + SPRITE_SET_OFF_FOLDER))
#define SET_NAME(s)   ((char *)((uint8_t *)(s) + SPRITE_SET_OFF_NAME))

/* 0x004236A0. Which record a set name means, and what its archive must be.
 *
 * Three names and three records, and the last arm is not a name at all:
 * anything that is neither "title" nor "shared" is a MAP, whose archive is
 * `<map>\objects\objects.dat`. That is the same 20-and-above band
 * SpriteSetForKey picks the third record for, arrived at from the other end.
 *
 * The return value is "nothing to do": each arm compares what the record
 * already holds against what it would be set to, and answers 1 when they
 * match. The two fixed arms compare the FILE name, the map arm compares the
 * FOLDER -- because the file name is "objects.dat" for every map and only the
 * folder distinguishes them. Getting that pair the wrong way round would make
 * every map after the first reuse the previous map's archive.
 *
 * The fixed arms also clear the folder, which is how their archives end up
 * being opened from the game root rather than from wherever the last map
 * left the current directory. */
int32_t __cdecl SpriteSetResolve(const char *name, void **set, uint32_t *id)
{
    char folder[0x40];
    char file[0x40];

    if (strcmp(name, (const char *)AM2_IMAGE(ADDR_STR_SET_TITLE)) == 0) {
        void *rec = (void *)AM2_IMAGE(ADDR_SPRITE_SET_TITLE);

        *set = rec;
        *id  = AM2_DAT_ID_TITLE;
        if (strcmp(SET_NAME(rec),
                   (const char *)AM2_IMAGE(ADDR_STR_DAT_TITLE)) == 0)
            return 1;
        SET_FOLDER(rec)[0] = '\0';
        strcpy(SET_NAME(rec), (const char *)AM2_IMAGE(ADDR_STR_DAT_TITLE));
        return 0;
    }

    if (strcmp(name, (const char *)AM2_IMAGE(ADDR_STR_SET_SHARED)) == 0) {
        void *rec = (void *)AM2_IMAGE(ADDR_SPRITE_SET_SHARED);

        *set = rec;
        *id  = AM2_DAT_ID_SHARED;
        if (strcmp(SET_NAME(rec),
                   (const char *)AM2_IMAGE(ADDR_STR_DAT_SHARED)) == 0)
            return 1;
        SET_FOLDER(rec)[0] = '\0';
        strcpy(SET_NAME(rec), (const char *)AM2_IMAGE(ADDR_STR_DAT_SHARED));
        return 0;
    }

    {
        void *rec = (void *)AM2_IMAGE(ADDR_SPRITE_SET_THIRD);

        *set = rec;
        *id  = AM2_DAT_ID_OBJECTS;
        orig_sprintf(folder, (const char *)AM2_IMAGE(ADDR_STR_FMT_OBJECTS_DIR),
                     name);
        strcpy(file, (const char *)AM2_IMAGE(ADDR_STR_DAT_OBJECTS));
        if (strcmp(SET_FOLDER(rec), folder) == 0)
            return 1;
        strcpy(SET_FOLDER(rec), folder);
        strcpy(SET_NAME(rec), file);
        return 0;
    }
}

/* 0x00423970. Close a set's archive and free its directory.
 *
 * Two things it does NOT do, both reproduced. It never clears the file or the
 * directory pointer, so both dangle afterwards and a second call on the same
 * record would close the file twice -- the guard is on the file being
 * non-NULL, which it still is. Nothing does call it twice: SpriteSetLoad
 * reassigns the file on the next line, and FreeSpriteSets runs once at
 * shutdown. And it clears four bytes at the front of the FOLDER rather than
 * one, which is a dword store where a terminator would have done; kept as a
 * dword so the record compares byte for byte. */
void __cdecl SpriteSetFree(void *set)
{
    am2_FILE *fp = SET_FILE(set);

    if (!fp)
        return;

    orig_fclose(fp);
    if (SET_DIR(set))
        orig_free(SET_DIR(set));
    *(uint32_t *)set = 0;
}

/* 0x004239B0. Open a set's archive and read everything but the sprites.
 *
 * Header, 256-entry palette, the two remap tables, then the directory. The
 * remap is what maps the archive's own palette onto the display's: each entry
 * is the nearest display index to that colour, and with no display palette
 * loaded it is the identity instead. The second table is the first with
 * entries 0..9 forced back to the identity -- the reserved block, the same
 * convention BMP_FLAG_RESERVE10 names -- and SpriteLoadFromDataFile picks
 * between them by format.
 *
 * The file name is copied out and straight back in around SpriteSetFree, and
 * that round trip is a no-op: the free clears the FOLDER, not the name.
 * Reproduced rather than dropped, because "this cannot matter" is a claim
 * about a function that may yet be read again.
 *
 * The directory is allocated, zeroed and then read over in full, so the zeroing
 * cannot be observed either. Also reproduced. */
int32_t __cdecl SpriteSetLoad(const char *name)
{
    uint8_t  *set;
    uint32_t  want = 0;
    uint32_t  id   = 0;
    char      file[0x100];
    am2_FILE *fp;
    int32_t   i;
    int32_t   count;

    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    if (SpriteSetResolve(name, (void **)&set, &want))
        return SET_FILE(set) != 0;

    SetGameDir(SET_FOLDER(set));

    strcpy(file, SET_NAME(set));
    if (SET_FILE(set))
        SpriteSetFree(set);
    strcpy(SET_NAME(set), file);

    SET_FILE(set) = orig_fopen(SET_NAME(set),
                               (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!SET_FILE(set)) {
        orig_log((const char *)(uintptr_t)ADDR_STR_DAT_OPEN_FAIL,
                 SET_NAME(set));
        return 0;
    }
    fp = SET_FILE(set);

    orig_fread(&id, 4, 1, fp);
    if (id != want) {
        orig_log((const char *)(uintptr_t)ADDR_STR_DAT_BAD_ID, SET_NAME(set));
        return 0;
    }

    orig_fread(set + SPRITE_SET_OFF_PALETTE, 4, 0x100, fp);

    {
        const uint32_t *pal = (const uint32_t *)(set + SPRITE_SET_OFF_PALETTE);
        uint8_t        *remap = set + SPRITE_SET_OFF_REMAP;
        const uint32_t *display = *(const uint32_t *const *)
                                      (uintptr_t)ADDR_ACTIVE_PALETTE;

        if (display) {
            /* The original pushes ONE argument here, not two -- `add esp, 0x10`
             * after this pair of calls covers exactly one push for the swap and
             * three for the match. SwapColourBytes reads no second parameter,
             * so what the original leaves in that slot is whatever was on the
             * stack; zero is the same function. */
            for (i = 0; i < 256; i++)
                remap[i] = NearestPalIndex(display,
                                           SwapColourBytes(pal[i], 0), 0);
        } else {
            for (i = 0; i < 256; i++)
                remap[i] = (uint8_t)i;
        }

        for (i = 0; i < AM2_PALETTE_RESERVED; i++)
            set[SPRITE_SET_OFF_REMAP10 + i] = (uint8_t)i;
        for (i = AM2_PALETTE_RESERVED; i < 256; i++)
            set[SPRITE_SET_OFF_REMAP10 + i] = remap[i];
    }

    orig_fread(set + SPRITE_SET_OFF_DIR_COUNT, 4, 1, fp);
    count = SET_COUNT(set);
    if (count > 0) {
        SET_DIR(set) = (AM2_SpriteDirEntry *)
                           orig_malloc((size_t)count * 8);
        memset(SET_DIR(set), 0, (size_t)count * 8);
        orig_fread(SET_DIR(set), (size_t)count * 8, 1, fp);
    }
    return 1;
}

/* 0x00423FE0. Fill a sprite record from the packed data file.
 *
 * Seek to the entry, check the key it starts with, then read the record: two
 * dimensions, a flags word, the hot spot, the two file fields, and one or two
 * length-prefixed blocks. The dimensions become the bounds rectangle with its
 * origin at zero, which is why left and top are written as constants.
 *
 * `format` is derived from the flags word rather than stored: bit 2 gives 1,
 * bit 3 gives 2, bit 4 gives 3, and none of them gives 0. Zero is the hardware
 * arm, and it is the one that goes to CreateBitmapSurface -- the other three
 * malloc the block and remap it in place. That is the same selector sprite.h's
 * union is decided by; here is where it is written.
 *
 * Which REMAP table applies is the only thing the format changes at the tail:
 * format 3 uses the one whose first ten entries are the identity. Both arms
 * pass the format itself as RemapRleRuns' `wide`, so the branch is about the
 * table alone -- reproduced as the original wrote it rather than folded.
 *
 * That table choice is NOT checked by anything, and it is worth saying so
 * rather than letting three passing mutations read as full coverage. Making
 * SpriteDirIndex always miss puts bootcamp 293,671 pixels out and drops four
 * of its log lines, and so does making SpriteSetForKey always answer the
 * shared archive -- so this function certainly runs and its archive choice is
 * certainly observed. But swapping REMAP10 for REMAP on the format-3 arm
 * leaves bootcamp at its usual 22 pixels with an identical log. Either no
 * format-3 sprite loads there or the two tables agree on the indices those
 * sprites use; the reserved-block distinction stays verified by reading.
 *
 * The failure path frees both blocks and clears them, and it cannot fire: both
 * were cleared on the way in and nothing between there and here writes either.
 * Reproduced.
 *
 * The original reads each dword into its own ARGUMENT slots -- MSVC reusing
 * the incoming stack -- so `frame` and `spr` are overwritten as it goes. Only
 * `flags` survives to be read at the end, which is why locals here are
 * equivalent rather than merely tidier. */
int32_t __cdecl SpriteLoadFromDataFile(AM2_Sprite *spr, int32_t set,
                                       int32_t index, int32_t frame,
                                       int32_t flags)
{
    const AM2_SpriteDirEntry *dir;
    uint8_t  *sset;
    am2_FILE *fp;
    uint32_t  key;
    int32_t   slot;
    int32_t   value;
    int32_t   len;
    int32_t   ok = 0;

    spr->image.rle16 = 0;
    spr->overlay     = 0;

    key  = PackKey((uint32_t)set, (uint32_t)index, (uint32_t)frame);
    sset = (uint8_t *)SpriteSetForKey(key);
    if (!sset)
        return 0;

    slot = SpriteDirIndex(sset, key);
    if (slot < 0)
        return 0;

    fp  = SET_FILE(sset);
    dir = SET_DIR(sset);

    if (orig_fseek(fp, (int32_t)dir[slot].offset, 0) != 0) {
        orig_log((const char *)(uintptr_t)ADDR_STR_DF_SEEK_FAIL,
                 dir[slot].offset);
    } else {
        orig_fread(&value, 4, 1, fp);
        if ((uint32_t)value == key)
            ok = 1;
        else
            orig_log((const char *)(uintptr_t)ADDR_STR_DF_BAD_OBJECT);
    }

    if (!ok) {
        if (spr->image.rle16)
            orig_free(spr->image.rle16);
        if (spr->overlay)
            orig_free(spr->overlay);
        spr->image.rle16 = 0;
        spr->overlay     = 0;
        return 0;
    }

    spr->id = key;

    orig_fread(&value, 4, 1, fp);
    spr->bounds.top   = 0;
    spr->bounds.left  = 0;
    spr->bounds.right = value;

    orig_fread(&value, 4, 1, fp);
    spr->bounds.bottom = value;

    orig_fread(&value, 4, 1, fp);
    spr->flags = (uint32_t)value;
    if (value & 4)
        spr->format = 1;
    else if (value & 8)
        spr->format = 2;
    else if (value & 0x10)
        spr->format = 3;
    else
        spr->format = 0;

    /* The caller may force the colour-key bit on; it can never turn one off. */
    if (flags & 1)
        spr->flags = (uint32_t)(value | 1);

    /* Both halves of each pair in one four-byte read, as the file stores
     * them and as the original reads them. */
    orig_fread(&spr->hotX, 4, 1, fp);
    orig_fread(&spr->attachX, 4, 1, fp);

    orig_fread(&len, 4, 1, fp);
    if (len > 0) {
        if (spr->format == 0) {
            spr->image.surface =
                CreateBitmapSurface(fp, (uint32_t)len, spr->bounds.right,
                                    spr->bounds.bottom,
                                    sset + SPRITE_SET_OFF_REMAP,
                                    (uint32_t)flags);
        } else {
            spr->image.rle16 = (AM2_Rle16 *)orig_malloc((size_t)len);
            orig_fread(spr->image.rle16, (size_t)len, 1, fp);
            if (spr->format == 3)
                RemapRleRuns(spr->image.rle16, (void *)(uintptr_t)len, 3,
                             sset + SPRITE_SET_OFF_REMAP10);
            else
                RemapRleRuns(spr->image.rle16, (void *)(uintptr_t)len,
                             (int32_t)spr->format,
                             sset + SPRITE_SET_OFF_REMAP);
        }
    }

    orig_fread(&len, 4, 1, fp);
    if (len > 0) {
        spr->overlay = (AM2_Rle16 *)orig_malloc((size_t)len);
        orig_fread(spr->overlay, (size_t)len, 1, fp);
    }
    return 1;
}

/* 0x00445C00. Put a lost sprite's pixels back from LOOSE files.
 *
 * SpriteLoadTriple's second half with the key taken from spr->id instead of
 * from arguments: the same three buffers, the same two directory formats, the
 * same glob. Two differences, both the original's. It never looks for the
 * `.sha`, so a lost surface cannot restore an overlay; and it returns
 * SpriteReloadNamed's result rather than a flat 1.
 *
 * The key is unpacked INLINE here where SpriteRebuildDf calls KeyFieldA/B/C
 * for the same three fields, and the set is compared UNSIGNED (`jb`) where
 * SpriteLoadTriple compares it signed (`jl`). Seven bits cannot be negative so
 * nothing can tell either pair apart; both are transcribed as written rather
 * than made to match.
 *
 * NEITHER rebuild is exercised, and not for want of a drive. They run only
 * when DirectDraw takes a surface back, which needs an alt-tab or a mode
 * change, and nothing under Xvfb does either. A clean `ab.sh all` after this
 * proves the SEAMS -- RestoreSpriteSurface calls both by name now, so a wrong
 * signature would show at once -- and reaches no line of either body. Both are
 * verified by reading. Anyone on a real display should alt-tab out of a
 * mission and back, which is the same thing RestoreTileSet has been waiting
 * for. */
int32_t __cdecl SpriteRebuildAlt(AM2_Sprite *spr, int32_t flags)
{
    char     dir[0x100];
    char     pattern[0x100];
    uint8_t  found[0x118];
    int32_t  handle;
    int32_t  rc;
    uint32_t id    = spr->id;
    uint32_t set   = (id >> 19) & 0x7Fu;
    uint32_t index = (id >> 7) & 0x3FFu;
    uint32_t frame = id & 0x7Fu;

    if (set >= 20)
        orig_sprintf(dir, (const char *)AM2_IMAGE(ADDR_STR_FMT_DIR_SUB),
                     (const char *)AM2_IMAGE(ADDR_MAP_BLOCK),
                     g_spriteSetDirs[set]);
    else
        orig_sprintf(dir, (const char *)AM2_IMAGE(ADDR_STR_FMT_S),
                     g_spriteSetDirs[set]);
    SetGameDir(dir);

    orig_sprintf(pattern, (const char *)AM2_IMAGE(ADDR_STR_GLOB_BMP),
                 (int32_t)set, (int32_t)index, (int32_t)frame);
    handle = orig_findfirst(pattern, found);
    if (handle == -1) {
        spr->image.rle16 = 0;
        return 0;
    }

    rc = SpriteReloadNamed(spr, (const char *)(found + AM2_FIND_OFF_NAME),
                           flags);
    orig_findclose(handle);
    return rc;
}

/* 0x004243B0. Put a lost sprite's pixels back from the packed data file.
 *
 * With no image at all there is nothing to put back INTO, so it delegates to
 * the full loader -- and that call is the fifth place the three key accessors
 * are used as {set, index, frame}. Otherwise it repeats the seek and the key
 * check and then reads SIX header dwords it throws away: width, height, flags,
 * the hot pair, the file pair. Only the length is kept. It does not compute
 * where the pixels start, it re-walks the header to get there, which is why
 * that read sequence has to match SpriteLoadFromDataFile's exactly.
 *
 * Four conditions share the failure path and two of them are not failures in
 * any ordinary sense. A zero length is one. The other is `spr->format != 0`: a
 * software-format sprite holds a malloc'd RLE rather than a surface and cannot
 * have lost anything, so arriving here with one means the CALLER was wrong --
 * and the response is to free both blocks and zero them. Reproduced. */
int32_t __cdecl SpriteRebuildDf(AM2_Sprite *spr, int32_t flags)
{
    const AM2_SpriteDirEntry *dir;
    uint8_t  *sset;
    am2_FILE *fp;
    int32_t   slot;
    int32_t   value;
    int32_t   len = 0;
    int32_t   i;
    int32_t   ok = 0;

    if (!spr->image.rle16)
        return SpriteLoadFromDataFile(spr, (int32_t)KeyFieldA(spr->id),
                                      (int32_t)KeyFieldB(spr->id),
                                      (int32_t)KeyFieldC(spr->id), flags);

    sset = (uint8_t *)SpriteSetForKey(spr->id);
    if (!sset)
        return 0;

    slot = SpriteDirIndex(sset, spr->id);
    if (slot < 0)
        return 0;

    fp  = SET_FILE(sset);
    dir = SET_DIR(sset);

    if (orig_fseek(fp, (int32_t)dir[slot].offset, 0) != 0) {
        orig_log((const char *)(uintptr_t)ADDR_STR_DF_SEEK_FAIL,
                 dir[slot].offset);
    } else {
        orig_fread(&value, 4, 1, fp);
        if ((uint32_t)value == spr->id) {
            /* The header again, discarded: width, height, flags, hot, file.
             * The sixth read is the one that matters. */
            for (i = 0; i < 5; i++)
                orig_fread(&value, 4, 1, fp);
            orig_fread(&len, 4, 1, fp);
            if (len > 0 && spr->format == 0)
                ok = 1;
        } else {
            orig_log((const char *)(uintptr_t)ADDR_STR_DF_BAD_OBJECT);
        }
    }

    if (!ok) {
        if (spr->image.rle16)
            orig_free(spr->image.rle16);
        if (spr->overlay)
            orig_free(spr->overlay);
        spr->image.rle16 = 0;
        spr->overlay     = 0;
        return 0;
    }

    return ReloadBitmapSurface(spr->image.surface, fp, (uint32_t)len,
                               spr->bounds.right, spr->bounds.bottom,
                               sset + SPRITE_SET_OFF_REMAP, (uint32_t)flags);
}

/* Allocate and fill one, or give the record back for free()ing. */
static AM2_Sprite *LoadSpriteRecord(int32_t set, int32_t index, int32_t frame,
                                    int32_t flags)
{
    AM2_Sprite *spr = (AM2_Sprite *)orig_malloc(sizeof(AM2_Sprite));

    /* The original zeroes with `rep stosd` for 0x10 dwords and does not check
     * the allocation, so neither does this. */
    memset(spr, 0, sizeof(AM2_Sprite));

    if (!SpriteLoadTriple(spr, set, index, frame, flags)) {
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
    int32_t slot = SpriteSlotOf(id);

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
    SpriteRegister(spr, id);
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
        spr->attachX = w;
        orig_fread(&w, 2, 1, fp);
        spr->attachY = w;

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

/* 0x00446290, two callers -- the roach and vehicle mask builders. Is this
 * sprite opaque at this point? The name is ours.
 *
 * A sprite with no image is never solid. Otherwise the answer comes from
 * `format`, the same field sprite.h's teardown reads: 1 is the 32-bit row
 * table and 2 or 3 the 16-bit one, so those three ask the run-length mask
 * itself; anything else -- which means 0, an image that is a DirectDraw
 * surface -- falls back to the bounding box.
 *
 * The original reads the packed point twice at overlapping offsets, `[esp+8]`
 * whole and `[esp+0xA]` for the upper half, so both mask arguments arrive with
 * junk above their sixteen bits. Both testers cast to uint16_t on the way in,
 * so passing the two halves is the same function.
 */
int32_t __cdecl SpriteSolidAt(AM2_Sprite *spr, AM2_Point at)
{
    if (!spr->image.rle16)
        return 0;
    if (spr->format == 1)
        return MaskPixelSolid32((uint32_t)at.x, (uint32_t)at.y,
                                spr->image.rle32);
    if (spr->format == 2 || spr->format == 3)
        return MaskPixelSolid((uint32_t)at.x, (uint32_t)at.y,
                              spr->image.rle16);
    return PointInRect(&spr->bounds, &at);
}

/* The three animation lookups -- 0x0044BB30, 0x0045D9B0 and 0x0045DA20, and
 * all three names are ours. Each finds the entry with a fixed id in its kind's
 * table, turns an 8-bit heading into one of the animation's directions, and
 * returns the sprite for frame 0 of that facing.
 *
 * They are the proof that anim.h's field names are right rather than plausible.
 * `directionBits` goes to RoundTo8 as its bit count, so it really is the log of
 * the facing count; the cell index is `frames * facing`, so `frames` really is
 * the inner stride and the grid really is facing-major. Nothing in the loader
 * could have settled either.
 *
 * On this side of the split rather than in anim.cpp because the return is an
 * AM2_Sprite, which carries an LPDIRECTDRAWSURFACE.
 */
#define g_soldierAnims ((AM2_AnimTable *)(uintptr_t)ADDR_SOLDIER_ANIMS)
#define g_turretAnims  ((AM2_AnimTable *)(uintptr_t)ADDR_TURRET_ANIMS)
#define g_vehicleAnims ((AM2_AnimTable *)(uintptr_t)ADDR_VEHICLE_ANIMS)

/* Written once and used three times, where the original writes it out three
 * times. The index it returns is the caller's business: two of them fall back
 * to entry 0 and one does not. */
static int32_t AnimIndexOfId(const AM2_AnimTable *t, int32_t id)
{
    int32_t i = 0;

    if (t->count > 0)
        for (i = 0; i < t->count; i++)
            if (t->entries[i].id == id)
                break;
    return i;
}

static AM2_Sprite *AnimSpriteAt(const AM2_Anim *a, uint32_t heading)
{
    int32_t facing = RoundTo8((int32_t)(heading & 0xFFu), a->directionBits) & 0xFF;

    return g_spriteList[a->cells[(int32_t)a->frames * facing].sprite];
}

AM2_Sprite *__cdecl SoldierAnimSprite(int32_t kind, uint32_t heading)
{
    const AM2_AnimTable *t = &g_soldierAnims[kind];
    int32_t              i = AnimIndexOfId(t, AM2_ANIM_ID_STAND);

    if (i >= t->count)
        i = 0;
    return AnimSpriteAt(t->entries[i].anim, heading);
}

AM2_Sprite *__cdecl VehicleAnimSprite(int32_t kind, uint32_t heading)
{
    const AM2_AnimTable *t = &g_vehicleAnims[kind];
    int32_t              i = AnimIndexOfId(t, AM2_ANIM_ID_VEHICLE);

    if (i >= t->count)
        i = 0;
    return AnimSpriteAt(t->entries[i].anim, heading);
}

/* The odd one out, in two ways. It RETURNS NULL when nothing carries the id,
 * where the other two fall back to entry 0 -- not tidied, since a turret with
 * no animation is a different thing from a turret drawn as its first one.
 *
 * And it opens with `lea esi, [eax*8 + 0x65A2A8]; test esi, esi; jne` -- a
 * null test on the address of a global plus an index, which cannot be zero.
 * Not reproduced; the same shape as UpdateMouseState's unreachable `je`. */
AM2_Sprite *__cdecl TurretAnimSprite(int32_t kind, uint32_t heading)
{
    const AM2_AnimTable *t = &g_turretAnims[kind];
    int32_t              i = AnimIndexOfId(t, AM2_ANIM_ID_VEHICLE);

    if (i >= t->count)
        return 0;
    return AnimSpriteAt(t->entries[i].anim, heading);
}

/* 0x0043C730, and the roach loader at 0x0043CCF0 tail-jumps to it. The name is
 * ours. Build the roach's collision mask -- one record per direction of the
 * animation with id 0x51, which is the only animation any vehicle or turret
 * file is ever asked for.
 *
 * The method is a 16-pixel grid over the sprite. Each block is sampled every
 * two pixels, 8 by 8 = 64 samples, and kept when at least 16 of them are
 * opaque; a kept block contributes one point, its far corner less the hot spot
 * and less 8, which puts it at the block's CENTRE relative to where the sprite
 * is drawn.
 *
 * The record is {count, up to 40 points} and 0xA4 bytes apart, so the local
 * scratch holds exactly as many as the record does -- 160 bytes, the top of a
 * 192-byte frame. Nothing bounds the count against either, so a sprite with 41
 * qualifying blocks would run off both; the grid step and the sprite sizes
 * make that unreachable and the original does not check.
 *
 * The lookup at the top is SoldierAnimSprite's, written out again with the
 * roach's table and no fallback difference -- except that this one leaves the
 * index at 0 when nothing matches, like the soldier and vehicle versions.
 */
#define g_roachAnims        ((AM2_AnimTable *)(uintptr_t)ADDR_ROACH_ANIMS)
#define g_roachMaskDirs      (*(int32_t *)(uintptr_t)ADDR_ROACH_MASK_DIRECTIONS)
#define g_roachMask   ((uint8_t *)(uintptr_t)ADDR_ROACH_MASK)

void __cdecl BuildRoachMask(void)
{
    const AM2_AnimTable *t = g_roachAnims;
    AM2_Anim            *a;
    int32_t              i = AnimIndexOfId(t, AM2_ANIM_ID_VEHICLE);
    int32_t              facing;
    uint8_t             *out;

    if (i >= t->count)
        i = 0;
    a = t->entries[i].anim;

    g_roachMaskDirs = a->directions;
    if (a->directions <= 0)
        return;

    out = g_roachMask;
    for (facing = 0; facing < a->directions; facing++) {
        AM2_Sprite *spr = g_spriteList[a->cells[(int32_t)a->frames * facing]
                                        .sprite];
        AM2_Point   found[AM2_MASK_POINTS];
        int32_t     count = 0;
        int32_t     x, y;

        for (y = AM2_MASK_STEP; y < spr->bounds.bottom;
             y += AM2_MASK_STEP)
            for (x = AM2_MASK_STEP; x < spr->bounds.right;
                 x += AM2_MASK_STEP) {
                int32_t solid = 0;
                int32_t sx, sy;

                for (sy = y - AM2_MASK_STEP; sy < y;
                     sy += AM2_MASK_SAMPLE)
                    for (sx = x - AM2_MASK_STEP; sx < x;
                         sx += AM2_MASK_SAMPLE) {
                        AM2_Point p;

                        p.x = (int16_t)sx;
                        p.y = (int16_t)sy;
                        if (SpriteSolidAt(spr, p))
                            solid++;
                    }

                if (solid < AM2_ROACH_MASK_MIN_SOLID)
                    continue;
                found[count].x = (int16_t)(x - spr->hotX - 8);
                found[count].y = (int16_t)(y - spr->hotY - 8);
                count++;
            }

        /* The count sits one dword BELOW the points, exactly as the original
         * writes it through `[ebp-4]`. Getting this wrong by one dword put the
         * whole table over the global at 0x00654CA4 with every point still
         * correct, and `bootcamp` and `mission` were both clean on it -- the
         * mis-centred trig table again. tools/maskdump.py is what caught
         * it. */
        *(int32_t *)(out - 4) = count;
        memcpy(out, found, (size_t)count * 4);
        out += AM2_MASK_STRIDE;
    }
}

/* 0x0045A450, one caller -- the vehicle loader at 0x0045A8C0, once per kind.
 * The roach builder again, and this one names the family: it logs
 * "vehicle mask direction: %d" under -traceVEH.
 *
 * Three differences from the roach, all reproduced. It takes the kind and
 * indexes everything by it. It keeps a block on TWELVE of the 64 samples
 * rather than sixteen. And its record index is `kind * 32 + dir`, so every
 * kind gets 32 slots whether its animation has that many directions or not.
 *
 * The bases are confirmed by tiling rather than by reading: the six turret
 * tables end exactly where the six direction counts begin, and those end
 * exactly where the records begin, and 6 * 32 records of 0xA4 end exactly at
 * ADDR_VEHICLE_ANIMS. If a layout does not tile, one of the bases is wrong --
 * which is how the roach table's was found to be.
 */
#define g_vehicleMaskDirs ((int32_t *)(uintptr_t)ADDR_VEHICLE_MASK_DIRECTIONS)
#define g_vehicleMask     ((uint8_t *)(uintptr_t)ADDR_VEHICLE_MASK)
#define g_traceVeh        (*(const int32_t *)(uintptr_t)ADDR_OPT_TRACE_VEH)

void __cdecl BuildVehicleMask(int32_t kind)
{
    const AM2_AnimTable *t = &g_vehicleAnims[kind];
    AM2_Anim            *a;
    int32_t              i = AnimIndexOfId(t, AM2_ANIM_ID_VEHICLE);
    int32_t              dir;

    if (i >= t->count)
        i = 0;
    a = t->entries[i].anim;

    g_vehicleMaskDirs[kind] = a->directions;
    if (a->directions <= 0)
        return;

    for (dir = 0; dir < a->directions; dir++) {
        AM2_Sprite *spr;
        AM2_Point   found[AM2_MASK_POINTS];
        int32_t     count = 0;
        int32_t     x, y;
        uint8_t    *out;

        if (g_traceVeh)
            am2_log("vehicle mask direction: %d\n", dir);

        spr = g_spriteList[a->cells[(int32_t)a->frames * dir].sprite];

        for (y = AM2_MASK_STEP; y < spr->bounds.bottom; y += AM2_MASK_STEP)
            for (x = AM2_MASK_STEP; x < spr->bounds.right; x += AM2_MASK_STEP) {
                int32_t solid = 0;
                int32_t sx, sy;

                for (sy = y - AM2_MASK_STEP; sy < y; sy += AM2_MASK_SAMPLE)
                    for (sx = x - AM2_MASK_STEP; sx < x; sx += AM2_MASK_SAMPLE) {
                        AM2_Point p;

                        p.x = (int16_t)sx;
                        p.y = (int16_t)sy;
                        if (SpriteSolidAt(spr, p))
                            solid++;
                    }

                if (solid < AM2_VEHICLE_MASK_MIN_SOLID)
                    continue;
                found[count].x = (int16_t)(x - spr->hotX - 8);
                found[count].y = (int16_t)(y - spr->hotY - 8);
                count++;
            }

        out = g_vehicleMask
              + (size_t)(kind * AM2_VEHICLE_MASK_DIRS + dir) * AM2_MASK_STRIDE;
        *(int32_t *)(out - 4) = count;
        memcpy(out, found, (size_t)count * 4);
    }
}

#define SPRITE_OFF_EXTRA 0x3Cu   /* the block freed beside the sprite */

void __cdecl FreeBitmap(void **pp)
{
    AM2_Sprite *spr = (AM2_Sprite *)*pp;
    uint8_t    *extra;

    if (!spr)
        return;
    ClearSprite(spr);
    /* Re-read: ClearSprite could have dropped it. */
    spr = (AM2_Sprite *)*pp;
    if (!spr)
        return;
    extra = *(uint8_t **)((uint8_t *)spr + SPRITE_OFF_EXTRA);
    if (extra)
        am2_free(extra);
    am2_free(*pp);
    *pp = 0;
}

AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a, int32_t b)
{
    return PreloadSprite((int32_t)KeyFieldA(key), (int32_t)KeyFieldB(key),
                         (int32_t)KeyFieldC(key), a, b);
}

/* 0x004462F0, nine callers. Make a sprite from a file: allocate one, zero it,
 * load it by name, and remember where it came from.
 *
 * THE POINT OF THE FUNCTION IS THE LAST STEP. AM2_Sprite::source is what
 * RestoreSpriteSurface reloads through when DirectDraw takes a surface back,
 * and this is where it gets filled -- with an ABSOLUTE path, built as the
 * current directory, a backslash, and the name. That matters because the game
 * chdirs into the map directory during a load: a relative name would stop
 * resolving as soon as the directory moved, and a surface lost after that
 * could not be rebuilt.
 *
 * A FAILED LOAD IS LOGGED AND KEPT. "Unable to load sprite %s" goes to the log
 * and the sprite is returned anyway, zeroed except for its source. So a caller
 * cannot tell failure from success by the return value, and none of the nine
 * tries -- each stores it straight into a slot.
 *
 * NEITHER ALLOCATION IS CHECKED, and the getcwd is not checked either. A
 * failing getcwd leaves the buffer as it was, which here is whatever the stack
 * held, and the two strcats then run off it. Reproduced.
 *
 * The original open-codes strlen, strcpy and both strcats as `repne scasb` and
 * `rep movs`, and leaves the getcwd arguments on the stack to be cleaned by a
 * later `add esp, 0xc` that also covers the malloc. Written as the string
 * operations they are; the stack accounting has nothing to reproduce.
 *
 * MEASURED AT TWO CALLS on a driven Boot Camp mission -- the briefing bitmap
 * and the instruction sign -- and both of those reach the screen, so the A/B's
 * pixels compare the load itself. What they do NOT compare is the source path,
 * because nothing reads it until DirectDraw takes a surface back, and nothing
 * under Xvfb does that; CLAUDE.md records the same gap for RestoreTileSet.
 * So the absolute-path construction, which is the point of the function, is
 * verified by reading.
 *
 * Landing it took SpriteReloadNamed to 0 on the same run: it is called by name
 * from here now.
 */
AM2_Sprite *__cdecl LoadBitmap(const char *name, int32_t flags)
{
    AM2_Sprite *spr;
    char        path[0x100];

    if (!name || !name[0])
        return (AM2_Sprite *)0;

    spr = (AM2_Sprite *)am2_malloc(sizeof(AM2_Sprite));
    memset(spr, 0, sizeof(AM2_Sprite));

    if (!SpriteReloadNamed(spr, name, flags))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_LOAD_SPRITE_FAIL), name);

    am2_getcwd(path, (int32_t)sizeof(path));
    strcat(path, (const char *)AM2_IMAGE(ADDR_STR_PATH_SEP));
    strcat(path, name);

    spr->source = (char *)am2_malloc(strlen(path) + 1);
    strcpy(spr->source, path);

    return spr;
}

/* 0x00414AD0, two callers. The sprite-index offset for the player's own army.
 *
 * It is CommArmyOfSlot on ADDR_DEFAULT_OWNER, clamped, times a hundred -- and
 * the original writes that multiply as two `lea eax,[eax+eax*4]` and a shift,
 * which is 25 then 4. Written as the hundred it is.
 *
 * ANYTHING ABOVE 3 BECOMES 0, so army 4 -- the neutral army -- shares army
 * zero's block rather than reading past the table. The compare is signed, so a
 * negative army would pass it and index backwards; no army is negative.
 */
int32_t __cdecl ArmySpriteBase(void)
{
    int32_t army = CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                  (int32_t)*(const uint32_t *)
                                      (uintptr_t)ADDR_DEFAULT_OWNER);

    if (army > 3)
        army = 0;

    return army * AM2_ARMY_SPRITE_BLOCK;
}

/* 0x00414B00, sixteen callers. PreloadSprite with the army's block added to
 * the index, and a fallback to the shared one.
 *
 * So the sprite table is a hundred indices per army over a shared block
 * beneath: the coloured sprite is tried first, and if it is not there the
 * uncoloured one is. That is the whole design and neither function says it
 * alone -- the offset function has no idea what it is an offset into, and this
 * one would look like a plain wrapper without it.
 *
 * ARMY 0 NEVER RETRIES, and not by a special case: its offset is already zero,
 * so the retry is guarded on the offset being non-zero and the two calls would
 * be identical. Reproduced as the guard it is rather than as a comparison
 * against army 0, which is what it means but not what it tests.
 *
 * `addref` is 1 on both calls, so every hit counts another holder. The
 * fallback's result is returned as it comes; a second failure answers NULL and
 * nothing here says so to the log.
 *
 * functions.tsv runs this together with its neighbour and reports 752 bytes
 * with sixteen callers. The function is 74 bytes; the sixteen are its own,
 * functions.tsv runs this together with its neighbour and reports 752 bytes
 * with sixteen callers. The function is 74 bytes; the sixteen are its own,
 * every one of them a call to this exact address.
 *
 * MEASURED AT 70 CALLS on a driven Boot Camp mission, with ArmySpriteBase
 * at 1 -- the other caller is this one, by name, so that runs 71 times.
 * PreloadSprite reads 597 from its other callers.
 *
 * WHAT THOSE 70 DO NOT COVER IS THE POINT OF THE FUNCTION. The player is
 * army 0 on this drive, so ArmySpriteBase returns 0, the index is passed
 * through unchanged and the retry is guarded off entirely. Every one of the
 * seventy exercised a plain forward to PreloadSprite. The offset, the clamp
 * and the fallback all need a player who is not army 0 -- which means a
 * multiplayer session, and no drive here has one. Said plainly rather than
 * left to read as seventy calls of coverage. */
void *__cdecl PreloadArmySprite(int32_t set, int32_t index, int32_t frame,
                                int32_t flags)
{
    int32_t base = ArmySpriteBase();
    void   *spr;

    spr = PreloadSprite(set, index + base, frame, flags, 1);
    if (spr || base <= 0)
        return spr;

    return PreloadSprite(set, index, frame, flags, 1);
}

/* 0x00408D20 and 0x00408DA0, with 0x00408E40 the alias. Load and free sprite
 * SET 19 as a block: one from index 2, twenty from index 3, eleven from
 * index 6, all with flags 0x1000 and addref 1.
 *
 * WHAT SET 19 IS IS NOT ESTABLISHED. The names say which set and index each
 * array holds, which is all the two functions show; the only other clue is
 * that 0x00408E00 takes the first sprite's bounds.right, doubles it and adds
 * ADDR_BITMAP_AREA_W, which is a layout number and not an identity.
 *
 * THE FREE ZEROES FAR MORE THAN THE LOAD FILLED, AND THE EXTRA IS NOT SPRITES.
 * It releases exactly the thirty-two sprites the load created, then memsets
 * 179 DWORDS from the first array -- 716 bytes, ending at 0x004F96A4. That
 * address is ADDR_AIR_SAVE_BLOCK + 0x248, the last byte of the air-support
 * state, so the sweep takes the sprite arrays and the whole queue with them
 * and stops exactly there.
 *
 * This entry first read "something else keeps sprite pointers in the same
 * block", which was a guess standing in for the arithmetic. Doing the
 * arithmetic named the set: these are the AIR SUPPORT sprites.
 *
 * The alias is one `jmp`, the same shape as FreeSpriteListAlias, and it is the
 * ONLY way in: the free itself has no other reference in the image. So both
 * callers -- the reloader at 0x00408E00 and the teardown at 0x00425732 -- go
 * through it, and patching the free alone would leave the alias jumping into
 * our detour, which is why both are patched.
 *
 * ReleaseSprite is given each pointer without a null test. It handles null
 * itself, which is what makes the load's failure path harmless here.
 *
 * MEASURED, AND THE THREE COUNTS CONFIRM THE ALIAS CLAIM. On a driven Boot
 * Camp mission LoadAirSprites reads 1, FreeAirSpritesAlias reads 1, and
 * FreeAirSprites reads 0 -- which is what "the alias is the only way in"
 * predicts, since the alias is ours and calls it by name. A second reference
 * to the free would have shown up as a non-zero count here.
 *
 * These sprites reach the screen, so a wrong index or a wrong count would move
 * the A/B's pixels rather than hiding; bootcamp's twenty-two is the check.
 */
void __cdecl LoadAirSprites(void)
{
    AM2_Sprite **p;
    int32_t      i;

    *(AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_2 =
        PreloadSprite(AM2_AIR_SPRITE_SET, 2, 0, 0x1000, 1);

    i = 0;
    for (p = (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_3;
         p < (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_6; p++, i++)
        *p = PreloadSprite(AM2_AIR_SPRITE_SET, 3, i, 0x1000, 1);

    i = 0;
    for (p = (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_6;
         p < (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_EDGE; p++, i++)
        *p = PreloadSprite(AM2_AIR_SPRITE_SET, 6, i, 0x1000, 1);
}

void __cdecl FreeAirSprites(void)
{
    AM2_Sprite **p;

    ReleaseSprite(*(AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_2);

    for (p = (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_3;
         p < (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_6; p++)
        ReleaseSprite(*p);

    for (p = (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_6;
         p < (AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_EDGE; p++)
        ReleaseSprite(*p);

    /* 179 dwords, not the 32 released above -- see the note. */
    memset((void *)(uintptr_t)ADDR_AIR_SPRITES_2, 0,
           AM2_AIR_SPRITES_CLEAR * 4);
}

void __cdecl FreeAirSpritesAlias(void)
{
    FreeAirSprites();
}

/* 0x00408E00. The air-support reset, and the only caller of the load. Free the
 * sprites through the alias, put both queue lengths back to zero, load the
 * sprites again, and measure the gauge track.
 *
 * IT FREES BEFORE IT LOADS, which is what makes it a reset rather than a
 * loader: the free's memset is what clears the rest of the air block, so the
 * two stores here are the only state it has to clear by hand -- and they are
 * the two the memset would clear anyway. The original writes them regardless.
 * Reproduced, because a reset that depends on the free's overreach for its
 * correctness is worth being able to see.
 *
 * THE TRACK IS COMPUTED FROM THE SPRITE AND SKIPPED WHEN THERE IS NONE. A
 * failed load leaves ADDR_AIR_SPRITES_2 null and the original simply does not
 * write ADDR_AIR_SPRITES_EDGE -- so it keeps whatever the memset left, which
 * is zero. The null test is the original's and not defensive tidying.
 *
 * 640 + 2 * bounds.right is the length 0x00409166 slides that sprite along,
 * scaling it by AIR_OFF_ACTIVE over the constant at 0x00473F30. Twice the
 * width, so the sprite is off both ends at the extremes.
 */
void __cdecl ResetAirSupport(void)
{
    AM2_Sprite *first;

    FreeAirSpritesAlias();

    *(int32_t *)(uintptr_t)(ADDR_AIR_SAVE_BLOCK + AIR_OFF_COUNT) = 0;
    *(int32_t *)(uintptr_t)(ADDR_AIR_SAVE_BLOCK + AIR_OFF_PASS_COUNT) = 0;

    LoadAirSprites();

    first = *(AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_2;
    if (first)
        *(int32_t *)(uintptr_t)ADDR_AIR_SPRITES_EDGE =
            *(int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W + first->bounds.right * 2;
}

/* FreeSpriteRegistry -- original 0x00445F40, three callers.
 *
 * Take the whole sprite registry down: close the open sprite file, release
 * every registered sprite, free the slot table and the id/slot pairs, and zero
 * the count and the capacity.
 *
 * IT FORCES THE REFCOUNT TO 1 BEFORE RELEASING. ReleaseSprite frees at zero
 * and takes one reference at a time, so a sprite held by three things would
 * survive a single release -- this clamps `refs` down to 1 first, exactly so
 * the release that follows is the last one. That clamp is the whole reason
 * this is not a loop of plain releases, and dropping it would leak every
 * shared sprite in the game.
 *
 * IT CLAMPS ONLY DOWNWARD. A refcount already at 1 or 0 is left alone, so a
 * sprite whose count is 0 is released once more and goes negative inside
 * ReleaseSprite. That is the original's and it is reachable: nothing here
 * checks whether a slot has already been released.
 *
 * THE TABLE POINTER IS RE-READ AFTER EVERY CALL and the count after every
 * iteration, because ReleaseSprite can free a slot and, through
 * ADDR_SPRITE_SLOT_OF's registry, move the tables. Written as the re-reads
 * they are rather than hoisted -- this is the one loop in the teardown where
 * hoisting would be wrong rather than merely different.
 *
 * The three frees are guarded independently: the file, then the table, then
 * the pairs, each skipped when already null. The count and capacity are zeroed
 * unconditionally at the end, unlike FreeScenarios, which skips its clears when
 * its table was null.
 *
 * The file goes through the game's own fclose because the game's CRT opened it.
 */
void __cdecl FreeSpriteRegistry(void)
{
    am2_FILE *fp = *(am2_FILE **)(uintptr_t)ADDR_SPRITE_FILE;
    AM2_Sprite **table;

    if (fp) {
        orig_fclose(fp);
        *(am2_FILE **)(uintptr_t)ADDR_SPRITE_FILE = (am2_FILE *)0;
    }

    table = *(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE;
    if (table) {
        int32_t i;

        for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_SPRITE_REG_COUNT;
             i++) {
            AM2_Sprite *spr =
                (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE)[i];

            if (!spr)
                continue;

            if (spr->refs > 1)
                spr->refs = 1;

            ReleaseSprite((*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE)[i]);
        }

        am2_free(*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE);
        *(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE = (AM2_Sprite **)0;
    }

    if (*(void **)(uintptr_t)ADDR_SPRITE_REG_PAIRS) {
        am2_free(*(void **)(uintptr_t)ADDR_SPRITE_REG_PAIRS);
        *(void **)(uintptr_t)ADDR_SPRITE_REG_PAIRS = (void *)0;
    }

    *(int32_t *)(uintptr_t)ADDR_SPRITE_REG_COUNT = 0;
    *(int32_t *)(uintptr_t)ADDR_SPRITE_REG_CAP   = 0;
}

int sprite_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_FREE_SPRITE_REGISTRY,
                        (const void *)FreeSpriteRegistry,
                        "FreeSpriteRegistry", 3);

    rc |= patch_replace(ADDR_SPRITE_SLOT_OF, (const void *)SpriteSlotOf,
                        "SpriteSlotOf", 1);
    rc |= patch_replace(ADDR_BUILD_VEHICLE_MASK,
                        (const void *)BuildVehicleMask,
                        "BuildVehicleMask", 1);
    rc |= patch_replace(ADDR_PRELOAD_SPRITE_KEY, (const void *)PreloadSpriteByKey,
                        "PreloadSpriteByKey", 3);
    rc |= patch_replace(ADDR_FREE_BITMAP, (const void *)FreeBitmap,
                        "FreeBitmap", 1);
    rc |= patch_replace(ADDR_BUILD_ROACH_MASK,
                        (const void *)BuildRoachMask,
                        "BuildRoachMask", 0);
    rc |= patch_replace(ADDR_SPRITE_SOLID_AT, (const void *)SpriteSolidAt,
                        "SpriteSolidAt", 2);
    rc |= patch_replace(ADDR_SOLDIER_ANIM_SPRITE, (const void *)SoldierAnimSprite,
                        "SoldierAnimSprite", 2);
    rc |= patch_replace(ADDR_VEHICLE_ANIM_SPRITE, (const void *)VehicleAnimSprite,
                        "VehicleAnimSprite", 2);
    rc |= patch_replace(ADDR_TURRET_ANIM_SPRITE, (const void *)TurretAnimSprite,
                        "TurretAnimSprite", 2);
    rc |= patch_replace(ADDR_DRAW_SPRITE, (const void *)DrawSprite, "DrawSprite", 4);
    rc |= patch_replace(ADDR_SPRITE_REGISTER, (const void *)SpriteRegister,
                        "SpriteRegister", 2);
    rc |= patch_replace(ADDR_LOAD_SHADOW_BMP, (const void *)LoadShadowBmp,
                        "LoadShadowBmp", 1);
    rc |= patch_replace(ADDR_SPRITE_LOAD_TRIPLE, (const void *)SpriteLoadTriple,
                        "SpriteLoadTriple", 2);
    rc |= patch_replace(ADDR_SPRITE_SET_FOR_KEY, (const void *)SpriteSetForKey,
                        "SpriteSetForKey", 3);
    rc |= patch_replace(ADDR_SPRITE_DIR_INDEX, (const void *)SpriteDirIndex,
                        "SpriteDirIndex", 2);
    rc |= patch_replace(ADDR_SPRITE_LOAD_DF, (const void *)SpriteLoadFromDataFile,
                        "SpriteLoadFromDataFile", 2);
    rc |= patch_replace(ADDR_SPRITE_SET_RESOLVE, (const void *)SpriteSetResolve,
                        "SpriteSetResolve", 1);
    rc |= patch_replace(ADDR_SPRITE_SET_LOAD, (const void *)SpriteSetLoad,
                        "SpriteSetLoad", 3);
    rc |= patch_replace(ADDR_SPRITE_SET_FREE, (const void *)SpriteSetFree,
                        "SpriteSetFree", 4);
    rc |= patch_replace(ADDR_LOAD_BITMAP_DESC, (const void *)LoadBitmapDescriptor,
                        "LoadBitmapDescriptor", 1);
    rc |= patch_replace(ADDR_SPRITE_RELOAD_NAMED, (const void *)SpriteReloadNamed,
                        "SpriteReloadNamed", 4);
    rc |= patch_replace(ADDR_LOAD_BITMAP, (const void *)LoadBitmap,
                        "LoadBitmap", 9);
    rc |= patch_replace(ADDR_ARMY_SPRITE_BASE, (const void *)ArmySpriteBase,
                        "ArmySpriteBase", 2);
    rc |= patch_replace(ADDR_LOAD_AIR_SPRITES, (const void *)LoadAirSprites,
                        "LoadAirSprites", 1);
    rc |= patch_replace(ADDR_FREE_AIR_SPRITES, (const void *)FreeAirSprites,
                        "FreeAirSprites", 1);
    rc |= patch_replace(ADDR_RESET_AIR_SUPPORT, (const void *)ResetAirSupport,
                        "ResetAirSupport", 0);
    rc |= patch_replace(ADDR_FREE_AIR_SPRITES_ALIAS,
                        (const void *)FreeAirSpritesAlias,
                        "FreeAirSpritesAlias", 2);
    rc |= patch_replace(ADDR_PRELOAD_ARMY_SPRITE,
                        (const void *)PreloadArmySprite,
                        "PreloadArmySprite", 16);
    rc |= patch_replace(ADDR_SPRITE_REBUILD_DF, (const void *)SpriteRebuildDf,
                        "SpriteRebuildDf", 1);
    rc |= patch_replace(ADDR_SPRITE_REBUILD_ALT, (const void *)SpriteRebuildAlt,
                        "SpriteRebuildAlt", 1);
    rc |= patch_replace(ADDR_PRELOAD_SPRITE_NAME, (const void *)PreloadSpriteName,
                        "PreloadSpriteName", 14);
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
    rc |= patch_replace(ADDR_INIT_MENU_SCREEN, (const void *)InitMenuScreen,
                        "InitMenuScreen", 2);
    rc |= patch_replace(ADDR_PRELOAD_SPRITE, (const void *)PreloadSprite,
                        "PreloadSprite", 37);
    return rc;
}
