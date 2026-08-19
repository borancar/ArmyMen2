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

/* ------------------------------------------------- object script ---- */

#define kObjScripts (*(AM2_ObjScript **)AM2_IMAGE(ADDR_OBJ_SCRIPTS))

/* The record the current statement is filling. NewObjScript has already
 * incremented the count, so the one being built is the last. */
static AM2_ObjScript *ScriptCurrentObj(void)
{
    return &kObjScripts[*(const int32_t *)AM2_IMAGE(ADDR_CURRENT_OBJ_SCRIPT)
                        - 1];
}

uint8_t *__cdecl ObjFrameNewAction(AM2_ObjFrame *f)
{
    if (f->actioncap <= f->actioncount) {
        int32_t cap = f->actioncap + 5;
        f->actions = (uint8_t *)am2_realloc(f->actions, (size_t)cap * 0x48);
        memset(f->actions + (size_t)f->actioncount * 0x48, 0, 5 * 0x48);
        f->actioncap = cap;
    }
    uint8_t *p = f->actions + (size_t)f->actioncount * 0x48;
    f->actioncount++;
    return p;
}

AM2_ObjFrame *__cdecl ObjStateNewFrame(AM2_ObjState *s)
{
    if (s->framecap <= s->framecount) {
        int32_t cap = s->framecap + 10;
        s->frames = (AM2_ObjFrame *)am2_realloc(
            s->frames, (size_t)cap * sizeof(AM2_ObjFrame));
        memset(&s->frames[s->framecount], 0, 10 * sizeof(AM2_ObjFrame));
        s->framecap = cap;
    }
    AM2_ObjFrame *p = &s->frames[s->framecount];
    s->framecount++;
    return p;
}

AM2_ObjState *__cdecl ObjScriptNewState(AM2_ObjScript *o)
{
    if (o->statecap <= o->statecount) {
        int32_t cap = o->statecap + 5;
        o->states = (AM2_ObjState *)am2_realloc(
            o->states, (size_t)cap * sizeof(AM2_ObjState));
        memset(&o->states[o->statecount], 0, 5 * sizeof(AM2_ObjState));
        o->statecap = cap;
    }
    AM2_ObjState *p = &o->states[o->statecount];
    o->statecount++;
    return p;
}

typedef int32_t (__cdecl *am2_parse3_fn)(AM2_ScriptCtx *, int32_t *,
                                         int32_t *, int32_t *, int32_t *);

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
        case 16: g = ADDR_SVAR_GREEN; break;
        case 17: g = ADDR_SVAR_TAN;   break;
        case 18: g = ADDR_SVAR_BLUE;  break;
        case 19: g = ADDR_SVAR_GREY;  break;
        case 20: g = ADDR_SVAR_ME;    break;
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
        if (type != 2 && type != AM2_NAME_TYPE_INTEGER) {
            am2_log("Line [%4d]:  Name '%s' already used for another type.\n",
                    ctx->tokens[*at].line,
                    ScriptTokenText(tok, kScriptWord));
            return 0;
        }
    } else {
        *out = AddNameTableName((const char *)ctx->tokens[*at].value, 2, 0);
    }

    /* Either way the token becomes a kind-7 reference and gives up its
     * string -- including when the name already existed. */
    am2_free(ctx->tokens[*at].value);
    ctx->tokens[*at].kind = 7;
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
        if (!((am2_parse3_fn)(uintptr_t)ADDR_SCRIPT_PARSE_EVENT)(
                ctx, at, &a, &b, &c))
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
        if (id == 45 || id == 47 || id == 14)   /* then, butnot, testvar */
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
        *out = AddNameTableName((const char *)ctx->tokens[*at].value, 2, 0);
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

    *mask = (int32_t)0x80000000;

    tok = &ctx->tokens[*at];
    int32_t army = 0;
    if (tok->kind == AM2_TOKEN_RESERVED) {
        switch ((int32_t)(uintptr_t)tok->value) {
        case 16: army = 0x40000000; break;      /* green */
        case 17: army = 0x20000000; break;      /* tan   */
        case 18: army = 0x10000000; break;      /* blue  */
        case 19: army = 0x08000000; break;      /* grey  */
        default: break;
        }
    }
    if (army) {
        if (!ScriptStep(ctx, at))
            return 0;
        *mask |= army;
    } else {
        *mask = (int32_t)0xF8000000;            /* any army */
    }

    tok = &ctx->tokens[*at];
    if (tok->kind == AM2_TOKEN_RESERVED) {
        int32_t type = 0;
        switch ((int32_t)(uintptr_t)tok->value) {
        case 30: type = 0x04000000; break;      /* item    */
        case 32: type = 0x01C00000; break;      /* sarge   */
        case 34: type = 0x01400000; break;      /* trooper */
        case 36: type = 0x00200000; break;      /* vehicle */
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
        *form = 0;
        (*at)++;
        return 1;
    }

    *val = (int32_t)0x80000000;

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
    *form = 1;

    tok = &ctx->tokens[*at];
    if (tok->kind == AM2_TOKEN_RESERVED &&
        (int32_t)(uintptr_t)tok->value == 184) {        /* group */
        if (!ScriptStep(ctx, at))
            return 0;
        *form = 2;
        *val = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ScriptStep(ctx, at))
            return 0;
    } else {
        *val = -1;
    }
    return 1;
}

