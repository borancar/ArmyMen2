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
#include "pmf.h"
#include "msgslot.h"   /* FindPlayerById */
#include "gameproc.h"  /* RequestState -- reconstructed */
#include "item.h"      /* UidOnWire, UidArmy -- reconstructed */
#include "objtable.h"  /* FindSlot, g_objTable -- reconstructed */
#include "event.h"     /* EventMessageReceive -- reconstructed */
#include "misc.h"      /* XorChecksum, reconstructed */
#include "rect.h"       /* AM2_Rect, for the dialog paint slot */
#include "objtype.h"    /* ObjIsType4 -- reconstructed */
#include "armymsg.h"    /* SendGamePause */
#include "army.h"      /* ListFirstField548 -- reconstructed */

/* GetTickCount through the game's own IAT slot, the same seam air.cpp uses:
 * an import of our own would resolve through our IAT, and this file is flat. */
typedef uint32_t (__stdcall *AM2_TickFn)(void);
#define orig_get_tick_count (*(AM2_TickFn *)g_iatGetTickCount)
/* SendPlayerMsg is reconstructed, in win32/dplay.cpp. Declared here rather
 * than by including that header because commmsg.cpp is on the flat side of
 * the split; its signature names no Win32 type. */
void __cdecl SendPlayerMsg(int32_t arg);

#include "../inject/orig.h"
#include "crt.h"        /* am2_log */
#include "image.h"      /* AM2_IMAGE */
#include "../inject/patch.h"

#ifdef AM2_STANDALONE
/* am2_game_version -- 0x00475894, the protocol version the multiplayer
 * handshake compares (read as a single int32). Transcribed out of the blob. */
extern "C" const int32_t am2_game_version = 1;
/* The comm-object slot at 0x004751B0 (also read as ARMY_TABLE) and the enum
 * count at 0x004751B4: a pointer to the comm object (the static instance in
 * origbss, am2_origstate.comm_global) and a count that seeds at 999. A real
 * pointer, not the number 0x004FA480 -- that only resolved while the struct
 * sat at its original VA. Plain linkage -- COMM_OBJECT is read across the
 * whole game via standalone.h. */
struct AM2_CommObjectSlot am2_comm_object = { am2_origstate.comm_global, 999 };
#endif

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
/* A fifth, added when CommSystemMessage was reconstructed: four integers and
 * a void *, so it declares here for exactly the reason the other four do. */
void __cdecl CommSystemMessage(void *msg, int32_t size, int32_t from,
                               int32_t to);
/* A fourth, and cdecl rather than thiscall -- it registers a DirectPlay id
 * into the player-record table and takes no comm object at all. */
int32_t __cdecl CommRegisterSelf(uint32_t id);
}

/* SendGameMsg is reconstructed and lives in win32/dplay.cpp, which this flat
 * module may not include -- dplay.h names DirectPlay types and
 * tools/checksplit.py refuses a flat module that reaches a Win32 header even
 * transitively. Its own signature names nothing platform, so a declaration
 * here is enough; `extern "C"` because that is how dplay.h declares it. */
extern "C" int32_t __cdecl SendGameMsg(void *msg, int32_t to, int32_t flags);


/* Fields of the comm object these two read. Named for position: what +0x3E4
 * and +0x418 actually hold is not established, only which one gates the send
 * and which the log. */
#define AM2_COMM_CONNECTED   0x3E4
#define AM2_COMM_LOG_ENABLED 0x418
#define AM2_COMM_SELF_ID     0x3CC
#define AM2_MSG_VALUE        8


#define kCommObj (g_commObject)

static inline void SendValueMsg(uintptr_t record, int32_t value, const char *fmt)
{
    const AM2_Comm *comm = kCommObj;

    *(int32_t *)(record + AM2_MSG_VALUE) = value;

    if (!comm->connected)
        return;

    SendGameMsg((void *)record, 0, 1);

    /* Re-read: the original does not reuse the register it had. */
    comm = kCommObj;
    if (!comm->verbose)
        return;

    am2_log(fmt, (int32_t)comm->selfId, value);
}

void __cdecl SendColorMsg(int32_t colour)
{
    SendValueMsg(((uintptr_t)g_msgColor), colour,
                 "SendColorMsg from %x , Color =%d \n");
}

void __cdecl SendTeamMsg(int32_t team)
{
    SendValueMsg(((uintptr_t)g_msgTeam), team,
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
AM2_MsgNode *__cdecl MsgListSetFlag(AM2_MsgList *list, int32_t key, int32_t set,
                             uint32_t bits)
{
    AM2_MsgList *l     = (AM2_MsgList *)list;
    uint32_t clear = ~bits;
    AM2_MsgNode *found;
    AM2_MsgNode *node;

    orig_wait_for_object(l->mutex, 0xFFFFFFFFu);
    found = 0;

    for (node = l->head; node;
         node = node->next) {
        if ((int32_t)node->key == key) {
            found = node;
            if (set)
                found->flags |= bits;
            else
                found->flags &= clear;

            break;
        }
    }

    orig_release_mutex(l->mutex);
    return found;
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
void __cdecl MsgListAdd(AM2_MsgList *list, AM2_MsgNode *node)
{
    AM2_MsgList *l = list;
    AM2_MsgNode *n = node;
    AM2_MsgNode *tail;
    int32_t  count;

    orig_wait_for_object(l->mutex, 0xFFFFFFFFu);

    n->next = (AM2_MsgNode *)0;

    tail = l->tail;
    n->prev = tail;

    if (tail == (AM2_MsgNode *)0)
        l->head = n;
    else
        tail->next = n;

    l->tail = n;

    count = l->count + 1;
    l->count = count;

    if (count < 0 || count > AM2_MSGLIST_SANE_MAX)
        orig_log("AddMsg: Impossible List Size %d \n", count);

    orig_release_mutex(l->mutex);
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
/* AM2_DLG_SLOT_PAINT and AM2_DLG_OFF_RECT moved to orig.h -- frame.cpp
 * needs the same two, and a second private copy is how two definitions of
 * one constant begin. */
#define AM2_DLG_SLOT_UPDATE 2

/* 0x0040F160, thiscall on the comm object: which slot holds this DirectPlay
 * id. Still original. */
typedef void (__cdecl *AM2_SendPlayersFn)(int32_t a);

void __cdecl SendGameReadyToLoadMsg(int32_t ready)
{
    /* The comm object is read at every use. */
#define COMM (kCommObj)
    void *dlg;

    /* Clients only -- the exact mirror of the host-only receive. */
    if (COMM->isHost)
        return;

    if (COMM->verbose)
        am2_log("SendGameReadyToLoadMsg\n %s", ready ? "TRUE" : "FALSE");

    COMM->slots[(uint32_t)AM2_THE_COMM->CommSlotOfId(
                            (int32_t)COMM->selfId)].readyToLoad = ready;

    if (COMM->verbose)
        am2_log("Setting m_ArmyReadyToLoad[%d] to %s\n",
                AM2_THE_COMM->CommSlotOfId(
                    (int32_t)COMM->selfId),
                ready ? "TRUE" : "FALSE");
#undef COMM

    *(int32_t *)(g_msgReadyToLoad + AM2_MSG_VALUE)
        = ready;
    SendGameMsg((void *)g_msgReadyToLoad, 0, 1);

    /* No null test on the dialog, unlike the host half. The original's. */
    dlg = g_paintObject;
    ObjVUpdate(dlg);
    dlg = g_paintObject;
    ObjVPaint(dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
}

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header for the reason script.cpp declares PreloadSprite:
 * this module is on the flat side of the split and audio.h names Win32 types.
 * The signature is audio.h's, spelled the same way so the two cannot drift. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int16_t x, int16_t y);

/* The pause trio is reconstructed, in win32/frame.cpp. Declared here rather
 * than by including that header for the same reason PlaySoundAt is: this
 * module is flat and frame.h names Win32 types. Spelled exactly as frame.h
 * spells them so the two cannot drift. */
extern "C" uint32_t __cdecl GetPauseFlags(void);
extern "C" void     __cdecl PauseGame(uint32_t bits);
extern "C" void     __cdecl UnPauseGame(uint32_t bits);


int32_t __cdecl SendMapMsg(int32_t result, int32_t unused)
{
    const AM2_Comm *comm = kCommObj;
    int32_t        rc   = 0;

    (void)unused;   /* pushed by every call site, read by none. */
    *(int32_t *)(g_msgMap + AM2_MSG_VALUE) = result;

    /* The host has nobody to tell, and says so with a 1 rather than a 0. */
    if (comm->isHost)
        return 1;

    if (comm->connected) {
        rc   = SendGameMsg((void *)g_msgMap, 0, 1);
        comm = kCommObj;
        if (comm->verbose) {
            /* "Error = %d" prints the ARGUMENT. `rc`, which is the error, is
             * not printed at all. The original's. */
            am2_log("SendMapMsg from %x   Error = %d \n",
                    (int32_t)comm->selfId, result);
        }
    }
    return rc;
}

void __cdecl ReceivedMapMsg(void *msg, int32_t dpid)
{
    /* The comm object and the message's value are read at every use. */
#define COMM  (kCommObj)
#define VALUE (*(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE))
    int32_t slot;

    if (!COMM->isHost)
        return;

    if (COMM->verbose)
        am2_log("ReceivedMapMsg from %x  Result = %d (4 is nominal) \n",
                dpid, VALUE);

    slot = AM2_THE_COMM->CommSlotOfId(dpid);

    /* From the table at 0x00411998, in table order. 5 and 7 fall through to
     * the same nothing as anything above 8. */
    switch (VALUE) {
    case 0:
        COMM->slots[(uint32_t)slot].mapOk = 1;
        break;
    case 1: case 2: case 3: case 4: case 6: case 8:
        COMM->slots[(uint32_t)slot].mapOk = 0;
        PlaySoundAt(3, 0, 0, 0, 0);
        break;
    default:
        break;
    }
#undef COMM
#undef VALUE
}

/* The tail both receivers share: repaint the current dialog, then tell
 * everyone the player list. The null test is here, and the re-read of the
 * global between the two slots is the original's -- it does not reuse the
 * pointer it just called through. */
static inline void RepaintDialogAndSendPlayers(void)
{
    void *dlg = g_paintObject;

    if (dlg) {
        ObjVUpdate(dlg);
        dlg = g_paintObject;
        ObjVPaint(dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
    }
    SendPlayerMsg(0);
}

void __cdecl ReceivedColorMsg(void *msg, int32_t dpid)
{
#define COMM (kCommObj)
    if (COMM->isHost) {
        int32_t slot;

        if (COMM->verbose)
            am2_log("ReceivedColorMsg from %x  Color =%d \n",
                    dpid, *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE));

        slot = ((AM2_Comm *)COMM)->CommSlotOfId(dpid);
        if (((AM2_Comm *)COMM)->CommSetArmyColour(slot,
                *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE)) != -1)
            RepaintDialogAndSendPlayers();
    }
#undef COMM
}

void __cdecl ReceivedTeamMsg(void *msg, int32_t dpid)
{
#define COMM (kCommObj)
    /* comm is re-read at each use; the body sits inside the host test, so
     * the registers are saved before it. */
    if (COMM->isHost) {
        int32_t slot;

        if (COMM->verbose)
            am2_log("ReceivedTeamMsg from %x  Team =%d \n",
                    dpid, *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE));

        slot = ((AM2_Comm *)COMM)->CommSlotOfId(dpid);
        /* Written straight in, with none of the colour half's checking. */
        COMM->slots[(uint32_t)slot].team
            = *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE);

        RepaintDialogAndSendPlayers();
    }
#undef COMM
}

/* The body of CommEndSetup, which its two callers in this file have written
 * out in place in the image; the function itself is also there for the
 * outside caller. */
static inline void CommEndSetupInline(void)
{
    int32_t i;

    /* The comm object is re-read from its global at every use. */
#define comm kCommObj
    /* Only the host decides that setup is over. */
    if (!comm->isHost)
        return;

    /* The count is re-read each time round rather than hoisted. */
    for (i = 0; i < comm->playerCount; i++) {
        if (comm->verbose)
            am2_log("m_ArmyReady[%d] = %d \n", i,
                    comm->slots[(uint32_t)i].ready);

        /* -1 is the only id treated as empty, though 0 is documented as one
         * too -- so a slot holding 0 must be ready. The original's test. */
        if ((int32_t)comm->slots[(uint32_t)i].id != -1
            && !comm->slots[(uint32_t)i].ready)
            return;
    }

    if (comm->verbose)
        am2_log("Sending EndSetupMessage\n");
#undef comm

    SendGameMsg(g_msgEndSetup, 0, 1);
    orig_post_message(g_enumContext, AM2_WM_SETUP_DONE, 0, 0);
}

void __cdecl CommEndSetup(void)
{
    CommEndSetupInline();
}

void __cdecl SendGameReadyMsg(int32_t ready)
{
    /* The comm object is re-read at every use, as the image does. */
#define comm (kCommObj)

    /* Gated on the verbosity field like the rest of the group -- unlike
     * SendGameStartMsg, whose opening line is not. */
    if (comm->verbose)
        am2_log("SendGameReadyMsg\n %s", ready ? "TRUE" : "FALSE");

    /* Our own slot: the id comes from the comm object, where the receive half
     * takes it from the message. */
    comm->slots[(uint32_t)((AM2_Comm *)comm)->CommSlotOfId(
                                (int32_t)comm->selfId)].ready = ready;

    if (comm->verbose) {
        /* The lookup runs a second time inside the `if`, as in both
         * ready-to-load halves. */
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                ((AM2_Comm *)comm)->CommSlotOfId(
                                     (int32_t)comm->selfId),
                ready ? "TRUE" : "FALSE");
    }

    /* The value is stored into the record BEFORE the arguments are complete --
     * the original writes it between the last push and the call. */
    *(int32_t *)(g_msgGameReady + AM2_MSG_VALUE)
        = ready;
    SendGameMsg((void *)g_msgGameReady, 0, 1);

#undef comm
    CommEndSetupInline();
}

void __cdecl ReceiveGameReadyMsg(void *msg, int32_t dpid)
{
    /* The comm object is re-read at every use, as the image does. */
#define COMM (kCommObj)
#define VALUE (*(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE))
    if (COMM->verbose)
        am2_log("ReceiveGameReadyMsg\n");

    COMM->slots[(uint32_t)((AM2_Comm *)COMM)->CommSlotOfId(dpid)].ready = VALUE;

    if (COMM->verbose)
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                ((AM2_Comm *)COMM)->CommSlotOfId(dpid),
                VALUE ? "TRUE" : "FALSE");

    CommEndSetupInline();
#undef COMM
#undef VALUE
}

