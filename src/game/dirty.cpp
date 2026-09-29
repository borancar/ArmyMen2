/* dirty.cpp -- see dirty.h. */
#include <stdint.h>

#include "dirty.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* AM2_IMAGE, not a bare cast: this module is in the offline test, where the
 * image is mapped wherever VirtualAlloc put it. Every other reference here is
 * to the same array. */

/* 0x0041DCE0. Empty the dirty list: record 0's two links and the tail index.
 *
 * It was ResetDrawCounts, and every name in it was wrong in the same way --
 * three globals cleared together look like three counters. Two are record
 * ZERO's own prev and next, and the third is the tail. See ADDR_DIRTY_TAIL. */
void __cdecl ResetDirtyList(void)
{
    *(uint16_t *)((void *)(uintptr_t)(ADDR_DIRTY_HEAD))  = 0;
    *(uint16_t *)((void *)(uintptr_t)(ADDR_DIRTY_PREV_HEAD)) = 0;
    *(uint16_t *)((void *)(uintptr_t)(ADDR_DIRTY_TAIL)) = 0;
}



/* 0x0041DD00, seven callers. Append one rectangle to the dirty list.
 *
 * The list is doubly linked through indices, not pointers, and record 0 is
 * the sentinel at both ends -- so linking the new record on is three stores:
 * its own next to 0, its prev to the old tail, and the old tail's next to it.
 *
 * THE TAIL IS ALSO THE ALLOCATOR. The new index is tail + 1, which is why
 * ADDR_DIRTY_TAIL reads like a count and why a record unlinked from the end
 * is handed straight back out on the next call. Nothing tracks free records
 * anywhere else.
 *
 * Overflow does not grow anything and does not drop the rectangle either: it
 * sets ADDR_FULL_REDRAW and returns, so the frame repaints whole. The bound
 * is a 16-BIT unsigned compare against AM2_DEPTH_MAX, done on tail + 1 before
 * anything is written.
 *
 * The original reads the 16-bit tail with a 32-bit load and then works in
 * `cx`, so the neighbouring global's bytes ride along in the top half of the
 * register and are never looked at. Written as the uint16 it is.
 *
 * NOTHING IN THE SUITE DISCRIMINATES IT, and it takes three measurements to
 * say that rather than one. It runs 618 times in live play and ZERO times at
 * the briefing, so `bootcamp` cannot see it at all -- which is why dropping
 * the prev link and then never linking the record at all both left it at its
 * usual 22 pixels. Run against `mission`, where it does execute, the
 * never-link build comes out at 293, inside the 281..308 band clean runs
 * give: the final shot is of a settled scene and every stale region has been
 * painted over by the time it is taken.
 *
 * That is a gap in the harness rather than a fact about this function, and
 * closing it is what moved these two out of win32/mapdraw.cpp: neither names
 * a Win32 type, so on the flat side they can be tested with no game at all.
 * tools/dirtycheck.py runs the ORIGINAL over eight sequences and records the
 * whole array; all three mutations the A/B could not see fail it, the
 * never-link one on seven sequences of eight. */
void __cdecl AddDirtyRect(int32_t left, int32_t top, int32_t right,
                          int32_t bottom)
{
    /* The record array indexed afresh for every store, as the image does,
     * and through one view with the tail in front of it: a store into a
     * record may land on the tail as far as the compiler knows, which is why
     * the image reads the tail again after them. */
    struct Rec { int32_t l, t, r, b; uint16_t prev, next; };
    struct List { uint16_t tail; uint16_t pad; struct Rec recs[AM2_DEPTH_MAX]; };
#define DL (*(struct List *)((void *)(uintptr_t)(ADDR_DIRTY_TAIL)))
    uint16_t next = (uint16_t)(DL.tail + 1);

    if (next >= AM2_DEPTH_MAX) {
        *(int32_t *)((void *)(uintptr_t)(ADDR_FULL_REDRAW)) = 1;
        return;
    }

    DL.recs[next].l    = left;
    DL.recs[next].t    = top;
    DL.recs[next].r    = right;
    DL.recs[next].b    = bottom;
    DL.recs[next].next = 0;
    DL.recs[next].prev = DL.tail;
    DL.recs[DL.tail].next = next;
    DL.tail = next;
#undef DL
}

int dirty_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_RESET_DIRTY_LIST, (const void *)ResetDirtyList,
                        "ResetDirtyList", 0);
    rc |= patch_replace(ADDR_ADD_DIRTY_RECT, (const void *)AddDirtyRect,
                        "AddDirtyRect", 7);
    return rc;
}
