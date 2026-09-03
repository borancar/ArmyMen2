/* pad.cpp -- see pad.h. */
#include <stdint.h>
#include <string.h>

#include "pad.h"
#include "objtable.h"  /* AM2_Object -- the uid is all this needs */
#include "objscript.h" /* ObjMatchesSel -- reconstructed */
#include "crt.h"       /* am2_log */
#include "misc.h"      /* ScriptCompare -- reconstructed */
#include "script.h"    /* AllocUid -- reconstructed */
#include "item.h"      /* DamageObject -- reconstructed */
#include "event.h"     /* EventRegister, EventNotify -- reconstructed */
#include "savetag.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kPads        ((void *)(uintptr_t)AM2_IMAGE(ADDR_PADS))
#define kPadNumbers  ((void *)(uintptr_t)AM2_IMAGE(ADDR_PAD_NUMBERS))
#define kPadCount    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_PAD_COUNT))

void __cdecl ResetPads(void)
{
    memset(kPadNumbers, 0, AM2_PAD_NUMBERS_BYTES);
    memset(kPads, 0, AM2_PADS_BYTES);
    kPadCount = 0;
}

int32_t __cdecl SavePadSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_PAD);

    /* Not a tag: the count, as data. The loader reads it with fread. */
    WriteSaveTag(fp, (uint32_t)kPadCount);

    /* Higher address first, which is the order the file wants. */
    orig_fwrite(kPadNumbers, AM2_PAD_NUMBERS_BYTES, 1, fp);
    orig_fwrite(kPads, AM2_PADS_BYTES, 1, fp);
    return 1;
}

int32_t __cdecl LoadPadSection(am2_FILE *fp)
{
    int32_t count;

    /* Before the tag check: a failed load has already forgotten every pad. */
    ResetPads();

    if (!CheckSaveTag(fp, AM2_SAVETAG_PAD,
                      (const char *)AM2_IMAGE(ADDR_STR_PAD_CPP), 0x1AD))
        return 0;

    orig_fread(&count, 4, 1, fp);
    kPadCount = count;

    orig_fread(kPadNumbers, AM2_PAD_NUMBERS_BYTES, 1, fp);
    orig_fread(kPads, AM2_PADS_BYTES, 1, fp);
    return 1;
}

void __cdecl ResetPadsAlias(void)
{
    ResetPads();
}

/* PadNumberEnter and PadNumberLeave -- originals 0x004376C0 and 0x00437770,
 * two callers each, both of them ObjTileHook: when an object's tile changes it
 * works out which pad numbers it has just ENTERED and which it has just LEFT
 * -- `~old & new` and `~new & old` over a byte per tile -- and runs one of
 * these on each.
 *
 * THE TWO ARE MIRROR IMAGES AND THAT IS WHAT NAMES THE COUNT. Entering bumps
 * PAD_OFF_ITEM_COUNT and leaving drops it, once per object that matches the
 * pad's selector; PadFinalise then compares the pad's threshold against it.
 * The field had been PAD_OFF_CMP_B, which is a position rather than a
 * meaning, and the leave half settles it in the program's own words: going
 * below zero logs "pad # %d thinks there are less than zero items on it".
 *
 * A PAD WITH NO COMPARISON DOES NOT COUNT AT ALL. `compared` -- set by the
 * parser when a `<`, `=` or `>` was seen -- chooses between the two arms:
 * with it, the count moves and PadFinalise decides whether that crossed the
 * threshold; without it, the object's arrival IS the event and the notify
 * goes out immediately, type 3 for entering and 2 for leaving. Those are the
 * same two types PadFinalise sends for a pad with no event id, against the
 * same pad id, which is why AM2_PAD_NOTIFY_ENTER and _LEAVE were already
 * named.
 *
 * BOTH ARMS TEST THE SELECTOR, and the original really does write the test
 * twice rather than once above the branch. Reproduced as two calls.
 *
 * The underflow is REPAIRED, not just reported: the count is reset to zero
 * and PadFinalise runs anyway. So a pad that has lost track recovers rather
 * than latching, and the log line is the only trace.
 *
 * The count is re-read from the record every iteration in both, and the
 * entry index is stepped by two bytes -- these are int16 indices into the pad
 * array, not pointers. */

