/* frame.cpp -- see frame.h. */
#include "../maprow.h"  /* FreeSeqContexts -- reconstructed */
#include "../msgslot.h"
#include "../commmsg.h"  /* CommDrainMsgs -- reconstructed */
#include "../../inject/win32.h"

#include <stdint.h>
#include <stdlib.h>   /* abs */

#include "movie.h"   /* MovieStepCurrent, states 0 and 3 */
#include "frame.h"
#include "../armymsg.h"  /* SendGamePause -- reconstructed */
#include "device.h"
#include "mapdraw.h"
#include "palette.h"
#include "font.h"     /* BuildFontAlias -- reconstructed */
#include "audio.h"    /* StartAudioStream -- reconstructed */
#include "../gamedir.h" /* SetGameDir -- reconstructed */
#include "surface.h"
#include "../../inject/orig.h"
#include "../../inject/patch.h"
#include "../gameproc.h"  /* RequestState -- reconstructed */
#include "../misc.h"
#include "../region.h"   /* BuildRegionGraph -- reconstructed */
#include "../definfo.h"  /* LoadDefTables -- reconstructed */
#include "../item.h"     /* SelectInventorySlot, DeselectAll */
#include "../objtype.h"  /* LookupType3ByUID -- reconstructed */
#include "../objtable.h" /* SelectUnit -- reconstructed */
/* Reconstructed in item.cpp and declared there rather than in item.h. */
void __cdecl SelectInventorySlot(AM2_Object *unit, int32_t slot);
#include "../army.h"     /* LookupOwnerObj -- reconstructed */
#include "../gameproc.h" /* LoadDefTables, LoadGame, OpenSaveForLoad */      /* IsKeyDown, KeyChanged -- reconstructed */
#include "../crt.h"      /* am2_sprintf -- the game's own */
#include "../text.h"     /* DrawText -- reconstructed, and NOT winuser's */
#include "../map.h"      /* ScriptListFind */
#include "sprite.h"       /* DrawSprite -- reconstructed */
#include "../image.h"     /* AM2_IMAGE */
#include "../gamedir.h"   /* SetGameDir -- reconstructed */
#include "winmain.h"      /* Ticks -- reconstructed */
#include "audio.h"       /* PlayDynamicSound, StopNamedSound */
#include "../script.h"   /* ReadScript -- reconstructed */
#include "../event.h"    /* MissionStartup, TimerTick */
#include "../air.h"      /* ClearFrameCounts */
#include "widget.h"      /* HudUpdate, HudPaint */
#include "../item.h"     /* ObjFrameSweep and the per-frame steps */
#include "dplay.h"       /* CommNoBuffers */
#include "../objscript.h" /* FreeObjScripts -- LevelTeardown */
#include "../pad.h"       /* ResetPads */
#include "../anim.h"      /* the four Free*Anims */
#include "../map.h"       /* FreeScenarios, FreeMapLayers */

#ifdef AM2_STANDALONE
/* Runtime-written .data-init game scalars read across the win32 layer: the
 * 640x480 screen dimensions, the fog-of-war flag (cheat-toggled), and the
 * info-overlay flag. Non-const; initial bytes byte-checked, plain linkage to
 * match standalone.h's shared declarations. */
int32_t am2_screen_w = 640;
int32_t am2_screen_h = 480;
int32_t am2_fog_of_war = 1;
int32_t am2_info_overlay_on = 1;
#endif

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_void_fn)(void);
typedef void    (__cdecl *am2_i32_fn)(int32_t);
typedef void    (__cdecl *am2_i32x2_fn)(int32_t, int32_t);
typedef int32_t (__cdecl *am2_int_fn)(void);
typedef int32_t (__cdecl *am2_list_fn)(void *list);

#define call0(a)  (((am2_void_fn)(uintptr_t)(a))())


typedef void    (__cdecl *AM2_NoArgFn)(void);
/* The image's logger reached with NOTHING pushed. Declared up here because
 * State2Enter needs it as well as TakeMenuRequest. */
#define orig_log_noargs   ((AM2_NoArgFn)(uintptr_t)ADDR_LOG)
typedef int32_t (__cdecl *AM2_ActionKeyFn)(int32_t action);
typedef void    (__cdecl *AM2_VoidFn0b)(void);
typedef int32_t (__cdecl *AM2_EventFlag8Fn)(void);
/* The camera pair, reached by cast for the same reason mapdraw.cpp does it:
 * audio.cpp already owns the g_ name on ADDR_LISTENER_POS. */
/* Spelled exactly as surface.cpp spells them; checkglobals enforces that. */
#define VIEW_TARGET (&g_viewTarget)
#define VIEW_EYE2   ((const AM2_Point *)&g_listenerPos)


/* 0x00424CA0, one caller -- the per-frame path. In-mission input: three
 * separate jobs that share a function because they share the mouse.
 *
 * ESCAPE first, tested RELEASED as everywhere else here, which asks for the
 * escape menu and marks the overlay dirty.
 *
 * Then the info bitmap. The action is 0x14 and the Boot Camp dialog names it
 * on screen -- "HIT F1 DURING GAME FOR MORE INFO". Outside a network game it
 * loads the level's bitmap, puts the sub-state into 0x16, pauses, and plays the
 * level's sound if it has one; inside a network game it calls
 * ADDR_SHOW_INFO_MP instead and pauses nothing, since one player must not stop
 * everyone else's clock.
 *
 * Then dismissal and scrolling, and both give way to whoever owns the input:
 * if g_charHandler is installed or ADDR_INPUT_SUPPRESS is set, this returns
 * without reading the mouse at all.
 *
 * The edge scroll is the interesting half. Within three pixels of any edge it
 * moves ADDR_VIEW_TARGET by `speed * frame delta in seconds` -- the same step
 * ViewUpdate glides the eye with, so the two agree by construction -- and
 * clears ADDR_OBJ_CTX_SET, which is what stops the camera snapping back to the
 * followed object the moment you scroll away from it.
 *
 * Note the asymmetry in what each edge reads: the right and bottom tests take
 * the eye's coordinate and ADD, while the left and top take the same
 * coordinate and SUBTRACT, so all four scroll from the EYE rather than from the
 * current target. A held scroll therefore advances one step per frame from
 * where the view actually is, not from where it was already heading. */
void __cdecl MissionInput(void)
{
    int32_t step;

    if (!IsKeyDown(AM2_DIK_ESCAPE) && KeyChanged(AM2_DIK_ESCAPE)) {
        g_overlayDirty = 1;
        g_subState = AM2_STATE_ESCAPE_MENU;
        return;
    }

    if (!g_mpSession) {
        if (ActionKeyReleased(AM2_ACTION_SHOW_INFO)) {
            if (g_levelStrC) {
                SetGameDir("bitmaps");
                FreeBitmap(&g_currentBitmap);
                g_currentBitmap = LoadBitmap(
                    &g_levelStrC, 0);
                g_subState =
                    AM2_SUBSTATE_INFO_BITMAP;
                g_overlayDirty = 1;
                SendGamePause(1, 8);
            }
            if (g_levelSoundName)
                /* Slot 0 at priority 1 -- the original's order; this once
                 * had the two swapped. */
                PlayDynamicSound(
                    &g_levelSoundName,
                    0, 0, 0, 0, 0, 1, 0);
            LatchKeyState();
        }
    } else if (ActionKeyReleased(AM2_ACTION_SHOW_INFO)) {
        ShowInfoMp();
    }

    if (g_charHandler)
        return;
    if (g_inputSuppress)
        return;

    /* flag8 OR (button AND changed) -- the mouse pair is an AND, which is what
     * makes this "a click just happened" rather than "the mouse did anything".
     * Getting it wrong dismisses the sign on the first mouse MOVE. */
    if (g_currentBitmap
        && (DismissKeyReleased()
            || (g_mouseButton
                && g_mouseChanged))) {
        g_mouseClaimed = 1;
        FreeBitmap(&g_currentBitmap);
        if (g_levelSoundName)
            StopNamedSound(&g_levelSoundName, 0);
    }

    /* Both set means give up: something is being followed AND the button is
     * held, so a drag must not fight the follow. Not `!button` -- that
     * inversion scrolls exactly when it should not, and is what the frame
     * counts caught. */
    if (g_objCtxValA
        && g_mouseButton)
        return;

    step = (int32_t)(g_viewSpeed
                     * g_frameDeltaSec);

#define cx (*(const int16_t *)&g_cursorPoint)
#define cy (*(const int16_t *)(((uintptr_t)&g_cursorPoint) + 2))

    if (cx > g_bitmapAreaW
              - AM2_SCROLL_MARGIN) {
        g_objCtxSet = 0;
        VIEW_TARGET->x = (int16_t)(VIEW_EYE2->x + step);
    }
    if (cx < AM2_SCROLL_MARGIN) {
        g_objCtxSet = 0;
        VIEW_TARGET->x = (int16_t)(VIEW_EYE2->x - step);
    }
    if (cy > g_bitmapAreaH
              - AM2_SCROLL_MARGIN) {
        g_objCtxSet = 0;
        VIEW_TARGET->y = (int16_t)(VIEW_EYE2->y + step);
    }
    if (cy < AM2_SCROLL_MARGIN) {
        g_objCtxSet = 0;
        VIEW_TARGET->y = (int16_t)(VIEW_EYE2->y - step);
    }
#undef cx
#undef cy
}

