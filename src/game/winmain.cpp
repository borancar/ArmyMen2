/* Application startup, the window, and the message loop -- reconstructed from
 * ArmyMen2.exe.
 *
 *   WinMain          0x0040B360   called once by the CRT startup
 *   InitApplication  0x0040B600   1 call site
 *   PumpMessage      0x0040B280   2 call sites
 *   PositionWindow   0x0040B070   2 call sites
 *
 * This is the whole of the game's contact with the window system: the mutex,
 * the class, the window, the queue, and where the window sits on the desktop.
 * Nothing else in the image calls CreateWindowEx or PeekMessage. What it hands
 * off to -- input, DirectDraw, the frame tick, the state machine -- is game
 * logic and stays in the original image, reached through the typed pointers
 * below.
 *
 * WinMain is stdcall, `ret 0x10`, and its four arguments are the standard ones.
 * The MSG it loops on is the 28 bytes of stack it zeroes on entry with a
 * `rep stosd` of 7 dwords, which is how the loop's shape was confirmed before a
 * single call had been identified.
 *
 * WHAT THE COMMAND LINE TURNED OUT TO BE HIDING
 *
 *   The flag parsing is a plain chain of strstr calls, and one of the switches
 *   it sets is `-w`. That global, 0x00507344, is the windowed-mode flag, and it
 *   is the condition on every piece of display behaviour that had looked
 *   unreachable: the border and repositioning here, the palettized primary in
 *   InitDirectDraw, and CalibratePalette. It reads 0 under the harness because
 *   nothing passes `-w`, not because the code is dead.
 *
 *   Three of the switches are people -- `-rob`, `-peter`, `-dan`. `-rob` is the
 *   flag src/game/objtable.cpp already knew as the one enabling AddToItemList's
 *   commentary; it just did not know it had a name.
 *
 * A note on argument evaluation. PositionWindow calls GetWindowLongA twice,
 * GetMenu between them, and feeds all three to AdjustWindowRectEx. The second
 * GetWindowLongA has to observe the SetWindowLongA above it, so the order is
 * load-bearing -- and C does not specify the order in which call arguments are
 * evaluated. The temporaries below are not for readability; removing them would
 * let the compiler reorder three Win32 calls against each other.
 */

#include "winmain.h"
#include "rect.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Every constant below was read out of the disassembly as a number and then
 * written as the SDK name it matches. These check that the substitution was
 * right, so a wrong guess is a build error rather than a subtly wrong window.
 * IDI_APPLICATION and IDC_ARROW are the other two -- both MAKEINTRESOURCE
 * casts, so not constant expressions, but both are 32512 which is the 0x7F00
 * the original pushes. */
static_assert(MUTEX_ALL_ACCESS == 0x1F0001, "MUTEX_ALL_ACCESS");
static_assert(CS_DBLCLKS == 0x0008, "CS_DBLCLKS");
static_assert((WS_POPUP | WS_VISIBLE) == 0x90000000, "window style");
static_assert(WS_EX_APPWINDOW == 0x00040000, "WS_EX_APPWINDOW");
static_assert((WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX) == 0x00C60000,
              "windowed style bits");
static_assert(GWL_STYLE == -16 && GWL_EXSTYLE == -20, "GetWindowLong indices");
static_assert((SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == 0x16, "resize");
static_assert((SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE) == 0x13, "restack");
static_assert((SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) == 0x15, "move");
static_assert(SPI_GETWORKAREA == 0x0030, "SPI_GETWORKAREA");
static_assert(WM_QUIT == 0x0012, "WM_QUIT");
static_assert(PM_REMOVE == 0x0001, "PM_REMOVE");
static_assert(sizeof(MSG) == 28, "the 7 dwords WinMain zeroes on entry");

/* ---- the globals this layer owns -------------------------------------- */

#define g_hInstance    (*(HINSTANCE *)(uintptr_t)ADDR_HINSTANCE)
#define g_hInstanceAux (*(uint32_t *)(uintptr_t)ADDR_HINSTANCE_AUX)
#define g_hWnd         (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_appMutex     (*(HANDLE *)(uintptr_t)ADDR_APP_MUTEX)
#define g_lastMessage  (*(uint32_t *)(uintptr_t)ADDR_LAST_MESSAGE)
#define g_screenW      (*(const int32_t *)(uintptr_t)ADDR_SCREEN_W)
#define g_screenH      (*(const int32_t *)(uintptr_t)ADDR_SCREEN_H)
#define g_screenRect   (*(AM2_Rect *)(uintptr_t)ADDR_SCREEN_RECT)
#define g_windowed     (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)
#define g_mapName      ((char *)(uintptr_t)ADDR_OPT_MAP_NAME)
#define g_commObject   (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)

/* An option global, written as int32_t whatever it is declared as elsewhere. */
#define opt(addr) (*(int32_t *)(uintptr_t)(addr))

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_hwnd_fn)(HWND hwnd);
typedef void    (__cdecl *am2_report_error_fn)(int32_t code, const char *what);

