/* mousepress.h -- the record behind g_mousePress (ADDR_MOUSE_PRESS[3]).
 *
 * Its own header rather than globaltypes.h: only device.cpp and region.cpp
 * read it, and one more struct type in every translation unit is enough to
 * turn a VC6 register tie elsewhere (CheatLine's view-origin adds swap
 * edx/eax). */
#ifndef AM2_MOUSEPRESS_H
#define AM2_MOUSEPRESS_H

#include <stdint.h>

/* One per button, in the order the button arrays use. */
typedef struct {
    int32_t  point;     /* the packed cursor at the moment it went down */
    uint32_t tick;      /* GetTickCount then */
} AM2_MousePress;

#endif
