#ifndef AM2_REPORT_H
#define AM2_REPORT_H

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x0041E7A0. Report a failed step, with the HRESULT that caused it.
 *
 * Formats `fmt` and its arguments, prefixes the result with the HRESULT, and
 * shows it in a message box. Always returns 0, which is what lets every caller
 * write `return ReportError(...)` to mean "this step failed". */
int32_t __cdecl ReportError(HRESULT hr, const char *fmt, ...);

/* Original: 0x0041E750. Report and then ask the window to close.
 *
 * Same formatting and the same message box, followed by WM_CLOSE posted to the
 * game window -- so the box is dismissed and then the ordinary shutdown runs.
 * Also always returns 0. */
int32_t __cdecl FatalError(const char *fmt, ...);

int report_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_REPORT_H */
