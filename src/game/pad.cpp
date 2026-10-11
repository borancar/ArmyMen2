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

#ifdef AM2_STANDALONE
/* am2_pad_bit_table -- 0x00486444, 66 int32: the set-bit masks 1<<0..1<<31,
 * then the clear-bit masks ~(1<<n), then 0x7FFFFFFF and two zeros. Read as
 * `table[n]` by the pad bit logic. Transcribed out of the blob. */
extern "C" const int32_t am2_pad_bit_table[66] = {
    1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384,
    32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608,
    16777216, 33554432, 67108864, 134217728, 268435456, 536870912, 1073741824,
    (int32_t)0x80000000,
    -2, -3, -5, -9, -17, -33, -65, -129, -257, -513, -1025, -2049, -4097,
    -8193, -16385, -32769, -65537, -131073, -262145, -524289, -1048577,
    -2097153, -4194305, -8388609, -16777217, -33554433, -67108865, -134217729,
    -268435457, -536870913, -1073741825, 2147483647, 0, 0,
};
#endif

void __cdecl ResetPads(void)
{
    memset(g_padNumbers, 0, AM2_PAD_NUMBERS_BYTES);
    memset(g_pads, 0, AM2_PADS_BYTES);
    g_padCount = 0;
}

int32_t __cdecl SavePadSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_PAD);

    /* Not a tag: the count, as data. The loader reads it with fread. */
    WriteSaveTag(fp, (uint32_t)g_padCount);

    /* Higher address first, which is the order the file wants. */
    orig_fwrite(g_padNumbers, AM2_PAD_NUMBERS_BYTES, 1, fp);
    orig_fwrite(g_pads, AM2_PADS_BYTES, 1, fp);
    return 1;
}

