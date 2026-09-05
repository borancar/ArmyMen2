/* fltcvt.cpp -- the CRT's decimal conversion of doubles, from _cfltcvt at
 * 0x00466A3D down to $I10_OUTPUT at 0x0046C8AF.
 *
 * Nothing here is floating point. __dtold (0x0046AD61) unpacks the double
 * into the CRT's twelve-byte long double -- a guard word, a 64-bit mantissa
 * with its leading one explicit, and a 15-bit exponent with the sign above
 * it -- and everything after is integer arithmetic on those bytes:
 * __ld12mul (0x0046CE77) is a five-word schoolbook multiply that keeps the
 * top six words and rounds half to even at the guard word, __multtenpow12
 * (0x0046D097) scales by a table of powers of ten the image carries, and
 * $I10_OUTPUT produces one decimal digit per multiplication by ten from the
 * top byte of the working value. _fptostr (0x0046AC86) rounds the digit
 * string to the digits asked for, and _cftof, _cftoe and _cftog
 * (0x0046687A, 0x00466776, 0x00466958) lay it out with the sign, the
 * locale's decimal point and the exponent. Because it is all integer work,
 * it reproduces the original's digits exactly, which is the point: sprintf
 * of "%6.2f" is how the script layer turns a float into text.
 *
 * The powers of ten, 0.1, "e+000" and the decimal point are read out of
 * the image rather than transcribed, and the statics _cftog shares with the
 * two below it are the image's own words, slid through AM2_IMAGE.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#include <string.h>

/* ---- the twelve-byte long double ------------------------------------------ */

/* Bytes 0-1 a guard word, 2-9 the mantissa, 10-11 the exponent and sign;
 * w0, w1, w2 are the three dwords the shift helpers move as a unit. */
typedef struct CRT_LD12 {
    uint8_t b[12];
} CRT_LD12;

static uint32_t ld_dw(const CRT_LD12 *x, int32_t at)
{
    uint32_t v;
    memcpy(&v, x->b + at, 4);
    return v;
}
static void ld_set_dw(CRT_LD12 *x, int32_t at, uint32_t v) { memcpy(x->b + at, &v, 4); }
static uint16_t ld_w(const CRT_LD12 *x, int32_t at)
{
    uint16_t v;
    memcpy(&v, x->b + at, 2);
    return v;
}
static void ld_set_w(CRT_LD12 *x, int32_t at, uint16_t v) { memcpy(x->b + at, &v, 2); }

/* 0x0046C78D: the 96 bits left one. */
static void crt_shl_12(CRT_LD12 *x)
{
    uint32_t w0 = ld_dw(x, 0), w1 = ld_dw(x, 4), w2 = ld_dw(x, 8);
    ld_set_dw(x, 0, w0 << 1);
    ld_set_dw(x, 4, (w1 << 1) | (w0 >> 31));
    ld_set_dw(x, 8, (w2 << 1) | (w1 >> 31));
}

/* 0x0046C7BB: the 96 bits right one. */
static void crt_shr_12(CRT_LD12 *x)
{
    uint32_t w0 = ld_dw(x, 0), w1 = ld_dw(x, 4), w2 = ld_dw(x, 8);
    ld_set_dw(x, 4, (w1 >> 1) | (w2 << 31));
    ld_set_dw(x, 0, (w0 >> 1) | (w1 << 31));
    ld_set_dw(x, 8, w2 >> 1);
}

/* 0x0046C70E: *out = a + b, answering the carry. */
static int32_t crt_addl(uint32_t a, uint32_t b, uint32_t *out)
{
    uint32_t s = a + b;
    *out = s;
    return s < a || s < b;
}

/* 0x0046C72F: a += b over the 96 bits, carries rippling up. */
static void crt_add_12(CRT_LD12 *a, const CRT_LD12 *b)
{
    uint32_t v;
    if (crt_addl(ld_dw(a, 0), ld_dw(b, 0), &v)) {
        ld_set_dw(a, 0, v);
        if (crt_addl(ld_dw(a, 4), 1, &v)) {
            ld_set_dw(a, 4, v);
            ld_set_dw(a, 8, ld_dw(a, 8) + 1);
        } else {
            ld_set_dw(a, 4, v);
        }
    } else {
        ld_set_dw(a, 0, v);
    }
    if (crt_addl(ld_dw(a, 4), ld_dw(b, 4), &v)) {
        ld_set_dw(a, 4, v);
        ld_set_dw(a, 8, ld_dw(a, 8) + 1);
    } else {
        ld_set_dw(a, 4, v);
    }
    crt_addl(ld_dw(a, 8), ld_dw(b, 8), &v);
    ld_set_dw(a, 8, v);
}

