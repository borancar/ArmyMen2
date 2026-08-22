/* msgslot.cpp -- see msgslot.h. */
#include <stdint.h>

#include "msgslot.h"
#include "rect.h"   /* AM2_Rect, for the dialog paint slot */
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

/* Fields of the comm object these two read. Named for position: what +0x3E4
 * and +0x418 actually hold is not established, only which one gates the send
 * and which the log. */
#define AM2_COMM_CONNECTED   0x3E4
#define AM2_COMM_LOG_ENABLED 0x418
#define AM2_COMM_SELF_ID     0x3CC
#define AM2_MSG_VALUE        8

typedef int32_t (__cdecl *am2_send_game_msg_fn)(void *msg, int32_t a, int32_t b);
#define orig_send_game_msg (*(am2_send_game_msg_fn)ADDR_SEND_GAME_MSG)

#define kCommObj (*(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT)

static void SendValueMsg(uintptr_t record, int32_t value, const char *fmt)
{
    const uint8_t *comm = kCommObj;

    *(int32_t *)(record + AM2_MSG_VALUE) = value;

    if (!*(const int32_t *)(comm + AM2_COMM_CONNECTED))
        return;

    orig_send_game_msg((void *)record, 0, 1);

    /* Re-read: the original does not reuse the register it had. */
    comm = kCommObj;
    if (!*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        return;

    am2_log(fmt, *(const int32_t *)(comm + AM2_COMM_SELF_ID), value);
}

void __cdecl SendColorMsg(int32_t colour)
{
    SendValueMsg((uintptr_t)ADDR_MSG_COLOR, colour,
                 "SendColorMsg from %x , Color =%d \n");
}

void __cdecl SendTeamMsg(int32_t team)
{
    SendValueMsg((uintptr_t)ADDR_MSG_TEAM, team,
                 "SendTeamMsg from %x , Team =%d \n");
}

/* ------------------------------------------------ message list ---- */

/* Reached through the GAME's import table rather than by importing the symbols
 * into am2hook.dll. Two reasons: this module is on the flat side of the split
 * and must name no Win32 type, and going through the game's own slot is what
 * device.cpp already does for the DirectX thunks. The handle is an opaque
 * pointer here, which is all these three need it to be. */
typedef uint32_t (__attribute__((stdcall)) *AM2_WaitFn)(void *h, uint32_t ms);
typedef int32_t (__attribute__((stdcall)) *AM2_ReleaseMutexFn)(void *h);
typedef int32_t (__attribute__((stdcall)) *AM2_PostMessageFn)(void *hwnd,
                                                              uint32_t msg,
                                                              uint32_t wp,
                                                              int32_t lp);
#define orig_wait_for_object \
    (**(AM2_WaitFn *)(uintptr_t)IAT_WAIT_FOR_SINGLE_OBJECT)
#define orig_release_mutex \
    (**(AM2_ReleaseMutexFn *)(uintptr_t)IAT_RELEASE_MUTEX)
#define orig_post_message \
    (**(AM2_PostMessageFn *)(uintptr_t)IAT_POST_MESSAGE_A)

/* 0x00401050. Append a node to the tail of a mutex-guarded list.
 *
 * The list is {mutex, head, tail, count} and a node is {prev, next}. Twelve
 * callers and multi-threaded, which is why every field write here sits between
 * the wait and the release exactly as the original places them -- CLAUDE.md
 * warns that a mistake in this cluster is a race rather than a crash, and a
 * race is precisely what no A/B in this project could see.
 *
 * The size complaint fires above 400 OR below zero -- the original tests the
 * sign separately, so a count that has wrapped negative is caught -- and it
 * does NOT stop the append. It is a diagnostic, and with the logger stubbed to
 * `ret` in this build it is not even that. Reproduced.
 *
 * Note the complaint is issued while the mutex is still held. */
void __cdecl MsgListAdd(void *list, void *node)
{
    uint8_t *l = (uint8_t *)list;
    uint8_t *n = (uint8_t *)node;
    uint8_t *tail;
    int32_t  count;

    orig_wait_for_object(*(void **)(l + MSGLIST_OFF_MUTEX), 0xFFFFFFFFu);

    *(void **)(n + MSGNODE_OFF_NEXT) = (void *)0;

    tail = *(uint8_t **)(l + MSGLIST_OFF_TAIL);
    *(void **)(n + MSGNODE_OFF_PREV) = tail;

    if (tail == (uint8_t *)0)
        *(void **)(l + MSGLIST_OFF_HEAD) = n;
    else
        *(void **)(tail + MSGNODE_OFF_NEXT) = n;

    *(void **)(l + MSGLIST_OFF_TAIL) = n;

    count = *(const int32_t *)(l + MSGLIST_OFF_COUNT) + 1;
    *(int32_t *)(l + MSGLIST_OFF_COUNT) = count;

    if (count < 0 || count > AM2_MSGLIST_SANE_MAX)
        orig_log("AddMsg: Impossible List Size %d \n", count);

    orig_release_mutex(*(void **)(l + MSGLIST_OFF_MUTEX));
}

/* 0x00402720. Ask the window to close, and say so.
 *
 * The flag is raised BEFORE the log line, so a log that shows the message also
 * shows the flag was already set -- the ordering is reproduced for that
 * reason rather than because anything reads it in between.
 *
 * The message goes through the game's own PostMessageA slot, so it lands in
 * the same queue WndProc reads. */
/* The two widget slots this reaches. Declared locally rather than by including
 * win32/widget.h, which would pull a COM header into a flat module -- see
 * tools/checksplit.py. The slot numbers are widget.h's. */
typedef void (__attribute__((thiscall)) *AM2_DlgUpdateFn)(void *w);
typedef void (__attribute__((thiscall)) *AM2_DlgPaintFn)(void *w, AM2_Rect r);
#define AM2_DLG_SLOT_PAINT  1
#define AM2_DLG_SLOT_UPDATE 2
#define AM2_DLG_OFF_RECT    0x14

/* 0x0040F160, thiscall on the comm object: which slot holds this DirectPlay
 * id. Still original. */
typedef int32_t (__attribute__((thiscall)) *AM2_SlotOfIdFn)(void *comm,
                                                            int32_t dpid);
#define orig_comm_slot_of_id \
    ((AM2_SlotOfIdFn)(uintptr_t)ADDR_COMM_SLOT_OF_ID)
typedef void (__cdecl *AM2_SendPlayersFn)(int32_t a);
#define orig_comm_send_players \
    (*(AM2_SendPlayersFn)(uintptr_t)ADDR_COMM_SEND_PLAYERS)

void __cdecl ReceiveGameReadyMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  value;
    int32_t  slot;
    int32_t  i;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceiveGameReadyMsg\n");

    value = *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE);
    slot  = orig_comm_slot_of_id(comm, dpid);
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY) = value;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                orig_comm_slot_of_id(comm, dpid),
                value ? "TRUE" : "FALSE");

    /* Only the host decides that setup is over. */
    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    /* The count is re-read each time round rather than hoisted. */
    for (i = 0; i < *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT); i++) {
        const uint8_t *rec = comm + (uint32_t)i * COMM_ARMY_RECORD_SIZE;

        if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
            am2_log("m_ArmyReady[%d] = %d \n", i,
                    *(const int32_t *)(rec + COMM_ARMY_OFF_READY));

        /* -1 is the only id treated as empty, though 0 is documented as one
         * too -- so a slot holding 0 must be ready. The original's test. */
        if (*(const int32_t *)(rec + AM2_PLAYER_ID) != -1
            && !*(const int32_t *)(rec + COMM_ARMY_OFF_READY))
            return;
    }

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("Sending EndSetupMessage\n");

    orig_send_game_msg((void *)(uintptr_t)ADDR_MSG_END_SETUP, 0, 1);
    orig_post_message(*(void **)(uintptr_t)ADDR_HWND, AM2_WM_SETUP_DONE, 0, 0);
}

void __cdecl ReceiveGameReadyToLoadMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  value;
    int32_t  slot;
    void    *dlg;

    /* Host only. */
    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceiveGameReadyToLoadMsg\n");

    value = *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE);
    slot  = orig_comm_slot_of_id(comm, dpid);
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY_TO_LOAD) = value;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED)) {
        /* The lookup runs a SECOND time here; the original does not reuse the
         * slot it just computed. */
        am2_log("Setting m_ArmyReadyToLoad[%d] to %s\n",
                orig_comm_slot_of_id(comm, dpid),
                value ? "TRUE" : "FALSE");
    }

    /* Repaint the lobby through the widget layer's update-then-paint pair. */
    dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;
    if (dlg) {
        ((AM2_DlgUpdateFn *)*(void **)dlg)[AM2_DLG_SLOT_UPDATE](dlg);
        dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;
        ((AM2_DlgPaintFn *)*(void **)dlg)[AM2_DLG_SLOT_PAINT](
            dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
    }

    orig_comm_send_players(0);
}

void __cdecl ReceiveEndSetupMsg(void)
{
    const uint8_t *comm = kCommObj;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceiveEndSetupMsg\n");

    orig_post_message(*(void **)(uintptr_t)ADDR_HWND, AM2_WM_SETUP_DONE, 0, 0);
}

