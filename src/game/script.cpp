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

/* PreloadSprite is reconstructed, in win32/sprite.cpp with the rest of the
 * sprite record. It is declared here rather than by including that header
 * because script.cpp is on the flat side of the split and must name no Win32
 * or COM type -- and AM2_Sprite has an LPDIRECTDRAWSURFACE in it. An
 * incomplete type is enough: the statement discards the sprite. */
struct AM2_Sprite;
extern "C" AM2_Sprite *__cdecl PreloadSprite(int32_t set, int32_t index,
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
    for (int32_t i = 0; i < AM2_SCRIPT_TOKEN_COUNT; i++) {
        const char *a = word;
        const char *b = am2_script_tokens[i].name;

        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return am2_script_tokens[i].id;
    }
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

    AM2_ScriptTok *tok = &ctx->tokens[ctx->count];
    tok->kind = kind;
    tok->line = line;

    if (kind == AM2_TOKEN_STRING) {
        size_t len = strlen((const char *)value) + 1;
        tok->value = am2_malloc(len);
        memcpy(tok->value, value, len);
    } else if (kind >= AM2_TOKEN_CONTROL_CHAR && kind <= AM2_TOKEN_FLOAT) {
        tok->value = (void *)(uintptr_t)*(const uint32_t *)value;
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
    uint8_t c = (uint8_t)text[0];
    if (c != '-' && (c < '0' || c > '9'))
        return 0;

    /* The original recomputes strlen on every iteration; the value cannot
     * change, so it is hoisted.
     *
     * The bound is `i < len`, so every character but the first is examined.
     * Getting this wrong is easy and I did: `repne scasb` from ecx = -1 counts
     * the terminator as well, so `not ecx` gives len+1 and the `dec` that
     * follows gives len -- not len-1. With the off-by-one, "1." came out as
     * the integer 1 where the original gives the float 1.0, which is what the
     * shipped text caught. */
    int32_t len = (int32_t)strlen(text);
    int32_t dots = 0;

    for (int32_t i = 1; i < len; i++) {
        c = (uint8_t)text[i];
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

void __cdecl ScriptNextToken(const char *line, AM2_ScriptCtx *ctx,
                             int32_t lineno)
{
    int32_t at = 0;

    if (line[0] == 0)
        return;

    do {
        uint8_t c = (uint8_t)line[at];

        /* `\` and `/` both end the line. The scripts write `//`, but the
         * second slash is never examined. */
        if (c == '\\' || c == '/')
            return;

        if (IsBlank(c)) {
            while (IsBlank((uint8_t)line[at + 1]))
                at++;
            at++;
            continue;
        }

        if (c == '"') {
            int32_t n = 0;
            at++;
            while ((c = (uint8_t)line[at]) != 0 && c != '"') {
                kScriptWord[n++] = (char)c;
                at++;
            }
            if (line[at] == '"')
                at++;
            kScriptWord[n] = 0;
            ScriptAddToken(ctx, AM2_TOKEN_STRING, kScriptWord, lineno);
            continue;
        }

        if (c == '\n')
            return;

        if (IsScriptDelim(c)) {
            kScriptWord[0] = (char)c;
            kScriptWord[1] = 0;

            /* `<`, `>` and `=` may pair with a following `=` or `>`, which is
             * where `<=`, `>=` and `<>` come from. Nothing checks that the
             * first character is one of those three, so `,=` and `(>` would
             * pair too -- and then fail the lookup and vanish. */
            uint8_t next = (uint8_t)line[at + 1];
            if (next == '=' || next == '>') {
                kScriptWord[1] = (char)next;
                kScriptWord[2] = 0;
                at++;
            }

            int32_t id = ScriptLookupToken(kScriptWord);
            if (id >= 0 && id <= 0xD)
                ScriptAddToken(ctx, AM2_TOKEN_CONTROL_CHAR, &id, lineno);
            /* An id above 13 is dropped here rather than emitted, which is the
             * mirror of the word path below. No delimiter has one. */
            at++;
            continue;
        }

        /* A bare word: everything up to a blank, a newline, end of line or a
         * delimiter. */
        int32_t n = 0;
        while (!IsBlank((uint8_t)line[at])) {
            c = (uint8_t)line[at];
            if (c == '\n' || c == 0 || IsScriptDelim(c))
                break;

            if (n < 0x3F) {
                kScriptWord[n] = (char)c;
            } else if (n == 0x3F) {
                /* Faithfully wrong: the original passes kScriptWord[0x3F] --
                 * one sign-extended character -- where the format wants a
                 * string, so the message prints garbage for %s. Reproduced
                 * rather than corrected; nothing in the shipped scripts has a
                 * token this long, so it has almost certainly never printed. */
                am2_log("Line [%4d]: Token too long truncating to: %s\n",
                    lineno, (int32_t)(signed char)kScriptWord[0x3F]);
            }
            n++;
            at++;
        }
        kScriptWord[n] = 0;

        /* _strlwr, because the keyword table is lower case throughout. */
        for (char *p = kScriptWord; *p; p++)
            if (*p >= 'A' && *p <= 'Z')
                *p = (char)(*p + 0x20);

        int32_t id = ScriptLookupToken(kScriptWord);
        if (id >= 0) {
            ScriptAddToken(ctx,
                           id <= 0xD ? AM2_TOKEN_CONTROL_CHAR
                                     : AM2_TOKEN_RESERVED,
                           &id, lineno);
            continue;
        }

        int32_t ival = 0;
        float fval = 0.0f;
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
        default:
            break;      /* unreachable; the original drops the token */
        }
    } while (line[at] != 0);
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

    for (int32_t i = 0; i < n; i++) {
        const char *a = kScriptNames[i].name;
        const char *b = name;

        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return i;
    }
    return -1;
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

    for (int32_t i = 0; i < kScriptNameCount; i++) {
        AM2_ScriptName *e   = &kScriptNames[i];
        int32_t         len = (int32_t)strlen(e->name);

        /* Length, then the bytes WITHOUT a terminator, then the three fields
         * that follow the pointer. */
        orig_fwrite(&len, 4, 1, fp);
        orig_fwrite(e->name, (size_t)len, 1, fp);
        orig_fwrite(&e->type, 0xC, 1, fp);
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
    int32_t meta[3];
    int32_t count;

    if (!CheckSaveTag(fp, AM2_SAVETAG_SCRIPT,
                      (const char *)AM2_IMAGE(ADDR_STR_SCRIPT_CPP), 0x1F8))
        return 0;

    /* After the tag check, so a foreign save leaves the table alone. */
    FreeScriptNames();

    orig_fread(&count, 4, 1, fp);

    for (int32_t i = 0; i < count; i++) {
        int32_t         len;
        AM2_ScriptName *e;
        size_t          n;

        orig_fread(&len, 4, 1, fp);
        orig_fread(name, (size_t)len, 1, fp);
        orig_fread(meta, 0xC, 1, fp);
        name[len] = 0;

        /* Ten at a time, the same step AddNameTableName uses. */
        if (kScriptNameCount >= kScriptNameCap) {
            kScriptNameCap += 10;
            kScriptNames = (AM2_ScriptName *)am2_realloc(
                kScriptNames, (size_t)kScriptNameCap * sizeof(AM2_ScriptName));
        }

        e    = &kScriptNames[kScriptNameCount];
        n    = strlen(name) + 1;
        e->name = (char *)am2_malloc(n);
        memcpy(e->name, name, n);
        e->type  = meta[0];
        e->value = meta[1];
        e->refs  = meta[2];
        kScriptNameCount++;
    }
    return 1;
}

int32_t __cdecl GetVarValue(int32_t index, int32_t *out)
{
    AM2_ScriptName *e;

    if (index <= 0) {
        *out = 0;
        return 0;
    }

    e = &kScriptNames[index];
    if (e->type != AM2_NAME_TYPE_INTEGER) {
        am2_log("GetVarValue: name %s is not a variable!\n", e->name);
        return 0;
    }

    *out = e->value;
    return 1;
}

/* No index check at all, unlike the getter. */
int32_t __cdecl SetVarValue(int32_t index, int32_t value)
{
    AM2_ScriptName *e = &kScriptNames[index];

    if (e->type != AM2_NAME_TYPE_INTEGER) {
        am2_log("SetVarValue: name %s is not a variable!\n", e->name);
        return 0;
    }

    e->value = value;
    return 1;
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

#define kNextUid         (*(int32_t *)AM2_IMAGE(ADDR_NEXT_UID))

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

    AM2_ScriptName *e = &kScriptNames[kScriptNameCount];

    /* strlen+1, because the `repne scasb` that sizes the block counts the
     * terminator -- the same arithmetic that caught ParseNumber out. */
    size_t len = strlen(name) + 1;
    e->name = (char *)am2_malloc(len);
    memcpy(e->name, name, len);
    e->refs = 1;
    e->type = type;

    switch (type) {
    case 0:
        e->value = AllocUid();
        break;
    case 1:
    case 2:
    case 3:
        e->value = uid;
        break;
    default:
        am2_log("AddNameTableName: invalid type; %s has type %d and uid %d\n",
                name, type, uid);
        /* Stored anyway, and the count still advances. */
        e->value = uid;
        break;
    }

    return kScriptNameCount++;
}

char *__cdecl ScriptTokenText(const AM2_ScriptTok *tok, char *out)
{
    const char *src;

    switch (tok->kind) {
    case AM2_TOKEN_UNKNOWN:
        /* A `char *` stored in the image, pointing at the string just past the
         * keyword table -- so BOTH the slot and the pointer it holds are image
         * addresses and both need the slide. Sliding only the slot reads the
         * right pointer and then follows it into whatever the test process has
         * at 0x0048825C, which came back as leftover script text rather than
         * as a fault. Any pointer stored IN the image needs AM2_IMAGE twice. */
        src = kUnknownWord;
        break;

    case AM2_TOKEN_CONTROL_CHAR:
    case AM2_TOKEN_RESERVED:
        src = ScriptTokenName((int32_t)(uintptr_t)tok->value);
        break;

    case AM2_TOKEN_INTEGER:
        sprintf(out, "%d", (int32_t)(uintptr_t)tok->value);
        return out;

    case AM2_TOKEN_FLOAT: {
        /* The value field holds the float's bits, not a pointer to them. */
        float f;
        memcpy(&f, &tok->value, sizeof f);
        sprintf(out, "%6.2f", (double)f);
        return out;
    }

    case AM2_TOKEN_STRING:
        src = (const char *)tok->value;
        break;

    case AM2_TOKEN_NAMEREF:
        src = kScriptNames[(uintptr_t)tok->value].name;
        break;

    default:
        /* Kind 6, and anything above 7, leave `out` untouched. */
        return out;
    }

    strcpy(out, src);
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
AM2_ScriptTok *ScriptExpect(AM2_ScriptCtx *ctx, int32_t *at,
                                   int32_t kind)
{
    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    AM2_ScriptTok *tok = &ctx->tokens[*at];
    if (tok->kind != kind) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(kind));
        return 0;
    }
    return tok;
}

int32_t __cdecl ScriptPreloadSprite(AM2_ScriptCtx *ctx, int32_t *at)
{
    int32_t arg[3];

    for (int32_t i = 0; i < 3; i++) {
        AM2_ScriptTok *tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
        if (!tok)
            return 0;
        arg[i] = (int32_t)(uintptr_t)tok->value;
    }

    /* The fourth is optional, and the index advances past the third whether or
     * not it is there -- so a missing one leaves *at on whatever follows and
     * ReadScript dispatches that normally. */
    int32_t flags = 0x1000;
    (*at)++;
    if (*at < ctx->count &&
        ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
        flags = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
    }

    PreloadSprite(arg[0], arg[1], arg[2], flags, 1);
    return 1;
}

int32_t __cdecl ScriptVariable(AM2_ScriptCtx *ctx, int32_t *at)
{
    AM2_ScriptTok *tok = ScriptExpect(ctx, at, AM2_TOKEN_STRING);
    if (!tok)
        return 0;

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

    tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
    if (!tok)
        return 0;

    kScriptNames[slot].value = (int32_t)(uintptr_t)tok->value;
    (*at)++;
    return 1;
}


static void ScriptAddEvent(AM2_ScriptCond *c, int32_t a, int32_t b, int32_t d)
{
    int32_t n = c->nevents++;
    c->events = (AM2_ScriptEvent *)am2_realloc(
        c->events, (size_t)c->nevents * sizeof(AM2_ScriptEvent));
    c->events[n].a = a;
    c->events[n].b = b;
    c->events[n].c = d;
    c->events[n].d = 0;
}

int32_t __cdecl ScriptResolveName(AM2_ScriptCtx *ctx, int32_t *at,
                                  int32_t *out, int32_t quiet)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_RESERVED) {
        uint32_t g = 0;
        switch ((int32_t)(uintptr_t)tok->value) {
        case 15: g = ADDR_SVAR_ID15;  break;   /* no keyword maps here */
        case AM2_TOK_GREEN:         g = ADDR_SVAR_GREEN; break;
        case AM2_TOK_TAN:           g = ADDR_SVAR_TAN;   break;
        case AM2_TOK_BLUE:          g = ADDR_SVAR_BLUE;  break;
        case AM2_TOK_GREY:          g = ADDR_SVAR_GREY;  break;
        case AM2_TOK_ME:            g = ADDR_SVAR_ME;    break;
        default:
            if (!quiet)
                am2_log("Line [%4d]:  Unexpected use of reserved word\n",
                        ctx->tokens[*at].line,
                        ScriptTokenText(tok, kScriptWord));
            return 0;
        }
        *out = *(const int32_t *)AM2_IMAGE(g);
        (*at)++;
        return 1;
    }

    if (tok->kind != AM2_TOKEN_STRING) {
        if (quiet)
            return 0;
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_STRING));
        return 0;
    }

    int32_t idx = ScriptFindName((const char *)tok->value);
    *out = idx;

    if (idx >= 0) {
        int32_t type = kScriptNames[idx].type;
        if (type != AM2_NAME_TYPE_REF && type != AM2_NAME_TYPE_INTEGER) {
            am2_log("Line [%4d]:  Name '%s' already used for another type.\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord));
            return 0;
        }
    } else {
        *out = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_REF, 0);
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
        if (k != AM2_TOKEN_STRING && k != AM2_TOKEN_RESERVED)
            return 1;

        int32_t a = 0, b = 0, c = 0;
        if (!ScriptParseEvent(ctx, at, &a, &b, &c))
            return 0;                   /* silent -- the callee has spoken */
        ScriptAddEvent(cond, a, b, c);

        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind != AM2_TOKEN_RESERVED)
            continue;
        int32_t id = (int32_t)(uintptr_t)t->value;
        if (id == AM2_TOK_THEN || id == AM2_TOK_BUTNOT || id == AM2_TOK_TESTVAR)   /* then, butnot, testvar */
            return 1;
    }
}

