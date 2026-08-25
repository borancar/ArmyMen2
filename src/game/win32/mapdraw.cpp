/* Map repainting -- reconstructed from ArmyMen2.exe.
 *
 *   SetDrawTarget     0x0041AC40   28 call sites
 *   RedrawMapRegion   0x0041CF90    7 call sites
 *
 * RedrawMapRegion is the dirty-rectangle repaint, and the fourth function found
 * bracketed by Lock/Unlock. It is mostly glue: the work is in the two callees
 * it still shares with the original image.
 *
 * The order matters and is worth stating, because it is not the obvious one.
 * The screen-space setup at 0x0042D9B0 takes the rectangle BY VALUE, so it
 * cannot hand a transformed rectangle back -- and indeed the tile walker is
 * given the original world-space rectangle, not a converted one. The two
 * callees therefore work in different coordinate spaces on purpose: one
 * prepares the screen area, the other walks tiles in world space.
 *
 * All three calls are cdecl and the original cleans all 24 bytes of their
 * arguments in one `add esp, 0x18` after the last of them, rather than after
 * each.
 */

#include "mapdraw.h"
#include "../objflag.h"  /* ObjFlagBit1 -- reconstructed */
#include "surface.h"
#include "../gamedir.h"
#include "palette.h"
#include "../misc.h"
#include "../../inject/patch.h"

#include <stdint.h>
#include <stdio.h>   /* SEEK_CUR only */

#define g_drawTarget (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET)
/* The OFFSCREEN surface, not the back buffer. This file used to call it
 * g_backBuffer, which made one identifier mean two different surfaces in two
 * files. The address was always right; the name was the trap, and both ADDR_
 * names involved have since been renamed to what they are. Verified against
 * the original: RedrawMapRegion locks [0x00503100]. */
#define g_offscreen (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_OFFSCREEN_SURFACE)
#define g_mapDesc    ((void *)(uintptr_t)ADDR_MAP_DESC)

/* 0x0041E440: the recursive tile walker. Shifts the rectangle's edges right by
 * 8 to get tile indices and bounds-checks them against the map descriptor.
 * Not reconstructed. */
typedef void (__cdecl *am2_draw_map_tiles_fn)(const AM2_Rect *world,
                                              void *mapDesc, int32_t flag);

/* The map is painted once into a cache surface and the visible part copied out
 * of it a rectangle at a time; this is that copy. Both surfaces and both
 * origins come from globals, so the whole of the caller's rectangle is consumed
 * as geometry.
 *
 * Two different origins are subtracted, which is the only subtle thing here.
 * The SOURCE rectangle is measured from the camera, in tiles scaled by 16 --
 * that is where the region sits on the painted map. The DESTINATION point is
 * measured from the screen origin instead. They are not the same offset and
 * using one for both would slide the backdrop against the sprites drawn over
 * it.
 *
 * Nothing checks the result. A failed BltFast leaves whatever was in the back
 * buffer and the tile walker draws over it regardless, as in the original. */
static_assert(DDBLTFAST_WAIT == 0x10, "DDBLTFAST_WAIT");

#define g_mapCache (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)
#define g_cameraX  (*(const int32_t *)(uintptr_t)ADDR_CAMERA_X)
#define g_cameraY  (*(const int32_t *)(uintptr_t)ADDR_CAMERA_Y)
#define g_viewX  (*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X)
#define g_viewY  (*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y)

/* Tiles are 16 pixels, so the camera scales by 16 to reach pixels. */
#define TILE_SHIFT 4

void __cdecl BlitMapBackdrop(AM2_Rect world)
{
    RECT src;

    src.left   = world.left   - (g_cameraX << TILE_SHIFT);
    src.top    = world.top    - (g_cameraY << TILE_SHIFT);
    src.right  = world.right  - (g_cameraX << TILE_SHIFT);
    src.bottom = world.bottom - (g_cameraY << TILE_SHIFT);

    IDirectDrawSurface_BltFast(g_offscreen,
                               (DWORD)(world.left - g_viewX),
                               (DWORD)(world.top  - g_viewY),
                               g_mapCache, &src, DDBLTFAST_WAIT);
}

void __cdecl SetDrawTarget(LPDIRECTDRAWSURFACE surf)
{
    /* The compare is redundant -- storing unconditionally would behave
     * identically -- but it is what the original does. */
    if (g_drawTarget != surf)
        g_drawTarget = surf;
}

void __cdecl RedrawMapRegion(const AM2_Rect *world)
{
    /* Degenerate regions are rejected before any work, and the test is for
     * equality rather than ordering: an inverted rectangle would slip through
     * here exactly as it does in the original. */
    if (world->top == world->bottom)
        return;
    if (world->left == world->right)
        return;

    BlitMapBackdrop(*world);

    SetDrawTarget(g_offscreen);
    if (!LockSurface(g_offscreen))
        return;

    DrawMapObjects(world, g_mapDesc, 0);
    UnlockSurface();
}

/* Paint the map into its cache surface, one tile at a time -- 0x0042D580.
 *
 * This is where the map actually becomes pixels. Everything else in this file
 * moves the result about; this makes it.
 *
 * The caller asks for a rectangle in tile coordinates, it is clipped against
 * the visible area, and every surviving tile is blitted from the tile sheet
 * into the cache. Two things are worth knowing.
 *
 * The visible area is the camera. The four dwords from ADDR_CAMERA_X are read
 * as a RECT here and handed straight to IntersectRect, so the camera position
 * and the visible-tile rectangle are the same four words seen two ways.
 *
 * And the tile index is the source coordinates. The low five bits are the
 * column, scaled by sixteen, so the sheet is 32 tiles across and the index
 * decodes to a position in it with no lookup table at all.
 *
 * The row is `(idx >> 5) * 16`, and the original writes it as `sar eax,1` then
 * `and al,0xF0` -- which masks only the LOW BYTE and leaves everything above
 * bit 7 alone. Reading that as `& 0xF0` caps the row at 15 and puts every tile
 * with an index of 512 or more in the wrong place; the Boot Camp A/B went from
 * 22 differing pixels to 33,137 and said so immediately. `& ~0xF` is the
 * faithful reading, and is exactly `(idx >> 5) * 16`.
 *
 * Exercised by every map repaint, so an error here is visible immediately --
 * which is the point of doing it. */
static_assert(DDBLT_WAIT == 0x01000000, "DDBLT_WAIT");

#define g_mapCacheSurf (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)
#define g_mapSprite    (*(uint8_t **)(uintptr_t)ADDR_MAP_SURFACE)
#define g_mapTiles     (*(const uint16_t **)(uintptr_t)ADDR_MAP_TILES)
#define g_mapRowShift  (*(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT)
#define g_visibleTiles ((const AM2_Rect *)(uintptr_t)ADDR_VISIBLE_TILES)

