/* user32.cpp -- the window, the message queue, the cursor, metrics and the
 * small rectangle helpers. One window exists at a time, which is one more
 * than the game ever asks for.
 */
#include "platform.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the window --------------------------------------------------------------- */

typedef struct W32L_WindowClass {
    char    name[64];
    WNDPROC proc;
} W32L_WindowClass;

typedef struct W32L_Window {
    WNDPROC proc;
    DWORD   style;
    DWORD   exStyle;
    int32_t w, h;
    int32_t alive;
} W32L_Window;

static W32L_WindowClass w32l_classes[4];
static int32_t         w32l_class_count;
static W32L_Window      w32l_window;
static int32_t         w32l_focused = 1;
static int32_t         w32l_cursor_count;    /* ShowCursor's counter */

#define W32L_HWND ((HWND)&w32l_window)

HWND w32l_the_window(void)
{
    return w32l_window.alive ? W32L_HWND : NULL;
}

WNDPROC w32l_window_proc(HWND hwnd)
{
    return hwnd == W32L_HWND && w32l_window.alive ? w32l_window.proc : NULL;
}

ATOM WINAPI RegisterClassA(const WNDCLASSA *wc)
{
    W32L_WindowClass *c;

    if (!wc || !wc->lpszClassName || w32l_class_count == 4)
        return 0;
    c = &w32l_classes[w32l_class_count++];
    strncpy(c->name, wc->lpszClassName, sizeof c->name - 1);
    c->proc = wc->lpfnWndProc;
    return (ATOM)w32l_class_count;
}

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR cls, LPCSTR title,
                            DWORD style, int32_t x, int32_t y, int32_t w,
                            int32_t h, HWND parent, HMENU menu,
                            HINSTANCE inst, LPVOID param)
{
    int32_t i;
    WNDPROC proc = NULL;

    (void)x; (void)y; (void)parent; (void)menu; (void)inst; (void)param;
    for (i = 0; i < w32l_class_count; i++)
        if (!strcmp(w32l_classes[i].name, cls))
            proc = w32l_classes[i].proc;
    if (!proc || w32l_window.alive)
        return NULL;
    if (w == CW_USEDEFAULT || w <= 0)
        w = 640;
    if (h == CW_USEDEFAULT || h <= 0)
        h = 480;

    w32l_window.proc = proc;
    w32l_window.style = style;
    w32l_window.exStyle = exStyle;
    w32l_window.w = w;
    w32l_window.h = h;
    w32l_window.alive = 1;
    w32l_host_window_open(title ? title : "", w, h);

    proc(W32L_HWND, WM_CREATE, 0, 0);
    /* What Windows sends a new visible window, in this order. The game's
     * frame tick is gated on WM_ACTIVATEAPP, so a window that never got it
     * would sit on a black screen. */
    proc(W32L_HWND, WM_ACTIVATEAPP, TRUE, 0);
    proc(W32L_HWND, WM_ACTIVATE, WA_ACTIVE, 0);
    proc(W32L_HWND, WM_SETFOCUS, 0, 0);
    if (style & WS_VISIBLE)
        w32l_queue_post(W32L_HWND, WM_PAINT, 0, 0);
    return W32L_HWND;
}

BOOL WINAPI DestroyWindow(HWND hwnd)
{
    if (hwnd != W32L_HWND || !w32l_window.alive)
        return FALSE;
    w32l_window.proc(W32L_HWND, WM_DESTROY, 0, 0);
    w32l_window.alive = 0;
    w32l_host_window_close();
    return TRUE;
}

BOOL WINAPI ShowWindow(HWND hwnd, int32_t cmd)
{
    (void)hwnd; (void)cmd;
    return TRUE;
}

BOOL WINAPI UpdateWindow(HWND hwnd)
{
    (void)hwnd;
    return TRUE;
}

/* The window has no frame of its own -- the host draws whatever decoration
 * it likes around a client area -- so the outer rectangle IS the client
 * rectangle, and sizing by either means the same thing. */
