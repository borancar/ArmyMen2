/* stdio.cpp -- the buffered FILE layer, from the bodies at 0x004644B7
 * (fwrite) through 0x00468168 (_getstream).
 *
 * A FILE is the CRT's 0x20-byte record, and the image's own twenty --
 * _iob at ADDR_CRT_IOB -- are the first twenty slots of __piob, the
 * pointer table _getstream searches. Every field the game can see is kept
 * where the original keeps it, because a FILE opened by one build's fopen
 * is read by the same build's fread and the layout is the contract between
 * them; the hybrid, which runs the original over our platform, is where the
 * two are compared (src/hybrid/crtcheck.cpp).
 *
 * The one thing done differently is WHEN __initstdio runs: the original's
 * startup calls it, the standalone and native builds have no such startup,
 * so the first _getstream does. Same tables, same contents, later.
 *
 * Two things the original does that look like defects and are kept: an
 * fopen leaves bufsiz holding whatever the slot's last user set, since
 * nothing on the open path writes it; and a read on a stream whose last
 * operation was a write leaves cnt at -1 through _filbuf's error arm, after
 * which the next fwrite copies min(remaining, 0xFFFFFFFF) bytes into the
 * buffer. The first is unobservable; the second is undefined behaviour the
 * game does not commit.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#define crt_iob      ((CRT_FILE *)(uintptr_t)AM2_IMAGE(ADDR_CRT_IOB))
#define crt_piob     (*(CRT_FILE ***)(uintptr_t)AM2_IMAGE(ADDR_CRT_PIOB))
#define crt_nstream  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_NSTREAM))
#define crt_commode  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_COMMODE))
#define crt_cflush   (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CFLUSH))
#define crt_errno    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRNO))
#define crt_stdout   (crt_iob + 1)
#define crt_stderr   (crt_iob + 2)

#define CRT_IOB_ENTRIES   20
#define CRT_NSTREAM_MAX   0x200
#define CRT_BUFSIZ        0x1000
#define CRT_SMALL_BUFSIZ  0x200
#define CRT_IOBUFFERED    (CRT_IONBF | CRT_IOMYBUF | CRT_IOYOURBUF)  /* 0x10C: has a buffer */
#define CRT_IOOPEN        (CRT_IOREAD | CRT_IOWRT | CRT_IORW)         /* 0x83: in use */

static CRT_IOINFO *crt_ioinfo_or_bad(int32_t fd)
{
    return fd != -1 ? crt_pioinfo(fd)
                    : (CRT_IOINFO *)(uintptr_t)AM2_IMAGE(ADDR_CRT_BADIOINFO);
}

void __cdecl crt_initstdio(void)
{
    int32_t i;

    if (crt_nstream == 0)
        crt_nstream = CRT_NSTREAM_MAX;
    else if (crt_nstream < CRT_IOB_ENTRIES)
        crt_nstream = CRT_IOB_ENTRIES;
    crt_piob = (CRT_FILE **)crt_calloc((uint32_t)crt_nstream, sizeof(CRT_FILE *));
    if (!crt_piob) {
        crt_nstream = CRT_IOB_ENTRIES;
        crt_piob = (CRT_FILE **)crt_calloc(CRT_IOB_ENTRIES, sizeof(CRT_FILE *));
        if (!crt_piob)
            crt_amsg_exit(0x1A);
    }
    for (i = 0; i < CRT_IOB_ENTRIES; i++)
        crt_piob[i] = &crt_iob[i];
    /* A standard stream with no handle behind it gets fd -1. The original
     * has run _ioinit by now; here the table may still be empty. */
    if (((CRT_IOINFO **)(uintptr_t)AM2_IMAGE(ADDR_CRT_PIOINFO))[0] == NULL)
        crt_ioinit();
    for (i = 0; i < 3; i++) {
        intptr_t h = crt_pioinfo(i)->osfhnd;

        if (h == -1 || h == 0)
            crt_iob[i].file = -1;
    }
}

CRT_FILE *__cdecl crt_getstream(void)
{
    CRT_FILE *f = NULL;
    int32_t   i;

    if (crt_piob == NULL)
        crt_initstdio();
    for (i = 0; i < crt_nstream; i++) {
        CRT_FILE *s = crt_piob[i];

        if (s == NULL) {
            crt_piob[i] = (CRT_FILE *)crt_malloc(sizeof(CRT_FILE));
            f = crt_piob[i];
            if (!f)
                return NULL;
            break;
        }
        if (!(s->flag & CRT_IOOPEN)) {
            f = s;
            break;
        }
    }
    if (!f)
        return NULL;
    f->file = -1;
    f->cnt = 0;
    f->flag = 0;
    f->base = NULL;
    f->ptr = NULL;
    f->tmpfname = NULL;
    return f;
}

