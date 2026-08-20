/* event.cpp -- registering the things a mission can react to.
 *
 * The original's own module: docs/functions.tsv puts 0x00421C70 inside
 * event.cpp's span, along with the registration table and every callback
 * named here.
 *
 * Only the declaring is reconstructed. The table itself, its teardown, the
 * uid counter and the notify are reached by address (see orig.h), which is
 * this project's usual shape for a function that sits on top of a subsystem
 * not yet taken: our code runs in the middle of a live path and the A/B
 * compares the result.
 */
#ifndef AM2_EVENT_H
#define AM2_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x00421C70, the last thing LoadLevelScript does.
 *
 * Three groups: the eight fixed win conditions, three rule events that take a
 * fresh uid each, and one registration per event term of every `if` the script
 * parsed -- which is why it runs after the parse and not before. */
void __cdecl DeclareRuleVars(void);

/* 0x0041EE70. Add one handler for the (key0, key1) pair in `bucket`, making
 * the entry if this is the first. `owns` makes the teardown free `arg` too.
 * A key0 of AM2_EVENT_NO_KEY registers nothing. */
void __cdecl EventRegister(int32_t bucket, int32_t key0, int32_t key1,
                           const void *fn, void *arg, int32_t owns);

/* 0x004223D0. Empty all nine buckets. */
void __cdecl EventClearAll(void);

int event_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_EVENT_H */
