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

#ifdef AM2_STANDALONE
/* The two base-ten scaling tables crt_multtenpow12 reads at 0x0048D618 and
 * 0x0048D778: seven 12-byte 80-bit long-double entries per octal-digit group
 * (10^1..10^7, then 10^8.., etc.), addressed bytewise. Pure const float data,
 * so the blob copies drop -- placed at their VAs and byte-verified. */
/* The "." decimal-point string at 0x0048CEA8 (localeconv would rewrite it; the
 * game keeps the "C" locale). Read as a single char; kept 2 bytes so the run
 * is a clean symbol. */
extern "C" uint8_t am2_crt_decimal_point[2] = { 0x2E, 0x00 };
extern "C" const uint8_t am2_crt_pow10_table[352] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x05, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFA, 0x08, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x9C, 0x0C, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0xC3, 0x0F, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x24, 0xF4, 0x12, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x96, 0x98, 0x16, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0xBC, 0xBE, 0x19, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xBF, 0xC9, 0x1B, 0x8E, 0x34, 0x40, 0x00, 0x00, 0x00, 0xA1,
    0xED, 0xCC, 0xCE, 0x1B, 0xC2, 0xD3, 0x4E, 0x40, 0x20, 0xF0, 0x9E, 0xB5, 0x70, 0x2B, 0xA8, 0xAD,
    0xC5, 0x9D, 0x69, 0x40, 0xD0, 0x5D, 0xFD, 0x25, 0xE5, 0x1A, 0x8E, 0x4F, 0x19, 0xEB, 0x83, 0x40,
    0x71, 0x96, 0xD7, 0x95, 0x43, 0x0E, 0x05, 0x8D, 0x29, 0xAF, 0x9E, 0x40, 0xF9, 0xBF, 0xA0, 0x44,
    0xED, 0x81, 0x12, 0x8F, 0x81, 0x82, 0xB9, 0x40, 0xBF, 0x3C, 0xD5, 0xA6, 0xCF, 0xFF, 0x49, 0x1F,
    0x78, 0xC2, 0xD3, 0x40, 0x6F, 0xC6, 0xE0, 0x8C, 0xE9, 0x80, 0xC9, 0x47, 0xBA, 0x93, 0xA8, 0x41,
    0xBC, 0x85, 0x6B, 0x55, 0x27, 0x39, 0x8D, 0xF7, 0x70, 0xE0, 0x7C, 0x42, 0xBC, 0xDD, 0x8E, 0xDE,
    0xF9, 0x9D, 0xFB, 0xEB, 0x7E, 0xAA, 0x51, 0x43, 0xA1, 0xE6, 0x76, 0xE3, 0xCC, 0xF2, 0x29, 0x2F,
    0x84, 0x81, 0x26, 0x44, 0x28, 0x10, 0x17, 0xAA, 0xF8, 0xAE, 0x10, 0xE3, 0xC5, 0xC4, 0xFA, 0x44,
    0xEB, 0xA7, 0xD4, 0xF3, 0xF7, 0xEB, 0xE1, 0x4A, 0x7A, 0x95, 0xCF, 0x45, 0x65, 0xCC, 0xC7, 0x91,
    0x0E, 0xA6, 0xAE, 0xA0, 0x19, 0xE3, 0xA3, 0x46, 0x0D, 0x65, 0x17, 0x0C, 0x75, 0x81, 0x86, 0x75,
    0x76, 0xC9, 0x48, 0x4D, 0x58, 0x42, 0xE4, 0xA7, 0x93, 0x39, 0x3B, 0x35, 0xB8, 0xB2, 0xED, 0x53,
    0x4D, 0xA7, 0xE5, 0x5D, 0x3D, 0xC5, 0x5D, 0x3B, 0x8B, 0x9E, 0x92, 0x5A, 0xFF, 0x5D, 0xA6, 0xF0,
    0xA1, 0x20, 0xC0, 0x54, 0xA5, 0x8C, 0x37, 0x61, 0xD1, 0xFD, 0x8B, 0x5A, 0x8B, 0xD8, 0x25, 0x5D,
    0x89, 0xF9, 0xDB, 0x67, 0xAA, 0x95, 0xF8, 0xF3, 0x27, 0xBF, 0xA2, 0xC8, 0x5D, 0xDD, 0x80, 0x6E,
    0x4C, 0xC9, 0x9B, 0x97, 0x20, 0x8A, 0x02, 0x52, 0x60, 0xC4, 0x25, 0x75, 0x00, 0x00, 0x00, 0x00,
};
extern "C" const uint8_t am2_crt_pow10_neg_table[352] = {
    0xCD, 0xCC, 0xCD, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xFB, 0x3F, 0x71, 0x3D, 0x0A, 0xD7,
    0xA3, 0x70, 0x3D, 0x0A, 0xD7, 0xA3, 0xF8, 0x3F, 0x5A, 0x64, 0x3B, 0xDF, 0x4F, 0x8D, 0x97, 0x6E,
    0x12, 0x83, 0xF5, 0x3F, 0xC3, 0xD3, 0x2C, 0x65, 0x19, 0xE2, 0x58, 0x17, 0xB7, 0xD1, 0xF1, 0x3F,
    0xD0, 0x0F, 0x23, 0x84, 0x47, 0x1B, 0x47, 0xAC, 0xC5, 0xA7, 0xEE, 0x3F, 0x40, 0xA6, 0xB6, 0x69,
    0x6C, 0xAF, 0x05, 0xBD, 0x37, 0x86, 0xEB, 0x3F, 0x33, 0x3D, 0xBC, 0x42, 0x7A, 0xE5, 0xD5, 0x94,
    0xBF, 0xD6, 0xE7, 0x3F, 0xC2, 0xFD, 0xFD, 0xCE, 0x61, 0x84, 0x11, 0x77, 0xCC, 0xAB, 0xE4, 0x3F,
    0x2F, 0x4C, 0x5B, 0xE1, 0x4D, 0xC4, 0xBE, 0x94, 0x95, 0xE6, 0xC9, 0x3F, 0x92, 0xC4, 0x53, 0x3B,
    0x75, 0x44, 0xCD, 0x14, 0xBE, 0x9A, 0xAF, 0x3F, 0xDE, 0x67, 0xBA, 0x94, 0x39, 0x45, 0xAD, 0x1E,
    0xB1, 0xCF, 0x94, 0x3F, 0x24, 0x23, 0xC6, 0xE2, 0xBC, 0xBA, 0x3B, 0x31, 0x61, 0x8B, 0x7A, 0x3F,
    0x61, 0x55, 0x59, 0xC1, 0x7E, 0xB1, 0x53, 0x7C, 0x12, 0xBB, 0x5F, 0x3F, 0xD7, 0xEE, 0x2F, 0x8D,
    0x06, 0xBE, 0x92, 0x85, 0x15, 0xFB, 0x44, 0x3F, 0x24, 0x3F, 0xA5, 0xE9, 0x39, 0xA5, 0x27, 0xEA,
    0x7F, 0xA8, 0x2A, 0x3F, 0x7D, 0xAC, 0xA1, 0xE4, 0xBC, 0x64, 0x7C, 0x46, 0xD0, 0xDD, 0x55, 0x3E,
    0x63, 0x7B, 0x06, 0xCC, 0x23, 0x54, 0x77, 0x83, 0xFF, 0x91, 0x81, 0x3D, 0x91, 0xFA, 0x3A, 0x19,
    0x7A, 0x63, 0x25, 0x43, 0x31, 0xC0, 0xAC, 0x3C, 0x21, 0x89, 0xD1, 0x38, 0x82, 0x47, 0x97, 0xB8,
    0x00, 0xFD, 0xD7, 0x3B, 0xDC, 0x88, 0x58, 0x08, 0x1B, 0xB1, 0xE8, 0xE3, 0x86, 0xA6, 0x03, 0x3B,
    0xC6, 0x84, 0x45, 0x42, 0x07, 0xB6, 0x99, 0x75, 0x37, 0xDB, 0x2E, 0x3A, 0x33, 0x71, 0x1C, 0xD2,
    0x23, 0xDB, 0x32, 0xEE, 0x49, 0x90, 0x5A, 0x39, 0xA6, 0x87, 0xBE, 0xC0, 0x57, 0xDA, 0xA5, 0x82,
    0xA6, 0xA2, 0xB5, 0x32, 0xE2, 0x68, 0xB2, 0x11, 0xA7, 0x52, 0x9F, 0x44, 0x59, 0xB7, 0x10, 0x2C,
    0x25, 0x49, 0xE4, 0x2D, 0x36, 0x34, 0x4F, 0x53, 0xAE, 0xCE, 0x6B, 0x25, 0x8F, 0x59, 0x04, 0xA4,
    0xC0, 0xDE, 0xC2, 0x7D, 0xFB, 0xE8, 0xC6, 0x1E, 0x9E, 0xE7, 0x88, 0x5A, 0x57, 0x91, 0x3C, 0xBF,
    0x50, 0x83, 0x22, 0x18, 0x4E, 0x4B, 0x65, 0x62, 0xFD, 0x83, 0x8F, 0xAF, 0x06, 0x94, 0x7D, 0x11,
    0xE4, 0x2D, 0xDE, 0x9F, 0xCE, 0xD2, 0xC8, 0x04, 0xDD, 0xA6, 0xD8, 0x0A, 0x00, 0x00, 0x00, 0x00,
};
#endif

