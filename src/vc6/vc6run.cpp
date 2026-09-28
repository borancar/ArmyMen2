/* vc6run.cpp -- slim runtime for the RUNNABLE VC6 standalone (make vc6-link
 * --run), the symbol-relative build whose goal is to RUN even though LINK.EXE
 * places sections at non-original VAs.  That it runs at all is the proof the
 * reconstruction uses proper structs/function-pointers, not absolute addresses.
 *
 * It is NOT src/standalone/runtime.cpp: that one pulls in platform.h (GCC
 * attributes VC6 can't parse) and the reconstructed CRT (crt_startup/crt_malloc
 * -- POSIX, GCC-only).  Here the CRT is VC6's real MSVCRT (LIBC.LIB): LIBC's
 * WinMainCRTStartup runs the C++ static initialisers and then WinMain, so no
 * hand-rolled CRT bring-up is needed.  This file supplies only the symbols the
 * AM2_STANDALONE game TUs still reference that MSVCRT does not provide.
 *
 * Everything here is milestone-minimal; the runtime reliances the RUN then
 * faults on (un-migrated globals read by absolute VA, the carried blob's moved
 * self-pointers) are the absolute-address dependencies to migrate away, one at
 * a time, until the image boots.
 */
#include "../game/win32/dplay.h"     /* comm decls + windows types, for the stubs */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

extern "C" {

/* ---- CRT seams (crt.h, C linkage) -> the genuine MSVC CRT ---------------- */
/* A logger "format" the game passes is only formatted if it is a readable
 * string that starts with text -- some call sites pass none at all (the retail
 * logger is a bare `ret`); see am2_sa_log below and runtime.cpp. */
static int vc6_fmt_ok(const char *fmt)
{
    int i;
    if (IsBadStringPtrA(fmt, 4096))
        return 0;
    for (i = 0; i < 8 && fmt[i]; i++)
        if ((unsigned char)fmt[i] >= 0x7F ||
            ((unsigned char)fmt[i] < 0x20 && fmt[i] != '\n' && fmt[i] != '\t' && fmt[i] != '\r'))
            return 0;
    return 1;
}
/* VC6RUN_LOG=<path> also writes the log to a file: under `wine explorer
 * /desktop=...` (the full-colour way to drive it) the game's stderr is not
 * connected to anything. */
static FILE *vc6_logfile(void)
{
    static int opened;
    static FILE *f;
    if (!opened) {
        const char *p = getenv("VC6RUN_LOG");
        opened = 1;
        if (p && *p)
            f = fopen(p, "w");
    }
    return f;
}
static void am2_log_impl(const char *fmt, ...)
{
    va_list ap;
    FILE *lf;
    if (!vc6_fmt_ok(fmt))
        return;
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
    if ((lf = vc6_logfile()) != 0) {
        va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap); fflush(lf);
    }
}
/* VC6RUN_ZEROHEAP=1 (diagnostic): hand out zero-filled blocks, to test
 * whether a behaviour depends on memory the game never initialised. */
static void *vc6_malloc(size_t n)
{
    static int zero = -1;
    void *p = malloc(n);
    if (zero < 0) zero = getenv("VC6RUN_ZEROHEAP") != 0;
    if (p && zero) memset(p, 0, n);
    return p;
}
void *(*am2_malloc)(size_t) = vc6_malloc;
void *(*am2_realloc)(void *, size_t) = realloc;
void  (*am2_free)(void *) = free;
void  (*am2_log)(const char *, ...) = am2_log_impl;
long  (*am2_strtol)(const char *, char **, int) = strtol;
char *(*am2_strtok)(char *, const char *) = strtok;
int   (*am2_sprintf)(char *, const char *, ...) = sprintf;
int   (*am2_chdir)(const char *) = _chdir;
char *(*am2_getcwd)(char *, int) = (char *(*)(char *, int))_getcwd;

/* Reconstructed-CRT calls the game TUs make by symbol -> the real MSVCRT.
 * Signatures match crt.h exactly (dplay.h pulls it in); CRT_FILE* is treated
 * as an opaque handle -- since ALL file ops route through these wrappers, the
 * real FILE* returned by crt_fopen flows back in unexamined, so the cast is
 * safe. */
