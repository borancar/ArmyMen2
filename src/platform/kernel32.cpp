/* kernel32.cpp -- time, modules and the import table, threads and events,
 * memory probes, the exception hook, the registry, drives, and the
 * Winsock names control.c uses. See platform.h for the layer's shape.
 */
#include "platform.h"
#include "handle.h"
#include <winsock2.h>
#include <io.h>
#include <ddraw.h>
#include <dinput.h>
#include <dsound.h>
#include <dplobby.h>
#include <direct.h>

#include <SDL3/SDL.h>

#include <pthread.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* winsock2.h routes control.c's setsockopt through the shim below; this
 * file calls the real one. */
#undef setsockopt

/* ---- logging ---------------------------------------------------------------- */

void am2_plat_log(const char *fmt, ...)
{
    static int quiet = -1;
    va_list ap;

    if (quiet < 0) {
        const char *q = getenv("AM2_PLATFORM_QUIET");
        quiet = q && *q && *q != '0';
    }
    if (quiet)
        return;
    fputs("platform: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void am2_plat_debug(const char *fmt, ...)
{
    static int on = -1;
    va_list ap;

    if (on < 0) {
        const char *d = getenv("AM2_PLATFORM_DEBUG");
        on = d && *d && *d != '0';
    }
    if (!on)
        return;
    fputs("platform: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- last error --------------------------------------------------------------- */

static __thread DWORD am2_last_error;

DWORD WINAPI GetLastError(void) { return am2_last_error; }
void  WINAPI SetLastError(DWORD err) { am2_last_error = err; }

/* ---- time ------------------------------------------------------------------ */

DWORD WINAPI GetTickCount(void)
{
    return (DWORD)SDL_GetTicks();
}

DWORD WINAPI timeGetTime(void)
{
    return (DWORD)SDL_GetTicks();
}

void WINAPI Sleep(DWORD ms)
{
    if (ms == 0)
        sched_yield();
    else
        SDL_Delay(ms);
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *out)
{
    out->QuadPart = (LONGLONG)SDL_GetPerformanceCounter();
    return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *out)
{
    out->QuadPart = (LONGLONG)SDL_GetPerformanceFrequency();
    return TRUE;
}

/* ---- modules and the import table --------------------------------------------- */

/* The original's IAT is carried in .origdat and bound at startup by the
 * generated am2_bind_imports, which asks for each entry by module and name.
 * Five sites in src/game then call through those slots -- GetTickCount
 * from the AI and comm timers, IntersectRect from the row layer, and the
 * Smacker entry points from movie.cpp -- so the answers have to be real
 * functions with the right convention.
 *
 * A name in a module we emulate that is not in the table gets a trap that
 * says which name it was, once, and answers 0. Most of the kernel32 list is
 * the MSVC CRT's own imports, which nothing in this tree calls. */

static const char *am2_missing_names[64];
static int32_t     am2_missing_count;

static int32_t WINAPI am2_import_missing(void)
{
    am2_plat_log("an import this build does not provide was called "
                 "(see the names logged at startup)");
    return 0;
}

/* Smacker: no movie can be opened, so every entry point is inert and
 * SmackOpen answers NULL, which PlayMovie treats as a missing file. */
static void *WINAPI am2_SmackOpen(const char *name, uint32_t flags, int32_t extra)
{
    (void)flags; (void)extra;
    am2_plat_log("SmackOpen(%s): movies are not available in this build", name);
    return NULL;
}
static void WINAPI am2_Smack_void1(void *smack) { (void)smack; }
static uint32_t WINAPI am2_SmackDDSurfaceType(void *surface) { (void)surface; return 0; }
static void WINAPI am2_SmackToBuffer(void *smack, uint32_t left, uint32_t top,
                                     uint32_t pitch, uint32_t height, void *buf,
                                     uint32_t flags)
{
    (void)smack; (void)left; (void)top; (void)pitch; (void)height; (void)buf; (void)flags;
}
static void WINAPI am2_SmackVolumePan(void *smack, uint32_t tracks, uint32_t vol,
                                      uint32_t pan)
{
    (void)smack; (void)tracks; (void)vol; (void)pan;
}

#define X(mod, fn) { mod, #fn, (const void *)&fn }
static const AM2_Export am2_exports[] = {
    X("ADVAPI32.dll", RegCreateKeyExA), X("ADVAPI32.dll", RegCloseKey),
    X("DDRAW.dll", DirectDrawCreate),
    X("DINPUT.dll", DirectInputCreateA),
    X("DSOUND.dll", DirectSoundCreate),
    X("GDI32.dll", RealizePalette), X("GDI32.dll", CreatePalette),
    X("GDI32.dll", SetSystemPaletteUse), X("GDI32.dll", GetPixel),
    X("GDI32.dll", SelectObject), X("GDI32.dll", GetSystemPaletteEntries),
    X("GDI32.dll", CreateFontA), X("GDI32.dll", TextOutA),
    X("GDI32.dll", GetTextExtentPoint32A), X("GDI32.dll", SetTextColor),
    X("GDI32.dll", SetBkMode), X("GDI32.dll", DeleteObject),
    X("GDI32.dll", SelectPalette), X("GDI32.dll", GetDeviceCaps),
    X("KERNEL32.dll", ReleaseMutex), X("KERNEL32.dll", CreateMutexA),
    X("KERNEL32.dll", OpenMutexA), X("KERNEL32.dll", GetExitCodeThread),
    X("KERNEL32.dll", CloseHandle), X("KERNEL32.dll", SetEvent),
    X("KERNEL32.dll", WaitForSingleObject), X("KERNEL32.dll", WaitForMultipleObjects),
    X("KERNEL32.dll", GetTickCount), X("KERNEL32.dll", GetProcAddress),
    X("KERNEL32.dll", GlobalFree), X("KERNEL32.dll", GlobalAlloc),
    X("KERNEL32.dll", Sleep), X("KERNEL32.dll", GetVolumeInformationA),
    X("KERNEL32.dll", GetDriveTypeA), X("KERNEL32.dll", GetLogicalDriveStringsA),
    X("KERNEL32.dll", QueryPerformanceCounter), X("KERNEL32.dll", QueryPerformanceFrequency),
    X("KERNEL32.dll", lstrlenA), X("KERNEL32.dll", GetModuleFileNameA),
    X("KERNEL32.dll", IsBadReadPtr), X("KERNEL32.dll", IsBadWritePtr),
    X("KERNEL32.dll", LoadLibraryA), X("KERNEL32.dll", FreeLibrary),
    X("KERNEL32.dll", SetThreadPriority), X("KERNEL32.dll", CreateThread),
    X("KERNEL32.dll", CreateEventA), X("KERNEL32.dll", GetModuleHandleA),
    X("KERNEL32.dll", SetCurrentDirectoryA), X("KERNEL32.dll", GetCurrentDirectoryA),
    X("KERNEL32.dll", GetLastError),
    /* The rest of KERNEL32 is what the MSVC 6 CRT inside the original
     * imports, provided by kernel32crt.cpp for the PE loader. */
    X("KERNEL32.dll", CompareStringA), X("KERNEL32.dll", CompareStringW),
    X("KERNEL32.dll", CreateDirectoryA), X("KERNEL32.dll", CreateFileA),
    X("KERNEL32.dll", DeleteFileA), X("KERNEL32.dll", ExitProcess),
    X("KERNEL32.dll", FileTimeToLocalFileTime), X("KERNEL32.dll", FileTimeToSystemTime),
    X("KERNEL32.dll", FindClose), X("KERNEL32.dll", FindFirstFileA),
    X("KERNEL32.dll", FindNextFileA), X("KERNEL32.dll", FlushFileBuffers),
    X("KERNEL32.dll", FreeEnvironmentStringsA), X("KERNEL32.dll", FreeEnvironmentStringsW),
    X("KERNEL32.dll", GetACP), X("KERNEL32.dll", GetCommandLineA),
    X("KERNEL32.dll", GetCPInfo), X("KERNEL32.dll", GetCurrentProcess),
    X("KERNEL32.dll", GetEnvironmentStrings), X("KERNEL32.dll", GetEnvironmentStringsW),
    X("KERNEL32.dll", GetFileAttributesA), X("KERNEL32.dll", GetFileType),
    X("KERNEL32.dll", GetFullPathNameA), X("KERNEL32.dll", GetLocalTime),
    X("KERNEL32.dll", GetOEMCP), X("KERNEL32.dll", GetStartupInfoA),
    X("KERNEL32.dll", GetStdHandle), X("KERNEL32.dll", GetStringTypeA),
    X("KERNEL32.dll", GetStringTypeW), X("KERNEL32.dll", GetSystemTime),
    X("KERNEL32.dll", GetTimeZoneInformation), X("KERNEL32.dll", GetVersion),
    X("KERNEL32.dll", HeapAlloc), X("KERNEL32.dll", HeapCreate),
    X("KERNEL32.dll", HeapDestroy), X("KERNEL32.dll", HeapFree),
    X("KERNEL32.dll", HeapReAlloc), X("KERNEL32.dll", HeapSize),
    { "KERNEL32.dll", "InterlockedExchange", (const void *)&am2_InterlockedExchange },
    X("KERNEL32.dll", IsBadCodePtr), X("KERNEL32.dll", LCMapStringA),
    X("KERNEL32.dll", LCMapStringW), X("KERNEL32.dll", MultiByteToWideChar),
    X("KERNEL32.dll", ReadFile), X("KERNEL32.dll", RemoveDirectoryA),
    X("KERNEL32.dll", RtlUnwind), X("KERNEL32.dll", SetEndOfFile),
    X("KERNEL32.dll", SetEnvironmentVariableA), X("KERNEL32.dll", SetFileAttributesA),
    X("KERNEL32.dll", SetFilePointer), X("KERNEL32.dll", SetHandleCount),
    X("KERNEL32.dll", SetStdHandle), X("KERNEL32.dll", SetUnhandledExceptionFilter),
    X("KERNEL32.dll", TerminateProcess), X("KERNEL32.dll", UnhandledExceptionFilter),
    X("KERNEL32.dll", VirtualAlloc), X("KERNEL32.dll", VirtualFree),
    X("KERNEL32.dll", WideCharToMultiByte), X("KERNEL32.dll", WriteFile),
    X("USER32.dll", PostMessageA), X("USER32.dll", EndPaint),
    X("USER32.dll", BeginPaint), X("USER32.dll", PostQuitMessage),
    X("USER32.dll", RedrawWindow), X("USER32.dll", GetUpdateRect),
    X("USER32.dll", DefWindowProcA), X("USER32.dll", SetRect),
    X("USER32.dll", GetSystemMetrics), X("USER32.dll", IsIconic),
    X("USER32.dll", ClientToScreen), X("USER32.dll", GetClientRect),
    X("USER32.dll", SetWindowPos), X("USER32.dll", GetWindowRect),
    X("USER32.dll", SystemParametersInfoA), X("USER32.dll", SetWindowLongA),
    X("USER32.dll", AdjustWindowRectEx), X("USER32.dll", GetMenu),
    X("USER32.dll", TranslateMessage), X("USER32.dll", GetWindowLongA),
    X("USER32.dll", DispatchMessageA), X("USER32.dll", GetFocus),
    X("USER32.dll", SetFocus), X("USER32.dll", UpdateWindow),
    X("USER32.dll", GetDC), X("USER32.dll", ShowCursor),
    X("USER32.dll", wsprintfA), X("USER32.dll", MessageBoxA),
    X("USER32.dll", ReleaseDC), X("USER32.dll", GetActiveWindow),
    X("USER32.dll", RegisterClassA), X("USER32.dll", LoadIconA),
    X("USER32.dll", LoadCursorA), X("USER32.dll", SetCursor),
    X("USER32.dll", CreateWindowExA), X("USER32.dll", IntersectRect),
    X("USER32.dll", PeekMessageA), X("USER32.dll", DestroyWindow),
    X("WINMM.dll", mmioDescend), X("WINMM.dll", mmioRead), X("WINMM.dll", mmioAdvance),
    X("WINMM.dll", timeBeginPeriod), X("WINMM.dll", timeSetEvent),
    X("WINMM.dll", timeKillEvent), X("WINMM.dll", timeEndPeriod),
    X("WINMM.dll", mmioGetInfo), X("WINMM.dll", mmioClose), X("WINMM.dll", mmioSetInfo),
    X("WINMM.dll", mmioSeek), X("WINMM.dll", mmioOpenA), X("WINMM.dll", mmioAscend),
    X("ole32.dll", CoInitialize), X("ole32.dll", CoCreateInstance),
    { "smackw32.dll", "_SmackOpen@12", (const void *)&am2_SmackOpen },
    { "smackw32.dll", "_SmackClose@4", (const void *)&am2_Smack_void1 },
    { "smackw32.dll", "_SmackDoFrame@4", (const void *)&am2_Smack_void1 },
    { "smackw32.dll", "_SmackNextFrame@4", (const void *)&am2_Smack_void1 },
    { "smackw32.dll", "_SmackWait@4", (const void *)&am2_Smack_void1 },
    { "smackw32.dll", "_SmackSoundUseDirectSound@4", (const void *)&am2_Smack_void1 },
    { "smackw32.dll", "_SmackDDSurfaceType@4", (const void *)&am2_SmackDDSurfaceType },
    { "smackw32.dll", "_SmackToBuffer@28", (const void *)&am2_SmackToBuffer },
    { "smackw32.dll", "_SmackVolumePan@16", (const void *)&am2_SmackVolumePan },
};
#undef X

static const char *const am2_modules[] = {
    "ADVAPI32.dll", "DDRAW.dll", "DINPUT.dll", "DSOUND.dll", "GDI32.dll",
    "KERNEL32.dll", "USER32.dll", "WINMM.dll", "ole32.dll", "smackw32.dll",
};

HMODULE WINAPI LoadLibraryA(LPCSTR name)
{
    size_t i;

    if (!name)
        return NULL;
    for (i = 0; i < sizeof am2_modules / sizeof am2_modules[0]; i++)
        if (!strcasecmp(am2_modules[i], name))
            return (HMODULE)(uintptr_t)(i + 1);
    /* cpuinf32.dll is the one the game itself asks for, and answering NULL
     * takes the path the original takes without it: "Missing cpuinf32.dll"
     * and the default machine speed. */
    am2_last_error = ERROR_FILE_NOT_FOUND;
    return NULL;
}

BOOL WINAPI FreeLibrary(HMODULE mod) { (void)mod; return TRUE; }

HMODULE WINAPI GetModuleHandleA(LPCSTR name)
{
    if (!name)
        return (HMODULE)(uintptr_t)0x400000u;
    return LoadLibraryA(name);
}

FARPROC WINAPI GetProcAddress(HMODULE mod, LPCSTR name)
{
    size_t      idx = (size_t)(uintptr_t)mod;
    const char *module;
    size_t      i;
    char        ordinal[16];

    if (idx == 0 || idx > sizeof am2_modules / sizeof am2_modules[0])
        return NULL;
    module = am2_modules[idx - 1];

    /* An import by ordinal arrives as a small integer. The one the game has
     * is DSOUND's #1, DirectSoundCreate. */
    if ((uintptr_t)name < 0x10000) {
        if (!strcasecmp(module, "DSOUND.dll") && (uintptr_t)name == 1)
            name = "DirectSoundCreate";
        else {
            snprintf(ordinal, sizeof ordinal, "#%u", (unsigned)(uintptr_t)name);
            name = ordinal;
        }
    }

    for (i = 0; i < sizeof am2_exports / sizeof am2_exports[0]; i++)
        if (!strcmp(am2_exports[i].module, module) &&
            !strcmp(am2_exports[i].name, name))
            return (FARPROC)am2_exports[i].fn;

    if (am2_missing_count < (int32_t)(sizeof am2_missing_names / sizeof am2_missing_names[0]))
        am2_missing_names[am2_missing_count++] = name;
    return (FARPROC)&am2_import_missing;
}

/* The loader's view of the table: NULL for a name we do not provide, so it
 * can make a trap that says which name, rather than the shared one. */
const void *am2_export_lookup(const char *module, const char *name)
{
    size_t i;

    for (i = 0; i < sizeof am2_exports / sizeof am2_exports[0]; i++)
        if (!strcasecmp(am2_exports[i].module, module) &&
            !strcmp(am2_exports[i].name, name))
            return am2_exports[i].fn;
    return NULL;
}

DWORD WINAPI GetModuleFileNameA(HMODULE mod, LPSTR out, DWORD cap)
{
    ssize_t n;

    (void)mod;
    if (!out || cap == 0)
        return 0;
    n = readlink("/proc/self/exe", out, cap - 1);
    if (n < 0) {
        out[0] = 0;
        return 0;
    }
    out[n] = 0;
    return (DWORD)n;
}

/* ---- memory probes and heaps --------------------------------------------- */

/* A page is readable if mincore knows it: ENOMEM says it is not mapped.
 * That is what the original's logger guard needs -- runtime.cpp asks
 * whether a "format string" the game handed it with an empty argument
 * frame is a string at all. */
static int32_t am2_page_mapped(const void *p)
{
    uintptr_t page = (uintptr_t)p & ~(uintptr_t)4095;
    unsigned char vec;

    return mincore((void *)page, 4096, &vec) == 0;
}

static BOOL am2_range_bad(const void *p, UINT_PTR n)
{
    uintptr_t a, end;

    if (!p)
        return TRUE;
    if (n == 0)
        return FALSE;
    a = (uintptr_t)p & ~(uintptr_t)4095;
    end = (uintptr_t)p + n - 1;
    for (; a <= end; a += 4096)
        if (!am2_page_mapped((const void *)a))
            return TRUE;
    return FALSE;
}

BOOL WINAPI IsBadReadPtr(LPCVOID p, UINT_PTR n) { return am2_range_bad(p, n); }
BOOL WINAPI IsBadWritePtr(LPVOID p, UINT_PTR n) { return am2_range_bad(p, n); }

BOOL WINAPI IsBadStringPtrA(LPCSTR p, UINT_PTR max)
{
    uintptr_t a;

    if (!p)
        return TRUE;
    for (a = (uintptr_t)p; max > 0; a++, max--) {
        if ((a & 4095) == 0 || a == (uintptr_t)p)
            if (!am2_page_mapped((const void *)a))
                return TRUE;
        if (*(const char *)a == 0)
            return FALSE;
    }
    return FALSE;
}

HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T bytes)
{
    void *p = malloc(bytes ? bytes : 1);

    if (p && (flags & GMEM_ZEROINIT))
        memset(p, 0, bytes);
    return (HGLOBAL)p;
}

HGLOBAL WINAPI GlobalFree(HGLOBAL mem)
{
    free(mem);
    return NULL;
}

/* ---- handles: threads, events, mutexes, files ------------------------------ */

/* One lock and one condition for every handle. The game has two threads
 * besides the main one and they rendezvous a handful of times a second, so
 * a shared condition is simpler than one per object and costs nothing. The
 * record itself is handle.h's, because kernel32crt.cpp makes the file and
 * find-file kinds for the original's CRT. */
static pthread_mutex_t am2_h_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  am2_h_cond = PTHREAD_COND_INITIALIZER;

AM2_Handle *am2_handle_new(int32_t kind)
{
    AM2_Handle *h = (AM2_Handle *)calloc(1, sizeof *h);

    if (h) {
        h->kind = kind;
        h->refs = 1;
        h->fd = -1;
    }
    return h;
}

void am2_handle_release(AM2_Handle *h)
{
    int32_t gone;

    pthread_mutex_lock(&am2_h_lock);
    gone = --h->refs == 0;
    pthread_mutex_unlock(&am2_h_lock);
    if (!gone)
        return;
    if (h->kind == AM2_H_FILE && !h->keepFd && h->fd >= 0)
        close(h->fd);
    if (h->kind == AM2_H_FIND && h->find)
        _findclose(h->find);
    free(h);
}

/* Called on every thread the game creates, before its start routine, when
 * set. The PE loader hangs each thread's own TEB on it. */
void (*am2_thread_attach)(void);

/* Call a thread's start routine without trusting its convention. The
 * game's packet thread is cdecl behind an LPTHREAD_START_ROUTINE cast --
 * dplay.cpp records the original's mismatch -- so it does not pop its
 * argument, and a plain stdcall call here left the trampoline four bytes
 * off and returning into address 8 at exit. Windows survives it because
 * its thread stub never returns through that frame; this restores the
 * stack pointer around the call, which works for either convention. */
static DWORD am2_call_thread_start(LPTHREAD_START_ROUTINE fn, LPVOID param)
{
    DWORD rc;

    __asm__ volatile(
        "mov %%esp, %%esi\n\t"
        "push %[param]\n\t"
        "call *%[fn]\n\t"
        "mov %%esi, %%esp\n\t"
        : "=a"(rc)
        : [fn] "r"(fn), [param] "r"(param)
        : "esi", "ecx", "edx", "memory", "cc");
    return rc;
}

static void *am2_thread_main(void *arg)
{
    AM2_Handle *h = (AM2_Handle *)arg;
    DWORD       rc;

    if (am2_thread_attach)
        am2_thread_attach();
    rc = am2_call_thread_start(h->start, h->param);

    pthread_mutex_lock(&am2_h_lock);
    h->exitCode = rc;
    h->done = 1;
    pthread_cond_broadcast(&am2_h_cond);
    pthread_mutex_unlock(&am2_h_lock);
    am2_handle_release(h);
    return NULL;
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES attr, SIZE_T stack,
                           LPTHREAD_START_ROUTINE start, LPVOID param,
                           DWORD flags, LPDWORD id)
{
    AM2_Handle    *h = am2_handle_new(AM2_H_THREAD);
    pthread_attr_t pa;

    (void)attr; (void)flags;
    if (!h)
        return NULL;
    h->start = start;
    h->param = param;
    h->exitCode = STILL_ACTIVE;
    h->refs = 2;                       /* the caller's handle and the thread */
    pthread_attr_init(&pa);
    if (stack)
        pthread_attr_setstacksize(&pa, stack < 65536 ? 65536 : stack);
    if (pthread_create(&h->thread, &pa, am2_thread_main, h) != 0) {
        pthread_attr_destroy(&pa);
        free(h);
        return NULL;
    }
    pthread_attr_destroy(&pa);
    pthread_detach(h->thread);
    if (id)
        *id = (DWORD)(uintptr_t)h;
    return (HANDLE)h;
}

BOOL WINAPI GetExitCodeThread(HANDLE thread, LPDWORD code)
{
    AM2_Handle *h = (AM2_Handle *)thread;

    if (!h || h->kind != AM2_H_THREAD)
        return FALSE;
    pthread_mutex_lock(&am2_h_lock);
    *code = h->done ? h->exitCode : STILL_ACTIVE;
    pthread_mutex_unlock(&am2_h_lock);
    return TRUE;
}

BOOL WINAPI SetThreadPriority(HANDLE thread, int32_t prio)
{
    (void)thread; (void)prio;
    return TRUE;
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES attr, BOOL manualReset,
                           BOOL initialState, LPCSTR name)
{
    AM2_Handle *h = am2_handle_new(AM2_H_EVENT);

    (void)attr; (void)name;
    if (!h)
        return NULL;
    h->manualReset = manualReset;
    h->signaled = initialState;
    return (HANDLE)h;
}

BOOL WINAPI SetEvent(HANDLE ev)
{
    AM2_Handle *h = (AM2_Handle *)ev;

    if (!h || h->kind != AM2_H_EVENT)
        return FALSE;
    pthread_mutex_lock(&am2_h_lock);
    h->signaled = 1;
    pthread_cond_broadcast(&am2_h_cond);
    pthread_mutex_unlock(&am2_h_lock);
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE ev)
{
    AM2_Handle *h = (AM2_Handle *)ev;

    if (!h || h->kind != AM2_H_EVENT)
        return FALSE;
    pthread_mutex_lock(&am2_h_lock);
    h->signaled = 0;
    pthread_mutex_unlock(&am2_h_lock);
    return TRUE;
}

/* The one-instance mutex. Nothing here is cross-process, so a mutex is a
 * handle that exists: CreateMutexA makes one, OpenMutexA finds none, and
 * the game reads that as "no other copy running". */
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES attr, BOOL owner, LPCSTR name)
{
    AM2_Handle *h = am2_handle_new(AM2_H_MUTEX);

    (void)attr; (void)owner; (void)name;
    if (!h)
        return NULL;
    am2_last_error = ERROR_SUCCESS;
    return (HANDLE)h;
}

HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name)
{
    (void)access; (void)inherit; (void)name;
    am2_last_error = ERROR_FILE_NOT_FOUND;
    return NULL;
}

BOOL WINAPI ReleaseMutex(HANDLE m)
{
    AM2_Handle *h = (AM2_Handle *)m;
    return h && h->kind == AM2_H_MUTEX;
}

/* Wait on the shared condition until `ready` says so or the deadline
 * passes. Called with the lock held. */
static DWORD am2_wait_until(int32_t (*ready)(HANDLE *, DWORD, BOOL),
                            HANDLE *hs, DWORD n, BOOL all, DWORD ms)
{
    struct timespec deadline;

    if (ms != INFINITE) {
        struct timeval now;
        gettimeofday(&now, NULL);
        deadline.tv_sec = now.tv_sec + ms / 1000;
        deadline.tv_nsec = (now.tv_usec + (ms % 1000) * 1000) * 1000;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    for (;;) {
        int32_t r = ready(hs, n, all);
        if (r >= 0)
            return (DWORD)r;
        if (ms == INFINITE) {
            pthread_cond_wait(&am2_h_cond, &am2_h_lock);
        } else if (pthread_cond_timedwait(&am2_h_cond, &am2_h_lock, &deadline)
                   == ETIMEDOUT) {
            r = ready(hs, n, all);
            return r >= 0 ? (DWORD)r : WAIT_TIMEOUT;
        }
    }
}

/* Is handle i satisfied? Consumes an auto-reset event when it is. */
static int32_t am2_handle_ready(AM2_Handle *h, int32_t consume)
{
    switch (h->kind) {
    case AM2_H_THREAD:
        return h->done;
    case AM2_H_EVENT:
        if (!h->signaled)
            return 0;
        if (consume && !h->manualReset)
            h->signaled = 0;
        return 1;
    case AM2_H_MUTEX:
        return 1;
    }
    return 1;
}

static int32_t am2_ready_any(HANDLE *hs, DWORD n, BOOL all)
{
    DWORD i;

    if (all) {
        for (i = 0; i < n; i++)
            if (!am2_handle_ready((AM2_Handle *)hs[i], 0))
                return -1;
        for (i = 0; i < n; i++)
            am2_handle_ready((AM2_Handle *)hs[i], 1);
        return WAIT_OBJECT_0;
    }
    for (i = 0; i < n; i++)
        if (am2_handle_ready((AM2_Handle *)hs[i], 1))
            return (int32_t)(WAIT_OBJECT_0 + i);
    return -1;
}

DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms)
{
    DWORD rc;

    if (!h)
        return WAIT_FAILED;
    pthread_mutex_lock(&am2_h_lock);
    rc = am2_wait_until(am2_ready_any, &h, 1, FALSE, ms);
    pthread_mutex_unlock(&am2_h_lock);
    return rc;
}

DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms)
{
    HANDLE copy[64];
    DWORD  rc, i;

    if (n == 0 || n > 64)
        return WAIT_FAILED;
    for (i = 0; i < n; i++) {
        if (!hs[i])
            return WAIT_FAILED;
        copy[i] = hs[i];
    }
    pthread_mutex_lock(&am2_h_lock);
    rc = am2_wait_until(am2_ready_any, copy, n, all, ms);
    pthread_mutex_unlock(&am2_h_lock);
    return rc;
}

