/* crt.h -- the MSVC 6 C runtime that is statically linked into ArmyMen2.exe,
 * reconstructed from its disassembly.
 *
 * The image carries its own CRT: 231 functions from 0x00464416 to the end
 * of .text, 42,860 bytes, of which the game calls 44 directly. The native
 * build ran the game over glibc behind those names, and glibc is not that
 * CRT: qsort orders equal keys differently, the heap hands out different
 * addresses, printf formats floats to its own rules, the locale functions
 * answer for a different table. Every one of those is a frame the original
 * and the reconstruction can disagree on. This directory is the CRT itself,
 * function by function, each from its own body, so that the standalone and
 * native builds run the runtime the original runs.
 *
 * Names carry a crt_ prefix -- crt_qsort, crt_malloc -- because the host's
 * own libc is in the same process and its names cannot be taken. The game
 * reaches them through the ADDR_CRT_* seams src/inject/standalone.h
 * re-points, so the reconstruction's sources do not change. State lives
 * where the original keeps it: rand's seed is the image's word at
 * ADDR_RAND_SEED, not a private one, so a build that mixes original and
 * reconstructed code shares it.
 *
 * Conventions are the original's: cdecl throughout, int32_t for int, and
 * the comment before each declaration opens with the address it
 * reconstructs.
 */
#ifndef AM2_PLATFORM_CRT_H
#define AM2_PLATFORM_CRT_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (__cdecl *crt_compare_fn)(const void *, const void *);

/* MSVC's FILE: _ptr, _cnt, _base, _flag, _file, _charbuf, _bufsiz,
 * _tmpfname. sprintf makes one on its stack over the caller's buffer. */
typedef struct CRT_FILE {
    char    *ptr;
    int32_t  cnt;
    char    *base;
    int32_t  flag;
    int32_t  file;
    int32_t  charbuf;
    int32_t  bufsiz;
    char    *tmpfname;
} CRT_FILE;

/* ---- sort.cpp ---------------------------------------------------------- */

/* 0x004660B2. Sort `num` records of `width` bytes at `base`, as MSVC 6 does:
 * a quicksort over an explicit stack of pending ranges, taking the middle
 * record as the pivot and swapping it to the front, with ranges of eight or
 * fewer records finished by shortsort. Equal keys end up where this
 * algorithm leaves them, which is not where glibc's mergesort does, and the
 * game draws in the order it sorts. */
void __cdecl crt_qsort(void *base, uint32_t num, uint32_t width, crt_compare_fn compare);

/* 0x00466280. Binary search for `key` among `num` sorted records of `width`
 * bytes at `base`; the matching record, or NULL. The probe is the middle
 * of the half, biased low on an even count, and a range of one is settled
 * by a single compare. */
void *__cdecl crt_bsearch(const void *key, const void *base, uint32_t num,
                          uint32_t width, crt_compare_fn compare);

/* ---- string.cpp -------------------------------------------------------- */

/* 0x0046551C. Split `str` at any of `delims`, continuing from the previous
 * token when `str` is NULL; the resumption point is the image's own word
 * at ADDR_CRT_STRTOK_NEXT. */
char *__cdecl crt_strtok(char *str, const char *delims);

/* 0x004663B0. First occurrence of `c` in `str`, the terminator included. */
char *__cdecl crt_strchr(const char *str, int32_t c);

/* 0x00464D40. First occurrence of `sub` in `str`; `str` itself for an empty
 * `sub`. Tail-calls strchr for a one-character `sub`. */
char *__cdecl crt_strstr(const char *str, const char *sub);

/* 0x00465610. Copy at most `n` characters, padding with NULs to `n`. */
char *__cdecl crt_strncpy(char *dst, const char *src, uint32_t n);

/* 0x00465160. Compare at most `n` characters, unsigned and case-sensitive,
 * answering exactly -1, 0 or 1. orig.h had this as _strnicmp. */
int32_t __cdecl crt_strncmp(const char *a, const char *b, uint32_t n);

