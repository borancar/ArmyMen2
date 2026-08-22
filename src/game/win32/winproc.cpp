/* The window procedure -- reconstructed from ArmyMen2.exe.
 *
 *   WndProc  0x0040A6B0  registered, not patched -- see winproc.h for why
 *
 * Everything Windows says to the game arrives here. The handlers fall into
 * three groups, and it is worth naming them because the shape of the original
 * only makes sense once they are separated:
 *
 *   Geometry.     WM_MOVE and WM_SIZE re-measure the client area into the
 *                 screen rectangle, so every locked-surface write stays
 *                 correctly offset after the user drags the window.
 *
 *   Surrender.    WM_PAINT, WM_ACTIVATE and WM_SYSCOMMAND all end up calling
 *                 FlipToGDISurface. An exclusive-mode DirectDraw application
 *                 owns the screen, and each of these is a moment where it has
 *                 to give it back -- to repaint, to yield to another window, to
 *                 let the screen saver or monitor power-down through.
 *
 *   Input.        WM_SETCURSOR hides the pointer, WM_CHAR forwards typed
 *                 characters, WM_ACTIVATEAPP gates the frame tick.
 *
 * The six forwarded messages are the comm subsystem's own. They are dispatched
 * through a 108-byte index table at 0x0040AF04 that maps the whole 0x400..0x46B
 * range onto just four targets -- three handlers and the default -- which is
 * how a range that looks like a hundred cases turned out to be three.
 *
 * FIDELITY NOTES -- three things below look wrong and are faithful.
 *
 *   WM_MOVE calls IsIconic and discards the result. The compiler reloads the
 *   window handle either side of it, so the call is genuinely there and its
 *   answer genuinely unused.
 *
 *   The geometry and paint handlers read the window out of the global rather
 *   than using the hWnd they were passed -- except ClientToScreen, which uses
 *   the parameter. Same window in practice; kept as written.
 *
 *   WM_PAINT calls RedrawWindow with a null window, which is the desktop, not
 *   the game window.
 */

#include "dplay.h"
#include "audio.h"
#include "winproc.h"
#include "winmain.h"
#include "mapdraw.h"
#include "../rect.h"
#include "../msgslot.h"  /* CommEndSetup, shared with both ready handlers */

#include <stdint.h>
#include <string.h>

/* Messages the original names by number. */
/* Not comm traffic, despite living in the same range: src/game/movie.cpp posts
 * these -- 0x400 when a film finishes, 0x402 when one could not be started --
 * and both mean "advance the state machine". They were named for their
 * neighbours before the movie player was read. */
/* Named from what POSTS them, which is the half of a window message that can
 * be checked. Every sender below was found by decoding forward from the
 * `push <msg>` to the PostMessageA that follows it -- and two candidates fell
 * out at that step: InitInput's `push 0x500` is DirectInput's version number
 * on its way to 0x00464410, and six `push 0x464` sites in the 0x0044E-0x00452
 * range are arguments to a CRT call. A constant that looks like a message is
 * not one.
 *
 *   0x0464  PacketThreadProc, when it has queued packets for the main thread
 *   0x046B  PacketThreadProc, when the send pool is empty
 *           -- both from the thread, which is why they are posted and not called
 *   0x046C  0x00410090 "DestroyPlayer Id=%x, to = %x", 0x00411C20
 *           "TIMING OUT PLAYER %d %s", and CommSend
 *   0x046D  0x00410090, the same DirectPlay system-message handler
 *   0x046E  the ready/end-setup handshake: 0x00410A10 "SendGameReadyMsg",
 *           0x00410B70 "ReceiveEndSetupMsg", 0x00410BB0 "ReceiveGameReadyMsg"
 *           and 0x00410CE0 "Sending EndSetupMessage"
 *   0x0500  AudioTimerProc, twice -- NOT comm traffic at all. It shared a case
 *           label with the others only because WndProc forwarded all six.
 */
/* The six now live in orig.h, because the comm side POSTS them and defining
 * them twice is how two copies of one constant come to disagree. */

static_assert(SC_SCREENSAVE == 0xF140 && SC_MONITORPOWER == 0xF170, "SC_*");
static_assert((RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
               RDW_UPDATENOW) == 0x185, "RedrawWindow flags");
