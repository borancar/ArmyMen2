/* script.cpp -- see script.h. */
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crt.h"
#include "misc.h"
#include "image.h"
#include "script.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* The table's bounds are the ones the original's loop uses, not a guess: it
 * starts at 0x00487C90 and stops when the cursor reaches 0x00488258. */
typedef struct {
    const char *name;
    int32_t     id;
} AM2_ScriptToken;

#define kScriptTokens   ((const AM2_ScriptToken *)AM2_IMAGE(0x00487C90u))
#define kScriptTokenEnd ((const AM2_ScriptToken *)AM2_IMAGE(0x00488258u))

int32_t __cdecl ScriptLookupToken(const char *word)
{
    const AM2_ScriptToken *e;

    for (e = kScriptTokens; e < kScriptTokenEnd; e++) {
        const char *a = word;
        const char *b = (const char *)AM2_IMAGE(e->name);

        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return e->id;
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
#define kScriptWord ((char *)AM2_IMAGE(0x00656354u))

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
    const AM2_ScriptToken *e;

    for (e = kScriptTokens; e < kScriptTokenEnd; e++)
        if (e->id == id)
            return (const char *)AM2_IMAGE(e->name);

    return 0;
}

#define kScriptNameCount (*(int32_t *)AM2_IMAGE(ADDR_SCRIPT_NAME_COUNT))
#define kScriptNames     (*(AM2_ScriptName **)AM2_IMAGE(ADDR_SCRIPT_NAMES))

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

#define kScriptNameCap   (*(int32_t *)AM2_IMAGE(ADDR_SCRIPT_NAME_CAP))
#define kNextUid         (*(int32_t *)AM2_IMAGE(ADDR_NEXT_UID))
#define kKindName(k)     ((const char *)AM2_IMAGE( \
                             ((const uint32_t *)AM2_IMAGE( \
                                 ADDR_SCRIPT_KIND_NAMES))[k]))

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
    e->live = 1;
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
        src = (const char *)AM2_IMAGE(
            *(const uint32_t *)AM2_IMAGE(ADDR_SCRIPT_UNKNOWN_STR));
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

    case 7:
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
    case 25:            /* preloadsprite */
    case 26:            /* pad */
    case 44:            /* if */
    case 133:           /* variable */
    case 139:           /* object */
    case 140:           /* objclass */
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
static AM2_ScriptTok *ScriptExpect(AM2_ScriptCtx *ctx, int32_t *at,
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

    ((void (__cdecl *)(int32_t, int32_t, int32_t, int32_t, int32_t))
        (uintptr_t)ADDR_PRELOAD_SPRITE)(arg[0], arg[1], arg[2], flags, 1);
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
    ctx->tokens[*at].kind = 7;
    ctx->tokens[*at].value = (void *)(uintptr_t)slot;

    tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
    if (!tok)
        return 0;

    kScriptNames[slot].value = (int32_t)(uintptr_t)tok->value;
    (*at)++;
    return 1;
}

/* The record ADDR_SCRIPT_OBJ_TARGET hands back. The first field selects the
 * form; the second is a name index for `object` and a pair of 16-bit class
 * fields for `objclass`, which overlap it. Written as a union because that is
 * what the original does -- a dword store on one path and two word stores on
 * the other, to the same offset. */
typedef struct {
    int32_t form;               /* 0 = object, 1 = objclass */
    union {
        int32_t  name;          /* +4, the name-table index */
        uint16_t cls[2];        /* +4 and +6 */
    } u;
} AM2_ScriptObjTarget;

/* Stamp the current script onto one object. */
static void ScriptAttachTo(uint8_t *obj)
{
    *(int32_t *)(obj + AM2_OBJ_SCRIPT) =
        *(const int32_t *)AM2_IMAGE(ADDR_CURRENT_OBJ_SCRIPT);
    *(int32_t *)(obj + AM2_OBJ_SCRIPT_PC) = 0;
    *(int32_t *)(obj + AM2_OBJ_SCRIPT_WAIT) = -1;
    *(int32_t *)(obj + AM2_OBJ_SCRIPT_STATE) = 0;
}

