/* script.cpp -- see script.h. */
#include <stdint.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crt.h"
#include "event.h"
#include "gamedir.h"
#include "savetag.h"
#include "misc.h"
#include "image.h"
#include "objscript.h"
#include "script.h"
#include "scriptint.h"
#include "pad.h"       /* PadFinalise -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"

#ifdef AM2_STANDALONE
/* The seven token-kind names at 0x00487C74 (a const char*[7], indexed by
 * kKindName), transcribed to C literals; the blob's name strings then drop. */
extern "C" const char *const am2_script_kind_names[7] = {
    "Unknown", "Control Character", "Reserved", "Integer",
    "Float", "String", "Name",
};
/* The five score-variable name slots at 0x00487C60 (const char*[5]), read by
 * name-table registration; transcribed so the blob name strings drop. The base
 * places the array, the other four alias into it. */
extern "C" const char *const am2_name_score_vars[5] = {
    "gamescorelimit", "greenscore", "tanscore", "bluescore", "greyscore",
};
/* The "unknown" word kUnknownWord returns for an unrecognised token: a char*
 * slot at 0x00488258 pointing at the string just after it. */
extern "C" const char *const am2_script_unknown_str[1] = { "unknown" };
#endif

/* PreloadSprite is reconstructed, in win32/sprite.cpp with the rest of the
 * sprite record. It is declared here rather than by including that header
 * because script.cpp is on the flat side of the split and must name no Win32
 * or COM type -- and AM2_Sprite has an LPDIRECTDRAWSURFACE in it. An
 * incomplete type is enough: the statement discards the sprite. */
struct AM2_Sprite;
AM2_Sprite *__cdecl PreloadSprite(int32_t set, int32_t index,
                                             int32_t frame, int32_t flags,
                                             int32_t addref);

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_str_fn)(const char *s);
typedef int32_t (__cdecl *am2_parse_action_fn)(AM2_ScriptCtx *ctx,
                                               int32_t *at,
                                               AM2_ScriptAction *act);
/* thiscall: ecx is loaded from the army table immediately before the call,
 * which is the tell for an i386 MSVC member function rather than a COM
 * dispatch. */

#define orig_parse_action      (*(am2_parse_action_fn)ADDR_SCRIPT_PARSE_ACTION)

int32_t __cdecl IsBlank(uint8_t c)
{
    return (c == ' ' || c == '\t' || c == '\r') ? 1 : 0;
}

int32_t __cdecl IsScriptDelim(uint8_t c)
{
    return (c == ')' || c == '(' || c == ',' || c == '<' || c == '=' ||
            c == '>' || c == '{' || c == '}' || c == '&' || c == '+') ? 1 : 0;
}

int32_t __cdecl ScriptLookupToken(const char *word)
{
    for (int32_t i = 0; i < AM2_SCRIPT_TOKEN_COUNT; i++)
        if (strcmp(word, am2_script_tokens[i].name) == 0)
            return am2_script_tokens[i].id;
    return -1;
}

void __cdecl ScriptFreeToken(AM2_ScriptTok *tok)
{
    if (tok->kind != AM2_TOKEN_STRING)
        return;
    if (tok->value)
        am2_free(tok->value);
    tok->value = 0;
}

void __cdecl ScriptGrowTokens(AM2_ScriptCtx *ctx)
{
    ctx->capacity += 10;
    ctx->tokens = (AM2_ScriptTok *)am2_realloc(
        ctx->tokens, (size_t)ctx->capacity * sizeof(AM2_ScriptTok));
}

void __cdecl ScriptAddToken(AM2_ScriptCtx *ctx, int32_t kind,
                            const void *value, int32_t line)
{
    if (ctx->count >= ctx->capacity)
        ScriptGrowTokens(ctx);

    /* The new token is re-indexed for every store. */
    ctx->tokens[ctx->count].kind = kind;
    ctx->tokens[ctx->count].line = line;

    switch (kind) {
    /* Four identical arms, written apart: VC6 folds shared labels into a
     * range test, and the image has the jump table. */
    case AM2_TOKEN_CONTROL_CHAR:
        ctx->tokens[ctx->count].value =
            (void *)(uintptr_t)*(const uint32_t *)value;
        break;
    case AM2_TOKEN_RESERVED:
        ctx->tokens[ctx->count].value =
            (void *)(uintptr_t)*(const uint32_t *)value;
        break;
    case AM2_TOKEN_INTEGER:
        ctx->tokens[ctx->count].value =
            (void *)(uintptr_t)*(const uint32_t *)value;
        break;
    case AM2_TOKEN_FLOAT:
        ctx->tokens[ctx->count].value =
            (void *)(uintptr_t)*(const uint32_t *)value;
        break;
    case AM2_TOKEN_STRING:
        ctx->tokens[ctx->count].value =
            am2_malloc(strlen((const char *)value) + 1);
        strcpy((char *)ctx->tokens[ctx->count].value, (const char *)value);
        break;
    }
    /* Kind 0 and kind 6 fall through with the value field left alone. */

    ctx->count++;
}

void __cdecl ScriptResetTokens(AM2_ScriptCtx *ctx)
{
    for (int32_t i = 0; i < ctx->count; i++)
        ScriptFreeToken(&ctx->tokens[i]);

    /* The original frees the array unconditionally, including when it is null
     * because nothing was ever parsed. free(NULL) is defined, so this is not a
     * defect to correct. */
    am2_free(ctx->tokens);
    ctx->tokens = 0;
    ctx->count = 0;
    ctx->capacity = 0;
}

int32_t __cdecl ScriptParseNumber(const char *text, int32_t *ival, float *fval)
{
    /* Plain (signed) chars, compared with `jl`/`jle`, and strlen taken again
     * on every pass, as the image does.
     *
     * The bound is `i < len`, so every character but the first is examined.
     * Getting this wrong is easy and I did: `repne scasb` from ecx = -1 counts
     * the terminator as well, so `not ecx` gives len+1 and the `dec` that
     * follows gives len -- not len-1. With the off-by-one, "1." came out as
     * the integer 1 where the original gives the float 1.0, which is what the
     * shipped text caught. */
    char    c = text[0];
    int32_t dots;
    int32_t i;

    if (c != '-' && (c < '0' || c > '9'))
        return 0;

    dots = 0;
    for (i = 1; i < (int32_t)strlen(text); i++) {
        c = text[i];
        if (c >= '0' && c <= '9')
            continue;
        if (c != '.')
            return 0;
        if (dots > 0)
            return 0;       /* a second `.` is not a number */
        dots++;
    }

    if (dots) {
        *fval = (float)atof(text);
        return 2;
    }
    *ival = (int32_t)atoi(text);
    return 1;
}

/* The word the tokeniser is assembling. A single global buffer in the image,
 * shared with every caller, so a token's text is only valid until the next one
 * is scanned -- which is why AddToken copies it for kind 5. 0x40 bytes: the
 * length check is against 0x3F and the terminator goes one past. */

/* The CRT's _strlwr, called rather than open-coded, as map.cpp and
 * objtype.cpp do. */
typedef char *(__cdecl *AM2_StrlwrFn)(char *);
#define orig_strlwr ((AM2_StrlwrFn)((void *)(uintptr_t)(ADDR_CRT_STRLWR)))

/* The character under the cursor is read again at every test, as the image
 * reads it; the three token calls with a pointer to a local are made from
 * separate statements that VC6 cross-jumps into one call. */
void __cdecl ScriptNextToken(const char *line, AM2_ScriptCtx *ctx,
                             int32_t lineno)
{
    int32_t at = 0;
    int32_t n, found, id, ival;
    float   fval;

    while (line[at] != 0) {
        /* `\` and `/` both end the line. The scripts write `//`, but the
         * second slash is never examined. */
        if (line[at] == '\\' || line[at] == '/')
            return;

        if (IsBlank((uint8_t)line[at])) {
            while (IsBlank((uint8_t)line[at]))
                at++;
        } else if (line[at] == '"') {
            n = 0;
            at++;
            while (line[at] != 0 && line[at] != '"') {
                kScriptWord[n] = line[at];
                n++;
                at++;
            }
            if (line[at] == '"')
                at++;
            kScriptWord[n] = 0;
            ScriptAddToken(ctx, AM2_TOKEN_STRING, kScriptWord, lineno);
        } else if (line[at] == '\n') {
            return;
        } else if (IsScriptDelim((uint8_t)line[at])) {
            kScriptWord[0] = line[at];
            kScriptWord[1] = 0;

            /* `<`, `>` and `=` may pair with a following `=` or `>`, which is
             * where `<=`, `>=` and `<>` come from. Nothing checks that the
             * first character is one of those three, so `,=` and `(>` would
             * pair too -- and then fail the lookup and vanish. */
            if (line[at + 1] == '=' || line[at + 1] == '>') {
                kScriptWord[1] = line[at + 1];
                kScriptWord[2] = 0;
                at++;
            }

            found = ScriptLookupToken(kScriptWord);
            if (found >= 0) {
                id = found;
                if (id <= 0xD)
                    ScriptAddToken(ctx, AM2_TOKEN_CONTROL_CHAR, &id, lineno);
            }
            /* An id above 13 is dropped here rather than emitted, which is the
             * mirror of the word path below. No delimiter has one. */
            at++;
        } else {
            /* A bare word: everything up to a blank, a newline, end of line
             * or a delimiter. */
            n = 0;
            while (!IsBlank((uint8_t)line[at]) && line[at] != '\n' &&
                   line[at] != 0 && !IsScriptDelim((uint8_t)line[at])) {
                if (n < 0x3F) {
                    kScriptWord[n] = line[at];
                } else if (n == 0x3F) {
                    /* Faithfully wrong: the original passes kScriptWord[0x3F]
                     * -- one sign-extended character -- where the format
                     * wants a string, so the message prints garbage for %s.
                     * Reproduced rather than corrected; nothing in the
                     * shipped scripts has a token this long, so it has almost
                     * certainly never printed. */
                    am2_log("Line [%4d]: Token too long truncating to: %s\n",
                            lineno, (int32_t)(signed char)kScriptWord[0x3F]);
                }
                n++;
                at++;
            }
            kScriptWord[n] = 0;

            /* Lower case, because the keyword table is lower case
             * throughout. */
            orig_strlwr(kScriptWord);

            found = ScriptLookupToken(kScriptWord);
            if (found >= 0) {
                id = found;
                if (id <= 0xD)
                    ScriptAddToken(ctx, AM2_TOKEN_CONTROL_CHAR, &id, lineno);
                else
                    ScriptAddToken(ctx, AM2_TOKEN_RESERVED, &id, lineno);
            } else {
                /* ival and fval are not initialised: ScriptParseNumber writes
                 * the one its answer names. */
                switch (ScriptParseNumber(kScriptWord, &ival, &fval)) {
                case 0:
                    ScriptAddToken(ctx, AM2_TOKEN_STRING, kScriptWord, lineno);
                    break;
                case 1:
                    ScriptAddToken(ctx, AM2_TOKEN_INTEGER, &ival, lineno);
                    break;
                case 2:
                    ScriptAddToken(ctx, AM2_TOKEN_FLOAT, &fval, lineno);
                    break;
                }
            }
        }
    }
}

const char *__cdecl ScriptTokenName(int32_t id)
{
    for (int32_t i = 0; i < AM2_SCRIPT_TOKEN_COUNT; i++)
        if (am2_script_tokens[i].id == id)
            return am2_script_tokens[i].name;

    return 0;
}


int32_t __cdecl ScriptFindName(const char *name)
{
    int32_t n = kScriptNameCount;

    /* strcmp, the intrinsic -- the two-bytes-a-step compare in the image. */
    for (int32_t i = 0; i < n; i++)
        if (strcmp(kScriptNames[i].name, name) == 0)
            return i;
    return -1;
}

/* Still original: the CRT lower-caser, the same seam map.cpp and definfo.cpp
 * already have. ScriptBindUniqueName calls it on its caller's buffer. */
typedef char *(__cdecl *AM2_StrlwrFn)(char *);
#define orig_strlwr       ((AM2_StrlwrFn)((void *)(uintptr_t)(ADDR_CRT_STRLWR)))

/* The nine globals DeclareBuiltinNames fills, each the name table INDEX of
 * one built-in name. */
#define g_svarId15        (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_ID15)))
#define g_svarGreen       (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_GREEN)))
#define g_svarTan         (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_TAN)))
#define g_svarBlue        (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_BLUE)))
#define g_svarGrey        (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_GREY)))
#define g_svarMe          (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_ME)))
#define g_svarDifficulty  (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_DIFFICULTY)))
#define g_svarSystemSpeed (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_SYSTEMSPEED)))
#define g_svarNumGreen    (*(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_NUMGREEN)))

/* DeclareBuiltinNames -- original 0x0043F6D0, one caller, and that caller is
 * the state-2 ENTRY, beside LoadDefaultCof. Empty the script name table and
 * put the nine names every mission script may use without declaring back into
 * it.
 *
 * SIX ARE TYPE 2 AND THREE ARE TYPE 3, which is the division worth keeping.
 * The six army keywords and `me` go in as AM2_NAME_TYPE_REF with a literal
 * value apiece -- the uid each keyword stands for. The last three go in as
 * AM2_NAME_TYPE_INTEGER, ordinary script variables, with values taken from
 * the game: the difficulty, ADDR_FAST_MACHINE, and a zero.
 *
 * `all` AND `green` ARE THE SAME VALUE, both 100. Every other keyword has its
 * own -- tan 200, grey 300, blue 400 -- so this is not a numbering scheme
 * with a gap in it, it is two names on one uid. Reproduced; nothing here says
 * whether "everyone" resolving to the player's own army is deliberate.
 *
 * IT NAMES THE ONE ADDR_SVAR_ID15 HOLDS. orig.h records that global as an id
 * "no entry in the keyword table produces", reachable only because two
 * functions compare a name index against it directly, and leaves the name
 * open. It is `all`: this is the only writer, and `all` is what it stores.
 *
 * `numgreen` goes in at ZERO and LoadDefaultCof overwrites it a moment later
 * with the trooper count, which is why that function can call
 * SetVarValueByName and expect the name to be there.
 *
 * The clear is FreeScriptNames, so a mission entered twice gets a fresh table
 * both times and the nine indices are re-derived rather than assumed.
 */
void __cdecl DeclareBuiltinNames(void)
{
    FreeScriptNames();

    g_svarId15 = AddNameTableName("all",
                                  AM2_NAME_TYPE_REF, AM2_SVAR_UID_ALL);
    g_svarGreen = AddNameTableName("green",
                                   AM2_NAME_TYPE_REF, AM2_SVAR_UID_GREEN);
    g_svarTan = AddNameTableName("tan",
                                 AM2_NAME_TYPE_REF, AM2_SVAR_UID_TAN);
    g_svarBlue = AddNameTableName("blue",
                                  AM2_NAME_TYPE_REF, AM2_SVAR_UID_BLUE);
    g_svarGrey = AddNameTableName("grey",
                                  AM2_NAME_TYPE_REF, AM2_SVAR_UID_GREY);
    g_svarMe = AddNameTableName("me",
                                AM2_NAME_TYPE_REF, AM2_SVAR_UID_ME);

    g_svarDifficulty = AddNameTableName(
        "difficulty", AM2_NAME_TYPE_INTEGER,
        *(const int32_t *)((void *)(uintptr_t)(ADDR_DIFFICULTY)));
    g_svarSystemSpeed = AddNameTableName(
        "systemspeed", AM2_NAME_TYPE_INTEGER,
        *(const int32_t *)((void *)(uintptr_t)(ADDR_FAST_MACHINE)));
    g_svarNumGreen = AddNameTableName(
        "numgreen", AM2_NAME_TYPE_INTEGER, 0);
}

