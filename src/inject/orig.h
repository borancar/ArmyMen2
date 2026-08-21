/* Typed pointers to functions still living inside the original ArmyMen2.exe.
 *
 * Relocations are stripped from the executable, so it can only ever map at its
 * preferred base of 0x400000 and these absolute addresses are stable.
 *
 * IMPORTANT: ArmyMen2.exe statically links the MSVC 6 CRT. Its FILE handles,
 * heap and locale state are entirely separate from the msvcrt.dll that our
 * injected DLL links against. Never pass a game FILE* to our own fread/fclose,
 * or a game pointer to our own free(). Route through the game's own CRT via the
 * declarations below until the owning subsystem is itself reconstructed.
 */

#ifndef AM2_ORIG_H
#define AM2_ORIG_H

#include <stdint.h>
#include <stddef.h>

/* ---- addresses ------------------------------------------------------- */

#define AM2_IMAGE_BASE      0x00400000u

/* Game code */
#define ADDR_CHECK_SAVE_TAG 0x004235D0u  /* BOOL(FILE*,uint32_t,const char*,int32_t) */
/* The mirror, and the reason CheckSaveTag has a tag to check. 46 callers --
 * three times CheckSaveTag's, because a section that is written once may be
 * bracketed by several markers. It is `fwrite(&tag, 4, 1, fp)` and nothing
 * else; the destination-aliasing subtlety CheckSaveTag has does not arise
 * when the value is going out rather than coming in. */
#define ADDR_WRITE_SAVE_TAG 0x00423680u  /* void(FILE*, uint32_t) */
#define ADDR_LOG            0x0045CAA0u  /* void(const char*,...) -- stubbed to `ret` */
#define ADDR_RECT_SET       0x0042E1C0u  /* void(AM2_Rect*,int32,int32,int32,int32) */
#define ADDR_CLAMP          0x0042E180u  /* int32_t(int32_t v, int32_t lo, int32_t hi) */
#define ADDR_POINT_IN_RECT  0x0042E1F0u  /* int32_t(const AM2_Rect*, const AM2_Point*) */

#define ADDR_CLIP_RECT      0x0042E220u  /* int32_t(src, clip, int32*, int32*, out) */

/* Text rendering. */
#define ADDR_DRAW_TEXT      0x00446930u  /* void(x,y,str,font,?,colour) */
/* Runtime font generation: GDI-render a character, then RLE it. */
#define ADDR_ENCODE_GLYPH   0x004464C0u  /* uint32_t(uint8_t*,int32,int32,int32) */
#define ADDR_RENDER_GLYPH   0x004465E0u  /* uint32_t(int32,char,HFONT,AM2_Rle16*,int32) */
#define ADDR_CREATE_GAME_FONT 0x00446450u /* HFONT(const char *face, int32 h, uint16 style) */
/* Named for the use font.cpp makes of it -- it GDI-renders glyphs onto it --
 * but InitDirectDraw shows it is the back buffer proper: the surface taken off
 * the primary with DDSCAPS_BACKBUFFER when fullscreen, and a plain offscreen
 * surface of the same size when windowed, where there is no flipping chain to
 * take one from. It is also what the lock target starts out pointing at. */
#define ADDR_FONT_SURFACE   0x004FE08Cu  /* IDirectDrawSurface *, the back buffer */
/* One record per font, 524 bytes apart -- BuildFont computes the stride as
 * ((f<<6)+f)*2+f then <<2, which is 131 dwords and not the 133 this said
 * before. Within a record: +0 the total encoded size, +4 a uint16 offset for
 * each of the 256 characters, +0x204 the pointer to the encoded glyphs. */
#define ADDR_FONT_STRIDE    524u
#define ADDR_GLYPH_SIZE     0x006598D0u  /* uint32_t, total encoded bytes */
#define ADDR_GLYPH_OFFSETS  0x006598D4u  /* uint16_t[256] */
#define ADDR_FONT_BASES     0x00659AD4u  /* uint8_t *, the encoded glyphs */
#define ADDR_FONT_DESCS     0x004897E8u  /* {const char *face; int32 h; uint16 style}[] */
#define ADDR_BUILD_FONT     0x004466E0u  /* int32_t(int32_t fontIndex) */
#define ADDR_GAME_MALLOC    0x004647F8u  /* the game's own CRT malloc */
#define ADDR_GAME_FREE      0x004646A9u  /* the game's own CRT free */
#define ADDR_SCREEN_CLIP    0x00485310u  /* AM2_Rect -- text and sprites share it */
/* 0x0041CBA0. A clipped vertical line straight into the locked framebuffer.
 * It names itself nowhere, so this is a role name; two callers. One of the 29
 * functions in CLAUDE.md's Lock/Unlock bracket list. */
#define ADDR_DRAW_VLINE     0x0041CBA0u  /* void(x, y0, y1, colour) */
#define ADDR_DRAW_HLINE     0x0041CC40u  /* void(y, x0, x1, colour) */
/* The rectangle outline both of them serve: two vertical edges then two
 * horizontal ones, all four INCLUSIVE of `right` and `bottom` -- which the
 * clipping inside the line drawers treats as exclusive. One caller. */
#define ADDR_DRAW_RECT      0x0041CDC0u  /* void(const AM2_Rect *, colour) */
/* 0x00413610. The one caller of DrawRect: it takes a rectangle stored in the
 * SAME space as ADDR_VIEW_ORIGIN, subtracts that origin to get screen
 * coordinates, and outlines it. A full Lock/Unlock bracket, unlike the two line
 * drawers below it.
 *
 * docs/functions.tsv gives the entry 256 bytes; the function ends at
 * 0x00413684 and another begins at 0x00413690. Merged, like so many here.
 *
 * The rectangle, its enable flag and its colour are touched by exactly three
 * functions -- this one and 0x004137D0 and 0x00413E70, all in the same
 * neighbourhood and none of which names itself. So the role name says what it
 * DOES; whether the box is a drag-selection, a highlight or something else is
 * not established. */
#define ADDR_DRAW_VIEW_RECT     0x00413610u  /* void(void) */
#define ADDR_VIEW_RECT_ON       0x004FCF58u  /* int32_t, gates the draw */
#define ADDR_VIEW_RECT          0x004FCF70u  /* AM2_Rect in view space */
#define ADDR_VIEW_RECT_COLOUR   0x004FE089u  /* uint8_t */

/* ---- DirectDraw ------------------------------------------------------
 *
 * The game does its own software rasterising, but it does it straight into a
 * locked DirectDraw surface. LockSurface (0x0041B9A0) fills a 0x6C-byte
 * DDSURFACEDESC via IDirectDrawSurface::Lock (vtable[25]), retrying through
 * Restore (vtable[27]) on DDERR_SURFACELOST (0x887601C2), and publishes
 * lpSurface and lPitch into the globals below. The sprite dispatcher calls
 * Unlock (vtable[32]) when it is done.
 *
 * So the blitters below are the game's own code and safe to replace. What must
 * NOT be touched is ddraw itself -- these write into surface bits that
 * DirectDraw owns and may move or lose between frames.
 */
/* Putting the finished frame on the screen: a Flip when fullscreen, a BltFast
 * from the back buffer when windowed, because a window has no flipping chain.
 * Gated by 0x004FA030 -- clear it and the game runs with nothing appearing. */
#define ADDR_PRESENT_FRAME  0x0041AC60u  /* void(void) */
/* Run at the top of every frame: any surface that has been lost -- to an
 * alt-tab or a mode change -- is restored before anything draws. */
#define ADDR_RESTORE_LOST   0x0041A8B0u  /* void(void) */
/* Force what is drawn onto the screen now, outside the normal frame rhythm. */
#define ADDR_REFRESH_SCREEN 0x0044D6D0u  /* void(void), 7 call sites */
/* Small DirectDraw wrappers that take their object as an argument, which is why
 * tools/comcalls.py cannot name the interface and why they were invisible in
 * docs/boundary.md until someone went looking. */
#define ADDR_RELEASE_PALETTE   0x0041B6A0u  /* void(void *holder) */
#define ADDR_SET_PALETTE_RANGE 0x0041B720u  /* void(PALETTEENTRY*, first, last) */
#define ADDR_SET_SURF_COLORKEY 0x0041B970u  /* void(surface *, uint8_t key) */
#define PALETTE_HOLDER_OFF     0x800u       /* where the DD palette hangs */

/* Copy a bottom-up 8-bit bitmap into a locked surface, optionally remapping
 * every byte through a 256-entry table. Stays original: pure pixel work with no
 * boundary in it, and shared by four callers.
 *
 * The last argument is a pointer, and every caller points it at a value it then
 * reads again afterwards -- so treat it as in/out and do not cache the value
 * across the call. Whether this routine actually writes through it was not
 * traced; passing the address and re-reading is faithful either way. */
#define ADDR_MAKE_BITMAP     0x0041BE80u  /* int32(src, pixels, dest, remap) */
#define ADDR_ENCODE_BIG      0x0041BBC0u  /* the >= 60000 pixel encoder */
#define ADDR_ENCODE_SMALL    0x0041BD20u
#define ADDR_ACTIVE_PALETTE  0x00477A58u  /* NULL means no remapping */
/* The record MakeBitmap fills in. Not an AM2_Sprite despite the resemblance. */
#define BMP_OFF_SURFACE      0x00u
#define BMP_OFF_WIDTH        0x04u
#define BMP_OFF_HEIGHT       0x08u
#define BMP_OFF_FLAGS        0x14u
#define BMP_OFF_KEY          0x18u   /* in: byte count, out: transparent index */
#define BMP_FLAG_NO_COLORKEY 0x0001u
#define BMP_FLAG_SYSMEM      0x0040u
#define BMP_FLAG_RESERVE10   0x0080u /* clear => reserve the first ten entries */
#define BMP_FLAG_SOFTWARE    0x1000u
#define BMP_SOFTWARE_LIMIT   0xEA60  /* 60000 pixels */
#define BMP_RESERVED_ENTRIES 10
#define ADDR_STR_BMP_NO_VIDMEM 0x00478280u
#define ADDR_STR_BMP_NO_SURF   0x00478304u
#define ADDR_STR_BMP_NO_LOCK   0x004782E4u
#define ADDR_BLIT_BITMAP_IN  0x0041BA90u
#define ADDR_CREATE_BITMAP   0x00423D90u  /* surface *(FILE*, ...) */
#define ADDR_RELOAD_BITMAP   0x00424280u  /* int32(surface*, FILE*, ...) */
#define ADDR_REFRESH_GATE   0x00412DE0u  /* void(int32), stays original */
#define ADDR_REFRESH_DRAW   0x00424BF0u  /* void(void), stays original */
#define ADDR_MAP_CACHE_SURFACE 0x00514E94u /* IDirectDrawSurface *, the painted map */
#define ADDR_PAINT_MAP_TILES   0x0042D580u /* void(const AM2_Rect *tiles) */
#define ADDR_MAP_TILES         0x00514EB8u /* uint16 *, one index per tile */
#define ADDR_MAP_ROW_SHIFT     0x00514DE4u /* int32, log2 of the map's width */
/* The camera doubles as the top-left of the visible-tile rectangle: the four
 * dwords from ADDR_CAMERA_X are used as a RECT to clip against. */
#define ADDR_VISIBLE_TILES     0x00514EA8u /* AM2_Rect, in tiles */
#define MAP_TILE_SIZE          16
#define MAP_SHEET_COLUMNS      0x1F   /* mask; the sheet is 32 tiles wide */
#define ADDR_FREE_MAP_SURFACES 0x0042D390u /* void(void) */
/* The menu's sprite table: 190 AM2_Sprite* laid out as 19 rows of 10, one more
 * slot just past the end, and a surface of its own. */
#define ADDR_FREE_MENU_SPRITES 0x00412F80u /* void(void) */
#define ADDR_MENU_SPRITES      0x004FCAACu /* AM2_Sprite *[190] */
#define ADDR_MENU_SPRITES_END  0x004FCDA4u /* one past; also cleared as a slot */
#define ADDR_MENU_SURFACE      0x004FCDF4u /* IDirectDrawSurface * */
/* The animated menu cursor, and the save-under it needs so the frame beneath
 * it survives. The LAST function in the image with any COM dispatch. */
#define ADDR_DRAW_MENU_CURSOR    0x00412FE0u  /* void(void) */
#define ADDR_MENU_ENABLED        0x004FCEF8u  /* int32_t; nothing drawn while 0 */
/* Where 0x00426F40 accumulates the mouse deltas into an absolute pointer,
 * clamped to the screen. The menu cursor is drawn here. */
#define ADDR_CURSOR_X            0x00485464u  /* int32_t */
#define ADDR_CURSOR_Y            0x00485468u  /* int32_t */
#define ADDR_MENU_ROW            0x004FCAA8u  /* int32_t, row into the sprite grid */
#define ADDR_MENU_ANIM_FRAME     0x004FCDECu  /* int32_t 0..9, -1 stops the cycle */
#define ADDR_MENU_ANIM_NEXT      0x004FCDE8u  /* uint32_t, tick the next frame is due */
#define MENU_ANIM_PERIOD         0xC8u        /* 200 ms */
/* Signed on purpose: the original compares with `jl`, and the frame index
 * can legitimately be -1, which is how a row says it does not animate. */
#define MENU_ANIM_FRAMES         0x0A
/* Non-zero once something has been saved under the cursor. */
#define ADDR_MENU_SAVED_VALID    0x004FCEFCu  /* int32_t */
#define ADDR_MENU_SAVED_RECT     0x004FCA98u  /* AM2_Rect, where it came from */
/* This frame's cursor rectangle and last frame's. */
#define ADDR_MENU_CURSOR_RECT    0x004FCDD8u  /* AM2_Rect */
#define ADDR_MENU_CURSOR_PREV    0x004FCDC8u  /* AM2_Rect */
/* Two optional overlays drawn with the cursor, and the flags and offsets they
 * carry. */
#define ADDR_MENU_OVERLAY_A      0x004FCDA8u  /* AM2_Sprite * */
#define ADDR_MENU_OVERLAY_B      0x004FCDACu  /* AM2_Sprite * */
#define ADDR_MENU_SPRITE_MODE    0x004FCDB0u  /* int32_t, non-zero -> mode 1 */
#define ADDR_MENU_OVERLAY_A_FLD  0x004FCDB4u
#define ADDR_MENU_OVERLAY_B_FLD  0x004FCDB8u
#define ADDR_MENU_CURSOR_DX      0x004FCDBCu  /* int16_t pair */
#define ADDR_MENU_OVERLAY_A_DX   0x004FCDC0u
#define ADDR_MENU_OVERLAY_B_DX   0x004FCDC4u
/* The rectangle the paint object reports, and the DDBLTFX the Blts pass. */
/* The fixed rectangle inside the menu surface that the save-under occupies.
 * It is a RECT and not a DDBLTFX -- it is passed as Blt's source when
 * restoring and as its destination when saving. */
#define ADDR_MENU_SAVE_SLOT      0x00476198u  /* RECT */
#define MENU_ROW_DIRECT          0x13         /* at or above, lock and draw directly */
#define ADDR_TICKS               0x00426CD0u  /* uint32_t(void) */
/* Sprite fields the cursor code reads: two int16 hotspot pairs and the
 * width/height, plus the mode slot DrawSprite consults. */
#define SPR_OFF_W                0x1Cu
#define SPR_OFF_H                0x20u
#define SPR_OFF_HOTX             0x24u   /* int16_t */
#define SPR_OFF_HOTY             0x26u   /* int16_t */
#define SPR_OFF_OVX              0x28u   /* int16_t */
#define SPR_OFF_OVY              0x2Au   /* int16_t */
#define SPR_OFF_MODE             0x34u
/* Holds a POINTER to the map sprite record -- reaching anything in it is two
 * dereferences. In that record: +0x10 the surface, +0x1c and +0x20 its width
 * and height, +8 a flag. PaintMapTiles and RestoreTileSet both read it. */
#define ADDR_MAP_SURFACE    0x00514E90u
/* Tail-called after a map restore -- but named for what it *is*, not for the
 * one call site. It went in as ADDR_ON_MAP_RESTORED; its own error strings say
 * `RestoreTileSet`, and it reloads the tileset from disk. */
#define ADDR_RESTORE_TILESET 0x0042C0E0u /* void(void) */
/* The `.atl` reader's inputs. The name is formatted into "%s.atl"; the path is
 * only probed, and DataPathExists' answer is discarded. */
#define ADDR_TILESET_NAME    0x00511A88u /* char[] */
#define ADDR_TILESET_PATH    0x00511AC8u /* char[] */
/* Non-zero reserves the first ten palette entries, as BMP_FLAG_RESERVE10 does
 * for MakeBitmap -- the same convention reached a different way. */
#define ADDR_TILESET_RESERVE 0x00511CC8u /* int32_t */
/* 0x00422FF0: reads one DIB chunk from an open stream into the 0x428-byte
 * bitmap header MakeBitmap also reads -- ten dwords then a 256-entry palette --
 * and returns the pixels in a buffer the caller frees. */
#define ADDR_READ_DIB_CHUNK  0x00422FF0u
#define ADDR_MSG_TILESET_OPEN 0x00486310u /* "Unable to open tileset" */
#define ADDR_MSG_TILESET_LOCK 0x004862ECu /* "Error on Lock in RestoreTileSet()" */
#define ADDR_MSG_TILESET_LOAD 0x004862D4u /* "Error in loadtileset()" */
#define ADDR_FMT_ATL          0x00486328u /* "%s.atl" */
#define ADDR_PRESENT_ENABLED 0x004FA030u /* int32_t */
#define ADDR_LOCK_SURFACE   0x0041B9A0u  /* int32_t(IDirectDrawSurface*) */
#define ADDR_UNLOCK_SURFACE 0x0041BA40u  /* int32_t(void) */
#define ADDR_SURFACE_LOCKED 0x004FDF80u  /* int32_t; non-zero while a lock is held */
#define ADDR_LOCKED_SURFACE 0x00507128u  /* IDirectDrawSurface *, currently locked */
#define ADDR_PRIMARY_SURFACE 0x00502AD4u /* IDirectDrawSurface *, Restore target */
#define ADDR_SCREEN_PITCH   0x00502AD0u  /* int32_t, DDSURFACEDESC.lPitch */
#define ADDR_FRAMEBUFFER    0x004FE1A8u  /* uint8_t *, DDSURFACEDESC.lpSurface */
/* Applied when the locked surface is the primary one. */
#define ADDR_ORIGIN_DX      0x00485330u  /* int32_t */
#define ADDR_ORIGIN_DY      0x00485334u  /* int32_t */

/* The software RLE blitter family. All four are the same routine differing
 * only in row-offset width and fill policy -- see src/game/blit.c. */
#define ADDR_BLIT_GLYPH     0x0041C710u  /* solid fill,   16-bit offsets */
#define ADDR_BLIT_COPY16    0x0041C2B0u  /* copy source,  16-bit offsets */
#define ADDR_BLIT_COPY32    0x0041C1C0u  /* copy source,  32-bit offsets */
#define ADDR_BLIT_REMAP16   0x0041C3A0u  /* copy via LUT, 16-bit offsets */

/* Sprite drawing. The dispatcher is not reconstructed yet; it fans out to the
 * four blitters above plus 0x0041C480 (656 bytes, unread) and 0x00445EB0,
 * which is not a blitter at all but a fallback chain that calls
 * IDirectDrawSurface::Restore on the sprite's own surface. */
#define ADDR_DRAW_SPRITE         0x00445FF0u  /* void(AM2_Sprite*,x,y,mode) */
#define ADDR_DRAW_SPRITE_CLIPPED 0x00446070u  /* void(spr,x,y,const AM2_Rect*,mode) */
#define ADDR_BLIT_OVERLAY        0x0041C480u  /* __fastcall(x,y,data,AM2_Rect) */
#define ADDR_RESTORE_CHAIN       0x00445EB0u  /* void(AM2_Sprite*) */
/* Sprite lifetime. The registry is a count at 0x006598C0 and a table of
 * AM2_Sprite* at 0x006598C4; the lookup walks it for a matching id. */
#define ADDR_FILL_SOUND_BUFFER   0x0040C440u  /* int32(buf, const void *, uint32) */
#define ADDR_STR_SND_LOCK_FAIL   0x00474E6Cu  /* "Unable to lock sound buffer\n" */
#define ADDR_STR_SND_NO_ARGS     0x00474E44u  /* "Fill sound buffer missing arguments\n" */
#define ADDR_BLIT_CENTRED        0x00445500u  /* thiscall void(this, surface *) */
#define BLIT_SRC_OFF_SURFACE     0x04u   /* the source, inside `this` */
#define BLIT_SRC_OFF_DESC        0x1Cu   /* -> {?, width, height} */
#define ADDR_DRAW_SEQ_BAR        0x004624A0u  /* void(x,y,colour,value,base) */
#define ADDR_SEQ_BAR_BG          0x00502AD9u  /* uint8_t, the unfilled colour */
#define ADDR_STR_SEQ_BLT_FAIL    0x0048CBE8u  /* "Couldn't Blt Seq Pixels\n" */
#define ADDR_RELEASE_SPRITE      0x00445D80u  /* void(AM2_Sprite *) */
#define ADDR_CLEAR_SPRITE        0x00445E40u  /* void(AM2_Sprite *) */
#define ADDR_SPRITE_SLOT_OF      0x00445990u  /* int32(uint32 id); <0 when absent */
#define ADDR_SPRITE_TABLE        0x006598C4u  /* AM2_Sprite ** */
#define ADDR_STR_RELEASE_WRONG   0x00489768u  /* "Error in release: Wrong sprite!\n" */
#define ADDR_STR_RELEASE_MISSING 0x00489740u  /* "Error in release: Sprite not found!\n" */
/* The three ways a sprite's pixels are put back after its surface is restored.
 * All stay original; which one applies is decided in RestoreSpriteSurface. */
#define ADDR_SPRITE_RELOAD_NAMED 0x004456B0u  /* int32(spr, const char *, flags) */
#define ADDR_SPRITE_REBUILD_DF   0x004243B0u  /* int32(spr, flags), when -df is set */
#define ADDR_SPRITE_REBUILD_ALT  0x00445C00u  /* int32(spr, flags), when it is not */
#define ADDR_STR_RESTORE_FAIL_S  0x004897ACu  /* "unable to restore sprite %s.\n" */
#define ADDR_STR_RESTORE_FAIL_X  0x0048978Cu  /* "unable to restore sprite %x.\n" */
#define ADDR_OVERLAY_PALETTE     0x004FE1A4u  /* void *, set before the overlay blit */
#define ADDR_DEFAULT_PALETTE     0x004FE084u  /* void *, used when the sprite has none */

/* Map repainting. World space is 1/256-tile fixed point; the camera origin is
 * scaled by 16 when converting to screen space. */
#define ADDR_SET_DRAW_TARGET     0x0041AC40u  /* void(LPDIRECTDRAWSURFACE) */
#define ADDR_REDRAW_MAP_REGION   0x0041CF90u  /* void(const AM2_Rect*) */
#define ADDR_BLIT_MAP_BACKDROP   0x0042D9B0u  /* void(AM2_Rect by value) */
#define ADDR_DRAW_MAP_TILES      0x0041E440u  /* void(const AM2_Rect*,void*,int32) */
/* A second offscreen surface, the same size again, and NOT the back buffer
 * despite the name -- that is ADDR_FONT_SURFACE. Kept as-is because the name is
 * already spread across mapdraw.cpp; the comment is the correction. */