void __cdecl PaintMapTiles(const AM2_Rect *tiles)
{
    LPDIRECTDRAWSURFACE dest = g_mapCacheSurf;
    LPDIRECTDRAWSURFACE sheet;
    AM2_Rect            clipped;
    int32_t             row, top;

    if (!dest)
        return;
    sheet = *(LPDIRECTDRAWSURFACE *)(g_mapSprite + 0x10);
    if (!sheet)
        return;

    if (!IntersectRect((LPRECT)&clipped, (const RECT *)tiles,
                       (const RECT *)g_visibleTiles))
        return;

    /* Tile coordinates become pixels relative to the camera. */
    top = (clipped.top - g_visibleTiles->top) * MAP_TILE_SIZE;

    for (row = clipped.top; row < clipped.bottom; row++) {
        const uint16_t *cell = g_mapTiles
                             + ((row << g_mapRowShift) + tiles->left);
        int32_t         left = (clipped.left - g_visibleTiles->left)
                               * MAP_TILE_SIZE;
        int32_t         col;

        for (col = clipped.left; col < clipped.right; col++) {
            uint16_t idx = *cell;
            RECT     to, from;

            to.left   = left;
            to.top    = top;
            to.right  = left + MAP_TILE_SIZE;
            to.bottom = top + MAP_TILE_SIZE;

            from.left   = (idx & MAP_SHEET_COLUMNS) * MAP_TILE_SIZE;
            from.top    = (int32_t)((idx >> 1) & ~0xFu);
            from.right  = from.left + MAP_TILE_SIZE;
            from.bottom = from.top + MAP_TILE_SIZE;

            IDirectDrawSurface_Blt(dest, &to, sheet, &from, DDBLT_WAIT, NULL);

            left += MAP_TILE_SIZE;
            cell++;
        }
        top += MAP_TILE_SIZE;
    }
}

/* Reload the tileset from disk into the map surface -- 0x0042C0E0.
 *
 * Reached from exactly one place: RestoreLostSurfaces, after the map surface
 * comes back from DDERR_SURFACELOST. Restoring a DirectDraw surface gives back
 * the memory but not the pixels, so they have to be read again -- and this
 * reads them from the `.atl` file rather than from any cache, which is why a
 * repaint that looks like it should be cheap opens a file.
 *
 * NAMED FROM ITS OWN STRINGS. It went into orig.h as ADDR_ON_MAP_RESTORED,
 * after the call site, and that is the one naming mistake this project keeps
 * making. The body says `RestoreTileSet` and `loadtileset`.
 *
 * The file is IFF: `FORM` <size> `TILE`, then chunks. Every `DIB ` chunk is one
 * tile bitmap; everything else is skipped with fseek. The loop is bounded by
 * the FORM size rather than by EOF, and the running offset counts the 8-byte
 * chunk header plus the payload -- so a chunk whose size is wrong walks the
 * counter off the end and the loop stops, rather than spinning.
 *
 * Two defects of the original are kept deliberately, both on the failure path.
 * A failed Lock is silent -- it frees the tile and moves to the next one -- and
 * the message that does exist, "Error on Lock in RestoreTileSet()", is printed
 * when the *copy* fails, by which time the surface is locked. That path does
 * not unlock it. So the one case that reports itself is also the one that
 * leaves the surface held; every later Lock in the process then fails. It is
 * the same shape as the Restore defect in LockSurface, and kept for the same
 * reason.
 *
 * THE OUT PARAMETER IS ONE BYTE, and that is worth recording because the
 * signature says otherwise. ReloadBitmapSurface hands this helper a whole dword
 * slot and reads the colour key back out of it; here the address passed is
 * unaligned -- the top byte of a scratch dword -- and never read. It cannot be
 * a dword store: the three bytes above it are the pixel buffer, which is freed
 * two statements later, so a 4-byte write would fault on the first tile of
 * every restore. The helper writes one byte. The value is discarded here.
 *
 * NOT EXERCISED, and the honest note is that it cannot easily be. Nothing loses
 * a surface under Xvfb -- no alt-tab, no mode change -- so neither the A/B nor
 * the pixel budget covers a line of this. It is verified by reading, by the
 * fingerprint, and by the fact that its callee set is entirely functions this
 * port already owns. Anyone running on a real display should alt-tab out of a
 * mission and back to exercise it. */
static_assert(mmioFOURCC('F', 'O', 'R', 'M') == 0x4D524F46, "'FORM'");
static_assert(mmioFOURCC('T', 'I', 'L', 'E') == 0x454C4954, "'TILE'");
static_assert(mmioFOURCC('D', 'I', 'B', ' ') == 0x20424944, "'DIB '");
static_assert(DDLOCK_WAIT == 1, "DDLOCK_WAIT");
static_assert(SEEK_CUR == 1, "the fseek whence the original pushes");
static_assert(sizeof(DDSURFACEDESC) == 0x6C, "the descriptor the original sizes");

/* Ten dwords then a 256-entry palette; MakeBitmap reads the same shape. */
#define DIB_HEADER_DWORDS  (10 + 256)
#define DIB_PALETTE_INDEX  10
#define TILESET_RESERVED   10

typedef int32_t (__cdecl *am2_sprintf_fn)(char *, const char *, ...);
typedef void *(__cdecl *am2_read_dib_fn)(am2_FILE *fp, uint32_t *header);

#define orig_sprintf       (*(am2_sprintf_fn)ADDR_GAME_SPRINTF)
#define orig_read_dib      (*(am2_read_dib_fn)ADDR_READ_DIB_CHUNK)

#define g_tilesetName    ((const char *)(uintptr_t)ADDR_TILESET_NAME)
#define g_tilesetPath    ((const char *)(uintptr_t)ADDR_TILESET_PATH)
#define g_tilesetReserve (*(int32_t *)(uintptr_t)ADDR_TILESET_RESERVE)
#define g_activePalette  (*(const uint32_t **)(uintptr_t)ADDR_ACTIVE_PALETTE)
/* g_mapSprite, above, is the same record PaintMapTiles reads: the global
 * holds a POINTER to it, so reaching the surface is two dereferences and
 * not one. Width and height sit at +0x1C and +0x20 of the same record. */
#define MAPSPR_OFF_SURFACE 0x10
#define MAPSPR_OFF_WIDTH   0x1C
#define MAPSPR_OFF_HEIGHT  0x20