CRT_FILE *__cdecl crt_openfile(const char *path, const char *mode, int32_t shflag, CRT_FILE *f)
{
    int32_t streamflag = crt_commode;
    int32_t oflag, fd;
    int32_t commodeset = 0, scanset = 0, more = 1;
    char    c;

    switch (*mode) {
    case 'a': oflag = CRT_O_WRONLY | CRT_O_CREAT | CRT_O_APPEND; streamflag |= CRT_IOWRT; break;
    case 'r': oflag = CRT_O_RDONLY; streamflag |= CRT_IOREAD; break;
    case 'w': oflag = CRT_O_WRONLY | CRT_O_CREAT | CRT_O_TRUNC; streamflag |= CRT_IOWRT; break;
    default:  return NULL;
    }

    /* The rest of the mode string, until a letter that cannot follow or
     * repeats: a repeat ENDS the scan rather than failing the open. */
    while ((c = *++mode) != 0 && more) {
        switch (c) {
        case '+':
            if (oflag & CRT_O_RDWR) { more = 0; break; }
            oflag = (oflag & ~CRT_O_WRONLY) | CRT_O_RDWR;
            streamflag = (streamflag & ~(CRT_IOREAD | CRT_IOWRT)) | CRT_IORW;
            break;
        case 'b':
            if (oflag & (CRT_O_TEXT | CRT_O_BINARY)) { more = 0; break; }
            oflag |= CRT_O_BINARY;
            break;
        case 't':
            if (oflag & (CRT_O_TEXT | CRT_O_BINARY)) { more = 0; break; }
            oflag |= CRT_O_TEXT;
            break;
        case 'c':
            if (commodeset) { more = 0; break; }
            commodeset = 1;
            streamflag |= CRT_IOCOMMIT;
            break;
        case 'n':
            if (commodeset) { more = 0; break; }
            commodeset = 1;
            streamflag &= ~CRT_IOCOMMIT;
            break;
        case 'S':
            if (scanset) { more = 0; break; }
            scanset = 1;
            oflag |= CRT_O_SEQUENTIAL;
            break;
        case 'R':
            if (scanset) { more = 0; break; }
            scanset = 1;
            oflag |= CRT_O_RANDOM;
            break;
        case 'T':
            if (oflag & CRT_O_SHORT_LIVED) { more = 0; break; }
            oflag |= CRT_O_SHORT_LIVED;
            break;
        case 'D':
            if (oflag & CRT_O_TEMPORARY) { more = 0; break; }
            oflag |= CRT_O_TEMPORARY;
            break;
        default:
            more = 0;
            break;
        }
    }

    fd = crt_sopen(path, oflag, shflag, 0x1A4);
    if (fd < 0)
        return NULL;
    crt_cflush++;
    f->flag = streamflag;
    f->cnt = 0;
    f->ptr = NULL;
    f->base = NULL;
    f->tmpfname = NULL;
    f->file = fd;
    return f;
}

CRT_FILE *__cdecl crt_fsopen(const char *path, const char *mode, int32_t shflag)
{
    CRT_FILE *f = crt_getstream();

    if (!f)
        return NULL;
    return crt_openfile(path, mode, shflag, f);
}

CRT_FILE *__cdecl crt_fopen(const char *path, const char *mode)
{
    return crt_fsopen(path, mode, CRT_SH_DENYNO);
}

void __cdecl crt_getbuf(CRT_FILE *f)
{
    crt_cflush++;
    f->base = (char *)crt_malloc(CRT_BUFSIZ);
    if (f->base) {
        f->flag |= CRT_IOMYBUF;
        f->bufsiz = CRT_BUFSIZ;
    } else {
        f->flag |= CRT_IONBF;
        f->base = (char *)&f->charbuf;
        f->bufsiz = 2;
    }
    f->ptr = f->base;
    f->cnt = 0;
}