#define orig_check_base_path  (*(am2_void_fn)ADDR_CHECK_BASE_PATH)
#define orig_startup_40b2b0   (*(am2_void_fn)ADDR_STARTUP_40B2B0)
#define orig_init_timer       (*(am2_void_fn)ADDR_INIT_TIMER)
#define orig_init_input       (*(am2_hwnd_fn)ADDR_INIT_INPUT)
#define orig_init_directdraw  (*(am2_hwnd_fn)ADDR_INIT_DIRECTDRAW)
#define orig_report_error     (*(am2_report_error_fn)ADDR_REPORT_ERROR)
#define orig_release_mutex    (*(am2_void_fn)ADDR_RELEASE_APP_MUTEX)
#define orig_startup_426b50   (*(am2_void_fn)ADDR_STARTUP_426B50)
#define orig_startup_4249c0   (*(am2_void_fn)ADDR_STARTUP_4249C0)
#define orig_startup_42dc30   (*(am2_void_fn)ADDR_STARTUP_42DC30)
#define orig_startup_409920   (*(am2_void_fn)ADDR_STARTUP_409920)
#define orig_startup_40c9b0   (*(am2_void_fn)ADDR_STARTUP_40C9B0)
#define orig_startup_42e580   (*(am2_void_fn)ADDR_STARTUP_42E580)
#define orig_start_intro      (*(am2_void_fn)ADDR_START_INTRO)
#define orig_run_frame        (*(am2_void_fn)ADDR_RUN_FRAME)
#define orig_shutdown_423d20  (*(am2_void_fn)ADDR_SHUTDOWN_423D20)
#define orig_shutdown_ddraw   (*(am2_void_fn)ADDR_SHUTDOWN_DDRAW)
#define orig_report_leaks     (*(am2_void_fn)ADDR_REPORT_LEAKS)
#define orig_free_mem_tracker (*(am2_void_fn)ADDR_FREE_MEM_TRACKER)

/* The game statically links its own MSVC 6 CRT and ours is a separate world,
 * but strstr crosses nothing: it reads two strings and returns a pointer into
 * the first. No heap, no FILE, no locale. Ours will do. */

/* ---- command line ------------------------------------------------------ */

/* Every switch is a substring test against the whole command line, so `-w` is
 * matched anywhere in it and not only as a whole word. That is the original's
 * behaviour and it is kept. Order matters where two switches share a global:
 * `-bm -sm` leaves the music flag clear because `-sm` is tested second. */
