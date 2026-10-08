/* msgslot.cpp -- see msgslot.h. */
#include <stdint.h>
#include <string.h>

#include "msgslot.h"
#include "rect.h"   /* AM2_Rect, for the dialog paint slot */
#include "armymsg.h" /* SendGamePause */
#include "../inject/orig.h"
#include "crt.h"        /* am2_log */
#include "image.h"      /* AM2_IMAGE */
#include "../inject/patch.h"
#include "misc.h"   /* CommArmyOfSlot -- reconstructed */
#include "pmf.h"    /* pmf_addr -- installing a thiscall method by address */

/* The null check comes first in every one of the six, before the division, so
 * a null record costs nothing and never divides.
 *
 * `rec`, not `comm`: these take a PLAYER record. See msgslot.h. */
/* `inline` is load-bearing: the original has this body at every call site,
 * and VC6's /O2 (/Ob1) inlines only functions declared inline. Out of line,
 * each caller compiled to push/call/pop -- the relink found it. */
static inline void SetSlotA(AM2_Flow *rec, uint32_t seq, int32_t value)
{
    if (!rec)
        return;
    rec->ringA[seq % AM2_MSGSLOT_COUNT] = value;
}

static inline void SetSlotB(AM2_Flow *rec, uint32_t seq, int32_t value)
{
    if (!rec)
        return;
    rec->ringB[seq % AM2_MSGSLOT_COUNT] = value;
}

void __cdecl MsgSlotA0(AM2_Flow *rec, uint32_t seq) { SetSlotA(rec, seq, 0); }
void __cdecl MsgSlotA1(AM2_Flow *rec, uint32_t seq) { SetSlotA(rec, seq, 1); }
void __cdecl MsgSlotA2(AM2_Flow *rec, uint32_t seq) { SetSlotA(rec, seq, 2); }
void __cdecl MsgSlotB0(AM2_Flow *rec, uint32_t seq) { SetSlotB(rec, seq, 0); }
void __cdecl MsgSlotB1(AM2_Flow *rec, uint32_t seq) { SetSlotB(rec, seq, 1); }
void __cdecl MsgSlotB2(AM2_Flow *rec, uint32_t seq) { SetSlotB(rec, seq, 2); }

uint32_t __cdecl MsgField12(const AM2_MsgList *list)
{
    return (uint32_t)list->count;
}

/* cdq/and 0x1F/add/sar is the compiler's signed divide by 32, not a shift. */
int32_t __cdecl CommMean32(const AM2_Flow *player)
{
    int32_t        sum = 0;
    int32_t        i;
    const int32_t *p = player->latency;

    for (i = 0; i < 32; i++)
        sum += p[i];

    return sum / 32;
}

/* 0x00402EC0. The same mean, by player ID rather than by pointer, and 0 for a
 * player nobody knows. The original has CommMean32's loop written out again
 * here, with the sum zeroed before the lookup. */
int32_t __cdecl PlayerLatency(uint32_t id)
{
    int32_t        sum = 0;
    int32_t        i;
    const AM2_Flow *player = FindPlayerById(id);
    const int32_t *p;

    if (!player)
        return 0;
    p = player->latency;
    for (i = 0; i < 32; i++)
        sum += p[i];
    return sum / 32;
}

#define AM2_RING32_COUNT  32

void __cdecl RingPush32(AM2_Flow *player, uint32_t value)
{
    int32_t  *index = &player->latencyAt;
    int32_t  *ring  = player->latency;

    ring[*index] = (int32_t)value;
    *index += 1;
    if (*index >= AM2_RING32_COUNT)
        *index = 0;
}

/* The record array and the four tallies the removal keeps. */
#define AM2_KEYED_TALLY_MAX 3

void __cdecl CommRemoveKeyed(AM2_Flow *player, uint32_t key)
{
    /* The search holds the count; the move re-reads it; the records shift
     * down as whole-struct copies. */
#define RECS  (player->nacks)
#define COUNT (player->nackCount)
    int32_t  n = COUNT;
    int32_t  i, j;
    uint32_t kind;

    /* Two tests of the same count: `je` on zero here, then the loop's own
     * entry test, `jle`. Only a negative count tells them apart. */
    if (n == 0)
        return;

    for (i = 0; i < n; i++) {
        if (RECS[i].seq == key) {
            j    = i + 1;
            kind = RECS[i].count;
            if (kind > AM2_KEYED_TALLY_MAX)
                kind = AM2_KEYED_TALLY_MAX;
            player->nackTally[kind] += 1;

            for (; j < COUNT; j++) {
                AM2_NackRec *src = &RECS[j];

                src[-1] = *src;
            }
            COUNT -= 1;
            return;
        }
    }
#undef RECS
#undef COUNT
}

