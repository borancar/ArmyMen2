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
#include "../pmf.h"
#include "movie.h"
#include "widget.h"      /* the widget vtable slots + DlgSelectMapDelete (movie vtable) */
#include "../gamedir.h"   /* SetGameDir, FileExists -- reconstructed */
#include "../gameproc.h"  /* SetGameOver, RequestState -- reconstructed */
#include "../misc.h"      /* MovieBuildName -- reconstructed */
#include "../image.h"
#include "palette.h"
#include "../../inject/patch.h"

#include <stdint.h>
#include <string.h>

#ifdef AM2_STANDALONE
/* am2_state_actions -- 0x0048654C, the game-state action table StateEnter0 and
 * WndProc dispatch through. Five 12-byte AM2_StateAction records {onEnter,
 * onMessage, id}; the handlers cast from their int32_t(void) reconstructions
 * to the table's void(void) slot type, exactly as the image stores the raw
 * addresses. Record 4's `id` field OVERLAPS the credits string the linker
 * placed right after (0x64657263 == "cred"); it is transcribed as that value
 * for byte-fidelity, and nothing reads record 4's id. This is the first
 * function-pointer table off the generator's fixups -- checkimagedata verifies
 * each pointer against the reconstruction that patches the image's address. */
extern "C" const AM2_StateAction am2_state_actions[5] = {
    { (am2_state_action_fn)StateEnterLogoMovie,
      (am2_state_action_fn)StateMessageLogoMovie,    1 },
    { (am2_state_action_fn)StateEnterAct1Movie,
      (am2_state_action_fn)StateMessageMovieToMenu,  2 },
    { (am2_state_action_fn)ReturnZero,
      (am2_state_action_fn)ReturnZero,               3 },
    { (am2_state_action_fn)ReturnZero,
      (am2_state_action_fn)ReturnZero,               4 },
    { (am2_state_action_fn)StateEnterCreditsMovie,
      (am2_state_action_fn)StateMessageMovieToMenu,  0x64657263u },
};

/* am2_movie_vtable -- 0x0046FAB4, the movie widget's vtable. MovieStop/MovieStart
 * stamp its address into the object, and MovieStepCurrent calls slot 0 (MoviePoll)
 * through it; the other five are the widget base slots, dispatched by the (still
 * original) widget layer. The original stored the raw function addresses here and
 * mkglobals rewrote them to our reconstructions at startup; transcribed as a named
 * C table it needs no fixup -- checkimagedata verifies each pointer against the
 * reconstruction that patches its image address, and the MIGRATED range in
 * mkglobals.py drops the six blob fixups. Six slots; anything past them
 * (0x0046FACC on) stays a fixed-up blob vtable and is unaffected. */
extern "C" const am2_movie_vfn am2_movie_vtable[6] = {
    (am2_movie_vfn)MoviePoll,
    (am2_movie_vfn)DlgSelectMapDelete,
    (am2_movie_vfn)WidgetPaintFwd2,
    (am2_movie_vfn)WidgetUpdateCancel,
    (am2_movie_vfn)WidgetTakeFocus,
    (am2_movie_vfn)WidgetRepaint,
};
#endif

/* The movie object and the Smacker handle are typed in movie.h. */

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
#define smack_close     (g_iatSmackClose)
#define smack_volumepan (g_iatSmackVolumepan)
#define smack_tobuffer  (g_iatSmackToBuffer)
#define smack_doframe   (g_iatSmackDoFrame)
#define smack_nextframe (g_iatSmackNextFrame)
#define smack_open      (g_iatSmackOpen)
#define smack_wait      (g_iatSmackWait)
#define smack_ddtype    (g_iatSmackDdtype)
#define smack_use_dsound (g_iatSmackUseDsound)
#if defined(_MSC_VER)
/* Under VC6 the imports are what the original declared: dllimport, so a
 * function called repeatedly has its IAT slot loaded into a register once. */
