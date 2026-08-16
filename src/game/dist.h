#ifndef AM2_DIST_H
#define AM2_DIST_H

#include <stdint.h>
#include "../inject/orig.h"

/* A map point: two signed 16-bit coordinates. Inferred from the movsx reads at
 * offsets 0 and 2 in ApproxDist. */
typedef struct {
    int16_t x;
    int16_t y;
} AM2_Point;

/* Original: 0x0042DDE0. Octagonal approximation of the distance between two
 * points -- max(|dx|,|dy|) + min(|dx|,|dy|)/2. */
int32_t __cdecl ApproxDist(const AM2_Point *a, const AM2_Point *b);

int dist_install(void);

#endif /* AM2_DIST_H */
