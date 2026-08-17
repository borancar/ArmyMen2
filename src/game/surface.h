#ifndef AM2_SURFACE_H
#define AM2_SURFACE_H

/* The DirectDraw bracket around every drawing operation.
 *
 * The game rasterises in software, so before it can draw anything it locks a
 * surface and takes the raw bits, and afterwards gives them back. Those two
 * calls are LockSurface (38 call sites) and UnlockSurface (34), and every
 * blitter reconstructed so far runs between them.
 *
 * This is the boundary where the reconstruction meets DirectDraw. The blitters
 * are the game's own software rasterisers and are ours to replace; these two do
 * no drawing at all -- they call out through the surface's COM vtable and cache
 * what comes back.
 *
 * DirectDraw's own declarations come from ddraw.h via win32.h.
 */

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x0041B9A0. Locks `surf` and publishes its bits and pitch into the
 * globals the blitters read. Returns 1 on success, 0 if a different surface is
 * already locked or the lock failed. Re-locking the surface already held
 * succeeds trivially. */
int32_t __cdecl LockSurface(LPDIRECTDRAWSURFACE surf);

/* Original: 0x0041BA40. Releases the current lock. Always returns 1, and is
 * safe to call when nothing is locked. */
int32_t __cdecl UnlockSurface(void);

/* Original: 0x0041B850. Create an offscreen surface, 6 call sites.
 *
 * Width and height are given; the pixel format is not, and is instead inherited
 * by reading the primary's descriptor and overwriting only the fields that
 * differ. That is the whole reason it asks DirectDraw a question first.
 *
 * `caps` is overloaded. The only bit read is DDSCAPS_OFFSCREENPLAIN, and its
 * meaning is "put this in system memory and do not argue" -- which is what the
 * game asks for, because it rasterises in software and a video-memory surface
 * would make every locked write cross the bus. When it is clear, DirectDraw
 * gets to choose, and a failure is retried in system memory anyway.
 *
 * A negative `colourKey` means none; otherwise it is set as a source colour key
 * with the same value at both ends of the range. Returns NULL on failure,
 * having already reported it. */
LPDIRECTDRAWSURFACE __cdecl CreateOffscreenSurface(int32_t width, int32_t height,
                                                   int32_t caps,
                                                   int32_t colourKey);

/* Original: 0x0041AD30. Fill a surface with one colour, 7 call sites.
 *
 * Returns 1 on success. The primary is filled through the screen rectangle and
 * every other surface in its entirety -- because the primary is the whole
 * desktop when windowed, and clearing all of it would wipe everyone else's
 * windows. */
int32_t __cdecl ClearSurface(LPDIRECTDRAWSURFACE surf, uint32_t colour);

/* Original: 0x0041AC60. Put the finished frame on the screen. 2 call sites.
 *
 * Fullscreen flips; windowed has no flipping chain, so it blits the back buffer
 * to the primary at the client origin instead. Both paths are skipped entirely
 * while 0x004FA030 is clear.
 *
 * The flip path is the interesting one. A flip can answer DDERR_WASSTILLDRAWING
 * -- the previous one has not reached the screen yet -- and rather than block,
 * the game peeks at its own message queue between retries so the window stays
 * answerable while it waits. It peeks without removing, so nothing is consumed;
 * the only message it acts on is WM_QUIT, which it turns straight back into
 * PostQuitMessage. A lost surface is restored and the frame abandoned. */
void __cdecl PresentFrame(void);

/* Original: 0x0041A950, 2 call sites. Release every DirectDraw object.
 *
 * Reverses InitDirectDraw, in order: clipper, offscreen surface, the palette
 * hanging off the movie palette holder, the primary -- and with it the back
 * buffer, which was never separately owned -- then the display mode goes back
 * and both interface generations are released.
 *
 * Every step is guarded, so it is safe to call twice or after a failed
 * bring-up, which is why WinMain can run it unconditionally on the way out. */
void __cdecl ShutdownDirectDraw(void);

/* Original: 0x0041A8B0, 2 call sites. Restore anything the display took back.
 *
 * Run at the top of every frame. A DirectDraw surface in an exclusive-mode
 * application is lost whenever the user alt-tabs or the mode changes, and a
 * lost surface fails every operation until it is restored -- so this asks each
 * one whether it is lost and restores the ones that say yes.
 *
 * The map's surface is the interesting case: it is reached through a
 * descriptor rather than a global, and restoring it successfully tail-calls
 * back into the map code, because the pixels are gone even though the surface
 * is back and something has to redraw them. */
void __cdecl RestoreLostSurfaces(void);

/* Original: 0x0044D6D0, 7 call sites. Put the current picture on screen now.
 *
 * Used when something has to be visible before the next frame would arrive --
 * a load, a mode change, a dialog. It clears the flag PresentFrame checks, so
 * the ordinary present cannot fire underneath it, draws the scene twice, blits
 * straight to the primary itself, and then restores the flag to whatever it
 * was rather than to 1. That last detail matters: the caller may have had
 * presenting turned off for its own reasons. */
void __cdecl RefreshScreen(void);

/* Original: 0x0041B6A0. Release the DirectDraw palette a holder owns and null
 * it. The same +0x800 slot ShutdownDirectDraw and the movie player both use. */
void __cdecl ReleasePalette(void *holder);

/* Original: 0x0041B720. Push entries `first` through `last` inclusive into the
 * display palette. Fullscreen only -- windowed, the desktop owns the palette
 * and the game has no business rewriting it. */
void __cdecl SetPaletteRange(PALETTEENTRY *entries, uint32_t first,
                             uint32_t last);

/* Original: 0x0041B970. Give a surface a one-colour source colour key, which is
 * how every sprite gets its transparent index. */
void __cdecl SetSurfaceColorKey(LPDIRECTDRAWSURFACE surf, uint8_t key);

/* Original: 0x00424280, 1 call site. Re-read one sprite from an open stream
 * straight into a surface.
 *
 * Reads `nbytes` from `fp`, gets the surface descriptor, locks it, copies the
 * bitmap in through the `remap` table, unlocks, and keys the surface on the
 * transparent index unless bit 0 of `flags` says not to.
 *
 * `nbytes` is in/out: it arrives as the byte count and is read again after the
 * copy as the colour-key index. It is passed by value here because the address
 * taken inside is of this function's own parameter slot, exactly as in the
 * original.
 *
 * Returns 0 when the read or the descriptor fails and 1 otherwise -- including,
 * wrongly, when the lock or the copy fails. See the note in surface.cpp. */
int32_t __cdecl ReloadBitmapSurface(LPDIRECTDRAWSURFACE surf, am2_FILE *fp,
                                    uint32_t nbytes, int32_t width, int32_t height,
                                    const uint8_t *remap, uint32_t flags);

/* Original: 0x0041CE20. Fill one clipped rectangle of the draw target.
 *
 * Refuses to run while a lock is held, because Blt needs the surface back
 * first -- which is why this is a no-op rather than a failure in that case.
 * The rectangle is clipped to the screen and, when the target is the primary,
 * shifted by the client origin: the primary is the whole desktop in a window,
 * so a rectangle in game coordinates is in the wrong place on it. */
void __cdecl ClearRegion(const RECT *r, uint8_t colour);

int surface_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SURFACE_H */
