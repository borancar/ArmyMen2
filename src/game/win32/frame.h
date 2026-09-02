/* frame.cpp -- what RunFrame calls, one level down.
 *
 * Eight functions: the input poll, a pre- and post-step that are comm
 * bookkeeping, and the five per-state handlers RunFrame's jump table selects.
 * Together they are the shape of a frame.
 *
 * The five share a pattern worth naming once. Two globals drive them:
 * ADDR_STATE_PENDING means a state change has been asked for and the handler
 * hands over to a leave path, and ADDR_STATE_ENTERED means this is the first
 * frame in the state and the entry action has not run yet. States 0 and 3
 * check them in the opposite order to one another, which is not obviously
 * deliberate but is reproduced.
 *
 * State 2 is the mission, and it is the only one with a sub-state of its own:
 * a 13-entry table indexed from AM2_SUBSTATE_BASE. Ordinary play sits in 33,
 * which is why arm 34 -- the in-mission ESCAPE handler -- never runs. See
 * CLAUDE.md, where that was mapped before this file existed.
 */
#ifndef AM2_FRAME_H
#define AM2_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x00426840, 0x004267C0, 0x00426800. The pause mask and the two functions
 * that move it, both named from their own log lines. Each bit is a reason the
 * game is paused; the frame chain checks the mask before doing work. */
uint32_t __cdecl GetPauseFlags(void);
void     __cdecl PauseGame(uint32_t bits);
void     __cdecl UnPauseGame(uint32_t bits);

void __cdecl PollInput(void);
void __cdecl FramePre(void);
void __cdecl FramePost(void);
void __cdecl State0Frame(void);
void __cdecl State1Frame(void);
void __cdecl State2Frame(void);

/* 0x004256F0. Leaving a level: delete whatever dialog is up, then twenty-four
 * subsystem teardowns in the original's order, then commit the state change. */
void __cdecl LevelTeardown(void);
void __cdecl State3Frame(void);
void __cdecl State4Frame(void);

/* 0x00426A90. The multiplayer end screen. Its argument is a RESULT CODE --
 * 0 won, 1 lost, 2 host left -- and anything else leaves the bitmap alone. */
void __cdecl ShowMpResult(int32_t result);

/* 0x00424BF0. Repaints the whole screen -- what TakeMenuRequest does instead
 * of the ordinary present. Sets the draw target three times, because two of
 * the painters it calls retarget it. */
void __cdecl RefreshDraw(void);

int frame_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00462600, three callers. The multiplayer scoreboard overlay: a row per
 * comm slot with a team marker, a pause badge, a latency bar, the scores and
 * the game type. Four Lock/Unlock brackets, each checked. */
void __cdecl PausedFrameStep(void);

#endif /* AM2_FRAME_H */
