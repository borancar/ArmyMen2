/* vc6host Win32/CRT shim -- Linux implementations of the KERNEL32, USER32 and
 * MSVCRT functions the VC6 toolchain imports. Grown in the order a run hits
 * them; everything not here yet resolves to a logging abort-stub in the loader.
 *
 * MSVCRT functions are __cdecl (most map straight onto glibc). KERNEL32/USER32
 * are __stdcall (WINAPI). Data exports (_iob, _adjust_fdiv, ...) resolve to the
 * ADDRESS of our object, exactly as a function name resolves to its code.
 */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <math.h>
#include <locale.h>

#define STDCALL __attribute__((stdcall))
#define CDECL   __attribute__((cdecl))

/* ------------------------------------------------------------------ startup */

/* The process arguments the compiler pass should see. Set by the runner. */
int    vc6_argc = 1;
char **vc6_argv = 0;
char **vc6_env  = 0;

static int  s_fmode   = 0;      /* _O_TEXT */
static int  s_commode = 0;
static char s_pgmname[] = "cl";
static char *s_pgmptr = s_pgmname;

int   _adjust_fdiv = 0;   /* Pentium FDIV fix flag: no fixup needed */
int   __mb_cur_max = 1;

typedef void (CDECL *PVFV)(void);
typedef int  (CDECL *PIFV)(void);

static void CDECL sh___set_app_type(int) {}
static void CDECL sh___setusermatherr(void *) {}
static int  *CDECL sh___p__fmode(void)   { return &s_fmode; }
static int  *CDECL sh___p__commode(void) { return &s_commode; }
static char***CDECL sh___p___initenv(void){ return &vc6_env; }
static char**CDECL sh___p__pgmptr(void)  { return &s_pgmptr; }
static unsigned CDECL sh__controlfp(unsigned, unsigned) { return 0x9001F; }

static void CDECL sh__initterm(PVFV *begin, PVFV *end)
{
    for (; begin < end; begin++)
        if (*begin) (*begin)();
}

typedef struct { int newmode; } StartupInfo;
static int CDECL sh___getmainargs(int *argc, char ***argv, char ***env,
                                  int, StartupInfo *)
{
    *argc = vc6_argc;
    *argv = vc6_argv;
    if (env) *env = vc6_env;
    return 0;
}

static int  CDECL sh__XcptFilter(unsigned, void *) { return 0; /* EXCEPTION_CONTINUE_SEARCH */ }
static int  CDECL sh__except_handler3(void *, void *, void *, void *) { return 1; /* ContinueSearch */ }
static int  CDECL sh___CxxFrameHandler(void *, void *, void *, void *) { return 1; }
static void*CDECL sh___dllonexit(void *f, void **, void **) { return f; }
static void*CDECL sh__onexit(void *f) { return f; }
static void CDECL sh__amsg_exit(int code)
{
    fprintf(stderr, "vc6host: CRT fatal error R%d\n", code);
    _exit(code ? code : 255);
}
static void CDECL sh__purecall(void) { fprintf(stderr, "vc6host: pure virtual call\n"); _exit(255); }
static void CDECL sh__setmbcp(int) {}

/* cl builds Windows paths (backslash separators); translate to POSIX at every
 * file-open boundary, and fold a leading drive letter "X:" away. */
static const char *winpath(const char *p, char *buf, size_t cap)
{
    if (!p) return p;
    size_t i = 0, j = 0;
    if (p[0] && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) i = 2;  /* drop "X:" */
    for (; p[i] && j < cap - 1; i++) buf[j++] = p[i] == '\\' ? '/' : p[i];
    buf[j] = 0;
    return buf;
}
#define WP(p) winpath((p), (char[4096]){0}, 4096)

/* ------------------------------------------------------------------- stdio  */
/* MSVC FILE: ptr,cnt,base,flag,file,charbuf,bufsiz,tmpfname (32 bytes). We run
 * it unbuffered -- base=0, cnt=0 -- so the header's putc/getc macros always
 * fall to _flsbuf/_filbuf, and those go straight to the fd in `file`. */
typedef struct { char *ptr; int cnt; char *base; int flag; int file;
                 int charbuf; int bufsiz; char *tmpfname; } MsvcFile;

#define F_READ   0x0001
#define F_WRITE  0x0002
#define F_OPEN   0x0080   /* in use */

static MsvcFile s_iob[20];
static int      s_iob_init;
static void iob_init(void)
{
    if (s_iob_init) return;
    s_iob_init = 1;
    s_iob[0].file = 0; s_iob[0].flag = F_READ  | F_OPEN;
    s_iob[1].file = 1; s_iob[1].flag = F_WRITE | F_OPEN;
    s_iob[2].file = 2; s_iob[2].flag = F_WRITE | F_OPEN;
}

static int CDECL sh__flsbuf(int ch, MsvcFile *f)
{
    unsigned char c = (unsigned char)ch;
    if (!f || write(f->file, &c, 1) != 1) return -1;   /* EOF */
    return c;
}
static int CDECL sh__filbuf(MsvcFile *f)
{
    unsigned char c;
    if (!f || read(f->file, &c, 1) != 1) return -1;
    return c;
}
static size_t CDECL sh_fwrite(const void *p, size_t sz, size_t n, MsvcFile *f)
{
    if (!f || sz == 0) return 0;
    ssize_t w = write(f->file, p, sz * n);
    return w <= 0 ? 0 : (size_t)w / sz;
}
static size_t CDECL sh_fread(void *p, size_t sz, size_t n, MsvcFile *f)
{
    if (!f || sz == 0) return 0;
    ssize_t r = read(f->file, p, sz * n);
    return r <= 0 ? 0 : (size_t)r / sz;
}
static int CDECL sh_fputc(int ch, MsvcFile *f) { return sh__flsbuf(ch, f); }
static int CDECL sh_fputs(const char *s, MsvcFile *f)
{ size_t n = strlen(s); return sh_fwrite(s, 1, n, f) == n ? 0 : -1; }
static int CDECL sh_fflush(MsvcFile *) { return 0; }         /* unbuffered */
static int CDECL sh_vfprintf(MsvcFile *f, const char *fmt, va_list ap)
{
    char buf[4096];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) return n;
    size_t m = (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;
    sh_fwrite(buf, 1, m, f);
    return n;
}
static int CDECL sh_fprintf(MsvcFile *f, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); int n = sh_vfprintf(f, fmt, ap); va_end(ap); return n; }
static int CDECL sh_printf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); int n = sh_vfprintf(&s_iob[1], fmt, ap); va_end(ap); return n; }
static int CDECL sh_vprintf(const char *fmt, va_list ap) { return sh_vfprintf(&s_iob[1], fmt, ap); }
static int CDECL sh_puts(const char *s)
{ sh_fwrite(s, 1, strlen(s), &s_iob[1]); char nl = '\n'; return sh_fwrite(&nl,1,1,&s_iob[1])==1 ? 0 : -1; }
static int CDECL sh_putchar(int c) { return sh__flsbuf(c, &s_iob[1]); }
static int CDECL sh_putc(int c, MsvcFile *f) { return sh__flsbuf(c, f); }
static int CDECL sh_getc(MsvcFile *f) { return sh__filbuf(f); }
static int CDECL sh_getchar(void) { return sh__filbuf(&s_iob[0]); }

