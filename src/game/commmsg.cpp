/* commmsg.cpp -- the comm object's MESSAGES: the three settings a player can
 * change in the lobby and the six-step handshake that starts a game.
 *
 * Split out of msgslot.cpp, and the selftest link is what asked for it. That
 * module is in SELFTEST_SRC because its slot writers, its latency ring and
 * CommRemoveKeyed are pure functions with recorded vectors; these are not and
 * never can be, since every one of them reads the comm object, logs, repaints
 * a dialog or plays a sound. The two had been growing in one file, and the
 * moment ReceivedMapMsg needed PlaySoundAt -- which lives in win32/audio.cpp,
 * where the selftest deliberately does not reach -- selftest.exe stopped
 * linking.
 *
 * The line the linker drew is the right one: what can be checked offline on
 * one side, what needs the running game on the other.
 *
 * The family shape, which holds for all of them: a send half fills a record in
 * .bss and hands it to SendGameMsg; a receive half is host-only, takes
 * (msg, dpid), finds the sender's slot with CommSlotOfId and writes a field of
 * the 112-byte per-army record. The log strings name the fields -- m_ArmyReady
 * and m_ArmyReadyToLoad are the original's own spellings.
 */
#include <stdint.h>
#include <string.h>

#include "commmsg.h"
#include "msgslot.h"   /* FindPlayerById */
#include "gameproc.h"  /* RequestState -- reconstructed */
#include "item.h"      /* UidOnWire, UidArmy -- reconstructed */
#include "objtable.h"  /* FindSlot, g_objTable -- reconstructed */
#include "event.h"     /* EventMessageReceive -- reconstructed */
#include "misc.h"      /* XorChecksum, reconstructed */
#include "rect.h"       /* AM2_Rect, for the dialog paint slot */
#include "objtype.h"    /* ObjIsType4 -- reconstructed */
#include "armymsg.h"    /* SendGamePause */
#include "../inject/orig.h"
#include "crt.h"        /* am2_log */
#include "image.h"      /* AM2_IMAGE */
#include "../inject/patch.h"

/* Three comm methods, forward-declared rather than reached by address.
 *
 * They live in win32/dplay.cpp and are declared in win32/dplay.h, which this
 * module cannot include: dplay.h names DirectPlay types and commmsg.cpp is in
 * the FLAT half, where tools/checksplit.py refuses anything that reaches a
 * Win32 header even transitively. Their own signatures name nothing platform
 * -- a void * and an integer -- so a declaration here is enough, and it is
 * what script.cpp already does for PreloadSprite and for the same reason.
 *
 * Reaching them by address instead is what tools/checkseams.py exists to
 * catch, and it could not: the macros were `#define` continued over two
 * lines, and the check matched a single line.
 *
 * `extern "C"` because that is how dplay.h declares them -- the linkage has
 * to match the definition, and a C++-mangled declaration here links against
 * nothing while looking perfectly correct. */
extern "C" {
int32_t __attribute__((thiscall)) CommSlotOfId(void *comm, uint32_t id);
int32_t __attribute__((thiscall)) CommGetSessionDesc(void *comm);
int32_t __attribute__((thiscall)) CommPlayerSlot(void *comm, uint32_t id);
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

/* 0x00401240, five callers. Find the node with a given key and set or clear
 * bits in its flags, under the list's mutex. Answers the node, or null.
 *
 * THE BITS ARGUMENT IS USED BOTH WAYS FROM ONE REGISTER. `not esi` is computed
 * before the walk even begins, so the clear arm ands with a complement the
 * function has been carrying since entry. One argument, two masks, chosen by
 * the third -- and reproduced as one parameter rather than split into a set
 * and a clear, which would read better and be a different function.
 *
 * EVERY EXIT RELEASES THE MUTEX, including the two that answer null, and there
 * are four of them. That is worth counting rather than assuming: a search
 * under a lock is exactly where an early return leaks one.
 *
 * The name is structural, as the rest of this family's are. It is the only
 * member that searches, and what the key and the flags MEAN is not
 * established.
 */
void *__cdecl MsgListSetFlag(void *list, int32_t key, int32_t set,
                             uint32_t bits)
{
    uint8_t *l = (uint8_t *)list;
    uint8_t *node;

    orig_wait_for_object(*(void **)(l + MSGLIST_OFF_MUTEX), 0xFFFFFFFFu);

    for (node = *(uint8_t **)(l + MSGLIST_OFF_HEAD); node;
         node = *(uint8_t **)(node + MSGNODE_OFF_NEXT)) {
        if (*(const int32_t *)(node + PACKET_REC_OFF_KEY) != key)
            continue;

        if (set)
            *(uint32_t *)(node + PACKET_REC_OFF_FLAGS) |= bits;
        else
            *(uint32_t *)(node + PACKET_REC_OFF_FLAGS) &= ~bits;

        orig_release_mutex(*(void **)(l + MSGLIST_OFF_MUTEX));
        return node;
    }

    orig_release_mutex(*(void **)(l + MSGLIST_OFF_MUTEX));
    return (void *)0;
}

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
typedef void (__cdecl *AM2_SendPlayersFn)(int32_t a);
#define orig_comm_send_players \
    (*(AM2_SendPlayersFn)(uintptr_t)ADDR_COMM_SEND_PLAYERS)

void __cdecl SendGameReadyToLoadMsg(int32_t ready)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  slot;
    void    *dlg;

    /* Clients only -- the exact mirror of the host-only receive. */
    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("SendGameReadyToLoadMsg\n %s", ready ? "TRUE" : "FALSE");

    slot = CommSlotOfId(comm,
                                *(const int32_t *)(comm + AM2_COMM_SELF_ID));
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY_TO_LOAD) = ready;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("Setting m_ArmyReadyToLoad[%d] to %s\n",
                CommSlotOfId(comm,
                                     *(const int32_t *)(comm + AM2_COMM_SELF_ID)),
                ready ? "TRUE" : "FALSE");

    *(int32_t *)((uint8_t *)(uintptr_t)ADDR_MSG_READY_TO_LOAD + AM2_MSG_VALUE)
        = ready;
    orig_send_game_msg((void *)(uintptr_t)ADDR_MSG_READY_TO_LOAD, 0, 1);

    /* No null test on the dialog, unlike the host half. The original's. */
    dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;
    ((AM2_DlgUpdateFn *)*(void **)dlg)[AM2_DLG_SLOT_UPDATE](dlg);
    dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;
    ((AM2_DlgPaintFn *)*(void **)dlg)[AM2_DLG_SLOT_PAINT](
        dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
}

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header for the reason script.cpp declares PreloadSprite:
 * this module is on the flat side of the split and audio.h names Win32 types.
 * The signature is audio.h's, spelled the same way so the two cannot drift. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int32_t x, int32_t y);

/* The pause trio is reconstructed, in win32/frame.cpp. Declared here rather
 * than by including that header for the same reason PlaySoundAt is: this
 * module is flat and frame.h names Win32 types. Spelled exactly as frame.h
 * spells them so the two cannot drift. */
extern "C" uint32_t __cdecl GetPauseFlags(void);
extern "C" void     __cdecl PauseGame(uint32_t bits);
extern "C" void     __cdecl UnPauseGame(uint32_t bits);

typedef int32_t (__attribute__((thiscall)) *AM2_SetColourFn)(void *comm,
                                                             int32_t slot,
                                                             int32_t colour);

int32_t __cdecl SendMapMsg(int32_t result, int32_t unused)
{
    const uint8_t *comm = kCommObj;
    int32_t        rc   = 0;

    (void)unused;   /* pushed by every call site, read by none. */
    *(int32_t *)((uint8_t *)(uintptr_t)ADDR_MSG_MAP + AM2_MSG_VALUE) = result;

    /* The host has nobody to tell, and says so with a 1 rather than a 0. */
    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return 1;

    if (!*(const int32_t *)(comm + AM2_COMM_CONNECTED))
        return rc;

    rc   = orig_send_game_msg((void *)(uintptr_t)ADDR_MSG_MAP, 0, 1);
    comm = kCommObj;
    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED)) {
        /* "Error = %d" prints the ARGUMENT. `rc`, which is the error, is not
         * printed at all. The original's. */
        am2_log("SendMapMsg from %x   Error = %d \n",
                *(const int32_t *)(comm + AM2_COMM_SELF_ID), result);
    }
    return rc;
}

