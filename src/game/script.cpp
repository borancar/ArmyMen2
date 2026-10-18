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
        g_difficulty);
    g_svarSystemSpeed = AddNameTableName(
        "systemspeed", AM2_NAME_TYPE_INTEGER,
        g_fastMachine);
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
void __cdecl ScriptBindUniqueName(AM2_Object *obj, char *name)
{
    AM2_Object *r = obj;
    char     tried[AM2_SCRIPT_UNIQUE_BUF];
    int32_t  n;
    int32_t  index;

    if (!obj || !name)
        return;

    n = 1;
    orig_strlwr(name);
    strcpy(tried, name);

    index = ScriptFindName(tried);
    while (index >= 0) {
        if (kScriptNames[index].value == 0) {
            r->nameIdx = index;
            kScriptNames[index].value =
                (int32_t)r->uid;
            return;
        }

        am2_sprintf(tried, (const char *)((void *)(uintptr_t)(AM2_STR_UNIQUE_SUFFIX)),
                    name, n++);
        index = ScriptFindName(tried);
    }

    r->nameIdx =
        AddNameTableName(tried, AM2_NAME_TYPE_REF,
                         (int32_t)r->uid);
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
        kScriptNames[kScriptNameCount].name = (char *)am2_malloc(strlen(name) + 1);
        strcpy(kScriptNames[kScriptNameCount].name, name);
        /* type, value and refs arrive and land as one 12-byte block. */
        *(AM2_NameMeta *)&kScriptNames[kScriptNameCount].type = *(const AM2_NameMeta *)&meta.type;
        kScriptNameCount++;
    }

    /* The two builtins a save must not freeze: they take the settings as
     * they are NOW, not as they were saved. Ours returned without these
     * until the relink showed the two calls. */
    SetVarValueByName("difficulty",
                      g_difficulty);
    SetVarValueByName("systemspeed",
                      g_fastMachine);
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

int32_t __cdecl AllocUid(void)
{
    return g_nextUid++;
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
    kScriptNames[kScriptNameCount].name = (char *)am2_malloc(strlen(name) + 1);
    strcpy(kScriptNames[kScriptNameCount].name, name);
    kScriptNames[kScriptNameCount].refs = 1;
    kScriptNames[kScriptNameCount].type = type;

    switch (type) {
    case 0:
        kScriptNames[kScriptNameCount].value = AllocUid();
        break;
    /* Three arms, identical, merged by the compiler -- written as one they
     * compile to a range test where the image has a four-slot table. */
    case 1:
        kScriptNames[kScriptNameCount].value = uid;
        break;
    case 2:
        kScriptNames[kScriptNameCount].value = uid;
        break;
    case 3:
        kScriptNames[kScriptNameCount].value = uid;
        break;
    default:
        am2_log("AddNameTableName: invalid type; %s has type %d and uid %d\n",
                name, type, uid);
        /* Stored anyway, and the count still advances. */
        kScriptNames[kScriptNameCount].value = uid;
        break;
    }

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

    if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        /* Each arm reads its own variable into *out. */
#define SVAR(a) (*(const int32_t *)((void *)(uintptr_t)(a)))
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case 15:            *out = SVAR(((uintptr_t)&g_svarId15));  break;   /* no keyword maps here */
        case AM2_TOK_GREEN: *out = SVAR(((uintptr_t)&g_svarGreen)); break;
        case AM2_TOK_TAN:   *out = SVAR(((uintptr_t)&g_svarTan));   break;
        case AM2_TOK_BLUE:  *out = SVAR(((uintptr_t)&g_svarBlue));  break;
        case AM2_TOK_GREY:  *out = SVAR(((uintptr_t)&g_svarGrey));  break;
        case AM2_TOK_ME:    *out = SVAR(((uintptr_t)&g_svarMe));    break;
        default:
            if (!quiet)
                am2_log("Line [%4d]:  Unexpected use of reserved word\n",
                        ctx->tokens[*at].line,
                        ScriptTokenText((&ctx->tokens[*at]), kScriptWord));
            return 0;
        }
#undef SVAR
        (*at)++;
        return 1;
    }

    /* Quiet is asked first, and the kind in each arm. */
    if (quiet) {
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING)
            return 0;
    } else if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText((&ctx->tokens[*at]), kScriptWord),
                kKindName(AM2_TOKEN_STRING));
        return 0;
    }

    int32_t idx = ScriptFindName((const char *)ctx->tokens[*at].value);
    *out = idx;

    if (idx < 0) {
        *out = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_REF, 0);
    } else {
        int32_t type = kScriptNames[idx].type;
        if (type != AM2_NAME_TYPE_REF && type != AM2_NAME_TYPE_INTEGER) {
            am2_log("Line [%4d]:  Name '%s' already used for another type.\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText((&ctx->tokens[*at]), kScriptWord));
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
static inline int32_t ScriptStep(AM2_ScriptCtx *ctx, int32_t *at)
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

    if (ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR) {
        int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (id != AM2_TOK_LPAREN && id != AM2_TOK_PLUS)       /* '(' or '+' */
            goto fail;                  /* silently, with no message */

        if ((int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_PLUS) { /* a leading '+' means relative */
            act->relative = 1;
            if (!ScriptStep(ctx, at))
                return 0;
        }
        if (!ScriptStep(ctx, at))       /* past the '(' */
            return 0;

        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->u.pos.x = (int16_t)(uintptr_t)ctx->tokens[*at].value;
            if (!ScriptStep(ctx, at))
                return 0;
        } else if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
                   (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_REFVAR) {
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
                    ScriptTokenText((&ctx->tokens[*at]), kScriptWord));
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_CONTROL_CHAR ||
            (int32_t)(uintptr_t)ctx->tokens[*at].value != AM2_TOK_COMMA) {      /* ',' */
            am2_log("Line [%4d]:  Comma expected in coordinates\n",
                    ctx->tokens[*at].line);
            return 0;
        }

        if (++(*at) >= ctx->count)      /* past the ',' */
            goto eos;

        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->u.pos.y = (int16_t)(uintptr_t)ctx->tokens[*at].value;
        } else if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
                   (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_REFVAR) {
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
                    ScriptTokenText((&ctx->tokens[*at]), kScriptWord));
            return 0;
        }
        if (++(*at) >= ctx->count) {
eos:
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_RPAREN) {  /* ')' */
            (*at)++;
            return 1;
        }
        am2_log("Line [%4d]:  Close parens expected in coordinates\n",
                ctx->tokens[*at].line);
fail:
        return 0;
    }

    if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
        idx = ScriptFindName((const char *)ctx->tokens[*at].value);
        if (idx >= 0 && kScriptNames[idx].type == AM2_NAME_TYPE_PAD) {          /* a pad */
            int32_t number = g_pads[kScriptNames[idx].value].number;
            /* Both centroid words at once, exactly as the original. */
            act->u.pos = g_padNumbers[number].centre;

            am2_free(ctx->tokens[*at].value);
            ctx->tokens[*at].kind = AM2_TOKEN_NAMEREF;
            ctx->tokens[*at].value = (void *)(uintptr_t)idx;
            (*at)++;
            return 1;
        }
        /* Not a pad -- fall through, and the name is resolved below. */
    }

    return ScriptResolveName(ctx, at, &act->target, quiet) != 0;
}

