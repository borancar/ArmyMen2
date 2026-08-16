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

AM2_Rect *__cdecl RectSet(AM2_Rect *r, int32_t left, int32_t top,
                          int32_t right, int32_t bottom)
{
    r->left   = left;
    r->top    = top;
    r->right  = right;
    r->bottom = bottom;
    return r;                   /* callers rely on this -- see rect.h */
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

/* ClipRect -- reconstructed from 0x0042E220.
 *
 * The 2D blit clipper: 18 call sites, all inside the large per-layer drawing
 * routines. Each edge is handled the same way -- if the clip edge cuts into the
 * placed rectangle, move the destination corner out to the clip edge and record
 * how far into the source that landed; otherwise take the source edge whole.
 * Any edge that puts the rectangle entirely outside the clip rejects the whole
 * blit.
 *
 * Note that `out` is in *source* space, not destination space: out->left and
 * out->top are offsets into the bitmap, which is what a blit needs in order to
 * skip the clipped-away columns and rows.
 *
 * The early rejects leave `out` partly written, which is why the return value
 * has to be checked rather than relying on an empty rectangle.
 */
int32_t __cdecl ClipRect(const AM2_Rect *src, const AM2_Rect *clip,
                         int32_t *x, int32_t *y, AM2_Rect *out)
{
    int32_t xoff = *x, yoff = *y;
    int32_t left   = src->left   + xoff;
    int32_t right  = src->right  + xoff;
    int32_t top    = src->top    + yoff;
    int32_t bottom = src->bottom + yoff;

    if (clip->left > left) {
        if (right <= clip->left)
            return 0;                       /* entirely left of the clip */
        *x = clip->left;
        out->left = clip->left - left;
    } else {
        out->left = 0;
    }

    if (right > clip->right) {
        if (left >= clip->right)
            return 0;                       /* entirely right of the clip */
        out->right = src->right - right + clip->right;
    } else {
        out->right = src->right;
    }

    if (clip->top > top) {
        if (bottom <= clip->top)
            return 0;                       /* entirely above the clip */
        *y = clip->top;
        out->top = clip->top - top;
    } else {
        out->top = 0;
    }

    if (bottom > clip->bottom) {
        if (top >= clip->bottom)
            return 0;                       /* entirely below the clip */
        out->bottom = src->bottom - bottom + clip->bottom;
    } else {
        out->bottom = src->bottom;
    }

    return 1;
}

int rect_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_RECT_SET, (const void *)RectSet, "RectSet", 5);
    rc |= patch_replace(ADDR_CLAMP, (const void *)Clamp, "Clamp", 3);
    rc |= patch_replace(ADDR_POINT_IN_RECT, (const void *)PointInRect, "PointInRect", 2);
    rc |= patch_replace(ADDR_CLIP_RECT, (const void *)ClipRect, "ClipRect", 5);
    return rc;
}