#define ADDR_BACK_SURFACE        0x00503100u  /* IDirectDrawSurface *, offscreen */
#define ADDR_MAP_DESC            0x00514F20u  /* map descriptor; +4 is a row count */
/* The left and top of the visible-area rectangle -- these are its first two
 * fields, not two loose globals: RedrawMapRegion is called with 0x00514E14
 * itself as its AM2_Rect *, which the trace shows plainly, so the four edges
 * run 0x514E14..0x514E20.
 *
 * Distinct from the camera below, which is in tiles: BlitMapBackdrop subtracts
 * the camera from the SOURCE rectangle and these from the DESTINATION point,
 * and they are not the same offset. Also distinct from ADDR_ORIGIN_DX/DY,
 * which shift for a windowed primary. */
#define ADDR_VIEW_ORIGIN_X       0x00514E14u  /* int32_t */
#define ADDR_VIEW_ORIGIN_Y       0x00514E18u  /* int32_t */
/* Last frame's copies, written by ComposeFrame at the end of every frame and
 * read by the dirty-rectangle merge to find what has scrolled. The listener
 * point is saved the same way and in the same block. */
#define ADDR_LISTENER_POS_PREV   0x00514E10u  /* AM2_Point */
#define ADDR_VIEW_RECT_PREV      0x00514E24u  /* AM2_Rect */
#define ADDR_SECOND_RECT         0x00514E34u  /* AM2_Rect, saved alongside */
#define ADDR_SECOND_RECT_PREV    0x00514E44u  /* AM2_Rect */
/* Where the finished frame lands on the back buffer, and the region of the
 * offscreen surface it comes from -- the same four numbers used both ways. */
#define ADDR_BLIT_RECT           0x00485320u  /* AM2_Rect */
/* Set by three places that invalidate the whole view, cleared once the frame
 * that honoured it has been composed. */
#define ADDR_FULL_REDRAW         0x00512460u  /* int32_t */
#define ADDR_COMPOSE_FRAME       0x0042DA30u  /* void(void) */
/* ComposeFrame's callees. 0x0042B420 decays a scroll counter and 0x0041DCE0
 * clears three word counters; both stay original.
 *
 * 0x0042D6D0 went in as ADDR_DRAW_SCENE, guessed from where ComposeFrame calls
 * it. Reading the body says otherwise: it recentres the camera on the view,
 * clamps it to the map, scrolls the CACHE surface onto itself by the tile
 * delta and repaints the strips that exposed. Same shape as ScrollView one
 * level down, in tiles rather than pixels. Renamed, and that is the fourth
 * call-site name this project has had to correct. */
#define ADDR_SCROLL_DECAY        0x0042B420u  /* void(void) */
#define ADDR_SCROLL_MAP_CACHE    0x0042D6D0u  /* void(void) */
/* The view's size in tiles -- camera right and bottom are these plus the
 * camera origin, which is how ADDR_VISIBLE_TILES's last two fields are kept. */
#define ADDR_VIEW_TILES_W        0x00514EA0u  /* int32_t */
#define ADDR_VIEW_TILES_H        0x00514EA4u  /* int32_t */
/* The map's size in tiles; the camera is clamped to one less than each. */
#define ADDR_MAP_TILES_W         0x00514DDCu  /* int32_t */
#define ADDR_MAP_TILES_H         0x00514DE0u  /* int32_t */
#define ADDR_MERGE_DIRTY         0x0041D060u  /* void(void) */
/* The rectangle the view is clipped against before anything is compared --
 * the map's extent on screen. */
#define ADDR_MAP_BOUNDS          0x00514DE8u  /* AM2_Rect */
/* Walks the registered dirty rectangles at 0x00508AC4 and repaints the ones
 * meeting the given region. Its only import is IntersectRect, so it is game
 * logic by the coverage rules and stays original. */
#define ADDR_REPAINT_DIRTY_LIST  0x0041D000u  /* void(const AM2_Rect *) */
#define ADDR_RESET_DRAW_COUNTS   0x0041DCE0u  /* void(void) */
#define ADDR_CAMERA_X            0x00514EA8u  /* int32_t */
#define ADDR_CAMERA_Y            0x00514EACu  /* int32_t */

/* Display palette calibration. Paints a ramp of every palette index onto the
 * primary surface and reads it back with GDI to learn what actually displays.
 * The colour matcher stays original -- it is pure arithmetic. */
#define ADDR_CALIBRATE_PALETTE   0x0041AFC0u  /* void(uint32_t *palette[512]) */
/* Scan the palette from `from` for the entry closest to a colour, by the
 * metric at ADDR_COLOUR_DISTANCE. Went in twice, as ADDR_NEAREST_PAL_INDEX too. */
#define ADDR_NEAREST_PAL_INDEX   0x0041B7C0u  /* uint8_t(const uint32_t*,uint32_t,uint32_t) */
#define ADDR_COLOUR_DISTANCE     0x0041B760u  /* int32_t(const uint32_t *a,
                                               * const uint32_t *b) */
#define AM2_COLOUR_DIST_MAX      0x2FFFD      /* the sentinel it starts from */

/* The GDI half of the palette. The game is 8-bit, so what it can actually show
 * is negotiated with Windows rather than chosen. */
#define ADDR_REALIZE_PALETTE     0x0041AF00u  /* void(const uint32_t *palette) */
/* SetGamePalette -- creates the DirectDraw palette and builds every remap
 * table the software blitters use. 0x0041B132 is the image's ONLY
 * CreatePalette, so until this was reconstructed the display palette was the
 * one DirectX object the port did not create. */
#define ADDR_SET_GAME_PALETTE    0x0041B0E0u  /* void(uint8_t *palette) */
#define ADDR_PALETTE_LOADED      0x00423C50u  /* void(void), run afterwards */
/* The fixed 256-entry table handed to CreatePalette when windowed, where the
 * desktop owns the real palette and the game may not set it. */
#define ADDR_GDI_PALETTE         0x00477E6Cu  /* PALETTEENTRY[256] */
/* Where the palette is copied wholesale once it is installed: 0x201 dwords,
 * which is the 256 entries plus the DirectDraw palette pointer after them. */
#define ADDR_PALETTE_COPY        0x005022C8u
/* Remap tables, one byte per palette index, all rebuilt by SetGamePalette. */
#define ADDR_REMAP_IDENTITY      0x004FD764u  /* uint8_t *, index -> itself */
#define ADDR_REMAP_DARK          0x004FE084u  /* uint8_t *, 70% brightness */
#define ADDR_REMAP_BRIGHT        0x00507230u  /* uint8_t *, +0x80 or 70% */
#define ADDR_REMAP_TINT          0x0047826Cu  /* uint8_t *, two colours by parity */
/* Four more, at 40/50/60/85% brightness, whose 256-byte blocks sit back to
 * back from 0x00502CEC. */
#define ADDR_REMAP_SHADES        0x004FE2B0u  /* uint8_t *[4] */
#define ADDR_REMAP_SHADE_STORE   0x00502CECu
/* Palette indices of the colours the engine asks for by name. */
#define ADDR_COLOUR_TABLE_BASE   0x004FE084u
#define ADDR_SNAPSHOT_PALETTE    0x00445170u  /* void(void) */
/* A LOGPALETTE living in .data with palVersion and palNumEntries already set
 * to 0x300 and 256; only the 256 entries after them are ever written. */
#define ADDR_LOGPALETTE          0x00477A60u
#define ADDR_LOGPALETTE_ENTRIES  0x00477A64u  /* PALETTEENTRY[256] */
#define ADDR_SYSTEM_PALETTE      0x006564A0u  /* PALETTEENTRY[256], read back from GDI */

/* ---- DirectPlay -------------------------------------------------------
 *
 * The last outward channel, and the least visible one. The game imports no
 * networking library at all -- no ws2_32, no wsock32, no dplayx, and not even
 * the strings -- because its multiplayer transport is DirectPlay obtained
 * through COM. These two CoCreateInstance sites are the whole of it.
 *
 * Reconstructed in src/game/win32/dplay.cpp. The GUIDs are the game's own copies.
 */
#define ADDR_COMM_CREATE_DPLAY   0x0040DD20u  /* thiscall int32(this, void *conn) */
/* Comm teardown: destroy the four mutex-guarded message lists, wake the packet
 * thread, wait for it and close the handles. */
#define ADDR_COMM_SHUTDOWN       0x004020A0u  /* void(void) */
#define ADDR_MSG_LIST_A          0x0048D8E8u
#define ADDR_MSG_LIST_B          0x004F48C8u
#define ADDR_MSG_LIST_C          0x0048D8D8u
#define ADDR_MSG_LIST_D          0x004F8780u
#define ADDR_COMM_EVENT          0x0048D8F8u  /* HANDLE, signalled to stop the thread */
#define ADDR_COMM_EVENT_2        0x0048D8FCu  /* HANDLE */
#define ADDR_PACKET_THREAD       0x004F48D8u  /* HANDLE */
#define ADDR_CREATE_LOBBY        0x0040DDD0u  /* int32 __stdcall(LPDIRECTPLAYLOBBY3A *) */
#define ADDR_CLSID_DIRECTPLAY    0x0046F6D8u  /* CLSID_DirectPlay */
#define ADDR_IID_DIRECTPLAY4A    0x0046F6C8u  /* IID_IDirectPlay4A */
#define ADDR_CLSID_DPLAY_LOBBY   0x0046F778u  /* CLSID_DirectPlayLobby */
#define ADDR_IID_DPLAY_LOBBY3A   0x0046F768u  /* IID_IDirectPlayLobby3A */
/* Both thiscall on the comm object, both taking nothing but `this`. */
#define ADDR_COMM_DROP_DPLAY     0x0040EA40u  /* thiscall int32(this) */
/* The DirectPlay lobby launch, reached when another application starts the
 * game through DirectPlay rather than the user starting it. */
#define ADDR_COMM_LOBBY_START    0x0040ED10u  /* thiscall int32(this) */
#define ADDR_READ_MP_MAPS        0x0043ECC0u  /* void(void), stays original */
#define ADDR_COMM_CREATE_PLAYER  0x0040DE10u  /* thiscall int32(this,name,evt,data,len) */
#define ADDR_COMM_REGISTER_SELF  0x004027F0u  /* void(DPID), stays original */
#define ADDR_DEFAULT_PLAYER_EVT  0x004F48C0u  /* HANDLE, used when none is given */
#define COMM_OFF_PLAYER_MADE     0x3E4u
#define COMM_OFF_JOINED          0x3DCu
#define COMM_SLOT_OFF_TAKEN      0x050u   /* the field StartSelectedGame sets */
#define ADDR_COMM_MARK_LOBBIED   0x0040F130u  /* void(void); sets comm+0x404 */
#define ADDR_ON_LOBBY_SLAVE      0x00410F70u  /* void(void), stays original */
#define COMM_OFF_LOBBY_BUF       0x3F0u   /* DPLCONNECTION, 0x800 bytes */
#define COMM_OFF_IS_HOST         0x3D8u   /* from DPCAPS_ISHOST */
#define COMM_OFF_SESSION_DESC    0x3E8u   /* the fetched DPSESSIONDESC2 */
#define COMM_OFF_LOBBIED         0x3F8u
#define COMM_OFF_LOBBY_STARTING  0x3FCu
#define LOBBY_CONN_BUF_SIZE      0x800u
#define ADDR_STR_LOBBY_START     0x004756A4u
#define ADDR_STR_LOBBY_NOMEM     0x0047566Cu
#define ADDR_STR_LOBBY_GCS_FAIL  0x0047563Cu
#define ADDR_STR_LOBBY_E_SMALL   0x00475624u
#define ADDR_STR_LOBBY_E_IFACE   0x00475608u
#define ADDR_STR_LOBBY_E_OBJECT  0x004755F0u
#define ADDR_STR_LOBBY_E_PARAMS  0x004755D8u
#define ADDR_STR_LOBBY_E_MEMORY  0x004755C4u
#define ADDR_STR_LOBBY_CONNECT   0x00475548u
#define ADDR_STR_LOBBY_CONNRET   0x0047552Cu
#define ADDR_STR_LOBBY_AS_HOST   0x00475504u
#define ADDR_STR_LOBBY_AS_SLAVE  0x004754DCu
#define COMM_OFF_LOBBY           0x3F4u   /* IDirectPlayLobby3A; the store at
                                           * 0x0040ED3C names it */
#define COMM_OFF_SEND_BUF        0x3E8u   /* game heap */
#define COMM_OFF_RECV_BUF        0x3F0u   /* game heap */
#define ADDR_REMOVE_PLAYER       0x004029B0u  /* void(uint32 id), 7 callers */
/* The pause mask and its pair of accessors, and all three name themselves:
 * 0x004267C0 logs "PauseGame: %x (set: %x)" and 0x00426800 logs
 * "UnPauseGame: %x (reset: %x)". They went in as event flags, which is what
 * they look like where the frame chain tests them -- but the tests read "is
 * the game paused", and each bit is a reason it is. */
#define ADDR_PAUSE_GAME          0x004267C0u  /* void(uint32 bits); ORs in */
#define ADDR_UNPAUSE_GAME        0x00426800u  /* void(uint32 bits); ANDs out */
#define ADDR_STR_PAUSE_GAME      0x00485250u  /* "PauseGame: %x (set: %x)\n" */
#define ADDR_STR_UNPAUSE_GAME    0x0048526Cu  /* "UnPauseGame: %x (reset: %x)\n" */
#define COMM_DROP_EVENT_MASK     0x1E78F0u    /* what the teardown clears */
#define ADDR_STR_RELEASING_COMM  0x00475434u  /* "Releasing Comm Connection \n" */
#define ADDR_COMM_UNKNOWN_4F48E0 0x004F48E0u  /* cleared with the connection */
#define ADDR_COMM_CONNECTED      0x0040E660u  /* thiscall int32(this) */
#define COMM_OFF_CAPS            0x42Cu   /* DPCAPS, filled by GetCaps */
#define COMM_OFF_BUFFER_MAX      0x410u   /* set to 0x400 by CommConstruct */
#define COMM_OFF_BUFFER_DEFAULT  0x428u   /* set to 0x3E4 by CommConstruct */
#define ADDR_STR_CAPS_HEAD       0x00475400u
#define ADDR_STR_CAPS_PACKET     0x004753E8u
#define ADDR_STR_CAPS_HEADER     0x004753D0u
#define ADDR_STR_CAPS_LATENCY    0x004753B8u
#define ADDR_STR_CAPS_TIMEOUT    0x004753A0u
#define ADDR_STR_CAPS_GUAR_YES   0x0047537Cu
#define ADDR_STR_CAPS_GUAR_NO    0x00475354u
#define ADDR_STR_CAPS_BUFFERS    0x0047531Cu
#define COMM_OFF_DPLAY           0x3ECu       /* IDirectPlay4A * inside the comm object */
/* Thin wrappers over the IDirectPlay4A the comm object holds. All three answer
 * 1 for success, and all three do nothing at all when there is no session. */
/* The comm object itself: a single global built by a C++ constructor that the
 * CRT runs before main, with the matching destructor handed to atexit. Both
 * are thiscall on the object at ADDR_COMM_OBJECT, and between them they hold
 * the game's ENTIRE registry surface -- one RegCreateKeyExA and one
 * RegCloseKey, and there is no third registry call anywhere in the image. */
#define ADDR_COMM_GLOBAL         0x004FA480u  /* the object itself; ADDR_COMM_OBJECT points at it */
#define ADDR_COMM_CONSTRUCT      0x0040DB80u  /* thiscall void *(this) */
#define ADDR_COMM_DESTRUCT       0x0040DCC0u  /* thiscall void(this) */
/* Called by the constructor, all three left original. */
/* Brings the packet subsystem up: four message lists, 400 buffers, two events
 * and the packet thread. It went in as "mirrors CommShutdown", guessed from the
 * call site; its own error string says "Error launching packet thread". */
#define ADDR_START_PACKET_THREAD 0x004021A0u  /* int32_t(void) */
/* Two 120-entry state arrays on the comm object, at +0x420 and +0x600, six
 * setters differing only in array and value. See src/game/msgslot.h -- nothing
 * in the image READS either array. */
#define ADDR_MSGSLOT_A1          0x004032C0u  /* void(comm, seq) */
#define ADDR_MSGSLOT_A0          0x004032F0u
#define ADDR_MSGSLOT_A2          0x00403320u
#define ADDR_MSGSLOT_B1          0x00403350u
#define ADDR_MSGSLOT_B0          0x00403380u
#define ADDR_MSGSLOT_B2          0x004033B0u
#define ADDR_MSG_FIELD_12        0x00401040u  /* uint32_t(const void *msg) */
/* Comm object bookkeeping, all on the same record. The 32-dword ring at +0x3A0
 * is written by one and averaged by the other. */
#define ADDR_RING_PUSH_32      0x00402E50u  /* void(comm, uint32_t sample) */
#define ADDR_COMM_REMOVE_KEYED 0x00402DB0u  /* void(comm, uint32_t key) */
#define ADDR_COMM_MEAN_32        0x00402E90u  /* int32_t(const void *comm) */
/* Bit 0 and bit 1 of the word at an object's +0. The tests return the masked
 * value, 1 or 2, not a boolean -- see src/game/objflag.h. */
#define ADDR_OBJ_FLAG_SET0       0x0040A010u  /* void(void *obj) */
#define ADDR_OBJ_FLAG_CLEAR0     0x0040A020u
#define ADDR_OBJ_FLAG_BIT0       0x0040A030u  /* uint32_t(const void *obj) */
#define ADDR_OBJ_FLAG_BIT1       0x0040A040u
#define ADDR_MSG_LIST_INIT       0x00401000u  /* int32_t(void *list) */
#define ADDR_MSG_LIST_ADD        0x00401050u  /* void(void *list, void *node) */
#define ADDR_EVENT_CLOSE         0x00402170u  /* void(void *holder) */
/* The four lists, in the order they are created. */
#define ADDR_MSG_LIST_POOL       0x0048D8E8u  /* the free-buffer pool */
#define ADDR_MSG_LIST_B          0x004F48C8u
#define ADDR_MSG_LIST_C          0x0048D8D8u
#define ADDR_MSG_LIST_D          0x004F8780u
/* 400 records of 0x28 bytes, each pointing at 0x400 bytes of buffer. */
#define ADDR_PACKET_RECORDS      0x004F48F8u
#define ADDR_PACKET_BUFFERS      0x0048D978u
#define ADDR_PACKET_BUFFERS_END  0x004F1978u
#define PACKET_RECORD_STRIDE     0x28u
#define PACKET_REC_OFF_SIZE      0x10u
#define PACKET_REC_OFF_DATA      0x20u
#define PACKET_BUFFER_BYTES      0x400u
/* Two auto-reset events, the second kept in two places. */
#define ADDR_PACKET_EVENT_A      0x0048D8F8u
#define ADDR_PACKET_EVENT_B      0x0048D8FCu
#define ADDR_PACKET_EVENT_B2     0x004F48C0u
#define ADDR_PACKET_STATE        0x004F877Cu  /* int32_t, set to 2 */
#define ADDR_PACKET_SLOT_RESET   0x00402750u  /* void(int32_t), six times */
#define ADDR_PACKET_THREAD_PROC  0x00401F00u  /* the thread, stays original */
#define ADDR_PACKET_THREAD_ID    0x004F8B90u  /* DWORD */
#define ADDR_PACKET_THREAD       0x004F48D8u  /* HANDLE */
#define ADDR_STR_THREAD_FAILED   0x0047384Cu  /* "Error launching packet thread" */
#define ADDR_GAME_SRAND          0x00464416u  /* void(uint32_t) */
#define ADDR_GAME_RAND           0x00464420u  /* int32_t(void) */
#define ADDR_COMM_INIT_DEFAULTS  0x0040FD40u  /* void(void); fills a global table */
#define ADDR_COMM_RESET_STATE    0x0040F380u  /* thiscall void(this) */

/* The six window messages WndProc used to hand back to the original. They are
 * comm traffic -- players joining and leaving, the host migrating, the session
 * ending -- reached through PostMessage from the DirectPlay callbacks.
 *
 * Names carry only what the BODY shows. Four of these name themselves in their
 * own format strings and are named accordingly; the rest say what was observed
 * and nothing more, because naming a function from the one call site that
 * happens to be in front of you is the mistake this file has already recorded
 * three times. */
#define ADDR_COMM_DRAIN_MSGS     0x00402690u  /* void(void), walks the msg list */
#define ADDR_COMM_NO_BUFFERS     0x00403280u  /* void(void), "COMM ERROR: NO BUFFERS" */
#define ADDR_COMM_PLAYER_SLOT    0x0040F320u  /* thiscall int32(this,id), 16 bytes */
#define ADDR_COMM_FIND_PLAYER    0x0040F330u  /* thiscall int32(this,id), -1 if absent */
#define ADDR_COMM_REMOVE_PLAYER  0x0040F640u  /* thiscall int32(this,id) --
                                               * "Remove Player numPlayers now = %d" */
#define ADDR_COMM_PLAYER_LEFT    0x0040F790u  /* thiscall int32(this,id), 272 bytes */
#define ADDR_COMM_END_SETUP      0x00410CE0u  /* void(void) -- "Sending EndSetupMessage" */
#define ADDR_COMM_SEND_PLAYERS   0x00411270u  /* void(int32) -- "SendPlayerMsg for %d" */
#define ADDR_COMM_SESSION_OVER   0x0040FB70u  /* thiscall void(this), tail-calls 0x40FAA0 */
#define ADDR_SHOW_MP_RESULT      0x00426A90u  /* void(int32) -- loads bitmaps/mpwon.bmp */
#define ADDR_SET_AI_CONTROL      0x004295C0u  /* void(int32), sets 0x00476FB0 */
#define ADDR_LOBBY_RESET         0x00413480u  /* void(void), 320 bytes */
#define ADDR_HUD_MESSAGE         0x004144A0u  /* void(const char *, int32), 384 bytes */
#define ADDR_MENU_MESSAGE        0x00431C30u  /* void(const char *, int32, int32) */
#define ADDR_CHAT_APPEND         0x00411E90u  /* void(const char *, int32), 128 bytes */
#define ADDR_SPRITE_DROP_NAMED   0x00457820u  /* void(int32, const void *), 128 bytes */

#define ADDR_AI_CONTROLLED       0x00476FB0u  /* int32_t, set by ADDR_SET_AI_CONTROL */
#define ADDR_PAUSE_FLAGS         0x005122FCu  /* uint32_t, one bit per reason */
/* Raised by 0x00411000 and lowered by the 0x046E handler, and read from 21
 * places -- the lobby, the overlay, TakeMenuRequest and the mission code. The
 * shape of a "this is a network game" flag; named for what is observed rather
 * than for the one call site that happened to be in front of me. */
#define ADDR_NET_GAME            0x00511DD4u  /* int32_t */
#define ADDR_GAME_OVER_FLAGS     0x00515FD8u  /* uint32_t, bit 18 selects the AI mode */
#define ADDR_HUD_MESSAGE_COLOUR  0x00507234u  /* uint8_t, colour for ADDR_HUD_MESSAGE */
#define ADDR_MP_LEAVE_SPRITE     0x0045A030u  /* const void *, passed to 0x00457820 */
#define ADDR_STR_ALLRIGHT_WAV    0x00474194u  /* "AllRight.wav" */
#define ADDR_STR_HOST_NOW        0x00474178u  /* "Player %s is now the host." */
#define ADDR_STR_LEFT_AI         0x004741ECu  /* "Player %s has left the game - now AI" */
#define ADDR_STR_LEFT_GAME       0x004741CCu  /* "Player %s has left the game." */
#define ADDR_STR_SET_SESSION_FAIL 0x004741A4u /* "Set Session Failed to reopen Session" */
#define ADDR_STR_DESTROYPLAYER   0x00474220u  /* "DESTROYPLAYER Win Message ..." */

