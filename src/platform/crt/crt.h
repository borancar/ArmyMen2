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
/* 0x00469CA0 and 0x00465710. */
char *__cdecl crt_strcpy(char *dst, const char *src);
/* 0x00466D80. memcpy. */
void *__cdecl crt_memcpy(void *dst, const void *src, uint32_t n);
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

/* ---- stdio.cpp --------------------------------------------------------- */

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

/* ---- standin.cpp -- NOT reconstructions -------------------------------- */

/* The heap (malloc 0x004647F8, free 0x004646A9, calloc 0x0046C18E) and
 * _amsg_exit (0x004665B6) are not read yet. These forward to the host so the
 * modules above can link; heap.cpp replaces the first three. */
void *__cdecl crt_malloc(uint32_t n);
void *__cdecl crt_calloc(uint32_t count, uint32_t size);
void  __cdecl crt_free(void *p);
void  __cdecl crt_amsg_exit(int32_t code);

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