static int is_std(MsvcFile *f) { return f >= s_iob && f < s_iob + 20; }
static MsvcFile *CDECL sh_fopen(const char *path, const char *mode)
{
    int fl;
    if (strchr(mode, 'r')) fl = strchr(mode, '+') ? O_RDWR : O_RDONLY;
    else if (strchr(mode, 'w')) fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    else if (strchr(mode, 'a')) fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    else return 0;
    int fd = open(WP(path), fl, 0666);
    if (fd < 0) return 0;
    MsvcFile *f = (MsvcFile *)calloc(1, sizeof *f);
    f->file = fd; f->flag = F_OPEN | (fl == O_RDONLY ? F_READ : F_WRITE);
    return f;
}
static MsvcFile *CDECL sh__fsopen(const char *path, const char *mode, int sh) { (void)sh; return sh_fopen(path, mode); }
static int CDECL sh_fclose(MsvcFile *f)
{
    if (!f) return -1;
    if (f->file > 2) close(f->file);
    if (is_std(f)) f->flag = 0; else free(f);
    return 0;
}
static MsvcFile *CDECL sh_freopen(const char *path, const char *mode, MsvcFile *f)
{
    if (!f) return 0;
    if (f->file > 2) close(f->file);
    MsvcFile *n = sh_fopen(path, mode);
    if (!n) return 0;
    f->file = n->file; f->flag = n->flag; if (!is_std(n)) free(n);
    return f;
}
static int  CDECL sh_fseek(MsvcFile *f, long off, int wh) { return lseek(f->file, off, wh) < 0 ? -1 : 0; }
static long CDECL sh_ftell(MsvcFile *f) { return (long)lseek(f->file, 0, SEEK_CUR); }
static void CDECL sh_rewind(MsvcFile *f) { lseek(f->file, 0, SEEK_SET); }
static int  CDECL sh_setvbuf(MsvcFile *f, char *b, int m, size_t s) { (void)f;(void)b;(void)m;(void)s; return 0; }
static char *CDECL sh_fgets(char *s, int n, MsvcFile *f)
{
    int i = 0;
    while (i < n - 1) { unsigned char c; if (read(f->file, &c, 1) <= 0) { if (!i) return 0; break; }
                        s[i++] = (char)c; if (c == '\n') break; }
    s[i] = 0; return s;
}
static int CDECL sh_flushall(void) { return 0; }
static int CDECL sh_fcloseall(void) { return 0; }

/* -------------------------------------------------------------- mbcs/ctype  */
/* The toolchain runs single-byte here (no DBCS code page), so every MBCS
 * routine is its plain-char equivalent. */
typedef unsigned char uchar;
static int    CDECL sh__mbsncmp(const uchar *a, const uchar *b, size_t n) { return strncmp((const char*)a,(const char*)b,n); }
static int    CDECL sh__mbscmp(const uchar *a, const uchar *b) { return strcmp((const char*)a,(const char*)b); }
static int    CDECL sh__mbsicmp(const uchar *a, const uchar *b) { return strcasecmp((const char*)a,(const char*)b); }
static int    CDECL sh__mbsnbcmp(const uchar *a, const uchar *b, size_t n) { return strncmp((const char*)a,(const char*)b,n); }
static int    CDECL sh__mbsnbicmp(const uchar *a, const uchar *b, size_t n) { return strncasecmp((const char*)a,(const char*)b,n); }
static uchar *CDECL sh__mbschr(const uchar *s, unsigned c) { return (uchar*)strchr((const char*)s,(int)c); }
static uchar *CDECL sh__mbsrchr(const uchar *s, unsigned c) { return (uchar*)strrchr((const char*)s,(int)c); }
static uchar *CDECL sh__mbsstr(const uchar *s, const uchar *t) { return (uchar*)strstr((const char*)s,(const char*)t); }
static uchar *CDECL sh__mbspbrk(const uchar *s, const uchar *t) { return (uchar*)strpbrk((const char*)s,(const char*)t); }
static size_t CDECL sh__mbsspn(const uchar *s, const uchar *t) { return strspn((const char*)s,(const char*)t); }
static uchar *CDECL sh__mbslwr(uchar *s) { for (uchar *p=s; *p; p++) *p=(uchar)tolower(*p); return s; }
static uchar *CDECL sh__mbsupr(uchar *s) { for (uchar *p=s; *p; p++) *p=(uchar)toupper(*p); return s; }
static uchar *CDECL sh__mbsinc(const uchar *p) { return (uchar*)(p+1); }
static uchar *CDECL sh__mbsdec(const uchar *s, const uchar *p) { return (uchar*)(p>s?p-1:s); }
static size_t CDECL sh__mbclen(const uchar *p) { (void)p; return 1; }
static size_t CDECL sh__mbstrlen(const char *s) { return strlen(s); }
static uchar *CDECL sh__mbsnbcpy(uchar *d, const uchar *s, size_t n) { strncpy((char*)d,(const char*)s,n); return d; }
static uchar *CDECL sh__mbstok(uchar *s, const uchar *d) { return (uchar*)strtok((char*)s,(const char*)d); }
static unsigned CDECL sh__mbctolower(unsigned c) { return (unsigned)tolower((int)c); }
static unsigned CDECL sh__mbctoupper(unsigned c) { return (unsigned)toupper((int)c); }
static void   CDECL sh__mbccpy(uchar *d, const uchar *s) { d[0]=s[0]; }
static int    CDECL sh__ismbcspace(unsigned c) { return isspace((int)(c&0xff)); }
static int    CDECL sh__isctype(int c, int mask)
{
    unsigned ch = (unsigned)c & 0xff; int r = 0;
    if ((mask&0x001)&&isupper(ch)) r|=0x001;
    if ((mask&0x002)&&islower(ch)) r|=0x002;
    if ((mask&0x004)&&isdigit(ch)) r|=0x004;
    if ((mask&0x008)&&isspace(ch)) r|=0x008;
    if ((mask&0x010)&&ispunct(ch)) r|=0x010;
    if ((mask&0x020)&&iscntrl(ch)) r|=0x020;
    if ((mask&0x040)&&(ch==' '||ch=='\t')) r|=0x040;
    if ((mask&0x080)&&isxdigit(ch)) r|=0x080;
    if ((mask&0x100)&&isalpha(ch)) r|=0x100;
    return r;
}
static unsigned char sh_mbctype_tbl[257];       /* all zero: no DBCS lead/trail */

/* _pctype: MSVC's ctype bit table, indexable [-128..255]; the export is the
 * address of the pointer that points at index 0. */
static unsigned short sh_ctype_tbl[384];
static unsigned short *sh_pctype = sh_ctype_tbl + 128;
__attribute__((constructor)) static void sh_init_ctype(void)
{
    for (int c = -128; c <= 255; c++) {
        unsigned short m = 0;
        if (c >= 0) { int u = c & 0xff;
            if (isupper(u)) m |= 0x001; if (islower(u)) m |= 0x002;
            if (isdigit(u)) m |= 0x004; if (isspace(u)) m |= 0x008;
            if (ispunct(u)) m |= 0x010; if (iscntrl(u)) m |= 0x020;
            if (u == ' ' || u == '\t') m |= 0x040; if (isxdigit(u)) m |= 0x080;
            if (isalpha(u)) m |= 0x100; }
        sh_ctype_tbl[c + 128] = m;
    }
}

/* ------------------------------------------------------------ string extras */
static char *CDECL sh__strlwr(char *s) { for (char *p=s; *p; p++) *p=(char)tolower((uchar)*p); return s; }
static char *CDECL sh__strupr(char *s) { for (char *p=s; *p; p++) *p=(char)toupper((uchar)*p); return s; }
static char *itoa_core(unsigned long v, char *buf, int radix, int neg)
{
    char t[36]; int i=0;
    if (radix<2||radix>36) { buf[0]=0; return buf; }
    if (!v) t[i++]='0';
    while (v) { int d=(int)(v%(unsigned)radix); t[i++]=(char)(d<10?'0'+d:'a'+d-10); v/=(unsigned)radix; }
    char *p=buf; if (neg) *p++='-'; while (i) *p++=t[--i]; *p=0; return buf;
}
static char *CDECL sh__itoa(int v, char *buf, int radix)
{ return (radix==10&&v<0) ? itoa_core((unsigned long)(-(long)v),buf,10,1) : itoa_core((unsigned long)(unsigned)v,buf,radix,0); }
static char *CDECL sh__ltoa(long v, char *buf, int radix)
{ return (radix==10&&v<0) ? itoa_core((unsigned long)(-v),buf,10,1) : itoa_core((unsigned long)v,buf,radix,0); }
static char *CDECL sh__ultoa(unsigned long v, char *buf, int radix)
{ return itoa_core(v,buf,radix,0); }

