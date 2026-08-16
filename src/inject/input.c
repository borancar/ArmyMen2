#include "input.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* DirectInput reports buffered events as (offset, value) pairs. For a keyboard
 * the offset is the DIK scancode; for a mouse it is the byte offset into
 * DIMOUSESTATE, so X/Y/Z are 0/4/8 and the buttons follow at 12. */
#define QUEUE_LEN 64
#define DIMOFS_X       0
#define DIMOFS_Y       4
#define DIMOFS_BUTTON0 12

struct event {
    uint32_t ofs;
    uint32_t data;
};

struct queue {
    struct event ev[QUEUE_LEN];
    uint32_t     head, tail;
};

static struct {
    uint8_t  held[256];
    uint32_t expiry[256];               /* 0 == until explicitly released */
    int32_t  dx, dy, dz;
    uint8_t  btn[AM2_MOUSE_BUTTONS];
    uint32_t btn_expiry[AM2_MOUSE_BUTTONS];
    struct queue kbd, mouse;
} g;

/* Caller holds the lock. Silently drops when full: a stuck-full queue means
 * the game has stopped reading, and blocking the socket thread would be worse
 * than losing input it was never going to see. */
static void push(struct queue *q, uint32_t ofs, uint32_t data)
{
    uint32_t next = (q->head + 1) % QUEUE_LEN;

    if (next == q->tail)
        return;
    q->ev[q->head].ofs  = ofs;
    q->ev[q->head].data = data;
    q->head = next;
}

static CRITICAL_SECTION g_lock;
static int              g_ready;

void input_init(void)
{
    if (g_ready)
        return;
    InitializeCriticalSection(&g_lock);
    memset(&g, 0, sizeof g);
    g_ready = 1;
}

void input_key(uint8_t dik, int32_t down, uint32_t hold_ms)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    g.held[dik]   = down ? 1 : 0;
    g.expiry[dik] = (down && hold_ms) ? GetTickCount() + hold_ms : 0;
    push(&g.kbd, dik, down ? 0x80u : 0u);
    LeaveCriticalSection(&g_lock);
}

void input_button(int32_t button, int32_t down, uint32_t hold_ms)
{
    if (!g_ready || button < 0 || button >= AM2_MOUSE_BUTTONS)
        return;
    EnterCriticalSection(&g_lock);
    g.btn[button]        = down ? 1 : 0;
    g.btn_expiry[button] = (down && hold_ms) ? GetTickCount() + hold_ms : 0;
    push(&g.mouse, (uint32_t)(DIMOFS_BUTTON0 + button), down ? 0x80u : 0u);
    LeaveCriticalSection(&g_lock);
}

void input_mouse_move(int32_t dx, int32_t dy, int32_t dz)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    g.dx += dx;
    g.dy += dy;
    g.dz += dz;
    if (dx)
        push(&g.mouse, DIMOFS_X, (uint32_t)dx);
    if (dy)
        push(&g.mouse, DIMOFS_Y, (uint32_t)dy);
    LeaveCriticalSection(&g_lock);
}

void input_clear(void)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    memset(g.held, 0, sizeof g.held);
    memset(g.expiry, 0, sizeof g.expiry);
    memset(g.btn, 0, sizeof g.btn);
    memset(g.btn_expiry, 0, sizeof g.btn_expiry);
    g.dx = g.dy = g.dz = 0;
    g.kbd.head = g.kbd.tail = 0;
    g.mouse.head = g.mouse.tail = 0;
    LeaveCriticalSection(&g_lock);
}

/* A timed hold expires the first time anyone looks at it after the deadline,
 * rather than on a timer thread. That keeps releases synchronised with the
 * game's own polling, so a tap can never be missed between two polls. */
static int expired(uint32_t deadline, uint32_t now)
{
    return deadline && (int32_t)(now - deadline) >= 0;
}