void __cdecl RestoreTileSet(void)
{
    uint32_t   header[DIB_HEADER_DWORDS];
    uint8_t    remap[256];
    char       path[256];
    uint32_t   scratch;              /* the byte out-param; discarded */
    DDSURFACEDESC desc;
    am2_FILE  *fp;
    uint32_t   magic, formSize, chunkId, chunkSize;
    int32_t    offset, i;

    /* The answer is discarded, and the call is not optional: SetGameDir
     * CHDIRS, and the fopen below opens a bare filename that only resolves
     * because of it. It went in as `orig_path_exists`, which made the chdir
     * look like a side effect rather than the reason it is here. */
    SetGameDir(g_tilesetPath);
    orig_sprintf(path, (const char *)(uintptr_t)ADDR_FMT_ATL, g_tilesetName);

    fp = orig_fopen(path, (const char *)(uintptr_t)ADDR_MODE_RB);
    if (!fp) {
        orig_log((const char *)(uintptr_t)ADDR_MSG_TILESET_OPEN);
        return;
    }

    orig_fread(&magic, 4, 1, fp);
    if (magic != mmioFOURCC('F', 'O', 'R', 'M'))
        goto bad;

    orig_fread(&formSize, 4, 1, fp);
    orig_fread(&magic, 4, 1, fp);
    if (magic != mmioFOURCC('T', 'I', 'L', 'E'))
        goto bad;

    offset = 12;
    do {
        LPDIRECTDRAWSURFACE surf;
        void    *pixels;
        int32_t  from = 0;

        orig_fread(&chunkId, 4, 1, fp);
        orig_fread(&chunkSize, 4, 1, fp);
        offset += 8;

        if (chunkId != mmioFOURCC('D', 'I', 'B', ' ')) {
            orig_fseek(fp, (int32_t)chunkSize, SEEK_CUR);
            offset += (int32_t)chunkSize;
            continue;
        }
        offset += (int32_t)chunkSize;

        pixels = orig_read_dib(fp, header);
        if (!pixels)
            goto bad;

        /* The same remap MakeBitmap builds, reached differently: there the
         * first ten entries are reserved when a flag in the bitmap record is
         * clear, here when a global is set. With no active palette the table
         * is the identity and the reserve is not consulted at all. */
        if (g_activePalette) {
            if (g_tilesetReserve) {
                for (i = 0; i < TILESET_RESERVED; i++)
                    remap[i] = (uint8_t)i;
                from = TILESET_RESERVED;
            }
            for (i = from; i < 256; i++)
                remap[i] = NearestPalIndex(g_activePalette,
                                             SwapColourBytes(header[DIB_PALETTE_INDEX + i], 0),
                                             (uint32_t)from);
        } else {
            for (i = 0; i < 256; i++)
                remap[i] = (uint8_t)i;
        }

        surf = *(LPDIRECTDRAWSURFACE *)(g_mapSprite + MAPSPR_OFF_SURFACE);
        desc.dwSize = sizeof(desc);
        if (IDirectDrawSurface_Lock(surf, NULL, &desc, DDLOCK_WAIT, NULL) == DD_OK) {
            int32_t width  = *(int32_t *)(g_mapSprite + MAPSPR_OFF_WIDTH);
            int32_t height = *(int32_t *)(g_mapSprite + MAPSPR_OFF_HEIGHT);

            if (orig_blit_bitmap_in(desc.lpSurface, desc.lPitch, pixels,
                                    width, height, remap, &scratch))
                IDirectDrawSurface_Unlock(surf, desc.lpSurface);
            else
                orig_log((const char *)(uintptr_t)ADDR_MSG_TILESET_LOCK);
        }

        orig_free(pixels);
    } while (offset < (int32_t)formSize);

    orig_fclose(fp);
    return;

bad:
    orig_log((const char *)(uintptr_t)ADDR_MSG_TILESET_LOAD);
    orig_fclose(fp);
}

/* Compose one frame and put it on the back buffer -- 0x0042DA30.
 *
 * The top of the drawing path, and the last DirectDraw blit outside the
 * reconstruction. Everything else in this file paints INTO the offscreen
 * surface; this is what moves the result onto the back buffer for
 * PresentFrame to flip.
 *
 * Four steps. Decay the scroll counter, draw the scene into the offscreen
 * surface, bring the map up to date -- either a full RedrawMapRegion of the
 * view when something invalidated it, or the cheaper dirty-rectangle merge --
 * and then BltFast the view rectangle across.
 *
 * THE SAME FOUR NUMBERS ARE USED TWICE, which is the only thing here that
 * reads oddly. ADDR_BLIT_RECT is handed to RectSet to build the SOURCE
 * rectangle, and its first two fields are also the DESTINATION point. That is
 * correct rather than a slip: the offscreen surface and the back buffer are the
 * same size and the view sits at the same place on both, so the copy is
 * position-preserving and one rectangle describes both ends.
 *
 * The tail saves this frame's view rectangle, the second rectangle beside it
 * and the listener position into their `_PREV` copies -- which is what the
 * dirty-rectangle merge compares against next frame to find what scrolled --
 * and then clears the full-redraw flag, because the frame that honoured it has
 * now been composed.
 *
 * RectSet is called for its return value, which is the rectangle it just
 * filled; the original then copies the four fields into a second local to hand
 * to BltFast rather than passing the first one. Written with one local here --
 * the addresses differ, nothing observable does.
 *
 * Exercised by every frame, so the Boot Camp pixel budget is what checks it. */
#define g_blitRect   ((AM2_Rect *)(uintptr_t)ADDR_BLIT_RECT)
/* Not const: ScrollDecay shifts all four edges every frame. */
#define g_viewRect   ((AM2_Rect *)(uintptr_t)ADDR_VIEW_ORIGIN_X)
#define g_fullRedraw (*(int32_t *)(uintptr_t)ADDR_FULL_REDRAW)
#define g_backBuffer (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER)

typedef void (__cdecl *am2_void_fn)(void);

/* Save `n` dwords from `src` to `dst`. The original writes the nine stores out
 * one at a time, interleaved; they are independent, so a loop is the same
 * thing. */
static void SavePrev(void *dst, const void *src, int32_t dwords)
{
    int32_t i;
    for (i = 0; i < dwords; i++)
        ((uint32_t *)dst)[i] = ((const uint32_t *)src)[i];
}

#define g_shakeTime    (*(int32_t *)(uintptr_t)ADDR_SHAKE_TIME)
#define g_shakePhaseX  (*(float *)(uintptr_t)ADDR_SHAKE_PHASE_X)
#define g_shakeStepX   (*(int32_t *)(uintptr_t)ADDR_SHAKE_STEP_X)
#define g_shakePhaseY  (*(float *)(uintptr_t)ADDR_SHAKE_PHASE_Y)
#define g_shakeStepY   (*(int32_t *)(uintptr_t)ADDR_SHAKE_STEP_Y)
#define g_shakeAmp     (*(int32_t *)(uintptr_t)ADDR_SHAKE_AMPLITUDE)
#define g_shakeRate    (*(const float *)(uintptr_t)ADDR_SHAKE_RATE)
#define g_frameDeltaMs (*(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS)

/* One axis of the shake: advance the phase, bounce it off +/-amp reversing
 * the step, and answer the whole-pixel offset. */
static int32_t ShakeAxis(float *phase, int32_t *step, int32_t amp)
{
    *phase = (float)*step * g_shakeRate + *phase;

    if (*phase > (float)amp) {
        *phase = (float)amp;
        *step  = -*step;
    } else if ((float)(-amp) > *phase) {
        *phase = -(float)amp;
        *step  = -*step;
    }

    /* _ftol truncates toward zero, which is what a C cast does. */
    return (int32_t)*phase;
}

/* 0x0042B420 -- the screen SHAKE, one step per frame, and the first thing
 * ComposeFrame does.
 *
 * The timer is counted down by the per-frame delta. When it runs out
 * everything is zeroed -- timer, both phases, both steps and the amplitude --
 * so the view returns to exactly where it was and no residue is left for the
 * next shake to inherit.
 *
 * The amplitude fades only over the LAST 1024 ms: above that the shake is at
 * full strength, and `(amp * left) >> 10` is an arithmetic shift, so the taper
 * is linear rather than smooth.
 *
 * What moves is the view RECTANGLE, not a camera or a scale: the X offset is
 * added to left and right and the Y offset to top and bottom. Both axes use
 * the same faded amplitude.
 *
 * The comparisons are the x87's, which is why they are written with the
 * constant on the left in the second arm -- `(-amp) > phase` and not
 * `phase < -amp`. For ordinary numbers the two are the same; the original
 * compares in that direction and there is no reason to differ. */
