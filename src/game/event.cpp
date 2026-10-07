/* event.cpp -- see event.h. */
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "crt.h"
#include "event.h"
#include "gameproc.h"  /* SaveGame -- reconstructed */
#include "image.h"
#include "misc.h"      /* FilterMatches */
#include "armymsg.h"   /* SendGamePause */
#include "gamedir.h"   /* SetGameDir */
#include "objscript.h" /* AM2_ObjScript, kObjScripts */
#include "scriptint.h"
#include "air.h"      /* ObjConceal -- reconstructed */
#include "pad.h"      /* EvtPadOn/EvtPadOff -- compared by POINTER below */
#include "objtable.h"
#include "dist.h"     /* AM2_Point, AngleBetween */
#include "region.h"   /* ActivateRegion, InactivateRegion */
#include "packkey.h"  /* KeyLookupTriple */
#include "map.h"      /* TileOfPoint */
#include "item.h"   /* UidOnWire */
#include "objtype.h"
#include "army.h"     /* AllyFlag -- reconstructed */
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


#define kScriptConditions (g_scriptConditions)

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

#define kEventTable (g_eventTable)
#define kImageFn(a)       ((const void *)((void *)(uintptr_t)(a)))


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

    /* Written out one registration at a time, as the image has them.
     *
     * The eight win conditions exist whether a script mentions them or not.
     * Each is declared into the name table on the spot -- ScriptNameUid
     * creates the entry when the lookup misses -- so a mission can test
     * `greenwins` without declaring it. The three rule events are not named
     * at all: each takes a fresh uid, parked in a global for whoever raises
     * it later. */
#define WIN(name, handler, army)                                            \
    EventRegister(0, ScriptNameUid((const char *)(uintptr_t)(name)), 0,     \
                  (const void *)(handler), (void *)(uintptr_t)(army), 0);
    WIN(((uintptr_t)g_nameGreenwins),     EvtArmyWins, 0)
    WIN(((uintptr_t)g_nameTanwins),       EvtArmyWins, 1)
    WIN(((uintptr_t)g_nameBluewins),      EvtArmyWins, 2)
    WIN(((uintptr_t)g_nameGreywins),      EvtArmyWins, 3)
    WIN(((uintptr_t)g_nameGreenteamwins), EvtTeamWins, 0)
    WIN(((uintptr_t)g_nameTanteamwins),   EvtTeamWins, 1)
    WIN(((uintptr_t)g_nameBlueteamwins),  EvtTeamWins, 2)
    WIN(((uintptr_t)g_nameGreyteamwins),  EvtTeamWins, 3)
#undef WIN

#define RULE(handler, slot) {                                               \
        int32_t uid = AllocUid();                                           \
        *(int32_t *)(uintptr_t)(slot) = uid;                                \
        EventRegister(0, uid, 0, (const void *)(handler), 0, 0);            \
    }
    RULE(EvtRuleA, ((uintptr_t)&g_ruleUidA))
    RULE(EvtRuleB, ((uintptr_t)&g_ruleUidB))
    RULE(EvtRuleC, ((uintptr_t)&g_ruleUidC))
#undef RULE

    /* One registration per event term of every `if`, with the condition itself
     * as the callback's argument -- which is how a fired event finds the
     * actions to run. `timeabsolute` is the exception: it has no event terms
     * at all, so it invents a uid, registers that, and announces it straight
     * away with the absolute time as the payload. */
    for (; cond; cond = (AM2_ScriptCond *)cond->next) {
        if (cond->kind == AM2_IF_TIMEABSOLUTE) {
            int32_t uid = AllocUid();

            /* By NAME now that it is ours -- checkseams.py caught both of
             * these the moment the patch went in, which is the fourth
             * spelling of that seam and the one that looks least like a
             * call. The cost is the usual one: the dispatcher reaches our
             * code without crossing the patched entry, so EvtCondition's
             * counter is blind by construction. */
            EventRegister(0, uid, 0, (const void *)EvtCondition, cond, 0);
            EventNotify(0, uid, 0, 0, 0, 0, 0, cond->number, 1, 0);
            continue;
        }

        for (int32_t i = 0; i < cond->nevents; i++)
            EventRegister(cond->events[i].a, cond->events[i].b,
                                cond->events[i].c,
                                (const void *)EvtCondition, cond, 0);
    }
}

/* ---- the object setters ------------------------------------------------ */

/* LookupByUID and ObjIsTypeIn238 are both reconstructed, so these call them
 * directly rather than through the image. Reaching for an orig_ macro here was
 * caught by tools/checkseams.py, which exists for exactly that. */

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
    AM2_Object *o;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    /* The object, not a pointer to its +8: the original addresses [eax+8]. */
    o = LookupByUID(uid);
    if (on)
        o->flags |= AM2_EVT_FLAG810;
    else
        o->flags &= ~AM2_EVT_FLAG810;
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
    AM2_Object *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;
    obj = LookupByUID(uid);
    if (!ObjIsTypeIn238(obj))
        return;

    obj->aiModePrev = obj->aiMode;
    obj->aiMode = mode;

    if (mode != AM2_AI_MODE_DEFEND)
        return;
    /* Tests sixteen bits and writes thirty-two -- the original's asymmetry,
     * not a transcription slip. */
    if (obj->scriptPoint.x != 0)
        return;
    obj->scriptPoint = obj->pos;
}

/* PlayDynamicSound is reconstructed, in win32/audio.cpp with the rest of the
 * sound code. Declared here rather than by including that header because
 * event.cpp is on the flat side of the split and must name no Win32 or COM
 * type -- the same reason script.cpp declares PreloadSprite itself. */
extern "C" void __cdecl PlayDynamicSound(const char *name, int32_t loop,
                                         int32_t unused, int16_t x, int16_t y,
                                         int32_t slot, int32_t priority,
                                         uint32_t owner);

void __cdecl EvtPlaySoundAt(const char *name, AM2_Point point, int32_t slot,
                            int32_t priority, int32_t loop)
{
    PlayDynamicSound(name, loop, 0, point.x,
                     point.y, slot, priority, 0);
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
    /* Walked through the list head itself, re-read after every free. */
    while (kScriptConditions) {
        AM2_ScriptCond *next = (AM2_ScriptCond *)kScriptConditions->next;

        if (kScriptConditions->events)
            am2_free(kScriptConditions->events);
        if (kScriptConditions->tests)
            am2_free(kScriptConditions->tests);
        if (kScriptConditions->actions)
            am2_free(kScriptConditions->actions);
        am2_free(kScriptConditions);

        kScriptConditions = next;
    }
    kScriptConditions = NULL;
}

void __cdecl EventDefaultName(int32_t kind, int32_t number, char *out)
{
    /* The arms are laid out 0, 3, 2, 4..8, 1 -- the order they are written. */
    switch (kind) {
    case AM2_EVT_CONTROL:
        sprintf(out, "unnamed Event_Control %d", number);
        break;
    case AM2_EVT_PADON:
        sprintf(out, "unnamed Event_PadActivated %d", number);
        break;
    case AM2_EVT_PADOFF:
        sprintf(out, "unnamed Event_PadDeactivated %d", number);
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
    case 1:
        /* Nothing produces kind 1, and this is the only place that says so in
         * code rather than by omission: no name at all, not a placeholder. */
        out[0] = '\0';
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
void __cdecl FreeBitmap(void **pp);

#define orig_fwrite (*(am2_fwrite_fn)ADDR_FWRITE)

void __cdecl SaveScriptCond(am2_FILE *fp, const AM2_ScriptCond *cond)
{
    int32_t i;

    orig_fwrite(cond, 0x30, 1, fp);

    for (i = 0; i < cond->nevents; i++)
        orig_fwrite(&cond->events[i], 0x10, 1, fp);

    for (i = 0; i < cond->nactions; i++) {
        /* A copy of the action, whose text slot carries the length instead
         * of the pointer. */
        AM2_ScriptAction copy = cond->actions[i];

        *(int32_t *)&copy.text = cond->actions[i].text
                                     ? (int32_t)strlen(cond->actions[i].text)
                                     : 0;
        orig_fwrite(&copy, 0x48, 1, fp);
        if (*(const int32_t *)&copy.text > 0)
            orig_fwrite(cond->actions[i].text,
                        (size_t)*(const int32_t *)&copy.text, 1, fp);
    }

    for (i = 0; i < cond->ntests; i++)
        orig_fwrite(&cond->tests[i], 0x1C, 1, fp);
}

#define orig_fread  (*(am2_fread_fn)ADDR_FREAD)
#define orig_malloc (*(am2_malloc_fn)ADDR_GAME_MALLOC)

#define kScriptContext (*(AM2_ScriptCtx *)g_scriptContext)

int32_t __cdecl LoadScriptCond(am2_FILE *fp, AM2_ScriptCond *cond)
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
        orig_fread(&cond->events[i], 0x10, 1, fp);

    if (cond->nactions > 0)
        cond->actions = (AM2_ScriptAction *)orig_malloc((size_t)cond->nactions * 0x48);
    for (i = 0; i < cond->nactions; i++) {
        int32_t len;

        orig_fread(&cond->actions[i], 0x48, 1, fp);
        /* The text slot holds the length on disk. */
        len = (int32_t)(uintptr_t)cond->actions[i].text;
        if (len > 0) {
            int32_t at;

            orig_fread(buf, (size_t)len, 1, fp);
            buf[len] = '\0';
            /* Kind 5 is the one that owns a malloc'd copy, so the token list
             * becomes the string's owner. `at` is read before the append. */
            at = kScriptContext.count;
            ScriptAddToken(&kScriptContext, AM2_TOKEN_STRING, buf, 0);
            cond->actions[i].text = (char *)kScriptContext.tokens[at].value;
        }
    }

    if (cond->ntests > 0)
        cond->tests = (AM2_ScriptTest *)orig_malloc((size_t)cond->ntests * 0x1C);
    for (i = 0; i < cond->ntests; i++)
        orig_fread(&cond->tests[i], 0x1C, 1, fp);
    return 1;
}

#define kPads ((AM2_Pad *)g_pads)

int32_t __cdecl LoadEventSection(am2_FILE *fp)
{
    int32_t tag, unused, key, kind, idx;
    AM2_Pad *pad;

    if (!CheckSaveTag(fp, AM2_SAVETAG_EVENT,
                      "C:\\ArmyMen2\\source\\event.cpp", 0xCCA))
        return 0;

    orig_fread(&tag, 4, 1, fp);
    while (tag == (int32_t)AM2_SAVE_RECORD_MARK) {
        /* Three dwords. The first is read and dropped -- see event.h. */
        orig_fread(&unused, 4, 1, fp);
        orig_fread(&key, 4, 1, fp);
        orig_fread(&kind, 4, 1, fp);
        (void)unused;

        /* One arm per kind, each with its own registration. Both pad arms
         * write +0x28, which is the ORIGINAL's behaviour and is reproduced:
         * only the enter handler reads that field, so a restored leave
         * registration compares +0x2C, which nothing here wrote, and
         * clobbers the enter uid on the way past. Verified at 0x0042268F
         * and 0x004226D5 -- the second arm kept the first's displacement.
         * Not fixed. */
        if (kind == (int32_t)AM2_EVTSAVE_PAD_A) {
            orig_fread(&idx, 4, 1, fp);
            pad = &kPads[idx];
            *(int32_t *)pad->rest = key;              /* +0x28 */
            EventRegister(0, key, 0, (const void *)EvtPadOn, pad, 0);
        } else if (kind == (int32_t)AM2_EVTSAVE_PAD_B) {
            orig_fread(&idx, 4, 1, fp);
            pad = &kPads[idx];
            *(int32_t *)pad->rest = key;              /* +0x28 */
            EventRegister(0, key, 0, (const void *)EvtPadOff, pad, 0);
        } else if (kind == (int32_t)AM2_EVTSAVE_OWNED) {
            void *rec = orig_malloc(0x10);

            orig_fread(rec, 0x10, 1, fp);
            /* owns = 1: the table's teardown frees this one. */
            EventRegister(1, key, 0,
                          (const void *)EvtRecordHandler,
                          rec, 1);
        }

        orig_fread(&tag, 4, 1, fp);
    }

    return 1;
}

int32_t __cdecl SaveEventSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_EVENT);

    for (int32_t b = 0; b < AM2_EVENT_BUCKETS; b++) {
        for (AM2_EventEntry *e = kEventTable[b]; e; e = e->next) {
            for (AM2_EventHandler *h = e->handlers; h; h = h->next) {
                /* Tested in this order, and the two pad arms want an argument
                 * while the owned one does not. A pad handler with a null
                 * argument falls through every arm and is skipped. Each arm
                 * writes its whole record -- the image has the four tags
                 * three times over -- and the pad index is an UNSIGNED
                 * byte difference over the record size. The loader drops
                 * the bucket number. */
                if (h->fn == (const void *)EvtPadOn && h->arg) {
                    WriteSaveTag(fp, AM2_SAVE_RECORD_MARK);
                    WriteSaveTag(fp, (uint32_t)b);
                    WriteSaveTag(fp, (uint32_t)e->key0);
                    WriteSaveTag(fp, AM2_EVTSAVE_PAD_A);
                    WriteSaveTag(fp, (uint32_t)((uint8_t *)h->arg
                                                - (uint8_t *)kPads)
                                     / (uint32_t)sizeof(AM2_Pad));
                } else if (h->fn == (const void *)EvtPadOff && h->arg) {
                    WriteSaveTag(fp, AM2_SAVE_RECORD_MARK);
                    WriteSaveTag(fp, (uint32_t)b);
                    WriteSaveTag(fp, (uint32_t)e->key0);
                    WriteSaveTag(fp, AM2_EVTSAVE_PAD_B);
                    WriteSaveTag(fp, (uint32_t)((uint8_t *)h->arg
                                                - (uint8_t *)kPads)
                                     / (uint32_t)sizeof(AM2_Pad));
                } else if (h->fn == (const void *)EvtRecordHandler) {
                    WriteSaveTag(fp, AM2_SAVE_RECORD_MARK);
                    WriteSaveTag(fp, (uint32_t)b);
                    WriteSaveTag(fp, (uint32_t)e->key0);
                    WriteSaveTag(fp, AM2_EVTSAVE_OWNED);
                    orig_fwrite(h->arg, 0x10, 1, fp);
                }
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
                      "C:\\ArmyMen2\\source\\event.cpp", 0x173))
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

#define kEventBlock ((void *)(uintptr_t)((void *)&g_eventBlock))

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
                      "C:\\ArmyMen2\\source\\event.cpp", 0xD9))
        return 0;

    /* The size, through the same helper, one source line later. */
    if (!CheckSaveTag(fp, AM2_EVENT_BLOCK_SIZE,
                      "C:\\ArmyMen2\\source\\event.cpp", 0xDB))
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
    AM2_Object           *obj;

    if (kScriptNames[nameidx].type != AM2_NAME_TYPE_REF) {
        orig_log("ERROR: ScriptSetObjBitmap was called with %s which is not "
                 "an item\n", kScriptNames[nameidx].name);
        return;
    }

    /* The uid in a local of its own: the image then loads it into eax, the
     * table base's register, where a read straight into the argument takes
     * the next scratch register. */
    {
        uint32_t uid = (uint32_t)kScriptNames[nameidx].value;
        obj = LookupByUID(uid);
    }

    if (ObjIsItem(obj)) {
        if (!ChangeObjectFrame(obj, frame, 1))
            orig_log("Error: ChangeObjectFrame returned false when passed %s "
                     "with index %d\n", kScriptNames[nameidx].name, frame);
    } else {
        orig_log("ERROR: ScriptSetObjBitmap was called with %s which is not "
                 "a valid object\n", kScriptNames[nameidx].name);
    }
}

