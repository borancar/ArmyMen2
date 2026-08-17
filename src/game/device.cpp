/* DirectDraw and DirectInput bring-up -- reconstructed from ArmyMen2.exe.
 *
 *   InitDirectDraw  0x0041AA10   1 call site
 *   InitInput       0x00426D30   1 call site
 *
 * The deepest point of the boundary: every DirectDraw and DirectInput object
 * the rest of the engine uses is created here, and the rest of the engine only
 * ever sees the globals these two publish.
 *
 * WHY THE DIRECTX ENTRY POINTS GO THROUGH THE GAME'S THUNKS
 *
 *   DirectDrawCreate and DirectInputCreateA are reached by calling 0x00463396
 *   and 0x00464410, which are the game's own one-instruction import thunks,
 *   rather than by importing the symbols into am2hook.dll.
 *
 *   For DirectDraw that is only consistency. For DirectInput it is required.
 *   src/inject/dinput_hook.c intercepts input by patching the game's IAT slot
 *   at 0x0046F014 and wrapping the interfaces that come back; an import of our
 *   own resolves through our IAT, walks straight past the hook, and takes the
 *   harness's ability to drive the game with it. The thunk reads the patched
 *   slot at the moment of the call, so the hook applies exactly as before.
 *
 *   This is the first reconstruction where replacing a function could have
 *   broken the harness rather than the game, and it would have failed quietly:
 *   everything would still run, and only injected input would stop arriving.
 *
 * The GUIDs, data formats and property blocks are the game's own copies in its
 * .rdata, passed by address. Nothing here needs dxguid, and using the game's
 * data rather than the SDK's is also the more faithful reading -- these are the
 * bytes the original passed.
 */

#include "device.h"
#include "../inject/patch.h"

#include <stdint.h>

/* GetDeviceCaps index, and the depth everything the game draws assumes. */
#define BITS_PER_PIXEL 8

static_assert(BITSPIXEL == 12, "GetDeviceCaps index");
static_assert(DDSCL_NORMAL == 8, "DDSCL_NORMAL");
static_assert((DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN) == 0x11, "exclusive mode");
static_assert(DDSD_CAPS == 1 && (DDSD_CAPS | DDSD_BACKBUFFERCOUNT) == 0x21,
              "DDSURFACEDESC flags");
static_assert(DDSCAPS_PRIMARYSURFACE == 0x200, "DDSCAPS_PRIMARYSURFACE");
static_assert((DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX) == 0x218,
              "flipping chain caps");
static_assert(DDSCAPS_BACKBUFFER == 4, "DDSCAPS_BACKBUFFER");
static_assert(DDSCAPS_OFFSCREENPLAIN == 0x40, "DDSCAPS_OFFSCREENPLAIN");
static_assert(sizeof(DDSURFACEDESC) == 0x6C, "the 0x6c bytes it reserves");
static_assert((DISCL_EXCLUSIVE | DISCL_FOREGROUND) == 5, "mouse cooperation");
static_assert((DISCL_NONEXCLUSIVE | DISCL_FOREGROUND) == 6, "keyboard cooperation");

#define g_ddraw       (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)
#define g_ddraw2      (*(LPDIRECTDRAW2 *)(uintptr_t)ADDR_DIRECTDRAW2)
#define g_primary     (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_backBuffer  (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_FONT_SURFACE)
#define g_offscreen   (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_SURFACE)
#define g_lockTarget  (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_LOCKED_SURFACE)
#define g_surfaceLocked (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)
#define g_pixelFormatByte (*(const uint8_t *)(uintptr_t)ADDR_PIXEL_FORMAT_BYTE)

#define g_dinput      (*(LPDIRECTINPUTA *)(uintptr_t)ADDR_DINPUT)
#define g_diMouse     (*(LPDIRECTINPUTDEVICEA *)(uintptr_t)ADDR_DI_MOUSE)
#define g_diKeyboard  (*(LPDIRECTINPUTDEVICEA *)(uintptr_t)ADDR_DI_KEYBOARD)

#define g_hInstance   (*(HINSTANCE *)(uintptr_t)ADDR_HINSTANCE)
#define g_screenW     (*(const int32_t *)(uintptr_t)ADDR_SCREEN_W)
#define g_screenH     (*(const int32_t *)(uintptr_t)ADDR_SCREEN_H)
#define g_windowed    (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)

