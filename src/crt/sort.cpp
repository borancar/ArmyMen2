/* sort.cpp -- qsort and bsearch, from 0x004660B2 and 0x00466280.
 *
 * MSVC 6's qsort is not glibc's. It is a quicksort with a median pivot and
 * an explicit stack, and the order in which it leaves EQUAL keys is a
 * property of these exact comparisons and swaps: the game sorts what it
 * draws, and two records that compare equal are drawn in whichever order
 * the sort left them. So this follows the original's control flow and its
 * pointer arithmetic rather than being a quicksort in the abstract.
 */
#include "crt.h"

/* 0x00466254. Exchange two records byte by byte; nothing when they are the
 * same record or the width is zero. */
static void __cdecl crt_swap(uint8_t *a, uint8_t *b, uint32_t width)
{
    if (a == b || width == 0)
        return;
    do {
        uint8_t t = *b;
        *b++ = *a;
        *a++ = t;
    } while (--width != 0);
}

/* 0x00466206. Selection sort of the records from lo to hi inclusive: for
 * each position from lo up, find the LARGEST record at or after it and swap
 * it to hi, then step hi down. The inner scan keeps the first of equal
 * maxima (`jle` skips an update on a tie), which is what places ties. */
static void __cdecl crt_shortsort(uint8_t *lo, uint8_t *hi, uint32_t width,
                                  crt_compare_fn compare)
{
    while (hi > lo) {
        uint8_t *max = lo;
        uint8_t *p;

        for (p = lo + width; p <= hi; p += width)
            if (compare(p, max) > 0)
                max = p;
        crt_swap(max, hi, width);
        hi -= width;
    }
}

#define CRT_QSORT_CUTOFF 8
#define CRT_QSORT_STACK  30

void __cdecl crt_qsort(void *base, uint32_t num, uint32_t width, crt_compare_fn compare)
{
    uint8_t *lostk[CRT_QSORT_STACK];
    uint8_t *histk[CRT_QSORT_STACK];
    int32_t  stkptr = 0;
    uint8_t *lo, *hi, *mid, *loguy, *higuy;
    uint32_t size;

    if (num < 2 || width == 0)
        return;

    lo = (uint8_t *)base;
    hi = (uint8_t *)base + width * (num - 1);

recurse:
    size = (uint32_t)(hi - lo) / width + 1;

    if (size <= CRT_QSORT_CUTOFF) {
        crt_shortsort(lo, hi, width, compare);
    } else {
        /* The middle record is the pivot and is swapped to lo. */
        mid = lo + (size / 2) * width;
        crt_swap(mid, lo, width);

        loguy = lo;
        higuy = hi + width;

        for (;;) {
            /* Walk loguy up past everything not greater than the pivot. */
            do {
                loguy += width;
            } while (loguy <= hi && compare(loguy, lo) <= 0);

            /* Walk higuy down past everything not less than the pivot. */
            do {
                higuy -= width;
            } while (higuy > lo && compare(higuy, lo) >= 0);

            if (higuy < loguy)
                break;
            crt_swap(loguy, higuy, width);
        }

        /* The pivot goes to higuy, which is its final place. */
        crt_swap(lo, higuy, width);

        /* Push the larger side, recurse on the smaller: the original's
         * stack holds at most thirty ranges that way. */
        if (higuy - 1 - lo >= hi - loguy) {
            if (lo + width < higuy) {
                lostk[stkptr] = lo;
                histk[stkptr] = higuy - width;
                stkptr++;
            }
            if (loguy < hi) {
                lo = loguy;
                goto recurse;
            }
        } else {
            if (loguy < hi) {
                lostk[stkptr] = loguy;
                histk[stkptr] = hi;
                stkptr++;
            }
            if (lo + width < higuy) {
                hi = higuy - width;
                goto recurse;
            }
        }
    }

    stkptr--;
    if (stkptr >= 0) {
        lo = lostk[stkptr];
        hi = histk[stkptr];
        goto recurse;
    }
}

void *__cdecl crt_bsearch(const void *key, const void *base, uint32_t num,
                          uint32_t width, crt_compare_fn compare)
{
    const uint8_t *lo = (const uint8_t *)base;
    const uint8_t *hi = (const uint8_t *)base + (num - 1) * width;
    const uint8_t *mid;
    uint32_t       half;
    int32_t        result;

    while (lo <= hi) {
        half = num / 2;
        if (half == 0) {
            /* One record left: answer it if it matches. */
            if (num == 0)
                return NULL;
            return compare(key, lo) ? NULL : (void *)lo;
        }
        /* The probe is the middle, biased low on an even count. */
        mid = lo + (num & 1 ? half : half - 1) * width;
        result = compare(key, mid);
        if (result == 0)
            return (void *)mid;
        if (result < 0) {
            hi = mid - width;
            num = num & 1 ? half : half - 1;
        } else {
            lo = mid + width;
            num = half;
        }
    }
    return NULL;
}