/* 0x0043F910, one caller, and that caller is LoadScriptName. Bind a loaded
 * record to a name in the script name table, INVENTING A FRESH NAME when the
 * one on the file is already taken.
 *
 * Three outcomes, and the middle one is the reason this is not four lines.
 * The name is looked up, and:
 *
 *   - not there: it is appended with type AM2_NAME_TYPE_REF and the record's
 *     value, and the record keeps that index;
 *   - there but its VALUE IS ZERO: that entry is adopted -- the record keeps
 *     its index and the record's value is written into it. Nothing else about
 *     the entry is touched, so its type and its reference count survive;
 *   - there with a value: "%s_%d" against the ORIGINAL name and a counter
 *     from 1, and round again.
 *
 * The suffix does not accumulate, because every attempt formats from `name`
 * rather than from the last attempt: a third try is "sarge_2" and never
 * "sarge_1_1".
 *
 * IT LOWER-CASES THE CALLER'S BUFFER IN PLACE, which orig.h's `const char *`
 * denied for as long as the function was only ever reached through the image.
 * LoadScriptName's buffer is a local so nothing observes it afterwards -- but
 * that is a fact about the one caller, not about this function.
 *
 * ITS BUFFER IS 64 BYTES AND THE NAME IT IS HANDED CAN BE 256. LoadScriptName
 * reads a length off the file into a 0x100-byte local and passes it straight
 * down, so a long enough name in a save file overruns this frame as well as
 * that one. Reproduced, and the same reasoning applies: a file this game
 * wrote cannot do it, because the save half writes strlen + 1 of a table
 * entry. Two unchecked copies in a row is worth saying once. */
void __cdecl ScriptBindUniqueName(void *rec, char *name)
{
    uint8_t *r = (uint8_t *)rec;
    char     tried[AM2_SCRIPT_UNIQUE_BUF];
    int32_t  n;
    int32_t  index;

    if (!rec || !name)
        return;

    n = 1;
    orig_strlwr(name);
    strcpy(tried, name);

    index = ScriptFindName(tried);
    while (index >= 0) {
        if (kScriptNames[index].value == 0) {
            *(int32_t *)(r + SCRIPT_REF_OFF_NAME_INDEX) = index;
            kScriptNames[index].value =
                *(const int32_t *)(r + SCRIPT_REF_OFF_VALUE);
            return;
        }

        am2_sprintf(tried, (const char *)((void *)(uintptr_t)(AM2_STR_UNIQUE_SUFFIX)),
                    name, n++);
        index = ScriptFindName(tried);
    }

    *(int32_t *)(r + SCRIPT_REF_OFF_NAME_INDEX) =
        AddNameTableName(tried, AM2_NAME_TYPE_REF,
                         *(const int32_t *)(r + SCRIPT_REF_OFF_VALUE));
}

/* The original re-reads the count every iteration. Nothing in the loop can
 * change it, so a plain bound is the same function; noted rather than
 * transcribed. It frees the TABLE as well as the names, and zeroes all three
 * globals -- so the table is not merely emptied, it is gone. */
void __cdecl FreeScriptNames(void)
{
    for (int32_t i = 0; i < kScriptNameCount; i++)
        if (kScriptNames[i].name)
            am2_free(kScriptNames[i].name);

    am2_free(kScriptNames);
    kScriptNames     = 0;
    kScriptNameCount = 0;
    kScriptNameCap   = 0;
}

int32_t __cdecl SaveScriptSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_SCRIPT);
    orig_fwrite(&kScriptNameCount, 4, 1, fp);

    /* The table is re-read at every use. Length, then the bytes WITHOUT a
     * terminator, then the three fields that follow the pointer. */
    for (int32_t i = 0; i < kScriptNameCount; i++) {
        int32_t len = (int32_t)strlen(kScriptNames[i].name);

        orig_fwrite(&len, 4, 1, fp);
        orig_fwrite(kScriptNames[i].name, (size_t)len, 1, fp);
        orig_fwrite(&kScriptNames[i].type, 0xC, 1, fp);
    }
    return 1;
}

/* The original's frame is 0x118 bytes and the name is read into it with no
 * bound check at all -- a saved length longer than the buffer walks off the
 * stack. Reproduced with a buffer of the same order rather than hardened,
 * because the exact bound is not established and a guard here would be a
 * behaviour this build does not have. */
#define AM2_SCRIPT_NAME_BUF 0x100

int32_t __cdecl LoadScriptSection(am2_FILE *fp)
{
    char    name[AM2_SCRIPT_NAME_BUF];
    /* The three fields that follow the name in both the file and the
     * table entry, in the same order. */
    typedef struct { int32_t type, value, refs; } AM2_NameMeta;
    /* A whole table entry on the stack; only the three fields after the
     * name pointer are read into it. */
    AM2_ScriptName meta;
    int32_t count;

    if (!CheckSaveTag(fp, AM2_SAVETAG_SCRIPT,
                      "C:\\ArmyMen2\\source\\script.cpp", 0x1F8))
        return 0;

    /* After the tag check, so a foreign save leaves the table alone. */
    FreeScriptNames();

    orig_fread(&count, 4, 1, fp);

    /* Counted down on `count` itself; nothing indexes by the pass. */
    for (; count > 0; count--) {
        int32_t len;

        orig_fread(&len, 4, 1, fp);
        orig_fread(name, (size_t)len, 1, fp);
        name[len] = 0;
        orig_fread(&meta.type, 0xC, 1, fp);

        /* Ten at a time, the same step AddNameTableName uses. */
        if (kScriptNameCount >= kScriptNameCap) {
            kScriptNameCap += 10;
            kScriptNames = (AM2_ScriptName *)am2_realloc(
                kScriptNames, (size_t)kScriptNameCap * sizeof(AM2_ScriptName));
        }

        /* Re-indexed at every use, strlen and strcpy, as AddNameTableName. */
#define E kScriptNames[kScriptNameCount]
        E.name = (char *)am2_malloc(strlen(name) + 1);
        strcpy(E.name, name);
        /* type, value and refs arrive and land as one 12-byte block. */
        *(AM2_NameMeta *)&E.type = *(const AM2_NameMeta *)&meta.type;
        kScriptNameCount++;
#undef E
    }

    /* The two builtins a save must not freeze: they take the settings as
     * they are NOW, not as they were saved. Ours returned without these
     * until the relink showed the two calls. */
    SetVarValueByName("difficulty",
                      *(const int32_t *)(uintptr_t)ADDR_DIFFICULTY);
    SetVarValueByName("systemspeed",
                      *(const int32_t *)(uintptr_t)ADDR_FAST_MACHINE);
    return 1;
}

int32_t __cdecl GetVarValue(int32_t index, int32_t *out)
{
    if (index <= 0) {
        *out = 0;
        return 0;
    }

    if (kScriptNames[index].type == AM2_NAME_TYPE_INTEGER) {
        *out = kScriptNames[index].value;
        return 1;
    }

    am2_log("GetVarValue: name %s is not a variable!\n", kScriptNames[index].name);
    return 0;
}

/* No index check at all, unlike the getter. */
int32_t __cdecl SetVarValue(int32_t index, int32_t value)
{
    if (kScriptNames[index].type == AM2_NAME_TYPE_INTEGER) {
        kScriptNames[index].value = value;
        return 1;
    }

    am2_log("SetVarValue: name %s is not a variable!\n", kScriptNames[index].name);
    return 0;
}

int32_t __cdecl SetVarValueByName(const char *name, int32_t value)
{
    int32_t index = ScriptFindName(name);

    if (index <= 0) {
        am2_log("SetVarValue: bad variable name %s\n", name);
        return 0;
    }

    return SetVarValue(index, value);
}

#define kNextUid         (*(int32_t *)((void *)(uintptr_t)(ADDR_NEXT_UID)))

int32_t __cdecl AllocUid(void)
{
    return kNextUid++;
}

int32_t __cdecl AddNameTableName(const char *name, int32_t type, int32_t uid)
{
    if (kScriptNameCount >= kScriptNameCap) {
        kScriptNameCap += 10;
        kScriptNames = (AM2_ScriptName *)am2_realloc(
            kScriptNames, (size_t)kScriptNameCap * sizeof(AM2_ScriptName));
    }

    /* The entry is re-indexed at every use; the name is sized by strlen
     * and copied by strcpy, two `repne scasb` walks as the image has. */
#define E kScriptNames[kScriptNameCount]
    E.name = (char *)am2_malloc(strlen(name) + 1);
    strcpy(E.name, name);
    E.refs = 1;
    E.type = type;

    switch (type) {
    case 0:
        E.value = AllocUid();
        break;
    /* Three arms, identical, merged by the compiler -- written as one they
     * compile to a range test where the image has a four-slot table. */
    case 1:
        E.value = uid;
        break;
    case 2:
        E.value = uid;
        break;
    case 3:
        E.value = uid;
        break;
    default:
        am2_log("AddNameTableName: invalid type; %s has type %d and uid %d\n",
                name, type, uid);
        /* Stored anyway, and the count still advances. */
        E.value = uid;
        break;
    }
#undef E

    return kScriptNameCount++;
}

char *__cdecl ScriptTokenText(const AM2_ScriptTok *tok, char *out)
{
    /* Each case copies for itself and the one return is shared; VC6
     * cross-jumps the identical strcpy tails, all but the first. */
    switch (tok->kind) {
    case AM2_TOKEN_UNKNOWN:
        /* A `char *` stored in the image, pointing at the string just past the
         * keyword table -- so BOTH the slot and the pointer it holds are image
         * addresses. */
        strcpy(out, kUnknownWord);
        break;

    case AM2_TOKEN_CONTROL_CHAR:
    case AM2_TOKEN_RESERVED:
        strcpy(out, ScriptTokenName((int32_t)(uintptr_t)tok->value));
        break;

    case AM2_TOKEN_INTEGER:
        sprintf(out, "%d", (int32_t)(uintptr_t)tok->value);
        break;

    case AM2_TOKEN_FLOAT:
        /* The value field holds the float's bits, not a pointer to them. */
        sprintf(out, "%6.2f", (double)*(const float *)&tok->value);
        break;

    case AM2_TOKEN_STRING:
        strcpy(out, (const char *)tok->value);
        break;

    case AM2_TOKEN_NAMEREF:
        strcpy(out, kScriptNames[(uintptr_t)tok->value].name);
        break;

    /* Kind 6, and anything above 7, leave `out` untouched. */
    }
    return out;
}

int32_t __cdecl ScriptIsStatementStart(const AM2_ScriptCtx *ctx,
                                       const int32_t *at)
{
    const AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind != AM2_TOKEN_RESERVED)
        return 0;

    switch ((int32_t)(uintptr_t)tok->value) {
    case AM2_TOK_PRELOADSPRITE:
    case AM2_TOK_PAD:
    case AM2_TOK_IF:
    case AM2_TOK_VARIABLE:
    case AM2_TOK_OBJECT:
    case AM2_TOK_OBJCLASS:
        return 1;
    default:
        return 0;
    }
}

/* The idiom every statement handler opens each of its arguments with:
 * advance, check for end of script, check the kind. The original writes it out
 * inline at each site -- four times in preloadsprite alone -- with identical
 * text; this is the same sequence, and the messages are the same two strings
 * the image holds.
 *
 * Returns the token on success, NULL after logging. *at is advanced either
 * way, which is what lets a failed handler leave ReadScript somewhere sane.
 */

int32_t __cdecl ScriptPreloadSprite(AM2_ScriptCtx *ctx, int32_t *at)
{
    int32_t        flags = 0x1000;
    AM2_ScriptTok *tok;
    int32_t        set, index, frame;

    /* The three arguments are read one after another, each through its own
     * copy of ScriptExpect. */
    if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
        return 0;
    tok = &ctx->tokens[*at];
    set = (int32_t)(uintptr_t)tok->value;
    if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
        return 0;
    tok = &ctx->tokens[*at];
    index = (int32_t)(uintptr_t)tok->value;
    if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
        return 0;
    tok = &ctx->tokens[*at];
    frame = (int32_t)(uintptr_t)tok->value;

    /* The fourth is optional, and the index advances past the third whether or
     * not it is there -- so a missing one leaves *at on whatever follows and
     * ReadScript dispatches that normally. */
    (*at)++;
    if (*at < ctx->count &&
        ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
        flags = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
    }

    PreloadSprite(set, index, frame, flags, 1);
    return 1;
}

int32_t __cdecl ScriptVariable(AM2_ScriptCtx *ctx, int32_t *at)
{
    AM2_ScriptTok *tok;
    if (!ScriptExpect(ctx, at, AM2_TOKEN_STRING))
        return 0;
    tok = &ctx->tokens[*at];

    if (ScriptFindName((const char *)tok->value) >= 0) {
        am2_log("Line [%4d]:  Duplicate variable name.\n",
                ctx->tokens[*at].line);
        return 0;
    }

    int32_t slot = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_INTEGER, 0);

    /* The token owned that string; the name table has its own copy now. */
    am2_free(ctx->tokens[*at].value);
    ctx->tokens[*at].kind = AM2_TOKEN_NAMEREF;
    ctx->tokens[*at].value = (void *)(uintptr_t)slot;

    if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
        return 0;
    tok = &ctx->tokens[*at];

    kScriptNames[slot].value = (int32_t)(uintptr_t)tok->value;
    (*at)++;
    return 1;
}


/* True when the token at *at is Reserved with this id. */
static inline int ScriptAtWord(const AM2_ScriptCtx *ctx, int32_t at, int32_t id)
{
    const AM2_ScriptTok *t = &ctx->tokens[at];
    return t->kind == AM2_TOKEN_RESERVED &&
           (int32_t)(uintptr_t)t->value == id;
}

/* Inline: the image has this body at each call site. */
/* Append one event to a condition. A macro, not an inline function: the
 * image stores the caller's own locals, where an inline's by-value
 * parameters are copies VC6 spills first. */
#define SCRIPT_ADD_EVENT(C, A, B, D)                                        \
    do {                                                                    \
        int32_t n_ = (C)->nevents++;                                        \
        (C)->events = (AM2_ScriptEvent *)am2_realloc(                       \
            (C)->events, (size_t)(C)->nevents * sizeof(AM2_ScriptEvent));   \
        (C)->events[n_].a = (A);                                            \
        (C)->events[n_].b = (B);                                            \
        (C)->events[n_].c = (D);                                            \
        (C)->events[n_].fired = 0;                                          \
    } while (0)

int32_t __cdecl ScriptResolveName(AM2_ScriptCtx *ctx, int32_t *at,
                                  int32_t *out, int32_t quiet)
{
    /* The token is addressed afresh at every use. */
#define tok (&ctx->tokens[*at])

    if (tok->kind == AM2_TOKEN_RESERVED) {
        /* Each arm reads its own variable into *out. */
#define SVAR(a) (*(const int32_t *)((void *)(uintptr_t)(a)))
        switch ((int32_t)(uintptr_t)tok->value) {
        case 15:            *out = SVAR(ADDR_SVAR_ID15);  break;   /* no keyword maps here */
        case AM2_TOK_GREEN: *out = SVAR(ADDR_SVAR_GREEN); break;
        case AM2_TOK_TAN:   *out = SVAR(ADDR_SVAR_TAN);   break;
        case AM2_TOK_BLUE:  *out = SVAR(ADDR_SVAR_BLUE);  break;
        case AM2_TOK_GREY:  *out = SVAR(ADDR_SVAR_GREY);  break;
        case AM2_TOK_ME:    *out = SVAR(ADDR_SVAR_ME);    break;
        default:
            if (!quiet)
                am2_log("Line [%4d]:  Unexpected use of reserved word\n",
                        ctx->tokens[*at].line,
                        ScriptTokenText(tok, kScriptWord));
            return 0;
        }
#undef SVAR
        (*at)++;
        return 1;
    }

    /* Quiet is asked first, and the kind in each arm. */
    if (quiet) {
        if (tok->kind != AM2_TOKEN_STRING)
            return 0;
    } else if (tok->kind != AM2_TOKEN_STRING) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_STRING));
        return 0;
    }

    int32_t idx = ScriptFindName((const char *)tok->value);
    *out = idx;

    if (idx < 0) {
        *out = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_REF, 0);
    } else {
        int32_t type = kScriptNames[idx].type;
        if (type != AM2_NAME_TYPE_REF && type != AM2_NAME_TYPE_INTEGER) {
            am2_log("Line [%4d]:  Name '%s' already used for another type.\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord));
            return 0;
        }
    }

    /* Either way the token becomes a kind-7 reference and gives up its
     * string -- including when the name already existed. */
    am2_free(ctx->tokens[*at].value);
    ctx->tokens[*at].kind = AM2_TOKEN_NAMEREF;
    ctx->tokens[*at].value = (void *)(uintptr_t)*out;
    (*at)++;
    return 1;
#undef tok
}

