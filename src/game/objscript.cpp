/* objscript.cpp -- see objscript.h.
 *
 * The original's own module boundary, not one invented here: the image carries
 * "C:\ArmyMen2\source\objscript.cpp" and the functions that reference it sit
 * in a band of their own around 0x004364A0..0x004375A0, well clear of
 * script.cpp's 0x0043EE80 and up.
 */
#include <stdint.h>
#include <string.h>

#include "crt.h"
#include "image.h"
#include "objscript.h"
#include "script.h"
#include "scriptint.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* ------------------------------------------------- object script ---- */



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
int objscript_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_SCRIPT_COMPARE3,
                        (const void *)ScriptCompare3, "ScriptCompare3", 1);
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
    rc |= patch_replace(ADDR_SCRIPT_OBJECT,
                        (const void *)GenerateObjScriptFromTokens,
                        "GenerateObjScriptFromTokens", 1);
    return rc;
}
