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

/* MSVC's rand. The constants are the ones in the image and the sequence is
 * observable in play, so this must be the LCG rather than the host's rand:
 * seed = seed * 0x343FD + 0x269EC3, answer = (seed >> 16) & 0x7FFF. */
static uint32_t am2_sa_seed = 1;

extern "C" int am2_sa_rand(void)
{
    am2_sa_seed = am2_sa_seed * 0x343FDu + 0x269EC3u;
    return (int)((am2_sa_seed >> 16) & 0x7FFFu);
}

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

/* The find-file family shares a struct with its caller. mingw's _finddata_t
 * matches MSVC 6's layout for the fields the game reads, so these pass it
 * straight through -- and they exist as wrappers so that if it ever stops
 * matching there is one place to fix. */
extern "C" intptr_t am2_sa_findfirst(const char *spec, void *data)
{
    return _findfirst(spec, (struct _finddata_t *)data);
}

extern "C" int am2_sa_findnext(intptr_t handle, void *data)
{
    return _findnext(handle, (struct _finddata_t *)data);
}

extern "C" int am2_sa_findclose(intptr_t handle) { return _findclose(handle); }

/* VC6's operator new answers NULL rather than throwing, and the game TESTS
 * the result -- CLAUDE.md notes 0x00451251 doing exactly that. The nothrow
 * form keeps that true, which is also what makes the widget destructors'
 * missing SEH frames safe. */
extern "C" void *am2_sa_operator_new(size_t n) { return malloc(n); }
extern "C" void am2_sa_operator_delete(void *p) { free(p); }


extern "C" long am2_sa_ftell(void *fp) { return ftell((FILE *)fp); }

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

/* ---- startup --------------------------------------------------------- */

/* Runs before WinMain. The allocator seam is pointed at the host CRT -- in
 * the injected build it has to be the MSVC heap inside ArmyMen2.exe, because
 * blocks cross between our code and the original's; here there is only one
 * heap and the question does not arise. */
extern "C" void am2_standalone_init(void)
{
    am2_malloc = malloc;
    am2_realloc = realloc;
    am2_free = free;
    am2_log = am2_sa_log;
    /* FIRST: the original's IAT as we carry it is the FILE's, which no
     * loader has fixed up, so every seam calling through it was jumping to a
     * name-table RVA. Binding it must precede anything that runs game code. */
    AddVectoredExceptionHandler(1, am2_sa_gap_filter);
    am2_bind_imports();
    am2_apply_fixups();

    /* The original's CRT ran the __xc_a..__xc_z table before WinMain and
     * ours does not, so the C++ static initializers are called here. Without
     * them the globals they fill stay null -- SetGamePalette writes through
     * one, g_remapIdent, and faulted on address 0. Fixups first: an
     * initializer may store a pointer the table also mentions. */
    am2_run_static_init();

    /* The injected queue the control socket writes into. dllmain.c does this
     * for the injected build; without it here every `key`, `type` and `mouse`
     * command was accepted and dropped, so a standalone run could be looked
     * at and not driven. */
    input_init();

    /* The control socket, which the injected build gets from the harness.
     * Without it a standalone run cannot be driven or dumped, so every
     * comparison against the injected build had to go through screenshots --
     * and tools/objdump.py, the object-table diff that is bootcamp's
     * sharpest artifact, was unavailable entirely. AM2_CONTROL=1 enables it,
     * exactly as it does there. */
    control_start();
}

/* WinMain is the reconstruction's own and is not touched, so the startup
 * hangs off a static initialiser instead -- which C++ runs before it. The
 * original had static initialisers of its own and they are all reconstructed,
 * so this is the same mechanism rather than a new one. */
namespace {
struct AM2_StandaloneStartup {
    AM2_StandaloneStartup() { am2_standalone_init(); }
};
AM2_StandaloneStartup am2_standalone_startup;
}
