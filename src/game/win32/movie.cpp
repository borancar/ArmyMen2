/* Smacker video playback -- reconstructed from ArmyMen2.exe.
 *
 *   MovieStop       0x00445120   2 call sites, thiscall
 *   MovieSetVolume  0x00445280   1 call site,  thiscall
 *
 * See movie.h on why smackw32 is reached through the game's own import thunks:
 * it is a third-party library with no header to include and no library to link,
 * so the IAT slot is the only place its address exists.
 *
 * The movie object is left opaque. Only the four fields these two touch are
 * named, because naming the rest would mean inventing a class layout that
 * nothing here needs and that the next function to be reconstructed would
 * probably contradict.
 */

#include "surface.h"
#include "movie.h"
#include "../gamedir.h"   /* SetGameDir, FileExists -- reconstructed */
#include "../gameproc.h"  /* SetGameOver, RequestState -- reconstructed */
#include "../misc.h"      /* MovieBuildName -- reconstructed */
#include "../image.h"
#include "palette.h"
#include "../../inject/patch.h"

#include <stdint.h>
#include <string.h>

/* Fields of the movie object, by offset. */
#define MOVIE_VTABLE   0x00u   /* re-stamped on stop */
#define MOVIE_SURFACE  0x04u   /* the surface it decodes onto */
#define MOVIE_ACTIVE   0x08u
#define MOVIE_SMACK    0x1Cu   /* the Smack * from SmackOpen */
#define MOVIE_TIMER_ID 0x28u

#define MOVIE_TIMER_RUN 0x0Cu   /* cleared when the last frame is reached */
#define MOVIE_UNKNOWN14 0x14u
#define MOVIE_SIZED     0x10u   /* non-zero when the caller gave a size */
#define MOVIE_BIG       0x18u   /* selects which Smacker buffer size to ask for */
#define MOVIE_UNKNOWN20 0x20u
#define MOVIE_NAME      0x2Cu   /* the filename, copied in */
#define MOVIE_SRC_X     0xACu
#define MOVIE_SRC_Y     0xB0u
#define MOVIE_SRC_W     0xB4u
#define MOVIE_SRC_H     0xB8u
#define MOVIE_WANT_W    0xBCu   /* -1 for "whatever the film is" */
#define MOVIE_WANT_H    0xC0u
#define MOVIE_DD_TYPE   0x24u   /* what SmackDDSurfaceType made of the surface */

#define fld(m, off, type) (*(type *)((uint8_t *)(m) + (off)))

/* Fields of Smacker's own handle. */
#define SMACK_HEIGHT      0x008u
#define SMACK_FRAMES      0x00Cu
#define SMACK_NEW_PALETTE 0x068u
#define SMACK_FRAME_NUM   0x374u

/* smackw32 entry points, called through the game's IAT exactly as it does. */
typedef void (__stdcall *am2_smack_close_fn)(void *smack);
typedef void (__stdcall *am2_smack_frame_fn)(void *smack);
typedef void *(__stdcall *am2_smack_open_fn)(const char *name, uint32_t flags,
                                             uint32_t extra);
typedef uint32_t (__stdcall *am2_smack_ddtype_fn)(void *surface);
typedef void (__stdcall *am2_smack_use_dsound_fn)(void *dsound);
typedef void (__stdcall *am2_smack_tobuffer_fn)(void *smack, uint32_t left,
                                                uint32_t top, uint32_t pitch,
                                                uint32_t destHeight, void *dest,
                                                uint32_t flags);
typedef void (__stdcall *am2_smack_volumepan_fn)(void *smack, uint32_t trackFlags,
                                                 uint32_t volume, uint32_t pan);
#define smack_close     (*(am2_smack_close_fn *)(uintptr_t)ADDR_IAT_SMACK_CLOSE)
#define smack_volumepan (*(am2_smack_volumepan_fn *)(uintptr_t)ADDR_IAT_SMACK_VOLUMEPAN)
#define smack_tobuffer  (*(am2_smack_tobuffer_fn *)(uintptr_t)ADDR_IAT_SMACK_TO_BUFFER)
#define smack_doframe   (*(am2_smack_frame_fn *)(uintptr_t)ADDR_IAT_SMACK_DO_FRAME)
#define smack_nextframe (*(am2_smack_frame_fn *)(uintptr_t)ADDR_IAT_SMACK_NEXT_FRAME)
#define smack_open      (*(am2_smack_open_fn *)(uintptr_t)ADDR_IAT_SMACK_OPEN)
#define smack_wait      (*(am2_smack_ddtype_fn *)(uintptr_t)ADDR_IAT_SMACK_WAIT)
#define smack_ddtype    (*(am2_smack_ddtype_fn *)(uintptr_t)ADDR_IAT_SMACK_DDTYPE)
#define smack_use_dsound (*(am2_smack_use_dsound_fn *)(uintptr_t)ADDR_IAT_SMACK_USE_DSOUND)