BOOL WINAPI AdjustWindowRectEx(LPRECT rc, DWORD style, BOOL menu, DWORD exStyle)
{
    (void)rc; (void)style; (void)menu; (void)exStyle;
    return TRUE;
}

BOOL WINAPI SetWindowPos(HWND hwnd, HWND after, int32_t x, int32_t y,
                         int32_t cx, int32_t cy, UINT flags)
{
    (void)after; (void)x; (void)y;
    if (hwnd != W32L_HWND || !w32l_window.alive)
        return FALSE;
    if (!(flags & SWP_NOSIZE) && cx > 0 && cy > 0 &&
        (cx != w32l_window.w || cy != w32l_window.h)) {
        w32l_window.w = cx;
        w32l_window.h = cy;
        w32l_host_window_open(NULL, cx, cy);
        w32l_window.proc(W32L_HWND, WM_SIZE, 0, MAKELONG(cx, cy));
    }
    return TRUE;
}

LONG WINAPI GetWindowLongA(HWND hwnd, int32_t index)
{
    if (hwnd != W32L_HWND)
        return 0;
    switch (index) {
    case GWL_STYLE:   return (LONG)w32l_window.style;
    case GWL_EXSTYLE: return (LONG)w32l_window.exStyle;
    }
    return 0;
}

LONG WINAPI SetWindowLongA(HWND hwnd, int32_t index, LONG value)
{
    LONG old = GetWindowLongA(hwnd, index);

    if (hwnd != W32L_HWND)
        return 0;
    switch (index) {
    case GWL_STYLE:   w32l_window.style = (DWORD)value; break;
    case GWL_EXSTYLE: w32l_window.exStyle = (DWORD)value; break;
    }
    return old;
}

BOOL WINAPI GetClientRect(HWND hwnd, LPRECT rc)
{
    if (hwnd != W32L_HWND || !rc)
        return FALSE;
    rc->left = rc->top = 0;
    rc->right = w32l_window.w;
    rc->bottom = w32l_window.h;
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT rc)
{
    int32_t x, y, w, h;

    if (hwnd != W32L_HWND || !rc)
        return FALSE;
    w32l_host_window_position(&x, &y, &w, &h);
    rc->left = x;
    rc->top = y;
    rc->right = x + w;
    rc->bottom = y + h;
    return TRUE;
}

BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT pt)
{
    int32_t x, y, w, h;

    if (hwnd != W32L_HWND || !pt)
        return FALSE;
    w32l_host_window_position(&x, &y, &w, &h);
    pt->x += x;
    pt->y += y;
    return TRUE;
}

BOOL WINAPI IsIconic(HWND hwnd)
{
    return hwnd == W32L_HWND && w32l_host_window_is_minimised();
}

HMENU WINAPI GetMenu(HWND hwnd) { (void)hwnd; return NULL; }

HWND WINAPI GetFocus(void)
{
    return w32l_window.alive && w32l_focused ? W32L_HWND : NULL;
}

HWND WINAPI GetActiveWindow(void)
{
    return GetFocus();
}

HWND WINAPI SetFocus(HWND hwnd)
{
    HWND old = GetFocus();
    if (hwnd == W32L_HWND)
        w32l_host_window_raise();
    return old;
}

BOOL WINAPI RedrawWindow(HWND hwnd, const RECT *rc, HANDLE rgn, UINT flags)
{
    (void)hwnd; (void)rc; (void)rgn; (void)flags;
    return TRUE;
}

/* Nothing ever needs painting: the game composes every frame itself and
 * the host shows what DirectDraw presents. So WM_PAINT has no damage. */
BOOL WINAPI GetUpdateRect(HWND hwnd, LPRECT rc, BOOL erase)
{
    (void)hwnd; (void)erase;
    if (rc)
        rc->left = rc->top = rc->right = rc->bottom = 0;
    return FALSE;
}

HDC WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT ps)
{
    if (ps) {
        memset(ps, 0, sizeof *ps);
        ps->hdc = GetDC(hwnd);
    }
    return ps ? ps->hdc : NULL;
}

