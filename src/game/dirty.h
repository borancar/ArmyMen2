/* dirty.cpp -- the frame's dirty-rectangle list.
 *
 * Five hundred 20-byte records, doubly linked through INDICES rather than
 * pointers, with record ZERO as the sentinel at both ends. That is why two of
 * the three globals beside the array are not globals at all -- see
 * ADDR_DIRTY_TAIL in orig.h, which has the whole story.
 *
 * FLAT, AND MOVED HERE TO MAKE IT SO. Both of these lived in
 * win32/mapdraw.cpp with the rest of the map drawing, and neither names a
 * Win32 type: they are index arithmetic over an array in the image. What that
 * buys is the offline test -- tools/dirtycheck.py runs the ORIGINAL over a
 * sequence of appends under Unicorn and tests/selftest.cpp replays it, which
 * is the only check either function has. Their pixels are invisible: the
 * append runs 618 times in live play and ZERO at the briefing, so `bootcamp`
 * cannot see it, and `mission` compares a settled final frame where every
 * stale region has already been painted over.
 *
 * RepaintDirtyList stays in mapdraw.cpp. It walks this list and then reaches
 * IntersectRect and the surface, so it belongs on the other side of the split.
 */
#ifndef AM2_DIRTY_H
#define AM2_DIRTY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x0041DCE0. Empty the list. */
void __cdecl ResetDirtyList(void);

/* 0x0041DD00. Append one rectangle. */
void __cdecl AddDirtyRect(int32_t left, int32_t top, int32_t right,
                          int32_t bottom);

int dirty_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_DIRTY_H */
