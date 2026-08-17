/* Streaming audio -- reconstructed from ArmyMen2.exe.
 *
 *   StopAudioStream   0x0040D5D0   6 call sites
 *   StartAudioStream  0x0040D680   3 call sites
 *
 * The music player, and the last corner of the boundary that is genuinely about
 * the outside world: a DirectSound buffer played on a loop, a WINMM timer
 * refilling it, and src/game/wavefile.cpp reading the .WAV underneath. All
 * three meet in these two functions.
 *
 * Neither gets far on this machine. Both begin by checking whether streaming is
 * enabled and whether a buffer exists, and DirectSound never opens a device
 * here, so they run and correctly do nothing. That is worth being plain about:
 * the early returns are verified, the bodies are not.
 */

#include "audio.h"
#include "wavefile.h"
#include "../inject/patch.h"

#include <stdint.h>

static_assert(DSBPLAY_LOOPING == 1, "DSBPLAY_LOOPING");

#define g_audioEnabled  (*(const int32_t *)(uintptr_t)ADDR_AUDIO_ENABLED)
#define g_buffer        (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_AUDIO_BUFFER)
#define g_playBuffer    (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_AUDIO_BUFFER_2)
#define g_timerId       (*(UINT *)(uintptr_t)ADDR_AUDIO_TIMER_ID)
#define g_timerRunning  (*(int32_t *)(uintptr_t)ADDR_AUDIO_TIMER_RUN)
#define g_period        (*(const uint32_t *)(uintptr_t)ADDR_AUDIO_PERIOD)
#define g_inCallback    (*(LONG *)(uintptr_t)ADDR_AUDIO_IN_CALLBACK)
#define g_trackArg      (*(int32_t *)(uintptr_t)ADDR_AUDIO_TRACK_ARG)
#define g_hmmio         (*(HMMIO *)(uintptr_t)ADDR_AUDIO_HMMIO)
#define g_waveFormat    (*(WAVEFORMATEX **)(uintptr_t)ADDR_AUDIO_WAVEFORMAT)
#define g_pathArg       (*(void **)(uintptr_t)ADDR_AUDIO_PATH_ARG)
#define g_hWnd          (*(HWND *)(uintptr_t)ADDR_HWND)

/* The timer resolution is stored multiplied by four. */
#define AUDIO_PERIOD_SHIFT 2
#define AUDIO_TIMER_RES    0x0A
#define AUDIO_DRAIN_MS     0x12C   /* 300ms between tries */

typedef void    (__cdecl *am2_audio_prepare_fn)(void *track);
typedef int32_t (__cdecl *am2_audio_check_fn)(void *arg);
#define orig_prepare_track (*(am2_audio_prepare_fn)ADDR_AUDIO_PREPARE)
#define orig_check_path    (*(am2_audio_check_fn)ADDR_AUDIO_CHECK_PATH)

void __cdecl StopAudioStream(void)
{
    if (!g_audioEnabled)
        return;
    if (!g_buffer)
        return;

    if (g_timerRunning) {
        timeKillEvent(g_timerId);
        timeEndPeriod(g_period >> AUDIO_PERIOD_SHIFT);

        /* The refill callback raises this flag while it is inside the buffer.
         * Clear it and keep clearing until it comes back already clear, which
         * is the only handshake there is between the two. */
        if (InterlockedExchange(&g_inCallback, 0)) {
            do {
                Sleep(AUDIO_DRAIN_MS);
            } while (InterlockedExchange(&g_inCallback, 0));
        }
    }

    IDirectSoundBuffer_Stop(g_buffer);
    WaveCloseReadFile(&g_hmmio, &g_waveFormat);
    g_timerRunning = 0;
    IDirectSoundBuffer_Release(g_buffer);
    g_buffer = NULL;
}

void __cdecl StartAudioStream(void *track, int32_t which)
{
    HRESULT hr;

    if (!g_audioEnabled)
        return;

    StopAudioStream();

    if (!orig_check_path(g_pathArg))
        return;

    orig_prepare_track(track);
    g_trackArg = which;

    if (!g_buffer)
        return;

    /* Looping, because the timer below is what keeps the loop from repeating
     * the same few seconds -- it refills behind the play cursor. */
    hr = IDirectSoundBuffer_Play(g_playBuffer, 0, 0, DSBPLAY_LOOPING);
    g_timerRunning = (hr == DS_OK);

    if (timeBeginPeriod(g_period >> AUDIO_PERIOD_SHIFT) != TIMERR_NOERROR) {
        /* No resolution, so nothing would refill it. Better silent than a
         * loop repeating one buffer forever. */
        IDirectSoundBuffer_Stop(g_playBuffer);
        g_timerRunning = 0;
        return;
    }

    g_timerId = timeSetEvent(g_period >> AUDIO_PERIOD_SHIFT, AUDIO_TIMER_RES,
                             (LPTIMECALLBACK)(uintptr_t)ADDR_AUDIO_TIMER_PROC,
                             0, TIME_PERIODIC);
}

#define g_dsBufA (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_DSOUND_BUF_A)
#define g_dsBufB (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_DSOUND_BUF_B)
#define g_dsBufC (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_DSOUND_BUF_C)

void __cdecl ReleaseSoundBuffers(void)
{
    /* Released without being nulled -- see audio.h. */
    if (g_dsBufA)
        IDirectSoundBuffer_Release(g_dsBufA);
    if (g_dsBufB)
        IDirectSoundBuffer_Release(g_dsBufB);
    if (g_dsBufC)
        IDirectSoundBuffer_Release(g_dsBufC);
}