/* 0x0046CE77: a = a * b.
 *
 * Only the upper diagonals of the five-by-five word product are formed --
 * for i in 0..4, the terms a[i+k] * b[4-k] with i+k < 5 -- into a six-word
 * accumulator whose word i holds product position i+4; the low four words
 * of the true product are never computed. The accumulator is normalised
 * left, or right with a sticky bit when the exponent went below zero, and
 * rounded at its guard word: up when that word is above 0x8000, or exactly
 * 0x8000 with the bit above it set. Words 1..5 are the result's mantissa. */
static void crt_ld12mul(CRT_LD12 *a, const CRT_LD12 *b)
{
    uint16_t ea = ld_w(a, 10), eb = ld_w(b, 10);
    uint32_t sign = (uint32_t)((ea ^ eb) & 0x8000);
    uint32_t e;
    CRT_LD12 acc;
    uint16_t accw[7];
    int32_t  i, k, sticky = 0;

    ea &= 0x7FFF;
    eb &= 0x7FFF;
    e = (uint32_t)ea + eb;
    if (ea >= 0x7FFF || eb >= 0x7FFF || (uint16_t)e > 0xBFFD)
        goto overflow;
    if ((uint16_t)e <= 0x3FBF)
        goto zero;
    if (ea == 0) {
        e++;
        if (!(ld_dw(a, 8) & 0x7FFFFFFF) && !ld_dw(a, 4) && !ld_dw(a, 0))
            goto zero;
    }
    if (eb == 0) {
        e++;
        if (!(ld_dw(b, 8) & 0x7FFFFFFF) && !ld_dw(b, 4) && !ld_dw(b, 0))
            goto zero;
    }

    memset(accw, 0, sizeof accw);
    for (i = 0; i < 5; i++) {
        for (k = 0; k < 5 - i; k++) {
            uint32_t prod = (uint32_t)ld_w(a, (i + k) * 2) * ld_w(b, (4 - k) * 2);
            uint32_t sum;
            uint32_t lo = (uint32_t)accw[i] | ((uint32_t)accw[i + 1] << 16);
            if (crt_addl(lo, prod, &sum))
                accw[i + 2]++;
            accw[i] = (uint16_t)sum;
            accw[i + 1] = (uint16_t)(sum >> 16);
        }
    }
    memcpy(acc.b, accw, 12);

    e = (uint16_t)(e + 0xC002);         /* - 0x3FFE, in the original's 16 bits */
    if ((int16_t)e > 0) {
        while (!(acc.b[11] & 0x80) && (int16_t)e > 0) {
            crt_shl_12(&acc);
            e = (uint16_t)(e - 1);
        }
    }
    if ((int16_t)e <= 0) {
        e = (uint16_t)(e - 1);
        if ((int16_t)e < 0) {
            int32_t n = -(int16_t)e;
            e = 0;
            while (n--) {
                if (acc.b[0] & 1)
                    sticky++;
                crt_shr_12(&acc);
            }
            if (sticky)
                acc.b[0] |= 1;
        }
    }
    if (ld_w(&acc, 0) > 0x8000 || (ld_dw(&acc, 0) & 0x1FFFF) == 0x18000) {
        uint32_t m1 = ld_dw(&acc, 2);
        if (m1 == 0xFFFFFFFFu) {
            uint32_t m2;
            ld_set_dw(&acc, 2, 0);
            m2 = ld_dw(&acc, 6);
            if (m2 == 0xFFFFFFFFu) {
                ld_set_dw(&acc, 6, 0);
                if (ld_w(&acc, 10) == 0xFFFF) {
                    e = (uint16_t)(e + 1);
                    ld_set_w(&acc, 10, 0x8000);
                } else {
                    ld_set_w(&acc, 10, (uint16_t)(ld_w(&acc, 10) + 1));
                }
            } else {
                ld_set_dw(&acc, 6, m2 + 1);
            }
        } else {
            ld_set_dw(&acc, 2, m1 + 1);
        }
    }
    if ((uint16_t)e >= 0x7FFF)
        goto overflow;
    ld_set_w(a, 0, ld_w(&acc, 2));
    ld_set_dw(a, 2, ld_dw(&acc, 4));
    ld_set_dw(a, 6, ld_dw(&acc, 8));
    ld_set_w(a, 10, (uint16_t)(e | sign));
    return;

overflow:
    ld_set_dw(a, 0, 0);
    ld_set_dw(a, 4, 0);
    ld_set_dw(a, 8, 0x7FFF8000u | (sign ? 0x80000000u : 0));
    return;
zero:
    ld_set_dw(a, 0, 0);
    ld_set_dw(a, 4, 0);
    ld_set_dw(a, 8, 0);
}

