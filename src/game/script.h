/* script.cpp -- the mission-script interpreter.
 *
 * The game ships its missions as readable text under data/<map>/, so this is
 * the one subsystem whose names come from the program's own vocabulary rather
 * than from us. docs/scripttokens.md lists all 185 keywords and the seven
 * token kinds; orig.h carries the call chain from LoadLevelScript down.
 */
#ifndef AM2_SCRIPT_H
#define AM2_SCRIPT_H

#include <stdint.h>

#include "objscript.h"
#include "scripttokens.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0x0043EE80. Space, tab or carriage return. NOT newline -- a reconstruction
 * that included it would still tokenise every shipped script correctly, since
 * NextToken tests for newline separately before asking. */
int32_t __cdecl IsBlank(uint8_t c);

/* 0x0043EEA0. One of ) ( , < = > { } & +. Exactly the first character of each
 * of the thirteen operator tokens, which is what makes the pairing of <=, >=
 * and <> a second step rather than part of this. */
int32_t __cdecl IsScriptDelim(uint8_t c);

/* 0x0043EEE0. The id for a keyword, or -1.
 *
 * A linear walk of the 185-entry table at 0x00487C90, comparing with an inlined
 * strcmp. Linear rather than sorted or hashed, and the table is not in id
 * order, so nothing cleverer is available without changing behaviour on a tie
 * -- and there are ties: `mine` and `yours` are both id 88.
 *
 * The comparison is case-SENSITIVE and every entry in the table is lower case.
 * The caller lower-cases the word first -- _strlwr at 0x0046D7D6, whose ASCII
 * path is `cmp 0x41 / cmp 0x5A / add 0x20` -- which is why the scripts can
 * write `Pad`, `pad` and `PAD` interchangeably and all three resolve. */
int32_t __cdecl ScriptLookupToken(const char *word);

/* The parse context at 0x00656478 and the records it holds.
 *
 * Both layouts are read out of the code that builds them -- AddToken indexes
 * with `lea edx,[eax+eax*2]` scaled by 4, so the stride is 12, and it stores
 * its kind at +0, its line at +4 and the value at +8. Grow adds ten entries at
 * a time and reallocs. */
typedef struct {
    int32_t kind;            /* AM2_TOKEN_*; docs/scripttokens.md */
    int32_t line;            /* the script line the token came from */
    void   *value;           /* kind 5 owns a copy; 1..4 hold a dword */
} AM2_ScriptTok;

typedef struct AM2_ScriptCtx {
    int32_t        capacity;
    int32_t        count;
    AM2_ScriptTok *tokens;
} AM2_ScriptCtx;

/* 0x0043F000. Release one token's value. Only kind 5 (String) owns one. */
void __cdecl ScriptFreeToken(AM2_ScriptTok *tok);

/* 0x0043F340. Ten more entries. Not a doubling -- a fixed step, so a long
 * script pays a realloc every ten tokens. Kept as-is. */
void __cdecl ScriptGrowTokens(AM2_ScriptCtx *ctx);

/* 0x0043F370. Append one token, growing first if the list is full.
 *
 * `value` is a pointer for every kind: kinds 1..4 store the dword it points
 * at, kind 5 stores a malloc'd copy of the string, and any other kind -- 0, or
 * 6 (Name) -- advances the count leaving the value field untouched. That last
 * case is the original's behaviour and not an oversight to tidy up: the switch
 * covers exactly `kind - 1` in 0..4. */
void __cdecl ScriptAddToken(AM2_ScriptCtx *ctx, int32_t kind,
                            const void *value, int32_t line);

/* 0x0043F2F0. Release every token and the array, and zero the context. */
void __cdecl ScriptResetTokens(AM2_ScriptCtx *ctx);

/* 0x0043EF70. Is this word a number? 0 no, 1 integer, 2 float.
 *
 * The first character must be `-` or a digit; every character after it must be
 * a digit or a single `.`. A `.` anywhere makes it a float, so "1." is 1.0 and
 * not the integer 1 -- which the EULA text shipped alongside the scripts, with
 * its numbered headings, is what proved. */
int32_t __cdecl ScriptParseNumber(const char *text, int32_t *ival, float *fval);

