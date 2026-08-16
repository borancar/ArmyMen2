#ifndef AM2_RECT_H
#define AM2_RECT_H

#include <stdint.h>
#include "../inject/orig.h"

/* Original: 0x0042E1C0. Writes a, b, c, d into dst[0..3]. */
void __cdecl RectSet(int32_t *dst, int32_t a, int32_t b, int32_t c, int32_t d);

int rect_install(void);

#endif /* AM2_RECT_H */
