/* kernel32crt.cpp -- the kernel32 surface the MSVC 6 CRT inside
 * ArmyMen2.exe imports, for the PE loader in src/hybrid.
 *
 * The reconstruction links against glibc and never calls any of this. The
 * ORIGINAL carries its own statically linked CRT, and that CRT reaches the
 * operating system through fifty-odd kernel32 imports: a heap and the
 * virtual memory its small-block allocator sits on, the environment and
 * the command line, standard handles and file I/O, the find-file walk,
 * code-page and string-classification queries its locale code makes at
 * startup, the clock, and the process exit. Every caller of every function
 * here sits above 0x00465000 in the image -- the CRT's half -- so what is
 * being emulated is Microsoft's runtime, not the game.
 *
 * Paths go through crt.cpp's am2_native_path, as the platform's own CRT
 * names do, so "<dir>\data\bootcamp" finds the same files either way.
 */
#include "platform.h"
#include "handle.h"
#include <direct.h>
#include <io.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>

extern char **environ;

/* ---- the command line and startup ----------------------------------------- */

static char am2_cmdline[4096] = "\"ArmyMen2.exe\"";

void am2_set_command_line(const char *args)
{
    snprintf(am2_cmdline, sizeof am2_cmdline, "\"ArmyMen2.exe\"%s%s",
             args && *args ? " " : "", args ? args : "");
}

LPSTR WINAPI GetCommandLineA(void) { return am2_cmdline; }

/* Windows 98 SE. The CRT reads the platform bit to pick between its ANSI
 * and Unicode paths and nothing in the game reads it at all. */
DWORD WINAPI GetVersion(void) { return 0xC0000A04u; }

void WINAPI GetStartupInfoA(LPSTARTUPINFOA out)
{
    memset(out, 0, sizeof *out);
    out->cb = sizeof *out;
    out->wShowWindow = SW_SHOWNORMAL;
}

HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(LONG_PTR)-1; }

void WINAPI ExitProcess(UINT code) { am2_host_exit((int32_t)code); }

BOOL WINAPI TerminateProcess(HANDLE proc, UINT code)
{
    (void)proc;
    am2_host_exit((int32_t)code);
}

static LPTOP_LEVEL_EXCEPTION_FILTER am2_top_filter;

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI
SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f)
{
    LPTOP_LEVEL_EXCEPTION_FILTER prev = am2_top_filter;
    am2_top_filter = f;
    return prev;
}

LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    return EXCEPTION_EXECUTE_HANDLER;
}

/* Only an exception being dispatched reaches this, and the loader has no
 * SEH dispatcher: a fault ends the process with the platform's signal line
 * before the chain is ever walked. Say so rather than unwind wrongly. */
void WINAPI RtlUnwind(PVOID frame, PVOID target, PEXCEPTION_RECORD er, PVOID ret)
{
    (void)frame; (void)target; (void)er; (void)ret;
    am2_plat_log("RtlUnwind: structured exception handling is not provided");
    abort();
}