/* 0x00465F90. Case-insensitive compare over A..Z, the C locale's arm;
 * exactly -1, 0 or 1. */
int32_t __cdecl crt_stricmp(const char *a, const char *b);

/* 0x0046D7D6. Lower-case A..Z in place, the C locale's arm; answers `s`. */
char *__cdecl crt_strlwr(char *s);

/* ---- conv.cpp ---------------------------------------------------------- */

/* 0x0046601C. Optional whitespace, an optional sign, then decimal digits,
 * accumulated without any overflow check. atoi at 0x004660A7 is a call
 * to it. */
int32_t __cdecl crt_atol(const char *s);
int32_t __cdecl crt_atoi(const char *s);

/* 0x00465198, over strtoxl at 0x004651AF: base 0, 2..36, the 0x prefix,
 * overflow clamped to the signed limits with errno set to ERANGE, and
 * `endptr` left at `nptr` when no digit was read. */
int32_t __cdecl crt_strtol(const char *nptr, char **endptr, int32_t base);

/* ---- printf.cpp -------------------------------------------------------- */

/* 0x00468AD8. The printf engine over a FILE; the characters written, or
 * -1 once the stream refused one. */
int32_t __cdecl crt_output(CRT_FILE *stream, const char *format, va_list argptr);

/* 0x00465A45 and 0x00464CE2. */
int32_t __cdecl crt_vsprintf(char *buf, const char *format, va_list args);
int32_t __cdecl crt_sprintf(char *buf, const char *format, ...);

/* 0x004698F0. */
int32_t __cdecl crt_strlen(const char *s);
/* 0x00469CA0. */
char *__cdecl crt_strcpy(char *dst, const char *src);
/* 0x00469CB0. strcat. */
char *__cdecl crt_strcat(char *dst, const char *src);
/* 0x0046B420. strcmp: -1, 0 or 1. */
int32_t __cdecl crt_strcmp(const char *a, const char *b);
/* 0x00466D80. memcpy. */
void *__cdecl crt_memcpy(void *dst, const void *src, uint32_t n);
/* 0x00465710. memmove. */
void *__cdecl crt_memmove(void *dst, const void *src, uint32_t n);
/* 0x00469714 and the _isdigit the conversions use: the ctype table's
 * answers for one byte. */
int32_t __cdecl crt_tolower(int32_t c);
int32_t __cdecl crt_isdigit(int32_t c);

/* ---- fltcvt.cpp -------------------------------------------------------- */

/* 0x00466A3D. A double to its decimal text in `buf` for conversion `fmt`
 * ('e', 'f' or 'g') at `precision` digits, upper-case exponent when `caps`. */
void __cdecl crt_cfltcvt(const double *value, char *buf, int32_t fmt, int32_t precision, int32_t caps);
/* 0x004666D2 and 0x00466678: strip trailing zeros after the point; force
 * a decimal point in. */
void __cdecl crt_cropzeros(char *buf);
void __cdecl crt_forcdecpt(char *buf);
/* 0x00466720. */
int32_t __cdecl crt_positive(const double *value);

/* ---- fltcvt.cpp, shared with strtod.cpp --------------------------------- */

/* The twelve-byte long double: bytes 0-1 a guard word, 2-9 the mantissa,
 * 10-11 the exponent and sign. */
typedef struct CRT_LD12 {
    uint8_t b[12];
} CRT_LD12;

uint32_t crt_ld_dw(const CRT_LD12 *x, int32_t at);
void     crt_ld_set_dw(CRT_LD12 *x, int32_t at, uint32_t v);
uint16_t crt_ld_w(const CRT_LD12 *x, int32_t at);
void     crt_ld_set_w(CRT_LD12 *x, int32_t at, uint16_t v);
/* 0x0046C78D / 0x0046C72F / 0x0046D097. */
void crt_shl_12(CRT_LD12 *x);
void crt_add_12(CRT_LD12 *a, const CRT_LD12 *b);
void crt_multtenpow12(CRT_LD12 *x, int32_t pow, int32_t rounding);

