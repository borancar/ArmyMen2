/* frame.cpp -- see frame.h. */
#include "../../inject/win32.h"

#include <stdint.h>

#include "frame.h"
#include "device.h"
#include "mapdraw.h"
#include "palette.h"
#include "surface.h"
#include "../../inject/orig.h"
#include "../../inject/patch.h"

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_void_fn)(void);
typedef void    (__cdecl *am2_i32_fn)(int32_t);
typedef void    (__cdecl *am2_i32x2_fn)(int32_t, int32_t);
typedef int32_t (__cdecl *am2_int_fn)(void);
typedef int32_t (__cdecl *am2_list_fn)(void *list);

#define call0(a)  (((am2_void_fn)(uintptr_t)(a))())
#define orig_event_flags   (*(am2_int_fn)ADDR_GET_EVENT_FLAGS)
#define orig_msg_list_free (*(am2_list_fn)ADDR_MSG_LIST_FREE)

#define g_comm         (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_statePending (*(const int32_t *)(uintptr_t)ADDR_STATE_PENDING)
#define g_stateEntered (*(int32_t *)(uintptr_t)ADDR_STATE_ENTERED)
#define g_subState     (*(const int32_t *)(uintptr_t)ADDR_MENU_MODE)
#define g_overlayDirty (*(const int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY)

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
    if (CommActive() && !orig_event_flags())
        call0(ADDR_COMM_DRAIN_MSGS);

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
    if (CommActive() && !orig_event_flags())
        ((am2_i32_fn)(uintptr_t)ADDR_COMM_FRAME_POST_A)(0);

    if (!CommActive())
        return;

    call0(ADDR_COMM_FRAME_POST_B);
    call0(ADDR_COMM_FRAME_POST_C);

    if (orig_msg_list_free((void *)(uintptr_t)ADDR_MSG_LIST_POOL)
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

    call0(ADDR_MOVIE_FRAME_STEP);
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

    if (!orig_event_flags()) {
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
        skipDispatch = orig_event_flags() != 0;
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
        if (orig_event_flags())
            call0(ADDR_SUBSTATE33_ALT);
        else
            call0(ADDR_TAKE_MENU_REQUEST);
    } else if (arm == 12) {
        call0(ADDR_SUBSTATE34_ESCAPE);
    }

    if ((orig_event_flags() & AM2_EVENT_FLAG_8)
        && ((am2_int_fn)(uintptr_t)ADDR_EVENT_FLAG_8_TEST)()) {
        ((am2_i32_fn)(uintptr_t)ADDR_CLEAR_EVENT_FLAGS)(AM2_EVENT_FLAG_8);
        ((am2_i32x2_fn)(uintptr_t)ADDR_EVENT_FLAG_8_SEND)(0, AM2_EVENT_FLAG_8);
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

    call0(ADDR_MOVIE_FRAME_STEP);
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
    return rc;
}