/* Smack handle fields used only by the opener. */
#define SMACK_WIDTH   0x004u
#define SMACK_PALETTE 0x08Au   /* three bytes per entry, in the Smack handle */

/* Windows keeps ten entries at each end; the film gets what is between. */
#define MOVIE_PALETTE_FIRST 10
#define MOVIE_PALETTE_LAST  245

#define MOVIE_TIMER_MS  0x32   /* 50ms per frame */
#define MOVIE_TIMER_RES 0x0A

typedef void (__stdcall *am2_movie_slot_fn)(void);
typedef void (__cdecl *am2_delete_fn)(void *);
#define orig_delete (*(am2_delete_fn)ADDR_GAME_DELETE)
/* Spelled as widget.cpp spells it, so the two stay one definition. */
typedef void *(__cdecl *AM2_OperatorNewFn)(uint32_t size);
#define orig_operator_new     ((AM2_OperatorNewFn)AM2_IMAGE(ADDR_GAME_OPERATOR_NEW))

#define g_movieDSound (*(void **)(uintptr_t)ADDR_DSOUND)
#define g_soundReady  (*(int32_t *)(uintptr_t)ADDR_MOVIE_SOUND_READY)

/* Still in the original image: the palette apply and the blit to screen are
 * game logic, and the timer callback that drives all this is never ours. */
typedef void (__attribute__((thiscall)) *am2_movie_arg_fn)(void *movie, void *arg);

#define g_moviePaletteOwner (*(uint8_t **)(uintptr_t)ADDR_MOVIE_PALETTE_OWNER)
#define MOVIE_PALETTE_OFF 0x800u
#define g_hWnd (*(HWND *)(uintptr_t)ADDR_HWND)

/* Non-zero once the movie subsystem has handed Smacker a DirectSound object. */
#define g_movieSoundReady (*(const int32_t *)(uintptr_t)ADDR_MOVIE_SOUND_READY)
#define g_movieCurrent (*(void **)(uintptr_t)ADDR_MOVIE_CURRENT)

/* 0x00445690, one caller. The movie's own offscreen surface.
 *
 * It takes a width and a height -- the original's caller computes them from
 * the film's source rectangle and pushes both, and this cleans 8 bytes off the
 * stack for them -- and then IGNORES them, creating a fixed 640x480 surface
 * every time. Reproduced with the arguments still in the signature, because
 * the caller genuinely passes them and a no-argument declaration would be a
 * different ABI; the discard is the original's and is the point of the
 * comment: a film smaller than the screen would still get a full-screen
 * surface.
 *
 * "Would" rather than "does", and that is measured. Replacing the literals
 * with `w` and `h` -- the mutation that tests exactly this claim -- changes
 * nothing on the intro, because a probe says the caller passes 640 by 480
 * both times it runs. The shipped film is already full-screen, so the discard
 * is real code with no observable consequence on any drive here, and the
 * frame it produces is identical either way.
 *
 * 0x40 is DDSCAPS_OFFSCREENPLAIN and the trailing 0 is the colour key, which
 * for CreateOffscreenSurface means key index zero rather than none -- a
 * negative is what asks for none. */
LPDIRECTDRAWSURFACE __cdecl MovieMakeSurface(int32_t w, int32_t h)
{
    (void)w;
    (void)h;
    /* 640x480 as IMMEDIATES in the original, not ADDR_SCREEN_W/H. Kept
     * literal: reading the globals instead would silently change behaviour in
     * any mode where they are not 640x480. */
    return CreateOffscreenSurface(0x280, 0x1E0, DDSCAPS_OFFSCREENPLAIN, 0);
}

/* Smacker names its audio tracks by bit, starting here and shifting up. */
#define SMACK_TRACK_FIRST 0x2000u
#define SMACK_TRACK_COUNT 7
#define SMACK_PAN_CENTRE  0x7FF6u

