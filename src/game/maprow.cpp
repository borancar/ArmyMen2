/* maprow.cpp -- the map's cell grid and its depth-sorted draw list.
 *
 * Flat, and it took a failing check to put it here. Not one of these four
 * functions names a platform type, so by this project's own rule they always
 * belonged on this side; they lived in win32/mapdraw.cpp only because
 * DepthCompare happened to be written there first, with ComposeFrame,
 * ScrollView and the line drawers between them.
 *
 * What forced the move was not tidiness. air.cpp and item.cpp call RowUpdate,
 * and a flat module may not reach a win32/ header even transitively -- so a
 * declaration-only header looked like enough. It is not: `make check` builds
 * tests/selftest.exe from the flat sources alone, and that link cannot see a
 * symbol defined in win32/. The boundary is a LINK-TIME fact, not only a
 * naming convention, and selftest-link is what says so.
 */
#include <stdint.h>
#include <string.h>

#include "maprow.h"
#include "objtable.h"
#include "objflag.h"   /* ObjFlagBit0 -- reconstructed */
#include "rect.h"      /* MakePoint */
#include "dist.h"      /* Log2Mask */
#include "crt.h"       /* the game's allocator owns the grid */
#include "item.h"      /* RowUnregisterAll -- reconstructed */
#include "misc.h"      /* ListUnlink -- reconstructed */
#include "anim.h"      /* AM2_Anim and the table it lives in */
#include "image.h"
#include "crt.h"       /* am2_malloc -- the game's own */
#include "../inject/orig.h"
#include "../inject/patch.h"

/* DirtyCollect is reconstructed, in dirty.cpp, and has no header of its own
 * -- item.cpp forward-declares it locally and this does the same. It used to
 * be reached here through a macro that SHADOWED its name, which pointed at
 * ADDR_DIRTY_COLLECT and so called our own code through the detour under a
 * name that said otherwise. checkseams could not see it: the macro is not
 * spelled `orig_`, and every non-orig_ #define was skipped. */
void __cdecl DirtyCollect(const AM2_Rect *r);

#define DEPTH_OBJ(n)  (*(void **)((uint8_t *)(n) + DEPTH_OFF_OBJ))
#define DEPTH_PREV(n) (*(uint8_t **)((uint8_t *)(n) + DEPTH_OFF_PREV))
#define DEPTH_NEXT(n) (*(uint8_t **)((uint8_t *)(n) + DEPTH_OFF_NEXT))

#define ROW_FLD(r, off, ty) (*(ty *)((uint8_t *)(r) + (off)))
#define MAPDESC_CELL(d, i) \
    ((void **)(*(uint8_t *const *)((const uint8_t *)(d) + MAPDESC_OFF_CELLS) \
               + (uint32_t)(i) * 4))

/* The projection the comparator turns a horizontal distance into: `dx` units
 * across, times the object's own slope, TRUNCATED TO 16 BITS.
 *
 * The 16 bits are not incidental -- the original takes `_ftol`'s answer in AX
 * and sign-extends that, so a projection past 32767 wraps rather than
 * saturating. Written out here because it is the one place this function can
 * surprise. */
static int32_t DepthProject(int32_t dx, float slope, int32_t y)
{
    return (int16_t)(int32_t)((float)dx * slope) + y;
}

/* 0x0041D740, nine callers -- which of two objects is drawn first.
 *
 * Four tests in order, each falling through to the next when it cannot
 * decide.
 *
 * A LAYER at +0x26, but only when BOTH objects have a positive one: a layer
 * of zero or less means "no layer", and two such objects are compared by
 * position instead of both being treated as layer 0.
 *
 * Then the SLOPE at +0x28, which is what makes this more than a y sort. Each
 * object projects the horizontal distance to the other onto the vertical
 * axis and the two answers are compared with the other's y. When both
 * projections agree the answer is theirs; when they disagree -- which is what
 * happens where two sprites overlap ambiguously -- the plain y comparison
 * decides. An object with a slope of exactly zero projects nothing, and the
 * three combinations of zero and non-zero are three separate arms.
 *
 * Then plain y. And finally the two POINTERS, compared as addresses, so the
 * order is total: two objects at the same place still have a fixed order and
 * the sort cannot loop. Answering 0 would have been the obvious thing and it
 * is not what this does -- 0 is reserved for a null argument. */
int32_t __cdecl DepthCompare(void *a, void *b)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    int16_t        la, lb;
    float          sa, sb;
    int32_t        ax, ay, bx, by;

    if (!pa || !pb)
        return 0;

    la = *(const int16_t *)(pa + OBJ_OFF_DEPTH_LAYER);
    lb = *(const int16_t *)(pb + OBJ_OFF_DEPTH_LAYER);
    if (la > 0 && lb > 0) {
        if (la > lb)
            return 1;
        if (la < lb)
            return -1;
    }

    sa = *(const float *)(pa + OBJ_OFF_DEPTH_SLOPE);
    sb = *(const float *)(pb + OBJ_OFF_DEPTH_SLOPE);
    ax = *(const int16_t *)(pa + ROW_OFF_X);
    ay = *(const int16_t *)(pa + ROW_OFF_Y);
    bx = *(const int16_t *)(pb + ROW_OFF_X);
    by = *(const int16_t *)(pb + ROW_OFF_Y);

    if (sa != 0.0f && sb != 0.0f) {
        int32_t fromA = DepthProject(bx - ax, sa, ay);
        int32_t fromB = DepthProject(ax - bx, sb, by);
        int32_t aInFront = (fromA > by) ? 1 : 0;

        if (ay > fromB && aInFront)
            return 1;
        if (ay < fromB && !aInFront)
            return -1;
        /* They disagree: plain y decides. */
    } else if (sa != 0.0f) {
        /* Only A has a slope, so only A's projection is available. */
        if (DepthProject(bx - ax, sa, ay) > by)
            return 1;
        if (DepthProject(bx - ax, sa, ay) == by)
            return (pb < pa) ? 1 : -1;
        return -1;
    } else if (sb != 0.0f) {
        int32_t fromB = DepthProject(ax - bx, sb, by);

        if (ay > fromB)
            return 1;
        if (ay == fromB)
            return (pb < pa) ? 1 : -1;
        return -1;
    }

    if ((int16_t)ay > (int16_t)by)
        return 1;
    if ((int16_t)ay < (int16_t)by)
        return -1;

    return (pb < pa) ? 1 : -1;
}

/* 0x0041D2B0, six callers. Give a row its entry buffer and put it on the map.
 *
 * The size is the point. A width and a height in world units become a cell
 * count by taking 2 off each first -- so a span that exactly fills a cell
 * boundary does not claim the next one -- shifting down by 8, and adding 2 back
 * for the partial cells at either end. The multiply is an 8-BIT `imul`, and
 * only AL is kept, so a row wide enough to need more than 255 cells wraps. That
 * is the original's and is reproduced; nothing this project drives comes near
 * it, and a wider type here would be a silent behaviour change.
 *
 * Every entry is initialised to point back at the row with both list links
 * null and no cell, which is exactly the state RowRegisterAll assumes and
 * RowUnregisterAll leaves behind.
 *
 * The rectangle is the sprite's box at the row's position less the hot spot --
 * and NOT less ROW_OFF_Y_ADJUST, which RowUpdate does subtract. The two
 * disagree, and this is the one that runs first; whatever adjustment the row
 * carries is applied only once RowUpdate has seen it. Reproduced rather than
 * reconciled, since making them agree would change one of them.
 *
 * A row with no sprite gets a zero rectangle and is not registered at all, and
 * the return value is the byte count either way.
 *
 * Measured: 2,512 calls in a Boot Camp mission -- and RowRegisterAll's counter
 * went 1,587 to 0 with it, this having been its only caller. That is the fifth
 * member of this family to fall silent for the same reason, and the file now
 * has exactly two counters that can move: this one and RowUpdate. */
int32_t __cdecl RowAlloc(int32_t w, int32_t h, void *row, void *desc)
{
    uint8_t *r = (uint8_t *)row;
    uint8_t *spr;
    int32_t  bytes;
    int32_t  i;

    if (w > 2)
        w -= 2;
    if (h > 2)
        h -= 2;

    r[ROW_OFF_OWNS] = (uint8_t)(int8_t)((int8_t)((w >> 8) + 2)
                                        * (int8_t)((h >> 8) + 2));

    bytes = (int32_t)r[ROW_OFF_OWNS] * (int32_t)ROW_ENTRY_BYTES;
    ROW_FLD(r, ROW_OFF_BUFFER, void *) = am2_malloc((uint32_t)bytes);

    for (i = 0; i < (int32_t)r[ROW_OFF_OWNS]; i++) {
        uint8_t *entry = ROW_FLD(r, ROW_OFF_BUFFER, uint8_t *)
                         + (uint32_t)i * ROW_ENTRY_BYTES;

        ROW_FLD(entry, DEPTH_OFF_OBJ, void *)       = r;
        ROW_FLD(entry, DEPTH_OFF_NEXT, uint32_t)    = 0;
        ROW_FLD(entry, DEPTH_OFF_PREV, uint32_t)    = 0;
        ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t) = -1;
    }

    spr = ROW_FLD(r, ROW_OFF_SPRITE, uint8_t *);
    if (!spr) {
        ROW_FLD(r, ROW_OFF_RECT + 0,  int32_t) = 0;
        ROW_FLD(r, ROW_OFF_RECT + 4,  int32_t) = 0;
        ROW_FLD(r, ROW_OFF_RECT + 8,  int32_t) = 0;
        ROW_FLD(r, ROW_OFF_RECT + 12, int32_t) = 0;
        return bytes;
    }

    ROW_FLD(r, ROW_OFF_RECT + 0, int32_t) =
        ROW_FLD(r, ROW_OFF_X, int16_t) - ROW_FLD(spr, SPR_OFF_HOTX, int16_t);
    ROW_FLD(r, ROW_OFF_RECT + 4, int32_t) =
        ROW_FLD(r, ROW_OFF_Y, int16_t) - ROW_FLD(spr, SPR_OFF_HOTY, int16_t);
    ROW_FLD(r, ROW_OFF_RECT + 8, int32_t) =
        ROW_FLD(spr, SPR_OFF_W, int32_t) + ROW_FLD(r, ROW_OFF_RECT + 0, int32_t);
    ROW_FLD(r, ROW_OFF_RECT + 12, int32_t) =
        ROW_FLD(spr, SPR_OFF_H, int32_t) + ROW_FLD(r, ROW_OFF_RECT + 4, int32_t);

    if (ObjFlagBit0(r))
        RowRegisterAll(r, desc);

    return bytes;
}

