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

/* 0x00420060. Point a named object at a different sprite frame. The name must
 * be type 2 and its uid must resolve to a valid object; every failure logs and
 * returns, leaving the object alone. */
void __cdecl ScriptSetObjBitmap(int32_t nameidx, int32_t frame);

/* The 40-byte message the event system puts on the wire. Derived from BOTH
 * sides -- EventMessageSend writes these offsets and EventMessageReceive reads
 * the same ones back -- so the layout is confirmed rather than inferred from
 * one direction.
 *
 * `maskA` and `maskB` went in as aux1/aux2, positional names, because neither
 * of these functions logs them or looks at them. EventTriggerImmediate settles
 * it: they are the maskA/maskB arguments of FilterMatches -- the SETS the
 * event belongs to, against which an entry's negative key is a subset test.
 * Neither uid takes part in matching at all.
 *
 * The three padding bytes after `type` are never written. The original leaves
 * them as whatever the stack held, and so does this -- the struct is a local
 * and only the named fields are assigned. */
typedef struct {
    uint16_t len;          /* +0x00, always 0x28 */
    uint16_t kind;         /* +0x02, AM2_ARMY_MSG_EVENT */
    uint32_t uid;          /* +0x04, the transport's own uid slot;
                            * EventMessageSend always writes 0 */
    uint8_t  type;         /* +0x08, the event type, narrowed to a byte */
    int32_t  num1;         /* +0x0C */
    uint32_t uid1;         /* +0x10, through UidOnWire */
    int32_t  maskA;        /* +0x14, a set membership; see above */
    int32_t  num2;         /* +0x18 */
    uint32_t uid2;         /* +0x1C, through UidOnWire */
    int32_t  maskB;        /* +0x20 */
    int32_t  removeevent;  /* +0x24 */
} AM2_EventMsg;

/* 0x0041F150. Pack those eight values into a message and hand it to
 * ArmyMessageSend. */
void __cdecl EventMessageSend(int32_t type, int32_t num1, uint32_t uid1,
                              int32_t maskA, int32_t num2, uint32_t uid2,
                              int32_t maskB, int32_t removeevent);

/* 0x0041F320. The other end: unpack one and raise it locally through
 * EventTriggerImmediate with `remote` set. */
void __cdecl EventMessageReceive(const AM2_EventMsg *msg);

/* 0x0041EF80. Raise an event now: walk the bucket `type` indexes and run every
 * matching entry's handlers. Broadcasts to peers once, unless `remote`.
 * `removeevent` unlinks and frees the entry afterwards. */
void __cdecl EventTriggerImmediate(int32_t type, int32_t num1, uint32_t uid1,
                                   int32_t maskA, int32_t num2, uint32_t uid2,
                                   int32_t maskB, int32_t removeevent,
                                   int32_t remote);

/* 0x00421430. Run an `if` statement's actions according to its mode: all, one
 * at random, one round-robin, or the one whose `onobjstate` name matches the
 * object's current state. Nine callers. */
void __cdecl RunCondActions(AM2_ScriptCond *c, void *arg);

/* 0x0041F520. Resolve a script name index to the uid it stands for. `me`
 * resolves to the caller's context instead; anything out of range, or not a
 * type-2 entry, gives 0. Fifty-three callers. */
uint32_t __cdecl ResolveUid(int32_t name, uint32_t me);

/* 0x0041F410. Raise an event after `delay`: allocate the 16-byte record the
 * handler will be given, start a timer, and register ADDR_EVT_RECORD_HANDLER
 * against the timer's id with `owns` set, so the teardown frees the record.
 * Does nothing further if the timer returns -100 or -101. */
void __cdecl EventTriggerDelayed(int32_t type, int32_t num, int32_t uid,
                                 int32_t delay, int32_t removeevent,
                                 int32_t arg);

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
/* 0x0041FAB0. Same shape as the two below, but gated on ObjIsType2 alone
 * rather than the 2/3/8 set, and writing +0x540.
 *
 * That offset is worth noting against misc.cpp's Field53C, which READS +0x53C
 * on some object: the two are adjacent dwords, and a record with fields past
 * 0x540 is far larger than AM2_Object's header. Whatever type 2 is, it carries
 * a substantial tail. */
void __cdecl EvtSetField540(uint32_t uid, int32_t value);

void __cdecl EvtSetModeF0(uint32_t uid, int32_t value);
void __cdecl EvtSetMode94(uint32_t uid, int32_t value);
void __cdecl EvtSetFlag810(uint32_t uid, int32_t on);
void __cdecl EvtSetOwner(uint32_t uid, int8_t owner);
void __cdecl EvtSetByte40(uint32_t uid, int8_t value);

/* 0x00420040. EvtSetByte40's type-3 counterpart, and the guard is the whole
 * difference between them: that one takes whatever LookupByUID returned and
 * checks only that it is non-null, while this one goes through
 * LookupType3ByUID and so writes to nothing but a type 3. Neither applies the
 * uid floor the rest of the family does.
 *
 * +0x530 is a third field in the same far tail: ObjType2Field548 reads +0x548
 * and EvtSetField540 writes +0x540, both on type 2, and this writes +0x530 on
 * type 3. Two different types with live fields past 0x500 says the two
 * records are alike out there, which is as much as these three accessors can
 * establish on their own. */
void __cdecl EvtSetByte530(uint32_t uid, int8_t value);

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

