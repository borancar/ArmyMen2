/* platform.h -- what the pieces of this layer share with each other.
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
 *   kernel32crt.cpp  what the MSVC 6 CRT inside the original imports:
 *                 heaps, virtual memory, files, the environment, code
 *                 pages, time. Only the PE loader (ArmyMen2's src/hybrid) reaches it.
 *   crt.cpp       MSVC's CRT names and Windows-to-native path translation.
 *
 * Everything below the window the game sees is emulated to the game; only
 * sdl.cpp talks to the host.
 */
#ifndef W32L_PLATFORM_H
#define W32L_PLATFORM_H

#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the application ---------------------------------------------------- */

/* What the application tells the layer about itself. The layer provides
 * main(), which calls the application's WinMain; this is the rest of what
 * it needs from it. Define w32l_app_info once in the application; the
 * layer's own definition is weak and names a generic program. */
typedef struct W32L_AppInfo {
    const char *name;           /* the window's title when CreateWindow
                                 * gives none, and SDL's application name */
    const char *version;        /* SDL's application version */
    const char *identifier;     /* SDL's reverse-DNS application id */
    const char *module_path;    /* what GetModuleFileNameA answers -- a fixed
                                 * Windows path, so a program that copies it
                                 * (the CRT's startup does) sees the same
                                 * bytes on every host -- and, after its last
                                 * backslash, the program name GetCommandLineA
                                 * starts with */
} W32L_AppInfo;
extern const W32L_AppInfo w32l_app_info;

/* ---- logging ------------------------------------------------------------ */

/* The platform's own diagnostics: stderr, prefixed, and never the game's
 * log -- that one is compared against the original's and must not gain
 * lines the original never wrote. W32L_PLATFORM_QUIET=1 silences it. */
void w32l_plat_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The same, only under W32L_PLATFORM_DEBUG=1: palette and surface traffic. */
void w32l_plat_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- the screen (sdl.cpp) ---------------------------------------------- */

/* Create or resize the host window for a client area of w x h. Called by
 * CreateWindowExA and by SetWindowPos when the size changes. */
