/* event.cpp -- see event.h. */
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "crt.h"
#include "event.h"
#include "image.h"
#include "misc.h"      /* FilterMatches */
#include "armymsg.h"   /* SendGamePause */
#include "gamedir.h"   /* SetGameDir */
#include "objscript.h" /* AM2_ObjScript, kObjScripts */
#include "scriptint.h"
#include "objtable.h"
#include "dist.h"     /* AM2_Point, AngleBetween */
#include "map.h"      /* TileOfPoint */
#include "item.h"   /* UidOnWire */
#include "objtype.h"
#include "msgslot.h"  /* CommMustBroadcast -- the multiplayer guard */
#include "savetag.h"
#include "script.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* ---- what stays in the original image --------------------------------- */

typedef void    (__cdecl *am2_event_notify_fn)(int32_t, int32_t, int32_t,
                                               int32_t, int32_t, int32_t,
                                               int32_t, int32_t, int32_t,
                                               int32_t);


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
            EventNotify(0, uid, 0, 0, 0, 0, 0, cond->number, 1, 0);
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
#define g_allyMatrix ((int32_t *)(uintptr_t)ADDR_ALLY_MATRIX)

void __cdecl EvtSetField540(uint32_t uid, int32_t value)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    obj = LookupByUID(uid);
    if (!ObjIsType2((const AM2_Object *)obj))
        return;
    *(int32_t *)((uint8_t *)obj + 0x540) = value;
}

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

/* The same store one type further along. LookupType3ByUID does the lookup and
 * the type test together, so the null check here covers both a uid that is not
 * registered and one that is registered as something else. */
void __cdecl EvtSetByte530(uint32_t uid, int8_t value)
{
    AM2_Object *obj;

    obj = LookupType3ByUID(uid);
    if (!obj)
        return;
    *(int8_t *)((uint8_t *)obj + 0x530) = value;
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

void __cdecl EventDefaultName(int32_t kind, int32_t number, char *out)
{
    switch (kind) {
    case AM2_EVT_CONTROL:
        sprintf(out, "unnamed Event_Control %d", number);
        break;
    case 1:
        /* Nothing produces kind 1, and this is the only place that says so in
         * code rather than by omission: no name at all, not a placeholder. */
        out[0] = '\0';
        break;
    case AM2_EVT_PADOFF:
        sprintf(out, "unnamed Event_PadDeactivated %d", number);
        break;
    case AM2_EVT_PADON:
        sprintf(out, "unnamed Event_PadActivated %d", number);
        break;
    case AM2_EVT_KILLED:
        sprintf(out, "unnamed Event_ItemDestroyed %d", number);
        break;
    case AM2_EVT_HIT:
        sprintf(out, "unnamed Event_ItemHit %d", number);
        break;
    case AM2_EVT_HEALED:
        sprintf(out, "unnamed Event_ItemHealed %d", number);
        break;
    case AM2_EVT_PICKEDUP:
        sprintf(out, "unnamed Event_ItemPickedup %d", number);
        break;
    case AM2_EVT_DROPPED:
        sprintf(out, "unnamed Event_ItemDropped %d", number);
        break;
    default:
        /* Both values, in the other order -- kind first here, number first in
         * every arm above. */
        sprintf(out, "event type %d num %d", kind, number);
        break;
    }
}

/* The game's own fwrite: the FILE was opened by the game's CRT, so it must be
 * the same CRT that writes to it -- the reason crt.h exists. */
/* Forward-declared rather than including win32/sprite.h: that header names
 * LPDIRECTDRAWSURFACE and this module is on the flat side. FreeBitmap's own
 * signature does not, which is what makes the declaration legal here -- the
 * same reason script.cpp forward-declares PreloadSprite. */
extern "C" void __cdecl FreeBitmap(void **pp);

#define orig_fwrite (*(am2_fwrite_fn)ADDR_FWRITE)

void __cdecl SaveScriptCond(am2_FILE *fp, const AM2_ScriptCond *cond)
{
    int32_t i;

    orig_fwrite(cond, 0x30, 1, fp);

    for (i = 0; i < cond->nevents; i++)
        orig_fwrite((const uint8_t *)cond->events + i * 0x10, 0x10, 1, fp);

    for (i = 0; i < cond->nactions; i++) {
        const uint8_t *act = (const uint8_t *)cond->actions + i * 0x48;
        const char    *text = *(char *const *)(act + 0x30);
        uint8_t        copy[0x48];
        int32_t        len = text ? (int32_t)strlen(text) : 0;

        memcpy(copy, act, sizeof copy);
        /* The pointer slot carries the length instead. */
        *(int32_t *)(copy + 0x30) = len;

        orig_fwrite(copy, 0x48, 1, fp);
        if (len > 0)
            orig_fwrite(text, (size_t)len, 1, fp);
    }

    for (i = 0; i < cond->ntests; i++)
        orig_fwrite((const uint8_t *)cond->tests + i * 0x1C, 0x1C, 1, fp);
}

#define orig_fread  (*(am2_fread_fn)ADDR_FREAD)
#define orig_malloc (*(am2_malloc_fn)ADDR_GAME_MALLOC)

#define kScriptContext (*(AM2_ScriptCtx *)AM2_IMAGE(ADDR_SCRIPT_CONTEXT))

void __cdecl LoadScriptCond(am2_FILE *fp, AM2_ScriptCond *cond)
{
    char    buf[0x800];
    int32_t i;

    /* Cleared before the read, not after: the read covers 0x00..0x2F and
     * `next` sits at +0x30, just past it. */
    cond->next = NULL;
    orig_fread(cond, 0x30, 1, fp);

    if (cond->nevents > 0)
        cond->events = (AM2_ScriptEvent *)orig_malloc((size_t)cond->nevents * 0x10);
    for (i = 0; i < cond->nevents; i++)
        orig_fread((uint8_t *)cond->events + i * 0x10, 0x10, 1, fp);

    if (cond->nactions > 0)
        cond->actions = (AM2_ScriptAction *)orig_malloc((size_t)cond->nactions * 0x48);
    for (i = 0; i < cond->nactions; i++) {
        uint8_t *act = (uint8_t *)cond->actions + i * 0x48;
        int32_t  len;

        orig_fread(act, 0x48, 1, fp);
        len = *(const int32_t *)(act + 0x30);
        if (len > 0) {
            int32_t at = kScriptContext.count;

            orig_fread(buf, (size_t)len, 1, fp);
            buf[len] = '\0';
            /* Kind 5 is the one that owns a malloc'd copy, so the token list
             * becomes the string's owner. `at` is read before the append. */
            ScriptAddToken(&kScriptContext, AM2_TOKEN_STRING, buf, 0);
            *(char **)(act + 0x30) = (char *)kScriptContext.tokens[at].value;
        }
    }

    if (cond->ntests > 0)
        cond->tests = (AM2_ScriptTest *)orig_malloc((size_t)cond->ntests * 0x1C);
    for (i = 0; i < cond->ntests; i++)
        orig_fread((uint8_t *)cond->tests + i * 0x1C, 0x1C, 1, fp);
}

#define kPads ((AM2_Pad *)AM2_IMAGE(ADDR_PADS))

int32_t __cdecl LoadEventSection(am2_FILE *fp)
{
    int32_t tag, unused, key, kind, idx;

    if (!CheckSaveTag(fp, AM2_SAVETAG_EVENT,
                      (const char *)AM2_IMAGE(ADDR_STR_EVENT_CPP), 0xCCA))
        return 0;

    orig_fread(&tag, 4, 1, fp);
    if (tag != (int32_t)AM2_SAVE_RECORD_MARK)
        return 1;

    do {
        /* Three dwords. The first is read and dropped -- see event.h. */
        orig_fread(&unused, 4, 1, fp);
        orig_fread(&key, 4, 1, fp);
        orig_fread(&kind, 4, 1, fp);
        (void)unused;

        if (kind == (int32_t)AM2_EVTSAVE_PAD_A ||
            kind == (int32_t)AM2_EVTSAVE_PAD_B) {
            AM2_Pad *pad;

            orig_fread(&idx, 4, 1, fp);
            pad = &kPads[idx];
            *(int32_t *)pad->rest = key;        /* +0x28 */
            EventRegister(0, key, 0,
                          (const void *)(uintptr_t)
                          (kind == (int32_t)AM2_EVTSAVE_PAD_A
                           ? ADDR_EVT_PAD_HANDLER_A : ADDR_EVT_PAD_HANDLER_B),
                          pad, 0);
        } else if (kind == (int32_t)AM2_EVTSAVE_OWNED) {
            void *rec = orig_malloc(0x10);

            orig_fread(rec, 0x10, 1, fp);
            /* owns = 1: the table's teardown frees this one. */
            EventRegister(1, key, 0,
                          (const void *)(uintptr_t)ADDR_EVT_RECORD_HANDLER,
                          rec, 1);
        }

        orig_fread(&tag, 4, 1, fp);
    } while (tag == (int32_t)AM2_SAVE_RECORD_MARK);

    return 1;
}

int32_t __cdecl SaveEventSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_EVENT);

    for (int32_t b = 0; b < AM2_EVENT_BUCKETS; b++) {
        for (AM2_EventEntry *e = kEventTable[b]; e; e = e->next) {
            for (AM2_EventHandler *h = e->handlers; h; h = h->next) {
                uint32_t kind;

                /* Tested in this order, and the two pad arms want an argument
                 * while the owned one does not. A pad handler with a null
                 * argument falls through every arm and is skipped. */
                if (h->fn == kImageFn(ADDR_EVT_PAD_HANDLER_A) && h->arg)
                    kind = AM2_EVTSAVE_PAD_A;
                else if (h->fn == kImageFn(ADDR_EVT_PAD_HANDLER_B) && h->arg)
                    kind = AM2_EVTSAVE_PAD_B;
                else if (h->fn == kImageFn(ADDR_EVT_RECORD_HANDLER))
                    kind = AM2_EVTSAVE_OWNED;
                else
                    continue;

                WriteSaveTag(fp, AM2_SAVE_RECORD_MARK);
                WriteSaveTag(fp, (uint32_t)b);       /* the loader drops this */
                WriteSaveTag(fp, (uint32_t)e->key0);
                WriteSaveTag(fp, kind);

                if (kind == AM2_EVTSAVE_OWNED)
                    orig_fwrite(h->arg, 0x10, 1, fp);
                else
                    WriteSaveTag(fp,
                        (uint32_t)((AM2_Pad *)h->arg - kPads));
            }
        }
    }

    WriteSaveTag(fp, AM2_SAVE_TAG_END);
    return 1;
}

int32_t __cdecl LoadScriptConditions(am2_FILE *fp)
{
    int32_t tag;

    FreeScriptConditions();

    if (!CheckSaveTag(fp, AM2_SAVETAG_CONDS,
                      (const char *)AM2_IMAGE(ADDR_STR_EVENT_CPP), 0x173))
        return 0;

    orig_fread(&tag, 4, 1, fp);
    while (tag == (int32_t)AM2_SAVE_RECORD_MARK) {
        AM2_ScriptCond *cond = (AM2_ScriptCond *)orig_malloc(sizeof *cond);

        memset(cond, 0, sizeof *cond);
        LoadScriptCond(fp, cond);
        /* Prepended, so the list ends up in reverse file order. */
        cond->next = kScriptConditions;
        kScriptConditions = cond;

        orig_fread(&tag, 4, 1, fp);
    }

    /* Reached from the loop AND from the wrong-marker exit above, so the
     * registrations are rebuilt even for an empty section. */
    DeclareRuleVars();
    return 1;
}

