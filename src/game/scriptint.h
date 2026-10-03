/* scriptint.h -- what script.cpp and objscript.cpp share.
 *
 * The two modules are the original's own division: the image carries
 * "C:\ArmyMen2\source\script.cpp" and "C:\ArmyMen2\source\objscript.cpp", and
 * the functions referencing each sit in two clean address bands -- objscript
 * around 0x004364A0..0x004375A0, script from 0x0043EE80 up. This header is the
 * private surface between them, not part of the reconstruction's API, which is
 * why it is separate from script.h.
 */
#ifndef AM2_SCRIPTINT_H
#define AM2_SCRIPTINT_H

#include <stdint.h>

#include "crt.h"
#include "image.h"
#include "script.h"
#include "objscript.h"
#include "../inject/orig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Image data we only READ. Every one of these is const, and that is a fact
 * about the binary rather than a convention: nothing in the image writes into
 * any of them. The keyword table is the case that was checked exhaustively --
 * three readers, all reconstructed, no writer anywhere -- and the rest are
 * string and index tables of the same kind. They sit in .data rather than
 * .rdata only because MSVC 6 puts an initialised aggregate holding pointers
 * there, for the relocations this image no longer carries.
 *
 * Anything NOT in this group is written by the reconstruction, and the types
 * below say which is which. */
/* The keyword table is no longer among these: it is written out in
 * scripttokens.h and carried, so nothing has to be mapped to answer what a
 * word means. See the comment there. */
#define kKindName(k)     ((const char *)((void *)(uintptr_t)( \
                             ((const uint32_t *)((void *)(uintptr_t)( \
                                 ((uintptr_t)g_scriptKindNames))))[k])))
#define kUnknownWord     ((const char *)((void *)(uintptr_t)( \
                             *(const uint32_t *)((void *)(uintptr_t)( \
                                 ((uintptr_t)g_scriptUnknownStr))))))
#define kPadBit(n)       ((g_padBitTable)[n])
#define kImageStr(a)     ((const char *)((void *)(uintptr_t)( \
                             *(const uint32_t *)((void *)(uintptr_t)(a)))))
#define kCommObject      (g_commObject)

/* Image data the reconstruction WRITES. */
#define kScriptWord      (g_scriptWordBuf)
#define kScriptNames     (*(AM2_ScriptName * *)&g_scriptNames)
#define kScriptNameCount (g_scriptNameCount)
#define kScriptNameCap   (g_scriptNameCap)
#define kObjScripts      (g_objScripts)
#define kObjScriptCount  (g_currentObjScript)
#define kObjScriptCap    (g_objScriptCap)

/* Advance one token and check its kind, with the two messages the original
 * writes out at every argument; *at moves either way. Not a function in the
 * image: its body is at every call site. It answers 1 or 0 rather than the
 * token, and the caller indexes the token itself -- a constant the inliner
 * can fold into the caller's test, which is how the image has no null test
 * after it. */
static inline int32_t ScriptExpect(AM2_ScriptCtx *ctx, int32_t *at,
                                   int32_t kind)
{
    if (++(*at) >= ctx->count) {
        am2_log("Unexpected end of script.\n");
        return 0;
    }
    if (ctx->tokens[*at].kind != kind) {
        am2_log("Line [%4d]:  '%s' found, but expected token of type %s\n",
                ctx->tokens[*at].line,
                ScriptTokenText(&ctx->tokens[*at], kScriptWord),
                kKindName(kind));
        return 0;
    }
    return 1;
}

/* One action, through the dump switch. objscript.cpp's frame blocks and
 * script.cpp's `if` both append actions, so both go through here. */
int32_t ScriptAction(AM2_ScriptCtx *ctx, int32_t *at,
                     AM2_ScriptAction *act);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SCRIPTINT_H */
