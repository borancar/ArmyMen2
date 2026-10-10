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

#include "surface.h"
#include "device.h"
#include "../mousepress.h"
#include "../crt.h"
#include "report.h"
#include "../rect.h"
#include "../../inject/patch.h"

/* The DirectInput device GUIDs the image carries in its own .rdata (GUID_SysMouse
 * / GUID_SysKeyboard), transcribed as their 16 bytes. 0x0046F5A8 / 0x0046F5B8. */
extern "C" const uint8_t am2_guid_sys_mouse[16] =
    { 0x60,0x2b,0x1d,0x6f, 0xa0,0xd5,0xcf,0x11, 0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00 };
extern "C" const uint8_t am2_guid_sys_keyboard[16] =
    { 0x61,0x2b,0x1d,0x6f, 0xa0,0xd5,0xcf,0x11, 0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00 };
/* IID_IDirectDraw2, the interface the game queries for. 0x0046F338. */
extern "C" const uint8_t am2_iid_directdraw2[16] =
    { 0xe0,0xf3,0xa6,0xb3, 0x43,0x2b,0xcf,0x11, 0xa2,0xde,0x00,0xaa,0x00,0xb9,0x33,0x56 };

#ifdef AM2_STANDALONE
/* The injected build reaches the input queue through dinput_hook.c,
 * which wraps the device's own GetDeviceState and GetDeviceData. The
 * standalone has no IAT to patch and no original device to wrap -- we
 * ARE the poller -- so the same two overlays are applied here instead,
 * at the points that hook applies them. */
#include "../../inject/input.h"
#endif

#include <stdint.h>

#ifdef AM2_STANDALONE
/* The mouse/cursor input state block at 0x00485458 (25 dwords, 0x485458..
 * 0x004854BC): deltas, cursor X/Y (init 320,200) and packed point, button and
 * change bits, press timers, activity, grab, hover uid, with a few unused
 * gap dwords the reader indexes past. It is one physical block written by
 * UpdateMouseState and the control socket and read all over, so it is migrated
 * as a single non-const array (initial bytes byte-checked) with each ADDR_
 * macro aliased to its slot; the cursor init keeps it in .data. */
int32_t am2_mouse_state[25] = { 0, 0, 0, 320, 200 };

/* The DirectInput buffer-size property at 0x004854F8 (a DIPROPDWORD: header
 * {size 0x14, hdrsize 0x10, obj 0, how 0} then dwData=0x20). Const, byte-checked. */
extern "C" const uint32_t am2_diprop_buffer_size[6] = {
    0x00000014u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000020u, 0x00000000u,
};
/* The image's c_dfDIKeyboard / c_dfDIMouse DIDATAFORMAT structs at 0x0046FD68 /
 * 0x0046FD80 (their trailing rgodf pointer is an image .text address; native
 * uses the SDK's own structs instead, so these are read only in the hybrid).
 * Transcribed byte-identically so the blob no longer carries them. */
extern "C" const uint32_t am2_df_keyboard[6] = {
    0x00000018u, 0x00000010u, 0x00000002u, 0x00000100u, 0x00000100u, 0x004633A0u,
};
extern "C" const uint32_t am2_df_mouse[6] = {
    0x00000018u, 0x00000010u, 0x00000002u, 0x00000010u, 0x00000007u, 0x004643A0u,
};

/* The screen-geometry block at 0x00485310 (12 dwords): SCREEN_CLIP {0,0,640,480}
 * (BITMAP_AREA_W/H view its right/bottom), BLIT_RECT {0,21,624,480} (HUD_PANEL_X
 * views field 2), and SCREEN_RECT/ORIGIN_DX/DY. One physical block read as RECTs
 * and scalars, migrated as a single non-const array with each macro aliased to
 * its slot; the non-zero dimensions keep it in .data. */
int32_t am2_screen_geom[12] = { 0, 0, 640, 480, 0, 21, 624, 480, 0, 0, 0, 0 };
#endif

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




/* THE DATA FORMATS ARE THE API'S, NOT THE IMAGE'S, in the standalone build.
 * The original's c_dfDIMouse is intact at ADDR_DF_MOUSE, but its `rgodf`
 * points at 0x004643A0 -- a const array MSVC placed BELOW .rdata, inside the
 * .text range the standalone build deliberately does not carry. So the
 * struct arrives and its array does not, and SetDataFormat answers
 * E_INVALIDARG.
 *
 * Reaching for the image's copy was never necessary: c_dfDIMouse and
 * c_dfDIKeyboard are part of the DirectInput contract and every SDK ships
 * the same bytes. Using the ones we link against removes the dependency
 * rather than working around it, which is what migrating a table out of the
 * copied image is supposed to look like. */
