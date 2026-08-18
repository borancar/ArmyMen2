/* The window procedure -- reconstructed from ArmyMen2.exe.
 *
 *   WndProc  0x0040A6B0  registered, not patched -- see winproc.h for why
 *
 * Everything Windows says to the game arrives here. The handlers fall into
 * three groups, and it is worth naming them because the shape of the original
 * only makes sense once they are separated:
 *
 *   Geometry.     WM_MOVE and WM_SIZE re-measure the client area into the
 *                 screen rectangle, so every locked-surface write stays
 *                 correctly offset after the user drags the window.
 *
 *   Surrender.    WM_PAINT, WM_ACTIVATE and WM_SYSCOMMAND all end up calling
 *                 FlipToGDISurface. An exclusive-mode DirectDraw application
 *                 owns the screen, and each of these is a moment where it has
 *                 to give it back -- to repaint, to yield to another window, to
 *                 let the screen saver or monitor power-down through.
 *
 *   Input.        WM_SETCURSOR hides the pointer, WM_CHAR forwards typed
 *                 characters, WM_ACTIVATEAPP gates the frame tick.
 *
 * The six forwarded messages are the comm subsystem's own. They are dispatched
 * through a 108-byte index table at 0x0040AF04 that maps the whole 0x400..0x46B
 * range onto just four targets -- three handlers and the default -- which is
 * how a range that looks like a hundred cases turned out to be three.
 *
 * FIDELITY NOTES -- three things below look wrong and are faithful.
 *
 *   WM_MOVE calls IsIconic and discards the result. The compiler reloads the
 *   window handle either side of it, so the call is genuinely there and its
 *   answer genuinely unused.
 *
 *   The geometry and paint handlers read the window out of the global rather
 *   than using the hWnd they were passed -- except ClientToScreen, which uses
 *   the parameter. Same window in practice; kept as written.
 *
 *   WM_PAINT calls RedrawWindow with a null window, which is the desktop, not
 *   the game window.
 */

#include "winproc.h"
#include "winmain.h"
#include "mapdraw.h"
#include "../rect.h"

#include <stdint.h>

/* Messages the original names by number. */
/* Not comm traffic, despite living in the same range: src/game/movie.cpp posts
 * these -- 0x400 when a film finishes, 0x402 when one could not be started --
 * and both mean "advance the state machine". They were named for their
 * neighbours before the movie player was read. */
#define AM2_WM_COMM_464      0x0464u
#define AM2_WM_COMM_46B      0x046Bu
#define AM2_WM_COMM_46C      0x046Cu
#define AM2_WM_COMM_46D      0x046Du
#define AM2_WM_COMM_46E      0x046Eu
#define AM2_WM_COMM_500      0x0500u

static_assert(SC_SCREENSAVE == 0xF140 && SC_MONITORPOWER == 0xF170, "SC_*");
static_assert((RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
               RDW_UPDATENOW) == 0x185, "RedrawWindow flags");
static_assert(WA_INACTIVE == 0, "WA_INACTIVE");
static_assert(sizeof(PAINTSTRUCT) == 64, "the 0x40 bytes of frame it reserves");

#define g_hWnd        (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_ddraw       (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)
#define g_screenRect  (*(AM2_Rect *)(uintptr_t)ADDR_SCREEN_RECT)
#define g_windowed    (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)
#define g_lastMessage (*(const uint32_t *)(uintptr_t)ADDR_LAST_MESSAGE)
#define g_appActive   (*(int32_t *)(uintptr_t)ADDR_APP_ACTIVE)
#define g_gameState   (*(const int32_t *)(uintptr_t)ADDR_GAME_STATE)
#define g_stateArg    (*(const int32_t *)(uintptr_t)ADDR_GAME_STATE_ARG)

/* 0x0065A058. Reached as object -> table -> slot, which is the exact shape of a
 * COM call and is not one: no `this` is passed, and the argument is a RECT by
 * value that the callee pops. Slot 1 is the repaint. */
typedef void (__stdcall *am2_repaint_fn)(RECT damage);
typedef struct { am2_repaint_fn *slots; } AM2_PaintObject;
#define g_paintObject (*(AM2_PaintObject **)(uintptr_t)ADDR_PAINT_OBJECT)

/* 0x00486550. Records of three dwords, the first a function to run for the
 * current state. Indexed by whatever 0x0042E5D0 answers. */
typedef void (__cdecl *am2_state_fn)(void);
typedef struct { am2_state_fn fn; uint32_t rest[2]; } AM2_StateEntry;
#define g_stateDispatch ((const AM2_StateEntry *)(uintptr_t)ADDR_STATE_DISPATCH)

/* 0x005125B8. Null until the game installs a keyboard consumer. */
typedef void (__cdecl *am2_char_fn)(uint32_t ch, uint32_t lo, uint32_t hi);
#define g_charHandler (*(am2_char_fn *)(uintptr_t)ADDR_CHAR_HANDLER)