/* Both descriptor parsers start with "is it just a name?" and resolve it the
 * same way -- find it, declare it with type 2 if new. */
static int32_t ScriptNamedTarget(AM2_ScriptCtx *ctx, int32_t *at,
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
static int ScriptStep(AM2_ScriptCtx *ctx, int32_t *at)
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

    tok = &ctx->tokens[*at];
    int32_t army = 0;
    if (tok->kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)tok->value) {
        case AM2_TOK_GREEN:         army = AM2_HIT_GREEN; break;
        case AM2_TOK_TAN:           army = AM2_HIT_TAN; break;
        case AM2_TOK_BLUE:          army = AM2_HIT_BLUE; break;
        case AM2_TOK_GREY:          army = AM2_HIT_GREY; break;
        default: break;
        }
    }
    if (army) {
        if (!ScriptStep(ctx, at))
            return 0;
        *mask |= army;
    } else {
        *mask = (int32_t)AM2_HIT_ALL_ARMIES;
    }

    tok = &ctx->tokens[*at];
    if (tok->kind == AM2_TOKEN_RESERVED) {
        int32_t type = 0;
        switch ((int32_t)(uintptr_t)tok->value) {
        case AM2_TOK_ITEM:          type = AM2_HIT_ITEM; break;
        case AM2_TOK_SARGE:         type = AM2_HIT_SARGE; break;
        case AM2_TOK_TROOPER:       type = AM2_HIT_TROOPER; break;
        case AM2_TOK_VEHICLE:       type = AM2_HIT_VEHICLE; break;
        default: break;
        }
        if (type) {
            if (!ScriptStep(ctx, at))
                return 0;
            *mask |= type;
        }
    }

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

    tok = &ctx->tokens[*at];
    int32_t which = -1;
    if (tok->kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)tok->value;
        if (id >= 16 && id <= 19)
            which = id - 16;                    /* green tan blue grey */
    }
    if (which < 0) {
        am2_log("Unknown item descriptor for ORDER or SETAIMODE event\n");
        return 0;
    }
    if (!ScriptStep(ctx, at))
        return 0;
    *army = which;
    *form = AM2_ORDER_ARMY;

    tok = &ctx->tokens[*at];
    if (tok->kind == AM2_TOKEN_RESERVED &&
        (int32_t)(uintptr_t)tok->value == AM2_TOK_GROUP) {        /* group */
        if (!ScriptStep(ctx, at))
            return 0;
        *form = AM2_ORDER_GROUP;
        *val = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ScriptStep(ctx, at))
            return 0;
    } else {
        *val = -1;
    }
    return 1;
}

