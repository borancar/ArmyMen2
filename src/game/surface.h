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

/* Original: 0x0041B9A0. Locks `surf` and publishes its bits and pitch into the
 * globals the blitters read. Returns 1 on success, 0 if a different surface is
 * already locked or the lock failed. Re-locking the surface already held
 * succeeds trivially. */
int32_t __cdecl LockSurface(LPDIRECTDRAWSURFACE surf);

/* Original: 0x0041BA40. Releases the current lock. Always returns 1, and is
 * safe to call when nothing is locked. */
int32_t __cdecl UnlockSurface(void);

int surface_install(void);

#endif /* AM2_SURFACE_H */