int32_t __cdecl LoadPadSection(am2_FILE *fp)
{
    int32_t count;

    /* Before the tag check: a failed load has already forgotten every pad. */
    ResetPads();

    if (!CheckSaveTag(fp, AM2_SAVETAG_PAD,
                      "C:\\ArmyMen2\\source\\pad.cpp", 0x1AD))
        return 0;

    orig_fread(&count, 4, 1, fp);
    g_padCount = count;

    orig_fread(g_padNumbers, AM2_PAD_NUMBERS_BYTES, 1, fp);
    orig_fread(g_pads, AM2_PADS_BYTES, 1, fp);
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

void __cdecl PadNumberEnter(AM2_Object *obj, AM2_PadNumber *padNumber)
{
    const AM2_PadNumber *pn = padNumber;
    AM2_Pad *pads = g_pads;
    int32_t  i    = 0;

    if (pn->count <= 0)
        return;

    do {
        AM2_Pad *pad = &pads[pn->pads[i]];

        if (pad->compared) {
            if (ObjMatchesSel(pad->specific,
                              pad->trigger, obj)) {
                pad->itemCount += 1;
                PadFinalise(pad, obj);
            }
        } else if (ObjMatchesSel(pad->specific,
                                 pad->trigger,
                                 obj)) {
            EventNotify(AM2_PAD_NOTIFY_ENTER,
                        pad->id,
                        ((const AM2_Object *)obj)->uid,
                        0, 0, 0, 0, 0, 0, 0);
        }
        i++;
    } while (i < pn->count);
}

void __cdecl PadNumberLeave(AM2_Object *obj, AM2_PadNumber *padNumber)
{
    const AM2_PadNumber *pn = padNumber;
    AM2_Pad *pads = g_pads;
    int32_t  i    = 0;

    if (pn->count <= 0)
        return;

    do {
        AM2_Pad *pad = &pads[pn->pads[i]];

        if (pad->compared) {
            if (ObjMatchesSel(pad->specific,
                              pad->trigger, obj)) {
                pad->itemCount -= 1;
                if (pad->itemCount < 0) {
                    am2_log("pad # %d thinks there are less than zero items on it\n",
                            /* A signed divide of the BYTE distance: the image's
                             * imul by 76's magic. Element subtraction compiles
                             * to VC6's unsigned exact divide instead. */
                            (int32_t)(((const uint8_t *)pn
                                       - (const uint8_t *)g_padNumbers)
                                      / AM2_PAD_NUMBER_STRIDE));
                    pad->itemCount = 0;
                }
                PadFinalise(pad, obj);
            }
        } else if (ObjMatchesSel(pad->specific,
                                 pad->trigger,
                                 obj)) {
            EventNotify(AM2_PAD_NOTIFY_LEAVE,
                        pad->id,
                        ((const AM2_Object *)obj)->uid,
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
void __cdecl PadFinalise(AM2_Pad *pad, AM2_Object *obj)
{
    AM2_Pad *p = pad;
    uint32_t uid = obj ? ((const AM2_Object *)obj)->uid : 0;

    if (ScriptCompare(p->threshold,
                      p->compare,
                      p->itemCount)) {
        if (p->inside)
            return;

        p->inside    = 1;
        p->uidLeave = 0;

        if (!p->delay0) {
            EventNotify(AM2_PAD_NOTIFY_ENTER,
                        p->id, uid,
                        0, 0, 0, 0, 0, 0, 0);
            return;
        }

        p->uidEnter = AllocUid();
        EventRegister(0, p->uidEnter, 0,
                      (const void *)EvtPadOn, p, 0);
        EventNotify(0, p->uidEnter, uid,
                    0, 0, 0, 0,
                    p->delay0, 1, 0);
        return;
    }

    if (!p->inside)
        return;

    p->inside    = 0;
    p->uidEnter = 0;

    if (!p->delay1) {
        EventNotify(AM2_PAD_NOTIFY_LEAVE,
                    p->id, uid,
                    0, 0, 0, 0, 0, 0, 0);
        return;
    }

    p->uidLeave = AllocUid();
    EventRegister(0, p->uidLeave, 0,
                  (const void *)EvtPadOff, p, 0);
    EventNotify(0, p->uidLeave, uid,
                0, 0, 0, 0,
                p->delay1, 1, 0);
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
int32_t __cdecl ObjTileHook(AM2_Object *obj)
{
    AM2_Object       *o = (AM2_Object *)obj;
    const uint8_t *bitLayer;
    uint32_t tile;
    uint32_t       prev;
    int32_t        changed;
    uint8_t        curBits;
    uint8_t        prevPad;
    uint8_t        curPad;
    uint8_t        prevBits;
    int32_t        i;

    /* Zeroed in the order the image stores them. */
    prevBits = 0;
    curBits = 0;
    prevPad = 0;
    curPad = 0;
    changed = 0;

    /* The damage pass, before the early exit. The original indexes the
     * pad layer here WITHOUT the null test both later passes make. */
    tile = o->tile;
    if (g_mapPadLayer[tile] > 0) {
        const AM2_PadNumber *num = &g_padNumbers[g_mapPadLayer[tile]];

        if (num->live != 0
            && num->count > 0) {
            /* The count is re-read each pass, as the image compares it; the
             * test above stands for the first. */
            i = 0;
            do {
                const AM2_Pad *pad = &g_pads[num->pads[i]];

                if (pad->damage != 0
                    && g_gameClockMs
                       > (uint32_t)pad->dueAt)
                    DamageObject(o,
                                 pad->damage,
                                 pad->damageKind,
                                 0, 0, 0);
            } while (++i < num->count);
        }
    }

    /* The tile is read from the object again for the compare. */
    prev = o->prevTile;
    if (o->tile == prev)
        return 0;

    /* The eight-bit layer. */
    bitLayer = g_mapPadbitLayer;
    if (g_mapPadbitLayer != 0) {      /* the global, re-read for the test */
        if (tile != 0) curBits  = bitLayer[tile];
        if (prev != 0) prevBits = bitLayer[prev];

        if (curBits != prevBits) {
            uint8_t entered = (uint8_t)(~prevBits & curBits);
            uint8_t left    = (uint8_t)(~curBits  & prevBits);
            const int32_t *mask = g_padBitTable;
            AM2_PadNumber *num = g_padNumbers;

            for (i = 0; i < AM2_PAD_BITS; i++, num++) {
                if (mask[i] & entered)
                    PadNumberEnter(o, num);
                else if (mask[i] & left)
                    PadNumberLeave(o, num);
            }
            changed = 1;
        }
    }

    /* And the pad-number layer, which is a separate mechanism. */
    if (g_mapPadLayer != 0) {
        if (tile != 0) curPad  = g_mapPadLayer[tile];
        if (prev != 0) prevPad = g_mapPadLayer[prev];

        if (curPad != prevPad) {
            if (curPad != 0)
                PadNumberEnter(o, &g_padNumbers[curPad]);
            if (prevPad != 0)
                PadNumberLeave(o, &g_padNumbers[prevPad]);
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
static inline void EvtPadRaise(const AM2_Pad *pad, int32_t uid, int32_t want,
                               int32_t payload, int32_t type)
{
    if (uid != want)
        return;

    EventNotify(type, pad->id, (uint32_t)payload,
                0, 0, 0, 0, 0, 0, 0);
}

void __cdecl EvtPadOn(int32_t a1, int32_t want, int32_t payload, int32_t a4,
                      int32_t a5, int32_t a6, int32_t a7, const AM2_Pad *pad)
{
    (void)a1; (void)a4; (void)a5; (void)a6; (void)a7;
    EvtPadRaise(pad, pad->uidEnter, want, payload, AM2_EVT_PADON);
}

void __cdecl EvtPadOff(int32_t a1, int32_t want, int32_t payload, int32_t a4,
                       int32_t a5, int32_t a6, int32_t a7, const AM2_Pad *pad)
{
    (void)a1; (void)a4; (void)a5; (void)a6; (void)a7;
    EvtPadRaise(pad, pad->uidLeave, want, payload, AM2_EVT_PADOFF);
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