/* ------------------------------------------------------------------ low I/O */
/* MSVC _O_* differ from Linux O_*: translate the ones the toolchain uses. */
static int msvc_oflags(int mo)
{
    int f = mo & 3;                       /* RDONLY/WRONLY/RDWR match */
    if (mo & 0x0008) f |= O_APPEND;
    if (mo & 0x0100) f |= O_CREAT;
    if (mo & 0x0200) f |= O_TRUNC;
    if (mo & 0x0400) f |= O_EXCL;
    return f;                             /* _O_TEXT/_O_BINARY: no-op on Linux */
}
static int  CDECL sh__write(int fd, const void *b, unsigned n) { return (int)write(fd, b, n); }
static int  CDECL sh__read(int fd, void *b, unsigned n) { return (int)read(fd, b, n); }
static int  CDECL sh__close(int fd) { return close(fd); }
static long CDECL sh__lseek(int fd, long off, int wh) { return (long)lseek(fd, off, wh); }
static int  CDECL sh__open(const char *p, int mo, ...)
{ int pm = 0666; if (mo & 0x100) { va_list a; va_start(a, mo); pm = va_arg(a, int); va_end(a); }
  return open(WP(p), msvc_oflags(mo), 0666); }
static int  CDECL sh__sopen(const char *p, int mo, int sh, ...) { (void)sh; return open(WP(p), msvc_oflags(mo), 0666); }
static int  CDECL sh__dup(int fd) { return dup(fd); }
static int  CDECL sh__dup2(int a, int b) { return dup2(a, b) < 0 ? -1 : 0; }
static int  CDECL sh__isatty(int fd) { return isatty(fd); }
static long CDECL sh__tell(int fd) { return (long)lseek(fd, 0, SEEK_CUR); }
static long CDECL sh__filelength(int fd) { struct stat st; return fstat(fd, &st) ? -1 : (long)st.st_size; }
static int  CDECL sh__chsize(int fd, long sz) { return ftruncate(fd, sz); }
static intptr_t CDECL sh__get_osfhandle(int fd) { return fd; }
static int  CDECL sh__setmode(int fd, int mode) { (void)fd; (void)mode; return 0x4000; }
static int  CDECL sh__commit(int fd) { return fsync(fd); }
static int  CDECL sh__unlink(const char *p) { return unlink(WP(p)); }

/* MSVC struct _stat: dev,ino,mode,nlink,uid,gid,rdev(all short bar dev/rdev),
 * off, then atime/mtime/ctime as 32-bit time_t. 36 bytes. */
typedef struct { uint32_t st_dev; uint16_t st_ino, st_mode; int16_t st_nlink,
    st_uid, st_gid; uint32_t st_rdev; int32_t st_size, atime, mtime, ctime; } MsvcStat;
static void fill_msvc_stat(MsvcStat *d, const struct stat *s)
{
    memset(d, 0, sizeof *d);
    d->st_mode = (uint16_t)s->st_mode; d->st_nlink = (int16_t)s->st_nlink;
    d->st_size = (int32_t)s->st_size;
    d->atime = (int32_t)s->st_atime; d->mtime = (int32_t)s->st_mtime; d->ctime = (int32_t)s->st_ctime;
}
static int CDECL sh__fstat(int fd, MsvcStat *d) { struct stat s; if (fstat(fd, &s)) return -1; fill_msvc_stat(d, &s); return 0; }
static int CDECL sh__stat(const char *p, MsvcStat *d) { struct stat s; if (stat(WP(p), &s)) return -1; fill_msvc_stat(d, &s); return 0; }

/* ----------------------------------------------------- C++ / math / misc CRT */
static void *CDECL sh_op_new(size_t n) { return malloc(n ? n : 1); }
static void  CDECL sh_op_delete(void *p) { free(p); }
int _adj_fdiv_r = 0;                        /* FDIV-fix helper (data)          */

/* _ftol: convert st(0) to a truncated 64-bit int in edx:eax, popping st(0). */
__attribute__((naked)) long long _vc6_ftol(void)
{
    __asm__ volatile(
        "sub $8, %esp\n\t"
        "fnstcw 6(%esp)\n\t"
        "movzwl 6(%esp), %eax\n\t"
        "orb $0x0C, %ah\n\t"           /* round toward zero */
        "movw %ax, 4(%esp)\n\t"
        "fldcw 4(%esp)\n\t"
        "fistpll (%esp)\n\t"
        "fldcw 6(%esp)\n\t"
        "mov (%esp), %eax\n\t"
        "mov 4(%esp), %edx\n\t"
        "add $8, %esp\n\t"
        "ret\n\t");
}

/* Minimal setjmp/longjmp (MSVC jmp_buf is >= 24 bytes; we use 6 dwords). */
__attribute__((naked)) int _vc6_setjmp3(void)
{
    __asm__ volatile(
        "mov 4(%esp), %ecx\n\t"        /* jmp_buf */
        "mov %ebp, 0(%ecx)\n\t"
        "mov %ebx, 4(%ecx)\n\t"
        "mov %edi, 8(%ecx)\n\t"
        "mov %esi, 12(%ecx)\n\t"
        "lea 4(%esp), %eax\n\t"        /* caller's esp after the call returns */
        "mov %eax, 16(%ecx)\n\t"
        "mov 0(%esp), %eax\n\t"        /* return address */
        "mov %eax, 20(%ecx)\n\t"
        "xor %eax, %eax\n\t"
        "ret\n\t");
}
__attribute__((naked)) void _vc6_longjmp(void)
{
    __asm__ volatile(
        "mov 4(%esp), %ecx\n\t"        /* jmp_buf */
        "mov 8(%esp), %eax\n\t"        /* value   */
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "mov $1, %eax\n\t"
        "1:\n\t"
        "mov 0(%ecx), %ebp\n\t"
        "mov 4(%ecx), %ebx\n\t"
        "mov 8(%ecx), %edi\n\t"
        "mov 12(%ecx), %esi\n\t"
        "mov 16(%ecx), %esp\n\t"
        "jmp *20(%ecx)\n\t");
}

static void CDECL sh__atoldbl(void *ld, const char *s) { if (ld) memset(ld, 0, 10); (void)s; }
static int  CDECL sh__futime(int fd, void *t) { (void)fd; (void)t; return 0; }

/* __unDName: leave the name mangled (diagnostics only). */
static char *CDECL sh___unDName(char *out, const char *name, int maxlen, void *(CDECL *alloc)(size_t), void *fr, unsigned flags)
{
    (void)fr; (void)flags;
    size_t n = strlen(name);
    char *dst = out ? out : (char *)(alloc ? alloc(n + 1) : malloc(n + 1));
    if (!dst) return 0;
    if (out && maxlen > 0) { strncpy(dst, name, maxlen - 1); dst[maxlen - 1] = 0; }
    else strcpy(dst, name);
    return dst;
}

/* wide strings: MSVC wchar_t is 16-bit, so glibc's 32-bit wcs* won't do. */
static size_t   CDECL sh_wcslen(const uint16_t *s) { size_t n = 0; while (s[n]) n++; return n; }
static int      CDECL sh_wcscmp(const uint16_t *a, const uint16_t *b) { while (*a && *a == *b) { a++; b++; } return (int)*a - (int)*b; }
static uint16_t*CDECL sh_wcscpy(uint16_t *d, const uint16_t *s) { uint16_t *r = d; while ((*d++ = *s++)) ; return r; }
static uint16_t*CDECL sh_wcscat(uint16_t *d, const uint16_t *s) { uint16_t *r = d; while (*d) d++; while ((*d++ = *s++)) ; return r; }
static int      CDECL sh_wcsncmp(const uint16_t *a, const uint16_t *b, size_t n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (int)*a - (int)*b : 0; }
static int      CDECL sh_swprintf(uint16_t *buf, const uint16_t *fmt, ...) { (void)fmt; if (buf) buf[0] = 0; return 0; }

