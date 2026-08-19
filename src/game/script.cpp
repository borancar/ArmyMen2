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
        (int32_t)(uintptr_t)tok->value != 136) {        /* refvar */
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
                               uint8_t *action, int32_t quiet)
{
    *(int32_t *)(action + AM2_ACT_RELATIVE) = 0;

    AM2_ScriptTok *tok = &ctx->tokens[*at];

    if (tok->kind == AM2_TOKEN_CONTROL_CHAR) {
        int32_t id = (int32_t)(uintptr_t)tok->value;
        if (id != 1 && id != 13)        /* '(' or '+' */
            return 0;                   /* silently, with no message */

        if (id == 13) {                 /* a leading '+' means relative */
            *(int32_t *)(action + AM2_ACT_RELATIVE) = 1;
            if (!ScriptStep(ctx, at))
                return 0;
        }
        if (!ScriptStep(ctx, at))       /* past the '(' */
            return 0;

        if (!ScriptCoord(ctx, at, (int16_t *)(action + AM2_ACT_POS),
                         (int32_t *)(action + AM2_ACT_XVAR)))
            return 0;
        if (!ScriptStep(ctx, at))
            return 0;

        tok = &ctx->tokens[*at];
        if (tok->kind != AM2_TOKEN_CONTROL_CHAR ||
            (int32_t)(uintptr_t)tok->value != 3) {      /* ',' */
            am2_log("Line [%4d]:  Comma expected in coordinates\n",
                    ctx->tokens[*at].line);
            return 0;
        }
        if (!ScriptStep(ctx, at))
            return 0;

        if (!ScriptCoord(ctx, at, (int16_t *)(action + AM2_ACT_POS + 2),
                         (int32_t *)(action + AM2_ACT_YVAR)))
            return 0;
        if (!ScriptStep(ctx, at))
            return 0;

        tok = &ctx->tokens[*at];
        if (tok->kind == AM2_TOKEN_CONTROL_CHAR &&
            (int32_t)(uintptr_t)tok->value == 2) {      /* ')' */
            (*at)++;
            return 1;
        }
        am2_log("Line [%4d]:  Close parens expected in coordinates\n",
                ctx->tokens[*at].line);
        return 0;
    }

    if (tok->kind == AM2_TOKEN_STRING) {
        int32_t idx = ScriptFindName((const char *)tok->value);
        if (idx >= 0 && kScriptNames[idx].type == 1) {          /* a pad */
            int32_t number = kPads[kScriptNames[idx].value].number;
            /* Both centroid words at once, exactly as the original. */
            *(int32_t *)(action + AM2_ACT_POS) =
                *(const int32_t *)&kPadNumbers[number].cx;

            am2_free(ctx->tokens[*at].value);
            ctx->tokens[*at].kind = 7;
            ctx->tokens[*at].value = (void *)(uintptr_t)idx;
            (*at)++;
            return 1;
        }
        /* Not a pad -- fall through, and the name is resolved below. */
    }

    return ScriptResolveName(ctx, at, (int32_t *)(action + AM2_ACT_NAME),
                             quiet) != 0;
}

/* The five `<verb> <target> [by <target>]` events, which differ only in the
 * kind they record and the two messages they print. */