/* 0x0046D097: x *= 10^pow, from the image's two tables -- seven entries a
 * group, the groups for the octal digits of |pow| -- with the guard word
 * cleared first when `rounding` is 0, and an entry whose guard word is
 * above half taken one below. */
static void crt_multtenpow12(CRT_LD12 *x, int32_t pow, int32_t rounding)
{
    const uint8_t *table = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_POW10_TABLE) - 0x60;

    if (pow == 0)
        return;
    if (pow < 0) {
        pow = -pow;
        table = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_POW10_NEG_TABLE) - 0x60;
    }
    if (rounding == 0)
        ld_set_w(x, 0, 0);
    while (pow != 0) {
        int32_t idx = pow & 7;
        table += 0x54;
        pow >>= 3;
        if (idx != 0) {
            CRT_LD12 entry;
            memcpy(entry.b, table + idx * 12, 12);
            if (ld_w(&entry, 0) >= 0x8000)
                ld_set_dw(&entry, 2, ld_dw(&entry, 2) - 1);
            crt_ld12mul(x, &entry);
        }
    }
}

/* 0x0046AD61: a double into the ten bytes below the guard word -- the
 * mantissa with its leading one made explicit and normalised, a denormal
 * shifted up until it is, a zero left as +0 whatever its sign. */
static void crt_dtold(CRT_LD12 *out, const double *value)
{
    uint32_t lo, hi;
    uint16_t top;
    uint32_t sign, exp, mhi, mlo, implicit = 0x80000000u;
    uint16_t e16;

    memcpy(&lo, value, 4);
    memcpy(&hi, (const uint8_t *)value + 4, 4);
    top = (uint16_t)(hi >> 16);
    sign = top & 0x8000;
    exp = (top >> 4) & 0x7FF;
    mhi = hi & 0xFFFFF;
    mlo = lo;
    if (exp != 0) {
        e16 = exp == 0x7FF ? 0x7FFF : (uint16_t)(exp + 0x3C00);
    } else {
        if (mhi == 0 && mlo == 0) {
            ld_set_dw(out, 2, 0);
            ld_set_dw(out, 6, 0);
            ld_set_w(out, 10, 0);
            return;
        }
        e16 = 0x3C01;
        implicit = 0;
    }
    {
        uint32_t h = (mlo >> 21) | (mhi << 11) | implicit;
        uint32_t l = mlo << 11;
        while (!(h & 0x80000000u)) {
            h = (h << 1) | (l >> 31);
            l <<= 1;
            e16 = (uint16_t)(e16 - 1);
        }
        ld_set_dw(out, 2, l);
        ld_set_dw(out, 6, h);
        ld_set_w(out, 10, (uint16_t)(e16 | sign));
    }
}

/* ---- $I10_OUTPUT ------------------------------------------------------------- */

/* What it fills: the decimal exponent, the sign character, the digit count
 * and the digits, at ADDR_CRT_FLTOUT_RAW. */
typedef struct CRT_FltRaw {
    int16_t exp;
    char    sign;
    uint8_t ndigits;
    char    digits[24];
} CRT_FltRaw;

/* 0x0046C8AF. `x` arrives as the ten bytes __dtold made, guard word absent.
 * Answers 1, or 0 for an infinity or a NaN, whose text is the CRT's own. */
