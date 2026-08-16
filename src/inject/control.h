/* External control socket.
 *
 * Listens on 127.0.0.1 (port from AM2_CTL_PORT, default 31337) and accepts a
 * line protocol that feeds the injected input state. This is what makes the
 * game scriptable from outside the process without depending on the display
 * server delivering events:
 *
 *     key return tap          press and release Return
 *     key down down 500       hold Down for 500ms
 *     key a up                release A
 *     mouse move 40 -20       relative motion
 *     mouse left tap          click
 *     state                   report what is currently held
 *     clear                   release everything
 *     ping                    liveness check
 *
 * Every command replies with a single `ok ...` or `err ...` line, so a client
 * can wait for acknowledgement rather than guessing at timing.
 */

#ifndef AM2_CONTROL_H
#define AM2_CONTROL_H

/* Start the listener thread. No-op unless AM2_CONTROL=1. Returns 0 on success. */
int control_start(void);

void control_stop(void);

#endif /* AM2_CONTROL_H */