int32_t __cdecl SaveScriptConditions(am2_FILE *fp)
{
    const AM2_ScriptCond *cond;

    WriteSaveTag(fp, AM2_SAVETAG_CONDS);

    /* `next` is a void * in the struct -- see script.h, where the field is
     * typed from its offset rather than from a declaration. */
    for (cond = kScriptConditions; cond;
         cond = (const AM2_ScriptCond *)cond->next) {
        WriteSaveTag(fp, AM2_SAVE_RECORD_MARK);
        SaveScriptCond(fp, cond);
    }

    WriteSaveTag(fp, AM2_SAVE_TAG_END);
    return 1;
}

#define kEventBlock ((void *)(uintptr_t)AM2_IMAGE(ADDR_EVENT_BLOCK))

int32_t __cdecl SaveEventBlock(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_EVENT_BLOCK);
    WriteSaveTag(fp, AM2_EVENT_BLOCK_SIZE);
    orig_fwrite(kEventBlock, AM2_EVENT_BLOCK_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadEventBlock(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_EVENT_BLOCK,
                      (const char *)AM2_IMAGE(ADDR_STR_EVENT_CPP), 0xD9))
        return 0;

    /* The size, through the same helper, one source line later. */
    if (!CheckSaveTag(fp, AM2_EVENT_BLOCK_SIZE,
                      (const char *)AM2_IMAGE(ADDR_STR_EVENT_CPP), 0xDB))
        return 0;

    orig_fread(kEventBlock, AM2_EVENT_BLOCK_SIZE, 1, fp);
    return 1;
}

void __cdecl EvtSetAllied(int32_t a, int32_t b)
{
    g_allyMatrix[b + a * 4] = 1;
}

void __cdecl EvtClearAllied(int32_t a, int32_t b)
{
    g_allyMatrix[b + a * 4] = 0;
}

/* ----------------------------------------------- script bitmaps ---- */

/* 0x00420060. Point a named object at a different sprite frame.
 *
 * Its own three error strings name it and describe every exit, which is most
 * of why it is legible at all: the name must be type 2, the uid behind it must
 * resolve to a valid object, and ChangeObjectFrame must accept the index.
 *
 * Worth a note on that type test. orig.h documents type 2 as
 * AM2_NAME_TYPE_REF, "a name used before declaring", while this function's
 * message for anything else is "which is not an item". Both readings survive
 * together -- a script naming a map object it did not declare gets a forward
 * reference, and those are exactly the names that carry an object uid -- but
 * the two descriptions are not obviously the same thing, and only one of them
 * came from a body. Recorded rather than reconciled.
 *
 * All three failures log and return; none is fatal, and the object is simply
 * left alone. */
void __cdecl ScriptSetObjBitmap(int32_t nameidx, int32_t frame)
{
    const AM2_ScriptName *e = am2_script_name(nameidx);
    void                 *obj;

    if (e->type != AM2_NAME_TYPE_REF) {
        orig_log("ERROR: ScriptSetObjBitmap was called with %s which is not "
                 "an item\n", e->name);
        return;
    }

    obj = LookupByUID((uint32_t)e->value);

    if (!ObjIsItem((const AM2_Object *)obj)) {
        orig_log("ERROR: ScriptSetObjBitmap was called with %s which is not "
                 "a valid object\n", e->name);
        return;
    }

    if (!ChangeObjectFrame(obj, frame, 1))
        orig_log("Error: ChangeObjectFrame returned false when passed %s "
                 "with index %d\n", e->name, frame);
}

/* ------------------------------------------------ event messages ---- */

/* The event logging switch, a field of the comm object. The logger it gates is
 * stubbed to `ret` in this build, so the whole branch is inert -- reproduced
 * because the call sites are what carry the format strings that named these
 * functions in the first place. */
#define kEventDebug \
    (*(const int32_t *)((const uint8_t *)*(void **)AM2_IMAGE(ADDR_COMM_OBJECT) \
                        + COMM_OFF_EVENT_DEBUG))


/* Both stay original and are reached by address. ArmyMessageSend is the whole
 * game's transport -- 20 callers -- and EventTriggerImmediate is the local
 * raise that Receive feeds. */
/* What a registered handler is called as: the seven event fields the trigger
 * received, then the handler's OWN argument from its registration. */
typedef void (__cdecl *AM2_EventHandlerFn)(int32_t type, int32_t num1,
                                           uint32_t uid1, int32_t maskA,
                                           int32_t num2, uint32_t uid2,
                                           int32_t maskB, void *arg);

/* 0x0041F150. Pack an event into a 40-byte message and send it.
 *
 * The uids go through UidOnWire, which is the identity on this target and a
 * byte-order hook on any other. The log prints five of the eight arguments and
 * skips the two masks entirely, which is why they carried positional names
 * until EventTriggerImmediate handed them to FilterMatches. */
void __cdecl EventMessageSend(int32_t type, int32_t num1, uint32_t uid1,
                              int32_t maskA, int32_t num2, uint32_t uid2,
                              int32_t maskB, int32_t removeevent)
{
    AM2_EventMsg msg;

    if (kEventDebug)
        orig_log("EventMessageSend: type=%d, num1=%d, uid1=%x, num2=%d, "
                 "uid2=%x\n", type, num1, uid1, num2, uid2);

    msg.len         = 0x28;
    msg.kind        = AM2_ARMY_MSG_EVENT;
    msg.uid         = 0;
    msg.type        = (uint8_t)type;
    msg.num1        = num1;
    msg.uid1        = UidOnWire(uid1);
    msg.maskA       = maskA;
    msg.num2        = num2;
    msg.uid2        = UidOnWire(uid2);
    msg.maskB       = maskB;
    msg.removeevent = removeevent;

    ArmyMessageSend(&msg);
}

/* 0x0041F320. Unpack one and raise it locally.
 *
 * `type` is read back with a SIGNED byte load, where the sender narrowed an
 * int32 to a byte -- so a type above 127 arrives negative. Reproduced.
 *
 * The second log line only appears when the event has a name: the buffer
 * EventDefaultName fills is tested at its first byte, and kind 1 is the one
 * that comes back empty. The buffer is the original's 256 bytes. */
void __cdecl EventMessageReceive(const AM2_EventMsg *msg)
{
    if (kEventDebug) {
        char name[256];

        orig_log("EventMessageReceive: type=%d, num1=%d, uid1=%x, num2=%d, "
                 "uid2=%x\n", (int32_t)(int8_t)msg->type, msg->num1,
                 msg->uid1, msg->num2, msg->uid2);

        EventDefaultName((int32_t)(int8_t)msg->type, msg->num1, name);

        if (name[0])
            orig_log("    Event receive %s remove %d \n", name,
                     msg->removeevent);
    }

    EventTriggerImmediate((int32_t)(int8_t)msg->type, msg->num1,
                                 UidOnWire(msg->uid1), msg->maskA,
                                 msg->num2, UidOnWire(msg->uid2),
                                 msg->maskB, msg->removeevent, 1);
}


typedef int32_t (__cdecl *AM2_SaveGameFn)(const char *name);
#define orig_save_game      ((AM2_SaveGameFn)(uintptr_t)ADDR_SAVE_GAME)

/* 0x00444EF0, two callers. Raise the level's own "startupN" script event, then
 * autosave the mission.
 *
 * The event name is built rather than looked up: "startup" and the level
 * index, with the index forced to 1 when it is not positive -- so a level that
 * never set one still fires `startup1`. If the script declares no such name,
 * ScriptNameUid answers zero or less and nothing is raised; a mission with no
 * startup block is silent rather than an error.
 *
 * The autosave has three separate guards and any of them cancels it: a level
 * id that is not positive, a zero first byte of ADDR_GAMEPROC_BLOCK, and
 * ADDR_WIN_ENABLED being set. That last is the interesting one -- the game
 * does not autosave once winning is enabled.
 *
 * The filename is copied into ADDR_GAMEPROC_STR_B afterwards, which is inside
 * the block the savegame itself writes out, so the name of the last autosave
 * survives into the next save. The original does that copy with `repne scasb`
 * and `rep movsd`, which is just strcpy after strlen and is written as one.
 *
 * Measured, and the autosave is attributed rather than assumed. The counter
 * reads 1 per mission on either drive. On Boot Camp no .sav is written at all,
 * so one of the three guards cancels it there and WHICH one is not
 * established. On the campaign it is written -- and to be sure that was ours
 * rather than the A/B's other half, the existing file was backdated to the
 * year 2020 and a single patched run started: it came back stamped with the
 * current minute at the same 176,850 bytes.
 *
 * Backdating an artefact and running one side is worth remembering. A file
 * mtime moving during a two-sided A/B says only that somebody wrote it. */
void __cdecl MissionStartup(void)
{
    char    name[AM2_MISSION_NAME_BYTES];
    int32_t level = *(const int32_t *)(uintptr_t)ADDR_LEVEL_INDEX;
    int32_t id;

    if (level <= 0)
        level = 1;

    sprintf(name, (const char *)AM2_IMAGE(ADDR_STR_STARTUP_FMT), level);
    id = ScriptNameUid(name);
    if (id > 0)
        EventNotify(0, id, 0, 0, 0, 0, 0, 0, 1, 0);

    *(int32_t *)(uintptr_t)ADDR_SCRIPT_STATE_FLAG = 1;

    if (*(const int32_t *)(uintptr_t)ADDR_LEVEL_ID <= 0)
        return;
    if (!*(const uint8_t *)(uintptr_t)ADDR_GAMEPROC_BLOCK)
        return;
    if (*(const int32_t *)(uintptr_t)ADDR_WIN_ENABLED)
        return;

    sprintf(name, (const char *)AM2_IMAGE(ADDR_STR_MISSION_SAV_FMT),
            *(const int32_t *)(uintptr_t)ADDR_LEVEL_ID,
            *(const int32_t *)(uintptr_t)ADDR_LEVEL_INDEX);
    orig_save_game(name);
    strcpy((char *)(uintptr_t)ADDR_GAMEPROC_STR_B, name);
}

/* 0x0041E950, one caller, on the per-frame path. Fire every timer that has
 * come due, and no more often than once every 100 ms.
 *
 * The gate is a SUBTRACTION against the last sweep rather than a comparison
 * with a deadline, so it survives the clock wrapping; the timers' own due
 * times are compared directly and do not.
 *
 * `removeevent` is set on the LAST fire -- the ninth argument is
 * `count == 1`, evaluated before the decrement -- so the event registration is
 * torn down by the same call that delivers the final tick rather than by a
 * separate pass.
 *
 * A slot is freed by zeroing its id, which is what the scan skips on, and the
 * live count is decremented with it. A repeating timer instead has its next
 * due time computed from NOW plus its period, not from the due time it just
 * met, so a timer whose sweep was late does not try to catch up.
 *
 * Measured, and the obvious counter would have misled. TimerTick reads 23,901
 * against ComposeFrame's 24,056, so the sweep is per-frame -- but CreateTimer
 * reads 0 on the same run, which looks like "no timers exist" and is worth
 * nothing: its only caller in the image is our own EventTriggerDelayed,
 * calling by name, so that counter cannot move at all.
 *
 * A probe settles it instead: THREE timers fire in a Boot Camp mission, slots
 * 0, 1 and 2, every one with count == 1. So the firing path runs, the
 * `removeevent` argument is exercised in its TRUE form, and the slot-free
 * branch with its count decrement runs three times. What does NOT run is the
 * repeating branch -- no timer here has a second tick, so the line that
 * recomputes `start` from now is reached by nothing. */
void __cdecl TimerTick(void)
{
    AM2_Timer *timers = (AM2_Timer *)(uintptr_t)ADDR_TIMER_TABLE;
    uint32_t   now    = *(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS);
    uint32_t  *last   = (uint32_t *)(uintptr_t)ADDR_EVENT_BLOCK;
    int32_t    i;

    if (now - *last < AM2_TIMER_TICK_MS)
        return;
    *last = now;

    for (i = 0; i < AM2_TIMER_MAX; i++) {
        AM2_Timer *t = &timers[i];

        if (!t->id)
            continue;
        if (t->start > now)
            continue;

        EventNotify(1, t->id, 0, 0, 0, 0, 0, 0, t->count == 1, 0);

        if (--t->count <= 0) {
            t->id = 0;
            (*(int32_t *)(uintptr_t)ADDR_TIMER_COUNT)--;
        } else {
            t->start = *(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS)
                       + t->period;
        }
    }
}

