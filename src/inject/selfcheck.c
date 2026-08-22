/* Differential self-check, in the game's own process -- see selfcheck.h.
 *
 * The harness is already injected into the game, so the original function is
 * at its address in the same address space as the reconstruction. Calling both
 * needs no emulator, and more to the point it needs no translation: one set of
 * pointers, one set of globals, and a scratch buffer both sides address
 * identically.
 *
 * tools/vectors.py does the same comparison offline with Unicorn, and that is
 * worth keeping -- it runs in seconds with no game at all, which is what makes
 * it usable while writing a function. But everything it has needed fixing for
 * has been a consequence of having two address spaces: a NULL argument emitted
 * as 0 minus the scratch base, written pointers that could not be compared
 * byte for byte, a replay buffer smaller than the emulator's map. None of that
 * exists here.
 *
 * NOT every reconstruction can be listed here. A function that follows a
 * pointer out of its argument, or takes a loop count from it, needs that field
 * to hold something sensible -- offline the emulator faults and the vector is
 * quietly dropped, but here a fault kills the game before anything is
 * reported. XorChecksum, ChainField14 and Field51MeetsMin were added and
 * removed again for exactly that: the run died with 47 functions announced and
 * no summary. They need the same seeding tools/vectors.py has, which this does
 * not have yet.
 *
 * This must run before install() patches anything. A patch overwrites the
 * original's first five bytes with a jump, and there is no trampoline, so
 * after that the original is not callable at all.
 */

#include "selfcheck.h"
#include "hooklog.h"
#include "orig.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../game/rect.h"
#include "../game/dist.h"
#include "../game/packkey.h"
#include "../game/objtype.h"
#include "../game/misc.h"
#include "../game/script.h"
#include "../game/item.h"
#include "../game/objflag.h"
#include "../game/msgslot.h"
#include "../game/army.h"

/* Big enough for the worst reach any function under test has. MaskPixelSolid
 * takes a row offset from the buffer itself, so it can address up to 0xFFFF
 * past its argument -- with a small buffer the first run walked off the end and
 * killed the process before a single comparison was logged. */
#define SCRATCH_BYTES 0x14000

static uint8_t g_scratch[SCRATCH_BYTES];

/* Same shape as the offline harness so a failure reads the same either way. */
static void fill_scratch(void)
{
    uint32_t i;

    for (i = 0; i < SCRATCH_BYTES; i++)
        g_scratch[i] = (uint8_t)((i * 7 + 13) & 0xFF);
}

/* Every reconstruction under test here is cdecl, so one invoker serves them
 * all: the caller cleans up, so passing more arguments than the callee reads
 * is harmless. */
typedef uint32_t (__cdecl *am2_any_fn)(uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, uint32_t);

struct check {
    const char *name;
    uint32_t    original;      /* address in the game image */
    void       *ours;
    int32_t     nargs;
    uint8_t     isptr[4];
};