/* ---- strtod.cpp -------------------------------------------------------- */

/* The record _fltin2 fills, ADDR_CRT_FLT_PTR's target. */
typedef struct CRT_FLT {
    int32_t flags;
    int32_t nbytes;
    int32_t lval;
    int32_t pad;
    double  dval;
} CRT_FLT;

/* The conversion parameters __ld12cvt takes, ADDR_CRT_CVTINFO_DOUBLE. */
typedef struct CRT_CVTINFO {
    int32_t max_exp, min_exp, mantbits, expbits, size, bias;
} CRT_CVTINFO;

#ifdef AM2_STANDALONE
/* Defined in strtod.cpp, placed at 0x0048D3C8; read there and (by address, as
 * the RTERR scan bound) in startup.cpp, so the declaration is shared here.
 * (Already inside crt.h's extern "C" block, so no linkage keyword here.) */
extern const CRT_CVTINFO am2_crt_cvtinfo_double[2];
/* Defined in fltcvt.cpp; POW10_TABLE's address is also crt_setmbcp's scan
 * bound in startup.cpp, so its declaration is shared. */
extern const uint8_t am2_crt_pow10_table[352];
/* The init/term tables (startup.cpp); XP/XT are read in exit.cpp, so shared. */
extern const uint32_t am2_crt_inittab[36];
/* The __badioinfo sentinel (lowio.cpp); also read in stdio.cpp, so shared. */
extern const uint8_t am2_crt_badioinfo[8];
/* The _pctype pointer slot (conv.cpp); read across four CRT TUs, so shared. */
extern uint32_t am2_crt_pctype;
/* Runtime-written .data-init CRT scalars, each read from more than one TU:
 * __mb_cur_max (conv.cpp), __app_type (startup.cpp), and the decimal-point
 * string (fltcvt.cpp). Non-const; initial bytes still equal the image. */
extern int32_t am2_crt_mb_cur_max;
extern int32_t am2_crt_app_type;
extern uint8_t am2_crt_decimal_point[2];
#endif

/* 0x004653B7. Leading space skipped, then _fltin2: an overflow answers
 * +/-HUGE_VAL with ERANGE, an underflow 0 with ERANGE, no digits 0 with
 * *end at `s`. */
double __cdecl crt_strtod(const char *s, char **end);
/* 0x00469858. `len` and the two zeros are unused. */
CRT_FLT *__cdecl crt_fltin2(const char *s, int32_t len, int32_t a, int32_t b);
/* 0x0046BCBD. The decimal parser: a twelve-state machine over the text,
 * up to 24 digits kept (the 25th rounds the 24th), then __mtold12 and a
 * power of ten. Answers a flag word: 1 underflow, 2 overflow, 4 no digits. */
uint32_t __cdecl crt_strgtold12(CRT_LD12 *out, const char **end, const char *s,
                                int32_t mult12, int32_t scale, int32_t decpt, int32_t implicit_e);
/* 0x0046C7E8. */
void __cdecl crt_mtold12(const char *digits, uint32_t n, CRT_LD12 *out);
/* 0x0046AC00 / 0x0046AA94. To a double: 0, or 1 for an overflow (infinity
 * is stored) or 2 for an underflow (zero or a denormal is stored). */
int32_t __cdecl crt_ld12tod(const CRT_LD12 *x, double *out);
int32_t __cdecl crt_ld12cvt(const CRT_LD12 *x, void *out, const CRT_CVTINFO *cvt);
/* 0x004697E0. _pctype[c] & mask; c is below 0x100 from every caller here. */
int32_t __cdecl crt_isctype(int32_t c, int32_t mask);



/* FILE.flag bits, the CRT's own names. */
#define CRT_IOREAD     0x0001
#define CRT_IOWRT      0x0002
#define CRT_IONBF      0x0004
#define CRT_IOMYBUF    0x0008
#define CRT_IOEOF      0x0010
#define CRT_IOERR      0x0020
#define CRT_IOSTRG     0x0040
#define CRT_IORW       0x0080
#define CRT_IOYOURBUF  0x0100
#define CRT_IOSETVBUF  0x0400
#define CRT_IOCTRLZ    0x2000
#define CRT_IOCOMMIT   0x4000