void __attribute__((thiscall)) MovieStop(void *movie)
{
    /* Re-stamped before anything else, and unconditionally -- so it happens
     * even on the early returns below. */
    fld(movie, MOVIE_VTABLE, uint32_t) = ADDR_MOVIE_VTABLE;

    if (!fld(movie, MOVIE_SMACK, void *))
        return;
    if (!fld(movie, MOVIE_ACTIVE, int32_t))
        return;

    if (fld(movie, MOVIE_TIMER_ID, UINT))
        timeKillEvent(fld(movie, MOVIE_TIMER_ID, UINT));
    fld(movie, MOVIE_TIMER_ID, UINT)   = 0;
    fld(movie, MOVIE_ACTIVE, int32_t)  = 0;

    IDirectDrawSurface_Release(fld(movie, MOVIE_SURFACE, LPDIRECTDRAWSURFACE));
    fld(movie, MOVIE_SURFACE, LPDIRECTDRAWSURFACE) = NULL;

    /* The handle is closed but not cleared, which is what the original does --
     * so a second Stop would close it twice. Both call sites drop the object
     * straight afterwards, which is presumably why it never mattered. */
    smack_close(fld(movie, MOVIE_SMACK, void *));
}

void __attribute__((thiscall)) MovieSetVolume(void *movie, int32_t volume)
{
    uint32_t track;
    int32_t  i;

    if (!g_movieSoundReady)
        return;

    for (i = 0, track = SMACK_TRACK_FIRST; i < SMACK_TRACK_COUNT; i++, track <<= 1)
        smack_volumepan(fld(movie, MOVIE_SMACK, void *), track,
                        (uint32_t)volume, SMACK_PAN_CENTRE);
}

/* Original: 0x00445320, 2 call sites. Push the film's palette to the display.
 *
 * Smacker keeps its palette inside its own handle, three bytes per entry. This
 * copies it into the game's PALETTEENTRY table -- entries 10 through 245 only,
 * which is exactly the range SnapshotSystemPalette marks PC_NOCOLLAPSE, because
 * the twenty at either end belong to Windows -- and then hands the whole table
 * to the surface's DirectDraw palette.
 *
 * The peFlags byte of each entry is stepped over rather than written, so the
 * NOCOLLAPSE marks survive a palette change. */
void __attribute__((thiscall)) MovieApplyPalette(void *movie,
                                                 LPDIRECTDRAWSURFACE surf)
{
    LPDIRECTDRAWPALETTE pal = NULL;
    const uint8_t      *src;
    LPPALETTEENTRY      dst = (LPPALETTEENTRY)(uintptr_t)ADDR_SYSTEM_PALETTE;
    int32_t             i;

    src = (const uint8_t *)fld(movie, MOVIE_SMACK, void *) + SMACK_PALETTE;

    for (i = MOVIE_PALETTE_FIRST; i <= MOVIE_PALETTE_LAST; i++) {
        dst[i].peRed   = *src++;
        dst[i].peGreen = *src++;
        dst[i].peBlue  = *src++;
    }

    IDirectDrawSurface_GetPalette(surf, &pal);
    if (pal) {
        IDirectDrawPalette_SetEntries(pal, 0, 0, 256, dst);
        IDirectDrawPalette_Release(pal);
    }
}

/* Original: 0x004451F0, 1 call site. Start playing: put a timer on it.
 *
 * Frames are driven by a multimedia timer rather than by the game loop, which
 * is why a cutscene keeps playing at its own rate whatever the frame rate is
 * doing. 50ms period, 10ms resolution, and the callback stays in the original
 * image -- it is what calls MovieDrawFrame.
 *
 * If the timer cannot be had the movie is not merely abandoned, it is destroyed
 * and the state machine told to move on, because there is nothing else to drive
 * it and the game would otherwise sit on a still frame forever. */
int32_t __attribute__((thiscall)) MovieStart(void *movie, void *arg)
{
    (void)arg;

    if (!fld(movie, MOVIE_SMACK, void *))
        return 0;
    if (fld(movie, MOVIE_TIMER_RUN, int32_t))
        return 0;
    fld(movie, MOVIE_TIMER_RUN, int32_t) = 1;

    /* Slot 0 of the current movie's own table, called without `this` -- the
     * same shape as the paint object in winproc.cpp. Two dereferences: the
     * global holds the object, the object begins with its table. */
    {
        void              *current = g_movieCurrent;
        am2_movie_slot_fn *vtbl    = *(am2_movie_slot_fn **)current;

        vtbl[0]();
    }

    fld(movie, MOVIE_TIMER_ID, UINT) =
        timeSetEvent(MOVIE_TIMER_MS, MOVIE_TIMER_RES,
                     MovieTimerProc, 0,
                     TIME_PERIODIC);

    if (fld(movie, MOVIE_TIMER_ID, UINT)) {
        fld(movie, MOVIE_ACTIVE, int32_t) = 1;
        return 1;
    }

    /* No timer, so no movie. Tear it down and let the game carry on. */
    {
        void *current = g_movieCurrent;

        if (current) {
            MovieStop(current);
            orig_delete(current);
        }
        g_movieCurrent = NULL;
    }
    PostMessageA(g_hWnd, AM2_WM_STATE_ABORT, 0, 0);
    return 0;
}

