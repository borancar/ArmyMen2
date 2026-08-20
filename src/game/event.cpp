/* event.cpp -- see event.h. */
#include <stdint.h>

#include <string.h>

#include "crt.h"
#include "event.h"
#include "image.h"
#include "script.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_event_notify_fn)(int32_t, int32_t, int32_t,
                                               int32_t, int32_t, int32_t,
                                               int32_t, int32_t, int32_t,
                                               int32_t);

#define orig_event_notify    (*(am2_event_notify_fn)ADDR_EVENT_NOTIFY)

#define kScriptConditions (*(AM2_ScriptCond **)AM2_IMAGE(ADDR_SCRIPT_CONDITIONS))

/* ---- the registration table -------------------------------------------
 *
 * Nine buckets, and the count is read out of the teardown's loop bound rather
 * than guessed: it walks to ADDR_SCRIPT_CONDITIONS, which is the next global.
 *
 * A bucket is a chain of entries keyed on a PAIR, and each entry a chain of
 * handlers. Registering the same pair twice therefore adds a second handler to
 * one entry rather than a second entry -- which is what makes an event with
 * several listeners work, and what the lookup below has to preserve. */
typedef struct AM2_EventHandler {
    const void              *fn;
    void                    *arg;
    int32_t                  owns;   /* free `arg` too, in the teardown */
    struct AM2_EventHandler *next;
} AM2_EventHandler;

typedef struct AM2_EventEntry {
    int32_t                  key0;
    int32_t                  key1;
    AM2_EventHandler        *handlers;
    struct AM2_EventEntry   *next;
} AM2_EventEntry;

#define kEventTable ((AM2_EventEntry **)AM2_IMAGE(ADDR_EVENT_TABLE))
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

/* 0x0041EE70, 19 call sites.
 *
 * A key0 of -2 registers nothing at all and is not an error -- an `if` whose
 * event term carries it simply has no listener. The bucket index is used
 * unchecked, so it is the caller's business that it is one of the nine.
 *
 * The entry is found or made first, then the handler is pushed onto its list.
 * Both allocations are the game's, because the teardown frees them with the
 * game's free. */
void __cdecl EventRegister(int32_t bucket, int32_t key0, int32_t key1,
                           const void *fn, void *arg, int32_t owns)
{
    if (key0 == AM2_EVENT_NO_KEY)
        return;

    AM2_EventEntry *e = kEventTable[bucket];

    while (e && !(e->key0 == key0 && e->key1 == key1))
        e = e->next;

    if (!e) {
        e = (AM2_EventEntry *)am2_malloc(sizeof(AM2_EventEntry));
        memset(e, 0, sizeof *e);
        e->next     = kEventTable[bucket];
        e->handlers = 0;
        e->key0     = key0;
        e->key1     = key1;
        kEventTable[bucket] = e;
    }

    AM2_EventHandler *h = (AM2_EventHandler *)am2_malloc(sizeof(AM2_EventHandler));

    memset(h, 0, sizeof *h);
    h->fn       = fn;
    h->arg      = arg;
    h->owns     = owns;
    h->next     = e->handlers;
    e->handlers = h;
}

/* 0x004223D0, 2 call sites. Empty every bucket.
 *
 * `owns` is what decides whether the handler's argument goes with it. Nothing
 * DeclareRuleVars registers sets it, so the `if` records that pass over the
 * conditions rather than freeing them -- they belong to the script. */
void __cdecl EventClearAll(void)
{
    for (int32_t b = 0; b < AM2_EVENT_BUCKETS; b++) {
        AM2_EventEntry *e = kEventTable[b];

        while (e) {
            AM2_EventEntry   *nexte = e->next;
            AM2_EventHandler *h     = e->handlers;

            while (h) {
                AM2_EventHandler *nexth = h->next;

                if (h->owns)
                    am2_free(h->arg);
                am2_free(h);
                h = nexth;
            }
            am2_free(e);
            e = nexte;
        }
        kEventTable[b] = 0;
    }
}

void __cdecl DeclareRuleVars(void)
{
    /* Read before the clear, not after, because that is the order the original
     * uses. The teardown walks the registration table and not this list, so
     * nothing observable turns on it -- but the two globals are one line apart
     * and getting the order from the disassembly costs nothing. */
    AM2_ScriptCond *cond = kScriptConditions;

    EventClearAll();

    for (uint32_t i = 0; i < sizeof kWinConditions / sizeof kWinConditions[0]; i++)
        EventRegister(0, ScriptNameUid((const char *)AM2_IMAGE(
                                   kWinConditions[i].name)),
                            0, kImageFn(kWinConditions[i].handler),
                            (void *)(uintptr_t)kWinConditions[i].army, 0);

    for (uint32_t i = 0; i < sizeof kRuleEvents / sizeof kRuleEvents[0]; i++) {
        int32_t uid = AllocUid();

        *(int32_t *)AM2_IMAGE(kRuleEvents[i].uidslot) = uid;
        EventRegister(0, uid, 0, kImageFn(kRuleEvents[i].handler), 0, 0);
    }

    /* One registration per event term of every `if`, with the condition itself
     * as the callback's argument -- which is how a fired event finds the
     * actions to run. `timeabsolute` is the exception: it has no event terms
     * at all, so it invents a uid, registers that, and announces it straight
     * away with the absolute time as the payload. */
    for (; cond; cond = (AM2_ScriptCond *)cond->next) {
        if (cond->kind == AM2_IF_TIMEABSOLUTE) {
            int32_t uid = AllocUid();

            EventRegister(0, uid, 0, kImageFn(ADDR_EVT_CONDITION),
                                cond, 0);
            orig_event_notify(0, uid, 0, 0, 0, 0, 0, cond->number, 1, 0);
            continue;
        }

        for (int32_t i = 0; i < cond->nevents; i++)
            EventRegister(cond->events[i].a, cond->events[i].b,
                                cond->events[i].c,
                                kImageFn(ADDR_EVT_CONDITION), cond, 0);
    }
}

int event_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_DECLARE_RULE_VARS, (const void *)DeclareRuleVars,
                        "DeclareRuleVars", 3);
    rc |= patch_replace(ADDR_EVENT_REGISTER, (const void *)EventRegister,
                        "EventRegister", 19);
    rc |= patch_replace(ADDR_EVENT_CLEAR_ALL, (const void *)EventClearAll,
                        "EventClearAll", 2);
    return rc;
}
