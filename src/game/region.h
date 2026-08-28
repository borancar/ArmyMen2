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

/* 0x0042B7F0. Of all the links a region has to another, the index of the
 * middle one; -1 when there are none. */
int32_t __cdecl MiddleRegionLink(int32_t region, int32_t to);

/* 0x00437E00. Choose which of three rules a point gets settled under, from
 * the object asking. See region.cpp for why it lives here. */
void __cdecl SetPointRule(void *obj);

/* 0x00406460 and 0x004066B0. Hops between two regions, and whether two objects
 * are in the same region or neighbouring ones. See region.cpp. */
int32_t __cdecl RegionHops(int32_t from, int32_t to, int32_t solve);
int32_t __cdecl RegionsNear(const void *a, const void *b, int32_t solve);

/* 0x0042BC70 and 0x0042BCB0. The script's `activateregion` and
 * `inactivateregion`: set or clear REGION_OFF_ACTIVE, and bump the routing
 * cache's generation stamp when -- and only when -- the flag changed. */
void __cdecl ActivateRegion(int32_t region);
void __cdecl InactivateRegion(int32_t region);

/* 0x00439E90. Can this object reach that point in a straight line? Traces the
 * tiles between and puts each to the object's installed point rule; on success
 * records the move on the object and returns 1. */
int32_t __cdecl BeginMoveTo(void *obj, uint32_t *to);

int region_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_REGION_H */
