/* ApproxDist -- reconstructed from ArmyMen2.exe 0x0042DDE0.
 *
 * Octagonal distance approximation between two 16-bit map points, avoiding a
 * square root:
 *
 *     max(|dx|,|dy|) + min(|dx|,|dy|) / 2
 *
 * expressed by the original as |dx| + |dy| - min/2, which is the same thing
 * because |dx| + |dy| == max + min. It over-estimates true Euclidean distance
 * by at most about 12%, and never under-estimates -- the usual trade for
 * range checks and AI target selection on a 1999 CPU.
 *
 * Original name not recovered; ApproxDist is ours. 70 direct call sites, 369
 * calls in the first seconds of Boot Camp gameplay.
 *
 * The operands are read with movsx from offsets 0 and 2, so map points are a
 * pair of signed 16-bit coordinates.
 *
 * Original body, for reference:
 *      mov   eax,[esp+4] / mov edx,[esp+8]
 *      movsx esi,[eax]   / movsx ecx,[edx]  / sub ecx,esi      ; dx
 *      movsx esi,[eax+2] / movsx eax,[edx+2]/ sub eax,esi      ; dy
 *      neg on each if negative                                 ; abs
 *      edx = min(ecx,eax) ; sar edx,1
 *      eax = eax - edx + ecx
 */

#include "dist.h"
#include "../inject/patch.h"

#include <stdint.h>

int32_t __cdecl ApproxDist(const AM2_Point *a, const AM2_Point *b)
{
    int32_t dx = (int32_t)b->x - (int32_t)a->x;
    int32_t dy = (int32_t)b->y - (int32_t)a->y;
    int32_t lo;

    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;

    lo = (dx < dy) ? dx : dy;

    return dx + dy - (lo >> 1);
}

/* 0x0042DE20. The same octagonal approximation as ApproxDist -- max + min/2,
 * written as dx + dy - min/2 -- but handed the deltas rather than two points.
 * ApproxDist is this with the subtraction done for it. */
int32_t __cdecl ApproxDistXY(int32_t dx, int32_t dy)
{
    int32_t lo;

    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;

    lo = (dx < dy) ? dx : dy;

    return dx - (lo >> 1) + dy;
}

/* 0x0042DD90. Wraps in BOTH directions -- see the note in dist.h. */
int32_t __cdecl AngleDelta(uint32_t from, uint32_t to)
{
    int32_t d = (int32_t)(to & 0xFFu) - (int32_t)(from & 0xFFu);

    if (d > 0x80)
        d -= 0x100;
    else if (d < -0x80)
        d += 0x100;
    return d;
}

/* 0x0042DFB0. The mask to 8 bits happens AFTER the rounding term is added. */
int32_t __cdecl RoundTo8(int32_t value, uint32_t bits)
{
    uint32_t b = bits & 0xFFu;
    int32_t  rounded = (int32_t)((uint32_t)(value + (1 << (7 - b))) & 0xFFu);

    return rounded >> (8 - b);
}

int dist_install(void)
{
    int rc = 0;

    /* Accumulated, not returned from the first: a `return` on line one made
     * the three below dead code, and they were never installed at all. */
    rc |= patch_replace(ADDR_APPROX_DIST, (const void *)ApproxDist,
                        "ApproxDist", 2);
    rc |= patch_replace(ADDR_APPROX_DIST_XY, (const void *)ApproxDistXY,
                        "ApproxDistXY", 2);
    rc |= patch_replace(ADDR_ANGLE_DELTA, (const void *)AngleDelta,
                        "AngleDelta", 2);
    rc |= patch_replace(ADDR_ROUND_TO_8, (const void *)RoundTo8, "RoundTo8", 2);
    return rc;
}