/* 0x0041D980, one caller. The counterpart of RowUnregisterAll: link every cell
 * the row's CURRENT rectangle covers, from entries that are assumed not to be
 * in any list yet.
 *
 * It shares RowUpdate's cell arithmetic exactly -- including the clamp of the
 * BOTTOM edge against COLS-1 rather than ROWS-1, which is the same quirk in the
 * same shape and is what makes it convincing as a deliberate copy in the
 * original rather than a slip in one of them.
 *
 * Two differences from RowUpdate and both follow from the assumption. There is
 * no unlink and no re-sort, because nothing here is already placed; and the
 * surplus entries at the end are cleared harder -- cell to -1 AND both list
 * links to zero -- where RowUpdate only marks the cell, because RowUpdate has
 * just unlinked them and this has not.
 *
 * Measured: 1,587 calls in a Boot Camp mission. Reconstructing it also closed
 * the subsystem: DepthLink's counter went from 2,758 to 0, because this was
 * its last caller that was still the original's. Every counter inside this
 * file now reads 0 except RowUpdate, which is the only member of the family
 * with callers outside it. That is what a finished subsystem looks like from
 * the counters, and it is indistinguishable from a broken one unless the
 * numbers before it are on record -- which is why they are, here and in the
 * commit. */
void __cdecl RowRegisterAll(void *row, void *desc)
{
    uint8_t *r = (uint8_t *)row;
    int32_t  cl, ct, cr, cb;
    int32_t  cols, rows, cell, stride, used;

    if (ROW_FLD(r, 0, uint32_t) & ROW_FLAG_REMOVED)
        return;

    cl = ROW_FLD(r, ROW_OFF_RECT + 0,  int32_t) >> 8;
    ct = ROW_FLD(r, ROW_OFF_RECT + 4,  int32_t) >> 8;
    cr = ROW_FLD(r, ROW_OFF_RECT + 8,  int32_t) >> 8;
    cb = ROW_FLD(r, ROW_OFF_RECT + 12, int32_t) >> 8;

    cols = ROW_FLD(desc, MAPDESC_OFF_COLS, int32_t);
    rows = ROW_FLD(desc, MAPDESC_OFF_ROWS, int32_t);

    if (cb < 0 || ct > rows - 1 || cr < 0 || cl > cols - 1)
        return;

    if (cl <= 0)
        cl = 0;
    if (ct <= 0)
        ct = 0;
    if (cr >= cols - 1)
        cr = cols - 1;
    if (cb >= cols - 1)          /* COLS, not ROWS -- as in RowUpdate */
        cb = cols - 1;

    used   = 0;
    cell   = (ct << ROW_FLD(desc, MAPDESC_OFF_SHIFT, int32_t)) + cl;
    stride = cols - cr + cl - 1;

    for (; ct <= cb; ct++, cell += stride) {
        int32_t x;

        for (x = cl; x <= cr; x++, cell++, used++) {
            uint8_t *entry = ROW_FLD(r, ROW_OFF_BUFFER, uint8_t *)
                             + (uint32_t)used * ROW_ENTRY_BYTES;

            ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t) = cell;
            DepthLink(entry, MAPDESC_CELL(desc, cell));
        }
    }

    for (; used < (int32_t)r[ROW_OFF_OWNS]; used++) {
        uint8_t *entry = ROW_FLD(r, ROW_OFF_BUFFER, uint8_t *)
                         + (uint32_t)used * ROW_ENTRY_BYTES;

        ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t) = -1;
        ROW_FLD(entry, DEPTH_OFF_NEXT, uint32_t)    = 0;
        ROW_FLD(entry, DEPTH_OFF_PREV, uint32_t)    = 0;
    }
}

/* 0x0041D480, THIRTY-SEVEN callers -- the centre of this subsystem. Bring one
 * row's membership of the map's cell grid up to date with where it now is.
 *
 * Four ways out before any work. ObjFlagBit0 clear means the row should not be
 * on the map at all, so it is unregistered; then a "nothing has changed" test;
 * then a null sprite; and then bit 1, which asks for removal and records
 * itself in bit 2 so the next call knows it is already out.
 *
 * The unchanged test is worth reading carefully. It wants the caller's flag
 * zero, the position equal to the remembered position, the sprite equal to the
 * remembered sprite, AND both bit 1 and bit 2 clear -- which the original
 * writes as `(flags & 2) == (flags & 4)` compared as BYTES, a thing that can
 * only hold when both are zero since 2 and 4 are never equal. Written here as
 * what it means rather than as what it says.
 *
 * The geometry is the sprite's rectangle placed at the row's position, less
 * the sprite's hot spot and, on the Y axis only, less ROW_OFF_Y_ADJUST. Cells
 * are that rectangle shifted right by 8, and the row is dropped entirely if it
 * lies wholly off the grid.
 *
 * One asymmetry in the clamping is the original's and is REPRODUCED rather
 * than corrected: left and right are clamped against COLS-1, and so is the
 * BOTTOM edge, where ROWS-1 is what the TOP edge was tested against. Every
 * shipped map seen so far is square, so the two are the same number and
 * nothing here can tell them apart -- the same situation CLAUDE.md records for
 * ADDR_MAP_TILES_W. Correcting it would be a behaviour change defended by a
 * guess.
 *
 * Then the double loop: for every cell the rectangle covers, take the next
 * entry of the row's buffer and put it in that cell. An entry already in the
 * right cell is only re-sorted, since its depth may have moved; an entry in
 * the wrong cell is unlinked first. Finally the entries the row no longer
 * needs are unlinked and marked -1, and the dirty rectangle is collected from
 * a COPY of the row's rect, because the collector intersects in place.
 *
 * Measured: 242,936 calls in a Boot Camp mission, which makes this the hottest
 * function reconstructed in this run of work by two orders of magnitude. The
 * frame it produces is correct by eye -- objects, signs, scenery and HUD all
 * present and in the right order -- and the A/B is clean, which for a function
 * that decides what is in which cell is a real result rather than a no-op.
 *
 * It also took three counters to zero in the same run, all of them expected:
 * RowUnregisterAll from 74, DepthResort from 3,914, and DepthCompare which was
 * already blind. Each is a callee this now reaches by name. DepthLink still
 * reads 2,758 because it has a second caller that is still the original's. */
void __cdecl RowUpdate(void *row, int32_t force, void *desc)
{
    uint8_t *r = (uint8_t *)row;
    uint8_t *spr;
    int32_t  left, top, right, bottom;
    int32_t  cl, ct, cr, cb;
    int32_t  cols, rows, cell, stride, used;

    if (!ObjFlagBit0(r)) {
        RowUnregisterAll(r, desc);
        return;
    }

    if (force == 0
        && ROW_FLD(r, ROW_OFF_X, int16_t) == ROW_FLD(r, ROW_OFF_PREV_X, int16_t)
        && ROW_FLD(r, ROW_OFF_Y, int16_t) == ROW_FLD(r, ROW_OFF_PREV_Y, int16_t)
        && !(ROW_FLD(r, 0, uint32_t)
             & (ROW_FLAG_REMOVED | ROW_FLAG_REMOVED_DONE))
        && ROW_FLD(r, ROW_OFF_SPRITE, void *)
           == ROW_FLD(r, ROW_OFF_PREV_SPRITE, void *))
        return;

    if (!ROW_FLD(r, ROW_OFF_SPRITE, void *))
        return;

    DirtyCollect((const AM2_Rect *)(r + ROW_OFF_RECT));

    if (ROW_FLD(r, 0, uint32_t) & ROW_FLAG_REMOVED) {
        RowUnregisterAll(r, desc);
        ROW_FLD(r, 0, uint32_t) |= ROW_FLAG_REMOVED_DONE;
        return;
    }
    ROW_FLD(r, 0, uint32_t) &= ~(uint32_t)ROW_FLAG_REMOVED_DONE;

    spr    = ROW_FLD(r, ROW_OFF_SPRITE, uint8_t *);
    left   = ROW_FLD(r, ROW_OFF_X, int16_t)
             - ROW_FLD(spr, SPR_OFF_HOTX, int16_t);
    top    = ROW_FLD(r, ROW_OFF_Y, int16_t)
             - ROW_FLD(spr, SPR_OFF_HOTY, int16_t)
             - ROW_FLD(r, ROW_OFF_Y_ADJUST, int16_t);
    right  = left + ROW_FLD(spr, SPR_OFF_W, int32_t);
    bottom = top + ROW_FLD(spr, SPR_OFF_H, int32_t);

    ROW_FLD(r, ROW_OFF_RECT + 0,  int32_t) = left;
    ROW_FLD(r, ROW_OFF_RECT + 4,  int32_t) = top;
    ROW_FLD(r, ROW_OFF_RECT + 8,  int32_t) = right;
    ROW_FLD(r, ROW_OFF_RECT + 12, int32_t) = bottom;

    cl = left >> 8;
    ct = top >> 8;
    cr = right >> 8;
    cb = bottom >> 8;

    cols = ROW_FLD(desc, MAPDESC_OFF_COLS, int32_t);
    rows = ROW_FLD(desc, MAPDESC_OFF_ROWS, int32_t);

    if (cb < 0 || ct > rows - 1 || cr < 0 || cl > cols - 1) {
        RowUnregisterAll(r, desc);
        return;
    }

    if (cl <= 0)
        cl = 0;
    if (ct <= 0)
        ct = 0;
    if (cr >= cols - 1)
        cr = cols - 1;
    if (cb >= cols - 1)          /* COLS, not ROWS -- see above */
        cb = cols - 1;

    used   = 0;
    cell   = (ct << ROW_FLD(desc, MAPDESC_OFF_SHIFT, int32_t)) + cl;
    stride = cols - cr + cl - 1;

    for (; ct <= cb; ct++, cell += stride) {
        int32_t x;

        for (x = cl; x <= cr; x++, cell++, used++) {
            uint8_t *entry = ROW_FLD(r, ROW_OFF_BUFFER, uint8_t *)
                             + (uint32_t)used * ROW_ENTRY_BYTES;
            int32_t  was   = ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t);

            if (was == cell) {
                DepthResort(entry, MAPDESC_CELL(desc, cell));
                continue;
            }
            if (was >= 0)
                ListUnlink(entry, MAPDESC_CELL(desc, was));

            ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t) = cell;
            DepthLink(entry, MAPDESC_CELL(desc, cell));
        }
    }

    while (used < (int32_t)r[ROW_OFF_OWNS]) {
        uint8_t *entry = ROW_FLD(r, ROW_OFF_BUFFER, uint8_t *)
                         + (uint32_t)used * ROW_ENTRY_BYTES;
        int32_t  was   = ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t);

        if (was < 0)
            break;

        ListUnlink(entry, MAPDESC_CELL(desc, was));
        ROW_FLD(entry, ROW_ENTRY_OFF_CELL, int32_t) = -1;
        used++;
    }

    {
        /* A copy, because the collector intersects its argument in place. */
        int32_t box[4];

        box[0] = ROW_FLD(r, ROW_OFF_RECT + 0,  int32_t);
        box[1] = ROW_FLD(r, ROW_OFF_RECT + 4,  int32_t);
        box[2] = ROW_FLD(r, ROW_OFF_RECT + 8,  int32_t);
        box[3] = ROW_FLD(r, ROW_OFF_RECT + 12, int32_t);
        DirtyCollect((const AM2_Rect *)box);
    }

    /* One dword, which is what pairs X with PREV_X and Y with PREV_Y. */
    ROW_FLD(r, ROW_OFF_PREV_X, uint32_t) = ROW_FLD(r, ROW_OFF_X, uint32_t);
    ROW_FLD(r, ROW_OFF_PREV_SPRITE, void *) =
        ROW_FLD(r, ROW_OFF_SPRITE, void *);
}

