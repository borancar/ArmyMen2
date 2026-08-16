/* Un-stub the game's own debug logger.
 *
 * The retail build reduced the logger at 0x0045CAA0 to a bare `ret`, but it did
 * so at the callee -- all 623 call sites still push their real format strings
 * and arguments. Pointing that address at a working implementation turns 599
 * recovered debug messages back on, live, during gameplay.
 *
 * Doing that naively crashes the game. Because the body was a no-op for the
 * whole of the game's shipping life, nothing ever validated what the call sites
 * pass, and some of them do not pass a string at all -- selecting Boot Camp
 * reaches sites whose "format string" is binary data. Handing that to
 * _vsnprintf means any byte pair that looks like %s dereferences a garbage
 * pointer, which is a reliable segfault.
 *
 * So this formats defensively rather than trusting the call sites:
 *   - the format string itself must be readable and printable, or the call is
 *     reported as suspect and skipped;
 *   - every %s argument is range-checked before it is dereferenced.
 *
 * Enabled with AM2_GAMELOG=1.
 */

#include "gamelog.h"
#include "hooklog.h"
#include "orig.h"
#include "patch.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounded, fault-tolerant strlen. Returns -1 if `p` is not a readable C string
 * of at most `max` printable characters. */
static int readable_cstr(const char *p, int max)
{
    int i;

    if (!p || (uintptr_t)p < 0x10000)
        return -1;
    for (i = 0; i < max; i++) {
        unsigned char c;
        if (IsBadReadPtr(p + i, 1))
            return -1;
        c = (unsigned char)p[i];
        if (c == '\0')
            return i;
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t')
            return -1;
        if (c > 0x7E)
            return -1;
    }
    return -1;
}

/* Format one conversion by handing the isolated spec back to the CRT, so the
 * fiddly parts (width, precision, flags) stay correct. We only take over
 * deciding which argument type to pull and whether it is safe to use. */
static void emit(char *out, size_t cap, size_t *at, const char *spec, char conv,
                 va_list *ap)
{
    char tmp[256];
    int  n = 0;

    switch (conv) {
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'c':
        n = _snprintf(tmp, sizeof tmp, spec, va_arg(*ap, int));
        break;
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
        n = _snprintf(tmp, sizeof tmp, spec, va_arg(*ap, double));
        break;
    case 'p':
        n = _snprintf(tmp, sizeof tmp, spec, va_arg(*ap, void *));
        break;
    case 's': {
        const char *s = va_arg(*ap, const char *);
        /* The whole reason this function exists. */
        if (readable_cstr(s, 512) < 0)
            n = _snprintf(tmp, sizeof tmp, "<bad:%p>", (void *)s);
        else
            n = _snprintf(tmp, sizeof tmp, spec, s);
        break;
    }
    default:
        n = _snprintf(tmp, sizeof tmp, "%s", spec);
        break;
    }

    if (n < 0)
        n = 0;
    if ((size_t)n > sizeof tmp - 1)
        n = (int)sizeof tmp - 1;
    tmp[n] = '\0';

    if (*at + (size_t)n < cap) {
        memcpy(out + *at, tmp, (size_t)n);
        *at += (size_t)n;
    }
}

static void safe_format(char *out, size_t cap, const char *fmt, va_list ap)
{
    size_t at = 0;

    while (*fmt && at < cap - 1) {
        char spec[64];
        int  si = 0;

        if (*fmt != '%') {
            out[at++] = *fmt++;
            continue;
        }
        spec[si++] = *fmt++;
        if (*fmt == '%') {
            out[at++] = '%';
            fmt++;
            continue;
        }
        /* flags, width, precision, length -- copied verbatim into `spec`. */
        while (*fmt && strchr("-+ #0", *fmt) && si < 40)
            spec[si++] = *fmt++;
        while (*fmt && (*fmt >= '0' && *fmt <= '9') && si < 48)
            spec[si++] = *fmt++;
        if (*fmt == '.') {
            spec[si++] = *fmt++;
            while (*fmt && (*fmt >= '0' && *fmt <= '9') && si < 56)
                spec[si++] = *fmt++;
        }
        while (*fmt && strchr("hlL", *fmt) && si < 58)
            spec[si++] = *fmt++;
        if (!*fmt)
            break;
        spec[si++] = *fmt;
        spec[si]   = '\0';
        emit(out, cap, &at, spec, *fmt, &ap);
        fmt++;
    }
    out[at < cap ? at : cap - 1] = '\0';
}

static void __cdecl game_log(const char *fmt, ...)
{
    char    buf[1024];
    size_t  n;
    va_list ap;

    /* A NULL format is the very first thing the game logs at startup. */
    if (!fmt)
        return;
    if (readable_cstr(fmt, 512) < 0) {
        static uint32_t suspect;
        if (++suspect <= 8)
            hooklog("gamelog: call site passed a non-string format (%p) -- "
                    "skipped; that argument is not a message", (void *)fmt);
        return;
    }

    va_start(ap, fmt);
    safe_format(buf, sizeof buf, fmt, ap);
    va_end(ap);

    /* Most messages carry their own trailing newline; the sink adds one. */
    n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    if (n)
        hooklog_raw(buf);
}

int gamelog_install(void)
{
    const char *opt = getenv("AM2_GAMELOG");

    if (!opt || *opt != '1') {
        hooklog("gamelog: disabled (set AM2_GAMELOG=1 to un-stub the logger)");
        return 0;
    }
    /* Variadic: only the format argument is at a known position, so trace 1. */
    return patch_replace(ADDR_LOG, (const void *)game_log, "Log", 1);
}
