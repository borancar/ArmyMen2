/* lowio.cpp -- the CRT's file descriptors: the __pioinfo handle table and
 * the primitives over it, from the bodies at 0x00466BC8..0x0046D113.
 *
 * An fd indexes 64 blocks of 32 eight-byte ioinfo records {osfhnd, osfile,
 * pipech}, block fd>>5, entry fd&31; _nhandle is 32 per block allocated.
 * Everything below reaches the OS through CreateFileA, ReadFile, WriteFile,
 * SetFilePointer, CloseHandle, FlushFileBuffers and SetEndOfFile, which the
 * native build supplies from src/platform/kernel32crt.cpp and the Wine
 * standalone takes from kernel32 -- so the hybrid, which runs the ORIGINAL
 * of every function here over the same platform, is the oracle for them.
 *
 * Text mode lives here, not in stdio: _read folds CR LF to LF and stops at
 * Ctrl-Z, _write expands LF to CR LF, and ftell's accounting reads the
 * FTEXT and FCRLF bits this module keeps.
 */
#include "crt.h"
#include "../../inject/win32.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The OS-error -> errno map at 0x0048D140 (45 {oserr, errno} pairs) that
 * crt_dosmaperr scans, ending at ADDR_CRT_ERRTABLE_END. Pure const data, so
 * the blob copy drops -- placed at its VA and byte-verified. ERRTABLE_END is
 * the loop bound, one past the array. */
/* The __badioinfo sentinel at 0x0048CC90 ({osfhnd=-1, osfile=0, pipech=0x0A,
 * pad}) that _pioinfo returns for a bad fd. A fixed CRT_IOINFO, byte-checked. */
extern "C" const uint8_t am2_crt_badioinfo[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x0A, 0x00, 0x00 };
extern "C" const uint32_t am2_crt_errtable[90] = {
    0x00000001u, 22u, 0x00000002u, 2u,  0x00000003u, 2u,  0x00000004u, 24u,
    0x00000005u, 13u, 0x00000006u, 9u,  0x00000007u, 12u, 0x00000008u, 12u,
    0x00000009u, 12u, 0x0000000Au, 7u,  0x0000000Bu, 8u,  0x0000000Cu, 22u,
    0x0000000Du, 22u, 0x0000000Fu, 2u,  0x00000010u, 13u, 0x00000011u, 18u,
    0x00000012u, 2u,  0x00000021u, 13u, 0x00000035u, 2u,  0x00000041u, 13u,
    0x00000043u, 2u,  0x00000050u, 17u, 0x00000052u, 13u, 0x00000053u, 13u,
    0x00000057u, 22u, 0x00000059u, 11u, 0x0000006Cu, 13u, 0x0000006Du, 32u,
    0x00000070u, 28u, 0x00000072u, 9u,  0x00000006u, 22u, 0x00000080u, 10u,
    0x00000081u, 10u, 0x00000082u, 9u,  0x00000083u, 22u, 0x00000084u, 13u,
    0x00000091u, 41u, 0x0000009Eu, 13u, 0x000000A1u, 2u,  0x000000A4u, 11u,
    0x000000A7u, 13u, 0x000000B7u, 17u, 0x000000CEu, 2u,  0x000000D7u, 11u,
    0x00000718u, 12u,
};
#endif

#define crt_nhandle     (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_NHANDLE))
#define crt_pioinfo_tab ((CRT_IOINFO **)(uintptr_t)AM2_IMAGE(ADDR_CRT_PIOINFO))
#define crt_errno       (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRNO))
#define crt_doserrno    (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_DOSERRNO))
#define crt_fmode       (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_FMODE))
#define crt_umaskval    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_UMASKVAL))
#define crt_app_type    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_APP_TYPE))
#define crt_badioinfo   ((CRT_IOINFO *)(uintptr_t)AM2_IMAGE(ADDR_CRT_BADIOINFO))

#define CRT_IOINFO_ARRAYS   64
#define CRT_IOINFO_ARRAY_ELTS 32
#define CRT_IOINFO_BLOCK_BYTES 0x100

CRT_IOINFO *crt_pioinfo(int32_t fd)
{
    return crt_pioinfo_tab[fd >> 5] + (fd & 0x1F);
}

/* Every entry point compares the fd against _nhandle UNSIGNED, so a
 * negative one fails the same way as a large one. */
