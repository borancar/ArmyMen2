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

/* Live globals inside the running image. */
#define g_objTable (*(AM2_ObjEntry **)(uintptr_t)ADDR_OBJ_TABLE)
#define g_objCount (*(int32_t *)(uintptr_t)ADDR_OBJ_COUNT)
#define g_objCap   (*(int32_t *)(uintptr_t)ADDR_OBJ_CAPACITY)

/* Original: 0x004277A0. Binary search by uid. Returns the index, or -1 with
 * the insertion position written to *insert_at. */
int32_t __cdecl FindSlot(uint32_t uid, int32_t *insert_at);

/* Original: 0x00427820. Returns the registered object for `uid`, or NULL. */
void *__cdecl LookupByUID(uint32_t uid);

int objtable_install(void);

#endif /* AM2_OBJTABLE_H */
