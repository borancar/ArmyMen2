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

typedef void (__cdecl *AM2_DirtyCollectFn)(const void *rect);
#define DirtyCollect ((AM2_DirtyCollectFn)(uintptr_t)ADDR_DIRTY_COLLECT)

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

    DirtyCollect(r + ROW_OFF_RECT);

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
        DirtyCollect(box);
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

int maprow_install(void)
{
    int rc = 0;

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
    return rc;
}