void __cdecl ReceivedMapMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  value;
    int32_t  slot;

    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceivedMapMsg from %x  Result = %d (4 is nominal) \n",
                dpid, *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE));

    slot  = CommSlotOfId(comm, dpid);
    value = *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE);

    /* From the table at 0x00411998, in table order. 5 and 7 fall through to
     * the same nothing as anything above 8. */
    switch (value) {
    case 0:
        *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                     + COMM_ARMY_OFF_MAP_OK) = 1;
        break;
    case 1: case 2: case 3: case 4: case 6: case 8:
        *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                     + COMM_ARMY_OFF_MAP_OK) = 0;
        PlaySoundAt(3, 0, 0, 0, 0);
        break;
    default:
        break;
    }
}

/* The tail both receivers share: repaint the current dialog, then tell
 * everyone the player list. The null test is here, and the re-read of the
 * global between the two slots is the original's -- it does not reuse the
 * pointer it just called through. */
static void RepaintDialogAndSendPlayers(void)
{
    void *dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;

    if (dlg) {
        ((AM2_DlgUpdateFn *)*(void **)dlg)[AM2_DLG_SLOT_UPDATE](dlg);
        dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;
        ((AM2_DlgPaintFn *)*(void **)dlg)[AM2_DLG_SLOT_PAINT](
            dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
    }
    orig_comm_send_players(0);
}

void __cdecl ReceivedColorMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  slot;

    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceivedColorMsg from %x  Color =%d \n",
                dpid, *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE));

    slot = CommSlotOfId(comm, dpid);
    if (CommSetArmyColour(
            comm, slot,
            *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE)) == -1)
        return;

    RepaintDialogAndSendPlayers();
}

void __cdecl ReceivedTeamMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  slot;

    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceivedTeamMsg from %x  Team =%d \n",
                dpid, *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE));

    slot = CommSlotOfId(comm, dpid);
    /* Written straight in, with none of the colour half's checking. */
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_TEAM)
        = *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE);

    RepaintDialogAndSendPlayers();
}

void __cdecl CommEndSetup(void)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  i;

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

void __cdecl SendGameReadyMsg(int32_t ready)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  slot;

    /* Gated on the verbosity field like the rest of the group -- unlike
     * SendGameStartMsg, whose opening line is not. */
    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("SendGameReadyMsg\n %s", ready ? "TRUE" : "FALSE");

    /* Our own slot: the id comes from the comm object, where the receive half
     * takes it from the message. */
    slot = CommSlotOfId(comm,
                                *(const int32_t *)(comm + AM2_COMM_SELF_ID));
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY) = ready;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED)) {
        /* The lookup runs a second time inside the `if`, as in both
         * ready-to-load halves. */
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                CommSlotOfId(comm,
                                     *(const int32_t *)(comm + AM2_COMM_SELF_ID)),
                ready ? "TRUE" : "FALSE");
    }

    /* The value is stored into the record BEFORE the arguments are complete --
     * the original writes it between the last push and the call. */
    *(int32_t *)((uint8_t *)(uintptr_t)ADDR_MSG_GAME_READY + AM2_MSG_VALUE)
        = ready;
    orig_send_game_msg((void *)(uintptr_t)ADDR_MSG_GAME_READY, 0, 1);

    CommEndSetup();
}

void __cdecl ReceiveGameReadyMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  value;
    int32_t  slot;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("ReceiveGameReadyMsg\n");

    value = *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE);
    slot  = CommSlotOfId(comm, dpid);
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY) = value;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                CommSlotOfId(comm, dpid),
                value ? "TRUE" : "FALSE");

    CommEndSetup();
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
    slot  = CommSlotOfId(comm, dpid);
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY_TO_LOAD) = value;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED)) {
        /* The lookup runs a SECOND time here; the original does not reuse the
         * slot it just computed. */
        am2_log("Setting m_ArmyReadyToLoad[%d] to %s\n",
                CommSlotOfId(comm, dpid),
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

typedef void *(__cdecl *AM2_FindPlayerFn)(uint32_t id);
typedef void (__cdecl *AM2_MsgHandlerFn)(void *msg, int32_t dpid);
/* Forward-declared rather than reached through win32/widget.h, which is the
   same reason script.cpp forward-declares PreloadSprite: this module is in the
   flat half and that header names Win32 types. The definition is in
   win32/widget.cpp, whose header is `extern "C"` like every other, so this
   has to be too or the two mangle differently and only the linker notices. */
extern "C" void __cdecl HudMessage(const char *text, int32_t colour);
/* MenuMessage is reconstructed below; this forward declaration is here
 * because the two callers in this file come before it. */
void __cdecl MenuMessage(const char *text, int32_t colour, int32_t indicator);


#define AM2_MSG_TYPE   0        /* the first dword: what the arm is chosen on */
#define AM2_MSG_SENDER 8        /* the chat arm reads this as a SIGNED byte */
#define AM2_MSG_TEXT   9

/* The chat arm. Which of the two message sinks it uses depends on the screen
 * that is up: the three menu screens get the menu one, everything else the
 * in-game HUD. */
static void DispatchChat(const uint8_t *msg)
{
    int32_t screen = *(const int32_t *)(uintptr_t)ADDR_MENU_MODE;
    int32_t sender = (int32_t)*(const int8_t *)(msg + AM2_MSG_SENDER);

    if (screen == 7 || screen == 9 || screen == 8) {
        /* The original loads only DL here and pushes the whole of EDX, so the
         * top three bytes of that argument are whatever was in the register.
         * The callee reads a byte; passing the byte is the same call. */
        MenuMessage((const char *)(msg + AM2_MSG_TEXT), sender, 1);
        return;
    }

    /* SIGNED, and then shifted left eight -- a negative sender would index
     * backwards out of the table. The original's, and the table is 256-byte
     * records of which only the first byte is read. */
    HudMessage((const char *)(msg + AM2_MSG_TEXT),
                     *(const uint8_t *)((uintptr_t)ADDR_CHAT_COLOUR_TABLE
                                        + (uintptr_t)(sender << 8)));
}

typedef int32_t (__attribute__((thiscall)) *AM2_GetSessionFn)(void *comm);
typedef int32_t (__cdecl *AM2_RegisterSelfFn)(uint32_t dpid);
typedef void (__cdecl *AM2_RequestStateFn)(int32_t state);

#define orig_comm_register_self \
    ((AM2_RegisterSelfFn)(uintptr_t)ADDR_COMM_REGISTER_SELF)

void __cdecl ReceiveStartGameMsg(void *msg, int32_t dpid)
{
    uint8_t *comm = (uint8_t *)kCommObj;
    int32_t  ok   = 1;
    int32_t  i;

    (void)dpid;   /* the dispatcher passes it; this one never reads it. */

    /* The host sent this; it does not receive it. */
    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED)) {
        /* "Seed is %d" pushes a literal 0 here as well as in the send half, so
         * the seed this function is about is never in either log. */
        am2_log("ReceiveStartGameMsg for %d Players.  Seed is %d \n",
                *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE), 0);
    }

    /* The count is re-read every iteration, as everywhere else in this file. */
    for (i = 0; i < *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT); i++) {
        uint32_t id = *(const uint32_t *)(comm
                                          + (uint32_t)i * COMM_ARMY_RECORD_SIZE
                                          + AM2_PLAYER_ID);

        if ((int32_t)id == *(const int32_t *)(comm + AM2_COMM_SELF_ID))
            continue;

        /* 7 is a result code, not a map. The second argument is the id, which
         * SendMapMsg does not read. */
        if (!SendMapMsg(7, (int32_t)id)) {
            ok = 0;
            am2_log("DPLAY ERROR SENDING TO %x\n", id);
        }

        /* Only if this player has no queue yet. Both tests re-read the id from
         * the record rather than reusing the one above. */
        if (!FindPlayerById(id) && !orig_comm_register_self(id)) {
            ok = 0;
            am2_log("FlowQ creation Failure %x\n", id);
        }
    }

    /* Checked once, after every player -- so one failure stops the game for
     * all of them. A zero player count skips this test entirely. */
    if (!ok) {
        CommGetSessionDesc(comm);
        am2_log("Error in start\n");
        return;
    }

    /* Nothing in the image handles 0x0469. */
    orig_post_message(*(void **)(uintptr_t)ADDR_HWND, AM2_WM_START_GAME, 0, 0);
    SendGamePause(1, 0x10000);
    RequestState(2);
    *(int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE = 1;
    *(int32_t *)(uintptr_t)ADDR_NET_GAME         = 1;
    *(int32_t *)(uintptr_t)ADDR_GAME_SEED =
        *(const int32_t *)((const uint8_t *)msg + MSG_START_OFF_SEED);
}