/* 0x0043F450. Split one line into tokens and append them to the context.
 *
 * Stops at end of line, at a newline, and at `\` or `/` -- the second is the
 * comment marker, and one slash is enough: `//` is what the scripts write but
 * a single `/` ends the line just as well.
 *
 * Words are lower-cased and looked up. An id of 0..13 is a Control Character
 * (kind 1), anything higher is Reserved (kind 2), and a word that is not in
 * the table is passed to ScriptParseNumber: Integer (3), Float (4), or String
 * (5) if it is not a number at all. Quoted text is always a String.
 *
 * Delimiters are looked up the same way, after `<`, `>` and `=` are given the
 * chance to pair with a following `=` or `>` -- which is what makes `<=`, `>=`
 * and `<>` single tokens. */
void __cdecl ScriptNextToken(const char *line, AM2_ScriptCtx *ctx,
                             int32_t lineno);

/* A declared name -- a script variable or object. Sixteen bytes, and every
 * field is established by AddNameTableName, which writes all four. */
typedef struct {
    char   *name;       /* a malloc'd copy; the table owns it */
    int32_t type;       /* AM2_NAME_TYPE_* */
    int32_t value;      /* a uid for type 0, the caller's value otherwise */
    int32_t refs;       /* a reference count: AddNameTableName sets it to 1
                         * and ScriptNameUid increments it on every reuse */
} AM2_ScriptName;

/* 0x0041E7F0. The next object uid. A bare post-increment of a global. */
int32_t __cdecl AllocUid(void);

/* 0x0043F7A0. Append a name to the table and return its index.
 *
 * Named from its own error string. Grows ten entries at a time like the token
 * list, copies the name onto the game's heap, and marks the entry live.
 *
 * Type 0 ignores `uid` and allocates a fresh one; 1, 2 and 3 store what they
 * are given. Any other type logs and then stores it anyway -- the entry is
 * still appended and the count still advances, so this is a complaint rather
 * than a rejection. */
int32_t __cdecl AddNameTableName(const char *name, int32_t type, int32_t uid);

/* 0x00443F70. The `variable` statement: `variable <name> <integer>`.
 *
 * Declares the name with type 3 and then overwrites its value with the
 * integer that follows. Returns 1 on success, 0 on any error, and on error
 * leaves *at wherever it got to -- ReadScript then carries on from there.
 *
 * It REWRITES the name token in place: the String token becomes kind 7 with
 * the table index as its value, and the string it owned is freed. That is
 * where kind 7 comes from, and why ScriptTokenText's kind-7 arm resolves
 * through the name table. */
int32_t __cdecl ScriptVariable(AM2_ScriptCtx *ctx, int32_t *at);

/* 0x0043EF40. The keyword whose id this is, or NULL. The reverse of
 * ScriptLookupToken, over the same table and just as linear. */
const char *__cdecl ScriptTokenName(int32_t id);

/* 0x0043F670. The index of a declared name, or -1. */
int32_t __cdecl ScriptFindName(const char *name);

/* 0x00444A90. Render one token as text into `out`, which it returns.
 *
 * Eight arms for seven kinds. Kind 0 gives "unknown", 1 and 2 the keyword's
 * own spelling, 3 "%d", 4 "%6.2f", 5 the string itself, and 7 the entry in the
 * name table that the value indexes. Kind 6 -- the one the kind table actually
 * calls `Name` -- writes NOTHING, sharing its arm with the out-of-range case.
 *
 * So the renderer disagrees with the kind table by one: there is no name for
 * kind 7 (index 7 of the array at 0x00487C74 is the keyword table's first
 * entry, `(`), yet 7 is the arm that resolves a name. Neither kind is emitted
 * by NextToken, so both are produced by a later pass, and which is which is
 * not established here. Recorded rather than tidied. */
char *__cdecl ScriptTokenText(const AM2_ScriptTok *tok, char *out);

/* 0x00444B80. Does the token at *at begin a top-level statement?
 *
 * A byte table over ids 0x19..0x8C, and it answers yes for exactly the six
 * that ReadScript dispatches on: preloadsprite, pad, if, variable, object and
 * objclass. The handlers use it to find where their own statement ends. */
int32_t __cdecl ScriptIsStatementStart(const AM2_ScriptCtx *ctx,
                                       const int32_t *at);

/* 0x00444CD0. Read a script file, tokenise every line, then interpret.
 *
 * Two passes over one context: the whole file is tokenised first, then the
 * token list is walked and each statement handed to its handler. Returns 0 if
 * the file could not be opened.
 *
 * Tokens already in the context are left alone -- the walk starts at the count
 * on entry -- so a caller can accumulate several files into one list. */
int32_t __cdecl ReadScript(const char *path, AM2_ScriptCtx *ctx);