/* --------------------------------------------- object shims ---- */

typedef void (__cdecl *AM2_Type2ActionFn)(void *obj);
typedef void (__cdecl *AM2_Type2ActionArgFn)(void *obj, int32_t arg);
typedef void (__cdecl *AM2_ObjAttachFn)(void *a, void *b);
#define orig_obj_attach_to \
    (*(AM2_ObjAttachFn)AM2_IMAGE(ADDR_OBJ_ATTACH_TO))
typedef void (__cdecl *AM2_GuardedActionFn)(void *obj, int32_t a, int32_t b,
                                            int32_t c, int32_t d, int32_t e);
typedef void (__cdecl *AM2_AtPointAFn)(int32_t a, uint32_t point, int32_t c);
typedef void (__attribute__((thiscall)) *AM2_ListRemoveAtFn)(void *list,
                                                             int32_t i);
typedef void (__cdecl *AM2_PointActionFn)(void *obj, uint32_t point);

/* 0x0041FE70. Deploy the object a uid names.
 *
 * `where` is a packed point, and a zero LOW WORD -- the x, tested as a 16-bit
 * value -- means "leave it where it is", at which point the object's own
 * position at +0x12 is used instead. Note the test is on the word, not the
 * whole dword, so a point with x == 0 and any y is treated as absent.
 *
 * Checks the pointer rather than the uid, like EvtObjAction and EvtSetByte40.
 */
void __cdecl EvtDeployItem(uint32_t uid, uint32_t where)
{
    uint8_t *obj = (uint8_t *)LookupByUID(uid);

    if (obj == (uint8_t *)0)
        return;

    if ((uint16_t)where == 0)
        where = *(const uint32_t *)(obj + OBJ_OFF_POS);

    DeployItem(obj, where, 0, 0);
}

/* 0x0041FBE0 and 0x0041FC10. The same shim twice, differing only in which
 * function it calls on a type-2 object.
 *
 * Role names throughout: neither callee says anything about itself, and object
 * type 2 is one of the three CLAUDE.md still lists as unidentified. What IS
 * established is the shape -- the uid threshold, the lookup, and the type test
 * in that order, with the lookup happening BEFORE the type test so a
 * registered object of the wrong type is looked up and discarded.
 *
 * ObjIsType2 is handed whatever LookupByUID returned, null included, which is
 * safe only because that function opens with a null test; the majority of this
 * family checks the uid instead, and these two check both. */
void __cdecl EvtType2ActionA(uint32_t uid)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (!ObjIsType2((const AM2_Object *)obj))
        return;

    Type2ActionA(obj);
}

void __cdecl EvtType2ActionB(uint32_t uid)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (!ObjIsType2((const AM2_Object *)obj))
        return;

    Type2ActionB(obj);
}

/* 0x0041FBA0. The third of the type-2 twins, and the only one that forwards an
 * argument. Same order as the other two -- threshold, lookup, type test. */
void __cdecl EvtType2ActionC(uint32_t uid, int32_t arg)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (!ObjIsType2((const AM2_Object *)obj))
        return;

    Type2ActionC(obj, arg);
}

/* 0x0041F6E0. The one in this family that checks NEITHER the pointer nor the
 * type -- only the uid threshold -- and then passes whatever LookupByUID
 * returned straight to 0x00428370.
 *
 * That is a fourth checking style, and it is the unsafe one: a uid at or above
 * the threshold that is not registered gives null. The callee is HealObject,
 * reconstructed since this comment was written, and it turns out to open with
 * its own null test -- so the hazard is real in this function and absorbed by
 * the next one. Reproduced, not guarded: adding a test here would still be
 * behaviour this build does not have.
 *
 * Note what the value means now that the callee is read. It is a PERCENTAGE of
 * maximum health, clamped to 0..100, so the script action behind this is
 * "heal that object by N percent". */
void __cdecl EvtObjSet(uint32_t uid, int32_t value)
{
    if (uid < AM2_UID_COUNTER_MIN)
        return;

    HealObject(LookupByUID(uid), value, 0);
}

/* 0x0041FEC0. Resurrect a named item, by its own log line.
 *
 * The same "a zero LOW WORD means leave it where it is" idiom EvtDeployItem
 * uses -- the x tested as 16 bits, so a point with x == 0 and any y counts as
 * absent -- and then the same DeployItem, differing in ONE argument: 1 here
 * where EvtDeployItem passes 0. That is the resurrect flag, and it is why the
 * callee's own message reads "DeployItem(resurrection)". Neither function
 * explains that string alone; both together do.
 *
 * The log is gated on the comm object's debug field, like the rest of this
 * module, and prints the point as two SIGNED 16-bit halves. */
void __cdecl ScriptResurrectItem(uint32_t uid, uint32_t where)
{
    uint8_t *obj;

    if (kEventDebug)
        orig_log("ScriptResurrectItem, uid=%x, pos=(%d,%d)\n", uid,
                 (int32_t)(int16_t)(where & 0xFFFFu),
                 (int32_t)(int16_t)(where >> 16));

    obj = (uint8_t *)LookupByUID(uid);

    if (obj == (uint8_t *)0)
        return;

    if ((uint16_t)where == 0)
        where = *(const uint32_t *)(obj + OBJ_OFF_POS);

    DeployItem(obj, where, AM2_DEPLOY_RESURRECT, 0);
}

/* 0x0041F8B0. Apply the point action to every object an army owns.
 *
 * Not a peer of the two shims below it, despite sitting between them: it
 * resolves the army to a comm slot with CommSlotForArmy -- which is THISCALL,
 * comm object in ecx, and whose `ret 4` is what makes the stack accounting
 * around it read oddly -- and walks the object list for that slot.
 *
 * Three per-object gates: the same +8 flag bit EvtGuardedAction tests, and a
 * filter compared against ObjFieldA which -1 disables. A uid that no longer
 * resolves is REMOVED from the list, and the index is deliberately not
 * advanced afterwards because the list has shifted under it.
 *
 * THE RELATIVE OFFSET ACCUMULATES, and that is a defect in the original rather
 * than a reading of it. `point` is copied into two registers before the loop
 * and never reloaded, so with `relative` set the second matching object gets
 * point + first.pos + second.pos, the third gets all three added, and so on.
 * The loop's back edge lands after the initialisation, which is what settles
 * it. Reproduced: nothing in this port may quietly fix a bug the game ships,
 * and a mission relying on the first object's offset would change behaviour if
 * it were fixed. */