/* 0x0041D8F0, two callers. Link a node that is NOT yet in the list into its
 * sorted place. The primitive under DepthInsert, which is a different address
 * and takes an object and a world rectangle instead.
 *
 * Same four-exit shape as DepthResort below, minus the unlink: empty list,
 * before the head, before some later node, or appended at the tail. It only
 * ever walks FORWARD, because a node that is not linked has no position to
 * walk back from.
 *
 * The head cases are the ones to get right. An empty list writes both of the
 * node's links to zero and makes it the head; taking the head position writes
 * `prev` to zero and `*head`, and the other two exits touch neither.
 *
 * Measured: 2,888 calls in a Boot Camp mission. Which exit each takes is not.
 *
 * Reconstructing this took DepthCompare's counter from 12,661 to ZERO in the
 * same run, with no behaviour change at all: its only two live callers are
 * this and DepthResort, and both now call it by name rather than through the
 * patched entry. A textbook instance of the first kind of counter blindness,
 * recorded here because it happened between two commits and would otherwise
 * read as a function that stopped running. */
void __cdecl DepthLink(void *node, void **head)
{
    uint8_t *n = (uint8_t *)node;
    uint8_t *e = (uint8_t *)*head;

    if (!e) {
        *head          = n;
        DEPTH_NEXT(n)  = (uint8_t *)0;
        DEPTH_PREV(n)  = (uint8_t *)0;
        return;
    }

    if (DepthCompare(DEPTH_OBJ(n), DEPTH_OBJ(e)) <= 0) {
        DEPTH_PREV(n) = (uint8_t *)0;
        DEPTH_NEXT(n) = e;
        DEPTH_PREV(e) = n;
        *head         = n;
        return;
    }

    while (DEPTH_NEXT(e)) {
        e = DEPTH_NEXT(e);
        if (DepthCompare(DEPTH_OBJ(n), DEPTH_OBJ(e)) <= 0) {
            DEPTH_NEXT(n)              = e;
            DEPTH_PREV(n)              = DEPTH_PREV(e);
            DEPTH_PREV(e)              = n;
            DEPTH_NEXT(DEPTH_PREV(n))  = n;
            return;
        }
    }

    DEPTH_NEXT(e) = n;
    DEPTH_PREV(n) = e;
    DEPTH_NEXT(n) = (uint8_t *)0;
}

/* 0x0041DB90, one caller -- ADDR_ROW_UPDATE. Put one node back into depth
 * order after the thing it points at has moved.
 *
 * The list is doubly linked and kept sorted by ADDR_DEPTH_COMPARE over the
 * OBJECT each node holds, not over the node. Only one node is out of place, so
 * this walks outward from it in whichever direction the first comparison
 * indicates and re-links it once -- an insertion sort's inner loop, run alone.
 *
 * Four exits and they are not symmetric, which is the part worth stating.
 * Walking toward the tail ends either before some node or after the LAST one;
 * walking toward the head ends either after some node or at the head itself,
 * and only that last case writes `*head`. The two middle cases do not, because
 * a node inserted between two others cannot become the head.
 *
 * Note also that the unlink differs between the two directions. Going forward,
 * `n->next` is known non-null -- that is why we are here -- so its back
 * pointer is written unconditionally; going backward, `n->prev` is the known
 * one and `n->next` has to be tested. The original writes each accordingly and
 * this reproduces the asymmetry rather than guarding both.
 *
 * It lives here rather than in flat map.cpp only because DepthCompare does:
 * this function names no platform type, but a flat module may not reach a
 * win32/ header even transitively, and splitting a two-function list across
 * the boundary to satisfy a rule about API contact would be worse.
 *
 * Measured: 3,922 calls in a Boot Camp mission, with DepthCompare at 12,661 on
 * the same run -- about 3.2 comparisons per call, which is the walk loops
 * doing real work rather than every node landing on the first test. This is
 * the hottest function reconstructed in this run of work, and it is pointer
 * surgery, so a wrong re-link would corrupt the draw order and show. Which of
 * the four exits each call takes is NOT measured. */
void __cdecl DepthResort(void *node, void **head)
{
    uint8_t *n = (uint8_t *)node;
    uint8_t *e;

    if (DEPTH_NEXT(n)
        && DepthCompare(DEPTH_OBJ(n), DEPTH_OBJ(DEPTH_NEXT(n))) > 0) {
        for (e = DEPTH_NEXT(n); DEPTH_NEXT(e); ) {
            e = DEPTH_NEXT(e);
            if (DepthCompare(DEPTH_OBJ(n), DEPTH_OBJ(e)) <= 0) {
                /* Unlink, then insert before `e`. */
                if (DEPTH_PREV(n))
                    DEPTH_NEXT(DEPTH_PREV(n)) = DEPTH_NEXT(n);
                else
                    *head = DEPTH_NEXT(n);
                DEPTH_PREV(DEPTH_NEXT(n)) = DEPTH_PREV(n);

                DEPTH_PREV(n) = DEPTH_PREV(e);
                DEPTH_NEXT(n) = e;
                DEPTH_NEXT(DEPTH_PREV(e)) = n;
                DEPTH_PREV(e) = n;
                return;
            }
        }

        /* Ran off the end: unlink and append after `e`, the last node. */
        if (DEPTH_PREV(n))
            DEPTH_NEXT(DEPTH_PREV(n)) = DEPTH_NEXT(n);
        else
            *head = DEPTH_NEXT(n);
        DEPTH_PREV(DEPTH_NEXT(n)) = DEPTH_PREV(n);

        DEPTH_PREV(n) = e;
        DEPTH_NEXT(n) = (uint8_t *)0;
        DEPTH_NEXT(e) = n;
        return;
    }

    if (!DEPTH_PREV(n)
        || DepthCompare(DEPTH_OBJ(n), DEPTH_OBJ(DEPTH_PREV(n))) >= 0)
        return;

    for (e = DEPTH_PREV(n); DEPTH_PREV(e); ) {
        e = DEPTH_PREV(e);
        if (DepthCompare(DEPTH_OBJ(n), DEPTH_OBJ(e)) >= 0) {
            /* Unlink, then insert after `e`. */
            DEPTH_NEXT(DEPTH_PREV(n)) = DEPTH_NEXT(n);
            if (DEPTH_NEXT(n))
                DEPTH_PREV(DEPTH_NEXT(n)) = DEPTH_PREV(n);

            DEPTH_PREV(n) = e;
            DEPTH_NEXT(n) = DEPTH_NEXT(e);
            DEPTH_PREV(DEPTH_NEXT(e)) = n;
            DEPTH_NEXT(e) = n;
            return;
        }
    }

    /* Ran off the front: unlink and make `n` the head, before `e`. */
    DEPTH_NEXT(DEPTH_PREV(n)) = DEPTH_NEXT(n);
    if (DEPTH_NEXT(n))
        DEPTH_PREV(DEPTH_NEXT(n)) = DEPTH_PREV(n);

    DEPTH_PREV(n) = (uint8_t *)0;
    DEPTH_NEXT(n) = e;
    DEPTH_PREV(e) = n;
    *head = n;
}

/* 0x0041D270, three callers. Drop the descriptor's grid and zero its shape.
 *
 * The three shape fields are cleared AFTER the free and the pointer is
 * cleared between them, which is the original's order and observable by
 * nothing. */
void __cdecl MapDescFree(void *desc)
{
    uint8_t *d     = (uint8_t *)desc;
    void    *cells = *(void **)(d + MAPDESC_OFF_CELLS);

    if (cells) {
        am2_free(cells);
        *(void **)(d + MAPDESC_OFF_CELLS) = (void *)0;
    }

    *(int32_t *)(d + MAPDESC_OFF_COLS)  = 0;
    *(int32_t *)(d + MAPDESC_OFF_ROWS)  = 0;
    *(int32_t *)(d + MAPDESC_OFF_SHIFT) = 0;
}

/* 0x0041D210, two callers -- 0x0042C8C0 builds both descriptors here, from
 * the map's world extent, one after the other.
 *
 * THE GRID IS SQUARE IN COLS. The allocation is `cols << shift` entries with
 * `shift` = Log2Mask(cols), so it is cols * cols when cols is a power of two;
 * MAPDESC_OFF_ROWS never enters the sizing at all. That settles something
 * RowRegisterAll already had a comment about: clamping its bottom edge to
 * cols - 1 rather than rows - 1 is not a slip, it is the bound the grid
 * actually has. The largest cell that clamp can produce is
 * ((cols-1) << shift) + cols - 1, exactly one short of the allocation.
 *
 * Log2Mask answers 0 for anything that is not a power of two, which would
 * make the grid one row of `cols` and the indexing fold into it. No map ships
 * that way; the extents are read from the map file and the game divides them
 * by 256 to get here.
 *
 * SHARPLY CHECKED, unlike most of what has landed lately. One added to the
 * shift puts `bootcamp` at 10,097 differing pixels against a budget of 500 --
 * every object registers in the wrong cell and the frame says so. Measured,
 * because a clean A/B over code that turns out to be undiscriminated is worth
 * nothing and this file has just had two of those. */
void __cdecl MapDescInit(void *desc, int32_t w, int32_t h)
{
    uint8_t *d = (uint8_t *)desc;
    int32_t  cols, shift, bytes;
    void    *cells;

    MapDescFree(d);

    cols = w >> AM2_CELL_SHIFT;
    *(int32_t *)(d + MAPDESC_OFF_COLS) = cols;
    *(int32_t *)(d + MAPDESC_OFF_ROWS) = h >> AM2_CELL_SHIFT;

    shift = Log2Mask(cols);
    *(int32_t *)(d + MAPDESC_OFF_SHIFT) = shift;

    bytes = (cols << shift) << 2;
    cells = am2_malloc((size_t)bytes);
    *(void **)(d + MAPDESC_OFF_CELLS) = cells;
    memset(cells, 0, (size_t)bytes);
}

