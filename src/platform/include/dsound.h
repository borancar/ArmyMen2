/* dsound.h -- DirectSound 3 as the game sees it: the device, a buffer and
 * the 3D listener. Declared in full so audio.cpp compiles; whether the
 * platform layer provides a device is DirectSoundCreate's answer. */
#ifndef AM2_PLATFORM_DSOUND_H
#define AM2_PLATFORM_DSOUND_H

#include <windows.h>

AM2_EXTERN_C_BEGIN

typedef struct IDirectSound           IDirectSound,           *LPDIRECTSOUND;
typedef struct IDirectSoundBuffer     IDirectSoundBuffer,     *LPDIRECTSOUNDBUFFER;
typedef struct IDirectSound3DListener IDirectSound3DListener, *LPDIRECTSOUND3DLISTENER;
typedef struct IDirectSound3DBuffer   IDirectSound3DBuffer,   *LPDIRECTSOUND3DBUFFER;

extern const GUID IID_IDirectSound;
extern const GUID IID_IDirectSoundBuffer;
extern const GUID IID_IDirectSound3DListener;
extern const GUID IID_IDirectSound3DBuffer;

#define _FACDS 0x878
#define MAKE_DSHRESULT(code) MAKE_HRESULT(1, _FACDS, code)
#define DS_OK                 S_OK
#define DSERR_ALLOCATED       MAKE_DSHRESULT(10)
#define DSERR_INVALIDPARAM    E_INVALIDARG
#define DSERR_INVALIDCALL     MAKE_DSHRESULT(50)
#define DSERR_GENERIC         E_FAIL
#define DSERR_OUTOFMEMORY     E_OUTOFMEMORY
#define DSERR_UNSUPPORTED     E_NOTIMPL
#define DSERR_NODRIVER        MAKE_DSHRESULT(120)
#define DSERR_BUFFERLOST      MAKE_DSHRESULT(150)
#define DSERR_UNINITIALIZED   MAKE_DSHRESULT(170)
#define DSERR_NOINTERFACE     E_NOINTERFACE

#define DSSCL_NORMAL       1
#define DSSCL_PRIORITY     2
#define DSSCL_EXCLUSIVE    3
#define DSSCL_WRITEPRIMARY 4

#define DSBCAPS_PRIMARYBUFFER        0x00000001
#define DSBCAPS_STATIC               0x00000002
#define DSBCAPS_LOCHARDWARE          0x00000004
#define DSBCAPS_LOCSOFTWARE          0x00000008
#define DSBCAPS_CTRL3D               0x00000010
#define DSBCAPS_CTRLFREQUENCY        0x00000020
#define DSBCAPS_CTRLPAN              0x00000040
#define DSBCAPS_CTRLVOLUME           0x00000080
#define DSBCAPS_CTRLDEFAULT          0x000000E0
#define DSBCAPS_CTRLALL              0x000000F0
#define DSBCAPS_STICKYFOCUS          0x00004000
#define DSBCAPS_GLOBALFOCUS          0x00008000
#define DSBCAPS_GETCURRENTPOSITION2  0x00010000
#define DSBCAPS_MUTE3DATMAXDISTANCE  0x00020000

#define DSBPLAY_LOOPING       0x00000001
#define DSBSTATUS_PLAYING     0x00000001
#define DSBSTATUS_BUFFERLOST  0x00000002
#define DSBSTATUS_LOOPING     0x00000004
#define DSBLOCK_FROMWRITECURSOR 0x00000001
#define DSBLOCK_ENTIREBUFFER  0x00000002
#define DSBVOLUME_MIN   (-10000)
#define DSBVOLUME_MAX   0
#define DSBPAN_LEFT     (-10000)
#define DSBPAN_CENTER   0
#define DSBPAN_RIGHT    10000
#define DS3D_IMMEDIATE  0
#define DS3D_DEFERRED   1

typedef struct _DSCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD rest[22];
} DSCAPS, *LPDSCAPS;

typedef struct _DSBCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwUnlockTransferRate;
    DWORD dwPlayCpuOverhead;
} DSBCAPS, *LPDSBCAPS;

