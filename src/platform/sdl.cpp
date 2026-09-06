/* sdl.cpp -- the outermost layer: main(), the host window, the renderer,
 * the event pump and the one place a frame reaches the screen.
 *
 * Nothing above this file knows SDL exists. It answers the requests
 * platform.h declares -- open a window this big, present these 8-bit
 * pixels through this palette, drain your events -- and translates the
 * host's events into what user32.cpp and dinput.cpp expect.
 *
 *   AM2_SCALE=N     integer window scale (default: the largest that fits)
 *   AM2_RESIZABLE=1 a free-size window; the default is fixed-size, which a
 *                   tiling compositor floats
 *   AM2_VSYNC=1     also wait for the display's vertical blank on present
 *                   (the clock paces the frame either way; AM2_FPS sets it)
 *   AM2_AUDIO_DUMP=<file>  write the mixed audio as raw stereo float
 */
#include "platform.h"
#include <dinput.h>

#include <SDL3/SDL.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../game/win32/winmain.h"

/* ---- state -------------------------------------------------------------------- */

static SDL_Window   *am2_sdl_window;
static SDL_Renderer *am2_sdl_renderer;
static SDL_Texture  *am2_sdl_texture;
static int32_t       am2_disp_w = 640, am2_disp_h = 480;
static int32_t       am2_client_w, am2_client_h;
static int32_t       am2_tex_w, am2_tex_h;
static int32_t       am2_cursor_shown = 1;
static int32_t       am2_mouse_last_x, am2_mouse_last_y;
static int32_t       am2_mouse_seen;
static int32_t       am2_scale;
static uint8_t       am2_keys[256];

/* ---- the window ---------------------------------------------------------------- */

static int32_t am2_pick_scale(int32_t w, int32_t h)
{
    const char *env = getenv("AM2_SCALE");
    SDL_DisplayID did;
    SDL_Rect      bounds;
    int32_t       s;

    if (env && atoi(env) > 0)
        return atoi(env);
    did = SDL_GetPrimaryDisplay();
    if (!did || !SDL_GetDisplayUsableBounds(did, &bounds))
        return 1;
    s = 1;
    while ((s + 1) * w <= bounds.w - 32 && (s + 1) * h <= bounds.h - 96)
        s++;
    return s;
}