/* The record these read is reached through the image's own lookup, which is
 * not reconstructed. See ADDR_FIND_PLAYER_BY_ID for why it keeps that name
 * when these two messages call the record a Flowq. */
typedef void *(__cdecl *am2_find_player_fn)(uint32_t id);

#define AM2_FLOW_PLAYER_MASK  0x14
#define AM2_FLOW_RESEND_MASK  0x18

uint32_t __cdecl GetPlayerMask(uint32_t id)
{
    AM2_Flow *q = FindPlayerById(id);

    if (q)
        return q->ownBit;
    am2_log("ERROR: GetPlayerMask: No Flowq for %X\n", id);
    return 0;
}

uint32_t __cdecl GetReSendMask(uint32_t id)
{
    AM2_Flow *q = FindPlayerById(id);

    if (q)
        return q->wantBit;
    am2_log("ERROR: GetReSendMask: No Flowq for %X\n", id);
    return 0;
}

void __attribute__((thiscall)) AM2_Comm::CommSetSlotRemote(int32_t slot)
{
    this->slots[(uint32_t)slot].remote = 1;
}

void __attribute__((thiscall)) AM2_Comm::CommClearSlotRemote(int32_t slot)
{
    this->slots[(uint32_t)slot].remote = 0;
}

int32_t __attribute__((thiscall)) AM2_Comm::CommSlotRemote(int16_t slot)
{
    const AM2_CommSlot *rec = &this->slots[(uint32_t)(int32_t)slot];
    int32_t        id  = (int32_t)this->slots[(uint32_t)(int32_t)slot].id;

    /* 0 and -1 are both "nobody", which is the convention CLAUDE.md records
     * for this field and which ReceiveGameReadyMsg's loop disagrees with. */
    if (id != 0 && id != -1)
        return rec->remote != 0;

    /* An empty slot that once held someone answers a different question. */
    if (rec->wasHere)
        return this->isHost == 0;

    return -1;
}


int32_t __attribute__((thiscall)) AM2_Comm::CommMustBroadcast(int16_t army)
{
    /* Nobody to tell. */
    if (!g_mpSession)
        return 0;

    /* `== 0`, as the original's neg/sbb/inc spells it -- not a logical not. */
    if (army != 4)
        return this->CommSlotRemote(army) == 0;

    /* The neutral army is the host's to speak for. */
    return this->isHost != 0;
}

/* NOT AM2_PLAYER_STRIDE -- orig.h already has that name on 0x70, the comm
 * object's own player slots. These are the flow records, a different array. */
#define AM2_FLOW_RECORDS 6
#define AM2_FLOW_STRIDE  0x7E0u

AM2_Flow *__cdecl FindPlayerById(uint32_t id)
{
    void    *found = 0;
    int32_t  rec = (int32_t)((uintptr_t)g_playerRecords);
    int32_t  end = rec + AM2_FLOW_RECORDS * AM2_FLOW_STRIDE;

    /* No break: every record is looked at and the last match is the answer.
     * A pointer-bound walk, not an index -- the original compares the cursor
     * against the end address (SIGNED, jl) rather than carrying a counter. */
    for (; rec < end; rec += AM2_FLOW_STRIDE)
        if (*(const uint32_t *)(uintptr_t)rec == id)
            found = (void *)(uintptr_t)rec;
    return (AM2_Flow *)found;
}

/* SendChatMsg lives here rather than beside SendColorMsg and SendTeamMsg,
 * which it otherwise belongs with. misc.cpp's Announce calls it and misc.cpp
 * is in the offline test's link; commmsg.cpp is not, and adding it drags in
 * two dozen symbols from modules that are not. The declaration is beside its
 * siblings in msgslot.h either way. */
/* SendGameMsg is reconstructed and lives in win32/dplay.cpp, which this flat
 * module may not include -- dplay.h names DirectPlay types and
 * tools/checksplit.py refuses a flat module that reaches a Win32 header even
 * transitively. Its own signature names nothing platform, so a declaration
 * here is enough; `extern "C"` because that is how dplay.h declares it. */
