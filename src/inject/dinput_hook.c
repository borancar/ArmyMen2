#include "dinput_hook.h"
#include "hooklog.h"
#include "input.h"
#include "orig.h"

#include <windows.h>
#include <string.h>

/* Import Address Table slot for DINPUT.dll!DirectInputCreateA.
 * RVA 0x0006F014 from the import directory, image base 0x00400000. */
#define IAT_DIRECTINPUTCREATEA 0x0046F014u

/* Vtable indices. IDirectInputA:      3 = CreateDevice.
 *                 IDirectInputDeviceA: 9 = GetDeviceState, 10 = GetDeviceData. */
#define VT_CREATE_DEVICE     3
#define VT_GET_DEVICE_STATE  9
#define VT_GET_DEVICE_DATA  10

typedef struct { void **vtbl; } IDI;
typedef struct { void **vtbl; } IDID;

typedef HRESULT (__stdcall *DirectInputCreateA_fn)(HINSTANCE, DWORD, IDI **, void *);
typedef HRESULT (__stdcall *CreateDevice_fn)(IDI *, const GUID *, IDID **, void *);
typedef HRESULT (__stdcall *GetDeviceState_fn)(IDID *, DWORD, void *);
typedef HRESULT (__stdcall *GetDeviceData_fn)(IDID *, DWORD, void *, DWORD *, DWORD);