static void am2_texture_fit(void)
{
    if (am2_sdl_texture && am2_tex_w == am2_disp_w && am2_tex_h == am2_disp_h)
        return;
    if (am2_sdl_texture)
        SDL_DestroyTexture(am2_sdl_texture);
    am2_sdl_texture = SDL_CreateTexture(am2_sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        am2_disp_w, am2_disp_h);
    if (am2_sdl_texture)
        SDL_SetTextureScaleMode(am2_sdl_texture, SDL_SCALEMODE_NEAREST);
    am2_tex_w = am2_disp_w;
    am2_tex_h = am2_disp_h;
    SDL_SetRenderLogicalPresentation(am2_sdl_renderer, am2_disp_w, am2_disp_h,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

void am2_host_window_open(const char *title, int32_t w, int32_t h)
{
    am2_client_w = w;
    am2_client_h = h;
    if (!am2_sdl_window) {
        const char *vs = getenv("AM2_VSYNC");
        const char *rs = getenv("AM2_RESIZABLE");
        int32_t     resizable = rs && *rs == '1';

        am2_scale = am2_pick_scale(w, h);
        /* Fixed-size by default: a toplevel whose minimum and maximum sizes
         * are equal is what a tiling compositor (sway, i3, Hyprland) floats
         * rather than tiles, and an integer-scaled 640x480 is what the game
         * wants anyway. AM2_RESIZABLE=1 gives the old free-size window,
         * letterboxed. */
        am2_sdl_window = SDL_CreateWindow(title ? title : "Army Men II",
                                          w * am2_scale, h * am2_scale,
                                          resizable ? SDL_WINDOW_RESIZABLE : 0);
        if (!am2_sdl_window) {
            fprintf(stderr, "platform: SDL_CreateWindow: %s\n", SDL_GetError());
            exit(1);
        }
        if (!resizable) {
            SDL_SetWindowMinimumSize(am2_sdl_window, w * am2_scale, h * am2_scale);
            SDL_SetWindowMaximumSize(am2_sdl_window, w * am2_scale, h * am2_scale);
        }
        am2_sdl_renderer = SDL_CreateRenderer(am2_sdl_window, NULL);
        if (!am2_sdl_renderer) {
            fprintf(stderr, "platform: SDL_CreateRenderer: %s\n", SDL_GetError());
            exit(1);
        }
        /* The clock paces the game (am2_host_wait_vblank), not the display:
         * a present that blocks on the compositor's vertical blank stacks a
         * second wait on top of that one, and an occluded or unfocused
         * window can be throttled to a frame a second, which the game reads
         * as huge time steps. AM2_VSYNC=1 turns the display's wait back on. */
        SDL_SetRenderVSync(am2_sdl_renderer, (vs && *vs == '1') ? 1 : 0);
        SDL_StartTextInput(am2_sdl_window);
        am2_plat_log("window %dx%d at scale %d, renderer %s", w, h, am2_scale,
                     SDL_GetRendererName(am2_sdl_renderer));
    } else {
        if (title)
            SDL_SetWindowTitle(am2_sdl_window, title);
        if (!(SDL_GetWindowFlags(am2_sdl_window) & SDL_WINDOW_RESIZABLE)) {
            /* Keep the fixed-size hint tracking the mode. */
            SDL_SetWindowMinimumSize(am2_sdl_window, w * am2_scale, h * am2_scale);
            SDL_SetWindowMaximumSize(am2_sdl_window, w * am2_scale, h * am2_scale);
        }
        SDL_SetWindowSize(am2_sdl_window, w * am2_scale, h * am2_scale);
    }
    am2_texture_fit();
    SDL_SetRenderDrawColor(am2_sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(am2_sdl_renderer);
    SDL_RenderPresent(am2_sdl_renderer);
}

void am2_host_window_close(void)
{
    if (am2_sdl_texture)
        SDL_DestroyTexture(am2_sdl_texture);
    if (am2_sdl_renderer)
        SDL_DestroyRenderer(am2_sdl_renderer);
    if (am2_sdl_window)
        SDL_DestroyWindow(am2_sdl_window);
    am2_sdl_texture = NULL;
    am2_sdl_renderer = NULL;
    am2_sdl_window = NULL;
}

void am2_host_window_set_title(const char *title)
{
    if (am2_sdl_window)
        SDL_SetWindowTitle(am2_sdl_window, title);
}

int32_t am2_host_window_is_minimised(void)
{
    return am2_sdl_window &&
           (SDL_GetWindowFlags(am2_sdl_window) & SDL_WINDOW_MINIMIZED) != 0;
}

void am2_host_window_raise(void)
{
    if (am2_sdl_window)
        SDL_RaiseWindow(am2_sdl_window);
}

/* The window sits at the origin of the screen the game sees. That screen
 * IS the window's client area -- DirectDraw's primary is exactly it -- so
 * the windowed path, which blits the back buffer to the primary at the
 * window's screen position, lands at (0, 0). Where the host has put the
 * window on the desktop is nobody's business above this file. */
void am2_host_window_position(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = 0;
    *y = 0;
    *w = am2_client_w;
    *h = am2_client_h;
}

void am2_host_display_mode(int32_t w, int32_t h)
{
    am2_disp_w = w;
    am2_disp_h = h;
    if (am2_sdl_renderer)
        am2_texture_fit();
}

void am2_host_display_size(int32_t *w, int32_t *h)
{
    *w = am2_disp_w;
    *h = am2_disp_h;
}

void am2_host_cursor_visible(int32_t visible)
{
    if (visible == am2_cursor_shown)
        return;
    am2_cursor_shown = visible;
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

/* ---- lockstep: the clock ---------------------------------------------------------- */

static pthread_t am2_game_thread;
static uint64_t  am2_clock_ns;          /* the virtual clock */
static uint64_t  am2_step_ns;           /* one pump's worth of it */
static uint32_t  am2_pump_count;        /* the replay's frame number */
static uint32_t  am2_present_count;     /* the frame log's index */

int32_t am2_host_lockstep(void)
{
    static int32_t on = -1;
    if (on < 0) {
        const char *l = getenv("AM2_LOCKSTEP");
        const char *ms = getenv("AM2_LOCKSTEP_MS");
        on = l && *l && *l != '0';
        am2_step_ns = ms && *ms ? (uint64_t)(atof(ms) * 1000000.0) : 1000000000ULL / 60;
    }
    return on;
}

uint64_t am2_host_clock_ns(void)
{
    return am2_host_lockstep() ? am2_clock_ns : SDL_GetTicksNS();
}

int32_t am2_host_on_game_thread(void)
{
    return pthread_equal(pthread_self(), am2_game_thread);
}

static void am2_timers_fire_due(void);

void am2_host_clock_advance_ms(uint32_t ms)
{
    am2_plat_debug("lockstep: sleep %u ms at pump %u", (unsigned)ms, (unsigned)am2_pump_count);
    am2_clock_ns += (uint64_t)ms * 1000000ULL;
    am2_timers_fire_due();
}

/* ---- lockstep: the frame log ---------------------------------------------------- */

static FILE    *am2_framelog;
static int32_t  am2_framelog_tried;
static char     am2_framedump_path[1024];   /* a pending dump of the next present */

static int32_t am2_write_ppm(const char *path, const uint8_t *pixels, int32_t pitch,
                             int32_t w, int32_t h, const PALETTEENTRY *palette)
{
    FILE   *fh = fopen(path, "wb");
    int32_t x, y;

    if (!fh)
        return 0;
    fprintf(fh, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const PALETTEENTRY *e = &palette[pixels[y * pitch + x]];
            fputc(e->peRed, fh); fputc(e->peGreen, fh); fputc(e->peBlue, fh);
        }
    fclose(fh);
    return 1;
}

static void am2_step_frame(const uint8_t *pixels, int32_t pitch, int32_t w,
                           int32_t h, const PALETTEENTRY *palette);

static uint64_t am2_fnv(uint64_t h, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t am2_frame_hash(const uint8_t *pixels, int32_t pitch, int32_t w,
                               int32_t h, const PALETTEENTRY *palette)
{
    uint64_t hash = 14695981039346656037ULL;
    int32_t  y;
    for (y = 0; y < h; y++)
        hash = am2_fnv(hash, pixels + y * pitch, (size_t)w);
    return am2_fnv(hash, (const uint8_t *)palette, 256 * sizeof *palette);
}

static void am2_frame_record(const uint8_t *pixels, int32_t pitch, int32_t w,
                             int32_t h, const PALETTEENTRY *palette)
{
    if (!am2_framelog_tried) {
        const char *path = getenv("AM2_FRAMELOG");
        const char *at = getenv("AM2_FRAMEDUMP_AT");
        const char *to = getenv("AM2_FRAMEDUMP_TO");
        am2_framelog_tried = 1;
        if (path && *path)
            am2_framelog = !strcmp(path, "-") ? stderr : fopen(path, "w");
        if (at && *at && to && *to && (uint32_t)atoi(at) == am2_present_count)
            snprintf(am2_framedump_path, sizeof am2_framedump_path, "%s", to);
    } else {
        const char *at = getenv("AM2_FRAMEDUMP_AT");
        const char *to = getenv("AM2_FRAMEDUMP_TO");
        if (at && *at && to && *to && (uint32_t)atoi(at) == am2_present_count)
            snprintf(am2_framedump_path, sizeof am2_framedump_path, "%s", to);
    }
    if (am2_framelog) {
        uint64_t hash = am2_frame_hash(pixels, pitch, w, h, palette);
        fprintf(am2_framelog, "%u %u %016llx\n", (unsigned)am2_present_count,
                (unsigned)am2_pump_count, (unsigned long long)hash);
        fflush(am2_framelog);
    }
    if (am2_framedump_path[0]) {
        if (am2_write_ppm(am2_framedump_path, pixels, pitch, w, h, palette))
            am2_plat_log("frame %u written to %s", (unsigned)am2_present_count, am2_framedump_path);
        am2_framedump_path[0] = 0;
    }
    am2_step_frame(pixels, pitch, w, h, palette);
    am2_present_count++;
}

/* ---- the frame ---------------------------------------------------------------------- */

void am2_host_present(const uint8_t *pixels, int32_t pitch, int32_t w,
                      int32_t h, const PALETTEENTRY *palette)
{
    uint32_t  lut[256];
    void     *dst;
    int       dstPitch;
    int32_t   x, y;

    am2_frame_record(pixels, pitch, w, h, palette);
    if (!am2_sdl_renderer)
        return;
    am2_texture_fit();
    if (!am2_sdl_texture)
        return;
    for (x = 0; x < 256; x++)
        lut[x] = 0xFF000000u | ((uint32_t)palette[x].peRed << 16) |
                 ((uint32_t)palette[x].peGreen << 8) | palette[x].peBlue;
    if (w > am2_tex_w)
        w = am2_tex_w;
    if (h > am2_tex_h)
        h = am2_tex_h;
    if (!SDL_LockTexture(am2_sdl_texture, NULL, &dst, &dstPitch))
        return;
    for (y = 0; y < h; y++) {
        const uint8_t *s = pixels + y * pitch;
        uint32_t      *d = (uint32_t *)((uint8_t *)dst + y * dstPitch);
        for (x = 0; x < w; x++)
            d[x] = lut[s[x]];
    }
    SDL_UnlockTexture(am2_sdl_texture);
    SDL_RenderClear(am2_sdl_renderer);
    SDL_RenderTexture(am2_sdl_renderer, am2_sdl_texture, NULL, NULL);
    SDL_RenderPresent(am2_sdl_renderer);
}

void am2_host_wait_vblank(void)
{
    static uint64_t next;
    static int64_t  period = -1;
    static uint64_t sec_start;
    static int32_t  frames;
    uint64_t        now = SDL_GetTicksNS();

    /* In lockstep the pump is the clock and a flip costs no real time,
     * unless AM2_LOCKSTEP_PACE=1 asks to watch it at speed. */
    if (am2_host_lockstep()) {
        static int32_t pace = -1;
        if (pace < 0) {
            const char *pc = getenv("AM2_LOCKSTEP_PACE");
            pace = pc && *pc && *pc != '0';
        }
        if (!pace)
            return;
    }
    if (period < 0) {
        const char *env = getenv("AM2_FPS");
        int32_t     fps = env && *env ? atoi(env) : 60;
        period = fps > 0 ? 1000000000LL / fps : 0;
    }
    frames++;
    if (now - sec_start >= 1000000000ULL) {
        if (sec_start)
            am2_plat_debug("%d flips/s", frames);
        sec_start = now;
        frames = 0;
    }
    if (period == 0)
        return;
    if (next == 0 || now > next + (uint64_t)period * 4)
        next = now;
    next += (uint64_t)period;
    if (next > now)
        SDL_DelayNS(next - now);
}

/* ---- input --------------------------------------------------------------------------- */

/* SDL scancodes to DirectInput's, which are PC set 1 with the extended
 * keys at 0x80 and up. Filled at first use: C++ has no designated array
 * initialisers. */
static uint8_t am2_dik_of_scancode[SDL_SCANCODE_COUNT];

static const struct { uint16_t scancode; uint8_t dik; } am2_dik_pairs[] = {
    { SDL_SCANCODE_ESCAPE, 0x01 }, { SDL_SCANCODE_1, 0x02 }, { SDL_SCANCODE_2, 0x03 },
    { SDL_SCANCODE_3, 0x04 }, { SDL_SCANCODE_4, 0x05 }, { SDL_SCANCODE_5, 0x06 },
    { SDL_SCANCODE_6, 0x07 }, { SDL_SCANCODE_7, 0x08 }, { SDL_SCANCODE_8, 0x09 },
    { SDL_SCANCODE_9, 0x0A }, { SDL_SCANCODE_0, 0x0B }, { SDL_SCANCODE_MINUS, 0x0C },
    { SDL_SCANCODE_EQUALS, 0x0D }, { SDL_SCANCODE_BACKSPACE, 0x0E }, { SDL_SCANCODE_TAB, 0x0F },
    { SDL_SCANCODE_Q, 0x10 }, { SDL_SCANCODE_W, 0x11 }, { SDL_SCANCODE_E, 0x12 },
    { SDL_SCANCODE_R, 0x13 }, { SDL_SCANCODE_T, 0x14 }, { SDL_SCANCODE_Y, 0x15 },
    { SDL_SCANCODE_U, 0x16 }, { SDL_SCANCODE_I, 0x17 }, { SDL_SCANCODE_O, 0x18 },
    { SDL_SCANCODE_P, 0x19 }, { SDL_SCANCODE_LEFTBRACKET, 0x1A }, { SDL_SCANCODE_RIGHTBRACKET, 0x1B },
    { SDL_SCANCODE_RETURN, 0x1C }, { SDL_SCANCODE_LCTRL, 0x1D }, { SDL_SCANCODE_A, 0x1E },
    { SDL_SCANCODE_S, 0x1F }, { SDL_SCANCODE_D, 0x20 }, { SDL_SCANCODE_F, 0x21 },
    { SDL_SCANCODE_G, 0x22 }, { SDL_SCANCODE_H, 0x23 }, { SDL_SCANCODE_J, 0x24 },
    { SDL_SCANCODE_K, 0x25 }, { SDL_SCANCODE_L, 0x26 }, { SDL_SCANCODE_SEMICOLON, 0x27 },
    { SDL_SCANCODE_APOSTROPHE, 0x28 }, { SDL_SCANCODE_GRAVE, 0x29 }, { SDL_SCANCODE_LSHIFT, 0x2A },
    { SDL_SCANCODE_BACKSLASH, 0x2B }, { SDL_SCANCODE_Z, 0x2C }, { SDL_SCANCODE_X, 0x2D },
    { SDL_SCANCODE_C, 0x2E }, { SDL_SCANCODE_V, 0x2F }, { SDL_SCANCODE_B, 0x30 },
    { SDL_SCANCODE_N, 0x31 }, { SDL_SCANCODE_M, 0x32 }, { SDL_SCANCODE_COMMA, 0x33 },
    { SDL_SCANCODE_PERIOD, 0x34 }, { SDL_SCANCODE_SLASH, 0x35 }, { SDL_SCANCODE_RSHIFT, 0x36 },
    { SDL_SCANCODE_KP_MULTIPLY, 0x37 }, { SDL_SCANCODE_LALT, 0x38 }, { SDL_SCANCODE_SPACE, 0x39 },
    { SDL_SCANCODE_CAPSLOCK, 0x3A }, { SDL_SCANCODE_F1, 0x3B }, { SDL_SCANCODE_F2, 0x3C },
    { SDL_SCANCODE_F3, 0x3D }, { SDL_SCANCODE_F4, 0x3E }, { SDL_SCANCODE_F5, 0x3F },
    { SDL_SCANCODE_F6, 0x40 }, { SDL_SCANCODE_F7, 0x41 }, { SDL_SCANCODE_F8, 0x42 },
    { SDL_SCANCODE_F9, 0x43 }, { SDL_SCANCODE_F10, 0x44 }, { SDL_SCANCODE_NUMLOCKCLEAR, 0x45 },
    { SDL_SCANCODE_SCROLLLOCK, 0x46 }, { SDL_SCANCODE_KP_7, 0x47 }, { SDL_SCANCODE_KP_8, 0x48 },
    { SDL_SCANCODE_KP_9, 0x49 }, { SDL_SCANCODE_KP_MINUS, 0x4A }, { SDL_SCANCODE_KP_4, 0x4B },
    { SDL_SCANCODE_KP_5, 0x4C }, { SDL_SCANCODE_KP_6, 0x4D }, { SDL_SCANCODE_KP_PLUS, 0x4E },
    { SDL_SCANCODE_KP_1, 0x4F }, { SDL_SCANCODE_KP_2, 0x50 }, { SDL_SCANCODE_KP_3, 0x51 },
    { SDL_SCANCODE_KP_0, 0x52 }, { SDL_SCANCODE_KP_PERIOD, 0x53 }, { SDL_SCANCODE_F11, 0x57 },
    { SDL_SCANCODE_F12, 0x58 }, { SDL_SCANCODE_KP_ENTER, 0x9C }, { SDL_SCANCODE_RCTRL, 0x9D },
    { SDL_SCANCODE_KP_DIVIDE, 0xB5 }, { SDL_SCANCODE_PRINTSCREEN, 0xB7 }, { SDL_SCANCODE_RALT, 0xB8 },
    { SDL_SCANCODE_PAUSE, 0xC5 }, { SDL_SCANCODE_HOME, 0xC7 }, { SDL_SCANCODE_UP, 0xC8 },
    { SDL_SCANCODE_PAGEUP, 0xC9 }, { SDL_SCANCODE_LEFT, 0xCB }, { SDL_SCANCODE_RIGHT, 0xCD },
    { SDL_SCANCODE_END, 0xCF }, { SDL_SCANCODE_DOWN, 0xD0 }, { SDL_SCANCODE_PAGEDOWN, 0xD1 },
    { SDL_SCANCODE_INSERT, 0xD2 }, { SDL_SCANCODE_DELETE, 0xD3 }, { SDL_SCANCODE_LGUI, 0xDB },
    { SDL_SCANCODE_RGUI, 0xDC }, { SDL_SCANCODE_APPLICATION, 0xDD },
};

static void am2_dik_table_init(void)
{
    static int32_t done;
    size_t i;

    if (done)
        return;
    done = 1;
    for (i = 0; i < sizeof am2_dik_pairs / sizeof am2_dik_pairs[0]; i++)
        am2_dik_of_scancode[am2_dik_pairs[i].scancode] = am2_dik_pairs[i].dik;
}

void am2_host_keyboard_state(uint8_t *out)
{
    memcpy(out, am2_keys, 256);
}

/* A virtual-key code for WM_KEYDOWN: letters and digits as themselves,
 * the few named keys the game's dialogs read, nothing else. */
static uint32_t am2_vk_of(SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z')
        return (uint32_t)(key - 'a' + 'A');
    if (key >= '0' && key <= '9')
        return (uint32_t)key;
    switch (key) {
    case SDLK_RETURN:    return 0x0D;
    case SDLK_ESCAPE:    return 0x1B;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB:       return 0x09;
    case SDLK_SPACE:     return 0x20;
    case SDLK_LEFT:      return 0x25;
    case SDLK_UP:        return 0x26;
    case SDLK_RIGHT:     return 0x27;
    case SDLK_DOWN:      return 0x28;
    case SDLK_DELETE:    return 0x2E;
    }
    return 0;
}

static void am2_mouse_to_logical(SDL_Event *ev, int32_t *lx, int32_t *ly)
{
    SDL_ConvertEventToRenderCoordinates(am2_sdl_renderer, ev);
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        *lx = (int32_t)ev->motion.x;
        *ly = (int32_t)ev->motion.y;
    } else {
        *lx = (int32_t)ev->button.x;
        *ly = (int32_t)ev->button.y;
    }
}

void (*am2_host_cursor_query)(int32_t *x, int32_t *y);

/* The game accumulates DirectInput deltas into a cursor of its own and
 * clamps it to the screen. Feeding it the difference between the pointer
 * and THAT cursor puts its cursor exactly under the host's pointer, without
 * taking the pointer away from the desktop -- which is what an exclusive
 * DirectInput mouse would do and what this build declines to do.
 *
 * The cursor can only be measured against once the game has applied what
 * was already sent, so while motion is still queued the delta is taken
 * from the last pointer position instead, and the next drained pump
 * corrects any drift. Without a query the pointer is assumed to have
 * started at the centre. */
static int32_t am2_mouse_dx, am2_mouse_dy;   /* coalesced within one pump */
static int32_t am2_button_down[3];           /* pressed, as far as the game knows */
static int32_t am2_button_release[3];        /* a release owed once the press is seen */

static void am2_mouse_moved(int32_t lx, int32_t ly)
{
    int32_t bx, by;

    if (!am2_mouse_seen) {
        am2_mouse_seen = 1;
        am2_mouse_last_x = am2_disp_w / 2;
        am2_mouse_last_y = am2_disp_h / 2;
    }
    bx = am2_mouse_last_x;
    by = am2_mouse_last_y;
    if (am2_host_cursor_query && am2_di_mouse_pending() == 0 &&
        am2_mouse_dx == 0 && am2_mouse_dy == 0)
        am2_host_cursor_query(&bx, &by);
    am2_mouse_dx += lx - bx;
    am2_mouse_dy += ly - by;
    am2_mouse_last_x = lx;
    am2_mouse_last_y = ly;
}

/* A press and its release in one poll are invisible to the game: the widget
 * layer reads the button state once a frame, and a click that comes and
 * goes between two reads never happened. Real hands are slower than that,
 * xdotool is not, so a release that would land in the same buffer as its
 * press is owed instead and paid once the game has drained the press. */
static void am2_mouse_button(int32_t b, int32_t down)
{
    if (down) {
        am2_button_release[b] = 0;
        am2_button_down[b] = 1;
        am2_di_mouse_button(b, 1);
    } else if (am2_button_down[b] && am2_di_mouse_queued() > 0) {
        am2_button_release[b] = 1;
    } else {
        am2_button_down[b] = 0;
        am2_di_mouse_button(b, 0);
    }
}

static void am2_mouse_pay_releases(void)
{
    int32_t b;

    if (am2_di_mouse_queued() > 0)
        return;
    for (b = 0; b < 3; b++)
        if (am2_button_release[b]) {
            am2_button_release[b] = 0;
            am2_button_down[b] = 0;
            am2_di_mouse_button(b, 0);
        }
}

/* Hand the coalesced motion to DirectInput. The game's poll clears its
 * per-axis deltas once and then, after EVERY event, adds whatever both
 * axes currently hold -- so an X event followed by a Y event applies the X
 * delta twice (that is the "acceleration" CLAUDE.md measured under Wine,
 * where the real device delivers the same pair). Each axis is therefore
 * followed by a zero for the same axis, which a device may legitimately
 * report, and the sum the game accumulates is exactly the motion. */
static void am2_mouse_flush(void)
{
    if (am2_mouse_dx) {
        am2_di_mouse_axis(DIMOFS_X, am2_mouse_dx);
        am2_di_mouse_axis(DIMOFS_X, 0);
    }
    if (am2_mouse_dy) {
        am2_di_mouse_axis(DIMOFS_Y, am2_mouse_dy);
        am2_di_mouse_axis(DIMOFS_Y, 0);
    }
    am2_mouse_dx = am2_mouse_dy = 0;
}

void (*am2_host_frame_hook)(void);
#ifdef AM2_DEVTOOLS
extern "C" void devtools_hotkey(int32_t which);
#endif

/* ---- lockstep: the replay ---------------------------------------------------------- */

void (*am2_host_cursor_set)(int32_t x, int32_t y);

/* AM2_REPLAY=<file>: one line per action, applied at the pump whose number
 * begins it. `#` starts a comment.
 *
 *   N key NAME down|up|tap      NAME is a DirectInput code (0x1C) or one of
 *                               the names below; tap is down now, up 4 later
 *   N button B down|up|tap      B is 0 left, 1 right, 2 middle
 *   N move DX DY                relative motion into the mouse buffer
 *   N cursor X Y                the game's cursor, as the socket's `cursor`
 *   N dump FILE                 the next frame presented, as a PPM
 *   N exit                      leave, exit code 0
 */
typedef struct AM2_ReplayLine {
    uint32_t frame;
    char     verb[8];
    int32_t  a, b;
    char     text[512];
} AM2_ReplayLine;
static AM2_ReplayLine *am2_replay;
static int32_t         am2_replay_count, am2_replay_next, am2_replay_tried;

static const struct { const char *name; uint8_t dik, vk; } am2_key_names[] = {
    { "RETURN", 0x1C, 0x0D }, { "ESCAPE", 0x01, 0x1B }, { "SPACE", 0x39, 0x20 },
    { "TAB", 0x0F, 0x09 }, { "UP", 0xC8, 0x26 }, { "DOWN", 0xD0, 0x28 },
    { "LEFT", 0xCB, 0x25 }, { "RIGHT", 0xCD, 0x27 }, { "LSHIFT", 0x2A, 0x10 },
    { "LCONTROL", 0x1D, 0x11 }, { "F1", 0x3B, 0x70 }, { "F2", 0x3C, 0x71 },
    { "F3", 0x3D, 0x72 }, { "F4", 0x3E, 0x73 }, { "F5", 0x3F, 0x74 },
    { "F9", 0x43, 0x78 }, { "F10", 0x44, 0x79 },
    { "Q", 0x10, 'Q' }, { "W", 0x11, 'W' }, { "E", 0x12, 'E' }, { "R", 0x13, 'R' },
    { "T", 0x14, 'T' }, { "Y", 0x15, 'Y' }, { "U", 0x16, 'U' }, { "I", 0x17, 'I' },
    { "O", 0x18, 'O' }, { "P", 0x19, 'P' }, { "A", 0x1E, 'A' }, { "S", 0x1F, 'S' },
    { "D", 0x20, 'D' }, { "F", 0x21, 'F' }, { "G", 0x22, 'G' }, { "H", 0x23, 'H' },
    { "J", 0x24, 'J' }, { "K", 0x25, 'K' }, { "L", 0x26, 'L' }, { "Z", 0x2C, 'Z' },
    { "X", 0x2D, 'X' }, { "C", 0x2E, 'C' }, { "V", 0x2F, 'V' }, { "B", 0x30, 'B' },
    { "N", 0x31, 'N' }, { "M", 0x32, 'M' },
    { "1", 0x02, '1' }, { "2", 0x03, '2' }, { "3", 0x04, '3' }, { "4", 0x05, '4' },
    { "5", 0x06, '5' }, { "6", 0x07, '6' }, { "7", 0x08, '7' }, { "8", 0x09, '8' },
    { "9", 0x0A, '9' }, { "0", 0x0B, '0' },
};

static int32_t am2_key_lookup(const char *name, uint8_t *dik, uint8_t *vk)
{
    size_t i;
    if (!strncasecmp(name, "0x", 2)) {
        *dik = (uint8_t)strtoul(name, NULL, 16);
        *vk = 0;
        return 1;
    }
    for (i = 0; i < sizeof am2_key_names / sizeof am2_key_names[0]; i++)
        if (!strcasecmp(am2_key_names[i].name, name)) {
            *dik = am2_key_names[i].dik;
            *vk = am2_key_names[i].vk;
            return 1;
        }
    return 0;
}

static void am2_replay_add(uint32_t frame, const char *verb, int32_t a, int32_t b, const char *text)
{
    AM2_ReplayLine *l;
    am2_replay = (AM2_ReplayLine *)realloc(am2_replay, (size_t)(am2_replay_count + 1) * sizeof *am2_replay);
    l = &am2_replay[am2_replay_count++];
    memset(l, 0, sizeof *l);
    l->frame = frame;
    snprintf(l->verb, sizeof l->verb, "%s", verb);
    l->a = a;
    l->b = b;
    if (text)
        snprintf(l->text, sizeof l->text, "%s", text);
}

static int am2_replay_order(const void *x, const void *y)
{
    const AM2_ReplayLine *p = (const AM2_ReplayLine *)x, *q = (const AM2_ReplayLine *)y;
    if (p->frame != q->frame)
        return p->frame < q->frame ? -1 : 1;
    return (int)(p - q > 0) - (int)(p - q < 0);
}

static void am2_replay_parse(int32_t n, uint32_t frame, const char *verb,
                             const char *w1, const char *w2);

static void am2_replay_load(void)
{
    const char *path = getenv("AM2_REPLAY");
    FILE       *fh;
    char        line[1024];
    int32_t     n = 0;

    am2_replay_tried = 1;
    if (!path || !*path)
        return;
    fh = fopen(path, "r");
    if (!fh) {
        /* Fatal, not a warning: the game chdirs into its data directory
         * before this runs, so a relative name is the commonest way here,
         * and a run with no script never reaches its `exit`. */
        am2_plat_log("replay: cannot open %s (use an absolute path)", path);
        am2_host_exit(2);
    }
    while (fgets(line, sizeof line, fh)) {
        char     verb[16] = "", w1[512] = "", w2[64] = "";
        unsigned frame;
        char    *hash = strchr(line, '#');
        n++;
        if (hash)
            *hash = 0;
        if (sscanf(line, "%u %15s %511s %63s", &frame, verb, w1, w2) < 2)
            continue;
        am2_replay_parse(n, frame, verb, w1, w2);
    }
    fclose(fh);
    if (am2_replay_count > 1)
        qsort(am2_replay, (size_t)am2_replay_count, sizeof *am2_replay, am2_replay_order);
    am2_plat_log("replay: %d actions from %s", am2_replay_count, path);
}

/* One replay action, from the file or from the step socket. */
static void am2_replay_parse(int32_t n, uint32_t frame, const char *verb,
                             const char *w1, const char *w2)
{
    {
        if (!strcmp(verb, "key")) {
            uint8_t dik, vk;
            if (!am2_key_lookup(w1, &dik, &vk)) {
                am2_plat_log("replay: line %d: unknown key %s", n, w1);
                return;
            }
            if (!strcmp(w2, "tap")) {
                am2_replay_add(frame, "key", dik | (vk << 8), 1, NULL);
                am2_replay_add(frame + 4, "key", dik | (vk << 8), 0, NULL);
            } else {
                am2_replay_add(frame, "key", dik | (vk << 8), !strcmp(w2, "down"), NULL);
            }
        } else if (!strcmp(verb, "button")) {
            int32_t bt = atoi(w1);
            if (!strcmp(w2, "tap")) {
                am2_replay_add(frame, "button", bt, 1, NULL);
                am2_replay_add(frame + 4, "button", bt, 0, NULL);
            } else {
                am2_replay_add(frame, "button", bt, !strcmp(w2, "down"), NULL);
            }
        } else if (!strcmp(verb, "move") || !strcmp(verb, "cursor")) {
            am2_replay_add(frame, verb, atoi(w1), atoi(w2), NULL);
        } else if (!strcmp(verb, "dump")) {
            am2_replay_add(frame, verb, 0, 0, w1);
        } else if (!strcmp(verb, "exit")) {
            am2_replay_add(frame, verb, 0, 0, NULL);
        } else {
            am2_plat_log("replay: line %d: unknown verb %s", n, verb);
        }
    }
}

static void am2_replay_apply(void)
{
    if (!am2_replay_tried)
        am2_replay_load();
    while (am2_replay_next < am2_replay_count &&
           am2_replay[am2_replay_next].frame <= am2_pump_count) {
        const AM2_ReplayLine *l = &am2_replay[am2_replay_next++];
        if (!strcmp(l->verb, "key")) {
            uint8_t dik = (uint8_t)l->a, vk = (uint8_t)(l->a >> 8);
            am2_keys[dik] = l->b ? 0x80 : 0;
            if (vk)
                am2_window_on_key(l->b, vk);
        } else if (!strcmp(l->verb, "button")) {
            am2_di_mouse_button(l->a, l->b);
        } else if (!strcmp(l->verb, "move")) {
            am2_di_mouse_motion(l->a, l->b);
        } else if (!strcmp(l->verb, "cursor")) {
            if (am2_host_cursor_set)
                am2_host_cursor_set(l->a, l->b);
            else
                am2_plat_log("replay: cursor: this binary installs no cursor setter");
        } else if (!strcmp(l->verb, "dump")) {
            snprintf(am2_framedump_path, sizeof am2_framedump_path, "%s", l->text);
        } else if (!strcmp(l->verb, "exit")) {
            am2_plat_log("replay: exit at pump %u, %u frames presented",
                         (unsigned)am2_pump_count, (unsigned)am2_present_count);
            am2_host_exit(0);
        }
    }
}

static void am2_audio_step(void);

/* ---- lockstep: the step socket ------------------------------------------------------ */

/* AM2_STEP=<unix socket path>: the pump waits for a coordinator before every
 * step, so two games can be driven through the same frames by one hand and
 * stopped, both alive, on the first frame that differs (tools/sidebyside.py).
 *
 * At each pump the game first REPORTS what happened since the last one --
 *
 *   pump N                       the pump about to run
 *   frame IDX HASH               each frame presented since the last report
 *   host key DIK VK 0|1          each host event it applied in the last pump
 *   host char C | host motion X Y | host button B 0|1 | host wheel D
 *   ready
 *
 * -- and then reads lines until `step`:
 *
 *   key|button|move|cursor|dump  a replay action, for THIS pump (the file's
 *                                grammar without the frame number)
 *   host ...                     another game's host event, applied through
 *                                the same handlers a real event takes
 *   last FILE                    the last frame presented, as a PPM; the
 *                                reply is `ok last FILE`
 *   step                         run the pump
 *
 * (`exit` is a replay action, so it leaves at the pump like the file's.)
 *
 * Host events reach the game only in step mode with a real window: they
 * are applied here and echoed in the next report, which is what lets the
 * coordinator hand them to the other side for the same pump. While the
 * game waits, the frame hook still runs, so the control socket's `snap`
 * is served on a trapped game. */
static int32_t  am2_step_fd = -2;              /* -2 untried, -1 off */
static char     am2_step_buf[4096];
static size_t   am2_step_len;
static uint64_t am2_step_hashes[64];
static uint32_t am2_step_indexes[64];
static int32_t  am2_step_nframes;
static char     am2_step_host[64][64];
static int32_t  am2_step_nhost;
static uint8_t *am2_step_last;
static int32_t  am2_step_last_w, am2_step_last_h;
static PALETTEENTRY am2_step_last_pal[256];

static int32_t am2_step_on(void)
{
    if (am2_step_fd == -2) {
        const char *path = getenv("AM2_STEP");
        struct sockaddr_un addr;
        am2_step_fd = -1;
        if (!path || !*path)
            return 0;
        am2_step_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
        if (am2_step_fd < 0 || connect(am2_step_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
            am2_plat_log("step: cannot connect to %s", path);
            am2_host_exit(2);
        }
        am2_plat_log("step: connected to %s", path);
    }
    return am2_step_fd >= 0;
}

static void am2_step_frame(const uint8_t *pixels, int32_t pitch, int32_t w,
                           int32_t h, const PALETTEENTRY *palette)
{
    int32_t y;

    if (am2_step_fd < 0)
        return;
    if (am2_step_nframes < 64) {
        am2_step_hashes[am2_step_nframes] = am2_frame_hash(pixels, pitch, w, h, palette);
        am2_step_indexes[am2_step_nframes] = am2_present_count;
        am2_step_nframes++;
    }
    if (am2_step_last_w != w || am2_step_last_h != h) {
        free(am2_step_last);
        am2_step_last = (uint8_t *)malloc((size_t)w * (size_t)h);
        am2_step_last_w = w;
        am2_step_last_h = h;
    }
    if (am2_step_last)
        for (y = 0; y < h; y++)
            memcpy(am2_step_last + (size_t)y * (size_t)w, pixels + y * pitch, (size_t)w);
    memcpy(am2_step_last_pal, palette, sizeof am2_step_last_pal);
}

static void am2_step_send(const char *line)
{
    size_t n = strlen(line);
    while (n) {
        ssize_t k = write(am2_step_fd, line, n);
        if (k <= 0) {
            am2_plat_log("step: coordinator gone at pump %u", (unsigned)am2_pump_count);
            am2_host_exit(0);
        }
        line += k;
        n -= (size_t)k;
    }
}

static void am2_step_note_host(const char *fmt, ...)
{
    va_list ap;
    if (am2_step_nhost >= 64)
        return;
    va_start(ap, fmt);
    vsnprintf(am2_step_host[am2_step_nhost], sizeof am2_step_host[0], fmt, ap);
    va_end(ap);
    am2_step_nhost++;
}

static void am2_step_report(void)
{
    char    line[128];
    int32_t i;

    snprintf(line, sizeof line, "pump %u\n", (unsigned)am2_pump_count + 1);
    am2_step_send(line);
    for (i = 0; i < am2_step_nframes; i++) {
        snprintf(line, sizeof line, "frame %u %016llx\n", (unsigned)am2_step_indexes[i],
                 (unsigned long long)am2_step_hashes[i]);
        am2_step_send(line);
    }
    for (i = 0; i < am2_step_nhost; i++) {
        snprintf(line, sizeof line, "%s\n", am2_step_host[i]);
        am2_step_send(line);
    }
    am2_step_nframes = 0;
    am2_step_nhost = 0;
    am2_step_send("ready\n");
}

/* A host event applied to this game, from its own window or from the
 * other side's report. `echo` says whether to put it in the next report. */
static void am2_step_host_apply(const char *w, int32_t echo)
{
    char     verb[16] = "";
    int32_t  a = 0, b = 0, c = 0;

    if (sscanf(w, "%15s %i %i %i", verb, &a, &b, &c) < 1)
        return;
    if (!strcmp(verb, "key")) {
        if (a > 0 && a < 256)
            am2_keys[a] = c ? 0x80 : 0;
        am2_window_on_key(c, (uint32_t)b);
        if (echo)
            am2_step_note_host("host key 0x%02x 0x%02x %d", a, b, c);
    } else if (!strcmp(verb, "char")) {
        am2_window_on_char((uint32_t)a);
        if (echo)
            am2_step_note_host("host char %d", a);
    } else if (!strcmp(verb, "motion")) {
        am2_mouse_moved(a, b);
        if (echo)
            am2_step_note_host("host motion %d %d", a, b);
    } else if (!strcmp(verb, "button")) {
        am2_mouse_flush();
        am2_mouse_button(a, b);
        if (echo)
            am2_step_note_host("host button %d %d", a, b);
    } else if (!strcmp(verb, "wheel")) {
        am2_mouse_flush();
        am2_di_mouse_wheel(a);
        if (echo)
            am2_step_note_host("host wheel %d", a);
    }
}

/* Read lines until `step`. Replay actions are queued for this pump and
 * applied by am2_replay_apply below; host lines are applied at once. */
static void am2_step_wait(void)
{
    for (;;) {
        char *nl;
        while ((nl = (char *)memchr(am2_step_buf, '\n', am2_step_len)) != NULL) {
            char   line[1024];
            size_t n = (size_t)(nl - am2_step_buf);
            char   verb[16] = "", w1[512] = "", w2[64] = "";

            if (n >= sizeof line)
                n = sizeof line - 1;
            memcpy(line, am2_step_buf, n);
            line[n] = 0;
            am2_step_len -= (size_t)(nl - am2_step_buf) + 1;
            memmove(am2_step_buf, nl + 1, am2_step_len);
            if (!strcmp(line, "step"))
                return;
            if (!strncmp(line, "host ", 5)) {
                am2_step_host_apply(line + 5, 0);
                continue;
            }
            if (!strncmp(line, "last ", 5)) {
                char reply[1100];
                int32_t ok = am2_step_last &&
                    am2_write_ppm(line + 5, am2_step_last, am2_step_last_w, am2_step_last_w,
                                  am2_step_last_h, am2_step_last_pal);
                snprintf(reply, sizeof reply, "%s last %s\n", ok ? "ok" : "err", line + 5);
                am2_step_send(reply);
                continue;
            }
            if (sscanf(line, "%15s %511s %63s", verb, w1, w2) >= 1)
                am2_replay_parse(0, am2_pump_count + 1, verb, w1, w2);
        }
        {
            struct pollfd pfd;
            ssize_t       k;
            pfd.fd = am2_step_fd;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 50) == 0) {
                /* Nothing yet: serve the control socket's requests, so a
                 * trapped game can still be snapshotted. */
                if (am2_host_frame_hook)
                    am2_host_frame_hook();
                continue;
            }
            if (am2_step_len >= sizeof am2_step_buf)
                am2_step_len = 0;
            k = read(am2_step_fd, am2_step_buf + am2_step_len, sizeof am2_step_buf - am2_step_len);
            if (k <= 0) {
                am2_plat_log("step: coordinator gone at pump %u", (unsigned)am2_pump_count);
                am2_host_exit(0);
            }
            am2_step_len += (size_t)k;
        }
    }
}

/* The window's own events, in step mode: applied and echoed. */
static void am2_step_host_events(void)
{
    SDL_Event ev;
    int32_t   moved = 0;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            am2_window_on_close();
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            char    w[64];
            uint8_t dik = ev.key.scancode < SDL_SCANCODE_COUNT
                        ? am2_dik_of_scancode[ev.key.scancode] : 0;
            if (ev.key.repeat)
                break;
            snprintf(w, sizeof w, "key %d %u %d", dik, (unsigned)am2_vk_of(ev.key.key), ev.key.down ? 1 : 0);
            am2_step_host_apply(w, 1);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            const char *s;
            for (s = ev.text.text; *s; s++)
                if ((uint8_t)*s < 0x80) {
                    char w[32];
                    snprintf(w, sizeof w, "char %d", (uint8_t)*s);
                    am2_step_host_apply(w, 1);
                }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            int32_t lx, ly;
            char    w[64];
            am2_mouse_to_logical(&ev, &lx, &ly);
            snprintf(w, sizeof w, "motion %d %d", lx, ly);
            am2_step_host_apply(w, 1);
            moved = 1;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int32_t lx, ly, b;
            char    w[64];
            am2_mouse_to_logical(&ev, &lx, &ly);
            snprintf(w, sizeof w, "motion %d %d", lx, ly);
            am2_step_host_apply(w, 1);
            b = ev.button.button == SDL_BUTTON_LEFT ? 0
              : ev.button.button == SDL_BUTTON_RIGHT ? 1
              : ev.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
            if (b >= 0) {
                snprintf(w, sizeof w, "button %d %d", b, ev.button.down ? 1 : 0);
                am2_step_host_apply(w, 1);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (ev.wheel.y != 0) {
                char w[32];
                snprintf(w, sizeof w, "wheel %d", (int32_t)(ev.wheel.y * 120));
                am2_step_host_apply(w, 1);
            }
            break;
        }
    }
    am2_mouse_flush();
    if (moved && am2_the_window())
        SendMessageA(am2_the_window(), WM_SETCURSOR, 0, 0);
}

void am2_host_pump(void)
{
    SDL_Event ev;
    int32_t   moved = 0;

    if (!am2_sdl_window)
        return;
    if (am2_host_frame_hook)
        am2_host_frame_hook();
    am2_dik_table_init();
    am2_mouse_pay_releases();
    if (am2_host_lockstep()) {
        /* The pump IS the clock: one step, the timers it passes, one
         * step's worth of sound, and the script's actions for this frame.
         * Host events are drained and dropped -- a stray real click would
         * make the run unrepeatable -- except the window closing. */
        if (am2_step_on()) {
            am2_step_report();
            am2_step_wait();
        }
        am2_pump_count++;
        am2_clock_ns += am2_step_ns;
        am2_timers_fire_due();
        am2_audio_step();
        am2_replay_apply();
        if (am2_step_fd >= 0) {
            am2_step_host_events();
            return;
        }
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                am2_window_on_close();
        return;
    }
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            am2_window_on_close();
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            am2_window_on_focus(1);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            am2_window_on_focus(0);
            break;
        case SDL_EVENT_WINDOW_MOVED:
            am2_window_on_moved(ev.window.data1, ev.window.data2);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            uint8_t dik = ev.key.scancode < SDL_SCANCODE_COUNT
                        ? am2_dik_of_scancode[ev.key.scancode] : 0;
#ifdef AM2_DEVTOOLS
            /* The development binary's quick savestate keys; the game never
             * reads F5 or F9 itself. */
            if (ev.key.down && !ev.key.repeat
                && (ev.key.scancode == SDL_SCANCODE_F5 || ev.key.scancode == SDL_SCANCODE_F9)) {
                devtools_hotkey(ev.key.scancode == SDL_SCANCODE_F5 ? 1 : 2);
                break;
            }
#endif
            if (dik)
                am2_keys[dik] = ev.key.down ? 0x80 : 0;
            if (!ev.key.repeat)
                am2_window_on_key(ev.key.down, am2_vk_of(ev.key.key));
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            const char *s;
            for (s = ev.text.text; *s; s++) {
                /* Latin-1 only: the game's fonts and dialogs are 8-bit. A
                 * two-byte UTF-8 sequence for U+0080..U+00FF is folded;
                 * anything wider is dropped. */
                uint8_t c = (uint8_t)*s;
                if (c < 0x80)
                    am2_window_on_char(c);
                else if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
                    uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
                    if (cp < 0x100)
                        am2_window_on_char(cp);
                    s++;
                } else {
                    while ((s[1] & 0xC0) == 0x80)
                        s++;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            int32_t lx, ly;
            am2_mouse_to_logical(&ev, &lx, &ly);
            am2_mouse_moved(lx, ly);
            moved = 1;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int32_t lx, ly, b;
            am2_mouse_to_logical(&ev, &lx, &ly);
            am2_mouse_moved(lx, ly);
            b = ev.button.button == SDL_BUTTON_LEFT ? 0
              : ev.button.button == SDL_BUTTON_RIGHT ? 1
              : ev.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
            am2_mouse_flush();             /* the press is at the pointer */
            if (b >= 0)
                am2_mouse_button(b, ev.button.down);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            am2_mouse_flush();
            if (ev.wheel.y != 0)
                am2_di_mouse_wheel((int32_t)(ev.wheel.y * 120));
            break;
        }
    }
    am2_mouse_flush();
    /* Windows asks the window what cursor to show whenever the pointer
     * moves over it; the game answers "none". */
    if (moved && am2_the_window())
        SendMessageA(am2_the_window(), WM_SETCURSOR, 0, 0);
}

/* ---- main --------------------------------------------------------------------------- */

/* ---- sound and timers ------------------------------------------------------ */

/* One SDL audio stream bound to the default playback device, pulled by the
 * device through its get-callback. The mixer above it (dsound.cpp) is what
 * knows about buffers; this only hands it a float scratch to fill. */
static SDL_AudioStream *am2_sdl_audio;
static am2_audio_mix_fn am2_audio_mix;
static void            *am2_audio_ud;
static float           *am2_audio_scratch;
static int32_t          am2_audio_scratch_frames;
/* AM2_AUDIO_DUMP=<file>: everything the mixer hands the device, as raw
 * interleaved stereo float at the mix rate -- the way to check the sound
 * without ears. */
static FILE            *am2_audio_dump;

static void SDLCALL am2_audio_pull(void *ud, SDL_AudioStream *stream,
                                   int additional, int total)
{
    int32_t frames = additional / (int32_t)(2 * sizeof(float));

    (void)ud; (void)total;
    if (frames <= 0)
        return;
    if (frames > am2_audio_scratch_frames) {
        float *n = (float *)realloc(am2_audio_scratch,
                                    (size_t)frames * 2 * sizeof(float));
        if (!n)
            return;
        am2_audio_scratch = n;
        am2_audio_scratch_frames = frames;
    }
    am2_audio_mix(am2_audio_ud, am2_audio_scratch, frames);
    SDL_PutAudioStreamData(stream, am2_audio_scratch,
                           frames * (int)(2 * sizeof(float)));
#ifdef AM2_DEVTOOLS
    if (am2_audio_dump)
        fwrite(am2_audio_scratch, 2 * sizeof(float), (size_t)frames, am2_audio_dump);
#endif
}

static int32_t  am2_audio_rate;
static uint64_t am2_audio_carry_ns;

/* One pump's worth of sound, mixed on the game thread and pushed: the
 * lockstep replacement for the device pulling on its own thread. */
static void am2_audio_step(void)
{
    uint64_t ns;
    int32_t  frames;

    if (!am2_sdl_audio || !am2_audio_mix)
        return;
    ns = am2_step_ns + am2_audio_carry_ns;
    frames = (int32_t)(ns * (uint64_t)am2_audio_rate / 1000000000ULL);
    am2_audio_carry_ns = ns - (uint64_t)frames * 1000000000ULL / (uint64_t)am2_audio_rate;
    if (frames <= 0)
        return;
    if (frames > am2_audio_scratch_frames) {
        float *n = (float *)realloc(am2_audio_scratch, (size_t)frames * 2 * sizeof(float));
        if (!n)
            return;
        am2_audio_scratch = n;
        am2_audio_scratch_frames = frames;
    }
    am2_audio_mix(am2_audio_ud, am2_audio_scratch, frames);
    /* Mixed and then DROPPED, not queued. A device drains its stream in
     * real time and the pump outruns real time whenever it is not sleeping,
     * so queueing here grew without bound: 5.6 KB a pump, 70 MB every
     * 12,400 frames, on a dummy device forever. The mix is what a
     * comparison wants and the dump below still sees it; the device is
     * opened so the game believes it has one, and hears nothing. */
#ifdef AM2_DEVTOOLS
    if (am2_audio_dump)
        fwrite(am2_audio_scratch, 2 * sizeof(float), (size_t)frames, am2_audio_dump);
#endif
}

int32_t am2_host_audio_open(int32_t rate, am2_audio_mix_fn mix, void *ud)
{
    SDL_AudioSpec spec;

    if (am2_sdl_audio)
        return 1;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        am2_plat_log("audio: SDL_InitSubSystem: %s", SDL_GetError());
        return 0;
    }
    spec.format   = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq     = rate;
    am2_audio_mix = mix;
    am2_audio_ud  = ud;
    am2_audio_rate = rate;
    am2_sdl_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                              &spec, am2_host_lockstep() ? NULL : am2_audio_pull,
                                              NULL);
    if (!am2_sdl_audio) {
        am2_plat_log("audio: no playback device (%s); the game runs silent",
                     SDL_GetError());
        return 0;
    }
