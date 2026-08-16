/* Rectangle and clamping primitives, reconstructed from ArmyMen2.exe.
 *
 *   RectSet      0x0042E1C0   write the four edges
 *   Clamp        0x0042E180   signed clamp into a range
 *   PointInRect  0x0042E1F0   containment test
 *
 * These three sit directly under the drawing code and are among the busiest
 * functions in the engine -- RectSet alone reached 469,197 calls in a single
 * Boot Camp session, across 186 direct call sites.
 *
 * Original names are not recovered; these are ours. RectSet matches
 * SetRect(RECT*, left, top, right, bottom) in shape and use, and the game
 * imports USER32's SetRect separately, so it is a private equivalent rather
 * than a wrapper.
 *
 * Between them they pin down AM2_Rect exactly: RectSet writes four dwords, and
 * PointInRect reads them back at +0/+4/+8/+0x0C, comparing the point's x
 * against the first and third and its y against the second and fourth. The
 * right and bottom comparisons use jge, so those edges are exclusive.
 */

#include "rect.h"
#include "dist.h"
#include "../inject/patch.h"

#include <stdint.h>

void __cdecl RectSet(AM2_Rect *r, int32_t left, int32_t top,
                     int32_t right, int32_t bottom)
{
    r->left   = left;
    r->top    = top;
    r->right  = right;
    r->bottom = bottom;
}

int32_t __cdecl Clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int32_t __cdecl PointInRect(const AM2_Rect *r, const AM2_Point *p)
{
    int32_t x = p->x;                    /* read with movsx: signed 16-bit */
    int32_t y;

    if (x < r->left || x >= r->right)
        return 0;

    y = p->y;
    if (y < r->top || y >= r->bottom)
        return 0;

    return 1;
}

int rect_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_RECT_SET, RectSet, "RectSet", 5);
    rc |= patch_replace(ADDR_CLAMP, Clamp, "Clamp", 3);
    rc |= patch_replace(ADDR_POINT_IN_RECT, PointInRect, "PointInRect", 2);
    return rc;
}
