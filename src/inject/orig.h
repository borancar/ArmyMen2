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
#define ADDR_LOG            0x0045CAA0u  /* void(const char*,...) -- stubbed to `ret` */
#define ADDR_RECT_SET       0x0042E1C0u  /* void(AM2_Rect*,int32,int32,int32,int32) */
#define ADDR_CLAMP          0x0042E180u  /* int32_t(int32_t v, int32_t lo, int32_t hi) */
#define ADDR_POINT_IN_RECT  0x0042E1F0u  /* int32_t(const AM2_Rect*, const AM2_Point*) */

#define ADDR_CLIP_RECT      0x0042E220u  /* int32_t(src, clip, int32*, int32*, out) */

/* Text rendering. */
#define ADDR_DRAW_TEXT      0x00446930u  /* void(x,y,str,font,?,colour) */
#define ADDR_GLYPH_OFFSETS  0x006598D4u  /* uint16_t[], indexed ch + font*262 */
#define ADDR_FONT_BASES     0x00659AD4u  /* uint8_t*[], indexed font*133 */
#define ADDR_SCREEN_CLIP    0x00485310u  /* AM2_Rect -- text and sprites share it */

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
#define ADDR_OVERLAY_PALETTE     0x004FE1A4u  /* void *, set before the overlay blit */
#define ADDR_DEFAULT_PALETTE     0x004FE084u  /* void *, used when the sprite has none */

/* Packed map key: A(7) | gap(2) | B(10) | C(7). */
#define ADDR_PACK_KEY       0x00433810u
#define ADDR_KEY_FIELD_A    0x00433830u
#define ADDR_KEY_FIELD_B    0x00433840u
#define ADDR_KEY_FIELD_C    0x00433850u

/* Object type predicates; all accept NULL and answer 0. */
#define ADDR_OBJ_IS_ITEM    0x00433860u  /* types 1, 4 */
#define ADDR_OBJ_IS_TYPE2   0x00457470u
#define ADDR_OBJ_IS_TYPE3   0x00457490u
#define ADDR_OBJ_IS_TYPE238 0x00457420u  /* types 2, 3, 8 */
#define ADDR_APPROX_DIST    0x0042DDE0u  /* int32_t(const AM2_Point*, const AM2_Point*) */
#define ADDR_FIND_SLOT      0x004277A0u  /* int32_t(uint32_t uid, int32_t *insert_at) */
#define ADDR_LOOKUP_BY_UID  0x00427820u  /* void *(uint32_t uid) */

/* The global object registry: an array of 12-byte {uid, obj, serial} records
 * kept sorted by uid. See src/game/objtable.c. */
#define ADDR_OBJ_TABLE      0x00514F0Cu  /* AM2_ObjEntry * */
#define ADDR_OBJ_COUNT      0x00514F04u  /* int32_t */
#define ADDR_OBJ_CAPACITY   0x00514F00u  /* int32_t */

#define ADDR_ADD_TO_ITEM_LIST 0x00429740u /* uint32_t(AM2_Object*, uint32_t) */
#define ADDR_REMOVE_FROM_ITEM_LIST 0x00428590u /* int32_t(AM2_Object*) */
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

/* ---- typed accessors -------------------------------------------------- */

/* The game's FILE is an MSVC 6 _iobuf. We never dereference it, so keep it
 * opaque rather than pretending it matches ours. */
typedef struct am2_FILE am2_FILE;

typedef size_t (__cdecl *am2_fread_fn)(void *buf, size_t size, size_t count,
                                       am2_FILE *fp);
typedef void   (__cdecl *am2_log_fn)(const char *fmt, ...);

typedef void *(__cdecl *am2_realloc_fn)(void *p, size_t n);
typedef void *(__cdecl *am2_memmove_fn)(void *dst, const void *src, size_t n);

#define orig_fread   (*(am2_fread_fn)ADDR_FREAD)
#define orig_log     (*(am2_log_fn)ADDR_LOG)
/* The object table was allocated by the game's CRT, so it must be grown by the
 * game's CRT -- our msvcrt has a different heap entirely. */
#define orig_realloc (*(am2_realloc_fn)ADDR_REALLOC)
#define orig_memmove (*(am2_memmove_fn)ADDR_MEMMOVE)

#endif /* AM2_ORIG_H */
