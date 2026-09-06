/* runtime.cpp -- what the standalone build has instead of the harness.
 *
 * The injected build reaches the game by patching it; a drop-in replacement
 * has nothing to patch and nothing to log through. This supplies the three
 * symbols that leaves undefined, the CRT wrappers src/inject/standalone.h
 * declares, and the startup that must run before WinMain touches anything.
 */
#include "../inject/win32.h"
#include "../inject/orig.h"
#include "../inject/input.h"
#include "../game/crt.h"
#include "../game/gameproc.h"
#include "../inject/control.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

extern "C" void am2_apply_fixups(void);
extern "C" void am2_run_static_init(void);
extern "C" void am2_bind_imports(void);

/* ---- what the harness provided -------------------------------------- */

/* There is nothing to detour: every function this would have patched IS the
 * binary now. It answers success so the install functions read as they do in
 * the injected build, and so a missing one is not mistaken for a failure. */
extern "C" int patch_replace(uintptr_t addr, const void *fn, const char *name,
                             int argc)
{
    (void)addr; (void)fn; (void)name; (void)argc;
    return 0;
}

/* The MULTI-PLAYER button was removed from the retail binary by a
 * post-compilation patch, and src/inject/restore.c puts it back for testing.
 * A standalone build has no such patch to undo -- what it draws is decided by
 * the reconstruction, not by six overwritten branches. */
extern "C" void restore_multiplayer(void) { }

static FILE *sa_logfile(void);   /* defined with the logger below */

extern "C" void hooklog(const char *fmt, ...)
{
    FILE   *fh = sa_logfile();
    va_list ap;

    if (!fh)
        return;
    va_start(ap, fmt);
    vfprintf(fh, fmt, ap);
    va_end(ap);
    fputc('\n', fh);
    fflush(fh);
}

/* The trace table is the patch stubs, and a standalone build has none: every
 * function IS the binary. `counts` therefore has nothing to report, which is
 * the same answer AM2_NOPATCH=1 gives in the injected build. */
extern "C" void trace_describe(char *out, uint32_t cap, const char *want)
{
    (void)want;
    if (cap)
        snprintf(out, cap, "(no counters: this build has no patch stubs)");
}

/* ---- the seams src/inject/standalone.h declares ---------------------- */

/* rand is src/platform/crt/rand.cpp's now, over the image's own seed; the
 * note that used to sit here about StartPacketThread and BuildRespawnPool
 * seeding a private word is in that file. */

/* The retail logger is a bare `ret`, and src/inject/gamelog.c patches it to
 * capture what the game writes -- which is half of what tools/ab.sh compares.
 * The standalone build does the same thing, to a file beside the exe: it goes
 * nowhere near the screen, so it cannot show lines the original did not, and
 * without it a standalone run has no diagnostics at all. AM2_LOG names the
 * file; unset means dropped, as the retail stub does. */
/* The log file, opened once. Shared by the game's logger and the harness's,
 * because in a standalone build they are one file: the control socket's
 * "bind/listen failed" is the line CLAUDE.md says to grep for when a drive
 * goes nowhere, and it has nowhere else to go. */
static FILE *sa_logfile(void)
{
    static FILE *fh;
    static int   tried;

    if (tried)
        return fh;
    tried = 1;

    {
        const char *path = getenv("AM2_LOG");
        if (path && *path) {
            fh = fopen(path, "w");
            return fh;
        }
    }
    /* Next to the exe, not the working directory: SetGameDir chdirs into the
     * map and avi directories as it goes, so a relative name lands somewhere
     * different depending on when it is opened -- which is why the first
     * attempt at this produced no file anywhere. */
    {
        char  buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
        if (n && n < sizeof buf) {
            char *slash = strrchr(buf, '\\');
            /* The native build's module path is a POSIX one. */
            if (!slash)
                slash = strrchr(buf, '/');
            if (slash && (size_t)(slash - buf) + 16 < sizeof buf) {
                strcpy(slash + 1, "am2port.log");
                fh = fopen(buf, "w");
            }
        }
    }
    return fh;
}

static void sa_vlog(const char *fmt, va_list ap)
{
    FILE *fh = sa_logfile();

    if (!fh)
        return;
    vfprintf(fh, fmt, ap);
    fflush(fh);
}