/* ScriptParseEvent: one `case` per event keyword, each written out in full
 * as the image has them -- the five `<verb> <target> [by <target>]` events and
 * the two pad events differ only in the kind they record and the messages
 * they print, which is what the macros carry. The pad macro takes its token
 * kind test as an argument: PADON writes `!=` and PADOFF a difference, a
 * number win per arm (the difference makes VC6 load the kind into a register
 * first, as the image does; `!=` in both arms makes PADOFF a memory compare). */
#define PAD_EVENT(K, NO_NAME, NOT_A_PAD, KTEST)                             \
    *kind = (K);                                                            \
    if (!ScriptStep(ctx, at))                                               \
        return 0;                                                           \
    tok = &ctx->tokens[*at];                                                \
    if (KTEST)                                                              \
        goto wrong_type;                                                    \
    name = (const char *)tok->value;                                        \
    idx = ScriptFindName(name);                                             \
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
    const char    *name;

    *val2 = 0;

    tok = &ctx->tokens[*at];
    if (tok->kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)tok->value) {
        case AM2_TOK_PADON:
            PAD_EVENT(AM2_EVT_PADON,
                      "Line [%4d]:  Name required after PADON\n",
                      "Line [%4d]:  PADON name was not a pad\n",
                      tok->kind != AM2_TOKEN_STRING);
        pad_found:
            /* The two pad events share this tail. */
            *val = kScriptNames[idx].value;
            (*at)++;
            return 1;
        case AM2_TOK_PADOFF:
            PAD_EVENT(AM2_EVT_PADOFF,
                      "Line [%4d]:  Name required after PADOFF\n",
                      "Line [%4d]:  PADOFF name was not a pad\n",
                      tok->kind - AM2_TOKEN_STRING);
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
#define SLOT(n) (((AM2_Comm *)g_armyTable)->CommSlotForArmy(n))
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

/* The same test on the token already reached. */
static inline int32_t ActAtEnd(const AM2_ScriptCtx *ctx, const int32_t *at)
{
    if (*at < ctx->count)
        return 1;
    am2_log("Unexpected end of script.\n");
    return 0;
}

/* Zero, with the message, when the token is not of the kind wanted. */
static inline int32_t ActTypeErr(const AM2_ScriptCtx *ctx, const int32_t *at, int32_t kind)
{
    am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
            ctx->tokens[*at].line, ScriptTokenText(&ctx->tokens[*at], kScriptWord), kKindName(kind));
    return 0;
}

static inline int32_t ActWant(const AM2_ScriptCtx *ctx, const int32_t *at, int32_t kind)
{
    if (ctx->tokens[*at].kind == kind)
        return 1;
    return ActTypeErr(ctx, at, kind);
}

/* `name` must be a declared integer variable: its index goes to xvar. */
static inline int32_t ActVarName(const AM2_ScriptCtx *ctx, int32_t *at, AM2_ScriptAction *act,
                                 const char *msg)
{
    int32_t v = ScriptFindName((const char *)ctx->tokens[*at].value);

    if (v < 0 || kScriptNames[v].type != AM2_NAME_TYPE_INTEGER) {
        am2_log(msg, ctx->tokens[*at].line, (const char *)ctx->tokens[*at].value);
        return 0;
    }
    act->xvar = v;
    (*at)++;
    return 1;
}

/* The same, as setvar and addvar test it: against -1 exactly. */
static inline int32_t ActVarNameEq(const AM2_ScriptCtx *ctx, int32_t *at, AM2_ScriptAction *act,
                                   const char *msg)
{
    int32_t v = ScriptFindName((const char *)ctx->tokens[*at].value);

    if (v == -1 || kScriptNames[v].type != AM2_NAME_TYPE_INTEGER) {
        am2_log(msg, ctx->tokens[*at].line, (const char *)ctx->tokens[*at].value);
        return 0;
    }
    act->xvar = v;
    (*at)++;
    return 1;
}


/* The trailing damage-kind word of `damage` and `setdamagepad`. Inline in
 * the image: two expansions, so the early exits of the second land on the
 * last bare return-1 copy instead of the first. */