BOOL WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps)
{
    if (ps && ps->hdc)
        ReleaseDC(hwnd, ps->hdc);
    return TRUE;
}

HICON WINAPI LoadIconA(HINSTANCE inst, LPCSTR name)
{
    (void)inst; (void)name;
    return (HICON)(uintptr_t)0x1C01u;
}

HCURSOR WINAPI LoadCursorA(HINSTANCE inst, LPCSTR name)
{
    (void)inst; (void)name;
    return (HCURSOR)(uintptr_t)0x1C02u;
}

/* The pointer is visible when ShowCursor's count is non-negative AND a
 * cursor is set. The game does both: it hides through the count at startup
 * and answers every WM_SETCURSOR with SetCursor(NULL). */
static HCURSOR w32l_cursor = (HCURSOR)(uintptr_t)0x1C02u;

static void w32l_cursor_apply(void)
{
    w32l_host_cursor_visible(w32l_cursor_count >= 0 && w32l_cursor != NULL);
}

HCURSOR WINAPI SetCursor(HCURSOR cur)
{
    HCURSOR old = w32l_cursor;
    w32l_cursor = cur;
    w32l_cursor_apply();
    return old;
}

int32_t WINAPI ShowCursor(BOOL show)
{
    w32l_cursor_count += show ? 1 : -1;
    w32l_cursor_apply();
    return w32l_cursor_count;
}

int32_t WINAPI GetSystemMetrics(int32_t index)
{
    int32_t w, h;

    w32l_host_display_size(&w, &h);
    switch (index) {
    case SM_CXSCREEN: return w;
    case SM_CYSCREEN: return h;
    }
    return 0;
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT param, PVOID out, UINT wini)
{
    (void)param; (void)wini;
    if (action == SPI_GETWORKAREA && out) {
        RECT *rc = (RECT *)out;
        int32_t w, h;
        w32l_host_display_size(&w, &h);
        rc->left = rc->top = 0;
        rc->right = w;
        rc->bottom = h;
        return TRUE;
    }
    return FALSE;
}

int32_t WINAPI MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type)
{
    (void)hwnd; (void)type;
    fprintf(stderr, "%s: %s\n", caption ? caption : "Error",
            text ? text : "");
    return 1;   /* IDOK */
}

BOOL WINAPI SetRect(LPRECT rc, int32_t l, int32_t t, int32_t r, int32_t b)
{
    if (!rc)
        return FALSE;
    rc->left = l;
    rc->top = t;
    rc->right = r;
    rc->bottom = b;
    return TRUE;
}

BOOL WINAPI IntersectRect(LPRECT out, const RECT *a, const RECT *b)
{
    RECT r;

    if (!out || !a || !b)
        return FALSE;
    r.left = a->left > b->left ? a->left : b->left;
    r.top = a->top > b->top ? a->top : b->top;
    r.right = a->right < b->right ? a->right : b->right;
    r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (r.left >= r.right || r.top >= r.bottom) {
        out->left = out->top = out->right = out->bottom = 0;
        return FALSE;
    }
    *out = r;
    return TRUE;
}

int32_t WINAPI wsprintfA(LPSTR out, LPCSTR fmt, ...)
{
    va_list ap;
    int32_t n;

    va_start(ap, fmt);
    n = vsnprintf(out, 1024, fmt, ap);
    va_end(ap);
    return n;
}

int32_t WINAPI lstrlenA(LPCSTR s)
{
    return s ? (int32_t)strlen(s) : 0;
}

/* ---- the message queue --------------------------------------------------------- */

#define W32L_QUEUE_CAP 512

static MSG             w32l_queue[W32L_QUEUE_CAP];
static int32_t         w32l_q_head, w32l_q_count;
static pthread_mutex_t w32l_q_lock = PTHREAD_MUTEX_INITIALIZER;

void w32l_queue_post(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MSG *m;

    pthread_mutex_lock(&w32l_q_lock);
    if (w32l_q_count < W32L_QUEUE_CAP) {
        m = &w32l_queue[(w32l_q_head + w32l_q_count) % W32L_QUEUE_CAP];
        m->hwnd = hwnd;
        m->message = msg;
        m->wParam = wp;
        m->lParam = lp;
        m->time = GetTickCount();
        m->pt.x = m->pt.y = 0;
        w32l_q_count++;
    }
    pthread_mutex_unlock(&w32l_q_lock);
}