/* 0x00444900. `preloadsprite <int> <int> <int> [<int>]`.
 *
 * Three required integers identifying a sprite, and an optional fourth that
 * defaults to 0x1000. The optional one is genuinely optional: end of script or
 * a non-Integer token there is not an error, it just takes the default and
 * leaves the token for ReadScript to dispatch. */
int32_t __cdecl ScriptPreloadSprite(AM2_ScriptCtx *ctx, int32_t *at);


/* A pad: a region of the map that triggers when something enters it. The
 * parser writes the first ten fields; the record is 72 bytes. */
typedef struct {
    int32_t id;             /* +0x00, its own index */
    int32_t number;         /* +0x04, the number the script gave, 0..255 */
    int32_t name;           /* +0x08, name-table index */
    int32_t compared;       /* +0x0C, 1 once a <, = or > has been seen */
    int32_t specific;       /* +0x10, 1 when a named item is the trigger */
    int32_t trigger;        /* +0x14, the flag set -- or the name index when
                             * `specific`, which is why the two are exclusive */
    int32_t compare;        /* +0x18, 0 for '=', 1 for '<', 2 for '>' */
    int32_t threshold;      /* +0x1C */
    int32_t delay0;         /* +0x20, from the optional `delay` clause */
    int32_t delay1;         /* +0x24 */
    uint8_t rest[72 - 0x28];
} AM2_Pad;

/* Everything sharing one pad number, and where that number sits on the map.
 * The centroid is in sixteenths of a cell. */
typedef struct {
    int16_t count;          /* +0x00 */
    int16_t pads[32];       /* +0x02, indices into the pad array */
    int16_t cx;             /* +0x42 */
    int16_t cy;             /* +0x44 */
    uint8_t rest[76 - 0x46];
} AM2_PadNumber;

/* 0x004440E0. `pad <name> <number> <trigger-words...> [<=> <int>] [delay a b]`.
 *
 * Declares the name with type 1, rewrites the name token to kind 7 as
 * `variable` does, computes the region's centroid the first time a number is
 * seen, then reads trigger keywords until one it does not recognise. */
int32_t __cdecl ScriptPad(AM2_ScriptCtx *ctx, int32_t *at);

/* One parsed action -- 0x48 bytes, and what every statement that does
 * something produces. ScriptParseAction fills it and the runtime consumes it.
 *
 * The fields are reused heavily, because 59 actions share one record: what
 * `subject` and `target` mean depends on `code`, and `n0`/`n1`/`army`/`extra`
 * are whatever that action needed. The names below are the dominant role, and
 * the notable exceptions are:
 *
 *   subject   the object, army, pad or region the action is about -- but
 *             `activateregion` puts a region NUMBER there, and `setvar` a
 *             variable's name index
 *   target    the second name or army; also where ScriptLocation puts a
 *             location it resolved to a name
 *   xvar/yvar a coordinate given as `refvar <name>` -- and xvar doubles as
 *             the variable for `tracevar`, `setvar` and `setfacing refvar`
 *   army      an army colour, but `dropitem` puts the item there and
 *             `fireweapon` the weapon
 *   extra     a damage kind, an order form, or `onobjstate`'s name
 *
 * uid/uid2 are only used by `trigger` and `triggerdelay`; the two unused
 * dwords are never written by the parser at all and stay zero from the memset.
 */
typedef struct AM2_ScriptAction {
    int32_t uid;            /* +0x00, opens as -2 */
    int32_t uid2;           /* +0x04 */
    int32_t delay;          /* +0x08 */
    int32_t unused0c;       /* +0x0C, never written here */
    int32_t unused10;       /* +0x10, never written here */
    int32_t code;           /* +0x14, which action -- see docs/scriptactions.md */
    int32_t subject;        /* +0x18 */
    int32_t target;         /* +0x1C */
    union {
        struct { int16_t x, y; } pos;   /* +0x20 */
        int32_t                  both;  /* a pad centroid, two words at once */
    } u;
    int32_t relative;       /* +0x24, a leading `+` on the coordinates */
    int32_t xvar;           /* +0x28 */
    int32_t yvar;           /* +0x2C */
    char   *text;           /* +0x30, the token's own string, not a copy */
    int32_t item;           /* +0x34, a weapon or pickup code */
    int32_t n0;             /* +0x38 */
    int32_t n1;             /* +0x3C */
    int32_t army;           /* +0x40 */
    int32_t extra;          /* +0x44 */
} AM2_ScriptAction;

/* One event term in an `if` condition -- three values from the event parser,
 * and a fourth field it always writes as zero. */