int32_t __cdecl ScriptParseValue(AM2_ScriptCtx *ctx, int32_t *at,
                                 int32_t *kind, int32_t *a, int32_t *b)
{
    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)tok->value;
        int32_t form = 0, names = 0, armies = 0;

        switch (id) {
        case 130: form = 2; names  = 1; break;  /* getdmglvl     */
        case 131: form = 3; names  = 1; break;  /* gethealth     */
        case 132: form = 4; names  = 1; break;  /* getdisguise   */
        case 179: form = 5; names  = 2; break;  /* hasitem       */
        case 181: form = 6; armies = 1; break;  /* iscoloringame */
        case 182: form = 7; armies = 2; break;  /* isally        */
        case 183: form = 8; armies = 1; break;  /* teamscore     */
        default:  form = 0; break;
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
    *kind = isliteral ? 0 : 1;
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
    case 16: army = 0; break;           /* green */
    case 17: army = 1; break;           /* tan   */
    case 18: army = 2; break;           /* blue  */
    case 19: army = 3; break;           /* grey  */
    default:
        am2_log("Line [%4d]:  Expected Army Color instead of '%s'\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord));
        return -1;
    }

    /* A thiscall on the army table -- ecx is loaded immediately before the
     * call, which is the tell CLAUDE.md records for an i386 MSVC member
     * function as opposed to a COM dispatch. */
    typedef int32_t (__thiscall *am2_army_fn)(void *, int32_t);
    int32_t rc = ((am2_army_fn)(uintptr_t)ADDR_ARMY_LOOKUP)(
        *(void **)AM2_IMAGE(ADDR_ARMY_TABLE), army);

    (*at)++;
    return rc;
}

int32_t __cdecl ScriptCompare3(int32_t a, int32_t op, int32_t b)
{
    switch (op) {
    case 0:  return b == a;
    case 1:  return b < a;
    case 2:  return b > a;
    default: return 0;
    }
}

int32_t __cdecl ScriptObjFrame(AM2_ScriptCtx *ctx, int32_t *at)
{
    uint8_t action[0x48];

    AM2_ObjScript *obj = ScriptCurrentObj();
    AM2_ObjState *state = &obj->states[obj->statecount - 1];
    AM2_ObjFrame *frame = ObjStateNewFrame(state);

    AM2_ScriptTok *tok = &ctx->tokens[*at];
    if (tok->kind != AM2_TOKEN_RESERVED) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_RESERVED));
        return 0;
    }

    tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
    if (!tok)
        return 0;
    frame->a = (int32_t)(uintptr_t)tok->value;

    tok = ScriptExpect(ctx, at, AM2_TOKEN_INTEGER);
    if (!tok)
        return 0;
    frame->b = (int32_t)(uintptr_t)tok->value;
    (*at)++;

    while (!ScriptIsStatementStart(ctx, at)) {
        /* `state` and `frame` both end this frame's action list. */
        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind == AM2_TOKEN_RESERVED) {
            int32_t id = (int32_t)(uintptr_t)t->value;
            if (id == 141 || id == 142)
                break;
        }

        if (!((int32_t (__cdecl *)(AM2_ScriptCtx *, int32_t *, uint8_t *))
                  (uintptr_t)ADDR_SCRIPT_PARSE_ACTION)(ctx, at, action))
            return 0;
        memcpy(ObjFrameNewAction(frame), action, 0x48);

        if (*at < ctx->count &&
            ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == 3) {   /* ',' */
            if (++(*at) >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
        }
    }
    return 1;
}