void input_pump(void)
{
    uint32_t now, i;

    if (!g_ready)
        return;
    now = GetTickCount();

    EnterCriticalSection(&g_lock);
    for (i = 0; i < 256; i++)
        if (g.held[i] && expired(g.expiry[i], now)) {
            g.held[i]   = 0;
            g.expiry[i] = 0;
            push(&g.kbd, i, 0);
        }
    for (i = 0; i < AM2_MOUSE_BUTTONS; i++)
        if (g.btn[i] && expired(g.btn_expiry[i], now)) {
            g.btn[i]        = 0;
            g.btn_expiry[i] = 0;
            push(&g.mouse, DIMOFS_BUTTON0 + i, 0);
        }
    LeaveCriticalSection(&g_lock);
}

uint32_t input_take_events(int32_t kind, void *buf, uint32_t elem,
                           uint32_t max, int32_t peek)
{
    static uint32_t seq;
    struct queue   *q;
    uint32_t        n = 0, tail, now;

    if (!g_ready || !buf || max == 0 || elem < 16)
        return 0;

    now = GetTickCount();
    EnterCriticalSection(&g_lock);
    q = (kind == AM2_DEV_KEYBOARD) ? &g.kbd
      : (kind == AM2_DEV_MOUSE)    ? &g.mouse
      : NULL;
    if (q) {
        tail = q->tail;
        while (n < max && tail != q->head) {
            /* DIDEVICEOBJECTDATA: dwOfs, dwData, dwTimeStamp, dwSequence. */
            uint32_t *slot = (uint32_t *)((uint8_t *)buf + (size_t)n * elem);
            slot[0] = q->ev[tail].ofs;
            slot[1] = q->ev[tail].data;
            slot[2] = now;
            slot[3] = ++seq;
            tail = (tail + 1) % QUEUE_LEN;
            n++;
        }
        if (!peek)
            q->tail = tail;
    }
    LeaveCriticalSection(&g_lock);
    return n;
}

void input_overlay_keyboard(uint8_t *state, uint32_t len)
{
    uint32_t now, i;

    if (!g_ready || !state)
        return;
    if (len > 256)
        len = 256;

    now = GetTickCount();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < len; i++) {
        if (!g.held[i])
            continue;
        if (expired(g.expiry[i], now)) {
            g.held[i]   = 0;
            g.expiry[i] = 0;
            continue;
        }
        state[i] |= 0x80;
    }
    LeaveCriticalSection(&g_lock);
}

void input_overlay_mouse(int32_t *axes, uint8_t *buttons, uint32_t nbuttons)
{
    uint32_t now, i;

    if (!g_ready)
        return;
    now = GetTickCount();

    EnterCriticalSection(&g_lock);
    if (axes) {
        axes[0] += g.dx;
        axes[1] += g.dy;
        axes[2] += g.dz;
        /* Relative motion is consumed by the poll that reports it. */
        g.dx = g.dy = g.dz = 0;
    }
    if (buttons) {
        if (nbuttons > AM2_MOUSE_BUTTONS)
            nbuttons = AM2_MOUSE_BUTTONS;
        for (i = 0; i < nbuttons; i++) {
            if (!g.btn[i])
                continue;
            if (expired(g.btn_expiry[i], now)) {
                g.btn[i]        = 0;
                g.btn_expiry[i] = 0;
                continue;
            }
            buttons[i] |= 0x80;
        }
    }
    LeaveCriticalSection(&g_lock);
}

/* ---- key names --------------------------------------------------------- */