static int32_t fd_in_table(int32_t fd)
{
    return (uint32_t)fd < (uint32_t)crt_nhandle;
}

static int32_t fd_is_open(int32_t fd)
{
    return fd_in_table(fd) && (crt_pioinfo(fd)->osfile & CRT_FOPEN);
}

static void bad_fd(void)
{
    crt_doserrno = 0;
    crt_errno = CRT_EBADF;
}

static void init_block(CRT_IOINFO *b)
{
    CRT_IOINFO *end = b + CRT_IOINFO_ARRAY_ELTS;

    for (; b < end; b++) {
        b->osfile = 0;
        b->osfhnd = -1;
        b->pipech = 0x0A;
    }
}

/* The original's startup runs _ioinit before anything opens a file; the
 * standalone and native builds have no such startup, so the first request
 * for a descriptor runs it. The hybrid runs the original's. */
static void ensure_ioinit(void)
{
    if (crt_pioinfo_tab[0] == NULL)
        crt_ioinit();
}

void __cdecl crt_ioinit(void)
{
    STARTUPINFOA si;
    CRT_IOINFO  *b;
    int32_t      fd, blk, n;

    b = (CRT_IOINFO *)crt_malloc(CRT_IOINFO_BLOCK_BYTES);
    if (!b)
        crt_amsg_exit(0x1B);
    crt_pioinfo_tab[0] = b;
    crt_nhandle = CRT_IOINFO_ARRAY_ELTS;
    init_block(b);

    /* Descriptors the parent passed through the reserved block: a count,
     * that many osfile bytes, then that many handles. */
    GetStartupInfoA(&si);
    if (si.cbReserved2 != 0 && si.lpReserved2 != NULL) {
        const uint8_t  *osfile = (const uint8_t *)si.lpReserved2 + 4;
        const intptr_t *osfhnd;

        n = *(const int32_t *)si.lpReserved2;
        osfhnd = (const intptr_t *)(osfile + n);
        if (n > 0x800)
            n = 0x800;
        for (blk = 1; crt_nhandle < n; blk++) {
            b = (CRT_IOINFO *)crt_malloc(CRT_IOINFO_BLOCK_BYTES);
            if (!b) {
                n = crt_nhandle;
                break;
            }
            crt_nhandle += CRT_IOINFO_ARRAY_ELTS;
            crt_pioinfo_tab[blk] = b;
            init_block(b);
        }
        for (fd = 0; fd < n; fd++) {
            if (osfhnd[fd] == -1 || !(osfile[fd] & CRT_FOPEN))
                continue;
            if (!(osfile[fd] & CRT_FPIPE) && GetFileType((HANDLE)osfhnd[fd]) == 0)
                continue;
            b = crt_pioinfo(fd);
            b->osfhnd = osfhnd[fd];
            b->osfile = osfile[fd];
        }
    }

    for (fd = 0; fd < 3; fd++) {
        HANDLE h;
        DWORD  type;

        b = crt_pioinfo(fd);
        if (b->osfhnd != -1) {
            b->osfile |= CRT_FTEXT;
            continue;
        }
        b->osfile = CRT_FOPEN | CRT_FTEXT;
        h = GetStdHandle(fd == 0 ? STD_INPUT_HANDLE
                       : fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        if (h == INVALID_HANDLE_VALUE) {
            b->osfile |= CRT_FDEV;
            continue;
        }
        type = GetFileType(h);
        if (type == 0) {
            b->osfile |= CRT_FDEV;
            continue;
        }
        b->osfhnd = (intptr_t)h;
        type &= 0xFF;
        if (type == FILE_TYPE_CHAR)
            b->osfile |= CRT_FDEV;
        else if (type == FILE_TYPE_PIPE)
            b->osfile |= CRT_FPIPE;
    }
    SetHandleCount((UINT)crt_nhandle);
}

int32_t __cdecl crt_alloc_osfhnd(void)
{
    int32_t blk, i;

    ensure_ioinit();
    for (blk = 0; blk < CRT_IOINFO_ARRAYS; blk++) {
        CRT_IOINFO *b = crt_pioinfo_tab[blk];

        if (b == NULL) {
            b = (CRT_IOINFO *)crt_malloc(CRT_IOINFO_BLOCK_BYTES);
            if (!b)
                return -1;
            crt_nhandle += CRT_IOINFO_ARRAY_ELTS;
            crt_pioinfo_tab[blk] = b;
            init_block(b);
            return blk << 5;
        }
        for (i = 0; i < CRT_IOINFO_ARRAY_ELTS; i++) {
            if (!(b[i].osfile & CRT_FOPEN)) {
                b[i].osfhnd = -1;
                return (blk << 5) + i;
            }
        }
    }
    return -1;
}

/* A console application (__app_type 1) also tells the OS about its three
 * standard handles; this image is a GUI one, so both arms are cold. */
static void std_handle(int32_t fd, HANDLE h)
{
    if (crt_app_type != 1)
        return;
    if (fd == 0)
        SetStdHandle(STD_INPUT_HANDLE, h);
    else if (fd == 1)
        SetStdHandle(STD_OUTPUT_HANDLE, h);
    else if (fd == 2)
        SetStdHandle(STD_ERROR_HANDLE, h);
}

int32_t __cdecl crt_set_osfhnd(int32_t fd, intptr_t h)
{
    if (fd_in_table(fd) && crt_pioinfo(fd)->osfhnd == -1) {
        std_handle(fd, (HANDLE)h);
        crt_pioinfo(fd)->osfhnd = h;
        return 0;
    }
    bad_fd();
    return -1;
}

int32_t __cdecl crt_free_osfhnd(int32_t fd)
{
    if (fd_is_open(fd) && crt_pioinfo(fd)->osfhnd != -1) {
        std_handle(fd, NULL);
        crt_pioinfo(fd)->osfhnd = -1;
        return 0;
    }
    bad_fd();
    return -1;
}

intptr_t __cdecl crt_get_osfhandle(int32_t fd)
{
    if (fd_is_open(fd))
        return crt_pioinfo(fd)->osfhnd;
    bad_fd();
    return -1;
}

void __cdecl crt_dosmaperr(uint32_t oserr)
{
    const uint32_t *t   = (const uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRTABLE);
    const uint32_t *end = (const uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRTABLE_END);

    crt_doserrno = oserr;
    for (; t < end; t += 2) {
        if (t[0] == oserr) {
            crt_errno = (int32_t)t[1];
            return;
        }
    }
    if (oserr >= 0x13 && oserr <= 0x24)
        crt_errno = CRT_EACCES;
    else if (oserr >= 0xBC && oserr <= 0xCA)
        crt_errno = CRT_ENOEXEC;
    else
        crt_errno = CRT_EINVAL;
}

int32_t __cdecl crt_isatty(int32_t fd)
{
    if (!fd_in_table(fd))
        return 0;
    return crt_pioinfo(fd)->osfile & CRT_FDEV;
}

int32_t __cdecl crt_setmode(int32_t fd, int32_t mode)
{
    CRT_IOINFO *p;
    int32_t     old;

    if (!fd_is_open(fd)) {
        crt_errno = CRT_EBADF;
        return -1;
    }
    p = crt_pioinfo(fd);
    old = p->osfile & CRT_FTEXT;
    if (mode == CRT_O_BINARY)
        p->osfile &= (uint8_t)~CRT_FTEXT;
    else if (mode == CRT_O_TEXT)
        p->osfile |= CRT_FTEXT;
    else {
        crt_errno = CRT_EINVAL;
        return -1;
    }
    return old ? CRT_O_TEXT : CRT_O_BINARY;
}

int32_t __cdecl crt_lseek(int32_t fd, int32_t off, int32_t whence)
{
    intptr_t h;
    DWORD    r, err;

    if (!fd_is_open(fd)) {
        bad_fd();
        return -1;
    }
    h = crt_get_osfhandle(fd);
    if (h == -1) {
        crt_errno = CRT_EBADF;
        return -1;
    }
    r = SetFilePointer((HANDLE)h, off, NULL, (DWORD)whence);
    err = r == INVALID_SET_FILE_POINTER ? GetLastError() : 0;
    if (err) {
        crt_dosmaperr(err);
        return -1;
    }
    crt_pioinfo(fd)->osfile &= (uint8_t)~CRT_FEOFLAG;
    return (int32_t)r;
}

int32_t __cdecl crt_close(int32_t fd)
{
    DWORD err = 0;

    if (!fd_is_open(fd)) {
        bad_fd();
        return -1;
    }
    if (crt_get_osfhandle(fd) == -1)
        err = 0;
    else if ((fd == 1 || fd == 2)
             && crt_get_osfhandle(2) == crt_get_osfhandle(1))
        err = 0;   /* stdout and stderr on one handle: closed once */
    else if (!CloseHandle((HANDLE)crt_get_osfhandle(fd)))
        err = GetLastError();
    crt_free_osfhnd(fd);
    crt_pioinfo(fd)->osfile = 0;
    if (err) {
        crt_dosmaperr(err);
        return -1;
    }
    return 0;
}

int32_t __cdecl crt_commit(int32_t fd)
{
    DWORD err;

    if (!fd_is_open(fd)) {
        crt_errno = CRT_EBADF;
        return -1;
    }
    err = FlushFileBuffers((HANDLE)crt_get_osfhandle(fd)) ? 0 : GetLastError();
    if (err) {
        crt_doserrno = err;
        crt_errno = CRT_EBADF;
        return -1;
    }
    return 0;
}

int32_t __cdecl crt_read(int32_t fd, void *buf, uint32_t cnt)
{
    CRT_IOINFO *p;
    uint8_t    *start = (uint8_t *)buf, *q, *pp, *end;
    DWORD       got = 0, err;
    int32_t     bytes_read = 0;
    char        peek;

    if (!fd_is_open(fd)) {
        bad_fd();
        return -1;
    }
    p = crt_pioinfo(fd);
    if (cnt == 0)
        return 0;
    if (p->osfile & CRT_FEOFLAG)
        return 0;

    /* A device or pipe may hold one byte read ahead by a previous call's
     * CR-at-the-end peek; it goes first. */
    q = start;
    if ((p->osfile & (CRT_FDEV | CRT_FPIPE)) && p->pipech != 0x0A) {
        cnt--;
        *q++ = p->pipech;
        bytes_read = 1;
        p->pipech = 0x0A;
    }
    if (!ReadFile((HANDLE)p->osfhnd, q, cnt, &got, NULL)) {
        err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            crt_errno = CRT_EBADF;
            crt_doserrno = ERROR_ACCESS_DENIED;
            return -1;
        }
        if (err == ERROR_BROKEN_PIPE)
            return 0;
        crt_dosmaperr(err);
        return -1;
    }
    bytes_read += (int32_t)got;
    if (!(p->osfile & CRT_FTEXT))
        return bytes_read;

    /* Text mode. FCRLF records that this read began on an LF, which is
     * what ftell needs to know about a CR LF split across two buffers. */
    if (got != 0 && start[0] == '\n')
        p->osfile |= CRT_FCRLF;
    else
        p->osfile &= (uint8_t)~CRT_FCRLF;

    pp = start;
    end = start + bytes_read;
    q = start;
    while (pp < end) {
        uint8_t ch = *pp;

        if (ch == 0x1A) {
            if (!(p->osfile & CRT_FDEV))
                p->osfile |= CRT_FEOFLAG;
            break;
        }
        if (ch != '\r') {
            *q++ = ch;
            pp++;
            continue;
        }
        if (pp < end - 1) {
            if (pp[1] == '\n') {
                pp += 2;
                *q++ = '\n';
            } else {
                *q++ = '\r';
                pp++;
            }
            continue;
        }
        /* A CR as the last byte read: peek one more. */
        pp++;
        if (!ReadFile((HANDLE)p->osfhnd, &peek, 1, &got, NULL) && GetLastError() != 0) {
            *q++ = '\r';
            continue;
        }
        if (got == 0) {
            *q++ = '\r';
            continue;
        }
        if (p->osfile & (CRT_FDEV | CRT_FPIPE)) {
            if (peek == '\n') {
                *q++ = '\n';
            } else {
                *q++ = '\r';
                p->pipech = (uint8_t)peek;
            }
        } else if (q == start && peek == '\n') {
            *q++ = '\n';
        } else {
            crt_lseek(fd, -1, 1);
            if (peek != '\n')
                *q++ = '\r';
        }
    }
    return (int32_t)(q - start);
}

