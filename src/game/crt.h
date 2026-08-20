/* crt.h -- the allocator the game is already using.
 *
 * The script token list is malloc'd, realloc'd and freed, and the buffers are
 * shared with original code that has not been reconstructed yet. Whoever frees
 * a block must be the one who allocated it, so the reconstruction cannot use
 * the harness DLL's own libc heap here -- it has to reach the statically linked
 * MSVC CRT inside ArmyMen2.exe, at the addresses that CRT's own callers use.
 *
 * This is not a general escape hatch. Nearly all of src/game/ is arithmetic
 * over memory the caller supplies and needs none of it; the seam exists for the
 * handful of places that pass ownership of a block across the boundary between
 * ours and the original's, which right now is the tokeniser and nothing else.
 *
 * The pointers are variables rather than fixed addresses for the reason
 * src/game/image.h gives: the offline test maps the image as inert data and
 * cannot call into it, so it points these at the host libc instead and the same
 * reconstruction runs unmodified.
 */
#ifndef AM2_CRT_H
#define AM2_CRT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void *(__cdecl *am2_malloc)(size_t);
extern void *(__cdecl *am2_realloc)(void *, size_t);
extern void  (__cdecl *am2_free)(void *);

/* The game's own logger. Same seam and the same reason: it is a function in
 * the image, so the offline test -- which maps the image as inert data -- has
 * to point it somewhere else. Defaults to dropping the message. */
extern void (__cdecl *am2_log)(const char *, ...);

/* The game's own _chdir -- SetCurrentDirectoryA with the CRT's drive-relative
 * bookkeeping behind it. Same seam again: SetGameDir is the one reconstructed
 * function that changes the process's directory, and the process it has to
 * change is the game's, through the CRT the rest of the image is already
 * using. The offline test points it at the host's. */
extern int32_t (__cdecl *am2_chdir)(const char *);

/* Point them at the game's own CRT. Called once by the harness, after the
 * image is known to be mapped at its base. */
void am2_crt_use_game(void);

/* Point them at whatever libc this build linked. For the offline test, where
 * the image is data and cannot be called. */
void am2_crt_use_host(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_CRT_H */
