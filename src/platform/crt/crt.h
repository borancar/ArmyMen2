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

/* ---- fltcvt.cpp -------------------------------------------------------- */

/* 0x00466A3D. A double to its decimal text in `buf` for conversion `fmt`
 * ('e', 'f' or 'g') at `precision` digits, upper-case exponent when `caps`. */
void __cdecl crt_cfltcvt(const double *value, char *buf, int32_t fmt, int32_t precision, int32_t caps);
/* 0x004666D2 and 0x00466678: strip trailing zeros after the point; force
 * a decimal point in. */
void __cdecl crt_cropzeros(char *buf);
void __cdecl crt_forcdecpt(char *buf);

/* ---- stdio.cpp --------------------------------------------------------- */

/* 0x00466AB3. Flush a full write buffer and store `ch`; -1 on failure. A
 * string stream (flag 0x40) has no file behind it and answers -1. */
int32_t __cdecl crt_flsbuf(int32_t ch, CRT_FILE *f);

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
