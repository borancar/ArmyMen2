#ifndef AM2_SPRITE_H
#define AM2_SPRITE_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"
#include "../blit.h"
#include "../rect.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* A drawable sprite. Only the fields the drawing path touches are named; the
 * real structure is larger.
 *
 * `image` is a union, and which arm applies is decided by `flags & 0x3C`:
 * with those bits set the sprite is drawn by the software RLE blitters and
 * `image` is encoded pixel data; with them clear it goes to
 * IDirectDrawSurface::BltFast and `image` is the source surface. An earlier
 * version of this header called it a surface outright, having only seen the
 * hardware path -- the second naming mistake in this file made by generalising
 * from one call site.
 */
typedef struct AM2_Sprite {
    uint32_t id;                 /* +0x00  0xFFFFFFFF when unregistered */
    int32_t  refs;               /* +0x04  ReleaseSprite frees at zero */
    uint32_t format;             /* +0x08  1, 2 or 3 selects a software blitter;
                                  *        ZERO means `image` is a DirectDraw
                                  *        surface, which is how the teardown
                                  *        below decides between Release and
                                  *        free */
    uint32_t flags;              /* +0x0C  bit0 clear => source colour key;
                                  *        bits 2..5 (0x3C) => software path */
    /* Which arm applies is decided by `flags & 0x3C` and then by `format`:
     * format 1 uses the 32-bit row table, formats 2 and 3 the 16-bit one. */
    union {
        AM2_Rle16                 *rle16;    /* software, formats 2 and 3 */
        AM2_Rle32                 *rle32;    /* software, format 1 */
        LPDIRECTDRAWSURFACE        surface;  /* hardware: BltFast source */
    } image;                     /* +0x10 */
    AM2_Rect bounds;             /* +0x14 .. +0x23 */
    int16_t  hotX;               /* +0x24, subtracted from the draw position */
    int16_t  hotY;               /* +0x26 */
    int16_t  fileA;              /* +0x28  read straight out of the sprite
                                  *        file by LoadSpriteSet, beside hotX
                                  *        and hotY and in the same shape --
                                  *        what they mean is not established,
                                  *        but they are not padding */
    int16_t  fileB;              /* +0x2A */
    uint8_t  pad2C[4];           /* +0x2C */
    AM2_Rle16 *overlay;          /* +0x30  second layer, drawn after the first */
    uint8_t *lut;                /* +0x34  256-entry remap table, may be NULL */
    void    *palette;            /* +0x38  overlay palette; NULL means default */
    char    *source;             /* +0x3C  where it was loaded from, or NULL.
                                  *        RestoreSpriteSurface reloads through
                                  *        it after a lost surface, and names it
                                  *        in the failure message. */
} AM2_Sprite;

/* DrawSprite -- original 0x00445FF0, 24 call sites.
 *
 * Places `spr` so its hot spot lands at (x, y), clips it against the screen
 * rectangle, and hands the survivor to the dispatcher below. */
void __cdecl DrawSprite(AM2_Sprite *spr, int32_t x, int32_t y, int32_t mode);

/* DrawSpriteClipped -- original 0x00446070, 14 call sites.
 *
 * The centre of the 2D pipeline: every large drawing composite reaches it. It
 * picks between the four software blitters and DirectDraw's BltFast, then draws
 * the optional overlay layer. */
void __cdecl DrawSpriteClipped(AM2_Sprite *spr, int32_t x, int32_t y,
                               const AM2_Rect *clipped, int32_t mode);

/* RestoreSpriteSurface -- original 0x00445EB0, the recovery chain reached when
 * DrawSpriteClipped's BltFast answers DDERR_SURFACELOST.
 *
 * Restores the surface and then puts the pixels back, because a restored
 * DirectDraw surface comes back with its memory undefined. Which route depends
 * on where the sprite came from: one that remembers a `source` is reloaded from
 * it, and one that does not is rebuilt by whichever of the two other paths `-df`
 * selects. Failure is logged and otherwise ignored -- the sprite simply draws
 * wrong.
 *
 * UNTESTED, like the other Restore path in surface.cpp: reaching it needs a
 * surface to be lost, which wants an alt-tab or a display mode change. */
void __cdecl RestoreSpriteSurface(AM2_Sprite *spr);

/* ClearSprite -- original 0x00445E40. Give back everything the sprite owns and
 * blank the record, leaving it reusable in place.
 *
 * ReleaseSprite -- original 0x00445D80. The same, one reference at a time: the
 * sprite is looked up in the registry, its count decremented, and only at zero
 * is the slot cleared and the record itself freed. A sprite the registry does
 * not hold, or holds under someone else's slot, is reported and freed anyway.
 *
 * Both free the `source` string without nulling it, unlike `image` and
 * `overlay`. That is the original's asymmetry and it is kept; ClearSprite
 * blanks the whole record immediately afterwards, and ReleaseSprite frees it,
 * so neither leaves a dangling pointer anyone can reach. */
void __cdecl ClearSprite(AM2_Sprite *spr);

/* PreloadSprite -- original 0x00445B00, 37 call sites, and what the script
 * statement `preloadsprite` drives.
 *
 * The three integers are a sprite identity that the loader turns into
 * "%02d_%03d_%02d_*.bmp"; here they are packed into the id the registry is
 * keyed on, `(((set << 12) + index) << 7) + frame`.
 *
 * `addref` chooses between the two ways of arriving at an already-loaded
 * sprite: non-zero counts another holder, zero only guarantees the count is at
 * least one and leaves an existing count alone. NULL if the load failed. */
AM2_Sprite *__cdecl PreloadSprite(int32_t set, int32_t index, int32_t frame,
                                  int32_t flags, int32_t addref);
void __cdecl ReleaseSprite(AM2_Sprite *spr);

/* Original: 0x004099F0, and the name is ours. Read a sprite set from an open
 * file into the sprite list: a count, then that many sprites.
 *
 * Each sprite is six uint16 fields -- bounds.right, bounds.bottom, hotX, hotY
 * and the pair at 0x0028 -- then a dword size and that many bytes of image,
 * then a dword size and that many bytes of overlay, the overlay only if its
 * size is positive. bounds.left and bounds.top are zeroed rather than read, so
 * a sprite's box always starts at the origin.
 *
 * The FORMAT is decided last and from the caller, not the file: bit 0x10 of
 * `flags` makes it 3, otherwise bit 0x08 makes it 2, otherwise it stays 0 --
 * which sprite.h's comment above records as meaning `image` is a DirectDraw
 * surface. So a set loaded with neither bit claims to hold surfaces while
 * holding file bytes; no caller does that, and the zero is left as it is.
 *
 * The first uint16 is read into the FILE POINTER'S OWN ARGUMENT SLOT, which
 * MSVC reuses because `fp` is live in a register by then. A local here.
 *
 * Nothing checks a malloc or a read. */
void __cdecl LoadSpriteSet(am2_FILE *fp, const uint8_t *table, int32_t from,
                           uint32_t flags);

/* FreeMenuSprites -- original 0x00412F80. Release all 190 menu sprites, the
 * slot past them, and the surface they were drawn from. */
void __cdecl FreeMenuSprites(void);

int sprite_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SPRITE_H */