typedef int32_t (__attribute__((thiscall)) *AM2_PlayerSlotFn)(void *comm,
                                                              int32_t dpid);

#define AM2_MSG_PAUSE  8        /* non-zero: pause. zero: resume. */
#define AM2_MSG_FLAGS  0x0C

/* One block of RemoteGamePause. Four explicit compares on the slot, exactly as
 * the original writes them -- a slot above 3 falls out with no call made and
 * the mask left alone, which is why the caller keeps it across both blocks. */
static uint32_t ApplyPauseBlock(int32_t slot, int32_t pause, uint32_t base)
{
    uint32_t bit;

    if (slot == 0)      bit = base;
    else if (slot == 1) bit = base << 1;
    else if (slot == 2) bit = base << 2;
    else if (slot == 3) bit = base << 3;
    else                return 0;

    if (pause)
        PauseGame(bit);
    else
        UnPauseGame(bit);
    return bit;
}

void __cdecl RemoteGamePause(void *msg, int32_t dpid)
{
    const uint8_t *m    = (const uint8_t *)msg;
    const uint8_t *comm = kCommObj;
    int32_t        slot = CommPlayerSlot((void *)comm, dpid);
    int32_t        pause = *(const int32_t *)(m + AM2_MSG_PAUSE);
    uint32_t       flags = *(const uint32_t *)(m + AM2_MSG_FLAGS);
    uint32_t       mask  = 0;

    /* Both blocks run if the message asks for both, and the second overwrites
     * the mask the log will print. */
    if (flags & MSG_PAUSE_FLAG_A)
        mask = ApplyPauseBlock(slot, pause, PAUSE_BIT_A_SLOT0);
    if (flags & MSG_PAUSE_FLAG_B)
        mask = ApplyPauseBlock(slot, pause, PAUSE_BIT_B_SLOT0);

    comm = kCommObj;
    if (!*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED) || !mask)
        return;

    am2_log("RemoteGamePause from %x; playerIndex== %d paused = %d "
            "pauseflags = %x (%x) (msg Pause=%x)\n",
            dpid, slot, pause, GetPauseFlags(), mask, flags);
}

typedef int32_t (__attribute__((thiscall)) *AM2_ArmyInPlayFn)(void *comm,
                                                              uint32_t uid);
typedef int32_t (__cdecl *AM2_KindFn)(uint32_t wire);
typedef int32_t (__cdecl *AM2_FilterFn)(void *msg, int32_t army);
/* The stubbed logger, reached as a plain three-argument function so the
 * message buffer can be passed where a format string goes without the
 * compiler having an opinion. It is one `ret` in this build. */
typedef void (__cdecl *AM2_RawLogFn)(const void *a, int32_t b, int32_t c);

/* 0x0042A7C0. The KIND of whatever a uid names, or 0.
 *
 * FindSlot wants somewhere to put an insertion point even when it finds the
 * entry, so the original hands it the address of its own first argument --
 * the uid it has already read into a register. The slot it writes there is
 * never looked at, and the uid is not used again. Reproduced with a local
 * rather than by clobbering the parameter, which is the same thing without
 * the trap.
 *
 * A missing slot, a null object and a kind of zero are all indistinguishable
 * in the answer. */
/* 0x0040F330, thiscall. The slot holding this DirectPlay id -- and what it
 * returns is the slot's own INDEX FIELD, not the loop counter. The two are
 * the same in every state this project has driven; they are separate fields
 * and the original reads the stored one, so this does too.
 *
 * -1 when the count is zero or nothing matches. */
int32_t __attribute__((thiscall)) CommFindPlayer(void *comm, int32_t dpid)
{
    const uint8_t *c = (const uint8_t *)comm;
    int32_t        n = *(const int32_t *)(c + COMM_OFF_PLAYER_COUNT);
    int32_t        i;

    for (i = 0; i < n; i++)
        if (*(const int32_t *)(c + (uint32_t)i * AM2_PLAYER_STRIDE
                               + AM2_PLAYER_ID) == dpid)
            return *(const int32_t *)(c + (uint32_t)i * AM2_PLAYER_STRIDE
                                      + AM2_PLAYER_INDEX);
    return -1;
}

/* 0x0040F920, thiscall. Is the army that owns this uid still playing?
 *
 * Army 4 is the neutral one and answers YES without touching the object,
 * which is the same special case CommArmyOfSlot and CommSlotForArmy both
 * carry. Everything else reads the slot's active flag. */
int32_t __attribute__((thiscall)) ArmyInPlay(void *comm, uint32_t uid)
{
    int32_t army = UidArmy((int32_t)UidOnWire(uid));

    if (army == AM2_PLAYERS_MAX)
        return 1;
    return *(const int32_t *)((const uint8_t *)comm
                              + (uint32_t)army * AM2_PLAYER_STRIDE
                              + AM2_PLAYER_ACTIVE);
}

int32_t __cdecl UidObjKind(uint32_t uid)
{
    int32_t   insert;
    int32_t   slot = FindSlot(uid, &insert);
    uint32_t *obj;

    if (slot < 0)
        return 0;
    obj = (uint32_t *)g_objTable[slot].obj;
    if (!obj)
        return 0;
    return (int32_t)obj[0];
}

typedef void *(__cdecl *AM2_TroopSubFn)(const void *at, int32_t army);
typedef void (__cdecl *AM2_PairApplyFn)(void *a, void *b, int32_t x, int32_t y);
#define orig_troop_sub_parse \
    ((AM2_TroopSubFn)(uintptr_t)ADDR_TROOP_SUB_PARSE)
#define orig_trooper_pair_apply \
    ((AM2_PairApplyFn)(uintptr_t)ADDR_TROOPER_PAIR_APPLY)

/* RecvTroopBatch -- original 0x0044CC90, one caller. Message kind 0x16.
 *
 * A BATCH, and that is why it is the only arm of the trooper dispatcher that
 * takes the army. The header's length bounds a run of variable-length
 * sub-records starting at +8; each is parsed by ADDR_TROOP_SUB_PARSE, which
 * answers the pointer past itself, and every one of them is handed the army.
 *
 * The bound is a 16-bit length ZERO-extended and added to the message's own
 * address, so a length below 8 makes the loop not run rather than run
 * backwards. Reproduced as the unsigned compare it is.
 */
void __cdecl RecvTroopBatch(void *msg, int32_t army)
{
    const uint8_t *m   = (const uint8_t *)msg;
    const uint8_t *end = m + *(const uint16_t *)m;
    const uint8_t *at  = m + sizeof(AM2_ArmyMsgHdr);

    while (at < end)
        at = (const uint8_t *)orig_troop_sub_parse(at, army);
}

/* RecvTroopPair -- original 0x0044C960, one caller. Message kind 0x18.
 *
 * Two uids and two dwords. The first uid must resolve; the second must
 * resolve AND be a type 4, which ADDR_OBJ_IS_TYPE4's own error string calls a
 * WEAPON. Then the pair and both dwords go to ADDR_TROOPER_PAIR_APPLY.
 *
 * THE TWO UIDS ARE LOOKED UP BY DIFFERENT FUNCTIONS, which is not a
 * transcription slip: the first goes through ObjByUidAlias and the second
 * through LookupByUID. Those are the same lookup one wrapper apart, so the
 * answers agree -- but the original really does call two different addresses
 * and this reproduces that rather than tidying to one.
 *
 * The two dwords are passed in the OTHER ORDER from the one they sit in:
 * +0x18 before +0x14 -- which is MSG_PAIR_OFF_BYTE before MSG_PAIR_OFF_ARG,
 * matching SendPairMsg's own (a, b, int8, int32). The sender was
 * reconstructed first and its comment said "nothing read so far says what the
 * pair means"; this confirms its field layout and its argument order, which
 * is what a receiver is for. Read off the pushes, not off the record.
 *
 * UidOnWire on both, and it is the identity -- see armymsg.cpp.
 */
void __cdecl RecvTroopPair(void *msg)
{
    const uint8_t *m = (const uint8_t *)msg;
    void          *a;
    void          *b;

    a = ObjByUidAlias(UidOnWire(*(const uint32_t *)(m + MSG_PAIR_OFF_A)));
    if (!a)
        return;

    b = LookupByUID(UidOnWire(*(const uint32_t *)(m + MSG_PAIR_OFF_B)));
    if (!b)
        return;

    if (!ObjIsType4((const AM2_Object *)b))
        return;

    orig_trooper_pair_apply(a, b,
                            *(const int32_t *)(m + MSG_PAIR_OFF_BYTE),
                            *(const int32_t *)(m + MSG_PAIR_OFF_ARG));
}