/* ------------------------------------------------ event messages ---- */

/* The event logging switch, a field of the comm object. The logger it gates is
 * stubbed to `ret` in this build, so the whole branch is inert -- reproduced
 * because the call sites are what carry the format strings that named these
 * functions in the first place. */
#define kEventDebug \
    (*(const int32_t *)((const uint8_t *)*(void * *)&g_commObject \
                        + COMM_OFF_VERBOSE))


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
    msg.uid         = 0;                  /* before kind, the original's order */
    msg.kind        = AM2_ARMY_MSG_EVENT;
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
    char    name[0x10];     /* "startup%d" */
    char    save[0x40];     /* "map%d_mission%d.sav" -- a buffer of its own */
    int32_t level = g_levelIndex;
    int32_t id;

    if (level <= 0)
        level = 1;

    sprintf(name, "startup%d", level);
    id = ScriptNameUid(name);
    if (id > 0)
        EventNotify(0, id, 0, 0, 0, 0, 0, 0, 1, 0);

    g_scriptStateFlag = 1;

    if (g_levelId <= 0)
        return;
    if (!*(const uint8_t *)&g_gameprocBlock)
        return;
    if (g_winEnabled)
        return;

    sprintf(save, "map%d_mission%d.sav",
            g_levelId,
            g_levelIndex);
    SaveGame(save);
    strcpy(&g_gameprocStrB, save);
}

/* The count and the table are ONE object in the image: a store into a timer
 * is a store the compiler has to assume reaches the count, which is why the
 * image reads the count again after filling a slot. Viewed from the count. */
typedef struct { int32_t count; AM2_Timer timers[AM2_TIMER_MAX]; } AM2_TimerBlock;
#define g_timerCount   (g_timerBlock.count)
#define g_timers       (g_timerBlock.timers)

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
#define NOW  (g_gameClockMs)
#define LAST (g_eventBlock)
    AM2_Timer *timers = g_timers;
    int32_t    i;

    /* The clock is read again at every use. */
    if (NOW - LAST < AM2_TIMER_TICK_MS)
        return;
    LAST = NOW;

    for (i = 0; i < AM2_TIMER_MAX; i++) {
        AM2_Timer *t = &timers[i];

        if (!t->id)
            continue;
        if (t->start > NOW)
            continue;

        EventNotify(1, t->id, 0, 0, 0, 0, 0, 0, t->count == 1, 0);

        if (--t->count <= 0) {
            t->id = 0;
            --g_timerCount;
        } else {
            t->start = NOW + t->period;
        }
    }
#undef NOW
#undef LAST
}

/* --------------------------------------------- object shims ---- */

typedef void (__cdecl *AM2_Type2ActionFn)(void *obj);
typedef void (__cdecl *AM2_Type2ActionArgFn)(void *obj, int32_t arg);
typedef void (__cdecl *AM2_GuardedActionFn)(void *obj, int32_t a, int32_t b,
                                            int32_t c, int32_t d, int32_t e);
typedef void (__cdecl *AM2_AtPointAFn)(int32_t a, AM2_Point point, int32_t c);
typedef void (__cdecl *AM2_PointActionFn)(void *obj, AM2_Point point);

/* 0x0041FE70. Deploy the object a uid names.
 *
 * `where` is a packed point, and a zero LOW WORD -- the x, tested as a 16-bit
 * value -- means "leave it where it is", at which point the object's own
 * position at +0x12 is used instead. Note the test is on the word, not the
 * whole dword, so a point with x == 0 and any y is treated as absent.
 *
 * Checks the pointer rather than the uid, like EvtObjAction and EvtSetByte40.
 */
void __cdecl EvtDeployItem(uint32_t uid, AM2_Point where)
{
    AM2_Object *obj = LookupByUID(uid);

    if (obj == (AM2_Object *)0)
        return;

    if (where.x == 0)
        where = obj->pos;

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

    Type2ActionA((AM2_Object *)obj);
}

