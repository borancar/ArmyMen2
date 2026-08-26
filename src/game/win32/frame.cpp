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
#include "surface.h"
#include "../../inject/orig.h"
#include "../../inject/patch.h"
#include "../gameproc.h"  /* RequestState -- reconstructed */
#include "../misc.h"      /* IsKeyDown, KeyChanged -- reconstructed */
#include "sprite.h"       /* DrawSprite -- reconstructed */
#include "mapdraw.h"      /* SetDrawTarget -- reconstructed */
#include "../image.h"     /* AM2_IMAGE */
#include "../gamedir.h"   /* SetGameDir -- reconstructed */
#include "winmain.h"      /* Ticks -- reconstructed */
#include "audio.h"       /* PlayDynamicSound, StopNamedSound */
#include "sprite.h"      /* FreeBitmap -- reconstructed */

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
typedef void   *(__cdecl *AM2_LoadBitmapFn2)(const char *name, int32_t flag);
typedef int32_t (__cdecl *AM2_EventFlag8Fn)(void);
#define orig_action_released ((AM2_ActionKeyFn)(uintptr_t)ADDR_ACTION_KEY_RELEASED)
/* The camera pair, reached by cast for the same reason mapdraw.cpp does it:
 * audio.cpp already owns the g_ name on ADDR_LISTENER_POS. */
#define g_mpSession     (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)
#define g_currentBitmap (*(void **)(uintptr_t)ADDR_CURRENT_BITMAP)
#define VIEW_TARGET ((AM2_Point *)(uintptr_t)ADDR_VIEW_TARGET)
#define VIEW_EYE2   ((const AM2_Point *)(uintptr_t)ADDR_LISTENER_POS)
#define orig_show_info_mp    ((AM2_VoidFn0b)(uintptr_t)ADDR_SHOW_INFO_MP)
#define orig_load_bitmap2    ((AM2_LoadBitmapFn2)(uintptr_t)ADDR_LOAD_BITMAP)
#define orig_event_flag8     ((AM2_EventFlag8Fn)(uintptr_t)ADDR_EVENT_FLAG_8_TEST)

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
        if (orig_action_released(AM2_ACTION_SHOW_INFO)) {
            if (*(const char *)(uintptr_t)ADDR_LEVEL_STR_C) {
                SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));
                FreeBitmap(&g_currentBitmap);
                g_currentBitmap = orig_load_bitmap2(
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
    } else if (orig_action_released(AM2_ACTION_SHOW_INFO)) {
        orig_show_info_mp();
    }

    if (*(void *const *)(uintptr_t)ADDR_CHAR_HANDLER)
        return;
    if (*(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)
        return;

    /* flag8 OR (button AND changed) -- the mouse pair is an AND, which is what
     * makes this "a click just happened" rather than "the mouse did anything".
     * Getting it wrong dismisses the sign on the first mouse MOVE. */
    if (g_currentBitmap
        && (orig_event_flag8()
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
typedef void *(__cdecl *AM2_LoadBitmapFn)(const char *name, int32_t flag);
#define orig_paused_frame_step \
    ((AM2_VoidFn0)(uintptr_t)ADDR_PAUSED_FRAME_STEP)
#define orig_load_bitmap  ((AM2_LoadBitmapFn)(uintptr_t)ADDR_LOAD_BITMAP)

#define g_currentBitmap (*(void **)(uintptr_t)ADDR_CURRENT_BITMAP)

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
    g_currentBitmap = orig_load_bitmap(
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
        call0(ADDR_COMM_NO_BUFFERS);
}

/* 0x004266B0. State 0 -- the intro. Leaving wins over entering, and the entry
 * action stamps the tick count that the rest of the state measures against. */
void __cdecl State0Frame(void)
{
    if (g_statePending) {
        call0(ADDR_STATE_LEAVE_COMMON);
        return;
    }

    if (g_stateEntered) {
        call0(ADDR_STATE0_ENTER);
        *(uint32_t *)(uintptr_t)ADDR_STATE0_TICK = GetTickCount();
    }

    MovieStepCurrent();
    call0(ADDR_STATE_FRAME_COMMON);
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
        call0(ADDR_STATE1_ENTER);

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

static const uint32_t kSubStatePainter[] = {
    0x00452F50u,           /* 23 */
    0x00452990u,           /* 24 */
    0x00453890u,           /* 25 */
    0x00452680u,           /* 26 */
    0x0044F9E0u,           /* 27 */
    0x00451210u,           /* 28 */
    0x00450250u,           /* 29 */
    0x00450250u,           /* 30 */
    0x004506A0u,           /* 31 */
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
            call0(ADDR_TAKE_MENU_REQUEST);
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
        call0(ADDR_SUBSTATE22);
    } else if (arm >= AM2_SUBSTATE_PAINTED_FIRST
               && arm <= AM2_SUBSTATE_PAINTED_LAST) {
        uint32_t painter = kSubStatePainter[arm - AM2_SUBSTATE_PAINTED_FIRST];

        if (g_overlayDirty)
            call0(painter);
        DrawMenuOverlay();
    } else if (arm == 11) {
        /* 33 -- ordinary play, and the arm Boot Camp sits in the whole time. */
        if (GetPauseFlags())
            MissionPausedFrame();
        else
            call0(ADDR_TAKE_MENU_REQUEST);
    } else if (arm == 12) {
        call0(ADDR_SUBSTATE34_ESCAPE);
    }

    if ((GetPauseFlags() & AM2_EVENT_FLAG_8)
        && ((am2_int_fn)(uintptr_t)ADDR_EVENT_FLAG_8_TEST)()) {
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
        call0(ADDR_STATE3_ENTER);

    if (g_statePending) {
        call0(ADDR_STATE_LEAVE_COMMON);
        return;
    }

    MovieStepCurrent();
    call0(ADDR_STATE_FRAME_COMMON);
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