void w32l_host_window_open(const char *title, int32_t w, int32_t h);
void w32l_host_window_close(void);
void w32l_host_window_set_title(const char *title);
int32_t w32l_host_window_is_minimised(void);
void w32l_host_window_raise(void);
void w32l_host_window_position(int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/* Set the logical display size the game asked DirectDraw for. */
void w32l_host_display_mode(int32_t w, int32_t h);
void w32l_host_display_size(int32_t *w, int32_t *h);

/* Put one 8-bit frame on the screen through a 256-entry palette. */
void w32l_host_present(const uint8_t *pixels, int32_t pitch, int32_t w,
                      int32_t h, const PALETTEENTRY *palette);

/* Block until the next vertical blank, as a DirectDraw Flip does. The
 * game's pace -- its walk speed, its turn rate -- is its frame rate, and
 * on the hardware it was written for that was the display's. W32L_FPS
 * overrides the 60 Hz default; 0 disables the wait. */
void w32l_host_wait_vblank(void);

/* Tear the host down and end the process: what ExitProcess does. */
void w32l_host_exit(int32_t code) __attribute__((noreturn));

/* ---- lockstep (sdl.cpp) ------------------------------------------------- */

/* W32L_LOCKSTEP=1: every source of nondeterminism the platform owns is
 * replaced by one it counts. Time is a virtual clock that advances by a
 * fixed step on every pump (W32L_LOCKSTEP_MS, default 1000/60) and by what
 * Sleep asks for on the game thread; multimedia timers fire from the pump
 * when the clock passes them, not from a host thread; the sound is mixed
 * on the game thread, one step's worth per pump; input comes from the
 * W32L_REPLAY script by pump number and from nowhere else; and every frame
 * presented is hashed into W32L_FRAMELOG. Two binaries fed the same script
 * then either match frame for frame or name the first frame that does
 * not, which is what tools/lockstep.sh reports. */
int32_t  w32l_host_lockstep(void);
/* Nanoseconds: the virtual clock in lockstep, the host's otherwise. */
uint64_t w32l_host_clock_ns(void);
uint32_t w32l_host_pump_number(void);
int32_t  w32l_trace_window(void);        /* W32L_TRACE_FROM..W32L_TRACE_TO holds the current pump */   /* lockstep: the pump about to run, 0 before the first */

/* The deterministic heap, src/fixedheap.cpp: HeapAlloc's blocks
 * and VirtualAlloc's reservations at addresses that depend only on the
 * sequence of calls, so two games over this platform allocate alike.
 * On unless W32L_FIXED_HEAP=0. */
int32_t  w32l_fixed_heap_on(void);
void    *w32l_fixed_alloc(size_t n, uint32_t pc);
void    *w32l_fixed_realloc(void *p, size_t n, uint32_t pc);
void     w32l_fixed_free(void *p);
size_t   w32l_fixed_size(const void *p);
int32_t  w32l_fixed_owns(const void *p);
void    *w32l_fixed_reserve(size_t size);
int32_t  w32l_fixed_release(void *addr);
int32_t  w32l_fixed_is_reservation(const void *addr);
uint8_t *w32l_fixed_heap_base(void);
uint32_t w32l_fixed_heap_used(void);
uint32_t w32l_fixed_heap_limit(void);
uint8_t *w32l_fixed_resv_base(void);
uint32_t w32l_fixed_resv_used(void);
void     w32l_fixed_resv_mark(uint32_t used);
void     w32l_fixed_stats(uint32_t *allocs, uint32_t *frees, void **free_head);
void     w32l_fixed_frame_check(void);
/* Sleep on the game thread in lockstep: the clock moves, the host does not. */
void     w32l_host_clock_advance_ms(uint32_t ms);
int32_t  w32l_host_on_game_thread(void);
/* Put the game's own cursor at a screen point, as the control socket's
 * `cursor` does: the runtime installs it, since only the game side knows
 * the globals. Used by the replay's `cursor` line. */
extern void (*w32l_host_cursor_set)(int32_t x, int32_t y);

/* Show or hide the host's pointer. */
void w32l_host_cursor_visible(int32_t visible);

/* Drain host events into the message queue and the input devices. */
void w32l_host_pump(void);

/* Called at the top of every pump, on the game thread, when set. The
 * development binary hangs its savestates on it. */
extern void (*w32l_host_frame_hook)(void);

/* Under W32L_DEVTOOLS F5 and F9 never reach the program: they call this,
 * when set, with 1 for F5 and 2 for F9 -- the development binary's quick
 * save and reload. */
extern void (*w32l_host_hotkey)(int32_t which);

/* Run a line of Lua on the game thread, at a pump boundary, from the step
 * socket's `lua` action -- so a give-weapon or poke lands on the SAME pump in
 * two lockstepped games and they stay frame-exact, where the control socket's
 * async `lua` would race the pump. The dev runtimes install it (only they
 * link the console); null in the retail build, where the verb just logs. */
extern const char *(*w32l_host_lua)(const char *code);

/* Keyboard state as DirectInput scancodes, 256 bytes, 0x80 when down. */
void w32l_host_keyboard_state(uint8_t *out);

/* ---- sound and timers (sdl.cpp) ------------------------------------------- */

/* Open the host's playback device at `rate` frames a second, stereo float.
 * `mix` is called from the host's audio thread whenever it wants more:
 * it must write `frames` interleaved stereo frames into `out`. Returns 0
 * when there is no device, which is DirectSoundCreate's DSERR_NODRIVER. */
typedef void (*w32l_audio_mix_fn)(void *ud, float *out, int32_t frames);
int32_t w32l_host_audio_open(int32_t rate, w32l_audio_mix_fn mix, void *ud);
void    w32l_host_audio_close(void);

/* A multimedia timer: `fn` runs on a host thread every `ms` (periodic) or
 * once. Returns 0 when none could be made. */
typedef void (*w32l_timer_fn)(void *ud);
uint32_t w32l_host_timer_add(uint32_t ms, int32_t periodic, w32l_timer_fn fn, void *ud);
void     w32l_host_timer_remove(uint32_t id);

/* crt.cpp's fopen, with the Windows path translated (see w32l_native_path). */
FILE *w32l_fopen(const char *path, const char *mode);

/* ---- the window and its queue (user32.cpp) ------------------------------ */

/* The one window the game creates, or NULL. */
HWND w32l_the_window(void);
WNDPROC w32l_window_proc(HWND hwnd);

/* Queue a message from any thread. */
void w32l_queue_post(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* Event pump hooks called by sdl.cpp. */
void w32l_window_on_close(void);
void w32l_window_on_focus(int32_t gained);
void w32l_window_on_char(uint32_t ch);
void w32l_window_on_key(int32_t down, uint32_t vk);
void w32l_window_on_moved(int32_t x, int32_t y);

/* ---- input (dinput.cpp) ---------------------------------------------------- */

void w32l_di_mouse_motion(int32_t dx, int32_t dy);
/* One axis event, zero included: DIMOFS_X or DIMOFS_Y. */
void w32l_di_mouse_axis(uint32_t ofs, int32_t delta);
void w32l_di_mouse_button(int32_t button, int32_t down);
void w32l_di_mouse_wheel(int32_t delta);
/* Motion events the game has not yet drained from the mouse's buffer. */
int32_t w32l_di_mouse_pending(void);
/* Any events at all still queued. */
int32_t w32l_di_mouse_queued(void);

/* Where the game's own cursor is, or NULL when nobody can say. The game
 * accumulates DirectInput deltas into a cursor of its own; the host keeps
 * that cursor under the pointer by measuring each delta from where the
 * cursor actually is, which only the game side knows. runtime.cpp installs
 * this in the native build. */
extern void (*w32l_host_cursor_query)(int32_t *x, int32_t *y);

/* ---- palettes and DCs (gdi32.cpp, ddraw.cpp) -------------------------------- */

/* The palette the screen is showing: what DirectDraw's primary has attached
 * or, failing that, what GDI last realised. Both modules write it, the
 * present reads it. */
extern PALETTEENTRY w32l_screen_palette[256];

/* The surface a device context draws on. A DC over a DirectDraw surface
 * carries that surface's pixels; the screen DC carries the primary's. */
typedef struct W32L_DCTarget {
    uint8_t      *pixels;
    int32_t       pitch;
    int32_t       width;
    int32_t       height;
    const PALETTEENTRY *palette;   /* 256 entries, or NULL */
    void        (*changed)(void);  /* called after a draw, or NULL */
} W32L_DCTarget;

HDC  w32l_dc_create(const W32L_DCTarget *target);
void w32l_dc_destroy(HDC dc);

/* DirectDraw answers the screen DC's target: the primary surface. Returns 0
 * with no primary, in which case GetDC hands out a DC that draws nowhere. */
int32_t w32l_ddraw_primary_target(W32L_DCTarget *out);

/* Nearest palette index to an RGB triple, over 256 entries. */
uint8_t w32l_palette_nearest(const PALETTEENTRY *pal, uint8_t r, uint8_t g, uint8_t b);

/* ---- the import table (kernel32.cpp) ------------------------------------ */

/* What LoadLibraryA and GetProcAddress answer from: every Win32 entry the
 * platform layer provides, by module and name. */
typedef struct W32L_Export {
    const char *module;
    const char *name;
    const void *fn;
} W32L_Export;

/* The table by module and name, NULL when absent. */
const void *w32l_export_lookup(const char *module, const char *name);

/* Called on every thread CreateThread makes, before its start routine,
 * when set. The PE loader gives each thread its own TEB through it. */
extern void (*w32l_thread_attach)(void);

/* What GetCommandLineA answers: the loader sets the arguments after the
 * program name, which the original's CRT strips off again for WinMain. */
void w32l_set_command_line(const char *args);

/* A plain function for the IAT, where windows.h has an inline. */
LONG WINAPI w32l_InterlockedExchange(volatile LONG *target, LONG value);

#ifdef __cplusplus
}
#endif

#endif /* W32L_PLATFORM_H */