static char *CDECL sh__strdate(char *b) { time_t t = time(0); struct tm *m = localtime(&t);
    sprintf(b, "%02d/%02d/%02d", m->tm_mon + 1, m->tm_mday, m->tm_year % 100); return b; }
static char *CDECL sh__strtime(char *b) { time_t t = time(0); struct tm *m = localtime(&t);
    sprintf(b, "%02d:%02d:%02d", m->tm_hour, m->tm_min, m->tm_sec); return b; }

/* ----------------------------------------------------------------- spawn    */
extern int vc6_spawn(int wait_mode, const char *file, char *const *argv);
static int CDECL sh__spawnvp(int mode, const char *cmd, char *const *argv) { return vc6_spawn(mode == 0 ? 0 : 1, cmd, argv); }
static int CDECL sh__spawnv (int mode, const char *cmd, char *const *argv) { return vc6_spawn(mode == 0 ? 0 : 1, cmd, argv); }

/* ------------------------------------------------------------------ errno   */
static int s_errno;
static unsigned long s_doserrno;
static int  *CDECL sh__errno(void) { return &s_errno; }
static unsigned long *CDECL sh___doserrno(void) { return &s_doserrno; }
static char *CDECL sh__strerror(const char *pfx)
{
    static char buf[512];
    snprintf(buf, sizeof buf, "%s%s%s", pfx ? pfx : "", pfx ? ": " : "", strerror(s_errno));
    return buf;
}

/* -------------------------------------------------------------------- env   */
static int CDECL sh__putenv(const char *s) { return putenv(strdup(s)); }
static void CDECL sh__searchenv(const char *file, const char *var, char *buf)
{
    buf[0] = 0;
    if (!access(file, F_OK)) { strcpy(buf, file); return; }
    const char *paths = getenv(var);
    if (!paths) return;
    char tmp[8192]; strncpy(tmp, paths, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    for (char *tok = strtok(tmp, ";:"); tok; tok = strtok(0, ";:")) {
        char cand[4096]; snprintf(cand, sizeof cand, "%s/%s", tok, file);
        if (!access(cand, F_OK)) { strcpy(buf, cand); return; }
    }
}

/* -------------------------------------------------------------------- temp  */
static char *CDECL sh__mktemp(char *t)
{
    size_t len = strlen(t), nx = 0;
    while (nx < len && (t[len-1-nx] == 'X' || t[len-1-nx] == 'x')) nx++;
    if (!nx) return t;
    static unsigned ctr = 1;
    for (int tries = 0; tries < 8192; tries++) {
        unsigned v = (unsigned)getpid() * 8192u + ctr++;
        for (size_t i = 0; i < nx; i++) { t[len-nx+i] = (char)('a' + v % 26); v /= 26; }
        if (access(t, F_OK) != 0) return t;
    }
    return t;
}
static char *CDECL sh__tempnam(const char *dir, const char *pfx)
{
    const char *d = dir && *dir ? dir : (getenv("TMP") ? getenv("TMP") : "/tmp");
    char buf[4096];
    static unsigned ctr = 1;
    for (int tries = 0; tries < 8192; tries++) {
        snprintf(buf, sizeof buf, "%s/%s%x%x", d, pfx ? pfx : "t", getpid(), ctr++);
        if (access(buf, F_OK) != 0) return strdup(buf);
    }
    return strdup(buf);
}

/* ------------------------------------------------------------------- paths  */
static void CDECL sh__splitpath(const char *path, char *drive, char *dir,
                                char *fname, char *ext)
{
    const char *p = path ? path : "";
    if (drive) drive[0] = 0;
    if (p[0] && p[1] == ':') { if (drive) { drive[0]=p[0]; drive[1]=':'; drive[2]=0; } p += 2; }
    const char *sep = 0, *q;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') sep = q;
    const char *base = sep ? sep + 1 : p;
    if (dir) { size_t n = sep ? (size_t)(sep + 1 - p) : 0; memcpy(dir, p, n); dir[n] = 0; }
    const char *dot = 0;
    for (q = base; *q; q++) if (*q == '.') dot = q;
    if (fname) { size_t n = dot ? (size_t)(dot - base) : strlen(base); memcpy(fname, base, n); fname[n] = 0; }
    if (ext) { if (dot) strcpy(ext, dot); else ext[0] = 0; }
}
static void CDECL sh__makepath(char *path, const char *drive, const char *dir,
                               const char *fname, const char *ext)
{
    path[0] = 0;
    if (drive && drive[0]) strcat(path, drive);
    if (dir && dir[0])     strcat(path, dir);
    if (fname && fname[0]) strcat(path, fname);
    if (ext && ext[0]) { if (ext[0] != '.') strcat(path, "."); strcat(path, ext); }
}
static char *CDECL sh__fullpath(char *abs, const char *rel, size_t maxlen)
{
    char tmp[4096];
    if (!realpath(rel, tmp)) { if (abs) { strncpy(abs, rel, maxlen); abs[maxlen?maxlen-1:0]=0; return abs; } return 0; }
    if (!abs) return strdup(tmp);
    strncpy(abs, tmp, maxlen); abs[maxlen ? maxlen - 1 : 0] = 0; return abs;
}
static char *CDECL sh__getcwd(char *buf, int size)
{ return buf ? getcwd(buf, size) : getcwd(0, 0); }
static int CDECL sh__access(const char *path, int mode)
{ return access(WP(path), mode & 6); }   /* MSVC 00/02/04/06 <-> F/W/R/RW */

/* ---------------------------------------------------------------- kernel32  */
/* Win32 is __stdcall (WINAPI). Minimal typedefs to keep the signatures honest. */
typedef void    *HANDLE;
typedef uint32_t DWORD;
typedef int      BOOL;

static DWORD s_last_error = 0;

static BOOL  STDCALL sh_SetConsoleCtrlHandler(void *h, BOOL add) { (void)h; (void)add; return 1; }
static DWORD STDCALL sh_GetVersion(void) { return 0x05650004u; }   /* NT 4.0 build 1381 */
static DWORD STDCALL sh_GetLastError(void) { return s_last_error; }
static void  STDCALL sh_SetLastError(DWORD e) { s_last_error = e; }
static DWORD STDCALL sh_SetErrorMode(DWORD m) { (void)m; return 0; }
static HANDLE STDCALL sh_GetCurrentProcess(void) { return (HANDLE)(uintptr_t)-1; }
static DWORD STDCALL sh_GetTickCount(void) { return 0; }
static void  STDCALL sh_Sleep(DWORD) {}
static BOOL  STDCALL sh_DisableThreadLibraryCalls(HANDLE) { return 1; }

extern uint32_t vc6_loadlibrary(const char *name);
extern uint32_t vc6_getproc(uint32_t hmod, const char *name);
extern uint32_t vc6_module_base(const char *name);
static HANDLE STDCALL sh_GetModuleHandleA(const char *name) { return (HANDLE)(uintptr_t)vc6_module_base(name); }
static HANDLE STDCALL sh_LoadLibraryA(const char *name) { return (HANDLE)(uintptr_t)vc6_loadlibrary(name); }
static void  *STDCALL sh_GetProcAddress(HANDLE h, const char *name) { return (void *)(uintptr_t)vc6_getproc((uint32_t)(uintptr_t)h, name); }
static BOOL   STDCALL sh_FreeLibrary(HANDLE h) { (void)h; return 1; }

/* The command line, built once from the pass argv. */
static char *sh_cmdline(void)
{
    static char line[8192]; static int built;
    if (!built) {
        built = 1; line[0] = 0;
        for (int i = 0; i < vc6_argc && vc6_argv; i++) {
            if (i) strncat(line, " ", sizeof line - strlen(line) - 1);
            strncat(line, vc6_argv[i], sizeof line - strlen(line) - 1);
        }
    }
    return line;
}
static char *STDCALL sh_GetCommandLineA(void) { return sh_cmdline(); }

/* GetSystemInfo / GlobalMemoryStatus: enough shape for the CRT's sizing. */
static void STDCALL sh_GetSystemInfo(void *si)
{
    if (si) { memset(si, 0, 36); ((DWORD *)si)[7] = 0x1000; /* dwPageSize @ +0x1c... */
              *(DWORD *)((char *)si + 4) = 0x1000;          /* dwPageSize (offset 4) */ }
}
static void STDCALL sh_GlobalMemoryStatus(void *ms)
{
    if (ms) { memset(ms, 0, 32); ((DWORD *)ms)[0] = 32;
              ((DWORD *)ms)[2] = 0x40000000; ((DWORD *)ms)[3] = 0x20000000; }
}

/* Console screen buffer info: report an 80x300 console so usage wraps at 80. */
static BOOL STDCALL sh_GetConsoleScreenBufferInfo(HANDLE, void *info)
{
    if (info) { memset(info, 0, 22);
        int16_t *w = (int16_t *)info;
        w[0] = 80; w[1] = 300;        /* dwSize   X,Y */
        w[5] = 79; w[6] = 24;         /* srWindow right,bottom-ish */
    }
    return 1;
}

/* Memory. VirtualAlloc/Free/Protect over mmap; the compiler asks for big
 * committed regions and relies on writability. */
/* We commit-on-reserve (a reserved region is already backed), so a later
 * MEM_COMMIT (0x1000) without MEM_RESERVE (0x2000) of a known address is just
 * that address. MEM_RESERVE maps a fresh region. */
static void *STDCALL sh_VirtualAlloc(void *addr, DWORD size, DWORD type, DWORD prot)
{
    (void)prot;
    void *p;
    if ((type & 0x1000) && !(type & 0x2000) && addr) p = addr;   /* commit of reserved */
    else {
        p = mmap(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | (addr ? MAP_FIXED_NOREPLACE : 0), -1, 0);
        if (p == MAP_FAILED && addr)
            p = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) p = 0;
    }
    if (getenv("VC6_VMLOG")) fprintf(stderr, "vc6host: VirtualAlloc(%p,0x%x,0x%x) = %p\n", addr, size, type, p);
    return p;
}
static BOOL STDCALL sh_VirtualFree(void *addr, DWORD size, DWORD type) { (void)addr;(void)size;(void)type; return 1; }
static BOOL STDCALL sh_VirtualProtect(void *a, DWORD s, DWORD p, DWORD *old) { (void)a;(void)s;(void)p; if (old) *old = 0x40; return 1; }
static void *STDCALL sh_LocalAlloc(DWORD flags, DWORD n) { void *p = malloc(n ? n : 1); if (p && (flags & 0x40)) memset(p, 0, n); return p; }