/* 0x00424B20, one caller, on the per-frame path -- and it is THE GAME CLOCK.
 * Measure how long the last frame took, clamp it, add it to
 * ADDR_GAME_CLOCK_MS, and publish the delta in both milliseconds and seconds.
 * Everything that treats 0x00511E04 as "now" is being driven from here.
 *
 * The clamp is 66 ms, so a frame that took longer advances the game by 66 ms
 * and no more: the world slows down rather than jumping, which is what keeps a
 * stalled frame from teleporting anything. The raw delta is stored FIRST and
 * the clamped value written over it, which is the original's order and is
 * visible to nothing in between.
 *
 * ADDR_FIXED_STEP would substitute a flat 16 ms for the measurement. Nothing
 * below the CRT line writes it -- three reads, no writers -- so that path is
 * dead and the game is always wall-clock timed. Reproduced anyway; it costs a
 * test that is always false.
 *
 * The seconds twin is delta * 0.001 and is what the screen shake integrates
 * its phase with. That is the whole reason the global used to be called
 * ADDR_SHAKE_RATE -- a name off its one reader rather than off this, its
 * writer.
 *
 * It lives in win32/ for one reason: it calls Ticks, which is reconstructed in
 * winmain.cpp, and a flat module may not reach a win32/ header even
 * transitively -- nor link against it, which is the harder half. The same
 * constraint that moved the depth list the other way.
 *
 * Measured: 19,803 calls against ComposeFrame's 19,977. And Ticks now reads
 * FOUR on the same run -- this function calls it by name roughly twenty
 * thousand times and the counter cannot see any of them, so what is left is
 * whatever other original callers it has. A counter that large collapsing to a
 * single digit is the most dramatic instance of the first blind spot in this
 * tree; nothing about the behaviour changed.
 *
 * The clock is also self-evidencing in a way most functions are not. It drives
 * ADDR_GAME_CLOCK_MS, which the timers, the pads and the audio all compare
 * against -- so a wrong delta here would stall or race every one of them at
 * once, and the mission plays normally. */
void __cdecl FrameClockStep(void)
{
#define CLOCK    (g_gameClockMs)
#define LAST     (g_lastTickMs)
#define DELTA_MS (g_frameDeltaMs)
    uint32_t  delta;
    uint32_t  now;

    if (g_fixedStep) {
        LAST  = CLOCK;
        delta = AM2_FIXED_STEP_MS;
        goto store;         /* one store, shared with the clamp's */
    } else {
        now = Ticks();

        delta    = now - LAST;
        LAST     = now;
        DELTA_MS = (int32_t)delta;          /* the raw one... */
        if (delta > AM2_FRAME_DELTA_CAP_MS) {
            delta    = AM2_FRAME_DELTA_CAP_MS;
store:
            DELTA_MS = (int32_t)delta;      /* ...then the clamp over it */
        }
    }

    CLOCK += delta;

    /* The original converts the delta to a 64-bit integer (it is unsigned),
     * multiplies by a float constant on the x87 stack and stores a float --
     * `fmul dword ptr [0.001f]`, which a float literal gives. */
    g_frameDeltaSec = delta * 0.001f;
#undef CLOCK
#undef LAST
#undef DELTA_MS
}

typedef void  (__cdecl *AM2_VoidFn0)(void);
/* PausedFrameStep is reconstructed below; frame.h declares it. */
void __cdecl PausedFrameStep(void);

/* Spelled exactly as surface.cpp spells them; checkglobals enforces that. */

/* 0x00425CD0. Sub-state 33's PAUSED arm -- what an in-mission frame does while
 * something has the game stopped, where TakeMenuRequest runs when nothing has.
 *
 * ESCAPE leaves. The test is `!IsKeyDown && KeyChanged`, the key being RELEASED
 * rather than pressed, which is the same idiom as the in-mission escape handler
 * and the widget layer's cancel; and it does two things, requesting state 1 and
 * clearing EVERY pause bit at once rather than the one that caused the pause.
 *
 * Otherwise it redraws. The wait bitmap appears only while the pause is one of
 * the four map-loading bits AND no bitmap is already up -- so it is put on
 * screen once, not once a frame, even though this runs every frame.
 *
 * And it is loaded, drawn and FREED within the one call, with the slot cleared
 * on both sides of the load. So the bitmap is not cached at all; the guard that
 * stops it reloading is the pause bits changing, not the slot being occupied,
 * which is why the slot can be freed immediately without the next frame
 * fetching it again. Reproduced as written, since holding it would be faster
 * and would also be a different program.
 *
 * UNEXERCISED, and it was picked expecting the opposite. "Runs every frame
 * while the game is paused" is true and useless: Boot Camp's opening dialogs
 * do not put the game in sub-state 33, they have arms of their own, so the
 * combination this arm needs -- sub-state 33 AND a pause -- does not arise on
 * any drive here. Its counter reads 0 with both dialogs up and 0 again in
 * play. The map-wait bitmap wants the four loading bits as well, which is
 * narrower still. Verified by reading; the A/B compares nothing here.
 *
 * The mistake is the same one CommDrainMsgs recorded: reading the condition a
 * caller calls under is not the same as OBSERVING that it holds. */
void __cdecl MissionPausedFrame(void)
{
    AM2_Sprite *bmp;

    if (!IsKeyDown(AM2_DIK_ESCAPE) && KeyChanged(AM2_DIK_ESCAPE)) {
        RequestState(1);
        UnPauseGame(g_eventFlags);
        return;
    }

    SetDrawTarget(*(LPDIRECTDRAWSURFACE *)&g_primarySurface);
    PausedFrameStep();

    if (!(g_eventFlags & AM2_PAUSE_MAP_WAIT))
        return;
    if (g_currentBitmap)
        return;

    SetGameDir("bitmaps");
    FreeBitmap(&g_currentBitmap);
    g_currentBitmap = LoadBitmap(
        "mapwait.bmp", 0);

    SetDrawTarget(*(LPDIRECTDRAWSURFACE *)&g_primarySurface);

    bmp = g_currentBitmap;
    DrawSprite(bmp,
               (g_bitmapAreaW
                - bmp->bounds.right) >> 1,
               (g_bitmapAreaH
                - bmp->bounds.bottom) >> 1,
               0);

    FreeBitmap(&g_currentBitmap);
}

/* ---- the pause mask ----------------------------------------------------
 *
 * 0x005122FC is one bit per reason the game is paused, and the two functions
 * that move it name themselves in their own log lines. That is worth knowing
 * where it is READ: the frame chain's `if (!GetPauseFlags())` tests below are
 * "if the game is not paused", not a check on some event queue, which is what
 * the names they went in under suggested.
 *
 * Both log only when the comm object's verbose flag is set, and both re-read
 * the global afterwards to log the value that landed rather than the one they
 * computed -- the same thing here, but it is what the original does. */
uint32_t __cdecl GetPauseFlags(void)
{
    return g_eventFlags;
}

/* Returns the new mask -- eax holds it on both paths, reloaded after the log. */
uint32_t __cdecl PauseGame(uint32_t bits)
{
#define FLAGS (g_eventFlags)
    FLAGS |= bits;
    if (*(const int32_t *)(g_commObject + COMM_OFF_VERBOSE))
        orig_log("PauseGame: %x (set: %x)\n", FLAGS, bits);
    return FLAGS;
}

uint32_t __cdecl UnPauseGame(uint32_t bits)
{
    FLAGS &= ~bits;
    if (*(const int32_t *)(g_commObject + COMM_OFF_VERBOSE))
        orig_log("UnPauseGame: %x (reset: %x)\n", FLAGS, bits);
    return FLAGS;
#undef FLAGS
}

/* Every comm step in the pre and post is behind the same field. */
/* `inline` is load-bearing: the original has this body at every call site,
 * and VC6's /O2 (/Ob1) inlines only functions declared inline. Out of line,
 * each caller compiled to push/call/pop -- the relink found it. */
static inline int32_t CommActive(void)
{
    return *(const int32_t *)(g_commObject + AM2_COMM_OFF_ACTIVE) != 0;
}

/* 0x00427420. Mouse then keyboard, and both are ours -- the original is a call
 * and a tail jump, which is the whole function. */
void __cdecl PollInput(void)
{
    PollMouse();
    PollKeyboard();
}

/* 0x0040AF70. Drain what arrived since the last frame, then the timeout sweep
 * -- which the original reaches by tail jump and runs whether or not this is a
 * network game, unlike the drain above it. */
void __cdecl FramePre(void)
{
    if (CommActive() && !GetPauseFlags())
        CommDrainMsgs();

    CheckPlayerTimeout();
}

/* 0x0040AFA0. The other half, and it asks the same question twice rather than
 * once -- the comm field is re-read between the two blocks, so a frame that
 * turns the connection off in the middle runs the first and not the second.
 *
 * The buffer check is the interesting part: fewer than ten free entries in the
 * pool and it hands off to the "COMM ERROR: NO BUFFERS" path by tail jump. */
void __cdecl FramePost(void)
{
    if (CommActive() && !GetPauseFlags())
        ArmyMessageFlush(0);

    if (!CommActive())
        return;

    FlushDelayedSends();
    ProcessResendQueue();

    /* Signed, as the original's `jge`. */
    if ((int32_t)MsgField12(g_msgListPool)
        < (int32_t)AM2_COMM_MIN_BUFFERS)
        CommNoBuffers();
}

/* Defined below, beside the rest of the per-frame chain. */
void __cdecl TakeMenuRequest(void);
void __cdecl Substate22(void);
void __cdecl RefreshDraw(void);
void __cdecl Substate34Escape(void);
void __cdecl StateEnter3(void);
void __cdecl StateEnter0(void);

/* 0x004266B0. State 0 -- the intro. Leaving wins over entering, and the entry
 * action stamps the tick count that the rest of the state measures against. */
void __cdecl State0Frame(void)
{
    if (g_statePending) {
        StateLeaveCommon();
        return;
    }

    if (g_stateEntered) {
        StateEnter0();
        g_state0Tick = GetTickCount();
    }

    MovieStepCurrent();
    PollMovieSkipKey();
}

/* The two still-original callees this needs. The digit table is 0x00412E00 --
 * it fills 0x004FCDF8 with a run of bytes and calls one more function; what
 * that table is FOR is not established, only that entering the title rebuilds
 * it. */
typedef void (__cdecl *AM2_VoidFn0)(void);