/* The player records live at COMM_OFF_PLAYERS, 112 bytes apart, and the name is
 * the first field -- which is how both "Player %s ..." messages are built. */
#define COMM_OFF_PLAYERS         0x218u
#define COMM_PLAYER_STRIDE       112u
#define COMM_OFF_VERBOSE         0x418u   /* non-zero: log every DESTROYPLAYER */
/* Where the registry key and the application GUID live in the image. Neither is
 * restated here -- the game's own copies are used, as with the DirectPlay
 * CLSIDs. The GUID is {2777D2A2-89D1-11D2-A387-00C04F79DCEB}. */
/* The copy-protection dialog's two strings. Every one of the five CD checks
 * uses this pair; see docs/binarypatches.md, and note that all five checks are
 * patched to unconditional in this build so none of them can appear. */
#define ADDR_CD_REQUIRED_TEXT    0x00475578u  /* "The ARMYMEN2 CD must be..." */
#define ADDR_CD_REQUIRED_CAPTION 0x004755B4u  /* "Copy Protection" */
#define ADDR_REGISTRY_KEY        0x004751E8u  /* "Software\\The 3DO Company\\Army Men II" */
#define ADDR_APP_GUID            0x0046F8A8u  /* GUID, the DirectPlay application id */
#define ADDR_COMM_CLOSE          0x0040DCF0u  /* int32_t(void) */
#define ADDR_COMM_INIT_CONN      0x0040DD90u  /* thiscall int32(this, conn) */
#define ADDR_COMM_SET_SESSION    0x0040E630u  /* thiscall int32(this, desc, flags) */
#define ADDR_COMM_GET_SESSION    0x0040E5A0u  /* thiscall int32(this) */
#define COMM_OFF_SESSION_BUF     0x3E8u       /* the description, on the game heap */

/* ---- streaming audio ---------------------------------------------------
 *
 * A looping DirectSound buffer kept fed by a multimedia timer, reading through
 * src/game/win32/wavefile.cpp. Reconstructed in src/game/win32/audio.cpp.
 */
#define ADDR_STOP_AUDIO_STREAM   0x0040D5D0u  /* void(void) */
#define ADDR_START_AUDIO_STREAM  0x0040D680u  /* void(void *track, int32) */
#define ADDR_AUDIO_ENABLED       0x004FA468u  /* int32_t; nothing runs while clear */
#define ADDR_AUDIO_BUFFER        0x004FA404u  /* IDirectSoundBuffer *, the stream */
#define ADDR_AUDIO_BUFFER_2      0x004FA440u  /* IDirectSoundBuffer *, the one played */
#define ADDR_STOP_ALL_SOUNDS     0x0040D730u  /* void(void) */
#define ADDR_FREE_SOUND          0x0040C6E0u  /* void(AM2_Sound *) */
/* 56 fixed slots of 16 bytes, then 17 pointers to allocated sounds. */
#define ADDR_SOUND_SLOTS         0x004FA040u
#define ADDR_SOUND_SLOTS_END     0x004FA3C0u
#define ADDR_SOUND_DYNAMIC       0x004FA3C0u  /* == SLOTS_END; the tables abut */
/* Inclusive: the original's loop is `cmp edi,0x4FA400 / jle`, so the entry AT
 * this address is processed too -- 17 pointers, not 16. */
#define ADDR_SOUND_DYNAMIC_LAST  0x004FA400u
/* Bulk operations over those two tables. */
#define ADDR_FREE_DYN_SOUNDS     0x0040B800u  /* void(void) */
#define ADDR_UPDATE_3D_AUDIO     0x0040BCF0u  /* void(void) */
#define ADDR_STOP_NAMED_SOUND    0x0040B860u  /* void(const char *, int32) */
#define SOUND_DYNAMIC_MAX_INDEX  0x10         /* inclusive; 17 slots */

/* The audio save section. Its tag is the only one outside the 0x0666xxxx
 * family. The saver stores, per dynamic slot, exactly the arguments it would
 * take to call PlayDynamicSound again -- looping, position, priority, owner --
 * behind a length-prefixed name, or a bare zero length if the slot is not
 * fully populated.
 *
 * Its loop bound is EXCLUSIVE: `cmp ebp,0x4FA400 / jl`, so it covers 16 slots.
 * FreeDynamicSounds uses `jle` over the same table and covers 17. The two
 * really do disagree; reproduce each as written rather than making them
 * agree. */
#define ADDR_SAVE_AUDIO_SECTION  0x0040BDF0u  /* int32_t(FILE *) */
#define ADDR_LOAD_AUDIO_SECTION  0x0040BF00u  /* int32_t(FILE *) */
#define AM2_SAVETAG_AUDIO        0x01326413u
#define SOUND_DYNAMIC_SAVED      16           /* exclusive bound, see above */
#define ADDR_STR_AUDIO_CPP      0x00474D7Cu  /* "C:\\ArmyMen2\\source\\audio.cpp" */
#define ADDR_LISTENER_POS        0x00514E0Cu  /* AM2_Point, the ear */
#define ADDR_DEFAULT_SOUND_POS   0x005125A0u  /* AM2_Point, used when a sound
                                               * has neither owner nor place */
#define ADDR_VOLUME_AT_ZERO      0x00512318u  /* int32_t, volume at no distance */
#define SOUND_REC_OFF_POS        0x10u   /* AM2_Point */
#define SOUND_REC_OFF_OWNER      0x14u   /* uid; the object making the sound */
#define SOUND_REC_OFF_ACTIVE     0x18u
#define OBJ_OFF_POS              0x12u   /* AM2_Point inside a game object */
#define SOUND_3D_CUTOFF          0x320   /* silence beyond this */
#define SOUND_3D_FALLOFF         3       /* volume lost per unit */
#define ADDR_INIT_WAVE_SOUNDS    0x0040C710u  /* int32(void) */
#define ADDR_LOAD_WAVE_SOUND     0x0040C530u  /* int32(void **slot, ds, name) */
#define ADDR_READ_WAVE_FILE      0x0040C340u  /* int32(0,name,&fmt,&data,&len,&raw) */
#define SOUND_RECORD_SIZE        0x20u    /* what a slot points at */
#define SOUND_REC_OFF_BUFFER     0x00u    /* IDirectSoundBuffer * */
#define SOUND_REC_OFF_NAME       0x04u    /* strdup of the wave's name */
#define SOUND_REC_OFF_STATE      0x08u
#define ADDR_STR_WAVE_NOMEM_DATA 0x00474D44u
#define ADDR_STR_WAVE_NOMEM_NAME 0x00474D18u
#define ADDR_STR_WAVE_NOLOAD     0x00474CF8u
#define ADDR_STR_WAVE_NOBUFFER   0x00474CD0u
#define ADDR_STR_WAVE_NOFILL     0x00474CA4u
#define ADDR_WAVE_NAMES          0x00474360u  /* const char *[32] */
#define ADDR_WAVE_NAMES_END      0x00474440u
#define ADDR_WAVE_DIR            0x004852CCu  /* const char *, probed first */
#define ADDR_STR_WAVE_INIT_FAIL  0x00474E8Cu  /* "Unable to initialize wave %d\n" */
#define SOUND_SLOT_STRIDE        0x10u
#define SOUND_SLOT_OFF_BUFFER    0x00u   /* IDirectSoundBuffer * */
#define SOUND_SLOT_OFF_BYTES     0x04u   /* DSBCAPS.dwBufferBytes */
#define SOUND_SLOT_OFF_VOLUME    0x08u   /* what it was last set to */
#define SOUND_SLOT_OFF_STARTED   0x0Cu   /* GetTickCount when it last began */
#define SOUND_FIXED_SLOTS        0x38    /* 56 */
#define ADDR_PLAY_SOUND_AT       0x0040C040u /* void(idx,flags,?,x,y) */
#define ADDR_PLAY_DYNAMIC_SOUND  0x0040B8F0u /* void(name,loop,?,x,y,slot,pri,owner) */
#define ADDR_VOS_DIR             0x00474D70u /* "audio\\vos" */
#define ADDR_VOLUME_VOICE        0x00512320u /* used for slots 0 and 16 */
#define SOUND_REC_OFF_OWNER_DS   0x08u   /* the IDirectSound it was made from */
#define SOUND_REC_OFF_PRIORITY   0x0Cu
#define SOUND_REC_OFF_LOOPING    0x1Cu
#define SOUND_VOICE_SLOT_HI      0x10    /* slot 16, like slot 0, is a voice */
#define ADDR_POINTS_EQUAL        0x0042E140u /* int32(const AM2_Point*, const AM2_Point*) */
#define ADDR_LOOKUP_OWNER_OBJ    0x00457750u /* void *(uint32 owner) */
#define SOUND_DYN_OFF_BUFFER     0x00u
#define SOUND_DYN_OFF_DATA       0x04u
#define SOUND_SLOT_STRIDE        0x10u
#define ADDR_RELEASE_SOUND_BUFS  0x0040C7D0u  /* void(void), 8 call sites */
#define ADDR_INIT_DIRECTSOUND    0x0040C800u  /* int32_t(void); 1 on success */
#define ADDR_SET_STREAM_VOLUME   0x0040CE90u  /* void(int32 pan) */
#define ADDR_DIRECTSOUNDCREATE   0x00463390u  /* jmp [0x0046F01C] */
#define ADDR_DSOUND              0x004FA46Cu  /* IDirectSound * (same as BUF_C) */
#define ADDR_DS_PRIMARY          0x004FA470u  /* the primary sound buffer */
#define ADDR_DS_LISTENER         0x004FA474u  /* IDirectSound3DListener * */
#define ADDR_IID_DS3D_LISTENER   0x0046F3E8u
#define ADDR_STREAM_VOLUME       0x0051231Cu  /* int32_t, the wanted volume */
#define ADDR_DSOUND_BUF_A        0x004FA470u
#define ADDR_DSOUND_BUF_B        0x004FA474u
#define ADDR_DSOUND_BUF_C        0x004FA46Cu
#define ADDR_AUDIO_TIMER_ID      0x004FA408u
#define ADDR_AUDIO_TIMER_RUN     0x004FA464u
#define ADDR_AUDIO_PERIOD        0x004FA448u  /* divided by 4 for timeBeginPeriod */
#define ADDR_AUDIO_IN_CALLBACK   0x004FA478u  /* set while the refill is running */
/* StartAudioStream's second argument, and RefillAudioBuffer shows what it is:
 * non-zero means rewind and keep going at the end of the file. */
#define ADDR_AUDIO_LOOPING       0x004FA45Cu
#define ADDR_REFILL_AUDIO        0x0040CD20u  /* void(void) */
/* The streaming buffer's size in bytes. It went in as ADDR_AUDIO_REFILL_BYTES
 * because RefillAudioBuffer hands it to Lock as the byte count -- which it does
 * only because that one fills the whole buffer in a single go. AudioTimerProc
 * uses it as the wrap modulus and as "how much to rewrite after a restore",
 * which is what it actually is. Another name taken from one call site. */
#define ADDR_AUDIO_BUFFER_SIZE  0x004FA444u  /* uint32_t */
#define ADDR_AUDIO_READ_FAILED   0x004FA458u
#define ADDR_AUDIO_AT_END        0x004FA460u
#define ADDR_AUDIO_VALID_BYTES   0x004FA454u  /* good data before the silence */
#define ADDR_AUDIO_DATA_CHUNK    0x004FA418u  /* MMCKINFO, the `data` chunk */
#define ADDR_AUDIO_RIFF_CHUNK    0x004FA42Cu  /* MMCKINFO, the RIFF */
#define ADDR_AUDIO_CURSOR_A      0x004FA450u  /* both cleared after a refill */
#define ADDR_AUDIO_CURSOR_B      0x004FA44Cu
#define ADDR_AUDIO_HMMIO         0x004FA414u  /* HMMIO, closed by WaveCloseReadFile */
#define ADDR_AUDIO_WAVEFORMAT    0x004FA410u  /* WAVEFORMATEX * */
/* The streaming refill callback. RECONSTRUCTED as AudioTimerProc in
 * src/game/win32/audio.cpp, but registered rather than patched -- StartAudioStream's
 * timeSetEvent is the only reference to this address in the image, and that
 * call is ours, so a detour here would be jumped to by nobody. Kept as an
 * address because the harness fingerprints it and because a probe may want to
 * hand the original back to timeSetEvent. */
#define ADDR_AUDIO_TIMER_PROC    0x0040D020u  /* LPTIMECALLBACK */
/* Opens the .WAV and creates the streaming buffer -- reconstructed as
 * OpenAudioStream. It went in as ADDR_AUDIO_PREPARE, from the one call site
 * in StartAudioStream, before anyone read the body. */
#define ADDR_OPEN_AUDIO_STREAM   0x0040CED0u  /* int32_t(const char *) */
/* Prefixes the install directory at 0x0051235C onto a relative path and answers
 * whether it is there. 82 callers and nothing audio-specific about it -- it was
 * ADDR_AUDIO_CHECK_PATH, named from the first call site it was seen at, which
 * is the mistake CLAUDE.md warns about. Stays original. */
#define ADDR_AUDIO_PATH_ARG      0x004852D4u

/* ---- Smacker video ----------------------------------------------------
 *
 * smackw32.dll has no SDK header and no import library, so its entry points are
 * reached through the game's own IAT slots -- the only place their addresses
 * exist. Reconstructed in src/game/win32/movie.cpp; the movie object is thiscall and
 * deliberately opaque, with only the fields actually touched named there.
 */
#define ADDR_MOVIE_STOP          0x00445120u  /* thiscall void(this) */
#define ADDR_MOVIE_SET_VOLUME    0x00445280u  /* thiscall void(this, int32) */
#define ADDR_MOVIE_VTABLE        0x0046FAB4u  /* stamped into the object */
#define ADDR_MOVIE_SOUND_READY   0x006598A8u  /* int32_t; set once Smacker has sound */
#define ADDR_MOVIE_OPEN          0x00444FC0u  /* thiscall this(this,name,w,h,big) */
#define ADDR_MOVIE_MAKE_SURFACE  0x00445690u  /* surface *(w, h), stays original */
#define ADDR_MOVIE_DSOUND        0x004FA46Cu  /* the DirectSound object, may be null */
#define ADDR_IAT_SMACK_OPEN      0x0046F2C8u
#define ADDR_IAT_SMACK_DDTYPE    0x0046F2CCu
#define ADDR_IAT_SMACK_USE_DSOUND 0x0046F2C4u
#define ADDR_MOVIE_DRAW_FRAME    0x004453C0u  /* thiscall void(this, arg) */
#define ADDR_MOVIE_FINISHED      0x00445600u  /* void(void); posts WM_USER */
#define ADDR_MOVIE_START         0x004451F0u  /* thiscall int32(this, arg) */
/* Lives inside what docs/functions.tsv reports as one 160-byte function at
 * 0x00445320 -- the inventory merged the two. It is a separate function. */
#define ADDR_MOVIE_POLL          0x00445390u  /* thiscall int32(this) */
#define ADDR_IAT_SMACK_WAIT      0x0046F2BCu
#define ADDR_MOVIE_APPLY_PALETTE 0x00445320u  /* thiscall void(this, surface) */
#define ADDR_MOVIE_CURRENT       0x006568A0u  /* the movie being played, or null */
#define ADDR_MOVIE_TIMER_PROC    0x004455E0u  /* the timer callback, stays original */
#define ADDR_GAME_DELETE         0x004648F5u  /* the game's own operator delete */
/* Posted to the window to advance the game state machine: 0x400 when a movie
 * finishes, 0x402 when one could not be started. Both land in the same handler,
 * which is why src/game/win32/winproc.cpp forwards them together. */
#define AM2_WM_STATE_ADVANCE     0x0400u
#define AM2_WM_STATE_ABORT       0x0402u
#define ADDR_MOVIE_PALETTE_OWNER 0x00477A58u  /* void **; +0x800 is a DD palette */
#define ADDR_IAT_SMACK_TO_BUFFER 0x0046F2B0u
#define ADDR_IAT_SMACK_DO_FRAME  0x0046F2B4u
#define ADDR_IAT_SMACK_NEXT_FRAME 0x0046F2B8u
#define ADDR_IAT_SMACK_CLOSE     0x0046F2C0u
#define ADDR_IAT_SMACK_VOLUMEPAN 0x0046F2ACu

/* .WAV reading through WINMM's multimedia file services -- the only file I/O
 * in the game that does not go through the CRT. Reconstructed in
 * src/game/win32/wavefile.cpp; these are the DirectX SDK sample's names. */
#define ADDR_WAVE_OPEN_FILE      0x0040CA10u  /* MMRESULT(char*,HMMIO*,WAVEFORMATEX**,MMCKINFO*) */
#define ADDR_WAVE_START_DATA     0x0040CBB0u  /* MMRESULT(HMMIO*,MMCKINFO*,MMCKINFO*) */
#define ADDR_WAVE_READ_FILE      0x0040CBF0u  /* MMRESULT(HMMIO,uint32,uint8*,MMCKINFO*,uint32*) */
#define ADDR_WAVE_CLOSE_FILE     0x0040CCE0u  /* MMRESULT(HMMIO*,WAVEFORMATEX**) */

/* Error reporting. Both format into the game's own static buffers and put a
 * message box up; both return 0, which is what lets callers `return` them. */
#define ADDR_FATAL_ERROR         0x0041E750u  /* int32_t(const char *fmt, ...) */
#define ADDR_ERROR_TEXT          0x0050B5E0u  /* char[], the formatted message */
#define ADDR_ERROR_TEXT_DD       0x0050B1E0u  /* char[], the same with an HRESULT */
#define ADDR_VSPRINTF            0x00465A45u  /* the game's own CRT vsprintf */

/* ---- application, window and message loop -----------------------------
 *
 * The outermost layer of the process: WinMain parses the command line, brings
 * up the window and DirectDraw, then runs a PeekMessage loop that ticks a frame
 * whenever the queue is empty.
 *
 * 0x00507344 is the single most useful global here. `-w` sets it, and it gates
 * every windowed-mode behaviour in the game -- the window gets a border and is
 * repositioned, DirectDraw asks for a palettized primary, and CalibratePalette
 * runs. Without it the game is fullscreen and none of that happens, which is
 * why CalibratePalette never fires under the harness as it is normally driven.
 */
#define ADDR_WIN_MAIN            0x0040B360u  /* int32 __stdcall(inst,prev,cmd,show) */
#define ADDR_INIT_APPLICATION    0x0040B600u  /* int32_t(HINSTANCE, int32_t nCmdShow) */
#define ADDR_PUMP_MESSAGE        0x0040B280u  /* int32_t(MSG *) -- 0 on WM_QUIT */
#define ADDR_POSITION_WINDOW     0x0040B070u  /* void(void) */
/* The window procedure. Reconstructed in src/game/win32/winproc.cpp, but NOT patched:
 * it is reached only through the WNDCLASS field that InitApplication fills in,
 * so pointing that at our own leaves the original intact and callable. The
 * messages that are pure comm and game logic are forwarded straight back to it
 * rather than reconstructed. Nothing else in the image refers to this address. */
#define ADDR_WND_PROC            0x0040A6B0u  /* LRESULT CALLBACK(HWND,UINT,WPARAM,LPARAM) */

/* State the window procedure reads. */
#define ADDR_DIRECTDRAW          0x004FDF78u  /* IDirectDraw * */
#define ADDR_PAINT_OBJECT        0x0065A058u  /* see winproc.cpp -- not COM */
/* The two text fields of the ENTER BATTLE NAME dialog, inside the paint
 * object: the session's name and the hosting player's. */
#define DLG_OFF_BATTLE_NAME      0x064u
#define DLG_OFF_PLAYER_NAME      0x084u
/* Where HostBattle keeps the two names after the session is up. */
#define ADDR_SAVED_PLAYER_NAME   0x00516094u  /* char[] */
#define ADDR_SAVED_BATTLE_NAME   0x005160D5u  /* char[] */
#define ADDR_HOST_BATTLE         0x0042FFF0u  /* void(void) */
#define ADDR_APP_ACTIVE          0x004FA02Cu  /* int32_t; RunFrame ticks only if set */
#define ADDR_CHAR_HANDLER        0x005125B8u  /* void(*)(wparam, lo, hi), may be null */
#define ADDR_GAME_STATE          0x00511DA4u  /* int32_t, 0..4 */
#define ADDR_GAME_STATE_ARG      0x00511DB4u  /* int32_t */
#define ADDR_STATE_DISPATCH      0x00486550u  /* 12-byte records; +0 is a function */
#define ADDR_ON_APP_ACTIVATED    0x004269B0u  /* void(void) */
#define ADDR_CURRENT_STATE       0x0042E5D0u  /* int32_t(void), indexes the above */
#define ADDR_STATE_LEAVE         0x0042E720u  /* void(void) */
/* The game is a five-state machine driven by RunFrame, and changing state is
 * two functions rather than one -- which the old name ADDR_STATE_ENTER hid.
 *
 * REQUEST stores the wanted state and raises a pending flag; it does NOT change
 * the current state. COMMIT, a separate function, moves the pending state into
 * the live one and lowers the flag.
 *
 * That flag is what makes the level teardown reachable: the state-2 handler at
 * ADDR_STATE2_FRAME checks it and tail-jumps to ADDR_LEVEL_TEARDOWN, so the
 * teardown runs when a transition is
 * requested while the game is ALREADY in state 2 -- that is, on leaving a
 * level. It is the only route to StopAllSounds, which is why no amount of
 * entering Boot Camp reaches it: entering is a transition INTO the state, and
 * only leaving triggers the teardown. */
#define ADDR_REQUEST_STATE       0x00424AD0u  /* void(int32_t) */
#define ADDR_COMMIT_STATE        0x00424AF0u  /* void(void) */
#define ADDR_STATE_PENDING       0x00511DACu  /* int32_t, a change is wanted */
#define ADDR_STATE_WANTED        0x00511DB0u  /* int32_t, -1 when none */
/* The other way the pending flag goes up, and the one that matters for the
 * teardown: 0x00425EE0 consumes a MENU REQUEST -- the same ADDR_MENU_REQUEST /
 * ADDR_MENU_REQUEST_SET pair StartSelectedGame and HostBattle write -- moves
 * the code to 0x00511DBC and raises the flag. So the in-game route to the
 * level teardown is a menu request raised while the game is in state 2. */
#define ADDR_TAKE_MENU_REQUEST   0x00425EE0u  /* void(void) */

/* ---- the mission-script interpreter --------------------------------------
 *
 * The game's missions are readable text under data/<map>/, and the engine
 * parses them at load. That makes this subsystem the one part of the
 * reconstruction whose names can be taken from the program's own vocabulary
 * rather than invented: docs/scripttokens.md lists all 141 keywords, and the
 * interpreter's own log strings say what it is doing.
 *
 * The chain, from WinMain down:
 *
 *   WinMain -> RunFrame -> ADDR_STATE2_FRAME -> ADDR_LOAD_LEVEL_SCRIPT
 *     -> ADDR_READ_SCRIPT -> ADDR_SCRIPT_NEXT_TOKEN
 *       -> ADDR_SCRIPT_NEXT_TOKEN -> IsBlank, IsScriptDelim
 *
 * ADDR_LOAD_LEVEL_SCRIPT builds "<map><n>.txt" when the level index is
 * positive and "<map>.txt" otherwise -- kitchen1.txt, kitchen2.txt -- resets
 * the context, declares the five score variables the scripts can read, and
 * parses. It logs "reading script %s:" and then "worked!" or "FAILED!", which
 * is how these three are named rather than guessed.
 *
 * The two already-reconstructed helpers at the bottom of that chain were
 * ported from their bodies alone, before any of this was known: IsBlank is
 * space/tab/CR and IsScriptDelim accepts exactly the first character of each
 * of tokens 1..13. They are the lexer's character classes. */
