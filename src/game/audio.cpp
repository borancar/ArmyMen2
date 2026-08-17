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
#include "dist.h"
#include "../inject/hooklog.h"

#include <stdlib.h>
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

    /* Ours, not the image's. This is the WndProc shape again -- the callback is
     * REGISTERED rather than detoured, and 0x0040D020 has exactly one reference
     * in the whole binary, which is this call. Patching it would install a jump
     * nothing ever reaches. The consequence is that it has no trace counter,
     * because the counters are the patch stubs. */
    g_timerId = timeSetEvent(g_period >> AUDIO_PERIOD_SHIFT, AUDIO_TIMER_RES,
                             AudioTimerProc, 0, TIME_PERIODIC);
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

#define g_bufferSize (*(const uint32_t *)(uintptr_t)ADDR_AUDIO_BUFFER_SIZE)
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

    if (IDirectSoundBuffer_Lock(g_buffer, 0, g_bufferSize, &ptr1, &len1,
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

/* The streaming refill, driven by a multimedia timer -- 0x0040D020.
 *
 * StartAudioStream hands this to timeSetEvent, so it is an LPTIMECALLBACK and
 * runs on winmm's thread, not the game's. It is the last DirectSound cluster
 * outside the reconstruction: GetStatus, Restore, GetCurrentPosition, Lock,
 * Unlock and Play, plus InterlockedExchange and PostMessageA. RefillAudioBuffer
 * is its sibling and fills the whole buffer once; this one keeps up with the
 * play cursor forever after.
 *
 * It had sat in orig.h as "the refill callback, stays original" with no reason
 * given, which CLAUDE.md says is the kind of decline worth revisiting.
 *
 * The shape is the standard circular-buffer chase. Whatever the play cursor has
 * passed since last time is free to overwrite, so that span is locked -- which
 * can come back as two regions when it wraps the end -- refilled from the .WAV,
 * and unlocked. A lost buffer is restored and then rewritten in full, because
 * restoring gets the memory back and not the samples.
 *
 * RE-ENTRANCY IS THE FIRST THING IT DOES. InterlockedExchange(&flag, 1) returns
 * what was there, so a non-zero answer means another tick is still inside and
 * this one returns immediately. Every exit path clears it. That is also why the
 * function is written with one exit label rather than early returns.
 *
 * Two asymmetries between the two lock regions are the original's, not slips.
 * The first region checks WaveReadFile's return and posts the end-of-stream
 * message if it failed; the second ignores the return entirely. And the
 * end-of-file bookkeeping -- setting AT_END and working out how many bytes are
 * still real -- is duplicated rather than shared.
 *
 * A THIRD, and the one most likely to be audible: when the file ends mid-way
 * through the SECOND lock region and the stream is not looping, the silence
 * fill starts at the beginning of that region rather than after the bytes just
 * read, so those samples are thrown away. Region one gets this right. The
 * difference is two instructions -- `sub` and `lea` -- present in the first
 * fill and absent from the second.
 *
 * A FOURTH, kept: when that first read fails the function jumps straight
 * to clearing the re-entrancy flag WITHOUT unlocking, so the buffer stays
 * locked and every later refill fails. Same shape as the Lock-failure paths,
 * which also skip the Unlock -- correctly, since there is nothing to unlock --
 * except that here there is.
 *
 * THE RESTORED FLAG IS READ UNINITIALISED, and this is a genuine defect in the
 * original rather than a misreading. Its stack slot is written in exactly one
 * place, the buffer-lost branch; the whole function contains only two stores
 * into its frame and the other is `status = 0`. MSVC kept the variable in ebx,
 * spilled it only on the branch that reassigns ebx, and never emitted the store
 * for the common path -- so the reload near the end picks up whatever the
 * previous tick left in that slot.
 *
 * It is initialised to 0 here. C cannot express "read uninitialised stack"
 * without undefined behaviour, and the whole observable consequence is a
 * possible extra Play(0, 0, DSBPLAY_LOOPING) on a buffer that is already
 * playing and looping, which DirectSound treats as a no-op. Written down rather
 * than quietly fixed, because it is a behavioural difference however small.
 *
 * Exercised: the stream runs whenever there is a device, so the ALSA null
 * configuration in CLAUDE.md reaches it. */
static_assert(DSBSTATUS_BUFFERLOST == 2, "DSBSTATUS_BUFFERLOST");
static_assert(DSBPLAY_LOOPING == 1, "DSBPLAY_LOOPING");

/* Posted to the game window when the stream runs dry. WM_USER + 0x100. */
#define AUDIO_STREAM_DONE 0x0500u

#define g_playBuffer  (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_AUDIO_BUFFER_2)
#define g_hwnd        (*(HWND *)(uintptr_t)ADDR_HWND)

/* 0x80 for unsigned 8-bit PCM, 0 for signed 16-bit. The original derives it
 * branchlessly with setne/dec/and and smears it across a dword; the four
 * places it does so are identical, so it is one helper here. */
static void fill_silence(void *dest, uint32_t bytes)
{
    memset(dest, (g_waveFormat->wBitsPerSample == 8) ? SILENCE_8BIT : 0, bytes);
}

/* The end-of-file bookkeeping the original writes out twice, once per region:
 * mark the stream ended, and record how much of the buffer is still real as the
 * distance from the PLAY cursor round to where we have written. Note it is the
 * play cursor and not the write cursor -- the original reads the slot
 * GetCurrentPosition filled with the former -- which is what makes the number
 * mean "samples still to be heard". */
static void mark_stream_end(uint32_t playCursor)
{
    g_atEnd = 1;
    g_validBytes = (playCursor > (uint32_t)g_cursorB)
                 ? g_bufferSize - playCursor + (uint32_t)g_cursorB
                 : (uint32_t)g_cursorB - playCursor;
}

/* Rewind to the start of the `data` chunk and keep reading until `bytes` of
 * `dest` are full, however many times round the file that takes. */
static void refill_looping(uint8_t *dest, uint32_t bytes, uint32_t got)
{
    do {
        dest  += got;
        bytes -= got;
        WaveStartDataRead(&g_hmmio, g_dataChunk, g_riffChunk);
        WaveReadFile(g_hmmio, bytes, dest, g_dataChunk, &got);
    } while (got < bytes);
}

void CALLBACK AudioTimerProc(UINT uID, UINT uMsg, DWORD_PTR dwUser,
                             DWORD_PTR dw1, DWORD_PTR dw2)
{
    void    *ptr1 = NULL, *ptr2 = NULL;
    DWORD    len1 = 0, len2 = 0;
    DWORD    status = 0, statusAgain, play = 0, write = 0;
    uint32_t avail, toWrite, got = 0;
    int32_t  restored = 0;          /* see the note above -- the original's is
                                       uninitialised on the common path */

    (void)uID; (void)uMsg; (void)dwUser; (void)dw1; (void)dw2;

    if (InterlockedExchange(&g_inCallback, 1))
        return;                     /* a previous tick has not finished */

    IDirectSoundBuffer_GetStatus(g_playBuffer, &status);
    if (status & DSBSTATUS_BUFFERLOST) {
        if (IDirectSoundBuffer_Restore(g_playBuffer) != DS_OK)
            goto done;
        restored  = 1;
        g_cursorB = 0;
    }

    /* Asked for and discarded, as in the original. */
    IDirectSoundBuffer_GetStatus(g_playBuffer, &statusAgain);
    IDirectSoundBuffer_GetCurrentPosition(g_playBuffer, &play, &write);

    /* Nothing has been consumed since last time, and nothing was lost. */
    if (play == (DWORD)g_cursorB && !restored)
        goto done;

    /* Ended, and the last real samples have now played out. Tell the game
     * once. */
    if (g_atEnd && !g_validBytes) {
        if (!g_readFailed) {
            g_readFailed = 1;
            PostMessageA(g_hwnd, AUDIO_STREAM_DONE, 0, 0);
        }
        goto done;
    }

    avail = (play >= (DWORD)g_cursorB)
          ? play - (uint32_t)g_cursorB
          : g_bufferSize - (uint32_t)g_cursorB + play;

    /* A restored buffer has lost every sample in it, so rewrite all of it. */
    toWrite    = restored ? g_bufferSize : avail;
    g_cursorA += (int32_t)avail;

    if (g_atEnd && g_validBytes) {
        /* Past the end of the file: keep the buffer fed with silence and count
         * down what is still real ahead of the play cursor. */
        g_validBytes = (avail > g_validBytes) ? 0 : g_validBytes - avail;

        if (IDirectSoundBuffer_Lock(g_playBuffer, (DWORD)g_cursorB, toWrite,
                                    &ptr1, &len1, &ptr2, &len2, 0) != DS_OK)
            goto done;

        fill_silence(ptr1, len1);
        if (ptr2 && len2)
            fill_silence(ptr2, len2);
    } else {
        if (IDirectSoundBuffer_Lock(g_playBuffer, (DWORD)g_cursorB, toWrite,
                                    &ptr1, &len1, &ptr2, &len2, 0) != DS_OK)
            goto done;

        if (len1) {
            if (g_readFailed) {
                fill_silence(ptr1, len1);
            } else if (WaveReadFile(g_hmmio, len1, (uint8_t *)ptr1,
                                    g_dataChunk, &got) != 0) {
                /* Only the first region reports a read failure. */
                if (!g_readFailed) {
                    g_readFailed = 1;
                    PostMessageA(g_hwnd, AUDIO_STREAM_DONE, 0, 0);
                }
                goto done;
            } else if (got < len1) {
                if (g_looping)
                    refill_looping((uint8_t *)ptr1, len1, got);
                else {
                    fill_silence((uint8_t *)ptr1 + got, len1 - got);
                    mark_stream_end(play);
                }
            }
        }

        if (len2) {
            if (g_readFailed) {
                fill_silence(ptr2, len2);
            } else {
                /* The second region does NOT check the return. Original. */
                WaveReadFile(g_hmmio, len2, (uint8_t *)ptr2, g_dataChunk, &got);
                if (got < len2) {
                    if (g_looping)
                        refill_looping((uint8_t *)ptr2, len2, got);
                    else {
                        /* THE WHOLE REGION, from its start -- not from `got`
                         * as region 1 does. The original has `sub ecx,edx` and
                         * `lea edi,[edx+esi]` in the first fill and neither in
                         * this one, so the samples just read are overwritten
                         * with silence. Kept; see the note at the top. */
                        fill_silence(ptr2, len2);
                        mark_stream_end(play);
                    }
                }
            }
        }
    }

    IDirectSoundBuffer_Unlock(g_playBuffer, ptr1, len1, ptr2, len2);

    g_cursorB += (int32_t)toWrite;
    if ((uint32_t)g_cursorB >= g_bufferSize)
        g_cursorB -= (int32_t)g_bufferSize;

    if (restored)
        IDirectSoundBuffer_Play(g_playBuffer, 0, 0, DSBPLAY_LOOPING);

done:
    InterlockedExchange(&g_inCallback, 0);
}

/* Put sample data into a DirectSound buffer -- 0x0040C440.
 *
 * The Lock/Unlock bracket, and the only place the game writes audio samples
 * across the boundary. A DirectSound buffer is circular, so a lock can hand
 * back two regions rather than one when the request wraps the end; both are
 * filled, the second continuing where the first stopped.
 *
 * How much is written is decided by DirectSound, not by the caller. `size` is
 * what is asked for, but the copies use the lengths the lock reports -- so a
 * buffer smaller than the request is filled to its own capacity rather than
 * overrun.
 *
 * STANDING NOTE -- the first region is cleared and then immediately overwritten
 * in full by the copy that follows, so the clear cannot be observed. It is the
 * original's and it is kept.
 *
 * The original reuses its own argument slots to receive the lock's out
 * parameters, which is why the disassembly appears to overwrite its arguments;
 * the two it still needs are in registers by then. Written with proper locals
 * here -- the addresses differ, nothing observable does.
 *
 * Not exercised on this machine: there is no audio device, so no buffer ever
 * exists to fill. See the note at the top of this file. */
static_assert(DSBLOCK_ENTIREBUFFER == 2, "DSBLOCK_ENTIREBUFFER");

int32_t __cdecl FillSoundBuffer(LPDIRECTSOUNDBUFFER buf, const uint8_t *data,
                                uint32_t size)
{
    void  *first, *second;
    DWORD  firstLen, secondLen;

    if (!buf || !data || !size) {
        orig_log((const char *)(uintptr_t)ADDR_STR_SND_NO_ARGS);
        return 0;
    }

    if (IDirectSoundBuffer_Lock(buf, 0, size, &first, &firstLen,
                                &second, &secondLen, 0) != DS_OK) {
        orig_log((const char *)(uintptr_t)ADDR_STR_SND_LOCK_FAIL);
        return 0;
    }

    memset(first, 0, firstLen);          /* see the note above */
    memcpy(first, data, firstLen);
    if (secondLen)
        memcpy(second, data + firstLen, secondLen);

    IDirectSoundBuffer_Unlock(buf, first, firstLen, second, secondLen);
    return 1;
}

/* ---- observing the sample path without an audio device -----------------
 *
 * AM2_DUMP_SOUND=1 makes each wave report what it is about to hand to
 * DirectSound: the name, the byte count, and an FNV-1a hash of the samples.
 *
 * This is the answer to a real problem. Nearly every function in this file is
 * unexercised, because DirectSound will not start without a device, and the
 * usual checks -- build, fingerprints, the A/B -- all pass whether the code is
 * right or wrong. Making sound audible is not the point and would not help
 * anyway; what is wanted is evidence that the bytes reaching the buffer are the
 * bytes in the file, and that can be had by printing a hash of them and
 * computing the same hash from the .WAV on disk.
 *
 * It covers WaveOpenFile, WaveReadFile, LoadWaveSound and everything they use
 * -- the whole read half of the path. It says nothing about the Lock/copy/
 * Unlock inside FillSoundBuffer, which is the half that needs a real buffer.
 *
 * Off unless asked for, and it writes through the harness logger rather than
 * the game's, so it cannot be mistaken for 1999 debug output. */
static int sound_dump_on(void)
{
    static int on = -1;

    if (on < 0) {
        const char *opt = getenv("AM2_DUMP_SOUND");
        on = (opt && *opt == '1');
    }
    return on;
}

static void sound_dump(const char *name, const uint8_t *data, uint32_t len)
{
    uint32_t hash = 2166136261u;   /* FNV-1a */
    uint32_t i;

    if (!sound_dump_on() || !data)
        return;
    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    hooklog("sound: %-20s %7u bytes  fnv1a=%08x", name, len, hash);
}

/* Shared by the three playback functions below: where the ear is, the two
 * volume levels, the object lookup and the wave reader. */
typedef void *(__cdecl *am2_lookup_uid_fn)(uint32_t uid);
#define orig_lookup_uid (*(am2_lookup_uid_fn)ADDR_LOOKUP_BY_UID)
#define g_listenerPos   (*(const AM2_Point *)(uintptr_t)ADDR_LISTENER_POS)
#define g_defaultPos    (*(const AM2_Point *)(uintptr_t)ADDR_DEFAULT_SOUND_POS)
#define g_volumeAtZero  (*(const int32_t *)(uintptr_t)ADDR_VOLUME_AT_ZERO)

/* The reader fills four out-parameters, and two of them are fields of the
 * DSBUFFERDESC itself -- which is why the original only ever assigns dwSize
 * and dwFlags by hand. Getting that wrong leaves the descriptor with no format
 * and no length, and CreateSoundBuffer refuses every wave in the game. */
typedef int32_t (__cdecl *am2_read_wave_fn)(int32_t zero, const char *name,
                                            LPWAVEFORMATEX *format,
                                            void **samples, DWORD *length,
                                            void **owned);
#define orig_read_wave (*(am2_read_wave_fn)ADDR_READ_WAVE_FILE)

/* Play a sound that is not one of the fixed ones -- 0x0040B8F0.
 *
 * The fixed table is loaded once at startup and played from; this is the other
 * half, for sounds named at the moment they are wanted -- the voice-overs under
 * `audio\vos`. Seventeen slots, and a slot holds whatever was last played
 * through it, so most of this function is deciding whether what is already
 * there can be reused.
 *
 * Reuse turns on the name. Same name, and the existing buffer is rewound and
 * played again. Different name, and the old one is stopped, released, freed and
 * the whole load sequence runs -- which is LoadWaveSound's body written out
 * again rather than called, exactly as the original has it.
 *
 * An empty name means stop: whatever is in the slot is silenced and nothing is
 * loaded.
 *
 * A sound already playing is protected twice over, by the priority stored on
 * the slot and by priority 3 specifically. Both comparisons are reproduced as
 * written; the first reads backwards -- it returns when the stored priority is
 * LOWER than the new one -- and that is what the original does.
 *
 * Position comes from the owning object when there is one, and its coordinates
 * are two separate words at +0x12 and +0x14, not the packed pair
 * Update3DAudioVolumes reads. Volume is the voice level for slots 0 and 16 and
 * the ordinary level otherwise, then the same 3-per-unit falloff and the same
 * 800-unit cutoff to silence.
 *
 * Exercisable: a Boot Camp mission plays voice-overs, which is how
 * FillSoundBuffer gets its one call in an otherwise fixed-table run. */
#define g_voiceVolume (*(const int32_t *)(uintptr_t)ADDR_VOLUME_VOICE)
#define g_dynamic     ((uint8_t **)(uintptr_t)ADDR_SOUND_DYNAMIC)

void __cdecl PlayDynamicSound(const char *name, int32_t loop, int32_t unused,
                              int32_t x, int32_t y, int32_t slot,
                              int32_t priority, uint32_t owner)
{
    uint8_t   *rec;
    AM2_Point  where;
    int32_t    dist = 0;
    int32_t    volume;
    DWORD      status;
    DWORD      looping;

    (void)unused;

    if (!g_audioEnabled)
        return;
    if (slot < 0 || slot > SOUND_DYNAMIC_MAX_INDEX)
        return;

    /* An owner overrides the caller's coordinates -- and stops being an owner
     * if it has gone away. */
    if (owner) {
        uint8_t *obj = (uint8_t *)orig_lookup_uid(owner);

        if (!obj) {
            owner = 0;
        } else {
            x = *(const int16_t *)(obj + 0x12);
            y = *(const int16_t *)(obj + 0x14);
        }
    }

    where.x = (int16_t)x;
    where.y = (int16_t)y;
    if (where.x != 0)
        dist = ApproxDist(&g_listenerPos, &where);

    rec = g_dynamic[slot];

    if (rec) {
        LPDIRECTSOUNDBUFFER buf = *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER);
        const char         *held;

        IDirectSoundBuffer_GetStatus(buf, &status);

        if (status & DSBSTATUS_PLAYING) {
            if (*name == '\0') {
                /* Asked to stop, and it is playing. */
                IDirectSoundBuffer_Stop(
                    *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
                *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
                return;
            }
            /* Reproduced as written, including the direction of the test. */
            if (*(int32_t *)(rec + SOUND_REC_OFF_PRIORITY) < priority)
                return;
            if (priority == 3)
                return;
        }

        held = *(const char **)(rec + SOUND_REC_OFF_NAME);
        if (held && strcmp(name, held) == 0) {
            /* The same sound again: keep the buffer, start it over. */
            if (*(void **)(rec + SOUND_REC_OFF_BUFFER)) {
                IDirectSoundBuffer_Stop(
                    *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
                *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
            }
            goto ready;
        }

        if (name && *name) {
            /* A different sound: the old one goes entirely. */
            if (*(void **)(rec + SOUND_REC_OFF_BUFFER)) {
                IDirectSoundBuffer_Stop(
                    *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
                *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
                IDirectSoundBuffer_Release(
                    *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
            }
            if (*(void **)(rec + SOUND_REC_OFF_NAME))
                orig_free(*(void **)(rec + SOUND_REC_OFF_NAME));
        } else {
            if (*(void **)(rec + SOUND_REC_OFF_BUFFER))
                IDirectSoundBuffer_Stop(
                    *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
            *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
            return;
        }
    } else {
        if (!name || *name == '\0')
            return;
        rec = (uint8_t *)orig_malloc(SOUND_RECORD_SIZE);
    }

    /* Probed and the answer discarded, as in InitWaveSounds. */
    orig_check_path((void *)(uintptr_t)ADDR_VOS_DIR);

    if (!rec) {
        orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOMEM_DATA);
        return;
    }

    {
        DSBUFFERDESC  desc;
        void         *raw = NULL;
        void         *owned = NULL;
        char         *copy;
        size_t        n;

        *(void **)(rec + SOUND_REC_OFF_BUFFER)   = NULL;
        *(void **)(rec + SOUND_REC_OFF_NAME)     = NULL;
        *(void **)(rec + SOUND_REC_OFF_OWNER_DS) = NULL;

        n = strlen(name) + 1;
        copy = (char *)orig_malloc(n);
        *(void **)(rec + SOUND_REC_OFF_NAME) = copy;
        if (!copy) {
            orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOMEM_NAME);
            goto drop_record;
        }
        memcpy(copy, name, n);

        memset(&desc, 0, sizeof desc);
        if (!orig_read_wave(0, name, &desc.lpwfxFormat, &raw,
                            &desc.dwBufferBytes, &owned)) {
            orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOLOAD, name);
            goto drop_name;
        }

        desc.dwSize  = sizeof desc;
        desc.dwFlags = DSBCAPS_STATIC | DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY
                     | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME
                     | DSBCAPS_GETCURRENTPOSITION2;

        if (IDirectSound_CreateSoundBuffer(
                g_dsound, &desc,
                (LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER),
                NULL) != DS_OK) {
            orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOBUFFER, name);
            goto drop_samples;
        }

        sound_dump(name, (const uint8_t *)raw, desc.dwBufferBytes);

        if (!FillSoundBuffer(
                *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER),
                (const uint8_t *)raw, desc.dwBufferBytes)) {
            orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOFILL, name);
            IDirectSoundBuffer_Release(
                *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
            goto drop_samples;
        }

        *(LPDIRECTSOUND *)(rec + SOUND_REC_OFF_OWNER_DS) = g_dsound;
        orig_free(owned);
        goto ready;

    drop_samples:
        orig_free(owned);
    drop_name:
        orig_free(*(void **)(rec + SOUND_REC_OFF_NAME));
    drop_record:
        orig_free(rec);
        g_dynamic[slot] = NULL;
        return;
    }

ready:
    /* Slots 0 and 16 are voices and have a level of their own. */
    volume = (slot == 0 || slot == SOUND_VOICE_SLOT_HI)
             ? g_voiceVolume : g_volumeAtZero;

    if (where.x != 0) {
        if (dist > SOUND_3D_CUTOFF)
            volume = DSBVOLUME_MIN;
        else
            volume -= dist * SOUND_3D_FALLOFF;
    }

    if (owner) {
        *(uint32_t *)(rec + SOUND_REC_OFF_OWNER) = owner;
    } else {
        ((AM2_Point *)(rec + SOUND_REC_OFF_POS))->x = (int16_t)x;
        ((AM2_Point *)(rec + SOUND_REC_OFF_POS))->y = (int16_t)y;
        *(uint32_t *)(rec + SOUND_REC_OFF_OWNER) = 0;
    }

    IDirectSoundBuffer_SetVolume(
        *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER), volume);

    IDirectSoundBuffer_GetStatus(
        *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER), &status);
    if (status & DSBSTATUS_PLAYING) {
        IDirectSoundBuffer_SetCurrentPosition(
            *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER), 0);
        *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 1;
    }

    looping = (loop == 1) ? DSBPLAY_LOOPING : 0;
    *(uint32_t *)(rec + SOUND_REC_OFF_LOOPING) = looping;
    IDirectSoundBuffer_Play(
        *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER), 0, 0, looping);

    g_dynamic[slot] = rec;
}

