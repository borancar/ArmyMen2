/* Generic argument tracing for patched functions.
 *
 * Reconstructed functions contain no tracing code of their own. Instead, when
 * AM2_TRACE=1 the harness interposes a generated stub between the game's call
 * sites and our replacement, which logs the arguments and then tail-jumps to
 * the real implementation.
 *
 * This works because the target is 32-bit cdecl: every argument is a dword on
 * the stack, so one stub shape covers every signature -- it only needs to know
 * how many dwords to read. No trampoline is required either, because the stub
 * jumps forward into our own code rather than back into patched bytes.
 */

#ifndef AM2_TRACE_H
#define AM2_TRACE_H

#include <stdint.h>

/* Is tracing enabled for this run? (AM2_TRACE=1) */
int trace_enabled(void);

/* Return an executable stub that logs `nargs` stack dwords as `name`, then
 * jumps to `fn`. Returns `fn` unchanged when tracing is off or on failure, so
 * callers can use the result unconditionally. */
const void *trace_wrap(const void *fn, const char *name, int32_t nargs);

/* As trace_wrap(), but built regardless of AM2_TRACE. Used by call-site
 * observation, where the stub *is* the mechanism rather than a diagnostic
 * layered on top of one. Returns NULL on failure. */
const void *trace_make_stub(const void *fn, const char *name, int32_t nargs);

#endif /* AM2_TRACE_H */