extern "C" void am2_sa_log(const char *fmt, ...)
{
    va_list ap;

    /* THE GAME CALLS THIS WITH NO ARGUMENTS AT ALL in places, and that is
     * not a defect: the retail logger is a bare `ret`, so a call site with
     * an empty argument frame is harmless there and CommConstruct has one --
     * CLAUDE.md records another at 0x00460290. The "format" is then whatever
     * sits above the return address, and formatting it faults inside the
     * CRT. So the pointer is checked before it is used: a logger that can
     * crash the game is worse than no logger. */
    if (IsBadStringPtrA(fmt, 4096))
        return;
    /* And a readable pointer is not yet a format string: natively that
     * junk pointer has landed inside the carried data, where it reads as a
     * few kilobytes of binary before the first NUL, and went into the log
     * as such. A format string starts with text. */
    {
        const unsigned char *p = (const unsigned char *)fmt;
        int i;
        for (i = 0; i < 8 && p[i]; i++)
            if (p[i] < 0x20 && p[i] != '\n' && p[i] != '\t' && p[i] != '\r')
                return;
            else if (p[i] >= 0x7F)
                return;
    }

    va_start(ap, fmt);
    sa_vlog(fmt, ap);
    va_end(ap);
}

extern "C" int32_t __stdcall am2_sa_ddraw_create(void *guid, void **out, void *outer)
{
    return (int32_t)DirectDrawCreate((GUID *)guid, (LPDIRECTDRAW *)out,
                                     (IUnknown *)outer);
}

extern "C" int32_t __stdcall am2_sa_dinput_create(void *inst, uint32_t ver, void **out,
                                        void *outer)
{
    return (int32_t)DirectInputCreateA((HINSTANCE)inst, (DWORD)ver,
                                       (LPDIRECTINPUTA *)out,
                                       (IUnknown *)outer);
}

extern "C" int32_t __stdcall am2_sa_dsound_create(void *guid, void **out, void *outer)
{
    return (int32_t)DirectSoundCreate((GUID *)guid, (LPDIRECTSOUND *)out,
                                      (IUnknown *)outer);
}

/* VC6's operator new answers NULL rather than throwing, and the game TESTS
 * the result -- CLAUDE.md notes 0x00451251 doing exactly that. The nothrow
 * form keeps that true, which is also what makes the widget destructors'
 * missing SEH frames safe. */
#ifdef AM2_DEVTOOLS
extern "C" void *am2_sa_operator_new(size_t n) { return am2_malloc(n); }
extern "C" void am2_sa_operator_delete(void *p) { am2_free(p); }
#else
extern "C" void *am2_sa_operator_new(size_t n) { return crt_operator_new((uint32_t)n); }
extern "C" void am2_sa_operator_delete(void *p) { crt_operator_delete(p); }
#endif

extern "C" void am2_sa_free_army_lists(void) { FreeArmyObjLists(); }

extern "C" int32_t am2_sa_unimplemented(void)
{
    am2_log("STANDALONE: an unreconstructed function was called\n");
    return 0;
}

/* ---- the int3 gap's exception handler ---------------------------------- */

/* The range the original's .text occupied is filled with int3, so a call to
 * a seam we have not redefined traps AT the intended callee rather than
 * sliding through zeros. This turns that trap into a sentence: the address
 * called, and the return address sitting on the stack, which names the
 * CALLER. Without it every missing seam costs a bisect with printf probes.
 */