#define g_depthCount (*(int32_t *)(uintptr_t)ADDR_DEPTH_COUNT)
#define g_depthHead  (*(int32_t *)(uintptr_t)ADDR_DEPTH_HEAD)
#define g_depthDC    (*(int32_t *)(uintptr_t)ADDR_DEPTH_FIELD_DC)

typedef void    (__cdecl *AM2_DrawObjFn)(void *obj, const AM2_Rect *world);
#define orig_draw_object  ((AM2_DrawObjFn)(uintptr_t)ADDR_DRAW_MAP_OBJECT)

/* One node of the depth list: the object, and the two links. The array is
 * fixed at 500 and the cursor sits eight bytes in front of it. */
typedef struct AM2_DepthNode {
    void                 *obj;
    struct AM2_DepthNode *prev;
    struct AM2_DepthNode *next;
} AM2_DepthNode;

#define g_depthNodes  ((AM2_DepthNode *)(uintptr_t)ADDR_DEPTH_NODES)
#define g_depthCursor (*(AM2_DepthNode **)(uintptr_t)ADDR_DEPTH_CURSOR)

typedef int32_t (__cdecl *AM2_DepthCompareFn)(void *a, void *b);
#define orig_depth_compare ((AM2_DepthCompareFn)(uintptr_t)ADDR_DEPTH_COMPARE)

/* Link a fresh node in after `at`, before `at`, or at the front, and leave the
 * cursor on it. Three shapes the original writes out four times between them;
 * the only one that needs care is `after`, which the original does NOT guard
 * against a null successor because it is only reached when there is one. */
static AM2_DepthNode *DepthLinkAfter(AM2_DepthNode *at, int32_t n)
{
    AM2_DepthNode *fresh = &g_depthNodes[n];

    g_depthCursor = fresh;
    fresh->prev   = at;
    fresh->next   = at->next;
    at->next      = fresh;
    fresh->next->prev = fresh;
    return fresh;
}

static AM2_DepthNode *DepthAppend(AM2_DepthNode *at, int32_t n)
{
    AM2_DepthNode *fresh = &g_depthNodes[n];

    g_depthCursor = fresh;
    at->next      = fresh;
    fresh->prev   = at;
    fresh->next   = (AM2_DepthNode *)0;
    return fresh;
}

static void DepthLinkBefore(AM2_DepthNode *at, int32_t n)
{
    AM2_DepthNode *fresh = &g_depthNodes[n];

    g_depthCursor = fresh;
    fresh->prev   = at->prev;
    fresh->next   = at;
    at->prev      = fresh;

    if (fresh->prev)
        fresh->prev->next = fresh;
    else
        g_depthHead = n;
}

/* 0x0041E160 -- insert one object into the depth list, sorted.
 *
 * The answer is NOT "did it draw": 0 means the list is FULL, which is what
 * makes the walker subdivide its region and try again with less to hold. An
 * object that does not meet the region is dropped and 1 is returned, because
 * nothing is wrong with the list.
 *
 * The list is doubly linked and kept sorted by the comparator, and the insert
 * starts from a CURSOR -- the node the last insert landed on -- rather than
 * from the head. Objects arrive in cell order, which is close to sorted, so
 * walking from the last position is usually a step or two; from the head it
 * would be a scan every time.
 *
 * Two flags override the comparator entirely: everything with 0x40 sorts
 * before everything with 0x20. It is read from both sides -- a 0x20 object
 * goes after the first 0x40 it finds, a 0x40 object before the first 0x20 --
 * and with neither present both fall through to the ordinary compare.
 *
 * An equal comparison inserts AFTER the cursor, so objects that compare the
 * same are drawn in the order the walk found them. */
int32_t __cdecl DepthInsert(void *obj, const AM2_Rect *world)
{
    RECT           hit;
    int32_t        n;
    int32_t        cmp;
    AM2_DepthNode *at;

    if (g_depthCount >= AM2_DEPTH_MAX)
        return 0;

    if (!IntersectRect(&hit, (const RECT *)((uint8_t *)obj + OBJ_OFF_BOUNDS),
                       (const RECT *)world))
        return 1;

    n = g_depthCount;
    g_depthNodes[n].obj = obj;

    if (n == 0) {
        g_depthHead            = 0;
        g_depthNodes[0].prev   = (AM2_DepthNode *)0;
        g_depthNodes[0].next   = (AM2_DepthNode *)0;
        g_depthCursor          = &g_depthNodes[0];
        g_depthCount           = 1;
        return 1;
    }

    if (*(const uint32_t *)obj & OBJ_DEPTH_FLAG_FRONT) {
        for (at = &g_depthNodes[g_depthHead]; at; at = at->next)
            if (*(const uint8_t *)at->obj & OBJ_DEPTH_FLAG_BACK) {
                g_depthCursor = at;
                if (at->next)
                    DepthLinkAfter(at, n);
                else
                    DepthAppend(at, n);
                g_depthCount = n + 1;
                return 1;
            }
    } else if (*(const uint32_t *)obj & OBJ_DEPTH_FLAG_BACK) {
        AM2_DepthNode *head = &g_depthNodes[g_depthHead];

        for (at = head; at; at = at->next)
            if (*(const uint8_t *)at->obj & OBJ_DEPTH_FLAG_FRONT) {
                if (at == head) {
                    /* At the front, and the head moves. */
                    g_depthCursor = &g_depthNodes[n];
                    at->prev      = g_depthCursor;
                    g_depthCursor->prev = (AM2_DepthNode *)0;
                    g_depthCursor->next = at;
                    g_depthHead   = n;
                    g_depthCount  = n + 1;
                    return 1;
                }
                DepthLinkBefore(at, n);
                g_depthCount = n + 1;
                return 1;
            }
    }

    at  = g_depthCursor;
    cmp = orig_depth_compare(obj, at->obj);

    if (cmp == 0) {
        DepthLinkAfter(at, n);
        g_depthCount = n + 1;
        return 1;
    }

    if (cmp < 0) {
        while (at->prev) {
            g_depthCursor = at->prev;
            if (orig_depth_compare(obj, g_depthCursor->obj) >= 0) {
                DepthLinkAfter(g_depthCursor, n);
                g_depthCount = n + 1;
                return 1;
            }
            at = g_depthCursor;
        }
        /* Off the front. */
        g_depthCursor       = &g_depthNodes[n];
        at->prev            = g_depthCursor;
        g_depthCursor->prev = (AM2_DepthNode *)0;
        g_depthCursor->next = at;
        g_depthHead         = n;
        g_depthCount        = n + 1;
        return 1;
    }

    while (at->next) {
        g_depthCursor = at->next;
        if (orig_depth_compare(obj, g_depthCursor->obj) <= 0) {
            DepthLinkBefore(g_depthCursor, n);
            g_depthCount = n + 1;
            return 1;
        }
        at = g_depthCursor;
    }

    DepthAppend(at, n);
    g_depthCount = n + 1;
    return 1;
}