typedef struct _DSBUFFERDESC {
    DWORD          dwSize;
    DWORD          dwFlags;
    DWORD          dwBufferBytes;
    DWORD          dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
} DSBUFFERDESC, *LPDSBUFFERDESC;
typedef const DSBUFFERDESC *LPCDSBUFFERDESC;

typedef struct _D3DVECTOR { float x, y, z; } D3DVECTOR;
typedef float D3DVALUE;

typedef struct _DS3DLISTENER {
    DWORD     dwSize;
    D3DVECTOR vPosition;
    D3DVECTOR vVelocity;
    D3DVECTOR vOrientFront;
    D3DVECTOR vOrientTop;
    D3DVALUE  flDistanceFactor;
    D3DVALUE  flRolloffFactor;
    D3DVALUE  flDopplerFactor;
} DS3DLISTENER, *LPDS3DLISTENER;

typedef struct IDirectSoundVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectSound *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectSound *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectSound *);
    HRESULT (STDMETHODCALLTYPE *CreateSoundBuffer)(IDirectSound *, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(IDirectSound *, LPDSCAPS);
    HRESULT (STDMETHODCALLTYPE *DuplicateSoundBuffer)(IDirectSound *, LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *);
    HRESULT (STDMETHODCALLTYPE *SetCooperativeLevel)(IDirectSound *, HWND, DWORD);
    HRESULT (STDMETHODCALLTYPE *Compact)(IDirectSound *);
    HRESULT (STDMETHODCALLTYPE *GetSpeakerConfig)(IDirectSound *, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *SetSpeakerConfig)(IDirectSound *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectSound *, LPCGUID);
} IDirectSoundVtbl;
struct IDirectSound { const IDirectSoundVtbl *lpVtbl; };

#define IDirectSound_QueryInterface(p, a, b)      ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectSound_AddRef(p)                    ((p)->lpVtbl->AddRef(p))
#define IDirectSound_Release(p)                   ((p)->lpVtbl->Release(p))
#define IDirectSound_CreateSoundBuffer(p, a, b, c) ((p)->lpVtbl->CreateSoundBuffer(p, a, b, c))
#define IDirectSound_GetCaps(p, a)                ((p)->lpVtbl->GetCaps(p, a))
#define IDirectSound_SetCooperativeLevel(p, a, b) ((p)->lpVtbl->SetCooperativeLevel(p, a, b))

typedef struct IDirectSoundBufferVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectSoundBuffer *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectSoundBuffer *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectSoundBuffer *);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(IDirectSoundBuffer *, LPDSBCAPS);
    HRESULT (STDMETHODCALLTYPE *GetCurrentPosition)(IDirectSoundBuffer *, LPDWORD, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetFormat)(IDirectSoundBuffer *, LPWAVEFORMATEX, DWORD, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetVolume)(IDirectSoundBuffer *, LPLONG);
    HRESULT (STDMETHODCALLTYPE *GetPan)(IDirectSoundBuffer *, LPLONG);
    HRESULT (STDMETHODCALLTYPE *GetFrequency)(IDirectSoundBuffer *, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetStatus)(IDirectSoundBuffer *, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectSoundBuffer *, LPDIRECTSOUND, LPCDSBUFFERDESC);
    HRESULT (STDMETHODCALLTYPE *Lock)(IDirectSoundBuffer *, DWORD, DWORD, LPVOID *, LPDWORD, LPVOID *, LPDWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *Play)(IDirectSoundBuffer *, DWORD, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetCurrentPosition)(IDirectSoundBuffer *, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetFormat)(IDirectSoundBuffer *, LPCWAVEFORMATEX);
    HRESULT (STDMETHODCALLTYPE *SetVolume)(IDirectSoundBuffer *, LONG);
    HRESULT (STDMETHODCALLTYPE *SetPan)(IDirectSoundBuffer *, LONG);
    HRESULT (STDMETHODCALLTYPE *SetFrequency)(IDirectSoundBuffer *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Stop)(IDirectSoundBuffer *);
    HRESULT (STDMETHODCALLTYPE *Unlock)(IDirectSoundBuffer *, LPVOID, DWORD, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *Restore)(IDirectSoundBuffer *);
} IDirectSoundBufferVtbl;
struct IDirectSoundBuffer { const IDirectSoundBufferVtbl *lpVtbl; };

