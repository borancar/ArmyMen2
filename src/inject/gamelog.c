/* Un-stub the game's own debug logger.
 *
 * The retail build reduced the logger at 0x0045CAA0 to a bare `ret`, but it did
 * so at the callee -- all 623 call sites still push their real format strings
 * and arguments. Pointing that address at a working implementation turns 599
 * recovered debug messages back on, live, during gameplay.
 *
 * This is opt-in via AM2_GAMELOG=1, and deliberately so. Because the body was a
 * no-op for the whole of the game's shipping life, any call site whose argument
 * list drifted out of sync with its format string would never have been
 * noticed. Re-enabling the logger makes those latent mismatches reachable, and
 * a stale %s will read a garbage pointer. Default off; enable when you want the
 * commentary and can tolerate the risk.
 */

#include "gamelog.h"
#include "hooklog.h"
#include "orig.h"
#include "patch.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void __cdecl game_log(const char *fmt, ...)
{
    char    buf[1024];
    size_t  n;
    va_list ap;

    if (!fmt)
        return;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = '\0';

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