static inline int32_t ActDamageKind(AM2_ScriptCtx *ctx, int32_t *at,
                                    AM2_ScriptAction *act)
{
    act->extra = 0;
    if (*at >= ctx->count || ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
        return 1;
    switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
    case AM2_TOK_PROJECTILE: act->extra = 2; (*at)++; return 1;
    case AM2_TOK_FIRE:       act->extra = 1; (*at)++; return 1;
    case AM2_TOK_CRUSH:      act->extra = 4; (*at)++; return 1;
    case AM2_TOK_EXPLOSION:  act->extra = 3; (*at)++; return 1;
    }
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
 * there.
 *
 * Transcribed case by case from the image, in its layout order, and checked
 * against it under Unicorn on every action in the shipped scripts plus a set
 * of error shapes (tools/actcheck.py). That pass found six answers the
 * earlier, table-driven version got wrong: setfacing/setgunfacing step over a
 * token that is neither a number nor `refvar`; a facing or a fireweapon/
 * unitfire bearing is cut to a byte; setitemowner steps past its number;
 * createroach, ally/unally and setforcecolor return whether the army parsed;
 * setobjstate, makesmoke/makeflame and createpowerup read their trailing
 * number with no bounds test; createpowerup ends silently at end of script;
 * createexplosion tests the bound before each step, not after. */
static int32_t ScriptParseActionRecon(AM2_ScriptCtx *ctx, int32_t *at,
                                      AM2_ScriptAction *act)
{
/* Every case is written out for itself, as the image has them: the token
 * step, the kind test with its own error, the store. The step, the end test,
 * the kind test and the variable-name lookup are the inline functions above,
 * each used as `if (!f(...)) return 0;`, and there is NO shared end-of-script
 * label: VC6 folds the eighty-odd identical `log; return 0` copies into the
 * one beside `default: return 0` (MOVEITEM's) and keeps the fall-through ones
 * private (CREATETROOPER, FIREWEAPON, TRIGGERDELAY), which is the image's
 * layout. A `goto eos` spelling can never reproduce it (docs/vc6-regalloc.md). */
    memset(act, 0, sizeof *act);
    act->uid = -2;

    if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
        return 0;

    switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {

    case AM2_TOK_SETCAMERAFOCUS:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x1D;
        /* Several arms end in this same call, each written out; VC6 points
         * the jumps of SUSPENDAI and REVIVEAI at this first copy, as in the
         * image. A goto from SUSPENDAI to a label here sends its jump to a
         * later copy instead. */
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case AM2_TOK_RESTORECAMERAFOCUS:
        (*at)++;
        act->code = 0x1E;
        return 1;

    case AM2_TOK_SUSPENDAI:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x1B;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            /* No name means everyone. */
            act->subject = g_svarId15;
            return 1;
        }
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case AM2_TOK_REVIVEAI:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x1C;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            act->subject = g_svarId15;
            return 1;
        }
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    /* The record keeps the token's own string. Nothing copies it and nothing
     * frees it here. */
    case AM2_TOK_SHOWMESSAGE:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 1;
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SHOWBITMAP:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 2;
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SHOWBITMAPNOPAUSE:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 3;
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SHOWFAILURE:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 4;
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SHOWPDA:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 5;
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_PLAYSOUND:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 6;
        act->text = (char *)ctx->tokens[*at].value;
        act->n0 = 0;
        act->n1 = 1;
        act->extra = 0;
        if (++(*at) >= ctx->count)
            return 1;
        ScriptLocation(ctx, at, act, 1);
        if (*at >= ctx->count || ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count || ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        /* No bounds check before this one, unlike the two above. */
        if (ctx->tokens[++(*at)].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->extra = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_PLAYITEMSOUND:
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->code = 7;
        act->text = (char *)ctx->tokens[*at].value;
        act->n0 = 0;
        act->n1 = 1;
        act->extra = 0;
        if (++(*at) >= ctx->count)
            return 1;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count || ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count || ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (ctx->tokens[++(*at)].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->extra = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_PLAYMUSIC:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 8;
        act->text = 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING)
            return 1;
        act->text = (char *)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count || ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_TRACEVAR:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 9;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        return ActVarName(ctx, at, act, "Line [%4d]:  expected variable name in TRACEVAR but variable "
                "%s not declared\n");

    case AM2_TOK_TRIGGER:
        if (!ActStep(ctx, at))
            return 0;
        /* The uid lands at +0x00 and the zero at +0x04 -- the call pushes
         * the record and then the record plus four. TRIGGERDELAY ends in
         * the same call, and the image has one copy of it, in that arm. */
        return ScriptObjectUid(ctx, at, &act->uid2, &act->uid) != 0;

    case AM2_TOK_TRIGGERDELAY:
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED
            && ((int32_t)(uintptr_t)ctx->tokens[*at].value) == AM2_TOK_REFVAR) {
            if (!ActStep(ctx, at))
                return 0;
            /* ActVarName written out with the step folded into its own test:
             * the image keeps this arm's end-of-script message inline and
             * jumps back to TRIGGER's uid call, where ActVarName's separate
             * increment let VC6 share the message and copy the call. */
            {
                int32_t v = ScriptFindName((const char *)ctx->tokens[*at].value);
                if (v < 0 || kScriptNames[v].type != AM2_NAME_TYPE_INTEGER) {
                    am2_log("Line [%4d]:  expected variable name but variable %s not "
                            "declared\n", ctx->tokens[*at].line, (const char *)ctx->tokens[*at].value);
                    return 0;
                }
                act->xvar = v;
            }
            if (++(*at) >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
            goto trigger_uid;
        } else if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
        else {
            act->delay = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (act->delay < 0) {
                am2_log("Line[%4d]:  Negative time set for a TRIGGERDELAY.\n",
                        ctx->tokens[*at].line);
                return 0;
            }
            (*at)++;
        }
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
    trigger_uid:
        return ScriptObjectUid(ctx, at, &act->uid2, &act->uid) != 0;

    case AM2_TOK_CREATETROOPER:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0D;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->text = (char *)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_RIFLE:     act->item = 1;    break;
        case AM2_TOK_BAZOOKA:   act->item = 4;    break;
        case AM2_TOK_GRENADE:   act->item = 2;    break;
        case AM2_TOK_FLAMER:    act->item = 3;    break;
        case AM2_TOK_MORTAR:    act->item = 5;    break;
        case AM2_TOK_AUTORIFLE: act->item = 10;   break;
        case AM2_TOK_VULCAN:    act->item = 29;   break;
        case AM2_TOK_SNIPER:    act->item = 30;   break;
        case AM2_TOK_SWEEPER:   act->item = 20;   break;
        default:
            /* flak, yours, explosive, detonator, lure, heavymg -- weapons
             * the powerup table has and this one does not */
            am2_log("Line [%4d]:  Illegal weapon type '%s'\n",
                    ctx->tokens[*at].line, ScriptTokenText(&ctx->tokens[*at], kScriptWord));
            return 0;
        }
        /* A location and an army, and the army's own result decides the
         * return. Written out in both creates rather than as one inline
         * helper: from the helper VC6 kept a single copy after
         * createvehicle; written out, the step stays here and
         * createvehicle's arms jump back to it, as in the image. */
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        {
            int32_t army = ScriptArmyColour(ctx, at);
            act->army = army;
            return army >= 0;
        }

    case AM2_TOK_CREATEVEHICLE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0F;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->text = (char *)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_JEEP:      act->n0 = 0;      break;
        case AM2_TOK_TANK:      act->n0 = 1;      break;
        case AM2_TOK_HALFTRACK: act->n0 = 2;      break;
        case AM2_TOK_CONVOY:    act->n0 = 3;      break;
        case AM2_TOK_BOAT:      act->n0 = 5;      break;
        default:                /* `vehicle` itself is not a chassis */
            am2_log("Line [%4d]:  Illegal vehicle type '%s'\n",
                    ctx->tokens[*at].line, ScriptTokenText(&ctx->tokens[*at], kScriptWord));
            return 0;
        }
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        {
            int32_t army = ScriptArmyColour(ctx, at);
            act->army = army;
            return army >= 0;
        }

    case AM2_TOK_CREATEPOWERUP:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x10;
        /* The name is optional here, unlike the other two creates. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
            act->text = (char *)ctx->tokens[*at].value;
            if (!ActStep(ctx, at))
                return 0;
        }
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_BAZOOKA:       act->item = 4;    break;
        case AM2_TOK_GRENADE:       act->item = 2;    break;
        case AM2_TOK_FLAMER:        act->item = 3;    break;
        case AM2_TOK_MORTAR:        act->item = 5;    break;
        case AM2_TOK_AUTORIFLE:     act->item = 10;   break;
        case AM2_TOK_FLAK:          act->item = 28;   break;
        case AM2_TOK_MINE:          act->item = 11;   break;
        case AM2_TOK_EXPLOSIVE:     act->item = 12;   break;
        case AM2_TOK_SWEEPER:       act->item = 20;   break;
        case AM2_TOK_MEDPACK:       act->item = 23;   break;
        case AM2_TOK_MEDKIT:        act->item = 22;   break;
        case AM2_TOK_LURE:          act->item = 14;   break;
        case AM2_TOK_AIRSTRIKE:     act->item = 24;   break;
        case AM2_TOK_PARATROOPERS:  act->item = 25;   break;
        case AM2_TOK_RECON:         act->item = 26;   break;
        case AM2_TOK_VULCAN:        act->item = 29;   break;
        case AM2_TOK_HEAVYMG:       act->item = 8;    break;
        case AM2_TOK_SNIPER:        act->item = 30;   break;
        case AM2_TOK_VEHICLEAMMO:   act->item = 32;   break;
        case AM2_TOK_VEHICLEARMOR:  act->item = 31;   break;
        case AM2_TOK_VEHICLENITRO:  act->item = 33;   break;
        case AM2_TOK_CAMOUFLAGE:    act->item = 34;   break;
        case AM2_TOK_DISGUISEGREEN: act->item = 35;   break;
        case AM2_TOK_DISGUISETAN:   act->item = 36;   break;
        case AM2_TOK_DISGUISEBLUE:  act->item = 37;   break;
        case AM2_TOK_DISGUISEGREY:  act->item = 38;   break;
        case AM2_TOK_MAGNIFYING:    act->item = 39;   break;
        case AM2_TOK_AEROSOL:       act->item = 40;   break;
        case AM2_TOK_WRENCH:        act->item = 41;   break;
        case AM2_TOK_M80:           act->item = 42;   break;
        default:                    /* `detonator` is not a pickup */
            am2_log("Line [%4d]:  Illegal powerup type '%s'\n",
                    ctx->tokens[*at].line, ScriptTokenText(&ctx->tokens[*at], kScriptWord));
            return 0;
        }
        /* Running out here is fine and silent. */
        if (++(*at) >= ctx->count)
            return 1;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        /* No bounds check before this read. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_CREATEEXPLOSION:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0E;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (act->n0 < 0x78 || act->n0 > 0x95) {
            am2_log("Line[%4d]:  Invalid explosion type.\n", ctx->tokens[*at].line);
            act->n0 = 0x7F;
        }
        act->n1 = 0;
        act->army = 4;
        /* The bound is tested BEFORE the step, each time. */
        if (*at >= ctx->count)
            return 1;
        (*at)++;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (*at >= ctx->count)
            return 1;
        (*at)++;
        act->army = ScriptArmyColour(ctx, at);
        if (act->army >= 0)
            return 1;
        act->army = 4;
        return 1;

    case AM2_TOK_CREATEROACH:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x11;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->text = (char *)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        act->army = ScriptArmyColour(ctx, at);
        return act->army >= 0;

    case AM2_TOK_SETOBJSTATE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x25;
        /* The object at +0x1C and the state at +0x18 -- the other way round
         * from how the statement reads. */
        if (!ScriptResolveName(ctx, at, &act->target, 0))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        {
            int32_t v = ScriptFindName((const char *)ctx->tokens[*at].value);

            if (v < 0)
                v = AddNameTableName((const char *)ctx->tokens[*at].value,
                                     AM2_NAME_TYPE_REF, 0);
            act->subject = v;
        }
        (*at)++;
        /* No bounds check before this read. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SETOBJBMP:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x24;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_HEAL:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0A;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SETMAXHEALTH:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0B;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_DAMAGE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x0C;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return ActDamageKind(ctx, at, act);

    case AM2_TOK_DEPLOY:
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

    case AM2_TOK_UNDEPLOY:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x13;
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case AM2_TOK_RESURRECT:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x14;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (*at >= ctx->count)
            return 1;
        ScriptLocation(ctx, at, act, 1);      /* as `deploy` */
        return 1;

    case AM2_TOK_ALLY:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x15;
        act->subject = ScriptArmyColour(ctx, at);
        if (act->subject < 0)
            return 0;
        act->target = ScriptArmyColour(ctx, at);
        return act->target >= 0;

    case AM2_TOK_UNALLY:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x16;
        act->subject = ScriptArmyColour(ctx, at);
        if (act->subject < 0)
            return 0;
        act->target = ScriptArmyColour(ctx, at);
        return act->target >= 0;

    case AM2_TOK_SETFORCECOLOR:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x17;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        act->target = ScriptArmyColour(ctx, at);
        return act->target >= 0;

    case AM2_TOK_MOVEITEM:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x1F;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        return ScriptLocation(ctx, at, act, 0) != 0;

    default:
        /* Every other reserved word in 64..186 -- the weapons, the pickups,
         * the AI modes and the poses. They are arguments, not actions. */
        return 0;

    case AM2_TOK_ORDER:
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            if (!ScriptResolveName(ctx, at, &act->subject, 0))
                return 0;
            act->extra = 0;
        } else if (!ScriptOrderTarget(ctx, at, &act->extra, &act->subject,
                                      &act->army))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        /* `order <who> follow <whom>` or `order <who> goto <somewhere>`. */
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_FOLLOW:
            if (!ActStep(ctx, at))
                return 0;
            act->code = 0x19;
            if (!ScriptResolveName(ctx, at, &act->target, 0))
                return 0;
            break;
        case AM2_TOK_GOTO:
            if (!ActStep(ctx, at))
                return 0;
            act->code = 0x18;
            if (!ScriptLocation(ctx, at, act, 0))
                return 0;
            break;
        default:
            return 0;
        }
        act->n0 = -1;
        /* No bounds check before this read. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED
            || ((int32_t)(uintptr_t)ctx->tokens[*at].value) != AM2_TOK_INMODE)
            return 1;
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_ATTACK: (*at)++; act->n0 = 6; return 1;
        case AM2_TOK_DEFEND: (*at)++; act->n0 = 7; return 1;
        case AM2_TOK_IGNORE: (*at)++; act->n0 = 2; return 1;
        case AM2_TOK_EVADE:  (*at)++; act->n0 = 5; return 1;
        }
        return 0;

    case AM2_TOK_SETFACING:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x20;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            /* A facing is a byte: the value is stored, then cut to one. */
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            act->n0 = (uint8_t)act->n0;
            (*at)++;
            return 1;
        }
        /* A facing may be `refvar <name>` instead of a literal. Anything
         * else is stepped over and leaves the field at zero. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED
            || ((int32_t)(uintptr_t)ctx->tokens[*at].value) != AM2_TOK_REFVAR)
            goto step_ret;
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        /* ActVarName written out; SETGUNFACING's is the same words. */
        {
            int32_t v = ScriptFindName((const char *)ctx->tokens[*at].value);
            if (v < 0 || kScriptNames[v].type != AM2_NAME_TYPE_INTEGER) {
                am2_log("Line [%4d]:  expected variable name but variable %s not "
                        "declared\n", ctx->tokens[*at].line, (const char *)ctx->tokens[*at].value);
                return 0;
            }
            act->xvar = v;
            (*at)++;
        }
        return 1;

    case AM2_TOK_SETGUNFACING:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x21;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            act->n0 = (uint8_t)act->n0;
            (*at)++;
            return 1;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED
            || ((int32_t)(uintptr_t)ctx->tokens[*at].value) != AM2_TOK_REFVAR)
            goto step_ret;
        if (!ActStep(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        return ActVarName(ctx, at, act, "Line [%4d]:  expected variable name but variable %s not "
                "declared\n");

    case AM2_TOK_SETVAR:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x23;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }
        if (!ActVarNameEq(ctx, at, act, "Line [%4d]:  expected variable name in SetVar but "
                   "variable %s not declared\n"))
            return 0;
        return 1;

    case AM2_TOK_ADDVAR:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x22;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        if (!ActVarNameEq(ctx, at, act, "Line [%4d]:  expected variable name in AddVar but "
                   "variable %s not declared\n"))
            return 0;
        return 1;

    case AM2_TOK_ACTIVATEREGION:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x26;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->subject = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        act->n0 = 1;
        (*at)++;
        return 1;

    case AM2_TOK_INACTIVATEREGION:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x26;
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        /* The value is read before the zero is stored: the image loads it,
         * stores the zero, stores it, then reloads the index. Written the
         * other way round VC6 recomputes the token's address after the
         * store. */
        {
            int32_t n = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            act->n0 = 0;
            act->subject = n;
        }
        (*at)++;
        return 1;

    case AM2_TOK_SETBRIEFING:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x27;
        goto string_arg;

    case AM2_TOK_SETBRIEFVO:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x28;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->text = (char *)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SETSTRATMAP:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x29;
    string_arg:
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
        act->text = (char *)ctx->tokens[*at].value;
    step_ret:
        (*at)++;
        return 1;

    case AM2_TOK_SETAIMODE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2A;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            if (!ScriptResolveName(ctx, at, &act->subject, 0))
                return 0;
            act->extra = 0;
        } else if (!ScriptOrderTarget(ctx, at, &act->extra, &act->subject,
                                      &act->army))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_ATTACK: (*at)++; act->n0 = 6; return 1;
        case AM2_TOK_DEFEND: (*at)++; act->n0 = 7; return 1;
        case AM2_TOK_IGNORE: (*at)++; act->n0 = 2; return 1;
        case AM2_TOK_EVADE:  (*at)++; act->n0 = 5; return 1;
        }
        am2_log("Line [%4d]:  Expected {attack|defend|ignore|evade} "
                "in setaimode\n", ctx->tokens[*at].line);
        return 0;

    case AM2_TOK_SETAIPOSE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2B;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        /* stand 1, kneel 2, prone 3, none 0. */
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_STAND: (*at)++; act->n0 = 1; return 1;
        case AM2_TOK_KNEEL: (*at)++; act->n0 = 2; return 1;
        case AM2_TOK_PRONE: (*at)++; act->n0 = 3; return 1;
        case AM2_TOK_NONE:  (*at)++; act->n0 = 0; return 1;
        }
        am2_log("Line [%4d]:  Expected {stand|kneel|prone|none} in "
                "setaipose\n", ctx->tokens[*at].line);
        return 0;

    case AM2_TOK_SETSPEED:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2C;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (!ActWant(ctx, at, AM2_TOKEN_RESERVED))
            return 0;
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_SLOW:   (*at)++; act->n0 = 1; return 1;
        case AM2_TOK_NORMAL: (*at)++; act->n0 = 0; return 1;
        }
        am2_log("Line [%4d]:  Expected {slow|normal} in setspeed\n",
                ctx->tokens[*at].line);
        return 0;

    case AM2_TOK_SETNPC:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2D;
        goto name_then_int;     /* the image shares ADDEXP's tail */

    case AM2_TOK_SETITEMFLAG:
        {
            if (!ActStep(ctx, at))
                return 0;
            const char *kn = kKindName(AM2_TOKEN_RESERVED);

            if (!ScriptResolveName(ctx, at, &act->subject, 0))
                return 0;
            if (!ActAtEnd(ctx, at))
                return 0;
            /* ActTypeErr written out with the kind name in a local taken right
             * after the first step: hoisting it from the error block into the
             * case's own block moves the web the image has here (218 -> 50). */
            if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
                am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                        ctx->tokens[*at].line, ScriptTokenText(&ctx->tokens[*at], kScriptWord), kn);
                return 0;
            }
            if (((int32_t)(uintptr_t)ctx->tokens[*at].value) != AM2_TOK_STRATEGIC) {
                am2_log("Line [%4d]:  Expected {strategic} in setitemflag\n",
                        ctx->tokens[*at].line);
                return 0;
            }
            if (!ActStep(ctx, at))
                return 0;
            act->code = 0x33;
            if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
                return 0;
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }

    case AM2_TOK_SETUILOCK:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x39;
        /* The same words as ADDEXP's tail, and VC6 keeps ADDEXP's copy:
         * this arm becomes one jump into it from the token load on, as in
         * the image. A goto there instead gives the block a second
         * predecessor before allocation and VC6 allocates it the register
         * way (docs/vc6-regalloc.md). */
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_SETDAMAGEPAD:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x3A;
        if (!ActWant(ctx, at, AM2_TOKEN_STRING))
            return 0;
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
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        act->n0 = 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
        }
        return ActDamageKind(ctx, at, act);

    case AM2_TOK_SETITEMOWNER:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x34;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = 4;
            (*at)++;
        } else {
            act->n0 = ScriptArmyColour(ctx, at);
            if (act->n0 < 0)
                return 0;
        }
        return 1;

    case AM2_TOK_SETM80:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2E;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        act->army = ScriptArmyColour(ctx, at);
        if (act->army >= 0)
            return 1;
        act->army = 4;
        return 1;

    case AM2_TOK_DROPITEM:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x38;
        /* The item goes to +0x40 and whoever drops it to +0x18, which is the
         * other way round from every other two-name action. */
        if (!ScriptResolveName(ctx, at, &act->army, 0))
            return 0;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ActAtEnd(ctx, at))
            return 0;
        return ScriptLocation(ctx, at, act, 0) != 0;

    case AM2_TOK_SETZOMBIE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x2F;
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case AM2_TOK_SETSCIENTIST:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x30;
        return ScriptResolveName(ctx, at, &act->subject, 0) != 0;

    case AM2_TOK_MAKESMOKE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x31;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        /* No bounds check before this read. */
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
        }
        return 1;

    case AM2_TOK_MAKEFLAME:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x32;
        if (!ScriptLocation(ctx, at, act, 0))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
        }
        return 1;

    case AM2_TOK_ADDEXP:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x35;
    name_then_int:
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        /* This arm owns the type-error block six INTEGER and STRING arms
         * share from their kind-name load on (the token's address pushed
         * from eax), and SETUILOCK's whole tail is a jump into this one.
         * Both need the plain WANT here and in SETUILOCK: a positive test
         * in either arm, or the step before the store, hands the block to
         * SETUILOCK's copy or leaves ADDEXP's test in a register
         * (docs/vc6-regalloc.md). */
        if (!ActWant(ctx, at, AM2_TOKEN_INTEGER))
            return 0;
        act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case AM2_TOK_FIREWEAPON:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x36;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (!ScriptResolveName(ctx, at, &act->army, 0))
            return 0;
        /* A bearing is a byte: the value is stored, then cut to one. */
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            act->n0 = (uint8_t)act->n0;
            if (++(*at) >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
        } else
            act->n0 = -1;
        return ScriptLocation(ctx, at, act, 0) != 0;

    case AM2_TOK_UNITFIRE:
        if (!ActStep(ctx, at))
            return 0;
        act->code = 0x37;
        if (!ScriptResolveName(ctx, at, &act->subject, 0))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            act->n0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            act->n0 = (uint8_t)act->n0;
            if (!ActStep(ctx, at))
                return 0;
        } else
            act->n0 = -1;