#ifdef AM2_DEVTOOLS
    {
        const char *dump = getenv("AM2_AUDIO_DUMP");

        if (dump && *dump)
            am2_audio_dump = fopen(dump, "wb");
    }
#endif
    SDL_ResumeAudioStreamDevice(am2_sdl_audio);
    am2_plat_log("audio: %d Hz stereo float through %s", rate,
                 SDL_GetCurrentAudioDriver());
    return 1;
}

void am2_host_audio_close(void)
{
    if (am2_sdl_audio) {
        SDL_DestroyAudioStream(am2_sdl_audio);   /* closes the device too */
        am2_sdl_audio = NULL;
    }
    if (am2_audio_dump) {
        fclose(am2_audio_dump);
        am2_audio_dump = NULL;
    }
}

typedef struct AM2_HostTimer {
    am2_timer_fn fn;
    void        *ud;
    int32_t      periodic;
    uint64_t     due, interval;     /* lockstep: on the virtual clock */
    uint32_t     id;
} AM2_HostTimer;

/* Lockstep's timers: a list the pump walks, in the order they were made. */
#define AM2_LOCK_TIMERS 32
static AM2_HostTimer *am2_lock_timers[AM2_LOCK_TIMERS];
static uint32_t       am2_lock_timer_next_id = 0x4000;