#define CRT_EOF        (-1)

/* 0x004648E2. fopen: _fsopen with _SH_DENYNO. */
CRT_FILE *__cdecl crt_fopen(const char *path, const char *mode);
/* 0x004648C2. A free stream from __piob, then _openfile on it. */
CRT_FILE *__cdecl crt_fsopen(const char *path, const char *mode, int32_t shflag);
/* 0x00467FF8. Parse `mode` into an _open flag word and the FILE's flag bits
 * (starting from _commode), _sopen the file and fill the FILE in. NULL when
 * the first mode letter is not r, w or a, or the open fails. */
CRT_FILE *__cdecl crt_openfile(const char *path, const char *mode, int32_t shflag, CRT_FILE *f);
/* 0x00468168. The first __piob slot that is empty or holds a closed FILE,
 * allocating a FILE for an empty slot; NULL when the table is full. */
CRT_FILE *__cdecl crt_getstream(void);
/* 0x0046486C. */
int32_t __cdecl crt_fclose(CRT_FILE *f);
/* 0x004645C1 / 0x004644B7. size*count bytes through the buffer, whole
 * buffer-multiples straight to _read/_write; the count of items done. */
uint32_t __cdecl crt_fread(void *buf, uint32_t size, uint32_t count, CRT_FILE *f);
uint32_t __cdecl crt_fwrite(const void *buf, uint32_t size, uint32_t count, CRT_FILE *f);
/* 0x004655B8. */
char *__cdecl crt_fgets(char *s, int32_t n, CRT_FILE *f);
/* 0x00464F18 / 0x00464DC0. fseek flushes and drops to _lseek; ftell undoes
 * the buffer, counting text-mode LFs as two bytes each. */
int32_t __cdecl crt_fseek(CRT_FILE *f, int32_t off, int32_t whence);
int32_t __cdecl crt_ftell(CRT_FILE *f);
/* 0x00465A96. fflush(NULL) is _flushall(0): every writer. */
int32_t __cdecl crt_fflush(CRT_FILE *f);
/* 0x00465AD1. _flush: write the buffer out; 0, or -1 with IOERR set. */
int32_t __cdecl crt_flush(CRT_FILE *f);
/* 0x00465B36. Over every open stream: flag 1 flushes all and counts the
 * successes, flag 0 flushes the writers and answers 0 or -1. */
int32_t __cdecl crt_flushall(int32_t flag);
/* 0x00466AB3 / 0x004670B5. The buffer-full and buffer-empty slow paths;
 * both answer the character or -1. A string stream answers -1 at once. */
int32_t __cdecl crt_flsbuf(int32_t ch, CRT_FILE *f);
int32_t __cdecl crt_filbuf(CRT_FILE *f);
/* 0x0046AE81 / 0x00467FCD. A 0x1000-byte buffer, or the FILE's own two
 * bytes when malloc fails; and its release. */
void __cdecl crt_getbuf(CRT_FILE *f);
void __cdecl crt_freebuf(CRT_FILE *f);
/* 0x00469BE6. __initstdio: __piob over the twenty _iob entries. The
 * original runs it from startup; here the first crt_getstream runs it. */
void __cdecl crt_initstdio(void);

/* ---- lowio.cpp --------------------------------------------------------- */

/* One entry of __pioinfo, 8 bytes. */
typedef struct CRT_IOINFO {
    intptr_t osfhnd;
    uint8_t  osfile;
    uint8_t  pipech;
    uint8_t  pad[2];
} CRT_IOINFO;

#define CRT_FOPEN       0x01
#define CRT_FEOFLAG     0x02
#define CRT_FCRLF       0x04
#define CRT_FPIPE       0x08
#define CRT_FNOINHERIT  0x10
#define CRT_FAPPEND     0x20
#define CRT_FDEV        0x40
#define CRT_FTEXT       0x80