/* TroopMessageRecv -- original 0x0044C590, one caller.
 *
 * The trooper half of the army-message dispatcher, and the sibling of
 * VehicleMsgRecv below: thirteen arms over kinds 0x16..0x22 and a log line
 * for anything else. It names itself in that line -- "Unknown Troop Message
 * of type %d Received".
 *
 * SEVEN OF THE THIRTEEN ARMS ARE THAT LOG. Kinds 0x1A..0x20 have no handler,
 * which is the same shape the vehicle half has and reads the same way: two
 * families share one number space, and a kind this one refuses may well be
 * another's.
 *
 * TWO ARMS NAME THEIR MESSAGE IN THE PROGRAM'S OWN VOCABULARY, and that is
 * the find here rather than the dispatch. Kind 0x21 logs "got
 * eTROOPER_DROP_ITEM_MESSAGE" and 0x22 "got eTROOPER_SET_WEAPON_MESSAGE" --
 * the `e` prefix being the original's enum convention. So two message codes
 * now have the names their authors used, not ours. 0x22 also closes a note
 * left in orig.h months ago, which recorded AM2_MSG_TROOPER_WEAPON as
 * "handled somewhere else entirely": this is somewhere else.
 *
 * Both of those logs are gated on COMM_OFF_VERBOSE and sit BEFORE the
 * handler, so the line goes out whether or not the handler does anything.
 *
 * Only the first arm takes the army, exactly as in the vehicle half.
 *
 * VERIFIED BY READING, for the same reason as its sibling: reached only for a
 * uid whose object kind is 2, off a packet from another player.
 */
typedef void (__cdecl *AM2_TroopMsgFn)(void *msg);
typedef void (__cdecl *AM2_TroopMsgArmyFn)(void *msg, int32_t army);

#define orig_recv_trooper_fire \
    ((AM2_TroopMsgFn)(uintptr_t)ADDR_RECV_TROOPER_FIRE)
#define orig_recv_troop_19 \
    ((AM2_TroopMsgFn)(uintptr_t)ADDR_RECV_TROOP_19)
#define orig_recv_troop_drop_item \
    ((AM2_TroopMsgFn)(uintptr_t)ADDR_RECV_TROOP_DROP_ITEM)
#define orig_recv_troop_set_weapon \
    ((AM2_TroopMsgFn)(uintptr_t)ADDR_RECV_TROOP_SET_WEAPON)

void __cdecl TroopMessageRecv(void *msg, int32_t army)
{
    uint32_t kind = *(const uint16_t *)((const uint8_t *)msg + 2);

    switch (kind) {
    case AM2_MSG_TROOP_FIRST:
        RecvTroopBatch(msg, army);
        return;

    case AM2_MSG_TROOPER_FIRE:
        orig_recv_trooper_fire(msg);
        return;

    case AM2_MSG_PAIR:
        RecvTroopPair(msg);
        return;

    case 0x19:
        orig_recv_troop_19(msg);
        return;

    case AM2_MSG_TROOPER_DROP_ITEM:
        if (*(const int32_t *)(kCommObj + COMM_OFF_VERBOSE))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_GOT_DROP_ITEM));
        orig_recv_troop_drop_item(msg);
        return;

    case AM2_MSG_TROOPER_WEAPON:
        if (*(const int32_t *)(kCommObj + COMM_OFF_VERBOSE))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_GOT_SET_WEAPON));
        orig_recv_troop_set_weapon(msg);
        return;

    default:
        orig_log((const char *)AM2_IMAGE(ADDR_STR_UNKNOWN_TROOP_MSG), kind);
        return;
    }
}

/* VehicleTakeOutOccupant -- original 0x0045ADD0, one caller.
 *
 * The receive side of a unit leaving a vehicle, and the exact mirror of what
 * army.cpp's ExitAllFromVehicle does locally: find the uid in the vehicle's
 * occupant list and drop that slot, clear the unit's OBJ_OFF_RIDING, and give
 * it the vehicle's OBJ_OFF_HEIGHT_SET so it steps out at the right height.
 *
 * The three fields were all already named, from the local side, which is what
 * made this readable at all -- OBJ_OFF_RIDING's own comment says "cleared as
 * an occupant gets out" and this is the other place that does it.
 *
 * TWO THINGS RUN WHETHER THE UID WAS FOUND OR NOT. The search breaks out on a
 * match and falls through on a miss, and the clear and the height happen
 * either way -- on whatever LookupByUID answers, which is not null-checked. A
 * uid that names nothing faults. The original's, and the third of these in
 * this family; the send side has the same shape with its vehicle.
 */
void __cdecl VehicleTakeOutOccupant(uint32_t uid, void *vehicle)
{
    uint8_t *v = (uint8_t *)vehicle;
    uint8_t *unit;
    int32_t  i;

    if (!vehicle)
        return;

    for (i = 0; i < *(const int32_t *)(v + VEHICLE_OFF_PTR_LIST + 4); i++) {
        if ((*(const uint32_t *const *)(v + VEHICLE_OFF_PTR_LIST + 8))[i]
                == uid) {
            ListRemoveAt(v + VEHICLE_OFF_PTR_LIST, i);
            break;
        }
    }

    unit = (uint8_t *)LookupByUID(uid);

    *(int32_t *)(unit + OBJ_OFF_RIDING) = 0;
    ApplyObjHeight(unit, *(const int8_t *)(v + OBJ_OFF_HEIGHT_SET));
}

/* RecvVehicleExit -- original 0x0045EAA0, one caller.
 *
 * The twin of ADDR_VEHICLE_DROP_OCCUPANT, and between them the send and the
 * receive of message kind 0x25 are both ours. It names itself the same way --
 * "-->Vehicle Exit Received: Vehicle: %x, trooper: %x", gated on
 * COMM_OFF_VERBOSE -- and calls the occupant a TROOPER where the sender calls
 * it an item. Neither word is ours and they disagree; the message is the same
 * twelve bytes either way.
 *
 * UidOnWire again, and again it is the identity -- see armymsg.cpp. Every
 * call here is reproduced because the original makes it, not because anything
 * is converted.
 */
void __cdecl RecvVehicleExit(void *msg)
{
    const uint8_t *m = (const uint8_t *)msg;
    void          *vehicle;

    if (*(const int32_t *)(kCommObj + COMM_OFF_VERBOSE))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_VEH_EXIT_RECV),
                 UidOnWire(*(const uint32_t *)(m + 4)),
                 UidOnWire(*(const uint32_t *)(m + 8)));

    vehicle = ObjByUidAlias(UidOnWire(*(const uint32_t *)(m + 4)));
    if (vehicle)
        VehicleTakeOutOccupant(UidOnWire(*(const uint32_t *)(m + 8)), vehicle);
}

/* VehicleMsgRecv -- original 0x0045E590, one caller.
 *
 * The vehicle half of the army-message dispatcher: eleven arms over kinds
 * 0x1B..0x25, and a log line for anything else. It names itself in that line
 * -- "Unknown Vehicle Message of type %d Received".
 *
 * FOUR OF THE ELEVEN ARMS ARE THE UNKNOWN LOG, and that is corroboration
 * rather than a hole. Kinds 0x20..0x23 have no handler here, and orig.h
 * already records AM2_MSG_TROOPER_WEAPON as 0x22 "handled somewhere else
 * entirely" with AM2_MSG_DEATH at 0x23. Two message families share one number
 * space and this dispatcher owns only its own end of it -- so a message it
 * refuses is not necessarily a message the game refuses.
 *
 * ONLY THE FIRST ARM TAKES THE ARMY. The other six are handed the message
 * alone, which is why the second parameter looks unused at six of seven call
 * sites; it is not dead, it is used once.
 *
 * Written as a switch rather than as the jump table the compiler chose,
 * because the arms are a contiguous run of kinds and the switch says so where
 * `table[kind - 0x1B]` does not.
 *
 * The seven handlers stay original and are reached by address, which is the
 * usual shape: our code runs in the middle of a live path and the layer below
 * it can wait. Kind 0x25 is the twin of ADDR_VEHICLE_DROP_OCCUPANT, which is
 * reconstructed -- so the send and receive of one message are now one ours and
 * one theirs, and that pair is worth closing next.
 *
 * VERIFIED BY READING. Its caller only reaches it for a uid whose object kind
 * is 3, off a packet from another player, so nothing without a multiplayer
 * session can execute one line of it.
 */
typedef void (__cdecl *AM2_VehMsgFn)(void *msg);
typedef void (__cdecl *AM2_VehMsgArmyFn)(void *msg, int32_t army);