/* Play one of the fixed sounds, somewhere -- 0x0040C040, 165 call sites and
 * the busiest thing in this file.
 *
 * Three decisions, in order.
 *
 * HOW LOUD. A sound at the origin plays at full volume. One with a position is
 * attenuated by its distance from the listener, and then possibly by less: if
 * the owning object is around and visible, the distance from *it* is used
 * instead when that is shorter, or a flat near-value when the two points
 * coincide. Past 600 units the sound is not played at all -- this is a cull,
 * not a fade to silence.
 *
 * WHETHER TO RE-SET THE VOLUME. Only if the previous copy has finished, or if
 * this one is louder. Otherwise a distant instance would quietly take over a
 * near one that is still playing. "Finished" is computed rather than asked:
 * the buffer's length in bytes divided by 22, which is milliseconds at 22 kHz,
 * added to the tick it started. The 0xBA2E8BA3 multiply in the original is
 * that division.
 *
 * WHETHER TO RESTART IT. Only matters if it is already playing, and `flags`
 * decides: bit 0 restarts unless the buffer is within 0x1130 bytes of the end,
 * on the grounds that it is nearly done anyway; bit 1 refuses to interrupt at
 * all; neither rewinds unconditionally. Then Play, which for an already
 * playing buffer is harmless.
 *
 * Exercisable at last -- see tools/alsa/asoundrc. Before there was a sound
 * device this could not run, and neither could anything it touches. */