#define kPads       ((AM2_Pad *)AM2_IMAGE(ADDR_PADS))
#define kPadCount   (*(int32_t *)AM2_IMAGE(ADDR_PAD_COUNT))
#define kPadNumbers ((AM2_PadNumber *)AM2_IMAGE(ADDR_PAD_NUMBERS))

/* One coordinate of a location: an integer, or `refvar <name>`. */
static int32_t ScriptCoord(AM2_ScriptCtx *ctx, int32_t *at,
                           int16_t *lit, int32_t *var)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_INTEGER) {
        *lit = (int16_t)(uintptr_t)tok->value;
        return 1;
    }

    if (tok->kind != AM2_TOKEN_RESERVED ||
        (int32_t)(uintptr_t)tok->value != AM2_TOK_REFVAR) {        /* refvar */
        am2_log("Line [%4d]:  '%s' found, but expected token of type Integer "
                "or keyword 'RefVar'\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord));
        return 0;
    }

    if (!ScriptStep(ctx, at))
        return 0;

    int32_t idx = ScriptFindName((const char *)ctx->tokens[*at].value);
    if (idx == -1 || kScriptNames[idx].type != AM2_NAME_TYPE_INTEGER) {
        am2_log("Line [%4d]:  expected variable name but variable %s not "
                "declared\n",
                ctx->tokens[*at].line, ctx->tokens[*at].value);
        return 0;
    }
    *var = idx;
    return 1;
}

int32_t __cdecl ScriptLocation(AM2_ScriptCtx *ctx, int32_t *at,
                               AM2_ScriptAction *act, int32_t quiet)
{
    act->relative = 0;

    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_CONTROL_CHAR) {
        int32_t id = (int32_t)(uintptr_t)tok->value;
        if (id != AM2_TOK_LPAREN && id != AM2_TOK_PLUS)        /* '(' or '+' */
            return 0;                   /* silently, with no message */

        if (id == AM2_TOK_PLUS) {                 /* a leading '+' means relative */
            act->relative = 1;
            if (!ScriptStep(ctx, at))
                return 0;
        }
        if (!ScriptStep(ctx, at))       /* past the '(' */
            return 0;

        if (!ScriptCoord(ctx, at, &act->u.pos.x, &act->xvar))
            return 0;
        if (!ScriptStep(ctx, at))
            return 0;

        tok = &ctx->tokens[*at];
        if (tok->kind != AM2_TOKEN_CONTROL_CHAR ||
            (int32_t)(uintptr_t)tok->value != AM2_TOK_COMMA) {      /* ',' */
            am2_log("Line [%4d]:  Comma expected in coordinates\n",
                    ctx->tokens[*at].line);
            return 0;
        }
        if (!ScriptStep(ctx, at))
            return 0;

        if (!ScriptCoord(ctx, at, &act->u.pos.y, &act->yvar))
            return 0;
        if (!ScriptStep(ctx, at))
            return 0;

        tok = &ctx->tokens[*at];
        if (tok->kind == AM2_TOKEN_CONTROL_CHAR &&
            (int32_t)(uintptr_t)tok->value == AM2_TOK_RPAREN) {      /* ')' */
            (*at)++;
            return 1;
        }
        am2_log("Line [%4d]:  Close parens expected in coordinates\n",
                ctx->tokens[*at].line);
        return 0;
    }

    if (tok->kind == AM2_TOKEN_STRING) {
        int32_t idx = ScriptFindName((const char *)tok->value);
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
}

/* The five `<verb> <target> [by <target>]` events, which differ only in the
 * kind they record and the two messages they print. */
static const struct {
    int32_t     id;
    int32_t     kind;
    const char *missing;
    const char *missing_by;
} kScriptEvents[5] = {
    { AM2_TOK_KILLED,   AM2_EVT_KILLED, "Line [%4d]:  Expected item name after KILLED\n",
             "Line [%4d]:  Expected item name after KILLED ... BY\n" },
    { AM2_TOK_HIT,      AM2_EVT_HIT, "Line [%4d]:  Expected item name after HIT\n",
             "Line [%4d]:  Expected item name after HIT ... BY\n" },
    { AM2_TOK_HEALED,   AM2_EVT_HEALED, "Line [%4d]:  Expected item name after HEALED\n",
             "Line [%4d]:  Expected item name after HEALED ... BY\n" },
    { AM2_TOK_PICKEDUP, AM2_EVT_PICKEDUP, "Line [%4d]:  Expected item name after PICKEDUP\n",
             "Line [%4d]:  Expected item name after PICKEDUP ... BY\n" },
    { AM2_TOK_DROPPED,  AM2_EVT_DROPPED, "Line [%4d]:  Expected item name after DROPPED\n",
             "Line [%4d]:  Expected item name after DROPPED ... BY\n" },
};

/* `padon <name>` and `padoff <name>`, which differ only in the kind and the
 * two messages. */
static int32_t ScriptPadEvent(AM2_ScriptCtx *ctx, int32_t *at, int32_t *val,
                              const char *no_name, const char *not_a_pad)
{
    if (!ScriptStep(ctx, at))
        return 0;

    AM2_ScriptTok *tok = &ctx->tokens[*at];
    if (tok->kind != AM2_TOKEN_STRING) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_STRING));
        return 0;
    }

    int32_t idx = ScriptFindName((const char *)tok->value);
    if (idx < 0) {
        am2_log(no_name, ctx->tokens[*at].line);
        return 0;
    }
    /* Type 1 is what `pad` declares. */
    if (kScriptNames[idx].type != AM2_NAME_TYPE_PAD) {
        am2_log(not_a_pad, ctx->tokens[*at].line);
        return 0;
    }

    *val = kScriptNames[idx].value;
    (*at)++;
    return 1;
}

