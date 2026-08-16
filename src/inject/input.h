/* Injected input state, fed by the control socket and consumed by the
 * DirectInput hooks.
 *
 * Driving the game through X11 does not work here: with no window manager on
 * Xvfb there is no foreground window, and DirectInput drops mouse input
 * entirely (keyboard happens to get through, which is misleading). Injecting
 * below DirectInput instead is deterministic, needs no display server
 * cooperation, and is scriptable.
 */

#ifndef AM2_INPUT_H
#define AM2_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


#define AM2_MOUSE_BUTTONS 4

void input_init(void);

/* Held until released. `until_ms` of 0 means "until an explicit release";
 * otherwise the key auto-releases once GetTickCount() passes it, which is how
 * `tap` works without having to count polls. */
void input_key(uint8_t dik, int32_t down, uint32_t hold_ms);
void input_button(int32_t button, int32_t down, uint32_t hold_ms);

/* Mouse motion is relative and accumulates until a poll consumes it. */
void input_mouse_move(int32_t dx, int32_t dy, int32_t dz);

void input_clear(void);

/* Overlay onto a real DirectInput keyboard state buffer (256 bytes, high bit
 * set means pressed). */
void input_overlay_keyboard(uint8_t *state, uint32_t len);

/* Overlay onto a DIMOUSESTATE / DIMOUSESTATE2. Consumes accumulated motion. */
void input_overlay_mouse(int32_t *axes, uint8_t *buttons, uint32_t nbuttons);

/* ---- buffered event delivery ------------------------------------------
 *
 * The game reads input through GetDeviceData rather than GetDeviceState, so
 * overlaying a polled state buffer drives nothing. Every state change is also
 * queued as a DirectInput event and appended to the buffered reads.
 */

#define AM2_DEV_KEYBOARD 1
#define AM2_DEV_MOUSE    2

/* Turn expired timed holds into release events. Call before draining. */
void input_pump(void);

/* Append queued events for `kind` into a DIDEVICEOBJECTDATA array. `elem` is
 * the caller's element stride, `max` the number of free slots. When `peek` is
 * set the events stay queued. Returns how many were written. */
uint32_t input_take_events(int32_t kind, void *buf, uint32_t elem,
                           uint32_t max, int32_t peek);

/* Human-readable summary of what is currently held. */
void input_describe(char *out, uint32_t cap);

/* Map a key name ("Return", "Up", "a", "F1", ...) to a DIK scancode.
 * Returns 0 when unknown. Also accepts "0x1c" style literals. */
uint8_t input_dik_from_name(const char *name);


#ifdef __cplusplus
}
#endif

#endif /* AM2_INPUT_H */