LONG WINAPI am2_InterlockedExchange(volatile LONG *target, LONG value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

BOOL WINAPI IsBadCodePtr(FARPROC p) { return IsBadReadPtr((LPCVOID)p, 1); }

/* ---- heaps ------------------------------------------------------------------ */

/* One heap: malloc's. The CRT creates its own with HeapCreate and hands
 * the handle back on every call, and there is nothing to tell apart. */
HANDLE WINAPI HeapCreate(DWORD options, SIZE_T initial, SIZE_T max)
{
    static int32_t n;
    (void)options; (void)initial; (void)max;
    return (HANDLE)(uintptr_t)(0x48450000u + (uint32_t)++n);
}

BOOL WINAPI HeapDestroy(HANDLE heap) { (void)heap; return TRUE; }

LPVOID WINAPI HeapAlloc(HANDLE heap, DWORD flags, SIZE_T n)
{
    void *p = malloc(n ? n : 1);
    (void)heap;
    if (p && (flags & HEAP_ZERO_MEMORY))
        memset(p, 0, n);
    return p;
}

BOOL WINAPI HeapFree(HANDLE heap, DWORD flags, LPVOID p)
{
    (void)heap; (void)flags;
    free(p);
    return TRUE;
}

LPVOID WINAPI HeapReAlloc(HANDLE heap, DWORD flags, LPVOID p, SIZE_T n)
{
    size_t old = p ? malloc_usable_size(p) : 0;
    void  *q;

    (void)heap;
    if (flags & HEAP_REALLOC_IN_PLACE_ONLY)
        return n <= old ? p : NULL;
    q = realloc(p, n ? n : 1);
    if (q && (flags & HEAP_ZERO_MEMORY) && n > old)
        memset((char *)q + old, 0, n - old);
    return q;
}

SIZE_T WINAPI HeapSize(HANDLE heap, DWORD flags, LPCVOID p)
{
    (void)heap; (void)flags;
    return p ? malloc_usable_size((void *)p) : 0;
}

/* ---- virtual memory ---------------------------------------------------------- */

/* The CRT's small-block heap reserves a megabyte at a time and commits it
 * in 32 KB steps; a reservation here is an anonymous mapping, already
 * readable and writable and zero, and a commit inside one is the address
 * handed back. What has to be remembered is a reservation's size, so a
 * release can unmap it. */
typedef struct AM2_VmRegion { void *base; size_t size; } AM2_VmRegion;
static AM2_VmRegion     am2_vm[256];
static pthread_mutex_t  am2_vm_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t am2_page_round(size_t n) { return (n + 4095) & ~(size_t)4095; }

LPVOID WINAPI VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD prot)
{
    int32_t exec = prot == PAGE_EXECUTE_READWRITE || (prot & 0xF0) != 0;
    void   *p;
    size_t  i;

    if (size == 0)
        return NULL;
    if ((type & MEM_RESERVE) || !addr) {
        p = mmap(addr, am2_page_round(size),
                 PROT_READ | PROT_WRITE | (exec ? PROT_EXEC : 0),
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        pthread_mutex_lock(&am2_vm_lock);
        for (i = 0; i < sizeof am2_vm / sizeof am2_vm[0]; i++)
            if (!am2_vm[i].base) {
                am2_vm[i].base = p;
                am2_vm[i].size = am2_page_round(size);
                break;
            }
        pthread_mutex_unlock(&am2_vm_lock);
        return p;
    }
    /* A commit inside a reservation: the pages are there already. */
    return (LPVOID)((uintptr_t)addr & ~(uintptr_t)4095);
}

BOOL WINAPI VirtualFree(LPVOID addr, SIZE_T size, DWORD type)
{
    size_t i;

    if (type & MEM_RELEASE) {
        pthread_mutex_lock(&am2_vm_lock);
        for (i = 0; i < sizeof am2_vm / sizeof am2_vm[0]; i++)
            if (am2_vm[i].base == addr) {
                munmap(am2_vm[i].base, am2_vm[i].size);
                am2_vm[i].base = NULL;
                break;
            }
        pthread_mutex_unlock(&am2_vm_lock);
        return TRUE;
    }
    if (type & MEM_DECOMMIT) {
        uintptr_t a = (uintptr_t)addr & ~(uintptr_t)4095;
        madvise((void *)a, am2_page_round(size + ((uintptr_t)addr - a)), MADV_DONTNEED);
        return TRUE;
    }
    return FALSE;
}

/* ---- the environment ----------------------------------------------------------- */

LPSTR WINAPI GetEnvironmentStrings(void)
{
    size_t total = 1, i;
    char  *block, *p;

    for (i = 0; environ && environ[i]; i++)
        total += strlen(environ[i]) + 1;
    block = (char *)malloc(total + 1);
    if (!block)
        return NULL;
    p = block;
    for (i = 0; environ && environ[i]; i++) {
        size_t n = strlen(environ[i]) + 1;
        memcpy(p, environ[i], n);
        p += n;
    }
    *p++ = 0;
    if (p == block + 1)
        *p = 0;
    return block;
}

BOOL WINAPI FreeEnvironmentStringsA(LPSTR env) { free(env); return TRUE; }

LPWSTR WINAPI GetEnvironmentStringsW(void)
{
    char   *a = GetEnvironmentStrings();
    size_t  n = 0, i;
    WCHAR  *w;

    if (!a)
        return NULL;
    while (a[n] || a[n + 1])
        n++;
    n += 2;
    w = (WCHAR *)malloc(n * sizeof *w);
    if (w)
        for (i = 0; i < n; i++)
            w[i] = (unsigned char)a[i];
    free(a);
    return w;
}

BOOL WINAPI FreeEnvironmentStringsW(LPWSTR env) { free(env); return TRUE; }

BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value)
{
    if (!name)
        return FALSE;
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
    return TRUE;
}

