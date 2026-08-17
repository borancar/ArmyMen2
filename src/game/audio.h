#ifndef AM2_AUDIO_H
#define AM2_AUDIO_H

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Streaming audio -- the music player.
 *
 * A DirectSound buffer is played on a loop while a multimedia timer refills it
 * from a .WAV on disk, which is what makes this the last piece of the boundary
 * that is genuinely about talking to the outside world rather than about game
 * state: WINMM's timer, DirectSound's buffer and src/game/wavefile.cpp's reader
 * all meet here.
 *
 * Both functions begin by checking whether streaming is on and whether there is
 * a buffer to stream into, and on this machine neither is ever true -- there is
 * no audio device for DirectSound to open. They run and correctly do nothing.
 * See CLAUDE.md; the deeper paths need a machine with sound.
 */

/* Original: 0x0040D5D0, 6 call sites. Stop the stream and let it go.
 *
 * Kills the refill timer, gives back the timer resolution it asked for, waits
 * for any refill already in progress to finish, then stops the buffer, closes
 * the .WAV and releases the buffer.
 *
 * The wait is a spin on InterlockedExchange with a 300ms Sleep between tries --
 * the timer callback sets a flag while it is inside, and this clears it and
 * keeps clearing until it finds it already clear. Crude, but it is the only
 * synchronisation between the two. */
void __cdecl StopAudioStream(void);

/* Original: 0x0040D680, 3 call sites. Start streaming a track.
 *
 * Stops whatever was playing first, then plays the buffer looping and puts a
 * multimedia timer on it to keep it fed. If the timer resolution cannot be had
 * the buffer is stopped again rather than left playing a loop nobody refills. */
void __cdecl StartAudioStream(void *track, int32_t which);

/* Original: 0x0040C7D0, 8 call sites. Release three DirectSound buffers.
 *
 * They are released and not nulled, so calling this twice would release them
 * twice. That is what the original does; every call site drops them
 * immediately afterwards. */
void __cdecl ReleaseSoundBuffers(void);

/* Original: 0x0040C800, 1 call site. Bring DirectSound up.
 *
 * Creates the object, takes priority cooperative level, makes a primary buffer
 * with 3D control, queries the 3D listener off it and gives that its starting
 * position, doppler and rolloff before committing. Returns 1 on success; every
 * failure names itself in the log, releases what it had and answers 0.
 *
 * On this machine it fails at the first step, because there is no device. */
int32_t __cdecl InitDirectSound(void);

/* Original: 0x0040CE90, 3 call sites. Set the stream's volume and pan.
 *
 * Volume comes from a global and pan from the caller, and the pan is skipped if
 * the volume did not take. */
void __cdecl SetStreamVolume(int32_t pan);

/* Original: 0x0040D730. Silence everything and let the dynamic sounds go.
 *
 * Two tables, treated differently. The 56 fixed slots are only stopped -- they
 * are owned elsewhere and will be wanted again. The 17 dynamic ones are stopped,
 * released and freed, because they were allocated for one use. The music stream
 * goes last, through StopAudioStream. */
void __cdecl StopAllSounds(void);

/* Original: 0x0040C6E0. Release one sound's buffer and free it.
 *
 * Both the sample data and the record itself come from the game's heap and go
 * back to it, not ours. */
void __cdecl FreeSound(void *snd);

/* Original: 0x0040C440, 2 call sites. Write sample data into a DirectSound
 * buffer through the Lock/Unlock bracket.
 *
 * A DirectSound buffer is circular, so a lock can return two regions when the
 * request wraps; both are filled. The amount written is whatever the lock
 * reports, not what the caller asked for. Returns 1 on success, 0 with a
 * message if an argument is missing or the lock fails. */
int32_t __cdecl FillSoundBuffer(LPDIRECTSOUNDBUFFER buf, const uint8_t *data,
                                uint32_t size);

/* Original: 0x0040C710. Load the 32 fixed wave sounds and record how big
 * DirectSound actually made each buffer. Always answers 1; a wave that fails
 * leaves its slot null and names its index in the log.
 *
 * Original: 0x0040B800. Release, free and clear the 17 dynamic sound slots,
 * which unlike the fixed ones are owned outright. */
/* Original: 0x0040C530. Load one wave: allocate a record, copy the name, read
 * the file, create the DirectSound buffer and fill it. `slot` receives the
 * record, whose first field is the buffer -- not the buffer itself. */
int32_t __cdecl LoadWaveSound(void **slot, LPDIRECTSOUND ds, const char *name);

/* Original: 0x0040BCF0. Once a frame, give every playing dynamic sound a
 * volume for where it is: silence past 800 units, otherwise the base volume
 * less three per unit. A sound whose owner has gone is stopped instead. */
void __cdecl Update3DAudioVolumes(void);

int32_t __cdecl InitWaveSounds(void);
void __cdecl FreeDynamicSounds(void);

int audio_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_AUDIO_H */