static_assert(DSBVOLUME_MIN == -10000, "DSBVOLUME_MIN");

/* Shared with Update3DAudioVolumes below: where the ear is, where a sound
 * with no place of its own is taken to be, and the volume at no distance. */

typedef int32_t (__cdecl *am2_points_equal_fn)(const AM2_Point *a,
                                               const AM2_Point *b);
typedef void *(__cdecl *am2_lookup_owner_fn)(uint32_t owner);
#define orig_points_equal (*(am2_points_equal_fn)ADDR_POINTS_EQUAL)
#define orig_lookup_owner (*(am2_lookup_owner_fn)ADDR_LOOKUP_OWNER_OBJ)
#define g_defaultOwner    (*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)

/* 22 bytes to the millisecond: the waves are 22 kHz, eight bit, mono. */
#define SOUND_BYTES_PER_MS   22
#define SOUND_PLAY_CUTOFF    0x258   /* 600 units; beyond this, do not play */
#define SOUND_NEAR_DISTANCE  0x20    /* used when owner and sound coincide */
#define SOUND_ALMOST_DONE    0x1130  /* bytes left below which a restart is
                                      * not worth it */
#define SOUND_FLAG_RESTART   1
#define SOUND_FLAG_NO_INTERRUPT 2

void __cdecl PlaySoundAt(int32_t index, int32_t flags, int32_t unused,
                         int32_t x, int32_t y)
{
    uint8_t             *slot;
    LPDIRECTSOUNDBUFFER  buf;
    DWORD                status;
    int32_t              volume;
    AM2_Point            where;

    (void)unused;

    if (!g_audioEnabled)
        return;
    if (index < 0 || index >= SOUND_FIXED_SLOTS)
        return;

    slot = (uint8_t *)(uintptr_t)ADDR_SOUND_SLOTS + index * SOUND_SLOT_STRIDE;
    if (!*(void **)slot)
        return;
    buf = *(LPDIRECTSOUNDBUFFER *)(*(uint8_t **)slot + SOUND_REC_OFF_BUFFER);
    if (!buf)
        return;

    IDirectSoundBuffer_GetStatus(buf, &status);

    where.x = (int16_t)x;
    where.y = (int16_t)y;

    if (where.x != 0) {
        int32_t  dist = ApproxDist(&g_listenerPos, &where);
        void    *owner = orig_lookup_owner(g_defaultOwner);

        /* An owner that can see gets a say, and only ever a shortening one. */
        if (owner && *(const int16_t *)((uint8_t *)owner + 0x62) > 0) {
            const AM2_Point *at = (const AM2_Point *)((uint8_t *)owner + OBJ_OFF_POS);
            int32_t          d2;

            if (orig_points_equal(&where, at))
                d2 = SOUND_NEAR_DISTANCE;
            else
                d2 = ApproxDist(at, &where) / 2;
            if (d2 < dist)
                dist = d2;
        }

        if (dist > SOUND_PLAY_CUTOFF)
            return;                       /* too far to bother with */
        volume = g_volumeAtZero - dist * SOUND_3D_FALLOFF;
    } else {
        volume = g_volumeAtZero;
    }

    if (volume < DSBVOLUME_MIN)
        volume = DSBVOLUME_MIN;

    /* Louder wins, and so does anything whose predecessor has finished. */
    if (!(status & DSBSTATUS_PLAYING) ||
        GetTickCount() > *(uint32_t *)(slot + SOUND_SLOT_OFF_STARTED)
                         + *(uint32_t *)(slot + SOUND_SLOT_OFF_BYTES)
                           / SOUND_BYTES_PER_MS ||
        volume > *(int32_t *)(slot + SOUND_SLOT_OFF_VOLUME)) {
        IDirectSoundBuffer_SetVolume(
            *(LPDIRECTSOUNDBUFFER *)(*(uint8_t **)slot + SOUND_REC_OFF_BUFFER),
            volume);
        *(int32_t *)(slot + SOUND_SLOT_OFF_VOLUME)  = volume;
        *(uint32_t *)(slot + SOUND_SLOT_OFF_STARTED) = GetTickCount();
    }

    if (status & DSBSTATUS_PLAYING) {
        buf = *(LPDIRECTSOUNDBUFFER *)(*(uint8_t **)slot + SOUND_REC_OFF_BUFFER);

        if (flags & SOUND_FLAG_RESTART) {
            DWORD played = 0;

            IDirectSoundBuffer_GetCurrentPosition(buf, &played, NULL);
            if (*(uint32_t *)(slot + SOUND_SLOT_OFF_BYTES) - played
                    < SOUND_ALMOST_DONE)
                goto play;                /* nearly over; let it finish */
            IDirectSoundBuffer_SetCurrentPosition(buf, 0);
        } else if (flags & SOUND_FLAG_NO_INTERRUPT) {
            return;
        } else {
            IDirectSoundBuffer_SetCurrentPosition(buf, 0);
        }
    }

play:
    IDirectSoundBuffer_Play(
        *(LPDIRECTSOUNDBUFFER *)(*(uint8_t **)slot + SOUND_REC_OFF_BUFFER),
        0, 0, 0);
}