void __cdecl EvtType2ActionB(uint32_t uid)
{
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (!ObjIsType2((const AM2_Object *)obj))
        return;

    Type2ActionB((AM2_Object *)obj);
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

    Type2ActionC((AM2_Object *)obj, arg);
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
    void *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);
    HealObject((AM2_Object *)obj, value, 0);
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
void __cdecl ScriptResurrectItem(uint32_t uid, AM2_Point where)
{
    AM2_Object *obj;

    /* `where` is an AM2_Point by value; its halves are read out of the
     * argument slot. */
    if (kEventDebug)
        orig_log("ScriptResurrectItem, uid=%x, pos=(%d,%d)\n", uid,
                 (int32_t)where.x,
                 (int32_t)where.y);

    obj = LookupByUID(uid);

    if (obj == (AM2_Object *)0)
        return;

    if (where.x == 0)
        where = obj->pos;

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
void __cdecl EvtArmyAtPoint(int32_t army, int32_t filter, AM2_Point point,
                            int32_t relative)
{
    int32_t  i;

    /* The slot goes back into `army`. */
    army = ((AM2_Comm *)*(void * *)&g_commObject)->CommSlotForArmy(army);
    i = 0;

    /* The slot's list is re-indexed at every use. */
#define list (g_armyObjLists[army])

    while (i < list->count) {
        AM2_Object *obj = LookupByUID(
            list->uids[i]);

        if (obj == 0) {
            ((AM2_Rec *)list)->ListRemoveAt(i);
            continue;                       /* the list shifted; do not ++ */
        }

        if (obj->flagsLo & OBJ_FLAG8_BLOCKED) {
            i++;
            continue;
        }

        if (filter != -1 && filter != (int32_t)ObjFieldA(obj)) {
            i++;
            continue;
        }

        if (relative) {
            /* Cumulative across objects, as the original's: each adds into
             * the argument's halves. */
            point.x = (int16_t)(point.x + obj->pos.x);
            point.y = (int16_t)(point.y + obj->pos.y);
        }

        PointActionA(obj, point);
        i++;
    }
#undef list
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
void __cdecl EvtAtPointA(uint32_t uid, AM2_Point point, int32_t relative)
{
    AM2_Object *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (obj == 0)
        return;

    if (relative) {
        /* Added into the argument's own halves. */
        point.x = (int16_t)(point.x + obj->pos.x);
        point.y = (int16_t)(point.y + obj->pos.y);
    }

    if (ObjIsType2(obj))
        obj->field540 = 0;

    PointActionA(obj, point);
}

void __cdecl EvtAtPointC(uint32_t uid, AM2_Point point, int32_t relative)
{
    AM2_Object *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (obj == 0)
        return;

    /* The point's halves read as words out of its argument slot; the
     * relative sum built as a struct (the image puts it in uid's slot). */
#define PT point
    if (relative) {
        AM2_Point at;

        at.x = (int16_t)(obj->pos.x + PT.x);
        at.y = (int16_t)(obj->pos.y + PT.y);
        PointActionC(obj, at);
        return;
    }

    if (obj->pos.x == PT.x
        && obj->pos.y == PT.y)
        return;

    PointActionC(obj, PT);
#undef PT
}

/* 0x0041F7F0. The "On" wrapper for EvtAtPointC, and the third of that shape:
 * `target` is acted on, `at` only supplies a position. */
void __cdecl EvtAtObjPosC(uint32_t target, uint32_t at, int32_t relative)
{
    const AM2_Object *obj;

    if (at < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(at);

    if (obj == (const AM2_Object *)0)
        return;

    EvtAtPointC(target, obj->pos, relative);
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
void __cdecl EvtByRefA(AM2_Point a, int32_t b)
{
    SeqAddKind5(&a, 0, b);
}

void __cdecl EvtByRefB(AM2_Point a, int32_t b)
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
    AM2_Object *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (obj == 0)
        return;

    if (obj->flagsLo & OBJ_FLAG8_BLOCKED)
        return;

    if (obj->health <= 0)
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
    const AM2_Object *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (obj == 0)
        return;

    EvtAtPointA(a, obj->pos, c);
}

void __cdecl EvtAtObjPosB(int32_t a, int32_t b, uint32_t uid, int32_t d)
{
    const AM2_Object *obj;

    if (uid < AM2_UID_COUNTER_MIN)
        return;

    obj = LookupByUID(uid);

    if (obj == 0)
        return;

    EvtArmyAtPoint(a, b, obj->pos, d);
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

    g_scriptStateFlag = 0;
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

    DestroyByType((AM2_Object *)obj);
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
    /* The name table's globals, read directly. */
    if (name < 0 || name >= kScriptNameCount)
        return 0;

    if (name == g_svarMe) {
        if (me != 0)
            return me;

        orig_log("Bad ME\n");
        return 0;
    }

    if (kScriptNames[name].type != AM2_NAME_TYPE_REF)
        return 0;

    return (uint32_t)kScriptNames[name].value;
}

/* ------------------------------------------- condition actions ---- */

typedef int32_t (__cdecl *AM2_RandFn)(void);
/* The action EXECUTOR stays original -- 4096 bytes in this same module, and
 * the one thing under the condition layer still not reconstructed. */
typedef void (__cdecl *AM2_RunScriptActionFn)(AM2_ScriptAction *act,
                                              void *arg);
#define orig_rand        (*(AM2_RandFn)((void *)(uintptr_t)(ADDR_GAME_RAND)))

/* 0x00421410. Run the i'th action of a condition.
 *
 * All it does is index: the action array is 0x48 bytes a stride, which is
 * AM2_ScriptAction's size arrived at here for the third time and from a third
 * direction -- the parser writes that record, SaveObjScriptSection copies it,
 * and this strides it. Four callers, so it is the shared way in rather than
 * something RunCondActions invented. */
static void __cdecl CondRunAction(AM2_ScriptCond *c, int32_t i, void *arg)
{
    RunScriptAction(&c->actions[i], arg);
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
        for (int32_t i = 0; i < c->nactions; i++)
            CondRunAction(c, i, arg);
        return;

    case 1: {
        int32_t pick = orig_rand() % c->nactions;

        CondRunAction(c, pick, arg);
        return;
    }

    case 2:
        /* The cursor is re-read after the action has run. */
        CondRunAction(c, *(const int8_t *)&c->cursor, arg);
        *(int8_t *)&c->cursor =
            (int8_t)((*(const int8_t *)&c->cursor + 1) % c->nactions);
        return;

    case 3: {
        AM2_Object       *obj;
        AM2_ObjScript *scr;
        int32_t        id;

        obj = LookupByUID(ResolveUid(c->objstate, (uint32_t)(uintptr_t)arg));

        if (!ObjIsItem(obj))
            return;

        id = obj->scriptId;
        if (id <= 0 || id > kObjScriptCount)
            return;

        scr = &kObjScripts[id - 1];

        for (int32_t i = 0; i < c->nactions; i++) {
            /* The entry is indexed afresh at each use, as the image does. */
#define E (kScriptNames[c->actions[i].extra])
            if (E.type != AM2_NAME_TYPE_REF)
                continue;

            if (E.value >= scr->statecount) {
                orig_log("Tried to switch on invalid state.\n");
                continue;
            }

            if (obj->scriptState == E.value) {
                CondRunAction(c, i, arg);
                return;
            }
#undef E
        }
        return;
    }

    default:
        return;
    }
}

/* EvtCondition -- original 0x00421E80, 976 bytes, cdecl, THIRTEEN exits.
 * Every `if` in a mission script, one registration per event term.
 *
 * IT NAMES ITSELF: "ERROR: a compound event got called for an event it
 * didn't care about" is what it logs when no term matches, which is how a
 * function with no callers of its own can still be identified. Nothing calls
 * it directly -- both `push 0x421e80` sites hand it to EventRegister as a
 * handler, so its arguments come from the dispatcher.
 *
 * EIGHT ARGUMENTS, and that is measured rather than counted by eye. The
 * dispatcher is `call dword ptr [esi]` at 0x0041F060 with `add esp, 0x20`
 * and eight pushes, and the handler node's own stored argument is pushed
 * FIRST, so the condition arrives LAST. EvtArmyWins reads its argument from
 * the same place, which is the second witness.
 *
 * The frame needed the tool. Doing it by hand put 0x00421EB1's operand on
 * the saved-ebx slot, which is not an argument at all; espmap says argument
 * 2, and it was wrong the same way about 0x00421EAC. See the espmap commit
 * -- it could not read this function until it stopped resolving the address
 * to its merged functions.tsv entry.
 *
 * NINE ARMS, EIGHT TARGETS, and script.h had already written down what each
 * one means. The kinds are the `if` grammar's forms, so the table and the
 * parser agree without either being derived from the other:
 *
 *   0 plain and 5 timeabsolute SHARE an arm -- evaluate the tests and run,
 *     no state at all. 5 is also the kind the prologue lets through with no
 *     matching term, and event.cpp already says why: `timeabsolute` has no
 *     event terms, so it registers an invented uid and announces itself.
 *   1 allof     every term must have fired.
 *   2 inorder   the matched term's INDEX must equal the running count, so
 *               they have to arrive in sequence.
 *   3 count n   n distinct terms, each counted once through its flag.
 *   4 repeat n  n firings, with no flags at all -- so one term firing n
 *               times satisfies it, which is the difference from 3.
 *   6 after     the terms are PAIRS: an even index is halved to index the
 *               pair and the target is nevents/2, which is `A after B and
 *               C after D` in the grammar.
 *   7, 8 butnot the last term is the exception: it only ever sets its flag
 *               and returns, and the others fire the condition only while
 *               that flag is still CLEAR.
 *
 * THE `butnot` RESET IS A DEFECT IN THE ORIGINAL AND IS REPRODUCED. Arm 1's
 * clear-down walks the array with a cursor stepping 0x10; arm 7's is the
 * same loop written with the MATCHED term's fixed offset, so it clears that
 * one flag nevents times and leaves every other term still marked. The two
 * loops look identical and are not, which is the AiStepTrack lesson again --
 * diffing them is what shows it. A `butnot` condition therefore only fires
 * once per matched term rather than rearming cleanly.
 *
 * The second argument to EvalCondTests is DEAD: the original pushes `arg`
 * and the callee reads only its first. Passed here as the original passes
 * it.
 */
void __cdecl EvtCondition(int32_t bucket, int32_t a2, void *arg, int32_t a4,
                          int32_t a5, int32_t a6, int32_t a7,
                          AM2_ScriptCond *c)
{
    int32_t hit = -1;
    int32_t i;

    (void)a6;

    /* Which of this condition's terms is the one that just fired. */
    for (i = 0; i < c->nevents; i++)
        if (c->events[i].a == bucket
            && FilterMatches(c->events[i].b, c->events[i].c, a2, a5, a4, a7))
            hit = i;

    if (hit == -1 && c->kind != AM2_IF_TIMEABSOLUTE) {
        orig_log("ERROR: a compound event got called for an event it "
                 "didn't care about\n");
        return;
    }

    /* Arms in the image's layout order. */
    switch (c->kind) {
    case AM2_IF_ALLOF:
        /* The tail is written out on both paths of the first test, as the
         * image has it. allof DOES clear, with a cursor that walks -- the
         * loop arm 7 got wrong. */
#define ALLOF_TAIL()                                                        \
        if (c->fired != c->nevents)                                         \
            return;                                                         \
        if (!EvalCondTests(c, arg))                                         \
            return;                                                         \
        for (i = 0; i < c->nevents; i++)                                    \
            c->events[i].fired = 0;                                         \
        c->fired = 0;                                                       \
        RunCondActions(c, arg);                                             \
        return
        if (!c->events[hit].fired) {
            c->fired++;
            c->events[hit].fired = 1;
            ALLOF_TAIL();
        }
        ALLOF_TAIL();
#undef ALLOF_TAIL

    case AM2_IF_INORDER:
        if (hit != c->fired)
            return;
        if (++c->fired != c->nevents)
            return;
        c->fired = 0;
        RunCondActions(c, arg);
        return;

    case AM2_IF_COUNT:
        if (c->events[hit].fired)
            return;
        c->events[hit].fired = 1;
        if (++c->fired != c->number)
            return;
        RunCondActions(c, arg);
        return;

    case AM2_IF_REPEAT:
        if (++c->fired != c->number)
            return;
        RunCondActions(c, arg);
        return;

    case AM2_IF_BUTNOT_KEYWORD:
        if (hit == c->nevents - 1) {
            /* The `butnot` term itself: remember it and do nothing else. */
            c->events[hit].fired = 1;
            return;
        }
#define BUTNOT_TAIL()                                                       \
        if (c->fired != c->nevents - 1)                                     \
            return;                                                         \
        if (c->events[c->nevents - 1].fired)                                \
            return;              /* the butnot term HAS fired: refuse */    \
        if (!EvalCondTests(c, arg))                                         \
            return;                                                         \
        c->fired = 0;                                                       \
        for (i = 0; i < c->nevents; i++)                                    \
            c->events[hit].fired = 0;                                       \
        RunCondActions(c, arg);                                             \
        return
        /* THE ORIGINAL'S RESET, DEFECT INCLUDED. `ebx` holds the MATCHED
         * term's byte offset and never advances, so the loop in the tail
         * clears one flag nevents times and leaves every other term still
         * marked. Arm 1's otherwise identical loop walks with a cursor
         * stepping 0x10; these two look the same and are not. The original
         * writes the tail twice, once on each of this arm's paths, and so
         * does this. A `butnot` therefore does not rearm cleanly. */
        if (!c->events[hit].fired) {
            c->fired++;
            c->events[hit].fired = 1;
            BUTNOT_TAIL();
        }
        BUTNOT_TAIL();
#undef BUTNOT_TAIL

    case AM2_IF_BUTNOT_STRING:
        /* NOT the same arm as 7, though it reads like it. This one counts
         * nothing: past the butnot check it falls THROUGH into the plain
         * arm's body in the image, so there is no all-but-last requirement
         * and no reset. An arm ending inside another, which the jump table
         * cannot show. */
        if (hit == c->nevents - 1) {
            c->events[hit].fired = 1;
            return;
        }
        if (c->events[c->nevents - 1].fired)
            return;
        /* Into the plain arm, as the image lays it out. */
        /* FALLTHROUGH */
    case AM2_IF_PLAIN:
    case AM2_IF_TIMEABSOLUTE:
        if (EvalCondTests(c, arg))
            RunCondActions(c, arg);
        return;

    case AM2_IF_AFTER:
        /* AN ODD INDEX CANCELS THE PAIR rather than being ignored, which is
         * how `A after B` enforces its ordering: B, the even one, marks the
         * pair and A, the odd one, unmarks it. Reading the odd path as a
         * bare return would have made `after` mean `both`. */
        if (!(hit & 1)) {
            if (!c->events[hit >> 1].fired) {
                c->events[hit >> 1].fired = 1;
                c->fired++;
            }
            if (c->fired != c->nevents >> 1)
                return;
            if (!EvalCondTests(c, arg))
                return;
            /* NO RESET HERE, unlike allof -- the pair flags stay set, so an
             * `after` that has been satisfied once fires again on every later
             * matching event. Different from arm 1 and deliberately not shared
             * with it. */
            RunCondActions(c, arg);
            return;
        }
        if (c->events[hit >> 1].fired == 1) {
            c->events[hit >> 1].fired = 0;
            c->fired--;
        }
        return;

    default:
        return;
    }
}

/* THE FIVE EVENT-TABLE HANDLERS, and the calling convention is worth stating
 * once for all of them because nothing CALLS these -- the dispatcher does, so
 * there is no call site to check the slots against.
 *
 * All eight arguments, as EvtCondition established from the dispatcher's
 * `call dword ptr [esi]` and `add esp, 0x20`. ARGUMENT 3 is the notify's
 * payload and ARGUMENT 8 is whatever EventRegister stored. Which half a
 * handler reads follows from how event.cpp registered it: the win conditions
 * pass their army index as the argument and read 8, while the rule events
 * pass 0 and take their uid from the notify in 3. Two apparent signatures,
 * one convention, established from three functions independently.
 *
 * Their sizes came out of finding the `ret` boundaries rather than out of
 * docs/functions.tsv, which runs the first three together -- a 192-byte span
 * that is really 16, 16 and 156. merges.py splits only where it can find a
 * reference and its own docstring says the result is a lower bound. */

/* 0x00422250 and 0x00422260 -- greenwins/tanwins/... and the team versions.
 *
 * SIXTEEN BYTES EACH AND THEY DIFFER IN ONE SEMANTIC BYTE: the flag that
 * says army or team. Diffed rather than eyeballed -- two bytes differ and the
 * other is the call displacement, which is a relocation, since the two calls
 * sit sixteen bytes apart and target the same address. */
void __cdecl EvtArmyWins(int32_t a1, int32_t a2, int32_t a3, int32_t a4,
                         int32_t a5, int32_t a6, int32_t a7, int32_t army)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    AdvanceMission(army, 0);
}

void __cdecl EvtTeamWins(int32_t a1, int32_t a2, int32_t a3, int32_t a4,
                         int32_t a5, int32_t a6, int32_t a7, int32_t team)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    AdvanceMission(team, 1);
}

