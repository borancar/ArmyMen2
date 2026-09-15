/* strtod.cpp -- strtod and the conversions under it, from the bodies at
 * 0x004653B7 (strtod), 0x00469858 (_fltin2), 0x0046BCBD (__strgtold12),
 * 0x0046C7E8 (__mtold12), 0x0046AC00 (__ld12tod), 0x0046AA94 (__ld12cvt)
 * and the seven mantissa helpers between 0x0046A89A and 0x0046AA07.
 *
 * Like the formatting in fltcvt.cpp, none of it is floating point: the
 * text becomes up to 24 decimal digits, the digits a twelve-byte long
 * double by repeated multiply-by-ten over three dwords, that a power of
 * ten from the image's tables, and the result a double by shifting and
 * rounding three dwords of mantissa. So it reproduces exactly in C, and
 * src/hybrid/crtcheck.cpp compares it against the original bit for bit.
 *
 * One rule worth stating because it is not the one you would write:
 * __ld12cvt rounds UP only when some bit lies beyond the position after
 * the round bit -- __round tests the bit at `nbits`, then asks whether
 * everything after position nbits+1 is clear -- so a value exactly half
 * way, and one with only the bit after the round bit set, both truncate.
 * The original's own answer on such inputs is what the check compares.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The two __ld12cvt parameter blocks at 0x0048D3C8: [0] double (IEEE-754
 * binary64) and [1] single (binary32); __ld12cvt for a double reads [0].
 * Pure const data, so the blob copy drops -- placed at its VA, verified.
 * (Its ADDRESS is also crt_nmsg_write's scan bound for the RTERR table.) */
extern "C" const CRT_CVTINFO am2_crt_cvtinfo_double[2] = {
    { 1024, -1023, 53, 11, 64, 1023 },   /* double: max/min exp, 53 mant, 11 exp, 8 bytes, bias 1023 */
    {  128,  -127, 24,  8, 32,  127 },   /* single: 24 mant, 8 exp, 4 bytes, bias 127 */
};

/* The double crt_strtod answers on overflow: +HUGE_VAL (0x7FF0..0 = +inf) at
 * 0x0048CEB0. Transcribed as the byte pattern (a numeric long-double literal
 * would not byte-match); read back as a double. */
extern "C" const uint8_t am2_crt_huge_val[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F };
/* The _fltin2 result-record pointer slot at 0x0048CEB8 (points at a CRT_FLT in
 * origbss, which is placed, so the raw value resolves); runtime-written. */
extern "C" uint32_t am2_crt_flt_ptr = 0x00664650u;
#endif

#define crt_errno       (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ERRNO))
#define crt_mb_cur_max  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MB_CUR_MAX))
#define crt_pctype_tab  (*(const uint16_t **)(uintptr_t)AM2_IMAGE(ADDR_CRT_PCTYPE))
#define crt_decimal_pt  (*(const char *)(uintptr_t)AM2_IMAGE(ADDR_CRT_DECIMAL_POINT))
#define crt_huge_val    (*(const double *)(uintptr_t)AM2_IMAGE(ADDR_CRT_HUGE_VAL))
#define crt_flt         (*(CRT_FLT **)(uintptr_t)AM2_IMAGE(ADDR_CRT_FLT_PTR))
#define crt_cvt_double  ((const CRT_CVTINFO *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CVTINFO_DOUBLE))

#define CRT_ERANGE      34
#define CRT_SPACE_MASK  8
#define CRT_DIGIT_MASK  4

#define CRT_SLD_UNDERFLOW 1
#define CRT_SLD_OVERFLOW  2
#define CRT_SLD_NODIGITS  4

#define CRT_FL_OVERFLOW  0x080
#define CRT_FL_UNDERFLOW 0x100
#define CRT_FL_BAD       0x200

#define CRT_MAX_DIGITS   24
#define CRT_MAX_EXP      5200

/* isdigit / isspace the way these bodies ask: through _isctype when the
 * code page is multibyte, straight from the table otherwise. */