void __cdecl EvtArmyAtPoint(int32_t army, int32_t filter, uint32_t point,
                            int32_t relative)
{
    void    *comm = *(void **)AM2_IMAGE(ADDR_COMM_OBJECT);
    int32_t  slot = CommSlotForArmy(comm, army);
    uint8_t *list = ((uint8_t **)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
    uint16_t x    = (uint16_t)(point & 0xFFFFu);
    uint16_t y    = (uint16_t)(point >> 16);
    int32_t  i    = 0;

    while (i < *(const int32_t *)(list + LIST_OFF_COUNT)) {
        const uint32_t *uids = *(const uint32_t **)(list + LIST_OFF_UIDS);
        uint8_t        *obj  = (uint8_t *)LookupByUID(uids[i]);

        if (obj == (uint8_t *)0) {
            ListRemoveAt(list, i);
            continue;                       /* the list shifted; do not ++ */
        }

        if (*(const uint8_t *)(obj + OBJ_OFF_FLAGS8) & OBJ_FLAG8_BLOCKED) {
            i++;
            continue;
        }

        if (filter != -1 && (int32_t)ObjFieldA(obj) != filter) {
            i++;
            continue;
        }

        if (relative) {
            x = (uint16_t)(x + *(const uint16_t *)(obj + OBJ_OFF_X));
            y = (uint16_t)(y + *(const uint16_t *)(obj + OBJ_OFF_Y));
            point = (uint32_t)x | ((uint32_t)y << 16);
        }

        PointActionA(obj, point);
        i++;
    }
}

/* 0x0041F820 and 0x0041F780. The "At" halves, and they are not the plain
 * point-takers the "On" wrappers made them look like.
 *
 * Each takes a uid of its own and a `relative` flag. When that flag is set the
 * object's position is ADDED to the point rather than replacing it -- which is
 * AM2_ScriptAction.relative, the leading `+` a script may put on coordinates.
 * So this is where that syntax is honoured, two levels below the parser that
 * recorded it.
 *
 * They differ in what they do with the result, and in one guard. EvtAtPointA
 * clears field 0x540 first if the object is type 2 -- the same field
 * EvtSetField540 exists for -- and always calls its action. EvtAtPointC
 * declines when the object is ALREADY at the point, but only on the
 * non-relative path; adding a zero offset would be a no-op anyway, so the
 * asymmetry costs nothing and is reproduced.
 *
 * The original builds the relative sum in its own first argument slot. That is
 * a register-allocation detail with no observable side, so a local is used. */
void __cdecl EvtAtPointA(uint32_t uid, uint32_t point, int32_t relative)
{
    uint8_t *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = (uint8_t *)LookupByUID(uid);

    if (obj == (uint8_t *)0)
        return;

    if (relative) {
        uint16_t x = (uint16_t)(point & 0xFFFFu)
                   + *(const uint16_t *)(obj + OBJ_OFF_X);
        uint16_t y = (uint16_t)(point >> 16)
                   + *(const uint16_t *)(obj + OBJ_OFF_Y);

        point = (uint32_t)x | ((uint32_t)y << 16);
    }

    if (ObjIsType2((const AM2_Object *)obj))
        *(int32_t *)(obj + OBJ_OFF_FIELD_540) = 0;

    PointActionA(obj, point);
}

void __cdecl EvtAtPointC(uint32_t uid, uint32_t point, int32_t relative)
{
    uint8_t *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = (uint8_t *)LookupByUID(uid);

    if (obj == (uint8_t *)0)
        return;

    if (relative) {
        uint16_t x = *(const uint16_t *)(obj + OBJ_OFF_X)
                   + (uint16_t)(point & 0xFFFFu);
        uint16_t y = *(const uint16_t *)(obj + OBJ_OFF_Y)
                   + (uint16_t)(point >> 16);

        PointActionC(obj, (uint32_t)x | ((uint32_t)y << 16));
        return;
    }

    if (*(const uint16_t *)(obj + OBJ_OFF_X) == (uint16_t)(point & 0xFFFFu)
        && *(const uint16_t *)(obj + OBJ_OFF_Y) == (uint16_t)(point >> 16))
        return;

    PointActionC(obj, point);
}

/* 0x0041F7F0. The "On" wrapper for EvtAtPointC, and the third of that shape:
 * `target` is acted on, `at` only supplies a position. */
void __cdecl EvtAtObjPosC(uint32_t target, uint32_t at, int32_t relative)
{
    const uint8_t *obj;

    if (at < AM2_UID_COUNTER_MIN)
        return;

    obj = (const uint8_t *)LookupByUID(at);

    if (obj == (const uint8_t *)0)
        return;

    EvtAtPointC(target, *(const uint32_t *)(obj + OBJ_OFF_POS), relative);
}

/* 0x0041FD10 and 0x0041FD30. Two shims that hand the ADDRESS of their first
 * argument to a function further up the image.
 *
 * Passing a parameter slot by reference is the whole of what they do, so the
 * callee writes back into a copy that dies on return -- unless it only reads.
 * Which it is is not established here; both callees are above the nominal CRT
 * line at 0x0045C000 and are game code rather than library, per tools/crt.py,
 * and neither names itself.
 *
 * The zero arguments differ in number and position between the two, so they
 * are not one function called twice. */
void __cdecl EvtByRefA(int32_t a, int32_t b)
{
    SeqAddKind5(&a, 0, b);
}

void __cdecl EvtByRefB(int32_t a, int32_t b)
{
    SeqAddKind7(&a, 0, 0, 0, b);
}

/* 0x0041F710. The most guarded member of the family: four tests before it acts.
 *
 * The uid threshold, then the pointer, then a flag bit at +8 that must be
 * CLEAR, then an int16 at +0x62 that must be positive. The last two are read
 * off the object rather than the event, so this is the shim that declines when
 * the object itself is not in a fit state -- whatever that state is. Three
 * trailing zero arguments go to a callee with nineteen callers. */
void __cdecl EvtGuardedAction(uint32_t uid, int32_t a, int32_t b)
{
    uint8_t *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = (uint8_t *)LookupByUID(uid);

    if (obj == (uint8_t *)0)
        return;

    if (*(const uint8_t *)(obj + OBJ_OFF_FLAGS8) & OBJ_FLAG8_BLOCKED)
        return;

    if (*(const int16_t *)(obj + OBJ_OFF_COUNT62) <= 0)
        return;

    DamageObject(obj, a, b, 0, 0, 0);
}

/* 0x0041F880 and 0x0041F970. Two more of the "On" shape.
 *
 * Each takes a uid where its twin takes a point, looks the object up, and
 * passes the object's own position at +0x12 through instead. event.h records
 * the same pattern for EvtPlaySoundOn against EvtPlaySoundAt; these are two
 * further pairs, and their "At" halves are still original -- small, and worth
 * taking next so each pair is whole.
 *
 * Note which argument carries the uid: the FIRST for most of this family, but
 * the second here and the third in the other, because the uid sits where the
 * point sits in the twin's signature. */
void __cdecl EvtAtObjPosA(int32_t a, uint32_t uid, int32_t c)
{
    const uint8_t *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = (const uint8_t *)LookupByUID(uid);

    if (obj == (const uint8_t *)0)
        return;

    EvtAtPointA(a, *(const uint32_t *)(obj + OBJ_OFF_POS), c);
}

void __cdecl EvtAtObjPosB(int32_t a, int32_t b, uint32_t uid, int32_t d)
{
    const uint8_t *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = (const uint8_t *)LookupByUID(uid);

    if (obj == (const uint8_t *)0)
        return;

    EvtArmyAtPoint(a, b, *(const uint32_t *)(obj + OBJ_OFF_POS), d);
}

/* ------------------------------------------------------- reset ---- */


/* 0x00422450. Drop the whole script and event state.
 *
 * Every callee is already reconstructed, so nothing here is new machinery --
 * what this function contributes is the ORDER, and that the four things belong
 * together at all. Names first, then the condition list, then the registration
 * table, then the flag.
 *
 * The order matters in one direction: EventClearAll frees handler arguments
 * whose `owns` flag is set, and the condition records those arguments point at
 * have already been freed by FreeScriptConditions. Nothing reads them in
 * between, so it is safe -- but it is the kind of sequencing to preserve
 * exactly rather than tidy. */
void __cdecl ResetScriptState(void)
{
    FreeScriptNames();
    FreeScriptConditions();
    EventClearAll();

    *(int32_t *)AM2_IMAGE(ADDR_SCRIPT_STATE_FLAG) = 0;
}

/* 0x0041FEA0. Resolve a uid and act on the object, if there is one.
 *
 * A role name for both halves: this function and the 96-byte one it calls at
 * 0x00428DA0 name themselves nowhere, and that one has twenty-two callers of
 * its own so it is somebody else's to identify.
 *
 * Note it checks the POINTER rather than the uid -- no AM2_UID_COUNTER_MIN
 * test -- which puts it with EvtSetByte40 rather than with the majority of
 * this family. */
void __cdecl EvtObjAction(uint32_t uid)
{
    void *obj = LookupByUID(uid);

    if (obj == (void *)0)
        return;

    DestroyByType(obj);
}

/* --------------------------------------------------- name -> uid ---- */

/* 0x0041F520, and fifty-three callers -- the densest thing in this module.
 *
 * A script writes names where the engine wants uids, and this is the one place
 * that turns one into the other. Three ways to get nothing back: an index out
 * of range at either end, and an entry that is not type 2. Only forward
 * references carry an object uid, which is the same test ScriptSetObjBitmap
 * makes before touching an object.
 *
 * `me` is the exception and the reason the function exists at all. When the
 * name is the one ADDR_SVAR_ME holds, the uid does not come from the table --
 * it comes from `me`, the caller's own context, so an action written once in
 * a script means a different object each time it fires. A caller with no
 * context gets the complaint and zero.
 *
 * That is also what settles ADDR_SVAR_ME: the argument is bounded against the
 * name COUNT before the comparison, so the global holds a name-table index,
 * not a uid as orig.h's group comment says. */
uint32_t __cdecl ResolveUid(int32_t name, uint32_t me)
{
    const AM2_ScriptName *e;

    if (name < 0 || name >= am2_script_name_count())
        return 0;

    if (name == *(const int32_t *)AM2_IMAGE(ADDR_SVAR_ME)) {
        if (me != 0)
            return me;

        orig_log("Bad ME\n");
        return 0;
    }

    e = am2_script_name(name);

    if (e->type != AM2_NAME_TYPE_REF)
        return 0;

    return (uint32_t)e->value;
}

/* ------------------------------------------- condition actions ---- */

typedef int32_t (__cdecl *AM2_RandFn)(void);
/* The action EXECUTOR stays original -- 4096 bytes in this same module, and
 * the one thing under the condition layer still not reconstructed. */
typedef void (__cdecl *AM2_RunScriptActionFn)(AM2_ScriptAction *act,
                                              void *arg);
#define orig_run_script_action_at \
    (*(AM2_RunScriptActionFn)AM2_IMAGE(ADDR_RUN_SCRIPT_ACTION))
#define orig_rand        (*(AM2_RandFn)AM2_IMAGE(ADDR_GAME_RAND))

/* 0x00421410. Run the i'th action of a condition.
 *
 * All it does is index: the action array is 0x48 bytes a stride, which is
 * AM2_ScriptAction's size arrived at here for the third time and from a third
 * direction -- the parser writes that record, SaveObjScriptSection copies it,
 * and this strides it. Four callers, so it is the shared way in rather than
 * something RunCondActions invented. */
static void __cdecl CondRunAction(AM2_ScriptCond *c, int32_t i, void *arg)
{
    orig_run_script_action_at(&c->actions[i], arg);
}

/* 0x00421430. Run an `if` statement's actions the way its `mode` says to.
 *
 * The four modes are the ones script.h already lists from the parser, now
 * confirmed from the far side:
 *
 *   0  every action, in order
 *   1  one at random
 *   2  one per firing, round robin
 *   3  the action whose `onobjstate` name matches the object's current state
 *
 * Mode 2 is what settles that +0x28 is not unused. It reads the field as a
 * SIGNED byte, runs that action, then stores (cursor + 1) % nactions back --
 * so the field is the round-robin position and script.h now says so.
 *
 * Mode 3 walks the actions looking at each one's `extra` field, which script.h
 * describes as "a damage kind, an order form, or `onobjstate`'s name". Here it
 * is unambiguously the third: it is a name-table index, and only entries of
 * type 2 are considered. The first whose value equals the object's current
 * state runs, and the walk stops.
 *
 * Two hazards reproduced. Modes 1 and 2 divide by `nactions` with no check, so
 * an `if` with a mode and no actions would fault -- the parser cannot produce
 * one. And mode 3's state bound is `>=` against statecount but the complaint
 * it logs does not stop the walk; it just skips that action. */
void __cdecl RunCondActions(AM2_ScriptCond *c, void *arg)
{
    if (c->actions == (AM2_ScriptAction *)0)
        return;

    switch (c->mode) {
    case 0:
        if (c->nactions <= 0)
            return;
        for (int32_t i = 0; i < c->nactions; i++)
            CondRunAction(c, i, arg);
        return;

    case 1:
        CondRunAction(c, orig_rand() % c->nactions, arg);
        return;

    case 2: {
        int32_t at = *(const int8_t *)&c->cursor;

        CondRunAction(c, at, arg);
        *(int8_t *)&c->cursor = (int8_t)((at + 1) % c->nactions);
        return;
    }

    case 3: {
        uint8_t       *obj;
        AM2_ObjScript *scr;
        int32_t        id;

        obj = (uint8_t *)LookupByUID(ResolveUid(c->objstate, (uint32_t)(uintptr_t)arg));

        if (!ObjIsItem((const AM2_Object *)obj))
            return;

        id = *(const int32_t *)(obj + OBJ_OFF_SCRIPT_ID);
        if (id <= 0 || id > kObjScriptCount)
            return;

        scr = &kObjScripts[id - 1];

        for (int32_t i = 0; i < c->nactions; i++) {
            const AM2_ScriptName *e =
                am2_script_name(c->actions[i].extra);

            if (e->type != AM2_NAME_TYPE_REF)
                continue;

            if (e->value >= scr->statecount) {
                orig_log("Tried to switch on invalid state.\n");
                continue;
            }

            if (*(const int32_t *)(obj + OBJ_OFF_SCRIPT_STATE) == e->value) {
                CondRunAction(c, i, arg);
                return;
            }
        }
        return;
    }

    default:
        return;
    }
}

/* --------------------------------------------- immediate trigger ---- */

/* Free one entry's handler chain and then the entry. `owns` decides whether
 * the handler's argument goes with it -- the same flag EventRegister stores
 * and the table teardown reads. */
static void FreeEventEntry(AM2_EventEntry *e)
{
    AM2_EventHandler *h = e->handlers;

    while (h != (AM2_EventHandler *)0) {
        AM2_EventHandler *next = h->next;

        if (h->owns)
            am2_free(h->arg);
        am2_free(h);
        h = next;
    }

    am2_free(e);
}

/* 0x0041EF80. Raise an event now: walk the bucket for `type`, and for every
 * entry whose key matches, call each handler in turn.
 *
 * `type` is the BUCKET INDEX -- the original indexes kEventTable directly with
 * it, so the nine buckets are nine event types rather than a hash.
 *
 * Two things this settles about the event model. A locally raised event is
 * broadcast to peers exactly once, through EventMessageSend, and only when
 * `remote` is 0 -- so an event arriving from the wire does not echo. And the
 * broadcast happens on the FIRST matching entry, before its handlers run, not
 * once per entry.
 *
 * `removeevent` unlinks the entry after its handlers have run and frees it.
 * The head case restarts from the new head; the other case keeps a `prev` it
 * only searches for once. Note that `prev` is NOT reset when the head is
 * removed, so a bucket that removes its head and later removes a middle entry
 * uses a stale `prev`. That is the original's, and it is reproduced. */
void __cdecl EventTriggerImmediate(int32_t type, int32_t num1, uint32_t uid1,
                                   int32_t maskA, int32_t num2, uint32_t uid2,
                                   int32_t maskB, int32_t removeevent,
                                   int32_t remote)
{
    AM2_EventEntry *e;
    AM2_EventEntry *prev = (AM2_EventEntry *)0;
    int32_t         sent = 0;

    if (kEventDebug)
        orig_log("EventTriggerImmediate: type %d, num: %d, uid: %x, "
                 "removeevent: %d, remote: %d\n",
                 type, num1, uid1, removeevent, remote);

    e = kEventTable[type];

    while (e != (AM2_EventEntry *)0) {
        /* The entry's key pair is the CRITERIA, the event's numbers the
         * candidate values, and its two mask fields the sets it belongs to.
         * Neither uid takes part in matching. */
        int32_t matched = FilterMatches(e->key0, e->key1, num1, num2,
                                        maskA, maskB);

        if (matched) {
            if (!remote && !sent) {
                EventMessageSend(type, num1, uid1, maskA, num2, uid2, maskB,
                                 removeevent);
                sent = 1;
            }

            for (AM2_EventHandler *h = e->handlers;
                 h != (AM2_EventHandler *)0; h = h->next)
                ((AM2_EventHandlerFn)h->fn)(type, num1, uid1, maskA,
                                            num2, uid2, maskB, h->arg);
        }

        if (!matched || !removeevent) {
            prev = e;
            e    = e->next;
            continue;
        }

        if (kEventTable[type] == e) {
            kEventTable[type] = e->next;
            FreeEventEntry(e);
            e = kEventTable[type];
        } else {
            if (prev == (AM2_EventEntry *)0) {
                prev = kEventTable[type];
                while (prev->next != e)
                    prev = prev->next;
            }
            prev->next = e->next;
            FreeEventEntry(e);
            e = prev->next;
        }
    }
}

/* 0x0041FC40. The fourth of the "look it up and act if the type fits" twins,
 * and the only one that admits types 2, 3 AND 8 rather than type 2 alone.
 * Same order as the others: threshold, lookup, type test. */
void __cdecl EvtType238Action(uint32_t uid, int32_t arg)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return;

    Type238Action(obj, arg);
}