/* 0x004223A0, 35 bytes -- the smallest of the three rule events. Deploy an
 * item at the object's own position. */
void __cdecl EvtRuleC(int32_t a1, int32_t a2, uint32_t uid, int32_t a4,
                      int32_t a5, int32_t a6, int32_t a7, int32_t a8)
{
    AM2_Object *obj;

    (void)a1; (void)a2; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;

    obj = LookupByUID(uid);
    if (!obj)
        return;

    DeployItem(obj, obj->pos, 0, 0);
}

/* 0x00422270, 156 bytes -- put a unit back on the map where it fell.
 *
 * THE GUARD IS A FOOTPRINT'S WORTH OF BLOCKAGE, not a passability constant.
 * BlockWeightAt is compared against AM2_CELL_WEIGHT_STEP, "what one footprint
 * point is worth", so the test reads as "something already occupies that
 * cell" -- and reaching for a new name for 15 would have added a fifth to the
 * four this file already distinguishes as four different concepts.
 *
 * The saved byte at +0x528 goes back into +0x40 before the deploy, and the
 * sound plays only for OUR army. */
void __cdecl EvtRuleA(int32_t a1, int32_t a2, uint32_t uid, int32_t a4,
                      int32_t a5, int32_t a6, int32_t a7, int32_t a8)
{
    AM2_Object *obj;

    (void)a1; (void)a2; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;

    obj = LookupByUID(uid);
    if (!obj || !ObjIsTypeIn238(obj))
        return;

    if (ObjIsType2(obj)
        && obj->aiDriven) {
        /* SIGNED: the original is `cmp eax,0xf; jge` at 0x004222C0, not jae.
         * Left unsigned, the int32_t promotes and a negative weight would
         * read as huge and take the opposite branch. */
        /* The reference point is a local copy, the start read in place:
         * one load, its copy pushed beside it, as the image has it. */
        AM2_Point ref = obj->spawnAt;

        if (BlockWeightAt((AM2_Object *)(void *)0,
                          obj->spawnAt, ref)
            >= (int32_t)AM2_CELL_WEIGHT_STEP)
            return;
    }

    obj->facing =
        obj->spawnFacing;
    DeployItem(obj, obj->spawnAt, 1, 0);

    if (obj->owner
        == *(const int32_t *)&g_defaultOwner)
        PlayDynamicSound("mp_pop.wav",
                         0, 0, 0, 0, 3, 2, 0);
}

/* 0x00422310, 141 bytes -- spawn a replacement trooper for a fallen one.
 *
 * SEVEN PUSHES BEFORE A ONE-ARGUMENT CALL. CommArmyOfSlot is thiscall with a
 * single slot argument and 47 callers; the seven values already on the stack
 * are CreateTrooper's, staged early, and the thiscall pops only its own. The
 * single `add esp, 0x28` at the end cleans ten. Counting the pushes in front
 * of the first call gives a seven-argument call to a one-argument function,
 * which is the shape CLAUDE.md says makes these bodies look impossible; what
 * settles it is the callee's arity, not the stack.
 *
 * The name handed to CreateTrooper is ADDR_DIR_SCRATCH, the shared scratch
 * buffer whoever raised the event filled.
 *
 * IT FORCES ATTACK. The new trooper's previous AI mode is saved to
 * OBJ_OFF_AI_MODE_PREV and the mode set to 6 -- the same field
 * PointerActionFollow writes 3 into, established earlier from the other
 * direction entirely. */
void __cdecl EvtRuleB(int32_t a1, int32_t a2, uint32_t uid, int32_t a4,
                      int32_t a5, int32_t a6, int32_t a7, int32_t a8)
{
    AM2_Object *obj;
    AM2_Object *unit;
    int32_t  army;

    (void)a1; (void)a2; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;

    obj = LookupByUID(uid);
    if (!obj || !ObjIsType2(obj))
        return;

    army = obj->owner;

    unit = (AM2_Object *)(uint8_t *)CreateTrooper(
               g_dirScratch,
               obj->pos.x,
               obj->pos.y,
               ((AM2_Comm *)kCommObject)->CommArmyOfSlot(army),
               army, 0, 0, 0, 1, 0);
    if (!unit)
        return;

    Type2ActionA(unit);
    unit->aiDriven   = 1;
    unit->aiModePrev =
        unit->aiMode;
    unit->aiMode    = AM2_AI_MODE_ATTACK;
    ObjConceal(unit, 0);
}

/* --------------------------------------------- immediate trigger ---- */

/* Free one entry's handler chain and then the entry. `owns` decides whether
 * the handler's argument goes with it -- the same flag EventRegister stores
 * and the table teardown reads. */
static inline void FreeEventEntry(AM2_EventEntry *e)
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
    AM2_EventEntry *prev;
    int32_t         sent;

    if (kEventDebug)
        orig_log("EventTriggerImmediate: type %d, num: %d, uid: %x, "
                 "removeevent: %d, remote: %d\n",
                 type, num1, uid1, removeevent, remote);

    sent = 0;
    e = kEventTable[type];
    prev = (AM2_EventEntry *)0;

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

        if (e == kEventTable[type]) {
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

    Type238Action((AM2_Object *)obj, arg);
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
    AM2_Object *obj = LookupByUID(uid);

    if (obj == 0)
        return;

    g_objCtxObjPrev =
        g_objCtxObj;
    g_objCtxValPrev =
        g_objCtxVal;
    g_objCtxObj  = obj;
    g_objCtxVal = (int32_t)obj->uid;
    g_objCtxSetPrev =
        g_objCtxSet;
    g_objCtxSet = 1;
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
    AM2_Object *obj;

    if (name == g_svarId15) {
        g_evtId15Flag = 1;

        obj = LookupByUID(
                  *(const uint32_t *)&g_evtId15Uid);

        if (obj != 0)
            obj->flags |= OBJ_FLAG8_BIT40;

        return;
    }

    obj = LookupByUID(ResolveUid(name, me));

    if (obj != 0)
        obj->flags &= ~(uint32_t)OBJ_FLAG8_BIT40;
}

void __cdecl EvtFlag40Set(int32_t name, uint32_t me)
{
    AM2_Object *obj;

    if (name == g_svarId15) {
        g_evtId15Flag = 0;
        return;
    }

    obj = LookupByUID(ResolveUid(name, me));

    if (obj != 0)
        obj->flags |= OBJ_FLAG8_BIT40;
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
    AM2_PtrList **lists = g_armyObjLists;
    int32_t   slot  = AM2_THE_COMM->CommSlotForArmy(
                                      army);
    int32_t   i     = 0;

    /* The list pointer is re-read from the table at every use. */
    while (i < lists[slot]->count) {
        AM2_Object *obj = LookupByUID(
            lists[slot]->uids[i]);

        if (obj == 0) {
            ((AM2_Rec *)lists[slot])->ListRemoveAt(i);
            continue;
        }

        if (obj->flagsLo & OBJ_FLAG8_BLOCKED) {
            i++;
            continue;
        }

        if (!ObjIsTypeIn238(obj)) {
            i++;
            continue;
        }

        if (filter != -1 && filter != (int32_t)ObjFieldA(obj)) {
            i++;
            continue;
        }

        obj->aiModePrev =
            obj->aiMode;
        obj->aiMode = value;

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
    AM2_Object *a;
    AM2_Object *b;

    if (uidA < AM2_UID_COUNTER_MIN || uidB < AM2_UID_COUNTER_MIN)
        return;

    a = LookupByUID(uidA);
    if (a == 0)
        return;

    b = LookupByUID(uidB);
    if (b == 0)
        return;

    if (ObjIsType2(a))
        a->field540 = 0;

    ObjAttachTo(a, b);
}

/* --------------------------------------------------- bitmaps ---- */

typedef void (__cdecl *AM2_FreeBitmapFn)(void **slot);
/* Forward-declared rather than reached through win32/sprite.h, the same way
   commmsg.cpp declares HudMessage: this module is flat and that header names
   Win32 types. The return type is spelled as the definition's, AM2_Sprite *,
   through an incomplete struct: MSVC mangles the return type into the name,
   so a `void *` here links under GCC and is unresolved under VC6's LINK. */
struct AM2_Sprite;
struct AM2_Sprite *__cdecl LoadBitmap(const char *name, int32_t flags);

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
    SetGameDir("bitmaps");

    g_subState = AM2_SUBSTATE_BITMAP;
    g_overlayDirty      = 1;

    SendGamePause(1, AM2_EVENT_FLAG_8);

    FreeBitmap(&g_currentBitmap);
    g_currentBitmap = LoadBitmap(name, 0);
}

