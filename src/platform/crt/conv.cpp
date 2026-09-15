/* conv.cpp -- atoi, atol and strtol, from 0x004660A7, 0x0046601C and
 * 0x004651AF.
 *
 * All three classify characters through the CRT's own table: _pctype is a
 * pointer in the image to a 256-entry word table whose bits are _UPPER 1,
 * _LOWER 2, _DIGIT 4, _SPACE 8 and _ALPHA 0x100, and the originals read it
 * directly when __mb_cur_max is 1, which it is under code page 1252, and
 * call _isctype otherwise. The table is the image's, reached through the
 * slide so the self-test can replay these against the recorded answers.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The CRT's _pctype classification table at 0x0048CCA0: 257 words, entry 0
 * the EOF (index -1) slot and 1..256 the bytes 0..255, bits _UPPER 1, _LOWER
 * 2, _DIGIT 4, _SPACE 8, _HEX 0x80, _ALPHA 0x100. Pure const data placed at
 * its VA; _pctype (0x0048CCA2, the blob pointer slot) already points at
 * &table[1], so no slide changes and every reader lands on this copy. */
extern "C" const uint16_t am2_crt_ctype[257] = {
    0x0000, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0028, 0x0028,
    0x0028, 0x0028, 0x0028, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0048, 0x0010, 0x0010,
    0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    0x0010, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0010,
    0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0081, 0x0081, 0x0081, 0x0081, 0x0081, 0x0081,
    0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
    0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0010, 0x0010, 0x0010, 0x0010,
    0x0010, 0x0010, 0x0082, 0x0082, 0x0082, 0x0082, 0x0082, 0x0082, 0x0002, 0x0002, 0x0002, 0x0002,
    0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002,
    0x0002, 0x0002, 0x0002, 0x0002, 0x0010, 0x0010, 0x0010, 0x0010, 0x0020, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};
#endif

#define crt_pctype     (*(const uint16_t **)(uintptr_t)AM2_IMAGE(ADDR_CRT_PCTYPE))
#define crt_mb_cur_max (*(const int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MB_CUR_MAX))
#define crt_errno      (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRNO))

#define CRT_UPPER  0x001
#define CRT_LOWER  0x002
#define CRT_DIGIT  0x004
#define CRT_SPACE  0x008
#define CRT_ALPHA  0x100
#define CRT_ERANGE 0x22

/* The classification the originals make: the table when the code page is
 * single-byte, _isctype otherwise. _isctype answers from the same table for
 * a single-byte page, so both arms read it here; the multibyte arm cannot
 * be reached under 1252 and is not reproduced. */
static uint32_t crt_ctype(uint32_t c)
{
    const uint16_t *table = (const uint16_t *)(uintptr_t)AM2_IMAGE((uintptr_t)crt_pctype);
    return table[c & 0xFF];
}

/* 0x004697E0. The word for `c` masked, with c in -1..0xFF: the table is
 * _ctype + 1, so -1 reads the entry before it. Above 0xFF the original
 * classifies a two-byte character through GetStringTypeA; nothing here
 * passes one and that arm is not reproduced. */
int32_t __cdecl crt_isctype(int32_t c, int32_t mask)
{
    const uint16_t *table = (const uint16_t *)(uintptr_t)AM2_IMAGE((uintptr_t)crt_pctype);

    if ((uint32_t)(c + 1) > 0x100)
        return c & mask;   /* unreachable: see above */
    return table[c] & mask;
}

/* 0x00469714, toupper for the ASCII range the single-byte page needs. */
static int32_t crt_toupper(int32_t c)
{
    return (crt_ctype((uint32_t)c) & CRT_LOWER) ? c - 0x20 : c;
}

int32_t __cdecl crt_tolower(int32_t c)
{
    return (crt_ctype((uint32_t)c) & CRT_UPPER) ? c + 0x20 : c;
}

int32_t __cdecl crt_isdigit(int32_t c)
{
    return (crt_ctype((uint32_t)c) & CRT_DIGIT) != 0;
}

int32_t __cdecl crt_atol(const char *s)
{
    uint32_t c, sign;
    int32_t  total = 0;

    while (crt_ctype((uint8_t)*s) & CRT_SPACE)
        s++;
    c = (uint8_t)*s++;
    sign = c;
    if (c == '-' || c == '+')
        c = (uint8_t)*s++;
    while (crt_ctype(c) & CRT_DIGIT) {
        total = 10 * total + ((int32_t)c - '0');
        c = (uint8_t)*s++;
    }
    return sign == '-' ? -total : total;
}

int32_t __cdecl crt_atoi(const char *s)
{
    return crt_atol(s);
}

#define CRT_FL_UNSIGNED  1
#define CRT_FL_NEG       2
#define CRT_FL_OVERFLOW  4
#define CRT_FL_READDIGIT 8

/* 0x004651AF, strtoxl: the worker under strtol and strtoul. */
static uint32_t crt_strtoxl(const char *nptr, char **endptr, int32_t ibase, uint32_t flags)
{
    const char *p = nptr;
    uint32_t    c, number = 0, maxval, digit;
    uint32_t    base;

    c = (uint8_t)*p++;
    while (crt_ctype(c) & CRT_SPACE)
        c = (uint8_t)*p++;
    if (c == '-') {
        flags |= CRT_FL_NEG;
        c = (uint8_t)*p++;
    } else if (c == '+') {
        c = (uint8_t)*p++;
    }

    if (ibase < 0 || ibase == 1 || ibase > 36) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }
    base = (uint32_t)ibase;
    if (base == 0) {
        if (c != '0')
            base = 10;
        else if (*p == 'x' || *p == 'X')
            base = 16;
        else
            base = 8;
    }
    if (base == 16 && c == '0' && (*p == 'x' || *p == 'X')) {
        c = (uint8_t)p[1];
        p += 2;
    }
    maxval = 0xFFFFFFFFu / base;

    for (;;) {
        if (crt_ctype(c) & CRT_DIGIT)
            digit = c - '0';
        else if (crt_ctype(c) & (CRT_UPPER | CRT_LOWER | CRT_ALPHA))
            digit = (uint32_t)crt_toupper((int32_t)c) - '0' - 7;
        else
            break;
        if (digit >= base)
            break;
        flags |= CRT_FL_READDIGIT;
        if (number < maxval || (number == maxval && digit <= 0xFFFFFFFFu % base))
            number = number * base + digit;
        else
            flags |= CRT_FL_OVERFLOW;
        c = (uint8_t)*p++;
    }
    p--;

    if (!(flags & CRT_FL_READDIGIT)) {
        if (endptr)
            p = nptr;
        number = 0;
    } else if ((flags & CRT_FL_OVERFLOW) ||
               (!(flags & CRT_FL_UNSIGNED) &&
                ((flags & CRT_FL_NEG) ? number > 0x80000000u : number > 0x7FFFFFFFu))) {
        crt_errno = CRT_ERANGE;
        number = (flags & CRT_FL_UNSIGNED) ? 0xFFFFFFFFu
               : (flags & CRT_FL_NEG) ? 0x80000000u : 0x7FFFFFFFu;
    }
    if (endptr)
        *endptr = (char *)p;
    if (flags & CRT_FL_NEG)
        number = (uint32_t)(-(int32_t)number);
    return number;
}

int32_t __cdecl crt_strtol(const char *nptr, char **endptr, int32_t base)
{
    return (int32_t)crt_strtoxl(nptr, endptr, base, 0);
}