uint32_t crt_fread(void *buf, uint32_t size, uint32_t count, CRT_FILE *f)
{ return (uint32_t)fread(buf, size, count, (FILE *)f); }
uint32_t crt_fwrite(const void *buf, uint32_t size, uint32_t count, CRT_FILE *f)
{ return (uint32_t)fwrite(buf, size, count, (FILE *)f); }
int32_t crt_rand(void) { return rand(); }
int32_t crt_time(int32_t *out)
{ time_t now = time(NULL); if (out) *out = (int32_t)now; return (int32_t)now; }
void *crt_malloc(uint32_t n) { return vc6_malloc(n); }
CRT_FILE *crt_fopen(const char *path, const char *mode) { return (CRT_FILE *)fopen(path, mode); }
int32_t crt_fclose(CRT_FILE *f) { return fclose((FILE *)f); }
char *crt_fgets(char *s, int32_t n, CRT_FILE *f) { return fgets(s, n, (FILE *)f); }
int32_t crt_findclose(int32_t handle) { return _findclose(handle); }
void *crt_bsearch(const void *key, const void *base, uint32_t num, uint32_t width, crt_compare_fn cmp)
{ return bsearch(key, base, num, width, cmp); }
void crt_qsort(void *base, uint32_t num, uint32_t width, crt_compare_fn cmp)
{ qsort(base, num, width, cmp); }
int32_t crt_sprintf(char *b, const char *f, ...)
{ va_list ap; va_start(ap, f); int r = vsprintf(b, f, ap); va_end(ap); return (int32_t)r; }
int32_t crt_vsprintf(char *b, const char *f, va_list ap) { return (int32_t)vsprintf(b, f, ap); }
int32_t crt_stricmp(const char *a, const char *b) { return _stricmp(a, b); }
char *crt_strlwr(char *s) { return _strlwr(s); }
double crt_strtod(const char *s, char **end) { return strtod(s, end); }
void crt_free(void *p) { free(p); }
void *crt_realloc(void *p, uint32_t n) { return realloc(p, n); }
int32_t crt_atoi(const char *s) { return atoi(s); }
void *crt_memmove(void *d, const void *s, uint32_t n) { return memmove(d, s, n); }
char *crt_strtok(char *s, const char *d) { return strtok(s, d); }
char *crt_strchr(const char *s, int32_t c) { return strchr(s, c); }
char *crt_strstr(const char *s, const char *sub) { return strstr(s, sub); }
char *crt_strncpy(char *d, const char *s, uint32_t n) { return strncpy(d, s, n); }
int32_t crt_strncmp(const char *a, const char *b, uint32_t n) { return strncmp(a, b, n); }
int32_t crt_fseek(CRT_FILE *f, int32_t off, int32_t whence) { return fseek((FILE *)f, off, whence); }
int32_t crt_ftell(CRT_FILE *f) { return ftell((FILE *)f); }
int32_t crt_fflush(CRT_FILE *f) { return fflush((FILE *)f); }
int32_t crt_mkdir(const char *p) { return _mkdir(p); }
int32_t crt_rmdir(const char *p) { return _rmdir(p); }
int32_t crt_remove(const char *p) { return remove(p); }
int32_t crt_chmod(const char *p, int32_t m) { return _chmod(p, m); }
int32_t crt_atexit(CRT_ExitFn fn) { (void)fn; return 0; }  /* boot: handlers unneeded */
/* CRT_FINDDATA is the MSVC6 _finddata_t exactly (attrib, three time_t, size,
 * name[260]: 0x118 bytes), so the real CRT's record is copied straight across. */
typedef char vc6_finddata_size_check[sizeof(CRT_FINDDATA) == sizeof(struct _finddata_t) ? 1 : -1];
int32_t crt_findfirst(const char *spec, CRT_FINDDATA *out)
{
    struct _finddata_t fd;
    long h = _findfirst(spec, &fd);
    if (h != -1) memcpy(out, &fd, sizeof fd);
    return (int32_t)h;
}
int32_t crt_findnext(int32_t h, CRT_FINDDATA *out)
{
    struct _finddata_t fd;
    int r = _findnext(h, &fd);
    if (r == 0) memcpy(out, &fd, sizeof fd);
    return r;
}

/* Standalone glue runtime.cpp defined (all extern "C", __stdcall for the
 * DirectX trio) -> the real Wine DirectX.  win32.h (via dplay.h) already
 * declares DirectDrawCreate/DirectInputCreateA/DirectSoundCreate and the
 * CINTERFACE pointer types; the import libs (ddraw/dinput/dsound) are linked. */