#ifdef AM2_STANDALONE
#define kFormatMouse      (&c_dfDIMouse)
#define kFormatKeyboard   (&c_dfDIKeyboard)
#else
#define kFormatMouse      (&g_dfMouse)
#define kFormatKeyboard   (&g_dfKeyboard)
#endif

/* The game's import thunks, not ours -- see the note at the top. */
typedef HRESULT (WINAPI *am2_ddraw_create_fn)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef HRESULT (WINAPI *am2_dinput_create_fn)(HINSTANCE, DWORD,
                                               LPDIRECTINPUTA *, IUnknown *);
#define orig_DirectDrawCreate  (*(am2_ddraw_create_fn)ADDR_DIRECTDRAWCREATE)
#define orig_DirectInputCreate (*(am2_dinput_create_fn)ADDR_DIRECTINPUTCREATE)

/* Game helpers, left in the original image. Both are themselves DirectDraw
 * callers and are on the list to reconstruct next. */
typedef LPDIRECTDRAWSURFACE (__cdecl *am2_create_offscreen_fn)(int32_t w, int32_t h,
                                                               int32_t caps,
                                                               int32_t which);
typedef void (__cdecl *am2_clear_surface_fn)(LPDIRECTDRAWSURFACE s, uint32_t fmt);

/* ---- display ----------------------------------------------------------- */

/* The desktop is not 8-bit and we want a window on it. The only way to change
 * the display mode is to own the display, so take it, switch it, give it back.
 * Three cooperative-level changes for one mode change. */
static inline HRESULT ForceEightBitDesktop(HWND hWnd)
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
    hr = IDirectDraw_QueryInterface(g_ddraw, g_iidDirectdraw2,
                                    (LPVOID *)&g_ddraw2);
    if (hr)
        return hr;

    if (g_windowed)
        hr = IDirectDraw2_SetCooperativeLevel(g_ddraw2, hWnd, DDSCL_NORMAL);
    else
        hr = IDirectDraw2_SetCooperativeLevel(g_ddraw2, hWnd,
                                              DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
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
    hr = IDirectDraw2_CreateSurface(g_ddraw2, &ddsd, &(*(LPDIRECTDRAWSURFACE *)&g_primarySurface), NULL);
    if (hr)
        return hr;

    if (g_windowed) {
        g_backBuffer = CreateOffscreenSurface(g_screenW, g_screenH,
                                             DDSCAPS_OFFSCREENPLAIN, -1);
    } else {
        caps.dwCaps = DDSCAPS_BACKBUFFER;
        hr = IDirectDrawSurface_GetAttachedSurface((*(LPDIRECTDRAWSURFACE *)&g_primarySurface), &caps,
                                                   &g_backBuffer);
        if (hr)
            return hr;
    }

    g_offscreen  = CreateOffscreenSurface(g_screenW, g_screenH,
                                         DDSCAPS_OFFSCREENPLAIN, -1);
    ClearSurface((*(LPDIRECTDRAWSURFACE *)&g_primarySurface), g_backgroundColour);
    ClearSurface(g_backBuffer, g_backgroundColour);

    /* Drawing starts aimed at the back buffer, with no lock held. */
    (*(LPDIRECTDRAWSURFACE *)&g_drawTarget)    = g_backBuffer;
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
        return ReportError(hr, "DirectInputCreate()");

    /* Mouse: exclusive, so the system pointer goes away and motion arrives
     * raw, and buffered, so it arrives as deltas rather than positions. */
    hr = IDirectInput_CreateDevice(g_dinput, g_guidSysMouse, &g_diMouse, NULL);
    if (hr)
        return ReportError(hr, "CreateDevice (mouse)");
    hr = IDirectInputDevice_SetDataFormat(g_diMouse, kFormatMouse);
    if (hr)
        return ReportError(hr, "SetDataFormat (mouse)");
    hr = IDirectInputDevice_SetCooperativeLevel(g_diMouse, hWnd,
                                                DISCL_EXCLUSIVE |
                                                DISCL_FOREGROUND);
    if (hr)
        return ReportError(hr, "SetCooperativeLevel (mouse)");
    hr = IDirectInputDevice_SetProperty(g_diMouse, DIPROP_BUFFERSIZE,
                                        &g_dipropBufferSize.diph);
    if (hr)
        return ReportError(hr, "Set buffer size (mouse)");

    /* Keyboard: non-exclusive. Taking it exclusively would take Alt-Tab too. */
    hr = IDirectInput_CreateDevice(g_dinput, g_guidSysKeyboard, &g_diKeyboard,
                                   NULL);
    if (hr)
        return ReportError(hr, "CreateDevice (keyboard)");
    hr = IDirectInputDevice_SetDataFormat(g_diKeyboard, kFormatKeyboard);
    if (hr)
        return ReportError(hr, "SetDataFormat (keyboard)");
    hr = IDirectInputDevice_SetCooperativeLevel(g_diKeyboard, hWnd,
                                                DISCL_NONEXCLUSIVE |
                                                DISCL_FOREGROUND);
    if (hr)
        return ReportError(hr, "SetCooperativeLevel (keyboard)");

    /* Rewind the two input cursors to the starts of their buffers.
     *
     * The three clears have memset's last two arguments SWAPPED -- a fill of
     * 0x100 (or 0x400) over zero bytes -- so nothing is cleared. That is why
     * the image loads the buffers and sizes into edi and eax and then stores
     * nothing: with a zero count VC6 drops the `rep stos` and keeps the setup.
     * Reproduced, since it is the original's behaviour and harmless (the
     * buffers are .bss, already zero). */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmemset-transposed-args"
#endif
    memset(g_keysBufferA, 0x100, 0);
    memset(g_keysBufferB, 0x100, 0);
    *(uint32_t *)&g_prevKeys = ((uintptr_t)g_keysBufferB);
    memset((void *)g_keyPressed, 0x400, 0);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    *(uint32_t *)&g_curKeys = ((uintptr_t)g_keysBufferA);
    return 1;
}