BOOL WINAPI CloseHandle(HANDLE h)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return FALSE;
    am2_handle_release((AM2_Handle *)h);
    return TRUE;
}

/* ---- critical sections ------------------------------------------------------- */

void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    pthread_mutex_t    *m = (pthread_mutex_t *)malloc(sizeof *m);
    pthread_mutexattr_t a;

    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    cs->impl = m;
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    if (cs->impl) {
        pthread_mutex_destroy((pthread_mutex_t *)cs->impl);
        free(cs->impl);
        cs->impl = NULL;
    }
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    if (!cs->impl)
        InitializeCriticalSection(cs);
    pthread_mutex_lock((pthread_mutex_t *)cs->impl);
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    if (cs->impl)
        pthread_mutex_unlock((pthread_mutex_t *)cs->impl);
}

/* ---- the exception hook ------------------------------------------------------- */

/* runtime.cpp registers one handler, whose job is to name a call into the
 * original's .text range -- the int3 gap -- and its caller. Natively that
 * range is not executable at all, so the call arrives as SIGSEGV with EIP
 * at the callee; SIGTRAP and SIGILL are taken too, for the same shape by
 * other means. The handler is given the Win32 view of the fault, and
 * whatever it says the process then dies the ordinary way, with a line on
 * stderr first so a native run does not have to find the log. */