typedef void    (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_int_fn)(void);
typedef void    (__cdecl *am2_int_arg_fn)(int32_t);

#define orig_release_mutex     (*(am2_void_fn)ADDR_RELEASE_APP_MUTEX)
#define orig_on_app_activated  (*(am2_void_fn)ADDR_ON_APP_ACTIVATED)
#define orig_current_state     (*(am2_int_fn)ADDR_CURRENT_STATE)
#define orig_state_leave       (*(am2_void_fn)ADDR_STATE_LEAVE)
#define orig_request_state       (*(am2_int_arg_fn)ADDR_REQUEST_STATE)

/* The original, still in the image and still callable because we never wrote
 * over it. The comm messages go straight back to it. */
typedef LRESULT (CALLBACK *am2_wndproc_fn)(HWND, UINT, WPARAM, LPARAM);
#define orig_wndproc (*(am2_wndproc_fn)ADDR_WND_PROC)

/* Re-measure the drawing rectangle after the window has moved or resized.
 * Windowed asks the window; fullscreen assumes the whole screen. */
static void RemeasureScreenRect(HWND hWnd)
{
    if (g_hWnd) {
        /* Answer discarded -- see the fidelity note. */
        IsIconic(g_hWnd);
        if (g_hWnd && g_windowed) {
            GetClientRect(g_hWnd, (LPRECT)&g_screenRect);
            ClientToScreen(hWnd, (LPPOINT)&g_screenRect.left);
            ClientToScreen(hWnd, (LPPOINT)&g_screenRect.right);
            return;
        }
    }
    SetRect((LPRECT)&g_screenRect, 0, 0,
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

/* Hand the screen back to GDI. Everything that interrupts an exclusive-mode
 * DirectDraw application funnels through here. */
static void FlipToGDI(void)
{
    if (g_ddraw)
        IDirectDraw_FlipToGDISurface(g_ddraw);
}

static LRESULT OnPaint(void)
{
    LPDIRECTDRAWSURFACE gdi;
    PAINTSTRUCT         ps;
    RECT                damage;

    IDirectDraw_FlipToGDISurface(g_ddraw);
    if (IDirectDraw_GetGDISurface(g_ddraw, &gdi) != DD_OK)
        return 1;
    SetDrawTarget(gdi);

    if (!GetUpdateRect(g_hWnd, &damage, TRUE))
        return 1;
    RedrawWindow(NULL, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    BeginPaint(g_hWnd, &ps);
    g_paintObject->slots[1](damage);
    EndPaint(g_hWnd, &ps);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {

    case WM_DESTROY:
        orig_release_mutex();
        PostQuitMessage(0);
        break;

    case WM_MOVE:
    case WM_SIZE:
        RemeasureScreenRect(hWnd);
        break;

    case WM_ACTIVATE:
        /* Only the low half is the activation state. Losing activation is the
         * cue to release the display; gaining it needs nothing. */
        if (LOWORD(wParam) != WA_INACTIVE)
            break;
        if (!g_ddraw)
            break;
        FlipToGDI();
        break;

    case WM_PAINT:
        if (!g_ddraw || !g_paintObject)
            break;
        return OnPaint();

    case WM_ACTIVATEAPP:
        /* The frame tick reads this global and does nothing while it is clear,
         * so this is what stops the game simulating in the background. */
        g_appActive = (int32_t)wParam;
        if (!wParam)
            break;
        orig_on_app_activated();
        break;

    case WM_SETCURSOR:
        /* The game draws its own pointer into the surface. */
        SetCursor(NULL);
        return 1;

    case WM_CHAR:
        /* PumpMessage records the last message it dispatched, so a WM_CHAR
         * arriving twice for one keystroke is recognised and dropped. */
        if (g_lastMessage == uMsg)
            return 0;
        if (!g_charHandler)
            break;
        g_charHandler((uint32_t)wParam, (uint32_t)lParam & 0xFFFFu,
                      (uint32_t)lParam >> 16);
        return 0;

    case WM_SYSCOMMAND:
        /* Swallow the screen saver outright; give the display back before
         * letting a monitor power-down through. */
        if (wParam == SC_SCREENSAVE)
            return 1;
        if (wParam != SC_MONITORPOWER)
            break;
        if (!g_ddraw)
            break;
        FlipToGDI();
        break;

    case AM2_WM_STATE_ADVANCE:
    case AM2_WM_STATE_ABORT:
        if (g_gameState == 0)
            g_stateDispatch[orig_current_state()].fn();
        else {
            orig_state_leave();
            orig_request_state(g_stateArg);
        }
        break;

    /* Comm subsystem traffic: game logic, left in the original image. */
    case AM2_WM_COMM_464:
    case AM2_WM_COMM_46B:
    case AM2_WM_COMM_46C:
    case AM2_WM_COMM_46D:
    case AM2_WM_COMM_46E:
    case AM2_WM_COMM_500:
        return orig_wndproc(hWnd, uMsg, wParam, lParam);

    default:
        break;
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}
