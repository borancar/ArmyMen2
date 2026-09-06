/* dir.cpp -- the CRT's directory calls, from the bodies at 0x00465BA3
 * (_findfirst) through 0x00466496 (_rmdir).
 *
 * Each is a thin translation of one kernel32 call: the Win32 record into
 * the CRT's, the Win32 error into errno through _dosmaperr or, for the
 * find family, a short table of its own. The one that does more is
 * _chdir, which keeps Win32's per-drive current directory in the =X:
 * environment variable the way the original does -- through
 * _mbctoupper, whose table only the original's startup fills.
 */
#include "crt.h"
#include "../../inject/win32.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#define crt_errno    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRNO))
#define crt_doserrno (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_DOSERRNO))

#define CRT_ENOENT  2
#define CRT_ENOMEM  12
#define CRT_ERANGE  34

/* FindFirstFileA's failures, by the find family's own rule rather than
 * _dosmaperr: not found and no more files are ENOENT, out of memory is
 * ENOMEM, everything else EINVAL. */
static void find_error(void)
{
    DWORD err = GetLastError();

    if (err >= 2 && err <= 3)
        crt_errno = CRT_ENOENT;
    else if (err == ERROR_NOT_ENOUGH_MEMORY)
        crt_errno = CRT_ENOMEM;
    else if (err == ERROR_NO_MORE_FILES)
        crt_errno = CRT_ENOENT;
    else
        crt_errno = CRT_EINVAL;
}

static void find_fill(CRT_FINDDATA *out, const WIN32_FIND_DATAA *in)
{
    out->attrib = in->dwFileAttributes == FILE_ATTRIBUTE_NORMAL ? 0 : in->dwFileAttributes;
    out->time_create = crt_timet_from_ft(&in->ftCreationTime);
    out->time_access = crt_timet_from_ft(&in->ftLastAccessTime);
    out->time_write = crt_timet_from_ft(&in->ftLastWriteTime);
    out->size = in->nFileSizeLow;
    crt_strcpy(out->name, in->cFileName);
}

int32_t __cdecl crt_findfirst(const char *spec, CRT_FINDDATA *out)
{
    WIN32_FIND_DATAA wfd;
    HANDLE           h = FindFirstFileA(spec, &wfd);

    if (h == INVALID_HANDLE_VALUE) {
        find_error();
        return -1;
    }
    find_fill(out, &wfd);
    return (int32_t)(intptr_t)h;
}

int32_t __cdecl crt_findnext(int32_t handle, CRT_FINDDATA *out)
{
    WIN32_FIND_DATAA wfd;

    if (!FindNextFileA((HANDLE)(intptr_t)handle, &wfd)) {
        find_error();
        return -1;
    }
    find_fill(out, &wfd);
    return 0;
}

int32_t __cdecl crt_findclose(int32_t handle)
{
    if (!FindClose((HANDLE)(intptr_t)handle)) {
        crt_errno = CRT_EINVAL;
        return -1;
    }
    return 0;
}

/* The shape mkdir, rmdir and remove share: one call, and on failure the
 * error through _dosmaperr. */
static int32_t one_call(BOOL ok)
{
    DWORD err = ok ? 0 : GetLastError();

    if (err) {
        crt_dosmaperr(err);
        return -1;
    }
    return 0;
}

int32_t __cdecl crt_mkdir(const char *path)
{
    return one_call(CreateDirectoryA(path, NULL));
}

int32_t __cdecl crt_rmdir(const char *path)
{
    return one_call(RemoveDirectoryA(path));
}

int32_t __cdecl crt_remove(const char *path)
{
    return one_call(DeleteFileA(path));
}

int32_t __cdecl crt_chmod(const char *path, int32_t pmode)
{
    DWORD attrs = GetFileAttributesA(path);

    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (pmode & 0x80)   /* _S_IWRITE */
            attrs &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
        else
            attrs |= FILE_ATTRIBUTE_READONLY;
        if (SetFileAttributesA(path, attrs))
            return 0;
    }
    crt_dosmaperr(GetLastError());
    return -1;
}

int32_t __cdecl crt_validdrive(int32_t drive)
{
    char root[4];
    UINT type;

    if (drive == 0)
        return 1;
    root[0] = (char)('@' + drive);
    root[1] = ':';
    root[2] = '\\';
    root[3] = 0;
    type = GetDriveTypeA(root);
    return type != DRIVE_UNKNOWN && type != DRIVE_NO_ROOT_DIR;
}

char *__cdecl crt_getdcwd(int32_t drive, char *buf, int32_t max)
{
    char    tmp[0x104];
    DWORD   len;

    if (drive != 0) {
        char  spec[4];
        char *part;

        if (!crt_validdrive(drive)) {
            crt_doserrno = ERROR_INVALID_DRIVE;
            crt_errno = CRT_EACCES;
            return NULL;
        }
        spec[0] = (char)('@' + drive);
        spec[1] = ':';
        spec[2] = '.';
        spec[3] = 0;
        len = GetFullPathNameA(spec, 0x104, tmp, &part);
    } else {
        len = GetCurrentDirectoryA(0x104, tmp);
    }
    if (len == 0)
        return NULL;
    len++;
    if (len > 0x104)
        return NULL;
    if (buf == NULL) {
        buf = (char *)crt_malloc((int32_t)len > max ? len : (uint32_t)max);
        if (!buf) {
            crt_errno = CRT_ENOMEM;
            return NULL;
        }
    } else if ((int32_t)len > max) {
        crt_errno = CRT_ERANGE;
        return NULL;
    }
    crt_strcpy(buf, tmp);
    return buf;
}

char *__cdecl crt_getcwd(char *buf, int32_t max)
{
    return crt_getdcwd(0, buf, max);
}

int32_t __cdecl crt_chdir(const char *path)
{
    char cur[0x105];
    char var[4];

    if (!SetCurrentDirectoryA(path))
        goto fail;
    if (!GetCurrentDirectoryA(0x105, cur))
        goto fail;
    /* A UNC path has no drive to record. */
    if ((cur[0] == '\\' || cur[0] == '/') && cur[1] == cur[0])
        return 0;
    var[0] = '=';
    var[1] = (char)crt_mbctoupper((uint8_t)cur[0]);
    var[2] = ':';
    var[3] = 0;
    if (!SetEnvironmentVariableA(var, cur))
        goto fail;
    return 0;

fail:
    crt_dosmaperr(GetLastError());
    return -1;
}