extern "C" {
__declspec(dllimport) void __stdcall SmackVolumePan(void *smack, uint32_t trackFlags,
                                                   uint32_t volume, uint32_t pan);
__declspec(dllimport) void __stdcall SmackToBuffer(void *smack, uint32_t left, uint32_t top,
                                                  uint32_t pitch, uint32_t destHeight,
                                                  void *dest, uint32_t flags);
__declspec(dllimport) void __stdcall SmackDoFrame(void *smack);
__declspec(dllimport) void __stdcall SmackNextFrame(void *smack);
__declspec(dllimport) uint32_t __stdcall SmackWait(void *smack);
__declspec(dllimport) void __stdcall SmackClose(void *smack);
__declspec(dllimport) void __stdcall SmackSoundUseDirectSound(void *dsound);
__declspec(dllimport) void *__stdcall SmackOpen(const char *name, uint32_t flags, uint32_t extra);
__declspec(dllimport) uint32_t __stdcall SmackDDSurfaceType(void *surface);
}
#undef smack_volumepan
#undef smack_tobuffer
#undef smack_doframe
#undef smack_nextframe
#undef smack_wait
#undef smack_close
#undef smack_use_dsound
#undef smack_open
#undef smack_ddtype
#define smack_volumepan  SmackVolumePan
#define smack_tobuffer   SmackToBuffer
#define smack_doframe    SmackDoFrame
#define smack_nextframe  SmackNextFrame
#define smack_wait(s)    SmackWait((void *)(s))
#define smack_close      SmackClose
#define smack_use_dsound SmackSoundUseDirectSound
#define smack_open       SmackOpen
#define smack_ddtype(s)  SmackDDSurfaceType((void *)(s))
#endif

/* Smack handle fields used only by the opener. */

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
#define orig_operator_new     ((AM2_OperatorNewFn)((void *)(uintptr_t)(ADDR_GAME_OPERATOR_NEW)))


/* Still in the original image: the palette apply and the blit to screen are
 * game logic, and the timer callback that drives all this is never ours. */

#define MOVIE_PALETTE_OFF 0x800u
#define g_hWnd (*(HWND *)&g_enumContext)

/* Non-zero once the movie subsystem has handed Smacker a DirectSound object. */

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

AM2_Movie::~AM2_Movie()
{
    AM2_Movie *movie = this;

    /* Re-stamped before anything else, and unconditionally -- so it happens
     * even on the early returns below. */
    movie->vtbl = g_movieVtable;

    if (!movie->smack)
        return;
    if (!movie->active)
        return;

    if (movie->timerId)
        timeKillEvent(movie->timerId);
    movie->timerId = 0;
    movie->active = 0;

    IDirectDrawSurface_Release(movie->surface);
    movie->surface = NULL;

    /* The handle is closed but not cleared, which is what the original does --
     * so a second Stop would close it twice. Both call sites drop the object
     * straight afterwards, which is presumably why it never mattered. */
    smack_close(movie->smack);
}