int32_t __cdecl ScriptParseEvents(AM2_ScriptCtx *ctx, int32_t *at,
                                  AM2_ScriptCond *cond)
{
    for (;;) {
        int32_t k = ctx->tokens[*at].kind;
        int32_t a, b, c;

        if (k != AM2_TOKEN_STRING && k != AM2_TOKEN_RESERVED)
            return 1;

        if (!ScriptParseEvent(ctx, at, &a, &b, &c))
            return 0;                   /* silent -- the callee has spoken */
        {
            int32_t n = cond->nevents++;

            cond->events = (AM2_ScriptEvent *)am2_realloc(
                cond->events, (size_t)cond->nevents * sizeof(AM2_ScriptEvent));
            cond->events[n].a     = a;
            cond->events[n].b     = b;
            cond->events[n].c     = c;
            cond->events[n].fired = 0;
        }

        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        {
            const AM2_ScriptTok *t = &ctx->tokens[*at];

            if ((t->kind == AM2_TOKEN_RESERVED
                 && (int32_t)(uintptr_t)t->value == AM2_TOK_THEN)
                || (t->kind == AM2_TOKEN_RESERVED
                    && (int32_t)(uintptr_t)t->value == AM2_TOK_BUTNOT)
                || (t->kind == AM2_TOKEN_RESERVED
                    && (int32_t)(uintptr_t)t->value == AM2_TOK_TESTVAR))
                return 1;
        }
    }
}

/* Both descriptor parsers start with "is it just a name?" and resolve it the
 * same way -- find it, declare it with type 2 if new. */
static inline int32_t ScriptNamedTarget(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *out)
{
    int32_t idx = ScriptFindName((const char *)ctx->tokens[*at].value);
    *out = idx;
    if (idx < 0)
        *out = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_REF, 0);
    return *out;
}

/* Advance past a word, reporting the end of the script. */
static inline int ScriptStep(AM2_ScriptCtx *ctx, int32_t *at)
{
    if (++(*at) < ctx->count)
        return 1;
    am2_log("Unexpected end of script.\n");
    return 0;
}

int32_t __cdecl ScriptHitTarget(AM2_ScriptCtx *ctx, int32_t *at, int32_t *mask)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_STRING) {
        ScriptNamedTarget(ctx, at, mask);
        (*at)++;
        return 1;
    }

    *mask = (int32_t)AM2_HIT_ANY;

    /* One arm per word, each stepping for itself -- the army, then the
     * kind. */
#define IS_WORD(t, w) ((t)->kind == AM2_TOKEN_RESERVED \
                       && (int32_t)(uintptr_t)(t)->value == (w))
#define ARM(w, bit)                     \
    if (IS_WORD(tok, w)) {              \
        if (!ScriptStep(ctx, at))       \
            return 0;                   \
        *mask |= (int32_t)(bit);        \
    }
    tok = &ctx->tokens[*at];
    ARM(AM2_TOK_GREEN, AM2_HIT_GREEN)
    else ARM(AM2_TOK_TAN, AM2_HIT_TAN)
    else ARM(AM2_TOK_BLUE, AM2_HIT_BLUE)
    else ARM(AM2_TOK_GREY, AM2_HIT_GREY)
    else
        *mask = (int32_t)AM2_HIT_ALL_ARMIES;

    tok = &ctx->tokens[*at];
    ARM(AM2_TOK_ITEM, AM2_HIT_ITEM)
    else ARM(AM2_TOK_SARGE, AM2_HIT_SARGE)
    else ARM(AM2_TOK_TROOPER, AM2_HIT_TROOPER)
    else ARM(AM2_TOK_VEHICLE, AM2_HIT_VEHICLE)
#undef ARM
#undef IS_WORD

    /* Unreachable -- see the header. */
    if (*mask == 0) {
        am2_log("Unknown item descriptor for HIT or KILLED event\n");
        return 0;
    }
    return 1;
}

int32_t __cdecl ScriptOrderTarget(AM2_ScriptCtx *ctx, int32_t *at,
                                  int32_t *form, int32_t *val, int32_t *army)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_STRING) {
        ScriptNamedTarget(ctx, at, val);
        *form = AM2_ORDER_NAME;
        (*at)++;
        return 1;
    }

    *val = (int32_t)AM2_HIT_ANY;

    /* One arm per colour word, each stepping for itself: green tan blue
     * grey are the reserved words 16 to 19. */
    tok = &ctx->tokens[*at];
#define IS_WORD(t, w) ((t)->kind == AM2_TOKEN_RESERVED \
                       && (int32_t)(uintptr_t)(t)->value == (w))
    if (IS_WORD(tok, 16)) {
        if (!ScriptStep(ctx, at))
            return 0;
        *army = 0;
    } else if (IS_WORD(tok, 17)) {
        if (!ScriptStep(ctx, at))
            return 0;
        *army = 1;
    } else if (IS_WORD(tok, 18)) {
        if (!ScriptStep(ctx, at))
            return 0;
        *army = 2;
    } else if (IS_WORD(tok, 19)) {
        if (!ScriptStep(ctx, at))
            return 0;
        *army = 3;
    } else {
        am2_log("Unknown item descriptor for ORDER or SETAIMODE event\n");
        return 0;
    }
    *form = AM2_ORDER_ARMY;

    tok = &ctx->tokens[*at];
    if (IS_WORD(tok, AM2_TOK_GROUP)) {          /* group */
        if (!ScriptStep(ctx, at))
            return 0;
        *form = AM2_ORDER_GROUP;
        *val = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ScriptStep(ctx, at))
            return 0;
    } else {
        *val = -1;
    }
#undef IS_WORD
    return 1;
}

#define kPads       ((AM2_Pad *)((void *)(uintptr_t)(ADDR_PADS)))
#define kPadCount   (*(int32_t *)((void *)(uintptr_t)(ADDR_PAD_COUNT)))
#define kPadNumbers ((AM2_PadNumber *)((void *)(uintptr_t)(ADDR_PAD_NUMBERS)))

/* ScriptLocation: each coordinate is an integer or `refvar <name>`, written
 * out at both uses because the image's two copies differ. X steps past the
 * token in each arm, and VC6 cross-jumps the refvar arm into the integer
 * arm's step; Y steps once after the join. The step past the comma and the
 * step after Y share one "Unexpected end of script." block. */
int32_t __cdecl ScriptLocation(AM2_ScriptCtx *ctx, int32_t *at,
                               AM2_ScriptAction *act, int32_t quiet)
{
    int32_t idx;

    act->relative = 0;

#define tok (&ctx->tokens[*at])

    if (tok->kind == AM2_TOKEN_CONTROL_CHAR) {
        int32_t id = (int32_t)(uintptr_t)tok->value;
        if (id != AM2_TOK_LPAREN && id != AM2_TOK_PLUS)       /* '(' or '+' */
            goto fail;                  /* silently, with no message */

        if ((int32_t)(uintptr_t)tok->value == AM2_TOK_PLUS) { /* a leading '+' means relative */
            act->relative = 1;
            if (!ScriptStep(ctx, at))
                return 0;
        }
        if (!ScriptStep(ctx, at))       /* past the '(' */
            return 0;

        if (tok->kind == AM2_TOKEN_INTEGER) {
            act->u.pos.x = (int16_t)(uintptr_t)tok->value;
            if (!ScriptStep(ctx, at))
                return 0;
        } else if (tok->kind == AM2_TOKEN_RESERVED &&
                   (int32_t)(uintptr_t)tok->value == AM2_TOK_REFVAR) {
            if (!ScriptStep(ctx, at))
                return 0;
            idx = ScriptFindName((const char *)ctx->tokens[*at].value);
            if (idx == -1 || kScriptNames[idx].type != AM2_NAME_TYPE_INTEGER) {
                am2_log("Line [%4d]:  expected variable name but variable %s "
                        "not declared\n",
                        ctx->tokens[*at].line, ctx->tokens[*at].value);
                return 0;
            }
            act->xvar = idx;
            if (!ScriptStep(ctx, at))
                return 0;
        } else {
            am2_log("Line [%4d]:  '%s' found, but expected token of type "
                    "Integer or keyword 'RefVar'\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord));
            return 0;
        }
        if (tok->kind != AM2_TOKEN_CONTROL_CHAR ||
            (int32_t)(uintptr_t)tok->value != AM2_TOK_COMMA) {      /* ',' */
            am2_log("Line [%4d]:  Comma expected in coordinates\n",
                    ctx->tokens[*at].line);
            return 0;
        }

        if (++(*at) >= ctx->count)      /* past the ',' */
            goto eos;

        if (tok->kind == AM2_TOKEN_INTEGER) {
            act->u.pos.y = (int16_t)(uintptr_t)tok->value;
        } else if (tok->kind == AM2_TOKEN_RESERVED &&
                   (int32_t)(uintptr_t)tok->value == AM2_TOK_REFVAR) {
            if (!ScriptStep(ctx, at))
                return 0;
            idx = ScriptFindName((const char *)ctx->tokens[*at].value);
            if (idx == -1 || kScriptNames[idx].type != AM2_NAME_TYPE_INTEGER) {
                am2_log("Line [%4d]:  expected variable name but variable %s "
                        "not declared\n",
                        ctx->tokens[*at].line, ctx->tokens[*at].value);
                return 0;
            }
            act->yvar = idx;
        } else {
            am2_log("Line [%4d]:  '%s' found, but expected token of type "
                    "Integer or keyword 'RefVar'\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord));
            return 0;
        }
        if (++(*at) >= ctx->count) {
eos:
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (tok->kind == AM2_TOKEN_CONTROL_CHAR &&
            (int32_t)(uintptr_t)tok->value == AM2_TOK_RPAREN) {  /* ')' */
            (*at)++;
            return 1;
        }
        am2_log("Line [%4d]:  Close parens expected in coordinates\n",
                ctx->tokens[*at].line);
fail:
        return 0;
    }

    if (tok->kind == AM2_TOKEN_STRING) {
        idx = ScriptFindName((const char *)tok->value);
        if (idx >= 0 && kScriptNames[idx].type == AM2_NAME_TYPE_PAD) {          /* a pad */
            int32_t number = kPads[kScriptNames[idx].value].number;
            /* Both centroid words at once, exactly as the original. */
            act->u.both = *(const int32_t *)&kPadNumbers[number].cx;

            am2_free(ctx->tokens[*at].value);
            ctx->tokens[*at].kind = AM2_TOKEN_NAMEREF;
            ctx->tokens[*at].value = (void *)(uintptr_t)idx;
            (*at)++;
            return 1;
        }
        /* Not a pad -- fall through, and the name is resolved below. */
    }

    return ScriptResolveName(ctx, at, &act->target, quiet) != 0;
#undef tok
}

/* ScriptParseEvent: one `case` per event keyword, each written out in full
 * as the image has them -- the five `<verb> <target> [by <target>]` events and
 * the two pad events differ only in the kind they record and the messages
 * they print, which is what the macros carry. */
#define PAD_EVENT(K, NO_NAME, NOT_A_PAD)                                    \
    *kind = (K);                                                            \
    if (!ScriptStep(ctx, at))                                               \
        return 0;                                                           \
    tok = &ctx->tokens[*at];                                                \
    if (tok->kind != AM2_TOKEN_STRING)                                      \
        goto wrong_type;                                                    \
    idx = ScriptFindName((const char *)tok->value);                         \
    if (idx < 0) {                                                          \
        am2_log(NO_NAME, ctx->tokens[*at].line);                            \
        return 0;                                                           \
    }                                                                       \
    /* Type 1 is what `pad` declares. */                                    \
    if (kScriptNames[idx].type != AM2_NAME_TYPE_PAD) {                      \
        am2_log(NOT_A_PAD, ctx->tokens[*at].line);                          \
        return 0;                                                           \
    }
/* `by <target>` is optional; without it *val2 keeps the zero written on
 * entry. */
#define HIT_EVENT(K, MISSING, MISSING_BY)                                   \
    *kind = (K);                                                            \
    if (!ScriptStep(ctx, at))                                               \
        return 0;                                                           \
    if (!ScriptHitTarget(ctx, at, val)) {                                   \
        am2_log(MISSING, ctx->tokens[*at].line);                            \
        return 0;                                                           \
    }                                                                       \
    tok = &ctx->tokens[*at];                                                \
    if (tok->kind != AM2_TOKEN_RESERVED                                     \
        || (int32_t)(uintptr_t)tok->value != AM2_TOK_BY)                    \
        return 1;                                                           \
    if (!ScriptStep(ctx, at))                                               \
        return 0;                                                           \
    if (!ScriptHitTarget(ctx, at, val2)) {                                  \
        am2_log(MISSING_BY, ctx->tokens[*at].line);                         \
        return 0;                                                           \
    }                                                                       \
    return 1

int32_t __cdecl ScriptParseEvent(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *val, int32_t *val2)
{
    AM2_ScriptTok *tok;
    int32_t        idx;

    *val2 = 0;

    tok = &ctx->tokens[*at];
    if (tok->kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)tok->value) {
        case AM2_TOK_PADON:
            PAD_EVENT(AM2_EVT_PADON,
                      "Line [%4d]:  Name required after PADON\n",
                      "Line [%4d]:  PADON name was not a pad\n");
        pad_found:
            /* The two pad events share this tail. */
            *val = kScriptNames[idx].value;
            (*at)++;
            return 1;
        case AM2_TOK_PADOFF:
            PAD_EVENT(AM2_EVT_PADOFF,
                      "Line [%4d]:  Name required after PADOFF\n",
                      "Line [%4d]:  PADOFF name was not a pad\n");
            goto pad_found;
        case AM2_TOK_KILLED:
            HIT_EVENT(AM2_EVT_KILLED,
                      "Line [%4d]:  Expected item name after KILLED\n",
                      "Line [%4d]:  Expected item name after KILLED ... BY\n");
        case AM2_TOK_HIT:
            HIT_EVENT(AM2_EVT_HIT,
                      "Line [%4d]:  Expected item name after HIT\n",
                      "Line [%4d]:  Expected item name after HIT ... BY\n");
        case AM2_TOK_HEALED:
            HIT_EVENT(AM2_EVT_HEALED,
                      "Line [%4d]:  Expected item name after HEALED\n",
                      "Line [%4d]:  Expected item name after HEALED ... BY\n");
        case AM2_TOK_PICKEDUP:
            HIT_EVENT(AM2_EVT_PICKEDUP,
                      "Line [%4d]:  Expected item name after PICKEDUP\n",
                      "Line [%4d]:  Expected item name after PICKEDUP ... BY\n");
        case AM2_TOK_DROPPED:
            HIT_EVENT(AM2_EVT_DROPPED,
                      "Line [%4d]:  Expected item name after DROPPED\n",
                      "Line [%4d]:  Expected item name after DROPPED ... BY\n");
        default:
            am2_log("Line [%4d]:  Unexpected reserved word in if statement.\n",
                    tok->line);
            return 0;
        }
    }

    /* A bare name: the event is that object, whatever happens to it. The
     * token is taken afresh from the cursor. */
    *kind = AM2_EVT_CONTROL;
    tok = &ctx->tokens[*at];
    if (tok->kind != AM2_TOKEN_STRING) {

wrong_type:
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_STRING));
        return 0;
    }

    *val = ScriptNameUid((const char *)tok->value);
    if (*val < 0)
        return 0;
    (*at)++;
    return 1;
}
#undef PAD_EVENT
#undef HIT_EVENT