int32_t __cdecl ScriptParseEvent(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *val, int32_t *val2)
{
    *val2 = 0;

    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)tok->value;

        if (id == AM2_TOK_PADON) {                                 /* padon */
            *kind = AM2_EVT_PADON;
            return ScriptPadEvent(ctx, at, val,
                                  "Line [%4d]:  Name required after PADON\n",
                                  "Line [%4d]:  PADON name was not a pad\n");
        }
        if (id == AM2_TOK_PADOFF) {                                 /* padoff */
            *kind = AM2_EVT_PADOFF;
            return ScriptPadEvent(ctx, at, val,
                                  "Line [%4d]:  Name required after PADOFF\n",
                                  "Line [%4d]:  PADOFF name was not a pad\n");
        }

        for (uint32_t i = 0; i < 5; i++) {
            if (kScriptEvents[i].id != id)
                continue;

            *kind = kScriptEvents[i].kind;
            if (!ScriptStep(ctx, at))
                return 0;

            if (!ScriptHitTarget(ctx, at, val)) {
                am2_log(kScriptEvents[i].missing, ctx->tokens[*at].line);
                return 0;
            }

            /* `by <target>` is optional; without it *val2 keeps the zero
             * written on entry. */
            tok = &ctx->tokens[*at];
            if (tok->kind != AM2_TOKEN_RESERVED ||
                (int32_t)(uintptr_t)tok->value != AM2_TOK_BY)
                return 1;

            if (!ScriptStep(ctx, at))
                return 0;
            if (!ScriptHitTarget(ctx, at, val2)) {
                am2_log(kScriptEvents[i].missing_by, ctx->tokens[*at].line);
                return 0;
            }
            return 1;
        }

        am2_log("Line [%4d]:  Unexpected reserved word in if statement.\n",
                ctx->tokens[*at].line);
        return 0;
    }

    /* A bare name: the event is that object, whatever happens to it. */
    *kind = AM2_EVT_CONTROL;
    if (tok->kind != AM2_TOKEN_STRING) {
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

int32_t __cdecl ScriptParseValue(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *a, int32_t *b)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)tok->value;
        /* `form` doubles as the flag for "this id IS a keyword form", so
         * zero means none and is tested as such below -- it is not
         * AM2_VAL_LITERAL, which the fall-through path sets instead. */
        int32_t form = 0, names = 0, armies = 0;

        switch (id) {
        case AM2_TOK_GETDMGLVL:     form = AM2_VAL_GETDMGLVL;    names  = 1; break;
        case AM2_TOK_GETHEALTH:     form = AM2_VAL_GETHEALTH;    names  = 1; break;
        case AM2_TOK_GETDISGUISE:   form = AM2_VAL_GETDISGUISE;  names  = 1; break;
        case AM2_TOK_HASITEM:       form = AM2_VAL_HASITEM;      names  = 2; break;
        case AM2_TOK_ISCOLORINGAME: form = AM2_VAL_ISCOLORINGAME; armies = 1; break;
        case AM2_TOK_ISALLY:        form = AM2_VAL_ISALLY;       armies = 2; break;
        case AM2_TOK_TEAMSCORE:     form = AM2_VAL_TEAMSCORE;    armies = 1; break;
        default: form = 0; break;
        }

        if (form) {
            if (++(*at) >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
            /* The failures below are silent: the callee has already said
             * whatever it is going to say. */
            if (names >= 1 && !ScriptResolveName(ctx, at, a, 0))
                return 0;
            if (names >= 2 && !ScriptResolveName(ctx, at, b, 0))
                return 0;
            if (armies >= 1)
                *a = ScriptArmyColour(ctx, at);
            if (armies >= 2)
                *b = ScriptArmyColour(ctx, at);
            *kind = form;
            return 1;
        }
    }

    /* An integer literal or a variable. */
    int32_t isliteral = 0;
    if (!ScriptIntOrVar(ctx, at, a, &isliteral)) {
        am2_log("Line [%4d]:  Unrecognized operand in testvar clause.\n",
                ctx->tokens[*at].line);
        return 0;
    }
    *kind = isliteral ? AM2_VAL_LITERAL : AM2_VAL_VARIABLE;
    return 1;
}

int32_t __cdecl ScriptNameUid(const char *name)
{
    int32_t n = kScriptNameCount;

    for (int32_t i = 0; i < n; i++) {
        const char *a = kScriptNames[i].name;
        const char *b = name;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a != *b)
            continue;

        if (kScriptNames[i].type != AM2_NAME_TYPE_OBJECT) {
            am2_log("Duplicate name '%s' used for different types\n",
                    kScriptNames[i].name);
            return 0;
        }
        kScriptNames[i].refs++;
        return kScriptNames[i].value;
    }

    /* Declaring it fresh does NOT bump the count -- it is left at the 1 that
     * AddNameTableName writes. */
    return kScriptNames[AddNameTableName(name, AM2_NAME_TYPE_OBJECT, 0)].value;
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

    int32_t army;
    switch ((int32_t)(uintptr_t)tok->value) {
    case AM2_TOK_GREEN:             army = 0; break;
    case AM2_TOK_TAN:               army = 1; break;
    case AM2_TOK_BLUE:              army = 2; break;
    case AM2_TOK_GREY:              army = 3; break;
    default:
        am2_log("Line [%4d]:  Expected Army Color instead of '%s'\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord));
        return -1;
    }

    int32_t rc = CommSlotForArmy(*(void **)AM2_IMAGE(ADDR_ARMY_TABLE), army);

    (*at)++;
    return rc;
}

/* Set from AM2_DUMP_ACTIONS and AM2_PARSE_ALL. */
int32_t am2_dump_actions = 0;
static int32_t am2_parse_all = 0;
static int32_t am2_probe_noaction = 0;
static void ScriptParseAll(void);

/* ---------------------------------------------------------- actions ---- */

/* Advance one token. Zero, with the message, at the end of the script. */
static int32_t ActStep(AM2_ScriptCtx *ctx, int32_t *at)
{
    if (++(*at) < ctx->count)
        return 1;
    am2_log("Unexpected end of script.\n");
    return 0;
}

static void ActTypeErr(AM2_ScriptCtx *ctx, int32_t *at, int32_t kind)
{
    am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
            ctx->tokens[*at].line,
            ScriptTokenText(&ctx->tokens[*at], kScriptWord),
            kKindName(kind));
}

/* `createtrooper`, `createvehicle`: a location and an army, and the army's
 * own result decides the return. */