extern "C" int32_t __cdecl SendGameMsg(void *msg, int32_t to, int32_t flags);

#define kMsgCommObj  (g_commObject)

/* 0x00411E90, and the one message in this family that is not a value.
 *
 * It TRUNCATES IN THE CALLER'S BUFFER. If the text is longer than 255 bytes
 * it writes a terminator at [0xFE] -- not at [0xFF], and not into a copy --
 * so a caller that passes a buffer it still wants intact loses the tail. The
 * argument is `char *` here rather than `const char *` for exactly that
 * reason, and no caller in the image passes a literal.
 *
 * The sender byte is 4 for a system announcement and otherwise the ARMY of
 * our own slot, which is a colour index for the chat window rather than a
 * player number. Note there is no connected check: unlike the colour and team
 * senders above, this one always hands the record on.
 *
 * `system` is only ever tested for zero, so the 4 is not a flag value. */
void __cdecl SendChatMsg(char *text, int32_t system)
{
    AM2_ChatMsg *record = g_msgChat;
    uint8_t  sender;

    if (strlen(text) > 0xFF)
        text[0xFE] = '\0';

    strcpy(record->text, text);

    if (system)
        sender = 4;
    else
        sender = (uint8_t)((AM2_Comm *)kMsgCommObj)->CommArmyOfSlot(
                     (int32_t)g_defaultOwner);

    record->ink = sender;
    SendGameMsg(record, 0, 0);
}

int msgslot_install(void)
{
    patch_replace(ADDR_FIND_PLAYER_BY_ID, (const void *)FindPlayerById,
                  "FindPlayerById", 1);
    patch_replace(ADDR_COMM_SET_REMOTE, pmf_addr(&AM2_Comm::CommSetSlotRemote),
                  "CommSetSlotRemote", 4);
    patch_replace(ADDR_COMM_CLEAR_REMOTE, pmf_addr(&AM2_Comm::CommClearSlotRemote),
                  "CommClearSlotRemote", 2);
    patch_replace(ADDR_COMM_MUST_BROADCAST, pmf_addr(&AM2_Comm::CommMustBroadcast),
                  "CommMustBroadcast", 9);
    patch_replace(ADDR_COMM_SLOT_REMOTE, pmf_addr(&AM2_Comm::CommSlotRemote),
                  "CommSlotRemote", 1);
    patch_replace(ADDR_MSGSLOT_A0, (const void *)MsgSlotA0, "MsgSlotA0", 2);
    patch_replace(ADDR_MSGSLOT_A1, (const void *)MsgSlotA1, "MsgSlotA1", 2);
    patch_replace(ADDR_MSGSLOT_A2, (const void *)MsgSlotA2, "MsgSlotA2", 2);
    patch_replace(ADDR_MSGSLOT_B0, (const void *)MsgSlotB0, "MsgSlotB0", 2);
    patch_replace(ADDR_MSGSLOT_B1, (const void *)MsgSlotB1, "MsgSlotB1", 2);
    patch_replace(ADDR_MSGSLOT_B2, (const void *)MsgSlotB2, "MsgSlotB2", 2);
    patch_replace(ADDR_MSG_FIELD_12, (const void *)MsgField12, "MsgField12", 1);
    patch_replace(ADDR_COMM_MEAN_32, (const void *)CommMean32, "CommMean32", 1);
    patch_replace(ADDR_PLAYER_LATENCY, (const void *)PlayerLatency,
                  "PlayerLatency", 1);
    patch_replace(ADDR_RING_PUSH_32, (const void *)RingPush32, "RingPush32", 2);
    patch_replace(ADDR_COMM_REMOVE_KEYED, (const void *)CommRemoveKeyed,
                  "CommRemoveKeyed", 2);
    patch_replace(ADDR_GET_PLAYER_MASK, (const void *)GetPlayerMask,
                  "GetPlayerMask", 4);
    patch_replace(ADDR_GET_RESEND_MASK, (const void *)GetReSendMask,
                  "GetReSendMask", 1);
    patch_replace(ADDR_SEND_CHAT_MSG, (const void *)SendChatMsg,
                  "SendChatMsg", 5);
    return 0;
}
