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
#define g_looping       (*(int32_t *)(uintptr_t)ADDR_AUDIO_LOOPING)
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
#define orig_check_path    (*(am2_audio_check_fn)ADDR_DATA_PATH_EXISTS)

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
    g_looping = which;

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

/* A sound: its buffer, its sample data, and a flag at +0x18 the player uses. */
#define SOUND_BUFFER 0x00u
#define SOUND_DATA   0x04u
#define SOUND_FLAG   0x18u
#define sfld(s, off, type) (*(type *)((uint8_t *)(s) + (off)))


static_assert(DSBSTATUS_PLAYING == 1, "DSBSTATUS_PLAYING");

/* Stop the buffer if it is playing. Asking first because Stop on an idle
 * buffer also rewinds it, and these are stopped in places that do not want
 * that. */
static void StopIfPlaying(LPDIRECTSOUNDBUFFER buf)
{
    /* Initialised, which the original does not bother to do -- it tests
     * whatever GetStatus left. Identical unless GetStatus fails without
     * writing, in which case the original tests stack garbage and this does
     * not stop the buffer. Deliberate, and the only place in this file that
     * departs from the original at all. */
    DWORD status = 0;

    IDirectSoundBuffer_GetStatus(buf, &status);
    if (status & DSBSTATUS_PLAYING)
        IDirectSoundBuffer_Stop(buf);
}

void __cdecl FreeSound(void *snd)
{
    if (!snd)
        return;

    if (sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER))
        IDirectSoundBuffer_Release(sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER));
    if (sfld(snd, SOUND_DATA, void *))
        orig_free(sfld(snd, SOUND_DATA, void *));
    orig_free(snd);
}

void __cdecl StopAllSounds(void)
{
    uint8_t **slot;
    void    **dyn;

    /* The fixed slots are borrowed, not owned -- stop them and leave them. */
    for (slot = (uint8_t **)(uintptr_t)ADDR_SOUND_SLOTS;
         slot < (uint8_t **)(uintptr_t)ADDR_SOUND_SLOTS_END;
         slot = (uint8_t **)((uint8_t *)slot + SOUND_SLOT_STRIDE)) {
        void *snd = *slot;

        if (snd && sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER))
            StopIfPlaying(sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER));
    }

    /* The dynamic ones were allocated for one use, so they go entirely. This
     * is FreeSound's body written out rather than called -- the original does
     * the same, and it also clears the flag at +0x18 on the way, which
     * FreeSound does not. */
    for (dyn = (void **)(uintptr_t)ADDR_SOUND_DYNAMIC;
         dyn <= (void **)(uintptr_t)ADDR_SOUND_DYNAMIC_LAST; dyn++) {
        void *snd = *dyn;

        if (!snd)
            continue;

        if (sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER)) {
            LPDIRECTSOUNDBUFFER buf = sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER);

            StopIfPlaying(buf);
            sfld(snd, SOUND_FLAG, int32_t) = 0;
            IDirectSoundBuffer_Release(buf);
            sfld(snd, SOUND_BUFFER, LPDIRECTSOUNDBUFFER) = NULL;
        }
        if (sfld(snd, SOUND_DATA, void *))
            orig_free(sfld(snd, SOUND_DATA, void *));
        sfld(snd, SOUND_DATA, void *) = NULL;
        orig_free(snd);
        *dyn = NULL;
    }

    StopAudioStream();
}

/* 8-bit PCM is unsigned, so its silence is 0x80; 16-bit is signed and silence
 * is 0. The original derives this branchlessly and then smears the byte across
 * a dword to fill with `rep stosd`. */
#define SILENCE_8BIT  0x80u

#define g_refillBytes (*(const uint32_t *)(uintptr_t)ADDR_AUDIO_REFILL_BYTES)
#define g_readFailed  (*(int32_t *)(uintptr_t)ADDR_AUDIO_READ_FAILED)
#define g_atEnd       (*(int32_t *)(uintptr_t)ADDR_AUDIO_AT_END)
#define g_validBytes  (*(uint32_t *)(uintptr_t)ADDR_AUDIO_VALID_BYTES)
#define g_dataChunk   ((MMCKINFO *)(uintptr_t)ADDR_AUDIO_DATA_CHUNK)
#define g_riffChunk   ((MMCKINFO *)(uintptr_t)ADDR_AUDIO_RIFF_CHUNK)
#define g_cursorA     (*(int32_t *)(uintptr_t)ADDR_AUDIO_CURSOR_A)
#define g_cursorB     (*(int32_t *)(uintptr_t)ADDR_AUDIO_CURSOR_B)

/* Original: 0x0040CD20. Top the streaming buffer up from the .WAV.
 *
 * Called from the multimedia timer StartAudioStream installs. Locks the buffer,
 * reads into it through src/game/wavefile.cpp, and unlocks. What happens at the
 * end of the file is the whole of the interesting part: looping rewinds to the
 * `data` chunk and keeps reading until the buffer is full, and not looping
 * records how much was real and pads the rest with silence -- 0x80 for 8-bit
 * PCM, which is unsigned, and 0 for 16-bit, which is not.
 *
 * The Lock-failed path still runs the Unlock, with nulls, exactly as the
 * original does. */
void __cdecl RefillAudioBuffer(void)
{
    void    *ptr1 = NULL, *ptr2 = NULL;
    DWORD    len1 = 0, len2 = 0;
    uint32_t got  = 0;

    if (IDirectSoundBuffer_Lock(g_buffer, 0, g_refillBytes, &ptr1, &len1,
                                &ptr2, &len2, 0) != DS_OK) {
        ptr1 = ptr2 = NULL;
        len1 = len2 = 0;
        goto unlock;
    }
    if (len1 == 0)
        goto unlock;

    if (WaveReadFile(g_hmmio, len1, (uint8_t *)ptr1, g_dataChunk, &got) != 0) {
        g_readFailed = 1;
        goto unlock;
    }
    if (got >= len1)
        goto unlock;

    if (g_looping) {
        /* Round again, and again if need be: rewind to the start of the data
         * chunk and keep filling from where the last read stopped. The cursor
         * and the count are both carried across iterations, so a file shorter
         * than the buffer is repeated as many times as it takes. */
        uint8_t *dest      = (uint8_t *)ptr1;
        uint32_t remaining = len1;

        do {
            dest      += got;
            remaining -= got;
            WaveStartDataRead(&g_hmmio, g_dataChunk, g_riffChunk);
            WaveReadFile(g_hmmio, remaining, dest, g_dataChunk, &got);
        } while (got < remaining);
    } else {
        uint8_t silence = (g_waveFormat->wBitsPerSample == 8) ? SILENCE_8BIT : 0;

        g_atEnd      = 1;
        g_validBytes = got;
        memset((uint8_t *)ptr1 + got, silence, len1 - got);
    }

unlock:
    IDirectSoundBuffer_Unlock(g_buffer, ptr1, len1, ptr2, len2);
    g_cursorA = 0;
    g_cursorB = 0;
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
    rc |= patch_replace(ADDR_STOP_ALL_SOUNDS, (const void *)StopAllSounds,
                        "StopAllSounds", 0);
    rc |= patch_replace(ADDR_REFILL_AUDIO, (const void *)RefillAudioBuffer,
                        "RefillAudioBuffer", 0);
    rc |= patch_replace(ADDR_FREE_SOUND, (const void *)FreeSound, "FreeSound", 1);
    return rc;
}