int32_t __cdecl crt_write(int32_t fd, const void *buf, uint32_t cnt)
{
    CRT_IOINFO    *p;
    const uint8_t *start = (const uint8_t *)buf;
    DWORD          written = 0, dosretval = 0;
    int32_t        charcount = 0, lfcount = 0;

    if (!fd_is_open(fd)) {
        bad_fd();
        return -1;
    }
    p = crt_pioinfo(fd);
    if (cnt == 0)
        return 0;
    if (p->osfile & CRT_FAPPEND)
        crt_lseek(fd, 0, 2);

    if (p->osfile & CRT_FTEXT) {
        /* LF becomes CR LF through a frame buffer of 0x400 bytes. The loop
         * tests the fill AFTER storing, so a LF landing at 0x3FF puts its
         * pair at 0x400 -- one byte past the buffer, into the frame's
         * unused padding in the original; the four spare bytes keep that
         * legal here. */
        const uint8_t *pp = start;
        uint8_t        lfbuf[0x404];

        do {
            uint8_t *q = lfbuf;
            int32_t  n;

            while ((uint32_t)(pp - start) < cnt) {
                uint8_t ch = *pp++;

                if (ch == '\n') {
                    lfcount++;
                    *q++ = '\r';
                }
                *q++ = ch;
                if ((int32_t)(q - lfbuf) >= 0x400)
                    break;
            }
            n = (int32_t)(q - lfbuf);
            if (!WriteFile((HANDLE)p->osfhnd, lfbuf, (DWORD)n, &written, NULL)) {
                dosretval = GetLastError();
                break;
            }
            charcount += (int32_t)written;
            if ((int32_t)written < n)
                break;
        } while ((uint32_t)(pp - start) < cnt);
    } else {
        if (WriteFile((HANDLE)p->osfhnd, buf, cnt, &written, NULL)) {
            dosretval = 0;
            charcount = (int32_t)written;
        } else {
            dosretval = GetLastError();
        }
    }

    if (charcount != 0)
        return charcount - lfcount;
    if (dosretval == 0) {
        /* Nothing written and no error: a Ctrl-Z to a device is fine,
         * anything else is a full disk. */
        if ((p->osfile & CRT_FDEV) && start[0] == 0x1A)
            return 0;
        crt_errno = CRT_ENOSPC;
        crt_doserrno = 0;
        return -1;
    }
    if (dosretval == ERROR_ACCESS_DENIED) {
        crt_errno = CRT_EBADF;
        crt_doserrno = ERROR_ACCESS_DENIED;
        return -1;
    }
    crt_dosmaperr(dosretval);
    return -1;
}