static_assert(WA_INACTIVE == 0, "WA_INACTIVE");
static_assert(sizeof(PAINTSTRUCT) == 64, "the 0x40 bytes of frame it reserves");

#define g_hWnd        (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_ddraw       (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)
#define g_screenRect  (*(AM2_Rect *)(uintptr_t)ADDR_SCREEN_RECT)
#define g_windowed    (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)
#define g_lastMessage (*(const uint32_t *)(uintptr_t)ADDR_LAST_MESSAGE)
#define g_appActive   (*(int32_t *)(uintptr_t)ADDR_APP_ACTIVE)
#define g_gameState   (*(const int32_t *)(uintptr_t)ADDR_GAME_STATE)
#define g_stateArg    (*(const int32_t *)(uintptr_t)ADDR_GAME_STATE_ARG)

/* 0x0065A058. Reached as object -> table -> slot, which is the exact shape of a
 * COM call and is not one: no `this` is passed, and the argument is a RECT by
 * value that the callee pops. Slot 1 is the repaint. */
typedef void (__stdcall *am2_repaint_fn)(RECT damage);
/* Slot 2 is the odd one out: it takes the object in ecx and nothing on the
 * stack, so it is thiscall where slot 1 is stdcall. */
typedef void (__attribute__((thiscall)) *am2_paint_flush_fn)(void *obj);
#define PAINT_OFF_DAMAGE 0x14u
typedef struct { am2_repaint_fn *slots; } AM2_PaintObject;
#define g_paintObject (*(AM2_PaintObject **)(uintptr_t)ADDR_PAINT_OBJECT)

/* 0x00486550. Records of three dwords, the first a function to run for the
 * current state. Indexed by whatever 0x0042E5D0 answers. */
typedef void (__cdecl *am2_state_fn)(void);
typedef struct { am2_state_fn fn; uint32_t rest[2]; } AM2_StateEntry;
#define g_stateDispatch ((const AM2_StateEntry *)(uintptr_t)ADDR_STATE_DISPATCH)

/* 0x005125B8. Null until the game installs a keyboard consumer. */
typedef void (__cdecl *am2_char_fn)(uint32_t ch, uint32_t lo, uint32_t hi);
#define g_charHandler (*(am2_char_fn *)(uintptr_t)ADDR_CHAR_HANDLER)

typedef void    (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_int_fn)(void);
typedef void    (__cdecl *am2_int_arg_fn)(int32_t);

#define orig_on_app_activated  (*(am2_void_fn)ADDR_ON_APP_ACTIVATED)
#define orig_current_state     (*(am2_int_fn)ADDR_CURRENT_STATE)
#define orig_state_leave       (*(am2_void_fn)ADDR_STATE_LEAVE)
#define orig_request_state       (*(am2_int_arg_fn)ADDR_REQUEST_STATE)

/* ---- comm traffic -------------------------------------------------------
 *
 * Six messages the DirectPlay callbacks post to the window: a player left, the
 * host migrated, the session ended, the message list has work, the send pool is
 * empty, and the audio stream should stop. WndProc used to hand all six back to
 * the original, which was cheap and correct and meant six handlers stayed
 * outside the reconstruction.
 *
 * They are game logic rather than boundary code -- the boundary-shaped calls
 * inside them (the session pair, StopAudioStream, PlayDynamicSound) were
 * already ours. What porting them buys is that WndProc no longer needs the
 * original image to be present, which is the direction the port is going.
 *
 * Only 0x0500 runs in a single-player session -- OpenAudioStream and InitInput
 * both post it -- so it is the only one the ordinary A/B covers. The other
 * five need a live DirectPlay session with a second player.
 */
#define g_commObject   (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_eventFlags   (*(uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS)
#define g_aiControlled (*(int32_t *)(uintptr_t)ADDR_AI_CONTROLLED)
/* Becoming the host is written into the same global that says whether
 * this is a multiplayer session at all -- see ADDR_MP_SESSION. */
#define g_hostChanged  (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)
#define g_netGame  (*(int32_t *)(uintptr_t)ADDR_NET_GAME)
#define g_gameOverFlags (*(const uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS)
#define g_hudColour    (*(const uint8_t *)(uintptr_t)ADDR_HUD_MESSAGE_COLOUR)
#define g_ourSlot     (*(const int32_t *)(uintptr_t)ADDR_OUR_SLOT)
#define g_overlayDirty (*(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY)
#define g_reqTaken     (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_TAKEN)