int32_t __cdecl ScriptObjState(AM2_ScriptCtx *ctx, int32_t *at)
{
    AM2_ObjScript *obj = ScriptCurrentObj();
    AM2_ObjState *state = ObjScriptNewState(obj);

    AM2_ScriptTok *tok = &ctx->tokens[*at];
    if (tok->kind != AM2_TOKEN_RESERVED) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_RESERVED));
        return 0;
    }

    tok = ScriptExpect(ctx, at, AM2_TOKEN_STRING);
    if (!tok)
        return 0;

    int32_t name = ScriptFindName((const char *)tok->value);
    if (name < 0)
        name = AddNameTableName((const char *)ctx->tokens[*at].value, 2, 0);

    /* The name resolves to this state's index within the object, not to
     * anything global. */
    kScriptNames[name].value = obj->statecount - 1;
    state->name = name;
    (*at)++;

    while (!ScriptIsStatementStart(ctx, at)) {
        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)t->value == 141)        /* another `state` */
            break;
        if (!ScriptObjFrame(ctx, at))
            return 0;
    }
    return 1;
}

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
    AM2_ObjScript *target = ((AM2_ObjScript *(__cdecl *)(void))
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
        if (!ScriptResolveName(ctx, at, &name, 0))
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
        if (!ScriptObjState(ctx, at))
            return 0;
    }
    return 1;
}

#define kPads       ((AM2_Pad *)AM2_IMAGE(ADDR_PADS))
#define kPadCount   (*(int32_t *)AM2_IMAGE(ADDR_PAD_COUNT))
#define kPadNumbers ((AM2_PadNumber *)AM2_IMAGE(ADDR_PAD_NUMBERS))

/* The trigger keyword each Reserved id contributes, for ids 16..42. Zero ends
 * the run -- the loop stops at the first word it does not recognise and leaves
 * it for what follows.
 *
 * Two entries look like slips in the original and are reproduced as they are.
 * `army0`..`army3` duplicate `green`/`tan`/`blue`/`grey`, which is reasonable.
 * `halftrack` shares 0x0200 with `grey`, which is not -- but nothing here can
 * tell whether that was intended, so it stands. */
