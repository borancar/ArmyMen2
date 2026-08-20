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

typedef LPDIRECTDRAWSURFACE (__cdecl *am2_make_surface_fn)(int32_t w, int32_t h);
#define orig_make_surface (*(am2_make_surface_fn)ADDR_MOVIE_MAKE_SURFACE)
#define g_movieDSound (*(void **)(uintptr_t)ADDR_MOVIE_DSOUND)
#define g_soundReady  (*(int32_t *)(uintptr_t)ADDR_MOVIE_SOUND_READY)

/* Still in the original image: the palette apply and the blit to screen are
 * game logic, and the timer callback that drives all this is never ours. */
typedef void (__attribute__((thiscall)) *am2_movie_arg_fn)(void *movie, void *arg);

#define g_moviePaletteOwner (*(uint8_t **)(uintptr_t)ADDR_MOVIE_PALETTE_OWNER)
#define MOVIE_PALETTE_OFF 0x800u
#define g_hWnd (*(HWND *)(uintptr_t)ADDR_HWND)

/* Non-zero once the movie subsystem has handed Smacker a DirectSound object. */
#define g_movieSoundReady (*(const int32_t *)(uintptr_t)ADDR_MOVIE_SOUND_READY)

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
        void              *current = *(void **)(uintptr_t)ADDR_MOVIE_CURRENT;
        am2_movie_slot_fn *vtbl    = *(am2_movie_slot_fn **)current;

        vtbl[0]();
    }

    fld(movie, MOVIE_TIMER_ID, UINT) =
        timeSetEvent(MOVIE_TIMER_MS, MOVIE_TIMER_RES,
                     (LPTIMECALLBACK)(uintptr_t)ADDR_MOVIE_TIMER_PROC, 0,
                     TIME_PERIODIC);

    if (fld(movie, MOVIE_TIMER_ID, UINT)) {
        fld(movie, MOVIE_ACTIVE, int32_t) = 1;
        return 1;
    }

    /* No timer, so no movie. Tear it down and let the game carry on. */
    {
        void *current = *(void **)(uintptr_t)ADDR_MOVIE_CURRENT;

        if (current) {
            MovieStop(current);
            orig_delete(current);
        }
        *(void **)(uintptr_t)ADDR_MOVIE_CURRENT = NULL;
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
        orig_make_surface(fld(movie, MOVIE_SRC_W, int32_t) -
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

int movie_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_MOVIE_STOP, (const void *)MovieStop, "MovieStop", 0);
    rc |= patch_replace(ADDR_MOVIE_SET_VOLUME, (const void *)MovieSetVolume,
                        "MovieSetVolume", 1);
    rc |= patch_replace(ADDR_MOVIE_DRAW_FRAME, (const void *)MovieDrawFrame,
                        "MovieDrawFrame", 1);
    rc |= patch_replace(ADDR_MOVIE_FINISHED, (const void *)MovieFinished,
                        "MovieFinished", 0);
    rc |= patch_replace(ADDR_MOVIE_OPEN, (const void *)MovieOpen, "MovieOpen", 4);
    rc |= patch_replace(ADDR_MOVIE_START, (const void *)MovieStart, "MovieStart", 1);
    rc |= patch_replace(ADDR_MOVIE_POLL, (const void *)MoviePoll, "MoviePoll", 0);
    rc |= patch_replace(ADDR_MOVIE_APPLY_PALETTE, (const void *)MovieApplyPalette,
                        "MovieApplyPalette", 1);
    return rc;
}