/* _open flags and share modes. */
#define CRT_O_RDONLY      0x0000
#define CRT_O_WRONLY      0x0001
#define CRT_O_RDWR        0x0002
#define CRT_O_APPEND      0x0008
#define CRT_O_RANDOM      0x0010
#define CRT_O_SEQUENTIAL  0x0020
#define CRT_O_TEMPORARY   0x0040
#define CRT_O_NOINHERIT   0x0080
#define CRT_O_CREAT       0x0100
#define CRT_O_TRUNC       0x0200
#define CRT_O_EXCL        0x0400
#define CRT_O_SHORT_LIVED 0x1000
#define CRT_O_TEXT        0x4000
#define CRT_O_BINARY      0x8000
#define CRT_SH_DENYRW     0x10
#define CRT_SH_DENYWR     0x20
#define CRT_SH_DENYRD     0x30
#define CRT_SH_DENYNO     0x40

#define CRT_ENOEXEC 8
#define CRT_EBADF   9
#define CRT_EACCES  13
#define CRT_EINVAL  22
#define CRT_EMFILE  24
#define CRT_ENOSPC  28

/* The ioinfo for `fd`, which must be below _nhandle. */
CRT_IOINFO *crt_pioinfo(int32_t fd);

/* 0x0046B0AE. */
int32_t __cdecl crt_sopen(const char *path, int32_t oflag, int32_t shflag, int32_t pmode);
/* 0x0046718E / 0x00466BC8. */
int32_t __cdecl crt_read(int32_t fd, void *buf, uint32_t n);
int32_t __cdecl crt_write(int32_t fd, const void *buf, uint32_t n);
/* 0x0046958F. The new position, or -1. */
int32_t __cdecl crt_lseek(int32_t fd, int32_t off, int32_t whence);
/* 0x00467F1A. */
int32_t __cdecl crt_close(int32_t fd);
/* 0x00469B8F. */
int32_t __cdecl crt_commit(int32_t fd);
/* 0x0046CB42. Extend with zeros or truncate, in binary mode. */
int32_t __cdecl crt_chsize(int32_t fd, int32_t size);
/* 0x0046D113. Set FTEXT from CRT_O_TEXT/CRT_O_BINARY; the old mode back. */
int32_t __cdecl crt_setmode(int32_t fd, int32_t mode);
/* 0x0046AEC5. */
int32_t __cdecl crt_isatty(int32_t fd);
/* 0x0046AEEB / 0x0046AF80 / 0x0046AFF7 / 0x0046B071. The handle table. */
int32_t __cdecl crt_alloc_osfhnd(void);
int32_t __cdecl crt_set_osfhnd(int32_t fd, intptr_t h);
int32_t __cdecl crt_free_osfhnd(int32_t fd);
intptr_t __cdecl crt_get_osfhandle(int32_t fd);
/* 0x00469D90. */
void __cdecl crt_dosmaperr(uint32_t oserr);
/* 0x004693E4. _ioinit: block 0 of the table, any handles the parent passed
 * through STARTUPINFO, and the three standard handles. Startup's job in the
 * original; the first crt_alloc_osfhnd runs it here. */
void __cdecl crt_ioinit(void);

/* ---- dir.cpp ----------------------------------------------------------- */

/* The CRT's _finddata_t: 0x118 bytes. */
typedef struct CRT_FINDDATA {
    uint32_t attrib;
    int32_t  time_create;
    int32_t  time_access;
    int32_t  time_write;
    uint32_t size;
    char     name[260];
} CRT_FINDDATA;

/* 0x00465BA3 / 0x00465C6D / 0x00465D32. FindFirstFileA and its two
 * siblings, with the record translated: attrib 0 for NORMAL, the three
 * times through crt_timet_from_ft, the low size, the name. */