/* State1Enter -- original 0x004262E0, one caller, which is State1Frame below.
 * Entering the title screen.
 *
 * The first half is unconditional setup: clear both surfaces, chdir to
 * 01-title, load the palette OUT OF THE SCREEN BITMAP -- the game's palette
 * for the whole title comes from `01_000_00_screen.bmp`, not from a palette
 * file of its own -- build fonts 1 and 2, rebuild the digit table, turn
 * presentation back on and clear the state-entered flag.
 *
 * ADDR_COMM_OBJECT HOLDS A POINTER, not the object. The first version of
 * this read `(const uint8_t *)ADDR_COMM_OBJECT + COMM_OFF_LOBBIED` and so
 * tested the dword at 0x004755A8, which is not zero -- every Boot Camp run
 * took the LOBBY arm, set ADDR_MP_SESSION, and never loaded the map. The A/B
 * caught it on bootcamp: the log stopped after "Lobby start" and printed the
 * multiplayer checksums where the original goes on to parse the script.
 * frame.cpp already had `g_comm` doing the dereference forty lines up.
 *
 * THE SECOND HALF DECIDES WHICH MENU IS ALREADY OPEN, and it has two sources.
 * When the comm object says LOBBIED, the title screen is skipped: menu mode 7
 * if we are the host and 9 if we are not, with ADDR_MP_SESSION set to 1 or 2
 * to match. That is a launch from an external lobby arriving straight in the
 * multiplayer screens.
 *
 * Otherwise it consults the pending MENU REQUEST through a jump table over
 * 7..0x12, and the table has only TWO ARMS. Requests 7, 9, 13 and 18 are
 * honoured -- the mode becomes the request and the request is reset to 1 --
 * and 8, 10, 11, 12, 14, 15, 16, 17 and everything outside the range all fall
 * to the same place: menu mode 1, the title's own. So twelve values index a
 * table that answers two ways, which is worth writing out rather than
 * collapsing: the four that are honoured are not a range and not a pattern.
 *
 * The request is reset to 1 and not to 0 on the honoured arm, which is the
 * same "1 means nothing pending" convention ADDR_MENU_REQUEST carries
 * elsewhere.
 *
 * It ends by marking the overlay dirty and starting title.wav, in that order.
 */
void __cdecl State1Enter(void)
{
    ClearBothSurfaces();
    SetGameDir(g_dirTitlePtr);
    LoadPaletteFile("01_000_00_screen.bmp",
                    *(void * *)&g_activePalette);
    SetGamePalette(*(uint8_t * *)&g_activePalette);

    BuildFontAlias(1);
    BuildFontAlias(2);
    InitMenuScreen();

    g_presentEnabled = 1;
    g_stateEntered   = 0;
    RefreshGate(1);

    {
        const uint8_t *comm = g_commObject;   /* the POINTER, dereferenced */

        if (*(const int32_t *)(comm + COMM_OFF_LOBBIED)) {
            if (*(const int32_t *)(comm + COMM_OFF_IS_HOST)) {
                g_subState = AM2_MENU_MODE_LOBBY_HOST;
                g_mpSession = 1;
            } else {
                g_subState = AM2_MENU_MODE_LOBBY_JOIN;
                g_mpSession = 2;
            }
        } else {
            switch (g_menuRequest) {
            case 7:
            case 9:
            case 13:
            case 18:
                g_subState    =
                    g_menuRequest;
                g_menuRequest = 1;
                break;
            default:
                g_subState = AM2_MENU_MODE_TITLE;
                break;
            }
        }
    }

    g_overlayDirty = 1;
    StartAudioStream((void *)"title.wav", 0);
}



/* State1Menu -- original 0x00426400, two callers, both in State1Frame below.
 * The title state's menu step: take down whatever dialog is up, open the one
 * ADDR_MENU_MODE now names, and paint it.
 *
 * THE JUMP TABLE IS THE DEFINITIVE LIST OF MENU SCREENS -- twenty-one arms,
 * mode N opening arm N, and every one of them already had a name in orig.h.
 * Writing it out is what confirms the four requests State1Enter honours: 7
 * and 9 are the host and join lobbies, 13 is MOVIES and 18 the replay prompt.
 * All four are screens you can arrive at without passing through the title,
 * which is exactly what that arm is for.
 *
 * A MODE OUTSIDE 1..21 OPENS NOTHING and is not an error -- the default arm
 * falls straight through to the paint, so a mode of 0 or 22 leaves the screen
 * as it is. Since the dialog was already destroyed above, that means a blank
 * screen rather than the previous one.
 *
 * THE TEARDOWN IS SLOT 0 WITH A FLAG OF 1, which is MSVC's scalar deleting
 * destructor -- so the dialog is freed, not merely closed, and the global is
 * cleared before the opener runs rather than after.
 *
 * THE DRAW TARGET IS SET TO THE PRIMARY BEFORE THE PAINT. It is
 * SetDrawTarget, not a clear: menus paint straight onto the front buffer
 * rather than through the back one the game uses in play.
 *
 * The rect is passed by value and the original reuses the SetDrawTarget
 * argument slot as its last dword -- `sub esp, 0xc` after a `push` that is
 * never cleaned, sixteen bytes in total, and the paint's own `ret 0x10`
 * cleans them. Written here as an ordinary by-value RECT, which compiles to
 * the same thing without the pun.
 *
 * It clears ADDR_OVERLAY_DIRTY last, whether or not anything was painted.
 */
