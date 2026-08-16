/* Observing an original function without replacing it.
 *
 * patch_replace() is one-way: it overwrites the target so the original body can
 * never run again. That is right for reconstruction, but useless for finding
 * out what a function we have *not* reconstructed actually does at runtime.
 *
 * observe_install() takes the opposite approach and leaves the function
 * completely untouched, rewriting the rel32 of each `call target` so it reaches
 * a logging stub which then jumps on to the real function. Arguments are
 * recorded, behaviour is unchanged, and no trampoline is required.
 *
 * Only direct `call rel32` sites can be redirected this way -- calls made
 * through function pointers or vtables still reach the original directly and
 * will not be seen. Site lists come from tools/callsites.py.
 */

#ifndef AM2_OBSERVE_H
#define AM2_OBSERVE_H

#include <stdint.h>

int observe_install(uint32_t target, const char *name, int32_t nargs,
                    const uint32_t *sites, int32_t nsites);

#define OBSERVE(target, name, nargs, table) \
    observe_install((target), (name), (nargs), (table), \
                    (int32_t)(sizeof(table) / sizeof((table)[0])))

#endif /* AM2_OBSERVE_H */
