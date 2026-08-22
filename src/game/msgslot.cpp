/* msgslot.cpp -- see msgslot.h. */
#include <stdint.h>

#include "msgslot.h"
#include "rect.h"   /* AM2_Rect, for the dialog paint slot */
#include "armymsg.h" /* SendGamePause */
#include "../inject/orig.h"
#include "crt.h"        /* am2_log */
#include "image.h"      /* AM2_IMAGE */
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

#define AM2_RING32_INDEX  0x39C
#define AM2_RING32_ENTRY  0x3A0
#define AM2_RING32_COUNT  32

void __cdecl RingPush32(void *comm, uint32_t value)
{
    uint8_t  *base  = (uint8_t *)comm;
    int32_t  *index = (int32_t *)(base + AM2_RING32_INDEX);
    uint32_t *ring  = (uint32_t *)(base + AM2_RING32_ENTRY);

    ring[*index] = value;
    *index += 1;
    if (*index >= AM2_RING32_COUNT)
        *index = 0;
}

/* The record array and the four tallies the removal keeps. */
#define AM2_KEYED_COUNT     0xB8        /* int32_t, how many records */
#define AM2_KEYED_FIRST     0xBC        /* the records themselves */
#define AM2_KEYED_STRIDE    12
#define AM2_KEYED_KIND      8           /* third dword of a record */
#define AM2_KEYED_TALLY     0x38C       /* four int32_t, indexed by kind */
#define AM2_KEYED_TALLY_MAX 3

void __cdecl CommRemoveKeyed(void *comm, uint32_t key)
{
    uint8_t *base  = (uint8_t *)comm;
    int32_t *count = (int32_t *)(base + AM2_KEYED_COUNT);
    uint8_t *rec   = base + AM2_KEYED_FIRST;
    int32_t  i;

    /* Two tests of the same count, and the original really does make both --
     * `je` on zero and then `jle`. Only a negative count tells them apart, and
     * both answer the same way for one; reproduced rather than folded. */
    if (*count == 0)
        return;
    if (*count <= 0)
        return;

    for (i = 0; i < *count; i++, rec += AM2_KEYED_STRIDE)
        if (*(uint32_t *)rec == key)
            break;
    if (i >= *count)
        return;

    {
        uint32_t kind = *(uint32_t *)(rec + AM2_KEYED_KIND);
        int32_t  j;

        if (kind > AM2_KEYED_TALLY_MAX)
            kind = AM2_KEYED_TALLY_MAX;
        *(int32_t *)(base + AM2_KEYED_TALLY + kind * 4) += 1;

        for (j = i + 1; j < *count; j++) {
            uint8_t *src = base + AM2_KEYED_FIRST + j * AM2_KEYED_STRIDE;
            uint8_t *dst = src - AM2_KEYED_STRIDE;

            *(uint32_t *)(dst + 0) = *(uint32_t *)(src + 0);
            *(uint32_t *)(dst + 4) = *(uint32_t *)(src + 4);
            *(uint32_t *)(dst + 8) = *(uint32_t *)(src + 8);
        }
        *count -= 1;
    }
}

/* The record these read is reached through the image's own lookup, which is
 * not reconstructed. See ADDR_FIND_PLAYER_BY_ID for why it keeps that name
 * when these two messages call the record a Flowq. */
typedef void *(__cdecl *am2_find_player_fn)(uint32_t id);
#define orig_find_player (*(am2_find_player_fn)ADDR_FIND_PLAYER_BY_ID)

#define AM2_FLOW_PLAYER_MASK  0x14
#define AM2_FLOW_RESEND_MASK  0x18

uint32_t __cdecl GetPlayerMask(uint32_t id)
{
    void *q = orig_find_player(id);

    if (!q) {
        am2_log("ERROR: GetPlayerMask: No Flowq for %X\n", id);
        return 0;
    }
    return *(uint32_t *)((uint8_t *)q + AM2_FLOW_PLAYER_MASK);
}