#define kIID_IDirectDraw2 (*(const GUID *)(uintptr_t)ADDR_IID_DIRECTDRAW2)
#define kGuidSysMouse     (*(const GUID *)(uintptr_t)ADDR_GUID_SYS_MOUSE)
#define kGuidSysKeyboard  (*(const GUID *)(uintptr_t)ADDR_GUID_SYS_KEYBOARD)
#define kFormatMouse      ((LPCDIDATAFORMAT)(uintptr_t)ADDR_DF_MOUSE)
#define kFormatKeyboard   ((LPCDIDATAFORMAT)(uintptr_t)ADDR_DF_KEYBOARD)
#define kBufferSizeProp   ((LPCDIPROPHEADER)(uintptr_t)ADDR_DIPROP_BUFFER_SIZE)

/* The game's import thunks, not ours -- see the note at the top. */
typedef HRESULT (WINAPI *am2_ddraw_create_fn)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef HRESULT (WINAPI *am2_dinput_create_fn)(HINSTANCE, DWORD,
                                               LPDIRECTINPUTA *, IUnknown *);
#define orig_DirectDrawCreate  (*(am2_ddraw_create_fn)ADDR_DIRECTDRAWCREATE)
#define orig_DirectInputCreate (*(am2_dinput_create_fn)ADDR_DIRECTINPUTCREATE)

/* Reported and then returned: it always answers 0, so `return ReportError(...)`
 * is how both of these say "failed". */
typedef int32_t (__cdecl *am2_report_error_fn)(HRESULT hr, const char *fmt, ...);
#define orig_report_error (*(am2_report_error_fn)ADDR_REPORT_ERROR)

/* Game helpers, left in the original image. Both are themselves DirectDraw
 * callers and are on the list to reconstruct next. */
typedef LPDIRECTDRAWSURFACE (__cdecl *am2_create_offscreen_fn)(int32_t w, int32_t h,
                                                               int32_t caps,
                                                               int32_t which);
typedef void (__cdecl *am2_clear_surface_fn)(LPDIRECTDRAWSURFACE s, uint32_t fmt);
#define orig_create_offscreen (*(am2_create_offscreen_fn)ADDR_CREATE_OFFSCREEN)
#define orig_clear_surface   (*(am2_clear_surface_fn)ADDR_CLEAR_SURFACE)

/* ---- display ----------------------------------------------------------- */

/* The desktop is not 8-bit and we want a window on it. The only way to change
 * the display mode is to own the display, so take it, switch it, give it back.
 * Three cooperative-level changes for one mode change. */