/* ---- the twelve-byte long double ------------------------------------------ */

/* CRT_LD12 is in crt.h: bytes 0-1 a guard word, 2-9 the mantissa, 10-11
 * the exponent and sign; w0, w1, w2 are the three dwords the shift helpers
 * move as a unit. strtod.cpp shares the four helpers below. */

uint32_t crt_ld_dw(const CRT_LD12 *x, int32_t at)
{
    uint32_t v;
    memcpy(&v, x->b + at, 4);
    return v;
}
void crt_ld_set_dw(CRT_LD12 *x, int32_t at, uint32_t v) { memcpy(x->b + at, &v, 4); }
uint16_t crt_ld_w(const CRT_LD12 *x, int32_t at)
{
    uint16_t v;
    memcpy(&v, x->b + at, 2);
    return v;
}
void crt_ld_set_w(CRT_LD12 *x, int32_t at, uint16_t v) { memcpy(x->b + at, &v, 2); }

/* 0x0046C78D: the 96 bits left one. */
void crt_shl_12(CRT_LD12 *x)
{
    uint32_t w0 = crt_ld_dw(x, 0), w1 = crt_ld_dw(x, 4), w2 = crt_ld_dw(x, 8);
    crt_ld_set_dw(x, 0, w0 << 1);
    crt_ld_set_dw(x, 4, (w1 << 1) | (w0 >> 31));
    crt_ld_set_dw(x, 8, (w2 << 1) | (w1 >> 31));
}

