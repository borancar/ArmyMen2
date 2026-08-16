#include "control.h"
#include "hooklog.h"
#include "input.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 31337
#define MAX_LINE     512

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

static void handle_line(SOCKET s, char *line)
{
    char *argv[8];
    int   argc = tokenize(line, argv, 8);

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