/* 0x0041FFD0. Push a one-deep "current object" context.
 *
 * Three globals are copied into three companions and then overwritten, which
 * is a save/restore pair with a depth of exactly one -- so a second call
 * before the matching restore loses the first saved value. Nothing here says
 * what the context is FOR, so all six keep positional names.
 *
 * The order matters and is preserved: every save happens before any install,
 * and the third pair's new value is a constant 1 rather than anything derived
 * from the object. */
void __cdecl EvtPushObjCtx(uint32_t uid)
{
    uint8_t *obj = (uint8_t *)LookupByUID(uid);

    if (obj == (uint8_t *)0)
        return;

    *(void **)AM2_IMAGE(ADDR_OBJ_CTX_OBJ_PREV) =
        *(void **)AM2_IMAGE(ADDR_OBJ_CTX_OBJ);
    *(int32_t *)AM2_IMAGE(ADDR_OBJ_CTX_VAL_PREV) =
        *(const int32_t *)AM2_IMAGE(ADDR_OBJ_CTX_VAL);
    *(int32_t *)AM2_IMAGE(ADDR_OBJ_CTX_SET_PREV) =
        *(const int32_t *)AM2_IMAGE(ADDR_OBJ_CTX_SET);

    *(void **)AM2_IMAGE(ADDR_OBJ_CTX_OBJ)  = obj;
    *(int32_t *)AM2_IMAGE(ADDR_OBJ_CTX_VAL) = *(const int32_t *)(obj + 4);
    *(int32_t *)AM2_IMAGE(ADDR_OBJ_CTX_SET) = 1;
}

/* 0x0041F570 and 0x0041F5C0. A pair over one object flag bit and one global,
 * and they are CROSSED rather than symmetric:
 *
 *              name == ID15                    any other name
 *   Clear      flag := 1, and SETS the bit     CLEARS the bit on the
 *              on the ID15 object              resolved object
 *   Set        flag := 0                       SETS the bit on the
 *                                              resolved object
 *
 * Each sets the bit in one arm and clears it in the other, and only the first
 * touches an object at all on the ID15 path. The names below describe the
 * ordinary arm, which is the one a script reaches by naming something; a
 * symmetric reading would be wrong in three of those four cells.
 *
 * ID15 is the id no keyword produces -- see ADDR_SVAR_ID15 -- so these are the
 * two functions that make that global live at all. */
void __cdecl EvtFlag40Clear(int32_t name, uint32_t me)
{
    uint8_t *obj;

    if (name == *(const int32_t *)AM2_IMAGE(ADDR_SVAR_ID15)) {
        *(int32_t *)AM2_IMAGE(ADDR_EVT_ID15_FLAG) = 1;

        obj = (uint8_t *)LookupByUID(
                  *(const uint32_t *)AM2_IMAGE(ADDR_EVT_ID15_UID));

        if (obj != (uint8_t *)0)
            *(uint32_t *)(obj + OBJ_OFF_FLAGS8) |= OBJ_FLAG8_BIT40;

        return;
    }

    obj = (uint8_t *)LookupByUID(ResolveUid(name, me));

    if (obj != (uint8_t *)0)
        *(uint32_t *)(obj + OBJ_OFF_FLAGS8) &= ~(uint32_t)OBJ_FLAG8_BIT40;
}

void __cdecl EvtFlag40Set(int32_t name, uint32_t me)
{
    uint8_t *obj;

    if (name == *(const int32_t *)AM2_IMAGE(ADDR_SVAR_ID15)) {
        *(int32_t *)AM2_IMAGE(ADDR_EVT_ID15_FLAG) = 0;
        return;
    }

    obj = (uint8_t *)LookupByUID(ResolveUid(name, me));

    if (obj != (uint8_t *)0)
        *(uint32_t *)(obj + OBJ_OFF_FLAGS8) |= OBJ_FLAG8_BIT40;
}

/* 0x0041FA10. Set a field on every object of an army that passes the gates.
 *
 * EvtArmyAtPoint's sibling, and the shared parts are worth naming as shared:
 * the same CommSlotForArmy lookup into the per-slot list, the same +8 flag
 * gate, the same ObjFieldA filter that -1 disables, and the same prune-a-dead-
 * uid-without-advancing. Two differences: this one also requires
 * ObjIsTypeIn238, and its action is a field write rather than a point action.
 *
 * That write is a one-deep save -- 0xE4 goes to 0xE8 before the new value goes
 * to 0xE4 -- which is the same shape EvtPushObjCtx uses on globals. So a second
 * call before anything restores loses the first saved value, here as there.
 *
 * Unlike EvtArmyAtPoint this one carries nothing that accumulates, so the
 * defect recorded there does not apply. */
void __cdecl EvtArmySetField(int32_t army, int32_t filter, int32_t value)
{
    void    *comm = *(void **)AM2_IMAGE(ADDR_COMM_OBJECT);
    int32_t  slot = CommSlotForArmy(comm, army);
    uint8_t *list = ((uint8_t **)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
    int32_t  i    = 0;

    while (i < *(const int32_t *)(list + LIST_OFF_COUNT)) {
        const uint32_t *uids = *(const uint32_t **)(list + LIST_OFF_UIDS);
        uint8_t        *obj  = (uint8_t *)LookupByUID(uids[i]);

        if (obj == (uint8_t *)0) {
            ListRemoveAt(list, i);
            continue;
        }

        if (*(const uint8_t *)(obj + OBJ_OFF_FLAGS8) & OBJ_FLAG8_BLOCKED) {
            i++;
            continue;
        }

        if (!ObjIsTypeIn238((const AM2_Object *)obj)) {
            i++;
            continue;
        }

        if (filter != -1 && (int32_t)ObjFieldA(obj) != filter) {
            i++;
            continue;
        }

        *(int32_t *)(obj + OBJ_OFF_FIELD_E8) =
            *(const int32_t *)(obj + OBJ_OFF_FIELD_E4);
        *(int32_t *)(obj + OBJ_OFF_FIELD_E4) = value;

        i++;
    }
}

/* 0x0041FD50. Give one object something to do with another.
 *
 * Both uids must clear the threshold and both must resolve -- and note the
 * order: BOTH thresholds are tested before either lookup, so a bad second uid
 * costs no lookup at all. That is the opposite of the hoisting mistake made
 * earlier in this module, and reproducing it exactly is the point.
 *
 * Field 0x540 is cleared on the first object when it is type 2, before the
 * pair is handed on. Third sighting of that step -- EvtAtPointA does it and
 * EvtSetField540 exists to write the field -- so several actions reset it
 * before giving an object something new to do, whatever it holds. */
void __cdecl EvtObjPair(uint32_t uidA, uint32_t uidB)
{
    uint8_t *a;
    uint8_t *b;

    if (uidA < AM2_UID_COUNTER_MIN || uidB < AM2_UID_COUNTER_MIN)
        return;

    a = (uint8_t *)LookupByUID(uidA);
    if (a == (uint8_t *)0)
        return;

    b = (uint8_t *)LookupByUID(uidB);
    if (b == (uint8_t *)0)
        return;

    if (ObjIsType2((const AM2_Object *)a))
        *(int32_t *)(a + OBJ_OFF_FIELD_540) = 0;

    orig_obj_attach_to(a, b);
}

/* --------------------------------------------------- bitmaps ---- */

typedef void (__cdecl *AM2_FreeBitmapFn)(void **slot);
/* Forward-declared rather than reached through win32/sprite.h, the same way
   commmsg.cpp declares HudMessage: this module is flat and that header names
   Win32 types. `extern "C"` because every header in this tree is -- and note
   the definition returns AM2_Sprite *, which cannot be spelled here, so this
   says void *. The two agree on a 32-bit pointer ABI and nothing checks it;
   said plainly rather than left for someone to find. */
extern "C" void *__cdecl LoadBitmap(const char *name, int32_t flags);

/* 0x0041F600 and 0x0041F650. Put a full-screen bitmap up, pausing or not.
 *
 * Both chdir into `bitmaps`, drop whatever bitmap is loaded, and load the
 * named one. The pausing form additionally sets the in-mission sub-state to
 * 0x16, raises a flag, and tells the other players with
 * SendGamePause(1, AM2_EVENT_FLAG_8).
 *
 * That last call names a bit. frame.cpp calls SendGamePause(0,
 * AM2_EVENT_FLAG_8) -- un-pause, reason 8 -- and this is the matching set, so
 * pause reason 8 is "a full-screen bitmap is up". CLAUDE.md records the event
 * flags as the pause mask without naming any of its bits; this names one.
 *
 * The pair corresponds to the script keywords `showbitmap` and
 * `showbitmapnopause`, tokens 65 and 66. That is inferred from which of them
 * pauses rather than traced through the action dispatcher, and is worth
 * checking against 0x00420410 when that function is read.
 *
 * Note the pause is announced BEFORE the bitmap is loaded, so a load that
 * fails still leaves the game paused. */
void __cdecl EvtShowBitmap(const char *name)
{
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));

    *(int32_t *)AM2_IMAGE(ADDR_MENU_MODE) = AM2_SUBSTATE_BITMAP;
    *(int32_t *)AM2_IMAGE(ADDR_OVERLAY_DIRTY)      = 1;

    SendGamePause(1, AM2_EVENT_FLAG_8);

    FreeBitmap((void **)AM2_IMAGE(ADDR_CURRENT_BITMAP));
    *(void **)AM2_IMAGE(ADDR_CURRENT_BITMAP) = LoadBitmap(name, 0);
}

void __cdecl EvtShowBitmapNoPause(const char *name)
{
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));

    FreeBitmap((void **)AM2_IMAGE(ADDR_CURRENT_BITMAP));
    *(void **)AM2_IMAGE(ADDR_CURRENT_BITMAP) = LoadBitmap(name, 0);
}

/* ------------------------------------------- object table record ---- */

/* 0x0041FF60. Point an object at one of the 256-byte records at
 * 0x004F9ACC. What they hold is not established.
 *
 * Kinds 2 and 3 are handled identically apart from WHICH pointer is written --
 * +0x4C0 for one and +0x4C8 for the other, both inside the sub-record at
 * obj+0x6C -- and then both propagate the same value through SetFieldInAll.
 * Two nearly-identical arms rather than one with a computed offset is how the
 * original does it, and reproducing that keeps the two kinds visibly distinct
 * rather than merged behind an arithmetic trick.
 *
 * Any other kind is refused with a complaint that names the CONDITION and not
 * the function, so this one keeps a role name. Object kinds 2 and 3 are two of
 * the three CLAUDE.md still lists as unidentified; that they share this
 * treatment and differ only by one field is a fact about them worth having.
 *
 * There is no bounds check on `index`. The record is at
 * 0x004F9ACC + index * 0x100 whatever `index` is. */