/* ---- code pages and strings ----------------------------------------------------- */

/* One byte is one character and it is Latin-1: the game's strings are
 * ASCII, and the CRT asks these questions once at startup to build its
 * per-byte classification tables. */
UINT WINAPI GetACP(void) { return 1252; }
UINT WINAPI GetOEMCP(void) { return 437; }

BOOL WINAPI GetCPInfo(UINT cp, LPCPINFO out)
{
    (void)cp;
    memset(out, 0, sizeof *out);
    out->MaxCharSize = 1;
    out->DefaultChar[0] = '?';
    return TRUE;
}

static size_t am2_wlen(const WCHAR *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int32_t WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int32_t n,
                                   LPWSTR out, int32_t cap)
{
    int32_t count, i;

    (void)cp; (void)flags;
    if (!src)
        return 0;
    count = n < 0 ? (int32_t)strlen(src) + 1 : n;
    if (cap == 0)
        return count;
    if (cap < count) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < count; i++)
        out[i] = (unsigned char)src[i];
    return count;
}

int32_t WINAPI WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int32_t n,
                                   LPSTR out, int32_t cap, LPCSTR defChar,
                                   LPBOOL usedDef)
{
    int32_t count, i;

    (void)cp; (void)flags; (void)defChar;
    if (usedDef)
        *usedDef = FALSE;
    if (!src)
        return 0;
    count = n < 0 ? (int32_t)am2_wlen(src) + 1 : n;
    if (cap == 0)
        return count;
    if (cap < count) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (src[i] < 256)
            out[i] = (char)src[i];
        else {
            out[i] = '?';
            if (usedDef)
                *usedDef = TRUE;
        }
    }
    return count;
}

static WORD am2_ctype1(uint32_t c)
{
    WORD t = 0;

    if (c >= 128)
        return c == 0xA0 ? C1_SPACE | C1_BLANK : 0;
    if (isupper((int)c))  t |= C1_UPPER | C1_ALPHA;
    if (islower((int)c))  t |= C1_LOWER | C1_ALPHA;
    if (isdigit((int)c))  t |= C1_DIGIT;
    if (isspace((int)c))  t |= C1_SPACE;
    if (ispunct((int)c))  t |= C1_PUNCT;
    if (iscntrl((int)c))  t |= C1_CNTRL;
    if (c == ' ' || c == '\t') t |= C1_BLANK;
    if (isxdigit((int)c)) t |= C1_XDIGIT;
    return t;
}

BOOL WINAPI GetStringTypeA(LCID lc, DWORD type, LPCSTR src, int32_t n, LPWORD out)
{
    int32_t i;

    (void)lc;
    if (n < 0)
        n = (int32_t)strlen(src);
    for (i = 0; i < n; i++)
        out[i] = type == CT_CTYPE1 ? am2_ctype1((unsigned char)src[i]) : 0;
    return TRUE;
}

