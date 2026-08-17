#include "control.h"
#include "hooklog.h"
#include "input.h"
#include "trace.h"
#include "orig.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 31337
/* Big enough for `counts` to name all MAX_TRACED functions at once. At 512 it
 * was not: the reply stopped mid-list and the four functions patched last never
 * appeared at all, which reads exactly like they were never installed. Raised
 * again with MAX_TRACED, which is the other half of the same failure. The
 * client frames on a newline and does not care how long a line is. */
#define MAX_LINE     4096

static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_thread;
static volatile LONG g_stop;

static int reply(SOCKET s, const char *fmt, ...)
{
    char    buf[MAX_LINE];
    int     n;
    va_list ap;

    va_start(ap, fmt);
    n = _vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        n = 0;
    buf[n] = '\n';
    return send(s, buf, n + 1, 0);
}

/* Split on spaces in place. Returns the token count. */
static int tokenize(char *line, char **argv, int max)
{
    int argc = 0;

    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t')
            *line++ = '\0';
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t')
            line++;
    }
    return argc;
}

static int button_from_name(const char *s)
{
    if (!strcmp(s, "left"))   return 0;
    if (!strcmp(s, "right"))  return 1;
    if (!strcmp(s, "middle")) return 2;
    if (s[0] >= '0' && s[0] <= '9')
        return atoi(s);
    return -1;
}

/* `tap` is a timed hold rather than an immediate press-release pair: the game
 * polls DirectInput once per frame, and an instant release would land between
 * two polls and never be observed. */
#define DEFAULT_TAP_MS 120

/* One frame is plenty; the field consumes a character per pump. */
#define TYPE_GAP_MS    40

static void handle_line(SOCKET s, char *line)
{
    char *argv[8];
    int   argc;

    /* `type <text>` goes first, before the line is split: the text is the rest
     * of the line verbatim, spaces and all, and tokenising would eat them.
     *
     * A text field reads WM_CHAR, which DirectInput never produces -- so the
     * `key` command below, which is what the game polls for menus and
     * movement, cannot fill one in. Posting WM_CHAR to the window directly
     * does, and it needs no X server cooperation at all: no XTEST, no real
     * key events, nothing outside the process. */
    if (!strncmp(line, "type ", 5)) {
        HWND        hwnd = *(HWND *)(uintptr_t)ADDR_HWND;
        const char *p    = line + 5;
        int         n    = 0;

        if (!hwnd) {
            reply(s, "err no window yet");
            return;
        }
        /* The real thing: WM_KEYDOWN, WM_CHAR, WM_KEYUP for every character.
         *
         * Two behaviours of this game decide the timing between them, and
         * getting either wrong is silent.
         *
         * WndProc drops a WM_CHAR when the previously dispatched message was
         * also a WM_CHAR -- reproduced from the original in winproc.cpp,
         * because a genuine keystroke is a keydown followed by a char, so two
         * chars running can only be a duplicate. Post a bare string and its
         * first character arrives and the rest vanish.
         *
         * And PumpMessage calls TranslateMessage, which turns our keydown into
         * a WM_CHAR of its own and appends it to the queue. So every character
         * is delivered twice unless something eats one -- and the duplicate
         * check above is exactly the thing that will, provided the translated
         * copy lands immediately after ours rather than after the keyup.
         *
         * Hence the pause before the keyup: it lets the pump dispatch our char
         * and the translated one back to back, so the game discards the second
         * itself. The two together produce one character, correctly cased,
         * from the full three-message sequence a keyboard would send. */
        for (; *p; p++, n++) {
            unsigned char c  = (unsigned char)*p;
            WPARAM        vk = (c >= 'a' && c <= 'z') ? (WPARAM)(c - 32) : (WPARAM)c;

            PostMessageA(hwnd, WM_KEYDOWN, vk, 1);
            PostMessageA(hwnd, WM_CHAR, (WPARAM)c, 1);
            Sleep(TYPE_GAP_MS);
            PostMessageA(hwnd, WM_KEYUP, vk, 0xC0000001u);
            Sleep(TYPE_GAP_MS);
        }
        reply(s, "ok typed %d char(s)", n);
        return;
    }

    argc = tokenize(line, argv, 8);

    if (argc == 0)
        return;

    if (!strcmp(argv[0], "ping")) {
        reply(s, "ok pong");
        return;
    }
    if (!strcmp(argv[0], "clear")) {
        input_clear();
        reply(s, "ok cleared");
        return;
    }
    /* `dump <hex addr> [len]` -- read the game's memory. Sprite and glyph data
     * only exists at runtime, so decoding an encoding by hand needs a way to
     * look at the real bytes. Bounded well under the reply buffer. */
    if (!strcmp(argv[0], "dump") && argc >= 2) {
        char           out[MAX_LINE];
        const uint8_t *p = (const uint8_t *)(uintptr_t)strtoul(argv[1], NULL, 16);
        uint32_t       n = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 32;
        uint32_t       i, at = 0;

        if (n == 0 || n > 96)
            n = 96;
        if (IsBadReadPtr(p, n)) {
            reply(s, "err %p not readable for %u bytes", (void *)p, n);
            return;
        }
        for (i = 0; i < n && at + 3 < sizeof out; i++)
            at += (uint32_t)_snprintf(out + at, sizeof out - at, "%02x", p[i]);
        out[at] = '\0';
        reply(s, "ok %p %s", (void *)p, out);
        return;
    }
    if (!strcmp(argv[0], "counts")) {
        char buf[MAX_LINE - 8];
        trace_describe(buf, sizeof buf);
        reply(s, "ok %s", buf[0] ? buf : "(nothing traced)");
        return;
    }
    if (!strcmp(argv[0], "state")) {
        char buf[MAX_LINE - 8];
        input_describe(buf, sizeof buf);
        reply(s, "ok %s", buf);
        return;
    }
    if (!strcmp(argv[0], "key") && argc >= 3) {
        uint8_t  dik = input_dik_from_name(argv[1]);
        uint32_t ms  = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10)
                                   : DEFAULT_TAP_MS;
        if (!dik) {
            reply(s, "err unknown key '%s'", argv[1]);
            return;
        }
        if (!strcmp(argv[2], "down"))
            input_key(dik, 1, (argc >= 4) ? ms : 0);
        else if (!strcmp(argv[2], "up"))
            input_key(dik, 0, 0);
        else if (!strcmp(argv[2], "tap"))
            input_key(dik, 1, ms);
        else {
            reply(s, "err expected down|up|tap");
            return;
        }
        reply(s, "ok key %s(%02x) %s", argv[1], dik, argv[2]);
        return;
    }
    if (!strcmp(argv[0], "mouse") && argc >= 2) {
        if (!strcmp(argv[1], "move") && argc >= 4) {
            input_mouse_move(atoi(argv[2]), atoi(argv[3]),
                             argc >= 5 ? atoi(argv[4]) : 0);
            reply(s, "ok mouse move");
            return;
        }
        if (argc >= 3) {
            int      btn = button_from_name(argv[1]);
            uint32_t ms  = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10)
                                       : DEFAULT_TAP_MS;
            if (btn < 0) {
                reply(s, "err unknown button '%s'", argv[1]);
                return;
            }
            if (!strcmp(argv[2], "down"))
                input_button(btn, 1, (argc >= 4) ? ms : 0);
            else if (!strcmp(argv[2], "up"))
                input_button(btn, 0, 0);
            else if (!strcmp(argv[2], "tap"))
                input_button(btn, 1, ms);
            else {
                reply(s, "err expected down|up|tap");
                return;
            }
            reply(s, "ok mouse %d %s", btn, argv[2]);
            return;
        }
    }
    reply(s, "err unknown command '%s'", argv[0]);
}

