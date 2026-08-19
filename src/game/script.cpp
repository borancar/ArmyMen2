/* script.cpp -- see script.h. */
#include <stdint.h>

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

int script_install(void)
{
    return patch_replace(ADDR_SCRIPT_LOOKUP_TOKEN,
                         (const void *)ScriptLookupToken,
                         "ScriptLookupToken", 1);
}