/* Stop one dynamic sound, by slot and by name -- 0x0040B860.
 *
 * The slot says which sound and the name says which sound it had better be:
 * the caller is stopping a thing it started, and a slot is reused, so the name
 * is the check that it has not been handed to someone else in between. An empty
 * name skips the check and stops whatever is there.
 *
 * The second reason this was on the declined list -- "walks the table comparing
 * names byte by byte" -- is just MSVC inlining strcmp two characters at a time.
 * It compares, it does not search.
 *
 * The bound is inclusive, `> 0x10` rather than `>= 0x10`, so all seventeen
 * slots are reachable. Same count as FreeDynamicSounds and StopAllSounds.
 *
 * One asymmetry, kept: with a name, a slot holding no buffer still gets its
 * active flag cleared; with an empty name it does not. The two paths join at
 * different points in the original.
 *
 * Unexercised: no audio device. */
void __cdecl StopNamedSound(const char *name, int32_t index)
{
    uint8_t             *rec;
    LPDIRECTSOUNDBUFFER  buf;

    if (!g_audioEnabled)
        return;
    if (index < 0 || index > SOUND_DYNAMIC_MAX_INDEX)
        return;

    rec = ((uint8_t **)(uintptr_t)ADDR_SOUND_DYNAMIC)[index];
    if (!rec)
        return;

    buf = *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER);

    if (*name == '\0') {
        /* Stop whatever is there, and only touch the flag if there was one. */
        if (!buf)
            return;
        IDirectSoundBuffer_Stop(buf);
        *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
        return;
    }

    {
        const char *held = *(const char **)(rec + SOUND_REC_OFF_NAME);

        if (!held)
            return;
        if (strcmp(name, held) != 0)
            return;      /* the slot has been reused; not ours to stop */
    }

    if (buf)
        IDirectSoundBuffer_Stop(buf);
    *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
}

