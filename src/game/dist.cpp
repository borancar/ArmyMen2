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

int dist_install(void)
{
    return patch_replace(ADDR_APPROX_DIST, (const void *)ApproxDist, "ApproxDist", 2);
}
