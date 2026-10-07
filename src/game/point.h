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

/* 0x0045F460's argument 5 and 6, which the original passes as ONE six-byte
 * structure -- three words, pushed as eight: every caller copies it with
 * `sub esp, 8; mov ecx, esp; mov [ecx], dword; mov [ecx+4], word`, so the top
 * two bytes of the pushed copy are whatever was on the stack. Both fields
 * have a fallback inside the callee: a zero point is filled from the target
 * object's position and a zero ground height from the shooter's own. */
typedef struct {
    AM2_Point at;
    int16_t   ground;
} AM2_FireSpot;

#endif
