#ifndef AM2_MOVIE_H
#define AM2_MOVIE_H

#include <stddef.h>
#include <stdint.h>
#include "../../inject/orig.h"

#ifdef __cplusplus   /* thiscall members -- see src/vc6 (byte-match) */
/* The Smacker handle, as far as the player reads it. smackw32 ships no
 * header, so the offsets are the ones the image reads. */
struct AM2_Smack {
    uint32_t version;               /* +0x000 */
    uint32_t width;                 /* +0x004 */
    uint32_t height;                /* +0x008 */
    uint32_t frames;                /* +0x00C */
    uint8_t  _pad10[0x58];          /* +0x010 */
    int32_t  newPalette;            /* +0x068 set when this frame changed it */
    uint8_t  palette[0x304];        /* +0x06C three bytes per entry */
    uint32_t palType;               /* +0x370 */
    uint32_t frameNum;              /* +0x374 */
};

struct AM2_Movie {
    const void         *vtbl;       /* +0x00 re-stamped on stop */
    LPDIRECTDRAWSURFACE surface;    /* +0x04 the surface it decodes onto */
    int32_t             active;     /* +0x08 */
    int32_t             running;    /* +0x0C cleared at the last frame */
    int32_t             sized;      /* +0x10 the caller gave a size */
    int32_t             frameDue;   /* +0x14 raised by the timer */
    int32_t             big;        /* +0x18 which Smacker buffer size */
    struct AM2_Smack   *smack;      /* +0x1C from SmackOpen */
    int32_t             unused20;    /* +0x20 */
    uint32_t            ddType;     /* +0x24 SmackDDSurfaceType's answer */
    uint32_t            timerId;    /* +0x28 */
    char                name[0x80]; /* +0x2C the filename, copied in */
    int32_t             srcX;       /* +0xAC */
    int32_t             srcY;       /* +0xB0 */
    int32_t             srcW;       /* +0xB4 */
    int32_t             srcH;       /* +0xB8 */
    int32_t             wantW;      /* +0xBC -1 for the film's own */
    int32_t             wantH;      /* +0xC0 */
    /* 0x00444FC0. PlayMovie builds it with `new`, so this is a real
     * constructor -- the original's `new` carries an EH state for it. */
    AM2_Movie(const char *name, int32_t wantW, int32_t wantH, int32_t big);
#if defined(__GNUC__)
    /* `new` on the game's heap, as AM2_Widget's: StateLeave frees it there. */
    static void *operator new(size_t n);
    static void operator delete(void *p);
#endif
    /* 0x00445120 (ADDR_MOVIE_STOP). The destructor, OUT of line: it
     * re-stamps the vtable first, as a destructor does, and the original's
     * teardowns are `delete` -- test, this call, then operator delete. Inline,
     * VC6 loaded the pointer straight into esi; out of line it goes through
     * ecx as `this` first, which is what the image's delete sites do. */
    ~AM2_Movie();
    /* 0x00445170, in the movie's own address band and called with the movie
     * in ecx: a member that ignores `this`. Defined in palette.cpp. */
    void __attribute__((thiscall)) SnapshotSystemPalette();
    void __attribute__((thiscall)) MovieSetVolume(int32_t volume);
    void __attribute__((thiscall)) MovieApplyPalette(LPDIRECTDRAWSURFACE surf);
    int32_t __attribute__((thiscall)) MovieStart(void *arg);
    void __attribute__((thiscall)) MovieDrawFrame(void *arg);
    int32_t __attribute__((thiscall)) MoviePoll();
};
static_assert(offsetof(AM2_Smack, newPalette) == 0x068, "smack newPalette");
static_assert(offsetof(AM2_Smack, palette)    == 0x06C, "smack palette");
static_assert(offsetof(AM2_Smack, frameNum)   == 0x374, "smack frameNum");
static_assert(offsetof(AM2_Movie, smack)      == 0x1C, "movie smack");
static_assert(offsetof(AM2_Movie, timerId)    == 0x28, "movie timerId");
static_assert(offsetof(AM2_Movie, srcX)       == 0xAC, "movie srcX");
static_assert(sizeof(AM2_Movie)               == AM2_MOVIE_SIZE, "movie size");
static inline void __attribute__((thiscall)) MovieApplyPalette(void *movie, LPDIRECTDRAWSURFACE surf)
{ ((AM2_Movie *)movie)->MovieApplyPalette(surf); }
#endif

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
/* C++ linkage under VC6: it assumes an extern "C" callee cannot throw, and
 * PlayMovie's `new AM2_Movie` must carry the EH state the original's does
 * (see rect.h). */
#if defined(__cplusplus) && !defined(_MSC_VER)
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

/* Original: 0x00445280, 1 call site. Set the volume on every audio track.
 *
 * Smacker addresses its tracks by bit, and this sets all seven of them to the
 * same volume with a fixed pan -- so it is one call written out seven times
 * rather than a loop. Skipped entirely when the movie subsystem never got sound
 * going. */
static inline void __attribute__((thiscall)) MovieSetVolume(void *movie, int32_t volume)
{ ((AM2_Movie *)movie)->MovieSetVolume(volume); }

/* Original: 0x004453C0, 1 call site. Decode one frame into the movie surface
 * and put it on screen. Driven by a multimedia timer, so it checks the window
 * still has focus first -- the timer keeps firing after an alt-tab. */
static inline void __attribute__((thiscall)) MovieDrawFrame(void *movie, void *arg)
{ ((AM2_Movie *)movie)->MovieDrawFrame(arg); }

/* Original: 0x00445600, 2 call sites. Post WM_USER to say the movie ended,
 * which is how a finished cutscene becomes a game state change. */
void __cdecl MovieFinished(void);

/* Original: 0x00445390, 2 call sites. Draw a frame if Smacker says one is due.
 * Called from the game loop rather than the timer -- both drive playback, and
 * SmackWait is what stops them drawing the same frame twice. */
static inline int32_t __attribute__((thiscall)) MoviePoll(void *movie)
{ return ((AM2_Movie *)movie)->MoviePoll(); }

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
void __cdecl MovieSetCurrent(AM2_Movie *movie);   /* 0x00445620 */
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
static inline int32_t __attribute__((thiscall)) MovieStart(void *movie, void *arg)
{ return ((AM2_Movie *)movie)->MovieStart(arg); }

int movie_install(void);

#if defined(__cplusplus) && !defined(_MSC_VER)
}
#endif

#endif /* AM2_MOVIE_H */
