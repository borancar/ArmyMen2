/* dsound.cpp -- DirectSound and winmm.
 *
 * DEFERRED: there is no sound device yet. DirectSoundCreate answers
 * DSERR_NODRIVER, which is what the original sees on a machine without
 * one, and every audio function in the game returns at its first line --
 * CLAUDE.md records that path being exercised on a host with no audio
 * server. The wave loader's mmio calls are declared so wavefile.cpp
 * links; with no device nothing calls them. The multimedia timer is only
 * ever set by the audio stream, for the same reason.
 */
#include "platform.h"
#include <dsound.h>

AM2_DEFINE_GUID_VALUE(IID_IDirectSound,           0x279AFA83, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectSoundBuffer,     0x279AFA85, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectSound3DListener, 0x279AFA84, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectSound3DBuffer,   0x279AFA86, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);

HRESULT WINAPI DirectSoundCreate(LPCGUID guid, LPDIRECTSOUND *out, IUnknown *outer)
{
    (void)guid; (void)outer;
    if (out)
        *out = NULL;
    am2_plat_log("DirectSound is not implemented yet; the game runs silent");
    return DSERR_NODRIVER;
}

/* ---- winmm: multimedia file I/O ------------------------------------------------ */

HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags)
{
    (void)name; (void)flags;
    if (info)
        info->wErrorRet = MMIOERR_CANNOTOPEN;
    return NULL;
}

MMRESULT WINAPI mmioClose(HMMIO h, UINT flags) { (void)h; (void)flags; return MMSYSERR_INVALHANDLE; }
LONG     WINAPI mmioRead(HMMIO h, HPSTR out, LONG n) { (void)h; (void)out; (void)n; return -1; }
LONG     WINAPI mmioSeek(HMMIO h, LONG off, int32_t whence) { (void)h; (void)off; (void)whence; return -1; }

MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent, UINT flags)
{
    (void)h; (void)ck; (void)parent; (void)flags;
    return MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT flags)
{
    (void)h; (void)ck; (void)flags;
    return MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI mmioGetInfo(HMMIO h, LPMMIOINFO info, UINT flags)
{
    (void)h; (void)info; (void)flags;
    return MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI mmioSetInfo(HMMIO h, const MMIOINFO *info, UINT flags)
{
    (void)h; (void)info; (void)flags;
    return MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT flags)
{
    (void)h; (void)info; (void)flags;
    return MMSYSERR_INVALHANDLE;
}

/* ---- winmm: timers --------------------------------------------------------------- */

MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK cb,
                             DWORD_PTR user, UINT flags)
{
    (void)delay; (void)resolution; (void)cb; (void)user; (void)flags;
    return 0;
}

MMRESULT WINAPI timeKillEvent(UINT id) { (void)id; return TIMERR_NOERROR; }
MMRESULT WINAPI timeBeginPeriod(UINT period) { (void)period; return TIMERR_NOERROR; }
MMRESULT WINAPI timeEndPeriod(UINT period) { (void)period; return TIMERR_NOERROR; }