typedef int32_t (__attribute__((thiscall)) *am2_comm_id_fn)(void *comm,
                                                            uint32_t id);
typedef void    (__attribute__((thiscall)) *am2_comm_void_fn)(void *comm);
typedef int32_t (__attribute__((thiscall)) *am2_comm_sess_fn)(void *comm,
                                                              void *desc,
                                                              int32_t flags);
typedef void (__cdecl *am2_log_fn)(const char *fmt, ...);
typedef int32_t (__cdecl *am2_sprintf_fn)(char *out, const char *fmt, ...);
typedef void (__cdecl *am2_str_int_fn)(const char *text, int32_t arg);
typedef void (__cdecl *am2_str_int2_fn)(const char *text, int32_t a, int32_t b);
typedef void (__cdecl *am2_int_fn2)(int32_t arg);
typedef void (__cdecl *am2_void_fn2)(void);
typedef void (__cdecl *am2_drop_fn)(int32_t slot, const void *what);
typedef void (__cdecl *am2_sound_fn)(const char *name, int32_t loop, int32_t a,
                                     int32_t x, int32_t y, int32_t slot,
                                     int32_t pri, int32_t owner);

#define orig_log            (*(am2_log_fn)ADDR_LOG)
#define orig_sprintf        (*(am2_sprintf_fn)ADDR_GAME_SPRINTF)
#define orig_drain_msgs     (*(am2_void_fn2)ADDR_COMM_DRAIN_MSGS)
#define orig_no_buffers     (*(am2_void_fn2)ADDR_COMM_NO_BUFFERS)
#define orig_player_slot    (*(am2_comm_id_fn)ADDR_COMM_PLAYER_SLOT)
#define orig_find_player    (*(am2_comm_id_fn)ADDR_COMM_FIND_PLAYER)
#define orig_remove_player_rec (*(am2_comm_id_fn)ADDR_COMM_REMOVE_PLAYER)
#define orig_player_left    (*(am2_comm_id_fn)ADDR_COMM_PLAYER_LEFT)
#define orig_send_players   (*(am2_int_fn2)ADDR_COMM_SEND_PLAYERS)
#define orig_session_over   (*(am2_comm_void_fn)ADDR_COMM_SESSION_OVER)
#define orig_comm_reset     (*(am2_comm_void_fn)ADDR_COMM_RESET_STATE)
#define orig_remove_player  (*(am2_int_fn2)ADDR_REMOVE_PLAYER)
#define orig_show_mp_result (*(am2_int_fn2)ADDR_SHOW_MP_RESULT)
#define orig_set_ai         (*(am2_int_fn2)ADDR_SET_AI_CONTROL)
#define orig_lobby_reset    (*(am2_void_fn2)ADDR_LOBBY_RESET)
#define orig_hud_message    (*(am2_str_int_fn)ADDR_HUD_MESSAGE)
#define orig_menu_message   (*(am2_str_int2_fn)ADDR_MENU_MESSAGE)
#define orig_chat_append    (*(am2_str_int_fn)ADDR_CHAT_APPEND)
#define orig_sprite_drop    (*(am2_drop_fn)ADDR_SPRITE_DROP_NAMED)

/* The player's name is the first field of its record. */
static const char *PlayerName(uint8_t *comm, int32_t slot)
{
    return (const char *)(comm + COMM_OFF_PLAYERS
                          + (uint32_t)slot * COMM_PLAYER_STRIDE);
}

/* Ask DirectPlay for the session description again and clear the two bits that
 * close it, so a replacement player can still join. Both halves are ours. */
static void ReopenSession(uint8_t *comm)
{
    void *desc;

    CommGetSessionDesc(comm);
    desc = *(void **)(comm + COMM_OFF_SESSION_DESC);
    if (!desc)
        return;
    *(uint32_t *)((uint8_t *)desc + 4) &= ~0x21u;
    if (CommSetSessionDesc(comm, desc, 0) < 0)
        orig_log((const char *)(uintptr_t)ADDR_STR_SET_SESSION_FAIL);
}

/* 0x0046C. A player record was destroyed. Three outcomes: in a menu it is a
 * lobby update, in a mission it is either us leaving or somebody else being
 * handed to the AI. */