static DWORD STDCALL sh_GetTempPathA(DWORD len, char *buf)
{
    const char *t = getenv("TMP"); if (!t) t = getenv("TEMP"); if (!t) t = "/tmp";
    char p[4096]; int n = snprintf(p, sizeof p, "%s/", t);
    if (buf && (DWORD)n < len) strcpy(buf, p);
    return (DWORD)n;
}
static DWORD STDCALL sh_GetTempFileNameA(const char *path, const char *pfx, unsigned uniq, char *buf)
{
    static unsigned ctr = 1;
    unsigned u = uniq ? uniq : ((unsigned)getpid() * 100u + ctr++);
    snprintf(buf, 260, "%s%.3s%x.tmp", path && *path ? path : "/tmp/", pfx ? pfx : "vc", u);
    if (!uniq) { int fd = open(buf, O_CREAT | O_RDWR, 0666); if (fd >= 0) close(fd); }
    return u & 0xFFFF;
}
static DWORD STDCALL sh_GetDiskFreeSpaceA(const char *r, DWORD *spc, DWORD *bps, DWORD *fc, DWORD *tc)
{ (void)r; if (spc)*spc=8; if (bps)*bps=512; if (fc)*fc=0x100000; if (tc)*tc=0x200000; return 1; }
static BOOL STDCALL sh_SetEnvironmentVariableA(const char *name, const char *val)
{ if (val) return setenv(name, val, 1) == 0; return unsetenv(name) == 0; }
static DWORD STDCALL sh_GetEnvironmentVariableA(const char *name, char *buf, DWORD len)
{ const char *v = getenv(name); if (!v) return 0; DWORD n = (DWORD)strlen(v);
  if (buf && n < len) strcpy(buf, v); return buf && n < len ? n : n + 1; }

/* ----------------------------------------------------------------- user32   */
extern int vc6_load_string(uint32_t hinst, uint32_t id, char *buf, int maxch);
static int STDCALL sh_LoadStringA(void *hinst, uint32_t id, char *buf, int maxch)
{ return vc6_load_string((uint32_t)(uintptr_t)hinst, id, buf, maxch); }

/* -------------------------------------------------- kernel32 files/handles  */
enum { H_FILE = 1, H_SEM, H_EVENT, H_MAP };
typedef struct { int type, fd; uint32_t map_size; } HObj;
static HObj s_htab[2048];
static int  s_hn = 1;                        /* index 0 == NULL handle */
#define INVALID_HANDLE ((HANDLE)(intptr_t)-1)
static HANDLE h_alloc(int type, int fd) { if (s_hn >= 2048) return INVALID_HANDLE;
    int i = s_hn++; s_htab[i].type = type; s_htab[i].fd = fd; s_htab[i].map_size = 0; return (HANDLE)(intptr_t)i; }
static HObj *h_obj(HANDLE h) { intptr_t i = (intptr_t)h; return (i <= 0 || i >= s_hn) ? 0 : &s_htab[i]; }

static HANDLE STDCALL sh_CreateFileA(const char *name, DWORD access, DWORD share,
        void *sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    (void)share; (void)sa; (void)flags; (void)tmpl;
    int of, rd = access & 0x80000000u, wr = access & 0x40000000u;
    of = (rd && wr) ? O_RDWR : wr ? O_WRONLY : O_RDONLY;
    switch (disp) { case 1: of |= O_CREAT|O_EXCL; break; case 2: of |= O_CREAT|O_TRUNC; break;
                    case 4: of |= O_CREAT; break; case 5: of |= O_TRUNC; break; default: break; }
    int fd = open(WP(name), of, 0666);
    return fd < 0 ? INVALID_HANDLE : h_alloc(H_FILE, fd);
}
static BOOL STDCALL sh_CloseHandle(HANDLE h) { HObj *o = h_obj(h); if (!o) return 0;
    if (o->type == H_FILE && o->fd >= 0) close(o->fd); o->type = 0; return 1; }
static BOOL STDCALL sh_ReadFile(HANDLE h, void *b, DWORD n, DWORD *done, void *ov)
{ (void)ov; HObj *o = h_obj(h); if (!o) return 0; ssize_t r = read(o->fd, b, n); if (done) *done = r < 0 ? 0 : (DWORD)r; return r >= 0; }
static BOOL STDCALL sh_WriteFile(HANDLE h, const void *b, DWORD n, DWORD *done, void *ov)
{ (void)ov; HObj *o = h_obj(h); if (!o) return 0; ssize_t r = write(o->fd, b, n); if (done) *done = r < 0 ? 0 : (DWORD)r; return r >= 0; }
static DWORD STDCALL sh_SetFilePointer(HANDLE h, long dist, long *distHigh, DWORD method)
{ (void)distHigh; HObj *o = h_obj(h); if (!o) return 0xFFFFFFFFu; return (DWORD)lseek(o->fd, dist, method); }
static DWORD STDCALL sh_GetFileSize(HANDLE h, DWORD *high) { HObj *o = h_obj(h); if (!o) return 0xFFFFFFFFu;
    struct stat st; if (fstat(o->fd, &st)) return 0xFFFFFFFFu; if (high) *high = 0; return (DWORD)st.st_size; }