void __cdecl ScriptSetObjTable(uint32_t uid, int32_t index)
{
    uint8_t *obj = (uint8_t *)LookupByUID(uid);
    uint8_t *sub;
    void    *rec;

    if (obj == (uint8_t *)0)
        return;

    rec = (uint8_t *)AM2_IMAGE(ADDR_OBJ_TABLE_RECORDS)
        + (uint32_t)index * AM2_OBJ_TABLE_REC_SIZE;
    sub = obj + OBJ_OFF_SUBRECORD;

    switch (*(const int32_t *)obj) {
    case 2:
        *(void **)(sub + SUBREC_OFF_TABLE_KIND2) = rec;
        SetFieldInAll(sub, rec);
        return;

    case 3:
        *(void **)(sub + SUBREC_OFF_TABLE_KIND3) = rec;
        SetFieldInAll(sub, rec);
        return;

    default:
        orig_log("Warning: check if this script command works with this "
                 "object type!\n");
        return;
    }
}

/* ------------------------------------------------ cond tests ---- */

typedef int32_t (__cdecl *AM2_EvalOperandFn)(int32_t a, int32_t b, int32_t c);
typedef void (__cdecl *AM2_MissionLocalFn)(int32_t a);
typedef void (__cdecl *AM2_MissionNetFn)(int32_t a, int32_t b);
#define orig_eval_operand   (*(AM2_EvalOperandFn)AM2_IMAGE(ADDR_EVAL_OPERAND))
#define orig_mission_local  (*(AM2_MissionLocalFn)AM2_IMAGE(ADDR_SCRIPT_FIND_FILE))

/* 0x00421750. Evaluate an `if`'s testvar comparisons. All must pass; a
 * condition with none passes trivially.
 *
 * Each test is two operand TRIPLES and an operator, and each triple is handed
 * whole to the operand evaluator -- so a testvar operand is three values, not
 * one, which is what AM2_ScriptTest's shape already implied and this confirms.
 *
 * The operator jump table settles script.h's codes from the far side: 0 '=',
 * 1 '<>', 2 '<', 3 '>', 4 '<=', 5 '>=', in exactly that order. Read from the
 * TABLE rather than from the arm layout, per the rule -- and here the six arms
 * happen to be laid out in the same order, which is only knowable by checking.
 *
 * An operator above 5 falls through to the loop's continue, so an unknown
 * comparison PASSES rather than failing. Reproduced. */
int32_t __cdecl EvalCondTests(const AM2_ScriptCond *c)
{
    for (int32_t i = 0; i < c->ntests; i++) {
        const AM2_ScriptTest *t = &c->tests[i];
        int32_t left  = orig_eval_operand(t->left[0], t->left[1], t->left[2]);
        int32_t right = orig_eval_operand(t->right[0], t->right[1],
                                          t->right[2]);

        switch (t->op) {
        case 0: if (left != right) return 0; break;
        case 1: if (left == right) return 0; break;
        case 2: if (left >= right) return 0; break;
        case 3: if (left <= right) return 0; break;
        case 4: if (left >  right) return 0; break;
        case 5: if (left <  right) return 0; break;
        default: break;                 /* unknown operator passes */
        }
    }

    return 1;
}

/* 0x00421C40. Route the end of a mission.
 *
 * In single player it goes straight to the script-file loader, the function
 * that builds "%s%d.txt" and reads the next mission; in a multiplayer session
 * it goes somewhere else entirely, and that path takes an extra argument the
 * local one drops.
 *
 * A role name, and a corrected one: reading this function alone suggested "the
 * single-player way of handling a condition". The callee's own format string
 * says it is the mission loader, which is why an address should be named from
 * the body at the OTHER end of the call as well as this one. */
void __cdecl AdvanceMission(int32_t a, int32_t b)
{
    if (*(const int32_t *)AM2_IMAGE(ADDR_MP_SESSION) == 0) {
        orig_mission_local(a);
        return;
    }

    MissionNetworked(a, b);
}

/* ---------------------------------------------- action point ---- */

/* ----------------------------------------------- firing a weapon ---- */

/* The four glue functions between RunScriptAction and the weapon dispatcher
 * at 0x0045F460, one per way a script can ask for a shot: an explicit weapon
 * or the unit's own, aimed at a point or at another object. RunScriptAction
 * picks between the pairs on the action's own fields and this is where each
 * lands.
 *
 * Three things they all do, and each is worth stating once here rather than
 * four times below.
 *
 * A NEGATIVE HEADING MEANS "WORK IT OUT". Every one of them tests the caller's
 * heading against zero and, only when it is negative, replaces it with
 * AngleBetween masked to eight bits. A non-negative one is passed straight
 * through UNMASKED, so a script writing 300 gets 300 and not 44 -- reproduced,
 * because the two paths really do differ and only one of them is clamped.
 *
 * THE SPOT IS TWO FIELDS AND BOTH HAVE A FALLBACK IN THE CALLEE. The point is
 * zero when the shot is at an object, and the ground height is zero when it
 * is at another object too; 0x0045F460 fills the first from the target's
 * position and the second from the shooter's own height. So the "at an
 * object" pair pass a spot of all zeros and let the callee do the work, while
 * the "at a point" pair look the tile's height up themselves.
 *
 * THE TOP TWO BYTES OF THE SPOT ARE NEVER WRITTEN. The original assembles it
 * in eight bytes of stack and fills a dword and a word, leaving the last two
 * as whatever was there. `pad` is left uninitialised here for the same
 * reason: writing a zero would be inventing a value the original does not
 * supply, and nothing correct in the callee can read it.
 *
 * The two with an explicit weapon LEND IT THE FIRING UNIT'S ARMY, and put the
 * old value back on the way out -- so the shot is attributed to whoever fired
 * it rather than to whoever owns the weapon object. The unit's-own-weapon
 * pair do not, because a held weapon already belongs to its holder. */

typedef struct {
    uint32_t at;      /* the packed destination, or 0 to use the target's */
    int16_t  ground;  /* the height there, or 0 to use the shooter's own */
    int16_t  pad;     /* NOT written by any caller; see above */
} AM2_FireSpot;

typedef int32_t (__cdecl *AM2_FireWeaponFn)(void *weapon, void *unit,
                                            int32_t height, int32_t heading,
                                            AM2_FireSpot spot, void *target);
#define orig_fire_weapon ((AM2_FireWeaponFn)(uintptr_t)ADDR_FIRE_WEAPON)

/* 0x004200F0. A named weapon, fired by a named unit, at a point. */
void __cdecl FireWeaponAtPoint(uint32_t weaponUid, uint32_t unitUid,
                               int32_t heading, uint32_t at)
{
    uint8_t     *weapon = (uint8_t *)WeaponByUid((int32_t)weaponUid);
    uint8_t     *unit;
    AM2_Point    dest;
    AM2_FireSpot spot;
    int8_t       lent;

    if (!weapon)
        return;
    unit = (uint8_t *)UnitByUid(unitUid);
    if (!unit)
        return;

    dest.x = (int16_t)(at & 0xFFFFu);
    dest.y = (int16_t)(at >> 16);

    spot.at     = at;
    spot.ground = (int16_t)TileAttrAt((uint32_t)TileOfPoint(at));

    lent = *(const int8_t *)(weapon + OBJ_OFF_ARMY);
    *(int8_t *)(weapon + OBJ_OFF_ARMY) = *(const int8_t *)(unit + OBJ_OFF_ARMY);

    if (heading < 0)
        heading = AngleBetween((const AM2_Point *)(unit + OBJ_OFF_POS), &dest);

    orig_fire_weapon(weapon, unit, ObjFieldB(unit), heading, spot, (void *)0);

    *(int8_t *)(weapon + OBJ_OFF_ARMY) = lent;
}

/* 0x004201A0. A named weapon, fired by a named unit, at another object.
 *
 * The one asymmetry in the family: this is the only one of the four that
 * measures the shooter with ObjHeight rather than reading OBJ_OFF_HEIGHT_ADJ
 * through ObjFieldB, so it accounts for the ground the shooter is standing on
 * and the other three do not. Reproduced; nothing here says which is meant.
 *
 * It also reads the TARGET's OBJ_OFF_HEIGHT_SET as a SIGNED byte, where
 * orig.h documents the field as unsigned. The sign-extension is in the
 * instruction (`movsx ax, byte ptr [esi+0x65]`), so it is the original's
 * reading of its own field and not ours. */
void __cdecl FireWeaponAtObject(uint32_t weaponUid, uint32_t unitUid,
                                int32_t heading, uint32_t targetUid)
{
    uint8_t     *weapon = (uint8_t *)WeaponByUid((int32_t)weaponUid);
    uint8_t     *unit;
    uint8_t     *target;
    AM2_FireSpot spot;
    int8_t       lent;

    if (!weapon)
        return;
    unit = (uint8_t *)UnitByUid(unitUid);
    if (!unit)
        return;
    target = (uint8_t *)LookupByUID(targetUid);
    if (!target)
        return;

    spot.at     = *(const uint32_t *)(target + OBJ_OFF_POS);
    spot.ground = (int16_t)*(const int8_t *)(target + OBJ_OFF_HEIGHT_SET);

    if (heading < 0)
        heading = AngleBetween((const AM2_Point *)(unit + OBJ_OFF_POS),
                               (const AM2_Point *)(target + OBJ_OFF_POS));

    lent = *(const int8_t *)(weapon + OBJ_OFF_ARMY);
    *(int8_t *)(weapon + OBJ_OFF_ARMY) = *(const int8_t *)(unit + OBJ_OFF_ARMY);

    orig_fire_weapon(weapon, unit, ObjHeight(unit), heading, spot, target);

    *(int8_t *)(weapon + OBJ_OFF_ARMY) = lent;
}

/* 0x00420260. The unit's own held weapon, at a point. */
void __cdecl UnitFireAtPoint(uint32_t unitUid, int32_t heading, uint32_t at)
{
    uint8_t     *unit = (uint8_t *)UnitByUid(unitUid);
    void        *weapon;
    AM2_Point    dest;
    AM2_FireSpot spot;

    if (!unit)
        return;
    weapon = HeldWeaponObj(unit);
    if (!weapon)
        return;

    dest.x = (int16_t)(at & 0xFFFFu);
    dest.y = (int16_t)(at >> 16);

    spot.at     = at;
    spot.ground = (int16_t)TileAttrAt((uint32_t)TileOfPoint(at));

    if (heading < 0)
        heading = AngleBetween((const AM2_Point *)(unit + OBJ_OFF_POS), &dest);

    orig_fire_weapon(weapon, unit, ObjFieldB(unit), heading, spot, (void *)0);
}

/* 0x00420300. The unit's own held weapon, at another object.
 *
 * The only one that passes an all-zero spot: both halves are left for
 * 0x0045F460 to fill from the target, which is what makes the two defaults
 * documented on ADDR_FIRE_WEAPON load-bearing rather than decorative. */
void __cdecl UnitFireAtObject(uint32_t unitUid, int32_t heading,
                              uint32_t targetUid)
{
    uint8_t     *unit = (uint8_t *)UnitByUid(unitUid);
    void        *weapon;
    uint8_t     *target;
    AM2_FireSpot spot;

    if (!unit)
        return;
    weapon = HeldWeaponObj(unit);
    if (!weapon)
        return;
    target = (uint8_t *)LookupByUID(targetUid);
    if (!target)
        return;

    spot.at     = 0;
    spot.ground = 0;

    if (heading < 0)
        heading = AngleBetween((const AM2_Point *)(unit + OBJ_OFF_POS),
                               (const AM2_Point *)(target + OBJ_OFF_POS));

    orig_fire_weapon(weapon, unit, ObjFieldB(unit), heading, spot, target);
}