/* 0x0041E440 -- the map's OBJECT painter: collect, sort, draw, and split the
 * region and recurse when the sort runs out of room.
 *
 * The grid is a fixed spatial hash and not the map's tile extent -- measured
 * as {cols 16, rows 16, shift 4} on both Boot Camp and the campaign's first
 * map, with the shift exactly log2(cols), so a cell is 256 world units
 * square.
 *
 * **The BOTTOM edge is clamped against `cols - 1`, not `rows - 1`.** That is
 * what the original does and it is reproduced. On a square grid the two are
 * the same number, which is why nothing has ever noticed; whether a
 * non-square grid exists is not established, so this is recorded rather than
 * corrected.
 *
 * The two "already seen" tests are the interesting part. An object wider or
 * taller than a cell is in several cells, and would otherwise be handed to
 * the sort once per cell -- so a cell only offers an object whose bounds
 * START in it. The exception is the clamped edge: at the first row or column
 * of the walk, an object that begins off-screen has no earlier cell to be
 * offered from, so the test is skipped there.
 *
 * When the sort refuses -- it holds at most 500 -- there are two behaviours.
 * With `deep` set, the rest of THIS CELL's list is abandoned and the walk
 * moves on. With it clear, the region is split in half along its longer axis
 * and each half is walked separately, which gives the sort two smaller sets
 * instead of one it cannot hold.
 *
 * The split sets `deep` for the halves once the region is down to 0x20, so
 * the recursion has a floor and cannot subdivide forever.
 *
 * **The vertical split leaves the second rectangle's BOTTOM unwritten.** Seven
 * of the eight fields are stored and `[esp+0x44]` is not; the horizontal
 * split writes all eight. Reproduced -- but ours is a different uninitialised
 * value from theirs, so this is one place where the two builds cannot be
 * expected to agree if it is ever reached. Nothing observed reaches it: it
 * needs 500 visible objects and a region wider than it is tall. */
void __cdecl DrawMapObjects(const AM2_Rect *world, void *desc, int32_t deep)
{
    const uint8_t *d    = (const uint8_t *)desc;
    int32_t        cols = 0;
    int32_t        last;
    int32_t        l, t, r, b, l0, t0;
    int32_t        row, col, stride;
    const uint8_t *const *cell;

    g_depthCount = 0;
    g_depthHead  = -1;
    g_depthDC    = 0;

    l = world->left   >> AM2_CELL_SHIFT;
    t = world->top    >> AM2_CELL_SHIFT;
    r = world->right  >> AM2_CELL_SHIFT;
    b = world->bottom >> AM2_CELL_SHIFT;

    if (b < 0)
        return;
    if (t > *(const int32_t *)(d + MAPDESC_OFF_ROWS) - 1)
        return;
    if (r < 0)
        return;

    cols = *(const int32_t *)(d + MAPDESC_OFF_COLS);
    last = cols - 1;
    if (l > last)
        return;

    l0 = (l <= 0) ? 0 : l;
    t0 = (t <= 0) ? 0 : t;
    if (r >= last)
        r = last;
    if (b >= last)          /* cols - 1, as the original has it */
        b = last;

    cell = (const uint8_t *const *)
           (*(const uint8_t *const *)(d + MAPDESC_OFF_CELLS))
           + ((t0 << *(const int32_t *)(d + MAPDESC_OFF_SHIFT)) + l0);
    stride = cols - r + l0 - 1;

    for (row = t0; row <= b; row++) {
        for (col = l0; col <= r; col++) {
            const uint8_t *node = *cell++;

            while (node) {
                void *obj = *(void *const *)(node + CELL_NODE_OFF_OBJ);

                if (!obj)
                    break;
                if (ObjFlagBit1(obj))
                    goto nextNode;

                if (*(const int32_t *)((uint8_t *)obj + OBJ_OFF_BOUNDS + 4)
                        >> AM2_CELL_SHIFT < row
                    && row > t0)
                    goto nextNode;
                if (*(const int32_t *)((uint8_t *)obj + OBJ_OFF_BOUNDS)
                        >> AM2_CELL_SHIFT < col
                    && col > l0)
                    goto nextNode;

                if (!DepthInsert(obj, world)) {
                    if (!deep)
                        goto subdivide;
                    break;      /* abandon the rest of this cell's list */
                }
            nextNode:
                node = *(const uint8_t *const *)(node + CELL_NODE_OFF_NEXT);
            }
        }
        cell += stride;
    }

    if (g_depthHead != -1) {
        const uint8_t *n = (const uint8_t *)(uintptr_t)ADDR_DEPTH_NODES
                           + (uint32_t)g_depthHead * AM2_DEPTH_NODE_SIZE;

        while (n) {
            orig_draw_object(*(void *const *)(n + DEPTH_OFF_OBJ), world);
            n = *(const uint8_t *const *)(n + DEPTH_OFF_NEXT);
        }
    }
    return;

subdivide:
    {
        int32_t   left   = world->left;
        int32_t   top    = world->top;
        int32_t   right  = world->right;
        int32_t   bottom = world->bottom;
        int32_t   dw     = right - left;
        int32_t   dh     = bottom - top;
        int32_t   half;
        int32_t   sub;
        AM2_Rect  a;
        AM2_Rect  c;

        if (dw > dh) {
            sub  = (dw <= AM2_SUBDIVIDE_FLOOR) ? deep : 1;
            half = left + dw / 2;

            a.left = left;   a.top = top;    a.right  = half;  a.bottom = bottom;
            c.left = half + 1; c.top = top;  c.right  = right;
            /* c.bottom is NOT written by the original -- see above. */
        } else {
            if (dh > AM2_SUBDIVIDE_FLOOR)
                deep = 1;
            sub  = deep;
            half = top + dh / 2;

            a.left = left; a.top = top;      a.right = right;  a.bottom = half;
            c.left = left; c.top = half + 1; c.right = right;  c.bottom = bottom;
        }

        DrawMapObjects(&a, desc, sub);
        DrawMapObjects(&c, desc, sub);
    }
}

void __cdecl ScrollDecay(void)
{
    int32_t left = g_shakeTime - g_frameDeltaMs;
    int32_t amp;
    int32_t dx;
    int32_t dy;

    g_shakeTime = left;
    if (left <= 0) {
        g_shakeTime   = 0;
        g_shakeStepX  = 0;
        g_shakeStepY  = 0;
        g_shakeAmp    = 0;
        g_shakePhaseX = 0.0f;
        g_shakePhaseY = 0.0f;
        return;
    }

    amp = g_shakeAmp;
    if (left <= AM2_SHAKE_FADE_MS)
        amp = (amp * left) >> 10;

    dx = ShakeAxis(&g_shakePhaseX, &g_shakeStepX, amp);
    g_viewRect->left  += dx;
    g_viewRect->right += dx;

    dy = ShakeAxis(&g_shakePhaseY, &g_shakeStepY, amp);
    g_viewRect->top    += dy;
    g_viewRect->bottom += dy;
}

