/* frame.cpp -- see frame.h. */
#include "../msgslot.h"
#include "../commmsg.h"  /* CommDrainMsgs -- reconstructed */
#include "../../inject/win32.h"

#include <stdint.h>

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
#include "../misc.h"      /* IsKeyDown, KeyChanged -- reconstructed */
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

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_void_fn)(void);
typedef void    (__cdecl *am2_i32_fn)(int32_t);
typedef void    (__cdecl *am2_i32x2_fn)(int32_t, int32_t);
typedef int32_t (__cdecl *am2_int_fn)(void);
typedef int32_t (__cdecl *am2_list_fn)(void *list);

#define call0(a)  (((am2_void_fn)(uintptr_t)(a))())

#define g_comm         (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_statePending (*(int32_t *)(uintptr_t)ADDR_STATE_PENDING)
#define g_stateEntered (*(int32_t *)(uintptr_t)ADDR_STATE_ENTERED)
#define g_subState      (*(int32_t *)(uintptr_t)ADDR_MENU_MODE)
#define g_overlayDirty (*(const int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY)

typedef int32_t (__cdecl *AM2_ActionKeyFn)(int32_t action);
typedef void    (__cdecl *AM2_VoidFn0b)(void);
typedef int32_t (__cdecl *AM2_EventFlag8Fn)(void);
/* The camera pair, reached by cast for the same reason mapdraw.cpp does it:
 * audio.cpp already owns the g_ name on ADDR_LISTENER_POS. */