#define orig_recv_vehicle_1b ((AM2_VehMsgArmyFn)(uintptr_t)ADDR_RECV_VEHICLE_1B)
#define orig_recv_vehicle_1c ((AM2_VehMsgFn)(uintptr_t)ADDR_RECV_VEHICLE_1C)
#define orig_recv_vehicle_1d ((AM2_VehMsgFn)(uintptr_t)ADDR_RECV_VEHICLE_1D)
#define orig_recv_vehicle_1e ((AM2_VehMsgFn)(uintptr_t)ADDR_RECV_VEHICLE_1E)
#define orig_recv_vehicle_1f ((AM2_VehMsgFn)(uintptr_t)ADDR_RECV_VEHICLE_1F)
#define orig_recv_vehicle_24 ((AM2_VehMsgFn)(uintptr_t)ADDR_RECV_VEHICLE_24)

void __cdecl VehicleMsgRecv(void *msg, int32_t army)
{
    uint32_t kind = *(const uint16_t *)((const uint8_t *)msg + 2);

    switch (kind) {
    case 0x1B: orig_recv_vehicle_1b(msg, army); return;
    case 0x1C: orig_recv_vehicle_1c(msg);       return;
    case 0x1D: orig_recv_vehicle_1d(msg);       return;
    case 0x1E: orig_recv_vehicle_1e(msg);       return;
    case 0x1F: orig_recv_vehicle_1f(msg);       return;
    case 0x24: orig_recv_vehicle_24(msg);       return;
    case AM2_MSG_VEHICLE_EXIT:
        RecvVehicleExit(msg);
        return;
    default:
        orig_log((const char *)AM2_IMAGE(ADDR_STR_UNKNOWN_VEH_MSG), kind);
        return;
    }
}

#define orig_raw_log       ((AM2_RawLogFn)(uintptr_t)ADDR_LOG)

/* Spelled exactly as objtable.h spells it -- uint32_t, not int32_t -- so the
 * two stay one definition. checkglobals refused the first attempt. */
#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)

#define AM2_ARMYMSG_SIZE 0x00   /* uint16_t */
#define AM2_ARMYMSG_TYPE 0x02   /* uint16_t */
#define AM2_ARMYMSG_UID  0x04

/* The tail both game-over arms share: record who won, ask for menu 0x22 and
 * raise the pending flag by hand rather than through RequestState. */
static void RecordGameOver(int32_t winner)
{
    *(int32_t *)(uintptr_t)ADDR_GAME_WINNER   = winner;
    *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST  = AM2_MENU_REQUEST_GAME_OVER;
    *(int32_t *)(uintptr_t)ADDR_STATE_PENDING = 1;
}

void __cdecl ReceiveArmyMsg(void *msg, int32_t slot, int32_t seq)
{
    uint8_t  *m    = (uint8_t *)msg;
    uint8_t  *comm = (uint8_t *)kCommObj;
    uint32_t  uid;
    int32_t   army;
    int32_t   kind;

    (void)seq;

    if (!*(const int32_t *)(comm + COMM_OFF_MSGS_ENABLED))
        return;

    uid = *(const uint32_t *)(m + AM2_ARMYMSG_UID);
    if (!ArmyInPlay(comm, uid)) {
        am2_log("ignoring message from defunct army\n");
        return;
    }

    /* uid 0 is nobody's, so the message is attributed to whoever sent it. */
    army = uid ? (int32_t)UidArmy(UidOnWire(uid)) : slot;

    /* ADDR_LOG, which is a single `ret` here -- and it is handed the MESSAGE
     * where a format string goes. Kept because it is what the image does. */
    orig_raw_log(m, 0, army);

    if (ArmyMsgFilter(m, army))
        return;

    kind = UidObjKind(UidOnWire(uid));
    switch (kind) {
    case 1:
    case 5:
        /* Accepted and ignored, in silence. */
        return;
    case 2:
        TroopMessageRecv(m, army);
        return;
    case 3:
        VehicleMsgRecv(m, army);
        return;
    default:
        break;   /* 4, and anything the table does not cover */
    }

    switch (*(const uint16_t *)(m + AM2_ARMYMSG_TYPE)) {
    case 4:
        am2_log("Received GAME_LOST_MESSAGE\n");
        RecordGameOver(army);
        return;

    case 5:
        am2_log("Received GAME_WON_MESSAGE\n");
        /* With winning disabled, a win is recorded exactly as a loss. */
        if (!*(const int32_t *)(uintptr_t)ADDR_WIN_ENABLED) {
            RecordGameOver(army);
            return;
        }
        RecordGameOver((int32_t)g_defaultOwner == army ? 1 : army);
        return;

    case 0x20:
        EventMessageReceive((const AM2_EventMsg *)m);
        return;

    default:
        am2_log("Unknown Army Msg Item Type %d, msgtype:%d, item uid: %x; "
                "msgsize: %d\n",
                kind, *(const uint16_t *)(m + AM2_ARMYMSG_TYPE),
                UidOnWire(uid),
                *(const uint16_t *)(m + AM2_ARMYMSG_SIZE));
        return;
    }
}

void __cdecl ReceivePacket(void *packet, int32_t dpid)
{
    uint8_t  *pkt   = (uint8_t *)packet;
    uint8_t  *comm  = (uint8_t *)kCommObj;
    int32_t   slot  = CommSlotOfId(comm, dpid);
    /* The length as it arrived. The loop below decrements the field itself,
     * and the bogus-length test is against THIS, not against the remainder. */
    int32_t   total = *(const int32_t *)(pkt + PACKET_OFF_LEN);
    uint8_t  *p     = pkt + PACKET_HEADER_SIZE;

    /* Only the first five packets of a session are logged. */
    if (*(const uint32_t *)(pkt + PACKET_OFF_SEQ) < 5
        && *(const int32_t *)(kCommObj + AM2_COMM_LOG_ENABLED))
        am2_log("Get Packed  %x bytes seq %d Chksum %x \n",
                *(const int32_t *)(pkt + PACKET_OFF_LEN),
                *(const int32_t *)(pkt + PACKET_OFF_SEQ),
                *(const int32_t *)(pkt + PACKET_OFF_CHECKSUM));

    /* A bad checksum is counted and complained about, and then the packet is
     * walked anyway. */
    if (XorChecksum(pkt)) {
        am2_log("Receive Checksum Error from %x seq %d\n",
                dpid, *(const int32_t *)(pkt + PACKET_OFF_SEQ));
        comm = (uint8_t *)kCommObj;
        *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                     + COMM_ARMY_OFF_CHKSUM_ERRS) += 1;
    }

    /* UNSIGNED, as the original's `jbe` is. It matters: the bogus-length test
     * below compares against the length on ENTRY, so a part longer than what
     * REMAINS is accepted and drives this field negative -- and a negative
     * length read unsigned is enormous, so the walk carries on off the end of
     * the packet rather than stopping. Signed here would quietly fix that, and
     * fixing it is not this port's job. */
    while (*(const uint32_t *)(pkt + PACKET_OFF_LEN) > PACKET_HEADER_SIZE) {
        int32_t part = *(const uint16_t *)p;

        if (!part) {
            am2_log("Received zero Length Message\n");
            return;
        }
        if (part >= total) {
            am2_log("Received Bogus Length Message, part greater than sum\n");
            return;
        }

        ReceiveArmyMsg(p, slot, *(const int32_t *)(pkt + PACKET_OFF_SEQ));

        /* Re-read, after the handler has had the bytes. */
        part = *(const uint16_t *)p;
        *(int32_t *)(pkt + PACKET_OFF_LEN) -= part;
        p += part;
    }
}

typedef int32_t (__cdecl *AM2_MapRulesFn)(int32_t a, int32_t b, int32_t c);
#define orig_check_map_rules ((AM2_MapRulesFn)(uintptr_t)ADDR_CHECK_MAP_RULES)
typedef int32_t (__cdecl *AM2_SprintfFn)(char *out, const char *fmt, ...);
#define orig_sprintf ((AM2_SprintfFn)(uintptr_t)ADDR_GAME_SPRINTF)

#define g_ourSlot      (*(int32_t *)(uintptr_t)ADDR_OUR_SLOT)

/* Update the current dialog and repaint it, with no player list sent after --
 * which is what separates this from RepaintDialogAndSendPlayers. */