place:
        return ScriptLocation(ctx, at, act, 0) != 0;
    }
#undef STEP
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

int32_t __cdecl ScriptPad(AM2_ScriptCtx *ctx, int32_t *at)
{
    int32_t name, number, total;

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

    if (g_padNumbers[number].count == 0) {
        if (number >= 8) {
            if (g_mapPadLayer) {
                int32_t i, sx, sy, n;

                /* One dword store covering both words -- the default before
                 * the scan. */
                n = 0;
                g_padNumbers[number].centre = g_zeroPoint;
                sx = 0;
                sy = 0;
                total = g_mapTilesW * g_mapTilesH;
                for (i = 0; i < total; i++) {
                    if ((int32_t)g_mapPadLayer[i] == number) {
                        sx += i % g_mapTilesH;
                        n++;
                        sy += i / g_mapTilesH;
                    }
                }
                if (n > 0) {
                    /* Sixteenths, rounded. */
                    g_padNumbers[number].cx = (int16_t)((sx * 16 + 8) / n);
                    g_padNumbers[number].cy = (int16_t)((sy * 16 + 8) / n);
                }
            }
        } else if (g_mapPadbitLayer) {
            int32_t i, sx, sy, n;

            g_padNumbers[number].centre = g_zeroPoint;
            sx = 0;
            sy = 0;
            total = g_mapTilesW * g_mapTilesH;
            for (i = 0, n = 0; i < total; i++) {
                if ((g_mapPadbitLayer[i] | kPadBit(number)) != 0) {
                    sx += i % g_mapTilesH;
                    sy += i / g_mapTilesH;
                    n++;
                }
            }
            if (n > 0) {
                g_padNumbers[number].cx = (int16_t)((sx * 16 + 8) / n);
                g_padNumbers[number].cy = (int16_t)((sy * 16 + 8) / n);
            }
        }
    }

    g_pads[g_padCount].id = g_padCount;
    g_pads[g_padCount].name = name;
    g_pads[g_padCount].compared = 0;
    g_pads[g_padCount].number = number;

    /* The slot takes the count itself, which is the id just stored: the
     * image stores the register holding g_padCount (`mov word ptr [...], bp`)
     * rather than re-reading the id; reading the id back costs the shape
     * seven. */
    g_padNumbers[number].pads[g_padNumbers[number].count] = (int16_t)g_padCount;
    g_padNumbers[number].count++;

    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    /* Trigger words, until one that is not a trigger word. */
    while (1) {
        if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_EVERYTHING:    g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_EVERYTHING; break;
        case AM2_TOK_SARGE:         g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_SARGE;      break;
        case AM2_TOK_UNIT:          g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_UNIT;       break;
        case AM2_TOK_TROOPER:       g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_TROOPER;    break;
        case AM2_TOK_TANK:          g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_TANK;       break;
        case AM2_TOK_VEHICLE:       g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_VEHICLE;    break;
        case AM2_TOK_HALFTRACK:     g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_GREY;       break;
        case AM2_TOK_CONVOY:        g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_CONVOY;     break;
        case AM2_TOK_BOAT:          g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_BOAT;       break;
        case AM2_TOK_GROUNDVEHICLE: g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_GROUNDVEH;  break;
        case AM2_TOK_NPC:           g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_NPC;        break;
        /* GREEN and ARMY0, TAN and ARMY1 have identical bodies but are separate
         * arms: the image's switch has them apart, and merged labels cost 74 bytes. */
        case AM2_TOK_GREEN:         g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_GREEN;      break;
        case AM2_TOK_ARMY0:         g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_GREEN;      break;
        case AM2_TOK_TAN:           g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_TAN;        break;
        case AM2_TOK_ARMY1:         g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_TAN;        break;
        case AM2_TOK_BLUE:
        case AM2_TOK_ARMY2:         g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_BLUE;       break;
        case AM2_TOK_GREY:
        case AM2_TOK_ARMY3:         g_pads[g_padCount].trigger |= (int32_t)AM2_PADTRIG_GREY;       break;
        default:
            goto triggers_done;
        }
        if (++(*at) >= ctx->count)
            goto triggers_done;
        } else
            break;
    }

    /* A String here names one specific item instead, which the flags
     * exclude. */
    if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
        if (g_pads[g_padCount].trigger != 0) {
            am2_log("Line [%4d]:  Pad can't have both specific item "
                    "and generic triggers\n", ctx->tokens[*at].line);
            return 0;
        }
        if (!ScriptResolveName(ctx, at, &number, 0))
            return 0;
        g_pads[g_padCount].specific = 1;
        g_pads[g_padCount].trigger = number;
    }