typedef struct {
    int16_t count;
    int16_t pads[1];    /* really 32; only the count bounds the walk */
} AM2_PadNumberHead;

void __cdecl PadNumberEnter(void *obj, void *padNumber)
{
    const AM2_PadNumberHead *pn = (const AM2_PadNumberHead *)padNumber;
    uint8_t *pads = (uint8_t *)kPads;
    int32_t  i    = 0;

    if (pn->count <= 0)
        return;

    do {
        uint8_t *pad = pads + (uint32_t)(int32_t)pn->pads[i] * AM2_PAD_STRIDE;

        if (*(const int32_t *)(pad + PAD_OFF_COMPARED)) {
            if (ObjMatchesSel(*(const int32_t *)(pad + PAD_OFF_SPECIFIC),
                              *(const int32_t *)(pad + PAD_OFF_TRIGGER), obj)) {
                *(int32_t *)(pad + PAD_OFF_ITEM_COUNT) += 1;
                PadFinalise(pad, obj);
            }
        } else if (ObjMatchesSel(*(const int32_t *)(pad + PAD_OFF_SPECIFIC),
                                 *(const int32_t *)(pad + PAD_OFF_TRIGGER),
                                 obj)) {
            EventNotify(AM2_PAD_NOTIFY_ENTER,
                        *(const int32_t *)(pad + PAD_OFF_ID),
                        obj ? ((const AM2_Object *)obj)->uid : 0,
                        0, 0, 0, 0, 0, 0, 0);
        }
        i++;
    } while (i < pn->count);
}

void __cdecl PadNumberLeave(void *obj, void *padNumber)
{
    const AM2_PadNumberHead *pn = (const AM2_PadNumberHead *)padNumber;
    uint8_t *pads = (uint8_t *)kPads;
    int32_t  i    = 0;

    if (pn->count <= 0)
        return;

    do {
        uint8_t *pad = pads + (uint32_t)(int32_t)pn->pads[i] * AM2_PAD_STRIDE;

        if (*(const int32_t *)(pad + PAD_OFF_COMPARED)) {
            if (ObjMatchesSel(*(const int32_t *)(pad + PAD_OFF_SPECIFIC),
                              *(const int32_t *)(pad + PAD_OFF_TRIGGER), obj)) {
                *(int32_t *)(pad + PAD_OFF_ITEM_COUNT) -= 1;
                if (*(const int32_t *)(pad + PAD_OFF_ITEM_COUNT) < 0) {
                    am2_log((const char *)AM2_IMAGE(ADDR_STR_PAD_UNDERFLOW),
                            (int32_t)(((const uint8_t *)pn
                                       - (const uint8_t *)kPadNumbers)
                                      / AM2_PAD_NUMBER_STRIDE));
                    *(int32_t *)(pad + PAD_OFF_ITEM_COUNT) = 0;
                }
                PadFinalise(pad, obj);
            }
        } else if (ObjMatchesSel(*(const int32_t *)(pad + PAD_OFF_SPECIFIC),
                                 *(const int32_t *)(pad + PAD_OFF_TRIGGER),
                                 obj)) {
            EventNotify(AM2_PAD_NOTIFY_LEAVE,
                        *(const int32_t *)(pad + PAD_OFF_ID),
                        obj ? ((const AM2_Object *)obj)->uid : 0,
                        0, 0, 0, 0, 0, 0, 0);
        }
        i++;
    } while (i < pn->count);
}

/* PadFinalise -- original 0x004375A0, one caller.
 *
 * Run one pad's trigger for a frame. A pad is a comparison, an "inside" flag,
 * and two events -- one for entering and one for leaving. This asks the
 * comparison, and acts only on the two transitions.
 *
 * THE TWO HALVES ARE MIRROR IMAGES AND THAT IS WHAT NAMES THE FIELDS.
 * Entering sets the inside flag, clears the LEAVE uid, and arms the ENTER
 * event; leaving clears the flag, clears the ENTER uid, and arms the LEAVE
 * event. Neither field would be identifiable alone; the symmetry is the
 * evidence.
 *
 * NOTHING HAPPENS ON THE TWO NON-TRANSITIONS. True while already inside and
 * false while already outside both return without touching anything, which is
 * what makes this edge-triggered rather than level-triggered.
 *
 * A PAD WITH NO EVENT STILL NOTIFIES, and with a DIFFERENT type: 3 for
 * entering and 2 for leaving, sent against the pad's own id. A pad WITH an
 * event allocates a uid, registers a handler against it, and notifies with
 * type 0 against that uid instead -- carrying the event id as the delay
 * argument and 1 as removeevent. So the two paths differ in the type, the
 * subject and three of the ten arguments, and only one of them registers.
 *
 * THE OBJECT ARGUMENT IS TURNED INTO A UID BEFORE ANYTHING ELSE, and a null
 * one becomes 0 rather than being refused -- so a pad can fire with no object
 * attached, and the notify carries a zero uid.
 *
 * The two handlers are distinct functions, 0x00437570 for entering and
 * 0x00437540 for leaving, and both are still the original's; each is
 * registered with the pad itself as its argument and `owns` clear, so the
 * teardown will not free the pad.
 */
