/* crtcheck.cpp -- the reconstructed CRT against the original, in the one
 * process that holds both.
 *
 * The hybrid runs the ORIGINAL CRT over src/platform: its fopen reaches the
 * platform's CreateFileA through the IAT, its fread the platform's ReadFile.
 * The reconstruction in src/platform/crt calls the same functions by name.
 * So with both linked into this binary, one scripted sequence of stdio
 * calls can be run through each, on its own copy of the same file, and
 * everything observable compared with no budget: the return value, errno
 * and _doserrno, the FILE's flag, cnt, bufsiz and ptr-base, a hash of the
 * bytes handed back, and after fclose the file itself. No Wine and no
 * emulator: the same platform beneath both, which is what makes the
 * comparison exact.
 *
 * It runs INSTEAD of the game: when AM2_CRTCHECK names a directory, the
 * loader points the image's WinMain here, so the original's startup --
 * _ioinit, __initstdio, the heap -- has run and the two stacks share the
 * tables it built. tools/crtcheck.sh drives it, headless.
 *
 * What the corpus reaches, by construction: CR LF pairs split across the
 * 0x1000 buffer boundary and across the 0x200 one fseek shrinks to, a CR
 * as the last byte of a file, Ctrl-Z in the middle and at the end, a
 * buffer beginning on LF (FCRLF), LF runs straddling _write's 0x400
 * expansion buffer, every mode letter _openfile parses and the repeat that
 * ends its scan, an update open on a Ctrl-Z-terminated file (the trim in
 * _sopen), and a file that does not exist. A device or pipe is not in it;
 * those arms of _read and _write stay verified by reading.
 */
#include "../inject/win32.h"
#include "../inject/orig.h"
#include "../platform/platform.h"
#include "../platform/crt/crt.h"
#include "../game/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef CRT_FILE *(__cdecl *AM2_CkFopen)(const char *, const char *);
typedef int32_t   (__cdecl *AM2_CkFclose)(CRT_FILE *);
typedef uint32_t  (__cdecl *AM2_CkFread)(void *, uint32_t, uint32_t, CRT_FILE *);
typedef uint32_t  (__cdecl *AM2_CkFwrite)(const void *, uint32_t, uint32_t, CRT_FILE *);
typedef char     *(__cdecl *AM2_CkFgets)(char *, int32_t, CRT_FILE *);
typedef int32_t   (__cdecl *AM2_CkFseek)(CRT_FILE *, int32_t, int32_t);
typedef int32_t   (__cdecl *AM2_CkFtell)(CRT_FILE *);
typedef int32_t   (__cdecl *AM2_CkFflush)(CRT_FILE *);

typedef struct AM2_CkStack {
    const char   *name;
    AM2_CkFopen   fopen;
    AM2_CkFclose  fclose;
    AM2_CkFread   fread;
    AM2_CkFwrite  fwrite;
    AM2_CkFgets   fgets;
    AM2_CkFseek   fseek;
    AM2_CkFtell   ftell;
    AM2_CkFflush  fflush;
} AM2_CkStack;

static const AM2_CkStack kOrig = {
    "orig",
    (AM2_CkFopen)(uintptr_t)ADDR_FOPEN,   (AM2_CkFclose)(uintptr_t)ADDR_FCLOSE,
    (AM2_CkFread)(uintptr_t)ADDR_FREAD,   (AM2_CkFwrite)(uintptr_t)ADDR_FWRITE,
    (AM2_CkFgets)(uintptr_t)ADDR_CRT_FGETS, (AM2_CkFseek)(uintptr_t)ADDR_FSEEK,
    (AM2_CkFtell)(uintptr_t)ADDR_FTELL,   (AM2_CkFflush)(uintptr_t)ADDR_CRT_FFLUSH,
};
static const AM2_CkStack kOurs = {
    "ours",
    crt_fopen, crt_fclose, crt_fread, crt_fwrite, crt_fgets, crt_fseek, crt_ftell, crt_fflush,
};

#define ck_errno    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRNO))
#define ck_doserrno (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_DOSERRNO))

/* ---- corpus ------------------------------------------------------------ */

typedef struct AM2_CkFile {
    const char *name;
    uint8_t    *data;
    uint32_t    len;
} AM2_CkFile;

#define CK_MAX_FILES 24
static AM2_CkFile ck_files[CK_MAX_FILES];
static int32_t    ck_nfiles;

static uint32_t ck_rng;
static uint32_t ck_rnd(void)
{
    ck_rng ^= ck_rng << 13;
    ck_rng ^= ck_rng >> 17;
    ck_rng ^= ck_rng << 5;
    return ck_rng;
}