triggers_done:

    /* An optional comparison, then the count it compares against. A pad
     * that ends after its trigger words -- or whose next token is not a
     * control character -- goes straight to binding the name and is never
     * finalised: both of those paths land on 0x00444842, past the call. */
    if (*at != ctx->count &&
        (ctx->tokens[*at].kind - AM2_TOKEN_CONTROL_CHAR) == 0) {
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_GT: g_pads[g_padCount].compare = AM2_PADCMP_GT; break;       /* '>' */
        case AM2_TOK_EQ: g_pads[g_padCount].compare = AM2_PADCMP_EQ; break;       /* '=' */
        case AM2_TOK_LT: g_pads[g_padCount].compare = AM2_PADCMP_LT; break;       /* '<' */
        default:
            am2_log("Line [%4d]:  Unexpected symbol in pad definition "
                    "should be '<=>'\n", ctx->tokens[*at].line);
            (void)*(volatile int32_t *)&number;
            return 0;
        }

        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        g_pads[g_padCount].compared = 1;

        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(&ctx->tokens[*at], kScriptWord),
                    kKindName(AM2_TOKEN_INTEGER));
            return 0;
        }
        g_pads[g_padCount].threshold = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;

        /* At the end of the script the pad is finalised at once; the
         * image has this call twice and VC6 shares its tail. */
        if (*at == ctx->count) {
            PadFinalise(&g_pads[g_padCount], (AM2_Object *)(void *)0);
        } else {
            /* `delay <int> <int>`, optional, and only after a comparison. */
            if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
                (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_DELAY) {
                if (!ScriptExpect(ctx, at, AM2_TOKEN_INTEGER))
                    return 0;
                g_pads[g_padCount].delay0 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
                /* The second check written out rather than through
                 * ScriptExpect: inlined twice the same way, the two copies
                 * merge, where the image keeps three integer checks. */
                if (++(*at) >= ctx->count) {
                    am2_log("Unexpected end of script.\n");
                    return 0;
                }
                if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
                    am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                            ctx->tokens[*at].line,
                            ScriptTokenText(&ctx->tokens[*at], kScriptWord),
                            kKindName(AM2_TOKEN_INTEGER));
                    return 0;
                }
                g_pads[g_padCount].delay1 = (int32_t)(uintptr_t)ctx->tokens[*at].value;
                (*at)++;
            }

            /* The second argument is an OBJECT and the original passes a literal
             * zero here, which the callee reads as "no object" -- so the notify
             * this pad eventually sends carries a zero uid. */
            PadFinalise(&g_pads[g_padCount], (AM2_Object *)(void *)0);
        }
    }

    /* The name resolves to the pad, and only now does the pad count move. */
    kScriptNames[g_pads[g_padCount].name].value = g_padCount;
    g_padCount++;
    return 1;
}

