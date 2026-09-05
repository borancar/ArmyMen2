/* windows.h -- the Win32 surface Army Men II compiles against, natively.
 *
 * This is NOT the SDK header and does not try to be. It declares exactly
 * what src/game/win32, src/inject/control.c, src/inject/input.c and
 * src/standalone/runtime.cpp use -- read out of those sources by
 * intersecting their identifiers with the SDK's -- and nothing else, so a
 * new Win32 dependency in the game is a compile error here rather than a
 * silent link against something the platform layer does not provide.
 *
 * Layouts are the SDK's wherever a structure could be shared with data the
 * original binary carries (.origdat holds DIPROPHEADERs, WAVEFORMATEXs and
 * the like) or wherever the reconstruction static_asserts a size: MSG is 28
 * bytes, DDSURFACEDESC 0x6C, DDBLTFX 0x64, PALETTEENTRY 4. Where a layout is
 * private between the game and src/platform it is still the SDK's, because
 * one rule is cheaper than two.
 *
 * Calling conventions are real: this is an i386 build and the game's own
 * code is stdcall, cdecl and thiscall in the same places the original was.
 *
 * Fixed-width types throughout, with the Win32 names as typedefs of them --
 * this is the one file where DWORD and friends are DEFINED rather than
 * used, which is what CLAUDE.md's rule against them is for.
 */
#ifndef AM2_PLATFORM_WINDOWS_H
#define AM2_PLATFORM_WINDOWS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifndef __i386__
#error "the reconstruction is a 32-bit program: build with -m32"
#endif

/* ---- calling conventions --------------------------------------------- */

#include <callconv.h>
#define WINAPI     __stdcall
#define CALLBACK   __stdcall
#define PASCAL     __stdcall
#define APIENTRY   __stdcall
#define STDMETHODCALLTYPE __stdcall
#define WINBASEAPI
#define WINUSERAPI
#define WINGDIAPI
#define FAR
#define NEAR
#define CONST const
#define VOID void

#ifdef __cplusplus
#define AM2_EXTERN_C_BEGIN extern "C" {
#define AM2_EXTERN_C_END   }
#else
#define AM2_EXTERN_C_BEGIN
#define AM2_EXTERN_C_END
#endif

AM2_EXTERN_C_BEGIN

/* ---- scalar types ------------------------------------------------------ */

typedef int32_t   BOOL, *LPBOOL;
typedef uint8_t   BYTE, *LPBYTE, *PBYTE;
typedef uint16_t  WORD, *LPWORD;
typedef uint32_t  DWORD, *LPDWORD, *PDWORD;
typedef int32_t   LONG, *LPLONG, *PLONG;
typedef uint32_t  ULONG;
typedef int32_t   INT;
typedef uint32_t  UINT, *PUINT;
typedef int16_t   SHORT;
typedef uint16_t  USHORT;
typedef char      CHAR;
typedef uint8_t   UCHAR;
typedef float     FLOAT;
typedef int64_t   LONGLONG;
typedef uint64_t  ULONGLONG;
typedef int32_t   INT_PTR, LONG_PTR, SSIZE_T;
typedef uint32_t  UINT_PTR, ULONG_PTR, DWORD_PTR, SIZE_T;
typedef void     *PVOID, *LPVOID, *HANDLE, **PHANDLE;
typedef const void *LPCVOID;
typedef char     *LPSTR, *PSTR, *NPSTR;
typedef const char *LPCSTR, *PCSTR;
typedef char     *HPSTR;
typedef uint16_t  WCHAR, *LPWSTR;
typedef const uint16_t *LPCWSTR;
typedef int32_t   HRESULT;
typedef int32_t   LRESULT;
typedef uint32_t  WPARAM;
typedef int32_t   LPARAM;
typedef uint32_t  COLORREF, *LPCOLORREF;
typedef WORD      ATOM;
typedef uint32_t  MMRESULT;
typedef int32_t (WINAPI *FARPROC)(void);
typedef int32_t (WINAPI *PROC)(void);

#define TRUE  1
#define FALSE 0
#ifndef NULL
#define NULL 0
#endif
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu

#define DECLARE_HANDLE(name) struct name##__ { int32_t unused; }; typedef struct name##__ *name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HINSTANCE);
typedef HINSTANCE HMODULE;
DECLARE_HANDLE(HICON);
typedef HICON HCURSOR;
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HGDIOBJ_);
typedef void *HGDIOBJ;
typedef HANDLE HGLOBAL;
typedef HKEY *PHKEY;

#define MAKEWORD(a, b)  ((WORD)(((BYTE)((a) & 0xFF)) | ((WORD)((BYTE)((b) & 0xFF))) << 8))
#define MAKELONG(a, b)  ((LONG)(((WORD)((a) & 0xFFFF)) | ((DWORD)((WORD)((b) & 0xFFFF))) << 16))
#define LOWORD(l)       ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l)       ((WORD)(((DWORD_PTR)(l) >> 16) & 0xFFFF))
#define LOBYTE(w)       ((BYTE)((DWORD_PTR)(w) & 0xFF))
#define HIBYTE(w)       ((BYTE)(((DWORD_PTR)(w) >> 8) & 0xFF))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA

/* ---- structures -------------------------------------------------------- */

typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;
typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
typedef GUID IID, CLSID, *LPGUID, *LPIID, *LPCLSID;
typedef const GUID *LPCGUID;
/* As in the SDK under CINTERFACE: a reference in C++, a pointer in C. The
 * game passes its GUIDs as `*(const GUID *)addr`, which binds a reference.
 * AM2_GUID_PTR gives the address either way. */
#ifdef __cplusplus
typedef const GUID &REFGUID, &REFIID, &REFCLSID;
#define AM2_GUID_PTR(g) (&(g))
#else
typedef const GUID *REFGUID, *REFIID, *REFCLSID;
#define AM2_GUID_PTR(g) (g)
#endif

static inline int am2_guid_equal(const GUID *a, const GUID *b)
{
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    return x[0] == y[0] && x[1] == y[1] && x[2] == y[2] && x[3] == y[3];
}
#define IsEqualGUID(a, b) am2_guid_equal(AM2_GUID_PTR(a), AM2_GUID_PTR(b))
#define IsEqualIID IsEqualGUID
#define IsEqualCLSID IsEqualGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern const GUID name
#define AM2_DEFINE_GUID_VALUE(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

/* ---- COM ----------------------------------------------------------------- */

#define S_OK           ((HRESULT)0)
#define S_FALSE        ((HRESULT)1)
#define E_NOTIMPL      ((HRESULT)0x80004001)
#define E_NOINTERFACE  ((HRESULT)0x80004002)
#define E_POINTER      ((HRESULT)0x80004003)
#define E_FAIL         ((HRESULT)0x80004005)
#define E_UNEXPECTED   ((HRESULT)0x8000FFFF)
#define E_OUTOFMEMORY  ((HRESULT)0x8007000E)
#define E_INVALIDARG   ((HRESULT)0x80070057)
#define E_HANDLE       ((HRESULT)0x80070006)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((uint32_t)(sev) << 31) | ((uint32_t)(fac) << 16) | ((uint32_t)(code))))

#define CLSCTX_INPROC_SERVER 0x1

typedef struct IUnknown IUnknown, *LPUNKNOWN;
typedef struct IUnknownVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG   (STDMETHODCALLTYPE *Release)(IUnknown *);
} IUnknownVtbl;
struct IUnknown { const IUnknownVtbl *lpVtbl; };
#define IUnknown_QueryInterface(p, a, b) ((p)->lpVtbl->QueryInterface(p, a, b))
#define IUnknown_AddRef(p)  ((p)->lpVtbl->AddRef(p))
#define IUnknown_Release(p) ((p)->lpVtbl->Release(p))

HRESULT WINAPI CoInitialize(LPVOID reserved);
void    WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD ctx,
                                REFIID iid, LPVOID *out);

/* ---- errors ------------------------------------------------------------ */

