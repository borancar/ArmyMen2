/* objscript.cpp -- see objscript.h.
 *
 * The original's own module boundary, not one invented here: the image carries
 * "C:\ArmyMen2\source\objscript.cpp" and the functions that reference it sit
 * in a band of their own around 0x004364A0..0x004375A0, well clear of
 * script.cpp's 0x0043EE80 and up.
 */
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "crt.h"
#include "image.h"
#include "objscript.h"
#include "savetag.h"
#include "script.h"
#include "scriptint.h"
/* The object table and the item predicate, both reconstructed. objscript.cpp
 * used to reach all four through the image under a SECOND set of ADDR_ names,
 * for addresses that already had one. Those four macros are gone now; the
 * survivors are ADDR_LOOKUP_BY_UID, ADDR_OBJ_IS_ITEM, ADDR_FIRST_ITEM and
 * ADDR_NEXT_ITEM. Both mistakes
 * at once, and checkseams could see neither: an inline cast written
 * ((Fn)(uintptr_t)ADDR_X)(...) matches none of its three patterns. */
#include "objtable.h"
#include "objtype.h"
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "item.h"    /* ChangeObjectFrame */

/* ------------------------------------------------- object script ---- */

/* The record the current statement is filling. NewObjScript has already
 * incremented the count, so the one being built is the last. */
static AM2_ObjScript *ScriptCurrentObj(void)
{
    return &kObjScripts[*(const int32_t *)AM2_IMAGE(ADDR_CURRENT_OBJ_SCRIPT)
                        - 1];
}

