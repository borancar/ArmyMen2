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

int audio_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_AUDIO_H */