#define ADDR_LOAD_LEVEL_SCRIPT   0x00425060u  /* void(void) */
#define ADDR_READ_SCRIPT         0x00444CD0u  /* int32_t(const char *, ctx *) */
/* Parse one typed line and run it -- the other caller of the tokeniser, and
 * NOT part of the ReadScript path, which was recorded here wrongly for several
 * commits. ReadScript tokenises with NextToken directly and never calls this.
 *
 * It names itself nowhere, so it has no name here either; the earlier
 * `ParseLine` was invented, like `ParseScriptFile` before it. What it is for
 * comes from its only caller instead: 0x00417B80 carries "Cheat!!!",
 * "I am the Juggernaut!", "I can fly!" and "Aye aye Captain!", so the typed
 * line is a cheat code and this is what executes one. */
#define ADDR_SCRIPT_RUN_LINE     0x00444C40u
#define ADDR_CHEAT_ENTRY         0x00417B80u
#define ADDR_SCRIPT_NEXT_TOKEN   0x0043F450u  /* the tokeniser; stops at // */
#define ADDR_SCRIPT_RESET        0x0043F2F0u  /* void(ctx *) */
#define ADDR_SCRIPT_LOOKUP_TOKEN 0x0043EEE0u  /* int32_t(const char *word) */

/* Where the ORIGINAL's keyword table is: 185 {const char *, int32_t} pairs,
 * the bounds being ScriptLookupToken's own -- it walks from the first and
 * stops at the second. The reconstruction no longer reads it; the table is
 * written out in game/scripttokens.h and generated from here, so these stay
 * as the record of where it came from and as what tools/scripttokens.py
 * reads. Nothing in the image writes it. */
#define ADDR_SCRIPT_TOKENS       0x00487C90u
#define ADDR_SCRIPT_TOKENS_END   0x00488258u
#define ADDR_SCRIPT_ADD_TOKEN    0x0043F370u  /* void(ctx, kind, value, line) */
#define ADDR_SCRIPT_WORD_BUF     0x00656354u  /* char[0x40], the scratch word */
#define ADDR_SCRIPT_FREE_TOKEN   0x0043F000u  /* void(token *) */
#define ADDR_SCRIPT_GROW_TOKENS  0x0043F340u  /* void(ctx *) */
#define ADDR_SCRIPT_PARSE_NUMBER 0x0043EF70u  /* int32_t(const char *, int32_t *, float *) */
#define ADDR_SCRIPT_TOKEN_NAME   0x0043EF40u  /* const char *(int32_t id) */
#define ADDR_SCRIPT_FIND_NAME    0x0043F670u  /* int32_t(const char *) */
#define ADDR_SCRIPT_TOKEN_TEXT   0x00444A90u  /* char *(tok *, char *out) */
#define ADDR_SCRIPT_IS_STMT      0x00444B80u  /* int32_t(ctx *, int32_t *at) */

/* The script's name table -- declared variables and objects. Sixteen bytes an
 * entry, the first field a `char *`. Written at runtime, so the offline test
 * cannot reach it; only the in-process check can. */
#define ADDR_SCRIPT_NAME_CAP     0x00656460u
#define ADDR_SCRIPT_NAME_COUNT   0x00656464u
#define ADDR_SCRIPT_NAMES        0x00656468u
#define AM2_SCRIPT_NAME_SIZE     0x10u
#define ADDR_SCRIPT_ADD_NAME     0x0043F7A0u  /* AddNameTableName */

/* The name table's value accessors, all three named by their own messages.
 * The two SetVarValue addresses say the same name because they are almost
 * certainly two overloads of it -- one taking an index, one a name. */
#define ADDR_GET_VAR_VALUE       0x00443E40u  /* int32_t(int32_t, int32_t *) */
#define ADDR_SET_VAR_VALUE       0x00443E90u  /* int32_t(int32_t, int32_t) */
#define ADDR_SET_VAR_BY_NAME     0x00443ED0u  /* int32_t(const char *, int32_t) */
#define ADDR_SCRIPT_ALLOC_UID    0x0041E7F0u  /* int32_t(void) */
#define ADDR_NEXT_UID            0x00511DF4u

/* The seven kind names, indexed by kind -- "Unknown", "Control Character",
 * "Reserved", "Integer", "Float", "String", "Name". The handlers pass entries
 * of this array straight into their "expected token of type %s" message. */
#define ADDR_SCRIPT_KIND_NAMES   0x00487C74u

/* A declared name's type, as AddNameTableName takes it. 0 allocates a fresh
 * uid; 1..3 store the one passed. Anything else is an error and stores it
 * anyway. Type 3 is what `variable` declares. */
#define AM2_NAME_TYPE_OBJECT     0   /* `object`; takes a fresh uid   */
#define AM2_NAME_TYPE_PAD        1   /* `pad`                         */
#define AM2_NAME_TYPE_REF        2   /* a name used before declaring  */
#define AM2_NAME_TYPE_INTEGER    3   /* `variable`                    */

/* Whether this is a multiplayer session, and which end of it. Zero is single
 * player; four places write it and only ever 1 or 2 -- the MULTI-PLAYER host
 * screen (0x004317C0, "Host is ready - waiting for players") writes 1, the
 * join screen (0x00433480) writes 2, and host migration writes 1 because you
 * have just become the host.
 *
 * Both readers this project has traced only test it against zero:
 * LoadLevelScript reads the rules and per-army AI scripts when it is set, and
 * ReadScript prints its summary when it is not. Ninety more sites read it, so
 * treat 1-versus-2 as unsettled -- the title screen also writes 2, which a
 * plain host/client reading does not explain, and nothing here needed to know.
 *
 * The two names this address carried before, ADDR_SCRIPT_QUIET and
 * ADDR_COMM_HOST_CHANGED, were each a guess from one call site. */
#define ADDR_MP_SESSION          0x00511DA0u

/* A `char *` to the string "unknown", parked immediately after the keyword
 * table -- it points at 0x0048825C, which is the table's own end address. */
#define ADDR_SCRIPT_UNKNOWN_STR  0x00488258u

/* The game's own statically linked MSVC CRT. Blocks cross between our code and
 * the original's, so the allocator has to be the same one; see game/crt.h. */
#define ADDR_CRT_MALLOC          0x004647F8u
#define ADDR_CRT_REALLOC         0x004646D8u
#define ADDR_CRT_GETCWD          0x00465DB5u  /* _getcwd(buf, max) */
#define ADDR_CRT_CHDIR           0x00465ED0u  /* _chdir: SetCurrentDirectoryA
                                               * then GetCurrentDirectoryA */
#define ADDR_CRT_FREE            0x004646A9u
#define ADDR_SCRIPT_DECLARE_VAR  0x0043F7A0u  /* handle(const char *, kind, init) */
#define ADDR_SCRIPT_FIND_FILE    0x00421890u  /* probes <map><n>.txt via _findfirst */

/* ReadScript names itself: "ReadScript: Could not open %s for ...". It fopen's
 * the file, fgets a line at a time, tokenises, and dispatches on the first
 * token of each statement.
 *
 * A token is 12 bytes: kind at +0, id at +8. The dispatch tests kind == 2,
 * which the kind array calls "Reserved" -- a keyword -- and then switches on
 * the id, which is the number docs/scripttokens.md lists against each word. So
 * every one of these handlers is named by the language rather than by us, and
 * the five of them are the whole top-level grammar: exactly the statements a
 * mission file contains. */
/* The parse context and the token record, read out of the `variable` handler
 * at 0x00443F70 -- 368 bytes and every field it touches is one of these.
 *
 *   ctx + 0x04   token count
 *   ctx + 0x08   token array
 *   token + 0x00 kind      (the array at 0x00487C74 names them)
 *   token + 0x04 text      the word as it appeared, used in error messages
 *   token + 0x08 id        the number docs/scripttokens.md lists
 *
 * Every handler is called as handler(int32_t *pos, ctx *), where *pos is the
 * index of the keyword and the handler advances it over what it consumes.
 *
 * One thing to know before writing a caller: an UNQUOTED identifier tokenises
 * as kind 5, which the kind array calls "String". `variable stopcloning 0`
 * requires kind 5 for the name. So "String" here means a word, not a quoted
 * literal, and kind 6 "Name" is something else again. */
#define AM2_SCRIPT_TOKEN_SIZE    12u
/* AddToken stores its second argument at +0x00, its fourth at +0x04 and the
 * value at +0x08 -- so the middle field is the LINE NUMBER, not the text. The
 * earlier labels here (TOK_TEXT/TOK_ID) were read off a call site rather than
 * out of the body, which is the mistake this project keeps making. */
#define AM2_SCRIPT_TOK_KIND      0x00u
#define AM2_SCRIPT_TOK_LINE      0x04u
#define AM2_SCRIPT_TOK_VALUE     0x08u
#define AM2_SCRIPT_CTX_CAPACITY  0x00u
#define AM2_SCRIPT_CTX_COUNT     0x04u
#define AM2_SCRIPT_CTX_TOKENS    0x08u

#define ADDR_SCRIPT_PRELOADSPRITE 0x00444900u  /* keyword 25 */
/* What the preloadsprite statement drives. Named from the filenames its
 * callee at 0x004457E0 builds -- "%02d_%03d_%02d_*.bmp" and the matching
 * ".sha" -- so the statement's three integers are a sprite identity triple. */
#define ADDR_PRELOAD_SPRITE       0x00445B00u
/* What PreloadSprite calls once it has decided the sprite is not loaded. The
 * first builds the two filenames above, chdirs with ADDR_SET_DATA_DIR, and
 * fills the record; the second grows the registry and puts the record in it.
 * Both stay original -- they are the bitmap loader and the table, not the
 * cache decision this port is taking over. */
#define ADDR_SPRITE_LOAD_TRIPLE   0x004457E0u  /* int32(spr,a,b,c,flags) */
#define ADDR_SPRITE_REGISTER      0x004459E0u  /* void(spr, id) */
#define ADDR_SCRIPT_PAD           0x004440E0u  /* keyword 26 */

/* Pads -- the script's trigger regions. Two tables: one of pad records in
 * definition order, and one indexed by the pad NUMBER a script gives, which
 * several pads may share. */
#define ADDR_PADS                 0x00516198u  /* AM2_Pad[], stride 72 */
#define ADDR_PAD_COUNT            0x00511DF8u
#define ADDR_PAD_NUMBERS          0x0051F198u  /* AM2_PadNumber[], stride 76 */
#define ADDR_PAD_FINALISE         0x004375A0u  /* void(AM2_Pad *, int32_t) */

/* The two map layers the centroid scan reads, one byte per cell. The first
 * holds a pad number per cell and serves numbers 8 and above; the second holds
 * a bitmask and serves 0..7, with ADDR_PAD_BIT_TABLE giving the bit. */
#define ADDR_MAP_PAD_LAYER        0x00514EC8u
#define ADDR_MAP_PADBIT_LAYER     0x00514EC4u
#define ADDR_PAD_BIT_TABLE        0x00486444u  /* int32_t[8], 1<<n */
#define ADDR_MAP_WIDTH            0x00514DE0u
#define ADDR_MAP_HEIGHT           0x00514DDCu
#define ADDR_PAD_DEFAULT_POS      0x005125A0u  /* both centroid words at once */
#define ADDR_SCRIPT_VARIABLE      0x00443F70u  /* keyword 133 */
#define ADDR_SCRIPT_IF            0x004432F0u  /* keyword 44 */

/* The sub-parsers `if` drives. All four stay the original's. */
#define ADDR_SCRIPT_SCAN_FOR      0x00442F10u  /* int32_t(ctx,from,want,stop) */
#define ADDR_SCRIPT_PARSE_EVENT   0x0043FF90u  /* (ctx,at,&a,&b,&c) */
#define ADDR_SCRIPT_HIT_TARGET    0x0043FAB0u  /* (ctx,at,&mask) */
#define ADDR_SCRIPT_LOCATION      0x004409F0u  /* (ctx,at,action,quiet) */

/* The action record is a struct now -- AM2_ScriptAction in game/script.h --
 * with its layout asserted against these offsets at compile time. */
#define ADDR_SCRIPT_ORDER_TARGET  0x0043FCF0u  /* (ctx,at,&form,&val,&army) */
#define ADDR_SCRIPT_PARSE_EVENTS  0x00440600u  /* (ctx,at,cond) */

/* The uids the four army keywords and `me` stand for. Not in keyword order,
 * and the first arm of the resolver's jump table serves id 15 -- which no
 * entry in the keyword table produces, so it cannot be reached. */
#define ADDR_SVAR_ID15            0x00656474u  /* unreachable */
#define ADDR_SVAR_GREEN           0x00656484u
#define ADDR_SVAR_TAN             0x00656498u
#define ADDR_SVAR_BLUE            0x00656454u
#define ADDR_SVAR_GREY            0x0065646Cu
/* ADDR_SVAR_ME at least is a name-table INDEX rather than a uid: ResolveUid
 * bounds its argument against ADDR_SCRIPT_NAME_COUNT and then compares it
 * against this. The group comment above says "uids"; that is right for what a
 * type-0 entry's value holds and wrong for this global. */
#define ADDR_SVAR_ME              0x00656458u
#define ADDR_SCRIPT_PARSE_VALUE   0x00443010u  /* (ctx,at,&a,&b,&c) */
#define ADDR_SCRIPT_PARSE_ACTION  0x00440D70u  /* (ctx,at,uint8_t[0x48]) */

/* Head of the condition list; each record links through its +0x30. */
#define ADDR_SCRIPT_CONDITIONS    0x00510214u

/* event.cpp's registration table and the three things DeclareRuleVars does to
 * it. The table is 1024 buckets of 16-byte nodes at 0x005101F0, chained
 * through +0x0C and keyed on the first two arguments of the register call.
 * All four stay original -- what is reconstructed is the declaring, not the
 * table. */
/* The registration table: NINE buckets at 0x005101F0, ending exactly where
 * ADDR_SCRIPT_CONDITIONS begins, which is how the count is known -- the
 * teardown's loop bound says so. It went in here as 1024, invented.
 *
 * A bucket holds a chain of 16-byte entries {key0, key1, handlers, next}, and
 * each entry a chain of 16-byte handlers {fn, arg, owns, next}. `owns` is the
 * sixth argument to the register call: set, the teardown frees `arg` as well
 * as the node. DeclareRuleVars passes 0 for it, so the conditions it registers
 * are not freed by the table that points at them. */
#define ADDR_EVENT_TABLE         0x005101F0u
#define AM2_EVENT_BUCKETS        9
#define AM2_EVENT_NO_KEY         (-2)         /* key0 == -2 registers nothing */
#define ADDR_EVENT_REGISTER      0x0041EE70u  /* void(bucket,k0,k1,fn,arg,owns) */
#define ADDR_EVENT_CLEAR_ALL     0x004223D0u  /* void(void), frees every node */
/* 0x00422450. Drop the whole script/event state in one go: the name table,
 * the condition list, the registration table, and one flag. All three callees
 * are already ours, so this is pure orchestration -- but the ORDER is the
 * content, and it is names, then conditions, then registrations. Role name;
 * one caller. */
#define ADDR_RESET_SCRIPT_STATE  0x00422450u  /* void(void) */
#define ADDR_SCRIPT_STATE_FLAG   0x00511DFCu  /* cleared by the reset above */
/* 0x0041FEA0. Look a uid up and, if it resolves, hand the object to
 * 0x00428DA0 -- 96 bytes with twenty-two callers, unnamed. Both role names:
 * neither says anything about itself. */
#define ADDR_EVT_OBJ_ACTION      0x0041FEA0u  /* void(uint32_t uid) */
/* 0x0041FE70. Look a uid up and deploy the object. DeployItem names itself --
 * "DeployItem(resurrection): uid:%x, health:%d" -- and takes the point to
 * deploy at; passing 0 there means "where it already is". */
#define ADDR_EVT_DEPLOY_ITEM     0x0041FE70u  /* void(uint32_t, uint32_t) */
#define ADDR_DEPLOY_ITEM         0x00428CA0u  /* void(obj, point, int32, int32) */
/* 0x0041FBE0 and 0x0041FC10 are the same shim twice: uid >= 1000, look it up,
 * and if ObjIsType2 call one function on it -- 0x00448170 for the first and
 * 0x00448220 for the second. Neither callee names itself and object type 2 is
 * one of the three CLAUDE.md still lists as unidentified, so these are role
 * names and the pair is distinguished only by which callee it reaches. */
#define ADDR_EVT_TYPE2_ACTION_A  0x0041FBE0u  /* void(uint32_t uid) */
#define ADDR_EVT_TYPE2_ACTION_B  0x0041FC10u  /* void(uint32_t uid) */
#define ADDR_TYPE2_ACTION_A      0x00448170u  /* void(void *obj) */
#define ADDR_TYPE2_ACTION_B      0x00448220u  /* void(void *obj) */
/* A third of the same twin, with an argument to pass on. */
#define ADDR_EVT_TYPE2_ACTION_C  0x0041FBA0u  /* void(uint32_t, int32_t) */
#define ADDR_TYPE2_ACTION_C      0x004480E0u  /* void(void *obj, int32_t) */
/* 0x0041F6E0. The one that does NOT null-check: it passes whatever LookupByUID
 * returned straight on. 0x00428370 has eight callers and no name. */
#define ADDR_EVT_OBJ_SET         0x0041F6E0u  /* void(uint32_t, int32_t) */
#define ADDR_OBJ_SET             0x00428370u  /* void(obj, int32_t, int32_t) */
/* 0x0041F710. The most guarded member of the family: uid threshold, pointer,
 * a flag bit CLEAR at +8, and a positive int16 at +0x62, all before it acts.
 * Its callee has nineteen callers and no name. */
#define ADDR_EVT_GUARDED_ACTION  0x0041F710u  /* void(uint32_t, int32, int32) */
#define ADDR_GUARDED_ACTION      0x00428140u  /* void(obj,int32,int32,0,0,0) */
#define OBJ_OFF_FLAGS8           8u    /* bit 2 blocks the action above */
#define OBJ_FLAG8_BLOCKED        4u
#define OBJ_OFF_COUNT62          0x62u /* int16; must be > 0 */

/* Two more of the "On" shape: take a uid, substitute the object's own position
 * for a point, and call the twin that takes the point directly. event.h
 * already records the pattern for EvtPlaySoundAt/EvtPlaySoundOn -- these are
 * two more pairs, and the "At" halves are themselves small and unreconstructed
 * (0x0041F820, 0x0041F8B0, and 0x0041F780 for a third pair). */
#define ADDR_EVT_AT_OBJ_POS_A    0x0041F880u  /* void(int32, uint32, int32) */
#define ADDR_EVT_AT_OBJ_POS_B    0x0041F970u  /* void(int32,int32,uint32,int32) */
/* The "At" halves are not plain point-takers, which the "On" wrappers above
 * made them look like. Each takes a UID as well, and a `relative` flag: when
 * it is set the object's own position is ADDED to the point rather than
 * replacing it. That flag is AM2_ScriptAction.relative -- the leading `+` on a
 * script's coordinates -- so this is where that syntax is honoured. */
#define ADDR_AT_POINT_A          0x0041F820u  /* void(uid, point, relative) */
/* NOT a peer of the two shims above, despite the matching address band: it
 * resolves an ARMY and walks every object that army owns. */
#define ADDR_EVT_ARMY_AT_POINT   0x0041F8B0u  /* void(army,filter,point,rel) */
#define ADDR_ARMY_OBJ_LISTS      0x004F9ECCu  /* one list per comm slot */
#define ADDR_LIST_REMOVE_AT      0x0042A750u  /* thiscall(list, index) */
#define LIST_OFF_COUNT           4u
#define LIST_OFF_UIDS            8u
#define ADDR_AT_POINT_C          0x0041F780u  /* void(uid, point, relative) */
/* The "On" wrapper for AT_POINT_C, exactly the shape of EvtAtObjPosA. */
#define ADDR_EVT_AT_OBJ_POS_C    0x0041F7F0u  /* void(uid, uid, int32) */
/* Two 32-byte shims that pass the ADDRESS of their first argument to a
 * function above the nominal CRT line -- game code, per tools/crt.py, not
 * library. Neither callee names itself. */
#define ADDR_EVT_BY_REF_A        0x0041FD10u  /* void(int32_t, int32_t) */
#define ADDR_EVT_BY_REF_B        0x0041FD30u  /* void(int32_t, int32_t) */
#define ADDR_BY_REF_ACTION_A     0x00462000u  /* void(int32_t *, int32, int32) */
#define ADDR_BY_REF_ACTION_B     0x00462080u  /* void(int32_t *,0,0,0,int32) */
#define ADDR_POINT_ACTION_A      0x004582F0u  /* void(obj, point) */
#define ADDR_POINT_ACTION_C      0x00428F80u  /* void(obj, point) */
#define OBJ_OFF_X                0x12u
#define OBJ_OFF_Y                0x14u
#define ADDR_OBJ_ACTION          0x00428DA0u  /* void(void *obj) */
/* 26 callers, and suppressed when the multiplayer session flag is set and the
 * comm object agrees, or when a state word reads 0x22. Named for what it is
 * observed to do from here -- announce an event -- and not from any one of
 * those callers. */
/* event.cpp's object setters: reach an object by uid and write one field.
 * They share a shape -- reject a uid below AM2_UID_COUNTER_MIN, look it up,
 * sometimes check the type or the null, then store. The differences between
 * them are exactly which of those guards each one has, so they are worth
 * keeping separate rather than folding into one helper. */
#define ADDR_EVT_SET_FIELD_540   0x0041FAB0u  /* void(uid, int32), type 2 only */
#define ADDR_EVT_SET_MODE_F0     0x0041FAE0u  /* void(uid, int32), +0xF0, type 2/3/8 */
#define ADDR_EVT_SET_MODE_94     0x0041FB10u  /* void(uid, int32), +0x94, type 2/3/8 */
#define ADDR_EVT_SET_FLAG810     0x0041FB40u  /* void(uid, int32), flags 0x810 */
#define ADDR_EVT_SET_OWNER       0x0041FB80u  /* void(uid, int8), +0x10 */
#define ADDR_EVT_SET_BYTE40      0x00420020u  /* void(uid, int8), +0x40 */
#define ADDR_EVT_SET_BYTE530     0x00420040u  /* void(uid, int8), +0x530, type 3 */
#define ADDR_LOAD_SCRIPT_COND    0x0041EC70u  /* void(FILE *, cond *) */
#define ADDR_LOAD_EVENT_SECTION  0x004225E0u  /* int32_t(FILE *) */
/* map.cpp's savegame section: one fixed 236-byte block and nothing else, which
 * is why the pair is 48 and 64 bytes. The block is at 0x00514D90. */
/* event.cpp's OTHER section: a tag, then the block's own length as a second
 * tag, then 16008 bytes straight out of 0x0050C368. The length goes out
 * through WriteSaveTag and comes back through CheckSaveTag, which is why
 * 0x00003E88 sits in docs/savetags.tsv looking like a twelfth section tag.
 * It is not one. */
/* pad.cpp's savegame section, and the reset the loader opens with. The two
 * blocks are ADDR_PAD_NUMBERS and ADDR_PADS, already named; the sizes here are
 * confirmed three ways -- the fwrite lengths, the fread lengths, and the reset's
 * `rep stosd` counts of 0x1300 and 0x2400 dwords. */
/* script.cpp's savegame section, and the table free the loader opens with.
 * The section exists in this shape because AM2_ScriptName begins with a
 * POINTER: the struct cannot go out whole, so the name travels as a length and
 * then the bytes, and only the 12 fields after the pointer are written raw. */
