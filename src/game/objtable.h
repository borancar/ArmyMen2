#ifndef AM2_OBJTABLE_H
#define AM2_OBJTABLE_H

#include <stdint.h>
#include "../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* One registry entry: 12 bytes, sorted ascending by uid.
 *
 * `stamp` is not a serial number, despite looking like one on first reading.
 * It is the iteration marker: FirstItem bumps a global stamp, NextItem skips
 * entries already carrying it and marks each one it returns. That is what makes
 * a walk safe while objects are being added, since an insert memmoves the tail
 * and invalidates any index the caller was holding.
 */
typedef struct {
    uint32_t uid;
    void    *obj;
    uint32_t stamp;
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

#define g_iterCursor (*(int32_t *)(uintptr_t)ADDR_ITER_CURSOR)
#define g_iterStamp  (*(uint32_t *)(uintptr_t)ADDR_ITER_STAMP)

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

/* 0x0044BA60, 14 callers. A cdecl wrapper for the line above and nothing else,
 * passing its one argument straight through. Reconstructed as the wrapper it
 * is: the callers reach it, not the thing it forwards to. */
void *__cdecl ObjByUidAlias(uint32_t uid);

/* Original: 0x00429740. Registers `obj`, allocating a UID when `uid` is 0.
 * Returns the UID it registered under. */
uint32_t __cdecl AddToItemList(AM2_Object *obj, uint32_t uid);

/* Original: 0x00428590. Unregisters `obj` by its uid. 1 if removed, 0 if it
 * was not in the table. */
int32_t __cdecl RemoveFromItemList(AM2_Object *obj);

/* Originals: 0x00427850 and 0x00427880. Walk every registered object.
 * Names are ours. Returns NULL at the end of the walk. */
void *__cdecl FirstItem(void);
void *__cdecl NextItem(void);

/* 0x00427CE0. Add an object's uid to the selected group, unless it is already
 * there or the group is over its cap, and mark the object. */
void __cdecl SelectUnit(void *obj);

/* Original: 0x00427C80, four callers. The counterpart of SelectUnit: drop the
 * object from the selection list and clear OBJ_FLAG_SELECTED. */
void __cdecl DeselectUnit(void *obj);

int objtable_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_OBJTABLE_H */