static void ParseCommandLine(char *cmdLine)
{
    char *p;

    if (strstr(cmdLine, "-nointro"))   opt(ADDR_OPT_NO_INTRO) = 1;
    if (strstr(cmdLine, "-w"))         opt(ADDR_OPT_WINDOWED) = 1;
    if (strstr(cmdLine, "-tracePF"))   opt(ADDR_OPT_TRACE_PF) = 1;
    if (strstr(cmdLine, "-traceVEH"))  opt(ADDR_OPT_TRACE_VEH) = 1;

    if (strstr(cmdLine, "-debugComm"))
        *(int32_t *)(g_commObject + COMM_OFF_DEBUG) = 1;
    if (strstr(cmdLine, "-traceComm"))
        *(int32_t *)(g_commObject + COMM_OFF_TRACE) = 1;
    if (strstr(cmdLine, "-logComm"))
        *(int32_t *)(g_commObject + COMM_OFF_LOG) = 1;

    if (strstr(cmdLine, "-tracewin"))  opt(ADDR_OPT_TRACE_WIN) = 1;
    if (strstr(cmdLine, "-dbg"))       opt(ADDR_OPT_DBG) = 1;
    if (strstr(cmdLine, "-rob"))       opt(ADDR_OPT_ROB) = 1;
    if (strstr(cmdLine, "-peter"))     opt(ADDR_OPT_PETER) = 1;
    if (strstr(cmdLine, "-dan"))       opt(ADDR_OPT_DAN) = 1;
    if (strstr(cmdLine, "-df"))        opt(ADDR_OPT_DF) = 0;
    if (strstr(cmdLine, "-bm"))        opt(ADDR_OPT_MUSIC) = 1;
    if (strstr(cmdLine, "-sm"))        opt(ADDR_OPT_MUSIC) = 0;
    if (strstr(cmdLine, "-nm"))        opt(ADDR_OPT_NM) = 1;

    /* `-map:NAME` -- everything after the colon up to the first character that
     * is not greater than a space. The comparison is signed, so a byte with the
     * top bit set ends the name just as a space would. */
    g_mapName[0] = '\0';
    p = strstr(cmdLine, "-map:");
    if (p) {
        int8_t  c = (int8_t)p[5];
        int32_t i = 0;

        p += 5;
        while (c > 0x20) {
            g_mapName[i++] = (char)c;
            c = (int8_t)p[1];
            p++;
        }
        g_mapName[i] = '\0';
    }
}

/* ---- the window -------------------------------------------------------- */