#define ERROR_SUCCESS          0
#define ERROR_FILE_NOT_FOUND   2
#define ERROR_PATH_NOT_FOUND   3
#define ERROR_ACCESS_DENIED    5
#define ERROR_INVALID_HANDLE   6
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ALREADY_EXISTS   183
#define WAIT_OBJECT_0          0
#define WAIT_TIMEOUT           0x102
#define WAIT_FAILED            0xFFFFFFFFu

DWORD WINAPI GetLastError(void);
void  WINAPI SetLastError(DWORD err);

/* ---- kernel: time ------------------------------------------------------ */

DWORD WINAPI GetTickCount(void);
void  WINAPI Sleep(DWORD ms);
BOOL  WINAPI QueryPerformanceCounter(LARGE_INTEGER *out);
BOOL  WINAPI QueryPerformanceFrequency(LARGE_INTEGER *out);

/* ---- kernel: modules --------------------------------------------------- */

HMODULE WINAPI LoadLibraryA(LPCSTR name);
BOOL    WINAPI FreeLibrary(HMODULE mod);
FARPROC WINAPI GetProcAddress(HMODULE mod, LPCSTR name);
HMODULE WINAPI GetModuleHandleA(LPCSTR name);
DWORD   WINAPI GetModuleFileNameA(HMODULE mod, LPSTR out, DWORD cap);

/* ---- kernel: memory probes and heaps ---------------------------------- */

BOOL WINAPI IsBadReadPtr(LPCVOID p, UINT_PTR n);
BOOL WINAPI IsBadWritePtr(LPVOID p, UINT_PTR n);
BOOL WINAPI IsBadStringPtrA(LPCSTR p, UINT_PTR max);

#define GMEM_FIXED    0x0000
#define GMEM_ZEROINIT 0x0040
#define GPTR          0x0040
HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T bytes);
HGLOBAL WINAPI GlobalFree(HGLOBAL mem);

/* ---- kernel: files, directories, drives -------------------------------- */

#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6

DWORD WINAPI GetLogicalDriveStringsA(DWORD cap, LPSTR out);
UINT  WINAPI GetDriveTypeA(LPCSTR root);
BOOL  WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volName, DWORD volCap,
                                   LPDWORD serial, LPDWORD maxComp,
                                   LPDWORD fsFlags, LPSTR fsName, DWORD fsCap);
BOOL  WINAPI SetCurrentDirectoryA(LPCSTR path);
DWORD WINAPI GetCurrentDirectoryA(DWORD cap, LPSTR out);

/* ---- kernel: threads and synchronisation ------------------------------ */

typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID param);
typedef struct _SECURITY_ATTRIBUTES {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

#define THREAD_PRIORITY_NORMAL  0
#define THREAD_PRIORITY_HIGHEST 2
#define MUTEX_ALL_ACCESS 0x1F0001u
#define STILL_ACTIVE 259

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES attr, SIZE_T stack,
                           LPTHREAD_START_ROUTINE start, LPVOID param,
                           DWORD flags, LPDWORD id);
BOOL   WINAPI GetExitCodeThread(HANDLE thread, LPDWORD code);
BOOL   WINAPI SetThreadPriority(HANDLE thread, int32_t prio);
BOOL   WINAPI CloseHandle(HANDLE h);
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES attr, BOOL manualReset,
                           BOOL initialState, LPCSTR name);
BOOL   WINAPI SetEvent(HANDLE ev);
BOOL   WINAPI ResetEvent(HANDLE ev);
DWORD  WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
DWORD  WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all,
                                     DWORD ms);
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES attr, BOOL owner, LPCSTR name);
HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
BOOL   WINAPI ReleaseMutex(HANDLE m);

/* Opaque here; sized for what the platform layer keeps in it. */
typedef struct _RTL_CRITICAL_SECTION {
    void    *impl;
    uint32_t reserved[5];
} CRITICAL_SECTION, *LPCRITICAL_SECTION;
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs);
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs);
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs);
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs);