/* 0x004203A0. Work out the point an action refers to. Fourteen callers.
 *
 * A script may express an action's coordinates three ways, and this is where
 * all three are resolved:
 *
 *   - as a pair of VARIABLES, when `xvar` is set -- both are read through
 *     GetVarValue and packed into one point;
 *   - as a literal, when the x half is non-zero;
 *   - and otherwise as "wherever the target object is", which is the same
 *     zero-x idiom EvtDeployItem and ScriptResurrectItem use. Third sighting,
 *     and worth treating as a convention of this codebase rather than a
 *     coincidence.
 *
 * The last case falls back to the literal -- that is, to a point whose x is
 * zero -- when the target does not resolve. Only `xvar` is tested to choose
 * the variable path; a `yvar` without an `xvar` is ignored.
 *
 * The original assembles the result in its own two argument slots. Locals here;
 * nothing observable turns on it. */
uint32_t __cdecl ActionPoint(const AM2_ScriptAction *act, uint32_t me)
{
    void *obj;

    if (act->xvar != 0) {
        int32_t x = 0;
        int32_t y = 0;

        GetVarValue(act->xvar, &x);
        GetVarValue(act->yvar, &y);

        return (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16);
    }

    if (act->u.pos.x != 0)
        return (uint32_t)act->u.both;

    obj = LookupByUID(ResolveUid(act->target, me));

    if (obj == (void *)0)
        return (uint32_t)act->u.both;

    return *(const uint32_t *)((const uint8_t *)obj + OBJ_OFF_POS);
}

/* ------------------------------------------------------ raise ---- */

/* 0x0041F4A0. The front door: raise an event now, or after a delay.
 *
 * Kept under the name the address already had -- ADDR_EVENT_NOTIFY, CLAUDE.md's
 * "the notify" -- rather than renamed to a synonym.
 *
 * Twenty-six callers, and two refusals before either. In a multiplayer session
 * only the HOST raises anything -- that is the whole authority rule, in one
 * condition. And nothing is raised while the in-mission sub-state is 34, which
 * CLAUDE.md records as the ESCAPE arm ordinary play is never in; so entering
 * that menu stops the event system rather than merely pausing the frame.
 *
 * The delayed path DROPS four arguments. EventTriggerDelayed takes no masks
 * and no second num/uid pair, so a delayed event cannot carry what an
 * immediate one can -- worth knowing before assuming `delay 0` and `delay 1`
 * differ only in timing.
 *
 * `remote` is passed as 0, which is right: an event raised here is local by
 * construction and EventTriggerImmediate will broadcast it. */
void __cdecl EventNotify(int32_t type, int32_t num1, uint32_t uid1,
                        int32_t maskA, int32_t num2, uint32_t uid2,
                        int32_t maskB, int32_t delay, int32_t removeevent,
                        int32_t arg)
{
    const uint8_t *comm = *(const uint8_t **)AM2_IMAGE(ADDR_COMM_OBJECT);

    if (*(const int32_t *)AM2_IMAGE(ADDR_MP_SESSION) != 0
        && *(const int32_t *)(comm + COMM_OFF_IS_HOST) == 0)
        return;

    if (*(const int32_t *)AM2_IMAGE(ADDR_MENU_MODE)
        == AM2_SUBSTATE_ESCAPE)
        return;

    if (delay > 0) {
        EventTriggerDelayed(type, num1, (int32_t)uid1, delay, removeevent,
                            arg);
        return;
    }

    EventTriggerImmediate(type, num1, uid1, maskA, num2, uid2, maskB,
                          removeevent, 0);
}

/* ----------------------------------------------- delayed trigger ---- */

/* 0x0041F410. Arrange for an event to be raised after a delay.
 *
 * Sixteen bytes are allocated and filled with what the event will need when it
 * fires -- type, num, uid, removeevent -- then a timer is started and
 * ADDR_EVT_RECORD_HANDLER is registered against the id the timer returns, with
 * that record as its argument. The last argument to EventRegister is 1, so the
 * registration OWNS the record and the table's teardown frees it. That is the
 * opposite of what DeclareRuleVars passes, and the difference is why the `if`
 * records it registers are not freed by the table.
 *
 * -100 and -101 are the timer's two failure returns. On either, the record is
 * simply dropped -- the original does not free it, and that leak is
 * reproduced. It is 16 bytes on a path that only runs when timers are
 * exhausted. */
void __cdecl EventTriggerDelayed(int32_t type, int32_t num, int32_t uid,
                                 int32_t delay, int32_t removeevent,
                                 int32_t arg)
{
    int32_t *rec;
    int32_t  id;

    if (kEventDebug)
        orig_log("EventTriggerDelayed: type %d, num: %d, uid: %x, "
                 "removeevent: %d, delay: %d\n",
                 type, num, uid, removeevent, delay);

    rec = (int32_t *)am2_malloc(16);

    rec[0] = type;
    rec[1] = num;
    rec[2] = uid;
    rec[3] = removeevent;

    /* One fire, relative, with a PERIOD OF ZERO -- which is only safe because
     * a single fire can never reach the division: with count == 1 the
     * "already elapsed" test is `start <= now`, and it answers before the
     * catch-up divides by the period. A caller asking for two fires at period
     * zero would divide by zero, and nothing here stops it. */
    id = CreateTimer((uint32_t)delay, 0, 0, 1, -2, arg);

    if (id == -100 || id == -101)
        return;

    EventRegister(1, id, 0, (const void *)AM2_IMAGE(ADDR_EVT_RECORD_HANDLER),
                  rec, 1);
}

/* The mission clock and the timer table, all in the image. */
#define g_gameClockMs  (*(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS))
#define g_timerCount   (*(int32_t *)AM2_IMAGE(ADDR_TIMER_COUNT))
#define g_timers       ((AM2_Timer *)AM2_IMAGE(ADDR_TIMER_TABLE))

int32_t __cdecl CreateTimer(uint32_t start, int32_t absolute, uint32_t period,
                            int32_t count, int32_t id, int32_t lowPriority)
{
    uint32_t now;
    int32_t  i;

    /* Two different thresholds, and the order matters: a low-priority request
     * is refused first and at a lower mark than a slow one. */
    if (lowPriority && g_timerCount > AM2_TIMER_LOWPRI_LIMIT) {
        orig_log("CreateTimer: lowpriority event ignored, count is %d\n",
                 g_timerCount);
        return AM2_TIMER_REFUSED;
    }
    if (period > AM2_TIMER_SLOW_PERIOD
        && g_timerCount > AM2_TIMER_SLOW_LIMIT) {
        orig_log("CreateTimer: long delayed event ignored, count is %d\n",
                 g_timerCount);
        return AM2_TIMER_REFUSED;
    }

    now = g_gameClockMs;
    if (!absolute)
        start += now;

    if (start <= now) {
        uint32_t skip;

        /* Already begun. If even the LAST fire is in the past there is
         * nothing to schedule. */
        if (start + (uint32_t)(count - 1) * period <= now)
            return AM2_TIMER_NO_ROOM;

        /* Catch up rather than fire late: drop the elapsed repeats. */
        skip   = (now - start) / period + 1;
        count -= (int32_t)skip;
        start += skip * period;
        if (count < 1)
            return AM2_TIMER_NO_ROOM;
        if (start <= now)
            return AM2_TIMER_NO_ROOM;
    }

    if (id == -2)
        id = AllocUid();

    /* A slot is free when its id is zero. */
    for (i = 0; i < AM2_TIMER_MAX; i++)
        if (g_timers[i].id == 0)
            break;
    if (i >= AM2_TIMER_MAX)
        return AM2_TIMER_NO_ROOM;

    g_timers[i].start  = start;
    g_timers[i].period = period;
    g_timers[i].count  = count;
    g_timers[i].id     = id;
    g_timerCount++;
    return id;
}

void __cdecl ResetTimers(void)
{
    uint32_t *id;

    *(int32_t *)(uintptr_t)ADDR_EVENT_BLOCK = 0;
    *(int32_t *)(uintptr_t)ADDR_TIMER_COUNT = 0;
    /* From the first record's id field to one past the last, sixteen bytes at
     * a time -- the records themselves are left alone. */
    for (id = (uint32_t *)(uintptr_t)(ADDR_TIMER_TABLE + 0xC);
         id < (uint32_t *)(uintptr_t)ADDR_TIMER_TABLE_ID_END;
         id = (uint32_t *)((uint8_t *)id + 0x10))
        *id = 0;
}

/* EvtDropItem -- original 0x0041FC80, one caller.
 *
 * The `dropitem` action: find a weapon uid among a trooper's inventory slots
 * and hand it to TrooperDropItem at a point.
 *
 * THE SEARCH STARTS AT SLOT 1, NOT SLOT 0. The scan walks
 * UNIT_OFF_INVENTORY + 4 upward for five slots, so a weapon sitting in slot 0
 * can never be dropped by this action. TrooperDropItem refuses slot 0 and slot
 * 6 as well -- `0 < slot < 6` -- so the two agree, and the slot the trooper
 * has in hand is not what this drops.
 *
 * BOTH UIDS ARE REFUSED BELOW AM2_UID_COUNTER_START, which is the same guard
 * the two type-2 shims above use. It is a range test on a uid rather than a
 * null check, and it catches a small integer that arrived where a uid was
 * meant.
 *
 * A ZERO POINT MEANS "WHERE THE TROOPER IS". The test is on the LOW SIXTEEN
 * BITS of the packed point, so a point whose x is 0 and whose y is not still
 * counts as zero and is replaced -- x==0 is the left edge of the map, which is
 * reachable. The original tests `dx`, and that is reproduced rather than
 * widened to the whole dword.
 *
 * THE MULTIPLAYER GUARD IS SKIPPED ENTIRELY IN SINGLE PLAYER. It runs only
 * when there is a session, and then asks CommMustBroadcast about the trooper's
 * own army; without one the action always proceeds. So the whole ownership
 * test is unreachable on every configuration this project can drive, and is
 * verified by reading.
 *
 * The Sarge test is on the object, not on the weapon: only Sarge drops things.
 */
void __cdecl EvtDropItem(uint32_t uid, uint32_t weaponUid, uint32_t at)
{
    uint8_t *obj;
    int32_t  slot;

    if (uid < AM2_UID_COUNTER_START || weaponUid < AM2_UID_COUNTER_START)
        return;

    obj = (uint8_t *)LookupByUID(uid);
    if (!ObjIsType2((const AM2_Object *)obj))
        return;

    if (*(void *const *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)*(const int8_t *)(obj + OBJ_OFF_ARMY)))
        return;

    if (!*(const int32_t *)(obj + OBJ_OFF_SARGE))
        return;

    if (!(uint16_t)at)
        at = *(const uint32_t *)(obj + OBJ_OFF_POS);

    for (slot = 1; slot < AM2_INVENTORY_SLOTS; slot++)
        if (*(const uint32_t *)(obj + UNIT_OFF_INVENTORY
                                + (size_t)slot * 4) == weaponUid) {
            TrooperDropItem(obj, slot, at);
            return;
        }
}