void __cdecl PositionWindow(void)
{
    RECT r;

    if (!g_windowed) {
        /* Fullscreen: the drawing rectangle is the screen, at the origin. */
        SetRect((LPRECT)(uintptr_t)ADDR_SCREEN_RECT, 0, 0, g_screenW, g_screenH);
        return;
    }

    /* Turn the borderless popup into an ordinary sizeable window. WS_POPUP and
     * the three bits about to be set are the only ones cleared, so WS_VISIBLE
     * survives and the window does not blink out. */
    {
        LONG style = GetWindowLongA(g_hWnd, GWL_STYLE);
        SetWindowLongA(g_hWnd, GWL_STYLE,
                       (style & 0x7F39FFFF) |
                       (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX));
    }

    /* Grow the requested client size by whatever the new frame costs. */
    SetRect(&r, 0, 0, g_screenW, g_screenH);
    {
        LONG exStyle = GetWindowLongA(g_hWnd, GWL_EXSTYLE);
        BOOL hasMenu = (GetMenu(g_hWnd) != NULL);
        LONG style   = GetWindowLongA(g_hWnd, GWL_STYLE);

        AdjustWindowRectEx(&r, style, hasMenu, exStyle);
    }
    SetWindowPos(g_hWnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);

    /* Drag it back onto the work area if the resize pushed the caption off the
     * top or left of the desktop. Only those two edges are checked -- running
     * off the bottom right is left alone. */
    {
        RECT work;

        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        GetWindowRect(g_hWnd, &r);
        if (r.left < work.left)
            r.left = work.left;
        if (r.top < work.top)
            r.top = work.top;
        SetWindowPos(g_hWnd, NULL, r.left, r.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* Publish where the client area ended up, in screen coordinates. Both
     * corners are converted, which is why the RECT is punned to two POINTs --
     * as the original does, and as the identical layout allows. */
    GetClientRect(g_hWnd, &r);
    ClientToScreen(g_hWnd, (LPPOINT)&r.left);
    ClientToScreen(g_hWnd, (LPPOINT)&r.right);

    g_screenRect.left   = r.left;
    g_screenRect.top    = r.top;
    g_screenRect.right  = r.right;
    g_screenRect.bottom = r.bottom;
}

int32_t __cdecl InitApplication(HINSTANCE hInstance, int32_t nCmdShow)
{
    WNDCLASSA wc;
    int32_t   err;

    (void)nCmdShow;   /* the window is created WS_VISIBLE; nothing to show */

    /* One instance at a time. A second copy finds the mutex and leaves without
     * a word -- which is also why a game left running by a crashed harness
     * silently prevents the next one from starting. */
    g_appMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "ArmyMenMutex");
    if (g_appMutex)
        return 0;
    g_appMutex = CreateMutexA(NULL, FALSE, "ArmyMenMutex");

    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = (WNDPROC)(uintptr_t)ADDR_WND_PROC;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    /* Resource 32512 out of the executable, not the system icon: the numeric
     * value of IDI_APPLICATION is reused as the game's own resource id. */
    wc.hIcon         = LoadIconA(hInstance, IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName  = "Armymen2";
    wc.lpszClassName = "Armymen2";
    RegisterClassA(&wc);

    g_hWnd = CreateWindowExA(WS_EX_APPWINDOW, "Armymen2", "Armymen II",
                             WS_POPUP | WS_VISIBLE, 0, 0,
                             g_screenW, g_screenH,
                             NULL, NULL, hInstance, NULL);
    if (!g_hWnd)
        return 0;

    if (g_windowed)
        UpdateWindow(g_hWnd);
    SetFocus(g_hWnd);

    orig_init_timer();
    if (!orig_init_input(g_hWnd)) {
        orig_release_mutex();
        DestroyWindow(g_hWnd);
        return 0;
    }

    /* Sized before DirectDraw, because the cooperative level and the mode it
     * asks for depend on the window being the shape it means to keep. */
    PositionWindow();

    err = orig_init_directdraw(g_hWnd);
    if (err) {
        orig_report_error(err, "InitDirectDraw");
        orig_release_mutex();
        DestroyWindow(g_hWnd);
        return 0;
    }

    /* Windowed mode gets positioned a second time. Setting the cooperative
     * level moves the window, so the client origin published above is stale by
     * now and has to be measured again. */
    if (g_windowed)
        PositionWindow();

    orig_startup_42dc30();
    orig_startup_409920();
    orig_startup_40c9b0();
    orig_startup_42e580();
    return 1;
}

/* ---- the message loop -------------------------------------------------- */

int32_t __cdecl PumpMessage(MSG *msg)
{
    if (msg->message == WM_QUIT)
        return 0;

    TranslateMessage(msg);
    DispatchMessageA(msg);
    g_lastMessage = msg->message;
    return 1;
}

int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                       char *lpCmdLine, int32_t nCmdShow)
{
    MSG msg;

    (void)hPrevInstance;   /* always NULL on Win32, and never read */

    memset(&msg, 0, sizeof msg);
    g_hInstance    = hInstance;
    g_hInstanceAux = 0;

    ParseCommandLine(lpCmdLine);

    CoInitialize(NULL);
    orig_check_base_path();
    orig_startup_40b2b0();

    if (!InitApplication(hInstance, nCmdShow))
        return 0;

    orig_startup_426b50();
    orig_startup_4249c0();
    orig_start_intro();

    /* Drain the queue, and when there is nothing in it run a frame. A game loop
     * rather than a GetMessage loop: the process never blocks waiting for
     * input, which is what makes it burn a core at idle. */
    for (;;) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!PumpMessage(&msg))
                break;
        } else {
            orig_run_frame();
        }
    }

    orig_shutdown_423d20();
    orig_shutdown_ddraw();
    orig_report_leaks();
    orig_free_mem_tracker();

    if (g_appMutex)
        ReleaseMutex(g_appMutex);

    return (int32_t)msg.wParam;
}

int winmain_install(void)
{
    int rc = 0;

    /* AM2_PROBE_NOWIN leaves this whole layer original, so a run can be
     * compared against the untouched startup path with every other patch still
     * in place. `run-stock` cannot do that -- it drops all 40 at once.
     *
     * Worth keeping because patching WinMain makes three of the four counts
     * permanently 0: once our WinMain calls our InitApplication directly, the
     * patched entry points are never reached and the counters cannot move.
     * This is the switch that makes them mean something again. */
    if (getenv("AM2_PROBE_NOWIN"))
        return 0;

    rc |= patch_replace(ADDR_POSITION_WINDOW, (const void *)PositionWindow,
                        "PositionWindow", 0);
    rc |= patch_replace(ADDR_PUMP_MESSAGE, (const void *)PumpMessage,
                        "PumpMessage", 1);
    rc |= patch_replace(ADDR_INIT_APPLICATION, (const void *)InitApplication,
                        "InitApplication", 2);
    rc |= patch_replace(ADDR_WIN_MAIN, (const void *)WinMain, "WinMain", 4);
    return rc;
}