int32_t __cdecl ScriptParseValue(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *a, int32_t *b)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    /* One arm per keyword form, each testing the token afresh and each with
     * its own exits -- the image's layout. A keyword's failures are silent:
     * the callee has already said whatever it is going to say. */
#define IS_KEYWORD(id) (tok->kind == AM2_TOKEN_RESERVED \
                        && (int32_t)(uintptr_t)tok->value == (id))
#define STEP_OR_FAIL()                                                      \
    if (++(*at) >= ctx->count) {                                            \
        am2_log("Unexpected end of script.\n");                             \
        return 0;                                                           \
    }
    if (IS_KEYWORD(AM2_TOK_GETDMGLVL)) {
        STEP_OR_FAIL();
        if (!ScriptResolveName(ctx, at, a, 0))
            return 0;
        *kind = AM2_VAL_GETDMGLVL;
        return 1;
    }
    if (IS_KEYWORD(AM2_TOK_GETHEALTH)) {
        STEP_OR_FAIL();
        if (!ScriptResolveName(ctx, at, a, 0))
            return 0;
        *kind = AM2_VAL_GETHEALTH;
        return 1;
    }
    if (IS_KEYWORD(AM2_TOK_GETDISGUISE)) {
        STEP_OR_FAIL();
        if (!ScriptResolveName(ctx, at, a, 0))
            return 0;
        *kind = AM2_VAL_GETDISGUISE;
        return 1;
    }
    if (IS_KEYWORD(AM2_TOK_HASITEM)) {
        STEP_OR_FAIL();
        if (!ScriptResolveName(ctx, at, a, 0))
            return 0;
        if (!ScriptResolveName(ctx, at, b, 0))
            return 0;
        *kind = AM2_VAL_HASITEM;
        return 1;
    }
    if (IS_KEYWORD(AM2_TOK_ISCOLORINGAME)) {
        STEP_OR_FAIL();
        *a = ScriptArmyColour(ctx, at);
        *kind = AM2_VAL_ISCOLORINGAME;
        return 1;
    }
    if (IS_KEYWORD(AM2_TOK_ISALLY)) {
        STEP_OR_FAIL();
        *a = ScriptArmyColour(ctx, at);
        *b = ScriptArmyColour(ctx, at);
        *kind = AM2_VAL_ISALLY;
        return 1;
    }
    if (IS_KEYWORD(AM2_TOK_TEAMSCORE)) {
        STEP_OR_FAIL();
        *a = ScriptArmyColour(ctx, at);
        *kind = AM2_VAL_TEAMSCORE;
        return 1;
    }
#undef STEP_OR_FAIL
#undef IS_KEYWORD

    /* An integer literal or a variable. The is-literal flag comes back in
     * `a`'s own argument slot -- dead once its value is passed -- which is
     * where the image keeps it. */
    if (!ScriptIntOrVar(ctx, at, a, (int32_t *)&a)) {
        am2_log("Line [%4d]:  Unrecognized operand in testvar clause.\n",
                ctx->tokens[*at].line);
        return 0;
    }
    *kind = *(const int32_t *)&a ? AM2_VAL_LITERAL : AM2_VAL_VARIABLE;
    return 1;
}

int32_t __cdecl ScriptNameUid(const char *name)
{
    int32_t n = kScriptNameCount;

    for (int32_t i = 0; i < n; i++) {
        if (strcmp(kScriptNames[i].name, name) == 0) {
            if (kScriptNames[i].type == AM2_NAME_TYPE_OBJECT) {
                kScriptNames[i].refs++;
                return kScriptNames[i].value;
            }
            am2_log("Duplicate name '%s' used for different types\n",
                    kScriptNames[i].name);
            return 0;
        }
    }

    /* Declaring it fresh does NOT bump the count -- it is left at the 1 that
     * AddNameTableName writes.
     *
     * THE CALL MUST COME BEFORE THE TABLE IS READ, and as one expression it
     * did not: `kScriptNames[AddNameTableName(...)]` let GCC load the table
     * pointer first, and AddNameTableName reallocates the table every tenth
     * name, so a forward reference that landed on a growth boundary read its
     * uid out of the FREED block -- 0 -- while the other forty names were
     * right. Boot Camp's `triggerdelay 100 showsign` was that name: the
     * delayed event was raised as (0, 0), nothing listened, and the port never
     * showed the instruction sign. tools/sidebyside.py found it as the first
     * frame past the tolerated 26 pixels; the actions oracle would have too,
     * had anything run it since. MSVC calls first and loads after. */
    int32_t idx = AddNameTableName(name, AM2_NAME_TYPE_OBJECT, 0);
    return kScriptNames[idx].value;
}

int32_t __cdecl ScriptIntOrVar(AM2_ScriptCtx *ctx, int32_t *at,
                               int32_t *value, int32_t *isliteral)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_INTEGER) {
        *value = (int32_t)(uintptr_t)tok->value;
        *isliteral = 1;
        (*at)++;
        return 1;
    }
    if (tok->kind != AM2_TOKEN_STRING)
        return 0;

    int32_t idx = ScriptFindName((const char *)tok->value);
    if (idx < 0)
        return 0;
    if (kScriptNames[idx].type != AM2_NAME_TYPE_INTEGER)
        return 0;

    *value = idx;
    *isliteral = 0;
    (*at)++;
    return 1;
}

int32_t __cdecl ScriptObjectUid(AM2_ScriptCtx *ctx, int32_t *at,
                                int32_t *zero, int32_t *uid)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind != AM2_TOKEN_STRING) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_STRING));
        return 0;
    }

    *zero = 0;
    *uid = ScriptNameUid((const char *)ctx->tokens[*at].value);
    if (*uid < 0)
        return 0;

    (*at)++;
    return 1;
}

int32_t __cdecl ScriptArmyColour(AM2_ScriptCtx *ctx, int32_t *at)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind != AM2_TOKEN_RESERVED) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_RESERVED));
        return 0;
    }

    /* One call per colour -- VC6 merges them and leaves the constant
     * pushes. */
#define SLOT(n) (((AM2_Comm *)*(void **)((void *)(uintptr_t)(ADDR_ARMY_TABLE)))->CommSlotForArmy(n))
    int32_t rc;
    int32_t v = (int32_t)(uintptr_t)tok->value;

    if (v == AM2_TOK_GREEN)
        rc = SLOT(0);
    else if (v == AM2_TOK_TAN)
        rc = SLOT(1);
    else if (v == AM2_TOK_BLUE)
        rc = SLOT(2);
    else if (v == AM2_TOK_GREY)
        rc = SLOT(3);
    else {
        am2_log("Line [%4d]:  Expected Army Color instead of '%s'\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord));
        return -1;
    }
#undef SLOT

    (*at)++;
    return rc;
}

/* Set from AM2_DUMP_ACTIONS and AM2_PARSE_ALL. Read in script_install,
 * which the NATIVE build never runs, so the first script load reads them
 * too: AM2_DUMP_ACTIONS printed nothing there for as long as it existed. */
int32_t am2_dump_actions = 0;
static int32_t am2_parse_all = 0;
static int32_t am2_probe_noaction = 0;
static void ScriptParseAll(void);

static void ScriptReadEnv(void)
{
    static int32_t done;
    if (done)
        return;
    done = 1;
    am2_dump_actions = getenv("AM2_DUMP_ACTIONS") != 0;
    am2_parse_all = getenv("AM2_PARSE_ALL") != 0;
    am2_probe_noaction = getenv("AM2_PROBE_NOACTION") != 0;
}

/* ---------------------------------------------------------- actions ---- */

/* Advance one token. Zero, with the message, at the end of the script. */
static inline int32_t ActStep(AM2_ScriptCtx *ctx, int32_t *at)
{
    if (++(*at) < ctx->count)
        return 1;
    am2_log("Unexpected end of script.\n");
    return 0;
}

static inline void ActTypeErr(AM2_ScriptCtx *ctx, int32_t *at, int32_t kind)
{
    am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
            ctx->tokens[*at].line,
            ScriptTokenText(&ctx->tokens[*at], kScriptWord),
            kKindName(kind));
}

/* `createtrooper`, `createvehicle`: a location and an army, and the army's
 * own result decides the return. */
static inline int32_t ActPlaceAndArmy(AM2_ScriptCtx *ctx, int32_t *at,
                               AM2_ScriptAction *act)
{
    if (!ActStep(ctx, at))
        return 0;
    if (!ScriptLocation(ctx, at, act, 0))
        return 0;
    int32_t army = ScriptArmyColour(ctx, at);
    act->army = army;
    return army >= 0;
}

/* The weapon a `createtrooper` carries. -1 for one the keyword table has but
 * the trooper table rejects. */
static inline int32_t ActWeapon(int32_t id)
{
    switch (id) {
    case AM2_TOK_RIFLE:             return 1;
    case AM2_TOK_BAZOOKA:           return 4;
    case AM2_TOK_GRENADE:           return 2;
    case AM2_TOK_FLAMER:            return 3;
    case AM2_TOK_MORTAR:            return 5;
    case AM2_TOK_AUTORIFLE:         return 10;
    case AM2_TOK_SWEEPER:           return 20;
    case AM2_TOK_VULCAN:            return 29;
    case AM2_TOK_SNIPER:            return 30;
    default:  return -1;    /* flak, yours, explosive, detonator, lure,
                             * heavymg -- weapons the powerup table has and
                             * this one does not */
    }
}

static inline int32_t ActChassis(int32_t id)
{
    switch (id) {
    case AM2_TOK_TANK:              return 1;
    case AM2_TOK_JEEP:              return 0;
    case AM2_TOK_HALFTRACK:         return 2;
    case AM2_TOK_CONVOY:            return 3;
    case AM2_TOK_BOAT:              return 5;
    default: return -1;     /* `vehicle` itself is not a chassis */
    }
}

static inline int32_t ActPickup(int32_t id)
{
    switch (id) {
    case AM2_TOK_BAZOOKA:           return 4;
    case AM2_TOK_GRENADE:           return 2;
    case AM2_TOK_FLAMER:            return 3;
    case AM2_TOK_MORTAR:            return 5;
    case AM2_TOK_AUTORIFLE:         return 10;
    case AM2_TOK_FLAK:              return 28;
    case AM2_TOK_MINE:              return 11;
    case AM2_TOK_EXPLOSIVE:         return 12;
    case AM2_TOK_SWEEPER:           return 20;
    case AM2_TOK_LURE:              return 14;
    case AM2_TOK_VULCAN:            return 29;
    case AM2_TOK_HEAVYMG:           return 8;
    case AM2_TOK_SNIPER:            return 30;
    case AM2_TOK_VEHICLEAMMO:       return 32;
    case AM2_TOK_VEHICLEARMOR:      return 31;
    case AM2_TOK_VEHICLENITRO:      return 33;
    case AM2_TOK_CAMOUFLAGE:        return 34;
    case AM2_TOK_DISGUISEGREEN:     return 35;
    case AM2_TOK_DISGUISETAN:       return 36;
    case AM2_TOK_DISGUISEBLUE:      return 37;
    case AM2_TOK_DISGUISEGREY:      return 38;
    case AM2_TOK_MAGNIFYING:        return 39;
    case AM2_TOK_AEROSOL:           return 40;
    case AM2_TOK_M80:               return 42;
    case AM2_TOK_MEDPACK:           return 23;
    case AM2_TOK_MEDKIT:            return 22;
    case AM2_TOK_AIRSTRIKE:         return 24;
    case AM2_TOK_PARATROOPERS:      return 25;
    case AM2_TOK_RECON:             return 26;
    case AM2_TOK_WRENCH:            return 41;
    default:  return -1;    /* `detonator` is not a pickup */
    }
}

/* projectile, fire, crush, explosion -- shared by `damage` and
 * `setdamagepad`, and the codes are not in keyword order. */
static inline int32_t ActDamageKind(int32_t id)
{
    switch (id) {
    case AM2_TOK_PROJECTILE:        return 2;
    case AM2_TOK_FIRE:              return 1;
    case AM2_TOK_CRUSH:             return 4;
    case AM2_TOK_EXPLOSION:         return 3;
    default:  return -1;
    }
}

/* attack, defend, ignore, evade -- shared by `order ... inmode` and
 * `setaimode`, and the codes are neither sequential nor in keyword order. */
static inline int32_t ActAiMode(int32_t id)
{
    switch (id) {
    case AM2_TOK_ATTACK:            return 6;
    case AM2_TOK_DEFEND:            return 7;
    case AM2_TOK_IGNORE:            return 2;
    case AM2_TOK_EVADE:             return 5;
    default:  return -1;
    }
}

/* A variable name that must already be declared, for the several actions that
 * take one. The message differs per caller, so it comes in. */
static inline int32_t ActVarName(AM2_ScriptCtx *ctx, int32_t *at, AM2_ScriptAction *act,
                          const char *undeclared)
{
    int32_t idx = ScriptFindName((const char *)ctx->tokens[*at].value);
    if (idx < 0 || kScriptNames[idx].type != AM2_NAME_TYPE_INTEGER) {
        am2_log(undeclared, ctx->tokens[*at].line, ctx->tokens[*at].value);
        return 0;
    }
    act->xvar = idx;
    (*at)++;
    return 1;
}

/* 0x00440D70. One act, into a 0x48-byte record.
 *
 * 59 keywords behind a byte-index dispatch, four of which dispatch again.
 * docs/scriptactions.md lists the whole vocabulary; the field each writes is
 * the interesting part and it is not consistent -- +0x18 and +0x1C hold names
 * and armies, +0x28 a variable, +0x30 a string, +0x34/+0x38/+0x3C/+0x40/+0x44
 * whatever that action needs.
 *
 * Failures split three ways and it matters which: some log and return 0, some
 * return 0 in silence because the callee has already spoken, and several
 * return 1 having simply stopped early -- an optional argument that was not
 * there. */
