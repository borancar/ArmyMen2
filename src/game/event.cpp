/* event.cpp -- see event.h. */
#include <stdint.h>

#include "event.h"
#include "image.h"
#include "script.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_event_register_fn)(int32_t a, int32_t uid,
                                                 int32_t c, const void *fn,
                                                 const void *arg, int32_t f);
typedef void    (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_take_uid_fn)(void);
typedef void    (__cdecl *am2_event_notify_fn)(int32_t, int32_t, int32_t,
                                               int32_t, int32_t, int32_t,
                                               int32_t, int32_t, int32_t,
                                               int32_t);

#define orig_event_register  (*(am2_event_register_fn)ADDR_EVENT_REGISTER)
#define orig_event_clear_all (*(am2_void_fn)ADDR_EVENT_CLEAR_ALL)
#define orig_take_uid        (*(am2_take_uid_fn)ADDR_SCRIPT_ALLOC_UID)
#define orig_event_notify    (*(am2_event_notify_fn)ADDR_EVENT_NOTIFY)

#define kScriptConditions (*(AM2_ScriptCond **)AM2_IMAGE(ADDR_SCRIPT_CONDITIONS))
#define kImageFn(a)       ((const void *)AM2_IMAGE(a))

/* The eight that exist whether a script mentions them or not. Each is declared
 * into the name table on the spot -- ScriptNameUid creates the entry when the
 * lookup misses -- so a mission can test `greenwins` without declaring it. */
static const struct {
    uint32_t name;      /* image address of the literal */
    uint32_t handler;
    int32_t  army;
} kWinConditions[] = {
    { ADDR_NAME_GREENWINS,     ADDR_EVT_ARMY_WINS, 0 },
    { ADDR_NAME_TANWINS,       ADDR_EVT_ARMY_WINS, 1 },
    { ADDR_NAME_BLUEWINS,      ADDR_EVT_ARMY_WINS, 2 },
    { ADDR_NAME_GREYWINS,      ADDR_EVT_ARMY_WINS, 3 },
    { ADDR_NAME_GREENTEAMWINS, ADDR_EVT_TEAM_WINS, 0 },
    { ADDR_NAME_TANTEAMWINS,   ADDR_EVT_TEAM_WINS, 1 },
    { ADDR_NAME_BLUETEAMWINS,  ADDR_EVT_TEAM_WINS, 2 },
    { ADDR_NAME_GREYTEAMWINS,  ADDR_EVT_TEAM_WINS, 3 },
};

/* The three that are not named at all: nothing looks them up by string, so
 * each takes a fresh uid and the uid is parked in a global for whoever raises
 * it later. */
static const struct {
    uint32_t handler;
    uint32_t uidslot;
} kRuleEvents[] = {
    { ADDR_EVT_RULE_A, ADDR_RULE_UID_A },
    { ADDR_EVT_RULE_B, ADDR_RULE_UID_B },
    { ADDR_EVT_RULE_C, ADDR_RULE_UID_C },
};

void __cdecl DeclareRuleVars(void)
{
    /* Read before the clear, not after, because that is the order the original
     * uses. The teardown walks the registration table and not this list, so
     * nothing observable turns on it -- but the two globals are one line apart
     * and getting the order from the disassembly costs nothing. */
    AM2_ScriptCond *cond = kScriptConditions;

    orig_event_clear_all();

    for (uint32_t i = 0; i < sizeof kWinConditions / sizeof kWinConditions[0]; i++)
        orig_event_register(0, ScriptNameUid((const char *)AM2_IMAGE(
                                   kWinConditions[i].name)),
                            0, kImageFn(kWinConditions[i].handler),
                            (const void *)(uintptr_t)kWinConditions[i].army, 0);

    for (uint32_t i = 0; i < sizeof kRuleEvents / sizeof kRuleEvents[0]; i++) {
        int32_t uid = orig_take_uid();

        *(int32_t *)AM2_IMAGE(kRuleEvents[i].uidslot) = uid;
        orig_event_register(0, uid, 0, kImageFn(kRuleEvents[i].handler), 0, 0);
    }

    /* One registration per event term of every `if`, with the condition itself
     * as the callback's argument -- which is how a fired event finds the
     * actions to run. `timeabsolute` is the exception: it has no event terms
     * at all, so it invents a uid, registers that, and announces it straight
     * away with the absolute time as the payload. */
    for (; cond; cond = (AM2_ScriptCond *)cond->next) {
        if (cond->kind == AM2_IF_TIMEABSOLUTE) {
            int32_t uid = orig_take_uid();

            orig_event_register(0, uid, 0, kImageFn(ADDR_EVT_CONDITION),
                                cond, 0);
            orig_event_notify(0, uid, 0, 0, 0, 0, 0, cond->number, 1, 0);
            continue;
        }

        for (int32_t i = 0; i < cond->nevents; i++)
            orig_event_register(cond->events[i].a, cond->events[i].b,
                                cond->events[i].c,
                                kImageFn(ADDR_EVT_CONDITION), cond, 0);
    }
}

int event_install(void)
{
    return patch_replace(ADDR_DECLARE_RULE_VARS, (const void *)DeclareRuleVars,
                         "DeclareRuleVars", 3);
}