static BOOL STDCALL sh_SetEndOfFile(HANDLE h) { HObj *o = h_obj(h); if (!o) return 0;
    return ftruncate(o->fd, lseek(o->fd, 0, SEEK_CUR)) == 0; }
static void ft_from_unix(time_t t, DWORD *out) { uint64_t v = ((uint64_t)t + 11644473600ULL) * 10000000ULL;
    if (out) { out[0] = (DWORD)v; out[1] = (DWORD)(v >> 32); } }
static BOOL STDCALL sh_GetFileTime(HANDLE h, DWORD *ct, DWORD *at, DWORD *wt) { HObj *o = h_obj(h); if (!o) return 0;
    struct stat st; if (fstat(o->fd, &st)) return 0; ft_from_unix(st.st_ctime, ct); ft_from_unix(st.st_atime, at); ft_from_unix(st.st_mtime, wt); return 1; }
static BOOL STDCALL sh_SetFileTime(HANDLE h, const void *a, const void *b, const void *c) { (void)h;(void)a;(void)b;(void)c; return 1; }

static HANDLE STDCALL sh_CreateFileMappingA(HANDLE hf, void *sa, DWORD prot, DWORD szHi, DWORD szLo, const char *name)
{ (void)sa; (void)prot; (void)szHi; (void)name; HObj *o = h_obj(hf);
  HANDLE m = h_alloc(H_MAP, o ? o->fd : -1); HObj *mo = h_obj(m); if (mo) mo->map_size = szLo; return m; }
static void *STDCALL sh_MapViewOfFileEx(HANDLE hm, DWORD access, DWORD offHi, DWORD offLo, DWORD size, void *base)
{
    (void)access; (void)offHi; HObj *o = h_obj(hm); if (!o) return 0;
    size_t len = size ? size : o->map_size;
    if (!len && o->fd >= 0) { struct stat st; if (!fstat(o->fd, &st)) len = st.st_size; }
    void *p = mmap(base, len ? len : 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | (base ? MAP_FIXED_NOREPLACE : 0), o->fd, offLo);
    return p == MAP_FAILED ? 0 : p;
}
static BOOL STDCALL sh_UnmapViewOfFile(void *p) { (void)p; return 1; }   /* leak-ok for a short compile */

static HANDLE STDCALL sh_CreateSemaphoreA(void *sa, long i, long m, const char *n) { (void)sa;(void)i;(void)m;(void)n; return h_alloc(H_SEM, -1); }
static BOOL   STDCALL sh_ReleaseSemaphore(HANDLE h, long n, long *prev) { (void)h;(void)n; if (prev) *prev = 0; return 1; }
static DWORD  STDCALL sh_WaitForSingleObject(HANDLE h, DWORD ms) { (void)h;(void)ms; return 0; }
static HANDLE STDCALL sh_OpenEventA(DWORD a, BOOL b, const char *n) { (void)a;(void)b;(void)n; return h_alloc(H_EVENT, -1); }
static BOOL   STDCALL sh_SetEvent(HANDLE h) { (void)h; return 1; }
static DWORD  STDCALL sh_SetHandleCount(DWORD n) { return n; }
static long   STDCALL sh_InterlockedExchange(long *p, long v) { long o = *p; *p = v; return o; }
static void   STDCALL sh_ExitProcess(unsigned code) { _exit(code); }

static int STDCALL sh_MultiByteToWideChar(DWORD cp, DWORD f, const char *mb, int mbc, uint16_t *wc, int wcc)
{ (void)cp;(void)f; int n = mbc < 0 ? (int)strlen(mb) + 1 : mbc; if (!wcc) return n;
  int k = 0; for (int i = 0; i < n && k < wcc; i++) wc[k++] = (unsigned char)mb[i]; return k; }
static int STDCALL sh_WideCharToMultiByte(DWORD cp, DWORD f, const uint16_t *wc, int wcc, char *mb, int mbc, const char *d, BOOL *u)
{ (void)cp;(void)f;(void)d;(void)u; int n = wcc < 0 ? (int)sh_wcslen(wc) + 1 : wcc; if (!mbc) return n;
  int k = 0; for (int i = 0; i < n && k < mbc; i++) mb[k++] = (char)wc[i]; return k; }
static DWORD STDCALL sh_FormatMessageA(DWORD f, const void *s, DWORD id, DWORD lang, char *buf, DWORD size, void *args)
{ (void)f;(void)s;(void)lang;(void)args; int n = snprintf(buf, size, "error %u", id); return n < 0 ? 0 : (DWORD)n; }
static void STDCALL sh_RaiseException(DWORD code, DWORD f, DWORD n, const void *a)
{ (void)f;(void)n;(void)a;
  fprintf(stderr, "vc6host: RaiseException 0x%08x from ret=%p arg0=%p\n",
          code, __builtin_return_address(0), a && n ? (void *)(uintptr_t)((const DWORD *)a)[0] : 0);
  _exit(4); }
static void STDCALL sh_DebugBreak(void) { fprintf(stderr, "vc6host: DebugBreak\n"); _exit(4); }

static int CDECL sh_remove(const char *p) { return remove(WP(p)); }
static int CDECL sh_rename(const char *a, const char *b) { char t[4096]; winpath(a,t,sizeof t);
    char u[4096]; winpath(b,u,sizeof u); return rename(t, u); }

/* --------------------------------------------------- heap / more kernel32   */
static HANDLE STDCALL sh_HeapCreate(DWORD a, DWORD b, DWORD c) { (void)a;(void)b;(void)c; return (HANDLE)(intptr_t)1; }
static void  *STDCALL sh_HeapAlloc(HANDLE h, DWORD flags, DWORD n) { (void)h; void *p = malloc(n ? n : 1); if (p && (flags & 8)) memset(p, 0, n); return p; }
static void  *STDCALL sh_HeapReAlloc(HANDLE h, DWORD flags, void *p, DWORD n) { (void)h;(void)flags; return realloc(p, n ? n : 1); }
static BOOL   STDCALL sh_HeapFree(HANDLE h, DWORD flags, void *p) { (void)h;(void)flags; free(p); return 1; }
static BOOL   STDCALL sh_HeapDestroy(HANDLE h) { (void)h; return 1; }
static HANDLE STDCALL sh_GetProcessHeap(void) { return (HANDLE)(intptr_t)1; }
static HANDLE STDCALL sh_GetCurrentThread(void) { return (HANDLE)(intptr_t)-2; }
static BOOL   STDCALL sh_SetThreadPriority(HANDLE h, int p) { (void)h;(void)p; return 1; }

