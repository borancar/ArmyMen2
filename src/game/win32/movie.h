#ifndef AM2_MOVIE_H
#define AM2_MOVIE_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Smacker video playback -- the game's cutscene and intro player.
 *
 * smackw32.dll is a third-party library with no SDK header and no import
 * library, so unlike DirectDraw or WINMM there is nothing to include and
 * nothing to link. Its entry points are reached the way the game reaches them:
 * through the import thunks in its own IAT, read at the moment of the call.
 * That is the same reasoning as src/game/win32/device.cpp uses for DirectInput, for a
 * different reason -- there it preserves a harness hook, here it is simply the
 * only way to name a function nobody declared.
 *
 * These are C++ methods on the game's movie object, so they are thiscall. The
 * object is treated as opaque: only the fields these functions touch are named,
 * rather than reconstructing a class layout that nothing here needs.
 */

/* Original: 0x00445120, 2 call sites. Stop playback and let everything go.
 *
 * Kills the multimedia timer that was driving the frames, releases the surface
 * the movie was decoding onto, and closes the Smacker handle. Does nothing at
 * all unless both a handle and an active flag are present, so it is safe to
 * call on a movie that never started. */
void __attribute__((thiscall)) MovieStop(void *movie);

/* Original: 0x00445280, 1 call site. Set the volume on every audio track.
 *
 * Smacker addresses its tracks by bit, and this sets all seven of them to the
 * same volume with a fixed pan -- so it is one call written out seven times
 * rather than a loop. Skipped entirely when the movie subsystem never got sound
 * going. */
void __attribute__((thiscall)) MovieSetVolume(void *movie, int32_t volume);

/* Original: 0x004453C0, 1 call site. Decode one frame into the movie surface
 * and put it on screen. Driven by a multimedia timer, so it checks the window
 * still has focus first -- the timer keeps firing after an alt-tab. */
void __attribute__((thiscall)) MovieDrawFrame(void *movie, void *arg);

/* Original: 0x00445600, 2 call sites. Post WM_USER to say the movie ended,
 * which is how a finished cutscene becomes a game state change. */
void __cdecl MovieFinished(void);

/* Original: 0x00444FC0, 1 call site. Open a .SMK and prepare to play it. A
 * `wantW` of -1 means "whatever size the film is". Returns `this`. */
void *__attribute__((thiscall)) MovieOpen(void *movie, const char *name,
                                          int32_t wantW, int32_t wantH,
                                          int32_t big);

/* Original: 0x00445390, 2 call sites. Draw a frame if Smacker says one is due.
 * Called from the game loop rather than the timer -- both drive playback, and
 * SmackWait is what stops them drawing the same frame twice. */
int32_t __attribute__((thiscall)) MoviePoll(void *movie);

/* The four one-liners around the "current movie" global at 0x006568A0.
 * Between them they are the whole of how the rest of the game touches a
 * playing film, and all four names are ours.
 *
 * MovieStepCurrent goes through the object's VTABLE rather than calling
 * MoviePoll -- slot 0 is 0x00445390, which is MoviePoll, but the dispatch is
 * what the original does and is reproduced. Two dereferences, not one: the
 * object, then its table. See CLAUDE.md, where writing that as one cost an
 * iteration.
 *
 * MovieForget tests the pointer and then stores zero either way, so the test
 * cannot change anything. Reproduced; it is one instruction and removing it
 * would be tidying rather than porting. */
void __cdecl MovieSetCurrent(void *movie);   /* 0x00445620 */
void __cdecl MovieStepCurrent(void);         /* 0x00445630, states 0 and 3 */
void __cdecl MovieEndCurrent(void);          /* 0x00445650 */
void __cdecl MovieForget(void);

/* 0x0042E720. Leaving a game state: stop and delete the state's movie if it
 * has one, and clear the primary surface either way. */
void __cdecl StateLeave(void);              /* 0x0042E720 */

/* 0x0042E5E0, four call sites. Chdir to where the film lives, refuse if it
 * is missing or -nm was given, and construct and start a movie object. */
void __cdecl PlayMovie(const char *name, int32_t big);

/* 0x004455E0. The movie timer's callback: mark the current movie ticked. */
void CALLBACK MovieTimerProc(UINT id, UINT msg, DWORD_PTR user,
                             DWORD_PTR dw1, DWORD_PTR dw2);

/* 0x0042E8E0, 0x0042E910, 0x0042E930, 0x0042E960, 0x0042E990. The five
 * distinct handlers of ADDR_STATE_ACTIONS: what to do on entering a game-over
 * state, and what to do when a message arrives while in it. Reached only
 * through that table, by frame.cpp and winproc.cpp. */
int32_t __cdecl StateEnterLogoMovie(void);
int32_t __cdecl StateEnterAct1Movie(void);
int32_t __cdecl StateEnterCreditsMovie(void);
int32_t __cdecl StateMessageLogoMovie(void);
int32_t __cdecl StateMessageMovieToMenu(void);

/* 0x004451F0. Put the multimedia timer on a movie. */
int32_t __attribute__((thiscall)) MovieStart(void *movie, void *arg);

int movie_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MOVIE_H */