/* Original: 0x00444FC0, 1 call site. Open a .SMK and get ready to play it.
 *
 * `wantW` of -1 means "whatever size the film is"; anything else is a requested
 * extent, and the source rectangle is then the caller's rather than the film's.
 * `big` picks between two Smacker buffer sizes -- the larger one is roughly
 * double, which is what a full-screen movie needs and a corner inset does not.
 *
 * Handing Smacker the DirectSound object is done once for the whole process
 * rather than once per movie, which is what the flag guards. It is also the
 * only place DirectSound appears outside device.cpp, and it is skipped entirely
 * when there is no DirectSound to hand over -- as here, where there is none.
 *
 * Returns `this`, in the way a C++ constructor-ish method does. */
void *__attribute__((thiscall)) MovieOpen(void *movie, const char *name,
                                          int32_t wantW, int32_t wantH,
                                          int32_t big)
{
    void *smack;

    fld(movie, MOVIE_WANT_H, int32_t) = wantH;
    fld(movie, MOVIE_BIG, int32_t)    = big;
    fld(movie, MOVIE_WANT_W, int32_t) = wantW;

    fld(movie, MOVIE_VTABLE, uint32_t)    = ADDR_MOVIE_VTABLE;
    fld(movie, MOVIE_ACTIVE, int32_t)     = 0;
    fld(movie, MOVIE_TIMER_RUN, int32_t)  = 0;
    fld(movie, MOVIE_SIZED, int32_t)      = 0;
    fld(movie, MOVIE_UNKNOWN14, int32_t)  = 0;
    fld(movie, MOVIE_SRC_X, int32_t)      = 0;
    fld(movie, MOVIE_SRC_Y, int32_t)      = 0;
    fld(movie, MOVIE_SRC_W, int32_t)      = 0;
    fld(movie, MOVIE_SRC_H, int32_t)      = 0;
    fld(movie, MOVIE_TIMER_ID, uint32_t)  = 0;
    fld(movie, MOVIE_SMACK, void *)       = NULL;
    fld(movie, MOVIE_UNKNOWN20, int32_t)  = 0;

    strcpy((char *)movie + MOVIE_NAME, name);

    if (fld(movie, MOVIE_WANT_W, int32_t) != -1)
        fld(movie, MOVIE_SIZED, int32_t) = 1;

    /* Once per process, and only if there is a DirectSound to give. */
    if (!g_soundReady && g_movieDSound) {
        smack_use_dsound(g_movieDSound);
        g_soundReady = 1;
    }

    fld(movie, MOVIE_SRC_X, int32_t) = 0;
    fld(movie, MOVIE_SRC_Y, int32_t) = 0;

    smack = smack_open(name, big ? 0x1FE440u : 0x000FE040u, 0xFFFFFFFFu);
    fld(movie, MOVIE_SMACK, void *) = smack;

    if (smack) {
        if (fld(movie, MOVIE_SIZED, int32_t)) {
            /* The caller named an extent, so the source rectangle is inclusive
             * of the last pixel rather than one past it. */
            fld(movie, MOVIE_SRC_W, int32_t) =
                fld(movie, MOVIE_WANT_W, int32_t) - 1;
            fld(movie, MOVIE_SRC_H, int32_t) =
                fld(movie, MOVIE_WANT_H, int32_t) - 1;
        } else {
            fld(movie, MOVIE_SRC_W, int32_t) = fld(smack, SMACK_WIDTH, int32_t);
            fld(movie, MOVIE_SRC_H, int32_t) = fld(smack, SMACK_HEIGHT, int32_t);
        }
    }

    /* Note this runs whether or not the open succeeded, on a source rectangle
     * that is still zero if it did not. Kept as written. */
    fld(movie, MOVIE_SURFACE, LPDIRECTDRAWSURFACE) =
        MovieMakeSurface(fld(movie, MOVIE_SRC_W, int32_t) -
                          fld(movie, MOVIE_SRC_X, int32_t),
                          fld(movie, MOVIE_SRC_H, int32_t) -
                          fld(movie, MOVIE_SRC_Y, int32_t));
    fld(movie, MOVIE_DD_TYPE, uint32_t) =
        smack_ddtype(fld(movie, MOVIE_SURFACE, LPDIRECTDRAWSURFACE));

    /* Called with `this` in ecx, and ignores it -- it reads only globals, takes
     * no stack arguments and ends in a plain `ret`, so our cdecl declaration of
     * it is ABI-identical. */
    SnapshotSystemPalette();
    return movie;
}