#define IDirectSoundBuffer_QueryInterface(p, a, b)  ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectSoundBuffer_AddRef(p)                ((p)->lpVtbl->AddRef(p))
#define IDirectSoundBuffer_Release(p)               ((p)->lpVtbl->Release(p))
#define IDirectSoundBuffer_GetCaps(p, a)            ((p)->lpVtbl->GetCaps(p, a))
#define IDirectSoundBuffer_GetCurrentPosition(p, a, b) ((p)->lpVtbl->GetCurrentPosition(p, a, b))
#define IDirectSoundBuffer_GetFormat(p, a, b, c)    ((p)->lpVtbl->GetFormat(p, a, b, c))
#define IDirectSoundBuffer_GetStatus(p, a)          ((p)->lpVtbl->GetStatus(p, a))
#define IDirectSoundBuffer_Lock(p, a, b, c, d, e, f, g) ((p)->lpVtbl->Lock(p, a, b, c, d, e, f, g))
#define IDirectSoundBuffer_Play(p, a, b, c)         ((p)->lpVtbl->Play(p, a, b, c))
#define IDirectSoundBuffer_SetCurrentPosition(p, a) ((p)->lpVtbl->SetCurrentPosition(p, a))
#define IDirectSoundBuffer_SetFormat(p, a)          ((p)->lpVtbl->SetFormat(p, a))
#define IDirectSoundBuffer_SetVolume(p, a)          ((p)->lpVtbl->SetVolume(p, a))
#define IDirectSoundBuffer_SetPan(p, a)             ((p)->lpVtbl->SetPan(p, a))
#define IDirectSoundBuffer_SetFrequency(p, a)       ((p)->lpVtbl->SetFrequency(p, a))
#define IDirectSoundBuffer_Stop(p)                  ((p)->lpVtbl->Stop(p))
#define IDirectSoundBuffer_Unlock(p, a, b, c, d)    ((p)->lpVtbl->Unlock(p, a, b, c, d))
#define IDirectSoundBuffer_Restore(p)               ((p)->lpVtbl->Restore(p))

typedef struct IDirectSound3DListenerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectSound3DListener *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectSound3DListener *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectSound3DListener *);
    HRESULT (STDMETHODCALLTYPE *GetAllParameters)(IDirectSound3DListener *, LPDS3DLISTENER);
    HRESULT (STDMETHODCALLTYPE *GetDistanceFactor)(IDirectSound3DListener *, D3DVALUE *);
    HRESULT (STDMETHODCALLTYPE *GetDopplerFactor)(IDirectSound3DListener *, D3DVALUE *);
    HRESULT (STDMETHODCALLTYPE *GetOrientation)(IDirectSound3DListener *, D3DVECTOR *, D3DVECTOR *);
    HRESULT (STDMETHODCALLTYPE *GetPosition)(IDirectSound3DListener *, D3DVECTOR *);
    HRESULT (STDMETHODCALLTYPE *GetRolloffFactor)(IDirectSound3DListener *, D3DVALUE *);
    HRESULT (STDMETHODCALLTYPE *GetVelocity)(IDirectSound3DListener *, D3DVECTOR *);
    HRESULT (STDMETHODCALLTYPE *SetAllParameters)(IDirectSound3DListener *, const DS3DLISTENER *, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetDistanceFactor)(IDirectSound3DListener *, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetDopplerFactor)(IDirectSound3DListener *, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetOrientation)(IDirectSound3DListener *, D3DVALUE, D3DVALUE, D3DVALUE, D3DVALUE, D3DVALUE, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetPosition)(IDirectSound3DListener *, D3DVALUE, D3DVALUE, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetRolloffFactor)(IDirectSound3DListener *, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetVelocity)(IDirectSound3DListener *, D3DVALUE, D3DVALUE, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *CommitDeferredSettings)(IDirectSound3DListener *);
} IDirectSound3DListenerVtbl;
struct IDirectSound3DListener { const IDirectSound3DListenerVtbl *lpVtbl; };