BOOL WINAPI GetStringTypeW(DWORD type, LPCWSTR src, int32_t n, LPWORD out)
{
    int32_t i;

    if (n < 0)
        n = (int32_t)am2_wlen(src);
    for (i = 0; i < n; i++)
        out[i] = type == CT_CTYPE1 ? am2_ctype1(src[i]) : 0;
    return TRUE;
}

static uint32_t am2_map_case(uint32_t c, DWORD flags)
{
    if (c >= 128)
        return c;
    if (flags & LCMAP_UPPERCASE)
        return (uint32_t)toupper((int)c);
    if (flags & LCMAP_LOWERCASE)
        return (uint32_t)tolower((int)c);
    return c;
}

int32_t WINAPI LCMapStringA(LCID lc, DWORD flags, LPCSTR src, int32_t n,
                            LPSTR out, int32_t cap)
{
    int32_t i;

    (void)lc;
    if (n < 0)
        n = (int32_t)strlen(src) + 1;
    if (cap == 0)
        return n;
    if (cap < n) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < n; i++)
        out[i] = (char)am2_map_case((unsigned char)src[i], flags);
    return n;
}

int32_t WINAPI LCMapStringW(LCID lc, DWORD flags, LPCWSTR src, int32_t n,
                            LPWSTR out, int32_t cap)
{
    int32_t i;

    (void)lc;
    if (n < 0)
        n = (int32_t)am2_wlen(src) + 1;
    if (cap == 0)
        return n;
    if (cap < n) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < n; i++)
        out[i] = (WCHAR)am2_map_case(src[i], flags);
    return n;
}

