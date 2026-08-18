#ifndef AM2_STARTGAME_H
#define AM2_STARTGAME_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* Original: 0x0042ECF0. The menu's "start the selected game" button handler.
 *
 * Registered rather than called -- the only reference to it in the image is a
 * `push 0x42ecf0` at 0x0042EBCA handing it to a button constructor -- so it is
 * patched, not detoured through a caller.
 *
 * Joins the highlighted session if the row carries a DirectPlay connection, and
 * otherwise starts a local game with three computer opponents. The CD check on
 * the local path is disabled in this executable and reproduced that way; see
 * cdcheck.h and docs/binarypatches.md. */
void __cdecl StartSelectedGame(void);

/* Original: 0x0042FFF0. The OK on ENTER BATTLE NAME: validate the two typed
 * names, hand the display back to GDI, open a DirectPlay session and create
 * the hosting player. */
void __cdecl HostBattle(void);

int startgame_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_STARTGAME_H */