/* 0x0046C7BB: the 96 bits right one. */
static void crt_shr_12(CRT_LD12 *x)
{
    uint32_t w0 = crt_ld_dw(x, 0), w1 = crt_ld_dw(x, 4), w2 = crt_ld_dw(x, 8);
    crt_ld_set_dw(x, 4, (w1 >> 1) | (w2 << 31));
    crt_ld_set_dw(x, 0, (w0 >> 1) | (w1 << 31));
    crt_ld_set_dw(x, 8, w2 >> 1);
}

/* 0x0046C70E: *out = a + b, answering the carry. */
static int32_t crt_addl(uint32_t a, uint32_t b, uint32_t *out)
{
    uint32_t s = a + b;
    *out = s;
    return s < a || s < b;
}

/* 0x0046C72F: a += b over the 96 bits, carries rippling up. */
void crt_add_12(CRT_LD12 *a, const CRT_LD12 *b)
{
    uint32_t v;
    if (crt_addl(crt_ld_dw(a, 0), crt_ld_dw(b, 0), &v)) {
        crt_ld_set_dw(a, 0, v);
        if (crt_addl(crt_ld_dw(a, 4), 1, &v)) {
            crt_ld_set_dw(a, 4, v);
            crt_ld_set_dw(a, 8, crt_ld_dw(a, 8) + 1);
        } else {
            crt_ld_set_dw(a, 4, v);
        }
    } else {
        crt_ld_set_dw(a, 0, v);
    }
    if (crt_addl(crt_ld_dw(a, 4), crt_ld_dw(b, 4), &v)) {
        crt_ld_set_dw(a, 4, v);
        crt_ld_set_dw(a, 8, crt_ld_dw(a, 8) + 1);
    } else {
        crt_ld_set_dw(a, 4, v);
    }
    crt_addl(crt_ld_dw(a, 8), crt_ld_dw(b, 8), &v);
    crt_ld_set_dw(a, 8, v);
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
    uint16_t ea = crt_ld_w(a, 10), eb = crt_ld_w(b, 10);
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
        if (!(crt_ld_dw(a, 8) & 0x7FFFFFFF) && !crt_ld_dw(a, 4) && !crt_ld_dw(a, 0))
            goto zero;
    }
    if (eb == 0) {
        e++;
        if (!(crt_ld_dw(b, 8) & 0x7FFFFFFF) && !crt_ld_dw(b, 4) && !crt_ld_dw(b, 0))
            goto zero;
    }

    memset(accw, 0, sizeof accw);
    for (i = 0; i < 5; i++) {
        for (k = 0; k < 5 - i; k++) {
            uint32_t prod = (uint32_t)crt_ld_w(a, (i + k) * 2) * crt_ld_w(b, (4 - k) * 2);
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
    if (crt_ld_w(&acc, 0) > 0x8000 || (crt_ld_dw(&acc, 0) & 0x1FFFF) == 0x18000) {
        uint32_t m1 = crt_ld_dw(&acc, 2);
        if (m1 == 0xFFFFFFFFu) {
            uint32_t m2;
            crt_ld_set_dw(&acc, 2, 0);
            m2 = crt_ld_dw(&acc, 6);
            if (m2 == 0xFFFFFFFFu) {
                crt_ld_set_dw(&acc, 6, 0);
                if (crt_ld_w(&acc, 10) == 0xFFFF) {
                    e = (uint16_t)(e + 1);
                    crt_ld_set_w(&acc, 10, 0x8000);
                } else {
                    crt_ld_set_w(&acc, 10, (uint16_t)(crt_ld_w(&acc, 10) + 1));
                }
            } else {
                crt_ld_set_dw(&acc, 6, m2 + 1);
            }
        } else {
            crt_ld_set_dw(&acc, 2, m1 + 1);
        }
    }
    if ((uint16_t)e >= 0x7FFF)
        goto overflow;
    crt_ld_set_w(a, 0, crt_ld_w(&acc, 2));
    crt_ld_set_dw(a, 2, crt_ld_dw(&acc, 4));
    crt_ld_set_dw(a, 6, crt_ld_dw(&acc, 8));
    crt_ld_set_w(a, 10, (uint16_t)(e | sign));
    return;

overflow:
    crt_ld_set_dw(a, 0, 0);
    crt_ld_set_dw(a, 4, 0);
    crt_ld_set_dw(a, 8, 0x7FFF8000u | (sign ? 0x80000000u : 0));
    return;
zero:
    crt_ld_set_dw(a, 0, 0);
    crt_ld_set_dw(a, 4, 0);
    crt_ld_set_dw(a, 8, 0);
}

/* 0x0046D097: x *= 10^pow, from the image's two tables -- seven entries a
 * group, the groups for the octal digits of |pow| -- with the guard word
 * cleared first when `rounding` is 0, and an entry whose guard word is
 * above half taken one below. */
void crt_multtenpow12(CRT_LD12 *x, int32_t pow, int32_t rounding)
{
    const uint8_t *table = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_POW10_TABLE) - 0x60;

    if (pow == 0)
        return;
    if (pow < 0) {
        pow = -pow;
        table = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_POW10_NEG_TABLE) - 0x60;
    }
    if (rounding == 0)
        crt_ld_set_w(x, 0, 0);
    while (pow != 0) {
        int32_t idx = pow & 7;
        table += 0x54;
        pow >>= 3;
        if (idx != 0) {
            CRT_LD12 entry;
            memcpy(entry.b, table + idx * 12, 12);
            if (crt_ld_w(&entry, 0) >= 0x8000)
                crt_ld_set_dw(&entry, 2, crt_ld_dw(&entry, 2) - 1);
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
            crt_ld_set_dw(out, 2, 0);
            crt_ld_set_dw(out, 6, 0);
            crt_ld_set_w(out, 10, 0);
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
        crt_ld_set_dw(out, 2, l);
        crt_ld_set_dw(out, 6, h);
        crt_ld_set_w(out, 10, (uint16_t)(e16 | sign));
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
    uint16_t expw = crt_ld_w(x, 10);
    uint32_t sign = expw & 0x8000, exp = expw & 0x7FFF;
    uint32_t mlo = crt_ld_dw(x, 2), mhi = crt_ld_dw(x, 6);
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
    crt_ld_set_w(&work, 0, 0);
    crt_ld_set_dw(&work, 2, mlo);
    crt_ld_set_dw(&work, 6, mhi);
    crt_ld_set_w(&work, 10, (uint16_t)exp);
    crt_multtenpow12(&work, -decexp, 1);
    if (crt_ld_w(&work, 10) >= 0x3FFF) {
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
    n = (int32_t)crt_ld_w(&work, 10) - 0x3FFE;
    crt_ld_set_w(&work, 10, 0);
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
