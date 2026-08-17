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

#include "movie.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

/* Fields of the movie object, by offset. */
#define MOVIE_VTABLE   0x00u   /* re-stamped on stop */
#define MOVIE_SURFACE  0x04u   /* the surface it decodes onto */
#define MOVIE_ACTIVE   0x08u
#define MOVIE_SMACK    0x1Cu   /* the Smack * from SmackOpen */
#define MOVIE_TIMER_ID 0x28u

#define MOVIE_TIMER_RUN 0x0Cu   /* cleared when the last frame is reached */
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

/* Still in the original image: the palette apply and the blit to screen are
 * game logic, and the timer callback that drives all this is never ours. */
typedef void (__attribute__((thiscall)) *am2_movie_arg_fn)(void *movie, void *arg);
#define orig_apply_palette (*(am2_movie_arg_fn)ADDR_MOVIE_APPLY_PALETTE)
#define orig_blit_to_screen (*(am2_movie_arg_fn)ADDR_MOVIE_BLIT)

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
        orig_apply_palette(movie, arg);

        /* See the standing note: a lock used as a question, never answered. */
        hr = IDirectDrawSurface_Lock(surf, NULL, &ddsd, DDLOCK_WAIT, NULL);
        if (hr == DDERR_SURFACELOST) {
            if (IDirectDrawSurface_Restore(surf) != DD_OK)
                return;
            IDirectDrawSurface_SetPalette(
                surf, *(LPDIRECTDRAWPALETTE *)(g_moviePaletteOwner +
                                               MOVIE_PALETTE_OFF));
            orig_apply_palette(movie, arg);
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

    orig_blit_to_screen(movie, arg);

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
    return rc;
}
