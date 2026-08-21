/* objscript.cpp -- the object-script model.
 *
 * The original's own module: the image carries the string
 * "C:\ArmyMen2\source\objscript.cpp", and the functions here occupy the
 * address band around it. Four levels, each owning the next:
 *
 *   object/objclass  ->  state <name>  ->  frame <a> <b>  ->  action, ...
 *
 * Every level is the same shape -- capacity, count, array -- and each grows by
 * a fixed step rather than doubling: five states, ten frames, five actions.
 */
#ifndef AM2_OBJSCRIPT_H
#define AM2_OBJSCRIPT_H

#include <stdint.h>

#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

struct AM2_ScriptCtx;
struct AM2_ScriptAction;

typedef struct {
    int32_t  a;             /* +0x00 */
    int32_t  b;             /* +0x04 */
    int32_t  actioncap;     /* +0x08 */
    int32_t  actioncount;   /* +0x0C */
    struct AM2_ScriptAction *actions;  /* +0x10 */
} AM2_ObjFrame;             /* 20 bytes */

typedef struct {
    int32_t       name;     /* +0x00, name-table index */
    int32_t       framecap; /* +0x04 */
    int32_t       framecount; /* +0x08 */
    AM2_ObjFrame *frames;   /* +0x0C */
} AM2_ObjState;             /* 16 bytes */

typedef struct {
    int32_t form;           /* +0x00, 0 = object, 1 = objclass */
    union {
        int32_t  name;      /* +0x04 for `object` */
        uint16_t cls[2];    /* +0x04 and +0x06 for `objclass` */
    } u;
    int32_t       statecap;   /* +0x08 */
    int32_t       statecount; /* +0x0C */
    AM2_ObjState *states;     /* +0x10 */
} AM2_ObjScript;            /* 20 bytes */

/* 0x00437010, 0x00437070, 0x004370D0. Append one entry, growing first. Each
 * returns the new entry, zeroed. */
struct AM2_ScriptAction *__cdecl ObjFrameNewAction(AM2_ObjFrame *f);
AM2_ObjFrame *__cdecl ObjStateNewFrame(AM2_ObjState *s);
AM2_ObjState *__cdecl ObjScriptNewState(AM2_ObjScript *o);

/* 0x00437130. The same again on the three globals that hold the object-script
 * array, stepping twenty. APPENDS -- it is not the accessor its one call site
 * makes it look like, and the count it leaves behind is the new script's id. */
AM2_ObjScript *__cdecl NewObjScript(void);

/* 0x004374F0. Compare `b` against `a`: op 0 equal, 1 less, 2 greater. The
 * argument order is the surprise -- the value under test is the THIRD
 * argument, and everything else answers 0. */
int32_t __cdecl ScriptCompare(int32_t a, int32_t op, int32_t b);

/* 0x00436C20. One `state <name>` block: the name, then frames until the next
 * `state` or the next top-level statement. */
int32_t __cdecl ScriptObjState(struct AM2_ScriptCtx *ctx, int32_t *at);

/* 0x004369E0. One `frame <int> <int>` block: two integers, then actions
 * separated by commas until `state`, `frame`, or a top-level statement. */
int32_t __cdecl ScriptObjFrame(struct AM2_ScriptCtx *ctx, int32_t *at);

/* 0x00436D60. `object <name>` and `objclass <int> <int>`, then a block of
 * state definitions -- GenerateObjScriptFromTokens, named from its own error
 * string. It is the only handler that reads its own keyword rather than being
 * told which one it is, which is how one function serves two ids. */
int32_t __cdecl GenerateObjScriptFromTokens(struct AM2_ScriptCtx *ctx,
                                            int32_t *at);

int objscript_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00436280. Write the object-script section: tag, count, then four nested
 * levels of record. Action strings are length-prefixed in place of the
 * pointer field; see the body. Always returns 1. */
int32_t __cdecl SaveObjScriptSection(am2_FILE *fp);

/* 0x004364A0. Read it back: free what is there, then rebuild all four levels,
 * allocating each from the count it just read and restoring each action's
 * string as a kind-5 token. Returns 0 if the section tag does not match. */
int32_t __cdecl LoadObjScriptSection(am2_FILE *fp);

/* 0x004368D0. Free all four levels and clear the three globals. An action's
 * `text` is NOT freed -- the token list owns it. Three callers. */
void __cdecl FreeObjScripts(void);

#endif /* AM2_OBJSCRIPT_H */