static int32_t ScriptParseActionRecon(AM2_ScriptCtx *ctx, int32_t *at,
                                      AM2_ScriptAction *act)
{
    memset(act, 0, sizeof *act);
    act->uid = -2;

    if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
        return 0;
    int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
    if (id < 64 || id > 186)
        return 0;

    switch (id) {

    case AM2_TOK_RESTORECAMERAFOCUS:
        (*at)++;
        act->code = 0x1E;
        return 1;

    case AM2_TOK_SETCAMERAFOCUS:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x1D;
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case AM2_TOK_SUSPENDAI:
    case AM2_TOK_REVIVEAI:
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_SUSPENDAI) ? 0x1B : 0x1C;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING)
            return ScriptResolveName(ctx, at, &act->subject, 0) != 0;
        /* No name means everyone, which is the same built-in uid the
         * resolver's unreachable id-15 arm would have given. */
        act->subject = *(const int32_t *)((void *)(uintptr_t)(ADDR_SVAR_ID15));
        return 1;

    case AM2_TOK_SHOWMESSAGE:
    case AM2_TOK_SHOWBITMAP:
    case AM2_TOK_SHOWBITMAPNOPAUSE:
    case AM2_TOK_SHOWFAILURE:
    case AM2_TOK_SHOWPDA:
    case AM2_TOK_SETBRIEFING:
    case AM2_TOK_SETBRIEFVO:
    case AM2_TOK_SETSTRATMAP:       {
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        static const struct { int32_t id, code; } kShow[] = {
            { 64, 1 }, { 65, 2 }, { 66, 3 }, { 71, 4 }, { 72, 5 },
            { 151, 0x27 }, { 152, 0x28 }, { 153, 0x29 },
        };
        for (uint32_t i = 0; i < sizeof kShow / sizeof kShow[0]; i++)
            if (kShow[i].id == id)
                act->code = kShow[i].code;
        /* The record keeps the token's own string. Nothing copies it and
         * nothing frees it here. */
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;
    }

    case 67:                                    /* playsound */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        act->code = 6;
        act->text = (char *)ctx->tokens[*at].value;
        act->n0 = 0;
        act->n1 = 1;
        act->extra = 0;
        if (++(*at) >= ctx->count)
            return 1;
        ScriptLocation(ctx, at, act, 1);
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        /* No bounds check before this one, unlike the two above. */
        if (ctx->tokens[++(*at)].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->extra = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 68:                                    /* playitemsound */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        act->code = 7;
        act->text = (char *)ctx->tokens[*at].value;
        act->n0 = 0;
        act->n1 = 1;
        act->extra = 0;
        if (++(*at) >= ctx->count)
            return 1;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (ctx->tokens[++(*at)].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->extra = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 69:                                    /* playmusic */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 8;
        act->text = 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING)
            return 1;
        act->text = (char *)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 70:                                    /* tracevar */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 9;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        return ActVarName(ctx, at, act,
            "Line [%4d]:  expected variable name in TRACEVAR but variable "
            "%s not declared\n");
    case 78:                                    /* trigger */
        if (!ActStep(ctx, at))
            return 0;
        /* The uid lands at +0x00, over the -2 the record opens with, and the
         * zero at +0x04 -- the call pushes the record and then the record
         * plus four. */
        return ScriptObjectUid(ctx, at, &act->uid2, &act->uid) != 0;

    case 79:                                    /* triggerdelay */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_REFVAR) {   /* refvar */
            if (!ActStep(ctx, at))
                return 0;
            if (!ActVarName(ctx, at, act,
                    "Line [%4d]:  expected variable name but variable %s not "
                    "declared\n"))
                return 0;
        } else if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->delay = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (act->delay < 0) {
                am2_log("Line[%4d]:  Negative time set for a TRIGGERDELAY.\n",
                        ctx->tokens[*at].line);
                return 0;
            }
            (*at)++;
        } else {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        return ScriptObjectUid(ctx, at, &act->uid2, &act->uid) != 0;

    case 80:                                    /* createtrooper */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0D;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        act->text = (char *)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t w = ActWeapon((int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (w < 0) {
                am2_log("Line [%4d]:  Illegal weapon type '%s'\n",
                        ctx->tokens[*at].line,
                        ScriptTokenText(&ctx->tokens[*at], kScriptWord));
                return 0;
            }
            act->item = w;
        }
        return ActPlaceAndArmy(ctx, at, act);

    case 113:                                   /* createvehicle */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0F;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        act->text = (char *)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t c = ActChassis((int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (c < 0) {
                am2_log("Line [%4d]:  Illegal vehicle type '%s'\n",
                        ctx->tokens[*at].line,
                        ScriptTokenText(&ctx->tokens[*at], kScriptWord));
                return 0;
            }
            act->n0 = c;
        }
        return ActPlaceAndArmy(ctx, at, act);

    case 114:                                   /* createpowerup */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x10;
        /* The name is optional here, unlike the other two creates. */
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            act->text = (char *)ctx->tokens[*at].value;
            if (!ActStep(ctx, at))
                return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t p = ActPickup((int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (p < 0) {
                am2_log("Line [%4d]:  Illegal powerup type '%s'\n",
                        ctx->tokens[*at].line,
                        ScriptTokenText(&ctx->tokens[*at], kScriptWord));
                return 0;
            }
            act->item = p;
        }
        if (!ActStep(ctx, at))
            return 1;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 115:                                   /* createexplosion */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0E;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (act->n0 < 0x78 || act->n0 > 0x95) {
            am2_log("Line[%4d]:  Invalid explosion type.\n",
                    ctx->tokens[*at].line);
            act->n0 = 0x7F;
        }
        act->n1 = 0;
        act->army = 4;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count)
            return 1;
        act->army = ScriptArmyColour(ctx, at);
        if (act->army < 0)
            act->army = 4;
        return 1;

    case 116:                                   /* createroach */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x11;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        act->text = (char *)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        act->army = ScriptArmyColour(ctx, at);
        return 1;

    case 138:                                   /* setobjstate */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x25;
        /* The object at +0x1C and the state at +0x18 -- the other way round
         * from how the statement reads. */
        if (!ScriptResolveName(ctx, at, &act->target, 0))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        {
            int32_t st = ScriptFindName((const char *)ctx->tokens[*at].value);
            if (st < 0)
                st = AddNameTableName((const char *)ctx->tokens[*at].value,
                                      AM2_NAME_TYPE_REF, 0);
            act->subject = st;
        }
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 137:                                   /* setobjbmp    */
    case 124:                                   /* heal         */
    case 123:                                   /* setmaxhealth */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_SETOBJBMP) ? 0x24 : (id == AM2_TOK_HEAL) ? 0x0A : 0x0B;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 125:                                   /* damage */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0C;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        act->extra = 0;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
            return 1;
        {
            int32_t k = ActDamageKind(
                (int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (k < 0)
                return 1;
            act->extra = k;
        }
        (*at)++;
        return 1;

    case 143:                                   /* deploy */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x12;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count)
            return 1;
        /* Quiet, and the result is thrown away: `deploy <name>,` is legal and
         * the comma is not a location. */
        ScriptLocation(ctx, at, act, 1);
        return 1;

    case 144:                                   /* undeploy */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x13;
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case 145:                                   /* resurrect */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x14;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count)
            return 1;
        ScriptLocation(ctx, at, act, 1);      /* as `deploy` */
        return 1;

    case 146:                                   /* ally   */
    case 147:                                   /* unally */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_ALLY) ? 0x15 : 0x16;
        act->subject = ScriptArmyColour(ctx, at);
        if (act->subject < 0)
            return 0;
        act->target = ScriptArmyColour(ctx, at);
        return 1;

    case 148:                                   /* setforcecolor */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x17;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        act->target = ScriptArmyColour(ctx, at);
        return 1;

    case 77:                                    /* moveitem */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x1F;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        return ScriptLocation(ctx, at, act, 0) != 0;

    case 117:                                   /* setfacing    */
    case 118:                                   /* setgunfacing */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_SETFACING) ? 0x20 : 0x21;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }
        /* A facing may be `refvar <name>` instead of a literal. Anything
         * else is accepted and leaves the field at zero. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED ||
            (int32_t)(uintptr_t)ctx->tokens[*at].value != AM2_TOK_REFVAR)
            return 1;
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        return ActVarName(ctx, at, act,
            "Line [%4d]:  expected variable name but variable %s not "
            "declared\n");

    case 134:                                   /* setvar */
    case 135:                                   /* addvar */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_SETVAR) ? 0x23 : 0x22;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }
        if (id == AM2_TOK_ADDVAR && ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        return ActVarName(ctx, at, act,
            id == AM2_TOK_SETVAR
                ? "Line [%4d]:  expected variable name in SetVar but "
                  "variable %s not declared\n"
                : "Line [%4d]:  expected variable name in AddVar but "
                  "variable %s not declared\n");

    case 149:                                   /* activateregion   */
    case 150:                                   /* inactivateregion */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x26;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n0 = (id == AM2_TOK_ACTIVATEREGION);
        act->subject = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 119: {                                 /* order */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            if (!ScriptResolveName(ctx, at, &act->subject, 0))
                return 0;
            act->extra = 0;
        } else {
            if (!ScriptOrderTarget(ctx, at, &act->extra, &act->subject,
                                   &act->army))
                return 0;
        }
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        /* `order <who> follow <whom>` or `order <who> <somewhere>`. */
        int32_t verb = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (verb == AM2_TOK_FOLLOW) {                      /* follow */
            if (!ActStep(ctx, at))
                return 0;
            act->code = 0x19;
            if (!ScriptResolveName(ctx, at, &act->target, 0))
                return 0;
        } else if (verb == AM2_TOK_GOTO) {               /* moveto */
            if (!ActStep(ctx, at))
                return 0;
            act->code = 0x18;
            if (!ScriptLocation(ctx, at, act, 0))
                return 0;
        } else {
            return 0;
        }
        act->n0 = -1;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_RESERVED ||
            (int32_t)(uintptr_t)ctx->tokens[*at].value != AM2_TOK_INMODE)   /* inmode */
            return 1;
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t m = ActAiMode((int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (m < 0)
                return 0;
            act->n0 = m;
        }
        (*at)++;
        return 1;
    }

    case 154:                                   /* setaimode */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2A;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            if (!ScriptResolveName(ctx, at, &act->subject, 0))
                return 0;
            act->extra = 0;
        } else if (!ScriptOrderTarget(ctx, at, &act->extra, &act->subject,
                                      &act->army)) {
            return 0;
        }
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t m = ActAiMode((int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (m < 0) {
                am2_log("Line [%4d]:  Expected {attack|defend|ignore|evade} "
                        "in setaimode\n", ctx->tokens[*at].line);
                return 0;
            }
            act->n0 = m;
        }
        (*at)++;
        return 1;

    case 159:                                   /* setaipose */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2B;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t m = (int32_t)(uintptr_t)ctx->tokens[*at].value - 160;
            if (m < 0 || m > 3) {
                am2_log("Line [%4d]:  Expected {stand|kneel|prone|none} in "
                        "setaipose\n", ctx->tokens[*at].line);
                return 0;
            }
            /* stand 1, kneel 2, prone 3, none 0. */
            act->n0 = (m == 3) ? 0 : m + 1;
        }
        (*at)++;
        return 1;

    case 164:                                   /* setspeed */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2C;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        {
            int32_t v = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (v == AM2_TOK_SLOW)                       /* slow   */
                act->n0 = 1;
            else if (v == AM2_TOK_NORMAL)                  /* normal */
                act->n0 = 0;
            else {
                am2_log("Line [%4d]:  Expected {slow|normal} in setspeed\n",
                        ctx->tokens[*at].line);
                return 0;
            }
        }
        (*at)++;
        return 1;

    case 167:                                   /* setnpc    */
    case 176:                                   /* addexp    */
    case 185:                                   /* setuilock */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_SETNPC) ? 0x2D : (id == AM2_TOK_ADDEXP) ? 0x35 : 0x39;
        if (id != AM2_TOK_SETUILOCK && !ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 173:                                   /* setitemflag */
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        if ((int32_t)(uintptr_t)ctx->tokens[*at].value != AM2_TOK_STRATEGIC) {  /* strategic */
            am2_log("Line [%4d]:  Expected {strategic} in setitemflag\n",
                    ctx->tokens[*at].line);
            return 0;
        }
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x33;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 186:                                   /* setdamagepad */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x3A;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        {
            int32_t p = ScriptFindName((const char *)ctx->tokens[*at].value);
            if (p < 0) {
                am2_log("Line [%4d]:  Name required after SetDamagePad\n",
                        ctx->tokens[*at].line);
                return 0;
            }
            if (kScriptNames[p].type != AM2_NAME_TYPE_PAD) {
                am2_log("Line [%4d]:  SetDamagePad name was not a pad\n",
                        ctx->tokens[*at].line);
                return 0;
            }
            /* The pad it resolves to, not the name-table index. */
            act->subject = kScriptNames[p].value;
        }
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        act->n0 = 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
        }
        act->extra = 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
            return 1;
        {
            int32_t k = ActDamageKind(
                (int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (k < 0)
                return 1;
            act->extra = k;
        }
        (*at)++;
        return 1;

    case 175:                                   /* setitemowner */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x34;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = 4;
            return 1;
        }
        act->n0 = ScriptArmyColour(ctx, at);
        return act->n0 >= 0;

    case 168:                                   /* setm80 */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2E;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        act->army = ScriptArmyColour(ctx, at);
        if (act->army < 0)
            act->army = 4;
        return 1;

    case 180:                                   /* dropitem */
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x38;
        /* The item goes to +0x40 and whoever drops it to +0x18, which is the
         * other way round from every other two-name action. */
        if (!ScriptResolveName(ctx, at, &act->army, 0))
            return 0;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        return ScriptLocation(ctx, at, act, 0) != 0;

    case 169:                                   /* setzombie    */
    case 170:                                   /* setscientist */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_SETZOMBIE) ? 0x2F : 0x30;
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case 171:                                   /* makesmoke */
    case 172:                                   /* makeflame */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_MAKESMOKE) ? 0x31 : 0x32;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 177:                                   /* fireweapon */
    case 178:                                   /* unitfire   */
        if (!ActStep(ctx, at))
            return 0;
        act->code = (id == AM2_TOK_FIREWEAPON) ? 0x36 : 0x37;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (id == AM2_TOK_FIREWEAPON && !ScriptResolveName(ctx, at, &act->army, 0))
            return 0;
        if (*at < ctx->count &&
            ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (++(*at) >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
        } else {
            act->n0 = -1;
        }
        return ScriptLocation(ctx, at, act, 0) != 0;

    default:
        /* Every other reserved word in 64..186 -- the weapons, the pickups,
         * the AI modes and the poses. They are arguments, not actions. */
        return 0;
    }
}

/* AM2_PROBE_NOACTION runs the ORIGINAL action parser under our ReadScript and
 * our dump, which is how tests/actions-reference.txt is recorded.
 *
 * It takes two things at once and they cannot be separated. Not installing the
 * detour is not enough, because ScriptIf and ScriptObjFrame call this
 * reconstruction directly and would never reach 0x00440D70 at all. Calling
 * through the address is not enough either: patch_replace overwrites the
 * original's first five bytes with a jump and leaves no trampoline, so with
 * the detour in place that address is us and the call would come straight back
 * here. So the one flag both skips the patch in script_install and sends this
 * call through the address, and the flag is read before either is used. */
static int32_t ScriptParseAction(AM2_ScriptCtx *ctx, int32_t *at,
                                 AM2_ScriptAction *act)
{
    if (am2_probe_noaction)
        return orig_parse_action(ctx, at, act);
    return ScriptParseActionRecon(ctx, at, act);
}

/* Every action goes through here, so one place can dump what was parsed.
 *
 * AM2_DUMP_ACTIONS=1 prints the 0x48-byte record for each action the scripts
 * produce. Run the game once with the original parser and once with ours and
 * diff the two logs: that compares every action the shipped missions actually
 * contain, in the real process, with no emulator and no translation. The
 * offline harness cannot reach this code at all -- it opens files and calls
 * into the image -- and would be slower than the game if it could. */
int32_t ScriptAction(AM2_ScriptCtx *ctx, int32_t *at, AM2_ScriptAction *act)
{
    int32_t line = *at < ctx->count ? ctx->tokens[*at].line : -1;
    int32_t rc;

    ScriptReadEnv();
    rc = ScriptParseAction(ctx, at, act);

    if (am2_dump_actions) {
        /* One call, not eighteen: the game's logger writes a line per call
         * and drops a fragment that does not end in a newline. */
        char buf[0x48 / 4 * 9 + 40];
        const uint32_t *w = (const uint32_t *)act;
        int n = sprintf(buf, "ACT %4d %d", line, rc);
        for (int32_t i = 0; i < 0x48 / 4; i++)
            n += sprintf(buf + n, " %08X", w[i]);
        sprintf(buf + n, "\n");
        am2_log("%s", buf);
    }
    return rc;
}