/* ---------------------------------------------------------------- if ---- */

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
    int32_t na, nb, nc;     /* the butnot event's own */
    AM2_ScriptAction act;
    AM2_ScriptCond *cond;

    /* The token is addressed afresh at every use, and a keyword test reads
     * the kind again each time. Three shapes of step, each where the image
     * has it: to the shared end-of-script block, to the second one inside
     * the action list, or with its own message (ScriptStep). */
#define IS_WORD(w)  ((&ctx->tokens[*at])->kind == AM2_TOKEN_RESERVED \
                     && (int32_t)(uintptr_t)(&ctx->tokens[*at])->value == (w))
#define STEP_EOS()  if (++(*at) >= ctx->count) goto end_of_script
#define STEP_LATE() if (++(*at) >= ctx->count) { am2_log("Unexpected end of script.\n"); return 0; }
#define STEP_OWN()  if (!ScriptStep(ctx, at)) return 0
/* A wrong token kind logs and returns WITHOUT freeing the record -- another
 * of the original's leaks. */
#define WANT_KIND(K)                                                        \
    if ((&ctx->tokens[*at])->kind != (K)) {                                 \
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n", \
                ctx->tokens[*at].line, ScriptTokenText((&ctx->tokens[*at]), kScriptWord), \
                kKindName(K));                                              \
        return 0;                                                           \
    }

    STEP_EOS();

    cond = (AM2_ScriptCond *)am2_malloc(sizeof *cond);
    memset(cond, 0, sizeof *cond);

    if (IS_WORD(AM2_TOK_TIMEABSOLUTE)) {
        cond->kind = AM2_IF_TIMEABSOLUTE;
        STEP_EOS();
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  Exptected number after TIMEABSOLUTE\n",
                    ctx->tokens[*at].line);
            goto fail;
        }
        cond->number = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (cond->number < 1) {
            am2_log("Line [%4d]:  TIMEABSOLUTE time must be positive\n",
                    ctx->tokens[*at].line);
            goto fail;
        }
        /* Then on to the testvar clause like every other form: the image
         * jumps to the TESTVAR test (0x004438D2), not to `then`, so
         * `timeabsolute N testvar ...` is accepted. This skipped it. */
        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

    } else if (ScriptScanFor(ctx, *at, AM2_TOK_AFTER, AM2_TOK_THEN)) {
        /* An `<event> after <event>` chain. */
        cond->kind = AM2_IF_AFTER;
        while (ctx->tokens[*at].kind == AM2_TOKEN_STRING
               || ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
            int32_t a, b, c3;   /* block-scoped: the image reads them past the realloc */

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

    } else if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)ctx->tokens[*at].value) {
        case AM2_TOK_ALLOF:
            cond->kind = AM2_IF_ALLOF;
            if ((uint32_t)++(*at) >= (uint32_t)ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
            break;
        case AM2_TOK_INORDER:
            cond->kind = AM2_IF_INORDER;
            if ((uint32_t)++(*at) >= (uint32_t)ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
            break;
        case AM2_TOK_REPEAT:
            cond->kind = AM2_IF_REPEAT;
            STEP_EOS();
            WANT_KIND(AM2_TOKEN_INTEGER)
            cond->number = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            STEP_OWN();
            if (!IS_WORD(AM2_TOK_OF)) {
                am2_log("Line [%4d]:  Missing 'of' in if-repeat statement.\n",
                        ctx->tokens[*at].line);
                goto fail;
            }
            STEP_OWN();
            break;
        case AM2_TOK_COUNT:
            cond->kind = AM2_IF_COUNT;
            STEP_EOS();
            WANT_KIND(AM2_TOKEN_INTEGER)
            cond->number = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            STEP_EOS();
            if (!IS_WORD(AM2_TOK_OF)) {
                am2_log("Line [%4d]:  Missing 'of' in if-repeat statement.\n",
                        ctx->tokens[*at].line);
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
            if ((uint32_t)*at >= (uint32_t)ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
        }

    } else if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
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
#define IS_CTRL(c) ((&ctx->tokens[*at])->kind == AM2_TOKEN_CONTROL_CHAR \
                    && (int32_t)(uintptr_t)(&ctx->tokens[*at])->value == (c))
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
then_word:
    if (!IS_WORD(AM2_TOK_THEN)) {
        am2_log("Line [%4d]:  Missing 'then' in if-statement.\n", ctx->tokens[*at].line);
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
        objname = ScriptFindName((const char *)ctx->tokens[*at].value);
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

    while (*at < ctx->count && ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR
           && (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_COMMA) {    /* ',' */
        STEP_LATE();
        if (cond->mode == AM2_THEN_ONOBJSTATE) {
            WANT_KIND(AM2_TOKEN_STRING)
            objname = ScriptFindName((const char *)ctx->tokens[*at].value);
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
    cond->next = g_scriptConditions;
    g_scriptConditions = cond;
    return 1;

fail:
    am2_free(cond);
    return 0;

fail_nofree:
    /* A failing event parse returns without freeing -- 0x00443E14 skips the
     * free that 0x00443E0B does. Another original leak, reproduced. */
    return 0;
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
        while (1) {
            if (orig_feof(fh)) break;
            if (orig_fgets(line, (int32_t)sizeof line, fh) == 0) break;
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
    if (g_mpSession == 0) {
        /* The token count comes from the GLOBAL context, not the one passed
         * in. They are the same object for every caller in the image, but the
         * original reads the global and so does this. */
        const AM2_ScriptCtx *g =
            &g_scriptContext;
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

    if (kCommObject->verbose)
        am2_log("reading script %s: ", path);

    ok = ReadScript(path, &g_scriptContext);

    /* Re-read the flag: ReadScript can have changed it. */
    if (kCommObject->verbose) {
        if (ok)
            am2_log("worked!\n");
        else
            am2_log("FAILED!\n");
    }
}

void __cdecl LoadLevelScript(void)
{
    char path[0x40];

    ScriptSetDataDir(g_tilesetPath);

    /* Level 0 is the map's only script and drops the number. */
    if (g_levelIndex > 0)
        sprintf(path, "%s%d.txt", g_mapName_, g_levelIndex);
    else
        sprintf(path, "%s.txt", g_mapName_);

    ScriptResetTokens(&g_scriptContext);

    /* The five score variables, declared before anything can reference them.
     * `gamescorelimit` is seeded with the configured limit and its index is
     * thrown away -- scripts reach it by name and nothing writes it back --
     * while the four army scores keep theirs, because the runtime updates
     * those by index. */
    AddNameTableName(kImageStr(((uintptr_t)g_nameScoreLimit)), AM2_NAME_TYPE_INTEGER,
                     g_scoreLimit);
    g_svarGreenscore =
        AddNameTableName(kImageStr(((uintptr_t)g_nameGreenscore)),
                         AM2_NAME_TYPE_INTEGER, 0);
    g_svarTanscore =
        AddNameTableName(kImageStr(((uintptr_t)g_nameTanscore)),
                         AM2_NAME_TYPE_INTEGER, 0);
    g_svarBluescore =
        AddNameTableName(kImageStr(((uintptr_t)g_nameBluescore)),
                         AM2_NAME_TYPE_INTEGER, 0);
    g_svarGreyscore =
        AddNameTableName(kImageStr(((uintptr_t)g_nameGreyscore)),
                         AM2_NAME_TYPE_INTEGER, 0);

    ScriptReadWithLog(path);

    if (g_mpSession) {
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

        ScriptSetDataDir(kImageStr(((uintptr_t)g_rulesDirStr)));
        sprintf(path, "%s.txt", g_mpScriptName);
        ScriptReadWithLog(path);

        for (i = 0; i < AM2_PLAYERS_MAX; i++) {
            if (!kCommObject->slots[(size_t)i].wasHere)
                continue;
            /* A slot a real player still holds needs no AI script. The test
             * is the DirectPlay id, so this reads the opposite way round from
             * the name it first went in under -- see ADDR_COMM_SLOT_HAS_PLAYER.
             * Its answer, zero on the way through, is what `colour` holds for
             * an army above 3: the switch has no default, and the format then
             * prints from NULL. No slot carries one, and this is the
             * original's own omission. */
            if ((colour = (const char *)(uintptr_t)
                     kCommObject->CommSlotHasPlayer(i)) != 0)
                continue;

            switch (kCommObject->slots[(size_t)i].colour) {
            case 0: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_GREEN); break;
            case 1: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_TAN);   break;
            case 2: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_BLUE);  break;
            case 3: colour = (const char *)((void *)(uintptr_t)AM2_STR_COLOUR_GREY);  break;
            }

            sprintf(path, "%s_ai_%s.txt", g_mpScriptName, colour);
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
    g_currentObjScript = 0;
    g_objScriptCap = 0;
    g_objScripts = 0;
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
    int32_t save_pads = g_padCount;
    int32_t save_obj = g_currentObjScript;
    AM2_ObjScript *save_objarr = g_objScripts;
    int32_t save_objcap = g_objScriptCap;
    static int16_t save_padnum[256];
    for (int32_t i = 0; i < 256; i++) {
        save_padnum[i] = g_padNumbers[i].count;
        g_padNumbers[i].count = 0;
    }
    kScriptNames = 0;
    kScriptNameCount = 0;
    kScriptNameCap = 0;
    g_padCount = 0;
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
    g_padCount = save_pads;
    g_currentObjScript = save_obj;
    g_objScripts = save_objarr;
    g_objScriptCap = save_objcap;
    for (int32_t i = 0; i < 256; i++)
        g_padNumbers[i].count = save_padnum[i];

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
