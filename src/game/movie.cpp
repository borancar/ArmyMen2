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

/* Fields of the movie object, by offset. */
#define MOVIE_VTABLE   0x00u   /* re-stamped on stop */
#define MOVIE_SURFACE  0x04u   /* the surface it decodes onto */
#define MOVIE_ACTIVE   0x08u
#define MOVIE_SMACK    0x1Cu   /* the Smack * from SmackOpen */
#define MOVIE_TIMER_ID 0x28u

#define fld(m, off, type) (*(type *)((uint8_t *)(m) + (off)))

/* smackw32 entry points, called through the game's IAT exactly as it does. */
typedef void (__stdcall *am2_smack_close_fn)(void *smack);
typedef void (__stdcall *am2_smack_volumepan_fn)(void *smack, uint32_t trackFlags,
                                                 uint32_t volume, uint32_t pan);
#define smack_close     (*(am2_smack_close_fn *)(uintptr_t)ADDR_IAT_SMACK_CLOSE)
#define smack_volumepan (*(am2_smack_volumepan_fn *)(uintptr_t)ADDR_IAT_SMACK_VOLUMEPAN)

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

int movie_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_MOVIE_STOP, (const void *)MovieStop, "MovieStop", 0);
    rc |= patch_replace(ADDR_MOVIE_SET_VOLUME, (const void *)MovieSetVolume,
                        "MovieSetVolume", 1);
    return rc;
}