typedef struct {
    int32_t a, b, c, d;
} AM2_ScriptEvent;

/* One `testvar` comparison: two operand triples and an operator code. */
typedef struct {
    int32_t left[3];
    int32_t right[3];
    int32_t op;         /* 0 '=', 1 '<>', 2 '<', 3 '>', 4 '<=', 5 '>=' */
} AM2_ScriptTest;

/* An `if` statement. Malloc'd, zeroed, and pushed onto the list at
 * ADDR_SCRIPT_CONDITIONS when it parses. 0x34 bytes. */
typedef struct {
    int32_t           kind;       /* +0x00, see ScriptIf */
    int32_t           number;     /* +0x04, the count for repeat/count/time */
    int32_t           nevents;    /* +0x08 */
    AM2_ScriptEvent  *events;     /* +0x0C */
    int32_t           unused10;   /* +0x10 */
    int32_t           ntests;     /* +0x14 */
    AM2_ScriptTest   *tests;      /* +0x18 */
    int32_t           nactions;   /* +0x1C */
    AM2_ScriptAction *actions;    /* +0x20 */
    int32_t           mode;       /* +0x24, 0 none 1 random 2 sequential
                                   *        3 onobjstate */
    int32_t           unused28;   /* +0x28 */
    int32_t           objstate;   /* +0x2C, a name index when mode is 3 */
    void             *next;       /* +0x30 */
} AM2_ScriptCond;

/* 0x004432F0. The `if` statement -- by a wide margin the largest of the five.
 *
 *   if [timeabsolute <n> | allof | inorder | repeat <n> of | count <n> of
 *      | <event> after <event> [and ...] | <expr> [butnot <event>]]
 *      [testvar <value> <op> <value> [and ...]]
 *      then [random|sequential|onobjstate <name>] <action> [, <action>...]
 *
 * `kind` records which form was taken: 0 plain, 1 allof, 2 inorder, 3 count,
 * 4 repeat, 5 timeabsolute, 6 after-chain, 7 butnot after a keyword form,
 * 8 butnot after a string form.
 *
 * Returns 1 on success, having pushed the record onto the condition list.
 * ReadScript counts those returns as its `compounds` total. */
/* 0x0043F9F0. The uid a name stands for, declaring it if it is new.
 *
 * Only type 0 -- object -- names qualify; a name already declared as anything
 * else logs "Duplicate name used for different types" and returns 0, which the
 * caller cannot tell from a legitimate uid of 0. Reusing an existing name
 * bumps its reference count; declaring a new one does not, so the count is
 * uses-after-the-first rather than uses. */
int32_t __cdecl ScriptNameUid(const char *name);

/* 0x00442F80. An integer literal or a declared `variable`.
 *
 * On success *value is the literal or the name-table index and *isliteral says
 * which. Anything else -- a name that is not declared, or declared as
 * something other than a variable -- returns 0 without a message. */
int32_t __cdecl ScriptIntOrVar(AM2_ScriptCtx *ctx, int32_t *at,
                               int32_t *value, int32_t *isliteral);

/* 0x0043FF00. A String naming an object, resolved to its uid. The third
 * argument is always written as zero and the fourth gets the uid. */
int32_t __cdecl ScriptObjectUid(AM2_ScriptCtx *ctx, int32_t *at,
                                int32_t *zero, int32_t *uid);

/* 0x00440930. One of `green`, `tan`, `blue`, `grey` -- ids 16..19 -- looked up
 * through the army table. Returns -1 on anything else, which is
 * distinguishable from the lookup's own result only by the caller knowing it. */
int32_t __cdecl ScriptArmyColour(AM2_ScriptCtx *ctx, int32_t *at);

/* 0x00440700. Resolve the token at *at to a name-table index.
 *
 * Three cases. A Reserved word in 15..20 is one of the built-in uids -- the
 * four armies and `me` -- and yields it directly. Any other Reserved word is
 * an error. A String is looked up, declared with type 2 if new, and the token
 * is REWRITTEN to kind 7 exactly as `variable` does; a name already declared
 * as something other than type 2 or 3 is rejected.
 *
 * `quiet` suppresses both the reserved-word message and the wrong-kind one,
 * but not the already-used-for-another-type one. Every caller so far passes 0. */
int32_t __cdecl ScriptResolveName(AM2_ScriptCtx *ctx, int32_t *at,
                                  int32_t *out, int32_t quiet);

