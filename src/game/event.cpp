/* event.cpp -- see event.h. */
#include <stdint.h>

#include <string.h>

#include "crt.h"
#include "event.h"
#include "image.h"
#include "objtable.h"
#include "objtype.h"
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

/* ---- the object setters ------------------------------------------------ */

/* LookupByUID and ObjIsTypeIn238 are both reconstructed, so these call them
 * directly rather than through the image. Reaching for an orig_ macro here was
 * caught by tools/checkseams.py, which exists for exactly that. */
#define g_evtMarks ((int32_t *)(uintptr_t)ADDR_EVT_MARKS)

/* The two that check the type. LookupByUID's answer goes straight into
 * ObjIsTypeIn238, which returns 0 for null, so the null case is covered by the
 * type test rather than by a test of its own. */
void __cdecl EvtSetModeF0(uint32_t uid, int32_t value)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    obj = LookupByUID(uid);
    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return;
    *(int32_t *)((uint8_t *)obj + 0xF0) = value;
}

void __cdecl EvtSetMode94(uint32_t uid, int32_t value)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    obj = LookupByUID(uid);
    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return;
    *(int32_t *)((uint8_t *)obj + 0x94) = value;
}

/* Sets or clears bits 4 and 11 together in the flags word at +8 -- the same
 * word ObjFieldA reads three bits out of at bit 18. No null check: see the
 * note in event.h. */
#define AM2_EVT_FLAG810 0x810u

void __cdecl EvtSetFlag810(uint32_t uid, int32_t on)
{
    uint32_t *flags;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    flags = (uint32_t *)((uint8_t *)LookupByUID(uid) + 8);
    if (on)
        *flags |= AM2_EVT_FLAG810;
    else
        *flags &= ~AM2_EVT_FLAG810;
}

/* +0x10 is AM2_Object.owner, which objtable.h reads with movsx as int8_t. */
void __cdecl EvtSetOwner(uint32_t uid, int8_t owner)
{
    if (uid < AM2_UID_COUNTER_MIN)
        return;
    *((int8_t *)LookupByUID(uid) + 0x10) = owner;
}

/* The one that checks the pointer and not the uid. */
void __cdecl EvtSetByte40(uint32_t uid, int8_t value)
{
    void *obj = LookupByUID(uid);

    if (!obj)
        return;
    *((int8_t *)obj + 0x40) = value;
}

/* The clamp is applied to the caller's value before the object is looked up,
 * so it happens even when the store does not. */
void __cdecl EvtSetWord60(uint32_t uid, int32_t value)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    if (value > 0x7FFF)
        value = 0x7FFF;
    obj = LookupByUID(uid);
    if (!obj)
        return;
    *(int16_t *)((uint8_t *)obj + 0x60) = (int16_t)value;
}

/* script.cpp's ActAiMode is where these codes come from. Only defend needs
 * naming here; the others pass through untouched. */
#define AM2_AI_MODE_DEFEND 7

void __cdecl EvtSetAiMode(uint32_t uid, int32_t mode)
{
    uint8_t *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    obj = (uint8_t *)LookupByUID(uid);
    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return;

    *(int32_t *)(obj + 0xE8) = *(int32_t *)(obj + 0xE4);
    *(int32_t *)(obj + 0xE4) = mode;

    if (mode != AM2_AI_MODE_DEFEND)
        return;
    /* Tests sixteen bits and writes thirty-two -- the original's asymmetry,
     * not a transcription slip. */
    if (*(int16_t *)(obj + 0xB4) != 0)
        return;
    *(int32_t *)(obj + 0xB4) = *(const int32_t *)(obj + 0x12);
}

/* PlayDynamicSound is reconstructed, in win32/audio.cpp with the rest of the
 * sound code. Declared here rather than by including that header because
 * event.cpp is on the flat side of the split and must name no Win32 or COM
 * type -- the same reason script.cpp declares PreloadSprite itself. */
extern "C" void __cdecl PlayDynamicSound(const char *name, int32_t loop,
                                         int32_t unused, int32_t x, int32_t y,
                                         int32_t slot, int32_t priority,
                                         uint32_t owner);

void __cdecl EvtPlaySoundAt(const char *name, uint32_t point, int32_t slot,
                            int32_t priority, int32_t loop)
{
    PlayDynamicSound(name, loop, 0, (int32_t)(point & 0xFFFFu),
                     (int32_t)(point >> 16), slot, priority, 0);
}

void __cdecl EvtPlaySoundOn(const char *name, uint32_t owner, int32_t slot,
                            int32_t priority, int32_t loop)
{
    PlayDynamicSound(name, loop, 0, 0, 0, slot, priority, owner);
}

/* The original re-reads the list head after every free, which is the compiler
 * assuming free might touch the global. It cannot, and the value is the same
 * each time, so a local reads identically. The head IS updated once per
 * record, before moving on, so a teardown interrupted part way leaves the
 * global pointing at the first record it has not freed. */
void __cdecl FreeScriptConditions(void)
{
    AM2_ScriptCond *cond = kScriptConditions;

    if (!cond) {
        kScriptConditions = NULL;
        return;
    }

    while (cond) {
        AM2_ScriptCond *next = (AM2_ScriptCond *)cond->next;

        if (cond->events)
            am2_free(cond->events);
        if (cond->tests)
            am2_free(cond->tests);
        if (cond->actions)
            am2_free(cond->actions);
        am2_free(cond);

        kScriptConditions = next;
        cond = next;
    }
    kScriptConditions = NULL;
}

void __cdecl EvtMarkSet(int32_t row, int32_t col)
{
    g_evtMarks[col + row * 4] = 1;
}

void __cdecl EvtMarkClear(int32_t row, int32_t col)
{
    g_evtMarks[col + row * 4] = 0;
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
    rc |= patch_replace(ADDR_EVT_SET_MODE_F0, (const void *)EvtSetModeF0,
                        "EvtSetModeF0", 2);
    rc |= patch_replace(ADDR_EVT_SET_MODE_94, (const void *)EvtSetMode94,
                        "EvtSetMode94", 2);
    rc |= patch_replace(ADDR_EVT_SET_FLAG810, (const void *)EvtSetFlag810,
                        "EvtSetFlag810", 2);
    rc |= patch_replace(ADDR_EVT_SET_OWNER, (const void *)EvtSetOwner,
                        "EvtSetOwner", 2);
    rc |= patch_replace(ADDR_EVT_SET_BYTE40, (const void *)EvtSetByte40,
                        "EvtSetByte40", 2);
    rc |= patch_replace(ADDR_FREE_SCRIPT_CONDS, (const void *)FreeScriptConditions,
                        "FreeScriptConditions", 0);
    rc |= patch_replace(ADDR_EVT_PLAY_SOUND_AT, (const void *)EvtPlaySoundAt,
                        "EvtPlaySoundAt", 5);
    rc |= patch_replace(ADDR_EVT_PLAY_SOUND_ON, (const void *)EvtPlaySoundOn,
                        "EvtPlaySoundOn", 5);
    rc |= patch_replace(ADDR_EVT_SET_WORD60, (const void *)EvtSetWord60,
                        "EvtSetWord60", 2);
    rc |= patch_replace(ADDR_EVT_SET_AI_MODE, (const void *)EvtSetAiMode,
                        "EvtSetAiMode", 2);
    rc |= patch_replace(ADDR_EVT_MARK_SET, (const void *)EvtMarkSet,
                        "EvtMarkSet", 2);
    rc |= patch_replace(ADDR_EVT_MARK_CLEAR, (const void *)EvtMarkClear,
                        "EvtMarkClear", 2);
    return rc;
}