static PVECTORED_EXCEPTION_HANDLER am2_veh;

static void am2_signal(int sig, siginfo_t *info, void *uc_)
{
    ucontext_t        *uc = (ucontext_t *)uc_;
    EXCEPTION_RECORD   er;
    CONTEXT            ctx;
    EXCEPTION_POINTERS ep;
    uintptr_t          eip = (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
    uintptr_t          esp = (uintptr_t)uc->uc_mcontext.gregs[REG_ESP];

    memset(&er, 0, sizeof er);
    memset(&ctx, 0, sizeof ctx);
    er.ExceptionCode = sig == SIGSEGV ? EXCEPTION_ACCESS_VIOLATION
                     : sig == SIGTRAP ? EXCEPTION_BREAKPOINT
                     : EXCEPTION_ILLEGAL_INSTRUCTION;
    er.ExceptionAddress = (PVOID)eip;
    er.NumberParameters = 2;
    er.ExceptionInformation[0] = 0;
    er.ExceptionInformation[1] = (ULONG_PTR)info->si_addr;
    ctx.Eip = (DWORD)eip;
    ctx.Esp = (DWORD)esp;
    ctx.Eax = uc->uc_mcontext.gregs[REG_EAX];
    ctx.Ebx = uc->uc_mcontext.gregs[REG_EBX];
    ctx.Ecx = uc->uc_mcontext.gregs[REG_ECX];
    ctx.Edx = uc->uc_mcontext.gregs[REG_EDX];
    ctx.Esi = uc->uc_mcontext.gregs[REG_ESI];
    ctx.Edi = uc->uc_mcontext.gregs[REG_EDI];
    ctx.Ebp = uc->uc_mcontext.gregs[REG_EBP];
    ep.ExceptionRecord = &er;
    ep.ContextRecord = &ctx;

    fprintf(stderr, "platform: fatal signal %d at eip=0x%08lx addr=%p "
            "(return address 0x%08lx)\n", sig, (unsigned long)eip,
            info->si_addr, (unsigned long)(esp ? *(uintptr_t *)esp : 0));
    if (am2_veh && am2_veh(&ep) == EXCEPTION_CONTINUE_EXECUTION)
        return;
    signal(sig, SIG_DFL);
    raise(sig);
}

PVOID WINAPI AddVectoredExceptionHandler(ULONG first,
                                         PVECTORED_EXCEPTION_HANDLER handler)
{
    struct sigaction sa;

    (void)first;
    am2_veh = handler;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = am2_signal;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    return (PVOID)handler;
}

/* ---- registry ----------------------------------------------------------------- */

/* The game creates one key at startup and closes it; it stores nothing. */
LONG WINAPI RegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls,
                            DWORD options, DWORD access,
                            LPSECURITY_ATTRIBUTES attr, PHKEY out,
                            LPDWORD disposition)
{
    (void)key; (void)sub; (void)reserved; (void)cls; (void)options;
    (void)access; (void)attr;
    if (out)
        *out = (HKEY)(uintptr_t)0x5A5A0001u;
    if (disposition)
        *disposition = REG_OPENED_EXISTING_KEY;
    return ERROR_SUCCESS;
}

