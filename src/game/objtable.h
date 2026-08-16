#ifndef AM2_OBJTABLE_H
#define AM2_OBJTABLE_H

#include <stdint.h>
#include "../inject/orig.h"

/* One registry entry: 12 bytes, sorted ascending by uid. */
typedef struct {
    uint32_t uid;
    void    *obj;
    uint32_t serial;
} AM2_ObjEntry;

/* A registered object, so far as AddToItemList needs to know it. Only the
 * fields it touches are named; the real structure is larger. */
typedef struct {
    uint32_t type;      /* +0x00  switch selector, 0..8 */
    uint32_t uid;       /* +0x04  written back on registration */
    uint8_t  pad[8];    /* +0x08 */
    int8_t   owner;     /* +0x10  read with movsx */
} AM2_Object;

/* Live globals inside the running image. */
#define g_objTable (*(AM2_ObjEntry **)(uintptr_t)ADDR_OBJ_TABLE)
#define g_objCount (*(int32_t *)(uintptr_t)ADDR_OBJ_COUNT)
#define g_objCap   (*(int32_t *)(uintptr_t)ADDR_OBJ_CAPACITY)

#define g_uidCounter  ((uint32_t *)(uintptr_t)ADDR_UID_COUNTERS)   /* [8] */
#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_debugItemList (*(int32_t *)(uintptr_t)ADDR_DEBUG_ITEMLIST)

/* UID layout: three owner bits above a 29-bit per-owner counter. */
#define AM2_UID_OWNER_SHIFT 29
#define AM2_UID_COUNTER_MASK 0x1FFFFFFFu
#define AM2_UID_COUNTER_MIN  0x3E8u        /* 1000; also the wrap value */
#define AM2_UID_COUNTER_MAX  0x1FFFFC18u   /* 2^29 - 1000 */

/* Original: 0x004277A0. Binary search by uid. Returns the index, or -1 with
 * the insertion position written to *insert_at. */
int32_t __cdecl FindSlot(uint32_t uid, int32_t *insert_at);

/* Original: 0x00427820. Returns the registered object for `uid`, or NULL. */
void *__cdecl LookupByUID(uint32_t uid);

/* Original: 0x00429740. Registers `obj`, allocating a UID when `uid` is 0.
 * Returns the UID it registered under. */
uint32_t __cdecl AddToItemList(AM2_Object *obj, uint32_t uid);

int objtable_install(void);

#endif /* AM2_OBJTABLE_H */
