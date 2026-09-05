/* dinput.h -- DirectInput 5 as the game sees it: one IDirectInput and the
 * device interface, buffered mouse data and the keyboard state array.
 *
 * DIPROPHEADER and DIDATAFORMAT keep the SDK layout: the buffer-size
 * property InitInput passes is read straight out of the original's data
 * (kBufferSizeProp in device.cpp), and c_dfDIMouse is typed C data in
 * build/standalone. DIDEVICEOBJECTDATA is the DirectInput 5 shape, 16
 * bytes, without the uAppData DirectInput 8 added. */
#ifndef AM2_PLATFORM_DINPUT_H
#define AM2_PLATFORM_DINPUT_H

#include <windows.h>

AM2_EXTERN_C_BEGIN

typedef struct IDirectInputA       IDirectInputA,       *LPDIRECTINPUTA;
typedef struct IDirectInputDeviceA IDirectInputDeviceA, *LPDIRECTINPUTDEVICEA;

extern const GUID GUID_SysMouse;
extern const GUID GUID_SysKeyboard;
extern const GUID IID_IDirectInputA;
extern const GUID IID_IDirectInputDeviceA;

#define DI_OK                 S_OK
#define DI_NOTATTACHED        S_FALSE
#define DI_BUFFEROVERFLOW     S_FALSE
#define DI_PROPNOEFFECT       S_FALSE
#define DIERR_INPUTLOST       ((HRESULT)0x8007001E)
#define DIERR_NOTACQUIRED     ((HRESULT)0x8007000C)
#define DIERR_INVALIDPARAM    E_INVALIDARG
#define DIERR_NOTINITIALIZED  ((HRESULT)0x80070015)
#define DIERR_UNSUPPORTED     E_NOTIMPL
#define DIERR_ACQUIRED        ((HRESULT)0x800700AA)
#define DIERR_OTHERAPPHASPRIO ((HRESULT)0x80070005)
#define DIERR_GENERIC         E_FAIL
#define DIERR_DEVICENOTREG    REGDB_E_CLASSNOTREG

#define DISCL_EXCLUSIVE    0x00000001
#define DISCL_NONEXCLUSIVE 0x00000002
#define DISCL_FOREGROUND   0x00000004
#define DISCL_BACKGROUND   0x00000008

#define DIPH_DEVICE   0
#define DIPH_BYOFFSET 1
#define DIPH_BYID     2

typedef struct DIPROPHEADER {
    DWORD dwSize;
    DWORD dwHeaderSize;
    DWORD dwObj;
    DWORD dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER *LPCDIPROPHEADER;

typedef struct DIPROPDWORD {
    DIPROPHEADER diph;
    DWORD        dwData;
} DIPROPDWORD, *LPDIPROPDWORD;

#define MAKEDIPROP(prop) (*(const GUID *)(prop))
#define DIPROP_BUFFERSIZE MAKEDIPROP(1)
#define DIPROP_AXISMODE   MAKEDIPROP(2)

typedef struct _DIOBJECTDATAFORMAT {
    const GUID *pguid;
    DWORD       dwOfs;
    DWORD       dwType;
    DWORD       dwFlags;
} DIOBJECTDATAFORMAT, *LPDIOBJECTDATAFORMAT;
typedef const DIOBJECTDATAFORMAT *LPCDIOBJECTDATAFORMAT;

typedef struct _DIDATAFORMAT {
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    LPDIOBJECTDATAFORMAT rgodf;
} DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT *LPCDIDATAFORMAT;

extern const DIDATAFORMAT c_dfDIMouse;
extern const DIDATAFORMAT c_dfDIKeyboard;

typedef struct DIDEVICEOBJECTDATA {
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;

typedef struct _DIMOUSESTATE {
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[4];
} DIMOUSESTATE, *LPDIMOUSESTATE;

#define DIMOFS_X       0
#define DIMOFS_Y       4
#define DIMOFS_Z       8
#define DIMOFS_BUTTON0 12
#define DIMOFS_BUTTON1 13
#define DIMOFS_BUTTON2 14
#define DIMOFS_BUTTON3 15

/* The keyboard scancodes the game names. Everything else in the 256-byte
 * state array is reached by number. */
#define DIK_ESCAPE     0x01
#define DIK_1          0x02
#define DIK_RETURN     0x1C
#define DIK_LCONTROL   0x1D
#define DIK_LSHIFT     0x2A
#define DIK_RSHIFT     0x36
#define DIK_LMENU      0x38
#define DIK_SPACE      0x39
#define DIK_RCONTROL   0x9D
#define DIK_RMENU      0xB8
#define DIK_UP         0xC8
#define DIK_LEFT       0xCB
#define DIK_RIGHT      0xCD
#define DIK_DOWN       0xD0

typedef struct IDirectInputAVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectInputA *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectInputA *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectInputA *);
    HRESULT (STDMETHODCALLTYPE *CreateDevice)(IDirectInputA *, REFGUID, LPDIRECTINPUTDEVICEA *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *EnumDevices)(IDirectInputA *, DWORD, LPVOID, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetDeviceStatus)(IDirectInputA *, REFGUID);
    HRESULT (STDMETHODCALLTYPE *RunControlPanel)(IDirectInputA *, HWND, DWORD);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectInputA *, HINSTANCE, DWORD);
} IDirectInputAVtbl;
struct IDirectInputA { const IDirectInputAVtbl *lpVtbl; };