/* Unacquire, then Release, then forget. */
/* Written out in place for each device, as the original has it: the global
 * is re-read for each call rather than held through a pointer. */
#define RELEASE_DEVICE(dev)                  \
    if (dev) {                               \
        IDirectInputDevice_Unacquire(dev);   \
        IDirectInputDevice_Release(dev);     \
        (dev) = NULL;                        \
    }

void __cdecl ShutdownInput(void)
{
    /* Nothing was created, so there is nothing to give back. */
    if (!g_dinput)
        return;

    RELEASE_DEVICE(g_diMouse)
    RELEASE_DEVICE(g_diDevice3)
    RELEASE_DEVICE(g_diKeyboard)

    /* Unguarded, unlike the devices -- but it was tested at the top. */
    IDirectInput_Release(g_dinput);
    g_dinput = NULL;
}

void __cdecl AcquireMouse(void)
{
    if (!g_diMouse)
        return;
    /* Only a success is recorded. Failing is ordinary: an exclusive foreground
     * device cannot be acquired while another window has the focus. */
    if (IDirectInputDevice_Acquire(g_diMouse) == DI_OK)
        g_mouseAcquired = 1;
}

/* Read the mouse -- 0x00427070.
 *
 * BUFFERED, not polled state: InitInput sets a buffer size property on the
 * device, so this drains a queue of DIDEVICEOBJECTDATA one event at a time
 * until GetDeviceData reports none left. Each event is one axis or one button,
 * never a snapshot, which is why the switch exists and why the per-poll deltas
 * are cleared once at the top and then accumulated.
 *
 * That queue is also why tools/point.py has to close the loop on a screenshot:
 * what arrives here are relative deltas that Wine has already scaled, so a
 * computed absolute move overshoots. See CLAUDE.md.
 *
 * DIERR_INPUTLOST is expected, not exceptional -- it is what an alt-tab looks
 * like -- so it re-acquires and retries once. Anything else fails the poll and
 * leaves the state alone.
 *
 * The wheel does NOT set the moved flag, only X and Y do. Kept: something
 * downstream distinguishes "the pointer moved" from "the wheel turned".
 *
 * Reached from PollInput, which is `call PollMouse; jmp PollKeyboard`. The
 * per-event callee is UpdateMouseState below. */
