#ifndef AM2_DEVICE_H
#define AM2_DEVICE_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Bringing up the display and the input devices.
 *
 * Between them these two create every DirectDraw and DirectInput object the
 * game owns. Everything else in the engine works through the globals they
 * publish, so this is the point where the process actually acquires the screen
 * and the mouse -- the deepest the boundary goes.
 *
 * Both call their DirectX entry point through the game's own import thunk
 * rather than an import of ours. See orig.h: for DirectInput that is not a
 * stylistic choice, it is what keeps the harness's input injection working.
 */

/* Original: 0x0041AA10. Create the DirectDraw objects and the surfaces.
 *
 * Returns 0 on success and the failing HRESULT otherwise. The shape is a chain
 * of calls each of which aborts on a non-zero result, with two things layered
 * on top:
 *
 * The desktop's colour depth decides how much work windowed mode is. The game
 * only draws 8-bit, so if the desktop is already 8-bit the window can simply be
 * created; if it is not, the only way to change it is to take the display
 * exclusively, switch it, and hand it back -- which is what the middle of this
 * function is doing, and why it sets the cooperative level three times.
 *
 * Fullscreen instead asks for a flipping chain with one back buffer, and takes
 * the back buffer off the primary. Windowed has no flipping chain, so the same
 * role is filled by an ordinary offscreen surface.
 */
HRESULT __cdecl InitDirectDraw(HWND hWnd);

/* Original: 0x00426D30. Create the DirectInput devices.
 *
 * Returns 1 on success and 0 on any failure, having already put a message box
 * on screen naming the step that failed.
 *
 * The mouse is taken exclusively and the keyboard is not, which is the usual
 * split: the game wants the pointer to disappear and the motion to be raw, but
 * grabbing the keyboard exclusively would take Alt-Tab with it. The mouse is
 * also given a buffer, which is what makes it deliver deltas through
 * GetDeviceData rather than absolute positions -- the reason tools/point.py has
 * to close the loop on a screenshot instead of computing where to move.
 */
int32_t __cdecl InitInput(HWND hWnd);

/* Original: 0x00426EA0, 1 call site. Give the input devices back.
 *
 * Unacquire then Release, three devices then the IDirectInput itself. The third
 * device at 0x00512FDC is never created by InitInput and is always null here;
 * the teardown handles it anyway, so something else was meant to fill it in. */
void __cdecl ShutdownInput(void);

/* Original: 0x00426F20, 2 call sites. Take the mouse.
 *
 * Records that it succeeded, and records nothing when it fails -- an exclusive
 * foreground device cannot be acquired while another window has focus, so this
 * failing is ordinary rather than exceptional. */
void __cdecl AcquireMouse(void);

/* Original: 0x00427070, reached from PollInput. Drain the mouse's buffered
 * event queue into the per-poll deltas and the button state. */
void __cdecl PollMouse(void);

/* 0x00426F40, 8 call sites. Turn the pending relative deltas into a clamped
 * absolute cursor and stamp any button that has just gone down. */
void __cdecl UpdateMouseState(void);

/* Original: 0x004272D0, reached from PollInput. Read the keyboard into the
 * double-buffered state array, re-acquiring once if the read fails, mirror the
 * left/right modifier pairs, and run the 250/150 ms auto-repeat. */
void __cdecl PollKeyboard(void);

int device_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_DEVICE_H */