static uint8_t *ck_alloc(uint32_t n)
{
    uint8_t *p = (uint8_t *)malloc(n ? n : 1);

    if (!p) {
        fprintf(stderr, "crtcheck: out of memory\n");
        am2_host_exit(2);
    }
    return p;
}

static void ck_add(const char *name, const uint8_t *data, uint32_t len)
{
    AM2_CkFile *f = &ck_files[ck_nfiles++];

    f->name = name;
    f->data = ck_alloc(len);
    memcpy(f->data, data, len);
    f->len = len;
}

/* Lines of text with the given line end, about `len` bytes. */
static uint32_t ck_lines(uint8_t *out, uint32_t len, const char *eol)
{
    uint32_t n = 0, i = 0;

    while (n + 40 < len) {
        n += (uint32_t)sprintf((char *)out + n, "line %u of the corpus, %u wide%s",
                               i, ck_rnd() % 30, eol);
        i++;
    }
    return n;
}

static void ck_build_corpus(void)
{
    static uint8_t buf[0x4000];
    uint32_t       n, i;

    ck_rng = 0x9E3779B9u;
    ck_add("empty", buf, 0);
    buf[0] = 'x';
    ck_add("one", buf, 1);
    buf[0] = 0x1A;
    ck_add("ctrlz-only", buf, 1);
    n = ck_lines(buf, 0x2800, "\n");
    ck_add("lf", buf, n);
    n = ck_lines(buf, 0x2800, "\r\n");
    ck_add("crlf", buf, n);

    /* CR exactly at the end of each 0x1000 buffer, with the LF first in the
     * next; and the same at 0x1FF/0x200 for the buffer fseek shrinks. */
    n = ck_lines(buf, 0x3400, "\r\n");
    for (i = 0x1FF; i < n; i += 0x1000) {
        buf[i] = '\r';
        buf[i + 1] = '\n';
    }
    for (i = 0xFFF; i + 1 < n; i += 0x1000) {
        buf[i] = '\r';
        buf[i + 1] = '\n';
    }
    ck_add("crlf-split", buf, n);

    /* Lone CRs, some doubled, and one as the very last byte. */
    n = ck_lines(buf, 0x2200, "\n");
    for (i = 37; i < n; i += 211)
        buf[i] = '\r';
    for (i = 0xFFE; i < n; i += 0x1000)
        buf[i] = '\r';
    buf[n++] = '\r';
    ck_add("lone-cr", buf, n);

    /* A buffer that begins on LF: CR last in one block, LF first in the
     * next, where the LF is what FCRLF records. */
    n = ck_lines(buf, 0x2400, "\n");
    buf[0xFFF] = '\r';
    buf[0x1000] = '\n';
    buf[0x1FFF] = 'a';
    buf[0x2000] = '\n';
    ck_add("lf-first", buf, n);

    n = ck_lines(buf, 0x2600, "\r\n");
    buf[0x1234] = 0x1A;
    ck_add("ctrlz-mid", buf, n);
    n = ck_lines(buf, 0x1800, "\r\n");
    buf[n++] = 0x1A;
    ck_add("ctrlz-end", buf, n);
    n = ck_lines(buf, 0x1000, "\n");
    buf[0x800] = 0x1A;
    buf[n++] = 0x1A;
    ck_add("ctrlz-both", buf, n);

    for (i = 0; i < 0x3333; i++)
        buf[i] = (uint8_t)ck_rnd();
    ck_add("binary", buf, 0x3333);

    /* Sizes around the two buffer sizes, of a 4-byte CR LF pattern. */
    {
        static const uint32_t sizes[] = { 0x1FF, 0x200, 0x201, 0xFFF, 0x1000, 0x1001, 0x2000 };
        static char           names[7][16];

        for (i = 0; i < 7; i++) {
            uint32_t k;

            for (k = 0; k < sizes[i]; k++)
                buf[k] = "ab\r\n"[k & 3];
            sprintf(names[i], "size-%x", sizes[i]);
            ck_add(names[i], buf, sizes[i]);
        }
    }
}

/* ---- scripts ----------------------------------------------------------- */

typedef struct AM2_CkRec {
    int32_t  ret;
    int32_t  err;
    uint32_t doserr;
    int32_t  flag;
    int32_t  cnt;
    int32_t  bufsiz;
    int32_t  off;
    uint32_t hash;
} AM2_CkRec;

static int32_t ck_trace;   /* AM2_CRTCHECK_TRACE: 1 names each script, 2 each call */

#define CK_OPS      80
#define CK_RECS     (CK_OPS + 2)
#define CK_DESC     48