/* 0x00443010. One operand of a `testvar` comparison.
 *
 * *kind says which form, and *a and *b carry its arguments:
 *
 *   0  an integer literal, in *a
 *   1  a declared variable, its name index in *a
 *   2  getdmglvl <obj>          3  gethealth <obj>
 *   4  getdisguise <obj>        5  hasitem <obj> <item>
 *   6  iscoloringame <army>     7  isally <army> <army>
 *   8  teamscore <army>
 *
 * Anything else is "Unrecognized operand in testvar clause." */
int32_t __cdecl ScriptParseValue(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *a, int32_t *b);

/* 0x0043FAB0. Who a `hit` or `killed` event is about.
 *
 * Either a named object -- *mask is its name-table index -- or a descriptor
 * built from an army word and a type word, packed into the top bits:
 * 0x80000000 always, then green/tan/blue/grey as 0x40/0x20/0x10/0x08000000,
 * then item 0x04000000, sarge 0x01C00000, trooper 0x01400000, vehicle
 * 0x00200000. An unrecognised army word gives 0xF8000000, which means all four.
 *
 * Its "Unknown item descriptor" message cannot fire: the value starts at
 * 0x80000000 and is only ever OR'd, so the zero test guarding it is never
 * true. Kept as the original has it. */
int32_t __cdecl ScriptHitTarget(AM2_ScriptCtx *ctx, int32_t *at,
                                int32_t *mask);

/* 0x0043FCF0. Who an `order` or `setaimode` event is about.
 *
 * *form is 0 for a named object (then *val is its name index), 1 for an army
 * (then *army is 0..3 and *val is -1), or 2 for an army plus a `group <n>`
 * (then *val is the group number). */
int32_t __cdecl ScriptOrderTarget(AM2_ScriptCtx *ctx, int32_t *at,
                                  int32_t *form, int32_t *val, int32_t *army);

/* 0x0043FF90. One event term of an `if` condition.
 *
 *   <name>                          kind 0, *val is its uid
 *   padoff <name>                   kind 2, *val is the pad index
 *   padon  <name>                   kind 3
 *   killed <target> [by <target>]   kind 4
 *   hit    ...                      kind 5
 *   healed ...                      kind 6
 *   pickedup ...                    kind 7
 *   dropped  ...                    kind 8
 *
 * Only seven keywords reach an arm; every other reserved word is "Unexpected
 * reserved word in if statement." *val2 is zeroed on entry and stays zero
 * unless a `by` clause supplies it. */
int32_t __cdecl ScriptParseEvent(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *val, int32_t *val2);

/* 0x004409F0. Where an action happens, written into the action record.
 *
 * Three forms. `( <x> , <y> )` writes the pair at +0x20, and a leading `+`
 * makes it relative, recorded at +0x24; either coordinate may be
 * `refvar <name>` instead, which stores the variable's name index at +0x28 or
 * +0x2C and leaves the literal alone. A pad name resolves to that pad
 * number's centroid, straight into +0x20 as one dword, and rewrites the token
 * to kind 7. Anything else falls through to ScriptResolveName at +0x1C.
 *
 * `quiet` is passed on to ScriptResolveName and reaches nothing else. */
int32_t __cdecl ScriptLocation(AM2_ScriptCtx *ctx, int32_t *at,
                               AM2_ScriptAction *act, int32_t quiet);

/* 0x00442F10. Does `want` appear before `stop`, scanning from `from`? */
int32_t __cdecl ScriptScanFor(const AM2_ScriptCtx *ctx, int32_t from,
                              int32_t want, int32_t stop);

int32_t __cdecl ScriptIf(AM2_ScriptCtx *ctx, int32_t *at);

/* 0x00440600. Events until a `then`, `butnot` or `testvar`, appended to the
 * condition. Stops without complaint at anything that is neither a String nor
 * a Reserved word. */
int32_t __cdecl ScriptParseEvents(AM2_ScriptCtx *ctx, int32_t *at,
                                  AM2_ScriptCond *cond);


/* For the offline test only: the name table lives in the image, so the test
 * needs a way to empty it between cases and to read an entry back. Not part of
 * the reconstruction -- nothing in the game calls these. */
void                  am2_script_reset_names(void);
int32_t               am2_script_name_count(void);
const AM2_ScriptName *am2_script_name(int32_t i);

/* Non-zero makes every parsed action record print. Set from AM2_DUMP_ACTIONS
 * so the same scripted run can be compared with the original parser and with
 * ours, in the game, without an emulator. */
extern int32_t am2_dump_actions;

int script_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SCRIPT_H */
