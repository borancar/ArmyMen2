/* noinput.cpp -- the release binary's answer to the harness's injected input.
 *
 * device.cpp's PollMouse and PollKeyboard merge the control socket's
 * injected keys and mouse events into what DirectInput returned, through
 * src/inject/input.c. The player's binary carries no control socket and no
 * input.c, so those three entries answer "nothing injected" here; the
 * development binary (AM2_DEVTOOLS) links the real input.c instead. */
#ifndef AM2_DEVTOOLS

#include <stdint.h>
#include <stddef.h>

extern "C" {

void input_pump(void) {}

uint32_t input_take_events(int32_t kind, void *buf, uint32_t elem,
                           uint32_t max, uint32_t flags)
{
    (void)kind; (void)buf; (void)elem; (void)max; (void)flags;
    return 0;
}

void input_overlay_keyboard(uint8_t *state, uint32_t len)
{
    (void)state; (void)len;
}

}

#endif /* !AM2_DEVTOOLS */