#define g_dsound     (*(LPDIRECTSOUND *)(uintptr_t)ADDR_DSOUND)
#define g_dsPrimary  (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_DS_PRIMARY)
#define g_dsListener (*(LPDIRECTSOUND3DLISTENER *)(uintptr_t)ADDR_DS_LISTENER)
#define g_streamVolume (*(const int32_t *)(uintptr_t)ADDR_STREAM_VOLUME)
#define kIID_DS3DListener (*(const IID *)(uintptr_t)ADDR_IID_DS3D_LISTENER)

typedef HRESULT (WINAPI *am2_dsound_create_fn)(GUID *, LPDIRECTSOUND *, IUnknown *);
#define orig_DirectSoundCreate (*(am2_dsound_create_fn)ADDR_DIRECTSOUNDCREATE)

/* The listener's starting parameters, as float bit patterns in the original. */
#define LISTENER_Z       (-1.0f)
#define DOPPLER_FACTOR   9.9f
#define ROLLOFF_FACTOR   0.25f

static_assert(DSSCL_PRIORITY == 2, "DSSCL_PRIORITY");
static_assert((DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D) == 0x11, "primary caps");
/* The original passes 1, and 1 is DS3D_DEFERRED -- DS3D_IMMEDIATE is 0. Which
 * is the point: all three listener settings are deferred and then applied in
 * one CommitDeferredSettings, so the listener never sees a half-changed state.
 * Reading it as IMMEDIATE would have made that final call look redundant. */
static_assert(DS3D_DEFERRED == 1 && DS3D_IMMEDIATE == 0, "DS3D_*");
static_assert(sizeof(DSBUFFERDESC) == 0x14, "DSBUFFERDESC");

/* Every failure below says which step it was, lets the buffers go and answers
 * 0 -- so the caller only has to check the one result. */
static int32_t SoundFailed(const char *what)
{
    orig_log(what);
    ReleaseSoundBuffers();
    return 0;
}

int32_t __cdecl InitDirectSound(void)
{
    DSBUFFERDESC desc;

    if (orig_DirectSoundCreate(NULL, &g_dsound, NULL) != DS_OK) {
        /* Nothing was created, so nothing is released here. */
        orig_log("Unable to create directsound object\n");
        return 0;
    }

    /* Priority rather than exclusive: the game wants to set the primary
     * buffer's format without stopping everyone else making noise. */
    if (IDirectSound_SetCooperativeLevel(g_dsound, g_hWnd, DSSCL_PRIORITY) != DS_OK)
        return SoundFailed("Unable to set direct sound cooperative level\n");

    desc.dwSize        = sizeof desc;
    desc.dwFlags       = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    desc.dwBufferBytes = 0;
    desc.dwReserved    = 0;
    desc.lpwfxFormat   = NULL;
    if (IDirectSound_CreateSoundBuffer(g_dsound, &desc, &g_dsPrimary, NULL) != DS_OK)
        return SoundFailed("Unable to create primary sound buffer\n");

    /* The 3D listener is not created, it is asked for -- it is the primary
     * buffer seen through another interface. */
    if (IDirectSoundBuffer_QueryInterface(g_dsPrimary, kIID_DS3DListener,
                                          (LPVOID *)&g_dsListener) != DS_OK)
        return SoundFailed("Unable to initialize 3d sound\n");

    if (IDirectSound3DListener_SetPosition(g_dsListener, 0.0f, 0.0f, LISTENER_Z,
                                           DS3D_DEFERRED) != DS_OK)
        return SoundFailed("Unable to set listener position\n");
    if (IDirectSound3DListener_SetDopplerFactor(g_dsListener, DOPPLER_FACTOR,
                                                DS3D_DEFERRED) != DS_OK)
        return SoundFailed("Unable to set sound doppler factor\n");
    if (IDirectSound3DListener_SetRolloffFactor(g_dsListener, ROLLOFF_FACTOR,
                                                DS3D_DEFERRED) != DS_OK)
        return SoundFailed("Unable to set sound roll off factor\n");
    if (IDirectSound3DListener_CommitDeferredSettings(g_dsListener) != DS_OK)
        return SoundFailed("Unable to commit sound settings\n");

    return 1;
}

void __cdecl SetStreamVolume(int32_t pan)
{
    if (!g_audioEnabled)
        return;
    if (!g_buffer)
        return;

    /* Pan is only worth setting if the volume took. */
    if (IDirectSoundBuffer_SetVolume(g_buffer, g_streamVolume) != DS_OK)
        return;
    IDirectSoundBuffer_SetPan(g_buffer, pan);
}

int audio_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_STOP_AUDIO_STREAM, (const void *)StopAudioStream,
                        "StopAudioStream", 0);
    rc |= patch_replace(ADDR_START_AUDIO_STREAM, (const void *)StartAudioStream,
                        "StartAudioStream", 2);
    rc |= patch_replace(ADDR_RELEASE_SOUND_BUFS, (const void *)ReleaseSoundBuffers,
                        "ReleaseSoundBuffers", 0);
    rc |= patch_replace(ADDR_INIT_DIRECTSOUND, (const void *)InitDirectSound,
                        "InitDirectSound", 0);
    rc |= patch_replace(ADDR_SET_STREAM_VOLUME, (const void *)SetStreamVolume,
                        "SetStreamVolume", 1);
    return rc;
}