static inline LONG InterlockedExchange(volatile LONG *target, LONG value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedCompareExchange(volatile LONG *target, LONG exch,
                                              LONG cmp)
{
    __atomic_compare_exchange_n(target, &cmp, exch, 0, __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return cmp;
}

/* ---- kernel: exceptions -------------------------------------------------- */

#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_ACCESS_VIOLATION   0xC0000005u
#define EXCEPTION_BREAKPOINT         0x80000003u
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_CONTINUE_SEARCH    0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

typedef struct _EXCEPTION_RECORD {
    DWORD  ExceptionCode;
    DWORD  ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID  ExceptionAddress;
    DWORD  NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

/* The i386 CONTEXT, from ContextFlags to SegSs. The extended registers are
 * not carried, since nothing reads them. */
typedef struct _CONTEXT {
    DWORD ContextFlags;
    DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    BYTE  FloatSave[112];
    DWORD SegGs, SegFs, SegEs, SegDs;
    DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
    DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
} CONTEXT, *PCONTEXT;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT          ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

typedef LONG (CALLBACK *PVECTORED_EXCEPTION_HANDLER)(PEXCEPTION_POINTERS);
PVOID WINAPI AddVectoredExceptionHandler(ULONG first,
                                         PVECTORED_EXCEPTION_HANDLER handler);


/* ---- kernel: what the original's own CRT imports ------------------------ */

/* Declared for src/platform/kernel32crt.cpp and the PE loader; nothing in
 * the reconstruction calls these, since it links against glibc's CRT. */

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define ERROR_NO_MORE_FILES 18
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_INSUFFICIENT_BUFFER 122

#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW        1
#define CREATE_ALWAYS     2
#define OPEN_EXISTING     3
#define OPEN_ALWAYS       4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY  0x01
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE   0x20
#define FILE_ATTRIBUTE_NORMAL    0x80
#define INVALID_FILE_ATTRIBUTES  0xFFFFFFFFu
#define FILE_BEGIN   0
#define FILE_CURRENT 1
#define FILE_END     2
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_DISK    1
#define FILE_TYPE_CHAR    2
#define FILE_TYPE_PIPE    3
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

#define HEAP_ZERO_MEMORY 0x08
#define HEAP_REALLOC_IN_PLACE_ONLY 0x10
#define MEM_COMMIT   0x1000
#define MEM_RESERVE  0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE  0x8000
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40

#define CP_ACP 0
#define CP_OEMCP 1
#define NORM_IGNORECASE 0x01
#define CT_CTYPE1 1
#define C1_UPPER  0x001
#define C1_LOWER  0x002
#define C1_DIGIT  0x004
#define C1_SPACE  0x008
#define C1_PUNCT  0x010
#define C1_CNTRL  0x020
#define C1_BLANK  0x040
#define C1_XDIGIT 0x080
#define C1_ALPHA  0x100
#define LCMAP_LOWERCASE 0x100
#define LCMAP_UPPERCASE 0x200
#define CSTR_LESS_THAN 1
#define CSTR_EQUAL 2
#define CSTR_GREATER_THAN 3
#define MAX_LEADBYTES 12
#define MAX_DEFAULTCHAR 2
#define TIME_ZONE_ID_UNKNOWN 0
#define EXCEPTION_EXECUTE_HANDLER 1

typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;
typedef struct _WIN32_FIND_DATAA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    nFileSizeHigh, nFileSizeLow;
    DWORD    dwReserved0, dwReserved1;
    CHAR     cFileName[MAX_PATH];
    CHAR     cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef struct _STARTUPINFOA {
    DWORD  cb;
    LPSTR  lpReserved, lpDesktop, lpTitle;
    DWORD  dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars;
    DWORD  dwFillAttribute, dwFlags;
    WORD   wShowWindow, cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;
typedef struct _CPINFO {
    UINT MaxCharSize;
    BYTE DefaultChar[MAX_DEFAULTCHAR];
    BYTE LeadByte[MAX_LEADBYTES];
} CPINFO, *LPCPINFO;
typedef struct _TIME_ZONE_INFORMATION {
    LONG       Bias;
    WCHAR      StandardName[32];
    SYSTEMTIME StandardDate;
    LONG       StandardBias;
    WCHAR      DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG       DaylightBias;
} TIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;
typedef LONG (WINAPI *LPTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS *);
typedef DWORD LCID;
typedef WORD *LPWORD_;

int32_t WINAPI CompareStringA(LCID lc, DWORD flags, LPCSTR a, int32_t na, LPCSTR b, int32_t nb);
int32_t WINAPI CompareStringW(LCID lc, DWORD flags, LPCWSTR a, int32_t na, LPCWSTR b, int32_t nb);
BOOL    WINAPI CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa);
HANDLE  WINAPI CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                           DWORD create, DWORD attrs, HANDLE tmpl);
BOOL    WINAPI DeleteFileA(LPCSTR path);
void    WINAPI ExitProcess(UINT code) __attribute__((noreturn));
BOOL    WINAPI FileTimeToLocalFileTime(const FILETIME *in, LPFILETIME out);
BOOL    WINAPI FileTimeToSystemTime(const FILETIME *in, LPSYSTEMTIME out);
BOOL    WINAPI FindClose(HANDLE h);
HANDLE  WINAPI FindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA out);
BOOL    WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA out);
BOOL    WINAPI FlushFileBuffers(HANDLE h);
BOOL    WINAPI FreeEnvironmentStringsA(LPSTR env);
BOOL    WINAPI FreeEnvironmentStringsW(LPWSTR env);
UINT    WINAPI GetACP(void);
LPSTR   WINAPI GetCommandLineA(void);
BOOL    WINAPI GetCPInfo(UINT cp, LPCPINFO out);
HANDLE  WINAPI GetCurrentProcess(void);
LPSTR   WINAPI GetEnvironmentStrings(void);
LPWSTR  WINAPI GetEnvironmentStringsW(void);
DWORD   WINAPI GetFileAttributesA(LPCSTR path);
DWORD   WINAPI GetFileType(HANDLE h);
DWORD   WINAPI GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *filePart);
void    WINAPI GetLocalTime(LPSYSTEMTIME out);
UINT    WINAPI GetOEMCP(void);
void    WINAPI GetStartupInfoA(LPSTARTUPINFOA out);
HANDLE  WINAPI GetStdHandle(DWORD which);
BOOL    WINAPI GetStringTypeA(LCID lc, DWORD type, LPCSTR src, int32_t n, LPWORD out);
BOOL    WINAPI GetStringTypeW(DWORD type, LPCWSTR src, int32_t n, LPWORD out);
void    WINAPI GetSystemTime(LPSYSTEMTIME out);
DWORD   WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION out);
DWORD   WINAPI GetVersion(void);
LPVOID  WINAPI HeapAlloc(HANDLE heap, DWORD flags, SIZE_T n);
HANDLE  WINAPI HeapCreate(DWORD options, SIZE_T initial, SIZE_T max);
BOOL    WINAPI HeapDestroy(HANDLE heap);
BOOL    WINAPI HeapFree(HANDLE heap, DWORD flags, LPVOID p);
LPVOID  WINAPI HeapReAlloc(HANDLE heap, DWORD flags, LPVOID p, SIZE_T n);
SIZE_T  WINAPI HeapSize(HANDLE heap, DWORD flags, LPCVOID p);
BOOL    WINAPI IsBadCodePtr(FARPROC p);
int32_t WINAPI LCMapStringA(LCID lc, DWORD flags, LPCSTR src, int32_t n, LPSTR out, int32_t cap);
int32_t WINAPI LCMapStringW(LCID lc, DWORD flags, LPCWSTR src, int32_t n, LPWSTR out, int32_t cap);
int32_t WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int32_t n, LPWSTR out, int32_t cap);
BOOL    WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPVOID overlapped);
BOOL    WINAPI RemoveDirectoryA(LPCSTR path);
void    WINAPI RtlUnwind(PVOID frame, PVOID target, PEXCEPTION_RECORD er, PVOID ret);
BOOL    WINAPI SetEndOfFile(HANDLE h);
BOOL    WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
BOOL    WINAPI SetFileAttributesA(LPCSTR path, DWORD attrs);
DWORD   WINAPI SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method);
UINT    WINAPI SetHandleCount(UINT n);
BOOL    WINAPI SetStdHandle(DWORD which, HANDLE h);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f);
BOOL    WINAPI TerminateProcess(HANDLE proc, UINT code);
LONG    WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS *ep);
LPVOID  WINAPI VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD prot);
BOOL    WINAPI VirtualFree(LPVOID addr, SIZE_T size, DWORD type);
int32_t WINAPI WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int32_t n, LPSTR out,
                                   int32_t cap, LPCSTR defChar, LPBOOL usedDef);
