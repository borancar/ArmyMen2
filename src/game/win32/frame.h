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

void __cdecl PollInput(void);
void __cdecl FramePre(void);
void __cdecl FramePost(void);
void __cdecl State0Frame(void);
void __cdecl State1Frame(void);
void __cdecl State2Frame(void);
void __cdecl State3Frame(void);
void __cdecl State4Frame(void);

int frame_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_FRAME_H */