void __cdecl PadFinalise(void *pad, void *obj)
{
    uint8_t *p = (uint8_t *)pad;
    uint32_t uid = obj ? ((const AM2_Object *)obj)->uid : 0;

    if (ScriptCompare(*(const int32_t *)(p + PAD_OFF_CMP_A),
                      *(const int32_t *)(p + PAD_OFF_CMP_OP),
                      *(const int32_t *)(p + PAD_OFF_ITEM_COUNT))) {
        if (*(const int32_t *)(p + PAD_OFF_INSIDE))
            return;

        *(int32_t *)(p + PAD_OFF_INSIDE)    = 1;
        *(int32_t *)(p + PAD_OFF_UID_LEAVE) = 0;

        if (!*(const int32_t *)(p + PAD_OFF_EVENT_ENTER)) {
            EventNotify(AM2_PAD_NOTIFY_ENTER,
                        *(const int32_t *)(p + PAD_OFF_ID), uid,
                        0, 0, 0, 0, 0, 0, 0);
            return;
        }

        *(int32_t *)(p + PAD_OFF_UID_ENTER) = AllocUid();
        EventRegister(0, *(const int32_t *)(p + PAD_OFF_UID_ENTER), 0,
                      (const void *)EvtPadOn, p, 0);
        EventNotify(0, *(const int32_t *)(p + PAD_OFF_UID_ENTER), uid,
                    0, 0, 0, 0,
                    *(const int32_t *)(p + PAD_OFF_EVENT_ENTER), 1, 0);
        return;
    }

    if (!*(const int32_t *)(p + PAD_OFF_INSIDE))
        return;

    *(int32_t *)(p + PAD_OFF_INSIDE)    = 0;
    *(int32_t *)(p + PAD_OFF_UID_ENTER) = 0;

    if (!*(const int32_t *)(p + PAD_OFF_EVENT_LEAVE)) {
        EventNotify(AM2_PAD_NOTIFY_LEAVE,
                    *(const int32_t *)(p + PAD_OFF_ID), uid,
                    0, 0, 0, 0, 0, 0, 0);
        return;
    }

    *(int32_t *)(p + PAD_OFF_UID_LEAVE) = AllocUid();
    EventRegister(0, *(const int32_t *)(p + PAD_OFF_UID_LEAVE), 0,
                  (const void *)EvtPadOff, p, 0);
    EventNotify(0, *(const int32_t *)(p + PAD_OFF_UID_LEAVE), uid,
                0, 0, 0, 0,
                *(const int32_t *)(p + PAD_OFF_EVENT_LEAVE), 1, 0);
}

