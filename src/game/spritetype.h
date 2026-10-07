/* spritetype.h -- the sprite record, on the flat side.
 *
 * The struct lived in win32/sprite.h, which made every reader of an ordinary
 * field -- PointActionC's attachX and attachY -- either a platform module or
 * a cast through SPRITE_OFF_*. Its one platform member is the hardware arm of
 * `image`, and that is a pointer: the flat side holds it without calling
 * anything through it. So it is spelled `struct IDirectDrawSurface *`, which
 * is exactly what ddraw.h's LPDIRECTDRAWSURFACE names, forward-declared here
 * so no Win32 header comes with it. */
#ifndef AM2_SPRITETYPE_H
#define AM2_SPRITETYPE_H

#include <stdint.h>
#include "blit.h"                /* AM2_Rle16, AM2_Rle32 */
#include "rect.h"                /* AM2_Rect */

struct IDirectDrawSurface;

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
    int32_t  format;             /* +0x08  1, 2 or 3 selects a software blitter;
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
        struct IDirectDrawSurface *surface;  /* hardware: BltFast source */
    } image;                     /* +0x10 */
    AM2_Rect bounds;             /* +0x14 .. +0x23 */
    int16_t  hotX;               /* +0x24, subtracted from the draw position */
    int16_t  hotY;               /* +0x26 */
    /* attachX and attachY are the OTHER half of the same two dwords the hot
     * spot comes out of. The data file stores {hotX, hotY} then this pair;
     * the bitmap path stores {hotX, attachX} and {hotY, attachY}, because it
     * has only biXPelsPerMeter and biYPelsPerMeter to smuggle them through
     * and splits them by axis.
     *
     * They were fileA and fileB, "what they MEAN is still not established",
     * until PointActionC turned up as a reader: it moves an object to a point
     * and then offsets every row AFTER the first by exactly these two, as an
     * X and a Y. So they are where an attached row sits relative to the one
     * carrying this sprite -- a turret on its body. One reader, but an
     * unambiguous one. */
    int16_t  attachX;            /* +0x28 */
    int16_t  attachY;            /* +0x2A */
    uint8_t  keyIndex;           /* +0x2C  the transparent palette index, out
                                  *        of the bitmap record's BMP_OFF_KEY.
                                  *        Was called pad2C; it is not pad. */
    uint8_t  pad2D[3];           /* +0x2D */
    AM2_Rle16 *overlay;          /* +0x30  second layer, drawn after the first */
    uint8_t *lut;                /* +0x34  256-entry remap table, may be NULL */
    void    *palette;            /* +0x38  overlay palette; NULL means default */
    char    *source;             /* +0x3C  where it was loaded from, or NULL.
                                  *        RestoreSpriteSurface reloads through
                                  *        it after a lost surface, and names it
                                  *        in the failure message. */
} AM2_Sprite;

/* One record of ADDR_SPRITE_GROUPS_C: a count and the sprites loaded for
 * it, AM2_SPRITEGRP_C_BYTES apart. */
typedef struct AM2_SpriteGroupC {
    int32_t      count;          /* +0x00 SPRITEGRP_C_OFF_COUNT */
    AM2_Sprite **sprites;        /* +0x04 SPRITEGRP_C_OFF_SPRITES */
    int32_t      field8;         /* +0x08, copied into the effect context */
} AM2_SpriteGroupC;

/* One particle kind at ADDR_PARTICLE_KINDS, 0x18 bytes: its frame count, its
 * sprites, and the x/y/z offset SpawnParticle starts a particle at. */
typedef struct AM2_ParticleKind {
    int32_t      frames;         /* +0x00 */
    AM2_Sprite **sprites;        /* +0x04 PARTICLE_OFF_SPRITES */
    int32_t      x;              /* +0x08 */
    int32_t      y;              /* +0x0C */
    int32_t      z;              /* +0x10 */
    int32_t      pad;            /* +0x14 */
} AM2_ParticleKind;

#endif /* AM2_SPRITETYPE_H */