uint32_t __cdecl GetReSendMask(uint32_t id)
{
    void *q = orig_find_player(id);

    if (!q) {
        am2_log("ERROR: GetReSendMask: No Flowq for %X\n", id);
        return 0;
    }
    return *(uint32_t *)((uint8_t *)q + AM2_FLOW_RESEND_MASK);
}

#define AM2_ARMY_STRIDE 112u

void __attribute__((thiscall)) CommSetSlotRemote(void *comm, int32_t slot)
{
    *(int32_t *)((uint8_t *)comm + (uint32_t)slot * AM2_ARMY_STRIDE
                 + COMM_ARMY_OFF_REMOTE) = 1;
}

void __attribute__((thiscall)) CommClearSlotRemote(void *comm, int32_t slot)
{
    *(int32_t *)((uint8_t *)comm + (uint32_t)slot * AM2_ARMY_STRIDE
                 + COMM_ARMY_OFF_REMOTE) = 0;
}

int32_t __attribute__((thiscall)) CommSlotRemote(void *comm, int16_t slot)
{
    const uint8_t *rec = (const uint8_t *)comm
                         + (uint32_t)(int32_t)slot * AM2_ARMY_STRIDE;
    int32_t        id  = *(const int32_t *)(rec + AM2_PLAYER_ID);

    /* 0 and -1 are both "nobody", which is the convention CLAUDE.md records
     * for this field and which ReceiveGameReadyMsg's loop disagrees with. */
    if (id != 0 && id != -1)
        return *(const int32_t *)(rec + COMM_ARMY_OFF_REMOTE) != 0;

    /* An empty slot that once held someone answers a different question. */
    if (*(const int32_t *)(rec + COMM_ARMY_OFF_WAS_HERE))
        return *(const int32_t *)((const uint8_t *)comm + COMM_OFF_IS_HOST) == 0;

    return -1;
}

int msgslot_install(void)
{
    patch_replace(ADDR_COMM_SET_REMOTE, (const void *)CommSetSlotRemote,
                  "CommSetSlotRemote", 4);
    patch_replace(ADDR_COMM_CLEAR_REMOTE, (const void *)CommClearSlotRemote,
                  "CommClearSlotRemote", 2);
    patch_replace(ADDR_COMM_SLOT_REMOTE, (const void *)CommSlotRemote,
                  "CommSlotRemote", 1);
    patch_replace(ADDR_MSGSLOT_A0, (const void *)MsgSlotA0, "MsgSlotA0", 2);
    patch_replace(ADDR_MSGSLOT_A1, (const void *)MsgSlotA1, "MsgSlotA1", 2);
    patch_replace(ADDR_MSGSLOT_A2, (const void *)MsgSlotA2, "MsgSlotA2", 2);
    patch_replace(ADDR_MSGSLOT_B0, (const void *)MsgSlotB0, "MsgSlotB0", 2);
    patch_replace(ADDR_MSGSLOT_B1, (const void *)MsgSlotB1, "MsgSlotB1", 2);
    patch_replace(ADDR_MSGSLOT_B2, (const void *)MsgSlotB2, "MsgSlotB2", 2);
    patch_replace(ADDR_MSG_FIELD_12, (const void *)MsgField12, "MsgField12", 1);
    patch_replace(ADDR_COMM_MEAN_32, (const void *)CommMean32, "CommMean32", 1);
    patch_replace(ADDR_RING_PUSH_32, (const void *)RingPush32, "RingPush32", 2);
    patch_replace(ADDR_COMM_REMOVE_KEYED, (const void *)CommRemoveKeyed,
                  "CommRemoveKeyed", 2);
    patch_replace(ADDR_GET_PLAYER_MASK, (const void *)GetPlayerMask,
                  "GetPlayerMask", 4);
    patch_replace(ADDR_GET_RESEND_MASK, (const void *)GetReSendMask,
                  "GetReSendMask", 1);
    return 0;
}