int32_t __cdecl crt_findfirst(const char *spec, CRT_FINDDATA *out);
int32_t __cdecl crt_findnext(int32_t handle, CRT_FINDDATA *out);
int32_t __cdecl crt_findclose(int32_t handle);
/* 0x00465ED0. SetCurrentDirectoryA, then the =X: variable Win32 keeps per
 * drive, unless the directory is a UNC path. */
int32_t __cdecl crt_chdir(const char *path);
/* 0x00465DB5 / 0x00465DC8. The current directory of drive 0 (current) or
 * 1..26, into `buf` or a malloc'd one of at least `max`. */
char *__cdecl crt_getcwd(char *buf, int32_t max);
char *__cdecl crt_getdcwd(int32_t drive, char *buf, int32_t max);
/* 0x00465E99. */
int32_t __cdecl crt_validdrive(int32_t drive);
/* 0x00465F56 / 0x00466496 / 0x0046646C / 0x00466359. */
int32_t __cdecl crt_mkdir(const char *path);
int32_t __cdecl crt_rmdir(const char *path);
int32_t __cdecl crt_remove(const char *path);
int32_t __cdecl crt_chmod(const char *path, int32_t pmode);

/* ---- time.cpp ---------------------------------------------------------- */

/* The tm _isindst reads; only the four fields __loctotime_t sets matter. */
typedef struct CRT_TM {
    int32_t tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
} CRT_TM;

/* 0x00465052. GetLocalTime through __loctotime_t; the DST answer for the
 * current minute is cached in the image. */
int32_t __cdecl crt_time(int32_t *out);
/* 0x00469652. Broken-down local time to seconds since 1970, applying
 * _timezone and, for dst 1 or for dst -1 with _isindst, _dstbias. -1
 * outside 1970..2038. */
int32_t __cdecl crt_loctotime_t(int32_t yr, int32_t mo, int32_t dy, int32_t hr, int32_t mn, int32_t sec, int32_t dst);
/* 0x00465D51. A FILETIME to local seconds since 1970; -1 for zero or failure. */
int32_t __cdecl crt_timet_from_ft(const void *ft);
/* 0x0046B615 / 0x0046B62A. Once: TZ if set, else GetTimeZoneInformation,
 * into _timezone, _daylight, _dstbias and _tzname. */
void __cdecl crt_tzset(void);
void __cdecl crt_tzset_body(void);
/* 0x0046B888. Whether the tm's day and time fall inside daylight saving,
 * by the two rules crt_cvtdate caches for its year. */
int32_t __cdecl crt_isindst(const CRT_TM *tb);
/* 0x0046BA34. One DST transition rule -- a week-and-weekday or an absolute
 * date -- to a day of the year and a millisecond of the day, cached. */
void __cdecl crt_cvtdate(int32_t trantype, int32_t datetype, int32_t year, int32_t month,
                         int32_t week, int32_t dayofweek, int32_t date, int32_t hour,
                         int32_t min, int32_t sec, int32_t msec);

/* ---- env.cpp / mbcs.cpp ------------------------------------------------ */

/* 0x0046CDFA. Over _environ, case-insensitively; NULL until startup has
 * built the table. */
char *__cdecl crt_getenv(const char *name);
/* 0x0046D189. Compare `n` bytes ignoring case through the locale; 0 equal,
 * 0x7FFFFFFF when the comparison itself fails. */
int32_t __cdecl crt_mbsnbicoll(const char *a, const char *b, uint32_t n);
/* 0x00469DF7. _mbctoupper: one byte through _mbctype and _mbcasemap. The
 * multibyte arm for c > 0xFF is not reproduced; its one caller passes a
 * byte. */
int32_t __cdecl crt_mbctoupper(int32_t c);

/* ---- heap.cpp ---------------------------------------------------------- */

/* One region of the small-block heap: a reserved megabyte, committed in
 * 32 KB groups of eight pages, with a bitmap per size class. */