static_assert(DIMOFS_X == 0 && DIMOFS_Y == 4 && DIMOFS_Z == 8, "axis offsets");
static_assert(DIMOFS_BUTTON0 == 12 && DIMOFS_BUTTON1 == 13
              && DIMOFS_BUTTON2 == 14, "button offsets");
static_assert((uint32_t)DIERR_INPUTLOST == 0x8007001Eu, "DIERR_INPUTLOST");

/* DirectInput marks a key or button down with the top bit of its byte. */
#define BUTTON_DOWN 0x80u
static_assert(sizeof(DIDEVICEOBJECTDATA) == 16, "DIDEVICEOBJECTDATA");





typedef int32_t (__cdecl *am2_clamp_fn)(int32_t v, int32_t lo, int32_t hi);

/* Run after every mouse event -- 0x00426F40, 8 call sites.
 *
 * The deltas PollMouse just wrote are relative, and this is what turns them
 * into a cursor: add, clamp to the screen rectangle, and pack the result into
 * the dword the rest of the game reads a position from. The clamp is inclusive
 * of the far edge minus one, so a 640-wide screen clamps to 639.
 *
 * Then the press bookkeeping. A button that is BOTH down and changed has just
 * gone down, and that stamps the position and the tick -- which is what makes
 * double-click and drag detection possible elsewhere. Only buttons 0 and 1
 * raise the moved flag; button 2 stamps its press and says nothing.
 *
 * The intermediate stores are kept. The original writes the unclamped sum to
 * the cursor globals before calling the clamp and the clamped value after, and
 * although nothing can observe the first write, removing it would be a change
 * to what the function does rather than to how it is written.
 *
 * One branch is not reproduced because it cannot be taken: after stamping
 * button 0 the original tests flags left by a compare it has already branched
 * on, so the `je` there always falls through. */
void __cdecl UpdateMouseState(void)
{
    uint32_t tick = GetTickCount();

    g_cursorX += g_mouseDX;
    g_cursorY += g_mouseDY;
    g_cursorX = Clamp(g_cursorX, g_screenClip.left, g_screenClip.right - 1);
    g_cursorY = Clamp(g_cursorY, g_screenClip.top, g_screenClip.bottom - 1);

    /* x first here schedules y's store first, as the image has it. */
    ((int16_t *)&g_cursorPoint)[0] = (int16_t)g_cursorX;
    ((int16_t *)&g_cursorPoint)[1] = (int16_t)g_cursorY;

    /* The three buttons are written out, not looped -- VC6 does not unroll
     * -- and the left one's whole condition is asked a second time for the
     * grab clear: the image branches on the change flag again there. */
    if ((&g_mouseButton)[0] && (&g_mouseChanged)[0]) {
        g_mousePress[0].point = g_cursorPoint;
        g_mousePress[0].tick  = tick;
    }
    if ((&g_mouseButton)[0] && (&g_mouseChanged)[0])
        g_mouseGrab = 0;
    if ((&g_mouseButton)[1] && (&g_mouseChanged)[1]) {
        g_mousePress[1].point = g_cursorPoint;
        g_mousePress[1].tick  = tick;
    }
    if ((&g_mouseButton)[2] && (&g_mouseChanged)[2]) {
        g_mousePress[2].point = g_cursorPoint;
        g_mousePress[2].tick  = tick;
    }

    if ((&g_mouseChanged)[0] || (&g_mouseChanged)[1])
        g_mouseMoved = 1;

    if (g_mouseDX || g_mouseDY || g_mouseMoved)
        *(int32_t *)&g_mouseActivity =
            *(const int32_t *)&g_gameClockMs;
}

/* One button event. `changed` is against the previous state rather than a
 * simple "went down", so a release marks it too -- and only a release clears
 * the claim the menus set. A macro, the button stored in each arm and READ
 * BACK for the comparison: the image stores the new state from a register it
 * then compares, where a `now` local let VC6 store the constant itself. */
#define MouseButton(n, data)                                                  \
    do {                                                                      \
        int32_t was_ = (&g_mouseButton)[n];                                   \
                                                                              \
        if ((data) & BUTTON_DOWN) {                                           \
            (&g_mouseButton)[n] = 1;                                          \
        } else {                                                              \
            (&g_mouseButton)[n] = 0;                                          \
            (&g_mouseClaimed)[n] = 0;                                         \
        }                                                                     \
        (&g_mouseChanged)[n] = ((&g_mouseButton)[n] != was_);                 \
    } while (0)