int32_t __cdecl crt_chsize(int32_t fd, int32_t size)
{
    int32_t cur, end, extend, retval = 0;

    if (!fd_is_open(fd)) {
        crt_errno = CRT_EBADF;
        return -1;
    }
    cur = crt_lseek(fd, 0, 1);
    if (cur == -1)
        return -1;
    end = crt_lseek(fd, 0, 2);
    if (end == -1)
        return -1;
    extend = size - end;
    if (extend > 0) {
        uint8_t blank[0x1000];
        int32_t oldmode, i;

        for (i = 0; i < 0x1000; i++)
            blank[i] = 0;
        oldmode = crt_setmode(fd, CRT_O_BINARY);
        do {
            int32_t n = extend < 0x1000 ? extend : 0x1000;
            int32_t r = crt_write(fd, blank, (uint32_t)n);

            if (r == -1) {
                if (crt_doserrno == ERROR_ACCESS_DENIED)
                    crt_errno = CRT_EACCES;
                retval = -1;
                break;
            }
            extend -= r;
        } while (extend > 0);
        crt_setmode(fd, oldmode);
    } else if (extend < 0) {
        crt_lseek(fd, size, 0);
        if (!SetEndOfFile((HANDLE)crt_get_osfhandle(fd))) {
            retval = -1;
            crt_errno = CRT_EACCES;
            crt_doserrno = GetLastError();
        }
    }
    crt_lseek(fd, cur, 0);
    return retval;
}