/* Follow the sounds around -- 0x0040BCF0.
 *
 * Once a frame, every playing sound in the dynamic table is asked where it is
 * and given a volume for it. Sixteen slots, from 1 rather than 0.
 *
 * Where a sound is comes from one of three places, in order: the object that
 * made it, if that object is still alive; the position stored on the sound
 * itself; or a default. An owner that has gone away is not a position at all --
 * the sound is stopped and the slot cleared, which is how a sound outliving its
 * source is cleaned up.
 *
 * The falloff is linear and blunt: silence past 800 units, and otherwise the
 * base volume less three per unit of distance. DirectSound volume is in
 * hundredths of a decibel and DSBVOLUME_MIN is -10000, which is the 0xFFFFD8F0
 * the original pushes.
 *
 * WHY THIS WAS DECLINED ONCE AND IS NOT NOW. It reads a position from
 * `[eax+0x12]` on one path and `[eax+0x10]` on another, which looked like two
 * overlapping fields of one record and therefore like guesswork. They are not
 * the same record: the lookup call in between reassigns eax, so +0x12 is the
 * game object's position and +0x10 is the sound's. Both are AM2_Point, and
 * ApproxDist -- already reconstructed -- takes exactly that.
 *
 * Unexercised: it returns at the first instruction while there is no audio. */