/* Original: 0x00445600, 2 call sites. Tell the window the movie ended.
 *
 * WM_USER, which the window procedure routes into the state machine -- so this
 * is the one place a finished cutscene becomes a game state change. */
void __cdecl MovieFinished(void)
{
    PostMessageA(g_hWnd, WM_USER, 0, 0);
}

/* Original: 0x004453C0, 1 call site. Decode one frame onto the surface.
 *
 * Driven from the multimedia timer the movie sets up, which is why the first
 * thing it does is check the game still has focus: the timer keeps firing when
 * the user alt-tabs away, and decoding into a surface nobody owns is at best
 * wasted.
 *
 * STANDING NOTE -- the palette probe locks and does not unlock.
 *
 *   When Smacker reports a new palette and the surface is not a type Smacker
 *   writes to directly, the original Locks the surface purely to find out
 *   whether it has been lost, and never Unlocks it. On the ordinary path that
 *   leaves the surface locked and the real Lock a few lines further down should
 *   fail. It is reproduced rather than fixed, as with the LockSurface
 *   post-Restore publish.
 *
 *   It survives because the condition is nearly always false: SmackDDSurfaceType
 *   recognises the surface, so MOVIE_DD_TYPE is non-zero and the whole block is
 *   skipped. The intro plays, which is the evidence.
 */
void __attribute__((thiscall)) MovieDrawFrame(void *movie, void *arg)
{
    DDSURFACEDESC ddsd;
    void         *smack = fld(movie, MOVIE_SMACK, void *);
    LPDIRECTDRAWSURFACE surf = fld(movie, MOVIE_SURFACE, LPDIRECTDRAWSURFACE);
    HRESULT       hr;

    /* The timer does not stop when the window loses focus, so this does. */
    if (GetFocus() != g_hWnd)
        return;

    if (fld(smack, SMACK_NEW_PALETTE, int32_t) &&
        !fld(movie, MOVIE_DD_TYPE, int32_t)) {
        MovieApplyPalette(movie, (LPDIRECTDRAWSURFACE)arg);

        /* See the standing note: a lock used as a question, never answered. */
        hr = IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL);
        if (hr == DDERR_SURFACELOST) {
            if (IDirectDrawSurface_Restore(surf) != DD_OK)
                return;
            IDirectDrawSurface_SetPalette(
                surf, *(LPDIRECTDRAWPALETTE *)(g_moviePaletteOwner +
                                               MOVIE_PALETTE_OFF));
            MovieApplyPalette(movie, (LPDIRECTDRAWSURFACE)arg);
        }
    }

    memset(&ddsd, 0, sizeof ddsd);
    ddsd.dwSize = sizeof ddsd;
    hr = IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL);
    if (hr == DDERR_SURFACELOST && IDirectDrawSurface_Restore(surf) != DD_OK)
        return;

    /* Straight into the locked bits, at the surface's own pitch. */
    smack_tobuffer(smack, 0, 0, (uint32_t)ddsd.lPitch,
                   fld(smack, SMACK_HEIGHT, uint32_t), ddsd.lpSurface,
                   fld(movie, MOVIE_DD_TYPE, uint32_t));
    smack_doframe(smack);
    IDirectDrawSurface_Unlock(surf, NULL);

    BlitCentred(movie, (LPDIRECTDRAWSURFACE)arg);

    /* Last frame ends the movie; anything else advances it. */
    smack = fld(movie, MOVIE_SMACK, void *);
    if (fld(smack, SMACK_FRAME_NUM, uint32_t) ==
        fld(smack, SMACK_FRAMES, uint32_t) - 1) {
        fld(movie, MOVIE_TIMER_RUN, int32_t) = 0;
        MovieFinished();
    } else {
        smack_nextframe(smack);
    }
}

/* Original: 0x00445390, 2 call sites. Draw a frame if one is due.
 *
 * Called from the game's own loop, not from the timer, and asks Smacker whether
 * the next frame's time has come -- SmackWait answers non-zero while it is
 * still early. So the timer is not the only thing driving playback: whichever
 * of the two notices first gets the frame, and SmackWait is what stops them
 * drawing it twice.
 *
 * Note the surface it draws to is the primary, passed straight through. That is
 * what MovieDrawFrame's second argument has been all along. */