static const uint32_t kPadTriggerBit[27] = {
    0x0040,  /* 16 green         */  0x0080,  /* 17 tan           */
    0x0100,  /* 18 blue          */  0x0200,  /* 19 grey          */
    0,       /* 20 me            */  0x0040,  /* 21 army0         */
    0x0080,  /* 22 army1         */  0x0100,  /* 23 army2         */
    0x0200,  /* 24 army3         */  0,       /* 25 preloadsprite */
    0,       /* 26 pad           */  0,       /* 27 padon         */
    0,       /* 28 padoff        */  0x0001,  /* 29 everything    */
    0,       /* 30 item          */  0,       /* 31               */
    0x0002,  /* 32 sarge         */  0x0004,  /* 33 unit          */
    0x0008,  /* 34 trooper       */  0x0010,  /* 35 tank          */
    0x0020,  /* 36 vehicle       */  0,       /* 37 jeep          */
    0x0200,  /* 38 halftrack     */  0x0400,  /* 39 convoy        */
    0x0800,  /* 40 boat          */  0x1000,  /* 41 groundvehicle */
    0x2000,  /* 42 npc           */
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
        mask = ((const int32_t *)AM2_IMAGE(ADDR_PAD_BIT_TABLE))[number];
    }
    if (!layer)
        return;

    /* One dword store covering both words -- the default before the scan. */
    *(int32_t *)&pn->cx = *(const int32_t *)AM2_IMAGE(ADDR_PAD_DEFAULT_POS);

    int32_t w = *(const int32_t *)AM2_IMAGE(ADDR_MAP_WIDTH);
    int32_t total = w * *(const int32_t *)AM2_IMAGE(ADDR_MAP_HEIGHT);
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
    int32_t name = AddNameTableName((const char *)ctx->tokens[*at].value, 1, 0);
    am2_free(ctx->tokens[*at].value);
    ctx->tokens[*at].kind = 7;
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
        if (op == 4)                    /* '<' */
            pad->compare = 1;
        else if (op == 6)               /* '=' */
            pad->compare = 0;
        else if (op == 7)               /* '>' */
            pad->compare = 2;
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
            (int32_t)(uintptr_t)ctx->tokens[*at].value == 43) {

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
         * on 0x00444842, past the call. */
        ((void (__cdecl *)(AM2_Pad *, int32_t))(uintptr_t)
            ADDR_PAD_FINALISE)(pad, 0);
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
    uint8_t action[0x48];
    int32_t objname = 0;

    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }

    AM2_ScriptCond *cond = (AM2_ScriptCond *)am2_malloc(sizeof *cond);
    memset(cond, 0, sizeof *cond);

    if (ScriptAtWord(ctx, *at, 63)) {           /* timeabsolute */
        cond->kind = 5;
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

    } else if (ScriptScanFor(ctx, *at, 52 /*after*/, 45 /*then*/)) {
        /* An `<event> after <event>` chain. */
        cond->kind = 6;
        for (;;) {
            int32_t k = ctx->tokens[*at].kind;
            if (k != AM2_TOKEN_STRING && k != AM2_TOKEN_RESERVED)
                break;

            if (!((am2_parse3_fn)(uintptr_t)ADDR_SCRIPT_PARSE_EVENT)(
                    ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);

            if (!ScriptAtWord(ctx, *at, 52)) {
                am2_log("Line [%4d]:  Missing 'after' in if-statement.\n",
                        ctx->tokens[*at].line);
                goto fail;
            }
            if (++(*at) >= ctx->count)
                goto end_of_script_late;

            if (!((am2_parse3_fn)(uintptr_t)ADDR_SCRIPT_PARSE_EVENT)(
                    ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);

            if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
                continue;
            if (ScriptAtWord(ctx, *at, 45) || ScriptAtWord(ctx, *at, 14))
                break;                          /* then, or testvar */
            if (!ScriptAtWord(ctx, *at, 53))    /* and */
                continue;
            if (++(*at) >= ctx->count)
                goto end_of_script_late;
        }

    } else if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        int wants_of = 0;

        switch (id) {
        case 46: cond->kind = 1; break;         /* allof   */
        case 48: cond->kind = 2; break;         /* inorder */
        case 50: cond->kind = 3; wants_of = 1; break;   /* count  */
        case 49: cond->kind = 4; wants_of = 1; break;   /* repeat */
        default: cond->kind = 0; break;         /* butnot, and anything else */
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

                if (!ScriptAtWord(ctx, *at, 51)) {      /* of */
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

        if (ScriptAtWord(ctx, *at, 47)) {       /* butnot */
            cond->kind = 7;
            if (++(*at) >= ctx->count)
                goto end_of_script;
            if (!((am2_parse3_fn)(uintptr_t)ADDR_SCRIPT_PARSE_EVENT)(
                    ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);
            if (*at >= ctx->count)
                goto end_of_script;
        }

    } else if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
        cond->kind = 0;
        if (!ScriptParseEvents(ctx, at, cond))
            goto fail;

        if (ScriptAtWord(ctx, *at, 47)) {       /* butnot */
            cond->kind = 8;
            if (++(*at) >= ctx->count)
                goto end_of_script;
            if (!((am2_parse3_fn)(uintptr_t)ADDR_SCRIPT_PARSE_EVENT)(
                    ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);
            if (*at >= ctx->count)
                goto end_of_script;
        }
    }

    /* ---- testvar ---- */
    if (ScriptAtWord(ctx, *at, 14)) {
        if (++(*at) >= ctx->count)
            goto end_of_script;

        while (!ScriptAtWord(ctx, *at, 45)) {   /* until `then` */
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
            case 4: test.op = 2; break;         /* <  */
            case 5: test.op = 4; break;         /* <= */
            case 6: test.op = 0; break;         /* =  */
            case 7: test.op = 3; break;         /* >  */
            case 8: test.op = 5; break;         /* >= */
            case 9: test.op = 1; break;         /* <> */
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
            if (id != 45 && id != 53)           /* then, and */
                goto incomplete;
            if (id == 53 && ++(*at) >= ctx->count)
                goto end_of_script_late;

            int32_t n = cond->ntests++;
            cond->tests = (AM2_ScriptTest *)am2_realloc(
                cond->tests, (size_t)cond->ntests * sizeof(AM2_ScriptTest));
            cond->tests[n] = test;
        }
    }

    /* ---- then ---- */
    if (!ScriptAtWord(ctx, *at, 45)) {
        am2_log("Line [%4d]:  Missing 'then' in if-statement.\n",
                ctx->tokens[*at].line);
        goto fail;
    }
    if (++(*at) >= ctx->count)
        goto end_of_script;

    if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED) {
        int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (id == 55) {                         /* random */
            cond->mode = 1;
            if (++(*at) >= ctx->count)
                goto end_of_script;
        } else if (id == 54) {                  /* sequential */
            cond->mode = 2;
            if (++(*at) >= ctx->count)
                goto end_of_script;
        } else if (id == 56) {                  /* onobjstate */
            cond->mode = 3;
            if (++(*at) >= ctx->count)
                goto end_of_script;
            if (!ScriptResolveName(ctx, at, &cond->objstate, 0))
                goto fail;
        } else {
            cond->mode = 0;
        }
    } else {
        cond->mode = 0;
    }

    /* ---- the action list ---- */
    for (;;) {
        if (cond->mode == 3) {
            AM2_ScriptTok *t = &ctx->tokens[*at];
            if (t->kind != AM2_TOKEN_STRING)
                goto want_string;
            objname = ScriptFindName((const char *)t->value);
            if (objname < 0)
                objname = AddNameTableName(
                    (const char *)ctx->tokens[*at].value, 2, 0);
            if (++(*at) >= ctx->count)
                goto end_of_script_late;
        }

        if (!((int32_t (__cdecl *)(AM2_ScriptCtx *, int32_t *, uint8_t *))
                  (uintptr_t)ADDR_SCRIPT_PARSE_ACTION)(ctx, at, action))
            goto fail;
        if (cond->mode == 3)
            *(int32_t *)(action + 0x44) = objname;

        int32_t n = cond->nactions++;
        cond->actions = (uint8_t *)am2_realloc(
            cond->actions, (size_t)cond->nactions * 0x48);
        memcpy(cond->actions + (size_t)n * 0x48, action, 0x48);

        if (*at >= ctx->count)
            break;
        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind != AM2_TOKEN_CONTROL_CHAR ||
            (int32_t)(uintptr_t)t->value != 3)   /* ',' */
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

/* No statement handler is left original. Each takes the context
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
                ScriptPad(ctx, &at);
                continue;
            }
            if (id == 133) {
                ScriptVariable(ctx, &at);
                continue;
            }
            if (id == 44) {
                if (ScriptIf(ctx, &at) == 1)
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
    rc |= patch_replace(ADDR_SCRIPT_PAD,
                        (const void *)ScriptPad,
                        "ScriptPad", 1);
    rc |= patch_replace(ADDR_SCRIPT_IF,
                        (const void *)ScriptIf,
                        "ScriptIf", 1);
    rc |= patch_replace(ADDR_SCRIPT_SCAN_FOR,
                        (const void *)ScriptScanFor, "ScriptScanFor", 1);
    rc |= patch_replace(ADDR_SCRIPT_COMPARE3,
                        (const void *)ScriptCompare3, "ScriptCompare3", 1);
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
    rc |= patch_replace(ADDR_OBJ_FRAME_NEW_ACTION,
                        (const void *)ObjFrameNewAction,
                        "ObjFrameNewAction", 1);
    rc |= patch_replace(ADDR_OBJ_STATE_NEW_FRAME,
                        (const void *)ObjStateNewFrame,
                        "ObjStateNewFrame", 1);
    rc |= patch_replace(ADDR_OBJ_SCRIPT_NEW_STATE,
                        (const void *)ObjScriptNewState,
                        "ObjScriptNewState", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJ_STATE,
                        (const void *)ScriptObjState, "ScriptObjState", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJ_FRAME,
                        (const void *)ScriptObjFrame, "ScriptObjFrame", 1);
    return rc;
}