void __cdecl ComposeFrame(void)
{
    AM2_Rect src;

    ScrollDecay();
    ScrollMapCache();

    if (g_fullRedraw)
        RedrawMapRegion(g_viewRect);
    else
        ScrollView();

    ResetDrawCounts();

    RectSet(&src, g_blitRect->left, g_blitRect->top,
            g_blitRect->right, g_blitRect->bottom);

    IDirectDrawSurface_BltFast(g_backBuffer,
                               (DWORD)g_blitRect->left,
                               (DWORD)g_blitRect->top,
                               g_offscreen, (LPRECT)&src, DDBLTFAST_WAIT);

    /* This frame becomes last frame. */
    SavePrev((void *)(uintptr_t)ADDR_LISTENER_POS_PREV,
             (const void *)(uintptr_t)ADDR_LISTENER_POS, 1);
    SavePrev((void *)(uintptr_t)ADDR_VIEW_RECT_PREV,
             (const void *)(uintptr_t)ADDR_VIEW_ORIGIN_X, 4);
    SavePrev((void *)(uintptr_t)ADDR_SECOND_RECT_PREV,
             (const void *)(uintptr_t)ADDR_SECOND_RECT, 4);

    g_fullRedraw = 0;
}

/* Scroll the offscreen surface and repaint what that exposed -- 0x0041D060.
 *
 * ComposeFrame's other branch: when nothing has invalidated the whole view,
 * this is what keeps it current. The map has already been drawn once, so
 * scrolling by a few pixels does not need it drawn again -- the part still on
 * screen is copied to where it now belongs and only the newly exposed strips
 * are painted.
 *
 * THE BLIT IS THE SURFACE ONTO ITSELF. Source and destination are both the
 * offscreen surface, which is what makes this a scroll rather than a copy.
 * DirectDraw is being asked to move a region within one surface, and it is only
 * safe because the two rectangles are the same size and BltFast handles the
 * overlap.
 *
 * The three rectangles are the whole of the logic. `now` is the current view
 * clipped to the map, `was` is last frame's -- ComposeFrame saved it -- and
 * `common` is what they share. No overlap at all means the view jumped further
 * than its own size, and then there is nothing to salvage and the lot is
 * repainted.
 *
 * The two strips are computed in the two coordinate systems that matter, which
 * is the easy thing to get wrong. The BLIT SOURCE is measured from the OLD
 * view's origin, because that is where those pixels currently sit; the
 * destination point is measured from the NEW one. Subtracting the same origin
 * twice would slide the map against itself by exactly the scroll distance.
 *
 * One rectangle is reused for both strips, and the halves are filled in at
 * different times: the vertical pass sets its top and bottom -- even in the
 * branch where it paints nothing -- and the horizontal pass then sets left and
 * right. That is the original\'s shape, and it is why the no-vertical-scroll
 * branch still writes two fields.
 *
 * Exercised by `tools/ab.sh mission`, which exists because nothing else got
 * here: while either opening dialog is up the game composes no frames at all,
 * so this ran zero times under every earlier configuration. */
#define g_mapBounds  ((const AM2_Rect *)(uintptr_t)ADDR_MAP_BOUNDS)
#define g_viewPrev   ((const AM2_Rect *)(uintptr_t)ADDR_VIEW_RECT_PREV)

typedef void (__cdecl *am2_rect_fn)(const AM2_Rect *r);

/* Scroll the painted map cache and repaint what that exposed -- 0x0042D6D0.
 *
 * ScrollView one level down: that one scrolls the OFFSCREEN surface by pixels
 * to follow the view, this scrolls the CACHE surface by whole tiles to follow
 * the camera. Both blit a surface onto itself; the difference is the unit and
 * what they repaint with -- RedrawMapRegion there, PaintMapTiles here.
 *
 * The camera is not moved every frame. It only jumps when the view has drifted
 * outside a TWO-TILE margin, which is what the `< cx*16 || > (cx+2)*16` test
 * is: within that band the existing cache still covers the view and nothing
 * needs to move at all. When it does jump it snaps to a tile boundary, so the
 * delta is always whole tiles and the blit is always tile-aligned.
 *
 * THREE EARLY EXITS, and they are not interchangeable. A pending full redraw
 * repaints the whole camera rect and returns, because scrolling pixels that are
 * about to be overwritten is wasted work. No movement at all returns having
 * done nothing. And a delta of a whole screen or more ALSO repaints everything
 * -- there is no overlap left to salvage, and blitting it would read outside
 * the surface.
 *
 * The two strips are painted with the vertical one first, and the horizontal
 * one is then narrowed to exclude it: the corner belongs to exactly one of
 * them, and painting it twice would be visible as a double-drawn tile.
 *
 * The log call on the no-movement path is the game's own and does nothing --
 * 0x0045CAA0 is five bytes of `ret` in this build. Kept because removing it
 * would be a behavioural difference rather than a transcription.
 *
 * Exercised by tools/ab.sh mission, which scrolls; nothing else moves the
 * camera. */
#define g_mapCacheS   (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)
#define g_camera      ((AM2_Rect *)(uintptr_t)ADDR_VISIBLE_TILES)
#define g_viewTilesW  (*(const int32_t *)(uintptr_t)ADDR_VIEW_TILES_W)
#define g_viewTilesH  (*(const int32_t *)(uintptr_t)ADDR_VIEW_TILES_H)
#define g_mapTilesW   (*(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W)
#define g_mapTilesH   (*(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H)

/* The margin, in tiles, the view may drift before the camera follows it. */
#define CAMERA_MARGIN 2

/* 0x0041D000 -- repaint every registered dirty rectangle that meets the given
 * region.
 *
 * The list is a chain of 20-byte records through an index at +0x12, and
 * record ZERO is the head sentinel: the walk starts at `records[0].next` and
 * stops when a `next` is zero, so index 0 is both the head and the end
 * marker. That is what the head global at ADDR_DIRTY_HEAD is -- it was filed
 * as a third draw counter, which is what it looks like from the sweep that
 * clears the three together.
 *
 * The clipped rectangle is what gets repainted, not the record: an entry
 * half outside the region redraws only the half inside. */
void __cdecl RepaintDirtyList(const AM2_Rect *region)
{
    uint32_t n = *(const uint16_t *)(uintptr_t)ADDR_DIRTY_HEAD;

    while (n) {
        uint8_t *rec = (uint8_t *)(uintptr_t)ADDR_DIRTY_RECTS
                       + n * AM2_DIRTY_RECORD_SIZE;
        RECT     hit;

        if (IntersectRect(&hit, (const RECT *)rec, (const RECT *)region))
            RedrawMapRegion((const AM2_Rect *)&hit);

        n = *(const uint16_t *)(rec + DIRTY_OFF_NEXT);
    }
}

