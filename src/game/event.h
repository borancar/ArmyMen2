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

/* AM2_ScriptCond and am2_FILE. script.h is on the flat side of the split and
 * names no Win32 type, so including it here costs nothing in portability. */
#include "script.h"
#include "../inject/orig.h"

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

/* Two event sound triggers, both thin wrappers over PlayDynamicSound.
 *
 * EvtPlaySoundAt (0x0041F680) takes a packed AM2_Point and splits it into the
 * x and y parameters. The original does that with an UNALIGNED dword read at
 * &point + 2, taking y without shifting and carrying the next argument's low
 * half in the top of the register. That is safe, and it is worth saying why
 * rather than copying the trick: PlayDynamicSound truncates both to int16_t
 * on its first two lines, so the rubbish above y is discarded before anything
 * reads it. `point >> 16` is the same function.
 *
 * EvtPlaySoundOn (0x0041F6B0) passes x and y as zero and an owner uid instead.
 * PlayDynamicSound then looks the object up and takes the position from +0x12
 * and +0x14, so the sound follows the unit rather than a fixed spot. */
void __cdecl EvtPlaySoundAt(const char *name, uint32_t point, int32_t slot,
                            int32_t priority, int32_t loop);
void __cdecl EvtPlaySoundOn(const char *name, uint32_t owner, int32_t slot,
                            int32_t priority, int32_t loop);

/* 0x0041EA80. Free the whole ADDR_SCRIPT_CONDITIONS list.
 *
 * Each record owns three allocations -- events, tests and actions -- and the
 * three the original frees are at +0x0C, +0x18 and +0x20, which are exactly
 * those fields of AM2_ScriptCond. `next` at +0x30 matches too, so the struct
 * script.h already carries is confirmed from the teardown as well as from the
 * parser that builds it.
 *
 * `mode`, `objstate` and the two unused words are not freed, which is right:
 * they are values rather than pointers. */
void __cdecl FreeScriptConditions(void);

/* 0x0041F200. Write a placeholder name for an event that the script did not
 * name, into `out`.
 *
 * One arm per event kind, and the strings are the game's own vocabulary for
 * them -- Event_Control, Event_PadActivated, Event_ItemDestroyed and the rest.
 * They confirm the AM2_EVT_* codes in orig.h from a second direction: those
 * were read off the parser, these off the namer, and the two agree kind for
 * kind. Kind 1 gets an empty string, which is the same "produced by nothing"
 * the parser already implies.
 *
 * `out` is unbounded -- the original sprintf's straight into it. The longest
 * name it can write is "unnamed Event_PadDeactivated " plus the number. */
void __cdecl EventDefaultName(int32_t kind, int32_t number, char *out);

/* 0x0041EB00. Write one `if` record to a save file.
 *
 * The format, and every size in it is confirmed by a struct script.h already
 * carries:
 *
 *   the record itself, 0x30 bytes -- NOT 0x34, so the `next` pointer is left
 *     out, which is right for something being serialised
 *   each event, 0x10 bytes             AM2_ScriptEvent is four ints
 *   each action, 0x48 bytes            AM2_ScriptAction
 *   each test, 0x1C bytes              AM2_ScriptTest is seven ints
 *
 * The order is record, events, ACTIONS, then tests -- not the order the fields
 * appear in the struct, where tests come before actions.
 *
 * An action's `text` is a pointer, so it cannot be written as it stands. The
 * original copies the action to a local, replaces the pointer AT +0x30 with
 * the string's length, writes the copy, and then writes the bytes from the
 * original pointer. A null text writes a length of zero and nothing after it,
 * so the reader can tell the two cases apart.
 *
 * The FILE is the FIRST parameter, not the second. The prologue loads ebx from
 * entry esp+8 and ebp from esp+4, then uses ebx as the record and ebp as the
 * stream -- so the order reads backwards from the order the registers appear
 * in. Getting it the wrong way round writes the record to a bogus stream, and
 * the campaign A/B caught it: "Saved 317 items" simply never appeared. */
void __cdecl SaveScriptCond(am2_FILE *fp, const AM2_ScriptCond *cond);

int event_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_EVENT_H */
