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
#include "surface.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <stdio.h>   /* SEEK_CUR only */

#define g_drawTarget (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_LOCKED_SURFACE)
/* ADDR_BACK_SURFACE is misnamed in orig.h and its comment there says so: this
 * is the OFFSCREEN surface, and the real back buffer is ADDR_FONT_SURFACE.
 * This file used to call it g_backBuffer, which made the same identifier mean
 * two different surfaces in two files -- device.cpp's g_backBuffer is the other
 * one. The address was always right; only the name was a trap, and it is the
 * kind that costs an afternoon the first time somebody writes code touching
 * both. Verified against the original: RedrawMapRegion locks [0x00503100]. */
#define g_offscreen (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_SURFACE)
#define g_mapDesc    ((void *)(uintptr_t)ADDR_MAP_DESC)

/* 0x0041E440: the recursive tile walker. Shifts the rectangle's edges right by
 * 8 to get tile indices and bounds-checks them against the map descriptor.
 * Not reconstructed. */
typedef void (__cdecl *am2_draw_map_tiles_fn)(const AM2_Rect *world,
                                              void *mapDesc, int32_t flag);
#define orig_draw_map_tiles (*(am2_draw_map_tiles_fn)ADDR_DRAW_MAP_TILES)

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

    orig_draw_map_tiles(world, g_mapDesc, 0);
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
typedef int32_t (__cdecl *am2_data_path_exists_fn)(const char *path);
typedef uint32_t (__cdecl *am2_colour_of_fn)(uint32_t entry);
typedef uint8_t (__cdecl *am2_match_colour_fn)(const void *palette,
                                               uint32_t colour, uint32_t from);

#define orig_sprintf       (*(am2_sprintf_fn)ADDR_GAME_SPRINTF)
#define orig_read_dib      (*(am2_read_dib_fn)ADDR_READ_DIB_CHUNK)
#define orig_path_exists   (*(am2_data_path_exists_fn)ADDR_DATA_PATH_EXISTS)
#define orig_colour_of     (*(am2_colour_of_fn)ADDR_COLOUR_OF_ENTRY)
#define orig_match_colour  (*(am2_match_colour_fn)ADDR_MATCH_COLOUR)

#define g_tilesetName    ((const char *)(uintptr_t)ADDR_TILESET_NAME)
#define g_tilesetPath    ((const char *)(uintptr_t)ADDR_TILESET_PATH)
#define g_tilesetReserve (*(int32_t *)(uintptr_t)ADDR_TILESET_RESERVE)
#define g_activePalette  (*(void **)(uintptr_t)ADDR_ACTIVE_PALETTE)
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

    /* The answer is discarded -- the call is kept because it is the original's
     * and because it resolves the data path as a side effect. */
    orig_path_exists(g_tilesetPath);
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
                remap[i] = orig_match_colour(g_activePalette,
                                             orig_colour_of(header[DIB_PALETTE_INDEX + i]),
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
#define g_viewRect   ((const AM2_Rect *)(uintptr_t)ADDR_VIEW_ORIGIN_X)
#define g_fullRedraw (*(int32_t *)(uintptr_t)ADDR_FULL_REDRAW)
#define g_backBuffer (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_FONT_SURFACE)

typedef void (__cdecl *am2_void_fn)(void);
#define orig_scroll_decay      (*(am2_void_fn)ADDR_SCROLL_DECAY)
#define orig_draw_scene        (*(am2_void_fn)ADDR_DRAW_SCENE)
#define orig_merge_dirty       (*(am2_void_fn)ADDR_MERGE_DIRTY)
#define orig_reset_draw_counts (*(am2_void_fn)ADDR_RESET_DRAW_COUNTS)

/* Save `n` dwords from `src` to `dst`. The original writes the nine stores out
 * one at a time, interleaved; they are independent, so a loop is the same
 * thing. */
static void SavePrev(void *dst, const void *src, int32_t dwords)
{
    int32_t i;
    for (i = 0; i < dwords; i++)
        ((uint32_t *)dst)[i] = ((const uint32_t *)src)[i];
}

void __cdecl ComposeFrame(void)
{
    AM2_Rect src;

    orig_scroll_decay();
    orig_draw_scene();

    if (g_fullRedraw)
        RedrawMapRegion(g_viewRect);
    else
        orig_merge_dirty();

    orig_reset_draw_counts();

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
#define orig_repaint_dirty (*(am2_rect_fn)ADDR_REPAINT_DIRTY_LIST)

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

    orig_repaint_dirty(&common);

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

int mapdraw_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_MERGE_DIRTY, (const void *)ScrollView,
                        "ScrollView", 0);

    rc |= patch_replace(ADDR_COMPOSE_FRAME, (const void *)ComposeFrame,
                        "ComposeFrame", 0);

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