/* 0x0040A050, two callers. Clear a map object and give it a sprite, a
 * position and the default palette.
 *
 * FILED HERE RATHER THAN BY BAND. It sits in air.cpp..audio.cpp, which is a
 * different translation unit from this file's 0x0041Dxxx -- but its 0x60
 * bytes are AM2_OBJ_ROW_STRIDE and the three fields it writes are the row's
 * sprite, packed position and palette, so it is the row family's constructor
 * and nothing else. That TU is already split three ways in this tree
 * (objflag.cpp, anim.cpp, win32/mapdraw.cpp), so one more split costs
 * nothing and filing it away from its family would.
 *
 * The clear is the whole 0x60 and the writes follow it, so every other field
 * really is zero on the way out -- including the flags, which means bit 0 is
 * CLEAR and the object is not drawn until somebody sets it.
 *
 * It runs 1,612 times in one Boot Camp mission and the palette line is still
 * NOT checked: nulling it leaves `bootcamp` at 76 pixels, inside the band.
 * Every object that reaches the screen has had its own palette written in by
 * then -- which is what MAPOBJ_OFF_PALETTE's note in orig.h already says
 * happens immediately before a draw. Warm code, undiscriminated line. */
void __cdecl RowInit(void *row, void *sprite, int32_t x, int32_t y)
{
    uint8_t *r = (uint8_t *)row;

    memset(r, 0, AM2_OBJ_ROW_STRIDE);
    *(void **)(r + ROW_OFF_SPRITE) = sprite;
    *(uint32_t *)(r + ROW_OFF_X)   = MakePoint((uint32_t)x, (uint32_t)y);
    *(void **)(r + MAPOBJ_OFF_PALETTE) =
        *(void *const *)(uintptr_t)ADDR_DEFAULT_PALETTE;
}

/* 0x0041D3D0, three callers. Put a new sprite on a row.
 *
 * IT REBUILDS ONLY WHEN THE NEW SPRITE NEEDS MORE CELLS. The count is the
 * arithmetic RowAlloc uses on the sprite's bounds -- and NOT the same types.
 * RowAlloc multiplies two int8 and stores a byte in ROW_OFF_OWNS; this
 * multiplies two int32 and compares against that byte. A sprite big enough to
 * overflow the byte looks larger here than the row can ever record, so it
 * takes the rebuild arm every time. Both halves are the original's, and
 * reconciling them would change one.
 *
 * THE REBUILD ARM SWAPS THE SPRITE IN THE MIDDLE. It clears bit 0, updates,
 * releases the row, THEN stores the new sprite, sets bit 0 again, and calls
 * RowAlloc with the bounds read back out of the row rather than out of the
 * argument -- so the order matters and a reconstruction that stored the
 * sprite first would size the buffer from the same sprite by accident and
 * agree until something else changed the field between.
 *
 * Both arms keep the OLD sprite in ROW_OFF_PREV_SPRITE, which is what
 * RowUpdate compares against, and both end in a RowUpdate. A null sprite
 * returns having touched nothing.
 */
void __cdecl RowSetSprite(void *row, void *sprite, void *desc)
{
    uint8_t *r = (uint8_t *)row;
    uint8_t *spr = (uint8_t *)sprite;
    int32_t  w, h, need;

    if (!spr)
        return;

    w = *(const int32_t *)(spr + SPRITE_OFF_BOUNDS + 8);
    h = *(const int32_t *)(spr + SPRITE_OFF_BOUNDS + 12);
    if (w > 2)
        w -= 2;
    if (h > 2)
        h -= 2;
    need = ((h >> 8) + 2) * ((w >> 8) + 2);

    if (need > (int32_t)r[ROW_OFF_OWNS]) {
        uint8_t *now;

        ObjFlagClear0(r);
        RowUpdate(r, 0, desc);
        RowRelease(r, desc);

        ROW_FLD(r, ROW_OFF_PREV_SPRITE, void *) =
            ROW_FLD(r, ROW_OFF_SPRITE, void *);
        ROW_FLD(r, ROW_OFF_SPRITE, void *) = spr;
        ObjFlagSet0(r);

        now = ROW_FLD(r, ROW_OFF_SPRITE, uint8_t *);
        RowAlloc(*(const int32_t *)(now + SPRITE_OFF_BOUNDS + 8),
                 *(const int32_t *)(now + SPRITE_OFF_BOUNDS + 12), r, desc);
        RowUpdate(r, 0, desc);
        return;
    }

    ROW_FLD(r, ROW_OFF_PREV_SPRITE, void *) = ROW_FLD(r, ROW_OFF_SPRITE, void *);
    ROW_FLD(r, ROW_OFF_SPRITE, void *) = spr;
    RowUpdate(r, 0, desc);
}

/* 0x0040A1A0, eleven callers. Put a row on an animation frame: pick up the
 * animation whose entry id matches, reset it to cell 0, and set the sprite the
 * new cell asks for.
 *
 * FOUR OF THE FRAME VALUES ARE NOT FRAMES. -2 returns at once, -1 means "the
 * frame it is already on", 0 clears bit 0 of the row's first dword and returns
 * without touching the animation at all, and only then is the value an id to
 * look up. So a caller can say "stop", "again" or "nothing" through the same
 * argument, and a reconstruction that range-checked the id would swallow three
 * of the four.
 *
 * `force` is only consulted for the early-out: without it, asking for the
 * frame the row is already on returns immediately. With it, everything below
 * runs again -- which is what restarts an animation from cell 0.
 *
 * A PENDING TABLE IS TAKEN UP HERE AND NOWHERE ELSE. ROW_OFF_ANIM_NEXT, when
 * set, becomes ROW_OFF_ANIM_CUR and is cleared; that is the only consumer of
 * the field, so a table queued by anyone else waits for the next frame change.
 *
 * THE SEARCH FALLS BACK TO ENTRY 0 RATHER THAN FAILING. An id that is not in
 * the table takes entry 0 and the row is still told it is on the requested
 * frame -- ROW_OFF_FRAME is written from the argument, not from the entry
 * found. Reproduced; a not-found that returned early would leave the row on
 * its old animation and is the obvious tidier version.
 *
 * The heading it draws with is ROW_OFF_HEADING plus a per-FRAME bias byte from
 * ADDR_FRAME_HEADING_BIAS, added as an 8-bit value so it wraps. That table is
 * zero almost everywhere: index 19 holds 0xC0, three quarters of a turn, so
 * exactly one animation is drawn facing backwards and the table exists for it.
 * The sum then goes through RoundTo8 with the animation's own directionBits,
 * which is how an 8-bit heading becomes one of 1, 2, 8, 16 or 32 directions.
 *
 * The cell index is `frames * direction + cell`, which is the direction-major
 * layout anim.h records, and the sprite is that cell's id through
 * ADDR_SPRITE_LIST.
 *
 * THE LAST TWO LINES ARE A SPECIAL CASE FOR ONE LUT. ROW_OFF_FIELD_3C takes
 * the animation's field4, DOUBLED when the row's MAPOBJ_OFF_LUT is
 * ADDR_ROW_LUT_DOUBLES and not otherwise. The comparison is on the LUT's
 * ADDRESS, not its contents. What makes that lut special is not established
 * here; six sites compare against it and none of them says.
 *
 * MEASURED AT 11,698 CALLS on a driven Boot Camp mission, which makes this the
 * best-covered thing landed in a while -- every animating row on screen goes
 * through it every time its frame changes. The four special frame values, the
 * fallback to entry 0 and the heading bias are all on that path; the LUT
 * special case is the one arm a run cannot be assumed to reach, since it needs
 * a row using that particular lut.
 *
 * Landing it also took RowSetSprite's counter down to 146: those are the calls
 * from its OTHER callers, since this one now calls by name. The usual cost,
 * named here so the drop is not read as a regression.
 */
void __cdecl SetAnimFrame(void *row, int16_t frame, int32_t force)
{
    uint8_t         *r = (uint8_t *)row;
    AM2_AnimTable   *t;
    AM2_AnimEntry   *e;
    AM2_Anim        *anim;
    int32_t          f = frame;
    int32_t          n, i;
    int32_t          dir, cell;
    uint8_t          heading;
    int16_t          v;

    if (!force && frame == *(const int16_t *)(r + ROW_OFF_FRAME))
        return;

    if (f == -2)
        return;
    if (f == -1)
        frame = *(const int16_t *)(r + ROW_OFF_FRAME);
    else if (f == 0) {
        *(uint32_t *)r &= 0xFFFFFFFEu;
        return;
    }

    t = *(AM2_AnimTable *const *)(r + ROW_OFF_ANIM_NEXT);
    if (t) {
        *(AM2_AnimTable **)(r + ROW_OFF_ANIM_CUR)  = t;
        *(AM2_AnimTable **)(r + ROW_OFF_ANIM_NEXT) = (AM2_AnimTable *)0;
    }

    t = *(AM2_AnimTable *const *)(r + ROW_OFF_ANIM_CUR);
    if (!t)
        return;

    n = t->count;
    i = 0;
    if (n > 0) {
        for (i = 0; i < n; i++)
            if (t->entries[i].id == (int32_t)frame)
                break;
        if (i >= n)
            i = 0;                      /* not found: entry 0, not a failure */
    }

    e = &t->entries[i];

    *(uint8_t *)(r + ROW_OFF_CELL)  = 0;
    *(int16_t *)(r + ROW_OFF_FRAME) = frame;
    *(AM2_Anim **)(r + ROW_OFF_ANIM_PLAYING) = e->anim;
    *(uint8_t *)(r + ROW_OFF_HEADING_BIAS) =
        ((const uint8_t *)AM2_IMAGE(ADDR_FRAME_HEADING_BIAS))[frame];
    *(int16_t *)(r + ROW_OFF_ANIM_NEXT_ID) = (int16_t)e->next;

    anim    = *(AM2_Anim *const *)(r + ROW_OFF_ANIM_PLAYING);
    heading = (uint8_t)(*(const uint8_t *)(r + ROW_OFF_HEADING)
                        + *(const uint8_t *)(r + ROW_OFF_HEADING_BIAS));

    dir  = (uint8_t)RoundTo8(heading, anim->directionBits);
    cell = anim->frames * dir + *(const uint8_t *)(r + ROW_OFF_CELL);

    RowSetSprite(row,
                 (*(void *const *const *)(uintptr_t)ADDR_SPRITE_LIST)
                     [anim->cells[cell].sprite],
                 (void *)(uintptr_t)ADDR_MAP_DESC);

    anim = *(AM2_Anim *const *)(r + ROW_OFF_ANIM_PLAYING);
    v    = anim->field4;
    *(int16_t *)(r + ROW_OFF_FIELD_3C) = v;

    if (*(const void *const *)(r + MAPOBJ_OFF_LUT)
        == (const void *)(uintptr_t)ADDR_ROW_LUT_DOUBLES)
        *(int16_t *)(r + ROW_OFF_FIELD_3C) = (int16_t)(v + v);
}