void __cdecl crt_freebuf(CRT_FILE *f)
{
    if ((f->flag & CRT_IOOPEN) && (f->flag & CRT_IOMYBUF)) {
        crt_free(f->base);
        f->flag &= ~(CRT_IOMYBUF | CRT_IOSETVBUF);
        f->ptr = NULL;
        f->base = NULL;
        f->cnt = 0;
    }
}

int32_t __cdecl crt_flush(CRT_FILE *f)
{
    int32_t rc = 0;

    if ((f->flag & (CRT_IOREAD | CRT_IOWRT)) == CRT_IOWRT
        && (f->flag & (CRT_IOMYBUF | CRT_IOYOURBUF))) {
        int32_t n = (int32_t)(f->ptr - f->base);

        if (n > 0) {
            if (crt_write(f->file, f->base, (uint32_t)n) == n) {
                if (f->flag & CRT_IORW)
                    f->flag &= ~CRT_IOWRT;
            } else {
                f->flag |= CRT_IOERR;
                rc = -1;
            }
        }
    }
    f->ptr = f->base;
    f->cnt = 0;
    return rc;
}

int32_t __cdecl crt_flushall(int32_t flag)
{
    int32_t i, count = 0, err = 0;

    for (i = 0; i < crt_nstream; i++) {
        CRT_FILE *f = crt_piob[i];

        if (!f || !(f->flag & CRT_IOOPEN))
            continue;
        if (flag == 1) {
            if (crt_fflush(f) != -1)
                count++;
        } else if (flag == 0) {
            if ((f->flag & CRT_IOWRT) && crt_fflush(f) == -1)
                err = -1;
        }
    }
    return flag == 1 ? count : err;
}

int32_t __cdecl crt_fflush(CRT_FILE *f)
{
    if (!f)
        return crt_flushall(0);
    if (crt_flush(f) != 0)
        return -1;
    if (f->flag & CRT_IOCOMMIT)
        return crt_commit(f->file) ? -1 : 0;
    return 0;
}

int32_t __cdecl crt_fclose(CRT_FILE *f)
{
    int32_t rc = -1;

    if (f->flag & CRT_IOSTRG) {
        f->flag = 0;
        return -1;
    }
    if (f->flag & CRT_IOOPEN) {
        rc = crt_flush(f);
        crt_freebuf(f);
        if (crt_close(f->file) < 0) {
            rc = -1;
        } else if (f->tmpfname) {
            crt_free(f->tmpfname);
            f->tmpfname = NULL;
        }
    }
    f->flag = 0;
    return rc;
}

int32_t __cdecl crt_flsbuf(int32_t ch, CRT_FILE *f)
{
    int32_t fd = f->file;
    int32_t charcount, written;

    if (!(f->flag & (CRT_IOWRT | CRT_IORW)) || (f->flag & CRT_IOSTRG)) {
        f->flag |= CRT_IOERR;
        return -1;
    }
    if (f->flag & CRT_IOREAD) {
        /* A read/write stream switching to writing: only past the end of
         * what it read. */
        f->cnt = 0;
        if (!(f->flag & CRT_IOEOF)) {
            f->flag |= CRT_IOERR;
            return -1;
        }
        f->ptr = f->base;
        f->flag &= ~CRT_IOREAD;
    }
    f->cnt = 0;
    f->flag = (f->flag & ~CRT_IOEOF) | CRT_IOWRT;
    charcount = 0;
    if (!(f->flag & CRT_IOBUFFERED)) {
        /* stdout and stderr on a terminal stay unbuffered. */
        if (!((f == crt_stdout || f == crt_stderr) && crt_isatty(fd)))
            crt_getbuf(f);
    }
    if (f->flag & (CRT_IOMYBUF | CRT_IOYOURBUF)) {
        charcount = (int32_t)(f->ptr - f->base);
        f->ptr = f->base + 1;
        f->cnt = f->bufsiz - 1;
        if (charcount > 0) {
            written = crt_write(fd, f->base, (uint32_t)charcount);
        } else {
            if (crt_ioinfo_or_bad(fd)->osfile & CRT_FAPPEND)
                crt_lseek(fd, 0, 2);
            written = 0;
        }
        *f->base = (char)ch;
    } else {
        charcount = 1;
        written = crt_write(fd, &ch, 1);
    }
    if (written != charcount) {
        f->flag |= CRT_IOERR;
        return -1;
    }
    return ch & 0xFF;
}