static int32_t is_class(uint8_t c, int32_t mask)
{
    if (crt_mb_cur_max > 1)
        return crt_isctype(c, mask);
    return ((const uint8_t *)crt_pctype_tab)[c * 2] & mask;
}

/* ---- the three-dword mantissa __ld12cvt works on ------------------------ */
/* m[0] is the most significant dword; a bit POSITION counts from its top. */

/* 0x0046A89A. */
static int32_t mant_zero_tail(const uint32_t *m, int32_t pos)
{
    int32_t  q = pos / 32, r = pos % 32;
    uint32_t mask = ~(0xFFFFFFFFu << (31 - r));   /* the bits after position pos */
    int32_t  i;

    if (m[q] & mask)
        return 0;
    for (i = q + 1; i < 3; i++)
        if (m[i] != 0)
            return 0;
    return 1;
}

/* 0x0046A8E3. */
static int32_t mant_incr(uint32_t *m, int32_t pos)
{
    int32_t  q = pos / 32, r = pos % 32;
    uint32_t bit = 1u << (31 - r);
    uint32_t sum = m[q] + bit;
    int32_t  carry = sum < m[q];

    m[q] = sum;
    for (q--; q >= 0 && carry; q--) {
        sum = m[q] + 1;
        carry = sum < m[q];
        m[q] = sum;
    }
    return carry;
}

/* 0x0046A939. Keep `nbits` bits; the carry out of the top. */
static int32_t mant_round(uint32_t *m, int32_t nbits)
{
    int32_t  q = nbits / 32, r = 31 - nbits % 32;
    int32_t  carry = 0;
    int32_t  i;

    if (m[q] & (1u << r)) {
        if (!mant_zero_tail(m, nbits + 1))
            carry = mant_incr(m, nbits - 1);
    }
    m[q] &= 0xFFFFFFFFu << r;
    for (i = q + 1; i < 3; i++)
        m[i] = 0;
    return carry;
}

/* 0x0046AA07. Right by n, bit by bit then dword by dword. */
static void mant_shr(uint32_t *m, int32_t n)
{
    int32_t  q = n / 32, r = n % 32;
    uint32_t carry = 0;
    int32_t  i;

    for (i = 0; i < 3; i++) {
        uint32_t keep = m[i] & ~(0xFFFFFFFFu << r);
        m[i] = (m[i] >> r) | carry;
        carry = keep << (32 - r);
    }
    for (i = 2; i >= 0; i--)
        m[i] = i >= q ? m[i - q] : 0;
}

/* 0x0046A9EC. */
static int32_t mant_is_zero(const uint32_t *m)
{
    return m[0] == 0 && m[1] == 0 && m[2] == 0;
}

int32_t __cdecl crt_ld12cvt(const CRT_LD12 *x, void *out, const CRT_CVTINFO *cvt)
{
    uint32_t m[3], saved[3];
    uint32_t sign = crt_ld_w(x, 10) & 0x8000;
    int32_t  exp = (crt_ld_w(x, 10) & 0x7FFF) - 0x3FFF;
    uint32_t expfield;
    int32_t  rc;

    m[0] = crt_ld_dw(x, 6);
    m[1] = crt_ld_dw(x, 2);
    m[2] = (uint32_t)crt_ld_w(x, 0) << 16;

    if (exp == -0x3FFF) {
        /* A zero, or a long double that is itself denormal. */
        if (mant_is_zero(m)) {
            expfield = 0;
            rc = 0;
        } else {
            m[0] = m[1] = m[2] = 0;
            expfield = 0;
            rc = 2;
        }
    } else {
        saved[0] = m[0]; saved[1] = m[1]; saved[2] = m[2];
        if (mant_round(m, cvt->mantbits))
            exp++;
        if (exp < cvt->min_exp - cvt->mantbits) {
            m[0] = m[1] = m[2] = 0;
            expfield = 0;
            rc = 2;
        } else if (exp <= cvt->min_exp) {
            /* A denormal: the unrounded mantissa shifted down into place,
             * rounded there, and no implicit bit to drop. */
            int32_t shift = cvt->min_exp - exp;

            m[0] = saved[0]; m[1] = saved[1]; m[2] = saved[2];
            mant_shr(m, shift);
            mant_round(m, cvt->mantbits);
            mant_shr(m, cvt->expbits + 1);
            expfield = 0;
            rc = 2;
        } else if (exp >= cvt->max_exp) {
            m[0] = m[1] = m[2] = 0;
            m[0] |= 0x80000000u;
            mant_shr(m, cvt->expbits);
            expfield = (uint32_t)(cvt->bias + cvt->max_exp);
            rc = 1;
        } else {
            m[0] &= 0x7FFFFFFFu;   /* the explicit leading one goes */
            mant_shr(m, cvt->expbits);
            expfield = (uint32_t)(cvt->bias + exp);
            rc = 0;
        }
    }

    expfield <<= 31 - cvt->expbits;
    if (sign)
        expfield |= 0x80000000u;
    expfield |= m[0];
    if (cvt->size == 64) {
        ((uint32_t *)out)[1] = expfield;
        ((uint32_t *)out)[0] = m[1];
    } else if (cvt->size == 32) {
        ((uint32_t *)out)[0] = expfield;
    }
    return rc;
}