/* 0x00434DA0, six callers. Build a ROW SET: one block of `count` rows, each
 * placed at its spec's offset from a base position and given an entry buffer
 * sized from the spec, with a bounding rect copied into the header.
 *
 * A SPEC IS FOUR int32 -- x, y, w, h -- and the two halves go to different
 * places: x and y are OFFSETS, added to the base the caller passes, and w and
 * h are a size handed straight to RowAlloc. Nothing in the spec is a sprite;
 * RowInit is given a null one, so every row starts blank and whatever draws it
 * sets the sprite later.
 *
 * The header is 0x20 bytes and only four things go in it: a zeroed dword at
 * +0, the count, the block, another zeroed dword at +0x0C, and sixteen bytes
 * of rect at +0x10. The two zeroed dwords are written explicitly rather than
 * left over from an allocation, so they are the caller's to read.
 *
 * THE ALLOCATION IS NOT CHECKED and neither is RowAlloc's answer, which is an
 * int32 the original discards. A count of zero allocates zero bytes, skips the
 * loop and still writes the header, so an empty set is a legal thing to build.
 *
 * The loop re-reads BOTH the count and the block pointer from the header every
 * iteration. Neither can change -- RowAlloc allocates the row's own buffer,
 * not this block -- and both re-reads are kept, because writing them as
 * loop-invariant reads would be asserting that rather than transcribing it.
 *
 * MEASURED AT 11 CALLS on a driven Boot Camp mission. RowAlloc reads 2,498
 * on the same run, which is its OTHER callers -- ours reaches it by name --
 * so nothing here says how many rows those eleven sets held, and whether
 * any of them had a count of zero is not established either. The header
 * writes and the loop are compared; the empty-set case is not claimed.
 */
void __cdecl BuildRowSet(void *set, int32_t count, const void *specs,
                         int32_t dx, int32_t dy, const void *rect)
{
    uint8_t *h = (uint8_t *)set;
    int32_t  i;

    *(int32_t *)h = 0;
    *(int32_t *)(h + ROWSET_OFF_COUNT) = count;
    *(void **)(h + ROWSET_OFF_ROWS) =
        am2_malloc((size_t)count * AM2_OBJ_ROW_STRIDE);

    for (i = 0; i < *(const int32_t *)(h + ROWSET_OFF_COUNT); i++) {
        const int32_t *spec =
            (const int32_t *)((const uint8_t *)specs
                              + (size_t)i * AM2_ROW_SPEC_BYTES);
        uint8_t *row = *(uint8_t *const *)(h + ROWSET_OFF_ROWS)
                       + (size_t)i * AM2_OBJ_ROW_STRIDE;

        RowInit(row, (void *)0, spec[0] + dx, spec[1] + dy);
        ObjFlagSet0(row);
        RowAlloc(spec[2], spec[3], row, (void *)AM2_IMAGE(ADDR_MAP_DESC));
    }

    *(int32_t *)(h + 0x0C) = 0;
    memcpy(h + ROWSET_OFF_RECT, rect, 16);
}

/* RowAnimField4 -- original 0x0040A130, two callers.
 *
 * This is the reader ROW_OFF_FIELD_2C's block in orig.h predicted from the
 * writing side: look an animation up in the row's current table by id, take
 * its AM2_Anim::field4, and DOUBLE it when the row's lut is the one that
 * doubles. Every other lut takes the value unchanged, which is exactly what
 * the comparison against ADDR_ROW_LUT_DOUBLES says.
 *
 * THE LUT IS COMPARED BY ADDRESS, not by anything in it. There is one lut that
 * doubles and the test is `row->lut == that table`, so a second doubling lut
 * could not be added without touching this function.
 *
 * THE FIRST TEST IS A SHORTCUT AND IT DOES NOT GO THROUGH THE TABLE. An id
 * equal to the row's own ROW_OFF_FRAME answers ROW_OFF_FIELD_3C directly --
 * the value already cached on the row -- and is NOT doubled. So the same id
 * gives two different answers depending on whether the row happens to be
 * showing it. Reproduced; it is the whole reason the cached field exists.
 *
 * A MISS FALLS BACK TO ENTRY 0 RATHER THAN FAILING, and does so without
 * checking the count, so a table with no entries is read out of bounds. That
 * is the same last-resort LoadAnimTable has and the same lack of a guard;
 * anim.h says no shipped file reaches it there, and nothing here changes that.
 *
 * Id 0 is refused before the table is touched at all.
 */
int16_t __cdecl RowAnimField4(const void *row, uint16_t id)
{
    const uint8_t       *r = (const uint8_t *)row;
    const AM2_AnimTable *table;
    int32_t              i = 0;
    int16_t              value;

    if ((int16_t)id == *(const int16_t *)(r + ROW_OFF_FRAME))
        return *(const int16_t *)(r + ROW_OFF_FIELD_3C);

    if (!id)
        return 0;

    table = *(const AM2_AnimTable *const *)(r + ROW_OFF_ANIM_CUR);
    if (!table)
        return 0;

    while (i < table->count && table->entries[i].id != (int32_t)(int16_t)id)
        i++;
    if (i >= table->count)
        i = 0;                  /* not found: entry 0, count unchecked */

    value = (int16_t)table->entries[i].anim->field4;

    if (*(const void *const *)(r + ROW_OFF_FIELD_2C)
        == (const void *)(uintptr_t)ADDR_ROW_LUT_DOUBLES)
        value = (int16_t)(value + value);

    return value;
}

/* BuildRowsFromDef -- original 0x00434C90, two callers, and BuildRowSet's
 * sibling one entry earlier in the image.
 *
 * Build a row set from a DEF record: allocate one block of rows, place each at
 * its spec's offset from the object's position, give it a depth key and an
 * entry buffer sized from its sprite, then copy the def's rect into the
 * header.
 *
 * THE TWO DIFFER IN WHERE THEIR INPUT COMES FROM AND IN THEIR SPEC SIZE.
 * BuildRowSet is handed a count, an array of four-int32 specs and a rect;
 * this one reads all three out of the def, and its specs are TWELVE bytes --
 * a sprite dword, two int16 offsets and an int16 depth, with two bytes spare.
 * Anything that assumes one stride from the other is wrong by a third.
 *
 * IT STORES THE DEF IN THE HEADER'S FIRST DWORD, where BuildRowSet zeroes it.
 * So a set built here remembers what it came from and one built there does
 * not, which is the only way to tell them apart afterwards.
 *
 * THE FLAG WRITTEN ON EACH ROW DEPENDS ON THE OBJECT, NOT THE SPEC, and it is
 * two conditions: bit 0 is SET only when the object's x is non-zero AND the
 * object is not destroyed. Every other case clears it. An x of exactly zero is
 * the map's left edge and reachable, so that is a live distinction and not a
 * null check in disguise -- the original tests the same argument it also adds
 * to every row's x.
 *
 * A FOURTH WRITER OF ROW_OFF_FIELD_26. The seq adders write 0x3E8 and 1 and
 * "terrain plus 0x3F2", ApplyHeightItem writes a scaled height, and this
 * copies an int16 straight out of the spec. Consistent with a depth key and
 * with nothing else.
 *
 * THE SUB-RECORD POINTER IS A POINTER INTO THE DEF, not a copy. The header's
 * +0x0C gets `def + 0x10` when the def's flag at +0x1C is set and NULL when it
 * is not -- so the set holds a borrowed pointer whose lifetime is the def's.
 *
 * The allocation is `count * 0x60` written as `(n + n*2) << 5`, which is the
 * row stride; nothing checks it succeeded, as nothing else in this subsystem
 * does.
 */
void __cdecl BuildRowsFromDef(void *set, const void *def, int32_t x, int32_t y,
                              uint32_t objFlags)
{
    uint8_t       *out = (uint8_t *)set;
    const uint8_t *d   = (const uint8_t *)def;
    int32_t        n   = *(const int32_t *)(d + DEFROWS_OFF_COUNT);
    int32_t        i;

    *(const void **)(out + ROWSET_OFF_DEF)   = def;
    *(int32_t *)(out + ROWSET_OFF_COUNT)     = n;
    *(void **)(out + ROWSET_OFF_ROWS)        =
        am2_malloc((size_t)n * AM2_OBJ_ROW_STRIDE);

    for (i = 0; i < *(const int32_t *)(out + ROWSET_OFF_COUNT); i++) {
        uint8_t *row = *(uint8_t **)(out + ROWSET_OFF_ROWS)
                       + (size_t)i * AM2_OBJ_ROW_STRIDE;
        const uint8_t *spec = *(const uint8_t *const *)(d + DEFROWS_OFF_SPECS)
                              + (size_t)i * AM2_DEFSPEC_BYTES;
        const uint8_t *spr;

        RowInit(row, *(void *const *)(spec + DEFSPEC_OFF_SPRITE),
                (int32_t)*(const int16_t *)(spec + DEFSPEC_OFF_DX) + x,
                (int32_t)*(const int16_t *)(spec + DEFSPEC_OFF_DY) + y);

        if (x && !(objFlags & OBJ_FLAG_DESTROYED))
            ObjFlagSet0(row);
        else
            ObjFlagClear0(row);

        *(int16_t *)(row + ROW_OFF_FIELD_26) =
            *(const int16_t *)(spec + DEFSPEC_OFF_DEPTH);

        spr = *(const uint8_t *const *)(row + ROW_OFF_SPRITE);
        RowAlloc(*(const int32_t *)(spr + SPR_OFF_W),
                 *(const int32_t *)(spr + SPR_OFF_H),
                 row, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    if (*(const int32_t *)(d + DEFROWS_OFF_HAS_SUBREC))
        *(const void **)(out + ROWSET_OFF_SUBREC) = d + DEFROWS_OFF_SUBREC;
    else
        *(const void **)(out + ROWSET_OFF_SUBREC) = (const void *)0;

    memcpy(out + ROWSET_OFF_RECT, d + DEFROWS_OFF_RECT, 16);
}

int maprow_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_BUILD_ROWS_FROM_DEF,
                        (const void *)BuildRowsFromDef,
                        "BuildRowsFromDef", 2);
    rc |= patch_replace(ADDR_ROW_ANIM_FIELD4, (const void *)RowAnimField4,
                        "RowAnimField4", 2);
    rc |= patch_replace(ADDR_BUILD_ROW_SET, (const void *)BuildRowSet,
                        "BuildRowSet", 6);

    rc |= patch_replace(ADDR_SET_ANIM_FRAME, (const void *)SetAnimFrame,
                        "SetAnimFrame", 11);

    rc |= patch_replace(ADDR_DEPTH_COMPARE, (const void *)DepthCompare,
                        "DepthCompare", 2);
    rc |= patch_replace(ADDR_DEPTH_LINK, (const void *)DepthLink,
                        "DepthLink", 2);
    rc |= patch_replace(ADDR_DEPTH_RESORT, (const void *)DepthResort,
                        "DepthResort", 2);
    rc |= patch_replace(ADDR_ROW_ALLOC, (const void *)RowAlloc,
                        "RowAlloc", 4);
    rc |= patch_replace(ADDR_ROW_REGISTER_ALL, (const void *)RowRegisterAll,
                        "RowRegisterAll", 2);
    rc |= patch_replace(ADDR_ROW_UPDATE, (const void *)RowUpdate,
                        "RowUpdate", 3);
    rc |= patch_replace(ADDR_MAP_DESC_FREE, (const void *)MapDescFree,
                        "MapDescFree", 3);
    rc |= patch_replace(ADDR_MAP_DESC_INIT, (const void *)MapDescInit,
                        "MapDescInit", 2);
    rc |= patch_replace(ADDR_ROW_INIT, (const void *)RowInit, "RowInit", 2);
    rc |= patch_replace(ADDR_ROW_SET_SPRITE, (const void *)RowSetSprite,
                        "RowSetSprite", 3);
    rc |= patch_replace(ADDR_ROWPOOL_A_INIT, (const void *)RowPoolAInit,
                        "RowPoolAInit", 1);
    rc |= patch_replace(ADDR_ROWPOOL_B_INIT, (const void *)RowPoolBInit,
                        "RowPoolBInit", 1);
    rc |= patch_replace(ADDR_ROWPOOL_A_FREE, (const void *)RowPoolAFree,
                        "RowPoolAFree", 1);
    rc |= patch_replace(ADDR_ROWPOOL_B_FREE, (const void *)RowPoolBFree,
                        "RowPoolBFree", 1);
    rc |= patch_replace(ADDR_ROWPOOL_A_RELEASE, (const void *)RowPoolARelease,
                        "RowPoolARelease", 1);
    rc |= patch_replace(ADDR_ROWPOOL_B_RELEASE, (const void *)RowPoolBRelease,
                        "RowPoolBRelease", 1);
    rc |= patch_replace(ADDR_ROWPOOL_A_EVICT, (const void *)RowPoolAEvict,
                        "RowPoolAEvict", 1);
    rc |= patch_replace(ADDR_ROWPOOL_B_EVICT, (const void *)RowPoolBEvict,
                        "RowPoolBEvict", 1);
    rc |= patch_replace(ADDR_ROWPOOL_A_ALLOC, (const void *)RowPoolAAlloc,
                        "RowPoolAAlloc", 1);
    rc |= patch_replace(ADDR_ROWPOOL_B_ALLOC, (const void *)RowPoolBAlloc,
                        "RowPoolBAlloc", 1);
    rc |= patch_replace(ADDR_SEQ_CTX_INIT, (const void *)SeqCtxInit,
                        "SeqCtxInit", 1);
    rc |= patch_replace(ADDR_SEQ_CTX_FREE, (const void *)SeqCtxFree,
                        "SeqCtxFree", 1);
    rc |= patch_replace(ADDR_SEQ_RETIRE, (const void *)SeqRetire,
                        "SeqRetire", 1);
    rc |= patch_replace(ADDR_SEQ_EVICT, (const void *)SeqEvict,
                        "SeqEvict", 1);
    rc |= patch_replace(ADDR_SEQ_ALLOC, (const void *)SeqAlloc,
                        "SeqAlloc", 1);
    rc |= patch_replace(ADDR_RESPAWN_KIND_ALLOWED,
                        (const void *)RespawnKindAllowed,
                        "RespawnKindAllowed", 2);
    rc |= patch_replace(ADDR_SEQ_SUBSYSTEM_INIT,
                        (const void *)SeqSubsystemInit,
                        "SeqSubsystemInit", 1);
    rc |= patch_replace(ADDR_FREE_SEQ_CONTEXTS, (const void *)FreeSeqContexts,
                        "FreeSeqContexts", 2);
    rc |= patch_replace(ADDR_RANDOM_RESPAWN_KIND,
                        (const void *)RandomRespawnKind,
                        "RandomRespawnKind", 2);
    return rc;
}

