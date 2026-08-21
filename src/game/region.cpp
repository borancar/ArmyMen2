/* region.cpp -- see region.h. */
#include <stdint.h>

#include "region.h"
#include "image.h"
#include "crt.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kRegionOfCell (*(uint8_t **)AM2_IMAGE(ADDR_REGION_OF_CELL))
/* 0x00514EF0 holds a POINTER to the array, not the array. Same shape as the
 * cell map above; getting this wrong took the game down on the first run. */
#define kRegions      (*(uint8_t **)AM2_IMAGE(ADDR_REGIONS))
#define kTracePF      (*(const int32_t *)AM2_IMAGE(ADDR_OPT_TRACE_PF))

#define kNLinks(r)  (*(uint8_t *)(kRegions + (r) * AM2_REGION_SIZE \
                                 + REGION_OFF_NLINKS))
#define kLinks(r)   (*(AM2_RegionLink **)(kRegions + (r) * AM2_REGION_SIZE \
                                          + REGION_OFF_LINKS))

/* 0x0042B860. Record that two cells connect their regions.
 *
 * Both cells are looked up in the per-cell region map, and region 0 means "no
 * region" -- either end being 0 drops the edge silently.
 *
 * The duplicate test compares the destination AND the near cell, not the far
 * one, so two different crossings into the same region are both kept while the
 * same crossing offered twice is not. That asymmetry is the original's.
 *
 * The array grows by exactly one link per call -- a realloc every time, with
 * no capacity -- and the new slot is zeroed before all three of its fields are
 * written, so the zeroing cannot be observed. Both reproduced.
 *
 * The edge is one-way: nothing here adds the reverse. Callers that want both
 * directions call twice.
 *
 * The log is gated on -tracePF, which is what identifies this graph as the
 * pathfinding structure, and it prints the two REGION ids rather than the
 * cells it was given. */
void __cdecl AddRegionLink(int32_t cell, int32_t neighbour)
{
    const uint8_t *map = kRegionOfCell;
    int32_t        a   = map[cell];
    int32_t        b   = map[neighbour];
    AM2_RegionLink *links;
    int32_t         n;

    if (a == 0 || b == 0)
        return;

    n = kNLinks(a);

    for (int32_t i = 0; i < n; i++) {
        if (kLinks(a)[i].to == b && (uint16_t)kLinks(a)[i].from == cell)
            return;
    }

    links = (AM2_RegionLink *)am2_realloc(kLinks(a),
                                          (size_t)(n + 1)
                                          * sizeof(AM2_RegionLink));
    kLinks(a) = links;

    n = kNLinks(a);
    links[n].to   = 0;
    links[n].from = 0;
    links[n].into = 0;

    links[n].to   = (int16_t)b;
    links[n].from = (int16_t)cell;
    links[n].into = (int16_t)neighbour;

    kNLinks(a) = (uint8_t)(n + 1);

    if (kTracePF)
        orig_log("Added Region link from %d to %d\n", a, b);
}

int region_install(void)
{
    return patch_replace(ADDR_ADD_REGION_LINK, (const void *)AddRegionLink,
                         "AddRegionLink", 2);
}