LONG WINAPI RegCloseKey(HKEY key) { (void)key; return ERROR_SUCCESS; }

/* ---- drives ---------------------------------------------------------------------- */

/* One fixed drive and no CD. The CD check in this retail binary is patched
 * out already; what remains asks these three and takes the no-disc path. */
DWORD WINAPI GetLogicalDriveStringsA(DWORD cap, LPSTR out)
{
    static const char drives[] = "C:\\\0";
    if (cap >= sizeof drives)
        memcpy(out, drives, sizeof drives);
    return sizeof drives - 1;
}

UINT WINAPI GetDriveTypeA(LPCSTR root)
{
    (void)root;
    return DRIVE_FIXED;
}

BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volName, DWORD volCap,
                                  LPDWORD serial, LPDWORD maxComp,
                                  LPDWORD fsFlags, LPSTR fsName, DWORD fsCap)
{
    (void)root;
    if (volName && volCap)
        volName[0] = 0;
    if (serial)
        *serial = 0;
    if (maxComp)
        *maxComp = 255;
    if (fsFlags)
        *fsFlags = 0;
    if (fsName && fsCap)
        fsName[0] = 0;
    return FALSE;
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR path)
{
    char native[AM2_NATIVE_PATH_MAX];

    if (!path)
        return FALSE;
    if (chdir(am2_native_path(path, native)) != 0) {
        am2_last_error = errno == ENOENT ? ERROR_PATH_NOT_FOUND : ERROR_ACCESS_DENIED;
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI GetCurrentDirectoryA(DWORD cap, LPSTR out)
{
    char buf[AM2_NATIVE_PATH_MAX];

    if (!getcwd(buf, sizeof buf))
        return 0;
    if (strlen(buf) + 1 > cap)
        return (DWORD)strlen(buf) + 1;
    strcpy(out, buf);
    return (DWORD)strlen(buf);
}

/* ---- COM ------------------------------------------------------------------------ */

HRESULT WINAPI CoInitialize(LPVOID reserved) { (void)reserved; return S_OK; }
void    WINAPI CoUninitialize(void) { }

/* ---- winsock ------------------------------------------------------------------------ */

int32_t WINAPI WSAStartup(WORD version, LPWSADATA out)
{
    if (out) {
        memset(out, 0, sizeof *out);
        out->wVersion = version;
        out->wHighVersion = version;
    }
    return 0;
}

int32_t WINAPI WSACleanup(void) { return 0; }
int32_t WINAPI WSAGetLastError(void) { return errno; }
int32_t WINAPI closesocket(SOCKET s) { return close(s); }

int32_t am2_ws_setsockopt(SOCKET s, int32_t level, int32_t name,
                          const char *val, int32_t len)
{
    /* Winsock's receive timeout is a DWORD of milliseconds; POSIX wants a
     * timeval. control.c passes the former. */
    if (level == SOL_SOCKET && name == SO_RCVTIMEO && len == 4) {
        struct timeval tv;
        DWORD ms = *(const DWORD *)val;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return setsockopt(s, level, name, &tv, sizeof tv);
    }
    return setsockopt(s, level, name, val, (socklen_t)len);
}
