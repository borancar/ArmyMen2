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

/* 0x0042DE20. The same octagonal approximation as ApproxDist, handed the
 * deltas rather than two points; ApproxDist is this with the subtraction done
 * for it.
 *
 * THIS COMMENT CALLED IT "max + min/2, written as dx + dy - min/2" AND THOSE
 * ARE NOT THE SAME FUNCTION. `dx + dy - (min >> 1)` is max + min - min/2,
 * which is max + CEIL(min/2), so the two differ for every odd min --
 * ApproxDistXY(4, 3) is 6 and max + min/2 is 5. The code was always right; the
 * prose beside it was a paraphrase that rounded the wrong way, and it was
 * copied into tools/pathcheck.py's model, where it disagreed with the original
 * on 40 of 81 small deltas and moved enough A* ties to pick different routes.
 * A formula restated in words is a second implementation and can be wrong on
 * its own. */
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

/* 0x0042DFE0, five callers, and the name is ours -- the function carries no
 * string. A power of two in 1..0x8000 becomes its bit index; everything else,
 * including 0 and anything with more than one bit set, becomes 0, which is the
 * same answer as for 1.
 *
 * MSVC compiled it as a 128-byte index table at 0x0042E08C over `value - 1`
 * feeding a nine-entry jump table at 0x0042E068 -- eight arms and a default --
 * and a compare chain for 0x100 upwards. The index table is 0, 1, 8, 2, 8, 8,
 * 8, 3, ... : non-default only at 0, 1, 3, 7, 15, 31, 63 and 127, so the low
 * half really is powers of two and nothing else.
 *
 * It writes `al` and leaves the rest of `eax` holding `value` or `value - 1`,
 * which is why the vectors compare a byte here. Its callers all store the
 * result into a byte field -- LoadAnimTable's is AM2_Anim::facingBits, where
 * the argument is the number of directions and the answer is the shift from
 * an 8-bit heading to one of them.
 */
uint8_t __cdecl Log2Mask(int32_t value)
{
    switch (value) {
    case 0x0001: return 0;
    case 0x0002: return 1;
    case 0x0004: return 2;
    case 0x0008: return 3;
    case 0x0010: return 4;
    case 0x0020: return 5;
    case 0x0040: return 6;
    case 0x0080: return 7;
    case 0x0100: return 8;
    case 0x0200: return 9;
    case 0x0400: return 10;
    case 0x0800: return 11;
    case 0x1000: return 12;
    case 0x2000: return 13;
    case 0x4000: return 14;
    case 0x8000: return 15;
    default:     return 0;
    }
}

#define g_atanSin ((const int8_t *)(uintptr_t)ADDR_TRIG_ATAN_SIN)
#define g_atanCos ((const int8_t *)(uintptr_t)ADDR_TRIG_ATAN_COS)

uint8_t __cdecl AngleOfDelta(int32_t dx, int32_t dy)
{
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    uint8_t h;

    if (adx > ady) {
        h = (uint8_t)g_atanCos[(dy << 9) / dx];
    } else {
        if (dx == 0)
            /* Straight up or straight down, and nothing to divide by. */
            return dy < 0 ? 0u : 0x80u;
        h = (uint8_t)g_atanSin[(dx << 9) / dy];
    }
    /* Eight-bit add, so it wraps inside the byte. */
    if (dx > 0)
        h = (uint8_t)(h + 0x80u);
    return h;
}

uint8_t __cdecl AngleBetween(const AM2_Point *from, const AM2_Point *to)
{
    /* Inlined in the original; one copy here. */
    return AngleOfDelta((int32_t)to->x - (int32_t)from->x,
                        (int32_t)to->y - (int32_t)from->y);
}

void __cdecl DistAndAngle(const AM2_Point *a, const AM2_Point *b,
                          int32_t *dist, uint8_t *angle)
{
    /* Both inlined in the original, and both to the instruction. The distance
     * goes out first. */
    *dist  = ApproxDist(a, b);
    *angle = AngleBetween(a, b);
}

int dist_install(void)
{
    int rc = 0;

    /* Accumulated, not returned from the first: a `return` on line one made
     * the three below dead code, and they were never installed at all. */
    rc |= patch_replace(ADDR_ANGLE_BETWEEN, (const void *)AngleBetween,
                        "AngleBetween", 2);
    rc |= patch_replace(ADDR_ANGLE_OF_DELTA, (const void *)AngleOfDelta,
                        "AngleOfDelta", 2);
    rc |= patch_replace(ADDR_DIST_AND_ANGLE, (const void *)DistAndAngle,
                        "DistAndAngle", 4);
    rc |= patch_replace(ADDR_APPROX_DIST, (const void *)ApproxDist,
                        "ApproxDist", 2);
    rc |= patch_replace(ADDR_APPROX_DIST_XY, (const void *)ApproxDistXY,
                        "ApproxDistXY", 2);
    rc |= patch_replace(ADDR_ANGLE_DELTA, (const void *)AngleDelta,
                        "AngleDelta", 2);
    rc |= patch_replace(ADDR_ROUND_TO_8, (const void *)RoundTo8, "RoundTo8", 2);
    rc |= patch_replace(ADDR_LOG2_MASK, (const void *)Log2Mask, "Log2Mask", 1);
    return rc;
}
