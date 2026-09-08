/* A small Lua console reachable over the control socket, so a dev session can
 * poke memory and call game functions without a rebuild -- e.g. give the
 * leader a weapon, teleport, flip a flag. Dev-only: linked into the socket
 * builds (am2hook.dll, armymen2-dev, armymen2-hybrid-dev) and nothing else.
 *
 * Bindings are deliberately tiny -- peek/poke and a raw cdecl `call` -- so
 * everything else is written in Lua on top. See the `lua` command in
 * control.c. Commands run on the SOCKET thread, exactly as `poke` already
 * does, so they race the game thread; that is fine for interactive use and is
 * NOT for frame-exact reproduction (use the AM2_ env hooks for that). */
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
