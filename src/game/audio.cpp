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

int audio_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_STOP_AUDIO_STREAM, (const void *)StopAudioStream,
                        "StopAudioStream", 0);
    rc |= patch_replace(ADDR_START_AUDIO_STREAM, (const void *)StartAudioStream,
                        "StartAudioStream", 2);
    return rc;
}
