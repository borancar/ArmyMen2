/* crt.cpp -- MSVC's CRT names, and the translation of the game's Windows
 * paths into the files that exist.
 *
 * The game reads its install directory with getcwd and then concatenates
 * with backslashes -- "<dir>\data\bootcamp" -- and its data on disk keeps
 * whatever case the installer left, which Wine hid. Every path-taking call
 * here goes through am2_native_path, which turns separators and then
 * resolves each component case-insensitively where the exact name is not
 * there. A file that does not exist under any case fails as it would have.
 */
#include <direct.h>
#include <io.h>
#include "platform.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ---- path translation ------------------------------------------------------------ */

/* Resolve one component of `path` (everything up to `end`) against the
 * directory it is in, replacing it with the entry whose name matches
 * case-insensitively when the exact name does not exist. */
static void am2_resolve_component(char *path, char *start, char *end)
{
    char           saved = *end;
    struct stat    st;
    DIR           *dir;
    struct dirent *de;
    char           parent[AM2_NATIVE_PATH_MAX];
    size_t         plen;

    *end = 0;
    if (stat(path, &st) == 0) {
        *end = saved;
        return;
    }
    plen = (size_t)(start - path);
    if (plen == 0) {
        strcpy(parent, ".");
    } else {
        memcpy(parent, path, plen);
        parent[plen] = 0;
        if (plen > 1 && parent[plen - 1] == '/')
            parent[plen - 1] = 0;
    }
    dir = opendir(parent);
    if (dir) {
        while ((de = readdir(dir)) != NULL) {
            if (!strcasecmp(de->d_name, start) && strlen(de->d_name) == strlen(start)) {
                memcpy(start, de->d_name, strlen(start));
                break;
            }
        }
        closedir(dir);
    }
    *end = saved;
}

char *am2_native_path(const char *win, char *out)
{
    const char *src = win;
    char       *dst = out;
    char       *p;

    /* A drive letter names a root we do not have: an AM2_DRIVE_<X>
     * environment variable can map it, and otherwise it is dropped so
     * "C:\foo" becomes "/foo". The game's own directory arrives as a
     * native path from getcwd, so this only matters for a path the user
     * typed. */
    if (isalpha((unsigned char)src[0]) && src[1] == ':') {
        char        var[] = "AM2_DRIVE_X";
        const char *map;
        var[10] = (char)toupper((unsigned char)src[0]);
        map = getenv(var);
        src += 2;
        if (map && *map) {
            size_t n = strlen(map);
            if (n >= AM2_NATIVE_PATH_MAX - 1)
                n = AM2_NATIVE_PATH_MAX - 1;
            memcpy(dst, map, n);
            dst += n;
            if (*src == '\\' || *src == '/')
                src++;
            if (dst > out && dst[-1] != '/')
                *dst++ = '/';
        }
    }
    while (*src && dst - out < AM2_NATIVE_PATH_MAX - 1) {
        char c = *src++;
        if (c == '\\')
            c = '/';
        /* Collapse doubled separators, which "dir\" + "\file" produces. */
        if (c == '/' && dst > out && dst[-1] == '/')
            continue;
        *dst++ = c;
    }
    *dst = 0;

    /* Resolve case component by component, only past the point where the
     * exact path stops existing. */
    p = out;
    if (*p == '/')
        p++;
    while (*p) {
        char *end = p;
        while (*end && *end != '/')
            end++;
        if (end > p && !(end - p == 1 && p[0] == '.') &&
            !(end - p == 2 && p[0] == '.' && p[1] == '.'))
            am2_resolve_component(out, p, end);
        if (!*end)
            break;
        p = end + 1;
    }
    return out;
}

/* ---- file calls ------------------------------------------------------------------- */

/* MSVC's text mode turns CR LF into LF on the way in, and the game's data
 * files are CR LF: mpmaps.txt read through glibc's fopen yields a "\r"
 * token on every blank line and the level list fails to parse. A text-mode
 * read here loads the file, drops the CRs, and serves it from memory, so
 * ftell and fseek stay consistent with what fgets returned; the offsets
 * are not the file's, but nothing the game does with them leaves the
 * stream. Writes stay plain: an LF-only file reads back the same way. */
static FILE *am2_fopen_text(const char *native)
{
    FILE   *fh = fopen(native, "rb");
    long    n;
    char   *buf, *dst;
    size_t  got, i;

    if (!fh)
        return NULL;
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (n < 0) {
        fclose(fh);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(fh);
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, fh);
    fclose(fh);
    dst = buf;
    for (i = 0; i < got; i++)
        if (!(buf[i] == '\r' && i + 1 < got && buf[i + 1] == '\n'))
            *dst++ = buf[i];
    /* fmemopen with a zero size is an error; an empty file is a stream that
     * ends at once, which needs a byte it never hands out. */
    if (dst == buf) {
        free(buf);
        return fmemopen((void *)"", 1, "r");
    }
    /* The buffer is leaked deliberately: fmemopen does not own it and the
     * game closes these through fclose, which cannot free it. Text files
     * are read a handful of times per session. */
    return fmemopen(buf, (size_t)(dst - buf), "r");
}

FILE *am2_fopen(const char *path, const char *mode)
{
    char   native[AM2_NATIVE_PATH_MAX];
    char   m[8];
    size_t i, n = 0;
    int32_t binary = 0, reading = 0, writing = 0;

    /* MSVC accepts a 't' for text mode; glibc does not want it. */
    for (i = 0; mode[i] && n < sizeof m - 1; i++) {
        if (mode[i] == 't')
            continue;
        if (mode[i] == 'b')
            binary = 1;
        if (mode[i] == 'r')
            reading = 1;
        if (mode[i] == 'w' || mode[i] == 'a' || mode[i] == '+')
            writing = 1;
        m[n++] = mode[i];
    }
    m[n] = 0;
    am2_native_path(path, native);
    if (reading && !writing && !binary)
        return am2_fopen_text(native);
    return fopen(native, m);
}

