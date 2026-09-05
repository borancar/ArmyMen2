/* devtools.cpp -- what the hybrid's development binary supplies to the
 * harness's control socket, which reads the game's globals by ADDRESS and
 * so works unchanged on the original code: `dump`, `peek`, `cursor`,
 * `keys` and the object-table walk tools/objdump.py does over `dump`.
 *
 * control.c also wants the trace counters, the savestate commands and a
 * logger. There are no patch stubs here, so no counters; no arena, so no
 * savestates; and the logger goes where the game's own log goes.
 */
#include "../platform/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void am2_hybrid_log_line(const char *fmt, va_list ap);

extern "C" void hooklog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    am2_hybrid_log_line(fmt, ap);
    va_end(ap);
}

extern "C" void trace_describe(char *out, uint32_t cap, const char *want)
{
    (void)want;
    if (cap)
        snprintf(out, cap, "(no counters: the hybrid runs the original unpatched)");
}

extern "C" int32_t devtools_command(int32_t argc, char **argv, char *out, size_t cap)
{
    (void)argc; (void)argv; (void)out; (void)cap;
    return 0;
}

extern "C" void devtools_hotkey(int32_t which) { (void)which; }