void __cdecl PollMouse(void)
{
    DIDEVICEOBJECTDATA od;
    DWORD              count = 1;

    if (!g_diMouse)
        return;
    if (!g_mouseAcquired)
        AcquireMouse();

#if defined(AM2_STANDALONE) && !defined(AM2_RELINK)   /* harness input, not the original */
    /* Once per poll, not once per event: input_pump expires timed holds, and
     * calling it inside the drain loop would retire a tap before the game had
     * seen its press. */
    input_pump();
#endif

    g_mouseDX = 0;
    g_mouseDY = 0;
    g_mouseDZ = 0;
    (&g_mouseChanged)[0] = 0;
    (&g_mouseChanged)[1] = 0;
    (&g_mouseChanged)[2] = 0;
    g_mouseMoved = 0;

    for (;;) {
        HRESULT hr = IDirectInputDevice_GetDeviceData(g_diMouse, sizeof(od),
                                                      &od, &count, 0);
        if ((uint32_t)hr == (uint32_t)DIERR_INPUTLOST) {
            g_mouseAcquired = 0;
            AcquireMouse();
            hr = IDirectInputDevice_GetDeviceData(g_diMouse, sizeof(od),
                                                  &od, &count, 0);
            if (hr != DI_OK) {
                g_mouseAcquired = 0;
                return;
            }
        }
        /* Asked again after a successful retry, as the image does: both
         * paths meet at one `cmp eax, esi`. */
        if (hr != DI_OK)
            return;

        if (count == 0) {
#if defined(AM2_STANDALONE) && !defined(AM2_RELINK)   /* harness input, not the original */
            /* Real events first, injected ones after, which is the order
             * dinput_hook.c appends them in -- injection adds to real input
             * rather than masking it. One at a time because that is the only
             * capacity this loop ever asks for. */
            count = input_take_events(AM2_DEV_MOUSE, &od, sizeof(od), 1, 0);
            if (count == 0)
                return;
#else
            return;
#endif
        }

        switch (od.dwOfs) {
        case DIMOFS_X:
            g_mouseMoved = 1;
            g_mouseDX = (int32_t)od.dwData;
            break;
        case DIMOFS_Y:
            g_mouseMoved = 1;
            g_mouseDY = (int32_t)od.dwData;
            break;
        case DIMOFS_Z:
            g_mouseDZ = (int32_t)od.dwData;
            break;
        case DIMOFS_BUTTON0: MouseButton(0, od.dwData); break;
        case DIMOFS_BUTTON1: MouseButton(1, od.dwData); break;
        case DIMOFS_BUTTON2: MouseButton(2, od.dwData); break;
        default:
            /* Every other offset falls straight through to the event hook,
             * which is what the jump table\'s default entry points at. */
            break;
        }

        UpdateMouseState();
    }
}

/* Read the keyboard -- 0x004272D0.
 *
 * The other end of the input channel from InitInput: that one creates the
 * device, this one reads it, every frame, through IDirectInputDevice. It is the
 * densest boundary function left in the image at 112 bytes per DirectX call.
 *
 * It was invisible until now. docs/functions.tsv runs it together with the
 * mouse poller, and between the two sits a jump table that linear disassembly
 * cannot get past, so the pair looked like one 944-byte function with five
 * scattered DirectInput calls. See tools/merges.py.
 *
 * DOUBLE-BUFFERED, and the swap is the first thing it does. Two adjacent
 * 256-byte buffers with two pointers into them; the pointers exchange, so what
 * was current becomes previous and the fresh read lands on top of the older
 * copy. Edge detection later is then just a byte compare between the two.
 *
 * A failed read is retried exactly once, through Acquire -- which is the normal
 * path after an alt-tab, not an error. If the retry fails the state is left as
 * it was rather than cleared, so keys do not spuriously release while the
 * window is inactive.
 *
 * MODIFIERS ARE MIRRORED, not merged. If either shift is down the other is made
 * to match, and likewise for control and alt, so the rest of the game can test
 * one scancode without caring which key the player used. The direction matters:
 * the left key wins if it is down, otherwise the right one is copied leftward.
 *
 * The repeat is 250 ms to the first repeat and 150 ms between, both measured
 * against GetTickCount. Note what the not-yet-due branch does NOT do: it leaves
 * the pressed flag alone rather than clearing it, so a caller that has not
 * consumed the previous press still sees it. Only a key that is physically up
 * clears the flag.
 *
 * The devices come from the globals rather than from an import, so the calls go
 * through whatever src/inject/dinput_hook.c wrapped them with -- which is what
 * makes injected input keep working. See the note at the top of this file. */