static BOOL STDCALL sh_CopyFileA(const char *src, const char *dst, BOOL failIfExists)
{
    char s[4096], d[4096]; winpath(src, s, sizeof s); winpath(dst, d, sizeof d);
    int in = open(s, O_RDONLY); if (in < 0) return 0;
    int out = open(d, O_WRONLY | O_CREAT | (failIfExists ? O_EXCL : O_TRUNC), 0666);
    if (out < 0) { close(in); return 0; }
    char buf[65536]; ssize_t r; BOOL ok = 1;
    while ((r = read(in, buf, sizeof buf)) > 0) if (write(out, buf, r) != r) { ok = 0; break; }
    close(in); close(out); return ok;
}
static HANDLE STDCALL sh_FindFirstFileA(const char *pat, void *data) { (void)pat; (void)data; s_last_error = 2; return INVALID_HANDLE; }
static BOOL   STDCALL sh_FindNextFileA(HANDLE h, void *data) { (void)h; (void)data; return 0; }
static BOOL   STDCALL sh_FindClose(HANDLE h) { (void)h; return 1; }
static DWORD  STDCALL sh_SearchPathA(const char *path, const char *file, const char *ext, DWORD len, char *buf, char **filepart)
{ (void)path;(void)file;(void)ext;(void)len;(void)buf; if (filepart) *filepart = 0; return 0; }
static BOOL   STDCALL sh_GetFileInformationByHandle(HANDLE h, void *info)
{ HObj *o = h_obj(h); if (!o) return 0; struct stat st; if (fstat(o->fd, &st)) return 0;
  memset(info, 0, 52); ((DWORD *)info)[7] = (DWORD)st.st_size; return 1; }  /* nFileSizeLow @ +0x1c */
static void   STDCALL sh_GetSystemTime(void *st) { if (!st) return; time_t t = time(0); struct tm *m = gmtime(&t);
  uint16_t *w = (uint16_t *)st; w[0]=m->tm_year+1900; w[1]=m->tm_mon+1; w[2]=m->tm_wday; w[3]=m->tm_mday;
  w[4]=m->tm_hour; w[5]=m->tm_min; w[6]=m->tm_sec; w[7]=0; }
static BOOL   STDCALL sh_SystemTimeToFileTime(const void *st, DWORD *ft)
{ (void)st; ft_from_unix(time(0), ft); return 1; }

/* advapi32 registry: report "not found" so callers fall back to defaults. */
static long STDCALL sh_RegOpenKeyExA(void *h, const char *sub, DWORD o, DWORD sam, void *out) { (void)h;(void)sub;(void)o;(void)sam; if (out) *(void **)out = 0; return 2; }
static long STDCALL sh_RegQueryValueExA(void *h, const char *v, DWORD *r, DWORD *ty, void *d, DWORD *cb) { (void)h;(void)v;(void)r;(void)ty;(void)d;(void)cb; return 2; }
static long STDCALL sh_RegCloseKey(void *h) { (void)h; return 0; }

/* a few more MSVCRT for mspdb60 */
static int  CDECL sh__getmbcp(void) { return 0; }
static int  CDECL sh__ismbblead(unsigned c) { (void)c; return 0; }
static int  CDECL sh__memicmp(const void *a, const void *b, size_t n) { return strncasecmp((const char *)a, (const char *)b, n); }
static int  CDECL sh__open_osfhandle(intptr_t h, int flags) { (void)flags; HObj *o = h_obj((HANDLE)h); return o ? o->fd : (int)h; }
static int  CDECL sh__utime2(const char *p, void *t) { (void)p; (void)t; return 0; }
static unsigned s_osver_val = 0;

/* --------------------------------------------------------------- the table  */

typedef struct { const char *module, *name; const void *fn; } Vc6Export;

#define M(nm, fn) { "MSVCRT.dll", nm, (const void *)(fn) }
#define K(nm, fn) { "KERNEL32.dll", nm, (const void *)(fn) }
#define U(nm, fn) { "USER32.dll", nm, (const void *)(fn) }