int32_t __cdecl crt_filbuf(CRT_FILE *f)
{
    int32_t n;

    if (!(f->flag & CRT_IOOPEN) || (f->flag & CRT_IOSTRG))
        return -1;
    if (f->flag & CRT_IOWRT) {
        f->flag |= CRT_IOERR;
        return -1;
    }
    f->flag |= CRT_IOREAD;
    if (!(f->flag & CRT_IOBUFFERED))
        crt_getbuf(f);
    else
        f->ptr = f->base;
    n = crt_read(f->file, f->base, (uint32_t)f->bufsiz);
    f->cnt = n;
    if (n == 0 || n == -1) {
        f->flag |= n == 0 ? CRT_IOEOF : CRT_IOERR;
        f->cnt = 0;
        return -1;
    }
    /* A read-only text stream: note whether the descriptor stopped on a
     * Ctrl-Z, which ftell counts as one more byte of the file. */
    if (!(f->flag & (CRT_IOWRT | CRT_IORW))) {
        if ((crt_ioinfo_or_bad(f->file)->osfile & (CRT_FTEXT | CRT_FEOFLAG))
            == (CRT_FTEXT | CRT_FEOFLAG))
            f->flag |= CRT_IOCTRLZ;
    }
    /* A buffer shrunk to 0x200 by fseek grows back on the next fill. */
    if (f->bufsiz == CRT_SMALL_BUFSIZ && (f->flag & CRT_IOMYBUF)
        && !(f->flag & CRT_IOSETVBUF))
        f->bufsiz = CRT_BUFSIZ;
    f->cnt = n - 1;
    return (uint8_t)*f->ptr++;
}

uint32_t __cdecl crt_fread(void *buf, uint32_t size, uint32_t count, CRT_FILE *f)
{
    uint32_t total = size * count;
    uint32_t remaining = total;
    uint32_t bufsize;
    char    *out = (char *)buf;

    if (total == 0)
        return 0;
    bufsize = (f->flag & CRT_IOBUFFERED) ? (uint32_t)f->bufsiz : CRT_BUFSIZ;
    while (remaining != 0) {
        if ((f->flag & CRT_IOBUFFERED) && f->cnt != 0) {
            /* Whatever the buffer holds first. */
            uint32_t n = remaining < (uint32_t)f->cnt ? remaining : (uint32_t)f->cnt;

            crt_memcpy(out, f->ptr, n);
            remaining -= n;
            f->cnt -= (int32_t)n;
            f->ptr += n;
            out += n;
        } else if (remaining >= bufsize) {
            /* Whole buffers straight into the caller's memory. */
            uint32_t n = bufsize ? remaining - remaining % bufsize : remaining;
            int32_t  r = crt_read(f->file, out, n);

            if (r == 0) {
                f->flag |= CRT_IOEOF;
                return (total - remaining) / size;
            }
            if (r == -1) {
                f->flag |= CRT_IOERR;
                return (total - remaining) / size;
            }
            remaining -= (uint32_t)r;
            out += r;
        } else {
            int32_t c = crt_filbuf(f);

            if (c == -1)
                return (total - remaining) / size;
            *out++ = (char)c;
            remaining--;
            bufsize = (uint32_t)f->bufsiz;
        }
    }
    return count;
}

uint32_t __cdecl crt_fwrite(const void *buf, uint32_t size, uint32_t count, CRT_FILE *f)
{
    uint32_t    total = size * count;
    uint32_t    remaining = total;
    uint32_t    bufsize;
    const char *in = (const char *)buf;

    if (total == 0)
        return 0;
    bufsize = (f->flag & CRT_IOBUFFERED) ? (uint32_t)f->bufsiz : CRT_BUFSIZ;
    while (remaining != 0) {
        int32_t buffered = f->flag & (CRT_IOMYBUF | CRT_IOYOURBUF);

        if (buffered && f->cnt != 0) {
            uint32_t n = remaining < (uint32_t)f->cnt ? remaining : (uint32_t)f->cnt;

            crt_memcpy(f->ptr, in, n);
            f->cnt -= (int32_t)n;
            f->ptr += n;
            remaining -= n;
            in += n;
        } else if (remaining >= bufsize) {
            uint32_t n;
            int32_t  r;

            if (buffered && crt_flush(f) != 0)
                return (total - remaining) / size;
            n = bufsize ? remaining - remaining % bufsize : remaining;
            r = crt_write(f->file, in, n);
            if (r == -1) {
                f->flag |= CRT_IOERR;
                return (total - remaining) / size;
            }
            in += r;
            remaining -= (uint32_t)r;
            if ((uint32_t)r < n) {
                f->flag |= CRT_IOERR;
                return (total - remaining) / size;
            }
        } else {
            if (crt_flsbuf((uint8_t)*in, f) == -1)
                return (total - remaining) / size;
            in++;
            remaining--;
            bufsize = (uint32_t)f->bufsiz;
            if ((int32_t)bufsize <= 0)
                bufsize = 1;
        }
    }
    return count;
}