static int32_t crt_i10_output(const CRT_LD12 *x, int32_t ndigits, int32_t flags, CRT_FltRaw *out)
{
    /* 0.1 as the original assembles it on its stack, byte by byte. */
    static const CRT_LD12 tenth_const = { { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xFB, 0x3F } };
    const CRT_LD12 *tenth = &tenth_const;
    CRT_LD12 work;
    uint16_t expw = ld_w(x, 10);
    uint32_t sign = expw & 0x8000, exp = expw & 0x7FFF;
    uint32_t mlo = ld_dw(x, 2), mhi = ld_dw(x, 6);
    int32_t  result = 1, decexp, n, count;
    char    *p;

    out->sign = sign ? '-' : ' ';
    if (exp == 0 && mhi == 0 && mlo == 0)
        goto zero;
    if (exp == 0x7FFF) {
        const char *text;
        out->exp = 1;
        if (!(mhi == 0x80000000u && mlo == 0) && !(mhi & 0x40000000u)) {
            text = "1#SNAN";
            out->ndigits = 6;
        } else if (sign && mhi == 0xC0000000u && mlo == 0) {
            text = "1#IND";
            out->ndigits = 5;
        } else if (mhi == 0x80000000u && mlo == 0) {
            text = "1#INF";
            out->ndigits = 5;
        } else {
            text = "1#QNAN";
            out->ndigits = 6;
        }
        crt_strcpy(out->digits, text);
        return 0;
    }

    /* The decimal exponent, estimated from the binary one with log10(2)
     * as 0x4D10 / 65536, then corrected below by one multiply. */
    decexp = (int16_t)(((int32_t)(exp * 0x4D10u) +
                        (int32_t)((((mhi >> 24) * 2) + (exp >> 8)) * 0x4Du) -
                        0x134312F4) >> 16);
    ld_set_w(&work, 0, 0);
    ld_set_dw(&work, 2, mlo);
    ld_set_dw(&work, 6, mhi);
    ld_set_w(&work, 10, (uint16_t)exp);
    crt_multtenpow12(&work, -decexp, 1);
    if (ld_w(&work, 10) >= 0x3FFF) {
        crt_ld12mul(&work, tenth);
        decexp++;
    }
    out->exp = (int16_t)decexp;
    if (flags & 1) {
        ndigits += decexp;
        if (ndigits <= 0)
            goto zero;
    }
    if (ndigits > 0x15)
        ndigits = 0x15;

    /* The value is now in [0.1, 1): with the exponent field cleared and
     * the mantissa shifted eight bits up, the top byte is the integer part
     * after each multiplication by ten. */
    n = (int32_t)ld_w(&work, 10) - 0x3FFE;
    ld_set_w(&work, 10, 0);
    for (count = 0; count < 8; count++)
        crt_shl_12(&work);
    if (n < 0) {
        n = (-n) & 0xFF;
        while (n-- > 0)
            crt_shr_12(&work);
    }
    p = out->digits;
    for (count = ndigits + 1; count > 0; count--) {
        CRT_LD12 tmp = work;
        crt_shl_12(&work);
        crt_shl_12(&work);
        crt_add_12(&work, &tmp);
        crt_shl_12(&work);
        *p++ = (char)(work.b[11] + '0');
        work.b[11] = 0;
    }

    /* The extra digit decides the rounding of the one before it. */
    {
        char *last = p - 2;
        if (p[-1] >= '5') {
            while (last >= out->digits && *last == '9')
                *last-- = '0';
            if (last < out->digits) {
                last++;
                out->exp++;
            }
            (*last)++;
        } else {
            while (last >= out->digits && *last == '0')
                last--;
            if (last < out->digits)
                goto zero;
        }
        out->ndigits = (uint8_t)(last - out->digits + 1);
        out->digits[out->ndigits] = 0;
    }
    return result;

zero:
    out->exp = 0;
    out->sign = ' ';
    out->ndigits = 1;
    out->digits[0] = '0';
    out->digits[1] = 0;
    return 1;
}

/* ---- _fltout and _fptostr ---------------------------------------------------- */

typedef struct CRT_StrFlt {
    int32_t sign;       /* the sign CHARACTER */
    int32_t decpt;
    int32_t flag;
    char   *mantissa;
} CRT_StrFlt;

#define crt_fltraw  ((CRT_FltRaw *)(uintptr_t)AM2_IMAGE(ADDR_CRT_FLTOUT_RAW))
#define crt_strflt  ((CRT_StrFlt *)(uintptr_t)AM2_IMAGE(ADDR_CRT_STRFLT))