static uint32_t ck_hash(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;

    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static void ck_snapshot(AM2_CkRec *r, const CRT_FILE *f, int32_t ret, uint32_t hash)
{
    r->ret = ret;
    r->err = ck_errno;
    r->doserr = ck_doserrno;
    r->flag = f ? f->flag : -1;
    r->cnt = f ? f->cnt : -1;
    /* bufsiz is written by _getbuf and fseek and by nothing that opens a
     * stream, so it holds whatever the slot's last user left and cannot
     * be seen through the API until a buffer exists. Compared only then. */
    r->bufsiz = f && (f->flag & (CRT_IONBF | CRT_IOMYBUF | CRT_IOYOURBUF)) ? f->bufsiz : -1;
    r->off = f && f->base ? (int32_t)(f->ptr - f->base) : -1;
    r->hash = hash;
}

static const uint32_t kReadSizes[] = { 1, 2, 3, 7, 0x1FF, 0x200, 0x201, 0x1000, 0x1001, 0x1FFF, 0x3000 };
static const int32_t  kSeekOffs[]  = { 0, 1, -1, 5, -5, 0x1FF, 0x200, 0x1000, -0x1000, 0x123 };
static const uint32_t kWriteSizes[] = { 1, 5, 0x3FF, 0x400, 0x401, 0x7FF, 0x800, 0x1000, 0x1001, 0x2000 };

/* A chunk of text for fwrite: LFs every so often, the odd lone CR and
 * Ctrl-Z, deterministic in the script's own generator. */
static void ck_chunk(uint8_t *out, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        uint32_t r = ck_rnd();

        out[i] = (r & 15) == 0 ? '\n' : (r & 63) == 1 ? '\r'
               : (r & 255) == 2 ? 0x1A : (uint8_t)('a' + (r >> 8) % 26);
    }
}

/* Run one script on one stack. `writing` selects the write-heavy mix.
 * Returns the number of records written. */
static int32_t ck_run(const AM2_CkStack *s, const char *path, const char *mode,
                      uint32_t seed, int32_t writing, AM2_CkRec *rec,
                      char (*desc)[CK_DESC])
{
    static uint8_t  buf[0x4000];
    CRT_FILE       *f;
    int32_t         i, n = 0;
    int32_t         readable = 1;   /* a read is legal at this point */
    int32_t         update = strchr(mode, '+') != NULL;

    if (ck_trace)
        fprintf(stderr, "crtcheck: %s %s \"%s\"\n", s->name, path, mode);
    ck_rng = seed;
    ck_errno = 0;
    ck_doserrno = 0;
    f = s->fopen(path, mode);
    sprintf(desc[n], "fopen \"%s\"", mode);
    ck_snapshot(&rec[n++], f, f != NULL, 0);
    if (!f)
        return n;

    for (i = 0; i < CK_OPS; i++) {
        uint32_t k = ck_rnd() % 10;
        uint32_t sz;
        int32_t  r;

        ck_errno = 0;
        ck_doserrno = 0;
        if (ck_trace > 1)
            fprintf(stderr, "crtcheck:   op %d kind %u\n", i, k);
        if (!writing && k < 4) {
            sz = kReadSizes[ck_rnd() % 11];
            if (k == 3) {
                uint32_t cnt = ck_rnd() % 9 + 1;
                sprintf(desc[n], "fread size %u count %u", sz / 8 + 1, cnt);
                memset(buf, 0xEE, sizeof buf);
                r = (int32_t)s->fread(buf, sz / 8 + 1, cnt, f);
                ck_snapshot(&rec[n++], f, r, ck_hash(buf, (sz / 8 + 1) * cnt));
            } else {
                sprintf(desc[n], "fread %u", sz);
                memset(buf, 0xEE, sizeof buf);
                r = (int32_t)s->fread(buf, 1, sz, f);
                ck_snapshot(&rec[n++], f, r, ck_hash(buf, sz));
            }
        } else if (writing && k < 5) {
            sz = kWriteSizes[ck_rnd() % 10];
            ck_chunk(buf, sz);
            if (k == 4 && sz >= 4) {
                sprintf(desc[n], "fwrite size 4 count %u", sz / 4);
                r = (int32_t)s->fwrite(buf, 4, sz / 4, f);
            } else {
                sprintf(desc[n], "fwrite %u", sz);
                r = (int32_t)s->fwrite(buf, 1, sz, f);
            }
            ck_snapshot(&rec[n++], f, r, 0);
            readable = 0;
        } else if (k < 6 && writing && !(update && readable)) {
            /* A read on a stream whose last operation was a write, or on
             * a write-only stream, is undefined and the ORIGINAL makes it
             * so concretely: _filbuf's error arm leaves cnt at -1 and the
             * next fwrite copies min(remaining, 0xFFFFFFFF) bytes into the
             * buffer. The reconstruction does the same, and both took the
             * process down here until the scripts stopped asking. */
            sprintf(desc[n], "ftell (in place of a read)");
            r = s->ftell(f);
            ck_snapshot(&rec[n++], f, r, 0);
        } else if (k < 6) {
            static const int32_t lens[] = { 2, 8, 80, 0x300, 0x1100 };
            int32_t len = lens[ck_rnd() % 5];
            char   *got;

            sprintf(desc[n], "fgets %d", len);
            memset(buf, 0xEE, sizeof buf);
            got = s->fgets((char *)buf, len, f);
            ck_snapshot(&rec[n++], f, got != NULL, ck_hash(buf, (uint32_t)len));
        } else if (k == 6) {
            sprintf(desc[n], "ftell");
            r = s->ftell(f);
            ck_snapshot(&rec[n++], f, r, 0);
        } else if (k < 9) {
            int32_t off = kSeekOffs[ck_rnd() % 10];
            int32_t whence = (int32_t)(ck_rnd() % 3);

            sprintf(desc[n], "fseek %d whence %d", off, whence);
            r = s->fseek(f, off, whence);
            ck_snapshot(&rec[n++], f, r, 0);
            readable = 1;
        } else {
            sprintf(desc[n], "fflush");
            r = s->fflush(f);
            ck_snapshot(&rec[n++], f, r, 0);
            readable = 1;
        }
    }
    if (ck_trace > 1)
        fprintf(stderr, "crtcheck:   fclose\n");
    ck_errno = 0;
    ck_doserrno = 0;
    sprintf(desc[n], "fclose");
    {
        int32_t r = s->fclose(f);
        ck_snapshot(&rec[n++], NULL, r, 0);
    }
    return n;
}