static LRESULT OnPlayerDestroyed(WPARAM wParam)
{
    uint8_t *comm = g_commObject;
    uint32_t id   = (uint32_t)wParam;
    int32_t  slot;
    char     text[128];

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_DESTROYPLAYER, id);

    if (!id || id == 0xFFFFFFFFu)
        return 1;
    if (orig_find_player(comm, id) == -1)
        return 1;

    /* Each player owns three bits; drop them all. */
    g_eventFlags &= ~(0x20810u << orig_player_slot(comm, id));

    if (g_gameState == 1 || (g_gameState == 2 && g_reqTaken == 0x22)) {
        /* Lobby: copy the name out before the record is recycled. */
        char name[128];

        slot = orig_player_slot(comm, id);
        strcpy(name, PlayerName(comm, slot));
        ((char *)PlayerName(comm, slot))[0] = '\0';

        {
            int32_t removed = orig_remove_player_rec(comm, id);

            orig_remove_player(id);
            if (removed && *(const int32_t *)(comm + COMM_OFF_IS_HOST))
                orig_send_players(1);
        }

        if (g_paintObject) {
            uint8_t     *obj = (uint8_t *)g_paintObject;
            void *const *vt  = *(void *const **)obj;

            ((am2_paint_flush_fn)vt[2])(obj);
            ((am2_repaint_fn)vt[1])(*(const RECT *)(obj + PAINT_OFF_DAMAGE));
            orig_sprintf(text, (const char *)(uintptr_t)ADDR_STR_LEFT_GAME, name);
            orig_menu_message(text, 4, 1);
        }

        if (*(const uint32_t *)(comm + COMM_OFF_PLAYER_COUNT) < 4
            && *(const int32_t *)(comm + COMM_OFF_IS_HOST))
            ReopenSession(comm);
        return 1;
    }

    if (id == *(const uint32_t *)(comm + COMM_OFF_PLAYER_MADE)) {
        /* It was us. Leave and show the multiplayer result screen. */
        orig_player_left(comm, id);
        orig_remove_player(id);
        orig_show_mp_result(2);
        return 1;
    }

    slot = orig_player_slot(comm, id);
    if (orig_player_left(comm, id) && g_gameState == 2 && g_netGame
        && *(const int32_t *)(comm + COMM_OFF_IS_HOST))
        CommEndSetup();
    orig_remove_player(id);

    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        orig_sprite_drop(slot, (const void *)(uintptr_t)ADDR_MP_LEAVE_SPRITE);

    orig_sprintf(text, (const char *)(uintptr_t)ADDR_STR_LEFT_AI,
                 PlayerName(comm, slot));
    orig_hud_message(text, g_hudColour);
    return 1;
}

/* 0x0046D. The host left and we are the new one. */
static LRESULT OnHostMigrated(void)
{
    uint8_t *comm = g_commObject;
    char     text[128];

    *(int32_t *)(comm + COMM_OFF_IS_HOST) = 1;
    g_hostChanged = 1;
    if (g_gameState == 1) {
        g_reqTaken     = 7;
        g_overlayDirty = 1;
    }

    if (*(const uint32_t *)(comm + COMM_OFF_PLAYER_COUNT) < 4)
        ReopenSession(comm);

    orig_sprintf(text, (const char *)(uintptr_t)ADDR_STR_HOST_NOW,
                 PlayerName(comm, g_ourSlot));
    if (g_gameState == 2)
        orig_hud_message(text, g_hudColour);
    else
        orig_menu_message(text, 4, 1);
    orig_chat_append(text, 1);
    return 1;
}

/* 0x0046E. The ready/end-setup handshake finished. */
static LRESULT OnSetupDone(void)
{
    uint8_t *comm = g_commObject;

    g_netGame = 0;
    orig_set_ai((int32_t)((g_gameOverFlags >> 18) & 1u));
    PlayDynamicSound((const char *)(uintptr_t)ADDR_STR_ALLRIGHT_WAV,
                      0, 0, 0, 0, 0, 3, 0);
    orig_lobby_reset();
    orig_comm_reset(comm);
    orig_session_over(comm);
    return 1;
}

/* Re-measure the drawing rectangle after the window has moved or resized.
 * Windowed asks the window; fullscreen assumes the whole screen. */