int32_t WINAPI CompareStringA(LCID lc, DWORD flags, LPCSTR a, int32_t na,
                              LPCSTR b, int32_t nb)
{
    int32_t i, la, lb, n, r = 0;

    (void)lc;
    la = na < 0 ? (int32_t)strlen(a) : na;
    lb = nb < 0 ? (int32_t)strlen(b) : nb;
    n = la < lb ? la : lb;
    for (i = 0; i < n && r == 0; i++) {
        uint32_t x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (flags & NORM_IGNORECASE) {
            x = am2_map_case(x, LCMAP_LOWERCASE);
            y = am2_map_case(y, LCMAP_LOWERCASE);
        }
        r = (int32_t)x - (int32_t)y;
    }
    if (r == 0)
        r = la - lb;
    return r < 0 ? CSTR_LESS_THAN : r > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

int32_t WINAPI CompareStringW(LCID lc, DWORD flags, LPCWSTR a, int32_t na,
                              LPCWSTR b, int32_t nb)
{
    int32_t i, la, lb, n, r = 0;

    (void)lc;
    la = na < 0 ? (int32_t)am2_wlen(a) : na;
    lb = nb < 0 ? (int32_t)am2_wlen(b) : nb;
    n = la < lb ? la : lb;
    for (i = 0; i < n && r == 0; i++) {
        uint32_t x = a[i], y = b[i];
        if (flags & NORM_IGNORECASE) {
            x = am2_map_case(x, LCMAP_LOWERCASE);
            y = am2_map_case(y, LCMAP_LOWERCASE);
        }
        r = (int32_t)x - (int32_t)y;
    }
    if (r == 0)
        r = la - lb;
    return r < 0 ? CSTR_LESS_THAN : r > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

/* ---- time ------------------------------------------------------------------------ */

/* FILETIME counts 100 ns from 1601; time_t counts seconds from 1970. */
#define AM2_FT_EPOCH 116444736000000000ULL

static uint64_t am2_ft_get(const FILETIME *ft)
{
    return ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
}

static void am2_ft_set(FILETIME *ft, uint64_t v)
{
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

static void am2_ft_from_time(FILETIME *ft, time_t t)
{
    am2_ft_set(ft, (uint64_t)t * 10000000ULL + AM2_FT_EPOCH);
}

static void am2_systemtime_from_tm(LPSYSTEMTIME st, const struct tm *t, uint32_t ms)
{
    st->wYear = (WORD)(t->tm_year + 1900);
    st->wMonth = (WORD)(t->tm_mon + 1);
    st->wDayOfWeek = (WORD)t->tm_wday;
    st->wDay = (WORD)t->tm_mday;
    st->wHour = (WORD)t->tm_hour;
    st->wMinute = (WORD)t->tm_min;
    st->wSecond = (WORD)t->tm_sec;
    st->wMilliseconds = (WORD)ms;
}

/* Lockstep's date: the image's own link date, plus the virtual clock. */
static void am2_lockstep_timeval(struct timeval *tv)
{
    uint64_t ns = am2_host_clock_ns();
    tv->tv_sec = 918000000 + (time_t)(ns / 1000000000ULL);
    tv->tv_usec = (suseconds_t)((ns % 1000000000ULL) / 1000);
}

void WINAPI GetSystemTime(LPSYSTEMTIME out)
{
    struct timeval tv;
    struct tm      t;

    if (am2_host_lockstep())
        am2_lockstep_timeval(&tv);
    else
        gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &t);
    am2_systemtime_from_tm(out, &t, (uint32_t)(tv.tv_usec / 1000));
}

void WINAPI GetLocalTime(LPSYSTEMTIME out)
{
    struct timeval tv;
    struct tm      t;

    if (am2_host_lockstep())
        am2_lockstep_timeval(&tv);
    else
        gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &t);
    am2_systemtime_from_tm(out, &t, (uint32_t)(tv.tv_usec / 1000));
}

static int32_t am2_gmtoff_seconds(void)
{
    time_t    now = time(NULL);
    struct tm t;

    localtime_r(&now, &t);
    return (int32_t)t.tm_gmtoff;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *in, LPFILETIME out)
{
    am2_ft_set(out, am2_ft_get(in) + (int64_t)am2_gmtoff_seconds() * 10000000LL);
    return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME *in, LPSYSTEMTIME out)
{
    uint64_t  v = am2_ft_get(in);
    time_t    secs = (time_t)((int64_t)(v - AM2_FT_EPOCH) / 10000000LL);
    struct tm t;

    gmtime_r(&secs, &t);
    am2_systemtime_from_tm(out, &t, (uint32_t)((v / 10000ULL) % 1000ULL));
    return TRUE;
}

/* The bias of the moment and no daylight rule, so the CRT's local time is
 * right today and stays a fixed offset; nothing the game does with a time
 * is finer than naming a save. */
DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION out)
{
    memset(out, 0, sizeof *out);
    out->Bias = -am2_gmtoff_seconds() / 60;
    return TIME_ZONE_ID_UNKNOWN;
}

/* ---- files ------------------------------------------------------------------------ */

static AM2_Handle *am2_file_handle(int32_t fd, int32_t keep)
{
    AM2_Handle *h = am2_handle_new(AM2_H_FILE);

    if (!h)
        return NULL;
    h->fd = fd;
    h->keepFd = keep;
    return h;
}

static int32_t am2_fd_of(HANDLE h)
{
    AM2_Handle *x = (AM2_Handle *)h;

    if (!x || h == INVALID_HANDLE_VALUE || x->kind != AM2_H_FILE)
        return -1;
    return x->fd;
}

static DWORD am2_errno_to_error(void)
{
    switch (errno) {
    case ENOENT:  return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:   return ERROR_ACCESS_DENIED;
    case EEXIST:  return ERROR_ALREADY_EXISTS;
    case EBADF:   return ERROR_INVALID_HANDLE;
    default:      return ERROR_INVALID_PARAMETER;
    }
}

HANDLE WINAPI GetStdHandle(DWORD which)
{
    static AM2_Handle *std3[3];
    int32_t i = which == STD_INPUT_HANDLE ? 0
              : which == STD_OUTPUT_HANDLE ? 1
              : which == STD_ERROR_HANDLE ? 2 : -1;

    if (i < 0)
        return INVALID_HANDLE_VALUE;
    if (!std3[i])
        std3[i] = am2_file_handle(i, 1);
    return (HANDLE)std3[i];
}