/* ---- the comparison ---------------------------------------------------- */

static int32_t ck_mismatches, ck_shown, ck_scripts, ck_ops, ck_filediffs;

static void ck_write_host(const char *path, const uint8_t *data, uint32_t len)
{
    FILE *h = fopen(path, "wb");

    if (!h || (len && fwrite(data, 1, len, h) != len) || fclose(h) != 0) {
        fprintf(stderr, "crtcheck: cannot write %s\n", path);
        am2_host_exit(2);
    }
}

static uint8_t *ck_read_host(const char *path, uint32_t *len)
{
    FILE    *h = fopen(path, "rb");
    uint8_t *p;
    long     n;

    if (!h) {
        *len = 0xFFFFFFFFu;   /* absent: distinct from empty */
        return NULL;
    }
    fseek(h, 0, SEEK_END);
    n = ftell(h);
    fseek(h, 0, SEEK_SET);
    p = ck_alloc((uint32_t)n);
    if (n && fread(p, 1, (size_t)n, h) != (size_t)n)
        n = -1;
    fclose(h);
    *len = (uint32_t)n;
    return p;
}

static void ck_show(const AM2_CkFile *cf, const char *mode, int32_t op, const char *what,
                    const AM2_CkRec *a, const AM2_CkRec *b)
{
    ck_mismatches++;
    if (ck_shown++ >= 40)
        return;
    fprintf(stderr, "crtcheck: MISMATCH %s mode \"%s\" op %d %s\n"
                    "  orig ret %d err %d/%u flag 0x%x cnt %d bufsiz %d off %d hash %08x\n"
                    "  ours ret %d err %d/%u flag 0x%x cnt %d bufsiz %d off %d hash %08x\n",
            cf->name, mode, op, what,
            a->ret, a->err, a->doserr, a->flag, a->cnt, a->bufsiz, a->off, a->hash,
            b->ret, b->err, b->doserr, b->flag, b->cnt, b->bufsiz, b->off, b->hash);
}