static void am2_timers_fire_due(void)
{
    static int32_t firing;
    int32_t        i;

    if (!am2_host_lockstep() || firing)
        return;
    firing = 1;
    for (i = 0; i < AM2_LOCK_TIMERS; i++) {
        int32_t guard = 0;
        /* A callback may kill its own timer and arm another into the same
         * slot, so nothing about `t` is trusted after fn returns: the slot
         * is re-read, and a record that is no longer there is not touched. */
        while (am2_lock_timers[i] && am2_clock_ns >= am2_lock_timers[i]->due && guard++ < 64) {
            AM2_HostTimer *t = am2_lock_timers[i];
            int32_t        periodic = t->periodic;
            uint64_t       interval = t->interval;
            am2_plat_debug("lockstep: timer %u fires at %llu ms", (unsigned)t->id,
                           (unsigned long long)(am2_clock_ns / 1000000ULL));
            t->fn(t->ud);
            if (am2_lock_timers[i] != t)
                break;
            if (!periodic) {
                am2_lock_timers[i] = NULL;
                free(t);
                break;
            }
            t->due += interval;
        }
    }
    firing = 0;
}

static Uint32 SDLCALL am2_timer_fire(void *ud, SDL_TimerID id, Uint32 interval)
{
    AM2_HostTimer *t = (AM2_HostTimer *)ud;

    (void)id;
    t->fn(t->ud);
    if (t->periodic)
        return interval;
    free(t);
    return 0;
}