int32_t __attribute__((thiscall)) MoviePoll(void *movie)
{
    if (fld(movie, MOVIE_TIMER_RUN, int32_t) &&
        smack_wait(fld(movie, MOVIE_SMACK, void *)) == 0)
        MovieDrawFrame(movie, *(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE);
    return 1;
}

/* Only the two fields these four touch are named, as everywhere else in this
 * file: the object is deliberately opaque. */
#define MOVIE_OFF_FLAG_0C   0x0Cu
#define MOVIE_OFF_FLAG_14   0x14u

void __cdecl MovieSetCurrent(void *movie)
{
    g_movieCurrent = movie;
}

void __cdecl MovieStepCurrent(void)
{
    void *movie = g_movieCurrent;
    void **vtable;

    if (!movie)
        return;
    /* Object, then table, then slot -- three levels, and the middle one is
     * easy to lose. Slot 0 is MoviePoll. */
    vtable = *(void ***)movie;
    (*(int32_t (__attribute__((thiscall)) *)(void *))vtable[0])(movie);
    /* Re-read the global, as the original does: the call above could have
     * cleared it. */
    if (g_movieCurrent)
        *(int32_t *)((uint8_t *)g_movieCurrent + MOVIE_OFF_FLAG_14) = 0;
}

void __cdecl MovieEndCurrent(void)
{
    void *movie = g_movieCurrent;

    if (!movie)
        return;
    *(int32_t *)((uint8_t *)movie + MOVIE_OFF_FLAG_0C) = 0;
    MovieFinished();
    g_movieCurrent = 0;
}

void __cdecl MovieForget(void)
{
    /* The test cannot change the outcome -- both arms leave the global at
     * zero -- and is reproduced rather than removed. */
    if (!g_movieCurrent)
        return;
    g_movieCurrent = 0;
}

/* Spelled as device.cpp and surface.cpp spell it, so the three stay one
 * definition rather than a drift. */
#define g_primarySurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)

/* 0x0042E720, four callers -- what leaving a game state does about the movie
 * and the screen.
 *
 * Two movie pointers are in play and they are NOT the same one: this owns
 * ADDR_STATE_MOVIE, while MovieForget clears ADDR_MOVIE_CURRENT. So the call
 * to MovieForget is not the teardown, it is the other global being let go
 * first, and the stop and delete below act on this one.
 *
 * The outer test and the inner one are both reproduced. The global is re-read
 * between them, which is what makes the second test more than a repetition:
 * MovieForget is a call, and nothing here establishes that it cannot reach
 * something that clears this pointer too.
 *
 * The screen is cleared to colour zero on BOTH paths -- with a movie and
 * without -- so leaving a state always blanks the primary. */
void __cdecl StateLeave(void)
{
    if (*(void **)(uintptr_t)ADDR_STATE_MOVIE) {
        void *movie;

        MovieForget();

        movie = *(void **)(uintptr_t)ADDR_STATE_MOVIE;
        if (movie) {
            MovieStop(movie);
            orig_delete(movie);
        }
        *(void **)(uintptr_t)ADDR_STATE_MOVIE = (void *)0;
    }

    ClearSurface(g_primarySurface, 0);
}

/* PlayMovie -- original 0x0042E5E0, four call sites.
 *
 * Open a film and start it: chdir to wherever it lives, refuse if the file is
 * not there or if -nm was given, tear down whatever the state was showing,
 * construct a movie object and set it going.
 *
 * TWO DIRECTORIES, and which one is decided by the film's NAME. "3do" and
 * "credits" -- matched by prefix, with strncmp and their own lengths -- come
 * from ADDR_DIR_SCRATCH, whatever the last caller left in it; everything else
 * comes from `avi`. That is the same split MovieBuildName exempts the same two
 * names from, one layer up, which is what makes it a rule rather than a
 * coincidence: those two are not the campaign's films and are not kept beside
 * them.
 *
 * -nm IS "NO MOVIES", and the whole image is two references to that global:
 * the switch parse and this gate. A flag read in exactly one place, by a
 * function that decides whether to play a film, does not need a second
 * witness. It was ADDR_OPT_NM.
 *
 * THE FAILURE PATH POSTS THE FINISHED MESSAGE RATHER THAN RETURNING QUIETLY,
 * which is the only thing that keeps the state machine moving: state 3 waits
 * for that message, so a missing .smk has to look exactly like a film that
 * played and ended. MovieFinished posts the same WM_USER from the other end.
 *
 * The volume is ADDR_VOLUME_VOICE put through `v * 3 + 0x8000`, floored to 0
 * below -2000. Transcribed rather than explained -- that is Smacker's scale
 * and nothing here says what it means.
 *
 * The MSVC SEH frame around all of this is not reproduced; see CLAUDE.md.
 * Neither is the unwind-state index the original writes as it goes.
 */
void __cdecl PlayMovie(const char *name, int32_t big)
{
    void   *movie;
    int32_t volume;

    if (strncmp((const char *)AM2_IMAGE(ADDR_STR_MOVIE_3DO), name, 3) == 0
        || strncmp((const char *)AM2_IMAGE(ADDR_STR_MOVIE_CREDITS),
                   name, 7) == 0)
        SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));
    else
        SetGameDir(*(const char *const *)AM2_IMAGE(ADDR_STR_AVI_DIR));

    if (!FileExists(name)
        || *(const int32_t *)(uintptr_t)ADDR_OPT_NO_MOVIES) {
        MovieFinished();
        return;
    }

    StateLeave();

    movie = orig_operator_new(AM2_MOVIE_SIZE);
    if (movie)
        movie = MovieOpen(movie, name, -1, -1, big);

    /* The original stores whatever the constructor returned, which for an
     * i386 MSVC constructor is `this`. Written as the pointer rather than as
     * a second variable, since the two cannot differ. */
    *(void **)(uintptr_t)ADDR_STATE_MOVIE = movie;

    ClearSurface(g_primarySurface, 0);
    MovieSetCurrent(*(void **)(uintptr_t)ADDR_STATE_MOVIE);

    volume = *(const int32_t *)(uintptr_t)ADDR_VOLUME_VOICE;
    MovieSetVolume(*(void **)(uintptr_t)ADDR_STATE_MOVIE,
                   volume > AM2_MOVIE_VOLUME_FLOOR
                       ? volume * 3 + 0x8000
                       : 0);
    MovieStart(*(void **)(uintptr_t)ADDR_STATE_MOVIE, (void *)0);
}