BOOL    WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPVOID overlapped);

/* ---- registry ---------------------------------------------------------- */

#define HKEY_CLASSES_ROOT   ((HKEY)(ULONG_PTR)0x80000000u)
#define HKEY_CURRENT_USER   ((HKEY)(ULONG_PTR)0x80000001u)
#define HKEY_LOCAL_MACHINE  ((HKEY)(ULONG_PTR)0x80000002u)
#define KEY_ALL_ACCESS      0xF003Fu
#define REG_OPTION_NON_VOLATILE 0
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

LONG WINAPI RegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls,
                            DWORD options, DWORD access,
                            LPSECURITY_ATTRIBUTES attr, PHKEY out,
                            LPDWORD disposition);
LONG WINAPI RegCloseKey(HKEY key);

/* ---- user: messages ---------------------------------------------------- */

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *PMSG, *LPMSG;

#define WM_NULL          0x0000
#define WM_CREATE        0x0001
#define WM_DESTROY       0x0002
#define WM_MOVE          0x0003
#define WM_SIZE          0x0005
#define WM_ACTIVATE      0x0006
#define WM_SETFOCUS      0x0007
#define WM_KILLFOCUS     0x0008
#define WM_PAINT         0x000F
#define WM_CLOSE         0x0010
#define WM_QUIT          0x0012
#define WM_ERASEBKGND    0x0014
#define WM_ACTIVATEAPP   0x001C
#define WM_SETCURSOR     0x0020
#define WM_QUERYNEWPALETTE 0x030F
#define WM_PALETTECHANGED 0x0311
#define WM_KEYDOWN       0x0100
#define WM_KEYUP         0x0101
#define WM_CHAR          0x0102
#define WM_SYSKEYDOWN    0x0104
#define WM_SYSKEYUP      0x0105
#define WM_SYSCOMMAND    0x0112
#define WM_MOUSEMOVE     0x0200
#define WM_LBUTTONDOWN   0x0201
#define WM_LBUTTONUP     0x0202
#define WM_RBUTTONDOWN   0x0204
#define WM_RBUTTONUP     0x0205
#define WM_USER          0x0400