/* Periodic records outlive their firing and are freed on removal, so they
 * are remembered here; a one-shot frees its own once it has fired. */
#define AM2_HOST_TIMERS 16
static AM2_HostTimer *am2_periodic[AM2_HOST_TIMERS];
static SDL_TimerID    am2_periodic_id[AM2_HOST_TIMERS];

uint32_t am2_host_timer_add(uint32_t ms, int32_t periodic, am2_timer_fn fn, void *ud)
{
    AM2_HostTimer *t = (AM2_HostTimer *)malloc(sizeof *t);
    SDL_TimerID    id;
    int32_t        i;

    if (!t)
        return 0;
    t->fn = fn;
    t->ud = ud;
    t->periodic = periodic;
    if (am2_host_lockstep()) {
        for (i = 0; i < AM2_LOCK_TIMERS; i++) {
            if (!am2_lock_timers[i]) {
                t->interval = (uint64_t)(ms ? ms : 1) * 1000000ULL;
                t->due = am2_clock_ns + t->interval;
                t->id = am2_lock_timer_next_id++;
                am2_lock_timers[i] = t;
                return t->id;
            }
        }
        free(t);
        return 0;
    }
    id = SDL_AddTimer(ms ? ms : 1, am2_timer_fire, t);
    if (!id) {
        free(t);
        return 0;
    }
    if (periodic) {
        for (i = 0; i < AM2_HOST_TIMERS; i++) {
            if (!am2_periodic[i]) {
                am2_periodic[i] = t;
                am2_periodic_id[i] = id;
                break;
            }
        }
    }
    return (uint32_t)id;
}