static const struct {
    int32_t     id;
    int32_t     kind;
    const char *missing;
    const char *missing_by;
} kScriptEvents[5] = {
    { 57, 4, "Line [%4d]:  Expected item name after KILLED\n",
             "Line [%4d]:  Expected item name after KILLED ... BY\n" },
    { 58, 5, "Line [%4d]:  Expected item name after HIT\n",
             "Line [%4d]:  Expected item name after HIT ... BY\n" },
    { 59, 6, "Line [%4d]:  Expected item name after HEALED\n",
             "Line [%4d]:  Expected item name after HEALED ... BY\n" },
    { 61, 7, "Line [%4d]:  Expected item name after PICKEDUP\n",
             "Line [%4d]:  Expected item name after PICKEDUP ... BY\n" },
    { 62, 8, "Line [%4d]:  Expected item name after DROPPED\n",
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
    if (kScriptNames[idx].type != 1) {
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

        if (id == 27) {                                 /* padon */
            *kind = 3;
            return ScriptPadEvent(ctx, at, val,
                                  "Line [%4d]:  Name required after PADON\n",
                                  "Line [%4d]:  PADON name was not a pad\n");
        }
        if (id == 28) {                                 /* padoff */
            *kind = 2;
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
                (int32_t)(uintptr_t)tok->value != 60)
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
    *kind = 0;
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

/* Set from AM2_DUMP_ACTIONS and AM2_PARSE_ALL. */
int32_t am2_dump_actions = 0;
static int32_t am2_parse_all = 0;
static int32_t am2_orig_actions = 0;
static void ScriptParseAll(void);

/* ---------------------------------------------------------- actions ---- */

#define ACT(off) (*(int32_t *)(action + (off)))

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
                               uint8_t *action)
{
    if (!ActStep(ctx, at))
        return 0;
    if (!ScriptLocation(ctx, at, action, 0))
        return 0;
    int32_t army = ScriptArmyColour(ctx, at);
    ACT(0x40) = army;
    return army >= 0;
}

/* The weapon a `createtrooper` carries. -1 for one the keyword table has but
 * the trooper table rejects. */
static int32_t ActWeapon(int32_t id)
{
    switch (id) {
    case 81:  return 1;     /* rifle     */
    case 82:  return 4;     /* bazooka   */
    case 83:  return 2;     /* grenade   */
    case 84:  return 3;     /* flamer    */
    case 85:  return 5;     /* mortar    */
    case 86:  return 10;    /* autorifle */
    case 91:  return 20;    /* sweeper   */
    case 93:  return 29;    /* vulcan    */
    case 95:  return 30;    /* sniper    */
    default:  return -1;    /* flak, yours, explosive, detonator, lure,
                             * heavymg -- weapons the powerup table has and
                             * this one does not */
    }
}

static int32_t ActChassis(int32_t id)
{
    switch (id) {
    case 35: return 1;      /* tank      */
    case 37: return 0;      /* jeep      */
    case 38: return 2;      /* halftrack */
    case 39: return 3;      /* convoy    */
    case 40: return 5;      /* boat      */
    default: return -1;     /* `vehicle` itself is not a chassis */
    }
}

static int32_t ActPickup(int32_t id)
{
    switch (id) {
    case 82:  return 4;     case 83:  return 2;     case 84:  return 3;
    case 85:  return 5;     case 86:  return 10;    case 87:  return 28;
    case 88:  return 11;    case 89:  return 12;    case 91:  return 20;
    case 92:  return 14;    case 93:  return 29;    case 94:  return 8;
    case 95:  return 30;    case 96:  return 32;    case 97:  return 31;
    case 98:  return 33;    case 99:  return 34;    case 100: return 35;
    case 101: return 36;    case 102: return 37;    case 103: return 38;
    case 104: return 39;    case 105: return 40;    case 106: return 42;
    case 107: return 23;    case 108: return 22;    case 109: return 24;
    case 110: return 25;    case 111: return 26;    case 112: return 41;
    default:  return -1;    /* `detonator` is not a pickup */
    }
}

/* projectile, fire, crush, explosion -- shared by `damage` and
 * `setdamagepad`, and the codes are not in keyword order. */
static int32_t ActDamageKind(int32_t id)
{
    switch (id) {
    case 126: return 2;     /* projectile */
    case 127: return 1;     /* fire       */
    case 128: return 4;     /* crush      */
    case 129: return 3;     /* explosion  */
    default:  return -1;
    }
}

/* attack, defend, ignore, evade -- shared by `order ... inmode` and
 * `setaimode`, and the codes are neither sequential nor in keyword order. */
static int32_t ActAiMode(int32_t id)
{
    switch (id) {
    case 155: return 6;     /* attack */
    case 156: return 7;     /* defend */
    case 157: return 2;     /* ignore */
    case 158: return 5;     /* evade  */
    default:  return -1;
    }
}

/* A variable name that must already be declared, for the several actions that
 * take one. The message differs per caller, so it comes in. */
static int32_t ActVarName(AM2_ScriptCtx *ctx, int32_t *at, uint8_t *action,
                          const char *undeclared)
{
    int32_t idx = ScriptFindName((const char *)ctx->tokens[*at].value);
    if (idx < 0 || kScriptNames[idx].type != AM2_NAME_TYPE_INTEGER) {
        am2_log(undeclared, ctx->tokens[*at].line, ctx->tokens[*at].value);
        return 0;
    }
    ACT(0x28) = idx;
    (*at)++;
    return 1;
}

/* 0x00440D70. One action, into a 0x48-byte record.
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
                                      uint8_t *action)
{
    memset(action, 0, 0x48);
    ACT(0x00) = -2;

    if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
        return 0;
    int32_t id = (int32_t)(uintptr_t)ctx->tokens[*at].value;
    if (id < 64 || id > 186)
        return 0;

    switch (id) {

    case 76:                                    /* restorecamerafocus */
        (*at)++;
        ACT(0x14) = 0x1E;
        return 1;

    case 75:                                    /* setcamerafocus */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x1D;
        return ScriptResolveName(ctx, at, &ACT(0x18), 0) != 0;

    case 73:                                    /* suspendai */
    case 74:                                    /* reviveai   */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 73) ? 0x1B : 0x1C;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING)
            return ScriptResolveName(ctx, at, &ACT(0x18), 0) != 0;
        /* No name means everyone, which is the same built-in uid the
         * resolver's unreachable id-15 arm would have given. */
        ACT(0x18) = *(const int32_t *)AM2_IMAGE(ADDR_SVAR_ID15);
        return 1;

    case 64:                                    /* showmessage       */
    case 65:                                    /* showbitmap        */
    case 66:                                    /* showbitmapnopause */
    case 71:                                    /* showfailure       */
    case 72:                                    /* showpda           */
    case 151:                                   /* setbriefing       */
    case 152:                                   /* setbriefvo        */
    case 153: {                                 /* setstratmap       */
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
                ACT(0x14) = kShow[i].code;
        /* The record keeps the token's own string. Nothing copies it and
         * nothing frees it here. */
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
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
        ACT(0x14) = 6;
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        ACT(0x38) = 0;
        ACT(0x3C) = 1;
        ACT(0x44) = 0;
        if (++(*at) >= ctx->count)
            return 1;
        ScriptLocation(ctx, at, action, 1);
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x3C) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        /* No bounds check before this one, unlike the two above. */
        if (ctx->tokens[++(*at)].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x44) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 68:                                    /* playitemsound */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        ACT(0x14) = 7;
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        ACT(0x38) = 0;
        ACT(0x3C) = 1;
        ACT(0x44) = 0;
        if (++(*at) >= ctx->count)
            return 1;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x3C) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (ctx->tokens[++(*at)].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x44) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 69:                                    /* playmusic */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 8;
        ACT(0x30) = 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING)
            return 1;
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 70:                                    /* tracevar */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 9;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        return ActVarName(ctx, at, action,
            "Line [%4d]:  expected variable name in TRACEVAR but variable "
            "%s not declared\n");
    case 78:                                    /* trigger */
        if (!ActStep(ctx, at))
            return 0;
        /* The uid lands at +0x00, over the -2 the record opens with, and the
         * zero at +0x04 -- the call pushes the record and then the record
         * plus four. */
        return ScriptObjectUid(ctx, at, &ACT(0x04), &ACT(0x00)) != 0;

    case 79:                                    /* triggerdelay */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == 136) {   /* refvar */
            if (!ActStep(ctx, at))
                return 0;
            if (!ActVarName(ctx, at, action,
                    "Line [%4d]:  expected variable name but variable %s not "
                    "declared\n"))
                return 0;
        } else if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            ACT(0x08) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (ACT(0x08) < 0) {
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
        return ScriptObjectUid(ctx, at, &ACT(0x04), &ACT(0x00)) != 0;

    case 80:                                    /* createtrooper */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x0D;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
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
            ACT(0x34) = w;
        }
        return ActPlaceAndArmy(ctx, at, action);

    case 113:                                   /* createvehicle */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x0F;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
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
            ACT(0x38) = c;
        }
        return ActPlaceAndArmy(ctx, at, action);

    case 114:                                   /* createpowerup */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x10;
        /* The name is optional here, unlike the other two creates. */
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
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
            ACT(0x34) = p;
        }
        if (!ActStep(ctx, at))
            return 1;
        if (!ScriptLocation(ctx, at, action, 0))
            return 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 115:                                   /* createexplosion */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x0E;
        if (!ScriptLocation(ctx, at, action, 0))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (ACT(0x38) < 0x78 || ACT(0x38) > 0x95) {
            am2_log("Line[%4d]:  Invalid explosion type.\n",
                    ctx->tokens[*at].line);
            ACT(0x38) = 0x7F;
        }
        ACT(0x3C) = 0;
        ACT(0x40) = 4;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x3C) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (++(*at) >= ctx->count)
            return 1;
        ACT(0x40) = ScriptArmyColour(ctx, at);
        if (ACT(0x40) < 0)
            ACT(0x40) = 4;
        return 1;

    case 116:                                   /* createroach */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x11;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        ACT(0x30) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptLocation(ctx, at, action, 0))
            return 0;
        ACT(0x40) = ScriptArmyColour(ctx, at);
        return 1;

    case 138:                                   /* setobjstate */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x25;
        /* The object at +0x1C and the state at +0x18 -- the other way round
         * from how the statement reads. */
        if (!ScriptResolveName(ctx, at, &ACT(0x1C), 0))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        {
            int32_t st = ScriptFindName((const char *)ctx->tokens[*at].value);
            if (st < 0)
                st = AddNameTableName(
                    (const char *)ctx->tokens[*at].value, 2, 0);
            ACT(0x18) = st;
        }
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 137:                                   /* setobjbmp    */
    case 124:                                   /* heal         */
    case 123:                                   /* setmaxhealth */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 137) ? 0x24 : (id == 124) ? 0x0A : 0x0B;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 125:                                   /* damage */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x0C;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        ACT(0x44) = 0;
        if (++(*at) >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
            return 1;
        {
            int32_t k = ActDamageKind(
                (int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (k < 0)
                return 1;
            ACT(0x44) = k;
        }
        (*at)++;
        return 1;

    case 143:                                   /* deploy */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x12;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count)
            return 1;
        /* Quiet, and the result is thrown away: `deploy <name>,` is legal and
         * the comma is not a location. */
        ScriptLocation(ctx, at, action, 1);
        return 1;

    case 144:                                   /* undeploy */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x13;
        return ScriptResolveName(ctx, at, &ACT(0x18), 0) != 0;

    case 145:                                   /* resurrect */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x14;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count)
            return 1;
        ScriptLocation(ctx, at, action, 1);      /* as `deploy` */
        return 1;

    case 146:                                   /* ally   */
    case 147:                                   /* unally */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 146) ? 0x15 : 0x16;
        ACT(0x18) = ScriptArmyColour(ctx, at);
        if (ACT(0x18) < 0)
            return 0;
        ACT(0x1C) = ScriptArmyColour(ctx, at);
        return 1;

    case 148:                                   /* setforcecolor */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x17;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        ACT(0x1C) = ScriptArmyColour(ctx, at);
        return 1;

    case 77:                                    /* moveitem */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x1F;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        return ScriptLocation(ctx, at, action, 0) != 0;

    case 117:                                   /* setfacing    */
    case 118:                                   /* setgunfacing */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 117) ? 0x20 : 0x21;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }
        /* A facing may be `refvar <name>` instead of a literal. Anything
         * else is accepted and leaves the field at zero. */
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED ||
            (int32_t)(uintptr_t)ctx->tokens[*at].value != 136)
            return 1;
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        return ActVarName(ctx, at, action,
            "Line [%4d]:  expected variable name but variable %s not "
            "declared\n");

    case 134:                                   /* setvar */
    case 135:                                   /* addvar */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 134) ? 0x23 : 0x22;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
            return 1;
        }
        if (id == 135 && ctx->tokens[*at].kind != AM2_TOKEN_STRING) {
            ActTypeErr(ctx, at, AM2_TOKEN_STRING);
            return 0;
        }
        return ActVarName(ctx, at, action,
            id == 134
                ? "Line [%4d]:  expected variable name in SetVar but "
                  "variable %s not declared\n"
                : "Line [%4d]:  expected variable name in AddVar but "
                  "variable %s not declared\n");

    case 149:                                   /* activateregion   */
    case 150:                                   /* inactivateregion */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x26;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x38) = (id == 149);
        ACT(0x18) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 119: {                                 /* order */
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
                return 0;
            ACT(0x44) = 0;
        } else {
            if (!ScriptOrderTarget(ctx, at, &ACT(0x44), &ACT(0x18),
                                   &ACT(0x40)))
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
        if (verb == 120) {                      /* follow */
            if (!ActStep(ctx, at))
                return 0;
            ACT(0x14) = 0x19;
            if (!ScriptResolveName(ctx, at, &ACT(0x1C), 0))
                return 0;
        } else if (verb == 121) {               /* moveto */
            if (!ActStep(ctx, at))
                return 0;
            ACT(0x14) = 0x18;
            if (!ScriptLocation(ctx, at, action, 0))
                return 0;
        } else {
            return 0;
        }
        ACT(0x38) = -1;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_RESERVED ||
            (int32_t)(uintptr_t)ctx->tokens[*at].value != 122)   /* inmode */
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
            ACT(0x38) = m;
        }
        (*at)++;
        return 1;
    }

    case 154:                                   /* setaimode */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x2A;
        if (ctx->tokens[*at].kind == AM2_TOKEN_STRING) {
            if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
                return 0;
            ACT(0x44) = 0;
        } else if (!ScriptOrderTarget(ctx, at, &ACT(0x44), &ACT(0x18),
                                      &ACT(0x40))) {
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
            ACT(0x38) = m;
        }
        (*at)++;
        return 1;

    case 159:                                   /* setaipose */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x2B;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
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
            ACT(0x38) = (m == 3) ? 0 : m + 1;
        }
        (*at)++;
        return 1;

    case 164:                                   /* setspeed */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x2C;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
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
            if (v == 165)                       /* slow   */
                ACT(0x38) = 1;
            else if (v == 166)                  /* normal */
                ACT(0x38) = 0;
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
        ACT(0x14) = (id == 167) ? 0x2D : (id == 176) ? 0x35 : 0x39;
        if (id != 185 && !ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 173:                                   /* setitemflag */
        if (!ActStep(ctx, at))
            return 0;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind != AM2_TOKEN_RESERVED) {
            ActTypeErr(ctx, at, AM2_TOKEN_RESERVED);
            return 0;
        }
        if ((int32_t)(uintptr_t)ctx->tokens[*at].value != 174) {  /* strategic */
            am2_log("Line [%4d]:  Expected {strategic} in setitemflag\n",
                    ctx->tokens[*at].line);
            return 0;
        }
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x33;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 186:                                   /* setdamagepad */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x3A;
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
            if (kScriptNames[p].type != 1) {
                am2_log("Line [%4d]:  SetDamagePad name was not a pad\n",
                        ctx->tokens[*at].line);
                return 0;
            }
            /* The pad it resolves to, not the name-table index. */
            ACT(0x18) = kScriptNames[p].value;
        }
        if (!ActStep(ctx, at))
            return 0;
        if (ctx->tokens[*at].kind != AM2_TOKEN_INTEGER) {
            ActTypeErr(ctx, at, AM2_TOKEN_INTEGER);
            return 0;
        }
        ACT(0x3C) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x38) = 0;
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            (*at)++;
        }
        ACT(0x44) = 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_RESERVED)
            return 1;
        {
            int32_t k = ActDamageKind(
                (int32_t)(uintptr_t)ctx->tokens[*at].value);
            if (k < 0)
                return 1;
            ACT(0x44) = k;
        }
        (*at)++;
        return 1;

    case 175:                                   /* setitemowner */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x34;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        if (ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            ACT(0x38) = 4;
            return 1;
        }
        ACT(0x38) = ScriptArmyColour(ctx, at);
        return ACT(0x38) >= 0;

    case 168:                                   /* setm80 */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x2E;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        ACT(0x40) = ScriptArmyColour(ctx, at);
        if (ACT(0x40) < 0)
            ACT(0x40) = 4;
        return 1;

    case 180:                                   /* dropitem */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = 0x38;
        /* The item goes to +0x40 and whoever drops it to +0x18, which is the
         * other way round from every other two-name action. */
        if (!ScriptResolveName(ctx, at, &ACT(0x40), 0))
            return 0;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (*at >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }
        return ScriptLocation(ctx, at, action, 0) != 0;

    case 169:                                   /* setzombie    */
    case 170:                                   /* setscientist */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 169) ? 0x2F : 0x30;
        return ScriptResolveName(ctx, at, &ACT(0x18), 0) != 0;

    case 171:                                   /* makesmoke */
    case 172:                                   /* makeflame */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 171) ? 0x31 : 0x32;
        if (!ScriptLocation(ctx, at, action, 0))
            return 0;
        if (*at >= ctx->count ||
            ctx->tokens[*at].kind != AM2_TOKEN_INTEGER)
            return 1;
        ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
        (*at)++;
        return 1;

    case 177:                                   /* fireweapon */
    case 178:                                   /* unitfire   */
        if (!ActStep(ctx, at))
            return 0;
        ACT(0x14) = (id == 177) ? 0x36 : 0x37;
        if (!ScriptResolveName(ctx, at, &ACT(0x18), 0))
            return 0;
        if (id == 177 && !ScriptResolveName(ctx, at, &ACT(0x40), 0))
            return 0;
        if (*at < ctx->count &&
            ctx->tokens[*at].kind == AM2_TOKEN_INTEGER) {
            ACT(0x38) = (int32_t)(uintptr_t)ctx->tokens[*at].value;
            if (++(*at) >= ctx->count) {
                am2_log("Unexpected end of script.\n");
                return 0;
            }
        } else {
            ACT(0x38) = -1;
        }
        return ScriptLocation(ctx, at, action, 0) != 0;

    default:
        /* Every other reserved word in 64..186 -- the weapons, the pickups,
         * the AI modes and the poses. They are arguments, not actions. */
        return 0;
    }
}

