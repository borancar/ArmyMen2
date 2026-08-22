/* trig.cpp -- see trig.h. */
#include <stdint.h>

#include "image.h"
#include "trig.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kCos   ((float *)AM2_IMAGE(ADDR_TRIG_COS))
#define kSin   ((float *)AM2_IMAGE(ADDR_TRIG_SIN))
/* Centres, so these are indexed by the signed ratio directly -- see orig.h. */
#define kAtanC ((int8_t *)AM2_IMAGE(ADDR_TRIG_ATAN_COS))
#define kAtanS ((int8_t *)AM2_IMAGE(ADDR_TRIG_ATAN_SIN))

#define kD(a)  (*(const double *)AM2_IMAGE(a))

/* fsin and fcos, not libm's. The original computes both from one loaded angle
 * with the x87 instructions, and glibc's sin and cos are not required to agree
 * with them in the last bits. We are in the game's own process and therefore
 * under the control word its CRT set, so these give exactly what the original
 * gave -- verified, all 4098 bytes, by tools/trigdump.py.
 *
 * Measured, and the measurement did NOT go the way the paragraph above
 * implies: building the tables with libm instead -- __builtin_cos, which on
 * this target really is a call and not an inlined fcos -- produces the same
 * 4098 bytes. The results are rounded to float on the way into the table, and
 * 24 bits of mantissa hide the disagreement for all 256 of these arguments.
 * So this is the conservative choice rather than a demonstrated requirement:
 * it is what the original executes, and the equivalence is a fact about this
 * data rather than a guarantee. Anyone who finds the asm inconvenient should
 * know it can go, and that no test here would notice. */
static inline double x87cos(double a)
{
    double r;
    __asm__ ("fcos" : "=t" (r) : "0" (a));
    return r;
}

static inline double x87sin(double a)
{
    double r;
    __asm__ ("fsin" : "=t" (r) : "0" (a));
    return r;
}

void __cdecl BuildTrigTables(void)
{
    /* The angle is ((i + 64) & 255) -- a quarter turn of phase, so index 0 is
     * not angle 0. The store index runs one ahead of the loop counter in the
     * original because it increments before the two fstp's; written out, that
     * is simply i over 0..255. */
    for (int32_t i = 0; i < 256; i++) {
        int32_t step = (i + 0x40) & 0xFF;
        double angle = (double)step * kD(ADDR_DBL_TWO_PI)
                       * kD(ADDR_DBL_ONE_256);

        kCos[i] = (float)x87cos(angle);
        kSin[i] = (float)(x87sin(angle) * kD(ADDR_DBL_SIN_SCALE));
    }

    /* Reverse lookup, twice over, the two differing only in which table is the
     * numerator. `ratio` walks -512..512 inclusive, so 1025 entries.
     *
     * The zero test is on the DENOMINATOR and it skips the candidate rather
     * than the entry: an angle whose denominator is zero contributes nothing
     * and the best-so-far stands. If every candidate is skipped the entry
     * keeps the byte from the initial best, which is 0. */
    for (int32_t pass = 0; pass < 2; pass++) {
        const float *num = pass ? kCos : kSin;
        const float *den = pass ? kSin : kCos;
        int8_t      *out = pass ? kAtanS : kAtanC;

        for (int32_t ratio = -AM2_TRIG_ATAN_RANGE;
             ratio <= AM2_TRIG_ATAN_RANGE; ratio++) {
            int8_t best = 0;
            /* 0x43000000 is 128.0f -- the starting distance, wider than any
             * real one, held as a float the whole way. */
            float bestd = 128.0f;

            for (int32_t a = 0; a <= 0x80; a++) {
                if (den[a] == kD(ADDR_DBL_ZERO))
                    continue;

                double d = (double)num[a] * kD(ADDR_DBL_512) / (double)den[a]
                           - (double)ratio;

                if (d < 0)
                    d = -d;
                if (d < (double)bestd) {
                    bestd = (float)d;
                    best = (int8_t)a;
                }
            }
            out[ratio] = best;
        }
    }
}

#define g_trigCos ((const float *)(uintptr_t)ADDR_TRIG_COS)
#define g_trigSin ((const float *)(uintptr_t)ADDR_TRIG_SIN)

float __cdecl Cos8(int32_t heading)
{
    return g_trigCos[heading & 0xFF];
}

float __cdecl Sin8(int32_t heading)
{
    return g_trigSin[heading & 0xFF];
}

int trig_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_COS8, (const void *)Cos8, "Cos8", 1);
    rc |= patch_replace(ADDR_SIN8, (const void *)Sin8, "Sin8", 1);
    rc |= patch_replace(ADDR_BUILD_TRIG_TABLES, (const void *)BuildTrigTables,
                        "BuildTrigTables", 1);
    return rc;
}