static int32_t ActPlaceAndArmy(AM2_ScriptCtx *ctx, int32_t *at,
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
static int32_t ActWeapon(int32_t id)
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

static int32_t ActChassis(int32_t id)
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

static int32_t ActPickup(int32_t id)
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
static int32_t ActDamageKind(int32_t id)
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
static int32_t ActAiMode(int32_t id)
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
static int32_t ActVarName(AM2_ScriptCtx *ctx, int32_t *at, AM2_ScriptAction *act,
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
        act->subject = *(const int32_t *)AM2_IMAGE(ADDR_SVAR_ID15);
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

    int32_t rc = ScriptParseAction(ctx, at, act);

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


/* The trigger keyword each Reserved id contributes, for ids 16..42. Zero ends
 * the run -- the loop stops at the first word it does not recognise and leaves
 * it for what follows.
 *
 * Two entries look like slips in the original and are reproduced as they are.
 * `army0`..`army3` duplicate `green`/`tan`/`blue`/`grey`, which is reasonable.
 * `halftrack` shares 0x0200 with `grey`, which is not -- but nothing here can
 * tell whether that was intended, so it stands. */
static const uint32_t kPadTriggerBit[27] = {
    /* 16 green */ AM2_PADTRIG_GREEN,   /* 17 tan  */ AM2_PADTRIG_TAN,
    /* 18 blue  */ AM2_PADTRIG_BLUE,    /* 19 grey */ AM2_PADTRIG_GREY,
    /* 20 me    */ 0,                   /* 21 army0 */ AM2_PADTRIG_GREEN,
    /* 22 army1 */ AM2_PADTRIG_TAN,     /* 23 army2 */ AM2_PADTRIG_BLUE,
    /* 24 army3 */ AM2_PADTRIG_GREY,    /* 25 preloadsprite */ 0,
    /* 26 pad   */ 0,                   /* 27 padon */ 0,
    /* 28 padoff */ 0,                  /* 29 everything */ AM2_PADTRIG_EVERYTHING,
    /* 30 item  */ 0,                   /* 31 (no keyword) */ 0,
    /* 32 sarge */ AM2_PADTRIG_SARGE,   /* 33 unit    */ AM2_PADTRIG_UNIT,
    /* 34 trooper */ AM2_PADTRIG_TROOPER, /* 35 tank  */ AM2_PADTRIG_TANK,
    /* 36 vehicle */ AM2_PADTRIG_VEHICLE, /* 37 jeep  */ 0,
    /* 38 halftrack */ AM2_PADTRIG_GREY, /* 39 convoy */ AM2_PADTRIG_CONVOY,
    /* 40 boat  */ AM2_PADTRIG_BOAT,    /* 41 groundvehicle */ AM2_PADTRIG_GROUNDVEH,
    /* 42 npc   */ AM2_PADTRIG_NPC,
};

/* Where on the map this pad number is, in sixteenths of a cell. Only computed
 * the first time a number is defined -- a second `pad` with the same number
 * joins the existing entry and leaves the centroid alone. */
static void ScriptPadCentroid(AM2_PadNumber *pn, int32_t number)
{
    const uint8_t *layer;
    int32_t mask = 0;

    if (number >= 8) {
        layer = *(const uint8_t **)AM2_IMAGE(ADDR_MAP_PAD_LAYER);
    } else {
        layer = *(const uint8_t **)AM2_IMAGE(ADDR_MAP_PADBIT_LAYER);
        mask = kPadBit(number);
    }
    if (!layer)
        return;

    /* One dword store covering both words -- the default before the scan. */
    *(int32_t *)&pn->cx = *(const int32_t *)AM2_IMAGE(ADDR_ZERO_POINT);

    /* The original divides by 0x00514DE0 to recover x, which is the map's
     * HEIGHT in tiles -- TileOfPoint, PointOfTile and ADDR_MAP_ROW_SHIFT all
     * make 0x00514DDC the width. Reproduced exactly: on a square map, and
     * every shipped one seen so far is, the two are the same number and the
     * scan is right by accident. */
    int32_t w = *(const int32_t *)AM2_IMAGE(ADDR_MAP_TILES_H);
    int32_t total = w * *(const int32_t *)AM2_IMAGE(ADDR_MAP_TILES_W);
    if (total <= 0)
        return;

    int32_t sx = 0, sy = 0, n = 0;
    for (int32_t i = 0; i < total; i++) {
        if (number >= 8) {
            if ((int32_t)layer[i] != number)
                continue;
        } else if ((layer[i] | mask) == 0) {
            /* Faithfully `or`, which is what the image holds -- 0B C2 at
             * 0x00444365. The mask is 1 << number and so never zero, which
             * makes this test always false: every cell on the map counts,
             * and the centroid for a low-numbered pad is the map's centre.
             * `and` is plainly what was meant. Pad numbers 5 and 6 do ship,
             * so this is live rather than theoretical. */
            continue;
        }
        sx += i % w;
        sy += i / w;
        n++;
    }
    if (n <= 0)
        return;

    /* Sixteenths, rounded. */
    pn->cx = (int16_t)((sx * 16 + 8) / n);
    pn->cy = (int16_t)((sy * 16 + 8) / n);
}

int32_t __cdecl ScriptPad(AM2_ScriptCtx *ctx, int32_t *at)
{
    AM2_ScriptTok *tok = ScriptExpect(ctx, at, AM2_TOKEN_STRING);
    if (!tok)
        return 0;

    if (ScriptFindName((const char *)tok->value) >= 0) {
        am2_log("Line [%4d]:  Duplicate pad name.\n", ctx->tokens[*at].line);
        return 0;
    }

    /* Type 1, where `variable` uses 3. */
    int32_t name = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_PAD, 0);
    am2_free(ctx->tokens[*at].value);
    ctx->tokens[*at].kind = AM2_TOKEN_NAMEREF;
    ctx->tokens[*at].value = (void *)(uintptr_t)name;

    tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
    if (!tok)
        return 0;

    int32_t number = (int32_t)(uintptr_t)tok->value;
    if (number < 0 || number >= 0x100) {
        am2_log("Line [%4d]:  Illegal Pad Number\n", ctx->tokens[*at].line);
        return 0;
    }

    AM2_PadNumber *pn = &kPadNumbers[number];
    if (pn->count == 0)
        ScriptPadCentroid(pn, number);

    /* The pad being built. It is not counted until the very end, so every
     * write below addresses the same record. */
    AM2_Pad *pad = &kPads[kPadCount];
    pad->id = kPadCount;
    pad->name = name;
    pad->compared = 0;
    pad->number = number;

    /* Note what is NOT cleared: trigger, specific, compare, threshold and both
     * delays keep whatever the previous use of this slot left. The array is
     * static and the count resets between maps, so a pad can inherit a stale
     * trigger set. Reproduced, because the `or` below depends on it. */

    pn->pads[pn->count] = (int16_t)kPadCount;
    pn->count++;

    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    /* Trigger words, until one that is not a trigger word. */
    for (;;) {
        tok = &ctx->tokens[*at];
        if (tok->kind != AM2_TOKEN_RESERVED)
            break;

        int32_t id = (int32_t)(uintptr_t)tok->value;
        if (id < 16 || id > 42 || kPadTriggerBit[id - 16] == 0)
            break;

        pad->trigger |= (int32_t)kPadTriggerBit[id - 16];
        if (++(*at) >= ctx->count)
            break;
    }

    /* A String here names one specific item instead, which the flags exclude. */
    if (*at < ctx->count && ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
        if (pad->trigger != 0) {
            am2_log("Line [%4d]:  Pad can't have both specific item and "
                    "generic triggers\n", ctx->tokens[*at].line);
            return 0;
        }
        int32_t item = 0;
        if (!ScriptResolveName(ctx, at, &item, 0))
            return 0;
        pad->specific = 1;
        pad->trigger = item;
    }

    /* An optional comparison, then the count it compares against. */
    if (*at < ctx->count &&
        ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR) {
        int32_t op = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (op == AM2_TOK_LT)                    /* '<' */
            pad->compare = AM2_PADCMP_LT;
        else if (op == AM2_TOK_EQ)               /* '=' */
            pad->compare = AM2_PADCMP_EQ;
        else if (op == AM2_TOK_GT)               /* '>' */
            pad->compare = AM2_PADCMP_GT;
        else {
            am2_log("Line [%4d]:  Unexpected symbol in pad definition "
                    "should be '<=>'\n", ctx->tokens[*at].line);
            return 0;
        }

        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        pad->compared = 1;

        tok = &ctx->tokens[*at];
        if (tok->kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord),
                    kKindName(AM2_TOKEN_INTEGER));
            return 0;
        }
        pad->threshold = (int32_t)(uintptr_t)tok->value;
        (*at)++;

        /* `delay <int> <int>`, optional, and only after a comparison. */
        if (*at < ctx->count &&
            ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_DELAY) {

            tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
            if (!tok)
                return 0;
            pad->delay0 = (int32_t)(uintptr_t)tok->value;

            tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
            if (!tok)
                return 0;
            pad->delay1 = (int32_t)(uintptr_t)tok->value;
            (*at)++;
        }

        /* Only reached with a comparison. A pad that ends after its trigger
         * words -- or whose next token is not a control character -- jumps
         * straight to binding the name, so it is never finalised. That is the
         * original's control flow, not an omission: both of those paths land
         * on 0x00444842, past the call.
         *
         * The second argument is an OBJECT and the original passes a literal
         * zero here, which the callee reads as "no object" -- so the notify
         * this pad eventually sends carries a zero uid. */
        PadFinalise(pad, (void *)0);
    }

    /* The name resolves to the pad, and only now does the pad count move. */
    kScriptNames[pad->name].value = kPadCount;
    kPadCount++;
    return 1;
}