/* The two 12-byte ROW POOLS -- see orig.h above ADDR_ROWPOOL_A_COUNT for the
 * layout and for why it tiles. Fifteen functions in the image implement five
 * roles three times: once hardcoded per pool, and once generically over a seq
 * context. These are the hardcoded pair.
 *
 * A SLOT IS A HANDLE ONTO A PERMANENT ROW. The initialiser mallocs one
 * 0x60-byte row per slot and nothing frees it until teardown; allocate and
 * release only shuffle which slot holds which. That is why the allocator
 * never writes ROWPOOL_OFF_ROW and its callers dereference it immediately. */
struct AM2_RowPool {
    volatile int32_t *count;
    volatile int32_t *tail;
    uint8_t          *entries;
    int32_t           capacity;   /* the eviction threshold */
    int32_t           budget;     /* how many one eviction pass may free */
    int32_t           slots;      /* capacity + budget; the array's real size */
    int32_t           rowDim;     /* RowAlloc's w and h -- 0x60 A, 0x80 B */
};

static uint8_t *RowPoolEntry(const AM2_RowPool *p, int32_t i)
{
    return p->entries + (int32_t)(i * AM2_ROWPOOL_ENTRY_BYTES);
}

/* 0x00460800 (pool A) and 0x00460AC0 (pool B). Zero the header, make entry 0
 * the head sentinel, and give every slot its permanent row.
 *
 * The row is zeroed with `mov ecx, 0x18; rep stosd` -- 24 DWORDS, which is
 * the 0x60 bytes and not 0x18 of them.
 *
 * AND EACH ROW IS THEN REGISTERED WITH RowAlloc, which the first version of
 * this omitted -- `ab.sh bootcamp` came back with 37% of the frame wrong and
 * an identical log, the map-load-truncated signature. The malloc gives the
 * row STRUCT its 0x60 bytes; RowAlloc gives it its extent, and the two pools
 * differ there: 0x60 square for A, 0x80 square for B.
 *
 * Its arguments are cleaned by a single `add esp, 0x14` that also cleans the
 * malloc's -- five dwords for two calls -- which is the cdecl shape CLAUDE.md
 * warns makes a call's arity unreadable from the cleanup alone. */
static void RowPoolInit(const AM2_RowPool *p)
{
    int32_t i;

    *p->tail = 0;
    *p->count = 0;

    for (i = 0; i < p->slots; i++) {
        uint8_t *e = RowPoolEntry(p, i);
        uint8_t *row;

        *(int16_t *)(e + ROWPOOL_OFF_ID) = (int16_t)i;
        *(int16_t *)(e + ROWPOOL_OFF_PREV) = -1;
        *(int16_t *)(e + ROWPOOL_OFF_NEXT) = -1;

        row = (uint8_t *)am2_malloc(AM2_ROWPOOL_ROW_BYTES);
        *(void **)(e + ROWPOOL_OFF_ROW) = row;
        memset(row, 0, AM2_ROWPOOL_ROW_BYTES);
        RowAlloc(p->rowDim, p->rowDim, row, (void *)(uintptr_t)ADDR_MAP_DESC);
    }
}

/* 0x00460860 (pool A) and 0x00460B30 (pool B). The ONLY place a row is freed.
 * Walks every slot -- not the list -- because a released slot still owns its
 * row, so following the links would leak every slot not currently linked. */
static void RowPoolTeardown(const AM2_RowPool *p)
{
    int32_t i;

    for (i = 0; i < p->slots; i++) {
        uint8_t *e = RowPoolEntry(p, i);
        void *row = *(void **)(e + ROWPOOL_OFF_ROW);

        if (row != 0) {
            RowRelease(row, (void *)(uintptr_t)ADDR_MAP_DESC);
            am2_free(row);
            *(void **)(e + ROWPOOL_OFF_ROW) = 0;
        }
        *(int16_t *)(e + ROWPOOL_OFF_NEXT) = -1;
        *(int16_t *)(e + ROWPOOL_OFF_PREV) = -1;
    }

    *p->tail = 0;
    *p->count = 0;
}

/* 0x00460A60 (pool A) and 0x00460D30 (pool B). The two are ONE function
 * emitted twice: with the six parameters substituted they are 18 instructions
 * each and differ in two, both the evict call and the branch over it.
 *
 * The cap test is `jle`, so SIGNED, and it is not a refusal -- over the cap it
 * evicts and then allocates anyway. This function always returns a slot. */
static uint8_t *RowPoolAlloc(const AM2_RowPool *p, void (*evict)(void))
{
    uint8_t *e;
    int32_t  i;

    if (*p->count > p->capacity)
        evict();

    i = *p->count + 1;
    *p->count = i;
    e = RowPoolEntry(p, i);

    *(int16_t *)(e + ROWPOOL_OFF_ID) = (int16_t)i;
    *(int16_t *)(e + ROWPOOL_OFF_PREV) = (int16_t)*p->tail;
    *(int16_t *)(e + ROWPOOL_OFF_NEXT) = -1;

    /* Link the previous tail to us. Entry 0 is the head sentinel, so this is
     * correct with an empty list too: tail is 0 and entry 0's next takes the
     * new index. */
    *(int16_t *)(RowPoolEntry(p, *p->tail) + ROWPOOL_OFF_NEXT) = (int16_t)i;
    *p->tail = i;

    return e;
}

static const AM2_RowPool kRowPoolA = {
    (volatile int32_t *)(uintptr_t)ADDR_ROWPOOL_A_COUNT,
    (volatile int32_t *)(uintptr_t)ADDR_ROWPOOL_A_TAIL,
    (uint8_t *)(uintptr_t)ADDR_ROWPOOL_A_ENTRIES,
    AM2_ROWPOOL_A_CAP, AM2_ROWPOOL_A_BUDGET,
    AM2_ROWPOOL_A_CAP + AM2_ROWPOOL_A_BUDGET, 0x60
};

static const AM2_RowPool kRowPoolB = {
    (volatile int32_t *)(uintptr_t)ADDR_ROWPOOL_B_COUNT,
    (volatile int32_t *)(uintptr_t)ADDR_ROWPOOL_B_TAIL,
    (uint8_t *)(uintptr_t)ADDR_ROWPOOL_B_ENTRIES,
    AM2_ROWPOOL_B_CAP, AM2_ROWPOOL_B_BUDGET,
    AM2_ROWPOOL_B_CAP + AM2_ROWPOOL_B_BUDGET, 0x80
};

void __cdecl RowPoolAInit(void) { RowPoolInit(&kRowPoolA); }
void __cdecl RowPoolBInit(void) { RowPoolInit(&kRowPoolB); }
void __cdecl RowPoolAFree(void) { RowPoolTeardown(&kRowPoolA); }
void __cdecl RowPoolBFree(void) { RowPoolTeardown(&kRowPoolB); }
/* 0x004608C0 (pool A) and 0x00460B90 (pool B). One function emitted twice:
 * masked of constants the two are 82 instructions each and IDENTICAL,
 * similarity 1.000.
 *
 * FOUR ROLES, and the last one is the trap. It
 *   1. clears bit 0 of the row and calls RowUpdate, if the bit was set;
 *   2. unlinks the entry both ways;
 *   3. COMPACTS -- swaps the last entry into the freed slot, exchanging the
 *      rows so every slot keeps owning one;
 *   4. ANSWERS THE NEXT INDEX, corrected for the swap.
 *
 * Every other call site discards the answer, so `void(entry *)` is what the
 * function looks like -- and the evictor's second loop depends on it, taking
 * the new head from each release. Written as void, that loop would spin on a
 * stale index or index by an address.
 *
 * The correction in step 4 matters on its own: if the entry that FOLLOWED the
 * released one happened to be the last, the swap moved it and its old index
 * is stale. Without the fixup a walker follows a dangling index one time in
 * N, which is exactly the kind of defect no drive here would surface. */