static void RepaintDialog(void)
{
    void *dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;

    if (!dlg)
        return;
    ((AM2_DlgUpdateFn *)*(void **)dlg)[AM2_DLG_SLOT_UPDATE](dlg);
    dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;
    ((AM2_DlgPaintFn *)*(void **)dlg)[AM2_DLG_SLOT_PAINT](
        dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
}

void __cdecl ReceivePlayerMsg(void *msg, int32_t dpid)
{
    const uint8_t *m    = (const uint8_t *)msg;
    uint8_t       *comm = (uint8_t *)kCommObj;
    int32_t       *slotSetting = (int32_t *)(uintptr_t)ADDR_ARMY_POINTS;
    const int32_t *settingEnd  = (const int32_t *)(uintptr_t)ADDR_SCORE_LIMIT;
    const uint8_t *rec;
    uint8_t       *ours;
    int32_t        i;

    (void)dpid;

    /* The host sends it. */
    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    /* Not gated on verbosity, alone among this function's logs. */
    am2_log("ReceivePlayerMsg for %d Players. I reckoned there were %d "
            "Players \n",
            *(const int32_t *)(m + MSG_PLAYER_COUNT),
            *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT));

    *(int32_t *)(comm + AM2_COMM_CONNECTED) =
        *(const int32_t *)(m + MSG_PLAYER_CONNECTED);

    if (*(const int32_t *)(kCommObj + AM2_COMM_LOG_ENABLED))
        am2_log("      map: %s, mapSum=%X, ruleSum=%X\n",
                (const char *)(m + MSG_PLAYER_MAP_NAME),
                *(const int32_t *)(m + MSG_PLAYER_MAP_SUM),
                *(const int32_t *)(m + MSG_PLAYER_RULE_SUM));

    comm = (uint8_t *)kCommObj;
    *(int32_t *)(comm + COMM_OFF_PLAYER_COUNT) =
        *(const int32_t *)(m + MSG_PLAYER_COUNT);
    *(int32_t *)(uintptr_t)ADDR_SCORE_LIMIT =
        *(const int32_t *)(m + MSG_PLAYER_SCORE_LIMIT);
    *(int32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS =
        *(const int32_t *)(m + MSG_PLAYER_OVER_FLAGS);
    *(int32_t *)(uintptr_t)ADDR_GAME_SETTING_22C =
        *(const int32_t *)(m + MSG_PLAYER_SETTING_22C);

    rec = m + MSG_PLAYER_RECORDS;
    for (i = 0; ; i++, rec += MSG_PLAYER_STRIDE, slotSetting++) {
        uint8_t *slotRec;
        int32_t  id = *(const int32_t *)(rec + REC_PLAYER_ID);

        comm = (uint8_t *)kCommObj;
        if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
            am2_log("                 player: %s %x %d\n",
                    (const char *)(rec + REC_PLAYER_NAME), id,
                    *(const int32_t *)(rec + REC_PLAYER_COLOUR));

        /* Done BEFORE the bound check, so a fifth record still lands here. */
        if (id == *(const int32_t *)(comm + AM2_COMM_SELF_ID)) {
            g_ourSlot      = i;
            g_defaultOwner = (uint32_t)i;
            CommClearSlotRemote(comm, i);
        } else {
            CommSetSlotRemote(comm, i);
        }

        /* Four entries, and the end is the address of the next global. This
         * BREAKS to the tail below -- it does not return, so everything after
         * the loop still runs on the fifth record. */
        if (slotSetting >= settingEnd)
            break;

        comm    = (uint8_t *)kCommObj;
        slotRec = comm + (uint32_t)i * COMM_ARMY_RECORD_SIZE;
        *(int32_t *)(slotRec + AM2_PLAYER_ID)         = id;
        *(int32_t *)(slotRec + COMM_ARMY_OFF_COLOUR)  =
            *(const int32_t *)(rec + REC_PLAYER_COLOUR);
        *(int32_t *)(slotRec + COMM_ARMY_OFF_TEAM)    =
            *(const int32_t *)(rec + REC_PLAYER_TEAM);
        *(int32_t *)(slotRec + COMM_ARMY_OFF_WAS_HERE) =
            *(const int32_t *)(rec + REC_PLAYER_WAS_HERE);
        *(int32_t *)(slotRec + COMM_ARMY_OFF_READY_TO_LOAD) =
            *(const int32_t *)(rec + REC_PLAYER_F270);
        *slotSetting = *(const int32_t *)(rec + REC_PLAYER_SETTING);

        if (id != *(const int32_t *)(comm + AM2_COMM_SELF_ID)) {
            /* The SECOND time for this record, and this one is bounded by the
             * count where the first is not. */
            if (i < *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT))
                CommSetSlotRemote(comm, i);
            comm = (uint8_t *)kCommObj;
            if (!FindPlayerById(
                    *(const uint32_t *)(slotRec + AM2_PLAYER_ID))) {
                am2_log("PlayerMsg received before DPSYS_CREATEPLAYERORGROUP. "
                        "Unusual but should work. No flowq yet",
                        *(const int32_t *)(slotRec + AM2_PLAYER_ID));
                comm = (uint8_t *)kCommObj;
            }
        }

        strcpy((char *)(comm + (uint32_t)i * COMM_ARMY_RECORD_SIZE
                        + COMM_OFF_PLAYERS),
               (const char *)(rec + REC_PLAYER_NAME));
    }

    /* Our own slot may have arrived empty; fill in the id we already know. */
    comm = (uint8_t *)kCommObj;
    ours = comm + (uint32_t)g_ourSlot * COMM_ARMY_RECORD_SIZE;
    if (*(const int32_t *)(ours + AM2_PLAYER_ID) == 0
        || *(const int32_t *)(ours + AM2_PLAYER_ID) == -1)
        *(int32_t *)(ours + AM2_PLAYER_ID) =
            *(const int32_t *)(comm + AM2_COMM_SELF_ID);

    if (*(const int32_t *)(m + MSG_PLAYER_HAS_MAP)) {
        int32_t rules;

        strcpy((char *)(uintptr_t)ADDR_MP_SCRIPT_NAME,
               (const char *)(m + MSG_PLAYER_MAP_NAME));
        strcpy((char *)(uintptr_t)ADDR_TILESET_NAME,
               (const char *)(m + MSG_PLAYER_LEVEL_NAME));
        rules = orig_check_map_rules(
                    *(const int32_t *)(m + MSG_PLAYER_RULE_ARG),
                    *(const int32_t *)(m + MSG_PLAYER_RULE_SUM),
                    *(const int32_t *)(m + MSG_PLAYER_MAP_SUM));
        SendMapMsg(rules, *(const int32_t *)(kCommObj + AM2_COMM_CONNECTED));
    }

    if (*(const int32_t *)(m + MSG_PLAYER_VERSION)
            != *(const int32_t *)(uintptr_t)ADDR_GAME_VERSION
        || *(const int32_t *)(m + MSG_PLAYER_CHECKSUM)
            != *(const int32_t *)(uintptr_t)ADDR_DATA_CHECKSUM) {
        char buf[0x30];

        /* OUR name, from ADDR_DEFAULT_OWNER -- the disagreeing client
         * announces itself rather than naming the host. */
        orig_sprintf(buf, "%s has a different version of the game.",
                     (const char *)(kCommObj
                                    + g_defaultOwner
                                      * COMM_ARMY_RECORD_SIZE
                                    + COMM_OFF_PLAYERS));
        MenuMessage(buf, 4, 0);
        SendChatMsg(buf, 1);
        SendMapMsg(5, *(const int32_t *)(kCommObj + AM2_COMM_CONNECTED));
    }

    RepaintDialog();
}

void __cdecl ReceiveFlowControlMsg(void *msg, int32_t dpid)
{
    const uint8_t *m    = (const uint8_t *)msg;
    uint8_t       *comm = (uint8_t *)kCommObj;
    uint8_t       *q;

    (void)dpid;   /* given, and never read -- alone in this family. */

    /* The host is the one that sends this. */
    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    *(int32_t *)(comm + COMM_OFF_SEND_FLAGS) =
        *(const int32_t *)(m + AM2_MSG_VALUE);

    /* OUR record, not the sender's. */
    comm = (uint8_t *)kCommObj;
    q = (uint8_t *)FindPlayerById(
            *(const uint32_t *)(comm + AM2_COMM_SELF_ID));
    if (!q)
        return;

    *(int32_t *)(q + FLOWQ_OFF_A) = *(const int32_t *)(m + 0x0C);
    *(int32_t *)(q + FLOWQ_OFF_B) = *(const int32_t *)(m + 0x10);
}