AM2_ScriptAction *__cdecl ObjFrameNewAction(AM2_ObjFrame *f)
{
    if (f->actioncap <= f->actioncount) {
        int32_t cap = f->actioncap + 5;
        f->actions = (AM2_ScriptAction *)am2_realloc(
            f->actions, (size_t)cap * sizeof(AM2_ScriptAction));
        memset(&f->actions[f->actioncount], 0, 5 * sizeof(AM2_ScriptAction));
        f->actioncap = cap;
    }
    AM2_ScriptAction *p = &f->actions[f->actioncount];
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

/* The fourth of the same shape, and the only one whose capacity, count and
 * array are three globals rather than three fields of a parent -- there is no
 * record above an object script. It steps by twenty where the others step by
 * five and ten.
 *
 * The count it bumps is the id: ScriptCurrentObj reads the same global back as
 * count - 1, and ScriptAttachTo stamps it onto each object unadjusted. So the
 * ids the scripts carry are one-based, and this is where that comes from. */
AM2_ObjScript *__cdecl NewObjScript(void)
{
    if (kObjScriptCap <= kObjScriptCount) {
        int32_t cap = kObjScriptCap + 20;
        kObjScripts = (AM2_ObjScript *)am2_realloc(
            kObjScripts, (size_t)cap * sizeof(AM2_ObjScript));
        memset(&kObjScripts[kObjScriptCount], 0,
               20 * sizeof(AM2_ObjScript));
        kObjScriptCap = cap;
    }
    AM2_ObjScript *p = &kObjScripts[kObjScriptCount];
    kObjScriptCount++;
    return p;
}

int32_t __cdecl ScriptCompare(int32_t a, int32_t op, int32_t b)
{
    switch (op) {
    case AM2_PADCMP_EQ: return b == a;
    case AM2_PADCMP_LT: return b < a;
    case AM2_PADCMP_GT: return b > a;
    default: return 0;
    }
}


int32_t __cdecl ScriptObjFrame(AM2_ScriptCtx *ctx, int32_t *at)
{
    AM2_ScriptAction act;

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
            if (id == AM2_TOK_STATE || id == AM2_TOK_FRAME)
                break;
        }

        if (!ScriptAction(ctx, at, &act))
            return 0;
        *ObjFrameNewAction(frame) = act;

        if (*at < ctx->count &&
            ctx->tokens[*at].kind == AM2_TOKEN_CONTROL_CHAR &&
            (int32_t)(uintptr_t)ctx->tokens[*at].value == AM2_TOK_COMMA) {   /* ',' */
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
        name = AddNameTableName((const char *)ctx->tokens[*at].value,
                                    AM2_NAME_TYPE_REF, 0);

    /* The name resolves to this state's index within the object, not to
     * anything global. */
    kScriptNames[name].value = obj->statecount - 1;
    state->name = name;
    (*at)++;

    while (!ScriptIsStatementStart(ctx, at)) {
        AM2_ScriptTok *t = &ctx->tokens[*at];
        if (t->kind == AM2_TOKEN_RESERVED &&
            (int32_t)(uintptr_t)t->value == AM2_TOK_STATE)        /* another `state` */
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
    AM2_ObjScript *target = NewObjScript();

    AM2_ScriptTok *tok = &ctx->tokens[*at];
    if (tok->kind != AM2_TOKEN_RESERVED) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(tok, kScriptWord),
                kKindName(AM2_TOKEN_RESERVED));
        return 0;
    }

    int32_t id = (int32_t)(uintptr_t)tok->value;

    if (id == AM2_TOK_OBJECT) {                    /* object <name> */
        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        target->form = AM2_OBJSCRIPT_OBJECT;

        int32_t name = 0;
        if (!ScriptResolveName(ctx, at, &name, 0))
            return 0;

        target->u.name = name;

        uint8_t *obj = (uint8_t *)LookupByUID(
            (uint32_t)(uintptr_t)kScriptNames[name].value);

        if (!obj || !ObjIsItem((const AM2_Object *)obj)) {
            am2_log("Token %s is not a valid object.\n",
                    kScriptNames[name].name);
            return 0;
        }
        ScriptAttachTo(obj);

    } else if (id == AM2_TOK_OBJCLASS) {             /* objclass <int> <int> */
        if (++(*at) >= ctx->count) {
            am2_log("Unexpected end of script.\n");
            return 0;
        }

        target->form = AM2_OBJSCRIPT_CLASS;

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

        for (uint8_t *obj = (uint8_t *)FirstItem(); obj;
             obj = (uint8_t *)NextItem()) {
            if (ObjIsItem((const AM2_Object *)obj))
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
/* ------------------------------------------------- script runner ---- */

/* The mission clock in milliseconds, so `obj[0xBC] >= now` really is a
 * deadline test and `obj[0xBC] = frame->a + now` really is scheduling the next
 * frame. This comment used to say the opposite -- "It is NOT a clock, probed
 * at 500 for twelve seconds of play" -- on a probe taken with a dialog up,
 * which pauses it. See ADDR_GAME_CLOCK_MS in orig.h. */
#define kScriptTiming (*(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS))

/* SetObjScriptState -- original 0x004372A0, one caller, which is
 * RunScriptAction: the `setobjstate` action. It names itself twice, in two
 * different refusals.
 *
 * Point a named object's object-script at a named STATE and a frame. Four
 * refusals come first and they are not interchangeable:
 *
 *   - a name index of zero or below is refused silently, before anything is
 *     read;
 *   - a name that is not AM2_NAME_TYPE_REF gets "which is not an item";
 *   - a name whose uid does not resolve, or resolves to something ObjIsItem
 *     refuses, gets "which is not a valid object" -- ONE message for two
 *     different failures;
 *   - and an object with no script id at all is refused SILENTLY, which is
 *     the fourth and the only one with no complaint of its own.
 *
 * THE "NO OBJECT SCRIPT" TEST IS AN ADDRESS COMPARED AGAINST ZERO. The
 * original computes `&kObjScripts[id - 1]` with an LEA and then tests the
 * RESULT for zero -- which can only happen when the table pointer is null and
 * the id is exactly 1. So it is "the table was never allocated" wearing the
 * shape of a null-record check.
 *
 * That is why the address is built with explicit arithmetic below. Written as
 * `&kObjScripts[id - 1]` the check is `!(&array[i])`, which a compiler is
 * entitled to fold to false and delete -- the reconstruction would then have
 * no refusal at all, and nothing in the build or the A/B would say so.
 *
 * The state's own value is bounds-checked against the script's `statecount`
 * and refused with "Bad state index" -- a fifth message, and the only one
 * that names a state rather than an object.
 *
 * THE FRAME ARGUMENT IS STORED ONE LESS THAN IT ARRIVES. `frame - 1` goes
 * into OBJ_OFF_SCRIPT_FRAME, so the action's frame numbers are 1-based and
 * the field is 0-based. OBJ_OFF_SCRIPT_NEXT is cleared, which makes the
 * script due immediately, and UpdateObjectScript is run before returning --
 * so the new state takes effect in this frame rather than the next.
 *
 * ONE FIELD IS LEFT AS A LITERAL. The "has no object script" message prints
 * the object's own name through `obj[0x0C]` as a name-table index, and this
 * is the ONLY reader of that offset anywhere in the image -- a scan for a
 * second one finds none. CLAUDE.md's rule applies: a field with one consumer
 * is a field you cannot name, and buying `OBJ_OFF_FIELD_0C` would also cost a
 * family alias against OBJ_OFF_BOUNDS. Left as 0x0C with this note.
 */
int32_t __cdecl SetObjScriptState(int32_t nameidx, int32_t stateName,
                                  int32_t frame)
{
    const AM2_ScriptName *names = kScriptNames;
    uint8_t              *obj;
    AM2_ObjScript        *script;
    uintptr_t             at;
    int32_t               state;

    if (nameidx <= 0)
        return 0;

    if (names[nameidx].type != AM2_NAME_TYPE_REF) {
        am2_log("ERROR: SetObjScriptState was called with %s which is not "
                "an item\n", names[nameidx].name);
        return 0;
    }

    obj = (uint8_t *)LookupByUID((uint32_t)names[nameidx].value);
    if (!obj || !ObjIsItem((const AM2_Object *)obj)) {
        am2_log("ERROR: SetObjScriptState was called with %s which is not "
                "a valid object\n", names[nameidx].name);
        return 0;
    }

    if (*(const int32_t *)(obj + OBJ_OFF_SCRIPT_ID) <= 0)
        return 0;

    /* Written as the original's LEA rather than as `&kObjScripts[id - 1]`,
     * because the test below is on the ADDRESS: taking the address of an
     * array element and asking whether it is null is something the compiler
     * may fold away, and this check has to survive. */
    at = (uintptr_t)kObjScripts
       + (uintptr_t)(*(const int32_t *)(obj + OBJ_OFF_SCRIPT_ID) - 1)
         * sizeof(AM2_ObjScript);
    script = (AM2_ObjScript *)at;
    if (at == 0) {
        am2_log("Tried to set object script state (%s) in object %s, which "
                "has no object script.\n",
                names[stateName].name,
                names[*(const int32_t *)(obj + 0x0C)].name);
        return 0;
    }

    state = names[stateName].value;
    if (state >= script->statecount) {
        am2_log("Bad state index associated with state %s\n",
                names[stateName].name);
        return 0;
    }

    *(int32_t *)(obj + OBJ_OFF_SCRIPT_STATE) = state;
    *(int32_t *)(obj + OBJ_OFF_SCRIPT_FRAME) = frame - 1;
    *(int32_t *)(obj + OBJ_OFF_SCRIPT_NEXT)  = 0;

    UpdateObjectScript(obj);
    return 1;
}

/* 0x004351C0 and 0x00420410 stay original and are reached by address. The
 * first is ChangeObjectFrame, named by the error string below; the second is
 * unidentified beyond its role, so it keeps a role name. */
typedef void (__cdecl *AM2_RunScriptActionFn)(AM2_ScriptAction *act,
                                              void *owner);

#define orig_run_script_action \
    (*(AM2_RunScriptActionFn)AM2_IMAGE(ADDR_RUN_SCRIPT_ACTION))

/* 0x004371A0. Step one object along its object script.
 *
 * The object carries four fields: which script it runs, which state it is in,
 * which frame it reached, and a timing value at +0xBC. Nothing happens while
 * that value is at or above the global; otherwise the next frame is selected,
 * the sprite is changed to that frame's second number, +0xBC is reset to the
 * frame's first number plus the global, and every action the frame owns is run
 * against the object's owner.
 *
 * Two bounds are worth reading twice, because neither is what a careful
 * version would write and both are reproduced. The state index is rejected
 * only when it is `> statecount`, so the value EQUAL to the count is accepted
 * and indexes one past the array. And the frame bound is `>=`, which is
 * correct -- so the two disagree with each other in the same function.
 *
 * The `scr == NULL` test is likewise the original's: it can only fire when the
 * table pointer is null and the id is 1, since the address is computed before
 * it is checked. */
int32_t __cdecl UpdateObjectScript(void *obj)
{
    uint8_t *o  = (uint8_t *)obj;
    int32_t  id = *(const int32_t *)(o + OBJ_OFF_SCRIPT_ID);

    if (id <= 0)
        return 0;

    AM2_ObjScript *scr = &kObjScripts[id - 1];

    if (scr == (AM2_ObjScript *)0)
        return 1;

    if (*(const uint32_t *)(o + OBJ_OFF_SCRIPT_NEXT) >= kScriptTiming)
        return 1;

    int32_t st = *(const int32_t *)(o + OBJ_OFF_SCRIPT_STATE);

    if (st > scr->statecount || st < 0) {
        orig_log("UpdateObjectScript: bad state index\n");
        return 0;
    }

    AM2_ObjState *state = &scr->states[st];
    int32_t       fr    = *(const int32_t *)(o + OBJ_OFF_SCRIPT_FRAME) + 1;

    if (fr >= state->framecount)
        return 1;

    *(int32_t *)(o + OBJ_OFF_SCRIPT_FRAME) = fr;

    AM2_ObjFrame *frame = &state->frames[fr];

    if (!ChangeObjectFrame(obj, frame->b, 1))
        orig_log("ChangeObjectFrame failed in UpdateObjectScript\n");

    *(int32_t *)(o + OBJ_OFF_SCRIPT_NEXT) = frame->a + (int32_t)kScriptTiming;

    void *owner = *(void **)(o + OBJ_OFF_OWNER);

    for (int32_t i = 0; i < frame->actioncount; i++)
        orig_run_script_action(&frame->actions[i], owner);

    return 1;
}

/* ------------------------------------------------------ save ---- */

/* 0x00436280. Four nested levels -- script, state, frame, action -- each
 * record written whole. The sizes the original pushes are 0x14, 0x10, 0x14
 * and 0x48, which is a second derivation of the layout in objscript.h and
 * script.h: nothing here was taken from the allocators that build them.
 *
 * An action is NOT written from the table. It is copied aside first so that
 * `text`, a pointer, can be replaced by the string's LENGTH in the copy; the
 * bytes follow the record, with no terminator. Every other pointer in the
 * section -- states, frames, actions -- goes out raw, so those dwords are
 * whatever the heap gave this process and the loader has to overwrite them.
 * That is the same reason the item section cannot be compared byte for byte
 * across two builds.
 *
 * The count goes out through WriteSaveTag, which is how every length in this
 * format travels; see savetag.cpp. */
int32_t __cdecl SaveObjScriptSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_OBJSCRIPT);
    WriteSaveTag(fp, (uint32_t)kObjScriptCount);   /* a count, not a tag */

    for (int32_t i = 0; i < kObjScriptCount; i++) {
        AM2_ObjScript *o = &kObjScripts[i];

        orig_fwrite(o, AM2_OBJ_SCRIPT_REC_SIZE, 1, fp);

        for (int32_t j = 0; j < o->statecount; j++) {
            AM2_ObjState *st = &o->states[j];

            orig_fwrite(st, AM2_OBJ_STATE_REC_SIZE, 1, fp);

            for (int32_t k = 0; k < st->framecount; k++) {
                AM2_ObjFrame *fr = &st->frames[k];

                orig_fwrite(fr, AM2_OBJ_FRAME_REC_SIZE, 1, fp);

                for (int32_t m = 0; m < fr->actioncount; m++) {
                    const AM2_ScriptAction *a = &fr->actions[m];
                    uint8_t copy[sizeof(AM2_ScriptAction)];
                    int32_t len = a->text ? (int32_t)strlen(a->text) : 0;

                    memcpy(copy, a, sizeof copy);
                    *(int32_t *)(copy + offsetof(AM2_ScriptAction, text)) = len;
                    orig_fwrite(copy, sizeof copy, 1, fp);

                    if (len > 0)
                        orig_fwrite(a->text, (size_t)len, 1, fp);
                }
            }
        }
    }

    return 1;
}

/* 0x004368D0. Free every level of the object-script table and clear the three
 * globals. Bottom-up, and each level's array is freed whenever the POINTER is
 * non-null -- a zero count skips the loop below it but still frees the array,
 * which is why an empty level the loader allocated does not leak.
 *
 * What it never frees is an action's `text`. That string belongs to the token
 * list on both paths: the parser leaves the token's own pointer there and the
 * loader points it at a kind-5 token it adds. Freeing it here would be a
 * double free, and the original does not. */
void __cdecl FreeObjScripts(void)
{
    AM2_ObjScript *tab = kObjScripts;

    if (tab == (AM2_ObjScript *)0)
        goto done;

    if (kObjScriptCount <= 0)
        goto freetable;

    for (int32_t i = 0; i < kObjScriptCount; i++) {
        AM2_ObjScript *o = &tab[i];

        if (o->states == (AM2_ObjState *)0)
            continue;

        for (int32_t j = 0; j < o->statecount; j++) {
            AM2_ObjState *st = &o->states[j];

            if (st->frames == (AM2_ObjFrame *)0)
                continue;

            for (int32_t k = 0; k < st->framecount; k++) {
                AM2_ObjFrame *fr = &st->frames[k];

                if (fr->actions != (AM2_ScriptAction *)0)
                    am2_free(fr->actions);
            }

            am2_free(st->frames);
        }

        am2_free(o->states);
    }

freetable:
    am2_free(tab);

done:
    kObjScripts     = (AM2_ObjScript *)0;
    kObjScriptCount = 0;
    kObjScriptCap   = 0;
}

/* 0x004364A0. The mirror of the saver, and the last loader in the format.
 *
 * Each level reads its record WHOLE -- pointer field and all -- and then
 * replaces that stale pointer with a fresh allocation. Note what is not
 * overwritten: the capacity comes out of the file and is kept, while the
 * allocation is sized by the COUNT. Where a level was saved with spare
 * capacity the two disagree afterwards, which is the original's behaviour and
 * is reproduced rather than corrected. Only the empty arms zero a capacity.
 *
 * An action's `text` arrives holding the LENGTH the saver put there. The
 * string that follows is not strdup'd: it is handed to ScriptAddToken as a
 * kind-5 token, which owns a malloc'd copy, and `text` is pointed at that
 * token's value. So a loaded action's string is owned exactly the way a parsed
 * one's is -- script.h's note that `text` is "the token's own string, not a
 * copy" holds on both paths, and the token list frees it either way.
 *
 * The scratch buffer is the original's 2 KB and the length is not checked
 * against it. Reproduced; no name in a shipped script comes close. */
int32_t __cdecl LoadObjScriptSection(am2_FILE *fp)
{
    AM2_ScriptCtx *ctx = (AM2_ScriptCtx *)AM2_IMAGE(ADDR_SCRIPT_CONTEXT);
    char           buf[2048];
    int32_t        count;

    FreeObjScripts();

    if (!CheckSaveTag(fp, AM2_SAVETAG_OBJSCRIPT,
                      (const char *)AM2_IMAGE(ADDR_STR_OBJSCRIPT_CPP), 0x68))
        return 0;

    orig_fread(&count, 4, 1, fp);
    kObjScriptCount = count;
    kObjScriptCap   = count;

    if (count == 0) {
        kObjScriptCap = 0;
        kObjScripts   = (AM2_ObjScript *)0;
        return 1;
    }

    kObjScripts = (AM2_ObjScript *)am2_realloc(
        kObjScripts, (size_t)count * sizeof(AM2_ObjScript));
    memset(kObjScripts, 0, (size_t)count * sizeof(AM2_ObjScript));

    for (int32_t i = 0; i < kObjScriptCount; i++) {
        AM2_ObjScript *o = &kObjScripts[i];

        orig_fread(o, AM2_OBJ_SCRIPT_REC_SIZE, 1, fp);

        if (o->statecount == 0) {
            o->statecap = 0;
            o->states   = (AM2_ObjState *)0;
            continue;
        }

        o->states = (AM2_ObjState *)am2_malloc(
            (size_t)o->statecount * sizeof(AM2_ObjState));
        memset(o->states, 0, (size_t)o->statecount * sizeof(AM2_ObjState));

        for (int32_t j = 0; j < o->statecount; j++) {
            AM2_ObjState *st = &o->states[j];

            orig_fread(st, AM2_OBJ_STATE_REC_SIZE, 1, fp);

            if (st->framecount == 0) {
                st->framecap = 0;
                st->frames   = (AM2_ObjFrame *)0;
                continue;
            }

            st->frames = (AM2_ObjFrame *)am2_malloc(
                (size_t)st->framecount * sizeof(AM2_ObjFrame));
            memset(st->frames, 0,
                   (size_t)st->framecount * sizeof(AM2_ObjFrame));

            for (int32_t k = 0; k < st->framecount; k++) {
                AM2_ObjFrame *fr = &st->frames[k];

                orig_fread(fr, AM2_OBJ_FRAME_REC_SIZE, 1, fp);

                if (fr->actioncount == 0) {
                    fr->actioncap = 0;
                    fr->actions   = (AM2_ScriptAction *)0;
                    continue;
                }

                fr->actions = (AM2_ScriptAction *)am2_malloc(
                    (size_t)fr->actioncount * sizeof(AM2_ScriptAction));
                memset(fr->actions, 0,
                       (size_t)fr->actioncount * sizeof(AM2_ScriptAction));

                for (int32_t m = 0; m < fr->actioncount; m++) {
                    AM2_ScriptAction *a = &fr->actions[m];
                    int32_t          *textslot;
                    int32_t           len, idx;

                    orig_fread(a, (size_t)sizeof(AM2_ScriptAction), 1, fp);

                    textslot = (int32_t *)((uint8_t *)a
                                           + offsetof(AM2_ScriptAction, text));
                    len = *textslot;
                    if (len <= 0)
                        continue;

                    orig_fread(buf, (size_t)len, 1, fp);
                    buf[len] = '\0';

                    idx = ctx->count;
                    ScriptAddToken(ctx, AM2_TOKEN_STRING, buf, 0);
                    a->text = (char *)ctx->tokens[idx].value;
                }
            }
        }
    }

    return 1;
}

/* ObjMatchesSel -- original 0x00437400, four callers.
 *
 * Does an object match a selector? There are two entirely separate answers
 * here and the first argument picks between them.
 *
 * NON-ZERO MEANS "BY NAME" and the other two arguments change meaning with
 * it: the second becomes an index into the script name table and the third
 * stays the object. The entry must exist, must be a REF -- a name used before
 * it was declared -- and its value must be this object's uid. Anything else,
 * including an out-of-range index, answers 0.
 *
 * ZERO MEANS "BY MASK" and the second argument is nine independent tests,
 * every one of which must pass. Four are type predicates, one is a vehicle
 * kind, and four are armies -- the local player's, or literally 1, 2 or 3.
 *
 * THE ARMY BITS ARE NOT A FIELD, THEY ARE FOUR SEPARATE TESTS, so a mask
 * asking for both army 1 and army 2 can never match rather than matching
 * either. The same is true of the type bits: 0x08 and 0x20 together want an
 * object that is both a type 2 and a type 3.
 *
 * THE LAST TEST IS INVERTED IN THE ORIGINAL and that is where a transcription
 * goes wrong. Every other bit branches to the failure on a mismatch; the army
 * 3 test branches to SUCCESS on a match and falls through to the failure. Same
 * meaning, one instruction shorter, and it is why `eax` is set to 1 in the
 * middle of the function rather than at the end.
 *
 * The local-army test sign-extends the byte and the three literal ones do not
 * -- `movsx` against `cmp byte`. No army is negative, so it cannot matter.
 */
int32_t __cdecl ObjMatchesSel(int32_t byName, int32_t sel, void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (byName) {
        const AM2_ScriptName *rec;

        if (sel < 0 || sel >= *(const int32_t *)AM2_IMAGE(ADDR_SCRIPT_NAME_COUNT))
            return 0;

        rec = &(*(const AM2_ScriptName *const *)
                    AM2_IMAGE(ADDR_SCRIPT_NAMES))[sel];

        if (rec->type != AM2_NAME_TYPE_REF)
            return 0;

        return rec->value == (int32_t)((const AM2_Object *)o)->uid;
    }

    if ((sel & AM2_SEL_TYPE_1OR4)
        && !ObjType2Field548((const AM2_Object *)o))
        return 0;
    if ((sel & AM2_SEL_TYPE_238) && !ObjIsTypeIn238((const AM2_Object *)o))
        return 0;
    if ((sel & AM2_SEL_TYPE_2) && !ObjIsType2((const AM2_Object *)o))
        return 0;
    if ((sel & AM2_SEL_TYPE_3) && !ObjIsType3((const AM2_Object *)o))
        return 0;

    if ((sel & AM2_SEL_VEHICLE_KIND_1)
        && *(const int32_t *)(o + VEHICLE_OFF_KIND) != 1)
        return 0;

    if ((sel & AM2_SEL_ARMY_LOCAL)
        && *(const int8_t *)(o + OBJ_OFF_ARMY)
           != (int8_t)*(const uint32_t *)AM2_IMAGE(ADDR_DEFAULT_OWNER))
        return 0;
    if ((sel & AM2_SEL_ARMY_1) && *(const uint8_t *)(o + OBJ_OFF_ARMY) != 1)
        return 0;
    if ((sel & AM2_SEL_ARMY_2) && *(const uint8_t *)(o + OBJ_OFF_ARMY) != 2)
        return 0;
    if ((sel & AM2_SEL_ARMY_3) && *(const uint8_t *)(o + OBJ_OFF_ARMY) != 3)
        return 0;

    return 1;
}

int objscript_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_OBJ_MATCHES_SEL,
                        (const void *)ObjMatchesSel,
                        "ObjMatchesSel", 4);
    rc |= patch_replace(ADDR_SET_OBJ_SCRIPT_STATE,
                        (const void *)SetObjScriptState,
                        "SetObjScriptState", 1);

    rc |= patch_replace(ADDR_SCRIPT_COMPARE,
                        (const void *)ScriptCompare, "ScriptCompare", 1);
    rc |= patch_replace(ADDR_SAVE_OBJSCRIPT_SECTION,
                        (const void *)SaveObjScriptSection,
                        "SaveObjScriptSection", 1);
    rc |= patch_replace(ADDR_LOAD_OBJSCRIPT_SECTION,
                        (const void *)LoadObjScriptSection,
                        "LoadObjScriptSection", 1);
    rc |= patch_replace(ADDR_FREE_OBJ_SCRIPTS,
                        (const void *)FreeObjScripts, "FreeObjScripts", 1);
    rc |= patch_replace(ADDR_UPDATE_OBJECT_SCRIPT,
                        (const void *)UpdateObjectScript,
                        "UpdateObjectScript", 1);
    rc |= patch_replace(ADDR_OBJ_FRAME_NEW_ACTION,
                        (const void *)ObjFrameNewAction,
                        "ObjFrameNewAction", 1);
    rc |= patch_replace(ADDR_OBJ_STATE_NEW_FRAME,
                        (const void *)ObjStateNewFrame,
                        "ObjStateNewFrame", 1);
    rc |= patch_replace(ADDR_OBJ_SCRIPT_NEW_STATE,
                        (const void *)ObjScriptNewState,
                        "ObjScriptNewState", 1);
    rc |= patch_replace(ADDR_NEW_OBJ_SCRIPT,
                        (const void *)NewObjScript, "NewObjScript", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJ_STATE,
                        (const void *)ScriptObjState, "ScriptObjState", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJ_FRAME,
                        (const void *)ScriptObjFrame, "ScriptObjFrame", 1);
    rc |= patch_replace(ADDR_SCRIPT_OBJECT,
                        (const void *)GenerateObjScriptFromTokens,
                        "GenerateObjScriptFromTokens", 1);
    return rc;
}