/* 0x0041EC70. Read one `if` record back, the mirror of SaveScriptCond, with
 * the same parameter order: the FILE first.
 *
 * It clears `next` BEFORE the read rather than after, which works because the
 * read is 0x30 bytes and `next` is at +0x30 -- just past the end. The counts
 * come out of the record itself, so each array is allocated from a number the
 * file supplied and then filled.
 *
 * An action's text was written as a length followed by the bytes. This reads
 * the length from the +0x30 slot, reads that many bytes into a 2 KB buffer,
 * terminates it, and hands it to ScriptAddToken as a kind-5 token -- the kind
 * that owns a malloc'd copy. The action's text then points at the token's
 * value, so the string outlives this function and is freed with the token list
 * rather than with the action.
 *
 * The buffer is 0x800 bytes and the length is whatever the file says, so a
 * corrupt save overruns the stack. That is the original's behaviour. */
void __cdecl LoadScriptCond(am2_FILE *fp, AM2_ScriptCond *cond);

/* 0x004225E0. Read the event.cpp section of a save file back, re-registering
 * every event it holds. Returns 0 if the section tag is wrong, 1 otherwise --
 * including when the section is empty.
 *
 * Each registration is three dwords and then a kind-dependent tail:
 *
 *   AM2_EVTSAVE_PAD_A / _B   one more dword, an index into ADDR_PADS. The pad
 *                            gets the key stored at +0x28 and is registered in
 *                            bucket 0 with handler A or B, not owned.
 *   AM2_EVTSAVE_OWNED        a 16-byte record malloc'd and read from the file,
 *                            registered in bucket 1 and OWNED, so the table's
 *                            teardown frees it.
 *
 * Two oddities, both reproduced. The first of the three dwords is read and
 * never used -- the key that reaches EventRegister is the second, and key1 is
 * passed as a literal 0 rather than the saved value. And that second dword is
 * read into the incoming `fp` argument's own stack slot, which the original
 * reuses as a local once it has copied fp into a register. */
int32_t __cdecl LoadEventSection(am2_FILE *fp);

/* 0x00422470. The mirror of LoadEventSection: walk all nine buckets, and for
 * every handler that is one of three known kinds write a record.
 *
 * It is the saver that explains two things the loader could only show the
 * shape of.
 *
 * The first of the loader's three dwords -- the one it reads and drops -- is
 * the BUCKET INDEX. So the file records which bucket a registration came from
 * and the loader ignores it, putting pads in bucket 0 and owned records in
 * bucket 1 regardless. Reproduced: writing anything else there would be a
 * change to the format for no reason, and the loader would not notice.
 *
 * And a pad handler's argument is a POINTER into ADDR_PADS, which is stored as
 * an INDEX -- the original divides by 72 with the usual reciprocal multiply,
 * 0x38E38E39 shifted right by 4. That is the AM2_Pad stride confirmed a fourth
 * way, after the two block lengths and ResetPads' stosd count.
 *
 * ONLY THREE HANDLER KINDS ARE SAVED. Anything registered with a different
 * function is skipped in silence, so a save does not round-trip the whole
 * table -- only the pads and the owned records. The two pad arms additionally
 * require a non-null argument; the owned arm does not check.
 *
 * The bucket walk stops at ADDR_SCRIPT_CONDITIONS, the next global, which is
 * the same bound the teardown uses and the second independent statement that
 * the table has nine buckets. */
int32_t __cdecl SaveEventSection(am2_FILE *fp);

 /* 0x0041EDD0. Read the whole condition list back from a save.
 *
 * Frees whatever is there first, then reads records until the marker stops
 * matching, and finishes by calling DeclareRuleVars -- which walks the list it
 * has just built and registers every event term. So loading a save rebuilds
 * both the conditions and their registrations, and nothing else has to know
 * the difference between a loaded mission and a parsed one.
 *
 * DeclareRuleVars runs even when the section is empty: the early exit for a
 * wrong first marker jumps INTO the tail rather than returning.
 *
 * Records are prepended, so the list comes out in reverse file order. Since
 * SaveScriptConditions walks the same list to write it, saving and loading
 * twice returns to the original order. */
int32_t __cdecl LoadScriptConditions(am2_FILE *fp);

/* 0x0041EC20. The mirror of LoadScriptConditions, and the ninth of nine
 * save/load section pairs -- SaveGame and LoadGame call their halves in the
 * same order, each saver sitting immediately before its loader in the image.
 *
 * It brackets the list with tags and writes one record marker before each
 * condition, which is the shape SaveItems has too. Three things it does not
 * do: it checks no write, it always answers 1, and it does not touch the list
 * -- so unlike the loader, which frees the list before it starts, this one is
 * safe to call twice.
 *
 * The list is walked HEAD FIRST, and the loader prepends, so a save/load round
 * trip reverses the order. Two round trips restore it. That was already known
 * from the loader's side; this is the half that makes it true. */
int32_t __cdecl SaveScriptConditions(am2_FILE *fp);

/* 0x0041E9E0 and 0x0041EA20. event.cpp's other savegame section, and the whole
 * of it: one tag, the block's length written as a second tag, and 16008 bytes
 * out of 0x0050C368.
 *
 * The length check is the part worth knowing. WriteSaveTag and CheckSaveTag
 * are general enough that a length travels through them exactly as a tag does,
 * so the loader verifies the size it is about to read without any code that
 * knows it is a size. That is why 0x00003E88 was recorded among the section
 * tags: from the loader's side it is indistinguishable from one.
 *
 * The saver checks nothing and always answers 1. The loader answers 0 if
 * either check fails and reads NOTHING -- so a truncated or foreign save
 * leaves the block as it was, which is the opposite of LoadItems, which
 * empties the item list before its tag check. */
int32_t __cdecl SaveEventBlock(am2_FILE *fp);
int32_t __cdecl LoadEventBlock(am2_FILE *fp);

int event_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_EVENT_H */