void *__cdecl MsgListRemHead(void *list)
{
    uint8_t *l = (uint8_t *)list;
    uint8_t *node;

    orig_wait_for_object(*(void **)(l + MSGLIST_OFF_MUTEX), 0xFFFFFFFFu);

    node = *(uint8_t **)(l + MSGLIST_OFF_HEAD);
    if (node) {
        uint8_t *next = *(uint8_t **)(node + MSGNODE_OFF_NEXT);
        int32_t  count;

        *(void **)(l + MSGLIST_OFF_HEAD) = next;
        if (!next)
            *(void **)(l + MSGLIST_OFF_TAIL) = next;   /* null */
        else
            *(void **)(next + MSGNODE_OFF_PREV) = (void *)0;

        count = *(const int32_t *)(l + MSGLIST_OFF_COUNT) - 1;
        *(int32_t *)(l + MSGLIST_OFF_COUNT) = count;
        if (count < 0 || count > AM2_MSGLIST_SANE_MAX)
            orig_log("RemHead: Impossible List Size %d \n", count);
    } else if (l == (uint8_t *)(uintptr_t)ADDR_MSG_LIST_POOL) {
        /* Only the POOL is worth complaining about: no message buffers left.
         * An empty ordinary queue is simply an idle one. */
        orig_log("Empty List! l->first = %d listsize = %d \n", node,
                 *(const int32_t *)(l + MSGLIST_OFF_COUNT));
    }

    orig_release_mutex(*(void **)(l + MSGLIST_OFF_MUTEX));
    return node;
}

void __cdecl ExitGamePostClose(void)
{
    *(int32_t *)AM2_IMAGE(ADDR_EXIT_GAME_FLAG) = 1;

    orig_log("Exit Game Posting WM_CLOSE from 3DONetwork\n");

    orig_post_message(*(void **)AM2_IMAGE(ADDR_HWND), AM2_WM_CLOSE, 0, 0);
}

int msgslot_install(void)
{
    patch_replace(ADDR_RECV_GAME_READY,
                  (const void *)ReceiveGameReadyMsg, "ReceiveGameReadyMsg", 1);
    patch_replace(ADDR_RECV_READY_TO_LOAD,
                  (const void *)ReceiveGameReadyToLoadMsg,
                  "ReceiveGameReadyToLoadMsg", 1);
    patch_replace(ADDR_RECEIVE_END_SETUP_MSG,
                  (const void *)ReceiveEndSetupMsg, "ReceiveEndSetupMsg", 1);
    patch_replace(ADDR_MSG_LIST_REM_HEAD, (const void *)MsgListRemHead,
                  "MsgListRemHead", 10);
    patch_replace(ADDR_MSG_LIST_ADD, (const void *)MsgListAdd,
                  "MsgListAdd", 12);
    patch_replace(ADDR_EXIT_GAME_POST_CLOSE, (const void *)ExitGamePostClose,
                  "ExitGamePostClose", 1);
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
    patch_replace(ADDR_SEND_COLOR_MSG, (const void *)SendColorMsg,
                  "SendColorMsg", 1);
    patch_replace(ADDR_SEND_TEAM_MSG, (const void *)SendTeamMsg,
                  "SendTeamMsg", 1);
    return 0;
}
