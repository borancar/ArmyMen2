/* Error reporting -- reconstructed from ArmyMen2.exe.
 *
 *   ReportError  0x0041E7A0  11 direct callers
 *   FatalError   0x0041E750   1 direct caller
 *
 * Two small Win32 boundary functions, and the only places in the game that put
 * a message box on the screen. Between them they are how every DirectDraw and
 * DirectInput failure reaches a human -- src/game/device.cpp and
 * src/game/surface.cpp both end their failure paths in ReportError.
 *
 * Both return 0. That is not incidental: it is what lets a caller write
 * `return ReportError(...)` and have it mean "this step failed", which is
 * exactly what InitInput does eight times over.
 *
 * The difference between them is what happens next. ReportError describes a
 * step that failed and lets the game carry on deciding what to do; FatalError
 * posts WM_CLOSE to the game window afterwards, so the message box is the last
 * thing that happens before the message loop winds down.
 *
 * Formatting goes through the game's own CRT vsprintf at 0x00465A45 rather than
 * ours. On i386 a va_list is a bare pointer into the caller's frame with no
 * runtime state behind it, so handing ours to the game's vsprintf is safe -- and
 * it keeps the formatting of the game's own format strings byte-identical
 * instead of merely equivalent.
 */

#include "report.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <stdarg.h>

static_assert(WM_CLOSE == 0x10, "WM_CLOSE");

#define g_hWnd      (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_errorText ((char *)(uintptr_t)ADDR_ERROR_TEXT)
#define g_errorTextDD ((char *)(uintptr_t)ADDR_ERROR_TEXT_DD)

/* The game's CRT, not ours -- see the note above. */
typedef int32_t (__cdecl *am2_vsprintf_fn)(char *buf, const char *fmt, va_list ap);
#define orig_vsprintf (*(am2_vsprintf_fn)ADDR_VSPRINTF)

int32_t __cdecl ReportError(HRESULT hr, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    orig_vsprintf(g_errorText, fmt, ap);
    va_end(ap);

    /* The step's description, prefixed with the HRESULT that caused it. */
    wsprintfA(g_errorTextDD, "DDERROR %08lx: %s", (unsigned long)hr, g_errorText);
    MessageBoxA(g_hWnd, g_errorTextDD, "ERROR", 0);
    return 0;
}

int32_t __cdecl FatalError(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    orig_vsprintf(g_errorText, fmt, ap);
    va_end(ap);

    MessageBoxA(g_hWnd, g_errorText, "ERROR", 0);
    /* Posted rather than sent, so the box is dismissed first and the shutdown
     * runs through the ordinary message loop. */
    PostMessageA(g_hWnd, WM_CLOSE, 0, 0);
    return 0;
}

int report_install(void)
{
    int rc = 0;

    /* nargs counts the fixed arguments only; the trace has no way to know how
     * many followed. */
    rc |= patch_replace(ADDR_REPORT_ERROR, (const void *)ReportError,
                        "ReportError", 2);
    rc |= patch_replace(ADDR_FATAL_ERROR, (const void *)FatalError,
                        "FatalError", 1);
    return rc;
}