BOOL WINAPI SetStdHandle(DWORD which, HANDLE h) { (void)which; (void)h; return TRUE; }
UINT WINAPI SetHandleCount(UINT n) { return n; }

DWORD WINAPI GetFileType(HANDLE h)
{
    int32_t     fd = am2_fd_of(h);
    struct stat st;

    if (fd < 0 || fstat(fd, &st) != 0)
        return FILE_TYPE_UNKNOWN;
    if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
        return FILE_TYPE_DISK;
    if (S_ISCHR(st.st_mode))
        return FILE_TYPE_CHAR;
    return FILE_TYPE_PIPE;
}

HANDLE WINAPI CreateFileA(LPCSTR path, DWORD access, DWORD share,
                          LPSECURITY_ATTRIBUTES sa, DWORD create, DWORD attrs,
                          HANDLE tmpl)
{
    char        native[AM2_NATIVE_PATH_MAX];
    int32_t     rd = (access & GENERIC_READ) != 0;
    int32_t     wr = (access & GENERIC_WRITE) != 0;
    int32_t     flags = wr ? (rd ? O_RDWR : O_WRONLY) : O_RDONLY;
    int32_t     fd;
    AM2_Handle *h;

    (void)share; (void)sa; (void)attrs; (void)tmpl;
    if (!path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    switch (create) {
    case CREATE_NEW:        flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS:     flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS:       flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }
    fd = open(am2_native_path(path, native), flags, 0666);
    if (fd < 0) {
        SetLastError(am2_errno_to_error());
        return INVALID_HANDLE_VALUE;
    }
    h = am2_file_handle(fd, 0);
    if (!h) {
        close(fd);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)h;
}

BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPVOID overlapped)
{
    int32_t fd = am2_fd_of(h);
    ssize_t r;

    (void)overlapped;
    if (got)
        *got = 0;
    if (fd < 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    do
        r = read(fd, buf, n);
    while (r < 0 && errno == EINTR);
    if (r < 0) {
        SetLastError(am2_errno_to_error());
        return FALSE;
    }
    if (got)
        *got = (DWORD)r;
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPVOID overlapped)
{
    int32_t fd = am2_fd_of(h);
    ssize_t r;

    (void)overlapped;
    if (put)
        *put = 0;
    if (fd < 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    do
        r = write(fd, buf, n);
    while (r < 0 && errno == EINTR);
    if (r < 0) {
        SetLastError(am2_errno_to_error());
        return FALSE;
    }
    if (put)
        *put = (DWORD)r;
    return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method)
{
    int32_t fd = am2_fd_of(h);
    int64_t off = hi ? ((int64_t)*hi << 32) | (uint32_t)lo : (int64_t)lo;
    int64_t r;

    if (fd < 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    r = lseek64(fd, off, method == FILE_BEGIN ? SEEK_SET
                       : method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    if (r < 0) {
        SetLastError(am2_errno_to_error());
        return INVALID_SET_FILE_POINTER;
    }
    if (hi)
        *hi = (LONG)(r >> 32);
    return (DWORD)r;
}

BOOL WINAPI SetEndOfFile(HANDLE h)
{
    int32_t fd = am2_fd_of(h);
    off_t   at;

    if (fd < 0)
        return FALSE;
    at = lseek(fd, 0, SEEK_CUR);
    return at >= 0 && ftruncate(fd, at) == 0;
}

BOOL WINAPI FlushFileBuffers(HANDLE h)
{
    int32_t fd = am2_fd_of(h);

    if (fd < 0)
        return FALSE;
    fsync(fd);
    return TRUE;
}

DWORD WINAPI GetFileAttributesA(LPCSTR path)
{
    char        native[AM2_NATIVE_PATH_MAX];
    struct stat st;
    DWORD       a;

    if (!path || stat(am2_native_path(path, native), &st) != 0) {
        SetLastError(path ? am2_errno_to_error() : ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    a = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st.st_mode & S_IWUSR))
        a |= FILE_ATTRIBUTE_READONLY;
    return a;
}

BOOL WINAPI SetFileAttributesA(LPCSTR path, DWORD attrs)
{
    char native[AM2_NATIVE_PATH_MAX];

    if (!path)
        return FALSE;
    return chmod(am2_native_path(path, native),
                 (attrs & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644) == 0;
}

BOOL WINAPI DeleteFileA(LPCSTR path)
{
    char native[AM2_NATIVE_PATH_MAX];

    if (!path || unlink(am2_native_path(path, native)) != 0) {
        SetLastError(path ? am2_errno_to_error() : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
    char native[AM2_NATIVE_PATH_MAX];

    (void)sa;
    if (!path || mkdir(am2_native_path(path, native), 0777) != 0) {
        SetLastError(path ? am2_errno_to_error() : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR path)
{
    char native[AM2_NATIVE_PATH_MAX];

    if (!path || rmdir(am2_native_path(path, native)) != 0) {
        SetLastError(path ? am2_errno_to_error() : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* Absolute if it names a root, else the working directory in front. The
 * result keeps the caller's separators; every consumer translates. */
DWORD WINAPI GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *filePart)
{
    char   full[AM2_NATIVE_PATH_MAX * 2];
    size_t n;

    if (!path)
        return 0;
    if (path[0] == '/' || path[0] == '\\' ||
        (isalpha((unsigned char)path[0]) && path[1] == ':')) {
        snprintf(full, sizeof full, "%s", path);
    } else {
        char cwd[AM2_NATIVE_PATH_MAX];
        if (!getcwd(cwd, sizeof cwd))
            strcpy(cwd, ".");
        snprintf(full, sizeof full, "%s\\%s", cwd, path);
    }
    n = strlen(full);
    if (n + 1 > cap)
        return (DWORD)n + 1;
    memcpy(out, full, n + 1);
    if (filePart) {
        char *s = out + n;
        while (s > out && s[-1] != '/' && s[-1] != '\\')
            s--;
        *filePart = s;
    }
    return (DWORD)n;
}

/* ---- find-file ----------------------------------------------------------------- */

/* crt.cpp's _findfirst does the walk -- the pattern, the case folding, the
 * order -- and this is its record in the shape the original's CRT wants,
 * which turns it straight back into a _finddata_t. */
static void am2_find_data(LPWIN32_FIND_DATAA out, const struct _finddata_t *fd)
{
    memset(out, 0, sizeof *out);
    out->dwFileAttributes = fd->attrib ? fd->attrib : FILE_ATTRIBUTE_NORMAL;
    am2_ft_from_time(&out->ftCreationTime, fd->time_create);
    am2_ft_from_time(&out->ftLastAccessTime, fd->time_access);
    am2_ft_from_time(&out->ftLastWriteTime, fd->time_write);
    out->nFileSizeLow = fd->size;
    snprintf(out->cFileName, sizeof out->cFileName, "%s", fd->name);
}

HANDLE WINAPI FindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA out)
{
    struct _finddata_t fd;
    intptr_t           f;
    AM2_Handle        *h;

    if (!spec) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    f = _findfirst(spec, &fd);
    if (f < 0) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    h = am2_handle_new(AM2_H_FIND);
    if (!h) {
        _findclose(f);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    h->find = f;
    am2_find_data(out, &fd);
    return (HANDLE)h;
}

BOOL WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA out)
{
    AM2_Handle        *x = (AM2_Handle *)h;
    struct _finddata_t fd;

    if (!x || h == INVALID_HANDLE_VALUE || x->kind != AM2_H_FIND) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (_findnext(x->find, &fd) != 0) {
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    am2_find_data(out, &fd);
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE h)
{
    AM2_Handle *x = (AM2_Handle *)h;

    if (!x || h == INVALID_HANDLE_VALUE || x->kind != AM2_H_FIND)
        return FALSE;
    am2_handle_release(x);
    return TRUE;
}