static const struct check kChecks[] = {
    { "Clamp",          ADDR_CLAMP,          (void *)Clamp,          3, {0,0,0,0} },
    { "ApproxDist",     ADDR_APPROX_DIST,    (void *)ApproxDist,     2, {1,1,0,0} },
    { "ApproxDistXY",   ADDR_APPROX_DIST_XY, (void *)ApproxDistXY,   2, {0,0,0,0} },
    { "AngleDelta",     ADDR_ANGLE_DELTA,    (void *)AngleDelta,     2, {0,0,0,0} },
    { "RoundTo8",       ADDR_ROUND_TO_8,     (void *)RoundTo8,       2, {0,0,0,0} },
    { "MakePoint",      ADDR_MAKE_POINT,     (void *)MakePoint,      2, {0,0,0,0} },
    { "PointsEqual",    ADDR_POINTS_EQUAL,   (void *)PointsEqual,    2, {0,0,0,0} },
    { "PointsDiffer",   ADDR_POINTS_DIFFER,  (void *)PointsDiffer,   2, {0,0,0,0} },
    { "PointInRect",    ADDR_POINT_IN_RECT,  (void *)PointInRect,    2, {1,1,0,0} },
    { "PackKey",        ADDR_PACK_KEY,       (void *)PackKey,        3, {0,0,0,0} },
    { "KeyFieldA",      ADDR_KEY_FIELD_A,    (void *)KeyFieldA,      1, {0,0,0,0} },
    { "KeyFieldB",      ADDR_KEY_FIELD_B,    (void *)KeyFieldB,      1, {0,0,0,0} },
    { "KeyFieldC",      ADDR_KEY_FIELD_C,    (void *)KeyFieldC,      1, {0,0,0,0} },
    { "UidArmy",        ADDR_UID_ARMY,       (void *)UidArmy,        1, {0,0,0,0} },
    { "UidOnWire",      ADDR_UID_ON_WIRE,    (void *)UidOnWire,      1, {0,0,0,0} },
    { "ObjIsItem",      ADDR_OBJ_IS_ITEM,    (void *)ObjIsItem,      1, {1,0,0,0} },
    { "ObjIsType2",     ADDR_OBJ_IS_TYPE2,   (void *)ObjIsType2,     1, {1,0,0,0} },
    /* The one army helper that can survive a random argument: ObjIsFriendly
     * reads its object out to +0x544, which the scratch covers.
     *
     * The other four in army.h are deliberately absent, and the reasons differ.
     * AllyFlag indexes a 4x4 matrix with no bounds check at all, so `pick`'s
     * large values would read unmapped memory; ArmiesAllied tail-calls it; and
     * ForEachArmyObject would CALL its second argument, which here is a random
     * integer.
     *
     * LookupOwnerObj is the interesting one, and it was added and taken out
     * again: it range-checks the army perfectly well, and then indexes
     * 0x004F9ECC, which is still NULL this early. This runs before install(),
     * which is before the game has loaded anything -- so "survives a random
     * argument" is not the only question. It has to survive the empty world
     * this runs in. */
    { "ObjIsFriendly",  ADDR_OBJ_IS_FRIENDLY, (void *)ObjIsFriendly,  1, {1,0,0,0} },
    { "ObjIsType3",     ADDR_OBJ_IS_TYPE3,   (void *)ObjIsType3,     1, {1,0,0,0} },
    { "ObjIsType4",     ADDR_OBJ_IS_TYPE4,   (void *)ObjIsType4,     1, {1,0,0,0} },
    { "ObjIsType8",     ADDR_OBJ_IS_TYPE8,   (void *)ObjIsType8,     1, {1,0,0,0} },
    { "ObjIsTypeIn238", ADDR_OBJ_IS_TYPE238, (void *)ObjIsTypeIn238, 1, {1,0,0,0} },
    { "ObjFieldA",      ADDR_OBJ_FIELD_A,    (void *)ObjFieldA,      1, {1,0,0,0} },
    { "ObjFieldB",      ADDR_OBJ_FIELD_B,    (void *)ObjFieldB,      1, {1,0,0,0} },
    { "ObjFlagBit0",    ADDR_OBJ_FLAG_BIT0,  (void *)ObjFlagBit0,    1, {1,0,0,0} },
    { "ObjFlagBit1",    ADDR_OBJ_FLAG_BIT1,  (void *)ObjFlagBit1,    1, {1,0,0,0} },
    { "IsBlank",        ADDR_IS_BLANK,       (void *)IsBlank,        1, {0,0,0,0} },
    { "IsScriptDelim",  ADDR_IS_SCRIPT_DELIM,(void *)IsScriptDelim,  1, {0,0,0,0} },
    { "IsKind10To17",   ADDR_IS_KIND_10_17,  (void *)IsKind10To17,   1, {0,0,0,0} },
    { "IsKind14Or22",   ADDR_IS_KIND_14_22,  (void *)IsKind14Or22,   1, {0,0,0,0} },
    { "IsKind7",        ADDR_IS_KIND_7,      (void *)IsKind7,        1, {1,0,0,0} },
    { "KindInSetA",     ADDR_KIND_IN_SET_A,  (void *)KindInSetA,     1, {0,0,0,0} },
    { "KindInSetB",     ADDR_KIND_IN_SET_B,  (void *)KindInSetB,     1, {0,0,0,0} },
    { "MapCode",        ADDR_MAP_CODE,       (void *)MapCode,        1, {0,0,0,0} },
    { "ScaleBy32Blocks",ADDR_SCALE_32_BLOCKS,(void *)ScaleBy32Blocks,1, {0,0,0,0} },
    { "AddByteSat",     ADDR_ADD_BYTE_SAT,   (void *)AddByteSat,     2, {0,0,0,0} },
    { "SwapColourBytes",ADDR_SWAP_COLOUR_BYTES,(void *)SwapColourBytes,2,{0,0,0,0} },
    { "ScriptCompare",  ADDR_SCRIPT_COMPARE, (void *)ScriptCompare,  3, {0,0,0,0} },
    { "TypesCompatible",ADDR_TYPES_COMPATIBLE,(void *)TypesCompatible,2,{0,0,0,0} },
    { "ComparePair",    ADDR_COMPARE_PAIR,   (void *)ComparePair,    2, {1,1,0,0} },
    { "CompareTriple",  ADDR_COMPARE_TRIPLE, (void *)CompareTriple,  2, {1,1,0,0} },
    { "CompareDword",   ADDR_COMPARE_DWORD,  (void *)CompareDword,   2, {1,1,0,0} },
    { "Field53C",       ADDR_FIELD_53C,      (void *)Field53C,       1, {1,0,0,0} },
    { "MaskPixelSolid", ADDR_MASK_PIXEL_SOLID,(void *)MaskPixelSolid,3, {0,0,1,0} },
    { "ObjKind538In10To17", ADDR_OBJ_KIND538_10_17,
                            (void *)ObjKind538In10To17, 1, {1,0,0,0} },
    { "FilterMatches",  ADDR_FILTER_MATCHES, (void *)FilterMatches,  6, {0,0,0,0} },
};

