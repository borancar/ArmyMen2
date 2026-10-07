/* point.h -- AM2_Point alone, for headers below dist.h in the include graph
 * (globaltypes.h is reached from orig.h, which dist.h itself includes). */
#ifndef AM2_POINT_H
#define AM2_POINT_H

#include <stdint.h>

/* A map point: two signed 16-bit coordinates. Inferred from the movsx reads at
 * offsets 0 and 2 in ApproxDist. */
typedef struct {
    int16_t x;
    int16_t y;
} AM2_Point;

#endif