void __cdecl ReceiveGameReadyToLoadMsg(void *msg, int32_t dpid)
{
    /* The comm object and the message's value are read at every use. */
#define COMM  (kCommObj)
#define VALUE (*(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE))
    void *dlg;

    /* Host only. */
    if (!COMM->isHost)
        return;

    if (COMM->verbose)
        am2_log("ReceiveGameReadyToLoadMsg\n");

    COMM->slots[(uint32_t)AM2_THE_COMM->CommSlotOfId(dpid)].readyToLoad = VALUE;

    if (COMM->verbose) {
        /* The lookup runs a SECOND time here; the original does not reuse the
         * slot it just computed. */
        am2_log("Setting m_ArmyReadyToLoad[%d] to %s\n",
                AM2_THE_COMM->CommSlotOfId(dpid),
                VALUE ? "TRUE" : "FALSE");
    }

    /* Repaint the lobby through the widget layer's update-then-paint pair. */
    dlg = g_paintObject;
    if (dlg) {
        ObjVUpdate(dlg);
        dlg = g_paintObject;
        ObjVPaint(dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
    }

    SendPlayerMsg(0);
#undef COMM
#undef VALUE
}

void __cdecl ReceiveEndSetupMsg(void *msg, int32_t from)
{
    const AM2_Comm *comm = kCommObj;

    (void)msg;
    (void)from;

    if (comm->verbose)
        am2_log("ReceiveEndSetupMsg\n");

    orig_post_message(g_enumContext, AM2_WM_SETUP_DONE, 0, 0);
}

AM2_MsgNode *__cdecl MsgListRemHead(AM2_MsgList *list)
{
    AM2_MsgList *l = list;
    AM2_MsgNode *node;

    orig_wait_for_object(l->mutex, 0xFFFFFFFFu);

    node = l->head;
    if (node) {
        AM2_MsgNode *next = node->next;
        int32_t  count;

        l->head = next;
        if (!next)
            l->tail = next;   /* null */
        else
            next->prev = (AM2_MsgNode *)0;

        count = l->count - 1;
        l->count = count;
        if (count < 0 || count > AM2_MSGLIST_SANE_MAX)
            orig_log("RemHead: Impossible List Size %d \n", count);
    } else if (l == g_msgListPool) {
        /* Only the POOL is worth complaining about: no message buffers left.
         * An empty ordinary queue is simply an idle one. */
        orig_log("Empty List! l->first = %d listsize = %d \n", node,
                 l->count);
    }

    orig_release_mutex(l->mutex);
    return node;
}

/* MsgListRemove -- original 0x00401410, three callers, and it NAMES ITSELF in
 * all three of its complaints: "RemMsg: Impossible List Size %d". Unlink a
 * node the caller already holds, wherever it sits, where MsgListRemHead
 * unlinks the head.
 *
 * The unlink is the ordinary four-case doubly-linked one and the list's TAIL
 * is maintained in both directions, which is what makes this a real removal
 * rather than the head-only shortcut next door.
 *
 * THREE COMPLAINTS AND ONE EARLY EXIT AMONG THEM, which is the part worth
 * reading twice. The size check is the same one every operation here makes.
 * Then, only if this list IS the pool, a null head is "RemMsg has set
 * freelist to EMPTY!!" -- and a pool that still HAS a head jumps past
 * everything to the unlock. The third, "Why is list %x empty?", fires for any
 * list whose head is null while its count is still positive, which is the
 * structure contradicting itself rather than a resource running out.
 *
 * So an emptied POOL gets the second complaint AND THEN THE THIRD, because
 * the original falls through rather than returning; the only thing the pool
 * check skips is a pool with buffers left. Writing the three as independent
 * ifs would be wrong in exactly one case -- and it is the common one, a pool
 * that is not empty.
 *
 * THE UNLINKED NODE'S OWN LINKS ARE LEFT ALONE, exactly as in MsgListRemHead,
 * so a caller walking a list while removing from it can still read the node's
 * `next` afterwards. Two of the three callers do.
 *
 * All three complaints are made while the mutex is still held, as in the rest
 * of the family.
 */
void __cdecl MsgListRemove(AM2_MsgList *list, AM2_MsgNode *node)
{
    AM2_MsgList *l = list;
    AM2_MsgNode *n = node;
    AM2_MsgNode *prev;
    AM2_MsgNode *next;

    orig_wait_for_object(l->mutex, 0xFFFFFFFFu);

    prev = n->prev;
    if (!prev) {
        next = n->next;
        l->head = next;
        if (next)
            next->prev = (AM2_MsgNode *)0;
        else
            l->tail = (AM2_MsgNode *)0;
    } else {
        next = n->next;
        if (!next) {
            prev->next = (AM2_MsgNode *)0;
            l->tail = prev;
        } else {
            prev->next = next;
            next->prev = prev;
        }
    }

#define COUNT (l->count)
    if (--COUNT < 0 || COUNT > AM2_MSGLIST_SANE_MAX)
        orig_log("RemMsg: Impossible List Size %d \n", COUNT);

    if (l == g_msgListPool) {
        /* The pool still has buffers: neither remaining complaint applies,
         * and the original jumps straight to the unlock. */
        if (l->head)
            goto unlock;
        orig_log("RemMsg has set freelist to EMPTY!! l->numItems = %d\n",
                 COUNT);
    }

    if (!l->head && COUNT > 0)
        orig_log("Why is list %x empty?  List Size %d \n", l, COUNT);

unlock:
    orig_release_mutex(l->mutex);
#undef COUNT
}

void __cdecl ExitGamePostClose(void)
{
    g_exitGameFlag = 1;

    /* Three trailing zeros, as the original pushes them: `add esp, 0x10`. */
    orig_log("Exit Game Posting WM_CLOSE from 3DONetwork\n", 0, 0, 0);

    orig_post_message(g_enumContext, AM2_WM_CLOSE, 0, 0);
}

typedef void *(__cdecl *AM2_FindPlayerFn)(uint32_t id);
typedef void (__cdecl *AM2_MsgHandlerFn)(void *msg, int32_t dpid);
/* Forward-declared rather than reached through win32/widget.h, which is the
   same reason script.cpp forward-declares PreloadSprite: this module is in the
   flat half and that header names Win32 types. The definition is in
   win32/widget.cpp, whose header is `extern "C"` like every other, so this
   has to be too or the two mangle differently and only the linker notices. */
extern "C" void __cdecl HudMessage(const char *text, uint8_t colour);
/* MenuMessage is reconstructed below; this forward declaration is here
 * because the two callers in this file come before it. */
void __cdecl MenuMessage(const char *text, uint8_t colour, int32_t indicator);


#define AM2_MSG_TYPE   0        /* the first dword: what the arm is chosen on */
#define AM2_MSG_SENDER 8        /* the chat arm reads this as a SIGNED byte */
#define AM2_MSG_TEXT   9

/* The chat arm. Which of the two message sinks it uses depends on the screen
 * that is up: the three menu screens get the menu one, everything else the
 * in-game HUD. */
static inline void DispatchChat(const uint8_t *msg)
{
    /* The sender byte is read on each path: a byte for MenuMessage's colour,
     * SIGNED and then shifted left eight for the HUD's table -- a negative
     * sender would index backwards out of it. The original's, and the table
     * is 256-byte records of which only the first byte is read. */
    int32_t screen = g_subState;

    if (screen == 7 || screen == 9 || screen == 8) {
        MenuMessage((const char *)(msg + AM2_MSG_TEXT),
                    *(const uint8_t *)(msg + AM2_MSG_SENDER), 1);
        return;
    }

    HudMessage((const char *)(msg + AM2_MSG_TEXT),
               *(const uint8_t *)(((uintptr_t)g_chatColourTable)
                                  + (uintptr_t)((int32_t)*(const int8_t *)
                                                (msg + AM2_MSG_SENDER) << 8)));
}

typedef int32_t (__cdecl *AM2_RegisterSelfFn)(uint32_t dpid);
typedef void (__cdecl *AM2_RequestStateFn)(int32_t state);


void __cdecl ReceiveStartGameMsg(void *msg, int32_t dpid)
{
    /* The comm object and each record's id are read at every use. */
#define COMM (kCommObj)
#define ID   (COMM->slots[(uint32_t)i].id)
    int32_t ok = 1;
    int32_t i;

    (void)dpid;   /* the dispatcher passes it; this one never reads it. */

    /* The host sent this; it does not receive it. */
    if (COMM->isHost)
        return;

    if (COMM->verbose) {
        /* "Seed is %d" pushes a literal 0 here as well as in the send half, so
         * the seed this function is about is never in either log. */
        am2_log("ReceiveStartGameMsg for %d Players.  Seed is %d \n",
                *(const int32_t *)((const uint8_t *)msg + AM2_MSG_VALUE), 0);
    }

    /* The count is re-read every iteration, as everywhere else in this file. */
    for (i = 0; i < COMM->playerCount; i++) {
        if ((int32_t)ID == (int32_t)COMM->selfId)
            continue;

        /* 7 is a result code, not a map. The second argument is the id, which
         * SendMapMsg does not read. */
        if (!SendMapMsg(7, (int32_t)ID)) {
            ok = 0;
            am2_log("DPLAY ERROR SENDING TO %x\n", ID);
        }

        /* Only if this player has no queue yet. Both tests re-read the id from
         * the record rather than reusing the one above. */
        if (!FindPlayerById(ID) && !CommRegisterSelf(ID)) {
            ok = 0;
            am2_log("FlowQ creation Failure %x\n", ID);
        }
    }

    /* Checked once, after every player -- so one failure stops the game for
     * all of them. A zero player count skips this test entirely. */
    if (ok) {
        /* Nothing in the image handles 0x0469. */
        orig_post_message(g_enumContext, AM2_WM_START_GAME, 0, 0);
        SendGamePause(1, 0x10000);
        RequestState(2);
        g_stateEnterOnce = 1;
        g_netGame         = 1;
        g_gameSeed =
            *(const int32_t *)((const uint8_t *)msg + MSG_START_OFF_SEED);
    } else {
        AM2_THE_COMM->CommGetSessionDesc();
        am2_log("Error in start\n");
    }
#undef COMM
#undef ID
}


#define AM2_MSG_PAUSE  8        /* non-zero: pause. zero: resume. */
#define AM2_MSG_FLAGS  0x0C

/* RemoteGamePause writes each block out: four explicit compares on the
 * slot for the pause arm and four more for the resume arm, the resume calls
 * cross-jumped by the compiler into one. A slot above 3 falls out with no
 * call made and the mask left alone. The message is read at every use. */
#define PAUSE_BLOCK(base)                                                   \
    if (*(const int32_t *)(m + AM2_MSG_PAUSE)) {                            \
        if (slot == 0)      { mask = (base);      PauseGame(mask); }        \
        else if (slot == 1) { mask = (base) << 1; PauseGame(mask); }        \
        else if (slot == 2) { mask = (base) << 2; PauseGame(mask); }        \
        else if (slot == 3) { mask = (base) << 3; PauseGame(mask); }        \
    } else {                                                                \
        if (slot == 0)      { mask = (base);      UnPauseGame(mask); }      \
        else if (slot == 1) { mask = (base) << 1; UnPauseGame(mask); }      \
        else if (slot == 2) { mask = (base) << 2; UnPauseGame(mask); }      \
        else if (slot == 3) { mask = (base) << 3; UnPauseGame(mask); }      \
    }

void __cdecl RemoteGamePause(void *msg, int32_t dpid)
{
    const uint8_t *m    = (const uint8_t *)msg;
    uint32_t       mask = 0;
    int32_t        slot = AM2_THE_COMM->CommPlayerSlot(dpid);

    /* Both blocks run if the message asks for both, and the second overwrites
     * the mask the log will print. */
    if (*(const uint32_t *)(m + AM2_MSG_FLAGS) & MSG_PAUSE_FLAG_A) {
        PAUSE_BLOCK(PAUSE_BIT_A_SLOT0)
    }
    if (*(const uint32_t *)(m + AM2_MSG_FLAGS) & MSG_PAUSE_FLAG_B) {
        PAUSE_BLOCK(PAUSE_BIT_B_SLOT0)
    }

    if (kCommObj->verbose && mask)
        am2_log("RemoteGamePause from %x; playerIndex== %d paused = %d "
                "pauseflags = %x (%x) (msg Pause=%x)\n",
                dpid, slot, *(const int32_t *)(m + AM2_MSG_PAUSE),
                GetPauseFlags(), mask, *(const uint32_t *)(m + AM2_MSG_FLAGS));
}
#undef PAUSE_BLOCK

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
/* 0x0040F330, thiscall. Find the slot holding this DirectPlay id and answer
 * that slot's ARMY (AM2_PLAYER_ARMY, +0x210) -- not the loop counter, and not
 * AM2_PLAYER_INDEX (+0x20C). It returned the index field until the VC6
 * relink compared the resolved bytes: `mov eax, [edx + ecx + 0x210]`. The two
 * fields agree in single player, which is why nothing had shown it.
 *
 * -1 when the count is zero or nothing matches. */
int32_t __attribute__((thiscall)) AM2_Comm::CommFindPlayer(int32_t dpid)
{
    AM2_Comm *comm = this;

    const AM2_Comm *c = comm;
    int32_t        n = c->playerCount;
    int32_t        i;

    for (i = 0; i < n; i++)
        if ((int32_t)c->slots[(uint32_t)i].id == dpid)
            return c->slots[(uint32_t)i].colour;
    return -1;
}

/* 0x0040F920, thiscall. Is the army that owns this uid still playing?
 *
 * Army 4 is the neutral one and answers YES without touching the object,
 * which is the same special case CommArmyOfSlot and CommSlotForArmy both
 * carry. Everything else reads the slot's active flag. */
int32_t __attribute__((thiscall)) AM2_Comm::ArmyInPlay(uint32_t uid)
{
    AM2_Comm *comm = this;

    int32_t army = UidArmy((int32_t)UidOnWire(uid));

    if (army != AM2_PLAYERS_MAX)
        return comm->slots[(uint32_t)army].wasHere;
    return 1;
}

int32_t __cdecl UidObjKind(uint32_t uid)
{
    int32_t   insert;
    int32_t   slot = FindSlot(uid, &insert);
    uint32_t *obj;

    if (slot >= 0) {
        obj = (uint32_t *)g_objTable[slot].obj;
        if (obj)
            return (int32_t)obj[0];
    }
    return 0;
}

typedef void *(__cdecl *AM2_TroopSubFn)(const void *at, int32_t army);
typedef void (__cdecl *AM2_PairApplyFn)(void *a, void *b, int32_t x, int32_t y);
#define orig_trooper_pair_apply \
    TrooperRemotePickupItem

/* AppendTroopState -- original 0x0044BC10, 656 bytes, one caller: TellOneSlot,
 * which walks a comm slot's army list and packs one message out of these.
 *
 * A DELTA ENCODER. It appends a variable-length record for one trooper, and
 * only when something has actually changed -- position, facing or pose, one
 * presence BIT each in the top three bits of the record's first dword.
 *
 * ITS RECEIVER IS ALREADY RECONSTRUCTED AND THIS CONFIRMS IT FROM THE OTHER
 * END. TroopSubParse documents the wire format; every claim it makes turns up
 * here independently -- the 29-bit uid with the army stripped (ADDR_UID_ON_WIRE
 * does the stripping), the three flags above it, the big-endian 24/16/8/0 head,
 * and the position as two TWELVE-bit fields in three bytes. CLAUDE.md prefers
 * the writer/reader pair over either alone; this is what that buys.
 *
 * IT ANSWERS AN OPEN QUESTION IN orig.h. TellOneSlot reserves TEN bytes for
 * the next record and its note calls that "a guess about
 * ADDR_APPEND_TROOP_STATE's output rather than a bound on it". It is a bound:
 * the head is 4, the pose byte is ALWAYS present, and position and facing add
 * at most 3 and 1 -- so a record is 5 bytes at least and 9 at most, and ten is
 * correct and tight by one.
 *
 * BIT 29 IS SET TWICE AND THE SECOND ONE IS NOT REDUNDANT. The conditional set
 * decides whether a record is emitted at all -- the function returns when no
 * flag is set -- and then bit 29 is forced ON unconditionally before the head
 * is written. So every record on the wire carries a pose byte whether or not
 * the pose changed. Dropping the second `or` as duplicated would change the
 * format.
 *
 * EACH FIELD HAS TWO THRESHOLDS AND ITS OWN CACHE. The trooper keeps a
 * (value, timestamp) pair per field, and a field goes on the wire when either
 * COMM_OFF_SEND_INTERVAL has passed and it differs at all, or the shorter
 * COMM_OFF_ENOUGH_CHANGE_LIMIT has passed and it differs a LOT -- more than 8 in an
 * axis for the position, or in the top nibble for the facing. Big changes get
 * through sooner. SARGE'S INTERVAL IS FORCED TO 1, so the leader is sent every
 * frame he moves.
 *
 * THE "TIME" IS A SEQUENCE, and the field it comes from is named for that:
 * FindPlayerById answers the sender's FLOW QUEUE -- the same lookup
 * DestroyFlow uses -- and FLOW_OFF_SEQUENCE is its counter. Every interval here is measured against the
 * sending player's +0x94, which TROOPER_OFF_LAST_SEQ is named for -- so the
 * thresholds count packets, not milliseconds, and a slow machine throttles by
 * the same amount as a fast one.
 *
 * The nibble byte is `((x8 ^ y4) & 0xF) ^ y4`, which takes its low nibble from
 * x's high bits and its high nibble from y's -- the merge idiom, and exactly
 * what TroopSubParse unpacks. */
void __cdecl AppendTroopState(void *msg, AM2_Object *o)
{
    uint8_t  *m = (uint8_t *)msg;
    uint8_t  *out;
    uint8_t  *outStart;
    uint32_t  seq;   /* the sending player's +0x94, a SEQUENCE not a clock */
    uint32_t  flags;
    uint32_t  interval;
    int32_t   dx, dy;

    if (!o->health)
        return;

    seq = FindPlayerById(AM2_THE_COMM->selfId)->sequence;

    if (o->lastSeq == seq)
        return;
    if (o->sentPoseSeq == seq && IsKind10To17(o->sentPose))
        return;

    dx = o->pos.x - o->sentPos.x;
    dy = o->pos.y - o->sentPos.y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    {
        /* The comm object is read at every use. */
#define comm AM2_THE_COMM
        interval = comm->sendInterval;
        if (o->sarge && interval > 1)
            interval = 1;


        /* The exact test JUMPS PAST the coarse one when it sets the flag, and
         * FALLS INTO it when it does not -- so a position that has not moved
         * still reaches the coarse test. It cannot pass it (dx and dy are then
         * zero), so an `else if` would answer the same; written as the branch
         * runs rather than as the answer it happens to give. */
        flags = 0;
        if (seq - o->sentPosSeq >= interval && (o->pos.x != o->sentPos.x || o->pos.y != o->sentPos.y))
            flags = 0x80000000u;
        else if (seq - o->sentPosSeq >= comm->enoughChangeLimit && (dx > 8 || dy > 8))
            flags = 0x80000000u;

        /* Each age is recomputed at its test, as the image does. */
        {
            if ((seq - o->sentFacingSeq >= interval && o->facing != o->sentFacing)
                || (seq - o->sentFacingSeq >= comm->enoughChangeLimit
                    && (((uint32_t)o->sentFacing ^ (uint32_t)o->facing)
                        & 0xFFFFFFF0u)))
                flags |= 0x40000000u;
        }

        if (seq - o->sentPoseSeq >= comm->enoughChangeLimit
            && o->pose != o->sentPose)
            flags |= 0x20000000u;
    }

    if (!flags)
        return;

    out = m + *(const uint16_t *)m;
    outStart = out;

    /* NOT redundant with the conditional set above: that one decides whether
     * to emit at all, this one puts the pose in every record. */
    flags |= 0x20000000u;

    {
        uint32_t head = (UidOnWire(o->uid) & 0x1FFFFFFFu) | flags;

        *out++ = (uint8_t)(head >> 24);
        *out++ = (uint8_t)(head >> 16);
        *out++ = (uint8_t)(head >> 8);
        *out++ = (uint8_t)head;
    }

    if (flags & 0x80000000u) {
        /* Clamped to twelve bits IN THE OBJECT, not just on the wire. */
        if (o->pos.x > 0xFFF)
            o->pos.x = 0xFFF;
        if (o->pos.y > 0xFFF)
            o->pos.y = 0xFFF;

        /* The position re-read from the object at each use. */
        *out++ = (uint8_t)o->pos.x;
        *out++ = (uint8_t)o->pos.y;
        *out++ = (uint8_t)(((((uint8_t)(o->pos.x >> 8))
                             ^ ((uint8_t)(o->pos.y >> 4))) & 0x0F)
                           ^ ((uint8_t)(o->pos.y >> 4)));

        o->sentPos = o->pos;
        o->sentPosSeq = seq;
    }

    if (flags & 0x40000000u) {
        *out++ = o->facing;
        o->sentFacing = o->facing;
        o->sentFacingSeq = seq;
    }

    if (flags & 0x20000000u) {
        *out++ = (uint8_t)o->pose;
        o->sentPose = o->pose;
        o->sentPoseSeq = seq;
    }

    /* THE LENGTH GAINS WHAT THIS CALL WROTE, NOT WHERE THE CURSOR ENDED.
     * The original saves the cursor's START into the argument slot at
     * 0x0044BDB8 -- overwriting `msg`, which it has already copied into ebp --
     * and subtracts THAT at 0x0044BE85. Reading the slot as the buffer makes
     * the tail look like `out - m`, which is the old length plus what was
     * written, so a second append would count the first one twice. Nothing
     * here could have caught it: TellOneSlot only runs with a live DirectPlay
     * session, which this machine cannot open. Found by reading the vehicle
     * sibling at 0x0045DAA0, which does the identical dance. */
    *(uint16_t *)m += (uint16_t)(out - outStart);
#undef comm
}


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
    const uint8_t *at  = m + sizeof(AM2_ArmyMsgHdr);   /* before end, as */
    const uint8_t *end = m + *(const uint16_t *)m;     /* the original   */

    while (at < end)
        at = (const uint8_t *)TroopSubParse(at, army);
}

