/* .WAV file reading -- reconstructed from ArmyMen2.exe.
 *
 *   WaveOpenFile       0x0040CA10   1 call site
 *   WaveStartDataRead  0x0040CBB0   4 call sites
 *   WaveCloseReadFile  0x0040CCE0   2 call sites
 *
 * Eleven import sites and no game logic whatsoever -- the densest remaining
 * corner of the boundary after the palette. Everything here is WINMM's
 * multimedia file services plus a pair of GlobalAlloc/GlobalFree, and it is the
 * only file I/O in the game that does not go through the CRT.
 *
 * See wavefile.h on the naming. These are the DirectX SDK's own wavefile
 * helpers, copied into the game as most 1999 titles copied them, and the SDK's
 * names are the honest ones to use.
 *
 * The one thing worth reading twice is the allocation size. A PCM file needs
 * exactly a WAVEFORMATEX; anything else carries a cbSize count of extra format
 * bytes after it, read from the file before the allocation is made. So the
 * size is sizeof(WAVEFORMATEX) + cbSize, and the extra bytes are read straight
 * in after the fixed part.
 */

#include "wavefile.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

/* The original hardcodes these; check the SDK agrees rather than assume it. */
static_assert((MMIO_ALLOCBUF | MMIO_READ) == 0x10000, "mmioOpen flags");
static_assert(MMIO_FINDCHUNK == 0x0010, "MMIO_FINDCHUNK");
static_assert(WAVE_FORMAT_PCM == 1, "WAVE_FORMAT_PCM");
static_assert(GMEM_FIXED == 0, "GMEM_FIXED");
static_assert(sizeof(PCMWAVEFORMAT) == 0x10, "PCMWAVEFORMAT");
static_assert(sizeof(WAVEFORMATEX) == 0x12, "WAVEFORMATEX");

/* mmioFOURCC is a macro over char literals; the original has the assembled
 * dwords. Same thing, checked both ways. */
static_assert(FOURCC_RIFF == 0x46464952, "'RIFF'");
static_assert(mmioFOURCC('W', 'A', 'V', 'E') == 0x45564157, "'WAVE'");
static_assert(mmioFOURCC('f', 'm', 't', ' ') == 0x20746D66, "'fmt '");
static_assert(mmioFOURCC('d', 'a', 't', 'a') == 0x61746164, "'data'");

MMRESULT __cdecl WaveOpenFile(char *fileName, HMMIO *phmmioIn,
                              WAVEFORMATEX **ppwfxInfo, MMCKINFO *pckInRIFF)
{
    PCMWAVEFORMAT pcmWaveFormat;
    MMCKINFO      ckIn;
    HMMIO         hmmio;
    MMRESULT      mmr;
    WORD          cbExtraAlloc;

    *ppwfxInfo = NULL;

    hmmio = mmioOpenA(fileName, NULL, MMIO_ALLOCBUF | MMIO_READ);
    if (!hmmio) {
        /* Nothing was opened and nothing allocated, but the cleanup below
         * copes with that, and this is the path the original takes. */
        mmr = (MMRESULT)-1;
        goto cleanup;
    }

    mmr = mmioDescend(hmmio, pckInRIFF, NULL, 0);
    if (mmr != MMSYSERR_NOERROR)
        goto cleanup;
    if (pckInRIFF->ckid != FOURCC_RIFF ||
        pckInRIFF->fccType != mmioFOURCC('W', 'A', 'V', 'E')) {
        mmr = (MMRESULT)-1;
        goto cleanup;
    }

    ckIn.ckid = mmioFOURCC('f', 'm', 't', ' ');
    mmr = mmioDescend(hmmio, &ckIn, pckInRIFF, MMIO_FINDCHUNK);
    if (mmr != MMSYSERR_NOERROR)
        goto cleanup;
    if (ckIn.cksize < sizeof pcmWaveFormat) {
        mmr = (MMRESULT)-1;
        goto cleanup;
    }

    if (mmioRead(hmmio, (HPSTR)&pcmWaveFormat, sizeof pcmWaveFormat) !=
        (LONG)sizeof pcmWaveFormat) {
        mmr = (MMRESULT)-1;
        goto cleanup;
    }

    /* PCM carries no extra format bytes and no count for them either, so the
     * count is only in the file for everything else. */
    if (pcmWaveFormat.wf.wFormatTag == WAVE_FORMAT_PCM) {
        cbExtraAlloc = 0;
    } else {
        if (mmioRead(hmmio, (HPSTR)&cbExtraAlloc, sizeof cbExtraAlloc) !=
            (LONG)sizeof cbExtraAlloc) {
            mmr = (MMRESULT)-1;
            goto cleanup;
        }
    }

    *ppwfxInfo = (WAVEFORMATEX *)GlobalAlloc(GMEM_FIXED,
                                             sizeof(WAVEFORMATEX) + cbExtraAlloc);
    if (!*ppwfxInfo) {
        mmr = (MMRESULT)-1;
        goto cleanup;
    }

    memcpy(*ppwfxInfo, &pcmWaveFormat, sizeof pcmWaveFormat);
    (*ppwfxInfo)->cbSize = cbExtraAlloc;

    if (cbExtraAlloc != 0) {
        if (mmioRead(hmmio, (HPSTR)((BYTE *)*ppwfxInfo + sizeof(WAVEFORMATEX)),
                     cbExtraAlloc) != (LONG)cbExtraAlloc) {
            mmr = (MMRESULT)-1;
            goto cleanup;
        }
    }

    /* Back out of the `fmt ` chunk so the caller starts from the RIFF again. */
    mmr = mmioAscend(hmmio, &ckIn, 0);
    if (mmr != MMSYSERR_NOERROR)
        goto cleanup;

    *phmmioIn = hmmio;
    return MMSYSERR_NOERROR;

cleanup:
    /* Undo whatever got as far as existing, in the reverse order. */
    if (*ppwfxInfo) {
        GlobalFree(*ppwfxInfo);
        *ppwfxInfo = NULL;
    }
    if (hmmio)
        mmioClose(hmmio, 0);
    *phmmioIn = NULL;
    return mmr;
}

MMRESULT __cdecl WaveStartDataRead(HMMIO *phmmioIn, MMCKINFO *pckIn,
                                   MMCKINFO *pckInRIFF)
{
    /* Past the RIFF header's four-byte form type, which mmioDescend counted as
     * part of the chunk but not as part of the data. */
    mmioSeek(*phmmioIn, pckInRIFF->dwDataOffset + 4, SEEK_SET);

    pckIn->ckid = mmioFOURCC('d', 'a', 't', 'a');
    return mmioDescend(*phmmioIn, pckIn, pckInRIFF, MMIO_FINDCHUNK);
}

MMRESULT __cdecl WaveCloseReadFile(HMMIO *phmmio, WAVEFORMATEX **ppwfxSrc)
{
    if (*ppwfxSrc) {
        GlobalFree(*ppwfxSrc);
        *ppwfxSrc = NULL;
    }
    if (*phmmio) {
        mmioClose(*phmmio, 0);
        *phmmio = NULL;
    }
    return MMSYSERR_NOERROR;
}

int wavefile_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_WAVE_OPEN_FILE, (const void *)WaveOpenFile,
                        "WaveOpenFile", 4);
    rc |= patch_replace(ADDR_WAVE_START_DATA, (const void *)WaveStartDataRead,
                        "WaveStartDataRead", 3);
    rc |= patch_replace(ADDR_WAVE_CLOSE_FILE, (const void *)WaveCloseReadFile,
                        "WaveCloseReadFile", 2);
    return rc;
}