void __cdecl EvtShowBitmapNoPause(const char *name)
{
    SetGameDir("bitmaps");

    FreeBitmap(&g_currentBitmap);
    g_currentBitmap = LoadBitmap(name, 0);
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
    AM2_Object *obj = LookupByUID(uid);

    if (obj == 0)
        return;

#define REC (g_objTableRecords \
             + (uint32_t)index * AM2_OBJ_TABLE_REC_SIZE)
    switch ((int32_t)obj->type) {
    default:
        orig_log("Warning: check if this script command works with this "
                 "object type!\n");
        return;
    case 3:
        obj->tableRec3 = REC;
        SetFieldInAll(&obj->rowSet, REC);
        return;
    case 2:
        obj->tableRec2 = REC;
        SetFieldInAll(&obj->rowSet, REC);
        return;

    }
#undef REC
}

/* ------------------------------------------------ cond tests ---- */

typedef int32_t (__cdecl *AM2_EvalOperandFn)(int32_t a, int32_t b, int32_t c);
typedef void (__cdecl *AM2_MissionNetFn)(int32_t a, int32_t b);
typedef int32_t (__cdecl *AM2_FindFirstFn)(const char *spec, void *data);
typedef int32_t (__cdecl *AM2_FindCloseFn)(int32_t handle);
#define orig_findfirst (*(AM2_FindFirstFn)((void *)(uintptr_t)(ADDR_CRT_FINDFIRST)))
#define orig_findclose (*(AM2_FindCloseFn)((void *)(uintptr_t)(ADDR_CRT_FINDCLOSE)))
/* MissionEnded is reconstructed below and called by name; its typedef went
 * with the seam. */
#define orig_mission_local  MissionEnded

/* MissionEnded -- original 0x00421890, one caller: AdvanceMission, which is
 * the single-player arm of the mission-end router.
 *
 * IT WAS ADDR_SCRIPT_FIND_FILE, and that name came from the one thing anybody
 * had looked at: the "%s%d.txt" it builds. Probing for the next sub-mission is
 * its FIRST test and not its job. Past that it advances the campaign, saves
 * the platoon to save\default.cof, unlocks a movie in Options.cfg, picks the
 * film and asks for the state that plays it. Sixth or seventh instance of a
 * name taken from one observation rather than the body.
 *
 * ITS ARGUMENT IS "LOST" and the paths are what say so: zero with a level in
 * hand takes the advance; anything else takes the arm that chooses one of
 * three loss movies and leaves AM2_MENU_REQUEST_LOST behind.
 *
 * THREE ENDINGS, AND THE FIRST IGNORES THE ARGUMENT ENTIRELY. With
 * ADDR_WIN_ENABLED set it probes for "<tileset><n+1>.txt" and, if that exists,
 * simply queues it and bumps the index -- won or lost. That asymmetry is the
 * original's and is not obviously deliberate; the flag is written by the win
 * condition, so reaching this arm with `lost` set may be unreachable rather
 * than wrong.
 *
 * THE SAME PROBE APPEARS TWICE, once in that arm and once in the advance, and
 * the two are not quite the same: the first falls through to "the campaign is
 * over" when the file is missing, the second falls through to the next LEVEL.
 * Written out twice, as the original has it, rather than factored -- the two
 * tails differ and a shared helper would have to carry a flag to say which.
 *
 * IT PICKS A LOSS MOVIE BY THE CLOCK AND RETRIES UNTIL IT GETS ONE.
 * ADDR_GAME_CLOCK_MS modulo three chooses among the record's three names and
 * the loop repeats while the chosen slot is empty -- so it spins on a ticking
 * global rather than scanning the three. It terminates because the arm is only
 * entered when the first of the three is non-empty, and the clock reaches 0
 * eventually; but it is a busy-wait on a clock and worth saying so. An empty
 * result after all that falls back to "grave".
 *
 * THE MOVIE UNLOCK IS A MAXIMUM AND IT IS WRITTEN THROUGH. Whenever the
 * record's index exceeds ADDR_MOVIE_COUNT the global rises and Options.cfg is
 * rewritten immediately -- so the viewer's list grows at the moment a mission
 * ends rather than at a save point.
 */