/* 0x0046ACFD: seventeen digits of the value into the CRT's static
 * records, answering the STRFLT view of them. */
static CRT_StrFlt *crt_fltout(const double *value)
{
    CRT_LD12    ld;
    CRT_FltRaw *raw = crt_fltraw;
    CRT_StrFlt *flt = crt_strflt;

    crt_dtold(&ld, value);
    flt->flag = crt_i10_output(&ld, 0x11, 0, raw);
    flt->sign = raw->sign;
    flt->decpt = raw->exp;
    flt->mantissa = raw->digits;
    return flt;
}

/* 0x0046AC86: `ndigits` digits of the mantissa into `buf`, rounded on the
 * next one, with a leading '0' to catch a carry -- kept, and the decimal
 * point moved, when it becomes a '1'. */
static void crt_fptostr(char *buf, int32_t ndigits, CRT_StrFlt *flt)
{
    const char *mant = flt->mantissa;
    char       *p = buf + 1;

    buf[0] = '0';
    if (ndigits > 0) {
        int32_t n = ndigits;
        while (n-- > 0) {
            char c = *mant;
            if (c)
                mant++;
            else
                c = '0';
            *p++ = c;
        }
    }
    *p = 0;
    if (ndigits >= 0 && *mant >= '5') {
        p--;
        while (*p == '9')
            *p-- = '0';
        (*p)++;
    }
    if (buf[0] == '1')
        flt->decpt++;
    else
        crt_memmove(buf, buf + 1, (uint32_t)crt_strlen(buf + 1) + 1);
}

/* ---- the layouts --------------------------------------------------------------- */

#define crt_g_fmt        (*(uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CFTOG_ACTIVE))
#define crt_g_pflt       (*(CRT_StrFlt **)(uintptr_t)AM2_IMAGE(ADDR_CRT_CFTOG_PFLT))
#define crt_g_magnitude  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CFTOG_MAGNITUDE))
#define crt_g_expansion  (*(uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CFTOG_EXPANSION))
#define crt_decimal_point (*(const char *)(uintptr_t)AM2_IMAGE(ADDR_CRT_DECIMAL_POINT))

/* 0x00466A8E: open `dist` bytes at `s`. */
static void crt_shift(char *s, int32_t dist)
{
    if (dist)
        crt_memmove(s + dist, s, (uint32_t)crt_strlen(s) + 1);
}

/* 0x0046AE20: the CRT's memset. */
static void crt_fill(char *dst, int32_t c, int32_t n)
{
    while (n-- > 0)
        *dst++ = (char)c;
}

/* 0x0046687A. */
static char *crt_cftof(const double *value, char *buf, int32_t ndec)
{
    CRT_StrFlt *flt;
    char       *p;
    int32_t     decpt;

    if (crt_g_fmt) {
        flt = crt_g_pflt;
        if (crt_g_magnitude == ndec) {
            p = buf + (flt->sign == '-') + crt_g_magnitude;
            p[0] = '0';
            p[1] = 0;
        }
    } else {
        flt = crt_fltout(value);
        crt_fptostr(buf + (flt->sign == '-'), ndec + flt->decpt, flt);
    }
    p = buf;
    if (flt->sign == '-')
        *p++ = '-';
    decpt = flt->decpt;
    if (decpt <= 0) {
        crt_shift(p, 1);
        *p++ = '0';
    } else {
        p += decpt;
    }
    if (ndec > 0) {
        int32_t n;
        crt_shift(p, 1);
        *p++ = crt_decimal_point;
        n = flt->decpt;
        if (n < 0) {
            n = -n;
            if (!crt_g_fmt && ndec < n)
                n = ndec;
            crt_shift(p, n);
            crt_fill(p, '0', n);
        }
    }
    return buf;
}

