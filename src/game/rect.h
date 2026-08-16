#ifndef AM2_RECT_H
#define AM2_RECT_H

#include <stdint.h>
#include "../inject/orig.h"
#include "dist.h"

/* An axis-aligned rectangle: four signed 32-bit edges.
 *
 * The layout is not a guess. RectSet writes four dwords, and PointInRect reads
 * them back at +0, +4, +8 and +0x0C comparing x against the first and third and
 * y against the second and fourth -- which fixes both the field order and the
 * fact that the right and bottom edges are exclusive.
 */
typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} AM2_Rect;

/* Original: 0x0042E1C0. Writes the four edges. */
void __cdecl RectSet(AM2_Rect *r, int32_t left, int32_t top,
                     int32_t right, int32_t bottom);

/* Original: 0x0042E180. Signed clamp of `v` into [lo, hi]. */
int32_t __cdecl Clamp(int32_t v, int32_t lo, int32_t hi);

/* Original: 0x0042E1F0. 1 when `p` lies inside `r`, 0 otherwise. Left and top
 * edges are inclusive, right and bottom exclusive. */
int32_t __cdecl PointInRect(const AM2_Rect *r, const AM2_Point *p);

/* Original: 0x0042E220. The blit clipper.
 *
 * `src` is the source rectangle in bitmap space and (*x, *y) where it is to be
 * drawn. Clips that against `clip`, updating *x and *y to the visible
 * destination corner and writing the visible sub-rectangle of the source into
 * `out`. Returns 0 when nothing is visible, in which case the outputs are only
 * partially written and must not be used.
 */
int32_t __cdecl ClipRect(const AM2_Rect *src, const AM2_Rect *clip,
                         int32_t *x, int32_t *y, AM2_Rect *out);

int rect_install(void);

#endif /* AM2_RECT_H */