typedef struct CRT_SBH_HEADER {
    uint32_t bitvEntryHi;   /* classes 0..31 with a free entry somewhere in the region */
    uint32_t bitvEntryLo;   /* classes 32..63 */
    uint32_t bitvCommit;    /* groups NOT committed */
    uint8_t *pHeapData;
    struct CRT_SBH_REGION *pRegion;
} CRT_SBH_HEADER;

/* 0x004647F8 / 0x00464900 / 0x004646A9 / 0x004648F5 / 0x004646D8 /
 * 0x0046C18E / 0x00469629. The CRT's allocator: blocks of 1016 bytes and
 * under from the small-block heap, larger ones from HeapAlloc, and a
 * retry through the new handler when _newmode (or operator new) asks. */
void *__cdecl crt_malloc(uint32_t n);
void *__cdecl crt_operator_new(uint32_t n);
void  __cdecl crt_free(void *p);
void  __cdecl crt_operator_delete(void *p);
void *__cdecl crt_realloc(void *p, uint32_t n);
void *__cdecl crt_calloc(uint32_t count, uint32_t size);
uint32_t __cdecl crt_msize(const void *p);
/* 0x0046480A / 0x00464836 / 0x00467EFF. */
void *__cdecl crt_nh_malloc(uint32_t n, int32_t nhflag);
void *__cdecl crt_heap_alloc(uint32_t n);
int32_t __cdecl crt_callnewh(uint32_t n);
/* 0x00467384 / 0x004673C0. Startup's; the first allocation runs them here. */
int32_t __cdecl crt_heap_init(int32_t mtflag);
int32_t __cdecl crt_sbh_heap_init(void);
/* 0x004673FE / 0x00467754 / 0x00467429 / 0x00467C09 / 0x00467A5D /
 * 0x00467B0E. The small-block heap itself. */
CRT_SBH_HEADER *__cdecl crt_sbh_find_block(const void *p);
void *__cdecl crt_sbh_alloc_block(uint32_t n);
void  __cdecl crt_sbh_free_block(CRT_SBH_HEADER *h, void *p);
int32_t __cdecl crt_sbh_resize_block(CRT_SBH_HEADER *h, void *p, uint32_t n);
CRT_SBH_HEADER *__cdecl crt_sbh_alloc_new_region(void);
int32_t __cdecl crt_sbh_alloc_new_group(CRT_SBH_HEADER *h);

/* ---- exit.cpp ---------------------------------------------------------- */

typedef void (__cdecl *CRT_ExitFn)(void);

/* 0x00465011 / 0x00464FA4 / 0x00465023. The onexit table: 32 entries from
 * startup, four more each time it fills. */
int32_t __cdecl crt_atexit(CRT_ExitFn fn);
CRT_ExitFn __cdecl crt_onexit(CRT_ExitFn fn);
void __cdecl crt_onexitinit(void);
/* 0x0046930F / 0x00469320 / 0x00469331. exit runs the onexit table from
 * the end, then the two terminator tables, then ExitProcess; _exit skips
 * the onexit table; doexit with retcaller set comes back instead. */
void __cdecl crt_exit(int32_t code);
void __cdecl crt_exit_quick(int32_t code);
void __cdecl crt_doexit(int32_t code, int32_t quick, int32_t retcaller);
/* 0x004693CA. Every non-NULL pointer between the two, in order. */
void __cdecl crt_initterm(CRT_ExitFn *begin, CRT_ExitFn *end);
/* 0x00469C8B / 0x0046C20B. What the pre-terminator table holds. */
void __cdecl crt_endstdio(void);
int32_t __cdecl crt_fcloseall(void);
/* 0x0046B3AD / 0x0046B3BE. The terminator table's entry, and its pair. */
void __cdecl crt_seh_set(void);
void __cdecl crt_seh_restore(void);

/* ---- startup.cpp ------------------------------------------------------- */

/* 0x004664C0, up to the call of WinMain: the version words, the heap,
 * lowio, the command line and environment, argv, envp, _cinit. The
 * standalone and native builds call this before WinMain; the original's
 * entry point then calls WinMain and exit, which they do from the host's
 * main and the startup object's destructor. */