/* ---------------------------------------------------------------- if ---- */

#define kScriptConds (*(AM2_ScriptCond **)AM2_IMAGE(ADDR_SCRIPT_CONDITIONS))

/* 0x00442F10. Does `want` appear before `stop`, scanning from `from`?
 *
 * The two are not tested symmetrically and the shape is a software-pipelined
 * loop rather than a tidy scan: `want` is tested on token i at iteration i,
 * while `stop` is tested on token i+1 at iteration i -- so for every token but
 * the first, `stop` is examined first. And the `stop` read happens before the
 * bounds check, so on the last iteration it reads one token past the end.
 * Both are reproduced; neither is safe to tidy without changing which form an
 * ambiguous statement parses as. */
int32_t __cdecl ScriptScanFor(const AM2_ScriptCtx *ctx, int32_t from,
                              int32_t want, int32_t stop)
{
    if (from >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    for (int32_t i = from;;) {
        const AM2_ScriptTok *t = &ctx->tokens[i];
        if (t->kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)t->value == want)
            return 1;

        const AM2_ScriptTok *n = &ctx->tokens[i + 1];
        i++;
        if (n->kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)n->value == stop)
            return 0;
        if (i >= ctx->count)
            break;
    }

    am2_log("Unexpected end of script.\n");
    return 0;
}

/* True when the token at *at is Reserved with this id. */
static int ScriptAtWord(const AM2_ScriptCtx *ctx, int32_t at, int32_t id)
{
    const AM2_ScriptTok *t = &ctx->tokens[at];
    return t->kind == AM2_TOKEN_RESERVED &&
           (int32_t)(uintptr_t)t->value == id;
}

int32_t __cdecl ScriptIf(AM2_ScriptCtx *ctx, int32_t *at)
{
    int32_t a = 0, b = 0, c3 = 0;
    AM2_ScriptAction act;
    int32_t objname = 0;

    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    AM2_ScriptCond *cond = (AM2_ScriptCond *)am2_malloc(sizeof *cond);
    memset(cond, 0, sizeof *cond);

    if (ScriptAtWord(ctx, *at, AM2_TOK_TIMEABSOLUTE)) {           /* timeabsolute */
        cond->kind = AM2_IF_TIMEABSOLUTE;
        if (++(*at) >= ctx->count)
            goto end_of_script;

        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  Exptected number after TIMEABSOLUTE\n",
                    t->line);
            goto fail;
        }
        cond->number = (int32_t)(uintptr_t)t->value;
        if (cond->number < 1) {
            am2_log("Line [%4d]:  TIMEABSOLUTE time must be positive\n",
                    ctx->tokens[*at].line);
            goto fail;
        }
        if (++(*at) >= ctx->count)
            goto end_of_script;

    } else if (ScriptScanFor(ctx, *at, AM2_TOK_AFTER, AM2_TOK_THEN)) {
        /* An `<event> after <event>` chain. */
        cond->kind = AM2_IF_AFTER;
        for (;;) {
            int32_t k = ctx->tokens[*at].kind;
            if (k != AM2_TOKEN_STRING && k != AM2_TOKEN_RESERVED)
                break;

            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);

            if (!ScriptAtWord(ctx, *at, AM2_TOK_AFTER)) {
                am2_log("Line [%4d]:  Missing 'after' in if-statement.\n",
                        ctx->tokens[*at].line);
                goto fail;
            }
            if (++(*at) >= ctx->count)
                goto end_of_script_late;

            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);

            if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
                continue;
            if (ScriptAtWord(ctx, *at, AM2_TOK_THEN) || ScriptAtWord(ctx, *at, AM2_TOK_TESTVAR))
                break;                          /* then, or testvar */
            if (!ScriptAtWord(ctx, *at, AM2_TOK_AND))    /* and */
                continue;
            if (++(*at) >= ctx->count)
                goto end_of_script_late;
        }

    } else if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        int wants_of = 0;

        switch (id) {
        case AM2_TOK_ALLOF:         cond->kind = AM2_IF_ALLOF; break;
        case AM2_TOK_INORDER:       cond->kind = AM2_IF_INORDER; break;
        case AM2_TOK_COUNT:         cond->kind = AM2_IF_COUNT; wants_of = 1; break;
        case AM2_TOK_REPEAT:        cond->kind = AM2_IF_REPEAT; wants_of = 1; break;
        default: cond->kind = AM2_IF_PLAIN; break;         /* butnot, and anything else */
        }

        if (id >= 46 && id <= 50) {
            if (++(*at) >= ctx->count)
                goto end_of_script;

            if (wants_of) {
                AM2_ScriptTok *t = &ctx->tokens[*at];
                if (t->kind != AM2_TOKEN_INTEGER)
                    goto want_integer;
                cond->number = (int32_t)(uintptr_t)t->value;
                if (++(*at) >= ctx->count)
                    goto end_of_script;

                if (!ScriptAtWord(ctx, *at, AM2_TOK_OF)) {      /* of */
                    am2_log("Line [%4d]:  Missing 'of' in if-repeat "
                            "statement.\n", ctx->tokens[*at].line);
                    goto fail;
                }
                if (++(*at) >= ctx->count)
                    goto end_of_script_late;
            }
        }

        if (!ScriptParseEvents(ctx, at, cond))
            goto fail;

        if (ScriptAtWord(ctx, *at, AM2_TOK_BUTNOT)) {       /* butnot */
            cond->kind = AM2_IF_BUTNOT_KEYWORD;
            if (++(*at) >= ctx->count)
                goto end_of_script;
            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);
            if (*at >= ctx->count)
                goto end_of_script;
        }

    } else if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
        cond->kind = AM2_IF_PLAIN;
        if (!ScriptParseEvents(ctx, at, cond))
            goto fail;

        if (ScriptAtWord(ctx, *at, AM2_TOK_BUTNOT)) {       /* butnot */
            cond->kind = AM2_IF_BUTNOT_STRING;
            if (++(*at) >= ctx->count)
                goto end_of_script;
            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);
            if (*at >= ctx->count)
                goto end_of_script;
        }
    }

    /* ---- testvar ---- */
    if (ScriptAtWord(ctx, *at, AM2_TOK_TESTVAR)) {
        if (++(*at) >= ctx->count)
            goto end_of_script;

        while (!ScriptAtWord(ctx, *at, AM2_TOK_THEN)) {   /* until `then` */
            AM2_ScriptTest test;
            memset(&test, 0, sizeof test);

            if (!ScriptParseValue(ctx, at, &test.left[0], &test.left[1],
                                  &test.left[2]))
                goto fail;
            if (*at >= ctx->count)
                goto end_of_script_late;

            AM2_ScriptTok *t = &ctx->tokens[*at];
            if (t->kind != AM2_TOKEN_CONTROL_CHAR)
                goto bad_operator;
            switch ((int32_t)(uintptr_t)t->value) {
            case AM2_TOK_LT:        test.op = AM2_CMP_LT; break;
            case AM2_TOK_LE:        test.op = AM2_CMP_LE; break;
            case AM2_TOK_EQ:        test.op = AM2_CMP_EQ; break;
            case AM2_TOK_GT:        test.op = AM2_CMP_GT; break;
            case AM2_TOK_GE:        test.op = AM2_CMP_GE; break;
            case AM2_TOK_NE:        test.op = AM2_CMP_NE; break;
            default: goto bad_operator;
            }

            if (++(*at) >= ctx->count)
                goto end_of_script_late;
            if (!ScriptParseValue(ctx, at, &test.right[0], &test.right[1],
                                  &test.right[2]))
                goto fail;
            if (*at >= ctx->count)
                goto end_of_script_late;

            t = &ctx->tokens[*at];
            if (t->kind != AM2_TOKEN_RESERVED)
                goto incomplete;
            int32_t id = (int32_t)(uintptr_t)t->value;
            if (id != AM2_TOK_THEN && id != AM2_TOK_AND)           /* then, and */
                goto incomplete;
            if (id == AM2_TOK_AND && ++(*at) >= ctx->count)
                goto end_of_script_late;

            int32_t n = cond->ntests++;
            cond->tests = (AM2_ScriptTest *)am2_realloc(
                cond->tests, (size_t)cond->ntests * sizeof(AM2_ScriptTest));
            cond->tests[n] = test;
        }
    }

    /* ---- then ---- */
    if (!ScriptAtWord(ctx, *at, AM2_TOK_THEN)) {
        am2_log("Line [%4d]:  Missing 'then' in if-statement.\n",
                ctx->tokens[*at].line);
        goto fail;
    }
    if (++(*at) >= ctx->count)
        goto end_of_script;

    if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (id == AM2_TOK_RANDOM) {                         /* random */
            cond->mode = AM2_THEN_RANDOM;
            if (++(*at) >= ctx->count)
                goto end_of_script;
        } else if (id == AM2_TOK_SEQUENTIAL) {                  /* sequential */
            cond->mode = AM2_THEN_SEQUENTIAL;
            if (++(*at) >= ctx->count)
                goto end_of_script;
        } else if (id == AM2_TOK_ONOBJSTATE) {                  /* onobjstate */
            cond->mode = AM2_THEN_ONOBJSTATE;
            if (++(*at) >= ctx->count)
                goto end_of_script;
            if (!ScriptResolveName(ctx, at, &cond->objstate, 0))
                goto fail;
        } else {
            cond->mode = AM2_THEN_NONE;
        }
    } else {
        cond->mode = AM2_THEN_NONE;
    }

    /* ---- the action list ---- */
    for (;;) {
        if (cond->mode == AM2_THEN_ONOBJSTATE) {
            AM2_ScriptTok *t = &ctx->tokens[*at];
            if (t->kind != AM2_TOKEN_STRING)
                goto want_string;
            objname = ScriptFindName((const char *)t->value);
            if (objname < 0)
                objname = AddNameTableName(
                    (const char *)ctx->tokens[*at].value,
                    AM2_NAME_TYPE_REF, 0);
            if (++(*at) >= ctx->count)
                goto end_of_script_late;
        }

        if (!ScriptAction(ctx, at, &act))
            goto fail;
        if (cond->mode == AM2_THEN_ONOBJSTATE)
            act.extra = objname;

        int32_t n = cond->nactions++;
        cond->actions = (AM2_ScriptAction *)am2_realloc(
            cond->actions, (size_t)cond->nactions * sizeof act);
        cond->actions[n] = act;

        if (*at >= ctx->count)
            break;
        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind != AM2_TOKEN_CONTROL_CHAR ||
            (int32_t)(uintptr_t)t->value != AM2_TOK_COMMA)   /* ',' */
            break;
        if (++(*at) >= ctx->count)
            goto end_of_script_late;
    }

    /* Newest first. */
    cond->next = kScriptConds;
    kScriptConds = cond;
    return 1;