/* air.cpp's savegame section: a tag and one 584-byte block, the same shape as
 * map.cpp's. The section map puts air at 588 bytes, which is 4 + 0x248. */
/* gameproc.cpp's savegame section: no tag of its own -- SaveGame writes
 * 0x06660666 before calling -- just the block LENGTH and then 1080 bytes from
 * 0x00511A68.
 *
 * The loader is the interesting half. Five things inside that block survive a
 * load: two strings, and the three audio volumes, which are already named
 * ADDR_VOLUME_AT_ZERO, ADDR_STREAM_VOLUME and ADDR_VOLUME_VOICE. They are
 * stashed on the stack, overwritten by the read, and put back -- so loading a
 * save does not reset what the player set. */
#define ADDR_SAVE_GAMEPROC       0x00426850u  /* int32_t(FILE *) */
#define ADDR_LOAD_GAMEPROC       0x00426880u  /* int32_t(FILE *) */
/* The two ends of the savegame format. SaveGame writes each section's tag and
 * calls its saver; LoadGame checks the outer tag, resets the token context and
 * runs the eleven loaders in the same order, closing the file on both exits. */
/* The section's own tag, written by SaveGame rather than by the saver. Three
 * readers check it: LoadGame, and 0x00425950 when it validates a save before
 * the mission starts. */
#define AM2_SAVETAG_GAMEPROC     0x06660666u
#define ADDR_LOAD_GAME           0x00425A10u  /* int32_t(FILE *) */
#define ADDR_SAVE_GAME           0x00425790u  /* int32_t(const char *) */
#define ADDR_GAMEPROC_BLOCK      0x00511A68u  /* also a string; see below */
#define AM2_GAMEPROC_SAVE_SIZE   0x438u       /* 1080 bytes, and its own tag */
#define ADDR_GAMEPROC_STR_B      0x00511B88u  /* a second string inside it */
#define ADDR_STR_GAMEPROC_CPP    0x004851ECu  /* "C:\\ArmyMen2\\source\\gameproc.cpp" */

#define ADDR_SAVE_AIR_SECTION    0x00409840u  /* int32_t(FILE *) */
#define ADDR_LOAD_AIR_SECTION    0x00409870u  /* int32_t(FILE *) */
#define ADDR_AIR_SAVE_BLOCK      0x004F945Cu
#define AM2_AIR_SAVE_SIZE        0x248u       /* 584 bytes */
#define AM2_SAVETAG_AIR          0x06660010u
#define ADDR_STR_AIR_CPP         0x004740B0u  /* "C:\\ArmyMen2\\source\\air.cpp" */

#define ADDR_FREE_SCRIPT_NAMES   0x0043F030u  /* void(void), 3 callers */
#define ADDR_SAVE_SCRIPT_SECTION 0x0043F0A0u  /* int32_t(FILE *) */
#define ADDR_LOAD_SCRIPT_SECTION 0x0043F150u  /* int32_t(FILE *) */
#define ADDR_STR_SCRIPT_CPP      0x004888A4u  /* "C:\\ArmyMen2\\source\\script.cpp" */
#define AM2_SAVETAG_SCRIPT       0x06660002u

#define ADDR_SAVE_EVENT_SECTION  0x00422470u  /* int32_t(FILE *) */
#define ADDR_SAVE_PAD_SECTION    0x00437A90u  /* int32_t(FILE *) */
#define ADDR_LOAD_PAD_SECTION    0x00437AE0u  /* int32_t(FILE *) */
#define ADDR_RESET_PADS          0x004373C0u  /* void(void), 2 callers */
#define ADDR_STR_PAD_CPP         0x004877F8u  /* "C:\\ArmyMen2\\source\\pad.cpp" */
#define AM2_SAVETAG_PAD          0x06660005u
#define AM2_PAD_NUMBERS_BYTES    0x4C00u      /* 256 entries of 76 */
#define AM2_PADS_BYTES           0x9000u      /* 512 entries of 72 */

#define ADDR_SAVE_EVENT_BLOCK    0x0041E9E0u  /* int32_t(FILE *) */
#define ADDR_LOAD_EVENT_BLOCK    0x0041EA20u  /* int32_t(FILE *) */
#define ADDR_EVENT_BLOCK         0x0050C368u
#define AM2_EVENT_BLOCK_SIZE     0x3E88u      /* 16008 bytes, and its own tag */
#define AM2_SAVETAG_EVENT_BLOCK  0x06660006u

#define ADDR_SAVE_MAP_SECTION    0x0042DB40u  /* int32_t(FILE *) */
#define ADDR_LOAD_MAP_SECTION    0x0042DB70u  /* int32_t(FILE *) */
#define ADDR_MAP_SAVE_BLOCK      0x00514D90u
#define AM2_MAP_SAVE_SIZE        0xECu        /* 236 bytes */
#define AM2_SAVETAG_MAP          0x06660009u
#define ADDR_STR_MAP_CPP         0x00486410u  /* "C:\\ArmyMen2\\source\\map.cpp" */
/* 0x0042DBB0, "Checksum of %s " and "is %x " -- its own name. Seven callers,
 * and it sits immediately after the two map save/load functions, which is the
 * evidence for filing it in map.cpp: the band alone only says
 * map.cpp..objscript.cpp. */
#define ADDR_CHECKSUM              0x0042DBB0u  /* uint32_t(const char *) */

#define ADDR_SAVE_SCRIPT_CONDS   0x0041EC20u  /* int32_t(FILE *) */
#define ADDR_LOAD_SCRIPT_CONDS   0x0041EDD0u  /* int32_t(FILE *) */
#define AM2_SAVETAG_CONDS        0x06660003u  /* event.cpp's other tag */
/* The three handlers a saved event registration is restored with. Two are in
 * the objscript band and take a pad; the third takes a 16-byte record read
 * straight from the file. Reached by address -- they are original code. */
#define ADDR_EVT_PAD_HANDLER_A   0x00437570u
#define ADDR_EVT_PAD_HANDLER_B   0x00437540u
#define ADDR_EVT_RECORD_HANDLER  0x0041F3E0u
/* The one that BUILDS such a record. EventTriggerDelayed mallocs 16 bytes,
 * fills it with (type, num, uid, removeevent), starts a timer and registers
 * ADDR_EVT_RECORD_HANDLER against the timer's id with that record as the
 * argument -- passing 1 for `owns`, so the teardown frees it. Named by its own
 * log string, "EventTriggerDelayed: type %d, num: %d, uid: %x, removeevent:
 * %d, delay: %d". */
#define ADDR_EVENT_TRIGGER_DELAYED 0x0041F410u
#define ADDR_CREATE_TIMER          0x0041E820u  /* "CreateTimer", 304 B */
/* The network half of the event system, and the two sides confirm each other:
 * EventMessageSend packs a 40-byte message and EventMessageReceive unpacks the
 * same offsets and hands them to EventTriggerImmediate. Each names itself in
 * its own log string, and EventTriggerImmediate's names the trailing argument
 * Receive passes as 1 -- "remote". */
#define ADDR_EVENT_MESSAGE_SEND    0x0041F150u
#define ADDR_EVENT_MESSAGE_RECV    0x0041F320u
#define ADDR_EVENT_TRIGGER_IMMED   0x0041EF80u  /* 464 B, 3 callers */
/* Runs one action of an `if` -- (cond, index, arg). Thirty-two bytes and four
 * callers: it does nothing but index the 0x48-byte action array and hand the
 * result to the executor, which is a third independent confirmation of
 * AM2_ScriptAction's size (the parser writes it, the saver copies it, this
 * strides it). Role name. */
#define ADDR_COND_RUN_ACTION       0x00421410u
/* 0x0041F520, 80 bytes and FIFTY-THREE callers. Resolves a script name index
 * to the uid it stands for, with `me` taken from the caller's context instead.
 * It names itself nowhere -- its only string is the complaint "Bad ME" -- so
 * this is a role name. */
#define ADDR_RESOLVE_UID           0x0041F520u
/* 0x00421430, "Tried to switch on invalid state." -- which names the
 * condition, not the function. It runs an `if` statement's action list the way
 * its `mode` says to. Role name. */
#define ADDR_COND_RUN_ACTIONS      0x00421430u
/* The predicate it walks a bucket with is FilterMatches, 0x0041EF20, which
 * this port already owns -- so no new name here. That is what gives the two
 * pass-through fields of an event a meaning: they are the maskA/maskB
 * arguments, the sets the event belongs to. */
/* 0x004105F0, "ArmyMessageSend" from its own three error strings -- 304 bytes
 * and 20 callers, so it is the transport the whole game sends through. */
#define ADDR_ARMY_MESSAGE_SEND     0x004105F0u
/* 0x00410820, "SendGamePause from %x  Pause =%s  Flags=%x". Eight callers.
 * It fills two fields of a message that lives in .bss at 0x004FAA50 and hands
 * it to SendGameMsg -- the header is set up elsewhere, since nothing in the
 * file image backs that address. */
#define ADDR_MSG_GAME_PAUSE        0x004FAA50u
#define MSG_PAUSE_OFF_PAUSE        8u
#define MSG_PAUSE_OFF_FLAGS        0x0Cu
#define ADDR_STR_TRUE              0x00475C20u
#define ADDR_STR_FALSE             0x00475C18u
/* The outgoing packet. 0x004FAA68 is its base and 0x004FAA6C is base+4 -- the
 * packet's own length field, which doubles as the write cursor. Flush resets
 * it to 0x14, so the packet header is twenty bytes and the first message lands
 * at base+0x14. */
#define ADDR_ARMY_PACKET           0x004FAA68u
#define ADDR_ARMY_PACKET_LEN       0x004FAA6Cu
#define AM2_ARMY_PACKET_HDR        0x14u
/* Every message on this transport opens with the same eight bytes: a length, a
 * kind, and a uid. ArmyMessageSend reads the third as one -- it logs
 * UidArmy(UidOnWire(msg->uid)) -- which is how the field is known to be a uid
 * and not the padding EventMessageSend's always-zero write suggested. */
#define AM2_ARMY_MSG_HDR           8u
#define AM2_ARMY_MSG_EVENT         0x0020u   /* the kind word EventMessageSend
                                              * stamps at offset 2 */
/* Gates the event logging in EventTriggerDelayed, EventMessageSend and
 * EventMessageReceive -- all three read it before calling the logger, and the
 * logger is stubbed to `ret` in this build, so it is inert either way. */
#define COMM_OFF_EVENT_DEBUG       0x418u
/* The event.cpp section's own tag and the path string CheckSaveTag is given.
 * docs/savetags.tsv lists all fifteen; this is the one at event.cpp:3274. */
#define AM2_SAVETAG_EVENT        0x06660004u
#define ADDR_STR_EVENT_CPP       0x004783F8u  /* "C:\\ArmyMen2\\source\\event.cpp" */

/* Chunk tags inside the event.cpp save section. */
/* "another record follows", and it is NOT event-specific -- the item section
 * uses the same marker, and so does every other section that stores a list.
 * It went in as AM2_EVTSAVE_RECORD when only event.cpp used it, and a second
 * name for the same value appeared the moment item.cpp needed one. One name. */
#define AM2_SAVE_RECORD_MARK     0x06660000u
#define AM2_EVTSAVE_PAD_A        0x06670004u
#define AM2_EVTSAVE_PAD_B        0x06670005u
#define AM2_EVTSAVE_OWNED        0x06670006u
/* (fp, cond), NOT (cond, fp). This comment said the latter, which is the order
 * the reconstruction had before the campaign A/B caught it -- so the fix landed
 * in the code and the wrong order stayed here describing it. Confirmed twice
 * over: LoadScriptConditions calls the loader as (fp, cond), and
 * SaveScriptConditions pushes cond then fp, which is (fp, cond) in cdecl. */
#define ADDR_SAVE_SCRIPT_COND    0x0041EB00u  /* void(FILE *, const cond *) */
#define ADDR_EVENT_DEFAULT_NAME  0x0041F200u  /* void(kind, number, char *out) */
#define ADDR_FREE_SCRIPT_CONDS   0x0041EA80u  /* void(void), frees the list */
#define ADDR_EVT_PLAY_SOUND_AT   0x0041F680u  /* void(name, point, slot, pri, loop) */
#define ADDR_EVT_PLAY_SOUND_ON   0x0041F6B0u  /* void(name, owner, slot, pri, loop) */
#define ADDR_EVT_SET_WORD60      0x0041F750u  /* void(uid, int32), clamped */
#define ADDR_EVT_SET_AI_MODE     0x0041F9B0u  /* void(uid, mode), +0xE4/+0xE8 */
#define ADDR_EVT_MARK_SET        0x0041FF20u  /* void(row, col) -> 1 */
#define ADDR_EVT_MARK_CLEAR      0x0041FF40u  /* void(row, col) -> 0 */
#define ADDR_EVT_MARKS           0x00511E60u  /* int32_t[][4] */
#define ADDR_EVENT_NOTIFY        0x0041F4A0u  /* void(10 args) */

/* The callbacks DeclareRuleVars registers. The first two are six-byte
 * wrappers over one shared handler differing only in a 0 or a 1, which is what
 * makes "army" and "team" safe to say; the other three are named for the
 * global their uid is stored in, because their bodies are not identified. */
/* Direct string literals, not pointers to them: DeclareRuleVars pushes these
 * addresses straight into ScriptNameUid. */
#define ADDR_NAME_GREENWINS      0x00478880u
#define ADDR_NAME_TANWINS        0x00478878u
#define ADDR_NAME_BLUEWINS       0x0047886Cu
#define ADDR_NAME_GREYWINS       0x00478860u
#define ADDR_NAME_GREENTEAMWINS  0x00478850u
#define ADDR_NAME_TANTEAMWINS    0x00478844u
#define ADDR_NAME_BLUETEAMWINS   0x00478834u
#define ADDR_NAME_GREYTEAMWINS   0x00478824u

#define ADDR_EVT_ARMY_WINS       0x00422250u
#define ADDR_EVT_TEAM_WINS       0x00422260u
#define ADDR_EVT_RULE_A          0x00422270u
#define ADDR_EVT_RULE_B          0x00422310u
#define ADDR_EVT_RULE_C          0x004223A0u
#define ADDR_EVT_CONDITION       0x00421E80u  /* every `if` in the script */
#define ADDR_RULE_UID_A          0x00510218u
#define ADDR_RULE_UID_B          0x0051021Cu
#define ADDR_RULE_UID_C          0x00510220u
/* Names recovered from the error strings the functions print about
 * themselves. None is reconstructed yet; recorded so the names are here when
 * they are, rather than being guessed at from a call site again. */
/* Its third argument to DeployItem is 1 where EvtDeployItem passes 0, and that
 * is the resurrect flag -- which is why the callee's own line reads
 * "DeployItem(resurrection)". Two functions, one flag, and the string
 * explains itself once both are read. */
#define ADDR_SCRIPT_RESURRECT_ITEM 0x0041FEC0u  /* void(uint32_t, uint32_t) */
#define AM2_DEPLOY_RESURRECT       1
#define ADDR_SCRIPT_SET_OBJ_BITMAP 0x00420060u
/* 0x004371A0. Advance one object along its object script by one frame, if its
 * deadline has passed. Two of its own log strings name it and its callee:
 * "UpdateObjectScript: bad state index" and "ChangeObjectFrame failed in
 * UpdateObjectScript". The comment here used to read "also ChangeObjectFrame",
 * which put two names on one address -- they are separate functions and the
 * second is 0x004351C0, reached from nine places. */
#define ADDR_UPDATE_OBJECT_SCRIPT  0x004371A0u  /* int32_t(void *obj) */
#define ADDR_CHANGE_OBJECT_FRAME   0x004351C0u  /* int32_t(obj, frame, int32) */
/* Runs one parsed action against an owner. 4096 bytes in event.cpp with three
 * callers, and it names itself nowhere -- so this is a ROLE, not a recovered
 * source name, and it stays that way until the body says otherwise. */
#define ADDR_RUN_SCRIPT_ACTION     0x00420410u  /* void(action *, void *owner) */

/* The four object fields the object-script runner uses, all read out of
 * UpdateObjectScript's body rather than guessed at a call site. */
#define OBJ_OFF_SCRIPT_ID        0xB0u   /* 1-based index into the table; 0 = none */
#define OBJ_OFF_SCRIPT_STATE     0xB4u
#define OBJ_OFF_SCRIPT_FRAME     0xB8u
#define OBJ_OFF_SCRIPT_NEXT      0xBCu   /* deadline, compared against 0x00511E04 */
#define OBJ_OFF_OWNER            0x04u   /* what a frame's actions are run against */
#define ADDR_SET_OBJ_SCRIPT_STATE  0x004372A0u
#define ADDR_DEF_PARSE_INFO_FILE   0x0041A5F0u
/* NOT DefGameParse -- 0x00424590 is docs/functions.tsv's merged 784-byte
 * entry, and the handler with the "DefGameParse:" string starts at 0x00424780.
 * Same mistake, same cause, as ADDR_DEF_LINK_PARSE two commits ago. */
#define ADDR_DEF_GAME_ENTRY        0x00424590u
#define ADDR_DEF_OBJ_PARSE         0x00435B60u
/* docs/functions.tsv merges THREE things into the 768-byte entry here:
 * DefObjParse itself (0x00435B60, a jump table over sixteen tokens), the table
 * at 0x00435BD8, and a separate OBJ-line parser at 0x00435C20 that zeroes a
 * 56-byte record and calls DefObjParse to fill its first field.
 *
 * That matters for naming. DefObjParse's own default arm is `or eax,-1; ret`
 * and logs NOTHING; the string "DefObjParse: Bad object Constant Type" is at
 * 0x00435C4C, inside the CALLER. So the self-naming sweep attributed the name
 * through a merge, and was right only by luck -- the caller happens to name
 * the callee it just complained about. */
#define ADDR_DEF_OBJ_LINE          0x00435C20u  /* the OBJ-line parser */
/* Its sink, analogous to DefAddLink: "duplicate record in object.aai file
 * %d-%d". Still original. */
#define ADDR_DEF_ADD_OBJ_REC       0x00435980u  /* void(const int32_t *rec) */
#define AM2_DEF_OBJ_REC_DWORDS     14
#define ADDR_DEF_OBJ_REC_CAP       0x00516178u
/* Frees BOTH def tables and zeroes all six globals. Role name; three callers.
 * The counts and capacities go to zero before the frees, and the pointers
 * after, so a caller that faults inside free() still sees consistent zeros. */
#define ADDR_DEF_FREE_TABLES       0x00435E60u  /* void(void) */

/* The object-definition files (.aai) have a command vocabulary of their OWN --
 * it is not docs/scripttokens.md's. 0x5F is LINK there; in the script table 95
 * is `sniper`, which is what makes conflating the two easy and wrong. */
#define ADDR_DEF_LINK_PARSE        0x004360C0u  /* int32_t(int32_t, char *) */
#define AM2_DEF_CMD_LINK           0x5F
/* Name -> index over a table of 12-byte entries at 0x00476FE0 {name, value,
 * ptr}, -1 when absent; DefObjParse is handed the index. Role name: it names
 * itself nowhere. */
#define ADDR_DEF_NAME_INDEX        0x0041A640u  /* int32_t(const char *) */
#define ADDR_DEF_NAME_TABLE        0x00476FE0u
/* Twelve bytes an entry: {const char *name, int32_t value, void *handler}.
 * The handler is passed the entry's VALUE, not its index. Those coincide over
 * the range that matters -- entries 77..96 all have value == index -- but not
 * generally: entry 1 is "trooperlevel1" with value 45. An earlier note here
 * said "the index IS the command id", which was true where it was looked at
 * and wrong as a rule.
 *
 * Reading the table by name settles the vocabulary outright. Entries 79..94
 * are rocks, bush, trees, ground, fence, wall, bridge, barrel, building,
 * pillbox, aagun, tent, garage, radar, miscellaneous, powerups -- exactly
 * DefObjParse's sixteen tokens -- and entry 95 is literally "link". An entry
 * with an empty name ends the table. */
#define AM2_DEF_KEYWORD_STRIDE     12
#define ADDR_CRT_STRTOL            0x00465198u
/* 0x0041A5F0 is DefParseInfoFile, by its own string; 0x0041A6B0 is the line
 * dispatcher it drives, and that one names itself nowhere. Both still
 * original. */
#define ADDR_DEF_DISPATCH_LINE     0x0041A6B0u  /* int32_t(FILE *) */
#define ADDR_CRT_FGETS             0x004655B8u
#define ADDR_CRT_STRLWR           0x0046D7D6u  /* the plain ASCII _strlwr */
#define ADDR_STR_DEF_FILE_MODE     0x004779F4u  /* "rt" */
#define AM2_DEF_LINE_MAX           0x140        /* both line buffers */

/* DefGameParse, by its own "DefGameParse: Bad Game Constant Type". It is the
 * handler for twenty .aai keywords, and docs/functions.tsv merges it into the
 * 784-byte entry at 0x00424590 -- the fourth merged entry found in this work.
 *
 * Twelve of the twenty arms share ONE target, `xor eax,eax; ret`:
 * vehicle_danger, vehicle_standoff, trooper_turn_rate, trooper_pose_rate,
 * trooper_slide_rate, defense_radius, attack_radius, attack_hunt,
 * follow_radius, follow_engaged_radius, gravity and scroll_speed are parsed
 * and then DISCARDED. Only the eight roach_* constants are stored, into the
 * eight consecutive dwords at 0x00487BA8. The keywords still parse, so the
 * files are still valid; the values simply do nothing in this build. */
#define ADDR_DEF_GAME_PARSE        0x00424780u  /* int32_t(int32_t, char *) */
#define ADDR_GAME_CONSTANTS        0x00487BA8u  /* the eight roach_* dwords */
#define AM2_DEF_CMD_GAME_FIRST     0x3B
#define AM2_DEF_CMD_ROACH_FIRST    0x47
#define AM2_DEF_CMD_GAME_LAST      0x4E
/* strtol into *out, 0 on failure. Its one string is "Bad or missing number",
 * which names the condition and not the function -- 48 callers. */
#define ADDR_DEF_PARSE_NUMBER      0x0041A250u  /* int32_t(int32_t *, const char *) */
#define ADDR_DEF_SEPARATORS        0x00477A4Cu  /* " \t\n;," */
/* The link table: 20-byte records, count first. CountLinksWithParent walks it
 * comparing the parent key at +0, which is how the record's first field is
 * known to be that key. */
#define ADDR_DEF_LINKS             0x0051617Cu
#define ADDR_DEF_LINK_COUNT        0x00516180u
#define ADDR_DEF_COUNT_LINKS       0x00435FA0u  /* int32_t(int32_t parentkey) */
/* Appends one link, refusing a duplicate. Role name -- its only string is
 * "duplicate link record in object.aai file". */
#define ADDR_DEF_ADD_LINK          0x00435EE0u  /* void(const AM2_DefLink *) */
/* 0x00435FD0 names itself nowhere; this is a ROLE. After every LINK line is
 * parsed it sorts the table with ComparePair (already ours) through the CRT's
 * qsort, then walks the distinct parent keys: each is unpacked with KeyFieldA
 * and KeyFieldB -- confirming from the far side that DefLinkParse packs
 * (type, number) -- and either has its link count stored into the AAI record
 * or produces "Object AAI record not found for link %02d-%-3d". */
#define ADDR_DEF_CHECK_LINKS       0x00435FD0u  /* void(void) */
#define ADDR_DEF_FIND_OBJ_REC      0x00435AC0u  /* void *(int32,int32,int32) */
/* The object records the .aai files define: 56 bytes each, sorted, and keyed
 * on their first three dwords -- which is what CompareTriple compares and why
 * the lookup builds a 56-byte partial record as its search key. */
