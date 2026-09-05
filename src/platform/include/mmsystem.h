/* mmsystem.h -- the winmm surface the game uses: multimedia file I/O for the
 * wave loader, the multimedia timer for the audio stream, and the wave
 * format structures. See windows.h for what this header is and is not. */
#ifndef AM2_PLATFORM_MMSYSTEM_H
#define AM2_PLATFORM_MMSYSTEM_H

#include <windows.h>

AM2_EXTERN_C_BEGIN

typedef DWORD FOURCC;
DECLARE_HANDLE(HMMIO);
typedef UINT MMVERSION;

#define mmioFOURCC(c0, c1, c2, c3) \
    ((FOURCC)(BYTE)(c0) | ((FOURCC)(BYTE)(c1) << 8) | \
     ((FOURCC)(BYTE)(c2) << 16) | ((FOURCC)(BYTE)(c3) << 24))
#define FOURCC_RIFF mmioFOURCC('R', 'I', 'F', 'F')
#define FOURCC_LIST mmioFOURCC('L', 'I', 'S', 'T')

#define MMSYSERR_NOERROR    0
#define MMSYSERR_ERROR      1
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_INVALHANDLE 5
#define MMSYSERR_NOMEM      7
#define MMSYSERR_INVALPARAM 11
#define MMIOERR_BASE        256
#define MMIOERR_FILENOTFOUND (MMIOERR_BASE + 1)
#define MMIOERR_OUTOFMEMORY  (MMIOERR_BASE + 2)
#define MMIOERR_CANNOTOPEN   (MMIOERR_BASE + 3)
#define MMIOERR_CANNOTCLOSE  (MMIOERR_BASE + 4)
#define MMIOERR_CANNOTREAD   (MMIOERR_BASE + 5)
#define MMIOERR_CANNOTWRITE  (MMIOERR_BASE + 6)
#define MMIOERR_CANNOTSEEK   (MMIOERR_BASE + 7)
#define MMIOERR_CANNOTEXPAND (MMIOERR_BASE + 8)
#define MMIOERR_CHUNKNOTFOUND (MMIOERR_BASE + 9)
#define MMIOERR_UNBUFFERED   (MMIOERR_BASE + 10)

#define MMIO_READ       0x00000000
#define MMIO_WRITE      0x00000001
#define MMIO_READWRITE  0x00000002
#define MMIO_ALLOCBUF   0x00010000
#define MMIO_FINDCHUNK  0x0010
#define MMIO_FINDRIFF   0x0020
#define MMIO_FINDLIST   0x0040
#define MMIO_CREATERIFF 0x0020
#define MMIO_CREATELIST 0x0040
#define MMIO_DEFAULTBUFFER 8192

typedef struct _MMCKINFO {
    FOURCC ckid;
    DWORD  cksize;
    FOURCC fccType;
    DWORD  dwDataOffset;
    DWORD  dwFlags;
} MMCKINFO, *LPMMCKINFO;

typedef struct _MMIOINFO {
    DWORD   dwFlags;
    FOURCC  fccIOProc;
    LPVOID  pIOProc;
    UINT    wErrorRet;
    HANDLE  htask;
    LONG    cchBuffer;
    HPSTR   pchBuffer;
    HPSTR   pchNext;
    HPSTR   pchEndRead;
    HPSTR   pchEndWrite;
    LONG    lBufOffset;
    LONG    lDiskOffset;
    DWORD   adwInfo[3];
    DWORD   dwReserved1;
    DWORD   dwReserved2;
    HMMIO   hmmio;
} MMIOINFO, *LPMMIOINFO;

HMMIO    WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags);
MMRESULT WINAPI mmioClose(HMMIO h, UINT flags);
LONG     WINAPI mmioRead(HMMIO h, HPSTR out, LONG n);
LONG     WINAPI mmioSeek(HMMIO h, LONG off, int32_t whence);
MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent,
                            UINT flags);
MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT flags);
MMRESULT WINAPI mmioGetInfo(HMMIO h, LPMMIOINFO info, UINT flags);
MMRESULT WINAPI mmioSetInfo(HMMIO h, const MMIOINFO *info, UINT flags);
MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT flags);

/* ---- wave formats --------------------------------------------------------- */

#define WAVE_FORMAT_PCM 1

#pragma pack(push, 1)
typedef struct waveformat_tag {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
} WAVEFORMAT;

typedef struct pcmwaveformat_tag {
    WAVEFORMAT wf;
    WORD       wBitsPerSample;
} PCMWAVEFORMAT;

typedef struct tWAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;
#pragma pack(pop)

/* ---- multimedia timers ---------------------------------------------------- */

typedef void (CALLBACK *LPTIMECALLBACK)(UINT id, UINT msg, DWORD_PTR user,
                                        DWORD_PTR dw1, DWORD_PTR dw2);
#define TIME_ONESHOT  0x0000
#define TIME_PERIODIC 0x0001
#define TIMERR_NOERROR 0
#define TIMERR_NOCANDO 97

MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK cb,
                             DWORD_PTR user, UINT flags);
MMRESULT WINAPI timeKillEvent(UINT id);
MMRESULT WINAPI timeBeginPeriod(UINT period);
MMRESULT WINAPI timeEndPeriod(UINT period);
DWORD    WINAPI timeGetTime(void);

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_MMSYSTEM_H */
