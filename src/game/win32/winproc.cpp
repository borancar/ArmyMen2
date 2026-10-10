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
#include "movie.h"    /* StateLeave -- reconstructed */
#include "audio.h"
#include "winproc.h"
#include "../item.h"   /* ObjToAI -- the walker's callback */
#include "winmain.h"
#include "mapdraw.h"
#include "../commmsg.h"  /* CommFindPlayer -- reconstructed */
#include "../rect.h"
#include "../msgslot.h"  /* CommEndSetup, shared with both ready handlers */
#include "../misc.h"     /* ObjVPaint / ObjVUpdate */
#include "../gameproc.h" /* GameOverState indexes the dispatch table */
#include "../army.h"     /* ForEachArmyObject, and the callback is not a sprite */
#include "../air.h"    /* SetFogOfWar */
#include "frame.h"    /* ShowMpResult */
#include "widget.h"   /* OnAppActivated */

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

#define g_hWnd        (*(HWND *)&g_enumContext)

/* 0x0065A058, the screen object: its vtable's slot 1 paints a rectangle
 * (thiscall, the RECT by value) and slot 2 updates. Both are called through
 * misc.h's ObjVPaint / ObjVUpdate. */
#define PAINT_OFF_DAMAGE 0x14u
typedef struct { void *const *slots; } AM2_PaintObject;

/* 0x00486550. Records of three dwords, the first a function to run for the
 * current state. Indexed by whatever 0x0042E5D0 answers. */
typedef void (__cdecl *am2_state_fn)(void);
/* AM2_StateAction is orig.h's, so frame.cpp and this file share one layout.
 * It used to be based one column late here -- see ADDR_STATE_ACTIONS -- which
 * read correctly only because `fn` was the single field anything touched. */

/* 0x005125B8. Null until the game installs a keyboard consumer. */
typedef void (__cdecl *am2_char_fn)(uint32_t ch, uint32_t lo, uint32_t hi);

typedef void    (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_int_fn)(void);
typedef void    (__cdecl *am2_int_arg_fn)(int32_t);


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
/* The multiplayer-session flag, and it is spelled that way here because that
 * is what it IS -- OnHostChanged writing 1 into it is not a second meaning,
 * it is a machine that has just become the host asserting there is a session.
 * It was g_hostChanged, which read as a different global; commmsg.cpp's
 * CommMustBroadcast needed the same address under the honest name and
 * checkglobals refused to let both exist. */

typedef void    (__attribute__((thiscall)) *am2_comm_void_fn)(void *comm);
typedef void (__cdecl *am2_log_fn)(const char *fmt, ...);
typedef int32_t (__cdecl *am2_sprintf_fn)(char *out, const char *fmt, ...);
typedef void (__cdecl *am2_str_int_fn)(const char *text, int32_t arg);
typedef void (__cdecl *am2_str_int2_fn)(const char *text, int32_t a, int32_t b);
typedef void (__cdecl *am2_int_fn2)(int32_t arg);
typedef void (__cdecl *am2_void_fn2)(void);
typedef void (__cdecl *am2_sound_fn)(const char *name, int32_t loop, int32_t a,
                                     int32_t x, int32_t y, int32_t slot,
                                     int32_t pri, int32_t owner);

#define orig_log            (*(am2_log_fn)ADDR_LOG)
#define orig_sprintf        (*(am2_sprintf_fn)ADDR_GAME_SPRINTF)

/* The player's name is the first field of its record. */
#if defined(_MSC_VER) && !defined(__GNUC__)
#define AM2_WP_INLINE static __forceinline
#else
#define AM2_WP_INLINE static inline
#endif
static inline const char *PlayerName(AM2_Comm *comm, int32_t slot)
{
    return comm->slots[(uint32_t)slot].name;
}

/* Ask DirectPlay for the session description again and clear the two bits that
 * close it, so a replacement player can still join. Both halves are ours. */
AM2_WP_INLINE void ReopenSession(void)
{
    void *desc;

    CommGetSessionDesc(g_commObject);
    desc = g_commObject->sessionDesc;
    if (!desc)
        return;
    ((LPDPSESSIONDESC2)desc)->dwFlags &= ~0x21u;
    if (((AM2_Comm *)g_commObject)->CommSetSessionDesc(desc, 0) < 0)
        orig_log("Set Session Failed to reopen Session\n");
}