int32_t __cdecl crt_ld12tod(const CRT_LD12 *x, double *out)
{
    return crt_ld12cvt(x, out, crt_cvt_double);
}

void __cdecl crt_mtold12(const char *digits, uint32_t n, CRT_LD12 *out)
{
    uint32_t exp = 0x404E;   /* the binary point after bit 79 */
    uint32_t i;

    crt_ld_set_dw(out, 0, 0);
    crt_ld_set_dw(out, 4, 0);
    crt_ld_set_dw(out, 8, 0);
    for (i = 0; i < n; i++) {
        CRT_LD12 tmp = *out, digit;

        crt_shl_12(out);
        crt_shl_12(out);
        crt_add_12(out, &tmp);
        crt_shl_12(out);
        crt_ld_set_dw(&digit, 0, (uint32_t)(int32_t)(int8_t)digits[i]);
        crt_ld_set_dw(&digit, 4, 0);
        crt_ld_set_dw(&digit, 8, 0);
        crt_add_12(out, &digit);
    }
    /* Normalise: a word at a time while the top dword is empty (a zero
     * never comes here: the caller keeps at least one non-zero digit),
     * then a bit at a time until bit 79 is set. */
    while (crt_ld_dw(out, 8) == 0) {
        uint32_t w0 = crt_ld_dw(out, 0), w1 = crt_ld_dw(out, 4);

        crt_ld_set_dw(out, 8, w1 >> 16);
        crt_ld_set_dw(out, 4, (w1 << 16) | (w0 >> 16));
        crt_ld_set_dw(out, 0, w0 << 16);
        exp -= 16;
    }
    while (!(crt_ld_dw(out, 8) & 0x8000)) {
        crt_shl_12(out);
        exp--;
    }
    crt_ld_set_w(out, 10, (uint16_t)exp);
}

