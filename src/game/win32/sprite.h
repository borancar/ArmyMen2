#ifndef AM2_SPRITE_H
#define AM2_SPRITE_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"
#include "../blit.h"
#include "../rect.h"
#include "../anim.h"

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
    /* fileA and fileB are the OTHER half of the same two dwords the hot spot
     * comes out of. The data file stores {hotX, hotY} then {fileA, fileB};
     * the bitmap path stores {hotX, fileA} and {hotY, fileB}, because it has
     * only biXPelsPerMeter and biYPelsPerMeter to smuggle them through and
     * splits them by axis. What they MEAN is still not established. */
    int16_t  fileA;              /* +0x28 */
    int16_t  fileB;              /* +0x2A */
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

/* DrawSprite -- original 0x00445FF0, 24 call sites.
 *
 * Places `spr` so its hot spot lands at (x, y), clips it against the screen
 * rectangle, and hands the survivor to the dispatcher below. */
/* One entry of the sorted index over the sprite table. */
typedef struct {
    uint32_t id;
    int32_t  slot;
} AM2_SpritePair;

/* 0x004459E0. Append the sprite and insert its {id, slot} pair in order. An
 * id already present is left alone and nothing is reported. */
void __cdecl SpriteRegister(AM2_Sprite *spr, uint32_t id);

/* 0x004457E0. Fill a record from {set, index, frame}: a tail call into the
 * packed data file by default, and a two-file glob under -df. */
int32_t __cdecl SpriteLoadTriple(AM2_Sprite *spr, int32_t set, int32_t index,
                                 int32_t frame, int32_t flags);

/* One entry of a sprite set's directory: the key, and where in the archive
 * that sprite starts. Sorted by key. */
typedef struct {
    uint32_t key;
    uint32_t offset;
} AM2_SpriteDirEntry;

/* 0x00423940. Which of the three sets holds a key. */
void *__cdecl SpriteSetForKey(uint32_t key);

/* 0x004230F0. Fill a 0x1C-byte bitmap record from a file NAME: open it, read
 * one DIB chunk, hand it to MakeBitmap, free the pixels. Zero if any step
 * failed. */
int32_t __cdecl LoadBitmapDescriptor(const char *name, void *out);

/* 0x004456B0. Reload a sprite from a named bitmap, through the record above. */
int32_t __cdecl SpriteReloadNamed(AM2_Sprite *spr, const char *name,
                                  int32_t flags);

/* 0x004236A0. Point `set` at the record a set name means and `id` at the file
 * id its archive must carry. Nonzero when the record is already pointing at
 * that file and there is nothing to open. */
int32_t __cdecl SpriteSetResolve(const char *name, void **set, uint32_t *id);

/* 0x004239B0. Open a set's archive: header, palette, remap tables and the
 * directory. Zero if it could not be opened or the id is wrong. */
int32_t __cdecl SpriteSetLoad(const char *name);

/* 0x00423970. Close a set's archive and free its directory. */
void __cdecl SpriteSetFree(void *set);

/* 0x00423D50. The directory index for a key, or -1. */
int32_t __cdecl SpriteDirIndex(void *set, uint32_t key);

/* 0x004243B0. Put a lost sprite's pixels back from the packed data file, and
 * 0x00445C00, the same from loose files. Both take the key from spr->id. */
int32_t __cdecl SpriteRebuildDf(AM2_Sprite *spr, int32_t flags);
int32_t __cdecl SpriteRebuildAlt(AM2_Sprite *spr, int32_t flags);

/* 0x00423FE0. Fill a record from the packed data file -- the default path,
 * and what SpriteLoadTriple tail-calls unless -df was given. */
int32_t __cdecl SpriteLoadFromDataFile(AM2_Sprite *spr, int32_t set,
                                       int32_t index, int32_t frame,
                                       int32_t flags);

/* 0x00445CF0. A sprite by NAME: parsed into {set, index, frame} when the name
 * carries them, and otherwise loaded as a bitmap and registered under set 99
 * with an index that counts up. */
