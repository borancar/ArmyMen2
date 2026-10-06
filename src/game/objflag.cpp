/* objflag.cpp -- see objflag.h. */
#include <stdint.h>

#include "objflag.h"
#include "maprow.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

void __cdecl ObjFlagSet0(AM2_MapRow *row)
{
    row->flags |= 1u;
}

void __cdecl ObjFlagClear0(AM2_MapRow *row)
{
    row->flags &= ~1u;
}

/* Masked value, not a boolean -- see the note in objflag.h. */
uint32_t __cdecl ObjFlagBit0(const AM2_MapRow *row)
{
    return row->flags & 1u;
}

uint32_t __cdecl ObjFlagBit1(const AM2_MapRow *row)
{
    return row->flags & 2u;
}

int objflag_install(void)
{
    patch_replace(ADDR_OBJ_FLAG_SET0, (const void *)ObjFlagSet0, "ObjFlagSet0", 1);
    patch_replace(ADDR_OBJ_FLAG_CLEAR0, (const void *)ObjFlagClear0, "ObjFlagClear0", 1);
    patch_replace(ADDR_OBJ_FLAG_BIT0, (const void *)ObjFlagBit0, "ObjFlagBit0", 1);
    patch_replace(ADDR_OBJ_FLAG_BIT1, (const void *)ObjFlagBit1, "ObjFlagBit1", 1);
    return 0;
}
