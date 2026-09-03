/* runtime.cpp -- what the standalone build has instead of the harness.
 *
 * The injected build reaches the game by patching it; a drop-in replacement
 * has nothing to patch and nothing to log through. This supplies the three
 * symbols that leaves undefined, the CRT wrappers src/inject/standalone.h
 * declares, and the startup that must run before WinMain touches anything.
 */
#include "../inject/win32.h"
#include "../inject/orig.h"
#include "../game/crt.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

extern "C" void am2_apply_fixups(void);

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

extern "C" void hooklog(const char *fmt, ...) { (void)fmt; }

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
extern "C" void am2_sa_log(const char *fmt, ...)
{
    static FILE *fh;
    static int tried;
    va_list ap;

    if (!tried) {
        const char *path = getenv("AM2_LOG");
        tried = 1;
        if (path && *path) {
            fh = fopen(path, "w");
        } else {
            /* Next to the exe, not the working directory: SetGameDir chdirs
             * into the map and avi directories as it goes, so a relative
             * name lands somewhere different depending on when it is
             * opened -- which is why the first attempt at this produced no
             * file anywhere. */
            char buf[MAX_PATH];
            DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
            if (n && n < sizeof buf) {
                char *slash = strrchr(buf, '\\');
                if (slash && (size_t)(slash - buf) + 16 < sizeof buf) {
                    strcpy(slash + 1, "am2port.log");
                    fh = fopen(buf, "w");
                }
            }
        }
    }
    if (!fh)
        return;

    va_start(ap, fmt);
    vfprintf(fh, fmt, ap);
    va_end(ap);
    fflush(fh);
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
    am2_apply_fixups();
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