#define IDirectSound3DListener_QueryInterface(p, a, b) ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectSound3DListener_AddRef(p)               ((p)->lpVtbl->AddRef(p))
#define IDirectSound3DListener_Release(p)              ((p)->lpVtbl->Release(p))
#define IDirectSound3DListener_SetDopplerFactor(p, a, b) ((p)->lpVtbl->SetDopplerFactor(p, a, b))
#define IDirectSound3DListener_SetPosition(p, a, b, c, d) ((p)->lpVtbl->SetPosition(p, a, b, c, d))
#define IDirectSound3DListener_SetRolloffFactor(p, a, b) ((p)->lpVtbl->SetRolloffFactor(p, a, b))
#define IDirectSound3DListener_CommitDeferredSettings(p) ((p)->lpVtbl->CommitDeferredSettings(p))

typedef struct IDirectSound3DBufferVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectSound3DBuffer *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectSound3DBuffer *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectSound3DBuffer *);
    HRESULT (STDMETHODCALLTYPE *GetAllParameters)(IDirectSound3DBuffer *, LPVOID);
    HRESULT (STDMETHODCALLTYPE *GetConeAngles)(IDirectSound3DBuffer *, LPDWORD, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetConeOrientation)(IDirectSound3DBuffer *, D3DVECTOR *);
    HRESULT (STDMETHODCALLTYPE *GetConeOutsideVolume)(IDirectSound3DBuffer *, LPLONG);
    HRESULT (STDMETHODCALLTYPE *GetMaxDistance)(IDirectSound3DBuffer *, D3DVALUE *);
    HRESULT (STDMETHODCALLTYPE *GetMinDistance)(IDirectSound3DBuffer *, D3DVALUE *);
    HRESULT (STDMETHODCALLTYPE *GetMode)(IDirectSound3DBuffer *, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetPosition)(IDirectSound3DBuffer *, D3DVECTOR *);
    HRESULT (STDMETHODCALLTYPE *GetVelocity)(IDirectSound3DBuffer *, D3DVECTOR *);
    HRESULT (STDMETHODCALLTYPE *SetAllParameters)(IDirectSound3DBuffer *, LPCVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetConeAngles)(IDirectSound3DBuffer *, DWORD, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetConeOrientation)(IDirectSound3DBuffer *, D3DVALUE, D3DVALUE, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetConeOutsideVolume)(IDirectSound3DBuffer *, LONG, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetMaxDistance)(IDirectSound3DBuffer *, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetMinDistance)(IDirectSound3DBuffer *, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetMode)(IDirectSound3DBuffer *, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetPosition)(IDirectSound3DBuffer *, D3DVALUE, D3DVALUE, D3DVALUE, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetVelocity)(IDirectSound3DBuffer *, D3DVALUE, D3DVALUE, D3DVALUE, DWORD);
} IDirectSound3DBufferVtbl;
struct IDirectSound3DBuffer { const IDirectSound3DBufferVtbl *lpVtbl; };

#define IDirectSound3DBuffer_Release(p)              ((p)->lpVtbl->Release(p))
#define IDirectSound3DBuffer_SetPosition(p, a, b, c, d) ((p)->lpVtbl->SetPosition(p, a, b, c, d))
#define IDirectSound3DBuffer_SetMinDistance(p, a, b) ((p)->lpVtbl->SetMinDistance(p, a, b))
#define IDirectSound3DBuffer_SetMaxDistance(p, a, b) ((p)->lpVtbl->SetMaxDistance(p, a, b))

HRESULT WINAPI DirectSoundCreate(LPCGUID guid, LPDIRECTSOUND *out, IUnknown *outer);

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_DSOUND_H */