/* RecvTrooperFire -- original 0x0044CB20, one caller. Message kind 0x17, and
 * the receiver for TrooperFireSend a few hundred lines up.
 *
 * IT SETTLES A NAME IN THE SENDER. That function's second parameter is called
 * `target` and its message field `shotAt`, both from the call site. This end
 * hands the same value to WeaponByUid -- which insists on kind 4 -- and then
 * searches for it among the six UNIT_OFF_INVENTORY slots to work out which
 * weapon index the shot came from. Two uses, both weapon-shaped: the field is
 * the WEAPON's uid, not the target's. The other field, +0x08, really is the
 * target, and both ends already call it `globTarg`.
 *
 * THE PAIR IS AN EXACT MIRROR IN ONE PLACE AND NOT IN ANOTHER. What the
 * sender ZEROES on the way out -- UNIT_OFF_FIRE_ACTIVE and UNIT_OFF_FIRE_F588
 * -- this sets to ONE on the way in, so the shot is armed on the receiving
 * machine as it is disarmed on the sending one. But the sender reads the
 * position out of the unit and this writes it back INTO the unit, so a remote
 * trooper's UNIT_OFF_FIRE_X/Y/Z come off the wire rather than from where it
 * actually stands.
 *
 * THE WEAPON GAINS OBJ_FLAG_NO_SWEEP, bit 27, whose meaning orig.h records as
 * unestablished. It is set here on a weapon that has just been fired
 * remotely, which is one more data point for whoever settles it.
 *
 * IF NO INVENTORY SLOT MATCHES, TROOPER_OFF_WEAPON is left alone. The six
 * comparisons are a chain and the last one simply falls through, so a weapon
 * the trooper is not carrying leaves the previous index standing rather than
 * clearing it.
 *
 * It reuses its own ARGUMENT SLOT to hold the trooper's wire uid, which is
 * why the log's first value is the uid and not the message pointer.
 */
void __cdecl RecvTrooperFire(void *msg)
{
    const AM2_TrooperFireMsg *m = (const AM2_TrooperFireMsg *)msg;
    uint32_t troopUid  = UidOnWire(m->hdr.uid);
    uint32_t targetUid = UidOnWire(m->target);
    uint32_t weaponUid = UidOnWire(m->shotAt);
    AM2_Object *unit;
    AM2_Object *weapon;

    unit = (AM2_Object *)(uint8_t *)ObjByUidAlias(troopUid);
    if (!unit)
        return;

    weapon = WeaponByUid((int32_t)weaponUid);
    if (!weapon)
        return;

    weapon->flags |= OBJ_FLAG_NO_SWEEP;

    unit->sight.bearing =
        unit->facing;
    unit->sight.field00 = 1;
    unit->sight.hit   = 1;
    unit->sight.seen   = 1;

    unit->sight.spot.at.x = m->x;
    unit->sight.spot.at.y = m->y;
    unit->sight.spot.ground = m->z;

    unit->sight.uid        = targetUid;
    unit->sight.weaponUid = weaponUid;

    /* Six tests written out, an else-if chain, not a loop. */
#define INV(n) (unit->inventory[n])
#define SEL    (unit->inventorySel)
    if (INV(0) == weaponUid)      SEL = 0;
    else if (INV(1) == weaponUid) SEL = 1;
    else if (INV(2) == weaponUid) SEL = 2;
    else if (INV(3) == weaponUid) SEL = 3;
    else if (INV(4) == weaponUid) SEL = 4;
    else if (INV(5) == weaponUid) SEL = 5;
#undef INV
#undef SEL

    unit->fireFlag =
        m->flag;

    if (kCommObj->verbose)
        am2_log("Trooper Fire Rec, trooper: %x,  face:%d, pos (%d,%d,%d),"
                " loctarg %x, globTarg %x, weapidx %d\n",
                troopUid,
                (int32_t)unit->facing,
                (int32_t)m->x,
                (int32_t)m->y,
                (int32_t)m->z,
                targetUid, m->target,
                unit->inventorySel);
}

/* RecvTrooperDropItem -- original 0x0044C9C0, one caller. Message kind 0x21,
 * eTROOPER_DROP_ITEM_MESSAGE in the original's own vocabulary.
 *
 * The receiver for what TrooperDropItemSend sends, so that function, this one
 * and TrooperDropItem are one closed group now -- and reading them together
 * is what makes the odd parts legible.
 *
 * IT PUTS THE UIDS THROUGH UidOnWire ON THE WAY IN, and the sender already
 * put them through on the way out. That is the THIRD place in this family
 * doing it to a value that is already a wire uid, after the sender's own
 * second log line. Harmless only because UidOnWire is the identity in this
 * build; if it ever stopped being, this is where a message would arrive
 * addressed to nobody.
 *
 * THE ITEM GOING MISSING IS A HANDLED CASE, NOT A FAILURE. When the uid does
 * not resolve it looks in the slot the message names, and if what is there is
 * a weapon carrying OBJ_FLAG_REPLACED -- the flag UseInventoryItem sets on a
 * spent one -- it removes the slot and says "Weapon destroyed before
 * dropping; but we handled it". Anything else is dropped in silence. So a
 * drop and a use racing each other is anticipated.
 *
 * AND A MESSAGE ABOUT OUR OWN UNIT IS REFUSED, which is the mirror of the
 * sender's gate: CommMustBroadcast on the TROOPER's army says the drop was
 * ours to begin with and has already happened locally. That arm returns
 * without touching anything, logging only if COMM_OFF_VERBOSE is set -- so
 * with logging off the two paths are indistinguishable from outside.
 *
 * THE AMMO IS ONLY APPLIED WHEN POSITIVE. A quantity of zero -- which is
 * exactly what UseInventoryItem's send puts in the message -- leaves the
 * item's own ITEM_OFF_AMMO alone, and TrooperDropItem then reads that zero
 * and makes the item vanish rather than placing it. The two functions agree
 * about the meaning of zero without either of them saying so.
 */
void __cdecl RecvTrooperDropItem(void *msg)
{
    const AM2_ItemMsg *m = (const AM2_ItemMsg *)msg;
    AM2_Object *trooper;
    AM2_Object *item;

    if (kCommObj->verbose)
        am2_log("-->Trooper Drop Item Received: Trooper: %x, item: %x,"
                " request: %d, slot: %d, quant: %d \n",
                UidOnWire(m->hdr.uid),
                UidOnWire(m->item),
                m->request,
                m->slot,
                m->quant);

    trooper = (AM2_Object *)(uint8_t *)ObjByUidAlias(
        UidOnWire(m->hdr.uid));
    if (!trooper)
        return;

    item = LookupByUID(
        UidOnWire(m->item));

    if (!item) {
        /* The slot is re-read from the message at each use. */
        AM2_Object *held = WeaponByUid(
            (int32_t)trooper->inventory[m->slot]);

        if (!held)
            return;
        if (!(held->flagsLo & OBJ_FLAG_REPLACED))
            return;

        RemoveInventoryItem(trooper, m->slot);
        if (kCommObj->verbose)
            am2_log("Weapon destroyed before dropping; but we handled it\n");
        return;
    }

    if (!ObjIsType4(item))
        return;

    if (kCommObj->verbose)
        am2_log("Drop item received & performed; ammo %d\n",
                m->quant);

    if (kCommObj->CommMustBroadcast(
            (int16_t)trooper->owner)) {
        if (kCommObj->verbose)
            am2_log("\tDrop Item already handled (it's my unit)\n");
        return;
    }

    if (m->quant > 0)
        item->ammo =
            m->quant;

    TrooperDropItem(trooper, m->slot,
                    m->at);
}

/* RecvTroopPair -- original 0x0044C960, one caller. Message kind 0x18.
 *
 * Two uids and two dwords. The first uid must resolve; the second must
 * resolve AND be a type 4, which ADDR_OBJ_IS_TYPE4's own error string calls a
 * WEAPON. Then the pair and both dwords go to TrooperRemotePickupItem.
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
    const AM2_PairMsg *m = (const AM2_PairMsg *)msg;
    void          *a;
    void          *b;

    a = ObjByUidAlias(UidOnWire(m->hdr.uid));
    if (!a)
        return;

    b = LookupByUID(UidOnWire(m->b));
    if (!b)
        return;

    if (!ObjIsType4((const AM2_Object *)b))
        return;

    orig_trooper_pair_apply((AM2_Object *)a, (AM2_Object *)b,
                            m->byteArg,
                            m->arg);
}

/* RecvTrooperSetWeapon -- original 0x0044C3E0, one caller.
 *
 * The receiver for eTROOPER_SET_WEAPON_MESSAGE, and the twin of armymsg.cpp's
 * SendTrooperSetWeapon -- so that message, like kind 0x25, is now ours at both
 * ends. Put the weapon's uid in the trooper's inventory SLOT, set the soldier
 * kind from the weapon's own code, and select that slot.
 *
 * IT CONFIRMS THE SENDER'S ONE UNNAMED FIELD. SendTrooperSetWeapon writes its
 * third value to a literal `msg + 0x18` -- the sender could say only that it
 * is "the weapon" the caller passed. Here it is the INDEX: the receiver uses
 * it twice, once to pick the UNIT_OFF_INVENTORY slot and once to write
 * UNIT_OFF_INVENTORY_SEL. So it is an inventory slot, 0..5, and
 * MSG_SETWEAPON_OFF_SLOT is named for that.
 *
 * ITS THREE LOG LINES ARE NOT GATED ON COMM_OFF_VERBOSE, unlike the pair the
 * vehicle-exit family has. One fires on every successful receive and the
 * other two on each of the two failures, so a multiplayer session with the
 * verbosity off still logs every weapon change. Reproduced; the asymmetry is
 * within one message family and is the original's.
 *
 * The success line is emitted BEFORE either lookup, so it announces a link
 * that the very next branch may refuse.
 *
 * SoldierKindForWeapon is ours, so it is called by name; checkseams failed
 * the build on the first attempt, where it had gone in as an orig_ macro out
 * of habit. Three of this function's four callees were already reconstructed
 * and I reached for the image for one of them anyway.
 *
 * The two uids resolve through different functions again -- ObjByUidAlias for
 * the trooper and WeaponByUid for the weapon, the latter insisting on kind 4
 * and complaining itself if it is not. What is stored is the WIRE UID, not
 * the object.
 */
