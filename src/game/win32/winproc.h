#ifndef AM2_WINPROC_H
#define AM2_WINPROC_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x0040A6B0. The window procedure -- everything Windows says to the
 * game, as opposed to everything the game says to Windows.
 *
 * This one is installed differently from every other reconstruction in the
 * project, and the difference is worth understanding.
 *
 * A window procedure is not called; it is *registered*. The only reference to
 * 0x0040A6B0 anywhere in the image is the WNDCLASS field that InitApplication
 * fills in -- and InitApplication is itself reconstructed now. So instead of
 * writing a `jmp` over the original, we simply register this one in its place.
 * The original stays intact and, unlike a detoured function, stays callable.
 *
 * That matters because the 2,256 bytes at 0x0040A6B0 are not all boundary code.
 * Roughly half is the comm subsystem's private message range -- WM_USER+n
 * traffic carrying multiplayer state, which is game logic that this port is
 * explicitly not reconstructing. Detouring would have forced a choice between
 * reimplementing all of it or none of it. Delegation allows the honest split:
 * every message whose handler touches USER32 or DirectDraw is reconstructed
 * here, and the six that are purely internal are handed straight back.
 *
 * The split is exact rather than approximate. Of the seventeen import sites in
 * the original, all seventeen are inside handlers reconstructed below; the six
 * forwarded messages (0x464, 0x46B, 0x46C, 0x46D, 0x46E, 0x500) contain no
 * import site at all. So no Win32 call in this function is left to the
 * original -- the delegation costs nothing in boundary coverage.
 *
 * Because it is registered rather than patched, it has no entry in `counts`.
 * It is verified by the game drawing at all: the paint, activation and cursor
 * messages all pass through it.
 */
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* There is deliberately no winproc_install(). Every other module here has one
 * because every other module is patched over its original; this one is
 * installed by InitApplication putting it in the WNDCLASS, so a no-op install
 * hook would only suggest a patch that does not exist. */

#ifdef __cplusplus
}
#endif

#endif /* AM2_WINPROC_H */