char *__cdecl crt_fgets(char *s, int32_t n, CRT_FILE *f)
{
    char *p = s;

    if (n <= 0)
        return NULL;
    if (--n == 0) {
        *p = 0;
        return s;
    }
    for (;;) {
        int32_t c = --f->cnt >= 0 ? (uint8_t)*f->ptr++ : crt_filbuf(f);

        if (c == -1) {
            if (p == s)
                return NULL;
            break;
        }
        *p++ = (char)c;
        if (c == '\n')
            break;
        if (--n == 0)
            break;
    }
    *p = 0;
    return s;
}

int32_t __cdecl crt_ftell(CRT_FILE *f)
{
    int32_t fd = f->file;
    int32_t filepos, offset, rdcnt;

    if (f->cnt < 0)
        f->cnt = 0;
    filepos = crt_lseek(fd, 0, 1);
    if (filepos < 0)
        return -1;
    if (!(f->flag & (CRT_IOMYBUF | CRT_IOYOURBUF)))
        return filepos - f->cnt;

    offset = (int32_t)(f->ptr - f->base);
    if (f->flag & (CRT_IOREAD | CRT_IOWRT)) {
        /* Each LF the buffer holds was a CR LF in the file. */
        if (crt_pioinfo(fd)->osfile & CRT_FTEXT) {
            const char *p;

            for (p = f->base; p < f->ptr; p++)
                if (*p == '\n')
                    offset++;
        }
    } else if (!(f->flag & CRT_IORW)) {
        crt_errno = CRT_EINVAL;
        return -1;
    }
    if (filepos == 0)
        return offset;
    if (f->flag & CRT_IOREAD) {
        if (f->cnt == 0) {
            /* The buffer is spent: the position is where the file is. */
            offset = 0;
        } else {
            rdcnt = (int32_t)(f->ptr - f->base) + f->cnt;
            if (crt_pioinfo(fd)->osfile & CRT_FTEXT) {
                if (crt_lseek(fd, 0, 2) == filepos) {
                    /* The buffer reaches the end of the file: count its
                     * LFs twice, and the Ctrl-Z it stopped on once. */
                    const char *p = f->base, *end = f->base + rdcnt;

                    for (; p < end; p++)
                        if (*p == '\n')
                            rdcnt++;
                    if (f->flag & CRT_IOCTRLZ)
                        rdcnt++;
                } else {
                    crt_lseek(fd, filepos, 0);
                    if ((uint32_t)rdcnt <= CRT_SMALL_BUFSIZ && (f->flag & CRT_IOMYBUF)
                        && !(f->flag & CRT_IOSETVBUF))
                        rdcnt = CRT_SMALL_BUFSIZ;
                    else
                        rdcnt = f->bufsiz;
                    if (crt_pioinfo(fd)->osfile & CRT_FCRLF)
                        rdcnt++;
                }
            }
            filepos -= rdcnt;
        }
    }
    return offset + filepos;
}

int32_t __cdecl crt_fseek(CRT_FILE *f, int32_t off, int32_t whence)
{
    if (!(f->flag & CRT_IOOPEN) || (whence != 0 && whence != 1 && whence != 2)) {
        crt_errno = CRT_EINVAL;
        return -1;
    }
    f->flag &= ~CRT_IOEOF;
    if (whence == 1) {
        off += crt_ftell(f);
        whence = 0;
    }
    crt_flush(f);
    if (f->flag & CRT_IORW) {
        f->flag &= ~(CRT_IOREAD | CRT_IOWRT);
    } else if ((f->flag & CRT_IOREAD) && (f->flag & CRT_IOMYBUF)
               && !(f->flag & CRT_IOSETVBUF)) {
        /* A seek usually precedes a short read: the next fill is small. */
        f->bufsiz = CRT_SMALL_BUFSIZ;
    }
    return crt_lseek(f->file, off, whence) == -1 ? -1 : 0;
}
