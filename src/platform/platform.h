/* platform.h -- what the pieces of src/platform share with each other.
 *
 * The layout of the layer, outermost first:
 *
 *   sdl.cpp       main(), the SDL window, renderer and event pump, and the
 *                 one place pixels reach the screen. SDL is the outermost
 *                 layer; nothing above it knows SDL exists.
 *   user32.cpp    the window, the message queue, cursor and metrics.
 *   gdi32.cpp     device contexts, palettes and fonts (stb_truetype).
 *   ddraw.cpp     DirectDraw: surfaces are plain 8-bit buffers, and the
 *                 primary's contents go through its palette to sdl.cpp.
 *   dinput.cpp    DirectInput: the keyboard state array and the buffered
 *                 mouse queue, both fed from sdl.cpp's pump.
 *   dsound.cpp    DirectSound and winmm.
 *   dplay.cpp     DirectPlay objects that decline everything.
 *   kernel32.cpp  time, modules and the import table, threads, events,
 *                 memory probes, the exception hook, the registry, drives.
 *   crt.cpp       MSVC's CRT names and Windows-to-native path translation.
 *
 * Everything below the window the game sees is emulated to the game; only
 * sdl.cpp talks to the host.
 */
#ifndef AM2_PLATFORM_H
#define AM2_PLATFORM_H

#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- logging ------------------------------------------------------------ */

/* The platform's own diagnostics: stderr, prefixed, and never the game's
 * log -- that one is compared against the original's and must not gain
 * lines the original never wrote. AM2_PLATFORM_QUIET=1 silences it. */
void am2_plat_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The same, only under AM2_PLATFORM_DEBUG=1: palette and surface traffic. */
void am2_plat_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- the screen (sdl.cpp) ---------------------------------------------- */

/* Create or resize the host window for a client area of w x h. Called by
 * CreateWindowExA and by SetWindowPos when the size changes. */
void am2_host_window_open(const char *title, int32_t w, int32_t h);
void am2_host_window_close(void);
void am2_host_window_set_title(const char *title);
int32_t am2_host_window_is_minimised(void);
void am2_host_window_raise(void);
void am2_host_window_position(int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/* Set the logical display size the game asked DirectDraw for. */
void am2_host_display_mode(int32_t w, int32_t h);
void am2_host_display_size(int32_t *w, int32_t *h);

/* Put one 8-bit frame on the screen through a 256-entry palette. */
void am2_host_present(const uint8_t *pixels, int32_t pitch, int32_t w,
                      int32_t h, const PALETTEENTRY *palette);

/* Block until the next vertical blank, as a DirectDraw Flip does. The
 * game's pace -- its walk speed, its turn rate -- is its frame rate, and
 * on the hardware it was written for that was the display's. AM2_FPS
 * overrides the 60 Hz default; 0 disables the wait. */
void am2_host_wait_vblank(void);

/* Show or hide the host's pointer. */
void am2_host_cursor_visible(int32_t visible);

/* Drain host events into the message queue and the input devices. */
void am2_host_pump(void);

/* Keyboard state as DirectInput scancodes, 256 bytes, 0x80 when down. */
void am2_host_keyboard_state(uint8_t *out);

/* ---- sound and timers (sdl.cpp) ------------------------------------------- */

/* Open the host's playback device at `rate` frames a second, stereo float.
 * `mix` is called from the host's audio thread whenever it wants more:
 * it must write `frames` interleaved stereo frames into `out`. Returns 0
 * when there is no device, which is DirectSoundCreate's DSERR_NODRIVER. */
typedef void (*am2_audio_mix_fn)(void *ud, float *out, int32_t frames);
int32_t am2_host_audio_open(int32_t rate, am2_audio_mix_fn mix, void *ud);
void    am2_host_audio_close(void);

/* A multimedia timer: `fn` runs on a host thread every `ms` (periodic) or
 * once. Returns 0 when none could be made. */
typedef void (*am2_timer_fn)(void *ud);
uint32_t am2_host_timer_add(uint32_t ms, int32_t periodic, am2_timer_fn fn, void *ud);
void     am2_host_timer_remove(uint32_t id);

/* crt.cpp's fopen, with the Windows path translated (see am2_native_path). */
FILE *am2_fopen(const char *path, const char *mode);

/* ---- the window and its queue (user32.cpp) ------------------------------ */

/* The one window the game creates, or NULL. */
HWND am2_the_window(void);
WNDPROC am2_window_proc(HWND hwnd);

/* Queue a message from any thread. */
void am2_queue_post(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* Event pump hooks called by sdl.cpp. */
void am2_window_on_close(void);
void am2_window_on_focus(int32_t gained);
void am2_window_on_char(uint32_t ch);
void am2_window_on_key(int32_t down, uint32_t vk);
void am2_window_on_moved(int32_t x, int32_t y);

/* ---- input (dinput.cpp) ---------------------------------------------------- */

void am2_di_mouse_motion(int32_t dx, int32_t dy);
/* One axis event, zero included: DIMOFS_X or DIMOFS_Y. */
void am2_di_mouse_axis(uint32_t ofs, int32_t delta);
void am2_di_mouse_button(int32_t button, int32_t down);
void am2_di_mouse_wheel(int32_t delta);
/* Motion events the game has not yet drained from the mouse's buffer. */
int32_t am2_di_mouse_pending(void);
/* Any events at all still queued. */
int32_t am2_di_mouse_queued(void);

/* Where the game's own cursor is, or NULL when nobody can say. The game
 * accumulates DirectInput deltas into a cursor of its own; the host keeps
 * that cursor under the pointer by measuring each delta from where the
 * cursor actually is, which only the game side knows. runtime.cpp installs
 * this in the native build. */
extern void (*am2_host_cursor_query)(int32_t *x, int32_t *y);

/* ---- palettes and DCs (gdi32.cpp, ddraw.cpp) -------------------------------- */

/* The palette the screen is showing: what DirectDraw's primary has attached
 * or, failing that, what GDI last realised. Both modules write it, the
 * present reads it. */
extern PALETTEENTRY am2_screen_palette[256];

/* The surface a device context draws on. A DC over a DirectDraw surface
 * carries that surface's pixels; the screen DC carries the primary's. */
typedef struct AM2_DCTarget {
    uint8_t      *pixels;
    int32_t       pitch;
    int32_t       width;
    int32_t       height;
    const PALETTEENTRY *palette;   /* 256 entries, or NULL */
    void        (*changed)(void);  /* called after a draw, or NULL */
} AM2_DCTarget;

HDC  am2_dc_create(const AM2_DCTarget *target);
void am2_dc_destroy(HDC dc);

/* DirectDraw answers the screen DC's target: the primary surface. Returns 0
 * with no primary, in which case GetDC hands out a DC that draws nowhere. */
int32_t am2_ddraw_primary_target(AM2_DCTarget *out);

/* Nearest palette index to an RGB triple, over 256 entries. */
uint8_t am2_palette_nearest(const PALETTEENTRY *pal, uint8_t r, uint8_t g, uint8_t b);

/* ---- the import table (kernel32.cpp) ------------------------------------ */

/* What LoadLibraryA and GetProcAddress answer from: every Win32 entry the
 * platform layer provides, by module and name. */
typedef struct AM2_Export {
    const char *module;
    const char *name;
    const void *fn;
} AM2_Export;

#ifdef __cplusplus
}
#endif

#endif /* AM2_PLATFORM_H */