static void RemeasureScreenRect(HWND hWnd)
{
    if (g_hWnd) {
        /* Answer discarded -- see the fidelity note. */
        IsIconic(g_hWnd);
        if (g_hWnd && g_windowed) {
            GetClientRect(g_hWnd, (LPRECT)&g_screenRect);
            ClientToScreen(hWnd, (LPPOINT)&g_screenRect.left);
            ClientToScreen(hWnd, (LPPOINT)&g_screenRect.right);
            return;
        }
    }
    SetRect((LPRECT)&g_screenRect, 0, 0,
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

/* Hand the screen back to GDI. Everything that interrupts an exclusive-mode
 * DirectDraw application funnels through here. */
static void FlipToGDI(void)
{
    if (g_ddraw)
        IDirectDraw_FlipToGDISurface(g_ddraw);
}

static LRESULT OnPaint(void)
{
    LPDIRECTDRAWSURFACE gdi;
    PAINTSTRUCT         ps;
    RECT                damage;

    IDirectDraw_FlipToGDISurface(g_ddraw);
    if (IDirectDraw_GetGDISurface(g_ddraw, &gdi) != DD_OK)
        return 1;
    SetDrawTarget(gdi);

    if (!GetUpdateRect(g_hWnd, &damage, TRUE))
        return 1;
    RedrawWindow(NULL, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    BeginPaint(g_hWnd, &ps);
    g_paintObject->slots[1](damage);
    EndPaint(g_hWnd, &ps);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {

    case WM_DESTROY:
        ShutdownSubsystems();
        PostQuitMessage(0);
        break;

    case WM_MOVE:
    case WM_SIZE:
        RemeasureScreenRect(hWnd);
        break;

    case WM_ACTIVATE:
        /* Only the low half is the activation state. Losing activation is the
         * cue to release the display; gaining it needs nothing. */
        if (LOWORD(wParam) != WA_INACTIVE)
            break;
        if (!g_ddraw)
            break;
        FlipToGDI();
        break;

    case WM_PAINT:
        if (!g_ddraw || !g_paintObject)
            break;
        return OnPaint();

    case WM_ACTIVATEAPP:
        /* The frame tick reads this global and does nothing while it is clear,
         * so this is what stops the game simulating in the background. */
        g_appActive = (int32_t)wParam;
        if (!wParam)
            break;
        orig_on_app_activated();
        break;

    case WM_SETCURSOR:
        /* The game draws its own pointer into the surface. */
        SetCursor(NULL);
        return 1;

    case WM_CHAR:
        /* PumpMessage records the last message it dispatched, so a WM_CHAR
         * arriving twice for one keystroke is recognised and dropped. */
        if (g_lastMessage == uMsg)
            return 0;
        if (!g_charHandler)
            break;
        g_charHandler((uint32_t)wParam, (uint32_t)lParam & 0xFFFFu,
                      (uint32_t)lParam >> 16);
        return 0;

    case WM_SYSCOMMAND:
        /* Swallow the screen saver outright; give the display back before
         * letting a monitor power-down through. */
        if (wParam == SC_SCREENSAVE)
            return 1;
        if (wParam != SC_MONITORPOWER)
            break;
        if (!g_ddraw)
            break;
        FlipToGDI();
        break;

    case AM2_WM_STATE_ADVANCE:
    case AM2_WM_STATE_ABORT:
        if (g_gameState == 0)
            g_stateDispatch[orig_current_state()].fn();
        else {
            orig_state_leave();
            orig_request_state(g_stateArg);
        }
        break;

    case AM2_WM_PACKETS_READY:
        orig_drain_msgs();
        return 1;

    case AM2_WM_NO_BUFFERS:
        orig_no_buffers();
        return 1;

    case AM2_WM_PLAYER_GONE:
        return OnPlayerDestroyed(wParam);

    case AM2_WM_HOST_CHANGED:
        return OnHostMigrated();

    case AM2_WM_SETUP_DONE:
        return OnSetupDone();

    case AM2_WM_STREAM_DONE:
        /* AudioTimerProc posts this when a stream runs out. Stop it and then
         * let DefWindowProcA have the message, which is what the original
         * does -- it falls through rather than returning. */
        StopAudioStream();
        break;

    default:
        break;
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}