void __cdecl State1Menu(void)
{
    void   *dlg = g_paintObject;

    if (dlg) {
        ObjVDelete(dlg, 1);
        g_paintObject = (uint8_t *)0;
    }

    /* One arm per screen, in the image's layout order; the case values are
     * the menu modes. */
    switch (g_subState) {
    case  1: OpenTitleScreen(); break;
    case  2: OpenSelectMap(); break;
    case  3: OpenSelectPlayer(); break;
    case  4: OpenEnterName(); break;
    case  5: OpenLoadGame(); break;
    case  6: OpenCommPanel(); break;
    case 10: OpenWarMenu(); break;
    case 11: OpenBattleName(); break;
    case 12: OpenBattleJoin(); break;
    case  7: OpenMpHost(); break;
    case  8: OpenMpOptions(); break;
    case  9: OpenMpJoin(); break;
    case 13: OpenMovies(); break;
    case 15: OpenControls(); break;
    case 21: OpenDeleteGame(); break;
    case 20: OpenDeletePlayer(); break;
    case 16: OpenDifficulty(); break;
    case 17: OpenQuitConfirm(); break;
    case 18: OpenReplayPrompt(); break;
    case 19: OpenAudioOptions(); break;
    case 14: OpenOptionsMenu(); break;
    }

    dlg = g_paintObject;
    if (dlg) {
        SetDrawTarget(*(LPDIRECTDRAWSURFACE *)&g_primarySurface);
        dlg = g_paintObject;
        ObjVPaint(dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
    }

    g_overlayDirty = 0;
}

/* 0x00426570. State 1 -- the menus.
 *
 * A pending menu request is consumed here rather than by the mission's
 * TakeMenuRequest: the request is moved into the sub-state, the request flag
 * cleared and the overlay marked dirty, and the same handler then runs. An
 * already-dirty overlay reaches it without a request at all.
 *
 * The repaint at the end is the two-dereference shape -- object, then its
 * vtable, then slot 2. Writing it as one dereference calls the vtable pointer
 * as a function and the game exits instantly. */
void __cdecl State1Frame(void)
{
    if (g_statePending) {
        State1Leave();
        return;
    }

    if (g_stateEntered)
        State1Enter();

    int32_t *requestSet = &g_menuRequestSet;

    if (*requestSet) {
        g_subState =
            g_menuRequest;
        *requestSet = 0;
        g_overlayDirty = 1;
        State1Menu();
    } else if (g_overlayDirty) {
        State1Menu();
    }

    if (!GetPauseFlags()) {
        SetDrawTarget(*(LPDIRECTDRAWSURFACE *)&g_primarySurface);

        void *obj = g_paintObject;

        /* its update virtual, as the thiscall the original makes */
        if (obj)
            ObjVUpdate(obj);
    }

    State1Common();
}

/* Nine of the thirteen sub-state arms are one shape -- repaint if the overlay
 * is dirty, then DrawMenuOverlay -- differing only in which painter they call;
 * 22, 32, 33 and 34 are each their own thing. They are State2Frame's switch,
 * written in the order the linker laid the bodies out (27, 28, 26, 29, 30, 31,
 * 25 among the painted ones) -- the jump table at ADDR_SUBSTATE_TABLE is what
 * says which arm is which; reading the bodies top to bottom and numbering as
 * you go gets four of them wrong. Arms 29 and 30 reach the same address
 * through two folded functions, OpenDeleteGame and OpenDeleteGameB. */


/* LevelTeardown -- original 0x004256F0, 160 bytes, one caller, and that caller
 * is State2Frame just below: the arm it takes when a state change is pending
 * while the game is already in state 2, which is what "leaving a level" means.
 * CLAUDE.md's chain -- menu request while in state 2, pending flag, this --
 * ends here.
 *
 * TWENTY-FIVE CALLS IN ORDER AND THE ORDER IS THE POINT. This is the
 * ShutdownSubsystems shape one level in: a fixed sequence of teardowns whose
 * arrangement is the fact worth preserving, where a name for each would be a
 * guess for each. TWENTY of the twenty-five were already reconstructed, which
 * is the measurement that made this worth writing rather than leaving at an
 * address -- the teardown was the last original thing holding them together.
 * Of the five that were not, two could be read off their own bodies and are
 * now ADDR_FREE_AIM_SPRITES and ADDR_FREE_SEQ_CONTEXTS; the other two carry
 * placeholders that say only their addresses.
 *
 * IT OPENS BY DELETING WHATEVER DIALOG IS UP. 0x0065A058 is the slot the
 * dialog opener stores into and `drive.sh ctl widgets` walks; slot 0 with a
 * flag of 1 is the MSVC scalar deleting destructor, the same call
 * FreeHudWidgets makes three times. Clearing the global afterwards is what
 * stops a second teardown from deleting it twice.
 *
 * The last call is a TAIL JUMP to CommitState in the original, which is a
 * compiler detail rather than behaviour: written as an ordinary call.
 *
 * TWO CDECL CLEANUPS ARE BATCHED INTO ONE `add esp, 8` covering pushes eleven
 * instructions apart -- the script context and the refresh gate's argument.
 * None of that survives into C, and it is noted because a reader matching the
 * disassembly will find one cleanup for two calls and wonder.
 *
 * EXERCISED, AND IT TOOK POKING A GLOBAL TO DO IT. No drive reaches this by
 * playing: the in-mission ESCAPE handler is sub-state 34 and ordinary play
 * sits at 33. Writing ADDR_MENU_REQUEST and its flag during a live Boot Camp
 * mission -- what that handler would write -- runs the whole sequence, which
 * was confirmed here by logging one marker per call and seeing all 23 arrive.
 *
 * WHAT THAT DRIVE ALSO FOUND IS NOT THIS FUNCTION'S, and it is worth stating
 * where it was nearly misattributed. Under AM2_NOPATCH the same poke ends with
 * sixty "DestroyWeapon, %x" lines as the level's items are freed; with the
 * reconstruction installed there are NONE, and the game does not return to the
 * title. That looked exactly like a defect in this transcription -- until the
 * same poke was run on the PARENT COMMIT, where this function is still the
 * original's and its callees are already ours: DestroyWeapon reads 0 there
 * too. So the divergence is somewhere else on the path and predates this
 * commit by an unknown margin; see STATUS.md. One control run separated
 * "my change broke it" from "nothing has ever tested this path". */
void __cdecl LevelTeardown(void)
{
    AM2_Widget *dlg = *(AM2_Widget * *)&g_paintObject;

    if (dlg) {
        WidgetVDelete(dlg, 1);
        *(AM2_Widget * *)&g_paintObject = (AM2_Widget *)0;
    }

    StopAllSounds();
    FreeObjScripts();
    ResetScriptState();
    ScriptResetTokens((AM2_ScriptCtx *)g_scriptContext);
    ResetPads();
    call0(ADDR_FREE_ARMY_LISTS_ALIAS);
    FreeSeqContexts();
    FreeAirSpritesAlias();
    FreeAimSprites();
    FreeAllSprites();
    FreeHudWidgets();
    RefreshGate(0);
    FreeMenuSprites();
    FreeAllFonts();
    FreeScenarios();
    FreeMapLayers();
    FreeExplosionAnims();
    FreeMissileAnims();
    FreeRoachAnims();
    FreeVehicleAnims();
    FreeSoldierAnims();
    FreeSpriteList();
    TeardownDefTables();
    CommitState();   /* a tail jump in the original */
}

/* 0x004260C0. State 2 -- a live mission.
 *
 * The entry action has an early RETURN in it that the other states do not:
 * with ADDR_STATE_ENTER_ONCE set it consumes a menu request, clears the flag
 * and leaves the frame there, so the sub-state dispatch below is skipped
 * entirely for that one frame. */
typedef int32_t (__cdecl *AM2_FindFirstFn2)(const char *, void *);
typedef int32_t (__cdecl *AM2_FindCloseFn)(int32_t);
#define orig_findfirst ((AM2_FindFirstFn2)((void *)(uintptr_t)(ADDR_CRT_FINDFIRST)))
#define orig_findclose ((AM2_FindCloseFn)((void *)(uintptr_t)(ADDR_CRT_FINDCLOSE)))

/* State2Enter -- original 0x00425300, 1168 bytes, one caller: RunFrame's
 * state-2 arm, reached once per mission start. Tear down the previous
 * screen, load the map and every animation table, build the HUD, place or
 * load the units, unpause.
 *
 * IT SETS ADDR_MENU_MODE TO 33. CLAUDE.md records 33 as the value a probe
 * reads throughout Boot Camp play, and uses it to explain why the in-mission
 * ESCAPE arm -- number 34 -- never runs. This is where the 33 is written, so
 * that is now true by construction rather than by observation.
 *
 * THE FOG AND SCRIPT-RELOAD TEST IS FOUR PATHS COMPUTING TWO STORES. The
 * original branches on the debug flag and the session so that two stores are
 * reached from four directions; every path was walked, and together they say
 * exactly: a network game clears the fog, a single-player one raises it and
 * additionally sets script-reloading when the debug flag is on. Written that
 * way rather than as the four jumps.
 *
 * A LOAD IS PENDING ONLY IF ONE WAS ASKED FOR *AND* THE FILE OPENS. Both
 * gameproc strings must be non-empty, and OpenSaveForLoad must answer a
 * handle; a failure resets the level to 1 and falls back to a fresh start.
 * The handle is carried all the way to LoadGame at the far end of the
 * function, which is why the original keeps it in esi across two hundred
 * instructions.
 *
 * PlaceScenario runs only when nothing is being loaded -- a fresh mission
 * places its units from the scenario table, a restored one gets them from the
 * save instead.
 *
 * The loading screen is drawn by hand: the level's own bitmap, a default if
 * that is missing, centred with the same signed halve the dialogs use, then
 * freed. The progress bar is stepped at ten fixed points from 10 to 100. */
void __cdecl State2Enter(void)
{
    AM2_Widget *screen = *(AM2_Widget * *)&g_paintObject;
    am2_FILE   *save   = (am2_FILE *)0;
    AM2_Sprite *splash;
    AM2_Object *leader;

    if (screen) {
        WidgetVDelete(screen, 1);
        g_paintObject = (uint8_t *)0;
    }
    if (g_currentBitmap)
        FreeBitmap(&g_currentBitmap);

    ClearBothSurfaces();
    g_presentEnabled = 1;

    if (*(const uint8_t *)&g_gameprocBlock == 0)
        goto no_load;
    if (*(const uint8_t *)&g_gameprocStrB == 0) {
    no_load:
        g_loadPending = 0;
    } else if (g_loadPending != 0) {
        save = OpenSaveForLoad();
        if (!save) {
            g_loadPending = 0;
            g_levelId     = 1;
            g_levelIndex  = 1;
        }
    }

    SetGameDir(g_tilesetPath);
    LoadPaletteFile("palette.bmp",
                    *(void * *)&g_activePalette);
    SetGamePalette(*(uint8_t * *)&g_activePalette);

    if (g_optDf)
        SpriteSetLoad(g_tilesetPath);

    ZeroUnread50C34C();

    if (g_optDbg != 0
        && g_mpSession == 0)
        g_scriptReloading = 1;
    if (g_mpSession != 0)
        g_fogOfWar = 0;
    else
        g_fogOfWar = 1;

    ResetItemsAndUids();
    g_haveDefaultCof = 0;

    /* A default .cof only matters for a fresh campaign level past the first. */
    if (g_mpSession == 0
        && g_loadPending == 0
        && g_levelId > 1) {
        uint8_t find[AM2_FINDDATA_BYTES];

        SetGameDir("save");
        int32_t h = orig_findfirst(
            "default.cof", find);
        if (h != -1) {
            g_haveDefaultCof = 1;
            orig_findclose(h);
        }
    }

    SetGameDir("bitmaps");
    splash = LoadBitmap(g_levelStrB, 1);
    if (!splash)
        splash = LoadBitmap("load_default.bmp", 1);
    if (splash) {
        SetDrawTarget(*(LPDIRECTDRAWSURFACE *)&g_primarySurface);
        DrawSprite(splash,
                   (g_screenW
                    - splash->bounds.right) / 2,
                   (g_screenH
                    - splash->bounds.bottom) / 2,
                   0);
    }
    FreeBitmap(&splash);

    ResetLevelState();
    BuildFontAlias(0);
    BuildFontAlias(1);

    SetGameDir(g_tilesetPath);
    LoadDefTables();
    Teardown445F40(g_dirScratch);
    DeclareBuiltinNames();
    ResetPadsAlias();
    FreeObjScripts();

    StartAudioStream(g_levelStrA, 1);
    LoadMap(&g_mapName_,
            g_tilesetPath);

    ProgressBar(0x0A);
    LoadTilesetPalettes();
    LoadSoldierAnims();
    ProgressBar(0x14);
    LoadVehicleAnims();
    LoadRoachAnims();
    ProgressBar(0x1E);
    LoadMissileAnims();
    LoadExplosionAnims();
    ProgressBar(0x28);
    LoadAllSprites();
    ProgressBar(0x46);
    ResetAirSupport();
    ProgressBar(0x4B);

    AimInit();
    SeqSubsystemInit();
    InitRemapsAndLists();

    if (g_haveDefaultCof != 0)
        LoadDefaultCof();
    if (g_loadPending == 0)
        PlaceScenario();

    ProgressBar(0x50);
    g_levelFlagE30 = 1;
    g_viewSnap      = 1;
    g_objCtxSet    = 1;

    BuildHudWidgets();
    InitMenuScreen();
    RefreshGate(1);
    g_fullRedraw = 1;
    ProgressBar(0x5A);
    ResetTimers();

    if (g_loadPending == 0)
        LoadLevelScript();
    else
        LoadGame(save);

    ProgressBar(0x64);

    if (g_missionRetry != 0) {
        /* Logged from the global, not a local: the image stores the count
         * and reads it back for the call. */
        g_missionRetry = 0;
        g_attemptCount = g_attemptCount + 1;
        orig_log("Attempt# %d\n", g_attemptCount);
    }
    orig_log("calculating region data...\n");

    BuildRegionGraph();

    /* Select whatever the local player leads -- its vehicle if it is in one. */
    leader = LookupOwnerObj(
                 (int32_t)g_defaultOwner);
    if (leader) {
        SelectInventorySlot(leader,
            leader->inventorySel);
        DeselectAll();
        if (leader->riding)
            SelectUnit(LookupType3ByUID(
                leader->riding));
        else
            SelectUnit(leader);
        g_viewSnap = 1;
    }

    /* `call 0x45CAA0` at 0x0042568A with NOTHING pushed -- the same
     * no-argument log call TakeMenuRequest makes, and it was MISSING here.
     * Found by counting the harness's "call site passed a non-string format"
     * notes on a load: the original emits two past the region pass and we
     * emitted one. Reproduced rather than tidied away, for the reason the
     * other one is: the format the logger reads is whatever sits above the
     * return address, and src/inject/gamelog.c already declines to print it. */
    orig_log_noargs();

    /* A FRESH single-player game comes up PAUSED under -dbg. All three
     * branches are `skip if set` except the last, so the call is reached only
     * when a load is NOT pending, there is no session, and the debug flag IS
     * on -- I had the first of the three inverted, and the symptom was that
     * our side ran straight past MESSAGE FROM HQ while the original sat on
     * it. 189,990 pixels, twice, and identical logs and object state either
     * way: the mission had loaded correctly and only the pause was missing. */
    if (g_loadPending == 0
        && g_mpSession == 0
        && g_optDbg != 0)
        PauseGame(8);

    g_subState        = AM2_MENU_MODE_PLAY;
    g_menuRequest     = 0;
    g_stateEntered    = 0;
    g_overlayDirty    = 1;
    g_scriptReloading = 0;
    SendGamePause(0, 0x10000);
}

void __cdecl State2Frame(void)
{
    if (g_statePending) {
        LevelTeardown();
        return;
    }

    if (g_stateEntered) {
        State2Enter();
        if (g_stateEnterOnce) {
            TakeMenuRequest();
            g_stateEnterOnce = 0;
            return;
        }
        if (GetPauseFlags())
            return;
    }

    /* The image's switch, arm for arm and in its layout order: 22, 23, 24,
     * 27, 28, 26, 29, 30, 31, 25, then 33 and 34. Each painted arm repaints
     * when the overlay is dirty and then draws it; 32 and anything out of
     * range fall to the common tail. */
#define PAINTED(painter)            \
        if (g_overlayDirty)         \
            painter();              \
        DrawMenuOverlay();          \
        break;
    switch (g_subState) {
    case 22: Substate22(); break;
    case 23: PAINTED(OpenGameMenu)
    case 24: PAINTED(OpenMessage)
    case 27: PAINTED(OpenAudioOptions)
    case 28: PAINTED(OpenControls)
    case 26: PAINTED(OpenLoadGame)
    case 29: PAINTED(OpenDeleteGame)
    case 30: PAINTED(OpenDeleteGameB)
    case 31: PAINTED(OpenOverwriteGame)
    case 25: PAINTED(OpenSaveGame)
    case 32: break;
    case 33:
        /* Ordinary play, and the arm Boot Camp sits in the whole time. */
        if (!GetPauseFlags())
            TakeMenuRequest();
        else
            MissionPausedFrame();
        break;
    case 34:
        Substate34Escape();
        break;
    }
#undef PAINTED

    if ((GetPauseFlags() & AM2_EVENT_FLAG_8)
        && DismissKeyReleased()) {
        UnPauseGame(AM2_EVENT_FLAG_8);
        SendGamePause(0, AM2_EVENT_FLAG_8);
    }
}

/* 0x00426760. State 3. Same parts as state 0 and the opposite order: the entry
 * action runs BEFORE the pending check, so a state that is entered and left in
 * the same frame runs its entry action here and would not in state 0. */
void __cdecl State3Frame(void)
{
    if (g_stateEntered)
        StateEnter3();

    if (g_statePending) {
        StateLeaveCommon();
        return;
    }

    MovieStepCurrent();
    PollMovieSkipKey();
}


/* 0x00425EE0, one caller -- RunFrame's state 2, on every unpaused frame. The
 * in-mission driver, and the last piece of this chain: every one of its
 * eighteen callees is now named and most are reconstructed.
 *
 * Three jobs share the entry. A pending MENU REQUEST short-circuits everything:
 * it becomes the wanted menu mode, the pair is cleared and the state-pending
 * flag goes up, which is the route CLAUDE.md traces from a menu request to the
 * level teardown. Otherwise, if the overlay is dirty, a level load is being
 * finished -- the clock is reset to zero, the base stamped from Ticks, and
 * MissionStartup fires. Otherwise the ordinary frame runs.
 *
 * The SCRIPT RELOAD arm is the odd one and it is gated twice: it needs
 * ADDR_SCRIPT_RELOAD raised, and what it does afterwards depends on -dbg. With
 * the switch on it raises the reloading flag, re-reads the script, redeclares
 * the rule variables, restarts the mission and then shows the info bitmap;
 * without it, the same reload happens but the function simply returns. So the
 * feature works in a retail build and only announces itself in a debug one.
 *
 * The log call has NO ARGUMENTS. The original pushes nothing and calls the
 * varargs logger, which is a `ret` in this build and harmless -- but it is
 * reproduced through a no-argument function pointer rather than as `Log("")`,
 * because those are different calls and this one runs once per level load with
 * our capture installed.
 *
 * A network game replaces the measured frame delta with a fixed 0.066 and
 * skips both the clock step and the timer sweep, so its timing comes from the
 * wire rather than from the wall clock. It also counts ten FRAMES -- not
 * milliseconds -- before looking once for abandoned armies.
 *
 * The tail is the same either way, and the last branch decides how the frame
 * reaches the screen: a full HudPaint and PresentFrame normally, or
 * RefreshDraw when ADDR_STATE_ENTER_ONCE is clear. */
void __cdecl TakeMenuRequest(void)
{
    if (g_menuRequestSet) {
        g_subState =
            g_menuRequest;
        g_menuRequest     = 0;
        g_menuRequestSet = 0;
        g_statePending    = 1;
        return;
    }

    if (g_overlayDirty) {
        if (!g_loadPending) {
            g_frameDeltaMs = 0;
            *(uint32_t *)&g_clockBaseMs =
                g_fixedStep ? 0u : Ticks();
            g_gameClockMs = 0;
            MissionStartup();
        }

        if (g_mpSession)
            g_netSettleCount = 1;

        g_lastTickMs =
            g_fixedStep ? 0u : Ticks();

        orig_log_noargs();      /* no arguments -- the original's, see above */

        g_scriptReload = 0;
        g_overlayDirty = 0;
        g_loadPending  = 0;
    } else if (g_scriptReload) {
        /* The option is read afresh after the reload, not held. */
        if (g_optDbg)
            g_scriptReloading = 1;

        ReadScript(g_scriptReloadPath,
                   (AM2_ScriptCtx *)g_scriptContext);
        DeclareRuleVars();
        MissionStartup();

        g_scriptReload = 0;
        if (!g_optDbg)
            return;

        g_scriptReloading = 0;
        g_subState = AM2_SUBSTATE_INFO_BITMAP;
        g_overlayDirty = 1;
        return;
    }

    if (!g_netGame) {
        FrameClockStep();
        TimerTick();
    }

    MissionInput();

    if (g_netGame) {
        *(uint32_t *)&g_frameDeltaSec = AM2_NET_FRAME_DELTA_SEC;

        if (g_mpSession) {
            int32_t n = g_netSettleCount;

            if (n > 0) {
                n++;
                g_netSettleCount = n;
                if (n == AM2_NET_SETTLE_FRAMES) {
                    g_netSettleCount = 0;
                    if (*(const int32_t *)(g_commObject + COMM_OFF_IS_HOST))
                        AiTakeAbandoned();
                }
            }
        }
    }

    RefreshObjCtx();
    HudUpdate();
    ClearFrameCounts();

    if (!g_netGame) {
        ObjFrameSweep();
    } else {
        void *paint = g_paintObject;

        if (paint)
            ObjVUpdate(paint);
    }

    PadAdvanceDeadlines();
    Update3DAudioVolumes();
    ViewUpdate();
    FreeOverdueItems();
    SeqRunBoth();
    FlameTick();
    AdvanceSecondDeadline();

    if (g_stateEnterOnce) {
        HudPaint();
        PresentFrame();
        return;
    }
    RefreshDraw();
}

/* Spelled exactly as surface.cpp, mapdraw.cpp and palette.cpp spell it, so the
 * four stay one definition; checkglobals enforces that. */


/* 0x004266F0, one caller -- RunFrame, on entering state 3. State 3 is the
 * MOVIE state, and the body is what settles that: it loads a GREYSCALE palette
 * off disk, builds a filename from ADDR_MOVIE_TO_PLAY and hands it to the
 * function that constructs the Smacker object.
 *
 * The palette is the giveaway and the order matters. "avi\greyscale.bmp" is
 * read into ADDR_ACTIVE_PALETTE and then made the game palette BEFORE both
 * surfaces are cleared, so the clear happens in the palette the film will play
 * in -- doing it the other way round would flash the menu's colours.
 *
 * All six calls are ours now -- LoadPaletteFile, SetGamePalette, RefreshGate,
 * LatchKeyState, MovieBuildName and PlayMovie. Nothing in this state reaches
 * the image any more.
 *
 * The name buffer is 0x40 bytes because that is the frame the original
 * reserves, and the builder writes into it unchecked. Reproduced at that size
 * rather than widened: a longer path would overflow here exactly as it does
 * there, and this is not the place to decide that is a bug.
 *
 * The last two lines are the state bookkeeping RunFrame reads back -- the
 * entry tick, and clearing ADDR_STATE_ENTERED so the transition is consumed. */
void __cdecl StateEnter3(void)
{
    char name[0x40];

    LoadPaletteFile("avi\\greyscale.bmp",
                    (void *)g_activePalette);
    SetGamePalette((uint8_t *)(uintptr_t)g_activePalette);
    ClearBothSurfaces();
    RefreshGate(0);

    MovieBuildName(name, &g_movieToPlay);
    PlayMovie(name, 0);

    LatchKeyState();

    /* GetTickCount itself, not Ticks: the original calls the import here,
     * so this stamp is milliseconds since boot even where Ticks would use the
     * performance counter. */
    g_state0Tick   = GetTickCount();
    g_stateEntered = 0;
}


/* 0x004265F0, one caller -- RunFrame, on entering state 0. Its first four
 * calls are IDENTICAL to StateEnter3's, and the shared part is the point: both
 * states show a film, so both put the greyscale palette up, make it the game
 * palette, clear both surfaces in it and open the refresh gate.
 *
 * Where they differ is what comes next. State 3 knows which film to play and
 * builds its name; state 0 DISPATCHES on the GAME OVER state instead, through
 * ADDR_STATE_ACTIONS -- one 12-byte record per state, whose first field is
 * what to do on entering.
 *
 * That table is SHARED with WndProc, which reads the second field of the same
 * record under a guard that the game state is 0 -- the very state being
 * entered here. So one record holds both halves: what state 0 does on entry
 * and what it does when a message arrives while in it. Finding that is what
 * moved the table's base back one column; see ADDR_STATE_ACTIONS in orig.h.
 *
 * The index is not range-checked and the original does not check it either.
 * The states any caller sets are 0, 1, 4 and -1, and -1 must never arrive
 * here: it would read twelve bytes before the table, where a bitmask table's
 * tail sits, and call 0x7FFFFFFF. Reproduced unguarded, and recorded rather
 * than quietly fixed -- adding a check would be inventing behaviour, and
 * hiding the hazard would be worse than either.
 *
 * The last line clears ADDR_STATE_ENTERED so the transition is consumed, which
 * is the one thing every entry action in this family does.
 *
 * WHAT VERIFIED THE BASE, since a table's base is exactly what an A/B cannot
 * check. A probe printed the state and the pointer it was about to call:
 * `over=0 fn=0042E8E0` and `over=1 fn=0042E930`, which are the column-0
 * entries of the table read straight out of the image. A base one column late
 * would have printed 0x0042E910 and 0x0042E960 -- the neighbouring column --
 * and nothing on screen would have differed for it.
 *
 * The counter is blind and a plain startup does not reach this at all: the
 * game BEGINS in state 0 rather than transitioning into it, so g_stateEntered
 * is never set for it there. ab.sh state3 is what reaches it, on the way back
 * out of the movie state, and it covers two different game-over states. */
void __cdecl StateEnter0(void)
{
    int32_t over;

    LoadPaletteFile("avi\\greyscale.bmp",
                    (void *)g_activePalette);
    SetGamePalette((uint8_t *)(uintptr_t)g_activePalette);
    ClearBothSurfaces();
    RefreshGate(0);

    over = GameOverState();
    (g_stateActions)[over].onEnter();

    g_stateEntered = 0;
}


/* 0x00425C10, one caller -- the sub-state table, arm 0x16. The INFO BITMAP,
 * which F1 raises during a mission: MissionInput loads the level's bitmap,
 * pauses with flag 8 and puts the sub-state here, and this is what holds it on
 * screen and takes it away again.
 *
 * Three parts and the ORDER between the first two is what makes it work. A
 * pending menu request is handled FIRST and unconditionally: the bitmap is
 * freed, the sub-state goes back to 0x21 -- ordinary play -- and both the
 * request and the dirty flag are cleared. That request is what the third part
 * sets, so the dismissal takes effect on the NEXT frame rather than inside the
 * frame that noticed the click. Nothing here is re-entrant and it does not
 * need to be.
 *
 * The second part paints, once, and only while the overlay is dirty -- the
 * flag is cleared before the bitmap is even tested, so a null bitmap still
 * consumes the repaint rather than leaving it pending for ever.
 *
 * The centring is against ADDR_SCREEN_W/H, NOT the ADDR_BITMAP_AREA pair that
 * RefreshDraw uses for the same job. Two different screen-size pairs live in
 * this image and they are not interchangeable; taken from the operands rather
 * than from what the other centring site does. Both shifts are `sar`, so
 * signed, which matters for a bitmap wider than the screen.
 *
 * The third part dismisses. Either the event-flag-8 test passes, or mouse
 * button 0 is BOTH down and changed -- a click, not a hold. It unpauses flag 8
 * and raises the menu request the first part will consume.
 *
 * VERIFIED BY DRIVING THE WHOLE ROUND TRIP, which exercises all three parts in
 * one go. In a live mission, reading the sub-state and the bitmap slot over
 * the control socket:
 *
 *   play         0x21   bmp = -
 *   after F1     0x16   bmp = 00896f04   raised, loaded, painted
 *   after click  0x21   bmp = 00000000   dismissed and FREED
 *
 * The counter is blind -- the sub-state dispatcher is ours -- so the globals
 * are the evidence, and they are better evidence than a count would have been.
 *
 * Tested in the failing direction by dropping the FreeBitmap: the slot still
 * reads 00896f04 after the click. That is a LEAK, and the screen returns to
 * play looking exactly right either way, so no pixel comparison could have
 * found it. */
void __cdecl Substate22(void)
{
    if (g_menuRequestSet) {
        FreeBitmap(&g_currentBitmap);
        g_subState        = AM2_SUBSTATE_PLAY;
        g_menuRequestSet = 0;
        g_overlayDirty    = 0;
        return;
    }

    if (g_overlayDirty) {
        /* The bitmap global is read again after the target is set. */
#define BMP (g_currentBitmap)
        g_overlayDirty = 0;
        if (BMP) {
            SetDrawTarget((*(LPDIRECTDRAWSURFACE *)&g_primarySurface));
            DrawSprite(BMP,
                       (g_screenW
                        - BMP->bounds.right) >> 1,
                       (g_screenH
                        - BMP->bounds.bottom) >> 1,
                       0);
        }
#undef BMP
    }

    if (DismissKeyReleased()
        || (g_mouseButton
            && g_mouseChanged)) {
        SendGamePause(0, AM2_EVENT_FLAG_8);
        g_menuRequestSet = 1;
    }
}


/* 0x00426A90, three callers. The multiplayer end screen.
 *
 * ITS ARGUMENT IS A RESULT CODE, NOT A BOOLEAN. 0 won, 1 lost, 2 the host
 * left -- and ANY OTHER VALUE leaves the bitmap alone while still doing
 * everything else. MissionNetworked only ever passes 0 or 1, which is why
 * that function's inverted flag reads as "lost"; it is one value of three.
 *
 * The three arms are identical but for the filename, and each frees the
 * current bitmap BEFORE loading -- so a caller that passes an unknown code
 * keeps whatever was on screen rather than being left with nothing.
 *
 * Order matters at the top: the pause bits are cleared, the data directory is
 * moved to "bitmaps", and only then is the draw target set. Loading happens
 * relative to that directory, so moving it after the load would find nothing.
 *
 * Two comm methods run before any of that -- the statistics report and the
 * property publish -- and the function TAIL-JUMPS to CommReopenSession, which
 * is reconstructed in dplay.cpp. So the end screen is also where the session
 * is handed back, and reproducing the tail call as an ordinary call at the end
 * is equivalent here only because nothing follows it.
 *
 * The log call takes NO ARGUMENTS, like TakeMenuRequest's. Reproduced through
 * a no-argument pointer rather than as Log(""), which is a different call.
 *
 * VERIFIED BY READING. All three callers are multiplayer end-of-mission
 * paths. */
void __cdecl ShowMpResult(int32_t result)
{
    UnPauseGame(AM2_MP_RESULT_UNPAUSE);
    SetGameDir("bitmaps");
    SetDrawTarget((*(LPDIRECTDRAWSURFACE *)&g_primarySurface));

    PausedFrameStep();
    CommReportStats(*(void * *)&g_commObject);
    CommPublishResult(*(void * *)&g_commObject);

    if (result == AM2_MP_RESULT_WON) {
        FreeBitmap(&g_currentBitmap);
        g_currentBitmap =
            LoadBitmap("mpwon.bmp", 0);
    } else if (result == AM2_MP_RESULT_LOST) {
        FreeBitmap(&g_currentBitmap);
        g_currentBitmap =
            LoadBitmap("mplost.bmp", 0);
    } else if (result == AM2_MP_RESULT_HOST_LEFT) {
        FreeBitmap(&g_currentBitmap);
        g_currentBitmap =
            LoadBitmap("mphostleft.bmp", 0);
    }

    g_subState     = AM2_SUBSTATE_ESCAPE;
    g_overlayDirty = 1;

    orig_log_noargs();
    CommReopenSession(*(void * *)&g_commObject);
}



/* 0x00424BF0, two callers. Repaints the whole screen from scratch -- what
 * TakeMenuRequest does instead of the ordinary present when the state has not
 * been entered this frame.
 *
 * THE DRAW TARGET IS SET THREE TIMES, always to the back buffer, and that is
 * not redundancy: ComposeFrame and the effect layer both retarget, so each
 * group of painters has to put it back. Dropping the repeats would work until
 * one of those two changed, which is precisely the kind of coupling worth
 * leaving alone. The original also defers all three pushes to one `add esp`,
 * which is the compiler's, not the function's.
 *
 * TWO LOG CALLS, BOTH WITH NO ARGUMENTS, bracketing HudPaint. Reproduced
 * through a no-argument pointer, as in TakeMenuRequest and ShowMpResult -- a
 * varargs call with none is not Log("").
 *
 * THE BITMAP IS CENTRED AGAINST ADDR_BITMAP_AREA_W/H, where Substate22 centres
 * the same kind of bitmap against ADDR_SCREEN_W/H. Two screen-size pairs live
 * in this image and the two sites disagree about which to use; taken from the
 * operands at each site rather than made consistent.
 *
 * Its guard is four tests deep and every one is a REFUSAL: no bitmap, or the
 * menu request is the info screen or the escape screen, or the overlay is
 * already dirty. So the bitmap is drawn only when nothing else is about to
 * repaint over it.
 *
 * IT WAS "VERIFIED BY READING" AND IT IS NOT ANY MORE. This comment used to
 * say the arm of TakeMenuRequest that reaches it needs ADDR_STATE_ENTER_ONCE
 * clear, "which no drive here produces". A probe says otherwise: an ordinary
 * Boot Camp start runs it 55 times, every one at ADDR_GAME_STATE 2, and all
 * 55 come through TakeMenuRequest -- RefreshScreen, the other caller, does
 * not run at all. They stop the moment the two opening dialogs are cleared,
 * so this is the repaint the game does WHILE a dialog is up and not part of
 * ordinary play at all.
 *
 * Its counter is blind, all three call sites being ours, which is why the
 * wrong claim survived: nothing contradicted it because nothing measured it.
 * The right response to a blind counter is a probe, which is what the rest
 * of this file keeps saying. */
void __cdecl RefreshDraw(void)
{
    AM2_Sprite *bmp;

    SetDrawTarget(g_backBuffer);
    CyclePalette();
    ComposeFrame();

    SetDrawTarget(g_backBuffer);
    AirFrameDraw();
    DrawEffectLayer();
    PausedFrameStep();

    SetDrawTarget(g_backBuffer);
    DrawSelection();
    DrawViewRect();

    orig_log_noargs();
    HudPaint();
    orig_log_noargs();

    bmp = g_currentBitmap;
    if (bmp
        && g_menuRequest != AM2_SUBSTATE_BITMAP
        && g_menuRequest != AM2_SUBSTATE_ESCAPE
        && !g_overlayDirty) {
        int32_t w = bmp->bounds.right;
        int32_t h = bmp->bounds.bottom;

        DrawSprite(bmp,
                   (g_bitmapAreaW - w) >> 1,
                   (g_bitmapAreaH - h) >> 1,
                   0);
    }

    DrawMenuCursor();
    PresentFrame();
}


/* 0x00425DA0, one caller -- the sub-state table, arm 34. The in-mission
 * ESCAPE screen, and the only arm ordinary play never enters: CLAUDE.md
 * records the sub-state sitting at 33 for a whole mission, which is why
 * pressing ESCAPE in Boot Camp does nothing.
 *
 * Same three-part shape as Substate22 -- consume a request, paint once, then
 * test for dismissal -- but each part differs.
 *
 * THE CONSUME LEAVES THE MISSION. It frees the bitmap, asks for state 1, and
 * then chooses the menu request BRANCHLESSLY: `neg; sbb; and -2; add 9`,
 * which is 7 when COMM_OFF_IS_HOST is set and 9 when it is not. Those are the
 * multiplayer HOST and JOIN panels -- tools/ab.sh reaches both by poking
 * exactly those codes, which is independent corroboration rather than a
 * reading of this one site.
 *
 * The dismissal writes MENU_REQUEST = 0 and raises the flag, and that 0 is
 * OVERWRITTEN by the 7 or 9 above on the very next frame. Reproduced: nothing
 * reads it in between, but the write is the original's and dropping it would
 * be a guess about what nothing reads.
 *
 * The bitmap is centred against ADDR_BITMAP_AREA_W/H, as RefreshDraw does and
 * Substate22 does not -- two of the three sites use this pair and one uses
 * ADDR_SCREEN_W/H. Taken from the operands, as at the other two.
 *
 * DRIVEN, BOTH ARMS, which CLAUDE.md says is impossible for this handler --
 * and it is, by playing: ordinary play sits in sub-state 33 and never enters
 * 34. Poking ADDR_MENU_MODE to 0x22 puts the dispatcher on this arm, and
 * pressing and releasing ESCAPE then does the rest. In a live Boot Camp
 * mission, reading the sub-state back afterwards:
 *
 *   not host                     -> 9    the JOIN panel
 *   COMM_OFF_IS_HOST poked to 1  -> 7    the HOST panel
 *
 * So the branchless select is exercised in both directions, not merely
 * transcribed. It also confirms from the inside what tools/ab.sh had been
 * assuming from the outside when it poked those two codes to reach those two
 * screens.
 *
 * The counter stays 0 -- the dispatcher is ours -- so the sub-state is the
 * evidence, as it was for Substate22.
 *
 * CLAUDE.md's account was written from a probe rather than the body: it says
 * the handler tests `!IsKeyDown(ESC) && KeyChanged(ESC)` and raises a menu
 * request. It does. */
void __cdecl Substate34Escape(void)
{
    if (g_menuRequestSet) {
        FreeBitmap(&g_currentBitmap);
        RequestState(1);
        /* The comm object is read after the calls. */
        g_menuRequest =
            *(const int32_t *)(g_commObject
                               + COMM_OFF_IS_HOST)
                ? AM2_MENU_REQ_MP_HOST : AM2_MENU_REQ_MP_JOIN;
        g_menuRequestSet = 0;
        return;
    }

    if (g_overlayDirty) {
        /* The bitmap global is read again after the target is set. */
#define BMP (g_currentBitmap)
        g_overlayDirty = 0;
        if (BMP) {
            SetDrawTarget((*(LPDIRECTDRAWSURFACE *)&g_primarySurface));
            DrawSprite(BMP,
                       (g_bitmapAreaW
                        - BMP->bounds.right) >> 1,
                       (g_bitmapAreaH
                        - BMP->bounds.bottom) >> 1,
                       0);
        }
#undef BMP
    }

    if (!IsKeyDown(AM2_DIK_ESCAPE) && KeyChanged(AM2_DIK_ESCAPE)) {
        g_menuRequest     = 0;
        g_menuRequestSet = 1;
    }
}


/* 0x00426790. State 4 -- leaving. Posts WM_CLOSE to the game's own window and
 * clears the entered flag so it happens once, and there is nothing else in it:
 * the shutdown proper runs from WndProc. */
void __cdecl State4Frame(void)
{
    if (!g_stateEntered)
        return;

    PostMessageA(*(HWND *)&g_enumContext, WM_CLOSE, 0, 0);
    g_stateEntered = 0;
}


/* 0x00426640, ADDR_STATE_LEAVE_COMMON -- what states 0 and 3 both tail-jump to
 * when they leave: end whatever movie is playing, then commit the pending
 * state. Two calls and nothing else, which is why it has no entry of its own
 * in functions.tsv and turns up only as a jump target. */
void __cdecl StateLeaveCommon(void)
{
    MovieEndCurrent();
    CommitState();
}


/* 0x004263E0, ADDR_STATE1_LEAVE, reached from State1Frame. Five steps and the
 * last is a TAIL JUMP to CommitState, so the state change is this function's
 * final act rather than something a caller does afterwards. */
void __cdecl State1Leave(void)
{
    RefreshGate(0);
    FreeMenuSprites();
    CloseScreen();
    StopAudioStream();
    CommitState();
}


/* 0x00426650, the "press anything to skip" poller, called by both movie
 * states.
 *
 * It sweeps scancodes 1..255 for one that was RELEASED -- `!IsKeyDown &&
 * KeyChanged`, the same idiom the in-mission ESCAPE handler uses -- and acts
 * only once the state has been up longer than 500 ms, which stops the
 * keystroke that STARTED the movie from immediately ending it.
 *
 * The original computes that test as `sbb`/`neg` and then an `abs` of the 0/1
 * it produced, which cannot change anything; only the comparison is written
 * here. The delta is UNSIGNED, which is right for a tick count.
 *
 * It was ADDR_STATE_FRAME_COMMON -- a name about where it sits in the frame
 * rather than what it does. */
void __cdecl PollMovieSkipKey(void)
{
    int32_t key;

    for (key = 1; key < 0x100; key++) {
        if (IsKeyDown(key) || !KeyChanged(key))
            continue;

        /* abs() of the COMPARISON, not of the difference: the parenthesis
         * is misplaced in the original, which compiles the unsigned test and
         * then takes abs of its 0 or 1. The same answer either way. */
        if (abs(GetTickCount() - g_state0Tick
                > AM2_MOVIE_SKIP_HOLDOFF_MS)) {
            MovieEndCurrent();
            LatchKeyState();
            return;
        }
    }
}


/* 0x00426270, ADDR_STATE1_COMMON. The name is positional and is KEPT -- it
 * really is state 1's shared frame step -- but the body is three distinct
 * things and the first is the one worth naming.
 *
 * Pause reason 8 means A FULL-SCREEN BITMAP IS UP, which orig.h settles from
 * the pair that sets it alongside the `showbitmap` script keyword. So the
 * first block is the instruction sign's dismissal: while that bit is set,
 * a released dismiss key clears it locally AND tells the other players, which
 * is why the un-pause is two calls rather than one.
 *
 * The rest is the frame: draw into the back buffer, paint whatever dialog is
 * up through its own vtable slot 1 with its own rectangle, put the cursor on
 * top, and present. */
void __cdecl State1Common(void)
{
    void *dlg;

    if (GetPauseFlags() & AM2_EVENT_FLAG_8) {
        if (DismissKeyReleased()) {
            UnPauseGame(AM2_EVENT_FLAG_8);
            SendGamePause(0, AM2_EVENT_FLAG_8);
        }
    }

    SetDrawTarget(g_backBuffer);

    dlg = g_paintObject;
    ObjVPaint(dlg, *(const AM2_Rect *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));

    DrawMenuCursor();
    PresentFrame();
}

int frame_install(void)
{
    int rc = 0;

    /* Every counter here reads 0: RunFrame is ours and calls all of them
     * directly. The patches are for the other callers -- PollInput has two. */
    rc |= patch_replace(ADDR_POLL_INPUT, (const void *)PollInput,
                        "PollInput", 2);
    rc |= patch_replace(ADDR_FRAME_PRE, (const void *)FramePre, "FramePre", 1);
    rc |= patch_replace(ADDR_FRAME_POST, (const void *)FramePost,
                        "FramePost", 1);
    rc |= patch_replace(ADDR_STATE0_FRAME, (const void *)State0Frame,
                        "State0Frame", 1);
    rc |= patch_replace(ADDR_STATE1_MENU, (const void *)State1Menu,
                        "State1Menu", 2);
    rc |= patch_replace(ADDR_STATE1_ENTER, (const void *)State1Enter,
                        "State1Enter", 1);
    rc |= patch_replace(ADDR_STATE1_FRAME, (const void *)State1Frame,
                        "State1Frame", 1);
    rc |= patch_replace(ADDR_STATE2_ENTER, (const void *)State2Enter,
                        "State2Enter", 1);
    rc |= patch_replace(ADDR_STATE2_FRAME, (const void *)State2Frame,
                        "State2Frame", 1);
    rc |= patch_replace(ADDR_LEVEL_TEARDOWN, (const void *)LevelTeardown,
                        "LevelTeardown", 1);
    rc |= patch_replace(ADDR_STATE3_FRAME, (const void *)State3Frame,
                        "State3Frame", 1);
    rc |= patch_replace(ADDR_STATE4_FRAME, (const void *)State4Frame,
                        "State4Frame", 1);
    rc |= patch_replace(ADDR_GET_PAUSE_FLAGS, (const void *)GetPauseFlags,
                        "GetPauseFlags", 13);
    rc |= patch_replace(ADDR_PAUSE_GAME, (const void *)PauseGame,
                        "PauseGame", 8);
    rc |= patch_replace(ADDR_SUBSTATE34_ESCAPE, (const void *)Substate34Escape,
                        "Substate34Escape", 0);
    rc |= patch_replace(ADDR_REFRESH_DRAW, (const void *)RefreshDraw,
                        "RefreshDraw", 2);
    rc |= patch_replace(ADDR_SHOW_MP_RESULT, (const void *)ShowMpResult,
                        "ShowMpResult", 3);
    rc |= patch_replace(ADDR_SUBSTATE22, (const void *)Substate22,
                        "Substate22", 0);
    rc |= patch_replace(ADDR_STATE0_ENTER, (const void *)StateEnter0,
                        "StateEnter0", 0);
    rc |= patch_replace(ADDR_STATE3_ENTER, (const void *)StateEnter3,
                        "StateEnter3", 0);
    rc |= patch_replace(ADDR_TAKE_MENU_REQUEST, (const void *)TakeMenuRequest,
                        "TakeMenuRequest", 0);
    rc |= patch_replace(ADDR_MISSION_INPUT, (const void *)MissionInput,
                        "MissionInput", 0);
    rc |= patch_replace(ADDR_FRAME_CLOCK_STEP, (const void *)FrameClockStep,
                        "FrameClockStep", 0);
    rc |= patch_replace(ADDR_MISSION_PAUSED_FRAME,
                        (const void *)MissionPausedFrame,
                        "MissionPausedFrame", 0);
    rc |= patch_replace(ADDR_UNPAUSE_GAME, (const void *)UnPauseGame,
                        "UnPauseGame", 19);
    rc |= patch_replace(ADDR_PAUSED_FRAME_STEP, (const void *)PausedFrameStep,
                        "PausedFrameStep", 0);
    rc |= patch_replace(ADDR_STATE_LEAVE_COMMON,
                        (const void *)StateLeaveCommon,
                        "StateLeaveCommon", 0);
    rc |= patch_replace(ADDR_STATE1_LEAVE, (const void *)State1Leave,
                        "State1Leave", 0);
    rc |= patch_replace(ADDR_POLL_MOVIE_SKIP,
                        (const void *)PollMovieSkipKey,
                        "PollMovieSkipKey", 0);
    rc |= patch_replace(ADDR_STATE1_COMMON, (const void *)State1Common,
                        "State1Common", 0);
    return rc;
}

/* Still the image's -- event.cpp reaches it the same way, and thiscall
 * because the comm object goes in ecx. */
typedef int32_t (__attribute__((thiscall)) *AM2_TeamScoreFn)(void *comm,
                                                             int32_t slot);

/* PausedFrameStep -- original 0x00462600, three callers. The multiplayer
 * scoreboard overlay: one row per comm slot with a team marker, a pause
 * badge, either a latency bar or an absent-player mark, the army score, the
 * team score, and the game type across the bottom.
 *
 * ITS THREE BITMASK TABLES ARE PER-SLOT PAUSE REASONS, decoded from the image
 * rather than read off the branches: 0x0048C7A8 holds {0x10, 0x20, 0x40,
 * 0x80}, 0x0048C7B8 {0x800, 0x1000, 0x2000, 0x4000} and 0x0048C7C8 {0x20000,
 * 0x40000, 0x80000, 0x100000}, each indexed by slot and tested against
 * ADDR_PAUSE_FLAGS -- which CLAUDE.md already establishes as one bit per
 * reason the game is paused. Three groups of four, and the sprite variant
 * each selects is different.
 *
 * OUR OWN SLOT TAKES A DIFFERENT PATH ENTIRELY, testing bits 3 and 16 rather
 * than indexing any table. So the local player's badge is decided by two
 * fixed bits and everyone else's by three tables, which reading the arms
 * without the tables would flatten into one rule.
 *
 * THE COORDINATE TABLE IS INDEXED BY THE ACTIVE PLAYER COUNT, not by slot:
 * four rows of four (x,y) pairs at 0x0048C768, giving x = {0}, {0,366},
 * {0,183,366}, {0,122,244,366} for one to four players, all at y = 21. So
 * the columns spread as players join, and DrawLatencyBar's `slot + row * 4`
 * indexes the same table with the same row -- which is what confirms both
 * readings.
 *
 * FOUR LOCK/UNLOCK BRACKETS, one per text block, and each is checked: a
 * failed lock RETURNS rather than skipping the draw, so the remaining rows
 * are abandoned too. That is the original's and it is why the function has
 * one exit despite four places that can fail.
 *
 * The team-mate count decides the marker's size -- 4 when a slot is alone on
 * its team and 3 when it is not -- and a slot with no team at all skips the
 * count entirely rather than counting zero.
 *
 * THE COLUMN ADVANCES ONLY FOR A SLOT THAT IS DRAWN. An empty slot's
 * `continue` jumps past the image's `add esi, 4`, so the active players fill
 * the row's columns in order however the slots are numbered, which is what a
 * row chosen by the active count needs. The game-type line reads the row's
 * start, kept in its own slot across the loop. And a paused slot (variant 2
 * or below) skips only its score text: the name box below it is still drawn.
 * An earlier reading had all three wrong -- a per-slot column, the advanced
 * pointer for the bottom line, and a `continue` past the name box -- and the
 * byte comparison is what showed them. */
void __cdecl PausedFrameStep(void)
{
    /* The image's three per-slot pause-bit tables, each its own global. */
#define kPauseBitsA ((const uint32_t *)(uintptr_t)ADDR_MP_PAUSE_BITS_A)
#define kPauseBitsB ((const uint32_t *)(uintptr_t)ADDR_MP_PAUSE_BITS_B)
#define kPauseBitsC ((const uint32_t *)(uintptr_t)ADDR_MP_PAUSE_BITS_C)

    /* The comm object is read at every use. */
#define comm (g_commObject)
    const int16_t *row;
    const int16_t *xy;
    int32_t        active;
    int32_t        idx;
    int32_t        slot;
    int32_t        n;

    if (g_mpSession == 0)
        return;

    active = 0;
    for (n = 0; n < 4; n++) {
        if (*(const int32_t *)(comm + COMM_OFF_SLOT_FIELD_25C
                               + n * COMM_PLAYER_STRIDE) != 0)
            active++;
    }

    idx = active - 1;
    row = (const int16_t *)(g_mpRowCoords
                            + (uint32_t)idx * 16u);
    xy = row;

    for (slot = 0; slot < 4; slot++) {
#define rec (comm + slot * COMM_PLAYER_STRIDE)
        int32_t  team;
        int32_t  mates;
        int32_t  variant;
        int32_t  badge;
        char     scoreText[20];
        char     teamText[20];
        int32_t  width;

        if (*(const int32_t *)(rec + COMM_OFF_SLOT_FIELD_25C) == 0)
            continue;

        team  = *(const int32_t *)(rec + COMM_OFF_SLOT_FIELD_258);
        mates = 0;
        if (team > 0) {
            for (n = 0; n < 4; n++) {
                const uint8_t *o = comm + COMM_OFF_SLOT_FIELD_258
                                   + n * COMM_PLAYER_STRIDE;
                if (*(const int32_t *)(o + 4) != 0
                    && *(const int32_t *)o == team)
                    mates++;
            }
        }

        badge   = 0;
        variant = 4;
        if (mates > 1)
            variant = 3;

        /* The pause flags are read afresh at each test. */
#define pause (g_eventFlags)
        if (slot == g_ourSlot) {
            if (pause & 8u)
                badge = 1;
            else if (pause & 0x10000u)
                variant = 0;
            else if (*(const int32_t *)(rec + 0x274u) == 0
                     && g_netGame != 0)
                variant = 1;
        } else if (pause & kPauseBitsB[slot]) {
            badge = 1;
        } else if (pause & kPauseBitsA[slot]) {
            variant = 2;
        } else if (pause & kPauseBitsC[slot]) {
            variant = 0;
        } else if (*(const int32_t *)(rec + 0x274u) == 0
                   && g_netGame != 0) {
            variant = 1;
        }

        DrawSprite((g_mpMarkGrid)
                       [((AM2_Comm *)comm)->CommArmyOfSlot(slot) * 5 + variant],
                   xy[0], xy[1], 0);

        if (*(const int32_t *)(rec + COMM_OFF_SLOT_FIELD_258) != 0) {
            /* The team sprite sits 0x5E right of the marker for the small
             * (team) marker and 0x3E for any other. The image's `sub 3; neg;
             * sbb; and ~0x1F; add 0x5E` is VC6's branchless form of a
             * two-constant ternary, not a rounding of x. */
            DrawSprite((g_mpTeamSprites)
                           [*(const int32_t *)(rec + COMM_OFF_SLOT_FIELD_258)],
                       (variant == 3 ? 0x5E : 0x3E) + xy[0],
                       xy[1] + 3, 0);
        }

        if (badge)
            DrawSprite(variant == 3 ? g_mpMarkA : g_mpMarkB,
                       xy[0], xy[1], 0);

        if (((AM2_Comm *)comm)->CommSlotHasPlayer(slot))
            DrawLatencyBar(idx, slot);
        else
            DrawSprite(g_mpMarkC,
                       xy[0] + 0x37, xy[1] + 3, 0);

        if (variant > 2) {
            am2_sprintf(scoreText, "%d",
                        GetArmyScore(comm, slot));
            width = TextExtent(scoreText, 1, NULL);
            if (!LockSurface(*(LPDIRECTDRAWSURFACE *)&g_drawTarget))
                return;
            DrawText(xy[0] - width + 0x32, xy[1] + 4, scoreText, 1, 0,
                     g_hiliteColour);
            UnlockSurface();

            if (mates > 1) {
                am2_sprintf(teamText, "%d",
                            ((AM2_Comm *)comm)->CommTeamScore(slot));
                width = TextExtent(teamText, 1, NULL);
                if (!LockSurface(
                        *(LPDIRECTDRAWSURFACE *)&g_drawTarget))
                    return;
                DrawText(xy[0] - width + 0x58, xy[1] + 4, teamText, 1, 0,
                         g_hiliteColour);
                UnlockSurface();
            }
        }

        if (g_infoOverlayOn) {
            RECT clip;

            /* The rect is built from its own corner, and the text sits two
             * pixels inside that corner. */
            clip.left   = xy[0];
            clip.top    = xy[1] + 0x14;
            clip.right  = clip.left + 0x6E;
            clip.bottom = clip.top + 0x14;
            if (!LockSurface(
                    *(LPDIRECTDRAWSURFACE *)&g_drawTarget))
                return;
            DrawTextClipped(clip.left + 2, clip.top + 2,
                            (const char *)(rec + 0x218u), 1, clip,
                            g_whiteInk);
            UnlockSurface();
        }
        xy += 2;
    }

    if (g_infoOverlayOn) {
        char buf[300];
        RECT clip;

        clip.top    = row[1] + 0x24;
        clip.left   = row[0];
        clip.right  = clip.left + 0x190;
        clip.bottom = clip.top + 0x14;
        am2_sprintf(buf, "Game type: %s, Goal: %d",
                    (const char *)ScriptListFind(
                        &g_mpScriptName) + 0x80,
                    g_scoreLimit);
        if (!LockSurface(*(LPDIRECTDRAWSURFACE *)&g_drawTarget))
            return;
        DrawTextClipped(clip.left + 2, clip.top + 2, buf, 1, clip,
                        g_whiteInk);
        UnlockSurface();
    }
#undef comm
#undef rec
#undef pause
#undef kPauseBitsA
#undef kPauseBitsB
#undef kPauseBitsC
}
