/* glue.cpp -- CRT-seam bindings and milestone stubs for the VC6 LINK milestone.
 *
 * The reconstruction reaches the game's CRT through function-pointer seams
 * (crt.h's AM2_ORIGFN: am2_malloc, am2_free, am2_log, ...), all C linkage.
 * The injected build fills them from the game's own statically-linked MSVC
 * CRT; the native build points them at the reconstructed CRT.  For the VC6
 * standalone the choice (per the build decision) is VC6's real MSVCRT, so each
 * seam is bound to the genuine CRT here.  Compiled by VC6 CL, linked against
 * LIBC.LIB.
 *
 * It also STUBS the symbols the reconstruction references that this image
 * otherwise lacks: the whole comm/multiplayer layer (defined in dplay.cpp,
 * which VC98's DirectX 6.0 headers can't compile -- IDirectPlay4A/Lobby3), the
 * the injected harness's hooklog /
 * restore_multiplayer / patch_replace, and the host trace/pump hooks.
 *
 * These are MILESTONE stubs: they make the image LINK into a valid PE, which
 * proves the VC6 compile -> blob -> LINK.EXE pipeline end to end.  They are NOT
 * a runtime -- a correct-running image needs the comm layer really
 * built and the blob's baked code-pointers rewritten (the fixups dataset).
 * Including dplay.h gives every comm stub the exact linkage/decoration its
 * callers expect (the extern "C" ones stay C, the rest keep C++ mangling).
 */
#include "../game/win32/dplay.h"     /* comm decls + windows types (HANDLE/DWORD/GUID) */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <stdarg.h>

/* ---- CRT seams (crt.h, C linkage) -> the genuine MSVC CRT ---------------- */
extern "C" {
static void am2_log_impl(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
void *(*am2_malloc)(size_t) = malloc;
void *(*am2_realloc)(void *, size_t) = realloc;
void  (*am2_free)(void *) = free;
void  (*am2_log)(const char *, ...) = am2_log_impl;
long  (*am2_strtol)(const char *, char **, int) = strtol;
char *(*am2_strtok)(char *, const char *) = strtok;
int   (*am2_sprintf)(char *, const char *, ...) = sprintf;
int   (*am2_chdir)(const char *) = _chdir;
char *(*am2_getcwd)(char *, int) = (char *(*)(char *, int))_getcwd;

/* ---- host / trace / CRT hooks (platform.h + crt.h, C linkage) ------------ */
unsigned int am2_host_pump_number(void) { return 0; }
int am2_trace_window(void) { return 0; }
void am2_crt_use_game(void) { }

/* ---- injected-harness stubs (unused in a drop-in image) ------------------ */
int patch_replace(unsigned int target, const void *replacement, const char *name, int nargs)
{ (void)target; (void)replacement; (void)name; (void)nargs; return 0; }
void hooklog(const char *fmt, ...) { (void)fmt; }
int restore_multiplayer(void) { return 0; }

} /* extern "C" */