static int32_t RowPoolRelease(const AM2_RowPool *p, uint8_t *e)
{
    int32_t  count = *p->count;
    int32_t  freed = *(int16_t *)(e + ROWPOOL_OFF_ID);
    int32_t  next  = *(int16_t *)(e + ROWPOOL_OFF_NEXT);
    int32_t  prev  = *(int16_t *)(e + ROWPOOL_OFF_PREV);
    uint8_t *last;
    void    *row;

    row = *(void **)(e + ROWPOOL_OFF_ROW);
    if ((*(uint32_t *)row & 1) != 0) {
        *(uint32_t *)row &= ~1u;
        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    /* Unlink. The head sentinel is entry 0, so prev is always a real slot. */
    *(int16_t *)(RowPoolEntry(p, prev) + ROWPOOL_OFF_NEXT) = (int16_t)next;
    if (next > 0)
        *(int16_t *)(RowPoolEntry(p, next) + ROWPOOL_OFF_PREV) = (int16_t)prev;
    if (*p->tail == freed)
        *p->tail = prev;

    /* Compact: the last entry moves down into the hole, and the freed slot's
     * row goes up to the vacated one. A MOVE rather than a swap would leak a
     * row per release and alias two entries onto one. */
    last = RowPoolEntry(p, count);
    if (freed != count) {
        int32_t lprev = *(int16_t *)(last + ROWPOOL_OFF_PREV);
        int32_t lnext = *(int16_t *)(last + ROWPOOL_OFF_NEXT);

        *(int16_t *)(RowPoolEntry(p, lprev) + ROWPOOL_OFF_NEXT) = (int16_t)freed;
        if (lnext > 0)
            *(int16_t *)(RowPoolEntry(p, lnext) + ROWPOOL_OFF_PREV) = (int16_t)freed;
        /* The original compares the saved `next` against the LAST ENTRY'S OWN
         * id (movsx from last+4 at 0x00460970), not against `count`. Those
         * are the same number whenever the pool is consistent, and using the
         * field is what the image does -- reproduce that rather than the
         * arithmetic that happens to agree with it. */
        if (next == *(int16_t *)(last + ROWPOOL_OFF_ID))
            next = freed;

        *(void **)(e + ROWPOOL_OFF_ROW)   = *(void **)(last + ROWPOOL_OFF_ROW);
        *(int32_t *)(e + ROWPOOL_OFF_ID)  = *(int32_t *)(last + ROWPOOL_OFF_ID);
        *(int32_t *)(e + ROWPOOL_OFF_NEXT)= *(int32_t *)(last + ROWPOOL_OFF_NEXT);
        *(void **)(last + ROWPOOL_OFF_ROW) = row;
        *(int16_t *)(e + ROWPOOL_OFF_ID)   = (int16_t)freed;
        if (*p->tail == count)
            *p->tail = freed;
    }

    *(int16_t *)(last + ROWPOOL_OFF_NEXT) = -1;
    *(int16_t *)(last + ROWPOOL_OFF_PREV) = -1;
    *p->count = count - 1;
    return next;
}
/* 0x004609D0 (pool A) and 0x00460CA0 (pool B). One function twice: with the
 * six parameters substituted they are 46 and 47 instructions and only EIGHT
 * differ, every one a branch displacement off by exactly 2 -- the imm32/imm8
 * encoding of the capacity propagating. Not a rewrite; a re-emission.
 *
 * TWO LOOPS, and reading only the first gives a tidy visibility cache that
 * misses what happens when the pool is full of things you can SEE:
 *
 *   1. walk the list from the head; release any entry whose row rectangle no
 *      longer meets the view; stop after `budget` releases or at the end.
 *   2. if the budget was NOT used up, release from the head `budget` more
 *      times REGARDLESS of any rectangle -- the pressure valve.
 *
 * The IntersectRect goes through the game's own IAT thunk, not an import of
 * ours, for the same reason device.cpp calls the DirectInput thunk: an import
 * in am2hook.dll would resolve through OUR IAT. (Here it is only USER32 and
 * nothing hooks it, so this is convention rather than necessity -- but the
 * convention is what keeps the rule legible.)
 *
 * Loop 2 takes its next index from the RELEASE'S RETURN VALUE. The original
 * loads the entry ADDRESS into eax, calls, and branches back to the lea --
 * which reads as indexing by an address until you notice `call` clobbers eax
 * with the answer. A version that walked saved indices would be wrong the
 * moment the compaction moved an entry. */
typedef int32_t (__stdcall *AM2_IntersectRectFn)(void *dst, const void *a,
                                                 const void *b);
#define orig_intersect_rect \
    (*(AM2_IntersectRectFn *)AM2_IMAGE(ADDR_IAT_INTERSECT_RECT))

static void RowPoolEvict(const AM2_RowPool *p)
{
    int32_t left = p->budget;
    int32_t i;

    if (*p->count <= p->capacity)
        return;

    i = *(int16_t *)(RowPoolEntry(p, 0) + ROWPOOL_OFF_NEXT);
    while (i > 0 && left > 0) {
        uint8_t *e = RowPoolEntry(p, i);
        uint8_t  scratch[16];
        void    *row = *(void **)(e + ROWPOOL_OFF_ROW);

        if (!orig_intersect_rect(scratch,
                                 (const void *)(uintptr_t)ADDR_VIEW_ORIGIN_X,
                                 (const uint8_t *)row + ROW_OFF_RECT)) {
            i = RowPoolRelease(p, e);
            left--;
            continue;
        }
        i = *(int16_t *)(e + ROWPOOL_OFF_NEXT);
    }

    /* The pressure valve: still over, so drop from the head regardless. */
    i = *(int16_t *)(RowPoolEntry(p, 0) + ROWPOOL_OFF_NEXT);
    while (left > 0) {
        i = RowPoolRelease(p, RowPoolEntry(p, i));
        left--;
    }
}

int32_t __cdecl RowPoolARelease(void *e)
{
    return RowPoolRelease(&kRowPoolA, (uint8_t *)e);
}
int32_t __cdecl RowPoolBRelease(void *e)
{
    return RowPoolRelease(&kRowPoolB, (uint8_t *)e);
}
void __cdecl RowPoolAEvict(void);
void __cdecl RowPoolBEvict(void);

void *__cdecl RowPoolAAlloc(void)
{
    return RowPoolAlloc(&kRowPoolA, RowPoolAEvict);
}
void *__cdecl RowPoolBAlloc(void)
{
    return RowPoolAlloc(&kRowPoolB, RowPoolBEvict);
}
void __cdecl RowPoolAEvict(void) { RowPoolEvict(&kRowPoolA); }
void __cdecl RowPoolBEvict(void) { RowPoolEvict(&kRowPoolB); }

/* ---- The SEQ contexts: the same five roles, parameterised ----------------
 *
 * These are the third implementation of the row-pool algorithm and the only
 * one that is a REWRITE rather than a re-emission -- 0.222 similarity against
 * the hardcoded pair, which diff at 1.000 against each other.
 *
 * Records are 48 bytes here, not 12, and the array is a POINTER at
 * SEQ_CTX_OFF_RECORDS rather than being inline -- so the same conceptual
 * field is one dereference deep in this shape and zero in the other.
 *
 * AND THE SLACK IS SOMEWHERE ELSE. The hardcoded pools carry `budget` spare
 * slots above their capacity; a context carries none, and its margin is
 * subtracted from the eviction threshold instead. Writing this from the
 * hardcoded version's outline would overrun the array. */
static uint8_t *SeqRecord(const uint8_t *ctx, int32_t i)
{
    return *(uint8_t **)(uintptr_t)(ctx + SEQ_CTX_OFF_RECORDS)
           + (int32_t)(i * AM2_SEQ_RECORD_SIZE);
}

/* 0x00460D90. Five arguments -- the caller pushes 0x20, 0x20, 0x28, 0xC8 and
 * the context, and the last two are RowAlloc's width and height, which the
 * hardcoded initialisers inline as 0x60 and 0x80 square.
 *
 * The loop runs to CAPACITY, which espmap settles: the bound at 0x00460E15
 * and `capacity` at 0x00460D9B read the same frame slot. */
void __cdecl SeqCtxInit(void *ctxv, int32_t capacity, int32_t margin,
                        int32_t w, int32_t h)
{
    uint8_t *ctx = (uint8_t *)ctxv;
    int32_t  i;

    *(int32_t *)(ctx + SEQ_CTX_OFF_CAPACITY) = capacity;
    *(int32_t *)(ctx + SEQ_CTX_OFF_MARGIN) = margin;
    *(int32_t *)(ctx + SEQ_CTX_OFF_TAIL) = 0;
    *(int32_t *)(ctx + SEQ_CTX_OFF_COUNT) = 0;
    *(void **)(ctx + SEQ_CTX_OFF_RECORDS) =
        am2_malloc((size_t)(capacity * AM2_SEQ_RECORD_SIZE));

    for (i = 0; i < capacity; i++) {
        uint8_t *rec = SeqRecord(ctx, i);
        uint8_t *row;

        *(int16_t *)(rec + SEQ_OFF_ID) = (int16_t)i;
        *(int16_t *)(rec + SEQ_OFF_NEXT) = -1;
        *(int16_t *)(rec + SEQ_OFF_PREV) = -1;

        row = (uint8_t *)am2_malloc(AM2_ROWPOOL_ROW_BYTES);
        *(void **)(rec + SEQ_OFF_ROW) = row;
        memset(row, 0, AM2_ROWPOOL_ROW_BYTES);
        RowAlloc(w, h, row, (void *)(uintptr_t)ADDR_MAP_DESC);
    }
}

/* 0x00460E30. Free every record's row. Opens with an early-out on a NULL
 * record array, which the hardcoded teardowns have no need of because their
 * arrays are static and always there -- a real difference between the two
 * shapes, not an oversight in either. */
void __cdecl SeqCtxFree(void *ctxv)
{
    uint8_t *ctx = (uint8_t *)ctxv;
    int32_t  i;

    if (*(void **)(ctx + SEQ_CTX_OFF_RECORDS) == 0)
        return;

    for (i = 0; i < *(const int32_t *)(ctx + SEQ_CTX_OFF_CAPACITY); i++) {
        uint8_t *rec = SeqRecord(ctx, i);
        void    *row = *(void **)(rec + SEQ_OFF_ROW);

        if (row != 0) {
            RowRelease(row, (void *)(uintptr_t)ADDR_MAP_DESC);
            am2_free(row);
            *(void **)(rec + SEQ_OFF_ROW) = 0;
        }
    }
}

/* 0x00460EC0. THE CONTEXT IS THE FIRST ARGUMENT and the record the second --
 * `push rec; push ctx; call; add esp, 8` at both of the evictor's call sites.
 * Read off the body alone it looks like (rec, ctx), which is what I wrote
 * first; the caller settles it and the callee's frame slots agree.
 *
 * Same four roles as the hardcoded release: clear the row's bit 0 and
 * RowUpdate, unlink both ways, compact by swapping the last record into the
 * hole, and answer the next index corrected for that swap. */
int32_t __cdecl SeqRetire(void *ctxv, void *recv)
{
    uint8_t *ctx = (uint8_t *)ctxv;
    uint8_t *rec = (uint8_t *)recv;
    int32_t  count = *(const int32_t *)(ctx + SEQ_CTX_OFF_COUNT);
    int32_t  freed = *(int16_t *)(rec + SEQ_OFF_ID);
    int32_t  next  = *(int16_t *)(rec + SEQ_OFF_NEXT);
    int32_t  prev  = *(int16_t *)(rec + SEQ_OFF_PREV);
    uint8_t *last;
    void    *row   = *(void **)(rec + SEQ_OFF_ROW);

    if ((*(uint32_t *)row & 1) != 0) {
        *(uint32_t *)row &= ~1u;
        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    *(int16_t *)(SeqRecord(ctx, prev) + SEQ_OFF_NEXT) = (int16_t)next;
    if (next > 0)
        *(int16_t *)(SeqRecord(ctx, next) + SEQ_OFF_PREV) = (int16_t)prev;
    if (*(const int32_t *)(ctx + SEQ_CTX_OFF_TAIL) == freed)
        *(int32_t *)(ctx + SEQ_CTX_OFF_TAIL) = prev;

    last = SeqRecord(ctx, count);
    if (freed != count) {
        int32_t lprev = *(int16_t *)(last + SEQ_OFF_PREV);
        int32_t lnext = *(int16_t *)(last + SEQ_OFF_NEXT);

        *(int16_t *)(SeqRecord(ctx, lprev) + SEQ_OFF_NEXT) = (int16_t)freed;
        if (lnext > 0)
            *(int16_t *)(SeqRecord(ctx, lnext) + SEQ_OFF_PREV) = (int16_t)freed;
        if (next == *(int16_t *)(last + SEQ_OFF_ID))
            next = freed;

        memcpy(rec, last, AM2_SEQ_RECORD_SIZE);
        *(void **)(last + SEQ_OFF_ROW) = row;
        *(int16_t *)(rec + SEQ_OFF_ID) = (int16_t)freed;
        if (*(const int32_t *)(ctx + SEQ_CTX_OFF_TAIL) == count)
            *(int32_t *)(ctx + SEQ_CTX_OFF_TAIL) = freed;
    }

    *(int16_t *)(last + SEQ_OFF_NEXT) = -1;
    *(int16_t *)(last + SEQ_OFF_PREV) = -1;
    *(int32_t *)(ctx + SEQ_CTX_OFF_COUNT) = count - 1;
    return next;
}

/* 0x00460FD0. Same two loops as the hardcoded evictor -- drop what the view
 * no longer meets, then drop from the head regardless if that freed too few. */
void __cdecl SeqEvict(void *ctxv)
{
    uint8_t *ctx = (uint8_t *)ctxv;
    int32_t  left = *(const int32_t *)(ctx + SEQ_CTX_OFF_MARGIN);
    int32_t  i;

    /* IT RE-TESTS THE THRESHOLD SeqAlloc ALREADY TESTED, and that is the only
     * guard it has. The first version of this invented two others -- a NULL
     * record array and a zero count -- and omitted this one, which lets a
     * context below the threshold fall into the second loop and free `margin`
     * live records from the head.
     *
     * It was written from the hardcoded evictor's shape from memory, two
     * commits after recording that these five are a REWRITE and that nothing
     * carries over but the outline. Verified against 0x00460FD3 now. */
    if (*(const int32_t *)(ctx + SEQ_CTX_OFF_COUNT)
        <= *(const int32_t *)(ctx + SEQ_CTX_OFF_CAPACITY)
           - *(const int32_t *)(ctx + SEQ_CTX_OFF_MARGIN))
        return;

    i = *(int16_t *)(SeqRecord(ctx, 0) + SEQ_OFF_NEXT);
    while (i > 0 && left > 0) {
        uint8_t *rec = SeqRecord(ctx, i);
        uint8_t  scratch[16];

        if (!orig_intersect_rect(scratch,
                                 (const void *)(uintptr_t)ADDR_VIEW_ORIGIN_X,
                                 *(uint8_t **)(rec + SEQ_OFF_ROW) + ROW_OFF_RECT)) {
            i = SeqRetire(ctx, rec);
            left--;
            continue;
        }
        i = *(int16_t *)(rec + SEQ_OFF_NEXT);
    }

    i = *(int16_t *)(SeqRecord(ctx, 0) + SEQ_OFF_NEXT);
    while (left > 0) {
        i = SeqRetire(ctx, SeqRecord(ctx, i));
        left--;
    }
}

/* 0x00461070. THE THRESHOLD IS `count > capacity - margin`, not
 * `count > capacity` as the hardcoded allocators test. The margin is this
 * shape's slack: subtracted from the threshold, where the hardcoded pools add
 * it to the array as spare slots. Writing this from their outline would run
 * the array off its end. */
void *__cdecl SeqAlloc(void *ctxv)
{
    uint8_t *ctx = (uint8_t *)ctxv;
    uint8_t *rec;
    int32_t  i;

    if (*(const int32_t *)(ctx + SEQ_CTX_OFF_COUNT)
        > *(const int32_t *)(ctx + SEQ_CTX_OFF_CAPACITY)
          - *(const int32_t *)(ctx + SEQ_CTX_OFF_MARGIN))
        SeqEvict(ctx);

    i = *(const int32_t *)(ctx + SEQ_CTX_OFF_COUNT) + 1;
    *(int32_t *)(ctx + SEQ_CTX_OFF_COUNT) = i;
    rec = SeqRecord(ctx, i);

    *(int16_t *)(rec + SEQ_OFF_ID) = (int16_t)i;
    *(int16_t *)(rec + SEQ_OFF_PREV) =
        (int16_t)*(const int32_t *)(ctx + SEQ_CTX_OFF_TAIL);
    *(int16_t *)(rec + SEQ_OFF_NEXT) = -1;
    *(int16_t *)(SeqRecord(ctx, *(const int32_t *)(ctx + SEQ_CTX_OFF_TAIL))
                 + SEQ_OFF_NEXT) = (int16_t)i;
    *(int32_t *)(ctx + SEQ_CTX_OFF_TAIL) = i;

    return rec;
}

/* ---- The respawn pool: two small functions off the map-load path --------- */

/* region.cpp spells this the same way; ADDR_GAME_RAND is above the nominal
 * CRT line but is the game's own LCG, not libc's. */
typedef int32_t (__cdecl *AM2_GameRandFn)(void);
#define orig_game_rand ((AM2_GameRandFn)(uintptr_t)AM2_IMAGE(ADDR_GAME_RAND))


/* 0x004600F0. May this weapon kind respawn?
 *
 * PERMISSIVE ON THREE OF FOUR PATHS -- it answers 1 unless a multiplayer
 * session is up AND the kind carries a mask AND that mask misses the game-over
 * flags. Since ADDR_MP_SESSION is 0 on every drive this project has, the
 * refusing exit cannot execute here and is verified by reading. */
int32_t __cdecl RespawnKindAllowed(int32_t kind)
{
    uint32_t mask;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION == 0)
        return 1;

    mask = ((const uint32_t *)(uintptr_t)ADDR_RESPAWN_KIND_MASK)[kind];
    if (mask == 0)
        return 1;
    if ((*(const uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS & mask) != 0)
        return 1;
    return 0;
}

/* 0x004601D0. Pick a respawn kind at random.
 *
 * IT HAS TWO OUTPUTS AND ONE OF THEM IS THE RETURN VALUE, which reading the
 * body alone misses: eax takes the chosen kind at 0x004601E1 and is never
 * written again, and 0x0042D1FA passes it straight to EnsureSpriteAaiRecord.
 * The OTHER caller discards it and uses only the out-pointer -- so one call
 * site would have confirmed the wrong reading.
 *
 * The division is `cdq; idiv`, SIGNED. MSVC's rand cannot answer negative so
 * it cannot matter, but writing it unsigned is a different function for an
 * input the original accepts.
 *
 * And 0x00662060 is NOT a table: it is ADDR_MISSILE_DEFS + 0x30, with the
 * lea/lea pair computing kind * AM2_MISSILE_DEF_BYTES. It has exactly the
 * shape of a fresh base-plus-stride array. */
int32_t __cdecl RandomRespawnKind(int32_t *out)
{
    int32_t r = orig_game_rand()
                % *(const int32_t *)(uintptr_t)ADDR_RESPAWN_KIND_COUNT;
    int32_t kind = (*(int32_t *const *)(uintptr_t)ADDR_RESPAWN_KINDS)[r];

    *out = *(const int32_t *)((const uint8_t *)AM2_IMAGE(ADDR_MISSILE_DEFS)
                               + kind * AM2_MISSILE_DEF_BYTES
                               + MISSILEDEF_OFF_FIELD_30);
    return kind;
}

/* 0x004610E0. The whole row/seq subsystem coming up: both hardcoded pools,
 * then both contexts. Reached from ADDR_STATE2_ENTER, so entering a mission
 * runs it.
 *
 * The two SeqCtxInit calls' TEN arguments are cleaned by one `add esp, 0x28`
 * -- the shared-cleanup shape again, which is why the arity of either call
 * cannot be read off the cleanup and had to come from the pushes. */
void __cdecl SeqSubsystemInit(void)
{
    RowPoolAInit();
    RowPoolBInit();
    SeqCtxInit((void *)(uintptr_t)ADDR_SEQ_CTX_A, 200, 40, 0x20, 0x20);
    SeqCtxInit((void *)(uintptr_t)ADDR_SEQ_CTX_B, 100, 25, 0x40, 0x40);
}

/* 0x00461120. The same subsystem coming down, and it is LIFO: the contexts
 * before the pools, and B before A in both halves where the init did A before
 * B. orig.h's note on this address -- "the same releaser over CTX_B then
 * CTX_A, then two more calls in the same band" -- is those two pool
 * teardowns, which had no names when it was written. */
void __cdecl FreeSeqContexts(void)
{
    SeqCtxFree((void *)(uintptr_t)ADDR_SEQ_CTX_B);
    SeqCtxFree((void *)(uintptr_t)ADDR_SEQ_CTX_A);
    RowPoolBFree();
    RowPoolAFree();
}
