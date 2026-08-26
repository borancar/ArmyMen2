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

#include "maprow.h"
#include "objtable.h"
#include "objflag.h"   /* ObjFlagBit0 -- reconstructed */
#include "item.h"      /* RowUnregisterAll -- reconstructed */
#include "misc.h"      /* ListUnlink -- reconstructed */
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
    ax = *(const int16_t *)(pa + OBJ_OFF_SCREEN_X);
    ay = *(const int16_t *)(pa + OBJ_OFF_SCREEN_Y);
    bx = *(const int16_t *)(pb + OBJ_OFF_SCREEN_X);
    by = *(const int16_t *)(pb + OBJ_OFF_SCREEN_Y);

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

int maprow_install(void)
{
    int rc = 0;

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
    return rc;
}