static const struct { const char *name; uint8_t dik; } kKeys[] = {
    { "escape", 0x01 }, { "esc", 0x01 },
    { "1", 0x02 }, { "2", 0x03 }, { "3", 0x04 }, { "4", 0x05 }, { "5", 0x06 },
    { "6", 0x07 }, { "7", 0x08 }, { "8", 0x09 }, { "9", 0x0A }, { "0", 0x0B },
    { "minus", 0x0C }, { "equals", 0x0D }, { "back", 0x0E }, { "backspace", 0x0E },
    { "tab", 0x0F },
    { "q", 0x10 }, { "w", 0x11 }, { "e", 0x12 }, { "r", 0x13 }, { "t", 0x14 },
    { "y", 0x15 }, { "u", 0x16 }, { "i", 0x17 }, { "o", 0x18 }, { "p", 0x19 },
    { "lbracket", 0x1A }, { "rbracket", 0x1B },
    { "return", 0x1C }, { "enter", 0x1C },
    { "lcontrol", 0x1D }, { "ctrl", 0x1D },
    { "a", 0x1E }, { "s", 0x1F }, { "d", 0x20 }, { "f", 0x21 }, { "g", 0x22 },
    { "h", 0x23 }, { "j", 0x24 }, { "k", 0x25 }, { "l", 0x26 },
    { "semicolon", 0x27 }, { "apostrophe", 0x28 }, { "grave", 0x29 },
    { "lshift", 0x2A }, { "shift", 0x2A }, { "backslash", 0x2B },
    { "z", 0x2C }, { "x", 0x2D }, { "c", 0x2E }, { "v", 0x2F }, { "b", 0x30 },
    { "n", 0x31 }, { "m", 0x32 }, { "comma", 0x33 }, { "period", 0x34 },
    { "slash", 0x35 }, { "rshift", 0x36 }, { "multiply", 0x37 },
    { "lmenu", 0x38 }, { "alt", 0x38 }, { "space", 0x39 }, { "capital", 0x3A },
    { "f1", 0x3B }, { "f2", 0x3C }, { "f3", 0x3D }, { "f4", 0x3E }, { "f5", 0x3F },
    { "f6", 0x40 }, { "f7", 0x41 }, { "f8", 0x42 }, { "f9", 0x43 }, { "f10", 0x44 },
    { "numlock", 0x45 }, { "scroll", 0x46 },
    { "numpad7", 0x47 }, { "numpad8", 0x48 }, { "numpad9", 0x49 },
    { "subtract", 0x4A }, { "numpad4", 0x4B }, { "numpad5", 0x4C },
    { "numpad6", 0x4D }, { "add", 0x4E }, { "numpad1", 0x4F },
    { "numpad2", 0x50 }, { "numpad3", 0x51 }, { "numpad0", 0x52 },
    { "decimal", 0x53 }, { "f11", 0x57 }, { "f12", 0x58 },
    { "numpadenter", 0x9C }, { "rcontrol", 0x9D }, { "divide", 0xB5 },
    { "sysrq", 0xB7 }, { "rmenu", 0xB8 }, { "pause", 0xC5 },
    { "home", 0xC7 }, { "up", 0xC8 }, { "prior", 0xC9 }, { "pageup", 0xC9 },
    { "left", 0xCB }, { "right", 0xCD }, { "end", 0xCF }, { "down", 0xD0 },
    { "next", 0xD1 }, { "pagedown", 0xD1 }, { "insert", 0xD2 }, { "delete", 0xD3 },
};

uint8_t input_dik_from_name(const char *name)
{
    char   low[32];
    size_t i, n;

    if (!name || !*name)
        return 0;

    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
        return (uint8_t)strtoul(name, NULL, 16);

    n = strlen(name);
    if (n >= sizeof low)
        return 0;
    for (i = 0; i < n; i++)
        low[i] = (char)((name[i] >= 'A' && name[i] <= 'Z')
                        ? name[i] - 'A' + 'a' : name[i]);
    low[n] = '\0';

    for (i = 0; i < sizeof kKeys / sizeof kKeys[0]; i++)
        if (strcmp(low, kKeys[i].name) == 0)
            return kKeys[i].dik;
    return 0;
}

void input_describe(char *out, uint32_t cap)
{
    uint32_t at = 0, i;

    if (!out || cap < 8)
        return;
    out[0] = '\0';
    if (!g_ready)
        return;

    EnterCriticalSection(&g_lock);
    at += (uint32_t)_snprintf(out + at, cap - at, "keys:");
    for (i = 0; i < 256 && at < cap - 12; i++)
        if (g.held[i])
            at += (uint32_t)_snprintf(out + at, cap - at, " %02x", i);
    at += (uint32_t)_snprintf(out + at, cap - at, " buttons:");
    for (i = 0; i < AM2_MOUSE_BUTTONS && at < cap - 8; i++)
        if (g.btn[i])
            at += (uint32_t)_snprintf(out + at, cap - at, " %u", i);
    _snprintf(out + at, cap - at, " motion: %d,%d,%d", g.dx, g.dy, g.dz);
    LeaveCriticalSection(&g_lock);
    out[cap - 1] = '\0';
}