void __attribute__((thiscall)) AM2_Movie::MovieSetVolume(int32_t volume)
{
    AM2_Movie *movie = this;

    if (!g_soundReady)
        return;

    /* Seven calls written out, one per track bit -- no loop in the image. */
    smack_volumepan(movie->smack, 0x0002000u, (uint32_t)volume, SMACK_PAN_CENTRE);
    smack_volumepan(movie->smack, 0x0004000u, (uint32_t)volume, SMACK_PAN_CENTRE);
    smack_volumepan(movie->smack, 0x0008000u, (uint32_t)volume, SMACK_PAN_CENTRE);
    smack_volumepan(movie->smack, 0x0010000u, (uint32_t)volume, SMACK_PAN_CENTRE);
    smack_volumepan(movie->smack, 0x0020000u, (uint32_t)volume, SMACK_PAN_CENTRE);
    smack_volumepan(movie->smack, 0x0040000u, (uint32_t)volume, SMACK_PAN_CENTRE);
    smack_volumepan(movie->smack, 0x0080000u, (uint32_t)volume, SMACK_PAN_CENTRE);
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
void __attribute__((thiscall)) AM2_Movie::MovieApplyPalette(LPDIRECTDRAWSURFACE surf)
{
    AM2_Movie *movie = this;

    LPDIRECTDRAWPALETTE pal = NULL;
    const uint8_t      *src;
    LPPALETTEENTRY      dst = (LPPALETTEENTRY)((uintptr_t)g_systemPalette_);
    int32_t             i;

    src = &movie->smack->palette[MOVIE_PALETTE_FIRST * 3];

    /* Indexed through the global itself, and bounded `< LAST + 1`: both are
     * what put the loop pointer and its test where the original has them. */
    for (i = MOVIE_PALETTE_FIRST; i < MOVIE_PALETTE_LAST + 1; i++) {
        ((LPPALETTEENTRY)((uintptr_t)g_systemPalette_))[i].peRed   = *src++;
        ((LPPALETTEENTRY)((uintptr_t)g_systemPalette_))[i].peGreen = *src++;
        ((LPPALETTEENTRY)((uintptr_t)g_systemPalette_))[i].peBlue  = *src++;
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
int32_t __attribute__((thiscall)) AM2_Movie::MovieStart(void *arg)
{
    AM2_Movie *movie = this;
    int32_t r = 0;

    (void)arg;

    if (movie->smack && !movie->running) {
        movie->running = 1;

        /* Slot 0 of the current movie's own table, a thiscall on it. */
        ObjVPoll(g_movieCurrent);

        movie->timerId =
            timeSetEvent(MOVIE_TIMER_MS, MOVIE_TIMER_RES,
                         MovieTimerProc, 0,
                         TIME_PERIODIC);

        if (!movie->timerId) {
            /* No timer, so no movie. Tear it down and let the game carry
             * on. */
            delete g_movieCurrent;
            g_movieCurrent = NULL;
            PostMessageA(g_hWnd, AM2_WM_STATE_ABORT, 0, 0);
            return 0;
        }
        movie->active = 1;
        r = 1;
    }
    return r;
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
 * A real constructor: PlayMovie builds the object with `new`. */
AM2_Movie::AM2_Movie(const char *name, int32_t wantW, int32_t wantH, int32_t big)
{
    AM2_Movie *movie = this;

    AM2_Smack *smack;

    movie->vtbl = g_movieVtable;
    movie->active = 0;
    movie->big = big;
    movie->wantW = wantW;
    movie->wantH = wantH;
    movie->running = 0;
    movie->sized = 0;
    movie->frameDue = 0;
    movie->srcX = 0;
    movie->srcY = 0;
    movie->srcW = 0;
    movie->srcH = 0;
    movie->timerId = 0;
    movie->smack = NULL;
    movie->field20 = 0;

    strcpy(movie->name, name);

    if (movie->wantW != -1)
        movie->sized = 1;

    /* Once per process, and only if there is a DirectSound to give. */
    if (!g_soundReady && g_movieDSound) {
        smack_use_dsound(g_movieDSound);
        g_soundReady = 1;
    }

    movie->srcX = 0;
    movie->srcY = 0;

    if (movie->big)
        smack = (AM2_Smack *)smack_open(name, 0x1FE440u, 0xFFFFFFFFu);
    else
        smack = (AM2_Smack *)smack_open(name, 0x000FE040u, 0xFFFFFFFFu);
    movie->smack = smack;

    if (smack) {
        if (movie->sized) {
            /* The caller named an extent, so the source rectangle is inclusive
             * of the last pixel rather than one past it. */
            movie->srcW =
                movie->wantW - 1;
            movie->srcH =
                movie->wantH - 1;
        } else {
            movie->srcW = smack->width;
            movie->srcH = smack->height;
        }
    }

    /* Note this runs whether or not the open succeeded, on a source rectangle
     * that is still zero if it did not. Kept as written. */
    movie->surface =
        MovieMakeSurface(movie->srcW -
                          movie->srcX,
                          movie->srcH -
                          movie->srcY);
    movie->ddType =
        smack_ddtype(movie->surface);

    /* A member: called with `this` in ecx, which it ignores. Being C++ is
     * also what makes this constructor one that may throw, which is why
     * PlayMovie's `new` carries an EH state. */
    this->SnapshotSystemPalette();
}

#if defined(__GNUC__)
void *AM2_Movie::operator new(size_t n)
{
    return ((void *(__cdecl *)(uint32_t))(uintptr_t)ADDR_GAME_OPERATOR_NEW)(n);
}
void AM2_Movie::operator delete(void *p)
{
    ((void (__cdecl *)(void *))(uintptr_t)ADDR_GAME_DELETE)(p);
}
void *__attribute__((thiscall)) am2_ctor_AM2_Movie(void *mem, const char *name, int32_t wantW, int32_t wantH, int32_t big)
{ return ::new (mem) AM2_Movie(name, wantW, wantH, big); }
#endif

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
void __attribute__((thiscall)) AM2_Movie::MovieDrawFrame(void *arg)
{
    AM2_Movie *movie = this;

    DDSURFACEDESC ddsd;
    /* Both are read off the movie at every use. */
#define smack (movie->smack)
#define surf  (movie->surface)
    HRESULT       hr;

    /* The timer does not stop when the window loses focus, so this does. */
    if (GetFocus() != g_hWnd)
        return;

    if (smack->newPalette &&
        !movie->ddType) {
        ::MovieApplyPalette(movie, (LPDIRECTDRAWSURFACE)arg);

        /* See the standing note: a lock used as a question, never answered. */
        hr = IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL);
        if (hr == DDERR_SURFACELOST) {
            if (IDirectDrawSurface_Restore(surf) != DD_OK)
                return;
            IDirectDrawSurface_SetPalette(
                surf, *(LPDIRECTDRAWPALETTE *)(g_moviePaletteOwner +
                                               MOVIE_PALETTE_OFF));
            ::MovieApplyPalette(movie, (LPDIRECTDRAWSURFACE)arg);
        }
    }

    memset(&ddsd, 0, sizeof ddsd);
    ddsd.dwSize = sizeof ddsd;
    hr = IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL);
    if (hr == DDERR_SURFACELOST && IDirectDrawSurface_Restore(surf) != DD_OK)
        return;

    /* Straight into the locked bits, at the surface's own pitch. */
    smack_tobuffer(smack, 0, 0, (uint32_t)ddsd.lPitch,
                   smack->height, ddsd.lpSurface,
                   movie->ddType);
    smack_doframe(smack);
    IDirectDrawSurface_Unlock(surf, NULL);

    BlitCentred(movie, (LPDIRECTDRAWSURFACE)arg);

    /* Last frame ends the movie; anything else advances it. */
    if (smack->frameNum ==
        smack->frames - 1) {
        movie->running = 0;
        MovieFinished();
    } else {
        smack_nextframe(smack);
    }
#undef surf
#undef smack
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
int32_t __attribute__((thiscall)) AM2_Movie::MoviePoll()
{
    AM2_Movie *movie = this;

    if (movie->running &&
        smack_wait(movie->smack) == 0)
        ::MovieDrawFrame(movie, *(LPDIRECTDRAWSURFACE *)&g_primarySurface);
    return 1;
}

void __cdecl MovieSetCurrent(AM2_Movie *movie)
{
    g_movieCurrent = movie;
}

void __cdecl MovieStepCurrent(void)
{
    AM2_Movie *movie = g_movieCurrent;

    if (!movie)
        return;
    /* Object, then table, then slot -- three levels, and the middle one is
     * easy to lose. Slot 0 is MoviePoll, called thiscall as the original. */
    ObjVPoll(movie);
    /* Re-read the global, as the original does -- but NOT tested again: the
     * original stores through it unconditionally (end of 0x00445630), so a poll
     * that cleared it would fault there too. It never does. */
    g_movieCurrent->frameDue = 0;
}

void __cdecl MovieEndCurrent(void)
{
    AM2_Movie *movie = g_movieCurrent;

    if (!movie)
        return;
    movie->running = 0;
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
    if (g_stateMovie) {
        AM2_Movie *movie;

        MovieForget();

        movie = g_stateMovie;
        delete movie;
        g_stateMovie = (AM2_Movie *)0;
    }

    ClearSurface((*(LPDIRECTDRAWSURFACE *)&g_primarySurface), 0);
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
    int32_t volume;

    if (strncmp("3do", name, 3) == 0
        || strncmp("credits",
                   name, 7) == 0)
        SetGameDir(g_dirScratch);
    else
        SetGameDir(g_strAviDir);

    if (!FileExists(name)
        || g_optNoMovies) {
        /* MovieFinished's body, inline here -- the image posts directly at
         * 0x0042E705 where its other two callers call 0x00445600. */
        PostMessageA(g_hWnd, WM_USER, 0, 0);
        return;
    }

    StateLeave();

    g_stateMovie = new AM2_Movie(name, -1, -1, big);

    ClearSurface((*(LPDIRECTDRAWSURFACE *)&g_primarySurface), 0);
    MovieSetCurrent(g_stateMovie);

    volume = g_voiceVolume;
    if (volume <= AM2_MOVIE_VOLUME_FLOOR)
        MovieSetVolume(g_stateMovie, 0);
    else
        MovieSetVolume(g_stateMovie, volume * 3 + 0x8000);
    MovieStart(g_stateMovie, (void *)0);
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

    MovieBuildName(name, "3do");
    PlayMovie(name, 0);
    return 0;
}

int32_t __cdecl StateEnterAct1Movie(void)
{
    char name[0x20];

    /* The directory table's first entry, which is "avi". */
    SetGameDir(g_strAviDir);
    MovieBuildName(name, "act1");
    PlayMovie(name, 0);
    return 0;
}

int32_t __cdecl StateEnterCreditsMovie(void)
{
    char name[0x20];

    MovieBuildName(name, "credits");
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
    AM2_Movie *movie = g_movieCurrent;

    (void)id; (void)msg; (void)user; (void)dw1; (void)dw2;

    if (movie)
        movie->frameDue = 1;
}

#if defined(__GNUC__)
void __attribute__((thiscall)) am2_dtor_AM2_Movie(AM2_Movie *m)
{ m->AM2_Movie::~AM2_Movie(); }
#endif

int movie_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_MOVIE_MAKE_SURFACE, (const void *)MovieMakeSurface,
                        "MovieMakeSurface", 2);
    rc |= patch_replace(ADDR_MOVIE_STOP, AM2_DTOR(AM2_Movie), "MovieStop", 0);
    rc |= patch_replace(ADDR_MOVIE_SET_VOLUME, pmf_addr(&AM2_Movie::MovieSetVolume),
                        "MovieSetVolume", 1);
    rc |= patch_replace(ADDR_MOVIE_DRAW_FRAME, pmf_addr(&AM2_Movie::MovieDrawFrame),
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
    rc |= patch_replace(ADDR_MOVIE_OPEN, AM2_CTOR(AM2_Movie), "MovieOpen", 4);
    rc |= patch_replace(ADDR_MOVIE_START, pmf_addr(&AM2_Movie::MovieStart), "MovieStart", 1);
    rc |= patch_replace(ADDR_PLAY_MOVIE, (const void *)PlayMovie,
                        "PlayMovie", 4);
    rc |= patch_replace(ADDR_MOVIE_POLL, pmf_addr(&AM2_Movie::MoviePoll), "MoviePoll", 0);
    rc |= patch_replace(ADDR_MOVIE_APPLY_PALETTE, pmf_addr(&AM2_Movie::MovieApplyPalette),
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
