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

int rect_install(void);

#endif /* AM2_RECT_H */
