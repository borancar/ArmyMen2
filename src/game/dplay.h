#ifndef AM2_DPLAY_H
#define AM2_DPLAY_H

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Creating the DirectPlay objects -- the game's entire networking boundary.
 *
 * It is worth saying why this is so small. Searching the import table for a
 * network library finds nothing: no ws2_32, no wsock32, no dplayx, and the
 * strings are not in the image either. The multiplayer transport is DirectPlay,
 * and DirectPlay is reached through COM, so the only trace of it in the imports
 * is ole32's CoCreateInstance -- twice, and these are both of them.
 *
 * So the whole of the game's outward network surface is here, and the rest of
 * the comm subsystem talks to an interface pointer.
 */

/* Original: 0x0040DD20. Create the IDirectPlay4A and give it a connection.
 *
 * thiscall: the comm object is in ecx and the connection blob is the one stack
 * argument. Drops any previous object first, then creates and -- if given a
 * connection -- initialises it and runs the connected hook. Returns 1 on
 * success, 0 on any failure. A null connection is not an error: it creates the
 * object and stops there, which is what enumerating providers wants. */
int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *comm, void *connection);

/* Original: 0x0040DDD0. Create the IDirectPlayLobby3A. stdcall, one argument.
 *
 * Writes the interface through the pointer whether or not it succeeded -- the
 * local it uses is zeroed first, so a failure stores NULL rather than leaving
 * the caller's pointer untouched. Returns 1 on success. */
int32_t __stdcall CreateDirectPlayLobby(LPDIRECTPLAYLOBBY3A *out);

/* Original: 0x004020A0. Shut the comm subsystem down: destroy the four
 * mutex-guarded message lists, signal the packet thread's event, collect its
 * exit code and close the handles. */
void __cdecl CommShutdown(void);

/* Original: 0x0040DCF0. Close the DirectPlay session.
 *
 * Answers 1 when there was no session to close, which is the same answer as
 * closing one successfully -- the caller only wants to know it can carry on. */
int32_t __cdecl CommClose(void);

/* Original: 0x0040DD90. Point the DirectPlay object at a connection.
 *
 * The same InitializeConnection CommCreateDirectPlay makes, reached separately
 * for a session that already has an object. Names the failure in the log. */
int32_t __attribute__((thiscall)) CommInitializeConnection(void *comm,
                                                           void *connection);

/* Original: 0x0040E630. Set the session description. */
int32_t __attribute__((thiscall)) CommSetSessionDesc(void *comm, void *desc,
                                                     uint32_t flags);

/* Original: 0x0040E5A0. Fetch the current session description into the comm
 * object, replacing whatever was there.
 *
 * Asked for twice on purpose: DirectPlay will not say how large the description
 * is except by refusing to write it, so the first call passes no buffer and
 * reads the size out of the complaint. Returns 1 on success. */
int32_t __attribute__((thiscall)) CommGetSessionDesc(void *comm);

/* Original: 0x0040DB80 and 0x0040DCC0, thiscall on the single global comm
 * object. The game's entire registry surface lives in these two: the key is
 * created at static-initialisation time and closed again from atexit, and
 * nothing is ever stored under it. See dplay.cpp. */
void *__attribute__((thiscall)) CommConstruct(void *comm);
void  __attribute__((thiscall)) CommDestruct(void *comm);

int dplay_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_DPLAY_H */