static_assert(DSBSTATUS_PLAYING == 1, "DSBSTATUS_PLAYING");
static_assert(DSBVOLUME_MIN == -10000, "DSBVOLUME_MIN");


void __cdecl Update3DAudioVolumes(void)
{
    uint8_t **slot;

    if (!g_audioEnabled)
        return;

    /* From the second slot, not the first -- 0x004FA3C4, not 0x004FA3C0. */
    for (slot = (uint8_t **)(uintptr_t)(ADDR_SOUND_DYNAMIC + 4);
         slot < (uint8_t **)(uintptr_t)ADDR_SOUND_DYNAMIC_LAST;
         slot++) {
        uint8_t             *rec = *slot;
        LPDIRECTSOUNDBUFFER  buf;
        DWORD                status;
        AM2_Point            where;
        uint32_t             owner;
        int32_t              dist;

        if (!rec)
            continue;
        buf = *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER);
        if (!buf)
            continue;

        IDirectSoundBuffer_GetStatus(buf, &status);
        if (!(status & DSBSTATUS_PLAYING)) {
            *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
            continue;
        }

        where = g_defaultPos;
        owner = *(uint32_t *)(rec + SOUND_REC_OFF_OWNER);

        if (owner) {
            void *obj = orig_lookup_uid(owner);

            if (!obj) {
                /* Whatever was making this noise is gone. */
                *(uint32_t *)(rec + SOUND_REC_OFF_OWNER) = 0;
                IDirectSoundBuffer_Stop(
                    *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
                *(uint32_t *)(rec + SOUND_REC_OFF_ACTIVE) = 0;
                continue;
            }
            where = *(const AM2_Point *)((uint8_t *)obj + OBJ_OFF_POS);
        } else if (((const AM2_Point *)(rec + SOUND_REC_OFF_POS))->x != 0) {
            where = *(const AM2_Point *)(rec + SOUND_REC_OFF_POS);
        }

        /* A zero x means nowhere, and nowhere gets left alone -- note this
         * path does NOT clear the active flag, unlike the two above. */
        if (where.x == 0)
            continue;

        dist = ApproxDist(&g_listenerPos, &where);
        if (dist > SOUND_3D_CUTOFF)
            IDirectSoundBuffer_SetVolume(buf, DSBVOLUME_MIN);
        else
            IDirectSoundBuffer_SetVolume(
                buf, g_volumeAtZero - dist * SOUND_3D_FALLOFF);
    }
}

