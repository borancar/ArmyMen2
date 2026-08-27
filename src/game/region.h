/* region.cpp -- the region graph.
 *
 * "Region" is the game's own word, from the log lines "Added Region link from
 * %d to %d" and "Activating Region %d". What those lines are FOR is settled by
 * the switch that gates them: -tracePF, already named ADDR_OPT_TRACE_PF. So
 * the region graph is this game's pathfinding structure, and the module is
 * named for the nodes rather than for the algorithm.
 *
 * A name of ours: the band is item.cpp..map.cpp, between two modules the image
 * does name, and it names no source file of its own.
 */
#ifndef AM2_REGION_H
#define AM2_REGION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One edge out of a region. Six bytes, and the two cells are kept as well as
 * the destination -- so a link records not just WHICH region it reaches but
 * where the crossing is. */
typedef struct {
    int16_t to;      /* +0x00, destination region id */
    int16_t from;    /* +0x02, the cell on this side */
    int16_t into;    /* +0x04, the cell on the far side */
} AM2_RegionLink;

/* 0x0042B860. Record that two cells connect their regions. Does nothing if
 * either cell has no region, or if this exact edge is already present. */
void __cdecl AddRegionLink(int32_t cell, int32_t neighbour);

/* 0x00437E00. Choose which of three rules a point gets settled under, from
 * the object asking. See region.cpp for why it lives here. */
void __cdecl SetPointRule(void *obj);

int region_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_REGION_H */