int32_t __cdecl GenerateObjScriptFromTokens(AM2_ScriptCtx *ctx, int32_t *at)
{
    /* This APPENDS a record and bumps the count, before the arguments are even
     * read -- so it happens on every call including the ones that go on to
     * fail, and the id later stamped onto each object is the incremented
     * count. Reading it as a plain accessor would still call it in the right
     * place, but would misdescribe why the count moves. */
    AM2_ScriptObjTarget *target = ((AM2_ScriptObjTarget *(__cdecl *)(void))
        (uintptr_t)ADDR_NEW_OBJ_SCRIPT)();

    AM2_ScriptTok *tok = &ctx->tokens[*at];
    if (tok->kind != AM2_TOKEN_RESERVED) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_RESERVED));
        return 0;
    }

    int32_t id = (int32_t)(uintptr_t)tok->value;

    if (id == 139) {                    /* object <name> */
        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        target->form = 0;

        int32_t name = 0;
        if (!((int32_t (__cdecl *)(AM2_ScriptCtx *, int32_t *, int32_t *,
                                   int32_t))(uintptr_t)
                  ADDR_SCRIPT_RESOLVE_NAME)(ctx, at, &name, 0))
            return 0;

        target->u.name = name;

        uint8_t *obj = ((uint8_t *(__cdecl *)(int32_t))(uintptr_t)
            ADDR_OBJ_BY_UID)(kScriptNames[name].value);

        if (!obj || !((int32_t (__cdecl *)(uint8_t *))(uintptr_t)
                          ADDR_OBJ_TAKES_SCRIPT)(obj)) {
            am2_log("Token %s is not a valid object.\n",
                    kScriptNames[name].name);
            return 0;
        }
        ScriptAttachTo(obj);

    } else if (id == 140) {             /* objclass <int> <int> */
        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        target->form = 1;

        /* The index is already on the first integer -- the advance above did
         * it -- so this one is checked in place rather than through
         * ScriptExpect, which would advance again. */
        tok = &ctx->tokens[*at];
        if (tok->kind != AM2_TOKEN_INTEGER) {
            am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord),
                    kKindName(AM2_TOKEN_INTEGER));
            return 0;
        }
        target->u.cls[0] = (uint16_t)(uintptr_t)tok->value;

        tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
        if (!tok)
            return 0;
        target->u.cls[1] = (uint16_t)(uintptr_t)tok->value;
        (*at)++;

        for (uint8_t *obj = ((uint8_t *(__cdecl *)(void))(uintptr_t)
                 ADDR_FIRST_SCRIPT_OBJ)();
             obj;
             obj = ((uint8_t *(__cdecl *)(void))(uintptr_t)
                 ADDR_NEXT_SCRIPT_OBJ)()) {
            if (((int32_t (__cdecl *)(uint8_t *))(uintptr_t)
                     ADDR_OBJ_TAKES_SCRIPT)(obj))
                ScriptAttachTo(obj);
        }

    } else {
        am2_log("Invalid token in GenerateObjScriptFromTokens\n");
        return 0;
    }

    /* The attribute block runs until the next top-level statement. */
    while (!ScriptIsStatementStart(ctx, at)) {
        if (!((int32_t (__cdecl *)(AM2_ScriptCtx *, int32_t *))(uintptr_t)
                  ADDR_SCRIPT_OBJ_ATTRIBUTE)(ctx, at))
            return 0;
    }
    return 1;
}

/* The remaining statement handlers are still the original's. Each takes the context
 * and a pointer to the walk index, which it advances past its own statement --
 * so ReadScript's loop makes no assumption about statement length. They are
 * reached by address because they are not reconstructed; nothing else about
 * this function has to wait for them. */
typedef void (__cdecl *am2_script_handler)(AM2_ScriptCtx *, int32_t *);
typedef int32_t (__cdecl *am2_script_if_handler)(AM2_ScriptCtx *, int32_t *);

int32_t __cdecl ReadScript(const char *path, AM2_ScriptCtx *ctx)
{
    char line[0x100];
    int32_t ok = 1;

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

            if (id == 25) {
                ScriptPreloadSprite(ctx, &at);
                continue;
            }
            if (id == 26) {
                ((am2_script_handler)(uintptr_t)ADDR_SCRIPT_PAD)(ctx, &at);
                continue;
            }
            if (id == 133) {
                ScriptVariable(ctx, &at);
                continue;
            }
            if (id == 44) {
                if (((am2_script_if_handler)(uintptr_t)
                         ADDR_SCRIPT_IF)(ctx, &at) == 1)
                    compounds++;
                continue;
            }
            /* `object` and `objclass` share a handler, which reads the
             * keyword itself rather than being told which one it is. */
            if (id == 139 || id == 140) {
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

    if (*(const int32_t *)AM2_IMAGE(ADDR_SCRIPT_QUIET) == 0) {
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

int script_install(void)
{
    int rc = 0;

    /* Token buffers pass between our code and the original's, so both sides
     * have to be on the game's heap. */
    am2_crt_use_game();

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
    rc |= patch_replace(ADDR_SCRIPT_FIND_NAME,
                        (const void *)ScriptFindName,
                        "ScriptFindName", 1);
    rc |= patch_replace(ADDR_SCRIPT_TOKEN_TEXT,
                        (const void *)ScriptTokenText,
                        "ScriptTokenText", 1);
    rc |= patch_replace(ADDR_SCRIPT_IS_STMT,
                        (const void *)ScriptIsStatementStart,
                        "ScriptIsStatementStart", 1);
    rc |= patch_replace(ADDR_SCRIPT_PARSE_FILE,
                        (const void *)ReadScript,
                        "ReadScript", 1);
    rc |= patch_replace(ADDR_SCRIPT_ALLOC_UID,
                        (const void *)AllocUid,
                        "AllocUid", 1);
    rc |= patch_replace(ADDR_SCRIPT_ADD_NAME,
                        (const void *)AddNameTableName,
                        "AddNameTableName", 1);
    rc |= patch_replace(ADDR_SCRIPT_VARIABLE,
                        (const void *)ScriptVariable,
                        "ScriptVariable", 1);
    rc |= patch_replace(ADDR_SCRIPT_PRELOADSPRITE,
                        (const void *)ScriptPreloadSprite,
                        "ScriptPreloadSprite", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJECT,
                        (const void *)GenerateObjScriptFromTokens,
                        "GenerateObjScriptFromTokens", 1);
    return rc;
}