/* One client is served at a time, which is ample for scripting -- but without a
 * timeout a client that connects and then stalls (or is killed mid-request, or
 * whose socket is left dangling) wedges the listener for the life of the
 * process. New connections then sit in the backlog and time out, which looks
 * exactly like a hung game. A receive timeout makes that self-healing. */
#define CLIENT_IDLE_MS 15000

static void serve(SOCKET client)
{
    char buf[MAX_LINE];
    int  used = 0;
    DWORD tmo = CLIENT_IDLE_MS;

    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);

    for (;;) {
        int n = recv(client, buf + used, (int)(sizeof buf - used - 1), 0);
        char *nl;

        if (n <= 0)
            return;                      /* closed, error, or idle timeout */
        used += n;
        buf[used] = '\0';

        while ((nl = strchr(buf, '\n')) != NULL) {
            int len = (int)(nl - buf);
            *nl = '\0';
            if (len && buf[len - 1] == '\r')
                buf[len - 1] = '\0';
            handle_line(client, buf);
            memmove(buf, nl + 1, (size_t)(used - len - 1));
            used -= len + 1;
            buf[used] = '\0';
        }
        if (used >= (int)sizeof buf - 1)
            used = 0;                       /* overlong line: drop it */
    }
}

static DWORD WINAPI control_thread(LPVOID unused)
{
    (void)unused;

    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        SOCKET client = accept(g_listen, NULL, NULL);
        if (client == INVALID_SOCKET)
            break;
        serve(client);
        closesocket(client);
    }
    return 0;
}

int control_start(void)
{
    const char        *opt = getenv("AM2_CONTROL");
    const char        *pstr = getenv("AM2_CTL_PORT");
    uint16_t           port = pstr && *pstr ? (uint16_t)atoi(pstr) : DEFAULT_PORT;
    WSADATA            wsa;
    struct sockaddr_in addr;
    int                yes = 1;

    if (!opt || *opt != '1') {
        hooklog("control: disabled (set AM2_CONTROL=1 to enable)");
        return 0;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        hooklog("control: WSAStartup failed");
        return 1;
    }
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) {
        hooklog("control: socket() failed");
        return 1;
    }
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* loopback only */

    if (bind(g_listen, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(g_listen, 4) != 0) {
        hooklog("control: bind/listen on port %u failed (%d)", port, WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return 1;
    }

    g_thread = CreateThread(NULL, 0, control_thread, NULL, 0, NULL);
    if (!g_thread) {
        hooklog("control: CreateThread failed");
        return 1;
    }
    hooklog("control: listening on 127.0.0.1:%u", port);
    return 0;
}

void control_stop(void)
{
    InterlockedExchange(&g_stop, 1);
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
}
