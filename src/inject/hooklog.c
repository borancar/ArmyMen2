#include "hooklog.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

static FILE       *g_fp;
static CRITICAL_SECTION g_lock;
static int         g_ready;

void hooklog_open(void)
{
    const char *path = getenv("AM2_LOG");

    InitializeCriticalSection(&g_lock);
    g_ready = 1;

    /* Our own msvcrt FILE*, never the game's -- see orig.h. */
    g_fp = fopen(path && *path ? path : "am2.log", "w");
    if (g_fp)
        setvbuf(g_fp, NULL, _IOLBF, 4096);
}

void hooklog_close(void)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    if (g_fp) {
        fclose(g_fp);
        g_fp = NULL;
    }
    LeaveCriticalSection(&g_lock);
}

void hooklog_raw(const char *line)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    if (g_fp) {
        fputs(line, g_fp);
        fputc('\n', g_fp);
    }
    LeaveCriticalSection(&g_lock);
    /* Also visible under WINEDEBUG=+debugstr / a debugger. */
    OutputDebugStringA(line);
}

void hooklog(const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = '\0';

    hooklog_raw(buf);
}
