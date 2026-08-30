/* pad.cpp -- see pad.h. */
#include <stdint.h>
#include <string.h>

#include "pad.h"
#include "objtable.h"  /* AM2_Object -- the uid is all this needs */
#include "objscript.h" /* ObjMatchesSel -- reconstructed */
#include "crt.h"       /* am2_log */
#include "misc.h"      /* ScriptCompare -- reconstructed */
#include "script.h"    /* AllocUid -- reconstructed */
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
                      (const void *)(uintptr_t)ADDR_EVT_PAD_HANDLER_A, p, 0);
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
                  (const void *)(uintptr_t)ADDR_EVT_PAD_HANDLER_B, p, 0);
    EventNotify(0, *(const int32_t *)(p + PAD_OFF_UID_LEAVE), uid,
                0, 0, 0, 0,
                *(const int32_t *)(p + PAD_OFF_EVENT_LEAVE), 1, 0);
}

void pad_install(void)
{
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
}