static void ck_compare(const char *dir, const AM2_CkFile *cf, const char *mode,
                       uint32_t seed, int32_t writing, int32_t exists)
{
    static AM2_CkRec ra[CK_RECS], rb[CK_RECS];
    static char      da[CK_RECS][CK_DESC], db[CK_RECS][CK_DESC];
    char             pa[4096], pb[4096];
    int32_t          na, nb, i;
    uint8_t         *fa, *fb;
    uint32_t         la, lb;

    snprintf(pa, sizeof pa, "%s/%s.orig", dir, cf->name);
    snprintf(pb, sizeof pb, "%s/%s.ours", dir, cf->name);
    unlink(pa);
    unlink(pb);
    if (exists) {
        ck_write_host(pa, cf->data, cf->len);
        ck_write_host(pb, cf->data, cf->len);
    }
    /* AM2_CRTCHECK_ONLY=orig|ours runs one stack alone, for telling a
     * defect of one from an interaction through the tables they share. */
    {
        const char *only = getenv("AM2_CRTCHECK_ONLY");

        na = only && !strcmp(only, "ours") ? 0 : ck_run(&kOrig, pa, mode, seed, writing, ra, da);
        nb = only && !strcmp(only, "orig") ? 0 : ck_run(&kOurs, pb, mode, seed, writing, rb, db);
        if (only) {
            if (na == 0) { memcpy(ra, rb, sizeof ra); na = nb; }
            if (nb == 0) { memcpy(rb, ra, sizeof rb); nb = na; }
        }
    }
    ck_scripts++;
    ck_ops += na;
    for (i = 0; i < na || i < nb; i++) {
        if (i >= na || i >= nb) {
            ck_show(cf, mode, i, i < na ? da[i] : db[i], i < na ? &ra[i] : &rb[i],
                    i < nb ? &rb[i] : &ra[i]);
            break;
        }
        if (memcmp(&ra[i], &rb[i], sizeof ra[i]) != 0)
            ck_show(cf, mode, i, da[i], &ra[i], &rb[i]);
    }
    fa = ck_read_host(pa, &la);
    fb = ck_read_host(pb, &lb);
    if (la != lb || (la != 0xFFFFFFFFu && la && memcmp(fa, fb, la) != 0)) {
        uint32_t at = 0;

        ck_filediffs++;
        if (la == lb)
            while (at < la && fa[at] == fb[at])
                at++;
        if (ck_shown++ < 40)
            fprintf(stderr, "crtcheck: FILE DIFFERS %s mode \"%s\": orig %u bytes, ours %u, first at %u\n",
                    cf->name, mode, la, lb, at);
    }
    free(fa);
    free(fb);
    unlink(pa);
    unlink(pb);
}

static const char *const kReadModes[] = {
    "r", "rb", "rt", "r+", "rb+", "r+t", "rS", "rR", "rT", "rc", "rn", "r+ +", "rbb", "rtb",
};
static const char *const kWriteModes[] = {
    "w", "wb", "wt", "a", "ab", "a+", "w+", "wb+", "r+", "rb+", "wc", "wn", "at+",
};
static const char *const kBadModes[] = { "x", "", "+r", "b" };

extern "C" int32_t WINAPI am2_crtcheck_main(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int32_t show);
extern "C" int32_t WINAPI am2_crtcheck_main(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int32_t show)
{
    const char *dir = getenv("AM2_CRTCHECK");
    int32_t     fi, mi;
    uint32_t    seed = 1;

    (void)inst; (void)prev; (void)cmdline; (void)show;
    ck_trace = getenv("AM2_CRTCHECK_TRACE") ? atoi(getenv("AM2_CRTCHECK_TRACE")) : 0;
    if (!dir || !*dir) {
        fprintf(stderr, "crtcheck: AM2_CRTCHECK must name a writable directory\n");
        am2_host_exit(2);
    }
    ck_build_corpus();

    for (fi = 0; fi < ck_nfiles; fi++) {
        for (mi = 0; mi < (int32_t)(sizeof kReadModes / sizeof *kReadModes); mi++)
            ck_compare(dir, &ck_files[fi], kReadModes[mi], seed++, 0, 1);
        for (mi = 0; mi < (int32_t)(sizeof kWriteModes / sizeof *kWriteModes); mi++)
            ck_compare(dir, &ck_files[fi], kWriteModes[mi], seed++, 1, 1);
    }
    /* Bad modes, and a file that is not there, on one file each. */
    for (mi = 0; mi < (int32_t)(sizeof kBadModes / sizeof *kBadModes); mi++)
        ck_compare(dir, &ck_files[3], kBadModes[mi], seed++, 0, 1);
    ck_compare(dir, &ck_files[3], "r", seed++, 0, 0);
    ck_compare(dir, &ck_files[3], "r+", seed++, 1, 0);
    ck_compare(dir, &ck_files[3], "a", seed++, 1, 0);
    ck_compare(dir, &ck_files[3], "w", seed++, 1, 0);

    fprintf(stderr, "crtcheck: %d scripts, %d calls, %d files: %d mismatching calls, %d differing files\n",
            ck_scripts, ck_ops, ck_nfiles, ck_mismatches, ck_filediffs);
    am2_host_exit(ck_mismatches || ck_filediffs ? 1 : 0);
}