static int32_t w32l_queue_take(MSG *out, int32_t remove)
{
    int32_t got = 0;

    pthread_mutex_lock(&w32l_q_lock);
    if (w32l_q_count > 0) {
        *out = w32l_queue[w32l_q_head];
        if (remove) {
            w32l_q_head = (w32l_q_head + 1) % W32L_QUEUE_CAP;
            w32l_q_count--;
        }
        got = 1;
    }
    pthread_mutex_unlock(&w32l_q_lock);
    return got;
}

BOOL WINAPI PostMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    w32l_queue_post(hwnd, msg, wp, lp);
    return TRUE;
}

void WINAPI PostQuitMessage(int32_t code)
{
    w32l_queue_post(NULL, WM_QUIT, (WPARAM)code, 0);
}

BOOL WINAPI PeekMessageA(LPMSG msg, HWND hwnd, UINT lo, UINT hi, UINT remove)
{
    (void)hwnd; (void)lo; (void)hi;
    w32l_host_pump();
    return w32l_queue_take(msg, (remove & PM_REMOVE) != 0);
}

BOOL WINAPI GetMessageA(LPMSG msg, HWND hwnd, UINT lo, UINT hi)
{
    while (!PeekMessageA(msg, hwnd, lo, hi, PM_REMOVE))
        Sleep(1);
    return msg->message != WM_QUIT;
}

/* WM_CHAR arrives from the host's text input already, so there is nothing
 * to translate -- and control.c's `type` command relies on a keydown NOT
 * producing a second character here (see its comment on duplicates). */
BOOL WINAPI TranslateMessage(const MSG *msg)
{
    (void)msg;
    return FALSE;
}

LRESULT WINAPI DispatchMessageA(const MSG *msg)
{
    WNDPROC proc = w32l_window_proc(msg->hwnd);

    if (!proc)
        return 0;
    return proc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC proc = w32l_window_proc(hwnd);
    return proc ? proc(hwnd, msg, wp, lp) : 0;
}

LRESULT WINAPI DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_SETCURSOR:
        w32l_cursor_apply();
        return TRUE;
    }
    return 0;
}

/* ---- what the host pump reports ---------------------------------------------------- */

void w32l_window_on_close(void)
{
    if (w32l_window.alive)
        w32l_queue_post(W32L_HWND, WM_CLOSE, 0, 0);
}

void w32l_window_on_focus(int32_t gained)
{
    if (!w32l_window.alive || w32l_focused == gained)
        return;
    w32l_focused = gained;
    if (gained) {
        w32l_queue_post(W32L_HWND, WM_ACTIVATEAPP, TRUE, 0);
        w32l_queue_post(W32L_HWND, WM_ACTIVATE, WA_ACTIVE, 0);
        w32l_queue_post(W32L_HWND, WM_SETFOCUS, 0, 0);
    } else {
        w32l_queue_post(W32L_HWND, WM_KILLFOCUS, 0, 0);
        w32l_queue_post(W32L_HWND, WM_ACTIVATE, WA_INACTIVE, 0);
        w32l_queue_post(W32L_HWND, WM_ACTIVATEAPP, FALSE, 0);
    }
}

void w32l_window_on_char(uint32_t ch)
{
    if (w32l_window.alive)
        w32l_queue_post(W32L_HWND, WM_CHAR, (WPARAM)ch, 1);
}

void w32l_window_on_key(int32_t down, uint32_t vk)
{
    if (w32l_window.alive)
        w32l_queue_post(W32L_HWND, down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)vk,
                       down ? 1 : (LPARAM)0xC0000001);
}

void w32l_window_on_moved(int32_t x, int32_t y)
{
    if (w32l_window.alive)
        w32l_queue_post(W32L_HWND, WM_MOVE, 0, MAKELONG(x, y));
}
