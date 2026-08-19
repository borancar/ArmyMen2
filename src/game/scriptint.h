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

/* The globals both modules reach. */
#define kScriptWord      ((char *)AM2_IMAGE(ADDR_SCRIPT_WORD_BUF))
#define kScriptNames     (*(AM2_ScriptName **)AM2_IMAGE(ADDR_SCRIPT_NAMES))
#define kScriptNameCount (*(int32_t *)AM2_IMAGE(ADDR_SCRIPT_NAME_COUNT))
#define kScriptNameCap   (*(int32_t *)AM2_IMAGE(ADDR_SCRIPT_NAME_CAP))
#define kObjScripts      (*(AM2_ObjScript **)AM2_IMAGE(ADDR_OBJ_SCRIPTS))
#define kKindName(k)     ((const char *)AM2_IMAGE( \
                             ((const uint32_t *)AM2_IMAGE( \
                                 ADDR_SCRIPT_KIND_NAMES))[k]))

/* Advance one token and check its kind, with the two messages the original
 * writes out at every argument. NULL after logging; *at moves either way. */
AM2_ScriptTok *ScriptExpect(AM2_ScriptCtx *ctx, int32_t *at, int32_t kind);

/* One action, through the dump switch. objscript.cpp's frame blocks and
 * script.cpp's `if` both append actions, so both go through here. */
int32_t ScriptAction(AM2_ScriptCtx *ctx, int32_t *at, uint8_t *action);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SCRIPTINT_H */
