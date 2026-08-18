#ifndef AM2_WINMAIN_H
#define AM2_WINMAIN_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* The outermost layer of the process -- everything between the CRT handing
 * control over and the game's own state machine taking it.
 *
 * All four functions here are boundary functions in the strict sense: between
 * them they own the mutex, the window class, the window, the message queue and
 * the window's position on the desktop. Nothing else in the game creates a
 * window or reads the queue.
 *
 * The command line is parsed here too, which is how `-w` was identified. It
 * turns out to be the switch the rest of the display code is conditioned on.
 */

/* Original: 0x0040B360. Called once by the CRT startup.
 *
 * Parses the command line into the option globals, brings the application up,
 * then runs the message loop until WM_QUIT and returns its wParam. Returns 0
 * without running anything if the application failed to start -- including the
 * ordinary case of a second copy finding the mutex already held. */
int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                       char *lpCmdLine, int32_t nCmdShow);

/* Original: 0x0040B600. Mutex, window class, window, input, DirectDraw.
 *
 * Returns 1 once the game is ready to run and 0 on any failure, having already
 * cleaned up and reported. `nCmdShow` is accepted and ignored: the window is
 * created WS_VISIBLE, so there is never a ShowWindow to pass it to. */
int32_t __cdecl InitApplication(HINSTANCE hInstance, int32_t nCmdShow);

/* Original: 0x0040B280. Translate and dispatch one message.
 *
 * Returns 0 when the message is WM_QUIT, which is what ends the main loop, and
 * 1 otherwise. */
int32_t __cdecl PumpMessage(MSG *msg);

/* Original: 0x0040B070. Fit the window to the requested screen size and record
 * where its client area landed.
 *
 * Fullscreen is the trivial case: the drawing rectangle is the whole screen at
 * the origin. Windowed has to work backwards -- give the window a caption and
 * border, ask AdjustWindowRectEx how much bigger that makes it, resize to suit,
 * nudge it inside the work area so the title bar is reachable, and finally map
 * the client corners to screen coordinates. Those corners become the origin
 * that every locked-surface write is offset by. */
void __cdecl PositionWindow(void);

/* Original: 0x0040B2B0. Decide whether this machine is fast enough, by loading
 * cpuinf32.dll and asking it. Publishes the answer to two globals and logs the
 * `system speed:` line. See winmain.cpp for what "fast" meant in 1999. */
void __cdecl DetectCpuSpeed(void);

/* Original: 0x00426B50. Find the drive holding the game CD, by walking the
 * logical drives for a CD-ROM labelled ARMYMEN2. Returns 1 if one was accepted
 * and records its root. */
int32_t __cdecl FindGameCD(void);

int winmain_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_WINMAIN_H */