static const GUID kSysKeyboard =
    { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID kSysMouse =
    { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

enum { DEV_OTHER = 0, DEV_KEYBOARD, DEV_MOUSE };

static DirectInputCreateA_fn orig_create;
static CreateDevice_fn       orig_create_device;
static GetDeviceState_fn     orig_get_state;
static GetDeviceData_fn      orig_get_data;

/* Vtables are shared by every instance of a class, so there are only ever a
 * couple of distinct devices. A tiny linear table is the right shape. */
static struct { IDID *dev; int kind; } g_devices[8];
static int g_ndevices;
static CRITICAL_SECTION g_lock;

static int kind_of(IDID *dev)
{
    int i, kind = DEV_OTHER;

    EnterCriticalSection(&g_lock);
    for (i = 0; i < g_ndevices; i++)
        if (g_devices[i].dev == dev) {
            kind = g_devices[i].kind;
            break;
        }
    LeaveCriticalSection(&g_lock);
    return kind;
}

static void remember(IDID *dev, int kind)
{
    EnterCriticalSection(&g_lock);
    if (g_ndevices < (int)(sizeof g_devices / sizeof g_devices[0])) {
        g_devices[g_ndevices].dev  = dev;
        g_devices[g_ndevices].kind = kind;
        g_ndevices++;
    }
    LeaveCriticalSection(&g_lock);
}

/* Replace one vtable slot, saving the original only the first time -- a second
 * pass would otherwise record our own hook as the original and recurse. */
static int patch_slot(void **vtbl, int index, void *hook, void **saved)
{
    DWORD prot = 0;

    if (vtbl[index] == hook)
        return 0;
    if (!VirtualProtect(&vtbl[index], sizeof(void *), PAGE_READWRITE, &prot))
        return 1;
    if (!*saved)
        *saved = vtbl[index];
    vtbl[index] = hook;
    VirtualProtect(&vtbl[index], sizeof(void *), prot, &prot);
    return 0;
}

static HRESULT __stdcall hook_get_state(IDID *dev, DWORD cb, void *data)
{
    HRESULT hr = orig_get_state(dev, cb, data);

    if (FAILED(hr) || !data)
        return hr;

    switch (kind_of(dev)) {
    case DEV_KEYBOARD:
        /* A DirectInput keyboard state buffer is 256 bytes, one per scancode. */
        if (cb >= 256)
            input_overlay_keyboard((uint8_t *)data, 256);
        break;
    case DEV_MOUSE:
        /* DIMOUSESTATE is three LONG axes then the button bytes; DIMOUSESTATE2
         * only differs by having eight buttons instead of four. */
        if (cb >= 16)
            input_overlay_mouse((int32_t *)data, (uint8_t *)data + 12, cb - 12);
        break;
    default:
        break;
    }
    return hr;
}

#define DIGDD_PEEK 0x00000001u

/* This game reads input buffered, so this -- not GetDeviceState -- is the path
 * that actually matters. Real events come back from the device first, then our
 * queued ones are appended into whatever capacity is left, so injection adds to
 * real input rather than masking it.
 *
 * The caller's capacity has to be captured before the call: pdwInOut is
 * in/out, and the original overwrites it with the number of events produced.
 */
static HRESULT __stdcall hook_get_data(IDID *dev, DWORD cbObj, void *rgdod,
                                       DWORD *pdwInOut, DWORD flags)
{
    DWORD   cap, got, n;
    HRESULT hr;
    int     kind;

    input_pump();

    if (!pdwInOut)
        return orig_get_data(dev, cbObj, rgdod, pdwInOut, flags);

    cap = *pdwInOut;
    hr  = orig_get_data(dev, cbObj, rgdod, pdwInOut, flags);

    /* A NULL buffer means the caller is draining or counting, not collecting. */
    if (FAILED(hr) || !rgdod || cbObj < 16)
        return hr;

    kind = kind_of(dev);
    if (kind != DEV_KEYBOARD && kind != DEV_MOUSE)
        return hr;

    got = *pdwInOut;
    if (got >= cap)
        return hr;

    n = input_take_events(kind == DEV_KEYBOARD ? AM2_DEV_KEYBOARD : AM2_DEV_MOUSE,
                          (uint8_t *)rgdod + (size_t)got * cbObj,
                          cbObj, cap - got, (flags & DIGDD_PEEK) != 0);
    *pdwInOut = got + n;
    return hr;
}

static HRESULT __stdcall hook_create_device(IDI *self, const GUID *guid,
                                            IDID **out, void *unk)
{
    HRESULT hr = orig_create_device(self, guid, out, unk);
    int     kind = DEV_OTHER;
    const char *what = "other";

    if (FAILED(hr) || !out || !*out)
        return hr;

    if (guid && memcmp(guid, &kSysKeyboard, sizeof(GUID)) == 0) {
        kind = DEV_KEYBOARD;
        what = "keyboard";
    } else if (guid && memcmp(guid, &kSysMouse, sizeof(GUID)) == 0) {
        kind = DEV_MOUSE;
        what = "mouse";
    }

    remember(*out, kind);
    patch_slot((*out)->vtbl, VT_GET_DEVICE_STATE, (void *)hook_get_state,
               (void **)&orig_get_state);
    patch_slot((*out)->vtbl, VT_GET_DEVICE_DATA, (void *)hook_get_data,
               (void **)&orig_get_data);

    hooklog("dinput: hooked %s device %p", what, (void *)*out);
    return hr;
}

static HRESULT __stdcall hook_create(HINSTANCE inst, DWORD version,
                                     IDI **out, void *unk)
{
    HRESULT hr = orig_create(inst, version, out, unk);

    if (FAILED(hr) || !out || !*out) {
        hooklog("dinput: DirectInputCreateA failed (hr %08lx)", (unsigned long)hr);
        return hr;
    }
    hooklog("dinput: DirectInputCreateA version %04lx -> %p",
            (unsigned long)version, (void *)*out);

    patch_slot((*out)->vtbl, VT_CREATE_DEVICE, (void *)hook_create_device,
               (void **)&orig_create_device);
    return hr;
}

int dinput_hook_install(void)
{
    void **slot = (void **)IAT_DIRECTINPUTCREATEA;
    DWORD  prot = 0;

    InitializeCriticalSection(&g_lock);
    input_init();

    if (IsBadReadPtr(slot, sizeof(void *))) {
        hooklog("dinput: IAT slot %08x unreadable", IAT_DIRECTINPUTCREATEA);
        return 1;
    }
    /* The slot must still point outside the game image -- into dinput.dll. If
     * it points inside, the loader has not filled it in or this is not the
     * binary the address was taken from. */
    if ((uintptr_t)*slot >= AM2_IMAGE_BASE && (uintptr_t)*slot < 0x00700000u) {
        hooklog("dinput: IAT slot %08x holds %p, not an imported function",
                IAT_DIRECTINPUTCREATEA, *slot);
        return 1;
    }

    orig_create = (DirectInputCreateA_fn)*slot;

    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &prot)) {
        hooklog("dinput: VirtualProtect failed on IAT slot");
        return 1;
    }
    *slot = (void *)hook_create;
    VirtualProtect(slot, sizeof(void *), prot, &prot);

    hooklog("dinput: IAT %08x DirectInputCreateA %p -> %p",
            IAT_DIRECTINPUTCREATEA, (void *)orig_create, (void *)hook_create);
    return 0;
}