#define WA_INACTIVE      0
#define WA_ACTIVE        1
#define WA_CLICKACTIVE   2
#define SC_SCREENSAVE    0xF140
#define SC_MONITORPOWER  0xF170
#define PM_NOREMOVE      0x0000
#define PM_REMOVE        0x0001
#define PM_NOYIELD       0x0002

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

BOOL    WINAPI PeekMessageA(LPMSG msg, HWND hwnd, UINT lo, UINT hi, UINT remove);
BOOL    WINAPI GetMessageA(LPMSG msg, HWND hwnd, UINT lo, UINT hi);
BOOL    WINAPI TranslateMessage(const MSG *msg);
LRESULT WINAPI DispatchMessageA(const MSG *msg);
BOOL    WINAPI PostMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void    WINAPI PostQuitMessage(int32_t code);
LRESULT WINAPI DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ---- user: windows ----------------------------------------------------- */

typedef struct tagWNDCLASSA {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int32_t   cbClsExtra;
    int32_t   cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
} WNDCLASSA, *LPWNDCLASSA;

typedef struct tagPAINTSTRUCT {
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

#define CS_VREDRAW  0x0001
#define CS_HREDRAW  0x0002
#define CS_DBLCLKS  0x0008
#define CS_OWNDC    0x0020

#define WS_OVERLAPPED   0x00000000u
#define WS_POPUP        0x80000000u
#define WS_VISIBLE      0x10000000u
#define WS_CAPTION      0x00C00000u
#define WS_BORDER       0x00800000u
#define WS_DLGFRAME     0x00400000u
#define WS_SYSMENU      0x00080000u
#define WS_THICKFRAME   0x00040000u
#define WS_MINIMIZEBOX  0x00020000u
#define WS_MAXIMIZEBOX  0x00010000u
#define WS_EX_APPWINDOW 0x00040000u
#define WS_EX_TOPMOST   0x00000008u

#define GWL_STYLE   (-16)
#define GWL_EXSTYLE (-20)

#define SWP_NOSIZE       0x0001
#define SWP_NOMOVE       0x0002
#define SWP_NOZORDER     0x0004
#define SWP_NOREDRAW     0x0008
#define SWP_NOACTIVATE   0x0010
#define SWP_SHOWWINDOW   0x0040
#define HWND_TOP         ((HWND)0)
#define HWND_BOTTOM      ((HWND)1)
#define HWND_TOPMOST     ((HWND)-1)
#define HWND_NOTOPMOST   ((HWND)-2)

#define SW_HIDE       0
#define SW_SHOWNORMAL 1
#define SW_SHOW       5

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SPI_GETWORKAREA 0x0030

#define RDW_INVALIDATE   0x0001
#define RDW_ERASE        0x0004
#define RDW_ALLCHILDREN  0x0080
#define RDW_UPDATENOW    0x0100

#define IDI_APPLICATION MAKEINTRESOURCE(32512)
#define IDC_ARROW       MAKEINTRESOURCE(32512)

#define MB_OK        0x0000
#define MB_ICONHAND  0x0010
#define MB_ICONERROR MB_ICONHAND

#define CW_USEDEFAULT ((int32_t)0x80000000)

ATOM    WINAPI RegisterClassA(const WNDCLASSA *wc);
HWND    WINAPI CreateWindowExA(DWORD exStyle, LPCSTR cls, LPCSTR title,
                               DWORD style, int32_t x, int32_t y, int32_t w,
                               int32_t h, HWND parent, HMENU menu,
                               HINSTANCE inst, LPVOID param);
BOOL    WINAPI DestroyWindow(HWND hwnd);
BOOL    WINAPI ShowWindow(HWND hwnd, int32_t cmd);
BOOL    WINAPI UpdateWindow(HWND hwnd);
BOOL    WINAPI SetWindowPos(HWND hwnd, HWND after, int32_t x, int32_t y,
                            int32_t cx, int32_t cy, UINT flags);
LONG    WINAPI GetWindowLongA(HWND hwnd, int32_t index);
LONG    WINAPI SetWindowLongA(HWND hwnd, int32_t index, LONG value);
BOOL    WINAPI AdjustWindowRectEx(LPRECT rc, DWORD style, BOOL menu,
                                  DWORD exStyle);
BOOL    WINAPI GetClientRect(HWND hwnd, LPRECT rc);
BOOL    WINAPI GetWindowRect(HWND hwnd, LPRECT rc);
BOOL    WINAPI ClientToScreen(HWND hwnd, LPPOINT pt);
BOOL    WINAPI IsIconic(HWND hwnd);
HMENU   WINAPI GetMenu(HWND hwnd);
HWND    WINAPI GetFocus(void);
HWND    WINAPI SetFocus(HWND hwnd);
HWND    WINAPI GetActiveWindow(void);
BOOL    WINAPI RedrawWindow(HWND hwnd, const RECT *rc, HANDLE rgn, UINT flags);
BOOL    WINAPI GetUpdateRect(HWND hwnd, LPRECT rc, BOOL erase);
HDC     WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT ps);
BOOL    WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps);
HICON   WINAPI LoadIconA(HINSTANCE inst, LPCSTR name);
HCURSOR WINAPI LoadCursorA(HINSTANCE inst, LPCSTR name);
HCURSOR WINAPI SetCursor(HCURSOR cur);
int32_t WINAPI ShowCursor(BOOL show);
int32_t WINAPI GetSystemMetrics(int32_t index);
BOOL    WINAPI SystemParametersInfoA(UINT action, UINT param, PVOID out,
                                     UINT wini);
