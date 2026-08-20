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

/* ---- the object setters -------------------------------------------------
 *
 * Seven functions that reach an object by uid and write one field. They share
 * a shape and differ only in which guards they carry, and the differences are
 * real rather than sloppiness -- reproducing them is the point:
 *
 *   uid >= AM2_UID_COUNTER_MIN   rejects a uid below 1000, the value
 *                                objtable.h documents as the per-owner
 *                                counter's floor
 *   type in {2,3,8}              ObjIsTypeIn238 on the looked-up object
 *   non-null                     only two of them check
 *
 * EvtSetOwner and EvtSetFlag810 check the uid but NOT the result, so a uid at
 * or above 1000 that is not in the table writes through whatever LookupByUID
 * returned. EvtSetByte40 checks the pointer but not the uid. Neither is
 * defensible as written, and both are the original's behaviour. */
void __cdecl EvtSetModeF0(uint32_t uid, int32_t value);
void __cdecl EvtSetMode94(uint32_t uid, int32_t value);
void __cdecl EvtSetFlag810(uint32_t uid, int32_t on);
void __cdecl EvtSetOwner(uint32_t uid, int8_t owner);
void __cdecl EvtSetByte40(uint32_t uid, int8_t value);

/* 0x0041FF20 and 0x0041FF40, over a table of rows of four dwords at
 * 0x00511E60. The index arithmetic is `(col + row * 4) * 4`, so `row` selects
 * a group of four and `col` an entry in it. */
void __cdecl EvtMarkSet(int32_t row, int32_t col);
void __cdecl EvtMarkClear(int32_t row, int32_t col);

/* 0x0041F750. Clamps to 0x7FFF and stores 16 bits at +0x60. The clamp happens
 * before the lookup, so a value above 32767 is capped even for a uid that
 * turns out not to exist. Checks both the uid and the pointer. */
void __cdecl EvtSetWord60(uint32_t uid, int32_t value);

/* 0x0041F9B0. Sets the object's AI mode at +0xE4, pushing the previous one
 * down to +0xE8 first.
 *
 * Mode 7 is `defend` -- script.cpp's ActAiMode maps the four keywords to
 * 6/7/2/5, which is neither sequential nor in keyword order. Defending with no
 * post recorded latches the object's own packed position from +0x12, which is
 * how `setaimode defend` with no coordinates comes to defend where the unit is
 * standing.
 *
 * The emptiness test reads 16 bits at +0xB4 and the store writes 32. Kept. */
void __cdecl EvtSetAiMode(uint32_t uid, int32_t mode);

int event_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_EVENT_H */
