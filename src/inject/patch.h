/* Installing our reconstructed functions over the originals. */

#ifndef AM2_PATCH_H
#define AM2_PATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Overwrite the first five bytes at `target` with `jmp rel32`, so every
 * existing call site reaches our code instead. This is a one-way replacement:
 * the original body becomes unreachable and there is no trampoline back to it.
 *
 * Only safe when nothing branches *into* the five bytes being overwritten.
 * tools/checkdetour.py verifies that offline before a target is added here.
 *
 * `nargs` is the number of stack dwords the function takes, used only for
 * argument tracing under AM2_TRACE=1. Pass 0 if unknown; pass 1 for a variadic
 * function to log just its format argument. It has no effect otherwise, and
 * reconstructed functions never need to know whether they are being traced.
 *
 * Returns 0 on success, non-zero on failure.
 */
int patch_replace(uint32_t target, const void *replacement, const char *name,
                  int32_t nargs);

/* Put a single byte back, having first checked what is there.
 *
 * Separate from patch_replace because it is a different kind of change: not
 * installing our code over the game's, but undoing an edit someone made to the
 * shipping executable. `expect` is what the byte must currently be; if it is
 * anything else nothing is written and this fails, so a wrong address or a
 * different build is loud rather than silently corrupting code.
 *
 * Returns 0 on success.
 */
int patch_byte(uint32_t target, uint8_t expect, uint8_t value, const char *what);

/* Number of patches installed so far. */
int patch_count(void);


#ifdef __cplusplus
}
#endif

#endif /* AM2_PATCH_H */