int32_t WINAPI MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type);
BOOL    WINAPI SetRect(LPRECT rc, int32_t l, int32_t t, int32_t r, int32_t b);
BOOL    WINAPI IntersectRect(LPRECT out, const RECT *a, const RECT *b);
int32_t WINAPI wsprintfA(LPSTR out, LPCSTR fmt, ...);
int32_t WINAPI lstrlenA(LPCSTR s);

/* MSVC's stdio spellings, which the harness's C uses. Defined in crt.cpp. */
int32_t _snprintf(char *out, size_t cap, const char *fmt, ...);
int32_t _vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);

/* ---- gdi ------------------------------------------------------------------ */

typedef struct tagPALETTEENTRY {
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagLOGPALETTE {
    WORD         palVersion;
    WORD         palNumEntries;
    PALETTEENTRY palPalEntry[1];
} LOGPALETTE, *LPLOGPALETTE;

typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;

#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1;
    WORD  bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;

#define BI_RGB 0

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | (((WORD)((BYTE)(g))) << 8) | (((DWORD)((BYTE)(b))) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)((c) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))

#define BITSPIXEL 12
#define PLANES    14
#define SIZEPALETTE 104
#define RASTERCAPS 38
#define RC_PALETTE 0x0100

#define TRANSPARENT 1
#define OPAQUE      2