/* 0x00466776. */
static char *crt_cftoe(const double *value, char *buf, int32_t ndec, int32_t caps)
{
    CRT_StrFlt *flt;
    char       *p, *e;
    int32_t     exp;

    if (crt_g_fmt) {
        flt = crt_g_pflt;
        crt_shift(buf + (flt->sign == '-'), ndec > 0);
    } else {
        flt = crt_fltout(value);
        crt_fptostr(buf + (flt->sign == '-') + (ndec > 0), ndec + 1, flt);
    }
    p = buf;
    if (flt->sign == '-')
        *p++ = '-';
    if (ndec > 0) {
        p[0] = p[1];
        p++;
        *p = crt_decimal_point;
    }
    e = p + ndec + (crt_g_fmt ? 0 : 1);
    crt_strcpy(e, (const char *)(uintptr_t)AM2_IMAGE(ADDR_CRT_EXPONENT_TEXT));
    if (caps)
        *e = 'E';
    e++;
    if (flt->mantissa[0] != '0') {
        exp = flt->decpt - 1;
        if (exp < 0) {
            exp = -exp;
            *e = '-';
        }
        e++;
        if (exp >= 100) {
            e[0] = (char)(e[0] + exp / 100);
            exp %= 100;
        }
        e++;
        if (exp >= 10) {
            e[0] = (char)(e[0] + exp / 10);
            exp %= 10;
        }
        e[1] = (char)(e[1] + exp);
    }
    return buf;
}

/* 0x00466A1A and 0x004669F3: the two above with the flag up, so they take
 * _cftog's digits rather than converting again. */
static char *crt_cftof2(const double *value, char *buf, int32_t ndec)
{
    char *r;
    crt_g_fmt = 1;
    r = crt_cftof(value, buf, ndec);
    crt_g_fmt = 0;
    return r;
}

static char *crt_cftoe2(const double *value, char *buf, int32_t ndec, int32_t caps)
{
    char *r;
    crt_g_fmt = 1;
    r = crt_cftoe(value, buf, ndec, caps);
    crt_g_fmt = 0;
    return r;
}

/* 0x00466958. */
static char *crt_cftog(const double *value, char *buf, int32_t ndec, int32_t caps)
{
    CRT_StrFlt *flt = crt_fltout(value);
    char       *p;
    int32_t     magnitude;

    crt_g_pflt = flt;
    crt_g_magnitude = flt->decpt - 1;
    p = buf + (flt->sign == '-');
    crt_fptostr(p, ndec, flt);
    magnitude = flt->decpt - 1;
    crt_g_expansion = crt_g_magnitude < magnitude;
    crt_g_magnitude = magnitude;
    if (magnitude < -4 || magnitude >= ndec)
        return crt_cftoe2(value, buf, ndec, caps);
    if (crt_g_expansion) {
        /* The original's walk steps past the terminator before it looks
         * back two: the last DIGIT goes, not the first. */
        while (*p++)
            ;
        p[-2] = 0;
    }
    return crt_cftof2(value, buf, ndec);
}

void __cdecl crt_cfltcvt(const double *value, char *buf, int32_t fmt, int32_t precision, int32_t caps)
{
    if (fmt == 'e' || fmt == 'E')
        crt_cftoe(value, buf, precision, caps);
    else if (fmt == 'f')
        crt_cftof(value, buf, precision);
    else
        crt_cftog(value, buf, precision, caps);
}

/* 0x004666D2: after the decimal point, drop trailing zeros and then the
 * point itself if nothing is left, keeping any exponent. */
void __cdecl crt_cropzeros(char *buf)
{
    char dp = crt_decimal_point;
    char *p = buf, *q;

    while (*p && *p != dp)
        p++;
    if (!*p++)
        return;
    while (*p && *p != 'e' && *p != 'E')
        p++;
    q = p - 1;
    while (*q == '0')
        q--;
    if (*q == dp)
        q--;
    q++;
    while ((*q++ = *p++) != 0)
        ;
}

/* 0x00466678: put a decimal point after the digits, before any exponent. */
void __cdecl crt_forcdecpt(char *buf)
{
    char *p = buf;
    char c;

    if (crt_tolower((uint8_t)*p) != 'e') {
        do {
            p++;
        } while (crt_isdigit((uint8_t)*p));
    }
    c = *p;
    *p++ = crt_decimal_point;
    for (;;) {
        char n = *p;
        *p++ = c;
        if (!c)
            break;
        c = n;
    }
}

/* 0x00466720: the table's `_positive`: 1 when the value is at least zero. */
int32_t __cdecl crt_positive(const double *value)
{
    return *value >= 0.0 ? 1 : 0;
}