/* 0x00437860, one caller -- ObjTileChanged, when OBJ_OFF_FLAGS bit 3 is clear.
 * What happens to an object as it crosses trigger pads.
 *
 * THE ORDERING IS THE PART orig.h ALREADY WARNS ABOUT: the damage pass runs
 * BEFORE the tile-changed early exit, so it is not part of the "something
 * moved" path however much its position in the body suggests otherwise.
 *
 * TWO INDEPENDENT PAD MECHANISMS, which is what a partial read destroys. The
 * eight-bit layer looks like the whole function and is not:
 *
 *   ADDR_MAP_PADBIT_LAYER  eight bits per tile. Entered is ~prev & cur and
 *                          left is ~cur & prev, walked against
 *                          ADDR_PAD_BIT_TABLE while stepping ADDR_PAD_NUMBERS
 *                          by its 0x4C stride.
 *   ADDR_MAP_PAD_LAYER     one pad NUMBER per tile. Enter and leave fire when
 *                          that number differs between the two tiles.
 *
 * Both strides come out of the original's lea chains independently of the
 * names -- 19*4 is 76 and 9*8 is 72 -- which is what CONFIRMS ADDR_PAD_NUMBERS
 * and ADDR_PADS rather than merely fitting them.
 *
 * IT RETURNS A FLAG AND THE CALLER DISCARDS IT: 1 when any enter or leave
 * fired, 0 from the unchanged-tile exit, and ObjTileChanged does
 * `call; add esp, 4` without testing eax. orig.h called it void(obj), which is
 * harmless and inaccurate. Reproduced as it is -- the mirror of Log2Mask,
 * where the function writes only al and the vectors needed a byte_ret flag. */
int32_t __cdecl ObjTileHook(void *obj)
{
    uint8_t       *o = (uint8_t *)obj;
    const uint8_t *padLayer = *(const uint8_t **)(uintptr_t)ADDR_MAP_PAD_LAYER;
    const uint8_t *bitLayer;
    uint32_t       tile = *(const uint16_t *)(o + OBJ_OFF_TILE);
    uint32_t       prev;
    int32_t        changed = 0;
    uint8_t        curPad = 0, prevPad = 0;
    uint8_t        curBits = 0, prevBits = 0;
    int32_t        i;

    /* The damage pass, before the early exit. */
    if (padLayer != 0 && padLayer[tile] > 0) {
        const uint8_t *num = (const uint8_t *)AM2_IMAGE(ADDR_PAD_NUMBERS)
                             + (uint32_t)padLayer[tile] * AM2_PAD_NUMBER_STRIDE;

        if (*(const int32_t *)(num + PADNUM_OFF_PADS) != 0
            && *(const int16_t *)(num + PADNUM_OFF_COUNT) > 0) {
            const int16_t *ids = (const int16_t *)(num + PADNUM_OFF_IDS);
            int32_t        n = *(const int16_t *)(num + PADNUM_OFF_COUNT);

            for (i = 0; i < n; i++) {
                const uint8_t *pad = (const uint8_t *)AM2_IMAGE(ADDR_PADS)
                                     + (uint32_t)ids[i] * AM2_PAD_STRIDE;

                if (*(const int32_t *)(pad + PAD_OFF_DAMAGE) != 0
                    && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                       > *(const uint32_t *)(pad + PAD_OFF_DAMAGE_DUE))
                    DamageObject(o,
                                 *(const int32_t *)(pad + PAD_OFF_DAMAGE),
                                 *(const int32_t *)(pad + PAD_OFF_DAMAGE_KIND),
                                 0, 0, 0);
            }
        }
    }

    prev = *(const uint32_t *)(o + OBJ_OFF_PREV_TILE);
    if (tile == prev)
        return 0;

    /* The eight-bit layer. */
    bitLayer = *(const uint8_t **)(uintptr_t)ADDR_MAP_PADBIT_LAYER;
    if (bitLayer != 0) {
        if (tile != 0) curBits  = bitLayer[tile];
        if (prev != 0) prevBits = bitLayer[prev];

        if (curBits != prevBits) {
            uint8_t entered = (uint8_t)(~prevBits & curBits);
            uint8_t left    = (uint8_t)(~curBits  & prevBits);
            const int32_t *mask = (const int32_t *)AM2_IMAGE(ADDR_PAD_BIT_TABLE);
            uint8_t *num = (uint8_t *)AM2_IMAGE(ADDR_PAD_NUMBERS);

            for (i = 0; i < AM2_PAD_BITS; i++, num += AM2_PAD_NUMBER_STRIDE) {
                if (mask[i] & entered)
                    PadNumberEnter(o, num);
                else if ((uint8_t)mask[i] & left)
                    PadNumberLeave(o, num);
            }
            changed = 1;
        }
    }

    /* And the pad-number layer, which is a separate mechanism. */
    if (padLayer != 0) {
        if (tile != 0) curPad  = padLayer[tile];
        if (prev != 0) prevPad = padLayer[prev];

        if (curPad != prevPad) {
            if (curPad != 0)
                PadNumberEnter(o, (uint8_t *)AM2_IMAGE(ADDR_PAD_NUMBERS)
                                  + (uint32_t)curPad * AM2_PAD_NUMBER_STRIDE);
            if (prevPad != 0)
                PadNumberLeave(o, (uint8_t *)AM2_IMAGE(ADDR_PAD_NUMBERS)
                                  + (uint32_t)prevPad * AM2_PAD_NUMBER_STRIDE);
            changed = 1;
        }
    }

    return changed;
}