#define FW_DONTCARE 0
#define FW_NORMAL   400
#define FW_BOLD     700
#define ANSI_CHARSET    0
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS  0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define DRAFT_QUALITY   1
#define DEFAULT_PITCH   0
#define FIXED_PITCH     1
#define VARIABLE_PITCH  2
#define FF_DONTCARE     0

#define SYSPAL_ERROR    0
#define SYSPAL_STATIC   1
#define SYSPAL_NOSTATIC 2
#define PC_RESERVED     0x01
#define PC_EXPLICIT     0x02
#define PC_NOCOLLAPSE   0x04

#define CLR_INVALID 0xFFFFFFFFu

HDC      WINAPI GetDC(HWND hwnd);
int32_t  WINAPI ReleaseDC(HWND hwnd, HDC dc);
int32_t  WINAPI GetDeviceCaps(HDC dc, int32_t index);
HGDIOBJ  WINAPI SelectObject(HDC dc, HGDIOBJ obj);
BOOL     WINAPI DeleteObject(HGDIOBJ obj);
HPALETTE WINAPI CreatePalette(const LOGPALETTE *lp);
HPALETTE WINAPI SelectPalette(HDC dc, HPALETTE pal, BOOL forceBackground);
UINT     WINAPI RealizePalette(HDC dc);
UINT     WINAPI SetSystemPaletteUse(HDC dc, UINT use);
UINT     WINAPI GetSystemPaletteEntries(HDC dc, UINT start, UINT count,
                                        LPPALETTEENTRY out);
COLORREF WINAPI GetPixel(HDC dc, int32_t x, int32_t y);
HFONT    WINAPI CreateFontA(int32_t height, int32_t width, int32_t escapement,
                            int32_t orientation, int32_t weight, DWORD italic,
                            DWORD underline, DWORD strikeout, DWORD charset,
                            DWORD outPrecision, DWORD clipPrecision,
                            DWORD quality, DWORD pitchAndFamily, LPCSTR face);
int32_t  WINAPI SetBkMode(HDC dc, int32_t mode);
COLORREF WINAPI SetTextColor(HDC dc, COLORREF colour);
BOOL     WINAPI TextOutA(HDC dc, int32_t x, int32_t y, LPCSTR text, int32_t n);
BOOL     WINAPI GetTextExtentPoint32A(HDC dc, LPCSTR text, int32_t n, LPSIZE out);

AM2_EXTERN_C_END

#include <mmsystem.h>

#endif /* AM2_PLATFORM_WINDOWS_H */