#define ADDR_DEF_OBJ_RECS          0x00516170u
#define ADDR_DEF_OBJ_REC_COUNT     0x00516174u
#define AM2_DEF_OBJ_REC_SIZE       0x38u
#define DEF_OBJ_REC_OFF_LINKS      0x0Cu        /* where the count is stored */
#define ADDR_CRT_QSORT             0x004660B2u
#define ADDR_CRT_BSEARCH           0x00466280u
/* The link table's capacity, and how it grows: 50 records to begin with, then
 * twenty MORE RECORDS at a time -- not twenty bytes. Both numbers are the
 * original's. */
#define ADDR_DEF_LINK_CAP          0x00516184u
#define AM2_DEF_LINK_INITIAL       0x32
#define AM2_DEF_LINK_GROW          0x14
#define ADDR_CRT_STRTOK            0x0046551Cu  /* the game's own; the state is
                                                 * shared with DefObjParse, so
                                                 * libc's would be wrong */
/* NOT DefLinkParse. 0x00436080 is a 52-byte wrapper that searches the link
 * table -- docs/functions.tsv merges the two and tools/merges.py does not
 * split them, so this address carried the wrong name until the body was read.
 * The function with the three "DefLinkParse:" strings is 0x004360C0, above. */
#define ADDR_DEF_LINK_SEARCH       0x00436080u  /* AM2_DefLink *(int32,int32) */

#define ADDR_SCRIPT_OBJECT        0x00436D60u  /* keywords 139 and 140 --
                                               * GenerateObjScriptFromTokens,
                                               * from its own error string */

/* APPENDS an object-script record and returns it -- not the accessor it looks
 * like from the call site. It grows the array at 0x00516188 twenty entries at
 * a time, zeroing the new ones, and increments the count at 0x0051618C, which
 * is the same global the attach below then reads. So the id stamped onto each
 * object is the count AFTER this call, and the record is 20 bytes.
 *
 * +0 is 0 for `object` and 1 for `objclass`; +4 is a dword name index in the
 * first case and two 16-bit class fields in the second. */
#define ADDR_NEW_OBJ_SCRIPT       0x00437130u
#define ADDR_OBJ_SCRIPTS          0x00516188u
#define ADDR_OBJ_SCRIPT_CAP       0x00516190u
#define AM2_OBJ_SCRIPT_REC_SIZE   20u

/* The object-script save section. The saver walks four levels -- script,
 * state, frame, action -- writing each record whole, and the sizes it uses
 * (0x14, 0x10, 0x14, 0x48) confirm objscript.h's struct layout independently
 * of how those structs were derived. The count doubles as a data value: it
 * goes out through WriteSaveTag, like every other length in this format. */
#define ADDR_SAVE_OBJSCRIPT_SECTION 0x00436280u  /* int32_t(FILE *) */
#define ADDR_LOAD_OBJSCRIPT_SECTION 0x004364A0u  /* int32_t(FILE *) */
/* Frees every level of the object-script table and is what the loader calls
 * first. Three callers. Reconstructed, so the loader calls it directly. */
#define ADDR_FREE_OBJ_SCRIPTS       0x004368D0u  /* void(void) */
#define ADDR_STR_OBJSCRIPT_CPP      0x0048758Cu  /* "C:\\ArmyMen2\\source\\objscript.cpp" */
#define AM2_SAVETAG_OBJSCRIPT       0x06660008u
#define AM2_OBJ_STATE_REC_SIZE      16u
#define AM2_OBJ_FRAME_REC_SIZE      20u

/* 0x00440700. Resolve a name token to its index in the name table, declaring
 * it if need be -- it reaches both ScriptFindName and AddNameTableName.
 * int32_t(ctx *, int32_t *at, int32_t *out, int32_t). */
#define ADDR_SCRIPT_RESOLVE_NAME  0x00440700u

/* 0x00436C20. One attribute statement inside an object block. The block ends
 * where ScriptIsStatementStart says the next top-level statement begins. */
#define ADDR_SCRIPT_OBJ_STATE     0x00436C20u  /* `state <name>` */
#define ADDR_SCRIPT_OBJ_FRAME     0x004369E0u  /* `frame <int> <int>` */
#define ADDR_OBJ_FRAME_NEW_ACTION 0x00437010u  /* 72-byte entries */
#define ADDR_OBJ_STATE_NEW_FRAME  0x00437070u  /* 20-byte entries */
#define ADDR_OBJ_SCRIPT_NEW_STATE 0x004370D0u  /* 16-byte entries */
#define ADDR_SCRIPT_COMPARE       0x004374F0u  /* int32_t(a, op, b) */
#define ADDR_SCRIPT_NAME_UID      0x0043F9F0u  /* int32_t(const char *) */
#define ADDR_SCRIPT_INT_OR_VAR    0x00442F80u  /* (ctx,at,&val,&isliteral) */
#define ADDR_SCRIPT_OBJECT_UID    0x0043FF00u  /* (ctx,at,&zero,&uid) */
#define ADDR_SCRIPT_ARMY_COLOUR   0x00440930u  /* int32_t(ctx,at) */
/* Not a table of its own: 0x004751B0 is ADDR_COMM_OBJECT under a second name,
 * which is how it came to look like one. The army lives in the player record
 * on the comm object, so this alias is kept only for the `this` these two
 * accessors want and says what it really is. */
#define ADDR_ARMY_TABLE           0x004751B0u  /* == ADDR_COMM_OBJECT */
#define ADDR_COMM_SLOT_FOR_ARMY   0x0040F250u  /* thiscall int32_t(this, army) */

/* Object lookup and iteration. The two iterators take no arguments: they walk
 * whatever the record at ADDR_SCRIPT_OBJ_TARGET selects, which the objclass
 * branch has just filled in. */
#define ADDR_OBJ_BY_UID           0x00427820u  /* obj *(int32_t uid) */
#define ADDR_FIRST_SCRIPT_OBJ     0x00427850u
#define ADDR_NEXT_SCRIPT_OBJ      0x00427880u
#define ADDR_OBJ_TAKES_SCRIPT     0x00433860u  /* int32_t(obj *) */

/* The object-script count, and what gets written into every object the
 * statement selects -- read AFTER ADDR_NEW_OBJ_SCRIPT has incremented it. */
#define ADDR_CURRENT_OBJ_SCRIPT   0x0051618Cu
#define AM2_OBJ_SCRIPT            0xB0u
#define AM2_OBJ_SCRIPT_PC         0xB4u
#define AM2_OBJ_SCRIPT_WAIT       0xB8u
#define AM2_OBJ_SCRIPT_STATE      0xBCu

/* The handlers describe their own statements, in their own error messages.
 * The pad handler carries "Duplicate pad name.", "Illegal Pad Number",
 * "Unexpected symbol in pad definition should be '<=>'" and, best of all,
 * "Pad can't have both specific item and generic trigger" -- so a pad is a
 * unique name, a range-checked number, and EITHER a specific item OR a
 * generic trigger, optionally with a comparison and a count. That is the
 * syntax the missions use, read out of the binary rather than inferred from
 * the examples.
 *
 * It also reports the token KIND by name when it rejects something --
 * "'%s' found, but expected token of type %s" -- which is what the array at
 * 0x00487C74 is for.
 *
 * The others do the same. `if` carries "Missing 'after' in if-statement.",
 * "Missing 'of' in if-repeat statement.", "Missing 'then' in if-statement.",
 * "Incomplete testvar clause.", "Unrecognized operator in testvar clause."
 * and "TIMEABSOLUTE time must be positive" -- with "Exptected" misspelled,
 * which is a good sign these are verbatim. `variable` carries "Duplicate
 * variable name."
 *
 * `object` carries "Invalid token in GenerateObjScriptFromTokens", which is a
 * FUNCTION NAME out of the original source -- the only one recovered so far
 * that was not inferred. */

#define ADDR_SCRIPT_CONTEXT      0x00656478u  /* what reset and parse are given */
#define ADDR_MAP_NAME            0x00511A88u  /* char[], "kitchen" */
#define ADDR_LEVEL_INDEX         0x00511D9Cu  /* int32_t; 0 means "<map>.txt" */

/* LoadLevelScript's world. */
/* Returns int32_t, not void -- 1 if it got there and 0 if it did not. The
 * declaration here said void for as long as script.cpp was the only caller
 * that mattered, because that one ignores the answer. */
#define ADDR_SET_DATA_DIR        0x00422DE0u  /* int32_t(const char *subdir) */
#define ADDR_GAME_DIR            0x0051235Cu  /* char[], the install directory */
#define ADDR_STR_AVI_DIR         0x004852C8u  /* const char **, -> "avi" */
#define ADDR_STR_PATH_SEP        0x00478984u  /* "\\" */
#define ADDR_MAP_FOLDER          0x00511AC8u  /* the map's own directory */
#define ADDR_RULES_DIR_STR       0x00485110u  /* a `char *` to "rules"    */
#define ADDR_SCORE_LIMIT         0x00515FF0u  /* seeds `gamescorelimit`   */
#define ADDR_DECLARE_RULE_VARS   0x00421C70u  /* greenwins, tanwins, ...  */

/* The five score variables' names, as `char *` in the image. Only the four
 * army ones keep the index AddNameTableName returns; `gamescorelimit`'s is
 * discarded, because scripts reach it by name and nothing updates it. */
#define ADDR_NAME_SCORE_LIMIT    0x00487C60u
#define ADDR_NAME_GREENSCORE     0x00487C64u
#define ADDR_NAME_TANSCORE       0x00487C68u
#define ADDR_NAME_BLUESCORE      0x00487C6Cu
#define ADDR_NAME_GREYSCORE      0x00487C70u
#define ADDR_SVAR_GREENSCORE     0x00656488u
#define ADDR_SVAR_TANSCORE       0x0065648Cu
#define ADDR_SVAR_BLUESCORE      0x00656490u
#define ADDR_SVAR_GREYSCORE      0x00656494u

/* One entry per player in the comm object, stride 0x70. */
#define AM2_PLAYER_STRIDE        0x70u
#define AM2_PLAYERS_MAX          4
#define AM2_PLAYER_ARMY          0x210u
#define AM2_PLAYER_ID            0x214u   /* the DirectPlay id; 0 or -1 is none */
#define AM2_PLAYER_ACTIVE        0x25Cu
#define AM2_COMM_VERBOSE         0x418u   /* gates the per-script logging */
/* This went in as ADDR_COMM_PLAYER_IS_AI, read off the one call site that
 * skips an AI script when it answers yes -- and it means the opposite. The
 * field it tests is +0x214, which is the id ADDR_COMM_FIND_PLAYER scans for,
 * so a slot answers yes while a real networked player still holds it. That
 * also explains "Player %s has left the game - now AI": losing the player
 * clears the id, and the slot becomes the AI's. Fourth time a name has been
 * taken from a call site and been wrong. */
#define ADDR_COMM_SLOT_HAS_PLAYER 0x0040F200u /* thiscall int32_t(this, slot) */
#define ADDR_MP_SCRIPT_NAME      0x00511C08u  /* char[], the multiplayer script */

/* The three names that used to live here -- GREENSCORE at 0x0065648C and so on
 * -- were shifted by one. They were read as though each store followed its own
 * call, and the stores lag: `mov [0x656488], eax` sits after the SECOND
 * AddNameTableName, so 0x656488 holds greenscore and not gamescorelimit.
 * Five names are declared and four indices kept; the one dropped is
 * gamescorelimit's. See ADDR_SVAR_GREENSCORE above.
 */

/* Token kinds, from the name array at 0x00487C74 indexed by kind. The score
 * variables are declared as kind 3, which is Integer -- so the argument that
 * looked like a magic 3 is a type. */
#define AM2_TOKEN_UNKNOWN        0
#define AM2_TOKEN_CONTROL_CHAR   1
#define AM2_TOKEN_RESERVED       2
#define AM2_TOKEN_INTEGER        3
#define AM2_TOKEN_FLOAT          4
#define AM2_TOKEN_STRING         5
#define AM2_TOKEN_NAME           6

/* Kind 7 has no entry in the kind-name array at all -- index 7 there is the
 * keyword table's first row. Nothing the tokeniser produces carries it; it is
 * written by the statement handlers, which rewrite a String naming something
 * into a reference to the name table. ScriptTokenText resolves it. */
#define AM2_TOKEN_NAMEREF        7

/* Which form an `if` took, recorded in the condition's first field. Derived
 * from the arms, not from a table -- the keyword each corresponds to is in the
 * name. */
#define AM2_IF_PLAIN             0
#define AM2_IF_ALLOF             1
#define AM2_IF_INORDER           2
#define AM2_IF_COUNT             3
#define AM2_IF_REPEAT            4
#define AM2_IF_TIMEABSOLUTE      5
#define AM2_IF_AFTER             6
#define AM2_IF_BUTNOT_KEYWORD    7
#define AM2_IF_BUTNOT_STRING     8

/* Which of the two forms an object script was declared with. */
#define AM2_OBJSCRIPT_OBJECT     0
#define AM2_OBJSCRIPT_CLASS      1

/* What an `if` event term is, in the first of its three values.
 *
 * The whole enum is confirmed twice over: the parser assigns these codes, and
 * 0x0041F200 formats a placeholder name per kind and its strings line up one
 * for one -- Event_PadDeactivated for 2, Event_PadActivated for 3, and so on
 * down to Event_ItemDropped for 8. Two functions written for different
 * purposes agreeing is better evidence than either alone.
 *
 * Kind 0 was AM2_EVT_NAME here, inferred. The game's own string calls it
 * Event_Control, so it is AM2_EVT_CONTROL now -- the program's word beats
 * ours. Kind 1 is produced by nothing, and 0x0041F200 gives it an empty name
 * rather than a placeholder, which is the same fact from the other side. */
#define AM2_EVT_CONTROL          0   /* just an object, whatever happens  */
#define AM2_EVT_PADOFF           2
#define AM2_EVT_PADON            3
#define AM2_EVT_KILLED           4
#define AM2_EVT_HIT              5
#define AM2_EVT_HEALED           6
#define AM2_EVT_PICKEDUP         7
#define AM2_EVT_DROPPED          8

/* What one side of a `testvar` comparison is. */
#define AM2_VAL_LITERAL          0
#define AM2_VAL_VARIABLE         1
#define AM2_VAL_GETDMGLVL        2
#define AM2_VAL_GETHEALTH        3
#define AM2_VAL_GETDISGUISE      4
#define AM2_VAL_HASITEM          5
#define AM2_VAL_ISCOLORINGAME    6
#define AM2_VAL_ISALLY           7
#define AM2_VAL_TEAMSCORE        8

/* Who an `order` or `setaimode` is aimed at. */
#define AM2_ORDER_NAME           0   /* a named object                    */
#define AM2_ORDER_ARMY           1   /* a whole army                      */
#define AM2_ORDER_GROUP          2   /* an army's `group <n>`             */

/* What follows `then`. */
#define AM2_THEN_NONE            0
#define AM2_THEN_RANDOM          1
#define AM2_THEN_SEQUENTIAL      2
#define AM2_THEN_ONOBJSTATE      3

/* A `testvar` comparison operator. Note this is NOT the encoding a pad uses
 * for the same three of these -- a pad writes 0/1/2 for =/</> while testvar
 * writes 0/2/3, with 1 taken by <>. Two encodings for one idea, in one file. */
#define AM2_CMP_EQ               0
#define AM2_CMP_NE               1
#define AM2_CMP_LT               2
#define AM2_CMP_GT               3
#define AM2_CMP_LE               4
#define AM2_CMP_GE               5

/* A pad's comparison, and what ScriptCompare takes. */
#define AM2_PADCMP_EQ            0
#define AM2_PADCMP_LT            1
#define AM2_PADCMP_GT            2

/* Who a `hit`, `killed`, `healed`, `pickedup` or `dropped` event is about,
 * packed into the top bits of one value by ScriptHitTarget.
 *
 * The army bits are one each. The class bits are not: `sarge` is three bits
 * and `trooper` is two of those same three, so a trooper mask is a subset of a
 * sarge mask -- the scheme is a hierarchy rather than a set of flags, which is
 * why `sarge` matching implies `trooper` matching and not the other way round.
 *
 * ALL_ARMIES is what an unrecognised army word gives, and it is exactly the
 * base bit with all four armies OR'd in. */
#define AM2_HIT_ANY          0x80000000u
#define AM2_HIT_GREEN        0x40000000u
#define AM2_HIT_TAN          0x20000000u
#define AM2_HIT_BLUE         0x10000000u
#define AM2_HIT_GREY         0x08000000u
#define AM2_HIT_ALL_ARMIES   0xF8000000u
#define AM2_HIT_ITEM         0x04000000u
#define AM2_HIT_SARGE        0x01C00000u
#define AM2_HIT_TROOPER      0x01400000u
#define AM2_HIT_VEHICLE      0x00200000u

/* What sets a pad off, in the pad record's trigger field. A different and
 * unrelated encoding from the hit masks above -- low bits, one per class, and
 * the armies sit in the middle of the class bits rather than above them. */
#define AM2_PADTRIG_EVERYTHING   0x0001u
#define AM2_PADTRIG_SARGE        0x0002u
#define AM2_PADTRIG_UNIT         0x0004u
#define AM2_PADTRIG_TROOPER      0x0008u
#define AM2_PADTRIG_TANK         0x0010u
#define AM2_PADTRIG_VEHICLE      0x0020u
#define AM2_PADTRIG_GREEN        0x0040u
#define AM2_PADTRIG_TAN          0x0080u
#define AM2_PADTRIG_BLUE         0x0100u
#define AM2_PADTRIG_GREY         0x0200u
#define AM2_PADTRIG_CONVOY       0x0400u
#define AM2_PADTRIG_BOAT         0x0800u
#define AM2_PADTRIG_GROUNDVEH    0x1000u
#define AM2_PADTRIG_NPC          0x2000u
#define ADDR_MENU_REQUEST_TAKEN  0x00511DBCu  /* int32_t, the consumed code */

#define ADDR_HINSTANCE           0x00512580u  /* HINSTANCE */
/* Not HINSTANCE-related at all, despite sitting beside it: DetectCpuSpeed sets
 * it, and it means "this machine is fast enough". WinMain clears it first. */
#define ADDR_FAST_MACHINE        0x00512584u  /* int32_t */
#define ADDR_HWND                0x0051245Cu  /* HWND, the one game window */
/* Both DirectPlay enumerations pass this same handle as their lpContext, and
 * CommSend posts to it -- so an "lpContext" in this game is the window. */
#define ADDR_APP_MUTEX           0x004FA034u  /* HANDLE "ArmyMenMutex" */
#define ADDR_LAST_MESSAGE        0x004F9FE4u  /* uint32_t, last dispatched message */
#define ADDR_SCREEN_W            0x004852D8u  /* int32_t */
#define ADDR_SCREEN_H            0x004852DCu  /* int32_t */
/* AM2_Rect. ADDR_ORIGIN_DX and ADDR_ORIGIN_DY above are its first two members:
 * PositionWindow writes all four, as the client area in screen coordinates
 * when windowed and as (0,0,w,h) when not. */
#define ADDR_SCREEN_RECT         0x00485330u

/* Command-line switches, all set to 1 when the flag is present. Recovered from
 * WinMain's strstr chain; three are developer names. */
#define ADDR_OPT_WINDOWED        0x00507344u  /* -w */
#define ADDR_OPT_NO_INTRO        0x004FA038u  /* -nointro */
#define ADDR_OPT_TRACE_PF        0x0050C35Cu  /* -tracePF */

/* What -tracePF traces: the region graph, which is this game's pathfinding
 * structure. The switch is the evidence for that -- the region log lines are
 * gated on it and nothing else is.
 *
 * ADDR_REGION_OF_CELL is one byte per map cell giving its region id, where 0
 * means "no region". ADDR_REGIONS is an array of 44-byte records; the two
 * fields reached here are a link COUNT at +8, a byte, and a pointer to the
 * links at +0x0C. A link is six bytes. */
#define ADDR_REGION_OF_CELL        0x00514ECCu  /* uint8_t * */
#define ADDR_REGIONS               0x00514EF0u
#define AM2_REGION_SIZE            44
#define REGION_OFF_NLINKS          8u   /* uint8_t */
#define REGION_OFF_LINKS           0x0Cu
#define ADDR_ADD_REGION_LINK       0x0042B860u  /* void(int32_t, int32_t) */
#define ADDR_OPT_TRACE_VEH       0x0050C360u  /* -traceVEH */
#define ADDR_OPT_TRACE_WIN       0x0050C354u  /* -tracewin */
#define ADDR_OPT_DBG             0x0050C358u  /* -dbg */
#define ADDR_OPT_ROB             0x004FD73Cu  /* -rob; same as ADDR_DEBUG_ITEMLIST */
#define ADDR_OPT_PETER           0x004FD744u  /* -peter */
#define ADDR_OPT_DAN             0x004FD740u  /* -dan */
#define ADDR_OPT_DF              0x0047894Cu  /* -df clears it; 1 by default */
#define ADDR_OPT_MUSIC           0x005125C4u  /* -bm sets, -sm clears */
#define ADDR_OPT_NM              0x0051259Cu  /* -nm */
#define ADDR_OPT_MAP_NAME        0x004F9FECu  /* char[], the text after -map: */
/* The comm subsystem object; -debugComm, -traceComm and -logComm set flags
 * inside it rather than in globals of their own. */
#define ADDR_COMM_OBJECT         0x004751B0u  /* void ** */
/* Reset to 0 before an enumeration and used by the callback as the slot it
 * fills next, so after EnumPlayers returns it is how many were found. */
#define ADDR_COMM_ENUM_COUNT     0x004751B4u  /* int32_t */
/* EnumPlayers' DPENUMPLAYERSCALLBACK2. Left original -- it is the other side
 * of the same enumeration and touches no import. */
#define ADDR_ENUM_PLAYERS_CB     0x0040E0B0u
#define ADDR_COMM_ENUM_PLAYERS   0x0040E200u  /* int32_t(void) */
/* A player slot's index field, which the session carries as dwUser1. 0x63
 * is what a reset slot holds -- no player. */
#define COMM_SLOT_OFF_INDEX      0x004u
#define COMM_SLOT_INDEX_NONE     0x63u

/* The menu's "do this next" pair. StartSelectedGame writes a request code into
 * the first and raises the flag in the second; the menu loop acts on it. The
 * codes seen so far are 1 (refused), 0xA (joined a session) and 0xB (start a
 * local game). */
#define ADDR_MENU_REQUEST        0x00511DC8u  /* int32_t, the code */
#define ADDR_MENU_MODE           0x00511DBCu  /* int32_t; 0x21 = back to play */
#define ADDR_OVERLAY_DIRTY       0x00511DC0u  /* int32_t; the primary needs saving */
#define ADDR_DRAW_MENU_OVERLAY   0x00425AF0u  /* void(void) */
#define ADDR_OVERLAY_PREPARE     0x00412D30u  /* void(int32, int32) */
#define MENU_MODE_PLAYING        0x21
/* The multiplayer session object and the two routines either side of it. */
#define ADDR_SESSION_OBJECT      0x00516130u  /* void *, made on demand */
#define ADDR_SESSION_CTOR        0x00453910u  /* thiscall void(this, int32) */
#define ADDR_SESSION_RESET       0x00453940u  /* thiscall void(this) */
/* Fills a 0x50-byte DPSESSIONDESC2 -- the app GUID from the comm object lands
 * at +0x18, which is guidApplication -- and asks DirectPlay to enumerate the
 * sessions matching it. Slot 13 is EnumSessions, not Open; it was briefly
 * ADDR_COMM_OPEN_SESSION on the strength of the descriptor alone, before the
 * slot was counted. Invisible to tools/comcalls.py because the interface lives
 * inside the comm object rather than in a global. */