int32_t __stdcall am2_sa_ddraw_create(void *guid, void **out, void *outer)
{ return (int32_t)DirectDrawCreate((GUID *)guid, (LPDIRECTDRAW *)out, (IUnknown *)outer); }
int32_t __stdcall am2_sa_dinput_create(void *inst, uint32_t ver, void **out, void *outer)
{ return (int32_t)DirectInputCreateA((HINSTANCE)inst, (DWORD)ver, (LPDIRECTINPUTA *)out, (IUnknown *)outer); }
int32_t __stdcall am2_sa_dsound_create(void *guid, void **out, void *outer)
{ return (int32_t)DirectSoundCreate((GUID *)guid, (LPDIRECTSOUND *)out, (IUnknown *)outer); }
void *am2_sa_operator_new(size_t n) { return vc6_malloc(n ? n : 1); }
void am2_sa_operator_delete(void *p) { free(p); }
void am2_sa_free_army_lists(void) { }
int32_t am2_sa_unimplemented(void) { return 0; }

/* Misc harness/CRT/input symbols the excluded TUs defined -- milestone stubs. */
void am2_crt_use_game(void) { }
void hooklog(const char *fmt, ...) { (void)fmt; }

void input_overlay_keyboard(uint8_t *state, uint32_t len) { (void)state; (void)len; }
void input_pump(void) { }
uint32_t input_take_events(int32_t kind, void *buf, uint32_t elem, uint32_t max)
{ (void)kind; (void)buf; (void)elem; (void)max; return 0; }


/* The standalone logger the game reaches through ADDR_LOG = &am2_sa_log.
 * The game calls it with NO arguments at some sites -- the retail logger is a
 * bare `ret`, so the "format" is whatever sits above the return address -- so,
 * as src/standalone/runtime.cpp's am2_sa_log does, a pointer that is not a
 * readable string starting with text is ignored rather than formatted. */
void am2_sa_log(const char *fmt, ...)
{
    va_list ap;
    FILE *lf;
    if (!vc6_fmt_ok(fmt))
        return;
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
    if ((lf = vc6_logfile()) != 0) {
        va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap); fflush(lf);
    }
}

/* Host/harness/comm symbols the excluded runtime.cpp and dplay.cpp defined;
 * milestone stubs so the runnable image links.  am2_host_pump_number is a
 * lockstep/trace hook; patch_replace is unused in a drop-in. */
unsigned int am2_host_pump_number(void) { return 0; }
int am2_trace_window(void) { return 0; }
int patch_replace(unsigned int t, const void *r, const char *n, int a)
{ (void)t; (void)r; (void)n; (void)a; return 0; }

} /* extern "C" */



/* The original's import address table, migrated to a placed C array
 * (standalone.h redirects ADDR_IAT_* / IAT_* into it).  C++ linkage, matching
 * standalone.h's `extern uint8_t am2_iat[720];`.  Filled at startup by
 * am2_bind_imports (imports.cpp), which writes through this symbol. */
uint8_t am2_iat[720];

/* The game rand's seed, a placed C global (C++ linkage, matches
 * ?am2_rand_seed@@3IA -> unsigned int am2_rand_seed). */
uint32_t am2_rand_seed = 1;

/* Milestone probe: a static initialiser so a run shows it reached _initterm
 * (C++ static init, before WinMain).  LIBC's WinMainCRTStartup runs these then
 * calls WinMain, so "static-init" printing but no further output localises a
 * hang to WinMain or its early callees. */
/* The game's own static initializers -- the XC half of am2_crt_inittab
 * (src/platform/crt/startup.cpp), which crt_cinit walks in the reconstructed-
 * CRT build.  Under real MSVCRT nothing walks it (LIBC runs only its own
 * .CRT$XC), so InitRemapIdentity never filled g_remapIdent and SetGamePalette
 * wrote through NULL.  The XI half is CRT internals (onexit/stdio/mbc/SEH) that
 * LIBC's own startup already covers.  Same order as the image's table. */
extern "C" {
void    __cdecl AirInitLeg2MsSpan(void);
void    __cdecl AirInitTurnYIn(void);
void    __cdecl AirInitTurnYOut(void);
void    __cdecl AirInitLeg2Divisor(void);
void    __cdecl AirInitLeg1X0(void);
void    __cdecl AirInitLeg3X1(void);
void    __cdecl AirInitLeg1Dx(void);
void    __cdecl AirInitLeg1Dy(void);
void    __cdecl AirInitLeg3Dx(void);
void    __cdecl AirInitLeg3Dy(void);
void    __cdecl AirInitLeg2Dy(void);
void    __cdecl InitViewColourCopy(void);
void    __cdecl InitRemapIdentity(void);
void    __cdecl InitDefaultPalette(void);
void    __cdecl InitRemapBright(void);
void    __cdecl InitOverlayPalette(void);
void    __cdecl InitStartupColours(void);
void    __cdecl InitStartupColoursB(void);
int32_t __cdecl CommGlobalInit(void);   /* dplay.cpp */
}
int32_t __cdecl SelListInit(void);          /* C++ linkage, as in item.h     */
void    __cdecl InitItemHeaderSize(void);   /* C++ linkage, as in gameproc.h */