AM2_Sprite *__cdecl PreloadSpriteName(const char *name, int32_t flags,
                                      int32_t addref);

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

/* Original: 0x00445990. A sprite id to its SLOT in the sprite table, or -1.
 * A binary search over a SECOND table of {id, slot} pairs kept sorted by id,
 * with an UNSIGNED comparison. */
int32_t __cdecl SpriteSlotOf(uint32_t id);

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

/* Original: 0x00409F50, six callers, and the name is ours. Open a sprite file
 * in "rb", read its 256-entry palette, turn that into a 256-byte remap table
 * by asking NearestPalIndex for the closest ACTIVE palette entry to each, load
 * the set through it, hand the still-open file to LoadAnimTable, and close.

 * `anims` is the caller's own animation table and `fallback` another already
 * loaded -- every soldier file in the group passes rifleman's. Both were
 * `int32_t` here when the tail was original and nothing said what they were.
 *
 * The `from` argument goes to both NearestPalIndex and LoadSpriteSet, so the
 * reserved block below it is respected in the same place twice: the table is
 * never built to point below it, and the walker never rewrites an index below
 * it either.
 *
 * The failure path writes no return value of its own and does not need to:
 * `eax` still holds the null fopen returned, so it answers 0 while the success
 * path answers 1. I read that as "returns whatever was in the register" first
 * and it is worth saying it is not -- the value is null by construction. */
int32_t __cdecl LoadSpriteFile(const char *path, AM2_AnimTable *anims,
                               const AM2_AnimTable *fallback,
                               int32_t from, uint32_t flags);

/* FreeMenuSprites -- original 0x00412F80. Release all 190 menu sprites, the
 * slot past them, and the surface they were drawn from. */
/* 0x00446410, 14 callers. Release a sprite through a POINTER TO the pointer:
 * clear it, free the block at +0x3C if there is one, free the sprite, and null
 * the caller's variable. The pointer is re-read after ClearSprite, which is
 * what the original does.
 *
 * `void **` rather than `AM2_Sprite **` so event.cpp can forward-declare it:
 * that module is on the flat side of the split and must not name a DirectDraw
 * type, and this signature does not. */
/* 0x00445AD0, 15 callers. PreloadSprite addressed by a PACKED KEY: the key is
 * split into PackKey's three fields and they become the first three
 * arguments. This is what ties packkey.cpp's field layout to something that
 * uses it -- the shifts and masks here are KeyFieldA, KeyFieldB and KeyFieldC
 * written out, and they agree exactly. */
AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a, int32_t b);

void __cdecl FreeBitmap(void **pp);

void __cdecl FreeMenuSprites(void);

/* 0x0045A450. The same for one vehicle kind, and the function that names the
 * family: it logs "vehicle mask direction: %d" under -traceVEH. It keeps a
 * block on twelve of the 64 samples where the roach wants sixteen. */
void __cdecl BuildVehicleMask(int32_t kind);

/* 0x0043C730. Build the roach's collision mask, one record per direction.
 * See the note in sprite.cpp for the grid and the record layout. */
void __cdecl BuildRoachMask(void);

/* 0x00446290. Is this sprite opaque at this point -- the run-length mask for a
 * software format, the bounding box for anything else. */
int32_t __cdecl SpriteSolidAt(AM2_Sprite *spr, AM2_Point at);

/* 0x0044BB30, 0x0045D9B0 and 0x0045DA20. The sprite to draw for a unit of this
 * kind pointed this way: the animation with a fixed id (1 for soldiers, 0x51
 * for vehicles and turrets), the heading rounded to one of its directions, frame
 * 0. See the note in sprite.cpp for what they settle about anim.h, and for the
 * one way the turret version differs. */
AM2_Sprite *__cdecl SoldierAnimSprite(int32_t kind, uint32_t heading);
AM2_Sprite *__cdecl VehicleAnimSprite(int32_t kind, uint32_t heading);
AM2_Sprite *__cdecl TurretAnimSprite(int32_t kind, uint32_t heading);

int sprite_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SPRITE_H */