/* ScriptPad. The pad being built is always kPads[kPadCount], re-read at every
 * use as the image does -- it is not counted until the very end, so every
 * write addresses the same record.
 *
 * Where on the map the pad number is, in sixteenths of a cell, is only
 * computed the first time a number is defined -- a second `pad` with the same
 * number joins the existing entry and leaves the centroid alone. The two
 * layers each have their own copy of the scan.
 *
 * The original divides by 0x00514DE0 to recover x, which is the map's HEIGHT
 * in tiles -- TileOfPoint, PointOfTile and ADDR_MAP_ROW_SHIFT all make
 * 0x00514DDC the width. Reproduced exactly: on a square map, and every
 * shipped one seen so far is, the two are the same number and the scan is
 * right by accident.
 *
 * The low-number test is faithfully `or`, which is what the image holds --
 * 0B C2 at 0x00444365. The mask is 1 << number and so never zero, which makes
 * the test always true: every cell on the map counts, and the centroid for a
 * low-numbered pad is the map's centre. `and` is plainly what was meant. Pad
 * numbers 5 and 6 do ship, so this is live rather than theoretical.
 *
 * Note what is NOT cleared: trigger, specific, compare, threshold and both
 * delays keep whatever the previous use of this slot left. The array is
 * static and the count resets between maps, so a pad can inherit a stale
 * trigger set. Reproduced, because the `or` below depends on it.
 *
 * Trigger words run until one that is not a trigger word. `army0`..`army3`
 * duplicate `green`/`tan`/`blue`/`grey`, which is reasonable; `halftrack`
 * shares 0x0200 with `grey`, which is not -- but nothing here can tell
 * whether that was intended, so it stands. */
#define PAD          (kPads[kPadCount])
#define PAD_LAYER    (*(const uint8_t **)((void *)(uintptr_t)(ADDR_MAP_PAD_LAYER)))
#define PADBIT_LAYER (*(const uint8_t **)((void *)(uintptr_t)(ADDR_MAP_PADBIT_LAYER)))
#define ZERO_POINT   (*(const int32_t *)((void *)(uintptr_t)(ADDR_ZERO_POINT)))
#define MAP_TILES_H  (*(const int32_t *)((void *)(uintptr_t)(ADDR_MAP_TILES_H)))
#define MAP_TILES_W  (*(const int32_t *)((void *)(uintptr_t)(ADDR_MAP_TILES_W)))

int32_t __cdecl ScriptPad(AM2_ScriptCtx *ctx, int32_t *at)
{
    int32_t name, number;
    int32_t i, sx, sy, n, total;

    if (!ScriptExpect(ctx, at, AM2_TOKEN_STRING))
        return 0;

    if (ScriptFindName((const char *)ctx->tokens[*at].value) >= 0) {
        am2_log("Line [%4d]:  Duplicate pad name.\n", ctx->tokens[*at].line);
        return 0;
    }

    /* Type 1, where `variable` uses 3. */
    name = AddNameTableName((const char *)ctx->tokens[*at].value,
                            AM2_NAME_TYPE_PAD, 0);
    am2_free(ctx->tokens[*at].value);
    ctx->tokens[*at].kind = AM2_TOKEN_NAMEREF;
    ctx->tokens[*at].value = (void *)(uintptr_t)name;

    if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
        return 0;

    number = (int32_t)(uintptr_t)ctx->tokens[*at].value;
    if (number < 0 || number >= 0x100) {
        am2_log("Line [%4d]:  Illegal Pad Number\n", ctx->tokens[*at].line);
        return 0;
    }

    if (kPadNumbers[number].count == 0) {
        if (number >= 8) {
            if (PAD_LAYER) {
                /* One dword store covering both words -- the default before
                 * the scan. */
                n = 0;
                *(int32_t *)&kPadNumbers[number].cx = ZERO_POINT;
                total = MAP_TILES_H * MAP_TILES_W;
                sx = 0;
                sy = 0;
                for (i = 0; i < total; i++) {
                    if ((int32_t)PAD_LAYER[i] == number) {
                        sx += i % MAP_TILES_H;
                        sy += i / MAP_TILES_H;
                        n++;
                    }
                }
                if (n > 0) {
                    /* Sixteenths, rounded. */
                    kPadNumbers[number].cx = (int16_t)((sx * 16 + 8) / n);
                    kPadNumbers[number].cy = (int16_t)((sy * 16 + 8) / n);
                }
            }
        } else if (PADBIT_LAYER) {
            n = 0;
            *(int32_t *)&kPadNumbers[number].cx = ZERO_POINT;
            total = MAP_TILES_H * MAP_TILES_W;
            sx = 0;
            sy = 0;
            for (i = 0; i < total; i++) {
                if ((PADBIT_LAYER[i] | kPadBit(number)) != 0) {
                    sx += i % MAP_TILES_H;
                    sy += i / MAP_TILES_H;
                    n++;
                }
            }
            if (n > 0) {
                kPadNumbers[number].cx = (int16_t)((sx * 16 + 8) / n);
                kPadNumbers[number].cy = (int16_t)((sy * 16 + 8) / n);
            }
        }
    }

    PAD.id = kPadCount;
    PAD.name = name;
    PAD.compared = 0;
    PAD.number = number;

    kPadNumbers[number].pads[kPadNumbers[number].count] = (int16_t)kPadCount;
    kPadNumbers[number].count++;

    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    /* Trigger words, until one that is not a trigger word. */
    while (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_EVERYTHING:    PAD.trigger |= (int32_t)AM2_PADTRIG_EVERYTHING; break;
        case AM2_TOK_SARGE:         PAD.trigger |= (int32_t)AM2_PADTRIG_SARGE;      break;
        case AM2_TOK_UNIT:          PAD.trigger |= (int32_t)AM2_PADTRIG_UNIT;       break;
        case AM2_TOK_TROOPER:       PAD.trigger |= (int32_t)AM2_PADTRIG_TROOPER;    break;
        case AM2_TOK_TANK:          PAD.trigger |= (int32_t)AM2_PADTRIG_TANK;       break;
        case AM2_TOK_VEHICLE:       PAD.trigger |= (int32_t)AM2_PADTRIG_VEHICLE;    break;
        case AM2_TOK_CONVOY:        PAD.trigger |= (int32_t)AM2_PADTRIG_CONVOY;     break;
        case AM2_TOK_BOAT:          PAD.trigger |= (int32_t)AM2_PADTRIG_BOAT;       break;
        case AM2_TOK_GROUNDVEHICLE: PAD.trigger |= (int32_t)AM2_PADTRIG_GROUNDVEH;  break;
        case AM2_TOK_NPC:           PAD.trigger |= (int32_t)AM2_PADTRIG_NPC;        break;
        case AM2_TOK_GREEN:
        case AM2_TOK_ARMY0:         PAD.trigger |= (int32_t)AM2_PADTRIG_GREEN;      break;
        case AM2_TOK_TAN:
        case AM2_TOK_ARMY1:         PAD.trigger |= (int32_t)AM2_PADTRIG_TAN;        break;
        case AM2_TOK_BLUE:
        case AM2_TOK_ARMY2:         PAD.trigger |= (int32_t)AM2_PADTRIG_BLUE;       break;
        case AM2_TOK_GREY:
        case AM2_TOK_ARMY3:
        case AM2_TOK_HALFTRACK:     PAD.trigger |= (int32_t)AM2_PADTRIG_GREY;       break;
        default:
            goto triggers_done;
        }
        if (++(*at) >= ctx->count)
            goto triggers_done;
    }

    /* A String here names one specific item instead, which the flags
     * exclude. */
    if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
        if (PAD.trigger != 0) {
            am2_log("Line [%4d]:  Pad can't have both specific item "
                    "and generic triggers\n", ctx->tokens[*at].line);
            return 0;
        }
        int32_t item;
        if (!ScriptResolveName(ctx, at, &item, 0))
            return 0;
        PAD.specific = 1;
        PAD.trigger = item;
    }
triggers_done:

    /* An optional comparison, then the count it compares against. A pad
     * that ends after its trigger words -- or whose next token is not a
     * control character -- goes straight to binding the name and is never
     * finalised: both of those paths land on 0x00444842, past the call. */
    if (*at != ctx->count &&
        ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR) {
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_GT: PAD.compare = AM2_PADCMP_GT; break;       /* '>' */
        case AM2_TOK_EQ: PAD.compare = AM2_PADCMP_EQ; break;       /* '=' */
        case AM2_TOK_LT: PAD.compare = AM2_PADCMP_LT; break;       /* '<' */
        default:
            am2_log("Line [%4d]:  Unexpected symbol in pad definition "
                    "should be '<=>'\n", ctx->tokens[*at].line);
            return 0;
        }

        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        PAD.compared = 1;

        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(&ctx->tokens[*at], kScriptWord),
                    kKindName(AM2_TOKEN_INTEGER));
            return 0;
        }
        PAD.threshold = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;

        /* `delay <int> <int>`, optional, and only after a comparison. */
        if (*at != ctx->count &&
            ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_DELAY) {
            if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
                return 0;
            PAD.delay0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
                return 0;
            PAD.delay1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
        }

        /* The second argument is an OBJECT and the original passes a literal
         * zero here, which the callee reads as "no object" -- so the notify
         * this pad eventually sends carries a zero uid. */
        PadFinalise(&PAD, (void *)0);
    }

    /* The name resolves to the pad, and only now does the pad count move. */
    kScriptNames[PAD.name].value = kPadCount;
    kPadCount++;
    return 1;
}
#undef PAD
#undef PAD_LAYER
#undef PADBIT_LAYER
#undef ZERO_POINT
#undef MAP_TILES_H
#undef MAP_TILES_W

/* ---------------------------------------------------------------- if ---- */

#define kScriptConds (*(AM2_ScriptCond **)((void *)(uintptr_t)(ADDR_SCRIPT_CONDITIONS)))

/* 0x00442F10. Does `want` appear before `stop`, scanning from `from`?
 *
 * The two are not tested symmetrically, because the step sits between them:
 * `want` is tested on token i at iteration i,
 * while `stop` is tested on token i+1 at iteration i -- so for every token but
 * the first, `stop` is examined first. And the `stop` read happens before the
 * bounds check, so on the last iteration it reads one token past the end.
 * Both are reproduced; neither is safe to tidy without changing which form an
 * ambiguous statement parses as. */
int32_t __cdecl ScriptScanFor(const AM2_ScriptCtx *ctx, int32_t from,
                              int32_t want, int32_t stop)
{
    while (from < ctx->count) {
        if (ctx->tokens[from].kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)ctx->tokens[from].value == want)
            return 1;
        from++;
        if (ctx->tokens[from].kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)ctx->tokens[from].value == stop)
            return 0;
    }

    am2_log("Unexpected end of script.\n");
    return 0;
}


int32_t __cdecl ScriptIf(AM2_ScriptCtx *ctx, int32_t *at)
{
    int32_t objname = 0;
    int32_t a, b, c3;
    int32_t na, nb, nc;     /* the butnot event's own */
    AM2_ScriptAction act;
    AM2_ScriptCond *cond;

    /* The token is addressed afresh at every use, and a keyword test reads
     * the kind again each time. Three shapes of step, each where the image
     * has it: to the shared end-of-script block, to the second one inside
     * the action list, or with its own message (ScriptStep). */
#define TOK         (&ctx->tokens[*at])
#define IS_WORD(w)  (TOK->kind == AM2_TOKEN_RESERVED \
                     && (int32_t)(uintptr_t)TOK->value == (w))
#define STEP_EOS()  if (++(*at) >= ctx->count) goto end_of_script
#define STEP_LATE() if (++(*at) >= ctx->count) goto end_of_script_late
#define STEP_OWN()  if (!ScriptStep(ctx, at)) return 0
/* A wrong token kind logs and returns WITHOUT freeing the record -- another
 * of the original's leaks. */