void __cdecl MissionEnded(int32_t lost)
{
    char     path[AM2_LEVEL_PROBE_BUF];
    char     found[AM2_FIND_DATA_BYTES];
    int32_t  h;
    uint8_t *rec;
    uint8_t *next;
    int32_t  movie;

    g_inputSuppress = 0;

    if (g_winEnabled) {
        SetGameDir(g_tilesetPath);
        am2_sprintf(path, "%s%d.txt",
                    &g_mapName_,
                    g_levelIndex + 1);

        h = orig_findfirst(path, found);
        if (h == -1) {
            movie = 0;

            rec = (uint8_t *)FindLevelRecord(
                      g_levelId);
            if (!rec)
                goto no_next;

            if (*(const char *)(rec + LEVEL_OFF_WIN_MOVIE)) {
                strcpy(&g_movieToPlay,
                       (const char *)(rec + LEVEL_OFF_WIN_MOVIE));
                movie = 1;
            }
            if (*(const int32_t *)(rec + LEVEL_OFF_MOVIE_INDEX)
                > g_movieCount) {
                g_movieCount =
                    *(const int32_t *)(rec + LEVEL_OFF_MOVIE_INDEX);
                SaveOptions();
            }
            if (!movie)
                goto no_next;

            RequestState(3);
            g_stateArg = 1;
            return;
        }

        orig_findclose(h);
        g_scriptReload = 1;
        g_attemptCount = 0;
        strcpy(g_scriptReloadPath, path);
        g_levelIndex += 1;
        return;
    }

    if (!lost && g_levelId > 0) {
        SetGameDir(g_tilesetPath);
        am2_sprintf(path, "%s%d.txt",
                    &g_mapName_,
                    g_levelIndex + 1);

        h = orig_findfirst(path, found);
        if (h == -1) {
            movie = 0;

            rec = (uint8_t *)FindLevelRecord(
                      g_levelId);
            if (rec) {
                if (*(const char *)(rec + LEVEL_OFF_WIN_MOVIE)) {
                    strcpy(&g_movieToPlay,
                           (const char *)(rec + LEVEL_OFF_WIN_MOVIE));
                    movie = 1;
                }
                if (*(const int32_t *)(rec + LEVEL_OFF_MOVIE_INDEX)
                    > g_movieCount) {
                    g_movieCount =
                        *(const int32_t *)(rec + LEVEL_OFF_MOVIE_INDEX);
                    SaveOptions();
                }
            }

            next = (uint8_t *)FindLevelRecord(
                       g_levelId + 1);
            SaveDefaultCof();

            g_levelIndex   = 1;
            g_levelId     += 1;
            g_attemptCount = 0;
            SelectLevel(next);

            if (movie) {
                RequestState(3);
                g_stateArg = (next != 0) + 1;
                return;
            }
            if (next) {
                RequestState(2);
                return;
            }
no_next:
            RequestState(1);
            return;
        }

        orig_findclose(h);
        strcpy(g_scriptReloadPath, path);
        g_scriptReload = 1;
        g_levelIndex += 1;
        return;
    }

    /* ---- lost, or no level in hand ---- */
    rec = (uint8_t *)FindLevelRecord(
              g_levelId);
    g_movieToPlay = '\0';

    if (rec && strlen((const char *)(rec + LEVEL_OFF_LOSE_MOVIES))) {
        do {
            /* The clock is read again on every pass -- the image does not
             * hoist it, so an empty pick waits for the clock to move on. */
            uint32_t pick =
                *(volatile const uint32_t *)((uintptr_t)&g_gameClockMs)
                % AM2_LOSE_MOVIE_COUNT;

            strcpy(&g_movieToPlay,
                   (const char *)(rec + LEVEL_OFF_LOSE_MOVIES
                                  + pick * AM2_LEVEL_STR_BYTES));
        } while (g_movieToPlay == '\0');
    }

    if (!strlen(&g_movieToPlay))
        strcpy(&g_movieToPlay,
               "grave");

    RequestState(3);
    g_stateArg = 1;
    g_menuRequest = AM2_MENU_REQUEST_LOST;
}

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
int32_t __cdecl EvalCondTests(const AM2_ScriptCond *c, void *unused)
{
    (void)unused;
    /* The test is re-indexed after each call: the tests pointer is read
     * again every time. */
    for (int32_t i = 0; i < c->ntests; i++) {
        int32_t left  = EvalOperand(c->tests[i].left[0], c->tests[i].left[1],
                                    c->tests[i].left[2]);
        int32_t right = EvalOperand(c->tests[i].right[0], c->tests[i].right[1],
                                    c->tests[i].right[2]);

        switch (c->tests[i].op) {
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
    if (g_mpSession == 0) {
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

/* AM2_FireSpot and FireWeapon moved to item.h when region.cpp's
 * TrooperFire needed them too -- one definition, for the reason
 * tools/merges.py imports coverage.REGISTERED rather than copying it. */

/* 0x004200F0. A named weapon, fired by a named unit, at a point. */
void __cdecl FireWeaponAtPoint(uint32_t weaponUid, uint32_t unitUid,
                               int32_t heading, AM2_Point at)
{
    AM2_Object     *weapon = WeaponByUid((int32_t)weaponUid);
    AM2_Object     *unit;
    AM2_FireSpot spot;
    int8_t       lent;

    if (!weapon)
        return;
    unit = (AM2_Object *)(uint8_t *)UnitByUid(unitUid);
    if (!unit)
        return;

    /* `at` is a point passed by value, read a word at a time. */
    spot.at.x   = at.x;
    spot.at.y   = at.y;
    spot.ground = (int16_t)TileAttrAt((uint32_t)TileOfPoint(at));

    lent = weapon->owner;
    weapon->owner = unit->owner;

    if (heading < 0)
        heading = AngleBetween(&unit->pos, &at);

    FireWeapon(weapon, unit, ObjFieldB(unit), heading, spot, (AM2_Object *)(void *)0);

    weapon->owner = lent;
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
    AM2_Object     *weapon = WeaponByUid((int32_t)weaponUid);
    AM2_Object     *unit;
    AM2_Object     *target;
    AM2_FireSpot spot;
    int32_t      lent;   /* movsx into ebx: an int, restored as a byte */

    if (!weapon)
        return;
    unit = (AM2_Object *)(uint8_t *)UnitByUid(unitUid);
    if (!unit)
        return;
    target = LookupByUID(targetUid);
    if (!target)
        return;

    spot.at.x   = (&target->pos)->x;
    spot.at.y   = (&target->pos)->y;
    spot.ground = (int16_t)target->heightSet;

    if (heading < 0)
        heading = AngleBetween(&unit->pos,
                               &target->pos);

    lent = weapon->owner;
    weapon->owner = unit->owner;

    FireWeapon(weapon, unit, ObjHeight(unit), heading, spot, target);

    weapon->owner = (int8_t)lent;
}

/* 0x00420260. The unit's own held weapon, at a point. */
void __cdecl UnitFireAtPoint(uint32_t unitUid, int32_t heading, AM2_Point at)
{
    AM2_Object     *unit = (AM2_Object *)UnitByUid(unitUid);
    void        *weapon;
    AM2_FireSpot spot;

    if (!unit)
        return;
    weapon = HeldWeaponObj(unit);
    if (!weapon)
        return;

    /* `at` is a point passed by value, read a word at a time. */
    spot.at.x   = at.x;
    spot.at.y   = at.y;
    spot.ground = (int16_t)TileAttrAt((uint32_t)TileOfPoint(at));

    if (heading < 0)
        heading = AngleBetween(&unit->pos, &at);

    FireWeapon((AM2_Object *)weapon, unit, ObjFieldB(unit), heading, spot, (AM2_Object *)(void *)0);
}

/* 0x00420300. The unit's own held weapon, at another object.
 *
 * The only one that passes an all-zero spot: both halves are left for
 * 0x0045F460 to fill from the target, which is what makes the two defaults
 * documented on ADDR_FIRE_WEAPON load-bearing rather than decorative. */
void __cdecl UnitFireAtObject(uint32_t unitUid, int32_t heading,
                              uint32_t targetUid)
{
    AM2_Object     *unit = (AM2_Object *)UnitByUid(unitUid);
    void        *weapon;
    AM2_Object     *target;
    AM2_FireSpot spot;

    if (!unit)
        return;
    weapon = HeldWeaponObj(unit);
    if (!weapon)
        return;
    target = LookupByUID(targetUid);
    if (!target)
        return;

    spot.at.x   = 0;
    spot.at.y   = 0;
    spot.ground = 0;

    if (heading < 0)
        heading = AngleBetween(&unit->pos,
                               &target->pos);

    FireWeapon((AM2_Object *)weapon, unit, ObjFieldB(unit), heading, spot, target);
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
AM2_Point __cdecl ActionPoint(const AM2_ScriptAction *act, uint32_t me)
{
    AM2_Object *obj;

    if (act->xvar != 0) {
        /* One value read twice into, not zeroed first: GetVarValue leaves
         * `v` alone for a name that is not a variable, so the image reads
         * whatever its stack slot held (the dead `act` argument). The port
         * starts it at 0 instead, as this reconstruction always has; VC6 is
         * given the original's uninitialised read. The point is built a half
         * at a time. */
#if defined(_MSC_VER)
        int32_t   v;
#else
        int32_t   v = 0;
#endif
        AM2_Point p;

        GetVarValue(act->xvar, &v);
        p.x = (int16_t)v;
        GetVarValue(act->yvar, &v);
        p.y = (int16_t)v;
        return p;
    }

    if (act->u.pos.x == 0) {
        obj = LookupByUID(ResolveUid(act->target, me));
        if (obj)
            return obj->pos;
    }
    return act->u.pos;
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
    const uint8_t *comm = g_commObject;

    if (g_mpSession != 0
        && *(const int32_t *)(comm + COMM_OFF_IS_HOST) == 0)
        return;

    if (g_subState
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


/* 0x0041F3E0, three registration sites and all three ours: the timer that
 * EventTriggerDelayed starts, and the savegame save and restore below.
 *
 * The third of the family whose other two are pad.cpp's EvtPadOn/EvtPadOff.
 * When a delayed event's timer fires, this is what raises it: unpack the
 * 16-byte record EventTriggerDelayed malloc'd and hand it straight to the
 * immediate trigger. The record is (type, num, uid, removeevent), which
 * orig.h had already recorded from the builder's own log line, and the nine
 * pushes here line up with EventTriggerImmediate's nine parameters one for
 * one -- the four masks and the second num/uid pair are all literal zero,
 * which is the delayed path dropping them, and `remote` is 0 because a timer
 * firing locally is not a peer's traffic.
 *
 * Argument 8 is the record EventRegister stored, the same slot the pad pair
 * takes its pad from. `owns` was 1 at registration, so the teardown frees the
 * record; nothing here does. */
void __cdecl EvtRecordHandler(int32_t a1, int32_t a2, int32_t a3, int32_t a4,
                              int32_t a5, int32_t a6, int32_t a7,
                              const int32_t *rec)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    EventTriggerImmediate(rec[0], rec[1], (uint32_t)rec[2],
                          0, 0, 0, 0, rec[3], 0);
}

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

    EventRegister(1, id, 0, (const void *)EvtRecordHandler,
                  rec, 1);
}

/* The mission clock and the timer table, all in the image. */

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
        start += skip * period;
        count -= (int32_t)skip;
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

    *(int32_t *)&g_eventBlock = 0;
    *(int32_t *)&g_timerBlock = 0;
    /* From the first record's id field to one past the last, sixteen bytes at
     * a time -- the records themselves are left alone. */
    /* Signed address compare -- the original walks the table with a signed
     * int32 cursor (`jl`, not `jb`); both ends are below 0x80000000 so the
     * result is identical, but the signedness is what MSVC 6 compiled. */
    for (id = (uint32_t *)(uintptr_t)(((uintptr_t)g_timerTable) + 0xC);
         (int32_t)(uintptr_t)id < (int32_t)((uintptr_t)g_timerTableIdEnd);
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
void __cdecl EvtDropItem(uint32_t uid, uint32_t weaponUid, AM2_Point at)
{
    AM2_Object *obj;
    int32_t  slot;

    if (uid < AM2_UID_COUNTER_START || weaponUid < AM2_UID_COUNTER_START)
        return;

    obj = LookupByUID(uid);
    if (!ObjIsType2(obj))
        return;

    if (*(void *const *)&g_mpSession
        && !AM2_THE_COMM->CommMustBroadcast(
                              (int16_t)obj->owner))
        return;

    if (!obj->sarge)
        return;

    if (!at.x)
        at = (obj)->pos;

    for (slot = 1; slot < AM2_INVENTORY_SLOTS; slot++)
        if (obj->inventory[slot] == weaponUid) {
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
    int32_t  i;

    if (uid < AM2_UID_COUNTER_START)
        return;

    target = LookupByUID(uid);
    if (!target)
        return;

    slot = AM2_THE_COMM->CommSlotForArmy(army);

    /* The army's list is re-read from the table at every use. */
#define LIST (g_armyObjLists[slot])
    i = 0;
    while (i < LIST->count) {
        AM2_Object *obj = LookupByUID(
            LIST->uids[i]);

        if (!obj) {
            ((AM2_Rec *)LIST)->ListRemoveAt(i);
            continue;           /* no step: the shifted-down entry is next */
        }

        if (!(obj->flagsLo & OBJ_FLAG_DESTROYED)
            && ObjIsTypeIn238(obj)
            && (filter == AM2_ATTACH_ANY || (uint32_t)filter == ObjFieldA(obj)))
            ObjAttachTo(obj, (AM2_Object *)target);
        i++;
    }
#undef LIST
}

/* The comm object's team score, still original. */
typedef int32_t (__attribute__((thiscall)) *AM2_TeamScoreFn)(void *comm,
                                                             int32_t slot);

/* EvalOperand -- original 0x00421590, two callers. Evaluate one side of a
 * testvar comparison: eight kinds through a jump table, and anything past them
 * answers the operand unchanged, which is how a LITERAL is expressed.
 *
 * THREE OF THE EIGHT ARE NAMED BY THE GAME'S OWN VOCABULARY. Kind 5 walks the
 * six UNIT_OFF_INVENTORY slots for a weapon uid, which is `hasitem`; kind 7 is
 * AllyFlag, which is `isally`; kind 8 reads a comm slot's team, which is
 * `teamscore`. All three are keywords docs/scripttokens.md already lists, and
 * a testvar operand evaluator is exactly where they would live -- so the
 * mapping is the program's own rather than mine.
 *
 * FOUR KINDS SHARE ONE PREAMBLE and it is written out four times in the
 * original: resolve the name to a uid, look the object up, and give up if it
 * is gone. Kinds 2 and 3 then want an ITEM, kinds 4 and 5 a TROOPER, and each
 * has its own default for the failure -- 0 for three of them and 5 for kind 4.
 * That last is the one worth noticing: a missing trooper and a trooper in
 * state 5 answer the same thing.
 *
 * KIND 6 ANSWERS 1 OR 2, NOT 1 OR 0. An empty slot leaves early with 0, and a
 * taken one answers 1 when CommSlotHasPlayer agrees and 2 when it does not --
 * so the caller has three outcomes to compare against, and the original spells
 * the last two as `neg; sbb; add 2` rather than as a branch.
 */
int32_t __cdecl EvalOperand(int32_t kind, int32_t a, int32_t b)
{
    AM2_Object *o;

    switch (kind) {
    case AM2_OPERAND_VARIABLE:
        return ((const int32_t *)((const uint8_t *)
                   g_scriptNames
                   + (uint32_t)a * AM2_NAME_TABLE_STRIDE))[2];

    case AM2_OPERAND_ITEM_FRAME:
        o = LookupByUID(ResolveUid(a, 0));
        if (!o)
            return 0;
        if (!ObjIsItem(o))
            return 0;
        return o->repairFrame;

    case AM2_OPERAND_HEALTH:
        o = LookupByUID(ResolveUid(a, 0));
        if (!o)
            return 0;
        return o->health;

    case AM2_OPERAND_TROOP_STATE:
        o = LookupByUID(ResolveUid(a, 0));
        if (!o)
            return 5;
        if (!ObjIsType2(o))
            return 5;
        return o->field530;

    case AM2_OPERAND_HASITEM: {
        uint32_t want;
        int32_t  i;

        o = LookupByUID(ResolveUid(a, 0));
        if (!o)
            return 0;
        if (!ObjIsType2(o))
            return 0;
        if (!o->sarge)
            return 0;

        want = ResolveUid(b, 0);
        if (!want)
            return 0;

        for (i = 0; i < AM2_INVENTORY_SLOTS; i++)
            if ((&o->inventory[0])[i] == want)
                return 1;
        return 0;
    }

    case AM2_OPERAND_SLOT_TAKEN: {
        const uint8_t *comm = g_commObject;

        if (*(const int32_t *)(comm + COMM_OFF_PLAYERS
                               + (uint32_t)a * COMM_PLAYER_STRIDE
                               + COMM_SLOT_OFF_TAKEN))
            return ((AM2_Comm *)(void *)comm)->CommSlotHasPlayer(a) ? 1 : 2;
        return 0;

    }

    case AM2_OPERAND_ISALLY:
        /* The original loads the comm object into ecx before this call and
         * AllyFlag is stdcall, so that load is dead -- it is the shape a
         * thiscall leaves behind, and the callee never reads it. Not
         * reproduced: there is no register to write in C, and nothing
         * observes it. */
        return AllyFlag(a, b) ? 1 : 0;

    case AM2_OPERAND_TEAMSCORE:
        return AM2_THE_COMM->CommTeamScore(a);

    default:
        return a;
    }
}


int event_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_RUN_SCRIPT_ACTION, (const void *)RunScriptAction,
                        "RunScriptAction", 3);
    rc |= patch_replace(ADDR_RESET_TIMERS, (const void *)ResetTimers,
                        "ResetTimers", 0);
    patch_replace(ADDR_EVAL_OPERAND, (const void *)EvalOperand,
                  "EvalOperand", 2);
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
    rc |= patch_replace(ADDR_EVT_ARMY_WINS, (const void *)EvtArmyWins, "EvtArmyWins", 8);
    rc |= patch_replace(ADDR_EVT_TEAM_WINS, (const void *)EvtTeamWins, "EvtTeamWins", 8);
    rc |= patch_replace(ADDR_EVT_RULE_A, (const void *)EvtRuleA, "EvtRuleA", 8);
    rc |= patch_replace(ADDR_EVT_RULE_B, (const void *)EvtRuleB, "EvtRuleB", 8);
    rc |= patch_replace(ADDR_EVT_RULE_C, (const void *)EvtRuleC, "EvtRuleC", 8);
    rc |= patch_replace(ADDR_EVT_CONDITION, (const void *)EvtCondition,
                        "EvtCondition", 8);
    rc |= patch_replace(ADDR_EVAL_COND_TESTS, (const void *)EvalCondTests,
                        "EvalCondTests", 6);
    rc |= patch_replace(ADDR_MISSION_ENDED, (const void *)MissionEnded,
                        "MissionEnded", 1);
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
    rc |= patch_replace(ADDR_EVT_RECORD_HANDLER,
                        (const void *)EvtRecordHandler,
                        "EvtRecordHandler", 1);
    return rc;
}

/* ---- RunScriptAction --------------------------------------------------- */
/* HudMessage lives behind win32/widget.h, which reaches windows.h; event.cpp
 * is flat, so it is declared rather than included. */
extern "C" void __cdecl HudMessage(const char *text, uint8_t colour);
extern "C" void __cdecl StartAudioStream(const char *name, int32_t loop);
extern "C" void __cdecl StopAudioStream(void);

#define AM2_CHEAT_ITEM_GROUP  0x2D


/* 0x00420410. Run one parsed action against an owner.
 *
 * IT MOSTLY DOES NOT RUN THE ACTION. An action whose uid is not -2 is handed
 * to EventNotify and executed later; only an action with no subject, or code
 * 0x1A, reaches the switch. Both hand-off branches are the same call and
 * differ only in where the delay comes from.
 *
 * The switch is a DIRECT table of 58 entries with 58 distinct arms -- no
 * shared arms, no byte index, no arm ending inside another. Every code is
 * one action keyword, and the callee names identify them: code 1 is
 * showmessage, 2 showbitmap, 3 showbitmapnopause, and so on.
 *
 * Code 0x1A is EMPTY, which is why the prologue exempts it from the
 * deferral: it is the one action that means "do nothing here". */
void __cdecl RunScriptAction(AM2_ScriptAction *act, void *owner)
{
    /* The variable reads each have their own frame slot in the image (a
     * 0x60 frame), not one shared block-local packed into a dead argument
     * slot: function-scope locals, one per use, the coordinate arms sharing
     * a point. */
    int32_t v1;
    int32_t v2;
    int32_t v3;
    AM2_Point p1;
    int32_t v4;
    AM2_Point p3;
    int32_t v6;
    int32_t v7;
    int32_t v8;
    int32_t v9;
    AM2_Point at;

    if (act->uid != -2 && act->code != 0x1A) {
        if (act->xvar > 0) {

            GetVarValue(act->xvar, &v1);
            EventNotify(act->uid2, act->uid, (uint32_t)(uintptr_t)owner,
                        0, 0, 0, 0, v1, 0, 0);
            return;
        }
        EventNotify(act->uid2, act->uid, (uint32_t)(uintptr_t)owner,
                    0, 0, 0, 0, act->delay, 0, 0);
        return;
    }

    switch (act->code) {
    case 0x01:                                  /* showmessage */
        if (act->text != 0)
            HudMessage(act->text, 0);
        return;

    case 0x02:                                  /* showbitmap */
        if (act->text != 0)
            EvtShowBitmap(act->text);
        return;

    case 0x03:                                  /* showbitmapnopause */
        if (act->text != 0)
            EvtShowBitmapNoPause(act->text);
        return;

    case 0x04:                                  /* showfailure */
        if (act->text != 0) {
            strcpy(g_messageText, act->text);
            strcpy(g_messageBmpName,
                   "02_016_00_failure.bmp");
            g_subState = 0x18;
            g_overlayDirty = 1;
        }
        return;

    case 0x05:                                  /* showpda */
        if (act->text != 0) {
            strcpy(g_messageText, act->text);
            strcpy(g_messageBmpName,
                   "02_017_00_hq.bmp");
            g_subState = 0x18;
            g_overlayDirty = 1;
        }
        return;

    case 0x06:                                  /* playsound */
        if (act->text != 0) {
            at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
            EvtPlaySoundAt(act->text, at,
                           act->n0, act->n1, act->extra);
        }
        return;

    case 0x07:                                  /* playsoundon */
        if (act->text != 0) {
            /* ActionPoint's answer is DISCARDED -- eax is overwritten by n0
             * before anything uses it.  Reproduced because the call may not
             * be pure, and because reading it as an argument to ResolveUid
             * is what the push order invites. */
            ActionPoint(act, (uint32_t)(uintptr_t)owner);
            EvtPlaySoundOn(act->text,
                           ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                           act->n0, act->n1, act->extra);
        }
        return;

    case 0x08:                                  /* playmusic */
        /* No file name STOPS the music: the image's else arm calls
         * StopAudioStream, which this case did not. */
        if (act->text != 0)
            StartAudioStream(act->text, act->n0);
        else
            StopAudioStream();
        return;

    case 0x09: {                                /* showvar */
        char    text[0x40];

        if (GetVarValue(act->xvar, &v2) == 0)
            return;
        am2_sprintf(text, "%d", v2);
        HudMessage(text, 0);
        return;
    }

    case 0x1F: {                                /* moveto */
        /* THREE PATHS, not a point-or-fallback pair.  A named variable pair
         * gives the point; failing that, an action carrying NO point at all
         * moves one object to another; failing that, the literal point the
         * script wrote is used.  The middle test is fireweapon's guard used
         * to SELECT rather than to refuse. */
        /* The subject is resolved afresh at each call, as the image does. */
#define UID ResolveUid(act->subject, (uint32_t)(uintptr_t)owner)
        if (act->xvar > 0) {

            GetVarValue(act->xvar, &v3);
            p1.x = (int16_t)v3;
            GetVarValue(act->yvar, &v3);
            p1.y = (int16_t)v3;
            EvtAtPointC(UID, p1, act->relative);
            return;
        }
        if (act->relative == 0 && act->u.pos.x == 0) {
            EvtAtObjPosC(UID,
                         ResolveUid(act->target, (uint32_t)(uintptr_t)owner),
                         0);
            return;
        }
        EvtAtPointC(UID, act->u.pos, act->relative);
        return;
#undef UID
    }

    case 0x1D:                                  /* setcamerafocus */
        EvtPushObjCtx(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner));
        return;

    case 0x1E:                                  /* restorecamerafocus */
        (*(int32_t *)&g_objCtxObj) = (*(const int32_t *)&g_objCtxObjPrev);
        g_objCtxVal = g_objCtxValPrev;
        g_objCtxSetPrev = g_objCtxSet;
        return;

    case 0x1B:                                  /* suspendai */
        EvtFlag40Clear(act->subject, (uint32_t)(uintptr_t)owner);
        return;

    case 0x1C:                                  /* reviveai */
        EvtFlag40Set(act->subject, (uint32_t)(uintptr_t)owner);
        return;

    case 0x0C:
        EvtGuardedAction(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                         act->n0, act->extra);
        return;

    case 0x0A:                                  /* setobjstate-ish */
        EvtObjSet(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner), act->n0);
        return;

    case 0x0B:
        EvtSetWord60(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                     act->n0);
        return;

    case 0x12:                                  /* dropitem */
        EvtDeployItem(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                      ActionPoint(act, (uint32_t)(uintptr_t)owner));
        return;

    case 0x13:
        EvtObjAction(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner));
        return;

    case 0x14:
        ScriptResurrectItem(ResolveUid(act->subject,
                                       (uint32_t)(uintptr_t)owner),
                            ActionPoint(act, (uint32_t)(uintptr_t)owner));
        return;

    case 0x15:                                  /* ally */
        EvtSetAllied(act->subject, act->target);
        return;

    case 0x16:                                  /* unally */
        EvtClearAllied(act->subject, act->target);
        return;

    case 0x17:
        ScriptSetObjTable(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                          act->target);
        return;

    case 0x0D: {                                /* createtrooper */
        AM2_Object *unit, *weapon;

        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x == 0)
            return;

        unit = (AM2_Object *)(uint8_t *)CreateTrooper(act->text, at.x,
                                        at.y,
                                        ((AM2_Comm *)kCommObject)->CommArmyOfSlot(
                                                       act->army),
                                        act->army, 0, 0, 0, 1, 0);
        if (unit == 0)
            return;

        /* Then the same weapon-attach the cheat console does, which is
         * worth saying because it is four calls in a fixed order and
         * getting one wrong leaves a trooper holding nothing. */
        weapon = (AM2_Object *)(uint8_t *)CreateWeapon(
                     g_dirScratch, act->army,
                     KeyLookupTriple(AM2_CHEAT_ITEM_GROUP, act->item, 0),
                     g_zeroXY,
                     4, -1, 0, 0);
        if (weapon == 0)
            return;

        weapon->owner = (uint8_t)act->army;
        unit->field54C =
            (int32_t)weapon->uid;
        SoldierKindForWeapon(unit,
                             *weapon->typeRec);
        SendTrooperSetWeapon(unit,
                             (int32_t)weapon->uid, 0);
        return;
    }

    case 0x0F: {                                /* createvehicle */
        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x == 0)
            return;

        /* TWO DIFFERENT MAPPINGS OF act->army IN ONE ARM. The sixth argument
         * is the army itself in single-player and its comm SLOT in
         * multiplayer; the fifth is CommArmyOfSlot of the same field, always.
         * They are not the same call and not the same direction. */
        CreateVehicle(act->n0, act->text, at.x, at.y,
                      ((AM2_Comm *)kCommObject)->CommArmyOfSlot(act->army),
                      ((g_mpSession) != 0)
                          ? ((AM2_Comm *)kCommObject)->CommSlotForArmy(act->army)
                          : act->army,
                      0, 0, 0, 0);
        return;
    }

    case 0x10:                                  /* createweapon at a point */
        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x == 0)
            return;
        CreateWeapon(act->text, 4,
                              KeyLookupTriple(AM2_CHEAT_ITEM_GROUP, act->item, 0),
                              at, 0, act->n0, 0, 0);
        return;

    case 0x0E:                                  /* createexplosion */
        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x == 0)
            return;
        CreateExplosion(at.x, at.y, act->n0,
                        act->army, 0, act->n1, 0, 0, 0, 0);
        return;

    case 0x11:                                  /* createroach */
        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x == 0)
            return;
        CreateRoach(0, act->text, at.x, at.y,
                    act->army, 0, 0, 0);
        return;

    case 0x18:                                  /* setaimode + move */
        /* Two complete arms, not one with a branch at the top: with `extra`
         * clear it is one unit, resolved afresh at every call; with it set
         * it is a whole ARMY, and the move goes through the army variants
         * too -- EvtArmyAtPoint and EvtAtObjPosB -- not only the mode. The
         * army arm used to move the single resolved unit. */
        if (act->extra == 0) {
            if (act->n0 >= 0)
                EvtSetAiMode(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                             act->n0);
            /* The same three paths as `moveto`, in the A variants. */
            if (act->xvar > 0) {

                GetVarValue(act->xvar, &v4);
                p1.x = (int16_t)v4;
                GetVarValue(act->yvar, &v4);
                p1.y = (int16_t)v4;
                EvtAtPointA(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                            p1, act->relative);
                return;
            }
            if (act->relative == 0 && act->u.pos.x == 0) {
                EvtAtObjPosA(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                             ResolveUid(act->target, (uint32_t)(uintptr_t)owner),
                             0);
                return;
            }
            EvtAtPointA(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                        act->u.pos, act->relative);
            return;
        }

        if (act->n0 >= 0)
            EvtArmySetField(act->army, act->subject, act->n0);
        if (act->xvar != 0) {

            GetVarValue(act->xvar, &v4);
            p3.x = (int16_t)v4;
            GetVarValue(act->yvar, &v4);
            p3.y = (int16_t)v4;
            EvtArmyAtPoint(act->army, act->subject, p3,
                           act->relative);
            return;
        }
        if (act->relative == 0 && act->u.pos.x == 0) {
            EvtAtObjPosB(act->army, act->subject,
                         ResolveUid(act->target, (uint32_t)(uintptr_t)owner), 0);
            return;
        }
        EvtArmyAtPoint(act->army, act->subject, act->u.pos, act->relative);
        return;

    /* Two arms of one shape: a value that comes from a VARIABLE when one is
     * named and from act->n0 otherwise.  The callees take an int8_t, and the
     * original's fallback loads only AL -- so the upper bits it pushes are
     * whatever was in eax.  Writing the parameter as a byte is what makes
     * that harmless, and is why the header's int8_t matters.  Each is two
     * whole calls, whose identical tails the compiler merges; `v` is not
     * initialised, so a failed variable read passes stack garbage, as the
     * original does. */
    case 0x20: {

        if (act->xvar > 0) {
            GetVarValue(act->xvar, &v6);
            EvtSetByte40(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                         (int8_t)v6);
            return;
        }
        EvtSetByte40(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                     (int8_t)act->n0);
        return;
    }

    case 0x21: {

        if (act->xvar > 0) {
            GetVarValue(act->xvar, &v7);
            EvtSetByte530(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                          (int8_t)v7);
            break;      /* out of the switch, not `return` -- the layout */
        }
        EvtSetByte530(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                      (int8_t)act->n0);
        return;
    }

    case 0x23: {                                /* setvar */
        /* NOT the 0x20/0x21 shape: this one tests xvar for NON-ZERO where its
         * neighbours test for positive, and it REFUSES outright when the
         * read fails rather than falling back to n0. */

        if (act->xvar != 0) {
            if (GetVarValue(act->xvar, &v8) == 0)
                return;
            SetVarValue(act->subject, v8);
            return;
        }
        SetVarValue(act->subject, act->n0);
        return;
    }

    case 0x22: {                                /* addtovar */
        /* Not the 0x20/0x21 shape: the value starts as n0 and the variable read
         * writes over it, so a failed read leaves n0 rather than 0. */
        v9 = act->n0;

        if (act->xvar > 0)
            GetVarValue(act->xvar, &v9);
        AddToVar(act->subject, v9);
        return;
    }

    case 0x19: {                                /* setaimode + attach */
        /* The same head as 0x18 and a completely different tail: no point
         * handling at all, just a pair of objects or an army attach. */
        if (act->extra == 0) {
            if (act->n0 >= 0)
                EvtSetAiMode(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                             act->n0);
            EvtObjPair(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                       ResolveUid(act->target, (uint32_t)(uintptr_t)owner));
            return;
        }
        if (act->n0 >= 0)
            EvtArmySetField(act->army, act->subject, act->n0);
        EvtArmyAttach(act->army, act->subject,
                      ResolveUid(act->target, (uint32_t)(uintptr_t)owner));
        return;
    }

    case 0x25:                                  /* setobjscriptstate */
        /* target FIRST: the original pushes n0, subject, target in that
         * order, so the LAST push -- target -- is the nameidx. Getting these
         * two round the wrong way makes every lookup fail by name, which the
         * campaign A/B reports as "SetObjScriptState was called with
         * rr_burner_low which is not a valid object". */
        SetObjScriptState(act->target, act->subject, act->n0);
        return;

    case 0x24: {                                /* setobjbitmap */
        /* THE VARIABLE IS READ AND THROWN AWAY. Both branches call with
         * act->n0; the only difference is that a positive xvar gets a
         * GetVarValue call whose answer nothing uses. Second arm in this
         * function to discard a call's result, after playsoundon. */
        int32_t ignored;                /* never cleared: nothing reads it */
        int32_t frame = act->n0;

        if (act->xvar > 0)
            GetVarValue(act->xvar, &ignored);
        ScriptSetObjBitmap(act->subject, frame);
        return;
    }

    case 0x26:                                  /* activateregion */
        if (act->subject < (int32_t)g_regionStride) {
            if (act->n0 != 0)
                ActivateRegion(act->subject);
            else
                InactivateRegion(act->subject);
        }
        return;

    /* Three level strings, three destinations, one shape. */
    case 0x27:                                  /* setbriefing */
        strcpy(&g_levelStrC, act->text);
        return;

    case 0x28:                                  /* setbriefvo */
        strcpy(&g_levelSoundName, act->text);
        return;

    case 0x29:                                  /* setstratmap */
        strcpy(g_levelStrD, act->text);
        return;

    case 0x2A:                                  /* setaimode, army or object */
        /* act->extra chooses the TARGET, not the value: the same act->n0
         * goes to a whole army or to one resolved object. */
        if (act->extra == 0)
            EvtSetAiMode(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                         act->n0);
        else
            EvtArmySetField(act->army, act->subject, act->n0);
        return;

    case 0x2B:
        EvtSetField540(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                       act->n0);
        return;

    case 0x2C:
        EvtSetModeF0(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                     act->n0);
        return;

    case 0x2D:
        EvtSetMode94(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                     act->n0);
        return;

    case 0x2E: {
        /* Both paths call the same function; multiplayer maps the army
         * through its comm SLOT and single-player passes the army itself. */
        int32_t army;

        if (g_mpSession != 0)
            army = ((AM2_Comm *)kCommObject)->CommSlotForArmy(act->army);
        else
            army = act->army;
        EvtType2ActionC(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                        army);
        return;
    }

    case 0x38:
        EvtDropItem(ResolveUid(act->army, (uint32_t)(uintptr_t)owner),
                    ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                    ActionPoint(act, (uint32_t)(uintptr_t)owner));
        return;

    case 0x39:                                  /* setuilock */
        g_inputSuppress = (act->n0 != 0) ? 1 : 0;
        return;

    case 0x2F:
        EvtType2ActionA(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner));
        return;

    case 0x30:
        EvtType2ActionB(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner));
        return;

    case 0x31:                                  /* by point, not by uid */
        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x != 0)
            EvtByRefA(at, act->n0);
        return;

    case 0x32:
        at = ActionPoint(act, (uint32_t)(uintptr_t)owner);
        if (at.x != 0)
            EvtByRefB(at, act->n0);
        return;

    case 0x33:
        EvtSetFlag810(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                      act->n0);
        return;

    case 0x34:                                  /* the owner is ONE BYTE */
        EvtSetOwner(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                    (int8_t)act->n0);
        return;

    case 0x35:
        EvtType238Action(ResolveUid(act->subject, (uint32_t)(uintptr_t)owner),
                         act->n0);
        return;

    case 0x36:                                  /* fireweapon at a point or object */
        /* Two arms of one `case`: an action that names a POINT -- a coordinate
         * variable (xvar), a relative `+` flag, or a nonzero WORD x -- fires at
         * that point; otherwise it fires at the named object. The third test is
         * a WORD. ORIGINAL 0x00420410: the point arm at 0x004211fa, the object
         * arm the fall-through at 0x004211b8; both resolve the weapon as
         * ResolveUid(subject, 0) and the unit as ResolveUid(army, owner), and
         * differ only in the last argument -- ActionPoint for the point,
         * ResolveUid(target, owner) for the object. */
        if (act->xvar > 0 || act->relative != 0 || act->u.pos.x != 0) {
            FireWeaponAtPoint(ResolveUid(act->subject, 0),
                              ResolveUid(act->army,
                                         (uint32_t)(uintptr_t)owner),
                              act->n0,
                              ActionPoint(act, (uint32_t)(uintptr_t)owner));
            return;
        }
        FireWeaponAtObject(ResolveUid(act->subject, 0),
                           ResolveUid(act->army,
                                      (uint32_t)(uintptr_t)owner),
                           act->n0,
                           ResolveUid(act->target,
                                      (uint32_t)(uintptr_t)owner));
        return;

    case 0x37:                                  /* unitfire at a point or object */
        /* 0x36's two arms with the unit's own weapon: a named POINT fires
         * there (0x00420260), otherwise at the named object. The point arm
         * used to be a refusal here, so `unitfire` at a point did nothing.
         * The same me=0 on the subject where the target uses the owner. */
        if (act->xvar > 0 || act->relative != 0 || act->u.pos.x != 0) {
            UnitFireAtPoint(ResolveUid(act->subject, 0), act->n0,
                            ActionPoint(act, (uint32_t)(uintptr_t)owner));
            return;
        }
        UnitFireAtObject(ResolveUid(act->subject, 0), act->n0,
                         ResolveUid(act->target, (uint32_t)(uintptr_t)owner));
        return;

    case 0x3A: {                                /* setdamagepad */
        /* The pad is indexed afresh at every store, as the image does. */
#define pad (g_pads \
             + (size_t)act->subject * AM2_PAD_STRIDE)
        if (act->subject < 0 || act->subject >= g_padCount)
            return;
        *(int32_t *)(pad + PAD_OFF_DAMAGE)      = act->n1;
        *(int32_t *)(pad + 0x38)                = act->n0;
        *(int32_t *)(pad + PAD_OFF_DAMAGE_KIND) = act->extra;

        /* Only a positive damage marks the pad's NUMBER as live, and the
         * number is reached through the pad's own +0x04 rather than by the
         * pad index -- two different arrays with two different strides. */
        if (act->n1 > 0) {
            uint8_t *num = g_padNumbers
                         + (size_t)*(const int32_t *)(pad + 0x04)
                           * AM2_PAD_NUMBER_STRIDE;

            *(int32_t *)(num + PADNUM_OFF_PADS) = 1;
        }
        return;
#undef pad
    }

    case 0x1A:                                  /* the empty action */
        return;

    default:
        return;
    }
}