uint32_t __cdecl crt_strgtold12(CRT_LD12 *out, const char **end, const char *s,
                                int32_t mult12, int32_t scale, int32_t decpt, int32_t implicit_e)
{
    char        buf[CRT_MAX_DIGITS + 1];
    char       *bp = buf;
    const char *p = s, *fallback = s;
    uint32_t    ndigits = 0;
    int32_t     decexp = 0, expsign = 1, expval = 0;
    int32_t     started = 0, expseen = 0, decptseen = 0;
    uint32_t    sign = 0;
    uint32_t    flags = 0;
    int32_t     state = 0;
    uint8_t     ch;
    CRT_LD12    ld;
    uint32_t    w0, w1, w2, wexp;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    /* The state machine; `ch` is consumed unless the arm steps `p` back. */
    for (;;) {
        ch = (uint8_t)*p++;
        switch (state) {
        case 0:   /* the start */
            if (ch >= '1' && ch <= '9') { state = 3; p--; }
            else if (ch == crt_decimal_pt) state = 5;
            else if (ch == '+') { sign = 0; state = 2; }
            else if (ch == '-') { sign = 0x8000; state = 2; }
            else if (ch == '0') state = 1;
            else { p--; goto done; }
            break;
        case 1:   /* leading zeros */
            started = 1;
            if (ch >= '1' && ch <= '9') { state = 3; p--; }
            else if (ch == crt_decimal_pt) state = 4;
            else if (ch == '+' || ch == '-') { p--; state = 11; }
            else if (ch == '0') state = 1;
            else goto exponent_letter;
            break;
        case 2:   /* after the sign */
            if (ch >= '1' && ch <= '9') { state = 3; p--; }
            else if (ch == crt_decimal_pt) state = 5;
            else if (ch == '0') state = 1;
            else { p = s; goto done; }
            break;
        case 3:   /* the integer digits */
            started = 1;
            while (is_class(ch, CRT_DIGIT_MASK)) {
                if (ndigits < CRT_MAX_DIGITS + 1) {
                    *bp++ = (char)(ch - '0');
                    ndigits++;
                } else {
                    decexp++;
                }
                ch = (uint8_t)*p++;
            }
            if (ch == crt_decimal_pt) { state = 4; break; }
            goto after_mantissa;
        case 4:   /* the fraction digits */
            started = 1;
            decptseen = 1;
            if (ndigits == 0) {
                while (ch == '0') {
                    decexp--;
                    ch = (uint8_t)*p++;
                }
            }
            while (is_class(ch, CRT_DIGIT_MASK)) {
                if (ndigits < CRT_MAX_DIGITS + 1) {
                    *bp++ = (char)(ch - '0');
                    ndigits++;
                    decexp--;
                }
                ch = (uint8_t)*p++;
            }
            goto after_mantissa;
        case 5:   /* a decimal point with nothing before it */
            decptseen = 1;
            if (is_class(ch, CRT_DIGIT_MASK)) { state = 4; p--; }
            else { p = s; goto done; }
            break;
        case 6:   /* after E: the number may end before it */
            fallback = p - 2;
            if (ch >= '1' && ch <= '9') { state = 9; p--; }
            else if (ch == '+') state = 7;
            else if (ch == '-') { expsign = -1; state = 7; }
            else if (ch == '0') state = 8;
            else { p = fallback; goto done; }
            break;
        case 7:   /* after the exponent's sign */
            if (ch >= '1' && ch <= '9') { state = 9; p--; }
            else if (ch == '0') state = 8;
            else { p = fallback; goto done; }
            break;
        case 8:   /* the exponent's leading zeros */
            expseen = 1;
            while (ch == '0')
                ch = (uint8_t)*p++;
            if (ch >= '1' && ch <= '9') { state = 9; p--; }
            else { p--; goto done; }
            break;
        case 9:   /* the exponent's digits, capped just past the range */
            expseen = 1;
            expval = 0;
            while (is_class(ch, CRT_DIGIT_MASK)) {
                expval = expval * 10 + (int8_t)ch - '0';
                if (expval > CRT_MAX_EXP) {
                    expval = CRT_MAX_EXP + 1;
                    break;
                }
                ch = (uint8_t)*p++;
            }
            while (is_class(ch, CRT_DIGIT_MASK))
                ch = (uint8_t)*p++;
            p--;
            goto done;
        case 11:  /* a sign after the mantissa: an exponent only if asked.
                   * Without it the original steps back and ends at once;
                   * its state 10 is that exit, never dispatched from here. */
            if (implicit_e == 0) { p--; goto done; }
            fallback = p - 1;
            if (ch == '+') state = 7;
            else if (ch == '-') { expsign = -1; state = 7; }
            else { p = fallback; goto done; }
            break;
        default:
            goto done;
        }
        continue;

after_mantissa:
        if (ch == '+' || ch == '-') { p--; state = 11; continue; }
exponent_letter:
        if (ch == 'D' || ch == 'E' || ch == 'd' || ch == 'e') { state = 6; continue; }
        p--;
        goto done;
    }

done:
    *end = p;
    if (!started) {
        w0 = w1 = w2 = wexp = 0;
        flags = CRT_SLD_NODIGITS;
        goto store;
    }
    /* Twenty-four digits are kept; a twenty-fifth rounds the last of them
     * (a 5 or more becomes 6 or more -- a value of 10 is left for
     * __mtold12 to add as ten), and every digit dropped scales up. */
    if (ndigits > CRT_MAX_DIGITS) {
        if (buf[CRT_MAX_DIGITS - 1] >= 5)
            buf[CRT_MAX_DIGITS - 1]++;
        ndigits = CRT_MAX_DIGITS;
        bp = buf + CRT_MAX_DIGITS;
        decexp++;
    }
    while (ndigits > 0 && bp[-1] == 0) {
        ndigits--;
        decexp++;
        bp--;
    }
    if (ndigits == 0) {
        w0 = w1 = w2 = wexp = 0;
        goto store;
    }
    {
        int32_t exp;

        crt_mtold12(buf, ndigits, &ld);
        exp = expsign < 0 ? -expval : expval;
        exp += decexp;
        if (!expseen)
            exp += scale;
        if (!decptseen)
            exp -= decpt;
        if (exp > CRT_MAX_EXP) {
            w0 = 0; w1 = 0; w2 = 0x80000000u; wexp = 0x7FFF;
            flags = CRT_SLD_OVERFLOW;
        } else if (exp < -CRT_MAX_EXP) {
            w0 = w1 = w2 = wexp = 0;
            flags = CRT_SLD_UNDERFLOW;
        } else {
            crt_multtenpow12(&ld, exp, mult12);
            w0 = crt_ld_w(&ld, 0);
            w1 = crt_ld_dw(&ld, 2);
            w2 = crt_ld_dw(&ld, 6);
            wexp = crt_ld_w(&ld, 10);
        }
    }
store:
    crt_ld_set_dw(out, 6, w2);
    crt_ld_set_dw(out, 2, w1);
    crt_ld_set_w(out, 10, (uint16_t)(wexp | sign));
    crt_ld_set_w(out, 0, (uint16_t)w0);
    return flags;
}