void __cdecl CommDispatchMessage(void *msg, int32_t dpid)
{
    const uint8_t *comm = kCommObj;
    uint8_t       *m    = (uint8_t *)msg;
    int32_t        type;

    if (!*(const int32_t *)(comm + COMM_OFF_MSGS_ENABLED))
        return;

    type = *(const int32_t *)(m + AM2_MSG_TYPE);

    /* From the table at 0x00410044, in TYPE order. The arms are laid out in a
     * different order entirely. */
    switch (type) {
    case 1:
        *(int32_t *)(uintptr_t)ADDR_LAST_MSG_VALUE =
            *(const int32_t *)(m + AM2_MSG_VALUE);
        *(uint32_t *)(uintptr_t)ADDR_LAST_MSG_CHECKSUM = XorChecksum(m);
        return;

    case 2:
        /* Known and ignored, in silence -- unlike the unknown arm below. */
        return;

    case 3:
        DispatchChat(m);
        return;

    case 5:
        ReceiveStartGameMsg(m, dpid);
        return;

    case 6:
        RemoteGamePause(m, dpid);
        return;

    case 7:
        ReceiveGameReadyMsg(m, dpid);
        return;

    case 8:
        /* Tested only to complain; the handler runs either way. */
        if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            am2_log("Message Error:  Host should not receive PLAYERMSG\n");
        ReceivePlayerMsg(m, dpid);
        return;

    case 9:
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            return;
        ReceivedColorMsg(m, dpid);
        return;

    case 10:
        /* The original pushes the message and the id at this call site even
         * though the handler takes neither. cdecl, so it is harmless; the
         * arguments are simply dropped. */
        ReceiveEndSetupMsg();
        return;

    case 11:
        ReceivePacket(m, dpid);
        /* The slot writer's first argument is a PLAYER record here, not the
         * comm object msgslot.h describes -- FindPlayerById returns one. */
        MsgSlotB0(FindPlayerById((uint32_t)dpid),
                  *(const uint32_t *)(m + AM2_MSG_VALUE));
        return;

    case 14:
        ReceiveFlowControlMsg(m, dpid);
        return;

    case 15:
        ReceivedMapMsg(m, dpid);
        return;

    case 17:
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            return;
        ReceivedTeamMsg(m, dpid);
        return;

    case 18:
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            return;
        ReceiveGameReadyToLoadMsg(m, dpid);
        return;

    default:
        /* 4, 12, 13, 16 and anything above 18. */
        am2_log("Unknown message type %d\n", type);
        return;
    }
}

/* +0x00 len, +0x02 kind, +0x04 uid -- AM2_ArmyMsgHdr -- then the rest. */
typedef struct {
    AM2_ArmyMsgHdr hdr;
    uint32_t       target;     /* +0x08, the trooper's local target on the wire */
    int16_t        x;          /* +0x0C */
    int16_t        y;          /* +0x0E */
    int16_t        z;          /* +0x10 */
    uint16_t       pad12;      /* +0x12, never written */
    uint32_t       shotAt;     /* +0x14, the target argument's uid on the wire */
    uint8_t        flag;       /* +0x18, the byte at +0x529 */
    uint8_t        pad19[3];
} AM2_TrooperFireMsg;

void __cdecl TrooperFireSend(void *trooper, void *target)
{
    uint8_t           *t = (uint8_t *)trooper;
    AM2_TrooperFireMsg msg;
    const uint8_t     *flow;
    int32_t            seq;

    if (*(const int32_t *)(kCommObj + COMM_OFF_DPLAY) == 0)
        return;

    flow = (const uint8_t *)FindPlayerById(
               (uint32_t)*(const int32_t *)(kCommObj + COMM_OFF_OUR_PLAYER_ID));
    seq = *(const int32_t *)(flow + FLOW_OFF_SEQUENCE);

    msg.hdr.len  = AM2_MSG_TROOPER_FIRE_LEN;
    msg.hdr.kind = AM2_MSG_TROOPER_FIRE;
    msg.hdr.uid  = UidOnWire(*(const uint32_t *)(t + 4));
    msg.target   = UidOnWire(*(const uint32_t *)(t + TROOPER_OFF_LOCAL_TARGET));
    msg.shotAt   = UidOnWire(*(const uint32_t *)((const uint8_t *)target + 4));
    msg.x        = *(const int16_t *)(t + TROOPER_OFF_POS_X);
    msg.y        = *(const int16_t *)(t + TROOPER_OFF_POS_Y);
    msg.z        = *(const int16_t *)(t + TROOPER_OFF_POS_Z);
    msg.flag     = *(t + TROOPER_OFF_FIRE_FLAG);

    /* Cleared before the send, not after. */
    *(int32_t *)(t + TROOPER_OFF_CLEAR_A) = 0;
    *(int32_t *)(t + TROOPER_OFF_CLEAR_B) = 0;

    ArmyMessageSend(&msg);

    if (*(const int32_t *)(kCommObj + AM2_COMM_VERBOSE))
        am2_log("Trooper Fire Send, trooper: %d,  face:%d, pos (%d,%d,%d), "
                "loctarg %x, globTarg %x, weap %d, seq:%d\n",
                *(const uint32_t *)(t + 4), *(t + TROOPER_OFF_FACING),
                (int32_t)msg.x, (int32_t)msg.y, (int32_t)msg.z,
                *(const uint32_t *)(t + TROOPER_OFF_LOCAL_TARGET), msg.target,
                *(const int32_t *)(t + TROOPER_OFF_WEAPON), seq);

    *(int32_t *)(t + TROOPER_OFF_LAST_SEQ) = seq;
}

/* 0x00431C30 -- put a line in the menu's message log and make the panel show
 * it. Three arguments and the third is not a flag: it picks WHICH of the
 * panel's two indicators blinks.
 *
 * The log is a string list capped at 100 lines, trimmed one at a time from
 * the OLDEST end. Above that comes the display half, and it is skipped
 * entirely in menu mode 8 -- so a message posted there is logged and not
 * shown.
 *
 * The display half reads the panel's chat widget out of the CURRENT SCREEN
 * without checking that the current screen is a panel. Reproduced: every
 * caller in the image is a panel or in-mission path, and a guard here would
 * be a different function.
 *
 * BlinkerStart and ListAdd live in win32/widget.cpp and are declared here
 * rather than included: this module is flat, and widget.h reaches
 * LPDIRECTDRAWSURFACE through the sprite in a widget. Same seam and the same
 * reason as the three comm methods above. */
extern "C" {
void __attribute__((thiscall)) BlinkerStart(void *w, uint32_t periodMs,
                                            int32_t flips);
void __attribute__((thiscall)) ListAdd(void *list, const char *name,
                                       void *value);
void __attribute__((thiscall)) ListDropOldest(void *list);
}

typedef void (__attribute__((thiscall)) *AM2_PaintSlotFn)(void *w, AM2_Rect r);
#define orig_chatbox_reflow \
            ((void (__attribute__((thiscall)) *)(void *)) \
             (uintptr_t)ADDR_CHATBOX_REFLOW)