void __cdecl ScrollMapCache(void)
{
    LPDIRECTDRAWSURFACE cache = g_mapCacheS;
    int32_t  dx = 0, dy = 0, adx, ady;
    AM2_Rect wide, tall, src;

    if (!cache)
        return;

    if (g_viewRect->left < (g_camera->left << TILE_SHIFT)
        || g_viewRect->left > ((g_camera->left + CAMERA_MARGIN) << TILE_SHIFT)) {
        int32_t now = g_viewRect->left >> TILE_SHIFT;
        dx = now - g_camera->left;
        g_camera->left  = now;
        g_camera->right = g_viewTilesW + now;
    }
    if (g_viewRect->top < (g_camera->top << TILE_SHIFT)
        || g_viewRect->top > ((g_camera->top + CAMERA_MARGIN) << TILE_SHIFT)) {
        int32_t now = g_viewRect->top >> TILE_SHIFT;
        dy = now - g_camera->top;
        g_camera->top    = now;
        g_camera->bottom = g_viewTilesH + now;
    }

    g_camera->left   = Clamp(g_camera->left,   0, g_mapTilesW - 1);
    g_camera->right  = Clamp(g_camera->right,  0, g_mapTilesW - 1);
    g_camera->top    = Clamp(g_camera->top,    0, g_mapTilesH - 1);
    g_camera->bottom = Clamp(g_camera->bottom, 0, g_mapTilesH - 1);

    if (g_fullRedraw) {
        PaintMapTiles(g_camera);
        return;
    }
    if (dx == 0 && dy == 0) {
        orig_log((const char *)g_camera);   /* stubbed to `ret` */
        return;
    }

    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;
    if (adx >= g_viewTilesW || ady >= g_viewTilesH) {
        PaintMapTiles(g_camera);
        return;
    }

    /* Move the tiles that are still good. Source measured from the positive
     * part of the delta, destination from the negative -- one of the two is
     * always zero, which is what makes this one expression for both
     * directions. */
    {
        int32_t sx = (dx > 0) ? dx : 0;
        int32_t sy = (dy > 0) ? dy : 0;

        src.left   = sx << TILE_SHIFT;
        src.top    = sy << TILE_SHIFT;
        src.right  = ((g_viewTilesW - adx) << TILE_SHIFT) + src.left;
        src.bottom = ((g_viewTilesH - ady) << TILE_SHIFT) + src.top;

        IDirectDrawSurface_BltFast(cache,
                                   (DWORD)(((dx > 0) ? 0 : -dx) << TILE_SHIFT),
                                   (DWORD)(((dy > 0) ? 0 : -dy) << TILE_SHIFT),
                                   cache, (LPRECT)&src, DDBLTFAST_WAIT);
    }

    wide.left  = g_camera->left;
    wide.right = g_camera->right;

    if (dx > 0) {
        tall.left  = g_camera->right - dx;
        tall.right = g_camera->right;
        wide.right = tall.left;
    } else if (dx < 0) {
        tall.left  = g_camera->left;
        tall.right = g_camera->left - dx;
        wide.left  = tall.right;
    }
    if (dx != 0) {
        tall.top    = g_camera->top;
        tall.bottom = g_camera->bottom;
        PaintMapTiles(&tall);
    }

    if (dy == 0)
        return;
    if (dy > 0) {
        wide.top    = g_camera->bottom - dy;
        wide.bottom = g_camera->bottom;
    } else {
        wide.top    = g_camera->top;
        wide.bottom = g_camera->top - dy;
    }
    PaintMapTiles(&wide);
}

void __cdecl ScrollView(void)
{
    AM2_Rect now, was, common, strip;

    IntersectRect((LPRECT)&now, (const RECT *)g_viewRect,
                  (const RECT *)g_mapBounds);
    IntersectRect((LPRECT)&was, (const RECT *)g_viewPrev,
                  (const RECT *)g_mapBounds);

    if (!IntersectRect((LPRECT)&common, (const RECT *)&now,
                       (const RECT *)&was)) {
        /* The view moved further than its own width or height. */
        RedrawMapRegion(&now);
        return;
    }

    /* Only worth moving if it actually moved. */
    if (now.left != was.left || now.top != was.top) {
        AM2_Rect src;

        src.left   = common.left   - was.left;
        src.top    = common.top    - was.top;
        src.right  = common.right  - was.left;
        src.bottom = common.bottom - was.top;

        IDirectDrawSurface_BltFast(g_offscreen,
                                   (DWORD)(common.left - now.left),
                                   (DWORD)(common.top  - now.top),
                                   g_offscreen, (LPRECT)&src, DDBLTFAST_WAIT);
    }

    RepaintDirtyList(&common);

    /* The horizontal band the scroll exposed, and the vertical extent the
     * next strip will use. */
    if (was.top < now.top) {
        strip.left   = now.left;
        strip.top    = common.bottom;
        strip.right  = now.right;
        strip.bottom = now.bottom;
        RedrawMapRegion(&strip);
        strip.top    = now.top;
        strip.bottom = common.bottom;
    } else if (was.top > now.top) {
        strip.left   = now.left;
        strip.top    = now.top;
        strip.right  = now.right;
        strip.bottom = common.top;
        RedrawMapRegion(&strip);
        strip.top    = common.top;
        strip.bottom = now.bottom;
    } else {
        strip.top    = now.top;
        strip.bottom = now.bottom;
    }

    /* The vertical band, over whatever rows the pass above settled on. */
    if (was.left < now.left) {
        strip.left  = common.right;
        strip.right = now.right;
        RedrawMapRegion(&strip);
    } else if (was.left > now.left) {
        strip.left  = now.left;
        strip.right = common.left;
        RedrawMapRegion(&strip);
    }
}

/* ------------------------------------------------------ vline ---- */

#define g_screenClip  (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_CLIP)
#define g_pitch       (*(const int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)
#define g_framebuffer (*(uint8_t **)(uintptr_t)ADDR_FRAMEBUFFER)
/* g_drawTarget above is already this address; one name for it. */

/* 0x0041CBA0. Draw a clipped vertical line one byte at a time.
 *
 * A role name -- it says nothing about itself. Four arguments: the column, the
 * two rows, and the colour byte.
 *
 * The first branch is the interesting one. When y0 > y1 the original calls
 * 0x0042E170, which is ADDR_NULL_STUB -- sixteen bytes containing a single
 * `ret`. Whatever was there in 1999, a swap or a complaint, is gone from this
 * build, and the values are used unswapped: the pointer arithmetic below then
 * puts `end` BEFORE `p`, the unsigned compare catches it, and nothing is
 * drawn. So an inverted line is silently dropped, and the call that was
 * supposed to prevent that does nothing. Reproduced, stub and all.
 *
 * Clipping is against the shared screen rectangle that text and sprites use.
 * Note the asymmetry the original has: `right` and `bottom` are exclusive
 * bounds tested with `>=`, and the bottom clamp is `bottom - 1`, while `left`
 * and `top` are inclusive.
 *
 * It locks the surface and does NOT unlock it -- the pairing is the caller's,
 * which is why this shows up in the bracket list with only one half. */
static void __cdecl DrawVLine(int32_t x, int32_t y0, int32_t y1,
                              int32_t colour)
{
    uint8_t *p;
    uint8_t *end;

    if (y0 > y1)
        NullStub();

    if (x < g_screenClip.left || x >= g_screenClip.right)
        return;

    if (y0 >= g_screenClip.bottom || y1 < g_screenClip.top)
        return;

    if (y0 < g_screenClip.top)
        y0 = g_screenClip.top;

    if (y1 >= g_screenClip.bottom)
        y1 = g_screenClip.bottom - 1;

    if (!LockSurface(g_drawTarget))
        return;

    p   = g_framebuffer + (int32_t)g_pitch * y0 + x;
    end = p + (int32_t)g_pitch * (y1 - y0);

    if (p > end)
        return;

    do {
        *p = (uint8_t)colour;
        p += g_pitch;
    } while (p <= end);
}