/* 0x0046C. A player record was destroyed. Three outcomes: in a menu it is a
 * lobby update, in a mission it is either us leaving or somebody else being
 * handed to the AI. */
AM2_WP_INLINE LRESULT OnPlayerDestroyed(WPARAM wParam)
{
    /* The comm object is read from its global at every use. */
    uint32_t id   = (uint32_t)wParam;
    int32_t  slot;
    char     text[128];

    if (g_commObject->verbose)
        orig_log("DESTROYPLAYER Win Message for player %x\n", id);

    if (!id || id == 0xFFFFFFFFu)
        return 1;
    if (CommFindPlayer(g_commObject, id) == -1)
        return 1;

    /* Each player owns three bits; drop them all. */
    g_eventFlags &= ~(0x20810u << ((AM2_Comm *)g_commObject)->CommPlayerSlot(id));

    if (g_gameState == 1 || (g_gameState == 2 && g_subState == 0x22)) {
        /* Lobby: copy the name out before the record is recycled. */
        char name[100];

        slot = ((AM2_Comm *)g_commObject)->CommPlayerSlot(id);
        strcpy(name, PlayerName(g_commObject, slot));
        ((char *)PlayerName(g_commObject, slot))[0] = '\0';

        {
            int32_t removed = ((AM2_Comm *)g_commObject)->CommRemovePlayer(id);

            DestroyFlow(id);
            if (removed && g_commObject->isHost)
                SendPlayerMsg(1);
        }

        if ((*(AM2_PaintObject * *)&g_paintObject)) {
            /* Slot 2 (update) then slot 1 (paint the damage rectangle), both
             * thiscall on the object, which the original re-reads from the
             * global for the second (`mov ecx, [0x0065A058]` at 0x0040AC73).
             * The paint was once called with no `this` at all, from a reading
             * that missed that load. */
            ObjVUpdate((*(AM2_PaintObject * *)&g_paintObject));
            {
                uint8_t *obj = (uint8_t *)(*(AM2_PaintObject * *)&g_paintObject);

                ObjVPaint(obj, *(const AM2_Rect *)(obj + PAINT_OFF_DAMAGE));
            }
            orig_sprintf(text, "Player %s has left the game.", name);
            MenuMessage(text, 4, 1);
        }

        if ((uint32_t)g_commObject->playerCount < 4
            && g_commObject->isHost)
            ReopenSession();
        return 1;
    }

    if (id != (uint32_t)g_commObject->connected) {
        int32_t left;

        slot = ((AM2_Comm *)g_commObject)->CommPlayerSlot(id);
        /* The flow goes BEFORE the setup is ended: the departure's result is
         * held across DestroyFlow and tested after it. */
        left = ((AM2_Comm *)g_commObject)->CommPlayerLeft(id);
        DestroyFlow(id);
        if (left && g_gameState == 2 && g_netGame
            && g_commObject->isHost)
            CommEndSetup();

        if (g_commObject->isHost)
            /* Not a sprite: hand the departed player's units to the AI. Both
             * halves are ours now -- the walk and the callback -- so the callback
             * goes in by NAME. It was ADDR_OBJ_TO_AI cast to a pointer while it
             * was still the original's, which is exactly the fourth spelling of a
             * seam checkseams was taught to catch: nothing on that line looked
             * like a call. See army.h. */
            ForEachArmyObject(slot, ObjToAI);

        orig_sprintf(text, "Player %s has left the game - now AI controlled.",
                     PlayerName(g_commObject, slot));
        HudMessage(text, g_hudColour);
        return 1;
    }

    /* It was us. Leave and show the multiplayer result screen. */
    ((AM2_Comm *)g_commObject)->CommPlayerLeft(id);
    DestroyFlow(id);
    ShowMpResult(2);
    return 1;
}

/* 0x0046D. The host left and we are the new one. */
AM2_WP_INLINE LRESULT OnHostMigrated(void)
{
    /* The comm object is read from its global at every use. */
    char     text[128];

    g_commObject->isHost = 1;
    g_mpSession = 1;
    if (g_gameState == 1) {
        g_subState     = 7;
        g_overlayDirty = 1;
    }

    if ((uint32_t)g_commObject->playerCount < 4)
        ReopenSession();

    orig_sprintf(text, "Player %s is now the host.",
                 PlayerName(g_commObject, g_ourSlot));
    if (g_gameState == 2)
        HudMessage(text, g_hudColour);
    else
        MenuMessage(text, 4, 1);
    SendChatMsg(text, 1);
    return 1;
}