/* Load one wave into a DirectSound buffer -- 0x0040C530.
 *
 * The middle of the chain: InitWaveSounds picks the names, this makes a buffer
 * and reads the file, and FillSoundBuffer writes the samples in. The slot the
 * caller passes does not hold the buffer -- it holds a 0x20-byte record, and
 * the buffer is that record's first field, which is why InitWaveSounds needs
 * two dereferences to reach it.
 *
 * The buffer is asked for with DSBCAPS_STATIC, 3D, frequency, pan, volume and
 * GETCURRENTPOSITION2 -- 0x100F2. Note that CTRL3D and CTRLPAN together is not
 * a combination DirectSound honours, a 3D buffer having no pan of its own;
 * asked for anyway, as written.
 *
 * Failure unwinds in stages and the stages differ, so they are kept apart
 * rather than merged: a name that will not allocate frees only the record, and
 * anything later frees the raw samples and the name too. The record is always
 * freed and the caller's slot always nulled before answering 0.
 *
 * Unexercised: no audio device, so CreateSoundBuffer never gets the chance to
 * fail or succeed. */
static_assert(sizeof(DSBUFFERDESC) == 0x14, "DSBUFFERDESC");
static_assert((DSBCAPS_STATIC | DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY
               | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME
               | DSBCAPS_GETCURRENTPOSITION2) == 0x100F2, "the buffer flags");


int32_t __cdecl LoadWaveSound(void **slot, LPDIRECTSOUND ds, const char *name)
{
    uint8_t      *rec;
    char         *nameCopy;
    void         *raw = NULL;      /* the samples, freed once uploaded */
    void         *owned = NULL;    /* the reader's own block, freed either way */
    DSBUFFERDESC  desc;
    size_t        n;

    rec = (uint8_t *)orig_malloc(SOUND_RECORD_SIZE);
    *slot = rec;
    if (!rec) {
        orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOMEM_DATA);
        return 0;
    }

    *(void **)(rec + SOUND_REC_OFF_BUFFER) = NULL;
    *(void **)(rec + SOUND_REC_OFF_NAME)   = NULL;
    *(void **)(rec + SOUND_REC_OFF_STATE)  = NULL;

    n = strlen(name) + 1;
    nameCopy = (char *)orig_malloc(n);
    *(void **)(rec + SOUND_REC_OFF_NAME) = nameCopy;
    if (!nameCopy) {
        orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOMEM_NAME);
        orig_free(rec);
        *slot = NULL;
        return 0;
    }
    memcpy(nameCopy, name, n);

    memset(&desc, 0, sizeof desc);

    /* Straight into the descriptor: the format and the length are the reader's
     * answers, not ours. */
    if (!orig_read_wave(0, name, &desc.lpwfxFormat, &raw, &desc.dwBufferBytes,
                        &owned)) {
        orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOLOAD, name);
        goto give_up;
    }

    desc.dwSize  = sizeof desc;
    desc.dwFlags = DSBCAPS_STATIC | DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY
                 | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME
                 | DSBCAPS_GETCURRENTPOSITION2;

    if (IDirectSound_CreateSoundBuffer(
            ds, &desc, (LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER),
            NULL) != DS_OK) {
        orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOBUFFER, name);
        goto give_up;
    }

    sound_dump(name, (const uint8_t *)raw, desc.dwBufferBytes);

    if (!FillSoundBuffer(*(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER),
                         (const uint8_t *)raw, desc.dwBufferBytes)) {
        orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_NOFILL, name);
        IDirectSoundBuffer_Release(
            *(LPDIRECTSOUNDBUFFER *)(rec + SOUND_REC_OFF_BUFFER));
        goto give_up;
    }

    *(void **)(rec + SOUND_REC_OFF_STATE) = NULL;
    orig_free(owned);
    return 1;

give_up:
    if (owned)
        orig_free(owned);
    if (*(void **)(rec + SOUND_REC_OFF_NAME))
        orig_free(*(void **)(rec + SOUND_REC_OFF_NAME));
    orig_free(rec);
    *slot = NULL;
    return 0;
}

