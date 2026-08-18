/* msgslot.cpp -- see msgslot.h. */
#include <stdint.h>

#include "msgslot.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* The null check comes first in every one of the six, before the division, so
 * a null comm object costs nothing and never divides. */
static void SetSlot(void *comm, uint32_t seq, uint32_t off, uint32_t value)
{
    if (!comm)
        return;
    ((uint32_t *)((uint8_t *)comm + off))[seq % AM2_MSGSLOT_COUNT] = value;
}

void __cdecl MsgSlotA0(void *comm, uint32_t seq) { SetSlot(comm, seq, AM2_MSGSLOT_A_OFF, 0); }
void __cdecl MsgSlotA1(void *comm, uint32_t seq) { SetSlot(comm, seq, AM2_MSGSLOT_A_OFF, 1); }
void __cdecl MsgSlotA2(void *comm, uint32_t seq) { SetSlot(comm, seq, AM2_MSGSLOT_A_OFF, 2); }
void __cdecl MsgSlotB0(void *comm, uint32_t seq) { SetSlot(comm, seq, AM2_MSGSLOT_B_OFF, 0); }
void __cdecl MsgSlotB1(void *comm, uint32_t seq) { SetSlot(comm, seq, AM2_MSGSLOT_B_OFF, 1); }
void __cdecl MsgSlotB2(void *comm, uint32_t seq) { SetSlot(comm, seq, AM2_MSGSLOT_B_OFF, 2); }

uint32_t __cdecl MsgField12(const void *msg)
{
    return *(const uint32_t *)((const uint8_t *)msg + 0x0C);
}

/* cdq/and 0x1F/add/sar is the compiler's signed divide by 32, not a shift. */
int32_t __cdecl CommMean32(const void *comm)
{
    const int32_t *p = (const int32_t *)((const uint8_t *)comm + 0x3A0);
    int32_t        sum = 0;
    int32_t        i;

    for (i = 0; i < 32; i++)
        sum += p[i];

    return (sum + (sum < 0 ? 31 : 0)) >> 5;
}

int msgslot_install(void)
{
    patch_replace(ADDR_MSGSLOT_A0, (const void *)MsgSlotA0, "MsgSlotA0", 2);
    patch_replace(ADDR_MSGSLOT_A1, (const void *)MsgSlotA1, "MsgSlotA1", 2);
    patch_replace(ADDR_MSGSLOT_A2, (const void *)MsgSlotA2, "MsgSlotA2", 2);
    patch_replace(ADDR_MSGSLOT_B0, (const void *)MsgSlotB0, "MsgSlotB0", 2);
    patch_replace(ADDR_MSGSLOT_B1, (const void *)MsgSlotB1, "MsgSlotB1", 2);
    patch_replace(ADDR_MSGSLOT_B2, (const void *)MsgSlotB2, "MsgSlotB2", 2);
    patch_replace(ADDR_MSG_FIELD_12, (const void *)MsgField12, "MsgField12", 1);
    patch_replace(ADDR_COMM_MEAN_32, (const void *)CommMean32, "CommMean32", 1);
    return 0;
}