/* Ours unless AM2_ORIG_ACTIONS is set, which is how the reference dump in
 * tests/actions-reference.txt was taken and how it is re-taken. */
static int32_t ScriptParseAction(AM2_ScriptCtx *ctx, int32_t *at,
                                 uint8_t *action)
{
    if (am2_orig_actions)
        return ((int32_t (__cdecl *)(AM2_ScriptCtx *, int32_t *, uint8_t *))
            (uintptr_t)ADDR_SCRIPT_PARSE_ACTION)(ctx, at, action);
    return ScriptParseActionRecon(ctx, at, action);
}

/* Every action goes through here, so one place can dump what was parsed.
 *
 * AM2_DUMP_ACTIONS=1 prints the 0x48-byte record for each action the scripts
 * produce. Run the game once with the original parser and once with ours and
 * diff the two logs: that compares every action the shipped missions actually
 * contain, in the real process, with no emulator and no translation. The
 * offline harness cannot reach this code at all -- it opens files and calls
 * into the image -- and would be slower than the game if it could. */
static int32_t ScriptAction(AM2_ScriptCtx *ctx, int32_t *at, uint8_t *action)
{
    int32_t line = *at < ctx->count ? ctx->tokens[*at].line : -1;

    int32_t rc = ScriptParseAction(ctx, at, action);

    if (am2_dump_actions) {
        /* One call, not eighteen: the game's logger writes a line per call
         * and drops a fragment that does not end in a newline. */
        char buf[0x48 / 4 * 9 + 40];
        const uint32_t *w = (const uint32_t *)action;
        int n = sprintf(buf, "ACT %4d %d", line, rc);
        for (int32_t i = 0; i < 0x48 / 4; i++)
            n += sprintf(buf + n, " %08X", w[i]);
        sprintf(buf + n, "\n");
        am2_log("%s", buf);
    }
    return rc;
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

        if (!ScriptAction(ctx, at, action))
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

            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
                goto fail_nofree;
            ScriptAddEvent(cond, a, b, c3);

            if (!ScriptAtWord(ctx, *at, 52)) {
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
            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
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
            if (!ScriptParseEvent(ctx, at, &a, &b, &c3))
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

        if (!ScriptAction(ctx, at, action))
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

    if (am2_parse_all) {
        /* Once, and not from inside itself. */
        am2_parse_all = 0;
        ScriptParseAll();
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

int script_install(void)
{
    int rc = 0;

    /* Token buffers pass between our code and the original's, so both sides
     * have to be on the game's heap. */
    am2_crt_use_game();

    am2_dump_actions = getenv("AM2_DUMP_ACTIONS") != 0;
    am2_parse_all = getenv("AM2_PARSE_ALL") != 0;
    am2_orig_actions = getenv("AM2_ORIG_ACTIONS") != 0;

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
    rc |= patch_replace(ADDR_READ_SCRIPT,
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
    rc |= patch_replace(ADDR_SCRIPT_PARSE_EVENT,
                        (const void *)ScriptParseEvent,
                        "ScriptParseEvent", 1);
    rc |= patch_replace(ADDR_SCRIPT_LOCATION,
                        (const void *)ScriptLocation, "ScriptLocation", 1);
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