CRT_FLT *__cdecl crt_fltin2(const char *s, int32_t len, int32_t a, int32_t b)
{
    CRT_FLT    *flt = crt_flt;
    CRT_LD12    ld;
    const char *end;
    double      dval;
    uint32_t    sld;
    int32_t     flags;

    (void)len; (void)a; (void)b;
    sld = crt_strgtold12(&ld, &end, s, 0, 0, 0, 0);
    if (sld & CRT_SLD_NODIGITS) {
        flags = CRT_FL_BAD;
        dval = 0.0;
    } else {
        int32_t r = crt_ld12tod(&ld, &dval);

        flags = 0;
        if ((sld & CRT_SLD_OVERFLOW) || r == 1)
            flags = CRT_FL_OVERFLOW;
        if ((sld & CRT_SLD_UNDERFLOW) || r == 2)
            flags |= CRT_FL_UNDERFLOW;
    }
    flt->flags = flags;
    flt->nbytes = (int32_t)(end - s);
    flt->dval = dval;
    return flt;
}

double __cdecl crt_strtod(const char *s, char **end)
{
    const char *p = s;
    CRT_FLT    *flt;

    while (is_class((uint8_t)*p, CRT_SPACE_MASK))
        p++;
    flt = crt_fltin2(p, crt_strlen(p), 0, 0);
    if (end)
        *end = (char *)p + flt->nbytes;
    if (flt->flags & (CRT_FL_BAD | 0x40)) {
        if (end)
            *end = (char *)s;
        return 0.0;
    }
    if (flt->flags & (CRT_FL_OVERFLOW | 1)) {
        crt_errno = CRT_ERANGE;
        return *p == '-' ? -crt_huge_val : crt_huge_val;
    }
    if (flt->flags & CRT_FL_UNDERFLOW) {
        crt_errno = CRT_ERANGE;
        return 0.0;
    }
    return flt->dval;
}
