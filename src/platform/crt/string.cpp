/* string.cpp -- the string functions the game calls, from their bodies.
 *
 * These are pure over the bytes their arguments point at, so each is
 * written for its observable behaviour rather than for the word-at-a-time
 * scans the originals use (strchr and strncpy walk dwords with the
 * 0x7EFEFEFF carry trick): the result is the same for every input, and
 * tests/vectors.h records the original's answers to check that it is.
 * Where the ORIGINAL's answer is a specific value rather than a sign --
 * strncmp returns exactly -1, 0 or 1 -- that value is reproduced.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

/* strtok's resumption point, the image's own word: a caller that passes
 * NULL continues from where the last call on any build left off. */
#define crt_strtok_next (*(char **)(uintptr_t)AM2_IMAGE(ADDR_CRT_STRTOK_NEXT))

char *__cdecl crt_strtok(char *str, const char *delims)
{
    uint8_t  map[32];
    char    *tok;
    int32_t  i;

    /* The delimiter set as a 256-bit map; the terminating NUL is in it
     * too, since the loop tests the byte after setting its bit. */
    for (i = 0; i < 32; i++)
        map[i] = 0;
    do {
        uint8_t c = (uint8_t)*delims;
        map[c >> 3] |= (uint8_t)(1 << (c & 7));
    } while (*delims++);

    if (!str)
        str = crt_strtok_next;

    /* Skip leading delimiters, but never the NUL. */
    while (*str && (map[(uint8_t)*str >> 3] & (1 << ((uint8_t)*str & 7))))
        str++;
    tok = str;

    /* Find the token's end: NUL stops there, a delimiter is overwritten
     * with NUL and stepped past. */
    while (*str) {
        if (map[(uint8_t)*str >> 3] & (1 << ((uint8_t)*str & 7))) {
            *str++ = 0;
            break;
        }
        str++;
    }
    crt_strtok_next = str;
    return tok == str ? (char *)0 : tok;
}

char *__cdecl crt_strchr(const char *str, int32_t c)
{
    /* The NUL counts as a character, so strchr(s, 0) finds the end. */
    for (;; str++) {
        if (*str == (char)c)
            return (char *)str;
        if (!*str)
            return (char *)0;
    }
}

char *__cdecl crt_strstr(const char *str, const char *sub)
{
    const char *s, *p;

    if (!*sub)
        return (char *)str;
    for (; *str; str++) {
        if (*str != *sub)
            continue;
        for (s = str + 1, p = sub + 1; *p && *s == *p; s++, p++)
            ;
        if (!*p)
            return (char *)str;
    }
    return (char *)0;
}

char *__cdecl crt_strncpy(char *dst, const char *src, uint32_t n)
{
    char *d = dst;

    /* The NUL that ends the copy counts against `n` like any other byte:
     * the first version did not count it and padded one byte past the
     * end, which the vectors' scribble check reported on the two rows
     * whose source was shorter than n. */
    while (n) {
        n--;
        if ((*d++ = *src++) == 0)
            break;
    }
    while (n) {
        *d++ = 0;
        n--;
    }
    return dst;
}

/* 0x00465160 is strncmp, not _strnicmp: a repne scasb to bound the length,
 * a repe cmpsb, and an UNSIGNED compare of the bytes that stopped it, with
 * no case folding anywhere. The answer is exactly -1, 0 or 1. */
int32_t __cdecl crt_strncmp(const char *a, const char *b, uint32_t n)
{
    if (n == 0)
        return 0;
    for (;;) {
        uint8_t x = (uint8_t)*a, y = (uint8_t)*b;
        if (x != y)
            return x < y ? -1 : 1;
        if (!x || --n == 0)
            return 0;
        a++;
        b++;
    }
}

/* Both case functions ask the CRT's locale handle for LC_CTYPE first --
 * __lc_handle[2], the image's word at ADDR_CRT_LC_HANDLE_CTYPE -- and take
 * a plain ASCII arm while it is zero, which it is from startup to exit:
 * nothing in the image calls setlocale. The other arm, LCMapString over a
 * malloc'd copy for _strlwr and tolower per byte for _stricmp, is not
 * reproduced; a build that somehow set a locale would fold ASCII here
 * where the original would fold the locale's letters. */
#define crt_lc_handle_ctype (*(const int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_LC_HANDLE_CTYPE))

/* 0x00465F90. The C-locale arm folds A..Z only, on both strings, and
 * answers -1, 0 or 1 from the first pair that still differs. Equal RAW
 * bytes are accepted before either is folded, so a byte above 0x7F
 * compares equal only to itself. */
int32_t __cdecl crt_stricmp(const char *a, const char *b)
{
    (void)crt_lc_handle_ctype;
    for (;;) {
        uint8_t x = (uint8_t)*a++, y = (uint8_t)*b++;
        if (x == y) {
            if (!x)
                return 0;
            continue;
        }
        if ((uint32_t)(x - 'A') < 26u)
            x += 0x20;
        if ((uint32_t)(y - 'A') < 26u)
            y += 0x20;
        if (x == y)
            continue;
        return x < y ? -1 : 1;
    }
}

/* 0x0046D7D6. The C-locale arm: A..Z become a..z in place, judged by a
 * SIGNED compare, so bytes above 0x7F are left alone. Answers the string. */
char *__cdecl crt_strlwr(char *s)
{
    char *p;

    (void)crt_lc_handle_ctype;
    for (p = s; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p += 0x20;
    return s;
}

/* 0x004698F0. The dword-at-a-time scan of the original finds the same
 * terminator this does. */
int32_t __cdecl crt_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (int32_t)(p - s);
}

/* 0x00469CA0: strcpy, dword-at-a-time in the original, byte-wise here. */
char *__cdecl crt_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != 0)
        ;
    return dst;
}

/* 0x00465710: memmove, which copies backwards when the destination lies
 * above an overlapping source. */
int32_t __cdecl crt_strcmp(const char *a, const char *b)
{
    /* The original compares a dword at a time when `a` is aligned; the
     * answer is the same byte-wise: the first differing byte decides. */
    while (*a && *a == *b) {
        a++;
        b++;
    }
    if ((uint8_t)*a == (uint8_t)*b)
        return 0;
    return (uint8_t)*a < (uint8_t)*b ? -1 : 1;
}

/* The original is overlap-safe: a destination inside the source's span
 * is copied from the end (`std; rep movsd`), so it is memmove under the
 * other name. Its dword unrolling and tail tables change nothing a caller
 * can see; the direction does. */
void *__cdecl crt_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d > s && d < s + n) {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    } else {
        while (n--)
            *d++ = *s++;
    }
    return dst;
}

void *__cdecl crt_memmove(void *dst, const void *src, uint32_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d <= s || d >= s + n) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}