/* EvtArmyAttach -- original 0x0041FDB0, one caller.
 *
 * Attach every object an army owns to one target: resolve the target uid,
 * resolve the army to a comm slot, and walk that slot's object list, calling
 * ObjAttachTo for each live type 2, 3 or 8 that passes the filter.
 *
 * FIFTH LOOP IN THIS TREE THAT DOES NOT ADVANCE OVER A REMOVAL, after
 * DrawSelection, CommReopenSession, Type2ActionAll and AwardOwnArmyXp. The
 * unresolved arm jumps past the increment and the bound is re-read from the
 * list at the bottom of every iteration, so the entry that shifts down is
 * looked at next.
 *
 * THE FILTER IS A VALUE, NOT A PREDICATE, and -1 means "all". When it is
 * anything else the object's ADDR_OBJ_FIELD_A must equal it. That accessor is
 * only consulted when the filter is set, so a caller passing -1 costs nothing
 * per object.
 *
 * THE TARGET IS RESOLVED ONCE, BEFORE THE WALK, and the resulting POINTER is
 * what every attach gets -- the original stores it back over its own argument
 * slot. So an attach that destroys the target would leave the rest of the walk
 * using a stale pointer; ObjAttachTo does not, and the order is the
 * original's.
 *
 * The uid is refused below AM2_UID_COUNTER_START, the same range test the
 * type-2 shims and EvtDropItem use rather than a null check.
 *
 * The list is re-read from ADDR_ARMY_OBJ_LISTS on every iteration and after
 * every removal. Nothing here can move it -- ListRemoveAt compacts in place --
 * but the original reloads, and it is written as the plain indexed access that
 * means.
 */
void __cdecl EvtArmyAttach(int32_t army, int32_t filter, uint32_t uid)
{
    void    *target;
    int32_t  slot;
    uint8_t *list;
    int32_t  i = 0;

    if (uid < AM2_UID_COUNTER_START)
        return;

    target = LookupByUID(uid);
    if (!target)
        return;

    slot = CommSlotForArmy(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                           (int16_t)army);
    list = ((uint8_t **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)[slot];

    while (i < *(const int32_t *)(list + LIST_OFF_COUNT)) {
        uint8_t *obj = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(list + LIST_OFF_UIDS))[i]);

        if (!obj) {
            ListRemoveAt(list, i);
            continue;           /* no step: the shifted-down entry is next */
        }

        if (!(*(const uint8_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            && ObjIsTypeIn238((const AM2_Object *)obj)
            && (filter == AM2_ATTACH_ANY || ObjFieldA(obj) == (uint32_t)filter))
            orig_obj_attach_to(obj, target);

        i++;
    }
}

int event_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_RESET_TIMERS, (const void *)ResetTimers,
                        "ResetTimers", 0);
    rc |= patch_replace(ADDR_CREATE_TIMER, (const void *)CreateTimer,
                        "CreateTimer", 20);
    rc |= patch_replace(ADDR_DECLARE_RULE_VARS, (const void *)DeclareRuleVars,
                        "DeclareRuleVars", 3);
    rc |= patch_replace(ADDR_EVENT_REGISTER, (const void *)EventRegister,
                        "EventRegister", 19);
    rc |= patch_replace(ADDR_EVENT_CLEAR_ALL, (const void *)EventClearAll,
                        "EventClearAll", 2);
    rc |= patch_replace(ADDR_RESET_SCRIPT_STATE,
                        (const void *)ResetScriptState, "ResetScriptState", 1);
    rc |= patch_replace(ADDR_EVT_OBJ_ACTION, (const void *)EvtObjAction,
                        "EvtObjAction", 1);
    rc |= patch_replace(ADDR_EVT_DEPLOY_ITEM, (const void *)EvtDeployItem,
                        "EvtDeployItem", 1);
    rc |= patch_replace(ADDR_EVT_TYPE2_ACTION_A,
                        (const void *)EvtType2ActionA, "EvtType2ActionA", 1);
    rc |= patch_replace(ADDR_EVT_TYPE2_ACTION_B,
                        (const void *)EvtType2ActionB, "EvtType2ActionB", 1);
    rc |= patch_replace(ADDR_EVT_TYPE2_ACTION_C,
                        (const void *)EvtType2ActionC, "EvtType2ActionC", 1);
    rc |= patch_replace(ADDR_EVT_DROP_ITEM, (const void *)EvtDropItem,
                        "EvtDropItem", 1);
    rc |= patch_replace(ADDR_EVT_ARMY_ATTACH, (const void *)EvtArmyAttach,
                        "EvtArmyAttach", 1);
    rc |= patch_replace(ADDR_EVT_OBJ_SET, (const void *)EvtObjSet,
                        "EvtObjSet", 1);
    rc |= patch_replace(ADDR_EVT_GUARDED_ACTION,
                        (const void *)EvtGuardedAction, "EvtGuardedAction", 1);
    rc |= patch_replace(ADDR_EVT_ARMY_SET_FIELD,
                        (const void *)EvtArmySetField, "EvtArmySetField", 3);
    rc |= patch_replace(ADDR_EVT_OBJ_PAIR, (const void *)EvtObjPair,
                        "EvtObjPair", 1);
    rc |= patch_replace(ADDR_EVT_SHOW_BITMAP, (const void *)EvtShowBitmap,
                        "EvtShowBitmap", 1);
    rc |= patch_replace(ADDR_EVT_SHOW_BITMAP_NP,
                        (const void *)EvtShowBitmapNoPause,
                        "EvtShowBitmapNoPause", 1);
    rc |= patch_replace(ADDR_EVT_FLAG40_CLEAR, (const void *)EvtFlag40Clear,
                        "EvtFlag40Clear", 1);
    rc |= patch_replace(ADDR_EVT_FLAG40_SET, (const void *)EvtFlag40Set,
                        "EvtFlag40Set", 1);
    rc |= patch_replace(ADDR_EVT_TYPE238_ACTION,
                        (const void *)EvtType238Action, "EvtType238Action", 1);
    rc |= patch_replace(ADDR_EVT_PUSH_OBJ_CTX, (const void *)EvtPushObjCtx,
                        "EvtPushObjCtx", 1);
    rc |= patch_replace(ADDR_SCRIPT_SET_OBJ_TABLE,
                        (const void *)ScriptSetObjTable,
                        "ScriptSetObjTable", 1);
    rc |= patch_replace(ADDR_EVAL_COND_TESTS, (const void *)EvalCondTests,
                        "EvalCondTests", 6);
    rc |= patch_replace(ADDR_ADVANCE_MISSION, (const void *)AdvanceMission,
                        "AdvanceMission", 2);
    rc |= patch_replace(ADDR_ACTION_POINT, (const void *)ActionPoint,
                        "ActionPoint", 14);
    rc |= patch_replace(ADDR_MISSION_STARTUP, (const void *)MissionStartup,
                        "MissionStartup", 0);
    rc |= patch_replace(ADDR_TIMER_TICK, (const void *)TimerTick,
                        "TimerTick", 0);
    rc |= patch_replace(ADDR_EVENT_NOTIFY, (const void *)EventNotify,
                        "EventNotify", 26);
    rc |= patch_replace(ADDR_SCRIPT_RESURRECT_ITEM,
                        (const void *)ScriptResurrectItem,
                        "ScriptResurrectItem", 1);
    rc |= patch_replace(ADDR_EVT_ARMY_AT_POINT,
                        (const void *)EvtArmyAtPoint, "EvtArmyAtPoint", 3);
    rc |= patch_replace(ADDR_AT_POINT_A, (const void *)EvtAtPointA,
                        "EvtAtPointA", 2);
    rc |= patch_replace(ADDR_AT_POINT_C, (const void *)EvtAtPointC,
                        "EvtAtPointC", 3);
    rc |= patch_replace(ADDR_EVT_AT_OBJ_POS_C, (const void *)EvtAtObjPosC,
                        "EvtAtObjPosC", 1);
    rc |= patch_replace(ADDR_EVT_BY_REF_A, (const void *)EvtByRefA,
                        "EvtByRefA", 1);
    rc |= patch_replace(ADDR_EVT_BY_REF_B, (const void *)EvtByRefB,
                        "EvtByRefB", 1);
    rc |= patch_replace(ADDR_EVT_AT_OBJ_POS_A, (const void *)EvtAtObjPosA,
                        "EvtAtObjPosA", 1);
    rc |= patch_replace(ADDR_EVT_AT_OBJ_POS_B, (const void *)EvtAtObjPosB,
                        "EvtAtObjPosB", 1);
    rc |= patch_replace(ADDR_EVT_SET_FIELD_540, (const void *)EvtSetField540,
                        "EvtSetField540", 2);
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
    rc |= patch_replace(ADDR_EVT_SET_BYTE530, (const void *)EvtSetByte530,
                        "EvtSetByte530", 2);
    rc |= patch_replace(ADDR_LOAD_SCRIPT_CONDS, (const void *)LoadScriptConditions,
                        "LoadScriptConditions", 1);
    rc |= patch_replace(ADDR_SAVE_SCRIPT_CONDS, (const void *)SaveScriptConditions,
                        "SaveScriptConditions", 1);
    rc |= patch_replace(ADDR_SAVE_EVENT_BLOCK, (const void *)SaveEventBlock,
                        "SaveEventBlock", 1);
    rc |= patch_replace(ADDR_LOAD_EVENT_BLOCK, (const void *)LoadEventBlock,
                        "LoadEventBlock", 1);
    rc |= patch_replace(ADDR_LOAD_EVENT_SECTION, (const void *)LoadEventSection,
                        "LoadEventSection", 1);
    rc |= patch_replace(ADDR_SAVE_EVENT_SECTION, (const void *)SaveEventSection,
                        "SaveEventSection", 1);
    rc |= patch_replace(ADDR_RESOLVE_UID, (const void *)ResolveUid,
                        "ResolveUid", 2);
    rc |= patch_replace(ADDR_FIRE_WEAPON_AT_POINT,
                        (const void *)FireWeaponAtPoint,
                        "FireWeaponAtPoint", 4);
    rc |= patch_replace(ADDR_FIRE_WEAPON_AT_OBJECT,
                        (const void *)FireWeaponAtObject,
                        "FireWeaponAtObject", 4);
    rc |= patch_replace(ADDR_UNIT_FIRE_AT_POINT,
                        (const void *)UnitFireAtPoint,
                        "UnitFireAtPoint", 3);
    rc |= patch_replace(ADDR_UNIT_FIRE_AT_OBJECT,
                        (const void *)UnitFireAtObject,
                        "UnitFireAtObject", 3);
    rc |= patch_replace(ADDR_COND_RUN_ACTION, (const void *)CondRunAction,
                        "CondRunAction", 4);
    rc |= patch_replace(ADDR_COND_RUN_ACTIONS, (const void *)RunCondActions,
                        "RunCondActions", 2);
    rc |= patch_replace(ADDR_EVENT_TRIGGER_IMMED,
                        (const void *)EventTriggerImmediate,
                        "EventTriggerImmediate", 1);
    rc |= patch_replace(ADDR_EVENT_TRIGGER_DELAYED,
                        (const void *)EventTriggerDelayed,
                        "EventTriggerDelayed", 1);
    rc |= patch_replace(ADDR_SCRIPT_SET_OBJ_BITMAP,
                        (const void *)ScriptSetObjBitmap,
                        "ScriptSetObjBitmap", 1);
    rc |= patch_replace(ADDR_EVENT_MESSAGE_SEND,
                        (const void *)EventMessageSend, "EventMessageSend", 1);
    rc |= patch_replace(ADDR_EVENT_MESSAGE_RECV,
                        (const void *)EventMessageReceive,
                        "EventMessageReceive", 1);
    rc |= patch_replace(ADDR_LOAD_SCRIPT_COND, (const void *)LoadScriptCond,
                        "LoadScriptCond", 2);
    rc |= patch_replace(ADDR_SAVE_SCRIPT_COND, (const void *)SaveScriptCond,
                        "SaveScriptCond", 2);
    rc |= patch_replace(ADDR_EVENT_DEFAULT_NAME, (const void *)EventDefaultName,
                        "EventDefaultName", 3);
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
    rc |= patch_replace(ADDR_EVT_SET_ALLIED, (const void *)EvtSetAllied,
                        "EvtSetAllied", 2);
    rc |= patch_replace(ADDR_EVT_CLEAR_ALLIED, (const void *)EvtClearAllied,
                        "EvtClearAllied", 2);
    return rc;
}