void am2_host_timer_remove(uint32_t id)
{
    int32_t i;

    if (am2_host_lockstep()) {
        for (i = 0; i < AM2_LOCK_TIMERS; i++)
            if (am2_lock_timers[i] && am2_lock_timers[i]->id == id) {
                free(am2_lock_timers[i]);
                am2_lock_timers[i] = NULL;
            }
        return;
    }

    /* Removing a one-shot that has already fired is a no-op; SDL says so
     * and its record is gone. */
    (void)SDL_RemoveTimer((SDL_TimerID)id);
    for (i = 0; i < AM2_HOST_TIMERS; i++) {
        if (am2_periodic[i] && am2_periodic_id[i] == (SDL_TimerID)id) {
            free(am2_periodic[i]);
            am2_periodic[i] = NULL;
            am2_periodic_id[i] = 0;
        }
    }
}

void am2_host_exit(int32_t code)
{
    am2_host_audio_close();
    am2_host_window_close();
    SDL_Quit();
    exit(code);
}

int main(int argc, char **argv)
{
    char    cmdline[4096];
    size_t  used = 0;
    int32_t i, rc;

    cmdline[0] = 0;
    for (i = 1; i < argc; i++) {
        size_t n = strlen(argv[i]);
        if (used + n + 2 >= sizeof cmdline)
            break;
        if (used)
            cmdline[used++] = ' ';
        memcpy(cmdline + used, argv[i], n);
        used += n;
        cmdline[used] = 0;
    }

    am2_game_thread = pthread_self();
    /* What GetCommandLineA answers: the CRT's startup reads it for argv
     * and for the line WinMain sees. The hybrid's loader does the same. */
    am2_set_command_line(cmdline);
    SDL_SetAppMetadata("Army Men II", "0.1", "org.armymen2.port");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "platform: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    rc = WinMain((HINSTANCE)(uintptr_t)0x400000u, NULL, cmdline, SW_SHOWNORMAL);
    am2_host_audio_close();
    am2_host_window_close();
    SDL_Quit();
    return rc;
}