/* 0x0041CC40. The horizontal twin of DrawVLine.
 *
 * Same shape throughout: the same inert ADDR_NULL_STUB call when the ends are
 * the wrong way round, the same asymmetric clip -- `right` and `bottom`
 * exclusive, `left` and `top` inclusive -- and the same Lock without an
 * Unlock.
 *
 * Where it differs is the fill. A vertical line steps by the pitch and can
 * only go a byte at a time; a horizontal one is contiguous, so the original
 * replicates the colour byte into a dword, `rep stosd` for count/4 and
 * `rep stosb` for the remaining count&3. That is memset exactly, and it is
 * written as memset here -- same bytes, and the shift-and-or dance says
 * nothing a reader needs.
 *
 * Note there is no `p > end` guard like DrawVLine's, because the count is
 * computed as x1 - x0 + 1 and the clip has already ordered them. An inverted
 * line still cannot reach here: it is dropped by the clip returning early. */
static void __cdecl DrawHLine(int32_t y, int32_t x0, int32_t x1,
                              int32_t colour)
{
    uint8_t *p;
    int32_t  count;

    if (x0 > x1)
        NullStub();

    if (y < g_screenClip.top || y >= g_screenClip.bottom)
        return;

    if (x0 >= g_screenClip.right || x1 < g_screenClip.left)
        return;

    if (x0 < g_screenClip.left)
        x0 = g_screenClip.left;

    if (x1 >= g_screenClip.right)
        x1 = g_screenClip.right - 1;

    if (!LockSurface(g_drawTarget))
        return;

    p     = g_framebuffer + (int32_t)g_pitch * y + x0;
    count = x1 - x0 + 1;

    memset(p, (uint8_t)colour, (size_t)count);
}

/* 0x0041CDC0. The rectangle outline the two line drawers exist for: left and
 * right edges, then top and bottom.
 *
 * `right` and `bottom` are drawn ON, so the rectangle is inclusive at every
 * edge -- while the clip inside the line drawers treats those same two as
 * exclusive bounds of the SCREEN. The two meanings coexist because they are
 * about different rectangles; it is still the kind of thing to read twice.
 *
 * The original pushes all sixteen arguments and cleans them with one
 * `add esp, 0x40` after the last call. */
static void __cdecl DrawRect(const AM2_Rect *r, int32_t colour)
{
    DrawVLine(r->left,  r->top,  r->bottom, colour);
    DrawVLine(r->right, r->top,  r->bottom, colour);
    DrawHLine(r->top,    r->left, r->right, colour);
    DrawHLine(r->bottom, r->left, r->right, colour);
}

/* 0x00413610. Outline a rectangle held in view space.
 *
 * A role name. The four edges live in the same coordinate space as
 * ADDR_VIEW_ORIGIN -- the map's scrolled origin -- so subtracting it gives
 * screen coordinates, which is the whole of what this function computes. What
 * the box MEANS is not established: its state is touched by exactly three
 * functions, this one and two neighbours, and none of them names itself.
 *
 * This is the full bracket. The two line drawers it reaches through DrawRect
 * each Lock without Unlocking; the Unlock for all of them is here, once, after
 * the whole outline is drawn. That is why counting "functions that call the
 * bracket" finds halves -- the pairing is per FEATURE, not per function.
 *
 * The colour is a byte read from its own global rather than passed in. */
static void __cdecl DrawViewRect(void)
{
    const AM2_Rect *box = (const AM2_Rect *)(uintptr_t)ADDR_VIEW_RECT;
    const int32_t   ox  = *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X;
    const int32_t   oy  = *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y;
    AM2_Rect        on_screen;
    int32_t         colour;

    if (*(const int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON == 0)
        return;

    if (!LockSurface(g_drawTarget))
        return;

    on_screen.left   = box->left   - ox;
    on_screen.top    = box->top    - oy;
    on_screen.right  = box->right  - ox;
    on_screen.bottom = box->bottom - oy;

    colour = *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR;

    DrawRect(&on_screen, colour);
    UnlockSurface();
}

void __cdecl ResetDrawCounts(void)
{
    *(uint16_t *)(uintptr_t)ADDR_DIRTY_HEAD  = 0;
    *(uint16_t *)(uintptr_t)ADDR_DRAW_COUNT_B = 0;
    *(uint16_t *)(uintptr_t)ADDR_DRAW_COUNT_A = 0;
}

int mapdraw_install(void)
{
    patch_replace(ADDR_RESET_DRAW_COUNTS, (const void *)ResetDrawCounts,
                  "ResetDrawCounts", 0);
    patch_replace(ADDR_DRAW_VLINE, (const void *)DrawVLine, "DrawVLine", 2);
    patch_replace(ADDR_DRAW_HLINE, (const void *)DrawHLine, "DrawHLine", 2);
    patch_replace(ADDR_DRAW_RECT, (const void *)DrawRect, "DrawRect", 2);
    patch_replace(ADDR_DRAW_VIEW_RECT, (const void *)DrawViewRect,
                  "DrawViewRect", 2);
    int rc = 0;

    rc |= patch_replace(ADDR_MERGE_DIRTY, (const void *)ScrollView,
                        "ScrollView", 0);
    rc |= patch_replace(ADDR_SCROLL_MAP_CACHE, (const void *)ScrollMapCache,
                        "ScrollMapCache", 0);

    rc |= patch_replace(ADDR_COMPOSE_FRAME, (const void *)ComposeFrame,
                        "ComposeFrame", 0);
    rc |= patch_replace(ADDR_SCROLL_DECAY, (const void *)ScrollDecay,
                        "ScrollDecay", 1);
    rc |= patch_replace(ADDR_REPAINT_DIRTY_LIST, (const void *)RepaintDirtyList,
                        "RepaintDirtyList", 1);
    rc |= patch_replace(ADDR_DRAW_MAP_OBJECTS, (const void *)DrawMapObjects,
                        "DrawMapObjects", 3);
    rc |= patch_replace(ADDR_DEPTH_INSERT, (const void *)DepthInsert,
                        "DepthInsert", 1);

    rc |= patch_replace(ADDR_SET_DRAW_TARGET, (const void *)SetDrawTarget, "SetDrawTarget", 1);
    rc |= patch_replace(ADDR_RESTORE_TILESET, (const void *)RestoreTileSet,
                        "RestoreTileSet", 1);
    rc |= patch_replace(ADDR_REDRAW_MAP_REGION, (const void *)RedrawMapRegion,
                        "RedrawMapRegion", 1);
    rc |= patch_replace(ADDR_BLIT_MAP_BACKDROP, (const void *)BlitMapBackdrop,
                        "BlitMapBackdrop", 4);
    rc |= patch_replace(ADDR_PAINT_MAP_TILES, (const void *)PaintMapTiles,
                        "PaintMapTiles", 1);
    return rc;
}