/* 0x0046E. The ready/end-setup handshake finished. */
AM2_WP_INLINE LRESULT OnSetupDone(void)
{
    /* The comm object is read from its global at every use. */

    g_netGame = 0;
    SetFogOfWar((int32_t)((g_gameOverFlags >> 18) & 1u));
    PlayDynamicSound("AllRight.wav",
                      0, 0, 0, 0, 0, 3, 0);
    BuildHudWidgets();
    CommResetStats(g_commObject);
    CommSessionOver(g_commObject);
    return 1;
}

/* Re-measure the drawing rectangle after the window has moved or resized.
 * Windowed asks the window; fullscreen assumes the whole screen. */
static inline void RemeasureScreenRect(HWND hWnd)
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
static inline void FlipToGDI(void)
{
    if (g_ddraw)
        IDirectDraw_FlipToGDISurface(g_ddraw);
}

static inline LRESULT OnPaint(void)
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
    /* thiscall on the screen object (`mov ecx, [0x0065A058]` at 0x0040A86B). */
    ObjVPaint((*(AM2_PaintObject * *)&g_paintObject), *(const AM2_Rect *)&damage);
    EndPaint(g_hWnd, &ps);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {

    case WM_ACTIVATE:
        /* Only the low half is the activation state. Losing activation is the
         * cue to release the display; gaining it needs nothing. */
        if (LOWORD(wParam) != WA_INACTIVE)
            break;
        if (!g_ddraw)
            break;
        FlipToGDI();
        break;

    case WM_MOVE:
    case WM_SIZE:
        RemeasureScreenRect(hWnd);
        break;

    case WM_DESTROY:
        ShutdownSubsystems();
        PostQuitMessage(0);
        break;

    case WM_PAINT:
        if (!g_ddraw || !(*(AM2_PaintObject * *)&g_paintObject))
            break;
        return OnPaint();

    case WM_CHAR:
        /* PumpMessage records the last message it dispatched, so a WM_CHAR
         * arriving twice for one keystroke is recognised and dropped. */
        if (g_lastMessage == uMsg)
            return 0;
        if (!(*(am2_char_fn *)&g_charHandler))
            break;
        (*(am2_char_fn *)&g_charHandler)((uint32_t)wParam, (uint32_t)lParam & 0xFFFFu,
                      (uint32_t)lParam >> 16);
        return 0;

    case WM_SETCURSOR:
        /* The game draws its own pointer into the surface. */
        SetCursor(NULL);
        return 1;

    case WM_ACTIVATEAPP:
        /* The frame tick reads this global and does nothing while it is clear,
         * so this is what stops the game simulating in the background. */
        g_appActive = (int32_t)wParam;
        if (!wParam)
            break;
        OnAppActivated();
        break;

    case WM_SYSCOMMAND:
        /* Swallow the screen saver outright; give the display back before
         * letting a monitor power-down through. */
        switch (wParam) {
        case SC_SCREENSAVE:
            return 1;
        case SC_MONITORPOWER:
            if (!g_ddraw)
                break;
            FlipToGDI();
            break;
        }
        break;

    case AM2_WM_STATE_ADVANCE:
    case AM2_WM_STATE_ABORT:
        if (g_gameState == 0)
            g_stateActions[GameOverState()].onMessage();
        else {
            StateLeave();
            RequestState(g_stateArg);
        }
        break;

    case AM2_WM_PACKETS_READY:
        CommDrainMsgs();
        return 1;

    case AM2_WM_NO_BUFFERS:
        CommNoBuffers();
        return 1;

    case AM2_WM_PLAYER_GONE:
        return OnPlayerDestroyed(wParam);

    case AM2_WM_STREAM_DONE:
        /* AudioTimerProc posts this when a stream runs out. Stop it and then
         * let DefWindowProcA have the message, which is what the original
         * does -- it falls through rather than returning. */
        StopAudioStream();
        break;
    default:
        break;

    case AM2_WM_SETUP_DONE:
        return OnSetupDone();

    case AM2_WM_HOST_CHANGED:
        return OnHostMigrated();
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}
