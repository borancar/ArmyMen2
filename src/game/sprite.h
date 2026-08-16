#ifndef AM2_SPRITE_H
#define AM2_SPRITE_H

#include <stdint.h>
#include "../inject/orig.h"
#include "rect.h"

/* A drawable sprite. Only the fields DrawSprite touches are named; the real
 * structure is larger and its remaining fields are unread here.
 *
 * The two pointers at +0x10 and +0x30 are alternatives -- DrawSprite draws
 * nothing when both are null -- but they are not two forms of the same thing.
 * `surface` is a COM interface, not pixel data: 0x00445EB0 calls
 * IDirectDrawSurface::Restore through it (`call [vtable+0x6C]`), so that path
 * hands the sprite to DirectDraw. `rle` is the software representation the
 * blitters in blit.c consume.
 */
typedef struct {
    uint8_t  pad00[0x10];
    void    *surface;    /* +0x10  IDirectDrawSurface * */
    AM2_Rect bounds;     /* +0x14 .. +0x23 */
    int16_t  hotX;       /* +0x24, subtracted from the draw position */
    int16_t  hotY;       /* +0x26 */
    uint8_t  pad28[8];   /* +0x28 */
    void    *rle;        /* +0x30  software RLE data */
} AM2_Sprite;

/* DrawSprite -- original 0x00445FF0, 24 call sites.
 *
 * Places `spr` so that its hot spot lands at (x, y), clips it against the
 * screen rectangle, and hands the survivor to the sprite dispatcher. Draws
 * nothing when the sprite is null, carries no pixel data, or falls entirely
 * outside the clip.
 */
void __cdecl DrawSprite(AM2_Sprite *spr, int32_t x, int32_t y, int32_t mode);

int sprite_install(void);

#endif /* AM2_SPRITE_H */
