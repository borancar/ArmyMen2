/* A small Lua console reachable over the control socket, so a dev session can
 * poke memory and call game functions without a rebuild -- e.g. give the
 * leader a weapon, teleport, flip a flag. Dev-only: linked into the socket
 * builds (am2hook.dll, armymen2-dev, armymen2-hybrid-dev) and nothing else.
 *
 * Bindings are deliberately tiny -- peek/poke and a raw cdecl `call` -- so
 * everything else is written in Lua on top. See the `lua` command in
 * control.c. Over the control socket, commands run on the SOCKET thread,
 * exactly as `poke` already does, so they race the game thread: fine for
 * interactive poking, NOT frame-exact. For a frame-exact modification (a give
 * that must land on the same pump in two lockstepped games) use the step
 * socket's `lua` action instead -- tools/sidebyside.py's inject file -- which
 * runs the same chunk through am2_host_lua on the game thread at the pump
 * boundary on both sides. */
#ifndef AM2_LUACONSOLE_H
#define AM2_LUACONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Lazily created on first eval; safe to call again. */
void        am2_lua_init(void);

/* Run `code` as a chunk in the persistent state. Returns a pointer to a
 * static buffer holding whatever the chunk print()ed plus any error, valid
 * until the next call. Never NULL. */
const char *am2_lua_eval(const char *code);

#ifdef __cplusplus
}
#endif

#endif
