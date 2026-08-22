/* trig.cpp -- the lookup tables the isometric projection runs on.
 *
 * 0x0042DC30, called once from WinMain. Four tables:
 *
 *   cos[256]   cos(2*pi*i/256)
 *   sin[256]   sin(2*pi*i/256) * -0.85, the isometric squash baked in
 *   atanC[1025], atanS[1025]   the reverse direction, byte per entry
 *
 * The two reverse tables are built by search, not by an inverse function: for
 * each ratio in -512..512 they walk all 129 angles and keep the one whose
 * ratio is closest. That is what the original does and it is why they cost
 * more than the forward tables put together.
 *
 * Everything here is x87 arithmetic and the results are stored as 32-bit
 * floats, so the tables are bit-comparable against the original's. They are,
 * and tools/trigdump.py is what says so.
 */
#ifndef AM2_TRIG_H
#define AM2_TRIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void __cdecl BuildTrigTables(void);

/* 0x0042DD70 and 0x0042DDC0, 17 callers each and four instructions apiece: an
 * 8-bit heading masked to 0..255, one `fld` from the table this file builds,
 * and a `ret`. They return a FLOAT, in st(0) -- which is why neither can go in
 * the vector harness or AM2_SELFCHECK, both of which compare eax.
 *
 * What DOES check them is tools/trigdump.py, one layer down: these two are the
 * only readers of the two forward tables, and the tables are compared byte for
 * byte against the original. A wrong index here would still be a wrong index
 * into a table known to be right. */
float __cdecl Cos8(int32_t heading);
float __cdecl Sin8(int32_t heading);

int trig_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_TRIG_H */