void __cdecl crt_startup(void);
/* 0x0046A396. The environment block, narrow, malloc'd. */
char *__cdecl crt_get_environment_strings(void);
/* 0x0046A149 / 0x0046A1E2. argv from _acmdln, through the two-pass parser:
 * once to count, once to fill. */
void __cdecl crt_setargv(void);
void __cdecl crt_parse_cmdline(const char *cmd, char **argv, char *args, int32_t *argc, int32_t *nchars);
/* 0x0046A090 / 0x0046A038. */
void __cdecl crt_setenvp(void);
char *__cdecl crt_wincmdln(void);
/* 0x0046C263 and its pieces. */
int32_t __cdecl crt_setmbcp(int32_t cp);
int32_t __cdecl crt_getsystemcp(int32_t cp);
uint32_t __cdecl crt_cp_lcid(int32_t cp);
void __cdecl crt_initmbctable(void);
void __cdecl crt_mbctype_reset(void);
void __cdecl crt_setsbuplow(void);
int32_t __cdecl crt_ismbblead(int32_t c);
int32_t __cdecl crt_ismbbtype(int32_t c, int32_t ctype_mask, int32_t mbctype_mask);
/* 0x004665B6 / 0x004665DB / 0x0046A5E1 / 0x0046A5A8 / 0x0046C685. */
void __cdecl crt_amsg_exit(int32_t rterrnum);
void __cdecl crt_fast_error_exit(int32_t rterrnum);
void __cdecl crt_nmsg_write(int32_t rterrnum);
void __cdecl crt_ff_msgbanner(void);
int32_t __cdecl crt_messagebox(const char *text, const char *caption, uint32_t type);
/* 0x0046443E and its pieces: _fpmath. */
void __cdecl crt_fpmath(void);
int32_t __cdecl crt_fdiv_detect(void);
int32_t __cdecl crt_fdiv_test(void);
void __cdecl crt_setdefaultprecision(void);
uint32_t __cdecl crt_control87(uint32_t newval, uint32_t mask);
uint32_t __cdecl crt_hw2abstract(uint32_t cw);
uint32_t __cdecl crt_abstract2hw(uint32_t flags);
/* 0x00464456. printf.cpp calls fltcvt.cpp by name: nothing to fill. */
void __cdecl crt_cfltcvt_init(void);
/* 0x004692E2. The XI table, then the XC table. */
void __cdecl crt_cinit(void);

/* ---- standin.cpp -- NOT reconstructions -------------------------------- */

/* 0x0046D236 __crtCompareStringA and 0x0046D1C8 __wtomb_environ are not
 * read. The first reaches CompareStringA with its own arguments and this
 * one does the same; the second converts a wide environment this ANSI
 * image never has. */
int32_t __cdecl crt_compare_string_a(uint32_t lcid, uint32_t flags, const char *a, int32_t na,
                                     const char *b, int32_t nb, uint32_t codepage);
int32_t __cdecl crt_wtomb_environ(void);


/* 0x0046BB74 __crtGetStringTypeA and 0x0046996B __crtLCMapStringA are the
 * locale layer's wrappers and are not read; these reach the two kernel32
 * calls with the arguments they would. */
int32_t __cdecl crt_get_string_type_a(uint32_t info, const char *src, int32_t n, uint16_t *out,
                                      uint32_t codepage, uint32_t lcid, int32_t error);
int32_t __cdecl crt_lcmapstring_a(uint32_t lcid, uint32_t flags, const char *src, int32_t n,
                                  char *dst, int32_t dstn, uint32_t codepage, int32_t error);

/* ---- rand.cpp ---------------------------------------------------------- */

/* 0x00464420. The linear congruential generator over the image's seed:
 * seed = seed * 0x343FD + 0x269EC3, answer (seed >> 16) & 0x7FFF. */
int32_t __cdecl crt_rand(void);

/* 0x00464416. Set the seed. */
void __cdecl crt_srand(uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PLATFORM_CRT_H */