#define ADDR_COMM_ENUM_SESSIONS  0x0040E3B0u  /* thiscall int32(this, void *) */
#define ADDR_COMM_JOIN_SESSION   0x0040E7B0u  /* thiscall int32(this, const GUID *) */
#define ADDR_COMM_RECEIVE        0x0040E8A0u  /* thiscall int32(this,from,to,flags,buf,len) */
#define ADDR_COMM_SLOT_OF_ID     0x0040F160u  /* thiscall int32(this, DPID) */
#define ADDR_STR_FLOW_UNPAUSE    0x00473A84u  /* "FLOW UNPAUSE nfree = %d\n" */
/* Receive keeps its own statistics, laid out like the send side's. */
#define COMM_OFF_RX_INDEX        0x008u
#define COMM_OFF_RX_TIMES        0x0FCu   /* uint32[30] */
#define COMM_OFF_RX_SIZES        0x174u   /* uint32[30] */
#define COMM_OFF_RX_MAX          0x1ECu
#define COMM_OFF_RX_BYTES        0x1F4u
#define COMM_OFF_RX_PACKETS      0x1FCu
#define COMM_SLOT_OFF_HEARD      0x060u   /* GetTickCount of the last packet */
#define COMM_FLOW_PAUSED_BIT     0x8000u
#define COMM_FLOW_FREE_OK        0x12C    /* 300 free entries is enough */
#define COMM_UNACKED_CLEAR       0x0F     /* alarm clears below this */
#define COMM_MSG_TYPE_ACK        0x0B
#define ADDR_COMM_SEND_PROPERTY  0x0040FAA0u  /* thiscall int32(this, uint32) */
#define ADDR_GUID_NULL           0x0046FD98u  /* all zeroes, as guidPlayer */
#define ADDR_GUID_GAME_PROPERTY  0x0046F888u  /* {BDD4B95F-D35C-11D0-...} */
#define ADDR_STR_CREATE_PLAYER_FAIL 0x00475248u
#define ADDR_STR_NUM_PLAYERS     0x00475230u
#define ADDR_STR_OPEN_FAILED     0x00475410u  /* " Open Session Failed returned %x \n" */
#define ADDR_ENUM_SESSIONS_CB    0x0040E280u  /* LPDPENUMSESSIONSCALLBACK2 */
/* The service-provider browser and its callback. The callback drops two
 * providers by name before adding the rest -- "Play on HEAT" and "Play on
 * Mplayer", both matchmaking services that no longer exist -- and its `ret 0x18`
 * matches LPDPENUMCONNECTIONSCALLBACK's six arguments exactly. */
#define ADDR_COMM_ENUM_CONNECTIONS 0x0040E530u /* thiscall int32(this, void *) */
#define ADDR_ENUM_CONNECTIONS_CB 0x0040E460u  /* LPDPENUMCONNECTIONSCALLBACK */
#define ADDR_CONNECTION_LIST     0x004FA900u  /* void *, what that callback fills */
#define ADDR_HOST_SLOT           0x004FA904u  /* int32_t, which player slot hosts */
#define ADDR_JOIN_CONTEXT        0x004751B4u  /* int32_t, cleared once a session exists */
/* Creates the DirectPlay session -- slot 24 is Open, with DPOPEN_CREATE. */
#define ADDR_COMM_OPEN_SESSION   0x0040DFC0u  /* thiscall int32(this, const char *) */
/* Appends one named entry to a list object. 16 callers. */
#define ADDR_LIST_ADD            0x00453A30u  /* thiscall void(this, const char *, void *) */
#define ADDR_STR_COMPUTER_ONLY   0x00475300u  /* "Play Against Computer Only" */

/* The packet transmit and the three helpers its watchdog uses. */
#define ADDR_COMM_SEND           0x0040EB70u  /* thiscall int32(this,id,flags,buf,len) */
/* Kept as PLAYER rather than renamed to FLOWQ, having checked both. The two
 * accessors below call what this returns a "Flowq" -- "No Flowq for %X" -- and
 * so does 0x004014C0 ("Interrupt Level Can't find FlowQ for %x"). But CommSend
 * logs "DPLAY ERROR: INVALID PLAYER IN SEND TO ID %x" for the very same id, so
 * both words are the program's own: it is a player's record, and the
 * flow-control code calls that record a FlowQ. One thing, two vocabularies,
 * and no reason to prefer either name.
 *
 * It scans six 0x7E0-byte records at 0x004F1980 and does NOT stop at the first
 * match -- eax is overwritten each time -- so the LAST match wins. */
#define ADDR_FIND_PLAYER_BY_ID   0x00402990u  /* void *(uint32 id); NULL when unknown */

/* Two masks out of that record, each named by its own error message. */
#define ADDR_GET_PLAYER_MASK     0x00402BD0u  /* uint32_t(uint32_t id) -- +0x14 */
#define ADDR_GET_RESEND_MASK     0x00402C00u  /* uint32_t(uint32_t id) -- +0x18 */

/* The comm layer's outgoing message hub, named by its own
 * "SendGameMsg, first message to %x, hehas set to %d". 928 bytes and 14
 * callers, so it is a hub rather than a helper; two of those callers are
 * reconstructed below and reach it through here. Among its other messages is
 * "Error Send can't find Flow for Player %x", which is the same player/FlowQ
 * synonym ADDR_FIND_PLAYER_BY_ID records. */
#define ADDR_SEND_GAME_MSG       0x004022D0u  /* int32_t(void *msg, int32, int32) */

/* Two static message records in .bss -- zero at load, filled in at 0x0040FE04
 * and 0x0040FE14 -- each with the value the sender writes at +8. */
#define ADDR_MSG_COLOR           0x004FC898u
#define ADDR_MSG_TEAM            0x004FC8A8u

/* The two senders themselves. */
#define ADDR_SEND_COLOR_MSG      0x004119C0u  /* void(int32_t colour) */
#define ADDR_SEND_TEAM_MSG       0x00411AC0u  /* void(int32_t team) */
#define ADDR_GET_PAUSE_FLAGS     0x00426840u  /* uint32(void) */
#define ADDR_STR_SEND_BADPLAYER  0x004754ACu
#define ADDR_STR_SEND_BADPARAM   0x00475478u
#define ADDR_STR_SEND_NOENTRY    0x00475450u
/* Comm object fields the send path uses. Slots are at COMM_SLOT_BASE with
 * stride 0x70; +0x08 is the player id and +0x58 the unacknowledged counter. */
#define COMM_OFF_STAT_INDEX      0x004u   /* ring cursor, 0..29 */
#define COMM_OFF_STAT_TIMES      0x00Cu   /* uint32[30] */
#define COMM_OFF_STAT_SIZES      0x084u   /* uint32[30] */
#define COMM_OFF_STAT_MAX        0x1F0u
#define COMM_OFF_STAT_BYTES      0x1F8u
#define COMM_OFF_STAT_PACKETS    0x200u
#define COMM_OFF_OUR_PLAYER_ID   0x3CCu
#define COMM_OFF_PLAYER_COUNT    0x3D0u
#define COMM_OFF_LOCAL           0x400u   /* set when the game is offline */
#define COMM_OFF_READY           0x3D8u
#define COMM_SLOT_OFF_NAME       0x00Cu   /* 0x40-byte string; CommConstruct
                                           * clears it, StartSelectedGame writes
                                           * "Computer%d" into it */
#define COMM_SLOT_OFF_ID         0x008u
#define COMM_SLOT_OFF_UNACKED    0x058u
#define COMM_STAT_RING           30u
#define ADDR_SESSION_LIST        0x004FA908u  /* void *, what the callback fills */
#define COMM_OFF_APP_GUID        0x3D4u       /* GUID *, set by CommConstruct */
/* Calls ADDR_SESSION_RESET on the object at 0x0051612C when there is one. */
#define ADDR_DROP_OBJ_51612C     0x00431D70u  /* void(void) */
#define ADDR_GAME_OPERATOR_NEW   0x00464900u  /* void *(size_t); MSVC operator new */
#define ADDR_START_MULTIPLAYER   0x0042F310u  /* void(void), a button handler */
#define ADDR_MP_DATA_PROBE       0x0048700Cu  /* "data\\mpalpine" */
#define ADDR_DATA_MISSING_TEXT   0x00486FA4u  /* "...multi-player with a compact installation." */
#define ADDR_DATA_MISSING_CAPTION 0x00486FFCu /* "Data Missing" */
#define ADDR_MENU_REQUEST_SET    0x00511DC4u  /* int32_t, non-zero when one is pending */

/* Copies the pending settings block at 0x00516xxx over the active one at
 * 0x00515Fxx, resets the comm slots and copies the two player-name strings.
 * Named for what its body does; stays original. */
#define ADDR_APPLY_GAME_SETTINGS 0x0042F170u  /* void(void) */
#define ADDR_FMT_COMPUTER_N      0x00486EC4u  /* "Computer%d" */
#define ADDR_START_SELECTED_GAME 0x0042ECF0u  /* void(void), a button handler */
#define COMM_OFF_DEBUG           0x418u
#define COMM_OFF_TRACE           0x470u
#define COMM_OFF_LOG             0x474u

/* Startup and shutdown steps WinMain drives. Named where the imports or the
 * COM calls inside them say what they are, left at their address where they do
 * not -- these are pure game logic and stay in the original image. */
#define ADDR_CHECK_BASE_PATH     0x00422DB0u  /* getcwd, complains past 255 chars */
#define ADDR_DETECT_CPU_SPEED    0x0040B2B0u  /* void(void); logs "system speed" */
#define ADDR_SLOW_MACHINE        0x005125C0u  /* int32_t, the inverse of the above */
#define ADDR_WINCPUID_FN         0x004F9FE0u  /* cached cpuinf32.dll exports */
#define ADDR_CPUNORMSPEED_FN     0x004F9FE8u
#define ADDR_INIT_TIMER          0x00426C50u  /* QueryPerformanceFrequency/Counter */
/* Variadic, and always returns 0 -- which is why both device bring-up routines
 * can `return ReportError(...)` and mean "failed". */
#define ADDR_REPORT_ERROR        0x0041E7A0u  /* int32_t(HRESULT, const char *fmt, ...) */

/* ---- device bring-up --------------------------------------------------
 *
 * The two functions that create every DirectDraw and DirectInput object the
 * game owns. Both are reconstructed in src/game/win32/device.cpp.
 *
 * They call DirectDrawCreate and DirectInputCreateA through the game's own
 * import thunks rather than through ours. For DirectDraw that is only tidiness;
 * for DirectInput it is required, because src/inject/dinput_hook.c works by
 * patching the game's IAT slot, and an import of our own would walk straight
 * past the hook and take the harness's input injection with it.
 */
#define ADDR_INIT_INPUT          0x00426D30u  /* int32_t(HWND); 1 on success */
#define ADDR_INIT_DIRECTDRAW     0x0041AA10u  /* HRESULT(HWND); 0 on success */
#define ADDR_DIRECTDRAWCREATE    0x00463396u  /* jmp [0x0046F00C] */
#define ADDR_DIRECTINPUTCREATE   0x00464410u  /* jmp [0x0046F014] -- the hooked slot */

/* DirectDraw. The game holds both interface generations: it queries v2 off v1
 * and then uses whichever one has the SetDisplayMode it wants, three arguments
 * on v1 and five on v2. */
#define ADDR_DIRECTDRAW2         0x004FE098u  /* IDirectDraw2 * */
#define ADDR_IID_DIRECTDRAW2     0x0046F338u  /* the game's own copy of the IID */
#define ADDR_PIXEL_FORMAT_BYTE   0x00502AD9u  /* uint8_t, passed to AttachPalette */
/* Both reconstructed in src/game/win32/surface.cpp.
 *
 * ClearSurface was called ADDR_ATTACH_PALETTE for one commit, guessed from its
 * call site in InitDirectDraw. It is nothing of the kind: vtable slot 5 is Blt,
 * and it is a colour fill. */
#define ADDR_CREATE_OFFSCREEN    0x0041B850u  /* surface *(w, h, caps, int32 key) */
#define ADDR_CLEAR_SURFACE       0x0041AD30u  /* int32_t(surface *, uint32_t colour) */
#define ADDR_CLEAR_REGION        0x0041CE20u  /* void(const RECT *, uint8_t) */

/* DirectInput. The GUIDs and data formats are the game's own copies in .rdata,
 * so nothing here needs dxguid. */
#define ADDR_DINPUT              0x00512FD0u  /* IDirectInputA * */
#define ADDR_DI_MOUSE            0x00512FD4u  /* IDirectInputDeviceA * */
#define ADDR_DI_KEYBOARD         0x00512FD8u  /* IDirectInputDeviceA * */
#define ADDR_DI_DEVICE_3         0x00512FDCu  /* a third device, never created here */
#define ADDR_DI_MOUSE_ACQUIRED   0x00512FE0u  /* int32_t */
#define ADDR_SHUTDOWN_INPUT      0x00426EA0u  /* void(void) */
#define ADDR_ACQUIRE_MOUSE       0x00426F20u  /* void(void) */
#define ADDR_GUID_SYS_MOUSE      0x0046F5A8u
#define ADDR_GUID_SYS_KEYBOARD   0x0046F5B8u
#define ADDR_DF_MOUSE            0x0046FD80u  /* DIDATAFORMAT c_dfDIMouse */
#define ADDR_DF_KEYBOARD         0x0046FD68u  /* DIDATAFORMAT c_dfDIKeyboard */
#define ADDR_DIPROP_BUFFER_SIZE  0x004854F8u  /* DIPROPDWORD, the buffered-input size */
/* The keyboard's double buffer: two 256-byte state arrays and the two pointers
 * PollKeyboard SWAPS each poll, so which array is current alternates and the
 * pointers are the only way to know. Nothing to do with the mouse cursor,
 * which is what the names they went in under -- ADDR_KEYS_NOW_PTR and _B --
 * read as; "cursor" meant a cursor into a buffer, and next to ADDR_CURSOR_X
 * that is a trap. Renamed, not aliased. */
#define ADDR_KEYS_NOW_PTR        0x005127C8u  /* uint8_t *, this poll */
#define ADDR_KEYS_PREV_PTR       0x005127CCu  /* uint8_t *, the one before */
#define ADDR_KEYS_BUFFER_A       0x005125C8u  /* uint8_t[256] */
#define ADDR_KEYS_BUFFER_B       0x005126C8u
#define AM2_KEY_STATES           256
#define AM2_KEY_DOWN             0x80u        /* DirectInput's down bit */
/* Auto-repeat state, one entry per DIK scancode. PollKeyboard writes both and
 * 0x00427430 reads the second -- `KeyPressed(dik)` is `g_keyPressed[dik & 0xff]`,
 * which is how the array's length and purpose were established. */
#define ADDR_KEY_REPEAT_AT       0x005127D0u  /* uint32_t[256], GetTickCount due */
#define ADDR_KEY_PRESSED         0x00512BD0u  /* int32_t[256] */
/* Mouse state, all written by PollMouse and read by the menus and the game.
 * The deltas are cleared at the top of every poll and the buffered events
 * accumulated into them; 0x00426F40 turns them into an absolute cursor. */
#define ADDR_MOUSE_DX            0x00485458u  /* int32_t, this poll */
#define ADDR_MOUSE_DY            0x0048545Cu  /* int32_t */
#define ADDR_MOUSE_DZ            0x00485460u  /* int32_t, the wheel */
#define ADDR_MOUSE_BUTTON        0x00485470u  /* int32_t[3], 1 while down */
#define ADDR_MOUSE_CHANGED       0x0048547Cu  /* int32_t[3], differs from last */
/* int32_t[3]. Menu code sets one when it takes responsibility for a click
 * (0x004142C8); PollMouse clears it when the button comes back up. */
#define ADDR_MOUSE_CLAIMED       0x00485488u
/* Not only movement, despite the name it went in under: 0x00426F40 also sets
 * it when button 0 or button 1 CHANGES, so it is "the mouse did something". */
#define ADDR_MOUSE_MOVED         0x00485494u  /* int32_t */
#define ADDR_CURSOR_POINT        0x0048546Cu  /* two int16 -- the clamped
                                               * cursor, x then y, which is
                                               * what a press records */
/* Three {point, tick} pairs, one per button, stamped when that button goes
 * down. The point is the packed dword above, not a pair of ints. */
#define ADDR_MOUSE_PRESS         0x00485498u
#define ADDR_MOUSE_ACTIVITY      0x004854B0u  /* set from ADDR_INPUT_CONTEXT on
                                               * any movement or button change */
#define ADDR_MOUSE_B0_EXTRA      0x004854B4u  /* zeroed when button 0 goes down;
                                               * nothing here reads it */
/* Read from 157 places and written from three, all of them in the state
 * machine -- ADDR_TAKE_MENU_REQUEST is one. What it MEANS is not established;
 * this records only that the mouse stamps it whenever there is input.
 *
 * UpdateObjectScript adds a datum and rules a reading OUT. It skips an object
 * while `obj[0xBC] >= this` and on advancing sets `obj[0xBC] = frame->a +
 * this`, which reads exactly like a deadline against a rising clock -- so that
 * is what went in here first. A live probe says no: it holds 0x1F4 (500) for
 * twelve seconds of Boot Camp play while ComposeFrame climbs and
 * UpdateObjectScript runs 177,370 times. It does not tick.
 *
 * So it is a state-scoped VALUE the object-script timing is measured against,
 * consistent with "written from three places in the state machine". Still not
 * established, and now with one fewer candidate. */
#define ADDR_INPUT_CONTEXT       0x00511E04u
#define ADDR_MOUSE_EVENT         0x00426F40u  /* void(void), after every event */
/* PollInput is `call PollMouse; jmp PollKeyboard` and nothing else. */
#define ADDR_POLL_INPUT          0x00427420u  /* void(void) */
#define ADDR_POLL_MOUSE          0x00427070u  /* void(void) */
#define ADDR_POLL_KEYBOARD       0x004272D0u  /* void(void) */
/* The globals and literals the WinMain chain touches. Read out of the bodies
 * rather than guessed; see src/game/win32/winmain.cpp for what each does. */
#define ADDR_STR_BASE_PATH_LONG  0x00478954u  /* "The base path is longer..." */
#define ADDR_PERF_FREQ           0x00512350u  /* int64, QueryPerformanceFrequency */
#define ADDR_PERF_START          0x00512578u  /* int64, the startup counter */
#define ADDR_PERF_PERIOD         0x00512570u  /* double, milliseconds per tick */
#define ADDR_PERF_WORD_A         0x00512568u  /* three int16 cleared with it */
#define ADDR_PERF_WORD_B         0x0051256Au
#define ADDR_PERF_WORD_C         0x0051256Cu
#define ADDR_DBL_MS_PER_SEC      0x0046F990u  /* 1000.0 */
#define ADDR_DBL_MAX_PERIOD      0x0046F988u  /* 1.0 -- worse than 1 kHz loses */
#define ADDR_STR_HIGH_PERF       0x00485438u  /* "Using High Performance Counter\n" */
#define ADDR_APP_MUTEX           0x004FA034u  /* HANDLE, ArmyMenMutex */
#define ADDR_SHUTDOWN_OBJ        0x00512308u  /* the `this` of the first teardown */
#define ADDR_FREE_SPRITE_LIST    0x004098B0u  /* what the alias jumps to */
#define ADDR_MAP_NAME_DEFAULT    0x00485108u  /* const char **, used when empty */
#define ADDR_MP_SCRIPT_DEFAULT   0x0048510Cu  /* const char ** */
#define ADDR_SPRITE_SET_LOAD     0x004239B0u  /* void(const char *) */
#define ADDR_SPRITE_SET_FREE     0x00423970u  /* void(void *set) */
#define ADDR_SPRITE_SET_TITLE    0x00510A40u
#define ADDR_SPRITE_SET_SHARED   0x00510230u
#define ADDR_SPRITE_SET_THIRD    0x00511250u
#define ADDR_STR_SET_TITLE       0x00478AC0u  /* "title" */
#define ADDR_STR_SET_SHARED      0x00478AACu  /* "shared" */
/* Renamed from ADDR_FILL_PALETTE, which was invented -- no such string exists
 * anywhere in the image. The body builds a 3-3-2 palette into the caller's
 * buffer: PALETTEENTRY quads at +0 and COLORREFs at +0x400, 256 of each. */
#define ADDR_BUILD_RGB332        0x0041ADE0u  /* void(void *out) */
#define ADDR_GAME_OVER_STATE     0x00515F88u  /* int32, cleared at startup */
#define ADDR_FRAME_PRE           0x0040AF70u  /* before the state handler */
#define ADDR_STATE_ENTERED       0x00511DA8u  /* int32; set on a transition, and
                                               * the handler clears it after it
                                               * has run the entry action once */
#define ADDR_STATE0_TICK         0x00511A60u  /* GetTickCount at state 0 entry */
#define ADDR_STATE_ENTER_ONCE    0x00511DD0u  /* int32; state 2 only, and it
                                               * RETURNS after clearing it */
#define AM2_SUBSTATE_BASE        22           /* what the 13-entry table at
                                               * 0x00426230 is indexed from */
#define ADDR_SUBSTATE_TABLE      0x00426230u
/* The frame chain's own callees, all still original. */
#define ADDR_COMM_FRAME_PRE_A    0x00411C20u  /* "TIMING OUT PLAYER" */
/* Its own log strings call it ArmyMessageFlush, so it is renamed rather than
 * aliased -- the old name came from the one call site in RunFrame, and knowing
 * the body means knowing what that step IS: the frame's post-work flushes the
 * outgoing message packet. ArmyMessageSend calls it too, whenever the packet
 * fills. Returns zero when it could not send. */
#define ADDR_ARMY_MESSAGE_FLUSH  0x00410420u  /* int32_t(int32_t) */
#define ADDR_COMM_FRAME_POST_B   0x00402F50u
#define ADDR_COMM_FRAME_POST_C   0x00403050u
#define AM2_COMM_MIN_BUFFERS     10           /* below this, COMM ERROR: NO BUFFERS */
#define AM2_COMM_OFF_ACTIVE      0x3DCu       /* gates all the comm frame work */
#define ADDR_STATE_LEAVE_COMMON  0x00426640u  /* states 0 and 3 tail-jump here */
#define ADDR_STATE_FRAME_COMMON  0x00426650u  /* and then here */
#define ADDR_STATE0_ENTER        0x004265F0u
#define ADDR_STATE3_ENTER        0x004266F0u
#define ADDR_STATE1_LEAVE        0x004263E0u
#define ADDR_STATE1_ENTER        0x004262E0u
#define ADDR_STATE1_MENU         0x00426400u
#define ADDR_STATE1_COMMON       0x00426270u
#define ADDR_MOVIE_FRAME_STEP    0x00445630u  /* states 0 and 3, per frame */
#define ADDR_STATE2_ENTER        0x00425300u
#define ADDR_SUBSTATE22          0x00425C10u
#define ADDR_SUBSTATE33_ALT      0x00425CD0u
#define ADDR_EVENT_FLAG_8_TEST   0x00424900u
/* Its own log string calls it SendGamePause, so it is renamed rather than
 * aliased -- the old name came from this one call site. Knowing the body makes
 * the call site legible: frame.cpp passes (0, AM2_EVENT_FLAG_8), which is
 * "un-pause, reason 8" told to the other players. CLAUDE.md already records
 * that the event flags ARE the pause mask; this is the send half. */