/* A cheap deterministic generator: the interesting values first, then a
 * repeatable spread. No RNG, so a failure is reproducible from its index. */
static uint32_t pick(uint32_t k, uint32_t slot)
{
    static const uint32_t edge[] = {
        0u, 1u, 0xFFFFFFFFu, 2u, 0xFFFFFFFEu, 7u, 8u, 255u, 256u,
        0x7FFFFFFFu, 0x80000000u, 100u, 0xFFFFFF9Cu, 29u, 30u, 42u, 70u,
    };
    uint32_t n = (uint32_t)(sizeof edge / sizeof edge[0]);

    if (k < n)
        return edge[(k + slot) % n];
    return (k * 2654435761u) ^ (slot * 40503u);
}

int selfcheck_run(void)
{
    const char *opt = getenv("AM2_SELFCHECK");
    uint32_t    i, k;
    int32_t     checked = 0, failed = 0;

    if (!opt || *opt != '1')
        return 0;

    hooklog("selfcheck: starting, %d functions",
            (int32_t)(sizeof kChecks / sizeof kChecks[0]));

    for (i = 0; i < sizeof kChecks / sizeof kChecks[0]; i++) {
        const struct check *c = &kChecks[i];
        int32_t             bad = 0;

        for (k = 0; k < 128; k++) {
            uint32_t a[6];
            uint32_t got, want;
            int32_t  j;

            fill_scratch();
            for (j = 0; j < 6; j++) {
                if (j < c->nargs && c->isptr[j])
                    /* Never NULL. Offline a null argument that faults simply
                     * drops the vector; here it takes the game with it, and
                     * ApproxDist -- which dereferences unconditionally -- died
                     * on the second function tested. The null paths are
                     * covered by tools/vectors.py, which can afford to fault. */
                    a[j] = (uint32_t)(uintptr_t)(g_scratch + 0x100 * (j + 1));
                else
                    a[j] = (j < c->nargs) ? pick(k, (uint32_t)j) : 0;
            }

            got = ((am2_any_fn)c->ours)(a[0], a[1], a[2], a[3], a[4], a[5]);
            fill_scratch();
            want = ((am2_any_fn)(uintptr_t)c->original)(a[0], a[1], a[2],
                                                        a[3], a[4], a[5]);
            checked++;
            if (got != want) {
                if (!bad)
                    hooklog("selfcheck: %s(%08x,%08x,%08x,%08x,%08x,%08x)"
                            " -> %08x, original %08x",
                            c->name, a[0], a[1], a[2], a[3], a[4], a[5],
                            got, want);
                bad++;
                failed++;
            }
        }
    }

    hooklog("selfcheck: %d calls across %d functions, %d disagree",
            checked, (int32_t)(sizeof kChecks / sizeof kChecks[0]), failed);
    return failed;
}