static LONG CALLBACK am2_sa_gap_filter(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t eip = (uintptr_t)er->ExceptionAddress;

    if (eip >= 0x00401000u && eip < 0x0046F000u) {
        uintptr_t *esp = (uintptr_t *)ep->ContextRecord->Esp;
        am2_log("GAP: called 0x%08lX from 0x%08lX -- a seam this build has "
                "not redefined\n", (unsigned long)eip,
                (unsigned long)(esp ? esp[0] : 0));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#ifdef AM2_NATIVE
/* The host layer keeps the game's cursor under the pointer; this is how it
 * learns where the game's cursor is. See platform.h. */
#include "../platform/platform.h"

static void am2_sa_cursor_query(int32_t *x, int32_t *y)
{
    *x = *(const int32_t *)(uintptr_t)ADDR_CURSOR_X;
    *y = *(const int32_t *)(uintptr_t)ADDR_CURSOR_Y;
}

/* The replay's `cursor`: the three globals the control socket's `cursor`
 * writes, clamped as UpdateMouseState clamps, and the moved flag a real
 * move sets. Kept identical to control.c's on purpose. */
static void am2_sa_cursor_set(int32_t x, int32_t y)
{
    int32_t       *cx   = (int32_t *)(uintptr_t)ADDR_CURSOR_X;
    int32_t       *cy   = (int32_t *)(uintptr_t)ADDR_CURSOR_Y;
    int16_t       *pt   = (int16_t *)(uintptr_t)ADDR_CURSOR_POINT;
    const int32_t *clip = (const int32_t *)(uintptr_t)ADDR_SCREEN_CLIP;

    if (x < clip[0]) x = clip[0];
    if (x > clip[2] - 1) x = clip[2] - 1;
    if (y < clip[1]) y = clip[1];
    if (y > clip[3] - 1) y = clip[3] - 1;
    *cx = x;
    *cy = y;
    pt[0] = (int16_t)x;
    pt[1] = (int16_t)y;
    *(int32_t *)(uintptr_t)ADDR_MOUSE_MOVED = 1;
}
#endif

/* ---- startup --------------------------------------------------------- */

/* Runs before WinMain. The allocator seam is pointed at the host CRT -- in
 * the injected build it has to be the MSVC heap inside ArmyMen2.exe, because
 * blocks cross between our code and the original's; here there is only one
 * heap and the question does not arise. */
extern "C" void am2_standalone_init(void)
{
    /* The CRT's own heap, src/platform/crt/heap.cpp: one allocator for the
     * game and the CRT's internals, so a block crossing between them --
     * a FILE's buffer, getcwd's malloc'd answer -- is freed where it was
     * made. Both binaries, now: the development one used to keep a
     * fixed-address arena of its own so a savestate could carry the heap,
     * and that arena is the platform's (src/platform/fixedheap.cpp), under
     * the CRT's HeapAlloc and VirtualAlloc, where the hybrid has it too. */
    am2_malloc = crt_malloc;
    am2_realloc = crt_realloc;
    am2_free = crt_free;
    am2_log = am2_sa_log;
    /* The directory calls too: crt.cpp's defaults are the host's chdir and
     * getcwd, which under mingw understand a backslash and under glibc do
     * not. _chdir is mingw's own there and the platform layer's translating
     * one in the native build, so SetGameDir's "<dir>\aai" works in both
     * before am2_crt_use_game has run. */
    am2_chdir = _chdir;
    am2_getcwd = _getcwd;
#ifdef AM2_NATIVE
    am2_host_cursor_query = am2_sa_cursor_query;
    am2_host_cursor_set = am2_sa_cursor_set;
#endif
    /* FIRST: the original's IAT as we carry it is the FILE's, which no
     * loader has fixed up, so every seam calling through it was jumping to a
     * name-table RVA. Binding it must precede anything that runs game code. */
    AddVectoredExceptionHandler(1, am2_sa_gap_filter);
    am2_bind_imports();
    am2_apply_fixups();

    /* The CRT's startup, src/platform/crt/startup.cpp: the version words,
     * the heap, lowio, the command line and environment, argv and envp,
     * then _cinit -- the C initializers and the C++ static initializers,
     * each through the image's own table, whose entries the fixups above
     * have just pointed at the reconstructions. Without the static
     * initializers the globals they fill stay null: SetGamePalette writes
     * through one, g_remapIdent, and faulted on address 0. Fixups first,
     * for that reason. */
    crt_startup();

    /* The injected queue the control socket writes into. dllmain.c does this
     * for the injected build; without it here every `key`, `type` and `mouse`
     * command was accepted and dropped, so a standalone run could be looked
     * at and not driven. */
#ifdef AM2_DEVTOOLS
    input_init();
#endif

    /* The control socket, which the injected build gets from the harness.
     * Without it a standalone run cannot be driven or dumped, so every
     * comparison against the injected build had to go through screenshots --
     * and tools/objdump.py, the object-table diff that is bootcamp's
     * sharpest artifact, was unavailable entirely. AM2_CONTROL=1 enables it,
     * exactly as it does there. */
#ifdef AM2_DEVTOOLS
    control_start();
    devtools_init();
#endif
}

/* WinMain is the reconstruction's own and is not touched, so the startup
 * hangs off a static initialiser instead -- which C++ runs before it. The
 * original had static initialisers of its own and they are all reconstructed,
 * so this is the same mechanism rather than a new one. */
namespace {
struct AM2_StandaloneStartup {
    AM2_StandaloneStartup() { am2_standalone_init(); }
    /* And the way out: the original's startup calls exit after WinMain
     * returns, which runs the onexit table the game's atexit calls filled
     * and the two terminator tables. Here the host's exit runs this
     * destructor, and doexit with retcaller set does the same and comes
     * back. */
    ~AM2_StandaloneStartup() { crt_doexit(0, 0, 1); }
};
AM2_StandaloneStartup am2_standalone_startup;
}