#define ADDR_SEND_GAME_PAUSE     0x00410820u  /* void(int32 pause, int32 mask) */
#define AM2_EVENT_FLAG_8         8
#define ADDR_FRAME_POST          0x0040AFA0u  /* after it, reached by tail jump */
#define ADDR_STATE0_FRAME        0x004266B0u
#define ADDR_STATE1_FRAME        0x00426570u
#define ADDR_STATE3_FRAME        0x00426760u
#define ADDR_STATE4_FRAME        0x00426790u
#define ADDR_COMM_OFF_LOBBY      0x3FCu       /* on the comm object */
#define ADDR_COMM_OFF_SKIP_INTRO 0x3F8u
#define ADDR_SET_GAME_OVER       0x0042E5A0u  /* void(int32) */
#define ADDR_LEAK_COUNT          0x0050C344u  /* int32 */
#define ADDR_LEAK_TOTAL          0x0050C340u  /* int32 */
#define ADDR_LEAK_RECORDS        0x0050C348u  /* the 16-byte records */
#define ADDR_STR_LEAK_HEADER     0x0047834Cu  /* "Unreleased memory (%d) blocks:\n" */
#define ADDR_STR_LEAK_ROW        0x0047832Cu  /* "%08d bytes  file: %s  line: %d\n" */
#define ADDR_CRT_STRNCPY         0x00465610u

/* The trig tables and the constants that build them. The store index runs one
 * ahead of the loop counter in the original, so the cos table really starts at
 * 0x00515784 and not at the 0x00515780 the first fstp encodes. */
#define ADDR_TRIG_COS            0x00515784u  /* float[256] */
#define ADDR_TRIG_SIN            0x00514F80u  /* float[256], scaled by -0.85 */
/* These two are the CENTRES, not the starts: the original indexes them with a
 * signed ratio running -512..512, so the table occupies base-512..base+512.
 * Taking them for starts puts each one 512 bytes late, and the sin one then
 * lands on top of half the cos table -- which is how it was found. The four
 * are contiguous and in this order: atanS 0x00515380, cos 0x00515784,
 * atanC 0x00515B84, with sin at 0x00514F80 ending exactly where atanS begins. */
#define ADDR_TRIG_ATAN_COS       0x00515D84u  /* int8[1025] centred here */
#define ADDR_TRIG_ATAN_SIN       0x00515580u  /* int8[1025] centred here */
#define AM2_TRIG_ATAN_RANGE      512
#define ADDR_DBL_TWO_PI          0x0046F9C8u  /* 6.283185307 */
#define ADDR_DBL_ONE_256         0x0046F9C0u  /* 0.00390625 */
#define ADDR_DBL_SIN_SCALE       0x0046F9B8u  /* -0.85, the isometric squash */
#define ADDR_DBL_512             0x0046F9B0u  /* 512.0 */
#define ADDR_DBL_ZERO            0x0046F920u  /* 0.0 */

#define ADDR_SHUTDOWN_SUBSYSTEMS   0x0040B220u  /* void(void) */
/* Look for the game CD: walk the logical drives, find one that is a CD-ROM and
 * whose volume label is ARMYMEN2, and remember where it is. */
#define ADDR_FIND_GAME_CD        0x00426B50u  /* int32_t(void) */
#define ADDR_CD_PRESENT          0x00512588u  /* int32_t */
#define ADDR_CD_FOUND_FLAG       0x00512594u  /* int32_t, set alongside it */
#define ADDR_CD_PATH             0x00512464u  /* char[], the drive root */
#define ADDR_CD_LABEL            0x004852B8u  /* "ARMYMEN2" */
#define ADDR_GAME_STRICMP        0x00465F90u  /* the game's own CRT */
#define ADDR_GAME_SPRINTF        0x00464CE2u
#define ADDR_RESET_TO_TITLE      0x004249C0u
#define ADDR_BUILD_TRIG_TABLES      0x0042DC30u
#define ADDR_FREE_SPRITE_LIST_ALIAS      0x00409920u
#define ADDR_INIT_AUDIO      0x0040C9B0u
#define ADDR_CLEAR_GAME_OVER      0x0042E580u
#define ADDR_START_INTRO         0x0040B7A0u  /* honours -nointro */
#define ADDR_RUN_FRAME           0x0040B000u  /* one tick; state machine of 5 */
/* RunFrame is a state machine. It returns at once unless ADDR_APP_ACTIVE is
 * set, then restores lost surfaces, polls input, and dispatches on
 * ADDR_GAME_STATE through the table below.
 *
 * State 2 is the level teardown, and it tail-JUMPS to 0x004256F0 rather than
 * calling it -- which is the only route to StopAllSounds. Those audio teardown
 * functions therefore need a mission to END; no amount of quitting from the
 * title screen reaches them. */
#define ADDR_RUN_FRAME_TABLE     0x0040B050u  /* void(*[5])(void), by ADDR_GAME_STATE */
/* Named from the RunFrame jump table at 0x0040B050, whose third entry this is,
 * and not from the one thing it does that anybody had looked at. It runs EVERY
 * FRAME of a mission. Two things come out of it:
 *
 *   - if ADDR_STATE_PENDING is set it tail-jumps to ADDR_LEVEL_TEARDOWN, which
 *     is a DIFFERENT function and the one that calls StopAllSounds;
 *   - otherwise it dispatches on ADDR_MENU_REQUEST_TAKEN, biased by 22, over a
 *     13-entry table at 0x00426230 -- the in-mission sub-states. */
#define ADDR_STATE2_FRAME        0x004260C0u  /* void(void), state 2 per frame */
/* The actual teardown, and it was called ADDR_STATE2_FRAME's address for as
 * long as anyone had written the name down. Reached ONLY by the tail jump at
 * 0x004260C9 -- no call site anywhere -- which is why a reachability scan that
 * looks for `call` and `push imm32` reports it as dead code. It calls
 * ADDR_STOP_ALL_SOUNDS as its second instruction-level call. */
#define ADDR_LEVEL_TEARDOWN      0x004256F0u  /* void(void), on leaving a level */
/* Sub-state 34 of ADDR_STATE2_FRAME's table, and the only in-mission code that
 * reads a key and raises a menu request. The test is `!IsKeyDown(ESC) &&
 * KeyChanged(ESC)`, i.e. ESCAPE on RELEASE. It does nothing during ordinary
 * play because the sub-state is not 34 then -- measured, not assumed. */
#define ADDR_SUBSTATE34_ESCAPE   0x00425DA0u  /* void(void) */
#define ADDR_FREE_SPRITE_SETS     0x00423D20u
#define ADDR_SHUTDOWN_DDRAW      0x0041A950u  /* void(void) */
#define ADDR_DD_CLIPPER          0x00507340u  /* IDirectDrawClipper * */
#define ADDR_REPORT_LEAKS        0x0041E690u  /* "Unreleased memory (%d) blocks:" */
#define ADDR_FREE_MEM_TRACKER    0x0041E710u

/* Packed map key: A(7) | gap(2) | B(10) | C(7). */
#define ADDR_PACK_KEY       0x00433810u
#define ADDR_KEY_FIELD_A    0x00433830u
#define ADDR_KEY_FIELD_B    0x00433840u
#define ADDR_KEY_FIELD_C    0x00433850u

/* Object type predicates; all accept NULL and answer 0. */
#define ADDR_OBJ_IS_ITEM    0x00433860u  /* types 1, 4 */
#define ADDR_OBJ_IS_TYPE2   0x00457470u
#define ADDR_OBJ_IS_TYPE3   0x00457490u
#define ADDR_OBJ_IS_TYPE8   0x004574B0u  /* int32_t(const AM2_Object *) */
#define ADDR_OBJ_IS_TYPE4   0x0045EEB0u

/* The lookup and the type test in one. Eight callers. */
#define ADDR_LOOKUP_TYPE3_BY_UID 0x0045D970u  /* AM2_Object *(uint32_t uid) */
#define ADDR_FIELD_53C         0x0045AFA0u  /* uint32_t(const void *) */
#define ADDR_ADD_BYTE_SAT      0x0045F440u  /* int32_t(base, add) */
#define ADDR_COMPARE_DWORD     0x0043E150u  /* int32_t(const void*, const void*) */
#define ADDR_COPY_BYTE_IF_SET  0x00408560u  /* void(unused, uint8_t*, const void*) */
#define ADDR_SCALE_32_BLOCKS   0x0042E4F0u  /* int32_t(int32_t) */
#define ADDR_TITLE_CASE        0x0042E510u  /* void(char *) */
#define ADDR_RESET_PAIR_MASK   0x0042F120u  /* void(uint32_t*, uint32_t*) */
#define ADDR_IS_KIND_7         0x00435640u  /* int32_t(const void *) */
#define ADDR_IS_BLANK          0x0043EE80u  /* int32_t(uint8_t) -- no '\n' */
#define ADDR_IS_SCRIPT_DELIM   0x0043EEA0u  /* int32_t(uint8_t) */
/* Exchange the first and third bytes of a packed colour, leaving the second
 * and clearing the fourth: a DIB entry is 0x00RRGGBB and the matcher above
 * reads 0x00BBGGRR. It was also ADDR_SWAP_COLOUR_BYTES, which named it for the
 * caller that hands it a bitmap's palette rather than for what it does. */
#define ADDR_SWAP_COLOUR_BYTES 0x0041AE90u  /* uint32_t(uint32_t) */
#define ADDR_NULL_STUB_4       0x004170E0u  /* void __stdcall(uint32_t) */
#define ADDR_NULL_STUB         0x0042E170u  /* void(void) */
#define ADDR_RETURN_ZERO       0x0042E980u  /* int32_t(void) */
#define ADDR_RETURN_ONE        0x004354F0u  /* int32_t(void) */
#define ADDR_REVERSE_BLOCKS    0x004231A0u  /* int32_t(dst, src, total, count) */
#define ADDR_COMPARE_PAIR      0x00435EB0u  /* int32_t(const void*, const void*) */
#define ADDR_MAP_CODE          0x00406920u  /* int32_t(int32_t) */
#define ADDR_COMPARE_TRIPLE    0x00435A80u  /* int32_t(const void*, const void*) */
#define ADDR_TYPES_COMPATIBLE  0x00433570u  /* int32_t(int32_t a, int32_t b) */
#define ADDR_SET_FACING_14     0x0043D450u  /* void(facing, src, out) */
#define ADDR_SET_FACING_08     0x0045C5E0u
#define ADDR_IS_KIND_10_17     0x0044BBF0u  /* int32_t(int32_t) */
#define ADDR_IS_KIND_14_22     0x00433500u  /* int32_t(int32_t) */
#define ADDR_CLASSIFY_CODE74   0x0040D7E0u  /* int32_t(const void *obj) */
#define ADDR_KIND_IN_SET_A     0x0045EE20u  /* int32_t(int32_t kind) */
#define ADDR_KIND_IN_SET_B     0x00433520u  /* int32_t(int32_t kind) */
/* The 1bpp source bitmap the RLE mask is encoded FROM, and the encoder's own
 * bit test. Rows run bottom-up, as a DIB's do. */
#define ADDR_BITMAP_BIT_SET  0x004232C0u  /* int32(base, x, y, height, stride) */


#define ADDR_MASK_PIXEL_SOLID  0x0041CF20u  /* int32_t(x, y, const void *mask) */
#define ADDR_MASK_PIXEL_SOLID32 0x0041CEC0u  /* same, dword row table */
#define ADDR_OBJ_MASK_BIT_AT   0x00435390u  /* int32_t(obj, const AM2_Point *) */
#define ADDR_OBJ_NEXT_KIND538 0x0040D880u  /* int32_t(obj, int32_t want) */
#define ADDR_COLLAPSE_DELTAS  0x00439CC0u  /* void(uint16_t *, int32_t *) */
#define ADDR_REMAP_RLE_RUNS   0x00423EE0u  /* void(rle, unused, wide, table) */
#define ADDR_XOR_CHECKSUM      0x00402700u  /* uint32_t(const void *record) */
#define ADDR_CHAIN_FIELD_14    0x004010B0u  /* uint32_t(const void *p) */
#define ADDR_LIST_PUSH_FRONT   0x00429F20u  /* void(void *node, void **head) */
#define ADDR_LIST_UNLINK       0x0041DAD0u  /* void(void *node, void **head) */
#define ADDR_REMAP_BYTES       0x0041BB60u  /* void(dst, src, table, count) */
#define ADDR_SET_FIELD_IN_ALL  0x00434E90u  /* int32_t(void *record, void *v) */
#define ADDR_FIELD51_MEETS_MIN 0x0040A490u  /* int32_t(const void *p) */
#define ADDR_OBJ_KIND538_10_17 0x0040D860u  /* int32_t(const void *obj) */
#define ADDR_FILTER_MATCHES    0x0041EF20u  /* int32_t(wantA,wantB,haveA,haveB,maskA,maskB) */
#define ADDR_CONSUME_PENDING   0x00408520u  /* void(src, dst, cfg) */
#define ADDR_FACING_DELTA_08   0x0045C870u  /* int32_t(const void*, int32_t) */
#define ADDR_FACING_DELTA_14   0x0043D550u
#define ADDR_MAP_CODE_18_28    0x00406A40u  /* int32_t(int32_t code) */
#define ADDR_OBJ_CODE_UNMAPPED 0x00449EF0u  /* int32_t(const void *obj) */
#define ADDR_MEETS_ALL_THREE   0x00409650u  /* int32_t(const void *p) */
#define ADDR_OBJ_TYPE2_FIELD548 0x00457450u /* uint32_t(const AM2_Object *) */
#define ADDR_POINTS_EQUAL      0x0042E140u  /* int32_t(AM2_Point, AM2_Point) */
#define ADDR_POINTS_DIFFER     0x0042E110u
#define ADDR_OBJ_IS_TYPE238 0x00457420u  /* types 2, 3, 8 */
#define ADDR_APPROX_DIST    0x0042DDE0u  /* int32_t(const AM2_Point*, const AM2_Point*) */
#define ADDR_APPROX_DIST_XY 0x0042DE20u  /* int32_t(dx, dy) -- the same maths */
#define ADDR_ANGLE_DELTA    0x0042DD90u  /* int32_t(from, to), 8-bit headings */
#define ADDR_ROUND_TO_8     0x0042DFB0u  /* int32_t(value, bits) */
#define ADDR_MAKE_POINT     0x0042E1A0u  /* uint32_t(x, y) -> packed AM2_Point */
#define ADDR_FIND_SLOT      0x004277A0u  /* int32_t(uint32_t uid, int32_t *insert_at) */
#define ADDR_LOOKUP_BY_UID  0x00427820u  /* void *(uint32_t uid) */

/* The global object registry: an array of 12-byte {uid, obj, serial} records
 * kept sorted by uid. See src/game/objtable.c. */
#define ADDR_OBJ_TABLE      0x00514F0Cu  /* AM2_ObjEntry * */
#define ADDR_OBJ_COUNT      0x00514F04u  /* int32_t */
#define ADDR_OBJ_CAPACITY   0x00514F00u  /* int32_t */

#define ADDR_ADD_TO_ITEM_LIST 0x00429740u /* uint32_t(AM2_Object*, uint32_t) */
/* item.cpp accessors. UID_ARMY is `uid >> 29`, the owner half of a uid -- the
 * same field AM2_UID_OWNER_SHIFT names, and what 0x0042A930 logs as "army".
 * UID_ON_WIRE returns its argument and is applied to uids crossing a comm
 * message; see src/game/item.h for why it is kept. */
/* item.cpp's savegame section. The saver brackets a FirstItem/NextItem walk
 * with tags; the loader checks the opening tag and then reads item markers
 * until one is not AM2_SAVE_ITEM_MARK. docs/savetags.tsv already had the
 * loader's site -- item.cpp line 1192, tag 0x06660007 -- from the string it
 * passes CheckSaveTag. */
#define ADDR_STR_ITEM_CPP    0x00485C58u  /* "C:\\ArmyMen2\\source\\item.cpp" */
#define ADDR_SAVE_ITEMS     0x00428950u  /* int32_t(FILE *) */
#define ADDR_LOAD_ITEMS     0x00428BB0u  /* int32_t(FILE *) */
#define ADDR_SAVE_ONE_ITEM  0x00428870u  /* void(FILE *, void *obj) */
#define ADDR_LOAD_ONE_ITEM  0x004289E0u  /* void(FILE *, int32_t) */
#define ADDR_ITEMS_RESET    0x00429450u  /* void(void) */

#define AM2_SAVE_TAG_ITEMS  0x06660007u  /* opens the section */
#define AM2_SAVE_TAG_END    0x06660001u  /* closes a list section */

#define ADDR_UID_ARMY        0x0042A7A0u  /* uint32_t(uint32_t uid) */
#define ADDR_UID_ON_WIRE     0x0042A7B0u  /* uint32_t(uint32_t uid) */
/* A 3-bit field at bit 18 of the object's word at +8, get and set. Named for
 * its position rather than its meaning; src/game/item.h records what points at
 * an army index and what argues against it. */
#define ADDR_OBJ_FIELD_A     0x0042A810u  /* uint32_t(const void *obj) */
#define ADDR_OBJ_SET_FIELD_A 0x0042A7F0u  /* void(void *obj, uint32_t) */
#define ADDR_OBJ_FIELD_B     0x00429560u  /* int32_t(const void *obj), +0x64 */
#define ADDR_REMOVE_FROM_ITEM_LIST 0x00428590u /* int32_t(AM2_Object*) */

/* 0x004285F0, "FreeItem %0x" -- its own name. A destructor dispatched on the
 * item kind at +0, with a jump table of eight arms. Four of them share one
 * callee and the rest have their own, each in a different translation unit,
 * which is what the kind really selects: whose object this is.
 *
 * Role names below, tagged with the kinds that reach them. Nothing here says
 * what those subsystems are called, and CLAUDE.md still lists object types 2,
 * 3 and 8 as unidentified -- this narrows where to look rather than answering
 * it. */
#define ADDR_FREE_ITEM             0x004285F0u  /* int32_t(void *, int32_t) */
#define ADDR_FREE_ITEM_COMMON      0x0043BBB0u  /* kinds 1, 5, 6, 8 */
#define ADDR_FREE_ITEM_KIND2       0x004478C0u
#define ADDR_FREE_ITEM_KIND3       0x0045B470u
#define ADDR_FREE_ITEM_KIND4       0x0045F290u  /* the one that logs */
#define ADDR_FREE_ITEM_KIND7       0x004355F0u
#define ADDR_FIRST_ITEM     0x00427850u  /* void *(void) */
#define ADDR_NEXT_ITEM      0x00427880u  /* void *(void) */

/* Iteration state. The cursor is an index into the table; the stamp is bumped
 * once per pass so a walk can tell which entries it has already visited even
 * though inserts shift indices underneath it. */
#define ADDR_ITER_CURSOR    0x00514F08u  /* int32_t */
#define ADDR_ITER_STAMP     0x0051308Cu  /* uint32_t */

/* A UID is (owner << 29) | counter, so eight owners each with a 29-bit
 * counter. These are the per-owner counters, indexed 0..7. */
#define ADDR_UID_COUNTERS   0x00511DE0u  /* uint32_t[8] */
/* Owner used for the object types that do not carry their own. */
#define ADDR_DEFAULT_OWNER  0x004F9FDCu  /* uint32_t */
/* Non-zero enables the AddToItemList commentary. */
#define ADDR_DEBUG_ITEMLIST 0x004FD73Cu  /* int32_t */

/* More of the statically linked MSVC 6 CRT. */
#define ADDR_REALLOC        0x004646D8u  /* void *(void*, size_t) */
#define ADDR_MEMMOVE        0x00465710u  /* void *(void*, const void*, size_t) */

/* Statically linked MSVC 6 CRT */
#define ADDR_FREAD          0x004645C1u  /* size_t(void*,size_t,size_t,FILE*) */
#define ADDR_FOPEN          0x004648E2u  /* FILE *(const char*, const char*) */
#define ADDR_FCLOSE         0x0046486Cu  /* int32_t(FILE*) */
#define ADDR_FSEEK          0x00464F18u  /* int32_t(FILE*, int32_t, int32_t) */
#define ADDR_FWRITE         0x004644B7u  /* size_t(const void*,size_t,size_t,FILE*) */
#define ADDR_MODE_RB        0x00474170u  /* "rb" */

/* ---- typed accessors -------------------------------------------------- */

/* The game's FILE is an MSVC 6 _iobuf. We never dereference it, so keep it
 * opaque rather than pretending it matches ours. */
typedef struct am2_FILE am2_FILE;

typedef size_t (__cdecl *am2_fread_fn)(void *buf, size_t size, size_t count,
                                       am2_FILE *fp);
typedef size_t (__cdecl *am2_fwrite_fn)(const void *buf, size_t size,
                                        size_t count, am2_FILE *fp);
typedef am2_FILE *(__cdecl *am2_fopen_fn)(const char *path, const char *mode);
typedef int32_t (__cdecl *am2_fclose_fn)(am2_FILE *fp);
typedef int32_t (__cdecl *am2_fseek_fn)(am2_FILE *fp, int32_t off, int32_t whence);
typedef void   (__cdecl *am2_log_fn)(const char *fmt, ...);

typedef void *(__cdecl *am2_malloc_fn)(size_t n);
typedef void  (__cdecl *am2_free_fn)(void *p);
typedef void *(__cdecl *am2_realloc_fn)(void *p, size_t n);
typedef void *(__cdecl *am2_memmove_fn)(void *dst, const void *src, size_t n);

/* See ADDR_BLIT_BITMAP_IN. `inout` is genuinely in/out; take its address from
 * a variable you re-read afterwards rather than from a temporary. */
typedef int32_t (__cdecl *am2_blit_bitmap_in_fn)(void *dest, int32_t pitch,
                                                 const void *src,
                                                 int32_t width, int32_t height,
                                                 const uint8_t *remap,
                                                 uint32_t *inout);

#define orig_fread   (*(am2_fread_fn)ADDR_FREAD)
#define orig_fwrite  (*(am2_fwrite_fn)ADDR_FWRITE)
/* The game's own stdio, for the same reason as its malloc: a FILE opened by
 * the game's CRT cannot be read or closed by ours. */
#define orig_fopen   (*(am2_fopen_fn)ADDR_FOPEN)
#define orig_fclose  (*(am2_fclose_fn)ADDR_FCLOSE)
#define orig_fseek   (*(am2_fseek_fn)ADDR_FSEEK)
#define orig_log     (*(am2_log_fn)ADDR_LOG)
/* The game's heap, not ours -- msvcrt has a different one entirely, so anything
 * the game allocated must be freed here and vice versa. */
#define orig_malloc  (*(am2_malloc_fn)ADDR_GAME_MALLOC)
#define orig_free    (*(am2_free_fn)ADDR_GAME_FREE)
/* ChangeObjectFrame, 0x004351C0, still original and now wanted from two
 * modules -- objscript.cpp's runner and event.cpp's ScriptSetObjBitmap -- so
 * the seam lives here rather than being declared twice. */
typedef int32_t (__cdecl *am2_change_object_frame_fn)(void *obj, int32_t frame,
                                                      int32_t flag);
#define orig_change_object_frame \
    (*(am2_change_object_frame_fn)ADDR_CHANGE_OBJECT_FRAME)
#define orig_blit_bitmap_in (*(am2_blit_bitmap_in_fn)ADDR_BLIT_BITMAP_IN)
/* The object table was allocated by the game's CRT, so it must be grown by the
 * game's CRT -- our msvcrt has a different heap entirely. */
#define orig_realloc (*(am2_realloc_fn)ADDR_REALLOC)
#define orig_memmove (*(am2_memmove_fn)ADDR_MEMMOVE)

#endif /* AM2_ORIG_H */
