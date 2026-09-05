/* sdl.cpp -- the outermost layer: main(), the host window, the renderer,
 * the event pump and the one place a frame reaches the screen.
 *
 * Nothing above this file knows SDL exists. It answers the requests
 * platform.h declares -- open a window this big, present these 8-bit
 * pixels through this palette, drain your events -- and translates the
 * host's events into what user32.cpp and dinput.cpp expect.
 *
 *   AM2_SCALE=N     integer window scale (default: the largest that fits)
 *   AM2_VSYNC=0     do not wait for vertical blank on present
 */
#include "platform.h"
#include <dinput.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        am2_scale = am2_pick_scale(w, h);
        am2_sdl_window = SDL_CreateWindow(title ? title : "Army Men II",
                                          w * am2_scale, h * am2_scale,
                                          SDL_WINDOW_RESIZABLE);
        if (!am2_sdl_window) {
            fprintf(stderr, "platform: SDL_CreateWindow: %s\n", SDL_GetError());
            exit(1);
        }
        am2_sdl_renderer = SDL_CreateRenderer(am2_sdl_window, NULL);
        if (!am2_sdl_renderer) {
            fprintf(stderr, "platform: SDL_CreateRenderer: %s\n", SDL_GetError());
            exit(1);
        }
        SDL_SetRenderVSync(am2_sdl_renderer, (vs && *vs == '0') ? 0 : 1);
        SDL_StartTextInput(am2_sdl_window);
        am2_plat_log("window %dx%d at scale %d, renderer %s", w, h, am2_scale,
                     SDL_GetRendererName(am2_sdl_renderer));
    } else {
        if (title)
            SDL_SetWindowTitle(am2_sdl_window, title);
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

/* ---- the frame ---------------------------------------------------------------------- */

void am2_host_present(const uint8_t *pixels, int32_t pitch, int32_t w,
                      int32_t h, const PALETTEENTRY *palette)
{
    uint32_t  lut[256];
    void     *dst;
    int       dstPitch;
    int32_t   x, y;

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

void am2_host_pump(void)
{
    SDL_Event ev;
    int32_t   moved = 0;

    if (!am2_sdl_window)
        return;
    am2_dik_table_init();
    am2_mouse_pay_releases();
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

    SDL_SetAppMetadata("Army Men II", "0.1", "org.armymen2.port");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "platform: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    rc = WinMain((HINSTANCE)(uintptr_t)0x400000u, NULL, cmdline, SW_SHOWNORMAL);
    am2_host_window_close();
    SDL_Quit();
    return rc;
}