static_assert(DIK_LSHIFT == 0x2A && DIK_RSHIFT == 0x36, "shift scancodes");
static_assert(DIK_LCONTROL == 0x1D && DIK_RCONTROL == 0x9D, "control scancodes");
static_assert(DIK_LMENU == 0x38 && DIK_RMENU == 0xB8, "alt scancodes");

#define KEY_STATES        256
#define KEY_DOWN          BUTTON_DOWN
#define REPEAT_FIRST_MS   250u
#define REPEAT_NEXT_MS    150u


/* If `a` is down, make `b` match it; otherwise if `b` is down, make `a` match.
 * The original writes this out three times. */
static inline void MirrorModifier(uint32_t a, uint32_t b)
{
    if (g_curKeys[a] & KEY_DOWN)
        g_curKeys[b] = g_curKeys[a];
    else if (g_curKeys[b] & KEY_DOWN)
        g_curKeys[a] = g_curKeys[b];
}

void __cdecl PollKeyboard(void)
{
    uint8_t  *keys;
    uint32_t  now;
    int32_t   i;

    if (!g_diKeyboard)
        return;

    /* Exchange the buffers, then read into what is now the current one. */
    keys       = g_prevKeys;
    g_prevKeys = g_curKeys;
    g_curKeys  = keys;

    if (IDirectInputDevice_GetDeviceState(g_diKeyboard, KEY_STATES, keys) != DI_OK) {
        if (IDirectInputDevice_Acquire(g_diKeyboard) != DI_OK)
            return;
        if (IDirectInputDevice_GetDeviceState(g_diKeyboard, KEY_STATES,
                                              g_curKeys) != DI_OK)
            return;
    }

#if defined(AM2_STANDALONE) && !defined(AM2_RELINK)   /* harness input, not the original */
    /* Before MirrorModifier, so an injected shift mirrors the way a real one
     * does -- the hook overlays inside GetDeviceState, which is upstream of
     * everything below. */
    input_overlay_keyboard(g_curKeys, KEY_STATES);
#endif
    MirrorModifier(DIK_LSHIFT, DIK_RSHIFT);
    MirrorModifier(DIK_LCONTROL, DIK_RCONTROL);
    MirrorModifier(DIK_LMENU, DIK_RMENU);

    now = GetTickCount();
    for (i = 0; i < KEY_STATES; i++) {
        /* The key buffers are indexed by the scancode as a BYTE (the image
         * masks the counter with 0xFF), the two per-key arrays by the int. */
        uint8_t k = (uint8_t)i;

        if (g_curKeys[k] & KEY_DOWN) {
            /* Read as signed chars -- DirectInput's `char[256]` state -- which
             * is why the image XORs whole registers rather than bytes. */
            if (((int8_t)g_prevKeys[k] ^ (int8_t)g_curKeys[k]) & KEY_DOWN) {
                /* Newly down this frame. */
                g_keyPressed[i] = 1;
                g_keyRepeat[i]  = now + REPEAT_FIRST_MS;
            } else if (now > g_keyRepeat[i]) {
                g_keyPressed[i] = 1;
                g_keyRepeat[i]  = now + REPEAT_NEXT_MS;
            }
        } else
            g_keyPressed[i] = 0;
    }
}

int device_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_MOUSE_EVENT, (const void *)UpdateMouseState,
                        "UpdateMouseState", 8);
    rc |= patch_replace(ADDR_POLL_MOUSE, (const void *)PollMouse,
                        "PollMouse", 0);
    rc |= patch_replace(ADDR_POLL_KEYBOARD, (const void *)PollKeyboard,
                        "PollKeyboard", 0);

    rc |= patch_replace(ADDR_INIT_DIRECTDRAW, (const void *)InitDirectDraw,
                        "InitDirectDraw", 1);
    rc |= patch_replace(ADDR_INIT_INPUT, (const void *)InitInput,
                        "InitInput", 1);
    rc |= patch_replace(ADDR_SHUTDOWN_INPUT, (const void *)ShutdownInput,
                        "ShutdownInput", 0);
    rc |= patch_replace(ADDR_ACQUIRE_MOUSE, (const void *)AcquireMouse,
                        "AcquireMouse", 0);
    return rc;
}