static HRESULT ForceEightBitDesktop(HWND hWnd)
{
    HRESULT hr;

    hr = IDirectDraw_SetCooperativeLevel(g_ddraw, hWnd,
                                         DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (hr)
        return hr;
    /* Whatever the desktop currently measures, at 8 bits. Note this is the v1
     * interface, whose SetDisplayMode takes no refresh rate or flags. */
    hr = IDirectDraw_SetDisplayMode(g_ddraw, GetSystemMetrics(SM_CXSCREEN),
                                    GetSystemMetrics(SM_CYSCREEN),
                                    BITS_PER_PIXEL);
    if (hr)
        return hr;
    return IDirectDraw_SetCooperativeLevel(g_ddraw, hWnd, DDSCL_NORMAL);
}

HRESULT __cdecl InitDirectDraw(HWND hWnd)
{
    DDSURFACEDESC ddsd;
    DDSCAPS       caps;
    HRESULT       hr;
    HDC           dc;
    int32_t       desktopBpp;

    dc = GetDC(hWnd);
    if (!dc)
        return E_OUTOFMEMORY;
    desktopBpp = GetDeviceCaps(dc, BITSPIXEL);
    ReleaseDC(hWnd, dc);

    hr = orig_DirectDrawCreate(NULL, &g_ddraw, NULL);
    if (hr)
        return hr;
    hr = IDirectDraw_QueryInterface(g_ddraw, kIID_IDirectDraw2,
                                    (LPVOID *)&g_ddraw2);
    if (hr)
        return hr;

    hr = IDirectDraw2_SetCooperativeLevel(
             g_ddraw2, hWnd,
             g_windowed ? DDSCL_NORMAL : (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN));
    if (hr)
        return hr;

    if (g_windowed) {
        if (desktopBpp != BITS_PER_PIXEL) {
            hr = ForceEightBitDesktop(hWnd);
            if (hr)
                return hr;
        }
    } else {
        /* The v2 interface, whose SetDisplayMode does take the extra two. */
        hr = IDirectDraw2_SetDisplayMode(g_ddraw2, g_screenW, g_screenH,
                                         BITS_PER_PIXEL, 0, 0);
        if (hr)
            return hr;
    }

    /* Fullscreen gets a flipping chain with one back buffer; windowed gets a
     * lone primary and makes its own back buffer below.
     *
     * The descriptor is deliberately not cleared, because the original does not
     * clear it: it writes dwSize, dwFlags, the caps and -- fullscreen only --
     * the back buffer count, and leaves the other twenty-odd fields as whatever
     * was on the stack. That is safe by contract, since DirectDraw reads only
     * the fields dwFlags names, and it is kept rather than tidied for the same
     * reason as the other reproduced originals. Worth knowing about if a
     * driver ever turns out to read an unflagged field. */
    ddsd.dwSize = sizeof ddsd;
    if (g_windowed) {
        ddsd.dwFlags = DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    } else {
        ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        ddsd.ddsCaps.dwCaps =
            DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        ddsd.dwBackBufferCount = 1;
    }
    hr = IDirectDraw2_CreateSurface(g_ddraw2, &ddsd, &g_primary, NULL);
    if (hr)
        return hr;

    if (g_windowed) {
        g_backBuffer = orig_create_offscreen(g_screenW, g_screenH,
                                             DDSCAPS_OFFSCREENPLAIN, -1);
    } else {
        caps.dwCaps = DDSCAPS_BACKBUFFER;
        hr = IDirectDrawSurface_GetAttachedSurface(g_primary, &caps,
                                                   &g_backBuffer);
        if (hr)
            return hr;
    }

    g_offscreen  = orig_create_offscreen(g_screenW, g_screenH,
                                         DDSCAPS_OFFSCREENPLAIN, -1);
    orig_clear_surface(g_primary, g_pixelFormatByte);
    orig_clear_surface(g_backBuffer, g_pixelFormatByte);

    /* Drawing starts aimed at the back buffer, with no lock held. */
    g_lockTarget    = g_backBuffer;
    g_surfaceLocked = 0;
    return 0;
}

/* ---- input ------------------------------------------------------------- */

int32_t __cdecl InitInput(HWND hWnd)
{
    HRESULT hr;

    hr = orig_DirectInputCreate(g_hInstance, DIRECTINPUT_VERSION, &g_dinput,
                                NULL);
    if (hr)
        return orig_report_error(hr, "DirectInputCreate()");

    /* Mouse: exclusive, so the system pointer goes away and motion arrives
     * raw, and buffered, so it arrives as deltas rather than positions. */
    hr = IDirectInput_CreateDevice(g_dinput, kGuidSysMouse, &g_diMouse, NULL);
    if (hr)
        return orig_report_error(hr, "CreateDevice (mouse)");
    hr = IDirectInputDevice_SetDataFormat(g_diMouse, kFormatMouse);
    if (hr)
        return orig_report_error(hr, "SetDataFormat (mouse)");
    hr = IDirectInputDevice_SetCooperativeLevel(g_diMouse, hWnd,
                                                DISCL_EXCLUSIVE |
                                                DISCL_FOREGROUND);
    if (hr)
        return orig_report_error(hr, "SetCooperativeLevel (mouse)");
    hr = IDirectInputDevice_SetProperty(g_diMouse, DIPROP_BUFFERSIZE,
                                        kBufferSizeProp);
    if (hr)
        return orig_report_error(hr, "Set buffer size (mouse)");

    /* Keyboard: non-exclusive. Taking it exclusively would take Alt-Tab too. */
    hr = IDirectInput_CreateDevice(g_dinput, kGuidSysKeyboard, &g_diKeyboard,
                                   NULL);
    if (hr)
        return orig_report_error(hr, "CreateDevice (keyboard)");
    hr = IDirectInputDevice_SetDataFormat(g_diKeyboard, kFormatKeyboard);
    if (hr)
        return orig_report_error(hr, "SetDataFormat (keyboard)");
    hr = IDirectInputDevice_SetCooperativeLevel(g_diKeyboard, hWnd,
                                                DISCL_NONEXCLUSIVE |
                                                DISCL_FOREGROUND);
    if (hr)
        return orig_report_error(hr, "SetCooperativeLevel (keyboard)");

    /* Rewind the two input cursors to the starts of their buffers.
     *
     * The original also loads 0x005125C8, 0x00512BD0, 0x100 and 0x400 into
     * registers here and never uses them -- the leftovers of two cleared
     * buffers, with the clearing itself gone. edi is restored and eax and ecx
     * are dead on return, so the register writes are provably unobservable and
     * only these two stores are kept. */
    *(uint32_t *)(uintptr_t)ADDR_INPUT_CURSOR_A = ADDR_INPUT_BUFFER_A;
    *(uint32_t *)(uintptr_t)ADDR_INPUT_CURSOR_B = ADDR_INPUT_BUFFER_B;
    return 1;
}

int device_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_INIT_DIRECTDRAW, (const void *)InitDirectDraw,
                        "InitDirectDraw", 1);
    rc |= patch_replace(ADDR_INIT_INPUT, (const void *)InitInput,
                        "InitInput", 1);
    return rc;
}