static const Vc6Export vc6_exports[] = {
    /* CRT startup */
    M("__set_app_type", sh___set_app_type),
    M("__setusermatherr", sh___setusermatherr),
    M("__p__fmode", sh___p__fmode),
    M("__p__commode", sh___p__commode),
    M("__p___initenv", sh___p___initenv),
    M("__p__pgmptr", sh___p__pgmptr),
    M("_controlfp", sh__controlfp),
    M("_initterm", sh__initterm),
    M("__getmainargs", sh___getmainargs),
    M("_XcptFilter", sh__XcptFilter),
    M("_except_handler3", sh__except_handler3),
    M("__CxxFrameHandler", sh___CxxFrameHandler),
    M("__dllonexit", sh___dllonexit),
    M("_onexit", sh__onexit),
    M("_amsg_exit", sh__amsg_exit),
    M("_purecall", sh__purecall),
    M("_setmbcp", sh__setmbcp),
    M("exit", exit),
    M("_exit", _exit),
    /* CRT data exports (resolve to the object's address) */
    M("_iob", s_iob),
    M("_adjust_fdiv", &_adjust_fdiv),
    M("__mb_cur_max", &__mb_cur_max),
    /* stdio (MSVC FILE-aware) */
    M("fwrite", sh_fwrite), M("fread", sh_fread),
    M("fputc", sh_fputc), M("fputs", sh_fputs), M("fflush", sh_fflush),
    M("fprintf", sh_fprintf), M("vfprintf", sh_vfprintf),
    M("printf", sh_printf), M("vprintf", sh_vprintf),
    M("puts", sh_puts), M("putchar", sh_putchar),
    M("putc", sh_putc), M("getc", sh_getc), M("getchar", sh_getchar),
    M("_flsbuf", sh__flsbuf), M("_filbuf", sh__filbuf),
    M("fopen", sh_fopen), M("_fsopen", sh__fsopen), M("fclose", sh_fclose),
    M("freopen", sh_freopen), M("fseek", sh_fseek), M("ftell", sh_ftell),
    M("rewind", sh_rewind), M("setvbuf", sh_setvbuf), M("fgets", sh_fgets),
    M("_flushall", sh_flushall), M("_fcloseall", sh_fcloseall),
    M("remove", sh_remove), M("rename", sh_rename),
    /* string / mem / fmt / math -- glibc is __cdecl and compatible */
    M("malloc", malloc), M("free", free), M("calloc", calloc), M("realloc", realloc),
    M("memcpy", memcpy), M("memmove", memmove), M("memset", memset),
    M("strcpy", strcpy), M("strncpy", strncpy), M("strcat", strcat), M("strncat", strncat),
    M("strcmp", strcmp), M("strncmp", strncmp), M("strlen", strlen),
    M("strchr", strchr), M("strrchr", strrchr), M("strstr", strstr), M("strpbrk", strpbrk),
    M("strcspn", strcspn), M("strtok", strtok),
    M("strtol", strtol), M("strtoul", strtoul), M("atoi", atoi), M("atol", atol),
    M("_stricmp", strcasecmp), M("_strnicmp", strncasecmp), M("_strdup", strdup),
    M("sprintf", sprintf), M("_snprintf", snprintf), M("_vsnprintf", vsnprintf),
    M("sscanf", sscanf), M("qsort", qsort), M("bsearch", bsearch),
    M("srand", srand), M("rand", rand),
    M("tolower", tolower), M("toupper", toupper),
    M("isalnum", isalnum), M("isalpha", isalpha), M("isdigit", isdigit),
    M("isprint", isprint), M("ispunct", ispunct), M("isspace", isspace),
    M("isxdigit", isxdigit),
    M("getenv", getenv),
    M("_errno", sh__errno), M("__doserrno", sh___doserrno),
    M("strerror", strerror), M("_strerror", sh__strerror),
    M("_spawnvp", sh__spawnvp), M("_spawnv", sh__spawnv),
    /* C++ / math / misc CRT */
    M("??2@YAPAXI@Z", sh_op_new), M("??3@YAXPAX@Z", sh_op_delete),
    M("_ftol", _vc6_ftol), M("_adj_fdiv_r", &_adj_fdiv_r),
    M("_setjmp3", _vc6_setjmp3), M("longjmp", _vc6_longjmp),
    M("_atoldbl", sh__atoldbl), M("_futime", sh__futime),
    M("__unDName", sh___unDName),
    M("wcslen", sh_wcslen), M("wcscmp", sh_wcscmp), M("wcscpy", sh_wcscpy),
    M("wcscat", sh_wcscat), M("wcsncmp", sh_wcsncmp), M("swprintf", sh_swprintf),
    M("_strdate", sh__strdate), M("_strtime", sh__strtime),
    M("ceil", ceil), M("floor", floor), M("time", time), M("ctime", ctime),
    M("setlocale", setlocale), M("mbtowc", mbtowc),
    /* mbcs / ctype (single-byte) */
    M("_mbsncmp", sh__mbsncmp), M("_mbscmp", sh__mbscmp), M("_mbsicmp", sh__mbsicmp),
    M("_mbsnbcmp", sh__mbsnbcmp), M("_mbsnbicmp", sh__mbsnbicmp),
    M("_mbschr", sh__mbschr), M("_mbsrchr", sh__mbsrchr), M("_mbsstr", sh__mbsstr),
    M("_mbspbrk", sh__mbspbrk), M("_mbsspn", sh__mbsspn),
    M("_mbslwr", sh__mbslwr), M("_mbsupr", sh__mbsupr),
    M("_mbsinc", sh__mbsinc), M("_mbsdec", sh__mbsdec), M("_mbclen", sh__mbclen),
    M("_mbstrlen", sh__mbstrlen), M("_mbsnbcpy", sh__mbsnbcpy), M("_mbstok", sh__mbstok),
    M("_mbctolower", sh__mbctolower), M("_mbctoupper", sh__mbctoupper),
    M("_mbccpy", sh__mbccpy), M("_ismbcspace", sh__ismbcspace),
    M("_isctype", sh__isctype), M("_mbctype", sh_mbctype_tbl),
    M("_pctype", &sh_pctype), M("_osver", &s_osver_val),
    M("_getmbcp", sh__getmbcp), M("_ismbblead", sh__ismbblead),
    M("_memicmp", sh__memicmp), M("_open_osfhandle", sh__open_osfhandle),
    M("_utime", sh__utime2),
    /* string extras */
    M("_strlwr", sh__strlwr), M("_strupr", sh__strupr),
    M("_itoa", sh__itoa), M("_ltoa", sh__ltoa), M("_ultoa", sh__ultoa),
    /* env */
    M("_putenv", sh__putenv), M("_searchenv", sh__searchenv),
    /* temp */
    M("_mktemp", sh__mktemp), M("_tempnam", sh__tempnam),
    /* low I/O */
    M("_write", sh__write), M("_read", sh__read), M("_close", sh__close),
    M("_lseek", sh__lseek), M("_open", sh__open), M("_sopen", sh__sopen),
    M("_dup", sh__dup), M("_dup2", sh__dup2), M("_isatty", sh__isatty),
    M("_tell", sh__tell), M("_filelength", sh__filelength), M("_chsize", sh__chsize),
    M("_get_osfhandle", sh__get_osfhandle), M("_setmode", sh__setmode),
    M("_commit", sh__commit), M("_unlink", sh__unlink),
    M("_fstat", sh__fstat), M("_stat", sh__stat),
    /* paths */
    M("_splitpath", sh__splitpath), M("_makepath", sh__makepath),
    M("_fullpath", sh__fullpath), M("_getcwd", sh__getcwd), M("_access", sh__access),
    /* kernel32 */
    K("SetConsoleCtrlHandler", sh_SetConsoleCtrlHandler),
    K("GetVersion", sh_GetVersion),
    K("GetLastError", sh_GetLastError), K("SetLastError", sh_SetLastError),
    K("SetErrorMode", sh_SetErrorMode),
    K("GetCurrentProcess", sh_GetCurrentProcess),
    K("GetTickCount", sh_GetTickCount), K("Sleep", sh_Sleep),
    K("DisableThreadLibraryCalls", sh_DisableThreadLibraryCalls),
    K("GetModuleHandleA", sh_GetModuleHandleA),
    K("LoadLibraryA", sh_LoadLibraryA), K("GetProcAddress", sh_GetProcAddress),
    K("FreeLibrary", sh_FreeLibrary),
    K("GetCommandLineA", sh_GetCommandLineA),
    K("GetSystemInfo", sh_GetSystemInfo),
    K("GlobalMemoryStatus", sh_GlobalMemoryStatus),
    K("GetConsoleScreenBufferInfo", sh_GetConsoleScreenBufferInfo),
    K("VirtualAlloc", sh_VirtualAlloc), K("VirtualFree", sh_VirtualFree),
    K("VirtualProtect", sh_VirtualProtect), K("LocalAlloc", sh_LocalAlloc),
    /* kernel32 files / handles / mapping */
    K("CreateFileA", sh_CreateFileA), K("CloseHandle", sh_CloseHandle),
    K("ReadFile", sh_ReadFile), K("WriteFile", sh_WriteFile),
    K("SetFilePointer", sh_SetFilePointer), K("GetFileSize", sh_GetFileSize),
    K("SetEndOfFile", sh_SetEndOfFile), K("GetFileTime", sh_GetFileTime),
    K("SetFileTime", sh_SetFileTime),
    K("CreateFileMappingA", sh_CreateFileMappingA), K("MapViewOfFileEx", sh_MapViewOfFileEx),
    K("UnmapViewOfFile", sh_UnmapViewOfFile),
    K("CreateSemaphoreA", sh_CreateSemaphoreA), K("ReleaseSemaphore", sh_ReleaseSemaphore),
    K("WaitForSingleObject", sh_WaitForSingleObject), K("OpenEventA", sh_OpenEventA),
    K("SetEvent", sh_SetEvent), K("SetHandleCount", sh_SetHandleCount),
    K("InterlockedExchange", sh_InterlockedExchange), K("ExitProcess", sh_ExitProcess),
    K("MultiByteToWideChar", sh_MultiByteToWideChar), K("WideCharToMultiByte", sh_WideCharToMultiByte),
    K("FormatMessageA", sh_FormatMessageA), K("RaiseException", sh_RaiseException),
    K("DebugBreak", sh_DebugBreak),
    /* heap + more kernel32 (mspdb60) */
    K("HeapCreate", sh_HeapCreate), K("HeapAlloc", sh_HeapAlloc),
    K("HeapReAlloc", sh_HeapReAlloc), K("HeapFree", sh_HeapFree),
    K("HeapDestroy", sh_HeapDestroy), K("GetProcessHeap", sh_GetProcessHeap),
    K("GetCurrentThread", sh_GetCurrentThread), K("SetThreadPriority", sh_SetThreadPriority),
    K("CopyFileA", sh_CopyFileA), K("FindFirstFileA", sh_FindFirstFileA),
    K("FindNextFileA", sh_FindNextFileA), K("FindClose", sh_FindClose),
    K("SearchPathA", sh_SearchPathA), K("GetFileInformationByHandle", sh_GetFileInformationByHandle),
    K("GetSystemTime", sh_GetSystemTime), K("SystemTimeToFileTime", sh_SystemTimeToFileTime),
    { "ADVAPI32.dll", "RegOpenKeyExA", (const void *)sh_RegOpenKeyExA },
    { "ADVAPI32.dll", "RegQueryValueExA", (const void *)sh_RegQueryValueExA },
    { "ADVAPI32.dll", "RegCloseKey", (const void *)sh_RegCloseKey },
    K("GetTempPathA", sh_GetTempPathA), K("GetTempFileNameA", sh_GetTempFileNameA),
    K("GetDiskFreeSpaceA", sh_GetDiskFreeSpaceA),
    K("SetEnvironmentVariableA", sh_SetEnvironmentVariableA),
    K("GetEnvironmentVariableA", sh_GetEnvironmentVariableA),
    U("LoadStringA", sh_LoadStringA),
    { 0, 0, 0 },
};

const void *vc6_shim_lookup(const char *module, const char *name)
{
    iob_init();
    for (size_t i = 0; i < sizeof vc6_exports / sizeof vc6_exports[0]; i++) {
        const Vc6Export *e = &vc6_exports[i];
        if (e->name && !strcasecmp(e->module, module) && !strcmp(e->name, name))
            return e->fn;
    }
    return 0;
}