void __cdecl MenuMessage(const char *text, int32_t colour, int32_t indicator)
{
    uint8_t *log = *(uint8_t **)(uintptr_t)ADDR_MENU_MSG_LIST;
    uint8_t *screen;
    uint8_t *chatbox;

    if (!log)
        return;

    ListAdd(log, text, (void *)(uintptr_t)((uint32_t)colour & 0xFFu));
    if (*(const int32_t *)log > AM2_MENU_MSG_MAX)
        ListDropOldest(log);

    if (*(const int32_t *)(uintptr_t)ADDR_MENU_MODE == AM2_MENU_MODE_NO_CHAT)
        return;

    screen = *(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT;
    if (!screen)
        return;

    chatbox = *(uint8_t **)(screen + MP_PANEL_OFF_CHATBOX);
    orig_chatbox_reflow(*(void **)(chatbox + CHATBOX_OFF_INNER));

    /* Re-read, as the original does: it loads the field again rather than
     * keeping the register it had across the call. */
    chatbox = *(uint8_t **)(screen + MP_PANEL_OFF_CHATBOX);
    ((AM2_PaintSlotFn *)*(void **)chatbox)[1](chatbox,
                                              *(const AM2_Rect *)(chatbox + 0x14));

    BlinkerStart(*(void **)(screen + (indicator ? MP_PANEL_OFF_BLINKER_1
                                                : MP_PANEL_OFF_BLINKER_0)),
                 AM2_BLINK_PERIOD, AM2_BLINK_FLASHES);
}

/* 0x00430120. The local half and the remote half of one announcement, and
 * the colour they agree on is 4 -- the same byte SendChatMsg stamps on a
 * system message, so the line looks the same at both ends. */
void __cdecl Announce(const char *text)
{
    MenuMessage(text, 4, 0);
    /* SendChatMsg truncates above 255 bytes in the buffer it is given, and
     * the original hands it this argument unchanged -- so Announce's own
     * `const` is a promise the callee does not keep. Nothing in the image
     * announces anything near that long. */
    SendChatMsg((char *)text, 1);
}

typedef void (__cdecl *AM2_CommSystemMsgFn)(void *msg, int32_t dpid,
                                            int32_t kind, int32_t last);
#define orig_comm_system_msg \
    ((AM2_CommSystemMsgFn)(uintptr_t)ADDR_COMM_SYSTEM_MSG)

/* 0x00402690. Take everything the receive thread has queued and hand each one
 * on, then return the node to the pool. Called from FramePre -- but only when
 * CommActive(), which is the point.
 *
 * Its own guard is AM2_COMM_OFF_ACTIVE, which is exactly the field
 * frame.cpp's CommActive() already tests before calling it, so at that call
 * site the test is redundant. Reproduced anyway: it is the function's, not the
 * caller's, and nothing says the next caller will check first.
 *
 * UNEXERCISED, and this was picked in the belief that it was hot. "Called once
 * a frame" is what FramePre looks like at a glance; the gate is on the same
 * line. Measured: MsgListRemHead, which this would call at least once per
 * frame, reads 0 over a whole Boot Camp mission, and MsgListInit reads 6 --
 * setup only. So AM2_COMM_OFF_ACTIVE is clear in single player and FramePre
 * never calls this at all. Verified by reading; the A/B compares nothing here.
 *
 * The message's +0x08 decides where it goes. Zero routes to the DirectPlay
 * SYSTEM handler with a zero in that argument's place; anything else is a game
 * message and goes to CommDispatchMessage with the field itself.
 *
 * The original pushes FOUR arguments for both and cleans them itself, but the
 * game-message half only ever reads two -- its first is a message pointer and
 * its second a dpid, which is exactly the signature that function was already
 * reconstructed with. So the extra pair is dead on that path and is not
 * reproduced: under cdecl the caller cleans, and a callee cannot observe
 * arguments it does not read. The system handler really does take four.
 *
 * The loop re-reads the head after each node rather than snapshotting the
 * list, so a handler that queues more work has it drained in the same pass. */
void __cdecl CommDrainMsgs(void)
{
    uint8_t *node;

    if (!*(const int32_t *)(kCommObj + AM2_COMM_OFF_ACTIVE))
        return;

    for (node = (uint8_t *)MsgListRemHead((void *)(uintptr_t)ADDR_MSG_LIST_B);
         node;
         node = (uint8_t *)MsgListRemHead((void *)(uintptr_t)ADDR_MSG_LIST_B)) {
        int32_t  kind = *(const int32_t *)(node + 0x08);
        int32_t  last = *(const int32_t *)(node + 0x0C);
        void    *msg  = *(void *const *)(node + 0x20);
        int32_t  dpid = *(const int32_t *)(node + 0x24);

        if (kind)
            CommDispatchMessage(msg, dpid);
        else
            orig_comm_system_msg(msg, dpid, 0, last);

        MsgListAdd((void *)(uintptr_t)ADDR_MSG_LIST_POOL, node);
    }
}

int commmsg_install(void)
{
    patch_replace(ADDR_TROOP_MESSAGE_RECV, (const void *)TroopMessageRecv,
                  "TroopMessageRecv", 1);
    patch_replace(ADDR_RECV_TROOP_16, (const void *)RecvTroopBatch,
                  "RecvTroopBatch", 1);
    patch_replace(ADDR_RECV_TROOP_PAIR, (const void *)RecvTroopPair,
                  "RecvTroopPair", 1);
    patch_replace(ADDR_VEHICLE_MSG_RECV, (const void *)VehicleMsgRecv,
                  "VehicleMsgRecv", 1);
    patch_replace(ADDR_RECV_VEHICLE_EXIT, (const void *)RecvVehicleExit,
                  "RecvVehicleExit", 1);
    patch_replace(ADDR_VEHICLE_TAKE_OUT, (const void *)VehicleTakeOutOccupant,
                  "VehicleTakeOutOccupant", 1);
    patch_replace(ADDR_COMM_FIND_PLAYER, (const void *)CommFindPlayer,
                  "CommFindPlayer", 2);
    patch_replace(ADDR_ARMY_IN_PLAY, (const void *)ArmyInPlay,
                  "ArmyInPlay", 2);
    patch_replace(ADDR_UID_OBJ_KIND, (const void *)UidObjKind,
                  "UidObjKind", 1);
    patch_replace(ADDR_TROOPER_FIRE_SEND, (const void *)TrooperFireSend,
                  "TrooperFireSend", 2);
    patch_replace(ADDR_RECV_ARMY_MSG, (const void *)ReceiveArmyMsg,
                  "ReceiveArmyMsg", 1);
    patch_replace(ADDR_RECV_PLAYER_MSG, (const void *)ReceivePlayerMsg,
                  "ReceivePlayerMsg", 1);
    patch_replace(ADDR_RECV_PACKET, (const void *)ReceivePacket,
                  "ReceivePacket", 1);
    patch_replace(ADDR_RECV_FLOW_CONTROL,
                  (const void *)ReceiveFlowControlMsg,
                  "ReceiveFlowControlMsg", 1);
    patch_replace(ADDR_RECV_GAME_PAUSE, (const void *)RemoteGamePause,
                  "RemoteGamePause", 1);
    patch_replace(ADDR_RECV_START_GAME_MSG,
                  (const void *)ReceiveStartGameMsg,
                  "ReceiveStartGameMsg", 1);
    patch_replace(ADDR_COMM_DISPATCH_MSG, (const void *)CommDispatchMessage,
                  "CommDispatchMessage", 1);
    patch_replace(ADDR_COMM_DRAIN_MSGS, (const void *)CommDrainMsgs,
                  "CommDrainMsgs", 0);
    patch_replace(ADDR_MSG_LIST_REM_HEAD, (const void *)MsgListRemHead,
                  "MsgListRemHead", 10);
    patch_replace(ADDR_MSG_LIST_SET_FLAG, (const void *)MsgListSetFlag,
                  "MsgListSetFlag", 5);
    patch_replace(ADDR_MSG_LIST_ADD, (const void *)MsgListAdd,
                  "MsgListAdd", 12);
    patch_replace(ADDR_SEND_READY_TO_LOAD,
                  (const void *)SendGameReadyToLoadMsg,
                  "SendGameReadyToLoadMsg", 1);
    patch_replace(ADDR_RECV_GAME_READY,
                  (const void *)ReceiveGameReadyMsg, "ReceiveGameReadyMsg", 1);
    patch_replace(ADDR_SEND_MAP_MSG, (const void *)SendMapMsg,
                  "SendMapMsg", 3);
    patch_replace(ADDR_RECV_MAP_MSG, (const void *)ReceivedMapMsg,
                  "ReceivedMapMsg", 1);
    patch_replace(ADDR_RECV_COLOR_MSG, (const void *)ReceivedColorMsg,
                  "ReceivedColorMsg", 1);
    patch_replace(ADDR_RECV_TEAM_MSG, (const void *)ReceivedTeamMsg,
                  "ReceivedTeamMsg", 1);
    patch_replace(ADDR_SEND_GAME_READY, (const void *)SendGameReadyMsg,
                  "SendGameReadyMsg", 1);
    patch_replace(ADDR_COMM_END_SETUP,
                  (const void *)CommEndSetup,
                  "CommEndSetup", 1);
    patch_replace(ADDR_RECV_READY_TO_LOAD,
                  (const void *)ReceiveGameReadyToLoadMsg,
                  "ReceiveGameReadyToLoadMsg", 1);
    patch_replace(ADDR_RECEIVE_END_SETUP_MSG,
                  (const void *)ReceiveEndSetupMsg, "ReceiveEndSetupMsg", 1);
    patch_replace(ADDR_EXIT_GAME_POST_CLOSE, (const void *)ExitGamePostClose,
                  "ExitGamePostClose", 1);
    patch_replace(ADDR_SEND_COLOR_MSG, (const void *)SendColorMsg,
                  "SendColorMsg", 1);
    patch_replace(ADDR_SEND_TEAM_MSG, (const void *)SendTeamMsg,
                  "SendTeamMsg", 1);
    patch_replace(ADDR_MENU_MESSAGE, (const void *)MenuMessage,
                  "MenuMessage", 6);
    patch_replace(ADDR_ANNOUNCE, (const void *)Announce, "Announce", 12);
    return 0;
}