#define IDirectInput_QueryInterface(p, a, b)  ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectInput_AddRef(p)                ((p)->lpVtbl->AddRef(p))
#define IDirectInput_Release(p)               ((p)->lpVtbl->Release(p))
#define IDirectInput_CreateDevice(p, a, b, c) ((p)->lpVtbl->CreateDevice(p, a, b, c))

typedef struct IDirectInputDeviceAVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectInputDeviceA *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectInputDeviceA *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectInputDeviceA *);
    HRESULT (STDMETHODCALLTYPE *GetCapabilities)(IDirectInputDeviceA *, LPVOID);
    HRESULT (STDMETHODCALLTYPE *EnumObjects)(IDirectInputDeviceA *, LPVOID, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetProperty)(IDirectInputDeviceA *, REFGUID, LPDIPROPHEADER);
    HRESULT (STDMETHODCALLTYPE *SetProperty)(IDirectInputDeviceA *, REFGUID, LPCDIPROPHEADER);
    HRESULT (STDMETHODCALLTYPE *Acquire)(IDirectInputDeviceA *);
    HRESULT (STDMETHODCALLTYPE *Unacquire)(IDirectInputDeviceA *);
    HRESULT (STDMETHODCALLTYPE *GetDeviceState)(IDirectInputDeviceA *, DWORD, LPVOID);
    HRESULT (STDMETHODCALLTYPE *GetDeviceData)(IDirectInputDeviceA *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetDataFormat)(IDirectInputDeviceA *, LPCDIDATAFORMAT);
    HRESULT (STDMETHODCALLTYPE *SetEventNotification)(IDirectInputDeviceA *, HANDLE);
    HRESULT (STDMETHODCALLTYPE *SetCooperativeLevel)(IDirectInputDeviceA *, HWND, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetObjectInfo)(IDirectInputDeviceA *, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetDeviceInfo)(IDirectInputDeviceA *, LPVOID);
    HRESULT (STDMETHODCALLTYPE *RunControlPanel)(IDirectInputDeviceA *, HWND, DWORD);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectInputDeviceA *, HINSTANCE, DWORD, REFGUID);
} IDirectInputDeviceAVtbl;
struct IDirectInputDeviceA { const IDirectInputDeviceAVtbl *lpVtbl; };

#define IDirectInputDevice_QueryInterface(p, a, b)    ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectInputDevice_AddRef(p)                  ((p)->lpVtbl->AddRef(p))
#define IDirectInputDevice_Release(p)                 ((p)->lpVtbl->Release(p))
#define IDirectInputDevice_SetProperty(p, a, b)       ((p)->lpVtbl->SetProperty(p, a, b))
#define IDirectInputDevice_Acquire(p)                 ((p)->lpVtbl->Acquire(p))
#define IDirectInputDevice_Unacquire(p)               ((p)->lpVtbl->Unacquire(p))
#define IDirectInputDevice_GetDeviceState(p, a, b)    ((p)->lpVtbl->GetDeviceState(p, a, b))
#define IDirectInputDevice_GetDeviceData(p, a, b, c, d) ((p)->lpVtbl->GetDeviceData(p, a, b, c, d))
#define IDirectInputDevice_SetDataFormat(p, a)        ((p)->lpVtbl->SetDataFormat(p, a))
#define IDirectInputDevice_SetCooperativeLevel(p, a, b) ((p)->lpVtbl->SetCooperativeLevel(p, a, b))

HRESULT WINAPI DirectInputCreateA(HINSTANCE inst, DWORD version,
                                  LPDIRECTINPUTA *out, IUnknown *outer);

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_DINPUT_H */
