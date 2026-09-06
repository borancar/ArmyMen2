/* dinput.cpp -- DirectInput: a keyboard whose state is the host's, and a
 * mouse whose buffered data is the host pointer's motion, fed by sdl.cpp.
 *
 * Cooperative levels are accepted and not enforced -- the exclusive
 * foreground mouse the game asks for would take the pointer off the
 * desktop, and this build leaves it there. Acquire always succeeds, and
 * the buffer size the game sets is honoured as a ring.
 */
#include "platform.h"
#include <dinput.h>

#include <stdlib.h>
#include <string.h>

AM2_DEFINE_GUID_VALUE(GUID_SysMouse,           0x6F1D2B60, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
AM2_DEFINE_GUID_VALUE(GUID_SysKeyboard,        0x6F1D2B61, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
AM2_DEFINE_GUID_VALUE(IID_IDirectInputA,       0x89521360, 0xAA8A, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
AM2_DEFINE_GUID_VALUE(IID_IDirectInputDeviceA, 0x5944E680, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

/* The two data formats, as the SDK defines them: a mouse is three axes
 * and four buttons in 16 bytes, a keyboard 256 bytes of keys. The game
 * only hands these back to SetDataFormat, which accepts them. */
static const DIOBJECTDATAFORMAT am2_mouse_objects[] = {
    { NULL, DIMOFS_X, 0x00FFFF03, 0 }, { NULL, DIMOFS_Y, 0x00FFFF03, 0 },
    { NULL, DIMOFS_Z, 0x80FFFF03, 0 },
    { NULL, DIMOFS_BUTTON0, 0x00FFFF0C, 0 }, { NULL, DIMOFS_BUTTON1, 0x00FFFF0C, 0 },
    { NULL, DIMOFS_BUTTON2, 0x80FFFF0C, 0 }, { NULL, DIMOFS_BUTTON3, 0x80FFFF0C, 0 },
};
const DIDATAFORMAT c_dfDIMouse = {
    sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), 0x00000002, 16, 7,
    (LPDIOBJECTDATAFORMAT)am2_mouse_objects,
};
const DIDATAFORMAT c_dfDIKeyboard = {
    sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), 0x00000002, 256, 0, NULL,
};

/* ---- objects ---------------------------------------------------------------------- */

enum { AM2_DI_MOUSE = 1, AM2_DI_KEYBOARD };

#define AM2_DI_RING 256

typedef struct AM2_DIDevice {
    const IDirectInputDeviceAVtbl *lpVtbl;
    int32_t  refs;
    int32_t  kind;
    int32_t  acquired;
    DWORD    bufferSize;
    DIDEVICEOBJECTDATA ring[AM2_DI_RING];
    int32_t  head, count;
    DWORD    sequence;
} AM2_DIDevice;

typedef struct AM2_DInput {
    const IDirectInputAVtbl *lpVtbl;
    int32_t refs;
} AM2_DInput;

static AM2_DIDevice *am2_mouse_device;
extern const IDirectInputDeviceAVtbl am2_device_vtbl;
extern const IDirectInputAVtbl       am2_dinput_vtbl;

/* ---- what the host reports ----------------------------------------------------------- */

static void am2_mouse_push(DWORD ofs, DWORD data)
{
    AM2_DIDevice *d = am2_mouse_device;
    DIDEVICEOBJECTDATA *e;
    int32_t cap;

    if (!d)
        return;
    cap = d->bufferSize ? (int32_t)d->bufferSize : 64;
    if (cap > AM2_DI_RING)
        cap = AM2_DI_RING;
    if (d->count == cap) {                 /* overflow: drop the oldest */
        d->head = (d->head + 1) % AM2_DI_RING;
        d->count--;
    }
    e = &d->ring[(d->head + d->count) % AM2_DI_RING];
    e->dwOfs = ofs;
    e->dwData = data;
    e->dwTimeStamp = GetTickCount();
    e->dwSequence = ++d->sequence;
    d->count++;
    if (getenv("AM2_TRACE_DI"))
        fprintf(stderr, "DI pump %u push ofs=%u data=%d queued=%d\n", (unsigned)am2_host_pump_number(),
                (unsigned)ofs, (int)data, (int)d->count);
}

void am2_di_mouse_motion(int32_t dx, int32_t dy)
{
    if (dx)
        am2_mouse_push(DIMOFS_X, (DWORD)dx);
    if (dy)
        am2_mouse_push(DIMOFS_Y, (DWORD)dy);
}

void am2_di_mouse_axis(uint32_t ofs, int32_t delta)
{
    am2_mouse_push(ofs, (DWORD)delta);
}

void am2_di_mouse_button(int32_t button, int32_t down)
{
    if (button >= 0 && button < 4)
        am2_mouse_push((DWORD)(DIMOFS_BUTTON0 + button), down ? 0x80 : 0);
}

void am2_di_mouse_wheel(int32_t delta)
{
    am2_mouse_push(DIMOFS_Z, (DWORD)delta);
}

int32_t am2_di_mouse_queued(void)
{
    return am2_mouse_device ? am2_mouse_device->count : 0;
}

int32_t am2_di_mouse_pending(void)
{
    AM2_DIDevice *d = am2_mouse_device;
    int32_t       i, n = 0;

    if (!d)
        return 0;
    for (i = 0; i < d->count; i++) {
        const DIDEVICEOBJECTDATA *e = &d->ring[(d->head + i) % AM2_DI_RING];
        if (e->dwOfs == DIMOFS_X || e->dwOfs == DIMOFS_Y)
            n++;
    }
    return n;
}

/* ---- the device ---------------------------------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE Dev_QueryInterface(IDirectInputDeviceA *p, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectInputDeviceA)) {
        *out = p;
        p->lpVtbl->AddRef(p);
        return DI_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Dev_AddRef(IDirectInputDeviceA *p)
{
    return (ULONG)++((AM2_DIDevice *)p)->refs;
}

static ULONG STDMETHODCALLTYPE Dev_Release(IDirectInputDeviceA *p)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;
    int32_t       n = --d->refs;

    if (n <= 0) {
        if (d == am2_mouse_device)
            am2_mouse_device = NULL;
        free(d);
        return 0;
    }
    return (ULONG)n;
}

#define DEVICE_UNSUPPORTED(name, ...) \
    static HRESULT STDMETHODCALLTYPE Dev_##name(IDirectInputDeviceA *, ##__VA_ARGS__) \
    { return DIERR_UNSUPPORTED; }
DEVICE_UNSUPPORTED(GetCapabilities, LPVOID)
DEVICE_UNSUPPORTED(EnumObjects, LPVOID, LPVOID, DWORD)
DEVICE_UNSUPPORTED(GetObjectInfo, LPVOID, DWORD, DWORD)
DEVICE_UNSUPPORTED(GetDeviceInfo, LPVOID)
DEVICE_UNSUPPORTED(RunControlPanel, HWND, DWORD)

static HRESULT STDMETHODCALLTYPE Dev_GetProperty(IDirectInputDeviceA *p, REFGUID prop, LPDIPROPHEADER hdr)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;

    if ((uintptr_t)AM2_GUID_PTR(prop) == 1 && hdr && hdr->dwSize >= sizeof(DIPROPDWORD)) {
        ((DIPROPDWORD *)hdr)->dwData = d->bufferSize;
        return DI_OK;
    }
    return DIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Dev_SetProperty(IDirectInputDeviceA *p, REFGUID prop, LPCDIPROPHEADER hdr)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;

    /* DIPROP_BUFFERSIZE is property 1, passed as a reference to address 1.
     * The header the game passes lives in its own data (kBufferSizeProp). */
    if ((uintptr_t)AM2_GUID_PTR(prop) == 1 && hdr && hdr->dwSize >= sizeof(DIPROPDWORD)) {
        d->bufferSize = ((const DIPROPDWORD *)hdr)->dwData;
        return DI_OK;
    }
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE Dev_Acquire(IDirectInputDeviceA *p)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;
    HRESULT       hr = d->acquired ? S_FALSE : DI_OK;

    d->acquired = 1;
    return hr;
}

static HRESULT STDMETHODCALLTYPE Dev_Unacquire(IDirectInputDeviceA *p)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;
    HRESULT       hr = d->acquired ? DI_OK : DI_NOTATTACHED;

    d->acquired = 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE Dev_GetDeviceState(IDirectInputDeviceA *p, DWORD size, LPVOID out)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;

    if (!d->acquired)
        return DIERR_NOTACQUIRED;
    if (d->kind == AM2_DI_KEYBOARD) {
        uint8_t keys[256];
        if (size < 256)
            return DIERR_INVALIDPARAM;
        am2_host_keyboard_state(keys);
        memcpy(out, keys, 256);
        return DI_OK;
    }
    if (size < sizeof(DIMOUSESTATE))
        return DIERR_INVALIDPARAM;
    memset(out, 0, sizeof(DIMOUSESTATE));
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE Dev_GetDeviceData(IDirectInputDeviceA *p, DWORD size,
                                                   LPDIDEVICEOBJECTDATA out, LPDWORD count,
                                                   DWORD flags)
{
    AM2_DIDevice *d = (AM2_DIDevice *)p;
    DWORD         want, got = 0;

    if (size != sizeof(DIDEVICEOBJECTDATA) || !count)
        return DIERR_INVALIDPARAM;
    if (!d->acquired)
        return DIERR_NOTACQUIRED;
    want = *count;
    if (want == INFINITE)
        want = (DWORD)d->count;
    while (got < want && d->count > 0) {
        if (out)
            out[got] = d->ring[d->head];
        if (getenv("AM2_TRACE_DI"))
            fprintf(stderr, "DI pump %u take ofs=%u data=%d left=%d%s\n", (unsigned)am2_host_pump_number(),
                    (unsigned)d->ring[d->head].dwOfs, (int)d->ring[d->head].dwData, (int)d->count - 1,
                    (flags & 1) ? " (peek)" : "");
        got++;
        if (!(flags & 1)) {                /* DIGDD_PEEK is 1 */
            d->head = (d->head + 1) % AM2_DI_RING;
            d->count--;
        } else if (got >= (DWORD)d->count) {
            break;
        }
    }
    *count = got;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE Dev_SetDataFormat(IDirectInputDeviceA *p, LPCDIDATAFORMAT fmt)
{
    (void)p;
    return fmt ? DI_OK : DIERR_INVALIDPARAM;
}

static HRESULT STDMETHODCALLTYPE Dev_SetEventNotification(IDirectInputDeviceA *p, HANDLE ev)
{
    (void)p; (void)ev;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE Dev_SetCooperativeLevel(IDirectInputDeviceA *p, HWND hwnd, DWORD flags)
{
    (void)p; (void)hwnd; (void)flags;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE Dev_Initialize(IDirectInputDeviceA *p, HINSTANCE inst, DWORD ver, REFGUID g)
{
    (void)p; (void)inst; (void)ver; (void)g;
    return DI_OK;
}

const IDirectInputDeviceAVtbl am2_device_vtbl = {
    Dev_QueryInterface, Dev_AddRef, Dev_Release, Dev_GetCapabilities, Dev_EnumObjects,
    Dev_GetProperty, Dev_SetProperty, Dev_Acquire, Dev_Unacquire,
    Dev_GetDeviceState, Dev_GetDeviceData, Dev_SetDataFormat, Dev_SetEventNotification,
    Dev_SetCooperativeLevel, Dev_GetObjectInfo, Dev_GetDeviceInfo, Dev_RunControlPanel,
    Dev_Initialize,
};

/* ---- the DirectInput object ------------------------------------------------------------------ */

static HRESULT STDMETHODCALLTYPE DI_QueryInterface(IDirectInputA *p, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectInputA)) {
        *out = p;
        p->lpVtbl->AddRef(p);
        return DI_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DI_AddRef(IDirectInputA *p)
{
    return (ULONG)++((AM2_DInput *)p)->refs;
}

static ULONG STDMETHODCALLTYPE DI_Release(IDirectInputA *p)
{
    AM2_DInput *di = (AM2_DInput *)p;
    int32_t     n = --di->refs;

    if (n <= 0) {
        free(di);
        return 0;
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE DI_CreateDevice(IDirectInputA *p, REFGUID guid,
                                                 LPDIRECTINPUTDEVICEA *out, IUnknown *outer)
{
    AM2_DIDevice *d;
    int32_t       kind;

    (void)p; (void)outer;
    *out = NULL;
    if (IsEqualGUID(guid, GUID_SysMouse))
        kind = AM2_DI_MOUSE;
    else if (IsEqualGUID(guid, GUID_SysKeyboard))
        kind = AM2_DI_KEYBOARD;
    else
        return DIERR_DEVICENOTREG;
    d = (AM2_DIDevice *)calloc(1, sizeof *d);
    if (!d)
        return E_OUTOFMEMORY;
    d->lpVtbl = &am2_device_vtbl;
    d->refs = 1;
    d->kind = kind;
    d->bufferSize = 0;
    if (kind == AM2_DI_MOUSE)
        am2_mouse_device = d;
    *out = (LPDIRECTINPUTDEVICEA)d;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE DI_EnumDevices(IDirectInputA *p, DWORD type, LPVOID cb, LPVOID ctx, DWORD flags)
{
    (void)p; (void)type; (void)cb; (void)ctx; (void)flags;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE DI_GetDeviceStatus(IDirectInputA *p, REFGUID g)
{
    (void)p; (void)g;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE DI_RunControlPanel(IDirectInputA *p, HWND hwnd, DWORD flags)
{
    (void)p; (void)hwnd; (void)flags;
    return DIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DI_Initialize(IDirectInputA *p, HINSTANCE inst, DWORD ver)
{
    (void)p; (void)inst; (void)ver;
    return DI_OK;
}

const IDirectInputAVtbl am2_dinput_vtbl = {
    DI_QueryInterface, DI_AddRef, DI_Release, DI_CreateDevice, DI_EnumDevices,
    DI_GetDeviceStatus, DI_RunControlPanel, DI_Initialize,
};

HRESULT WINAPI DirectInputCreateA(HINSTANCE inst, DWORD version, LPDIRECTINPUTA *out,
                                  IUnknown *outer)
{
    AM2_DInput *di;

    (void)inst; (void)version; (void)outer;
    di = (AM2_DInput *)calloc(1, sizeof *di);
    if (!di)
        return E_OUTOFMEMORY;
    di->lpVtbl = &am2_dinput_vtbl;
    di->refs = 1;
    *out = (LPDIRECTINPUTA)di;
    return DI_OK;
}