want_integer:
    am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
            ctx->tokens[*at].line,
            ScriptTokenText(&ctx->tokens[*at], kScriptWord),
            kKindName(AM2_TOKEN_INTEGER));
    goto fail;

want_string:
    am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
            ctx->tokens[*at].line,
            ScriptTokenText(&ctx->tokens[*at], kScriptWord),
            kKindName(AM2_TOKEN_STRING));
    goto fail;

bad_operator:
    am2_log("Line [%4d]:  Unrecognized operator in testvar clause.\n",
            ctx->tokens[*at].line);
    goto fail;

incomplete:
    am2_log("Line [%4d]:  Incomplete testvar clause.\n",
            ctx->tokens[*at].line);
    goto fail;

end_of_script_late:
end_of_script:
    /* The original leaks the record on every "Unexpected end of script" path
     * -- only the messages that route through 0x00443E03 reach the free. Kept,
     * because a script that takes one of these is already being rejected and
     * the process is about to log and carry on. */
    am2_log("Unexpected end of script.\n");
    return 0;

fail:
    am2_free(cond);
    return 0;

fail_nofree:
    /* A failing event parse returns without freeing -- 0x00443E14 skips the
     * free that 0x00443E0B does. Another original leak, reproduced. */
    return 0;
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

    if (am2_dump_actions)
        am2_log("READSCRIPT %s\n", path);

    FILE *fh = fopen(path, "rt");
    if (!fh) {
        am2_log("ReadScript: Could not open %s for reading.\n", path);
        ok = 0;
    }

    int32_t lines = 0;
    int32_t at = ctx->count;    /* resume past whatever is already parsed */

    if (ok) {
        while (!feof(fh) && fgets(line, sizeof line, fh)) {
            ScriptNextToken(line, ctx, lines);
            lines++;
        }
        fclose(fh);
    }

    /* Second pass: walk the tokens and dispatch each statement. */
    int32_t compounds = 0;
    int32_t reported = -1;      /* the last line an error was reported for */

    while (at < ctx->count) {
        AM2_ScriptTok *tok = &ctx->tokens[at];

        if (tok->kind == AM2_TOKEN_RESERVED) {
            int32_t id = (int32_t)(uintptr_t)tok->value;

            if (id == AM2_TOK_PRELOADSPRITE) {
                ScriptPreloadSprite(ctx, &at);
                continue;
            }
            if (id == AM2_TOK_PAD) {
                ScriptPad(ctx, &at);
                continue;
            }
            if (id == AM2_TOK_VARIABLE) {
                ScriptVariable(ctx, &at);
                continue;
            }
            if (id == AM2_TOK_IF) {
                if (ScriptIf(ctx, &at) == 1)
                    compounds++;
                continue;
            }
            /* `object` and `objclass` share a handler, which reads the
             * keyword itself rather than being told which one it is. */
            if (id == AM2_TOK_OBJECT || id == AM2_TOK_OBJCLASS) {
                GenerateObjScriptFromTokens(ctx, &at);
                continue;
            }
        }

        /* Not a statement. Report once per line, not once per token -- a line
         * of six unknown words would otherwise give six identical messages. */
        if (tok->line > reported) {
            am2_log("Line [%4d]: Unknown Initial Word: %s\n",
                    tok->line, ScriptTokenText(tok, kScriptWord));
            reported = ctx->tokens[at].line;
        }
        at++;
    }

    if (am2_parse_all) {
        /* Once, and not from inside itself. */
        am2_parse_all = 0;
        ScriptParseAll();
    }

    /* Single player only: a multiplayer load goes on to read the rules and
     * the per-army AI scripts, and the summary would be one of four. */
    if (*(const int32_t *)AM2_IMAGE(ADDR_MP_SESSION) == 0) {
        /* The token count comes from the GLOBAL context, not the one passed
         * in. They are the same object for every caller in the image, but the
         * original reads the global and so does this. */
        const AM2_ScriptCtx *g =
            (const AM2_ScriptCtx *)AM2_IMAGE(ADDR_SCRIPT_CONTEXT);
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


#define kMapName      ((const char *)AM2_IMAGE(ADDR_MAP_NAME))
#define kMapFolder    ((const char *)AM2_IMAGE(ADDR_MAP_FOLDER))
#define kMpScriptName ((const char *)AM2_IMAGE(ADDR_MP_SCRIPT_NAME))
#define kLevelIndex   (*(const int32_t *)AM2_IMAGE(ADDR_LEVEL_INDEX))
#define kMpSession    (*(const int32_t *)AM2_IMAGE(ADDR_MP_SESSION))

/* Change into one of the game's data directories. Everything the script loader
 * opens is relative to wherever this last left us. */
static void ScriptSetDataDir(const char *dir)
{
    SetGameDir(dir);
}

/* Read one script, with the progress line the comm object's verbose flag asks
 * for. The three sites in LoadLevelScript are identical but for the path. */
static void ScriptReadWithLog(const char *path)
{
    int32_t verbose = *(const int32_t *)(kCommObject + AM2_COMM_VERBOSE) != 0;

    if (verbose)
        am2_log("reading script %s: ", path);

    int32_t ok = ReadScript(path, (AM2_ScriptCtx *)AM2_IMAGE(ADDR_SCRIPT_CONTEXT));

    /* Re-read the flag: ReadScript can have changed it. */
    if (*(const int32_t *)(kCommObject + AM2_COMM_VERBOSE))
        am2_log(ok ? "worked!\n" : "FAILED!\n");
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

    ScriptResetTokens((AM2_ScriptCtx *)AM2_IMAGE(ADDR_SCRIPT_CONTEXT));

    /* The five score variables, declared before anything can reference them.
     * `gamescorelimit` is seeded with the configured limit and its index is
     * thrown away -- scripts reach it by name and nothing writes it back --
     * while the four army scores keep theirs, because the runtime updates
     * those by index. */
    AddNameTableName(kImageStr(ADDR_NAME_SCORE_LIMIT), AM2_NAME_TYPE_INTEGER,
                     *(const int32_t *)AM2_IMAGE(ADDR_SCORE_LIMIT));
    *(int32_t *)AM2_IMAGE(ADDR_SVAR_GREENSCORE) =
        AddNameTableName(kImageStr(ADDR_NAME_GREENSCORE),
                         AM2_NAME_TYPE_INTEGER, 0);
    *(int32_t *)AM2_IMAGE(ADDR_SVAR_TANSCORE) =
        AddNameTableName(kImageStr(ADDR_NAME_TANSCORE),
                         AM2_NAME_TYPE_INTEGER, 0);
    *(int32_t *)AM2_IMAGE(ADDR_SVAR_BLUESCORE) =
        AddNameTableName(kImageStr(ADDR_NAME_BLUESCORE),
                         AM2_NAME_TYPE_INTEGER, 0);
    *(int32_t *)AM2_IMAGE(ADDR_SVAR_GREYSCORE) =
        AddNameTableName(kImageStr(ADDR_NAME_GREYSCORE),
                         AM2_NAME_TYPE_INTEGER, 0);

    ScriptReadWithLog(path);

    if (kMpSession) {
        static const uint32_t kColourName[4] = {
            0x00476A68u, 0x00485148u, 0x00485140u, 0x00485138u,
        };
        char aipath[0x40];

        ScriptSetDataDir(kImageStr(ADDR_RULES_DIR_STR));
        sprintf(path, "%s.txt", kMpScriptName);
        ScriptReadWithLog(path);

        for (int32_t i = 0; i < AM2_PLAYERS_MAX; i++) {
            uint8_t *slot = kCommObject + (size_t)i * AM2_PLAYER_STRIDE;

            if (!*(const int32_t *)(slot + AM2_PLAYER_ACTIVE))
                continue;
            /* A slot a real player still holds needs no AI script. The test
             * is the DirectPlay id, so this reads the opposite way round from
             * the name it first went in under -- see ADDR_COMM_SLOT_HAS_PLAYER. */
            if (CommSlotHasPlayer(kCommObject, i))
                continue;

            int32_t army = *(const int32_t *)(slot + AM2_PLAYER_ARMY);
            /* An army above 3 leaves the pointer at whatever the call above
             * returned -- zero -- and the format prints from NULL. No slot
             * carries one, and this is the original's own omission. */
            const char *colour = 0;
            if ((uint32_t)army <= 3)
                colour = (const char *)AM2_IMAGE(kColourName[army]);

            sprintf(aipath, "%s_ai_%s.txt", kMpScriptName, colour);
            ScriptReadWithLog(aipath);
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
    *(int32_t *)AM2_IMAGE(ADDR_CURRENT_OBJ_SCRIPT) = 0;
    *(int32_t *)AM2_IMAGE(ADDR_OBJ_SCRIPT_CAP) = 0;
    *(AM2_ObjScript **)AM2_IMAGE(ADDR_OBJ_SCRIPTS) = 0;
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
    int32_t save_obj = *(int32_t *)AM2_IMAGE(ADDR_CURRENT_OBJ_SCRIPT);
    AM2_ObjScript *save_objarr = *(AM2_ObjScript **)AM2_IMAGE(ADDR_OBJ_SCRIPTS);
    int32_t save_objcap = *(int32_t *)AM2_IMAGE(ADDR_OBJ_SCRIPT_CAP);
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
    *(int32_t *)AM2_IMAGE(ADDR_CURRENT_OBJ_SCRIPT) = save_obj;
    *(AM2_ObjScript **)AM2_IMAGE(ADDR_OBJ_SCRIPTS) = save_objarr;
    *(int32_t *)AM2_IMAGE(ADDR_OBJ_SCRIPT_CAP) = save_objcap;
    for (int32_t i = 0; i < 256; i++)
        kPadNumbers[i].count = save_padnum[i];

    am2_log("PARSEALL done: %d files\n", files);
}

/* Still original: the action runtime. ScriptRunLine hands it a NULL owner,
 * where a mission's own actions carry the object they belong to. */
typedef void (__cdecl *AM2_RunActionFn)(AM2_ScriptAction *act, void *owner);
#define orig_run_script_action \
            ((AM2_RunActionFn)AM2_IMAGE(ADDR_RUN_SCRIPT_ACTION))

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

    ctx.capacity = 0;
    ctx.count    = 0;
    ctx.tokens   = 0;

    ScriptResetTokens(&ctx);
    ScriptNextToken(line, &ctx, 0);

    if (!ScriptParseAction(&ctx, &at, &act)) {
        ScriptResetTokens(&ctx);
        return 0;
    }

    orig_run_script_action(&act, (void *)0);
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

    if (index <= 0)
        return 0;

    if (!GetVarValue(index, &v))
        return 0;

    v += delta;

    return SetVarValue(index, v) ? 1 : 0;
}

int script_install(void)
{
    int rc = 0;

    /* Token buffers pass between our code and the original's, so both sides
     * have to be on the game's heap. */
    am2_crt_use_game();

    am2_dump_actions = getenv("AM2_DUMP_ACTIONS") != 0;
    am2_parse_all = getenv("AM2_PARSE_ALL") != 0;
    am2_probe_noaction = getenv("AM2_PROBE_NOACTION") != 0;

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