#define WANT_KIND(K)                                                        \
    if (TOK->kind != (K)) {                                                 \
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n", \
                ctx->tokens[*at].line, ScriptTokenText(TOK, kScriptWord),   \
                kKindName(K));                                              \
        return 0;                                                           \
    }

    STEP_EOS();

    cond = (AM2_ScriptCond *)am2_malloc(sizeof *cond);
    memset(cond, 0, sizeof *cond);

    if (IS_WORD(AM2_TOK_TIMEABSOLUTE)) {
        cond->kind = AM2_IF_TIMEABSOLUTE;
        STEP_EOS();
        if (TOK->kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  Exptected number after TIMEABSOLUTE\n",
                    TOK->line);
            goto fail;
        }
        cond->number = (int32_t)(uintptr_t)TOK->value;
        if (cond->number < 1) {
            am2_log("Line [%4d]:  TIMEABSOLUTE time must be positive\n",
                    ctx->tokens[*at].line);
            goto fail;
        }
        STEP_OWN();

    } else if (ScriptScanFor(ctx, *at, AM2_TOK_AFTER, AM2_TOK_THEN)) {
        /* An `<event> after <event>` chain. */
        cond->kind = AM2_IF_AFTER;
        while (TOK->kind == AM2_TOKEN_STRING || TOK->kind == AM2_TOKEN_RESERVED) {
            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            SCRIPT_ADD_EVENT(cond, a, b, c3);

            if (!IS_WORD(AM2_TOK_AFTER)) {
                am2_log("Line [%4d]:  Missing 'after' in if-statement.\n",
                        ctx->tokens[*at].line);
                goto fail;
            }
            STEP_LATE();

            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            SCRIPT_ADD_EVENT(cond, a, b, c3);

            if (IS_WORD(AM2_TOK_THEN) || IS_WORD(AM2_TOK_TESTVAR))
                break;
            if (IS_WORD(AM2_TOK_AND))
                STEP_LATE();
        }

    } else if (TOK->kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)TOK->value) {
        case AM2_TOK_ALLOF:
            cond->kind = AM2_IF_ALLOF;
            STEP_OWN();
            break;
        case AM2_TOK_INORDER:
            cond->kind = AM2_IF_INORDER;
            STEP_OWN();
            break;
        case AM2_TOK_REPEAT:
            cond->kind = AM2_IF_REPEAT;
            STEP_EOS();
            WANT_KIND(AM2_TOKEN_INTEGER)
            cond->number = (int32_t)(uintptr_t)TOK->value;
            STEP_OWN();
            if (!IS_WORD(AM2_TOK_OF)) {
                am2_log("Line [%4d]:  Missing 'of' in if-repeat statement.\n",
                        TOK->line);
                goto fail;
            }
            STEP_OWN();
            break;
        case AM2_TOK_COUNT:
            cond->kind = AM2_IF_COUNT;
            STEP_EOS();
            WANT_KIND(AM2_TOKEN_INTEGER)
            cond->number = (int32_t)(uintptr_t)TOK->value;
            STEP_EOS();
            if (!IS_WORD(AM2_TOK_OF)) {
                am2_log("Line [%4d]:  Missing 'of' in if-repeat statement.\n",
                        TOK->line);
                goto fail;
            }
            STEP_OWN();
            break;
        default:                        /* butnot, and anything else */
            cond->kind = AM2_IF_PLAIN;
            break;
        }

        if (!ScriptParseEvents(ctx, at, cond))
            goto fail;

        if (IS_WORD(AM2_TOK_BUTNOT)) {
            cond->kind = AM2_IF_BUTNOT_KEYWORD;
            STEP_EOS();
            if (!ScriptParseEvent(ctx, at, &na, &nb, &nc))
                goto fail_nofree;
            SCRIPT_ADD_EVENT(cond, na, nb, nc);
            if (*at >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
        }

    } else if (TOK->kind == AM2_TOKEN_STRING) {
        cond->kind = AM2_IF_PLAIN;
        if (!ScriptParseEvents(ctx, at, cond))
            goto fail;

        if (IS_WORD(AM2_TOK_BUTNOT)) {
            cond->kind = AM2_IF_BUTNOT_STRING;
            STEP_EOS();
            if (!ScriptParseEvent(ctx, at, &na, &nb, &nc))
                goto fail_nofree;
            SCRIPT_ADD_EVENT(cond, na, nb, nc);
            if (*at >= ctx->count)
                goto end_of_script;
        }
    }

    /* ---- testvar ---- */
    if (IS_WORD(AM2_TOK_TESTVAR)) {
        STEP_EOS();

        while (!IS_WORD(AM2_TOK_THEN)) {        /* until `then` */
            AM2_ScriptTest test;
            memset(&test, 0, sizeof test);

            if (!ScriptParseValue(ctx, at, &test.left[0], &test.left[1],
                                  &test.left[2]))
                goto fail;
            if (*at >= ctx->count)
                goto end_of_script_late;

            /* One test per operator, the kind read again with each. */
#define IS_CTRL(c) (TOK->kind == AM2_TOKEN_CONTROL_CHAR \
                    && (int32_t)(uintptr_t)TOK->value == (c))
            if (IS_CTRL(AM2_TOK_LT))
                test.op = AM2_CMP_LT;
            else if (IS_CTRL(AM2_TOK_LE))
                test.op = AM2_CMP_LE;
            else if (IS_CTRL(AM2_TOK_EQ))
                test.op = AM2_CMP_EQ;
            else if (IS_CTRL(AM2_TOK_GT))
                test.op = AM2_CMP_GT;
            else if (IS_CTRL(AM2_TOK_GE))
                test.op = AM2_CMP_GE;
            else if (IS_CTRL(AM2_TOK_NE))
                test.op = AM2_CMP_NE;
            else {
                am2_log("Line [%4d]:  Unrecognized operator in testvar "
                        "clause.\n", ctx->tokens[*at].line);
                goto fail;
            }
#undef IS_CTRL

            STEP_LATE();
            if (!ScriptParseValue(ctx, at, &test.right[0], &test.right[1],
                                  &test.right[2]))
                goto fail;
            if (*at >= ctx->count)
                goto end_of_script_late;

            if (!IS_WORD(AM2_TOK_THEN) && !IS_WORD(AM2_TOK_AND)) {
                am2_log("Line [%4d]:  Incomplete testvar clause.\n",
                        ctx->tokens[*at].line);
                goto fail;
            }
            if (IS_WORD(AM2_TOK_AND))
                STEP_LATE();

            int32_t n = cond->ntests++;
            cond->tests = (AM2_ScriptTest *)am2_realloc(
                cond->tests, (size_t)cond->ntests * sizeof(AM2_ScriptTest));
            cond->tests[n] = test;
        }
    }

    /* ---- then ---- */
    if (!IS_WORD(AM2_TOK_THEN)) {
        am2_log("Line [%4d]:  Missing 'then' in if-statement.\n", TOK->line);
        goto fail;
    }
    STEP_EOS();

    if (IS_WORD(AM2_TOK_RANDOM)) {
        cond->mode = AM2_THEN_RANDOM;
        goto then_step;
    } else if (IS_WORD(AM2_TOK_SEQUENTIAL)) {
        cond->mode = AM2_THEN_SEQUENTIAL;
then_step:
        if (++(*at) >= ctx->count) {
end_of_script:
            /* The original leaks the record on every "Unexpected end of
             * script" path -- only the messages that route through
             * 0x00443E03 reach the free. Kept, because a script that takes
             * one of these is already being rejected and the process is
             * about to log and carry on. */
            am2_log("Unexpected end of script.\n");
            return 0;
        }
    } else if (IS_WORD(AM2_TOK_ONOBJSTATE)) {
        cond->mode = AM2_THEN_ONOBJSTATE;
        STEP_OWN();
        if (!ScriptResolveName(ctx, at, &cond->objstate, 0))
            goto fail;
    } else {
        cond->mode = AM2_THEN_NONE;
    }

    /* ---- the action list ---- */
    /* The first action is written out apart from the rest: its array is
     * malloc'd (0x00443C89), where the later ones grow it by realloc. */
    if (cond->mode == AM2_THEN_ONOBJSTATE) {
        WANT_KIND(AM2_TOKEN_STRING)
        objname = ScriptFindName((const char *)TOK->value);
        if (objname < 0)
            objname = AddNameTableName((const char *)ctx->tokens[*at].value,
                                       AM2_NAME_TYPE_REF, 0);
        if (++(*at) >= ctx->count) {
end_of_script_late:
            am2_log("Unexpected end of script.\n");
            return 0;
        }
    }
    if (!ScriptAction(ctx, at, &act))
        goto fail;
    if (cond->mode == AM2_THEN_ONOBJSTATE)
        act.extra = objname;
    cond->nactions = 1;
    cond->actions = (AM2_ScriptAction *)am2_malloc(sizeof act);
    cond->actions[0] = act;

    while (*at < ctx->count && TOK->kind == AM2_TOKEN_CONTROL_CHAR
           && (int32_t)(uintptr_t)TOK->value == AM2_TOK_COMMA) {    /* ',' */
        STEP_LATE();
        if (cond->mode == AM2_THEN_ONOBJSTATE) {
            WANT_KIND(AM2_TOKEN_STRING)
            objname = ScriptFindName((const char *)TOK->value);
            if (objname < 0)
                objname = AddNameTableName(
                    (const char *)ctx->tokens[*at].value, AM2_NAME_TYPE_REF, 0);
            STEP_LATE();
        }
        if (!ScriptAction(ctx, at, &act))
            goto fail;
        if (cond->mode == AM2_THEN_ONOBJSTATE)
            act.extra = objname;
        cond->nactions++;
        cond->actions = (AM2_ScriptAction *)am2_realloc(
            cond->actions, (size_t)cond->nactions * sizeof act);
        cond->actions[cond->nactions - 1] = act;
    }

    /* Newest first. */
    cond->next = kScriptConds;
    kScriptConds = cond;
    return 1;

fail:
    am2_free(cond);
    return 0;

fail_nofree:
    /* A failing event parse returns without freeing -- 0x00443E14 skips the
     * free that 0x00443E0B does. Another original leak, reproduced. */
    return 0;
#undef TOK
#undef IS_WORD
#undef STEP_EOS
#undef STEP_LATE
#undef STEP_OWN
#undef WANT_KIND
}

/* No statement handler is left original. Each takes the context and a pointer
 * to the walk index, which it advances past its own statement, so the loop
 * below makes no assumption about statement length -- which is what let them
 * be reconstructed one at a time while the rest were still reached by
 * address. */
int32_t __cdecl ReadScript(const char *path, AM2_ScriptCtx *ctx)
{
    char line[0x100];
    int32_t ok = 1;

#if !defined(AM2_RELINK)   /* harness trace, not the original's */
    if (am2_dump_actions)
        am2_log("READSCRIPT %s\n", path);
#endif

    /* The GAME's stdio, not the host's. The original opens and reads the
     * script through its own CRT (0x00444CEF fopen, 0x00444D40 fgets,
     * 0x00444D6B fclose), whose first fgets allocates the stream's
     * 4,096-byte buffer on the CRT heap; the host's fopen allocated
     * nothing there, and every heap address the mission built after its
     * script was one block off the original's -- which the depth
     * comparator's pointer tie-break turned into a differing pixel. The
     * feof is the original's inlined test of the FILE's flag word. */
    am2_FILE *fh = orig_fopen(path, "rt");
    if (!fh) {
        am2_log("ReadScript: Could not open %s for reading.\n", path);
        ok = 0;
    }

    int32_t reported = -1;      /* the last line an error was reported for */
    int32_t lines = 0;
    int32_t at = ctx->count;    /* resume past whatever is already parsed */

    if (ok) {
        while (!orig_feof(fh) && orig_fgets(line, (int32_t)sizeof line, fh)) {
            ScriptNextToken(line, ctx, lines);
            lines++;
        }
        orig_fclose(fh);
    }

    /* Second pass: walk the tokens and dispatch each statement. The kind is
     * tested again with every keyword, as the image does. */
    int32_t compounds = 0;

#define IS_WORD(w) (tok->kind == AM2_TOKEN_RESERVED \
                    && (int32_t)(uintptr_t)tok->value == (w))
    while (at < ctx->count) {
        AM2_ScriptTok *tok = &ctx->tokens[at];

        if (IS_WORD(AM2_TOK_PRELOADSPRITE)) {
            ScriptPreloadSprite(ctx, &at);
        } else if (IS_WORD(AM2_TOK_PAD)) {
            ScriptPad(ctx, &at);
        } else if (IS_WORD(AM2_TOK_VARIABLE)) {
            ScriptVariable(ctx, &at);
        } else if (IS_WORD(AM2_TOK_IF)) {
            if (ScriptIf(ctx, &at) == 1)
                compounds++;
        /* `object` and `objclass` share a handler, which reads the keyword
         * itself rather than being told which one it is. */
        } else if (IS_WORD(AM2_TOK_OBJECT)) {
            GenerateObjScriptFromTokens(ctx, &at);
        } else if (IS_WORD(AM2_TOK_OBJCLASS)) {
            GenerateObjScriptFromTokens(ctx, &at);
        } else {
            /* Not a statement. Report once per line, not once per token -- a
             * line of six unknown words would otherwise give six identical
             * messages. */
            if (tok->line > reported) {
                am2_log("Line [%4d]: Unknown Initial Word: %s\n",
                        ctx->tokens[at].line, ScriptTokenText(tok, kScriptWord));
                reported = ctx->tokens[at].line;
            }
            at++;
        }
    }
#undef IS_WORD

#if !defined(AM2_RELINK)   /* the AM2_PARSE_ALL oracle, not the original's */
    if (am2_parse_all) {
        /* Once, and not from inside itself. */
        am2_parse_all = 0;
        ScriptParseAll();
    }
#endif

    /* Single player only: a multiplayer load goes on to read the rules and
     * the per-army AI scripts, and the summary would be one of four. */
    if (*(const int32_t *)((void *)(uintptr_t)(ADDR_MP_SESSION)) == 0) {
        /* The token count comes from the GLOBAL context, not the one passed
         * in. They are the same object for every caller in the image, but the
         * original reads the global and so does this. */
        const AM2_ScriptCtx *g =
            (const AM2_ScriptCtx *)((void *)(uintptr_t)(ADDR_SCRIPT_CONTEXT));
        am2_log("lines: %d  tokens: %d  names: %d  compounds: %d\n",
                lines, g->count, kScriptNameCount, compounds);
        am2_log("Finished Processing Script... "
                "Press SPACE to continue.\n");
    }

    return ok;
}

void am2_script_reset_names(void)
{
    for (int32_t i = 0; i < kScriptNameCount; i++)
        am2_free(kScriptNames[i].name);
    am2_free(kScriptNames);
    kScriptNames = 0;
    kScriptNameCount = 0;
    kScriptNameCap = 0;
}

int32_t am2_script_name_count(void)
{
    return kScriptNameCount;
}

const AM2_ScriptName *am2_script_name(int32_t i)
{
    return &kScriptNames[i];
}


#define kMapName      ((const char *)((void *)(uintptr_t)(ADDR_MAP_NAME)))
#define kMapFolder    ((const char *)((void *)(uintptr_t)(ADDR_MAP_FOLDER)))
#define kMpScriptName ((const char *)((void *)(uintptr_t)(ADDR_MP_SCRIPT_NAME)))
#define kLevelIndex   (*(const int32_t *)((void *)(uintptr_t)(ADDR_LEVEL_INDEX)))
#define kMpSession    (*(const int32_t *)((void *)(uintptr_t)(ADDR_MP_SESSION)))

/* Change into one of the game's data directories. Everything the script loader
 * opens is relative to wherever this last left us. */
static inline void ScriptSetDataDir(const char *dir)
{
    SetGameDir(dir);
}

/* Read one script, with the progress line the comm object's verbose flag asks
 * for. The three sites in LoadLevelScript are identical but for the path. */
static inline void ScriptReadWithLog(const char *path)
{
    int32_t ok;

    if (*(const int32_t *)(kCommObject + COMM_OFF_VERBOSE))
        am2_log("reading script %s: ", path);

    ok = ReadScript(path, (AM2_ScriptCtx *)((void *)(uintptr_t)(ADDR_SCRIPT_CONTEXT)));

    /* Re-read the flag: ReadScript can have changed it. */
    if (*(const int32_t *)(kCommObject + COMM_OFF_VERBOSE)) {
        if (ok)
            am2_log("worked!\n");
        else
            am2_log("FAILED!\n");
    }
}

void __cdecl LoadLevelScript(void)
{
    char path[0x40];

    ScriptSetDataDir(kMapFolder);

    /* Level 0 is the map's only script and drops the number. */
    if (kLevelIndex > 0)
        sprintf(path, "%s%d.txt", kMapName, kLevelIndex);
    else
        sprintf(path, "%s.txt", kMapName);

    ScriptResetTokens((AM2_ScriptCtx *)((void *)(uintptr_t)(ADDR_SCRIPT_CONTEXT)));

    /* The five score variables, declared before anything can reference them.
     * `gamescorelimit` is seeded with the configured limit and its index is
     * thrown away -- scripts reach it by name and nothing writes it back --
     * while the four army scores keep theirs, because the runtime updates
     * those by index. */
    AddNameTableName(kImageStr(ADDR_NAME_SCORE_LIMIT), AM2_NAME_TYPE_INTEGER,
                     *(const int32_t *)((void *)(uintptr_t)(ADDR_SCORE_LIMIT)));
    *(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_GREENSCORE)) =
        AddNameTableName(kImageStr(ADDR_NAME_GREENSCORE),
                         AM2_NAME_TYPE_INTEGER, 0);
    *(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_TANSCORE)) =
        AddNameTableName(kImageStr(ADDR_NAME_TANSCORE),
                         AM2_NAME_TYPE_INTEGER, 0);
    *(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_BLUESCORE)) =
        AddNameTableName(kImageStr(ADDR_NAME_BLUESCORE),
                         AM2_NAME_TYPE_INTEGER, 0);
    *(int32_t *)((void *)(uintptr_t)(ADDR_SVAR_GREYSCORE)) =
        AddNameTableName(kImageStr(ADDR_NAME_GREYSCORE),
                         AM2_NAME_TYPE_INTEGER, 0);

    ScriptReadWithLog(path);

    if (kMpSession) {
    /* MP team-colour names, folded to literals in the standalone build. */
#ifndef AM2_STR_COLOUR_GREEN
#define AM2_STR_COLOUR_GREEN   0x00476A68u  /* green */
#endif
#ifndef AM2_STR_COLOUR_TAN
#define AM2_STR_COLOUR_TAN     0x00485148u  /* tan */
#endif
#ifndef AM2_STR_COLOUR_BLUE
#define AM2_STR_COLOUR_BLUE    0x00485140u  /* blue */
#endif
#ifndef AM2_STR_COLOUR_GREY
#define AM2_STR_COLOUR_GREY    0x00485138u  /* grey */
#endif
        const char *colour;
        int32_t     i;

        ScriptSetDataDir(kImageStr(ADDR_RULES_DIR_STR));
        sprintf(path, "%s.txt", kMpScriptName);
        ScriptReadWithLog(path);

        for (i = 0; i < AM2_PLAYERS_MAX; i++) {
#define SLOT (kCommObject + (size_t)i * AM2_PLAYER_STRIDE)
            if (!*(const int32_t *)(SLOT + AM2_PLAYER_ACTIVE))
                continue;
            /* A slot a real player still holds needs no AI script. The test
             * is the DirectPlay id, so this reads the opposite way round from
             * the name it first went in under -- see ADDR_COMM_SLOT_HAS_PLAYER.
             * Its answer, zero on the way through, is what `colour` holds for
             * an army above 3: the switch has no default, and the format then
             * prints from NULL. No slot carries one, and this is the
             * original's own omission. */
            if ((colour = (const char *)(uintptr_t)
                     ((AM2_Comm *)kCommObject)->CommSlotHasPlayer(i)) != 0)
                continue;

            switch (*(const int32_t *)(SLOT + AM2_PLAYER_ARMY)) {
            case 0: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_GREEN); break;
            case 1: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_TAN);   break;
            case 2: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_BLUE);  break;
            case 3: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_GREY);  break;
            }
