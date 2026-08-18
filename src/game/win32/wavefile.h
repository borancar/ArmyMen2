#ifndef AM2_WAVEFILE_H
#define AM2_WAVEFILE_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Reading .WAV files, through the multimedia file services in WINMM.
 *
 * This is the only place the game opens a file with anything other than the
 * CRT, and the whole of its RIFF parsing. What comes out feeds DirectSound;
 * what goes in is a filename.
 *
 * The three functions are recognisably the wavefile helpers from the DirectX
 * SDK's own sample code, which 1999 games copied wholesale, so they are named
 * as that sample names them. Nothing about them is Army Men II specific --
 * which is exactly why they are worth having: they are pure Win32 boundary and
 * contain no game logic at all.
 */

/* Original: 0x0040CA10. Open a .WAV and read its format chunk.
 *
 * Descends into the RIFF, checks it really is a WAVE, finds the `fmt ` chunk
 * and allocates a WAVEFORMATEX to hold it -- plus however many extra bytes a
 * non-PCM format asks for, which is why the size is not a constant. Leaves the
 * file open and positioned, with the handle in *phmmioIn.
 *
 * Returns 0 on success. On failure it frees and closes everything it had got
 * as far as, so there is nothing for the caller to undo. */
MMRESULT __cdecl WaveOpenFile(char *fileName, HMMIO *phmmioIn,
                              WAVEFORMATEX **ppwfxInfo, MMCKINFO *pckInRIFF);

/* Original: 0x0040CBB0. Seek to the `data` chunk and descend into it, leaving
 * the file ready for mmioRead. 4 call sites. */
MMRESULT __cdecl WaveStartDataRead(HMMIO *phmmioIn, MMCKINFO *pckIn,
                                   MMCKINFO *pckInRIFF);

/* Original: 0x0040CBF0. Read up to `cbRead` bytes of the current chunk. 6 call
 * sites -- the most-used of the four.
 *
 * Reads out of mmio's own buffer rather than through mmioRead, calling
 * mmioAdvance to refill it whenever it runs dry, which is what makes it worth
 * having: one buffer and no second copy. Never reads past the end of the chunk
 * and subtracts whatever it takes from `pckIn->cksize`, so repeated calls walk
 * the chunk to its end.
 *
 * Returns 0 with the byte count in *cbActualRead. A short file -- advanced and
 * still empty -- answers -1 rather than an MMRESULT, since nothing failed. */
MMRESULT __cdecl WaveReadFile(HMMIO hmmio, uint32_t cbRead, uint8_t *pbDest,
                              MMCKINFO *pckIn, uint32_t *cbActualRead);

/* Original: 0x0040CCE0. Release both halves of what WaveOpenFile produced and
 * null the caller's pointers. Safe on either being already null; always
 * returns 0. */
MMRESULT __cdecl WaveCloseReadFile(HMMIO *phmmio, WAVEFORMATEX **ppwfxSrc);

int wavefile_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_WAVEFILE_H */
