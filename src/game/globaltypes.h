/* globaltypes.h -- the record types of the original's global tables.
 *
 * Shared by the layout (origstate.h, which declares the storage), by the
 * per-build name map (globals.h) and by the relink's typed externs
 * (src/vc6/include/am2_relink_os.h), so a global has one type everywhere it
 * is declared. C-compatible: plain structs, fixed-width fields. */
#ifndef AM2_GLOBALTYPES_H
#define AM2_GLOBALTYPES_H

#include <stdint.h>

/* One A* node of FindPath's tile search, ADDR_PATH_NODES[0x10000], indexed
 * by tile. The original addresses the table as an array of these --
 * `[tile*16 + base + field]` -- not through a pointer to the element. */
typedef struct AM2_PathNode {
    uint16_t g;         /* +0x00 PATHNODE_OFF_G,      cost from the start */
    uint16_t h;         /* +0x02 PATHNODE_OFF_H,      ApproxDistXY * 1.5 */
    uint16_t depth;     /* +0x04 PATHNODE_OFF_DEPTH,  hops from the start */
    uint16_t stamp;     /* +0x06 PATHNODE_OFF_STAMP,  the search generation */
    uint16_t parent;    /* +0x08 PATHNODE_OFF_PARENT, the came-from tile */
    uint16_t prev;      /* +0x0A PATHNODE_OFF_PREV,   open-list links, by g+h */
    uint16_t next;      /* +0x0C PATHNODE_OFF_NEXT */
    uint8_t  state;     /* +0x0E PATHNODE_OFF_STATE,  1 open, 2 closed */
    uint8_t  pad;       /* +0x0F */
} AM2_PathNode;

#endif /* AM2_GLOBALTYPES_H */