#undef SLOT

            sprintf(path, "%s_ai_%s.txt", kMpScriptName, colour);
            ScriptReadWithLog(path);
        }
    }

    DeclareRuleVars();
}

/* AM2_PARSE_ALL=1: after the game's own first script load, parse every other
 * script it ships and dump what each action came out as.
 *
 * The point is coverage. bootcamp and the campaign between them reach 24 of
 * the 59 action keywords; 48 appear in at least one shipped file, and the only
 * way to see all 48 in one run is to feed the parser every file. Doing that
 * inside the game costs one run and needs no emulator -- the parser, the
 * allocator, the name table and the pad tables are all the real ones.
 *
 * It wrecks the game's own state, so this is a probe configuration and nothing
 * else. The file list comes from tools/scriptlist.py because enumerating a
 * directory is Win32 and this file does not name a Win32 type. */
/* Count, capacity AND the array together. Zeroing only the count leaves
 * NewObjScript with a capacity it believes, so it hands back entry 0 without
 * clearing it -- stale state list, stale counts -- and the next `state` writes
 * past an array that is not there any more. That took the sweep down after two
 * files. */
static void ScriptResetObjScripts(void)
{
    *(int32_t *)((void *)(uintptr_t)(ADDR_CURRENT_OBJ_SCRIPT)) = 0;
    *(int32_t *)((void *)(uintptr_t)(ADDR_OBJ_SCRIPT_CAP)) = 0;
    *(AM2_ObjScript **)((void *)(uintptr_t)(ADDR_OBJ_SCRIPTS)) = 0;
}

static void ScriptParseAll(void)
{
    /* An absolute path from the environment: the game chdirs into the map
     * directory before loading, so ReadScript sees a bare filename and
     * nothing relative to the game root can be opened from here. */
    const char *list = getenv("AM2_SCRIPTS");
    FILE *fh = list ? fopen(list, "rt") : 0;
    if (!fh) {
        am2_log("ScriptParseAll: cannot open %s\n", list ? list : "(unset)");
        return;
    }

    /* State accumulates across the sweep and the fixed tables eventually
     * overflow -- it gets through about seventy files before the process goes
     * down. Clearing between files is worse, not better: it took the sweep
     * down after two, because the arrays and their capacities have to be
     * cleared together and the entries the game itself is holding must not be
     * freed. Two runs with the list in opposite orders cover everything, which
     * is cheaper than getting a reset exactly right for a probe.
     *
     * The sweep still gets tables of its own. Reusing the game's and clearing
     * between files frees names the loaded mission is still holding, which
     * takes the process down on the first file -- measured. Saving the
     * pointers and starting from empty leaves the live state untouched. */
    AM2_ScriptName *save_names = kScriptNames;
    int32_t save_ncount = kScriptNameCount, save_ncap = kScriptNameCap;
    int32_t save_pads = kPadCount;
    int32_t save_obj = *(int32_t *)((void *)(uintptr_t)(ADDR_CURRENT_OBJ_SCRIPT));
    AM2_ObjScript *save_objarr = *(AM2_ObjScript **)((void *)(uintptr_t)(ADDR_OBJ_SCRIPTS));
    int32_t save_objcap = *(int32_t *)((void *)(uintptr_t)(ADDR_OBJ_SCRIPT_CAP));
    static int16_t save_padnum[256];
    for (int32_t i = 0; i < 256; i++) {
        save_padnum[i] = kPadNumbers[i].count;
        kPadNumbers[i].count = 0;
    }
    kScriptNames = 0;
    kScriptNameCount = 0;
    kScriptNameCap = 0;
    kPadCount = 0;
    ScriptResetObjScripts();

    char line[0x100];
    int32_t files = 0;
    while (fgets(line, sizeof line, fh)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (!n)
            continue;

        AM2_ScriptCtx ctx = { 0, 0, 0 };
        am2_log("PARSEALL %s\n", line);
        ReadScript(line, &ctx);
        ScriptResetTokens(&ctx);
        files++;
    }
    fclose(fh);

    kScriptNames = save_names;
    kScriptNameCount = save_ncount;
    kScriptNameCap = save_ncap;
    kPadCount = save_pads;
    *(int32_t *)((void *)(uintptr_t)(ADDR_CURRENT_OBJ_SCRIPT)) = save_obj;
    *(AM2_ObjScript **)((void *)(uintptr_t)(ADDR_OBJ_SCRIPTS)) = save_objarr;
    *(int32_t *)((void *)(uintptr_t)(ADDR_OBJ_SCRIPT_CAP)) = save_objcap;
    for (int32_t i = 0; i < 256; i++)
        kPadNumbers[i].count = save_padnum[i];

    am2_log("PARSEALL done: %d files\n", files);
}

/* Still original: the action runtime. ScriptRunLine hands it a NULL owner,
 * where a mission's own actions carry the object they belong to. */
typedef void (__cdecl *AM2_RunActionFn)(AM2_ScriptAction *act, void *owner);

/* 0x00444C40, three callers -- the cheat table's fallback. Tokenise one typed
 * line, parse it as an ACTION, and run it.
 *
 * It is the reason ADDR_CHEAT_ENTRY has an unrecognised-word path at all: a
 * cheat the table does not know is handed here and treated as a script action,
 * so anything a mission script can do can be typed. That is the whole content
 * of the function and it is worth stating, because the name -- which is ours,
 * the function carries no string -- makes it sound like part of ReadScript.
 * It is not; ReadScript tokenises with ScriptNextToken directly.
 *
 * ONE TOKEN, NOT A LINE. ScriptNextToken is called ONCE, so what is parsed is
 * whatever the first call leaves in the context -- and ScriptParseAction then
 * pulls more tokens itself as it needs them. A reconstruction that looped the
 * tokeniser to exhaustion first would agree on everything a cheat can express
 * and diverge on the first line that ends mid-action.
 *
 * BOTH EXITS RESET THE CONTEXT and neither frees anything else, so the token
 * list belongs to the reset. The failure path resets and answers 0 without
 * running anything; the success path runs the action, THEN resets, so the
 * action record is consumed before the tokens it names are dropped.
 *
 * The line number handed to the tokeniser is 0 -- there is no line, and any
 * complaint about a typed cheat reports line zero.
 */
int32_t __cdecl ScriptRunLine(const char *line)
{
    AM2_ScriptCtx    ctx;
    AM2_ScriptAction act;
    int32_t          at = 0;

    /* A memset, which is what puts the zero in a register for the three
     * fields and leaves `at` an immediate. */
    memset(&ctx, 0, sizeof ctx);

    ScriptResetTokens(&ctx);
    ScriptNextToken(line, &ctx, 0);

    if (!ScriptParseAction(&ctx, &at, &act)) {
        ScriptResetTokens(&ctx);
        return 0;
    }

    RunScriptAction(&act, (void *)0);
    ScriptResetTokens(&ctx);
    return 1;
}

/* AddToVar -- original 0x00443F10, one caller.
 *
 * Read a script variable, add a delta, write it back, and answer whether both
 * halves succeeded. The one caller is the `changevar` action, which passes the
 * variable's index and the amount from the script.
 *
 * THE NON-POSITIVE GUARD IS ON THE INDEX, NOT THE VALUE: index 0 is "no
 * variable" throughout this subsystem, and a negative one is nothing at all.
 * Both answer 0 without touching anything.
 *
 * The original writes the sum back into its own local before calling
 * SetVarValue and never reads it again, so the local is dead; it is a local
 * here too only because that is what the two calls need between them.
 *
 * GetVarValue leaves its out-parameter alone on failure, and the original
 * zeroes the local before the call for exactly that reason -- so a failed read
 * would add the delta to 0 rather than to rubbish. It never gets that far,
 * because the failure is tested first, but the zero is reproduced: it is the
 * same care misc.cpp's ScriptArmyScore already needed.
 */
int32_t __cdecl AddToVar(int32_t index, int32_t delta)
{
    int32_t v = 0;
    int32_t ok;

    if (index > 0) {
        /* The read's answer is kept as a flag and tested after the add. */
        ok = GetVarValue(index, &v) != 0;
        v += delta;
        if (ok && SetVarValue(index, v))
            return 1;
    }
    return 0;
}

int script_install(void)
{
    int rc = 0;

    /* Token buffers pass between our code and the original's, so both sides
     * have to be on the game's heap. */
    am2_crt_use_game();

    ScriptReadEnv();

    rc |= patch_replace(ADDR_IS_BLANK,
                        (const void *)IsBlank, "IsBlank", 1);
    rc |= patch_replace(ADDR_IS_SCRIPT_DELIM,
                        (const void *)IsScriptDelim, "IsScriptDelim", 1);
    rc |= patch_replace(ADDR_SCRIPT_LOOKUP_TOKEN,
                        (const void *)ScriptLookupToken,
                        "ScriptLookupToken", 1);
    rc |= patch_replace(ADDR_SCRIPT_FREE_TOKEN,
                        (const void *)ScriptFreeToken,
                        "ScriptFreeToken", 1);
    rc |= patch_replace(ADDR_SCRIPT_GROW_TOKENS,
                        (const void *)ScriptGrowTokens,
                        "ScriptGrowTokens", 1);
    rc |= patch_replace(ADDR_SCRIPT_ADD_TOKEN,
                        (const void *)ScriptAddToken,
                        "ScriptAddToken", 1);
    rc |= patch_replace(ADDR_SCRIPT_RESET,
                        (const void *)ScriptResetTokens,
                        "ScriptResetTokens", 1);
    rc |= patch_replace(ADDR_SCRIPT_PARSE_NUMBER,
                        (const void *)ScriptParseNumber,
                        "ScriptParseNumber", 1);
    rc |= patch_replace(ADDR_SCRIPT_NEXT_TOKEN,
                        (const void *)ScriptNextToken,
                        "ScriptNextToken", 1);
    rc |= patch_replace(ADDR_SCRIPT_UNIQUE_NAME,
                        (const void *)ScriptBindUniqueName,
                        "ScriptBindUniqueName", 2);
    rc |= patch_replace(ADDR_DECLARE_BUILTIN_NAMES,
                        (const void *)DeclareBuiltinNames,
                        "DeclareBuiltinNames", 1);
    rc |= patch_replace(ADDR_SCRIPT_TOKEN_NAME,
                        (const void *)ScriptTokenName,
                        "ScriptTokenName", 1);
    rc |= patch_replace(ADDR_GET_VAR_VALUE, (const void *)GetVarValue,
                        "GetVarValue", 2);
    rc |= patch_replace(ADDR_SET_VAR_VALUE, (const void *)SetVarValue,
                        "SetVarValue", 2);
    rc |= patch_replace(ADDR_SET_VAR_BY_NAME, (const void *)SetVarValueByName,
                        "SetVarValueByName", 2);
    rc |= patch_replace(ADDR_ADD_TO_VAR, (const void *)AddToVar,
                        "AddToVar", 1);
    rc |= patch_replace(ADDR_FREE_SCRIPT_NAMES, (const void *)FreeScriptNames,
                        "FreeScriptNames", 0);
    rc |= patch_replace(ADDR_SAVE_SCRIPT_SECTION, (const void *)SaveScriptSection,
                        "SaveScriptSection", 1);
    rc |= patch_replace(ADDR_LOAD_SCRIPT_SECTION, (const void *)LoadScriptSection,
                        "LoadScriptSection", 1);
    rc |= patch_replace(ADDR_SCRIPT_FIND_NAME,
                        (const void *)ScriptFindName,
                        "ScriptFindName", 1);
    rc |= patch_replace(ADDR_SCRIPT_TOKEN_TEXT,
                        (const void *)ScriptTokenText,
                        "ScriptTokenText", 1);
    rc |= patch_replace(ADDR_SCRIPT_IS_STMT,
                        (const void *)ScriptIsStatementStart,
                        "ScriptIsStatementStart", 1);
    rc |= patch_replace(ADDR_LOAD_LEVEL_SCRIPT,
                        (const void *)LoadLevelScript, "LoadLevelScript", 0);
    rc |= patch_replace(ADDR_READ_SCRIPT,
                        (const void *)ReadScript,
                        "ReadScript", 1);
    rc |= patch_replace(ADDR_SCRIPT_ALLOC_UID,
                        (const void *)AllocUid,
                        "AllocUid", 1);
    rc |= patch_replace(ADDR_SCRIPT_RUN_LINE, (const void *)ScriptRunLine,
                        "ScriptRunLine", 3);
    rc |= patch_replace(ADDR_SCRIPT_ADD_NAME,
                        (const void *)AddNameTableName,
                        "AddNameTableName", 1);
    rc |= patch_replace(ADDR_SCRIPT_VARIABLE,
                        (const void *)ScriptVariable,
                        "ScriptVariable", 1);
    rc |= patch_replace(ADDR_SCRIPT_PRELOADSPRITE,
                        (const void *)ScriptPreloadSprite,
                        "ScriptPreloadSprite", 1);
    rc |= patch_replace(ADDR_SCRIPT_PAD,
                        (const void *)ScriptPad,
                        "ScriptPad", 1);
    rc |= patch_replace(ADDR_SCRIPT_IF,
                        (const void *)ScriptIf,
                        "ScriptIf", 1);
    rc |= patch_replace(ADDR_SCRIPT_SCAN_FOR,
                        (const void *)ScriptScanFor, "ScriptScanFor", 1);
    rc |= patch_replace(ADDR_SCRIPT_NAME_UID,
                        (const void *)ScriptNameUid, "ScriptNameUid", 1);
    rc |= patch_replace(ADDR_SCRIPT_INT_OR_VAR,
                        (const void *)ScriptIntOrVar, "ScriptIntOrVar", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJECT_UID,
                        (const void *)ScriptObjectUid, "ScriptObjectUid", 1);
    rc |= patch_replace(ADDR_SCRIPT_ARMY_COLOUR,
                        (const void *)ScriptArmyColour, "ScriptArmyColour", 1);
    rc |= patch_replace(ADDR_SCRIPT_RESOLVE_NAME,
                        (const void *)ScriptResolveName,
                        "ScriptResolveName", 1);
    rc |= patch_replace(ADDR_SCRIPT_PARSE_EVENTS,
                        (const void *)ScriptParseEvents,
                        "ScriptParseEvents", 1);
    rc |= patch_replace(ADDR_SCRIPT_PARSE_VALUE,
                        (const void *)ScriptParseValue,
                        "ScriptParseValue", 1);
    rc |= patch_replace(ADDR_SCRIPT_HIT_TARGET,
                        (const void *)ScriptHitTarget, "ScriptHitTarget", 1);
    rc |= patch_replace(ADDR_SCRIPT_ORDER_TARGET,
                        (const void *)ScriptOrderTarget,
                        "ScriptOrderTarget", 1);
    rc |= patch_replace(ADDR_SCRIPT_PARSE_EVENT,
                        (const void *)ScriptParseEvent,
                        "ScriptParseEvent", 1);
    /* Worth detouring even though our own ScriptIf and ScriptObjFrame call it
     * directly: ADDR_SCRIPT_RUN_LINE is still the original's and reaches it by
     * address, so without this a typed cheat code would run the original
     * parser while every script ran ours. Skipped under AM2_PROBE_NOACTION,
     * which is the only way the address still leads to the original. */
    if (!am2_probe_noaction)
        rc |= patch_replace(ADDR_SCRIPT_PARSE_ACTION,
                            (const void *)ScriptParseActionRecon,
                            "ScriptParseAction", 3);
    rc |= patch_replace(ADDR_SCRIPT_LOCATION,
                        (const void *)ScriptLocation, "ScriptLocation", 1);
    return rc;
}