/* Bring the fixed wave sounds up -- 0x0040C710.
 *
 * Thirty-two names in a table, each loaded into one of the sixteen-byte slots
 * at ADDR_SOUND_SLOTS. What makes it boundary code rather than a loop is the
 * second half of each iteration: having got a buffer, it asks DirectSound how
 * big that buffer actually is and records the answer beside it, because the
 * length the file claims and the length the buffer got need not agree.
 *
 * DSBCAPS is identified by its size: the original writes 0x14 into the
 * descriptor before the call, and reads back the field at +8, which is
 * dwBufferBytes. Slot 3 on IDirectSoundBuffer is GetCaps.
 *
 * A wave that fails to load leaves its slot null and says which index it was.
 * Nothing stops on it; the game runs with that sound silent. Always answers 1.
 *
 * Unexercised here -- with no audio device there is no IDirectSound to load
 * into, and the loader fails at the first step. */
static_assert(sizeof(DSBCAPS) == 0x14, "DSBCAPS");

#define g_waveNames    ((const char *const *)(uintptr_t)ADDR_WAVE_NAMES)
#define g_soundSlots   ((uint8_t *)(uintptr_t)ADDR_SOUND_SLOTS)
#define g_dsound       (*(LPDIRECTSOUND *)(uintptr_t)ADDR_DSOUND)

#define WAVE_COUNT \
    ((ADDR_WAVE_NAMES_END - ADDR_WAVE_NAMES) / sizeof(const char *))

int32_t __cdecl InitWaveSounds(void)
{
    int32_t i;

    /* The directory is probed and the answer thrown away, exactly as written. */
    orig_check_path(*(void **)(uintptr_t)ADDR_WAVE_DIR);

    for (i = 0; i < (int32_t)WAVE_COUNT; i++) {
        uint8_t *slot = g_soundSlots + i * SOUND_SLOT_STRIDE;
        DSBCAPS  caps;

        if (!LoadWaveSound((void **)slot, g_dsound, g_waveNames[i])) {
            orig_log((const char *)(uintptr_t)ADDR_STR_WAVE_INIT_FAIL, i);
            *(void **)slot = NULL;
            continue;
        }

        /* TWO dereferences. The slot holds a record, and the buffer is that
         * record's first field -- `mov eax,[esi]` then `mov eax,[eax]` in the
         * original. One dereference would call the record's first word as a
         * vtable, and with no audio device on this machine nothing would ever
         * have shown it. See the note in CLAUDE.md about this exact shape. */
        caps.dwSize = sizeof caps;
        IDirectSoundBuffer_GetCaps(
            *(LPDIRECTSOUNDBUFFER *)(*(uint8_t **)slot + SOUND_SLOT_OFF_BUFFER),
            &caps);
        *(uint32_t *)(slot + SOUND_SLOT_OFF_BYTES) = caps.dwBufferBytes;
    }
    return 1;
}

/* Let the dynamic sounds go -- 0x0040B800.
 *
 * The seventeen slots at ADDR_SOUND_DYNAMIC, which unlike the fixed ones above
 * are allocated per use and owned outright: the DirectSound buffer is
 * Released, the sample data freed, and the record itself freed. StopAllSounds
 * treats the two tables the same way and for the same reason.
 *
 * The bound is inclusive -- the original compares against the last slot's
 * address with `jle`, not one past it -- so all seventeen are covered rather
 * than sixteen. */
void __cdecl FreeDynamicSounds(void)
{
    uint8_t **slot;

    if (!g_audioEnabled)
        return;

    for (slot = (uint8_t **)(uintptr_t)ADDR_SOUND_DYNAMIC;
         slot <= (uint8_t **)(uintptr_t)ADDR_SOUND_DYNAMIC_LAST;
         slot++) {
        uint8_t *snd = *slot;

        if (!snd)
            continue;
        if (*(void **)(snd + SOUND_DYN_OFF_BUFFER))
            IDirectSoundBuffer_Release(
                *(LPDIRECTSOUNDBUFFER *)(snd + SOUND_DYN_OFF_BUFFER));
        if (*(void **)(snd + SOUND_DYN_OFF_DATA))
            orig_free(*(void **)(snd + SOUND_DYN_OFF_DATA));
        orig_free(snd);
        *slot = NULL;
    }
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
    rc |= patch_replace(ADDR_FILL_SOUND_BUFFER, (const void *)FillSoundBuffer,
                        "FillSoundBuffer", 3);
    rc |= patch_replace(ADDR_INIT_WAVE_SOUNDS, (const void *)InitWaveSounds,
                        "InitWaveSounds", 0);
    rc |= patch_replace(ADDR_LOAD_WAVE_SOUND, (const void *)LoadWaveSound,
                        "LoadWaveSound", 3);
    rc |= patch_replace(ADDR_FREE_DYN_SOUNDS, (const void *)FreeDynamicSounds,
                        "FreeDynamicSounds", 0);
    rc |= patch_replace(ADDR_UPDATE_3D_AUDIO, (const void *)Update3DAudioVolumes,
                        "Update3DAudioVolumes", 0);
    rc |= patch_replace(ADDR_STOP_NAMED_SOUND, (const void *)StopNamedSound,
                        "StopNamedSound", 2);
    rc |= patch_replace(ADDR_PLAY_SOUND_AT, (const void *)PlaySoundAt,
                        "PlaySoundAt", 5);
    rc |= patch_replace(ADDR_PLAY_DYNAMIC_SOUND, (const void *)PlayDynamicSound,
                        "PlayDynamicSound", 8);
    rc |= patch_replace(ADDR_SET_STREAM_VOLUME, (const void *)SetStreamVolume,
                        "SetStreamVolume", 1);
    rc |= patch_replace(ADDR_STOP_ALL_SOUNDS, (const void *)StopAllSounds,
                        "StopAllSounds", 0);
    rc |= patch_replace(ADDR_REFILL_AUDIO, (const void *)RefillAudioBuffer,
                        "RefillAudioBuffer", 0);
    rc |= patch_replace(ADDR_FREE_SOUND, (const void *)FreeSound, "FreeSound", 1);
    return rc;
}