/* ---- the state-action table's movie handlers ----------------------------
 *
 * 0x0042E8E0, 0x0042E910, 0x0042E930, 0x0042E960 and 0x0042E990: five of the
 * ten function pointers in ADDR_STATE_ACTIONS, whose other five slots are all
 * ReturnZero. Named by DATA -- nothing calls any of them, so an xref sweep of
 * .text answers nothing for all five; both consumers reach them through the
 * table and both are already ours (frame.cpp's StateEnter0 reads column 0,
 * WndProc reads column 1).
 *
 *   idx   onEnter                      onMessage
 *    0    play the 3do logo            end, game over 1, request state 0
 *    1    play act1 (from "avi")       end, clear game over, request the menu
 *    2    ReturnZero                   ReturnZero
 *    3    ReturnZero                   ReturnZero
 *    4    play the credits             end, clear game over, request the menu
 *
 * THEY LIVE HERE BECAUSE THE ADDRESS BAND SAYS SO. They were written into
 * misc.cpp first, beside the MovieBuildName and ReturnZero they share the
 * table with, and that broke `make check`'s selftest link -- misc.cpp is in
 * SELFTEST_SRC, which links without DirectX, and these call PlayMovie and
 * StateLeave. That is the air.cpp lesson exactly: which module a function goes
 * in is decided by the LINK, not only by checksplit. The right answer was
 * already visible in the addresses -- PlayMovie is 0x0042E5E0 and StateLeave
 * 0x0042E720, so this band is the original's own translation unit for them.
 *
 * ENTER LOGO AND ENTER CREDITS ARE ONE FUNCTION TWICE -- 48 bytes differing in
 * FOUR, of which only ONE is semantic: the string operand. The other three are
 * the two call displacements, which are relocations. Left written out for the
 * same reason as the pad pair; the string is the entire difference.
 *
 * The `lea` displacements look wrong until the DEFERRED CLEANUP is read. These
 * are cdecl and the compiler cleans every call's arguments together in the
 * epilogue's single `add esp`, so the buffer's displacement GROWS by 8 after
 * each call rather than returning to where it started. 0x0042E930's 0x34 is
 * 0x20 of buffer plus one, two and two pushed arguments.
 *
 * All five end `xor eax, eax`, so they are int32_t(void) like the ReturnZero
 * sharing their columns; orig.h's am2_state_action_fn says void, which is what
 * the two call sites want rather than what the functions are. The table is the
 * image's own data and we patch only the addresses, so neither needs a cast. */

int32_t __cdecl StateEnterLogoMovie(void)
{
    char name[0x20];

    MovieBuildName(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_3DO));
    PlayMovie(name, 0);
    return 0;
}

int32_t __cdecl StateEnterAct1Movie(void)
{
    char name[0x20];

    /* The directory table's first entry, which is "avi". */
    SetGameDir(*(const char *const *)AM2_IMAGE(ADDR_STR_AVI_DIR));
    MovieBuildName(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_ACT1));
    PlayMovie(name, 0);
    return 0;
}

int32_t __cdecl StateEnterCreditsMovie(void)
{
    char name[0x20];

    MovieBuildName(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_CREDITS));
    PlayMovie(name, 0);
    return 0;
}