/* 0x00437570 and 0x00437540, registered here and by event.cpp's savegame
 * restore.
 *
 * What a pad raises once it has scheduled an enter or leave event: compare the
 * uid the pad recorded for that transition against the one the notify carried,
 * and if they match, raise the pad event itself with the pad's own id.
 *
 * ONE FUNCTION PARAMETERISED TWICE, and that is measured rather than eyeballed
 * -- the two bodies are 48 bytes and differ in THREE, of which only two are
 * semantic: the field (0x28 against 0x2C) and the event type (3 against 2).
 * The third is the call displacement, a relocation, since the two calls sit 48
 * bytes apart and reach the same EventNotify. Written out rather than merged
 * because at this size the shared helper is longer than the pair, and because
 * which field each reads is the entire distinction between them.
 *
 * They were ADDR_EVT_PAD_HANDLER_A and _B. Two namings the tree already
 * carried agree on what they are: PAD_OFF_UID_ENTER at 0x28 pairs with
 * AM2_EVT_PADON, and PAD_OFF_UID_LEAVE at 0x2C with AM2_EVT_PADOFF.
 *
 * Argument 8 is the pad EventRegister stored and argument 2 is the uid to
 * match, which is the convention every handler in event.cpp follows. */
static void EvtPadRaise(const uint8_t *pad, int32_t want, int32_t payload,
                        uint32_t field, int32_t type)
{
    if (*(const int32_t *)(pad + field) != want)
        return;

    EventNotify(type, *(const int32_t *)(pad + PAD_OFF_ID), (uint32_t)payload,
                0, 0, 0, 0, 0, 0, 0);
}

void __cdecl EvtPadOn(int32_t a1, int32_t want, int32_t payload, int32_t a4,
                      int32_t a5, int32_t a6, int32_t a7, const uint8_t *pad)
{
    (void)a1; (void)a4; (void)a5; (void)a6; (void)a7;
    EvtPadRaise(pad, want, payload, PAD_OFF_UID_ENTER, AM2_EVT_PADON);
}

void __cdecl EvtPadOff(int32_t a1, int32_t want, int32_t payload, int32_t a4,
                       int32_t a5, int32_t a6, int32_t a7, const uint8_t *pad)
{
    (void)a1; (void)a4; (void)a5; (void)a6; (void)a7;
    EvtPadRaise(pad, want, payload, PAD_OFF_UID_LEAVE, AM2_EVT_PADOFF);
}

void pad_install(void)
{
    patch_replace(ADDR_OBJ_TILE_HOOK, (const void *)ObjTileHook,
                  "ObjTileHook", 1);
    patch_replace(ADDR_PAD_FINALISE, (const void *)PadFinalise,
                  "PadFinalise", 1);
    patch_replace(ADDR_PAD_NUMBER_ENTER, (const void *)PadNumberEnter,
                  "PadNumberEnter", 2);
    patch_replace(ADDR_PAD_NUMBER_LEAVE, (const void *)PadNumberLeave,
                  "PadNumberLeave", 2);
    patch_replace(ADDR_RESET_PADS, (const void *)ResetPads, "ResetPads", 0);
    patch_replace(ADDR_RESET_PADS_ALIAS, (const void *)ResetPadsAlias,
                  "ResetPadsAlias", 0);
    patch_replace(ADDR_SAVE_PAD_SECTION, (const void *)SavePadSection,
                  "SavePadSection", 1);
    patch_replace(ADDR_LOAD_PAD_SECTION, (const void *)LoadPadSection,
                  "LoadPadSection", 1);
    patch_replace(ADDR_EVT_PAD_ON, (const void *)EvtPadOn, "EvtPadOn", 1);
    patch_replace(ADDR_EVT_PAD_OFF, (const void *)EvtPadOff, "EvtPadOff", 1);
}
