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

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct {
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

/* A declared name -- a script variable or object. Sixteen bytes; only the
 * first field is established, and the rest is whatever ScriptDeclareVar
 * (0x0043F7A0) writes. Named opaquely rather than guessed at. */
typedef struct {
    const char *name;
    uint8_t     rest[12];
} AM2_ScriptName;

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

int script_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SCRIPT_H */