int32_t __cdecl crt_sopen(const char *path, int32_t oflag, int32_t shflag, int32_t pmode)
{
    SECURITY_ATTRIBUTES sa;
    CRT_IOINFO *p;
    HANDLE      h;
    DWORD       access, share, create, attrs, type;
    uint8_t     fileflags;
    int32_t     fd;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    if (oflag & CRT_O_NOINHERIT) {
        sa.bInheritHandle = FALSE;
        fileflags = CRT_FNOINHERIT;
    } else {
        sa.bInheritHandle = TRUE;
        fileflags = 0;
    }
    if (!(oflag & CRT_O_BINARY)
        && ((oflag & CRT_O_TEXT) || crt_fmode != CRT_O_BINARY))
        fileflags |= CRT_FTEXT;

    switch (oflag & 3) {
    case CRT_O_RDONLY: access = GENERIC_READ; break;
    case CRT_O_WRONLY: access = GENERIC_WRITE; break;
    case CRT_O_RDWR:   access = GENERIC_READ | GENERIC_WRITE; break;
    default:           goto einval;
    }
    switch (shflag) {
    case CRT_SH_DENYRW: share = 0; break;
    case CRT_SH_DENYWR: share = FILE_SHARE_READ; break;
    case CRT_SH_DENYRD: share = FILE_SHARE_WRITE; break;
    case CRT_SH_DENYNO: share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    default:            goto einval;
    }
    switch (oflag & (CRT_O_CREAT | CRT_O_TRUNC | CRT_O_EXCL)) {
    case 0:
    case CRT_O_EXCL:
        create = OPEN_EXISTING; break;
    case CRT_O_CREAT:
        create = OPEN_ALWAYS; break;
    case CRT_O_TRUNC:
    case CRT_O_TRUNC | CRT_O_EXCL:
        create = TRUNCATE_EXISTING; break;
    case CRT_O_CREAT | CRT_O_TRUNC:
        create = CREATE_ALWAYS; break;
    case CRT_O_CREAT | CRT_O_EXCL:
    case CRT_O_CREAT | CRT_O_TRUNC | CRT_O_EXCL:
        create = CREATE_NEW; break;
    default:
        goto einval;
    }

    attrs = FILE_ATTRIBUTE_NORMAL;
    if ((oflag & CRT_O_CREAT) && !((~crt_umaskval & pmode) & 0x80))
        attrs = FILE_ATTRIBUTE_READONLY;
    if (oflag & CRT_O_TEMPORARY) {
        attrs |= FILE_FLAG_DELETE_ON_CLOSE;
        access |= DELETE;
    }
    if (oflag & CRT_O_SHORT_LIVED)
        attrs |= FILE_ATTRIBUTE_TEMPORARY;
    if (oflag & CRT_O_SEQUENTIAL)
        attrs |= FILE_FLAG_SEQUENTIAL_SCAN;
    else if (oflag & CRT_O_RANDOM)
        attrs |= FILE_FLAG_RANDOM_ACCESS;

    fd = crt_alloc_osfhnd();
    if (fd == -1) {
        crt_doserrno = 0;
        crt_errno = CRT_EMFILE;
        return -1;
    }
    h = CreateFileA(path, access, share, &sa, create, attrs, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        crt_dosmaperr(GetLastError());
        return -1;
    }
    type = GetFileType(h);
    if (type == 0) {
        CloseHandle(h);
        crt_dosmaperr(GetLastError());
        return -1;
    }
    if (type == FILE_TYPE_CHAR)
        fileflags |= CRT_FDEV;
    else if (type == FILE_TYPE_PIPE)
        fileflags |= CRT_FPIPE;

    crt_set_osfhnd(fd, (intptr_t)h);
    fileflags |= CRT_FOPEN;
    p = crt_pioinfo(fd);
    p->osfile = fileflags;

    /* A text file opened for update: a trailing Ctrl-Z is cut off so an
     * append does not land behind it. */
    if (!(fileflags & (CRT_FDEV | CRT_FPIPE)) && (fileflags & CRT_FTEXT)
        && (oflag & CRT_O_RDWR)) {
        int32_t eof = crt_lseek(fd, -1, 2);

        if (eof == -1) {
            if (crt_doserrno != ERROR_NEGATIVE_SEEK)
                goto fail;
        } else {
            char ch = 0;

            if (crt_read(fd, &ch, 1) == 0 && ch == 0x1A) {
                if (crt_chsize(fd, eof) == -1)
                    goto fail;
            }
            if (crt_lseek(fd, 0, 0) == -1)
                goto fail;
        }
    }
    if (!(fileflags & (CRT_FDEV | CRT_FPIPE)) && (oflag & CRT_O_APPEND))
        p->osfile |= CRT_FAPPEND;
    return fd;

fail:
    crt_close(fd);
    return -1;
einval:
    crt_errno = CRT_EINVAL;
    crt_doserrno = 0;
    return -1;
}