#define g_mpSession     (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)
#define g_currentBitmap (*(void **)(uintptr_t)ADDR_CURRENT_BITMAP)
/* Spelled exactly as surface.cpp spells them; checkglobals enforces that. */
#define g_primarySurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_backBuffer     (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER)
#define VIEW_TARGET ((AM2_Point *)(uintptr_t)ADDR_VIEW_TARGET)
#define VIEW_EYE2   ((const AM2_Point *)(uintptr_t)ADDR_LISTENER_POS)


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
    int16_t cx, cy;

    if (!IsKeyDown(AM2_DIK_ESCAPE) && KeyChanged(AM2_DIK_ESCAPE)) {
        *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 1;
        *(int32_t *)(uintptr_t)ADDR_MENU_MODE = AM2_STATE_ESCAPE_MENU;
        return;
    }

    if (!g_mpSession) {
        if (ActionKeyReleased(AM2_ACTION_SHOW_INFO)) {
            if (*(const char *)(uintptr_t)ADDR_LEVEL_STR_C) {
                SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));
                FreeBitmap(&g_currentBitmap);
                g_currentBitmap = LoadBitmap(
                    (const char *)(uintptr_t)ADDR_LEVEL_STR_C, 0);
                *(int32_t *)(uintptr_t)ADDR_MENU_MODE =
                    AM2_SUBSTATE_INFO_BITMAP;
                *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 1;
                SendGamePause(1, 8);
            }
            if (*(const char *)(uintptr_t)ADDR_LEVEL_SOUND_NAME)
                PlayDynamicSound(
                    (const char *)(uintptr_t)ADDR_LEVEL_SOUND_NAME,
                    0, 0, 0, 0, 1, 0, 0);
            LatchKeyState();
        }
    } else if (ActionKeyReleased(AM2_ACTION_SHOW_INFO)) {
        ShowInfoMp();
    }

    if (*(void *const *)(uintptr_t)ADDR_CHAR_HANDLER)
        return;
    if (*(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)
        return;

    /* flag8 OR (button AND changed) -- the mouse pair is an AND, which is what
     * makes this "a click just happened" rather than "the mouse did anything".
     * Getting it wrong dismisses the sign on the first mouse MOVE. */
    if (g_currentBitmap
        && (DismissKeyReleased()
            || (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
                && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED))) {
        *(int32_t *)(uintptr_t)ADDR_MOUSE_CLAIMED = 1;
        FreeBitmap(&g_currentBitmap);
        if (*(const char *)(uintptr_t)ADDR_LEVEL_SOUND_NAME)
            StopNamedSound((const char *)(uintptr_t)ADDR_LEVEL_SOUND_NAME, 0);
    }

    /* Both set means give up: something is being followed AND the button is
     * held, so a drag must not fight the follow. Not `!button` -- that
     * inversion scrolls exactly when it should not, and is what the frame
     * counts caught. */
    if (*(const int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A
        && *(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
        return;

    step = (int32_t)((double)*(const int32_t *)(uintptr_t)ADDR_VIEW_SPEED
                     * (double)*(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC);

    cx = *(const int16_t *)(uintptr_t)ADDR_CURSOR_POINT;
    cy = *(const int16_t *)((uintptr_t)ADDR_CURSOR_POINT + 2);

    if (cx > *(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W
              - AM2_SCROLL_MARGIN) {
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 0;
        VIEW_TARGET->x = (int16_t)(VIEW_EYE2->x + step);
    }
    if (cx < AM2_SCROLL_MARGIN) {
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 0;
        VIEW_TARGET->x = (int16_t)(VIEW_EYE2->x - step);
    }
    if (cy > *(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H
              - AM2_SCROLL_MARGIN) {
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 0;
        VIEW_TARGET->y = (int16_t)(VIEW_EYE2->y + step);
    }
    if (cy < AM2_SCROLL_MARGIN) {
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET = 0;
        VIEW_TARGET->y = (int16_t)(VIEW_EYE2->y - step);
    }
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
    uint32_t *clock   = (uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS);
    uint32_t *last    = (uint32_t *)(uintptr_t)ADDR_LAST_TICK_MS;
    int32_t  *deltaMs = (int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;
    uint32_t  delta;

    if (*(const int32_t *)(uintptr_t)ADDR_FIXED_STEP) {
        *last = *clock;
        delta = AM2_FIXED_STEP_MS;
        *deltaMs = (int32_t)delta;
    } else {
        uint32_t now = Ticks();

        delta    = now - *last;
        *last    = now;
        *deltaMs = (int32_t)delta;          /* the raw one... */
        if (delta > AM2_FRAME_DELTA_CAP_MS) {
            delta    = AM2_FRAME_DELTA_CAP_MS;
            *deltaMs = (int32_t)delta;      /* ...then the clamp over it */
        }
    }

    *clock += delta;

    /* The original converts the delta to a 64-bit integer, multiplies in
     * DOUBLE precision -- `fmul` against a dword operand widens it -- and
     * stores a float. Written the same way round rather than as a float
     * multiply, which would round twice. */
    *(float *)(uintptr_t)ADDR_FRAME_DELTA_SEC =
        (float)((double)delta
                * (double)*(const float *)AM2_IMAGE(ADDR_MS_TO_SEC));
}

typedef void  (__cdecl *AM2_VoidFn0)(void);
#define orig_paused_frame_step \
    ((AM2_VoidFn0)(uintptr_t)ADDR_PAUSED_FRAME_STEP)

#define g_currentBitmap (*(void **)(uintptr_t)ADDR_CURRENT_BITMAP)
/* Spelled exactly as surface.cpp spells them; checkglobals enforces that. */
#define g_primarySurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_backBuffer     (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER)

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
    void *bmp;

    if (!IsKeyDown(AM2_DIK_ESCAPE) && KeyChanged(AM2_DIK_ESCAPE)) {
        RequestState(1);
        UnPauseGame(*(const uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS);
        return;
    }

    SetDrawTarget(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE);
    orig_paused_frame_step();

    if (!(*(const uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS & AM2_PAUSE_MAP_WAIT))
        return;
    if (g_currentBitmap)
        return;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));
    FreeBitmap(&g_currentBitmap);
    g_currentBitmap = LoadBitmap(
        (const char *)AM2_IMAGE(ADDR_STR_MAPWAIT_BMP), 0);

    SetDrawTarget(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE);

    bmp = g_currentBitmap;
    DrawSprite((AM2_Sprite *)bmp,
               (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W
                - *(const int32_t *)((const uint8_t *)bmp + SPR_OFF_W)) >> 1,
               (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H
                - *(const int32_t *)((const uint8_t *)bmp + SPR_OFF_H)) >> 1,
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
    return *(const uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS;
}

void __cdecl PauseGame(uint32_t bits)
{
    uint32_t *flags = (uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS;

    *flags |= bits;
    if (*(const int32_t *)(g_comm + AM2_COMM_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_PAUSE_GAME, *flags, bits);
}

void __cdecl UnPauseGame(uint32_t bits)
{
    uint32_t *flags = (uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS;

    *flags &= ~bits;
    if (*(const int32_t *)(g_comm + AM2_COMM_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_UNPAUSE_GAME, *flags, bits);
}

/* Every comm step in the pre and post is behind the same field. */
static int32_t CommActive(void)
{
    return *(const int32_t *)(g_comm + AM2_COMM_OFF_ACTIVE) != 0;
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

    call0(ADDR_COMM_FRAME_PRE_A);
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
        ((am2_i32_fn)(uintptr_t)ADDR_ARMY_MESSAGE_FLUSH)(0);

    if (!CommActive())
        return;

    call0(ADDR_COMM_FRAME_POST_B);
    call0(ADDR_COMM_FRAME_POST_C);

    if (MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_POOL)
        < AM2_COMM_MIN_BUFFERS)
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
        call0(ADDR_STATE_LEAVE_COMMON);
        return;
    }

    if (g_stateEntered) {
        StateEnter0();
        *(uint32_t *)(uintptr_t)ADDR_STATE0_TICK = GetTickCount();
    }

    MovieStepCurrent();
    call0(ADDR_STATE_FRAME_COMMON);
}

/* The two still-original callees this needs. The digit table is 0x00412E00 --
 * it fills 0x004FCDF8 with a run of bytes and calls one more function; what
 * that table is FOR is not established, only that entering the title rebuilds
 * it. */
typedef void (__cdecl *AM2_VoidFn0)(void);
#define orig_init_digit_table ((AM2_VoidFn0)(uintptr_t)ADDR_INIT_DIGIT_TABLE)

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
    SetGameDir(*(const char *const *)(uintptr_t)ADDR_DIR_TITLE_PTR);
    LoadPaletteFile((const char *)(uintptr_t)ADDR_STR_SCREEN_BMP,
                    *(void **)(uintptr_t)ADDR_ACTIVE_PALETTE);
    SetGamePalette(*(uint8_t **)(uintptr_t)ADDR_ACTIVE_PALETTE);

    BuildFontAlias(1);
    BuildFontAlias(2);
    orig_init_digit_table();

    *(int32_t *)(uintptr_t)ADDR_PRESENT_ENABLED = 1;
    *(int32_t *)(uintptr_t)ADDR_STATE_ENTERED   = 0;
    RefreshGate(1);

    {
        const uint8_t *comm = g_comm;   /* the POINTER, dereferenced */

        if (*(const int32_t *)(comm + COMM_OFF_LOBBIED)) {
            if (*(const int32_t *)(comm + COMM_OFF_IS_HOST)) {
                *(int32_t *)(uintptr_t)ADDR_MENU_MODE = AM2_MENU_MODE_LOBBY_HOST;
                *(int32_t *)(uintptr_t)ADDR_MP_SESSION = 1;
            } else {
                *(int32_t *)(uintptr_t)ADDR_MENU_MODE = AM2_MENU_MODE_LOBBY_JOIN;
                *(int32_t *)(uintptr_t)ADDR_MP_SESSION = 2;
            }
        } else {
            int32_t request = *(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST;

            if (request == 7 || request == 9 || request == 13
                || request == 18) {
                *(int32_t *)(uintptr_t)ADDR_MENU_MODE    = request;
                *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST = 1;
            } else {
                *(int32_t *)(uintptr_t)ADDR_MENU_MODE = AM2_MENU_MODE_TITLE;
            }
        }
    }

    *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 1;
    StartAudioStream((void *)(uintptr_t)ADDR_STR_TITLE_WAV, 0);
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
        call0(ADDR_STATE1_LEAVE);
        return;
    }

    if (g_stateEntered)
        State1Enter();

    int32_t *requestSet = (int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET;

    if (*requestSet) {
        *(int32_t *)(uintptr_t)ADDR_MENU_MODE =
            *(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST;
        *requestSet = 0;
        *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 1;
        call0(ADDR_STATE1_MENU);
    } else if (g_overlayDirty) {
        call0(ADDR_STATE1_MENU);
    }

    if (!GetPauseFlags()) {
        SetDrawTarget(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE);

        void *obj = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;

        if (obj) {
            void **vtable = *(void ***)obj;

            ((void (__attribute__((thiscall)) *)(void *))vtable[2])(obj);
        }
    }

    call0(ADDR_STATE1_COMMON);
}

/* Nine of the thirteen sub-state arms are one shape -- repaint if the overlay
 * is dirty, then DrawMenuOverlay -- differing only in which painter they call.
 * Those are the table; 22, 32, 33 and 34 are each their own thing and are
 * written out. Arms 29 and 30 really do share a painter.
 *
 * The table is in TABLE order, which is not the order the arms appear in the
 * code: the linker laid the bodies out 27, 28, 26, 29, 30, 31, 25, so reading
 * them top to bottom and numbering as you go gets four of them wrong. Take the
 * order from the jump table at ADDR_SUBSTATE_TABLE. */
#define AM2_SUBSTATE_PAINTED_FIRST 1     /* arm 23 */
#define AM2_SUBSTATE_PAINTED_LAST  9     /* arm 31 */

/* FIVE OF THE NINE ARE OURS AND WERE REACHED THROUGH THE IMAGE, which nothing
 * could see while the table held plain integers. A bare hex literal is not an
 * ADDR_ name, so checkseams' "named by address" rule -- whose own docstring
 * claimed to cover "a table of plain integers that are function pointers" --
 * walked straight past all five. It has a rule for the literal now, and this
 * table is why.
 *
 * ALL NINE ARE NAMED, so nothing here reaches the image and the shape says
 * nothing -- there is no mixed case left to read. The rule is for the future:
 * if an arm ever goes back to being the original's, write it as an ADDR_ name
 * cast through AM2_IMAGE and never as a bare integer, because that is the one
 * spelling checkseams could not see and it is how five of these hid. */
typedef void (__cdecl *AM2_SubStatePainter)(void);

static const AM2_SubStatePainter kSubStatePainter[] = {
    OpenGameMenu,                                 /* 23 */
    OpenMessage,                                  /* 24 */
    OpenSaveGame,                                 /* 25 */
    OpenLoadGame,                                 /* 26 */
    OpenAudioOptions,                             /* 27 */
    OpenControls,                                 /* 28 */
    OpenDeleteGame,                               /* 29 */
    OpenDeleteGame,                               /* 30 */
    OpenOverwriteGame,                            /* 31 */
};


/* 0x004260C0. State 2 -- a live mission.
 *
 * The entry action has an early RETURN in it that the other states do not:
 * with ADDR_STATE_ENTER_ONCE set it consumes a menu request, clears the flag
 * and leaves the frame there, so the sub-state dispatch below is skipped
 * entirely for that one frame. */
void __cdecl State2Frame(void)
{
    if (g_statePending) {
        call0(ADDR_LEVEL_TEARDOWN);
        return;
    }

    int32_t skipDispatch = 0;

    if (g_stateEntered) {
        int32_t *once = (int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE;

        call0(ADDR_STATE2_ENTER);
        if (*once) {
            TakeMenuRequest();
            *once = 0;
            return;
        }
        skipDispatch = GetPauseFlags() != 0;
    }

    if (skipDispatch)
        return;

    uint32_t arm = (uint32_t)(g_subState - AM2_SUBSTATE_BASE);

    /* Out of range falls straight through to the flag-8 block below -- the
     * original's `ja` skips the switch and lands on the common tail, so an
     * unknown sub-state is not an error. Arm 32 is in range and does the same
     * nothing, by pointing at that tail in the table. */
    if (arm == 0) {
        Substate22();
    } else if (arm >= AM2_SUBSTATE_PAINTED_FIRST
               && arm <= AM2_SUBSTATE_PAINTED_LAST) {
        AM2_SubStatePainter painter =
            kSubStatePainter[arm - AM2_SUBSTATE_PAINTED_FIRST];

        if (g_overlayDirty)
            painter();
        DrawMenuOverlay();
    } else if (arm == 11) {
        /* 33 -- ordinary play, and the arm Boot Camp sits in the whole time. */
        if (GetPauseFlags())
            MissionPausedFrame();
        else
            TakeMenuRequest();
    } else if (arm == 12) {
        Substate34Escape();
    }

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
        call0(ADDR_STATE_LEAVE_COMMON);
        return;
    }

    MovieStepCurrent();
    call0(ADDR_STATE_FRAME_COMMON);
}

typedef void (__cdecl *AM2_NoArgFn)(void);
typedef void (__attribute__((thiscall)) *AM2_PaintUpdateFn)(void *self);
#define orig_log_noargs   ((AM2_NoArgFn)(uintptr_t)ADDR_LOG)

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
    if (*(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET) {
        *(int32_t *)(uintptr_t)ADDR_MENU_MODE =
            *(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST;
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST     = 0;
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET = 0;
        *(int32_t *)(uintptr_t)ADDR_STATE_PENDING    = 1;
        return;
    }

    if (!*(const int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY) {
        if (*(const int32_t *)(uintptr_t)ADDR_SCRIPT_RELOAD) {
            int32_t dbg = *(const int32_t *)(uintptr_t)ADDR_OPT_DBG;

            if (dbg)
                *(int32_t *)(uintptr_t)ADDR_SCRIPT_RELOADING = 1;

            ReadScript((const char *)(uintptr_t)ADDR_SCRIPT_RELOAD_PATH,
                       (AM2_ScriptCtx *)(uintptr_t)ADDR_SCRIPT_CONTEXT);
            DeclareRuleVars();
            MissionStartup();

            *(int32_t *)(uintptr_t)ADDR_SCRIPT_RELOAD = 0;
            if (!dbg)
                return;

            *(int32_t *)(uintptr_t)ADDR_SCRIPT_RELOADING = 0;
            *(int32_t *)(uintptr_t)ADDR_MENU_MODE = AM2_SUBSTATE_INFO_BITMAP;
            *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 1;
            return;
        }
    } else {
        if (!*(const int32_t *)(uintptr_t)ADDR_LOAD_PENDING) {
            *(int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS = 0;
            *(uint32_t *)(uintptr_t)ADDR_CLOCK_BASE_MS =
                *(const int32_t *)(uintptr_t)ADDR_FIXED_STEP ? 0u : Ticks();
            *(uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS = 0;
            MissionStartup();
        }

        if (g_mpSession)
            *(int32_t *)(uintptr_t)ADDR_NET_SETTLE_COUNT = 1;

        *(uint32_t *)(uintptr_t)ADDR_LAST_TICK_MS =
            *(const int32_t *)(uintptr_t)ADDR_FIXED_STEP ? 0u : Ticks();

        orig_log_noargs();      /* no arguments -- the original's, see above */

        *(int32_t *)(uintptr_t)ADDR_SCRIPT_RELOAD = 0;
        *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 0;
        *(int32_t *)(uintptr_t)ADDR_LOAD_PENDING  = 0;
    }

    if (!*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        FrameClockStep();
        TimerTick();
    }

    MissionInput();

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        *(uint32_t *)(uintptr_t)ADDR_FRAME_DELTA_SEC = AM2_NET_FRAME_DELTA_SEC;

        if (g_mpSession) {
            int32_t n = *(const int32_t *)(uintptr_t)ADDR_NET_SETTLE_COUNT;

            if (n > 0) {
                n++;
                *(int32_t *)(uintptr_t)ADDR_NET_SETTLE_COUNT = n;
                if (n == AM2_NET_SETTLE_FRAMES) {
                    *(int32_t *)(uintptr_t)ADDR_NET_SETTLE_COUNT = 0;
                    if (*(const int32_t *)(g_comm + COMM_OFF_IS_HOST))
                        AiTakeAbandoned();
                }
            }
        }
    }

    RefreshObjCtx();
    HudUpdate();
    ClearFrameCounts();

    if (!*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        ObjFrameSweep();
    } else {
        void *paint = *(void *const *)(uintptr_t)ADDR_PAINT_OBJECT;

        if (paint)
            ((AM2_PaintUpdateFn *)*(void **)paint)[2](paint);
    }

    PadAdvanceDeadlines();
    Update3DAudioVolumes();
    ViewUpdate();
    FreeOverdueItems();
    SeqRunBoth();
    FlameTick();
    AdvanceSecondDeadline();

    if (*(const int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE) {
        HudPaint();
        PresentFrame();
        return;
    }
    RefreshDraw();
}

/* Spelled exactly as surface.cpp, mapdraw.cpp and palette.cpp spell it, so the
 * four stay one definition; checkglobals enforces that. */
#define g_activePalette   (*(const uint32_t **)(uintptr_t)ADDR_ACTIVE_PALETTE)


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

    LoadPaletteFile((const char *)AM2_IMAGE(ADDR_STR_GREYSCALE_BMP),
                    (void *)g_activePalette);
    SetGamePalette((uint8_t *)(uintptr_t)g_activePalette);
    ClearBothSurfaces();
    RefreshGate(0);

    MovieBuildName(name, (const char *)(uintptr_t)ADDR_MOVIE_TO_PLAY);
    PlayMovie(name, 0);

    LatchKeyState();

    *(uint32_t *)(uintptr_t)ADDR_STATE0_TICK   = Ticks();
    *(int32_t  *)(uintptr_t)ADDR_STATE_ENTERED = 0;
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

    LoadPaletteFile((const char *)AM2_IMAGE(ADDR_STR_GREYSCALE_BMP),
                    (void *)g_activePalette);
    SetGamePalette((uint8_t *)(uintptr_t)g_activePalette);
    ClearBothSurfaces();
    RefreshGate(0);

    over = GameOverState();
    ((const AM2_StateAction *)(uintptr_t)ADDR_STATE_ACTIONS)[over].onEnter();

    *(int32_t *)(uintptr_t)ADDR_STATE_ENTERED = 0;
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
    if (*(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET) {
        FreeBitmap(&g_currentBitmap);
        *(int32_t *)(uintptr_t)ADDR_MENU_MODE        = AM2_SUBSTATE_PLAY;
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET = 0;
        *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY    = 0;
        return;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY) {
        void *bmp = g_currentBitmap;

        *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 0;
        if (bmp) {
            int32_t w = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_W);
            int32_t h = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_H);

            SetDrawTarget(g_primarySurface);
            DrawSprite((AM2_Sprite *)bmp,
                       (*(const int32_t *)(uintptr_t)ADDR_SCREEN_W - w) >> 1,
                       (*(const int32_t *)(uintptr_t)ADDR_SCREEN_H - h) >> 1,
                       0);
        }
    }

    if (DismissKeyReleased()
        || (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
            && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)) {
        SendGamePause(0, AM2_EVENT_FLAG_8);
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET = 1;
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
    void *comm = *(void **)(uintptr_t)ADDR_COMM_OBJECT;

    UnPauseGame(AM2_MP_RESULT_UNPAUSE);
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));
    SetDrawTarget(g_primarySurface);

    orig_paused_frame_step();
    CommReportStats(comm);
    CommPublishResult(comm);

    if (result == AM2_MP_RESULT_WON) {
        FreeBitmap(&g_currentBitmap);
        g_currentBitmap =
            LoadBitmap((const char *)AM2_IMAGE(ADDR_STR_MP_WON), 0);
    } else if (result == AM2_MP_RESULT_LOST) {
        FreeBitmap(&g_currentBitmap);
        g_currentBitmap =
            LoadBitmap((const char *)AM2_IMAGE(ADDR_STR_MP_LOST), 0);
    } else if (result == AM2_MP_RESULT_HOST_LEFT) {
        FreeBitmap(&g_currentBitmap);
        g_currentBitmap =
            LoadBitmap((const char *)AM2_IMAGE(ADDR_STR_MP_HOST_LEFT), 0);
    }

    *(int32_t *)(uintptr_t)ADDR_MENU_MODE     = AM2_SUBSTATE_ESCAPE;
    *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 1;

    orig_log_noargs();
    CommReopenSession(comm);
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
    void *bmp;

    SetDrawTarget(g_backBuffer);
    CyclePalette();
    ComposeFrame();

    SetDrawTarget(g_backBuffer);
    AirFrameDraw();
    DrawEffectLayer();
    orig_paused_frame_step();

    SetDrawTarget(g_backBuffer);
    DrawSelection();
    DrawViewRect();

    orig_log_noargs();
    HudPaint();
    orig_log_noargs();

    bmp = g_currentBitmap;
    if (bmp
        && *(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST != AM2_SUBSTATE_BITMAP
        && *(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST != AM2_SUBSTATE_ESCAPE
        && !*(const int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY) {
        int32_t w = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_W);
        int32_t h = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_H);

        DrawSprite((AM2_Sprite *)bmp,
                   (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W - w) >> 1,
                   (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H - h) >> 1,
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
    if (*(const int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET) {
        const uint8_t *comm = *(const uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;

        FreeBitmap(&g_currentBitmap);
        RequestState(1);
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET = 0;
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST =
            *(const int32_t *)(comm + COMM_OFF_IS_HOST) ? AM2_MENU_REQ_MP_HOST
                                                        : AM2_MENU_REQ_MP_JOIN;
        return;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY) {
        void *bmp = g_currentBitmap;

        *(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY = 0;
        if (bmp) {
            int32_t w = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_W);
            int32_t h = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_H);

            SetDrawTarget(g_primarySurface);
            DrawSprite((AM2_Sprite *)bmp,
                       (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W - w) >> 1,
                       (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H - h) >> 1,
                       0);
        }
    }

    if (!IsKeyDown(AM2_DIK_ESCAPE) && KeyChanged(AM2_DIK_ESCAPE)) {
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST     = 0;
        *(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET = 1;
    }
}


/* 0x00426790. State 4 -- leaving. Posts WM_CLOSE to the game's own window and
 * clears the entered flag so it happens once, and there is nothing else in it:
 * the shutdown proper runs from WndProc. */
void __cdecl State4Frame(void)
{
    if (!g_stateEntered)
        return;

    PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND, WM_CLOSE, 0, 0);
    g_stateEntered = 0;
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
    rc |= patch_replace(ADDR_STATE1_ENTER, (const void *)State1Enter,
                        "State1Enter", 1);
    rc |= patch_replace(ADDR_STATE1_FRAME, (const void *)State1Frame,
                        "State1Frame", 1);
    rc |= patch_replace(ADDR_STATE2_FRAME, (const void *)State2Frame,
                        "State2Frame", 1);
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
    return rc;
}
