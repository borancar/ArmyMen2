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

#include "commmsg.h"
#include "misc.h"      /* XorChecksum, reconstructed */
#include "rect.h"       /* AM2_Rect, for the dialog paint slot */
#include "armymsg.h"    /* SendGamePause */
#include "../inject/orig.h"
#include "crt.h"        /* am2_log */
#include "image.h"      /* AM2_IMAGE */
#include "../inject/patch.h"

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

    slot = orig_comm_slot_of_id(comm,
                                *(const int32_t *)(comm + AM2_COMM_SELF_ID));
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY_TO_LOAD) = ready;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("Setting m_ArmyReadyToLoad[%d] to %s\n",
                orig_comm_slot_of_id(comm,
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
#define orig_comm_set_army_colour \
    ((AM2_SetColourFn)(uintptr_t)ADDR_COMM_SET_ARMY_COLOUR)

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

    slot  = orig_comm_slot_of_id(comm, dpid);
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

    slot = orig_comm_slot_of_id(comm, dpid);
    if (orig_comm_set_army_colour(
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

    slot = orig_comm_slot_of_id(comm, dpid);
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
    slot = orig_comm_slot_of_id(comm,
                                *(const int32_t *)(comm + AM2_COMM_SELF_ID));
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY) = ready;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED)) {
        /* The lookup runs a second time inside the `if`, as in both
         * ready-to-load halves. */
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                orig_comm_slot_of_id(comm,
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
    slot  = orig_comm_slot_of_id(comm, dpid);
    *(int32_t *)(comm + (uint32_t)slot * COMM_ARMY_RECORD_SIZE
                 + COMM_ARMY_OFF_READY) = value;

    if (*(const int32_t *)(comm + AM2_COMM_LOG_ENABLED))
        am2_log("Setting m_ArmyReady[%d] to %s\n",
                orig_comm_slot_of_id(comm, dpid),
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

typedef void *(__cdecl *AM2_FindPlayerFn)(uint32_t id);
typedef void (__cdecl *AM2_MsgHandlerFn)(void *msg, int32_t dpid);
typedef void (__cdecl *AM2_HudMessageFn)(const char *text, int32_t colour);
typedef void (__cdecl *AM2_MenuMessageFn)(const char *text, int32_t a,
                                          int32_t b);

#define orig_find_player_by_id \
    ((AM2_FindPlayerFn)(uintptr_t)ADDR_FIND_PLAYER_BY_ID)
#define orig_recv_player_msg \
    ((AM2_MsgHandlerFn)(uintptr_t)ADDR_RECV_PLAYER_MSG)
#define orig_hud_message   ((AM2_HudMessageFn)(uintptr_t)ADDR_HUD_MESSAGE)
#define orig_menu_message  ((AM2_MenuMessageFn)(uintptr_t)ADDR_MENU_MESSAGE)

#define AM2_MSG_TYPE   0        /* the first dword: what the arm is chosen on */
#define AM2_MSG_SENDER 8        /* the chat arm reads this as a SIGNED byte */
#define AM2_MSG_TEXT   9

/* The chat arm. Which of the two message sinks it uses depends on the screen
 * that is up: the three menu screens get the menu one, everything else the
 * in-game HUD. */
static void DispatchChat(const uint8_t *msg)
{
    int32_t screen = *(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST_TAKEN;
    int32_t sender = (int32_t)*(const int8_t *)(msg + AM2_MSG_SENDER);

    if (screen == 7 || screen == 9 || screen == 8) {
        /* The original loads only DL here and pushes the whole of EDX, so the
         * top three bytes of that argument are whatever was in the register.
         * The callee reads a byte; passing the byte is the same call. */
        orig_menu_message((const char *)(msg + AM2_MSG_TEXT), sender, 1);
        return;
    }

    /* SIGNED, and then shifted left eight -- a negative sender would index
     * backwards out of the table. The original's, and the table is 256-byte
     * records of which only the first byte is read. */
    orig_hud_message((const char *)(msg + AM2_MSG_TEXT),
                     *(const uint8_t *)((uintptr_t)ADDR_CHAT_COLOUR_TABLE
                                        + (uintptr_t)(sender << 8)));
}

typedef int32_t (__attribute__((thiscall)) *AM2_GetSessionFn)(void *comm);
typedef int32_t (__cdecl *AM2_RegisterSelfFn)(uint32_t dpid);
typedef void (__cdecl *AM2_RequestStateFn)(int32_t state);

#define orig_comm_get_session \
    ((AM2_GetSessionFn)(uintptr_t)ADDR_COMM_GET_SESSION)
#define orig_comm_register_self \
    ((AM2_RegisterSelfFn)(uintptr_t)ADDR_COMM_REGISTER_SELF)
#define orig_request_state \
    ((AM2_RequestStateFn)(uintptr_t)ADDR_REQUEST_STATE)

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
        if (!orig_find_player_by_id(id) && !orig_comm_register_self(id)) {
            ok = 0;
            am2_log("FlowQ creation Failure %x\n", id);
        }
    }

    /* Checked once, after every player -- so one failure stops the game for
     * all of them. A zero player count skips this test entirely. */
    if (!ok) {
        orig_comm_get_session(comm);
        am2_log("Error in start\n");
        return;
    }

    /* Nothing in the image handles 0x0469. */
    orig_post_message(*(void **)(uintptr_t)ADDR_HWND, AM2_WM_START_GAME, 0, 0);
    SendGamePause(1, 0x10000);
    orig_request_state(2);
    *(int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE = 1;
    *(int32_t *)(uintptr_t)ADDR_NET_GAME         = 1;
    *(int32_t *)(uintptr_t)ADDR_GAME_SEED =
        *(const int32_t *)((const uint8_t *)msg + MSG_START_OFF_SEED);
}

typedef int32_t (__attribute__((thiscall)) *AM2_PlayerSlotFn)(void *comm,
                                                              int32_t dpid);
#define orig_comm_player_slot \
    ((AM2_PlayerSlotFn)(uintptr_t)ADDR_COMM_PLAYER_SLOT)

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
    int32_t        slot = orig_comm_player_slot((void *)comm, dpid);
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

typedef void (__cdecl *AM2_ArmyMsgFn)(void *msg, int32_t slot, int32_t seq);
#define orig_recv_army_msg ((AM2_ArmyMsgFn)(uintptr_t)ADDR_RECV_ARMY_MSG)

void __cdecl ReceivePacket(void *packet, int32_t dpid)
{
    uint8_t  *pkt   = (uint8_t *)packet;
    uint8_t  *comm  = (uint8_t *)kCommObj;
    int32_t   slot  = orig_comm_slot_of_id(comm, dpid);
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

        orig_recv_army_msg(p, slot, *(const int32_t *)(pkt + PACKET_OFF_SEQ));

        /* Re-read, after the handler has had the bytes. */
        part = *(const uint16_t *)p;
        *(int32_t *)(pkt + PACKET_OFF_LEN) -= part;
        p += part;
    }
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
    q = (uint8_t *)orig_find_player_by_id(
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
        orig_recv_player_msg(m, dpid);
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
        MsgSlotB0(orig_find_player_by_id((uint32_t)dpid),
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

int commmsg_install(void)
{
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
    patch_replace(ADDR_MSG_LIST_REM_HEAD, (const void *)MsgListRemHead,
                  "MsgListRemHead", 10);
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
    return 0;
}
