#ifndef AM2_SPRITE_H
#define AM2_SPRITE_H

#include <stdint.h>
#include "../inject/orig.h"
#include "rect.h"

/* Declared rather than included: pulling ddraw.h in here would drag in
 * winuser.h, whose DrawText macro collides with our reconstructed DrawText.
 * The DirectDraw includes live in sprite.c. */
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
typedef struct {
    uint8_t  pad00[8];
    uint32_t format;             /* +0x08  1, 2 or 3; selects the blitter */
    uint32_t flags;              /* +0x0C  bit0 clear => source colour key;
                                  *        bits 2..5 (0x3C) => software path */
    union {
        uint8_t                   *rle;      /* software: encoded pixels */
        struct IDirectDrawSurface *surface;  /* hardware: BltFast source */
    } image;                     /* +0x10 */
    AM2_Rect bounds;             /* +0x14 .. +0x23 */
    int16_t  hotX;               /* +0x24, subtracted from the draw position */
    int16_t  hotY;               /* +0x26 */
    uint8_t  pad28[8];           /* +0x28 */
    uint8_t *overlay;            /* +0x30  second layer, drawn after the first */
    uint8_t *lut;                /* +0x34  256-entry remap table, may be NULL */
    void    *palette;            /* +0x38  overlay palette; NULL means default */
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

int sprite_install(void);

#endif /* AM2_SPRITE_H */