int32_t am2_remove(const char *path)
{
    char native[AM2_NATIVE_PATH_MAX];
    return remove(am2_native_path(path, native));
}

int32_t _chdir(const char *path)
{
    char native[AM2_NATIVE_PATH_MAX];
    return chdir(am2_native_path(path, native));
}

char *_getcwd(char *out, int32_t cap)
{
    return getcwd(out, (size_t)cap);
}

int32_t _mkdir(const char *path)
{
    char native[AM2_NATIVE_PATH_MAX];
    return mkdir(am2_native_path(path, native), 0777);
}

int32_t _rmdir(const char *path)
{
    char native[AM2_NATIVE_PATH_MAX];
    return rmdir(am2_native_path(path, native));
}

int32_t _chmod(const char *path, int32_t mode)
{
    char native[AM2_NATIVE_PATH_MAX];
    /* MSVC's _S_IWRITE is 0x80; anything else leaves the file read-only. */
    return chmod(am2_native_path(path, native), (mode & 0x80) ? 0644 : 0444);
}

int32_t _access(const char *path, int32_t mode)
{
    char native[AM2_NATIVE_PATH_MAX];
    return access(am2_native_path(path, native), mode);
}

/* ---- strings ------------------------------------------------------------------------ */

int32_t _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
int32_t _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }

/* Writes only the characters it changes, as MSVC's does. That is not a
 * nicety: map.cpp lowercases the literal "camera", which the original kept
 * in writable .data and this build keeps in .rodata, and an unconditional
 * store there faulted on the first map load. */
char *_strlwr(char *s)
{
    char *p;
    for (p = s; *p; p++) {
        int c = (unsigned char)*p;
        if (isupper(c))
            *p = (char)tolower(c);
    }
    return s;
}

int32_t _snprintf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int32_t n;

    va_start(ap, fmt);
    n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    /* MSVC answers -1 when the output was cut, and leaves it unterminated;
     * the callers here test only for failure. */
    return (size_t)n >= cap ? -1 : n;
}

int32_t _vsnprintf(char *out, size_t cap, const char *fmt, va_list ap)
{
    int32_t n = vsnprintf(out, cap, fmt, ap);
    return (size_t)n >= cap ? -1 : n;
}

/* ---- the find-file family --------------------------------------------------------- */

/* A handle is the directory listing taken at _findfirst, filtered by the
 * pattern as the caller walks it. MSVC's includes "." and ".." for a
 * pattern that admits them, and so does this. */
typedef struct AM2_Find {
    char  **names;
    int32_t count;
    int32_t next;
    char    dir[AM2_NATIVE_PATH_MAX];
} AM2_Find;

static int32_t am2_find_fill(AM2_Find *f, struct _finddata_t *out)
{
    while (f->next < f->count) {
        const char *name = f->names[f->next++];
        char        path[AM2_NATIVE_PATH_MAX * 2];
        struct stat st;

        snprintf(path, sizeof path, "%s/%s", f->dir, name);
        memset(out, 0, sizeof *out);
        if (stat(path, &st) == 0) {
            out->attrib = S_ISDIR(st.st_mode) ? _A_SUBDIR : _A_NORMAL;
            if (!(st.st_mode & S_IWUSR))
                out->attrib |= _A_RDONLY;
            out->size = (uint32_t)st.st_size;
            out->time_create = (int32_t)st.st_ctime;
            out->time_access = (int32_t)st.st_atime;
            out->time_write = (int32_t)st.st_mtime;
        }
        strncpy(out->name, name, sizeof out->name - 1);
        return 0;
    }
    errno = ENOENT;
    return -1;
}

static int am2_name_compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

intptr_t _findfirst(const char *spec, struct _finddata_t *out)
{
    char           native[AM2_NATIVE_PATH_MAX];
    char          *slash, *pattern;
    AM2_Find      *f;
    DIR           *dir;
    struct dirent *de;
    int32_t        cap = 0;

    am2_native_path(spec, native);
    slash = strrchr(native, '/');
    if (slash) {
        *slash = 0;
        pattern = slash + 1;
    } else {
        pattern = native;
    }
    f = (AM2_Find *)calloc(1, sizeof *f);
    if (!f)
        return -1;
    strcpy(f->dir, slash ? (native[0] ? native : "/") : ".");
    dir = opendir(f->dir);
    if (!dir) {
        free(f);
        errno = ENOENT;
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        if (fnmatch(pattern, de->d_name, FNM_CASEFOLD) != 0)
            continue;
        if (f->count == cap) {
            cap = cap ? cap * 2 : 32;
            f->names = (char **)realloc(f->names, (size_t)cap * sizeof *f->names);
        }
        f->names[f->count++] = strdup(de->d_name);
    }
    closedir(dir);
    if (f->count > 1)
        qsort(f->names, (size_t)f->count, sizeof *f->names, am2_name_compare);
    if (am2_find_fill(f, out) != 0) {
        _findclose((intptr_t)f);
        return -1;
    }
    return (intptr_t)f;
}

int32_t _findnext(intptr_t handle, struct _finddata_t *out)
{
    AM2_Find *f = (AM2_Find *)handle;
    if (!f)
        return -1;
    return am2_find_fill(f, out);
}

int32_t _findclose(intptr_t handle)
{
    AM2_Find *f = (AM2_Find *)handle;
    int32_t   i;

    if (!f)
        return -1;
    for (i = 0; i < f->count; i++)
        free(f->names[i]);
    free(f->names);
    free(f);
    return 0;
}