extern "C" void am2_bind_imports(void);   /* src/standalone/imports.cpp */
extern "C" void *__cdecl LookupByUID(int32_t uid);   /* objtable.cpp */

static void (__cdecl *const vc6_game_inittab[])(void) = {
    AirInitLeg2MsSpan, AirInitTurnYIn, AirInitTurnYOut, AirInitLeg2Divisor,
    AirInitLeg1X0, AirInitLeg3X1, AirInitLeg1Dx, AirInitLeg1Dy, AirInitLeg3Dx,
    AirInitLeg3Dy, AirInitLeg2Dy,
    (void (__cdecl *)(void))CommGlobalInit,
    InitViewColourCopy, InitRemapIdentity, InitDefaultPalette, InitRemapBright,
    InitOverlayPalette,
    (void (__cdecl *)(void))SelListInit,
    InitStartupColours, InitItemHeaderSize, InitStartupColoursB,
};

/* VC6RUN_WATCH=1: a diagnostic thread that appends a few core globals to
 * C:\\vc6watch.txt every half second -- game state, the pause
 * mask, the mission clock and frame delta, the performance-counter period --
 * so a run that renders but does not advance can say which of them is stuck.
 * Off unless asked for; nothing reads it back. */
static DWORD WINAPI vc6_watch(void *arg)
{
    (void)arg;
    for (;;) {
        /* absolute: the game chdirs into its data folders as it loads */
        FILE *f = fopen("C:\\vc6watch.txt", "a");
        if (f) {
            {
                const uint8_t *lo = (const uint8_t *)LookupByUID(
                    *(const uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID);
                const int32_t *rec = (const int32_t *)(uintptr_t)ADDR_SELECTED_UIDS;
                fprintf(f, "owner=%u leader=%u obj=%p type=%d sel=%d rec=%d,%d,%08x flags=%08x riding=%u hp=%d | ",
                        *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                        *(const uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID,
                        lo, lo ? *(const int16_t *)lo : -1,
                        *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT,
                        rec[0], rec[1], (uint32_t)rec[2],
                        lo ? *(const uint32_t *)(lo + OBJ_OFF_FLAGS) : 0,
                        lo ? *(const uint32_t *)(lo + OBJ_OFF_RIDING) : 0,
                        lo ? *(const int16_t *)(lo + OBJ_OFF_HEALTH) : -1);
            }
            fprintf(f, "state=%d pause=%08x clock=%u delta=%d period=%g freq=%I64d start=%I64d k=%g\n",
                    *(const int32_t *)(uintptr_t)ADDR_GAME_STATE,
                    *(const uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS,
                    *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS,
                    *(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS,
                    *(const double *)(uintptr_t)ADDR_PERF_PERIOD,
                    *(const __int64 *)(uintptr_t)ADDR_PERF_FREQ,
                    *(const __int64 *)(uintptr_t)ADDR_PERF_START,
                    *(const double *)(uintptr_t)ADDR_DBL_MS_PER_SEC);
            fclose(f);
        }
        Sleep(500);
    }
    return 0;
}

/* Diagnostic trace sink for AM2_VC6_TRACE builds (VC6_CFLAGS=/DAM2_VC6_TRACE). */
extern "C" void vc6_trace(const char *fmt, ...)
{
    FILE *f = fopen("C:\\vc6trace.txt", "a");
    va_list ap;
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fclose(f);
}

namespace {
struct Vc6RunProbe {
    Vc6RunProbe()
    {
        if (getenv("VC6RUN_PROBE")) { fprintf(stderr, "vc6run: static-init reached\n"); fflush(stderr); }
        am2_bind_imports();   /* fill am2_iat before anything calls through it */
        if (getenv("VC6RUN_WATCH")) {
            DWORD tid;
            CreateThread(NULL, 0, vc6_watch, NULL, 0, &tid);
        }
        for (size_t i = 0; i < sizeof vc6_game_inittab / sizeof vc6_game_inittab[0]; i++)
            vc6_game_inittab[i]();
    }
};
Vc6RunProbe vc6run_probe;
}