int32_t __cdecl StateMessageLogoMovie(void)
{
    StateLeave();
    SetGameOver(1);
    RequestState(0);
    return 0;
}

int32_t __cdecl StateMessageMovieToMenu(void)
{
    StateLeave();
    SetGameOver(-1);
    RequestState(AM2_STATE_MENU);
    return 0;
}


/* 0x004455E0. The multimedia timer callback MoviePlay starts: five stdcall
 * arguments -- timeSetEvent's uID, uMsg, dwUser and two spares -- every one of
 * them ignored, and the whole body is one store into the current movie.
 *
 * It runs on the timer thread rather than the game's, which is why it does as
 * little as it does: it raises a flag and lets the frame notice.
 *
 * REGISTERED RATHER THAN PATCHED. The timeSetEvent call above is ours and now
 * passes this by name, so the image address is never called and a detour there
 * would be a jump nothing reaches -- the same standing AudioTimerProc has, and
 * the reason both are in coverage.py's REGISTERED instead of the patch list.
 * The cost is that it gets no trace counter. */
void CALLBACK MovieTimerProc(UINT id, UINT msg, DWORD_PTR user,
                             DWORD_PTR dw1, DWORD_PTR dw2)
{
    uint8_t *movie = *(uint8_t **)(uintptr_t)ADDR_MOVIE_CURRENT;

    (void)id; (void)msg; (void)user; (void)dw1; (void)dw2;

    if (movie)
        fld(movie, MOVIE_UNKNOWN14, int32_t) = 1;
}

int movie_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_MOVIE_MAKE_SURFACE, (const void *)MovieMakeSurface,
                        "MovieMakeSurface", 2);
    rc |= patch_replace(ADDR_MOVIE_STOP, (const void *)MovieStop, "MovieStop", 0);
    rc |= patch_replace(ADDR_MOVIE_SET_VOLUME, (const void *)MovieSetVolume,
                        "MovieSetVolume", 1);
    rc |= patch_replace(ADDR_MOVIE_DRAW_FRAME, (const void *)MovieDrawFrame,
                        "MovieDrawFrame", 1);
    rc |= patch_replace(ADDR_MOVIE_SET_CURRENT, (const void *)MovieSetCurrent,
                        "MovieSetCurrent", 1);
    rc |= patch_replace(ADDR_MOVIE_FRAME_STEP, (const void *)MovieStepCurrent,
                        "MovieStepCurrent", 0);
    rc |= patch_replace(ADDR_MOVIE_END_CURRENT, (const void *)MovieEndCurrent,
                        "MovieEndCurrent", 0);
    rc |= patch_replace(ADDR_MOVIE_FORGET, (const void *)MovieForget,
                        "MovieForget", 0);
    rc |= patch_replace(ADDR_STATE_LEAVE, (const void *)StateLeave,
                        "StateLeave", 4);
    rc |= patch_replace(ADDR_MOVIE_FINISHED, (const void *)MovieFinished,
                        "MovieFinished", 0);
    rc |= patch_replace(ADDR_MOVIE_OPEN, (const void *)MovieOpen, "MovieOpen", 4);
    rc |= patch_replace(ADDR_MOVIE_START, (const void *)MovieStart, "MovieStart", 1);
    rc |= patch_replace(ADDR_PLAY_MOVIE, (const void *)PlayMovie,
                        "PlayMovie", 4);
    rc |= patch_replace(ADDR_MOVIE_POLL, (const void *)MoviePoll, "MoviePoll", 0);
    rc |= patch_replace(ADDR_MOVIE_APPLY_PALETTE, (const void *)MovieApplyPalette,
                        "MovieApplyPalette", 1);
    rc |= patch_replace(ADDR_STATE_ENTER_LOGO,
                        (const void *)StateEnterLogoMovie,
                        "StateEnterLogoMovie", 0);
    rc |= patch_replace(ADDR_STATE_ENTER_ACT1,
                        (const void *)StateEnterAct1Movie,
                        "StateEnterAct1Movie", 0);
    rc |= patch_replace(ADDR_STATE_ENTER_CREDITS,
                        (const void *)StateEnterCreditsMovie,
                        "StateEnterCreditsMovie", 0);
    rc |= patch_replace(ADDR_STATE_MSG_LOGO,
                        (const void *)StateMessageLogoMovie,
                        "StateMessageLogoMovie", 0);
    rc |= patch_replace(ADDR_STATE_MSG_TO_MENU,
                        (const void *)StateMessageMovieToMenu,
                        "StateMessageMovieToMenu", 0);
    return rc;
}