void __cdecl RecvTrooperSetWeapon(void *msg)
{
    const AM2_SetWeaponMsg *m = (const AM2_SetWeaponMsg *)msg;
    uint32_t       trooperUid;
    uint32_t       weaponUid;
    AM2_Object       *trooper;
    AM2_Object          *weapon;

    trooperUid = UidOnWire(m->hdr.uid);
    weaponUid  = UidOnWire(m->weapon);

    orig_log("Received TrooperSetWeapon message: linking trooper %08x to weapon %08x\n",
             trooperUid, weaponUid);

    trooper = (AM2_Object *)(uint8_t *)ObjByUidAlias(trooperUid);
    if (!trooper) {
        orig_log("Received TrooperSetWeapon message: trooper %08x not found\n",
                 trooperUid);
        return;
    }

    weapon = WeaponByUid(weaponUid);
    if (!weapon) {
        orig_log("Received TrooperSetWeapon message: weapon %08x not found\n",
                 weaponUid);
        return;
    }

    /* The slot is re-read from the message at each use. */
    trooper->inventory[m->slot] =
        weaponUid;

    SoldierKindForWeapon(
        trooper,
        (uint32_t)weapon->typeRec->kind);

    trooper->inventorySel =
        m->slot;
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

/* RecvTrooperWantItem is reconstructed, in armymsg.cpp, and called by name.
 * Declared here rather than by including that header, the same way
 * SendPlayerMsg is above: commmsg.cpp is flat and this signature names no
 * Win32 type. */
void __cdecl RecvTrooperWantItem(void *msg);

void __cdecl TroopMessageRecv(void *msg, int32_t army)
{
    uint32_t kind = *(const uint16_t *)((const uint8_t *)msg + 2);

    switch (kind) {
    case AM2_MSG_TROOP_FIRST:
        RecvTroopBatch(msg, army);
        return;

    case AM2_MSG_TROOPER_FIRE:
        RecvTrooperFire(msg);
        return;

    /* 0x19 before PAIR: arm bodies follow source order, and the original's
     * third arm is the want-item one. */
    case 0x19:
        RecvTrooperWantItem(msg);
        return;

    case AM2_MSG_PAIR:
        RecvTroopPair(msg);
        return;

    case AM2_MSG_TROOPER_DROP_ITEM:
        if (kCommObj->verbose)
            orig_log("troopMessageReceive: got eTROOPER_DROP_ITEM_MESSAGE\n");
        RecvTrooperDropItem(msg);
        return;

    case AM2_MSG_TROOPER_WEAPON:
        if (kCommObj->verbose)
            orig_log("troopMessageReceive: got eTROOPER_SET_WEAPON_MESSAGE\n");
        RecvTrooperSetWeapon(msg);
        return;

    default:
        orig_log("Unknown Troop Message of type %d Received\n", kind);
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
void __cdecl VehicleTakeOutOccupant(uint32_t uid, AM2_Object *vehicle)
{
    AM2_Object *v = (AM2_Object *)vehicle;
    AM2_Object *unit;
    int32_t  i;

    if (!vehicle)
        return;

    for (i = 0; i < v->riders.count; i++) {
        if ((v->riders.uids)[i]
                == uid) {
            ((AM2_Rec *)&v->riders)->ListRemoveAt(i);
            break;
        }
    }

    unit = LookupByUID(uid);

    unit->searchBase = 0;
    ApplyObjHeight(unit, v->heightSet);
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
    const AM2_VehicleMsg *m = (const AM2_VehicleMsg *)msg;
    void          *vehicle;

    if (kCommObj->verbose)
        orig_log("-->Vehicle Exit Received: Vehicle: %x, trooper: %x\n",
                 UidOnWire(m->hdr.uid),
                 UidOnWire(m->occupant));

    vehicle = ObjByUidAlias(UidOnWire(m->hdr.uid));
    if (vehicle) {
        /* The trooper's uid is decoded before the vehicle is pushed. */
        uint32_t trooper = UidOnWire(m->occupant);
        VehicleTakeOutOccupant(trooper, (AM2_Object *)vehicle);
    }
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


void __cdecl VehicleMsgRecv(void *msg, int32_t army)
{
    uint32_t kind = *(const uint16_t *)((const uint8_t *)msg + 2);

    switch (kind) {
    case 0x1B: RecvVehicle1B(msg, army);        return;
    case 0x1C: RecvVehicle1C(msg);              return;
    case 0x1D: RecvVehicle1D(msg);              return;
    case 0x1E: RecvVehicle1E(msg);              return;
    case 0x1F: RecvVehicle1F(msg);              return;
    case 0x24: RecvVehicle24(msg);              return;
    case AM2_MSG_VEHICLE_EXIT:
        RecvVehicleExit(msg);
        return;
    default:
        orig_log("Unknown Vehicle Message of type %d Received\n", kind);
        return;
    }
}

#define orig_raw_log       ((AM2_RawLogFn)(uintptr_t)ADDR_LOG)

/* Spelled exactly as objtable.h spells it -- uint32_t, not int32_t -- so the
 * two stay one definition. checkglobals refused the first attempt. */

#define AM2_ARMYMSG_SIZE 0x00   /* uint16_t */
#define AM2_ARMYMSG_TYPE 0x02   /* uint16_t */
#define AM2_ARMYMSG_UID  0x04

/* The tail both game-over arms share: record who won, ask for menu 0x22 and
 * raise the pending flag by hand rather than through RequestState. */
static inline void RecordGameOver(int32_t winner)
{
    g_gameWinner   = winner;
    g_menuRequest  = AM2_MENU_REQUEST_GAME_OVER;
    g_statePending = 1;
}

void __cdecl ReceiveArmyMsg(void *msg, int32_t slot, int32_t seq)
{
    uint8_t  *m    = (uint8_t *)msg;
    AM2_Comm  *comm = kCommObj;
    /* The uid is read from the message at every use. */
#define uid (*(const uint32_t *)(m + AM2_ARMYMSG_UID))
    int32_t   army;
    int32_t   kind;

    (void)seq;

    if (!comm->msgsEnabled)
        return;

    if (!((AM2_Comm *)(comm))->ArmyInPlay(uid)) {
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

    /* An if-chain on the 16-bit type, not a switch, every arm returning. */
    if (*(const uint16_t *)(m + AM2_ARMYMSG_TYPE) == 4) {
        am2_log("Received GAME_LOST_MESSAGE\n");
        RecordGameOver(army);
        return;
    }
    if (*(const uint16_t *)(m + AM2_ARMYMSG_TYPE) == 5) {
        am2_log("Received GAME_WON_MESSAGE\n");
        /* With winning disabled, a win is recorded exactly as a loss. Our
         * own win is recorded as winner 1. */
        if (g_winEnabled) {
            g_gameWinner = 1;
            if ((int32_t)g_defaultOwner != army)
                g_gameWinner = army;
            g_menuRequest  = AM2_MENU_REQUEST_GAME_OVER;
            g_statePending = 1;
        } else {
            RecordGameOver(army);
        }
        return;
    }
    if (*(const uint16_t *)(m + AM2_ARMYMSG_TYPE) == 0x20) {
        EventMessageReceive((const AM2_EventMsg *)m);
        return;
    }
    am2_log("Unknown Army Msg Item Type %d, msgtype:%d, item uid: %x; "
            "msgsize: %d\n",
            kind, *(const uint16_t *)(m + AM2_ARMYMSG_TYPE),
            UidOnWire(uid),
            *(const uint16_t *)(m + AM2_ARMYMSG_SIZE));
#undef uid
}

void __cdecl ReceivePacket(void *packet, int32_t dpid)
{
    AM2_Packet  *pkt = (AM2_Packet *)packet;
    /* The length as it arrived, read before the slot lookup. The loop below
     * decrements the field itself, and the bogus-length test is against
     * THIS, not against the remainder. */
    int32_t   total = (int32_t)pkt->len;
    int32_t   slot  = AM2_THE_COMM->CommSlotOfId(dpid);
    uint8_t  *p     = (uint8_t *)packet + PACKET_HEADER_SIZE;
    uint16_t  part;
    int32_t   step;

    /* Only the first five packets of a session are logged. */
    if (pkt->seq < 5
        && kCommObj->verbose)
        am2_log("Get Packed  %x bytes seq %d Chksum %x \n",
                (int32_t)pkt->len,
                (int32_t)pkt->seq,
                (int32_t)pkt->checksum);

    /* A bad checksum is counted and complained about, and then the packet is
     * walked anyway. */
    if (XorChecksum(pkt)) {
#define ERRS (kCommObj->slots[(uint32_t)slot].chksumErrs)
        am2_log("Receive Checksum Error from %x seq %d\n",
                dpid, (int32_t)pkt->seq);
        ERRS = ERRS + 1;
#undef ERRS
    }

    /* UNSIGNED, as the original's `jbe` is. It matters: the bogus-length test
     * below compares against the length on ENTRY, so a part longer than what
     * REMAINS is accepted and drives this field negative -- and a negative
     * length read unsigned is enormous, so the walk carries on off the end of
     * the packet rather than stopping. Signed here would quietly fix that, and
     * fixing it is not this port's job.
     *
     * Shaped as the image lays it out: an endless loop with three returns.
     * VC6 copies the zero-part test to the loop's entry and bottom, and the
     * length exits share the epilogue that follows the bogus complaint --
     * a `while (part != 0)` with the zero complaint after the loop sends
     * them to the zero complaint's epilogue instead. */
    if (pkt->len > PACKET_HEADER_SIZE) {
        for (;;) {
            part = *(const uint16_t *)p;
            if (part == 0) {
                am2_log("Received zero Length Message\n");
                return;
            }
            if ((int32_t)part >= total) {
                am2_log("Received Bogus Length Message, part greater than sum\n");
                return;
            }

            ReceiveArmyMsg(p, slot, (int32_t)pkt->seq);

            /* Re-read, after the handler has had the bytes, and as an int. */
            step = *(const uint16_t *)p;
            p += step;
            pkt->len -= step;
            if (pkt->len <= PACKET_HEADER_SIZE)
                return;
        }
    }
}

/* FORWARD-DECLARED RATHER THAN INCLUDED, for the reason script.cpp gives for
 * PreloadSprite: CheckMapRules lives in win32/widget.cpp because it paints
 * widgets, and this is a flat module that checksplit.py forbids from naming a
 * Win32 type. One declaration is cheaper than moving the function to the
 * wrong side of the split. */
extern "C" int32_t __cdecl CheckMapRules(uint32_t rulesSum,
                                        uint32_t scriptSum,
                                        uint32_t mapSum);
typedef int32_t (__cdecl *AM2_SprintfFn)(char *out, const char *fmt, ...);
#define orig_sprintf ((AM2_SprintfFn)(uintptr_t)ADDR_GAME_SPRINTF)


/* Update the current dialog and repaint it, with no player list sent after --
 * which is what separates this from RepaintDialogAndSendPlayers. */
static inline void RepaintDialog(void)
{
    void *dlg = g_paintObject;

    if (!dlg)
        return;
    ObjVUpdate(dlg);
    dlg = g_paintObject;
    ObjVPaint(dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
}

void __cdecl ReceivePlayerMsg(const AM2_PlayerMsg *msg, int32_t dpid)
{
    int32_t             *slotSetting;
    int32_t             *ours;
    int32_t              i;

    (void)dpid;

    /* Every use re-reads the comm object, as the original does. */
#define comm AM2_THE_COMM
#define SETTING_END ((int32_t)((uintptr_t)&g_scoreLimit))

    /* The host sends it. */
    if (comm->isHost)
        return;

    /* Not gated on verbosity, alone among this function's logs. */
    am2_log("ReceivePlayerMsg for %d Players. I reckoned there were %d "
            "Players \n",
            msg->count, comm->playerCount);

    comm->connected = msg->connected;

    if (comm->verbose)
        am2_log("      map: %s, mapSum=%X, ruleSum=%X\n",
                msg->mapName, msg->mapSum, msg->ruleSum);

    comm->playerCount = msg->count;
    g_scoreLimit = msg->scoreLimit;
    *(int32_t *)&g_gameOverFlags = msg->overFlags;
    g_gameSetting22c = msg->setting22c;

    /* Four entries, and the end is the address of the next global, compared
     * SIGNED both times: once as the loop's own bottom test (`jl`) and once
     * mid-body. The loop test stops after the fourth record, so the mid-body
     * break can never fire; it is in the image all the same. (This was once
     * written as an unbounded loop with only the break, which ran a fifth
     * record through the log and CommSetSlotRemote.) */
    slotSetting = g_armyPoints;
    i = 0;
    while ((int32_t)(uintptr_t)slotSetting < SETTING_END) {
        if (comm->verbose)
            am2_log("                 player: %s %x %d\n",
                    msg->recs[i].name, msg->recs[i].id, msg->recs[i].colour);

        /* Done before the (dead) mid-body bound check. */
        if (msg->recs[i].id == comm->selfId) {
            g_ourSlot      = i;
            g_defaultOwner = (uint32_t)i;
            comm->CommClearSlotRemote(i);
        } else {
            comm->CommSetSlotRemote(i);
        }

        if ((int32_t)(uintptr_t)slotSetting >= SETTING_END)
            break;

        comm->slots[i].id          = msg->recs[i].id;
        comm->slots[i].colour      = msg->recs[i].colour;
        comm->slots[i].team        = msg->recs[i].team;
        comm->slots[i].wasHere     = msg->recs[i].wasHere;
        comm->slots[i].readyToLoad = msg->recs[i].readyToLoad;
        *slotSetting = msg->recs[i].setting;

        if (msg->recs[i].id != comm->selfId) {
            /* The SECOND time for this record, and this one is bounded by the
             * count where the first is not. */
            if (i < comm->playerCount)
                comm->CommSetSlotRemote(i);
            if (!FindPlayerById(comm->slots[i].id))
                am2_log("PlayerMsg received before DPSYS_CREATEPLAYERORGROUP. "
                        "Unusual but should work. No flowq yet for id %x\n",
                        comm->slots[i].id);
        }

        strcpy(comm->slots[i].name, msg->recs[i].name);
        /* The three steps at the bottom of the body, not in a `for` header:
         * VC6 lays the loop out differently for the two. */
        i++, slotSetting++;
    }
#undef SETTING_END

    /* Our own slot may have arrived empty; fill in the id we already know. */
    ours = (int32_t *)&comm->slots[g_ourSlot].id;
    if (*ours == 0 || *ours == -1)
        *ours = comm->selfId;

    if (msg->hasMap) {
        int32_t rules;

        strcpy(&g_mpScriptName, msg->mapName);
        strcpy(&g_mapName_, msg->levelName);
        rules = CheckMapRules(msg->ruleArg, msg->ruleSum, msg->mapSum);
        SendMapMsg(rules, comm->connected);
    }

    if (msg->version != g_gameVersion
        || msg->checksum != g_dataChecksum) {
        char buf[0x40];

        /* OUR name, from ADDR_DEFAULT_OWNER -- the disagreeing client
         * announces itself rather than naming the host. */
        orig_sprintf(buf, "%s has a different version of the game.",
                     comm->slots[g_defaultOwner].name);
        MenuMessage(buf, 4, 0);
        SendChatMsg(buf, 1);
        SendMapMsg(5, comm->connected);
    }

    RepaintDialog();
#undef comm
}

void __cdecl ReceiveFlowControlMsg(void *msg, int32_t dpid)
{
    const AM2_FlowCtlMsg *m = (const AM2_FlowCtlMsg *)msg;
    AM2_Comm       *comm = kCommObj;
    AM2_Flow       *q;

    (void)dpid;   /* given, and never read -- alone in this family. */

    /* The host is the one that sends this. */
    if (comm->isHost)
        return;

    comm->sendFlags =
        (uint32_t)m->sendFlags;

    /* OUR record, not the sender's. */
    q = FindPlayerById(
            kCommObj->selfId);
    if (!q)
        return;

    q->lagMs = m->lagMs;
    q->lagSpread = m->lagSpread;
}

void __cdecl CommDispatchMessage(void *msg, int32_t size, int32_t from,
                                 int32_t to)
{
    const AM2_Comm *comm = kCommObj;
    uint8_t       *m    = (uint8_t *)msg;
    int32_t        type;

    /* The queue node's length and addressee: passed, not read. Every
     * handler is given the SENDER, the third argument (0x0040FEE6 and the
     * rest read [esp+0x10] under one push). */
    (void)size;
    (void)to;

    if (!comm->msgsEnabled)
        return;

    type = *(const int32_t *)(m + AM2_MSG_TYPE);

    /* The arms in the image's layout order, which is the source order VC6
     * keeps; the table at 0x00410044 is in TYPE order. */
    switch (type) {
    case 1:
        g_lastMsgValue =
            *(const int32_t *)(m + AM2_MSG_VALUE);
        g_lastMsgChecksum = XorChecksum(m);
        return;

    case 11:
        ReceivePacket(m, from);
        /* The slot writer's first argument is a PLAYER record here, not the
         * comm object msgslot.h describes -- FindPlayerById returns one. */
        MsgSlotB0(FindPlayerById((uint32_t)from),
                  *(const uint32_t *)(m + AM2_MSG_VALUE));
        return;

    case 5:
        ReceiveStartGameMsg(m, from);
        return;

    case 8:
        /* Tested only to complain; the handler runs either way. */
        if (comm->isHost)
            am2_log("Message Error:  Host should not receive PLAYERMSG\n");
        ReceivePlayerMsg((const AM2_PlayerMsg *)m, from);
        return;

    case 15:
        ReceivedMapMsg(m, from);
        return;

    case 9:
        if (!comm->isHost)
            return;
        ReceivedColorMsg(m, from);
        return;

    case 17:
        if (!comm->isHost)
            return;
        ReceivedTeamMsg(m, from);
        return;

    case 18:
        if (!comm->isHost)
            return;
        ReceiveGameReadyToLoadMsg(m, from);
        return;

    case 7:
        ReceiveGameReadyMsg(m, from);
        return;

    case 3:
        DispatchChat(m);
        return;

    case 6:
        RemoteGamePause(m, from);
        return;

    case 10:
        ReceiveEndSetupMsg(m, from);
        return;

    case 14:
        ReceiveFlowControlMsg(m, from);
        return;

    case 2:
        /* Known and ignored, in silence -- unlike the unknown arm below. */
        return;

    default:
        /* 4, 12, 13, 16 and anything above 18. */
        am2_log("Unknown message type %d\n", type);
        return;
    }
}


void __cdecl TrooperFireSend(AM2_Object *trooper, AM2_Object *target)
{
    AM2_Object           *t = (AM2_Object *)trooper;
    AM2_TrooperFireMsg msg;
    const AM2_Flow     *flow;
    int32_t            seq;

    if (kCommObj->dplay == 0)
        return;

    flow = FindPlayerById(
               (uint32_t)(int32_t)kCommObj->selfId);
    seq = (int32_t)flow->sequence;

    msg.hdr.len  = AM2_MSG_TROOPER_FIRE_LEN;
    msg.hdr.kind = AM2_MSG_TROOPER_FIRE;
    msg.hdr.uid  = UidOnWire(t->uid);
    msg.target   = UidOnWire(t->sight.uid);
    msg.shotAt   = UidOnWire(target->uid);
    msg.x        = t->sight.spot.at.x;
    msg.y        = t->sight.spot.at.y;
    msg.z        = t->sight.spot.ground;
    msg.flag     = t->fireFlag;

    /* Cleared before the send, not after. */
    t->sight.field00 = 0;
    t->sight.hit = 0;

    ArmyMessageSend(&msg);

    if (kCommObj->verbose)
        am2_log("Trooper Fire Send, trooper: %d,  face:%d, pos (%d,%d,%d), "
                "loctarg %x, globTarg %x, weap %d, seq:%d\n",
                t->uid, t->facing,
                (int32_t)msg.x, (int32_t)msg.y, (int32_t)msg.z,
                t->sight.uid, msg.target,
                t->inventorySel, seq);

    t->lastSeq = seq;
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
 * BlinkerStart, ListAdd and ArrowBarFollowEnd live in win32/widget.cpp and
 * are declared here rather than included: this module is flat, and widget.h
 * reaches LPDIRECTDRAWSURFACE through the sprite in a widget. Same seam and
 * the same reason as the three comm methods above.
 *
 * The widget at MP_PANEL_OFF_CHATBOX is the scrolling text list itself and
 * its 0x7C is LIST_OFF_ARROWBAR -- the bar beside it, which is what gets
 * told to follow the end. It was CHATBOX_OFF_INNER until the bar's own
 * method was read; one offset, one name. */
extern "C" {
}


void __cdecl MenuMessage(const char *text, uint8_t colour, int32_t indicator)
{
    uint8_t *log = *(uint8_t * *)&g_menuMsgList;
    uint8_t *screen;
    uint8_t *chatbox;

    if (!log)
        return;

    ((AM2_Rec *)log)->ListAdd(text, (void *)(uintptr_t)colour);
    if (*(const int32_t *)log > AM2_MENU_MSG_MAX)
        ListDropOldest(log);

    if (g_subState == AM2_MENU_MODE_NO_CHAT)
        return;

    screen = g_paintObject;
    if (!screen)
        return;

    chatbox = *(uint8_t **)(screen + MP_PANEL_OFF_CHATBOX);
    ArrowBarFollowEnd(*(void **)(chatbox + LIST_OFF_ARROWBAR));

    /* Re-read, as the original does: it loads the field again rather than
     * keeping the register it had across the call. */
    chatbox = *(uint8_t **)(screen + MP_PANEL_OFF_CHATBOX);
    ObjVPaint(chatbox, *(const AM2_Rect *)(chatbox + 0x14));

    if (indicator)
        (*(AM2_BlinkerOps **)(screen + MP_PANEL_OFF_BLINKER_1))
            ->BlinkerStart(AM2_BLINK_PERIOD, AM2_BLINK_FLASHES);
    else
        (*(AM2_BlinkerOps **)(screen + MP_PANEL_OFF_BLINKER_0))
            ->BlinkerStart(AM2_BLINK_PERIOD, AM2_BLINK_FLASHES);
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

/* CommSystemMessage is reconstructed in win32/dplay.cpp and its typedef went
 * with it. Worth knowing what this call site does with the three arguments
 * after the message: the callee's own format strings call them size, from and
 * to, and this passes the node's dpid, a constant 0 and the node's +0x0C. So
 * "DPSYS_HOST Size=%d" logs a player id and "from=%x" is always zero. The
 * names stay the function's own, because they are what it says about itself;
 * the mismatch is the original's. */
#define orig_comm_system_msg CommSystemMessage

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
 * The message's +0x08 is its SENDER, and it decides where it goes: zero is
 * DPID_SYSMSG and routes to the DirectPlay SYSTEM handler; anything else is a
 * game message and goes to CommDispatchMessage. Both are handed (message,
 * length, sender, addressee) -- the node's +0x20, +0x24, +0x08 and +0x0C.
 * This once read +0x24 as the sender, and CommDispatchMessage passed it on to
 * every handler as the player id; it is the message length.
 *
 * The loop re-reads the head after each node rather than snapshotting the
 * list, so a handler that queues more work has it drained in the same pass. */
void __cdecl CommDrainMsgs(void)
{
    AM2_MsgNode *node;

    if (!kCommObj->active)
        return;

    for (node = MsgListRemHead(g_msgListB);
         node;
         node = MsgListRemHead(g_msgListB)) {
        /* The node: +0x08 sender, +0x0C addressee, +0x20 message, +0x24
         * its length -- what IDirectPlay::Receive filled in. */
        int32_t  from = (int32_t)node->from;
        void    *msg  = node->body;
#define to ((int32_t)node->to)

        /* DPID_SYSMSG is 0: a system message goes to the system handler. */
        if (from == 0)
            orig_comm_system_msg(msg,
                                 (int32_t)node->bodyLen,
                                 0, to);
        else
            CommDispatchMessage(msg,
                                (int32_t)node->bodyLen,
                                from, to);

        MsgListAdd(g_msgListPool, node);
    }
#undef to
}

/* 0x00401210. Drain one message list into another, head first, until the
 * source is empty. RemHead answers null when there is nothing left, which is
 * both the loop's test and its value. */
void __cdecl DrainMsgList(void *list)
{
    AM2_MsgNode *node;

    while ((node = MsgListRemHead((AM2_MsgList *)list)) != (void *)0)
        MsgListAdd(g_msgListPool, node);
}

/* TellEachSlot -- original 0x0044C550, one caller.
 *
 * Walk the four comm slots and run one thing on every slot that is OCCUPIED
 * and that CommMustBroadcast accepts. Both tests, in that order, and the
 * second is the usual "must I tell the other players about this army" --
 * which answers no outside a multiplayer session, so this loop does nothing
 * in single player however many slots are filled.
 *
 * The slot index and the byte offset are carried separately -- `esi` by 0x70
 * and `edi` by one -- and the bound is on the OFFSET, `esi < 0x1C0`. Four
 * records of 112 bytes. Written as an index with the stride applied, which is
 * the same walk; the original's two counters are the compiler's.
 */
void __cdecl TellEachSlot(void)
{
    int32_t slot;

    for (slot = 0; slot < AM2_COMM_SLOTS; slot++) {
        /* WAS_HERE (+0x25C), the original's `[esi + ecx + 0x25C]`. This read
         * COMM_ARMY_OFF_REMOTE (+0x20C) until the VC6 relink put the two side
         * by side -- a different field of the same record, so it asked the
         * wrong question of each slot in a multiplayer session. */
        if (!kCommObj->slots[slot].wasHere)
            continue;
        if (!((AM2_Comm *)(void *)kCommObj)->CommMustBroadcast((int16_t)slot))
            continue;

        TellOneSlot(slot);
    }
}

/* TroopSubParse -- original 0x0044BEA0, one caller: the kind 0x16 batch above.
 *
 * Parse one variable-length trooper record and answer the pointer past it. A
 * four-byte header, then up to four more bytes, each present only if the
 * header says so.
 *
 * THE HEADER IS BIG-ENDIAN AND IT IS READ BYTE BY BYTE. Four separate loads
 * shifted into place -- 24, 16, 8, 0 -- on a little-endian machine, so this is
 * a deliberate wire order and not a struct read. Its low 29 bits are the uid
 * and the top three are one presence flag each.
 *
 * THE ARMY IS SHIFTED INTO THE SAME THREE BITS THE FLAGS CAME OUT OF. The
 * lookup key is `(word & 0x1FFFFFFF) | (army << 29)`, so the uid on the wire
 * carries no army and the receiver supplies its own. That is why the flags can
 * live up there at all: they are stripped before the key is built.
 *
 * THE POSITION IS THREE BYTES FOR TWO TWELVE-BIT FIELDS. Low byte of x, low
 * byte of y, then one byte whose low nibble is x's high four bits and whose
 * high nibble is y's. The original stashes the first two bytes in its own
 * ARGUMENT SLOTS and reads them back, which is why the reconstruction needs
 * two locals where the disassembly appears to need none.
 *
 * NOTHING CHECKS THE LOOKUP. `ObjByUidAlias` answers NULL for a uid that no
 * longer resolves, and every field below is written through it unconditionally
 * -- including the fire-mode read at the end, which happens even when neither
 * optional field was present. A stale uid in a packet faults the receiver.
 * The original's, and reproduced.
 *
 * THE LAST TEST IS ON A FIELD THIS FUNCTION MAY NOT HAVE WRITTEN. It reads
 * UNIT_OFF_FIRE_MODE whether or not the mode byte was in the record, so a
 * record carrying neither optional field still clears the F588/F58C pair
 * unless the mode already sitting there is 0x1C..0x1E.
 *
 * Both optional writes stamp UNIT_OFF_FIRE_STAMP and set UNIT_OFF_FIRE_ACTIVE,
 * and they do it separately rather than once at the end -- so a record with
 * both fields writes the clock twice.
 */
const void *__cdecl TroopSubParse(const uint8_t *p, int32_t army)
{
    uint32_t       head;
    AM2_Object       *obj;

    /* Big-endian, a byte and a pointer step at a time. */
    head  = (uint32_t)*p++ << 24;
    head |= (uint32_t)*p++ << 16;
    head |= (uint32_t)*p++ << 8;
    head |= (uint32_t)*p++;

    obj = (AM2_Object *)(uint8_t *)ObjByUidAlias((head & AM2_TROOPSUB_UID_MASK)
                                   | ((uint32_t)army
                                      << AM2_TROOPSUB_ARMY_SHIFT));

    if (head & AM2_TROOPSUB_HAS_POS) {
        uint8_t lox = *p++;
        uint8_t loy = *p++;
        uint8_t hi  = *p++;

        obj->pos.x =
            (uint16_t)(lox | ((uint16_t)(hi & 0x0Fu) << 8));
        obj->pos.y =
            (uint16_t)(loy | ((uint16_t)(hi & 0xF0u) << 4));
    }

    if (head & AM2_TROOPSUB_HAS_FACING) {
        obj->sight.bearing = *p++;
        obj->fireStamp =
            g_gameClockMs;
        obj->sight.field00 = 1;
    }

    if (head & AM2_TROOPSUB_HAS_MODE) {
        obj->sight.state = (int32_t)*p++;
        obj->fireStamp =
            g_gameClockMs;
        obj->sight.field00 = 1;
    }

    {
        int32_t mode = obj->sight.state;

        if (mode < AM2_FIRE_MODE_KEEPS_LO || mode > AM2_FIRE_MODE_KEEPS_HI) {
            obj->sight.hit = 0;
            obj->sight.seen = 0;
        }
    }

    return p;
}

/* CommSend is reconstructed, in win32/dplay.cpp with the rest of the comm
 * object. It is declared here rather than by including that header because
 * commmsg.cpp is on the flat side of the split and dplay.h pulls in win32.h --
 * the same reason script.cpp forward-declares PreloadSprite. Its four
 * arguments are exactly four of the delayed node's fields. */

/* And CommRecentTotal beside it, for the same reason: bytes sent in the last
 * 100 ms, which is the only thing the governor below measures.
 *
 * It needs `extern "C"` and CommSend does NOT, which looks inconsistent and is
 * not: dplay.h declares this one inside its extern "C" block, and does not
 * declare CommSend at all -- that name's only declaration is the line above,
 * so both sides of it are C++. The linkage has to match the header that
 * exists, not the one next to it. */
extern "C" 
/* FlushDelayedSends -- original 0x00402F50, one caller, the frame chain's
 * post-work. It lives here rather than in air.cpp, whose band the image
 * suggests, for two reasons that point the same way: this file already owns
 * the message lists and their helpers, and air.cpp is in SELFTEST_SRC, where
 * a dependency on CommSend cannot be linked. Drain the delayed send queue: every node whose deadline
 * GetTickCount has reached goes out through CommSend and its buffer goes back
 * to the pool.
 *
 * IT WAS CALLED ADDR_COMM_FRAME_POST_B, a name off that one call site, which
 * says only WHEN it runs. What makes the body readable is the pair of lists:
 * MsgListInsert puts a node into ADDR_MSG_LIST_DELAYED in ascending
 * MSGNODE_OFF_KEY order and this compares that key against GetTickCount, so
 * for this list the key is a millisecond deadline and the list is a timer
 * queue. The node then goes to ADDR_MSG_LIST_POOL, which is what settles that
 * the second list is the free-buffer pool -- and retires ADDR_MSG_LIST_A, the
 * letter that was sitting on the same address.
 *
 * A FAILED SEND LEAKS THE NODE. It has already been unlinked by
 * MsgListRemHead, and every error arm returns without putting it back on
 * either list, so the buffer is gone. That is the original's behaviour and is
 * reproduced; returning it to the pool would be tidier and would differ. Any
 * result other than the five named is logged by the general line alone.
 *
 * The loop RE-READS the head each time round rather than walking the node's
 * own next pointer, which is what keeps it safe against the packet thread
 * inserting while it runs -- MsgListRemHead takes the list's mutex and this
 * holds no node across the gap.
 */
void __cdecl FlushDelayedSends(void)
{
    uint32_t  now  = orig_get_tick_count();
    AM2_MsgList  *list = (AM2_MsgList *)g_msgListDelayed;

    while (list->head) {
        AM2_MsgNode *node = (AM2_MsgNode *)list->head;
        int32_t  hr;

        if (now < node->key)
            return;

        node = MsgListRemHead(list);

        hr = AM2_THE_COMM->CommSend(
                      node->to,
                      node->flags,
                      node->body,
                      node->bodyLen);

        if (hr < 0) {
            am2_log("DPlaySend Failure %x to %x size %d\n", hr,
                    (int32_t)node->to,
                    (int32_t)node->bodyLen);

            if ((uint32_t)hr == AM2_DPERR_BUSY)
                am2_log("DPLAY ERROR: Busy\n");
            else if ((uint32_t)hr == AM2_DPERR_INVALIDOBJECT)
                am2_log("DPLAY ERROR: Invalid Object\n");
            else if ((uint32_t)hr == AM2_E_INVALIDARG)
                am2_log("DPLAY ERROR: INVALID PARAMETERS\n");
            else if ((uint32_t)hr == AM2_DPERR_INVALIDPLAYER)
                am2_log("DPLAY ERROR: INVALID PLAYER\n");
            else if ((uint32_t)hr == AM2_DPERR_SENDTOOBIG)
                am2_log("DPLAY ERROR: Send too big\n");
            return;
        }

        MsgListAdd(g_msgListPool, node);
    }
}


/* CommInitDefaults -- original 0x0040FD40, one caller. Stamp the two-dword
 * header of twenty messages that live in .bss: a kind and the record's whole
 * size, and nothing else.
 *
 * IT FILLS NO TABLE, which is what orig.h called it. The twenty records are
 * scattered across 0x004F48E8..0x004FC8A8 with no stride between them, so this
 * is twenty separate structures being initialised in one place rather than an
 * array being walked.
 *
 * THE KIND IS NOT UNIQUE: four different records take kind 0x0B at sizes
 * 0x400, 0x14, 0x14 and 0x400. So a kind selects a HANDLER and the record says
 * which conversation it belongs to; a survey assuming one record per kind
 * would come up four short.
 *
 * Forty assignments, as the original has them -- it is straight-line code, not
 * a loop over a table. The table they were once written as was EXTRACTED from
 * the disassembly by script rather than transcribed, which is the lesson
 * tools/posecheck.py records about hand-copied tables.
 *
 * The order is the image's, including the two records whose halves are
 * written far apart (0x004FC3A8 and 0x004FC3B8): VC6 keeps statement order
 * here, so that is how the source had them.
 *
 * Four of the twenty already had names, which is what confirms the layout
 * rather than assuming it: ADDR_MSG_CHAT is kind 3 at 0x108 bytes, a 256-byte
 * line plus a header, and ADDR_MSG_COLOR and ADDR_MSG_TEAM are both 0x0C.
 */
void __cdecl CommInitDefaults(void)
{
#define MSG_KIND(addr, kind) \
    *(int32_t *)((uint8_t *)(uintptr_t)(addr) + COMMMSG_OFF_KIND) = (kind);
#define MSG_SIZE(addr, size) \
    *(int32_t *)((uint8_t *)(uintptr_t)(addr) + COMMMSG_OFF_SIZE) = (size);
#define MSG_INIT(addr, kind, size) MSG_KIND(addr, kind) MSG_SIZE(addr, size)
    MSG_KIND(((uintptr_t)&g_playerMsg)       , 0x08)
    MSG_INIT(((uintptr_t)g_msgKind01)       , 0x01, 0x404)
    MSG_INIT(((uintptr_t)g_msgKind02)       , 0x02, 0x010)
    MSG_INIT(((uintptr_t)g_msgChat)         , 0x03, 0x108)
    MSG_INIT(((uintptr_t)g_msgKind04)       , 0x04, 0x010)
    MSG_INIT(((uintptr_t)g_msgGameStart)   , 0x05, 0x194)
    MSG_SIZE(((uintptr_t)&g_playerMsg)       , 0x238)
    MSG_INIT(((uintptr_t)g_msgGamePause)   , 0x06, 0x010)
    MSG_INIT(((uintptr_t)g_msgReadyToLoad), 0x12, 0x00C)
    MSG_INIT(((uintptr_t)g_msgGameReady)   , 0x07, 0x00C)
    MSG_INIT(((uintptr_t)g_msgMap)          , 0x0F, 0x010)
    MSG_INIT(((uintptr_t)g_msgColor)        , 0x09, 0x00C)
    MSG_INIT(((uintptr_t)g_msgTeam)         , 0x11, 0x00C)
    MSG_INIT(((uintptr_t)g_msgEndSetup)    , 0x0A, 0x008)
    MSG_INIT(((uintptr_t)g_msgKind0d)       , 0x0D, 0x10C)
    MSG_INIT(((uintptr_t)g_resendBuf)       , 0x0B, 0x400)
    MSG_INIT(((uintptr_t)g_armyPacket)      , 0x0B, 0x014)
    MSG_INIT(((uintptr_t)g_armyPacketB)    , 0x0B, 0x014)
    MSG_INIT(((uintptr_t)g_resendScratch)   , 0x0B, 0x400)
    MSG_INIT(((uintptr_t)g_msgKind0c)       , 0x0C, 0x010)
    MSG_INIT(((uintptr_t)g_msgKind10)       , 0x10, 0x014)
#undef MSG_KIND
#undef MSG_SIZE
#undef MSG_INIT
}


/* SendChatTo -- original 0x00411F10, one caller. The same chat record
 * ADDR_SEND_CHAT_MSG broadcasts, addressed to ONE player instead.
 *
 * IT TRUNCATES AT 254 AND MEASURES TWICE. The length test is `> 0xFF`, and
 * what it then writes is a terminator at 0xFE -- so a string of exactly 255
 * characters is left alone and a longer one is cut to 254. Then it measures
 * again for the copy, because the truncation may have changed the answer. Two
 * `repne scasb` runs over the same buffer, and reproduced as two: folding them
 * would be right for every input and would still be a different program.
 *
 * THE INK IS BYTE 1 OF ADDR_OBJ_TABLE_RECORDS, indexed by the sender's army,
 * with ADDR_COLOUR_WHITE when it is zero. It went in as a table of its own at
 * 0x004F9ACD -- a field pointer named as a base, which is what orig.h now
 * records. A second reader takes it exactly the same way and hands the answer
 * to a HUD text call, which is what says the byte is an ink and settles one
 * byte of a record orig.h otherwise calls unestablished.
 *
 * THE ARGUMENT SHUFFLE AGAIN, THE THIRD TIME THIS FAMILY HAS SHOWN IT.
 * SendGameMsg's trailing zero is pushed BEFORE CommPlayerId is called, that
 * call takes the one argument above it and cleans only its own, and the two
 * remaining pushes complete the three. Pairing each push with the nearest call
 * reads it as a two-argument SendGameMsg and a two-argument CommPlayerId, and
 * both would be wrong.
 */
void __cdecl SendChatTo(char *text, int32_t slot)
{
    AM2_ChatMsg *rec = g_msgChat;

    if (strlen(text) > AM2_CHAT_TEXT_MAX)
        text[AM2_CHAT_TEXT_MAX - 1] = 0;

    strcpy(rec->text, text);

    /* Stored, then tested and overwritten: the image writes the ink
     * before it knows whether it is zero. */
    rec->ink =
        *(g_objTableRecords
          + (uint32_t)kCommObj->CommArmyOfSlot(
                *(const int32_t *)&g_defaultOwner)
            * AM2_OBJ_TABLE_REC_SIZE
          + OBJ_TABLE_REC_OFF_INK);
    if (!rec->ink)
        rec->ink = g_whiteInk;

    SendGameMsg(rec, kCommObj->CommPlayerId(slot), 0);
}



/* 0x00411FB0 -- the BANDWIDTH GOVERNOR, and the last game function below the
 * CRT line that was still the image's.
 *
 * Measure what went out in the last 100 ms and move enoughChangeLimit so the
 * next window fits: above 300 bytes raise it, by two rather than one past 600;
 * below 150 lower it again, but only while it is above ADDR_CHANGE_LIMIT_FLOOR
 * so a quiet network cannot wind the throttle down to nothing. Then the
 * INTERVAL is always the limit plus one.
 *
 * BOTH LOG LINES NAME THE FIELD -- "enoughChangeLimit increased to %d" -- which
 * is where COMM_OFF_ENOUGH_CHANGE_LIMIT gets its name from, in place of the
 * COMM_OFF_SEND_COARSE we had invented for it.
 *
 * IT RUNS AT MOST ONCE PER SEQUENCE. The gate is our own player record's
 * FLOW_OFF_SEQUENCE against ADDR_BANDWIDTH_LAST_SEQ, so however often the
 * frame calls this, the limit moves once per sequence and no faster.
 *
 * The two sweeps at the end run whether or not anything was adjusted -- they
 * are past the early-out, not inside either arm, which is why the `jae` at the
 * top skips only the tuning. */
void __cdecl CommTuneChangeLimit(void)
{
    /* The comm object is read at every use. */
#define COMM  (g_commObject)
#define LIMIT (COMM->enoughChangeLimit)
    uint32_t seq;
    uint32_t sent;

    seq = FindPlayerById(COMM->selfId)->sequence;

    if (g_bandwidthLastSeq < seq) {
        sent = (uint32_t)AM2_THE_COMM->CommRecentTotal();

        if (sent > AM2_BANDWIDTH_HIGH) {
            if (sent > AM2_BANDWIDTH_VERY_HIGH)
                LIMIT += 2u;
            else
                LIMIT += 1u;
            g_bandwidthLastSeq = seq;
            orig_log("Exceeded bandwidth (%d), enoughChangeLimit increased to %d\n",
                     sent, LIMIT);
        } else if (LIMIT > g_changeLimitFloor
                   && sent < AM2_BANDWIDTH_LOW) {
            LIMIT -= 1u;
            g_bandwidthLastSeq = seq;
            orig_log("Less than half bandwidth (%d), enoughChangeLimit decreased to %d\n",
                     sent, LIMIT);
        }

        COMM->sendInterval = LIMIT + 1u;
    }

    TellEachSlot();
    SendAllVehicleUpdates();
#undef COMM
#undef LIMIT
}

int commmsg_install(void)
{
    patch_replace(ADDR_TROOP_SUB_PARSE, (const void *)TroopSubParse,
                  "TroopSubParse", 1);
    patch_replace(ADDR_FLUSH_DELAYED_SENDS, (const void *)FlushDelayedSends,
                  "FlushDelayedSends", 1);
    patch_replace(ADDR_COMM_INIT_DEFAULTS, (const void *)CommInitDefaults,
                  "CommInitDefaults", 1);
    patch_replace(ADDR_SEND_CHAT_TO, (const void *)SendChatTo,
                  "SendChatTo", 1);
    patch_replace(ADDR_DRAIN_MSG_LIST, (const void *)DrainMsgList,
                  "DrainMsgList", 1);
    patch_replace(ADDR_TELL_EACH_SLOT, (const void *)TellEachSlot,
                  "TellEachSlot", 1);
    patch_replace(ADDR_TROOP_MESSAGE_RECV, (const void *)TroopMessageRecv,
                  "TroopMessageRecv", 1);
    patch_replace(ADDR_RECV_TROOP_DROP_ITEM, (const void *)RecvTrooperDropItem,
                  "RecvTrooperDropItem", 1);
    patch_replace(ADDR_RECV_TROOPER_FIRE, (const void *)RecvTrooperFire,
                  "RecvTrooperFire", 1);
    patch_replace(ADDR_RECV_TROOP_16, (const void *)RecvTroopBatch,
                  "RecvTroopBatch", 1);
    patch_replace(ADDR_APPEND_TROOP_STATE, (const void *)AppendTroopState,
                  "AppendTroopState", 2);
    patch_replace(ADDR_RECV_VEHICLE_1B, (const void *)RecvVehicle1B,
                  "RecvVehicle1B", 2);
    patch_replace(ADDR_VEHICLE_UPDATE_APPLY,
                  (const void *)VehicleUpdateApply,
                  "VehicleUpdateApply", 2);
    patch_replace(ADDR_RECV_VEHICLE_1D, (const void *)RecvVehicle1D,
                  "RecvVehicle1D", 1);
    patch_replace(ADDR_RECV_VEHICLE_1F, (const void *)RecvVehicle1F,
                  "RecvVehicle1F", 1);
    patch_replace(ADDR_RECV_VEHICLE_1C, (const void *)RecvVehicle1C,
                  "RecvVehicle1C", 1);
    patch_replace(ADDR_RECV_VEHICLE_1E, (const void *)RecvVehicle1E,
                  "RecvVehicle1E", 1);
    patch_replace(ADDR_RECV_VEHICLE_24, (const void *)RecvVehicle24,
                  "RecvVehicle24", 1);
    patch_replace(ADDR_RECV_TROOP_PAIR, (const void *)RecvTroopPair,
                  "RecvTroopPair", 1);
    patch_replace(ADDR_RECV_TROOP_SET_WEAPON,
                  (const void *)RecvTrooperSetWeapon,
                  "RecvTrooperSetWeapon", 1);
    patch_replace(ADDR_VEHICLE_MSG_RECV, (const void *)VehicleMsgRecv,
                  "VehicleMsgRecv", 1);
    patch_replace(ADDR_RECV_VEHICLE_EXIT, (const void *)RecvVehicleExit,
                  "RecvVehicleExit", 1);
    patch_replace(ADDR_VEHICLE_TAKE_OUT, (const void *)VehicleTakeOutOccupant,
                  "VehicleTakeOutOccupant", 1);
    patch_replace(ADDR_COMM_FIND_PLAYER, pmf_addr(&AM2_Comm::CommFindPlayer),
                  "CommFindPlayer", 2);
    patch_replace(ADDR_ARMY_IN_PLAY, pmf_addr(&AM2_Comm::ArmyInPlay),
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
    patch_replace(ADDR_MSG_LIST_REMOVE, (const void *)MsgListRemove,
                  "MsgListRemove", 3);
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
    patch_replace(ADDR_RECV_MSG_4014C0, (const void *)FlowRecvMessage,
                  "FlowRecvMessage", 1);
    patch_replace(ADDR_COMM_TUNE_CHANGE_LIMIT,
                  (const void *)CommTuneChangeLimit,
                  "CommTuneChangeLimit", 0);
    return 0;
}

/* 0x004014C0 -- the flow-control receive path, called from the receive
 * thread for every message that arrives on a reliable channel.
 *
 * Its own log strings name it: "Interrupt Level Can't find FlowQ for %x"
 * says where it runs, and the three cases it dispatches are the three
 * messages of the protocol -- AM2_FLOW_DATA, AM2_FLOW_NACK and
 * AM2_FLOW_PULSE_ACK. orig.h carries the full reading; the parts that
 * matter for this transcription are repeated where they bite.
 *
 * THE RETURN VALUE IS THE INTERFACE. 1 means the message is dealt with and
 * the caller must not queue it; 0 means it goes on the incoming list. Every
 * exit below is one or the other deliberately, and the default case is
 * literally the shared `return 0` epilogue.
 *
 * THE PACKET FIELDS ARE OVERLOADED BY MESSAGE TYPE. On a data message +8 is
 * the sequence and +0xC the checksum, which is what PACKET_OFF_SEQ and
 * PACKET_OFF_CHECKSUM say. On a NACK the same two dwords are the first and
 * last sequence being asked for. That is the type-6 blast situation again --
 * one record read two ways -- so the nack arm spells them out rather than
 * using names that would be actively misleading there.
 *
 * Nothing here can be A/B'd: no DirectPlay session opens on this machine, so
 * every counter in this function is 0 on every drive the project has. It is
 * verified by reading, which is weaker than the rest of the tree. */
#define FLOWMSG_NACK_FIRST   0x08u   /* PACKET_OFF_SEQ, read as a range base */
#define FLOWMSG_NACK_LAST    0x0Cu   /* PACKET_OFF_CHECKSUM, read as its end */

/* Flow-record fields only this function touches. The in-order arrival
 * statistics are a second set beside the round-trip ones: the gap between
 * deliveries, its minimum, maximum and running total, and the time of the
 * last in-order arrival they are measured from. */
#define FLOW_OFF_NACKS_SENT     0x38u  /* sequences asked for again */
#define FLOW_OFF_GAP_MIN        0x50u  /* reset to 10,000 on sequence 1 */
#define FLOW_OFF_GAP_MAX        0x54u
#define FLOW_OFF_GAP_TOTAL      0x58u
#define FLOW_OFF_DELIVERED_AT   0x70u  /* GetTickCount at the last delivery */

/* The out-of-order queue at FLOW_OFF_QUEUE is a message list: its head node
 * is the dword after the list's first. */
#define FLOW_QUEUE_HEAD(f) (f->queue.head)

/* The NACK message this side sends, kind 0xC: the range lives where a data
 * message keeps its sequence and checksum. */
#define FLOW_NACK_OUT_FIRST \
    (*(uint32_t *)(uintptr_t)(((uintptr_t)g_msgKind0c) + FLOWMSG_NACK_FIRST))
#define FLOW_NACK_OUT_LAST \
    (*(uint32_t *)(uintptr_t)(((uintptr_t)g_msgKind0c) + FLOWMSG_NACK_LAST))

/* Both live in win32/dplay.cpp and win32/air.h's C++ half, and this is the
 * flat half, so they are declared here rather than included -- the same
 * reasoning as the CommSend declaration above, and the same as script.cpp's
 * PreloadSprite. Including dplay.h would reach DirectPlay's COM types
 * transitively and tools/checksplit.py would fail this file, correctly:
 * nothing below talks to an API, it hands buffers to functions that do. */
void *__cdecl MsgListCopyByKey(AM2_MsgList *list, int32_t key, void *dst);
extern "C" void __cdecl DumpMsgList(AM2_MsgList *list);
void *__cdecl MsgListInsert(AM2_MsgList *list, AM2_MsgNode *node);
int32_t __cdecl PeerShouldNack(AM2_Flow *peer, uint32_t seq);

/* 0x004014C0 -- the flow-control receive path. See orig.h for the full
 * reading; the short version is that this is called from the receive thread
 * ("Interrupt Level Can't find FlowQ for %x") for every message arriving on
 * a reliable channel, and dispatches three protocol messages.
 *
 * IT ANSWERS 1 WHEN IT HAS DEALT WITH THE MESSAGE and 0 when the caller
 * should queue it. The constant is the whole interface: a 0 where a 1
 * belongs plays a duplicate twice, and a 1 where a 0 belongs drops real
 * traffic. Every arm that keeps a node puts it somewhere itself -- back in
 * the pool, on the incoming list, or in the flow's out-of-order queue -- and
 * answers 1; only the lookup failures, the default, and a message with no
 * sender answer anything else.
 *
 * THE NOT-FOUND EXITS ARE NOT SYMMETRIC. A missing flow queue makes the DATA
 * arm answer 1 and the other two answer 0. Nothing in the code comments on
 * it and it would be very easy to regularise; it is reproduced as found.
 *
 * WRITTEN OUT AS THE IMAGE HAS IT, every arm in place. An earlier version
 * shared the two send-queue retirement loops through one helper and stopped
 * the DATA arm after the duplicate test, answering 0 for everything else;
 * that delivered every message through the caller unordered and never
 * advanced NEXT_EXPECTED, nacked nothing, and returned NACKs to the incoming
 * list rather than the pool. Byte-matching against the image is what showed
 * the missing two thirds. The two retirement loops are NOT identical: the
 * PULSE ACK copy prints "PULSE Adding to freelist" unconditionally and the
 * DATA copy prints nothing there, and each has its own "???" line for a
 * round trip over ten seconds.
 *
 * THE PACKET FIELDS ARE OVERLOADED BY MESSAGE TYPE. On a data message +8 is
 * the sequence and +0xC the checksum, which is what PACKET_OFF_SEQ and
 * PACKET_OFF_CHECKSUM say. On a NACK the same two dwords are the first and
 * last sequence being asked for, spelled FLOWMSG_NACK_FIRST and _LAST.
 *
 * Nothing here can be A/B'd: no DirectPlay session opens on this machine, so
 * it is verified by reading and by the byte comparison. */
int32_t __cdecl FlowRecvMessage(void *nodev)
{
#define comm (g_commObject)
#define VERBOSE (comm->verbose)
#define FROM (node->from)
#define MINE (comm->selfId)
    AM2_MsgNode *node = (AM2_MsgNode *)nodev;
    AM2_FlowMsg *msg;
    AM2_Flow *sender, *me;
    AM2_MsgNode *sent;
    AM2_Packet *copy;
    uint32_t mask, now, seq, expected, last, lat, gap;
    int32_t  resent, any;

    if (FROM == 0)
        return NoteKind31(node);

    msg = (AM2_FlowMsg *)node->body;

    switch (msg->pkt.kind) {
    case AM2_FLOW_PULSE_ACK:
        sender = FindPlayerById(FROM);
        me     = FindPlayerById(MINE);
        if (!sender) {
            am2_log("Interrupt Level Can't find FlowQ for %x\n", FROM);
            break;
        }
        if (!me) {
            am2_log("Interrupt Level Can't find FlowQ for %x\n", MINE);
            break;
        }
        mask = GetPlayerMask(FROM);
        now  = orig_get_tick_count();

        /* THE ACK IS CUMULATIVE: one message retires every sequence from
         * the peer's last acknowledged up to the one it carries. HE_HAS is
         * written once, after the loop, which reads it. */
        if (msg->pkt.ack
                > sender->heHas
            && msg->pkt.ack
                < me->sequence) {
            for (seq = sender->heHas + 1;
                 seq <= msg->pkt.ack; seq++) {
                /* Not behind COMM_OFF_VERBOSE, unlike most of these. */
                am2_log(" Got Pulse Ack for  seq %d\n", seq);

                sent = MsgListSetFlag(
                    g_msgListSendq, (int32_t)seq, 0,
                    (int32_t)mask);
                if (!sent) {
                    if (VERBOSE)
                        am2_log("Flow Ack for Message not in sendqueue sequence %d from %x  %d mask %x  \n",
                                seq, FROM, FROM, mask);
                    if (VERBOSE)
                        am2_log("Remote %d is acking %d, I had thru %d \n",
                                FROM, msg->pkt.ack,
                                sender->heHas);
                    continue;
                }

                MsgSlotA0(sender, seq);
                sender->acksSeen += 1;
                lat = now - sent->stamp;
                if (lat > AM2_FLOW_RTT_CAP
                    && seq == sender->heHas + 1)
                    am2_log("??? PULSE seq %6d latency %d acks for %d msgs %d thru %d  nxtSeq = %d \n",
                            seq, lat,
                            msg->pkt.ack
                                - sender->heHas,
                            sender->heHas,
                            msg->pkt.ack,
                            me->sequence);

                /* The minimum is taken BEFORE the 10,000 ms clamp and the
                 * maximum after, so a long stall moves the max to exactly
                 * 10,000 and leaves the min alone. */
                if (sender->rttMin == 0
                    || lat < sender->rttMin)
                    sender->rttMin = lat;
                if (lat > AM2_FLOW_RTT_CAP)
                    lat = AM2_FLOW_RTT_CAP;
                if (lat > sender->rttMax)
                    sender->rttMax = lat;
                sender->rttTotal += lat;
                RingPush32(sender, lat);

                /* Nobody else is still waiting on it: the buffer goes home. */
                if ((g_playerSlotMask
                     & sent->flags) == 0) {
                    am2_log("PULSE Adding to freelist from sendque Buffer seq %d  elelment %x \n",
                            sent->key, sent);
                    MsgListRemove(g_msgListSendq, sent);
                    MsgListAdd(g_msgListPool, sent);
                }
            }
            sender->heHas =
                msg->pkt.ack;
        }

        /* AND THEN IT FALLS INTO THE NACK ARM. Having acked everything
         * through, whatever the message still names is asked for again in
         * the same pass -- the fall-through is the protocol, and the NACK
         * arm's two lookups run again. A pulse ack naming nothing simply
         * returns its buffer. */
        if (msg->nack.first == 0)
            goto pool;
        /* fall through */

    case AM2_FLOW_NACK:
        /* AT MOST TWO GO OUT NOW. Past that the arm stops sending and sets
         * the send queue's flag to 1 instead, leaving the message for
         * whatever drains that queue. */
        resent = 0;
        sender = FindPlayerById(FROM);
        me     = FindPlayerById(MINE);
        if (!sender) {
            am2_log("Interrupt Level Can't find FlowQ for %x\n", FROM);
            break;
        }
        if (!me) {
            am2_log("Interrupt Level Can't find FlowQ for %x\n", MINE);
            break;
        }
        if (VERBOSE)
            am2_log("GOT NACK: seq %d thru %d from %d (%x)  nxtSeq = %d \n",
                    msg->nack.first,
                    msg->nack.last,
                    FROM, FROM, me->sequence);

        mask = sender->wantBit;
        for (seq = msg->nack.first;
             seq <= msg->nack.last; seq++) {
            copy = (AM2_Packet *)MsgListCopyByKey(
                g_msgListSendq, (int32_t)seq,
                g_resendScratch);
            if (!copy) {
                /* Unconditional, like the pulse ack line. */
                am2_log("Flow Nack Resend: Msg not found seq %d hehas %d\n",
                        seq, sender->heHas);
                continue;
            }
            if (resent < AM2_FLOW_RESEND_BURST) {
                /* The resent copy carries our CURRENT ack, not the one it was
                 * first sent with, and is re-checksummed for it. */
                copy->ack =
                    sender->nextExpected;
                copy->checksum = 0;
                copy->checksum = XorChecksum(copy);
                {
                    int32_t rc = ((AM2_Comm *)(void *)comm)->CommSend(
                        FROM, 0, copy, copy->len);

                    if (rc < 0)
                        am2_log("DPlaySend Failure %x to %x size %d\n",
                                rc, FROM, copy->len);
                }
                MsgSlotA2(sender, seq);
                if (VERBOSE)
                    am2_log("Resend: seq %d to %d (%x)  nxtSeq = %d\n",
                            seq, FROM, FROM,
                            me->sequence);
                sender->resends += 1;
                resent++;
                MsgListSetFlag(g_msgListSendq,
                               (int32_t)seq, 0, (int32_t)mask);
            } else {
                if (VERBOSE)
                    am2_log("ADDING to Resend Queue seq %d to id %x  mask %x\n",
                            seq, FROM, mask);
                MsgListSetFlag(g_msgListSendq,
                               (int32_t)seq, 1, (int32_t)mask);
            }
        }
        goto pool;

    case AM2_FLOW_DATA:
        mask   = GetPlayerMask(FROM);
        sender = FindPlayerById(FROM);
        if (!sender) {
            am2_log("Can't Find FlowQ for id %x  seq \n",
                    FROM, msg->pkt.seq);
            return 1;
        }
        me = FindPlayerById(MINE);
        if (!me) {
            am2_log("Can't Find my FlowQ for id %x\n", MINE);
            return 1;
        }

        seq = msg->pkt.seq;
        if (seq < AM2_FLOW_TRACE_FIRST)
            am2_log("Received seq %d  expected %d from %x, he has my seqthru  %d\t\n",
                    seq, sender->nextExpected + 1,
                    FROM, msg->pkt.ack);

        /* A corrupt message is DROPPED, not queued -- hence the 1. */
        if (XorChecksum(msg)) {
            am2_log("Ack for sequence %d had bad checksum %x\n",
                    msg->pkt.seq,
                    msg->pkt.checksum);
            sender->badChecksums += 1;
            return 1;
        }

        /* TWO JOIN PATHS, both adopting the peer's numbering by writing
         * `NEXT_EXPECTED = seq - 1`. The first fires when our own sequence
         * is still 1, our next-expected is 0 and the flow is not ready, and
         * ONLY when we are not the host -- "Starting Slave Session". */
        expected = sender->nextExpected + 1;
        if (me->sequence == 1 && expected == 1
            && me->ready == 0) {
            if (comm->isHost == 0) {
                if (VERBOSE)
                    am2_log("Starting Slave Session, from %x, Received seq %d, flow->gotseq=%d, flow->hehas=%d, myFlow->active=%d\n",
                            FROM, msg->pkt.seq,
                            sender->nextExpected,
                            sender->heHas,
                            me->ready);
                sender->nextExpected =
                    msg->pkt.seq - 1;
                expected = msg->pkt.seq;
                me->ready = 1;
                me->readyAt = orig_get_tick_count();
                /* A SECOND read, deliberately: this one has half a round
                 * trip taken off it, so it estimates the PEER's clock base.
                 * +0x68 has one toucher in this image and stays unnamed. */
                me->peerClock =
                    orig_get_tick_count()
                    - (me->field68 >> 1);
            }
        } else if (sender->nextExpected == 0) {
            if (VERBOSE)
                am2_log("First message received from player %x, Received seq %d\n",
                        FROM, msg->pkt.seq);
            sender->nextExpected =
                msg->pkt.seq - 1;
            expected = msg->pkt.seq;
        }

        sender->recvPackets += 1;
        sender->recvBytes +=
            msg->pkt.len;
        now = orig_get_tick_count();
        sender->recvAt = now;

        /* A JOIN-TIME FILTER, not a window check: past sequence 10 nothing
         * is dropped for being far ahead. The node goes back to the pool. */
        if (expected < 10 && msg->pkt.seq > expected
            && msg->pkt.seq - expected > 50) {
            if (VERBOSE)
                am2_log("Residual Message from %x: expected  %d received %d  from %x hehas %d \n",
                        FROM, expected, msg->pkt.seq,
                        FROM, msg->pkt.ack);
            goto pool;
        }

        /* The piggybacked ack: EVERY DATA MESSAGE CARRIES ONE, which is why
         * this arm retires the send queue too -- its own copy of the loop. */
        if (msg->pkt.ack
                > sender->heHas
            && msg->pkt.ack
                < me->sequence) {
            if (msg->pkt.seq == 1) {
                if (VERBOSE)
                    am2_log("New Player joining flow he first got my seq %d hehas was %d  \n",
                            msg->pkt.ack,
                            sender->heHas);
                /* The dump is NOT behind the verbose flag; only the line. */
                DumpMsgList(g_msgListSendq);
            }
            /* A WARNING, NOT A GUARD: it proceeds either way. */
            if (msg->pkt.ack
                    - sender->heHas
                > AM2_FLOW_ACK_WARN)
                am2_log(" No many acks in one packet\t %d thru %d \n",
                        msg->pkt.ack,
                        sender->heHas);

            for (seq = sender->heHas + 1;
                 seq <= msg->pkt.ack; seq++) {
                sent = MsgListSetFlag(
                    g_msgListSendq, (int32_t)seq, 0,
                    (int32_t)mask);
                if (!sent) {
                    if (VERBOSE)
                        am2_log("Flow Ack for Message not in sendqueue sequence %d from %x  %d mask %x  \n",
                                seq, FROM, FROM, mask);
                    /* %x here where the PULSE ACK arm prints %d. */
                    if (VERBOSE)
                        am2_log("Remote %x is acking %d, I had thru %d \n",
                                FROM, msg->pkt.ack,
                                sender->heHas);
                    continue;
                }

                MsgSlotA0(sender, seq);
                sender->acksSeen += 1;
                lat = now - sent->stamp;
                if (lat > AM2_FLOW_RTT_CAP
                    && seq == sender->heHas + 1)
                    am2_log("??? seq %6d from %x latency %d acks for %d msgs %d thru %d  nxtSeq = %d \n",
                            seq, FROM, lat,
                            msg->pkt.ack
                                - sender->heHas,
                            sender->heHas,
                            msg->pkt.ack,
                            me->sequence);

                if (sender->rttMin == 0
                    || lat < sender->rttMin)
                    sender->rttMin = lat;
                if (lat > AM2_FLOW_RTT_CAP)
                    lat = AM2_FLOW_RTT_CAP;
                if (lat > sender->rttMax)
                    sender->rttMax = lat;
                sender->rttTotal += lat;
                RingPush32(sender, lat);

                /* No "Adding to freelist" line in this copy. */
                if ((sent->flags
                     & g_playerSlotMask) == 0) {
                    MsgListRemove(g_msgListSendq, sent);
                    MsgListAdd(g_msgListPool, sent);
                }
            }
        }
        sender->heHas =
            msg->pkt.ack;

        if (msg->pkt.seq
                > sender->highestSeq)
            sender->highestSeq =
                msg->pkt.seq;

        expected = sender->nextExpected + 1;

        /* Below what we already have: a resend we did not need. Returning
         * 1 here is what stops it being played twice. */
        if (msg->pkt.seq < expected) {
            sender->duplicates += 1;
            if (VERBOSE)
                am2_log("Duplicate: expected %d received %d  from %x hehas %d \n",
                        expected, msg->pkt.seq,
                        FROM, msg->pkt.ack);
            goto pool;
        }

        /* AHEAD OF WHAT WE EXPECT: park it in the flow's sorted queue and
         * nack the gap -- up to the queue's first sequence, or up to this
         * one if the queue was empty. More than fifty missing is logged and
         * the NACK message's own range clipped to ten, but the local loops
         * below still walk the whole gap. */
        if (msg->pkt.seq > expected) {
            node->key =
                msg->pkt.seq;
            MsgListInsert(&sender->queue, node);
            last = msg->pkt.seq - 1;
            if (FLOW_QUEUE_HEAD(sender))
                last = ChainField14(&sender->queue) - 1;
            FLOW_NACK_OUT_FIRST = expected;
            FLOW_NACK_OUT_LAST  = last;
            if (last < expected)
                last = expected;
            if (last - expected > 50) {
                am2_log("Flow Nacking ERROR too many missing from %d thru %d \n",
                        expected, last);
                FLOW_NACK_OUT_LAST = expected + 10;
            }

            any = 0;
            for (seq = expected; seq <= last; seq++)
                if (PeerShouldNack(sender, seq))
                    any = 1;
            if (any) {
                sender->nacksSent +=
                    last - expected + 1;
                if (VERBOSE)
                    am2_log("Flow Nacking %d thru %d  to %x at time %d I am good thru %d \n",
                            expected, last, FROM, now,
                            sender->nextExpected);
                SendGameMsg(g_msgKind0c, (int32_t)FROM, 0);
                for (; expected <= last; expected++)
                    MsgSlotB2(sender, expected);
            }
            return 1;
        }

        /* EXACTLY WHAT WE EXPECT: deliver it, time it, and then deliver
         * every queued message that now follows on. */
        if (msg->pkt.seq != expected)
            return 0;
        MsgListAdd(g_msgListB, node);
        if (expected == 1) {
            sender->gapTotal    = 0;
            sender->gapMin      = 10000;
            sender->gapMax      = 0;
            sender->deliveredAt = now;
        } else {
            if (sender->deliveredAt == 0)
                sender->deliveredAt = now;
            /* A clock that went BACKWARDS reads as a gap the size of the
             * old timestamp. Reproduced. */
            if (now < sender->deliveredAt)
                gap = sender->deliveredAt;
            else
                gap = now - sender->deliveredAt;
            sender->deliveredAt = now;
            if (gap < sender->gapMin)
                sender->gapMin = gap;
            if (gap > sender->gapMax)
                sender->gapMax = gap;
            if (gap > 100000)
                am2_log("Unusually large time between messages %d seconds\n",
                        gap / 1000);
            sender->gapTotal += gap;
        }
        MsgSlotB1(sender, msg->pkt.seq);
        CommRemoveKeyed(sender, expected);
        sender->nextExpected = expected;
        expected++;
        while (ChainField14(&sender->queue) == expected) {
            AM2_MsgNode *m = MsgListRemHead(&sender->queue);

            if (m)
                MsgListAdd(g_msgListB, m);
            CommRemoveKeyed(sender, expected);
            sender->nextExpected = expected;
            expected++;
        }
        return 1;
    }
    return 0;

pool:
    MsgListAdd(g_msgListPool, node);
    return 1;
#undef comm
#undef VERBOSE
#undef FROM
#undef MINE
}

/* RecvVehicle1B -- original 0x0045EB10, one caller. Message kind 0x1B is a
 * BATCH of vehicle updates, and this is the walk: the first word of the
 * message is its total length, the records start at +8, and each one is
 * handed to the applier, which returns the cursor past what it consumed.
 *
 * THE BOUND IS THE MESSAGE'S OWN LENGTH WORD, which is what
 * VehicleUpdateAppend maintains from the other end -- it adds only the bytes
 * each append wrote, so the two agree exactly. That agreement is the whole
 * protocol, and it is why the length defect fixed in AppendTroopState
 * mattered: a sender that over-counts sends a bound this loop would read
 * past.
 *
 * The applier is reached by ADDRESS because it is still the image's. */
uint8_t *__cdecl VehicleUpdateApply(uint8_t *at, int32_t army);

void __cdecl RecvVehicle1B(void *msg, int32_t army)
{
    uint8_t       *m   = (uint8_t *)msg;
    uint8_t       *at  = m + 8;                     /* before end, as the */
    const uint8_t *end = m + *(const uint16_t *)m;  /* original computes  */

    while (at < end)
        at = VehicleUpdateApply(at, army);
}

/* RecvVehicle1E -- original 0x0045E810, one caller, and IT DOES NOTHING.
 *
 * It resolves both uids, checks the second is a weapon, and then calls
 * 0x0045CAA0 with two arguments and no format string. That address is a bare
 * `c3` in this retail image, so the whole function is three lookups and a
 * discarded result -- a debug print that was compiled out, leaving the
 * guards that fed it.
 *
 * THE CALL IS NOT REPRODUCED, for the reason Step3Drive's header gives at
 * length: 0x0045CAA0 is also ADDR_LOG, which src/inject/gamelog.c patches
 * into a real varargs logger, so emitting it would hand our logger an object
 * pointer as a format string. The lookups ARE reproduced -- they are what the
 * original executes, and ObjByUidAlias is not free of side effects for us to
 * assume away. */
void __cdecl RecvVehicle1E(void *msg)
{
    const AM2_VehicleMsg *m = (const AM2_VehicleMsg *)msg;
    void          *vehicle;
    void          *item;

    vehicle = ObjByUidAlias(UidOnWire(m->hdr.uid));
    if (vehicle == 0)
        return;

    item = LookupByUID(UidOnWire(m->occupant));
    if (item == 0)
        return;

    if (!ObjIsType4((const AM2_Object *)item))
        return;

    /* The log that is a `ret`; see above. Emitted only under VC6, where
     * ADDR_LOG really is the bare `ret`; the injected build patches it into
     * a varargs logger that must not be handed an object as its format. */
#ifdef _MSC_VER
    ((void (__cdecl *)(void *, void *))(uintptr_t)ADDR_LOG)(vehicle, item);
#endif
}

/* RecvVehicle24 -- original 0x0045EA30, one caller. Kind 0x24, the receive
 * half of SendVehicleEnter, and it names itself: "-->Vehicle Enter Received:
 * Vehicle: %x, trooper: %x", gated on COMM_OFF_VERBOSE.
 *
 * THE SENDER SAYS `item` AND THE RECEIVER SAYS `trooper` for the same twelve
 * bytes -- the same disagreement RecvVehicleExit's header already records for
 * kind 0x25. Neither word is ours; both are kept where they are.
 *
 * The vehicle resolves through ObjByUidAlias and the passenger does NOT
 * resolve at all here: BoardVehicle takes the uid and does its own lookup,
 * which is why only one of the two is turned into a pointer. */
void __cdecl RecvVehicle24(void *msg)
{
    const AM2_VehicleMsg *m = (const AM2_VehicleMsg *)msg;
    AM2_Comm       *comm = g_commObject;
    void          *vehicle;

    if (comm->verbose)
        am2_log((const char *)(uintptr_t)"-->Vehicle Enter Received: Vehicle: %x, trooper: %x\n",
                UidOnWire(m->hdr.uid),
                UidOnWire(m->occupant));

    vehicle = ObjByUidAlias(UidOnWire(m->hdr.uid));
    if (vehicle == 0)
        return;

    /* The trooper's uid is decoded before the vehicle is pushed. */
    {
        uint32_t trooper = UidOnWire(m->occupant);
        BoardVehicle(trooper, (AM2_Object *)vehicle);
    }
}

/* VehicleUpdateApply -- original 0x0045DF10, one caller: RecvVehicle1B's
 * walk. The decode half of the vehicle delta protocol, and it names itself
 * "-->vehicleUpdateMessageUnpack: ID:%x, Change:%d%d%d, Pos (%d,%d),
 * facing:%d, gunfacing:%d, intent.facing:%d, intent.gunfacing:%d, action:%d,
 * rev:%d, half:%d, slow:%d".
 *
 * IT RETURNS THE CURSOR, which is what makes the batch loop terminate: each
 * record is self-describing, its length implied by which of the three change
 * bits are set, so the caller cannot know where the next one starts without
 * this function saying so.
 *
 * THE UID'S TOP THREE BITS ARE REPLACED, NOT MASKED OFF. The sender ORs the
 * change flags into them; this strips them with 0x1FFFFFFF and ORs the ARMY
 * in their place before looking the object up. So the same three bits carry
 * the flags on the wire and the army in memory, and a reading that only
 * masked would look up the wrong object for every army but zero.
 *
 * THAT LOG LINE NAMED FOUR FIELDS THIS PROJECT HAD ONLY NUMBERED. The action
 * byte packs a 3-bit code at +0x580 with three flags above it -- `rev` at
 * +0x584, `half` at +0x588, `slow` at +0x58C -- and those are the same three
 * the sender reads to build it. The bit positions are 7, 6 and 5, so the
 * three flags are unpacked in the opposite order from the one they are
 * printed in; taking them off the format string alone would transpose them.
 *
 * AND THE ROUND TRIP IS ASYMMETRIC AT ONE FIELD, which is recorded rather
 * than explained. The sender builds the byte from +0x544, the vehicle KIND --
 * the same field Step3Drive dispatches its engine sounds on -- and this
 * writes the low three bits to +0x580 and never touches +0x544. A remote
 * vehicle's kind is therefore never updated by an update message, which is
 * consistent with a kind that is fixed at creation, but the two ends
 * genuinely name different storage and nothing here settles why.
 *
 * NO NULL CHECK, and it is the original's. ObjByUidAlias's answer goes
 * straight into ObjClearFootprint and then into a dozen field writes with no
 * test between. Reproduced; a message naming an object this client does not
 * have would fault the same way in both. */
uint8_t *__cdecl VehicleUpdateApply(uint8_t *at, int32_t army)
{
    AM2_Object  *o;
    uint32_t  head;
    uint8_t   b0, b1, b2;

    /* Four bytes, big-endian, exactly as VehicleUpdateAppend wrote them. */
    head  = (uint32_t)*at++ << 24;
    head |= (uint32_t)*at++ << 16;
    head |= (uint32_t)*at++ << 8;
    head |= (uint32_t)*at++;

    o = (AM2_Object *)(uint8_t *)ObjByUidAlias((head & 0x1FFFFFFFu)
                                 | ((uint32_t)army << 29));
    ObjClearFootprint(o);

    if (head & 0x80000000u) {
        /* Twelve bits an axis: a low byte each, then a byte holding both
         * high nibbles -- x's in the low half, z's in the high half. */
        b0 = *at++;
        b1 = *at++;
        b2 = *at++;
        o->pos.x =
            (uint16_t)(((uint16_t)(b2 & 0x0Fu) << 8) | b0);
        o->pos.y =
            (uint16_t)(((uint16_t)(b2 & 0xF0u) << 4) | b1);

        {
            uint8_t action = *at++;

            o->gate = 1;
            o->ctl.state = (int32_t)(action & 7u);
            o->ctl.field0C = (int32_t)((uint32_t)action >> 7);
            o->ctl.stop = (int32_t)(((uint32_t)action >> 6) & 1u);
            o->ctl.approach = (int32_t)(((uint32_t)action >> 5) & 1u);
        }
    }

    if (head & 0x40000000u) {
        o->facing = *at++;
        o->turretFacing = *at++;
        o->gate = 1;
    }

    if (head & 0x20000000u) {
        o->ctl.heading = *at++;
        o->ctl.aim = *at++;
        o->gate = 1;
    }

    /* A received update always clears the destination: a remote vehicle is
     * driven by its owner's packets, not by a route of ours. */
    o->dest = g_zeroXY;
    ObjSetFootprint(o);

    if (g_commObject->verbose)
        am2_log((const char *)(uintptr_t)"-->vehicleUpdateMessageUnpack: ID:%x, Change:%d%d%d, Pos (%d,%d), facing:%d, gunfacing:%d, intent.facing:%d, intent.gunfacing:%d, action:%d, rev:%d, half:%d, slow:%d\n",
                o->uid,
                head >> 31, (head >> 30) & 1u, (head >> 29) & 1u,
                o->pos.x,
                o->pos.y,
                (uint32_t)o->facing,
                (uint32_t)o->turretFacing,
                (uint32_t)o->ctl.heading,
                (uint32_t)o->ctl.aim,
                o->ctl.state,
                o->ctl.field0C,
                o->ctl.stop,
                o->ctl.approach);

    return at;
}


/* RecvVehicle1D -- original 0x0045E630, one caller. Message kind 0x1D, and it
 * names itself: "-->Vehicle Want Item Received: Vehicle: %x, item: %x,
 * request: %d, slot: %d, quant: %d". The receive half of
 * SendVehicleWantItem, which this project named earlier today off that same
 * sender's log.
 *
 * THE REQUEST FIELD SPLITS THE FUNCTION BY ROLE, and the split is the
 * protocol. COMM_OFF_IS_HOST at +0x3D8 decides which half of the message set a
 * client will even look at: a HOST handles requests 0 and 1 -- the WANTs --
 * and returns at once on 2 and 3; a CLIENT does the opposite. So a client
 * asks, the host decides, and the host's answer comes back as a DO. Reading
 * the four arms without that gate suggests every peer runs all four.
 *
 * ONLY ONE OF THE FOUR ARMS DOES ANYTHING. Requests 1, 2 and 3 are a log
 * apiece and nothing else -- and the logger is a bare `ret` in this retail
 * image, so on this build they are empty. Request 0, WANT_PICKUP, is the
 * whole function: it grants ammo and sends the DO back.
 *
 * THE GRANT IS CLAMPED AND DEDUCTED IN ONE STEP. The asker names a quantity;
 * the host gives it `min(asked, ITEM_OFF_AMMO)` and takes exactly that much
 * off the item, so two clients racing for one pickup cannot both be paid.
 * The reply reuses SendVehicleWantItem with request 2, and THAT CALL
 * INDEPENDENTLY CONFIRMS the sender's argument identity: it pushes vehicle,
 * item, 2, slot, quant in the order orig.h records from the log's own push
 * sequence, which was derived from the other end entirely.
 *
 * The pickup is refused unless the item's army is AM2_ARMY_NEUTRAL -- an
 * item on the ground belongs to nobody, and one still owned cannot be taken.
 *
 * Every Log here carries its format string, so all of them are reproduced;
 * that is the distinction Step3Drive's header draws against the two-argument
 * call to the same address. */
void __cdecl RecvVehicle1D(void *msg)
{
    const AM2_ItemMsg *m = (const AM2_ItemMsg *)msg;
    /* The comm object and the request are read afresh at every use. */
#define comm    (g_commObject)
#define REQUEST (m->request)
    AM2_Object       *vehicle;
    AM2_Object       *item;

    if (comm->verbose)
        am2_log((const char *)(uintptr_t)"-->Vehicle Want Item Received: Vehicle: %x, item: %x, request: %d, slot: %d, quant: %d \n",
                UidOnWire(m->hdr.uid),
                UidOnWire(m->item),
                m->request,
                m->slot,
                m->quant);

    if (comm->isHost != 0) {
        if (REQUEST == 2)
            return;
        if (REQUEST == 3)
            return;
    } else {
        if (REQUEST == 0)
            return;
        if (REQUEST == 1)
            return;
    }

    vehicle = (AM2_Object *)(uint8_t *)ObjByUidAlias(UidOnWire(m->hdr.uid));
    if (vehicle == 0)
        return;

    /* The two DO arms are gated a second time, on the vehicle's own army --
     * a peer only acts on a grant it is entitled to see. */
    if ((REQUEST == 2 || REQUEST == 3)
        && !((AM2_Comm *)comm)->CommMustBroadcast(
                              (int16_t)vehicle->owner))
        return;

    item = LookupByUID(UidOnWire(m->item));
    if (item == 0)
        return;
    if (!ObjIsType4(item))
        return;

    if (REQUEST == 2) {
        if (comm->verbose)
            am2_log((const char *)(uintptr_t)"Tell pickup item received; ammo %d\n",
                    m->quant);
        return;
    }

    if (REQUEST == 3) {
        if (comm->verbose)
            am2_log((const char *)(uintptr_t)"Tell Drop item received; ammo %d\n",
                    m->quant);
        /* The answer is discarded -- the original calls it and drops it. */
        (void)((AM2_Comm *)comm)->CommMustBroadcast(
                  (int16_t)vehicle->owner);
        return;
    }

    if (REQUEST == 0) {
        if ((uint8_t)item->owner == AM2_ARMY_NEUTRAL) {
            int32_t have    = item->ammo;
            int32_t granted = m->quant;

            if (granted >= have)
                granted = have;
            item->ammo = have - granted;

            SendVehicleWantItem(vehicle, item, 2,
                                (int8_t)(uint8_t)m->slot, granted);
            am2_log((const char *)(uintptr_t)"Request pickup item received & OKed; ammo %d\n",
                    granted);
        } else {
            am2_log((const char *)(uintptr_t)"Request pickup item received & denied\n");
        }
        return;
    }

    if (REQUEST == 1) {
        if (comm->verbose)
            am2_log((const char *)(uintptr_t)"Request drop item received; ammo %d\n",
                    m->quant);
    }
#undef REQUEST
#undef comm
}

/* RecvVehicle1F -- original 0x0045E860, one caller. Message kind 0x1F, the
 * drop counterpart of RecvVehicle1D, and it names itself: "-->Vehicle Drop
 * Item Received: Vehicle: %x, item: %x, request: %d, slot: %d, quant: %d".
 *
 * IT WRITES NOTHING. Not one store to memory in 288 bytes -- measured, not
 * read off: a decoded scan of every `mov`/`or`/`and`/`add` with a memory
 * destination finds zero. Every arm is a guard or a log, and since the retail
 * build stubs the logger to a bare `ret`, the whole function is a chain of
 * lookups whose results are discarded.
 *
 * That is worth stating rather than glossing, because a handler with three
 * exits and five calls reads as though it does something. Its own text says
 * where the work went: "Weapon destroyed before dropping; but we handled it"
 * -- the drop is performed elsewhere and this message is the notification,
 * so the handler is diagnostics for a state change that has already
 * happened.
 *
 * THE MISSING-ITEM ARM IS NOT AN ERROR PATH. When the dropped item no longer
 * resolves, it checks the VEHICLE'S OWN weapon for flag 2 and reports the
 * race as handled; when it does resolve, it checks the item is a weapon and
 * reports the drop as performed. Two different questions about two different
 * objects, and reading the second lookup as a retry of the first inverts
 * which one the flag belongs to.
 *
 * All four Log calls carry format strings and are reproduced. */
void __cdecl RecvVehicle1F(void *msg)
{
    const AM2_ItemMsg *m = (const AM2_ItemMsg *)msg;
    /* The comm object is read at every use. */
#define comm (g_commObject)
    AM2_Object       *vehicle;
    void          *item;

    if (comm->verbose)
        am2_log((const char *)(uintptr_t)"-->Vehicle Drop Item Received: Vehicle: %x, item: %x, request: %d, slot: %d, quant: %d \n",
                UidOnWire(m->hdr.uid),
                UidOnWire(m->item),
                m->request,
                m->slot,
                m->quant);

    vehicle = (AM2_Object *)(uint8_t *)ObjByUidAlias(UidOnWire(m->hdr.uid));
    if (vehicle == 0)
        return;

    item = LookupByUID(UidOnWire(m->item));

    if (item == 0) {
        /* The item is already gone. Ask the VEHICLE's weapon, not the item:
         * flag 2 on it is what says the drop was dealt with locally. */
        const AM2_Object *w = WeaponByUid(
            vehicle->weaponUid);

        if (w == 0)
            return;
        if ((w->flags & 2u) == 0)
            return;
        if (comm->verbose)
            am2_log((const char *)(uintptr_t)"Weapon destroyed before dropping; but we handled it\n");
        return;
    }

    if (!ObjIsType4((const AM2_Object *)item))
        return;

    if (comm->verbose)
        am2_log((const char *)(uintptr_t)"Drop item received & performed; ammo %d\n",
                m->quant);

    if (!((AM2_Comm *)comm)->CommMustBroadcast(
                           (int16_t)vehicle->owner))
        return;

    if (comm->verbose)
        am2_log((const char *)(uintptr_t)"\tDrop Item already handled (it's my unit)\n");
#undef comm
}

/* RecvVehicle1C -- original 0x0045E980, one caller. Message kind 0x1C, the
 * receive half of SendVehicleFire, and it names itself: "Vehicle Fire Rec,
 * vehicle: %x,  gunface:%d, pos (%d,%d,%d), loctarg %x, globTarg %x".
 *
 * IT SETS THE FLAG THE SENDER CLEARS. SendVehicleFire zeroes
 * UNIT_OFF_FIRE_ACTIVE on the way out -- the send is its own
 * acknowledgement -- and this raises it on the remote copy, which is what
 * makes that peer's vehicle fire. So the flag is not state being mirrored, it
 * is a request being handed over, and the asymmetry is the mechanism rather
 * than an oversight.
 *
 * THE WIRE LAYOUT AGREES WITH THE SENDER FIELD FOR FIELD, which is worth
 * recording because the two were read from opposite ends on different days:
 * uid at +4, target at +8, the three position words at +0x0C, +0x0E and
 * +0x10, the gun facing at +0x12 and the seed at +0x14. The sender writes the
 * gun facing as a WORD and this reads a BYTE off the same offset -- the low
 * half, which is all a facing needs, and the upper byte is written and never
 * read.
 *
 * `loctarg` and `globTarg` in that message are the SAME uid printed twice --
 * once as this client resolved it and once raw off the wire. UidOnWire is the
 * identity on every drive here, so the two always agree; the message exists
 * to catch the case where they would not.
 *
 * No null check on the message's own fields, only on the resolved vehicle --
 * the original's, reproduced. */
void __cdecl RecvVehicle1C(void *msg)
{
    const AM2_VehicleFireMsg *m = (const AM2_VehicleFireMsg *)msg;
    uint32_t       vehUid    = UidOnWire(m->hdr.uid);
    uint32_t       targetUid = UidOnWire(m->target);
    AM2_Object       *veh       = (AM2_Object *)ObjByUidAlias(vehUid);

    if (veh == 0)
        return;

    veh->turretFacing    = (uint8_t)m->gun;
    veh->ctl.hit = 1;
    veh->ctl.spot.at.x      = m->x;
    veh->ctl.spot.at.y      = m->y;
    veh->ctl.spot.ground      = m->z;
    veh->ctl.uid   = targetUid;
    /* +0x529 is the spread seed; see SendVehicleFire on why it stays raw. */
    veh->fireFlag               = m->seed;

    if (g_commObject->verbose)
        am2_log((const char *)(uintptr_t)"Vehicle Fire Rec, vehicle: %x,  gunface:%d, pos (%d,%d,%d), loctarg %x, globTarg %x\n",
                vehUid,
                (uint32_t)veh->turretFacing,
                (int32_t)m->x,
                (int32_t)m->y,
                (int32_t)m->z,
                targetUid,
                m->target);
}
