/* air.cpp -- see air.h. */
#include <stdint.h>
#include <stdlib.h>   /* abs */

#include "air.h"
#include "rect.h"     /* AM2_Rect, Clamp */
#include "region.h"   /* SettlePointInRegion -- reconstructed */
#include "map.h"      /* TileOfPoint -- reconstructed */
#include "item.h"     /* UidArmy -- reconstructed */
#include "objtable.h" /* AM2_Object, FirstItem, NextItem */
#include "dist.h"     /* ApproxDist, AngleDelta -- reconstructed */
#include "trig.h"     /* Cos8, Sin8 -- reconstructed */
#include "objtype.h"  /* ObjIsTypeIn238 -- reconstructed */
#include "crt.h"      /* am2_free, am2_realloc -- the game's own */
#include "savetag.h"
#include "packkey.h" /* KeyLookupTriple -- reconstructed */
#include "misc.h"    /* CommArmyOfSlot -- reconstructed */
#include "armymsg.h" /* SendTrooperSetWeapon -- reconstructed */
#include "spritetype.h" /* AM2_Sprite */
#include "image.h"
#include "misc.h"   /* MeetsAllThree -- reconstructed */
#include "msgslot.h" /* CommMustBroadcast -- reconstructed */
#include "script.h"     /* ScriptFindName -- reconstructed */
#include "scriptint.h"  /* kScriptNames */

#ifdef AM2_STANDALONE
/* The nine airstrike formation-slot records at 0x0048BD90 ({int32 parent,
 * uint8 angle, pad, int16 dist}, 8 bytes each) that SlotFormationPoint reads
 * for slots 1..8 (record 0 is the -1 sentinel). Read through a uint8_t* at
 * byte offsets, so transcribed as the byte blob it is; placed at its VA. */
/* The two air-path turn-Y scratch words at 0x00473F6E/0x00473F72 (int16, zero
 * at init, written by the airstrike path setup), each between placed int16
 * neighbours. All-zero, so forced into .data. */
int16_t am2_air_path_turn_y_in[1]  __attribute__((section(".data.am2_air_path_turn_y_in")))  = { 0 };
int16_t am2_air_path_turn_y_out[1] __attribute__((section(".data.am2_air_path_turn_y_out"))) = { 0 };
extern "C" const uint8_t am2_slot_recs[72] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,   /* [0] parent -1 */
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x50, 0x00,   /* [1] angle 128 dist 80 */
    0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x44, 0x00,   /* [2] angle 96  dist 68 */
    0x00, 0x00, 0x00, 0x00, 0xA0, 0x00, 0x44, 0x00,   /* [3] angle 160 dist 68 */
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x44, 0x00,   /* [4] angle 32  dist 68 */
    0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x44, 0x00,   /* [5] angle 224 dist 68 */
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x50, 0x00,   /* [6] angle 64  dist 80 */
    0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x50, 0x00,   /* [7] angle 192 dist 80 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00,   /* [8] angle 0   dist 80 */
};
#endif
#include "army.h"   /* ObjIsFriendly -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "commmsg.h"  /* MsgListSetFlag, MsgListAdd -- reconstructed */
#include "gameproc.h" /* NoteKind31 -- reconstructed */
#include "maprow.h"   /* RowUpdate -- reconstructed */

#ifdef AM2_STANDALONE
/* Global structures transcribed out of the carried .rdata blob -- the air
 * strike's path/drop tables and the formation slot table. See STATUS.md's
 * MIGRATION note; each is byte-identical to the image and referenced only from
 * reconstructed code. */

/* am2_formation_slots -- 0x00473EA0, twelve 6-byte slot records: byte 0 is the
 * facing, bytes 2..3 an int16 distance, the rest padding (FormationPoint reads
 * `*rec` and `*(int16 *)(rec+2)`; stride AM2_FORMATION_SLOT_STRIDE = 6). */
extern "C" const uint8_t am2_formation_slots[72] = {
    128,0,  64,0, 0,0,    96,0,  48,0, 0,0,   160,255, 48,0, 0,0,
    128,0,  96,0, 0,0,    32,0,  96,0, 0,0,   224,255, 96,0, 0,0,
     64,0,  64,0, 0,0,   192,255, 64,0, 0,0,     0,0, 128,0, 0,0,
     96,0,  96,0, 0,0,   160,255, 96,0, 0,0,   128,0, 128,0, 0,0,
};

/* am2_air_drop_offsets -- 0x00473F74, three {int16 dx, int16 dy} drop points. */
extern "C" const int16_t am2_air_drop_offsets[6] = { 0, 0, -48, 32, 48, 32 };

/* am2_air_drop_facings -- 0x00473F80, the facing for each of the three drops. */
extern "C" const uint8_t am2_air_drop_facings[3] = { 0, 96, 160 };

/* am2_air_frame_hotspots -- 0x00473FB0, eleven {int16 x, int16 y} sprite
 * hotspots for the transport's animation frames. */
extern "C" const int16_t am2_air_frame_hotspots[22] = {
    118, 87, 112, 78, 109, 67, 109, 64, 108, 66, 105, 68,
    108, 71, 105, 71, 101, 67,  91, 60,  77, 66,
};

/* am2_air_strike_kinds -- 0x00473FDC, the six object kinds an air strike can
 * deliver. */
extern "C" const int32_t am2_air_strike_kinds[6] = { 129, 130, 140, 120, 148, 149 };

/* The air-strike gauge and flight-path scalars, read as *(const int32_t/double/
 * int16_t *) at their own addresses (0x00473F20..0x00473F74). Types are the
 * readers': the three slopes are double at 8-byte spacing, the four PATH_TURN/
 * AWAY values are int16 packed two-per-dword just below AIR_DROP_OFFSETS. */
extern "C" const int32_t am2_air_gauge_x0    = -278;    /* 0x00473F20 */
extern "C" const int32_t am2_air_gauge_y0    = 320;     /* 0x00473F24 */
extern "C" const double  am2_air_gauge_slope = 0.43;    /* 0x00473F28 */
extern "C" const int32_t am2_air_gauge_ms    = 2500;    /* 0x00473F30 */
extern "C" const int32_t am2_air_pass_ms     = 3000;    /* 0x00473F34 */
extern "C" const int32_t am2_air_cycle_ms    = 1000;    /* 0x00473F38 */
extern "C" const int32_t am2_air_run_ms      = 2000;    /* 0x00473F3C */
extern "C" const int32_t am2_air_leg1_ms     = 800;     /* 0x00473F40 */
extern "C" const int32_t am2_air_leg2_ms     = 1200;    /* 0x00473F44 */
extern "C" const int32_t am2_air_path_in_y   = 520;     /* 0x00473F48 */
extern "C" const int32_t am2_air_path_out_y  = -100;    /* 0x00473F4C */
extern "C" const double  am2_air_leg1_slope  = 0.95;    /* 0x00473F50 */
extern "C" const double  am2_air_leg3_slope  = 1.18;    /* 0x00473F58 */
extern "C" const int32_t am2_air_path_mid_y  = 240;     /* 0x00473F60 */
extern "C" const int32_t am2_air_path_apex_x = 340;     /* 0x00473F64 */
extern "C" const int32_t am2_air_path_half_y = 80;      /* 0x00473F68 */
extern "C" const int16_t am2_air_path_turn_x     = 300; /* 0x00473F6C */
extern "C" const int16_t am2_air_path_away_x     = 300; /* 0x00473F70 */
/* 0x00473F6E TURN_Y_IN and 0x00473F72 TURN_Y_OUT are NOT migrated: they are
 * runtime state, recomputed by AirInitTurnYIn/Out at init (see below), so they
 * stay at their writable .origdat placement rather than a read-only C const. */
#endif

#define kAirSaveBlock (g_airSaveBlock)

int32_t __cdecl SaveAirSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_AIR);
    orig_fwrite(kAirSaveBlock, AM2_AIR_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadAirSection(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_AIR,
                      "C:\\ArmyMen2\\source\\air.cpp", 0x28B))
        return 0;

    orig_fread(kAirSaveBlock, AM2_AIR_SAVE_SIZE, 1, fp);
    return 1;
}

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header because this module is on the flat side of the
 * split and audio.h names Win32 types -- the same reason commmsg.cpp does it,
 * and spelled the same way so the two cannot drift. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int16_t x, int16_t y);

/* Every one of these is a field of the block above -- the queue and the
 * savegame section are the same 584 bytes, which is why they are written as
 * offsets rather than as addresses of their own. */
#define g_airActive  (kAirSaveBlock->active)
#define g_airPending (kAirSaveBlock->pending)
#define g_airCount   (kAirSaveBlock->count)
#define g_airWhere   (kAirSaveBlock->where)
#define g_airKind    (kAirSaveBlock->kind)
#define g_airFrom    (kAirSaveBlock->from)
#define g_airExtra   (kAirSaveBlock->extra)
#define g_airFlagA   (kAirSaveBlock->flagA)
#define g_airFlagB   (kAirSaveBlock->flagB)

typedef uint32_t (__stdcall *AM2_GetTickCountFn)(void);
#define orig_get_tick_count \
    (*(AM2_GetTickCountFn *)g_iatGetTickCount)

/* PeerShouldNack -- original 0x00402C30, one caller. Should a NACK go out for
 * this sequence number, or has one gone recently enough? It names itself twice
 * -- "Nacking %6d to %x ..." and " Nack Rec Array full for ID %x, %d" -- and
 * the first of those names four of the peer's fields as well, which is why
 * this one needed no guessing at all.
 *
 * THE RATE LIMIT IS THE MEASURED LATENCY, CAPPED. Two running sums are divided
 * by their counts: PEER_OFF_INTERVAL_SUM by INTERVAL_N minus one, and
 * PEER_OFF_LATENCY_SUM by LATENCY_N. The latency is what a repeat must wait,
 * capped at AM2_NACK_INTERVAL_CAP -- and the log prints BOTH the capped value
 * (as `nackinterval`) and the uncapped one (as `Latency`), which is how the
 * two are told apart.
 *
 * The two divisors are guarded differently and both are reproduced: the
 * interval's is `n < 2 ? 1 : n - 1` and the latency's is `n < 1 ? 1 : n`. So
 * the first averages over the GAPS between samples and the second over the
 * samples, which is what those two quantities are.
 *
 * A SEQUENCE NOT SEEN BEFORE IS APPENDED AND THE ANSWER IS A GUESS. The new
 * record's count starts at 1 or 0 by whether PEER_OFF_FIELD_40 is below half
 * of PEER_OFF_FIELD_38, and that same flag is what the function returns. So
 * the first nack for a sequence is sent or withheld on a ratio between two
 * fields nothing here identifies, and every later one on the clock.
 *
 * THE ARRAY NEVER GROWS PAST FIFTY-NINE and the last slot is reused rather
 * than the append refused: the count is incremented, tested against
 * AM2_NACK_RECS_MAX, and decremented back with a message. So a peer that
 * misses sixty distinct sequences keeps overwriting slot 59, and the record
 * for the sixtieth is lost the moment the sixty-first arrives. The message is
 * NOT behind COMM_OFF_VERBOSE, unlike the other one.
 *
 * The count is tested twice on the way into the search -- `je` and then `jle`
 * -- so a negative count skips the loop where the first test alone would not.
 * Written as the `> 0` the pair amounts to.
 */
int32_t __cdecl PeerShouldNack(AM2_Flow *peer, uint32_t seq)
{
    AM2_Flow *p   = peer;
    uint32_t  now = (uint32_t)orig_get_tick_count();
    uint32_t  n;
    uint32_t  interval;
    uint32_t  latency;
    uint32_t  capped;
    int32_t   count;
    int32_t   i;

    n = p->nextExpected;
    interval = p->gapTotal
               / (n < 2 ? 1u : n - 1u);

    n = p->acksSeen;
    latency = p->rttTotal / (n < 1 ? 1u : n);
    capped = latency > AM2_NACK_INTERVAL_CAP ? AM2_NACK_INTERVAL_CAP : latency;

    /* The records are addressed through the live count at every use; the
     * match is handled out of line, after the append, as the image lays it
     * out. */
#define NCOUNT (p->nackCount)
#define NREC(k) (&p->nacks[(uint32_t)(k)])
    count = NCOUNT;
    if (count) {
        for (i = 0; i < count; i++)
            if (NREC(i)->seq == seq)
                goto found;
    }

    {
        int32_t first = p->duplicates
                        < (p->nacksSent >> 1);

        NREC(NCOUNT)->seq   = seq;
        NREC(NCOUNT)->time  = now;
        NREC(NCOUNT)->count = first ? 1 : 0;

        if (++NCOUNT >= AM2_NACK_RECS_MAX) {
            NCOUNT--;
            am2_log(" Nack Rec Array full for ID %x, %d\n",
                    p->id, NCOUNT);
        }
        return first;
    }

found:
    {
        uint32_t *t = &NREC(i)->time;

        if (now > *t + capped) {
            if (g_commObject->verbose)
                am2_log("Nacking %6d to %x at %d  >?  %d  ( %d ) Interval = %d Latency = %d count = %d nackinterval = %d\n",
                        seq, p->id, now,
                        *t + capped, now - *t - capped,
                        interval, latency,
                        (int32_t)NREC(i)->count, capped);

            NREC(i)->count += 1;
            *t = now;
            return 1;
        }
        return 0;
    }
#undef NREC
#undef NCOUNT
}

/* Forward-declared rather than included: ObjectsInRect lives in
 * win32/mapdraw.h because it clips with IntersectRect, and including that
 * header here would drag windows.h into a flat module. The same reason
 * script.cpp forward-declares PreloadSprite. */
struct AM2_Object *__cdecl ObjectsInRect(const AM2_Rect *r, const struct AM2_MapDesc *desc,
                            int32_t (__cdecl *keep)(AM2_Object *obj));

uint32_t __cdecl FindEnemyNear(AM2_Point where, uint32_t from)
{
    AM2_Rect  box;
    AM2_Object  *o;

    /* `where` is a packed point, read a half at a time from its own slot. */
    box.left   = where.x - AM2_AIR_ENEMY_RADIUS;
    box.right  = where.x + AM2_AIR_ENEMY_RADIUS;
    box.top    = where.y - AM2_AIR_ENEMY_RADIUS;
    box.bottom = where.y + AM2_AIR_ENEMY_RADIUS;

    o = ObjectsInRect(&box, g_objMapDesc,
                                 MeetsAllThree);

    /* `owner` is objtable.h's AM2_Object field at 0x0010, which orig.h's
     * OBJ_OFF_OWNER is NOT -- that constant is 0x0004 and belongs to a
     * different structure entirely. Two right names, one collision. */
    for (; o; o = o->queryNext) {
        const AM2_Object *obj = o;

        /* Once per candidate, not once before the loop. */
        if ((int32_t)obj->owner != (int32_t)UidArmy(from)
            && o->health > 0)
            return obj->uid;
    }
    return 0;
}


/* 0x004296E0, eight callers. Reveal one object: show it through the fog.
 *
 * Two flags and they are not symmetric. `OBJ_FLAG_REVEALED` goes up
 * unconditionally and is what callers test to know this has been done;
 * `OBJ_FLAG_CONCEALED` is the one that gates the work, and if it was already
 * down the rows are left alone. So calling this twice raises the first bit
 * twice and re-links once, which is the point of having two.
 *
 * The row loop re-reads the count every iteration and clears bit 1 of each
 * row before calling ADDR_ROW_UPDATE, in that order -- a clear bit 1 is what
 * makes that function RE-LINK the row into the map's cell lists, which is how
 * a revealed object comes back onto the map. ADDR_OBJ_CONCEAL is the exact
 * inverse, setting the bit and removing.
 *
 * This was `TakeOffMap`, and both flags were named the other way round too.
 * See the fog-of-war block in orig.h: the cheat table settles it, because
 * "I see everything!" is what reaches this function. */
void __cdecl RevealObj(AM2_Object *o)
{
    int32_t     i;

    if (!o)
        return;

    o->flags |= OBJ_FLAG_REVEALED;
    if (!(o->flags & OBJ_FLAG_CONCEALED))
        return;
    o->flags &= ~(uint32_t)OBJ_FLAG_CONCEALED;

    for (i = 0; i < o->rowSet.count; i++) {
        AM2_MapRow *row = &o->rowSet.rows[i];

        row->flags &= ~(uint32_t)ROW_FLAG_REMOVED;
        RowUpdate(row, 0, g_mapDesc);
    }
}


/* 0x00429650, thirteen callers. RevealObj's inverse: sets OBJ_FLAG_CONCEALED
 * and takes the object's rows OUT of the map's cell lists.
 *
 * It is not a mirror image, though, and the two places it differs are the
 * whole of its extra behaviour.
 *
 * FIRST, IT CLEARS OBJ_FLAG_REVEALED ON THE WAY IN -- but not always. The
 * clear is skipped when the object carries flag 0x10 AND is a type 2 AND its
 * +0x530 is 5. Three conditions deep, and the middle one costs a call. What
 * that combination means is not established; what it does is leave the
 * revealed flag standing while the object is concealed, which the reveal side
 * has no equivalent of.
 *
 * SECOND, IT HAS A FORCE ARGUMENT and RevealObj does not. Without it the
 * function declines unless ADDR_FOG_OF_WAR is ZERO, which is the fog-ON state
 * -- see the note on that global, whose polarity is the opposite of its name.
 * So a caller can conceal something while the fog cheat has everything
 * revealed, and the cheat's own sweep passes 0 so it cannot.
 *
 * The row loop is RevealObj's with both bits the other way: ROW_FLAG_REMOVED
 * is SET rather than cleared before RowUpdate, which is what makes that
 * function unlink the row instead of re-linking it.
 *
 * VERIFIED BY READING. Thirteen callers and the fog cheat is the reachable
 * one, which needs a typed cheat code this project does not drive. */
void __cdecl ObjConceal(AM2_Object *obj, int32_t force)
{
    int32_t  i;

    if (!obj)
        return;

    if (obj->flags & OBJ_FLAG_CONCEALED)
        return;

    /* A BIT4 object keeps REVEALED unless it is a type-2 whose +0x530 is not
     * 5: the image's failed ObjIsType2 jumps past the clear (0x00429671). */
    if (!(obj->flags & OBJ_FLAG_BIT4))
        obj->flags &= ~(uint32_t)OBJ_FLAG_REVEALED;
    else if (ObjIsType2((const AM2_Object *)obj)
             && obj->field530 != 5)
        obj->flags &= ~(uint32_t)OBJ_FLAG_REVEALED;

    if (g_fogOfWar && !force)
        return;

    obj->flags |= OBJ_FLAG_CONCEALED;

    for (i = 0; i < obj->rowSet.count; i++) {
        uint8_t *row = ((uint8_t *)obj->rowSet.rows)
                       + (uint32_t)i * AM2_OBJ_ROW_STRIDE;

        *(uint32_t *)row |= (uint32_t)ROW_FLAG_REMOVED;
        RowUpdate((AM2_MapRow *)row, 0, g_mapDesc);
    }
}

/* 0x004295C0, one caller -- WndProc's setup-done handler, which passes bit 18
 * of the game flags, so fog is a negotiated multiplayer option.
 *
 * CORRECTED: this said "a non-zero argument turns fog OFF", which had the
 * flag's polarity backwards. ADDR_FOG_OF_WAR is ZERO when fog is on -- see the
 * note there, where the cheat toggle settles it -- so a non-zero argument
 * turns fog ON and returns at once.
 *
 * The work is therefore done when fog is turned OFF, and what it does is
 * reveal every type 2/3/8 object, which is exactly what turning fog off should
 * mean. The old reading had it revealing everything while enabling fog, and
 * that was written down as "counter-intuitive" instead of as a warning.
 *
 * The inner body is RevealObj's MINUS ONE LINE, and the missing line is the
 * point: RevealObj also sets OBJ_FLAG_REVEALED, and this does not. So it
 * cannot be written as a call to RevealObj however similar the two look --
 * that would leave every object on the map carrying a revealed flag it never
 * had, and RevealNearby skips an object already carrying it. The original
 * inlines the shared part and drops that one write; reproduced the same way.
 *
 * Everything else matches: guard on CONCEALED, clear it, then walk the rows
 * clearing ROW_FLAG_REMOVED before RowUpdate, which is what re-links each row
 * into the map's cell lists.
 *
 * VERIFIED BY READING, and the wall is a known one. The single call site is
 * WndProc's AM2_WM_SETUP_DONE arm, and that message is posted by the ready /
 * end-setup handshake -- one of the five window messages this project can only
 * reach with a live DirectPlay session and a second player. So it is the same
 * standing as those five, and weaker than the rest of this file.
 *
 * What IS checked is the shape rather than the run: the inner body is
 * RevealObj's, which is exercised, and the one line that differs is called out
 * above precisely because a reader comparing them would otherwise assume they
 * are the same code. */
void __cdecl SetFogOfWar(int32_t fogOn)
{
    void *obj;

    if (fogOn) {
        g_fogOfWar = 0;
        return;
    }
    g_fogOfWar = 1;

    for (obj = FirstItem(); obj; obj = NextItem()) {
        AM2_Object *o = (AM2_Object *)obj;
        uint32_t flags;
        int32_t  i;

        if (o->flags & OBJ_FLAG_DESTROYED)
            continue;
        if (!ObjIsTypeIn238((const AM2_Object *)obj))
            continue;

        flags = o->flags;
        if (!(flags & OBJ_FLAG_CONCEALED))
            continue;
        o->flags = flags & ~(uint32_t)OBJ_FLAG_CONCEALED;

        for (i = 0; i < o->rowSet.count; i++) {
            uint8_t *row = ((uint8_t *)o->rowSet.rows)
                           + (uint32_t)i * AM2_OBJ_ROW_STRIDE;

            *(uint32_t *)row &= ~(uint32_t)ROW_FLAG_REMOVED;
            RowUpdate((AM2_MapRow *)row, 0, g_mapDesc);
        }
    }
}

/* 0x004035F0, one caller, on the per-frame path. Zero two counters -- and
 * both of them are vestigial, which took a whole-image scan to be sure of
 * rather than the usual one below the CRT line.
 *
 * There are exactly THREE references to the pair in the entire image: the two
 * writes here, and a single read of ADDR_PERFRAME_COUNT_A at the top of
 * 0x00403B40, which returns immediately when it is above 10. Nothing
 * increments either one. So the first is always zero and that guard is dead
 * code, and the second is written once a frame and read by nothing at all.
 *
 * This is the third piece of bookkeeping with no consumer on this one path --
 * after ADDR_SECOND_DEADLINE, whose writes have no reader, and ADDR_FIXED_STEP,
 * whose reads have no writer. Whatever this per-frame block once did, a good
 * deal of it was cut and the scaffolding left standing.
 *
 * Reconstructed rather than skipped for the same reason as the others: it is
 * two stores on a path that runs every frame, and leaving them out would be a
 * difference even though making them is not.
 *
 * Measured at 18,069 calls against ComposeFrame's 18,146 -- once a frame, like
 * everything else on this path. Which is worth putting beside the finding: two
 * stores to nothing, eighteen thousand times a mission. */
void __cdecl ClearFrameCounts(void)
{
    g_perframeCountA = 0;
    g_perframeCountB = 0;
}

/* 0x00403AF0, three callers. The object's position, moved by its sprite's
 * second anchor pair -- the one DrawMenuCursor ADDS when it places the cursor
 * and this SUBTRACTS to get back to where the object logically is.
 *
 * Which row supplies the sprite is the odd part and it is reproduced exactly:
 * exactly one row uses row 0, more than one uses row ONE, and no rows at all
 * falls through with the position unadjusted. The null test is applied AFTER
 * the stride is added, so an object claiming two rows with a null array tests
 * 0x60 rather than 0 and passes -- the original's behaviour, kept.
 *
 * Only a null object gets the zero point; every other way out returns the
 * position as it stood. The original writes all of this through its own
 * argument slot, which is why the point and the object share a register in
 * the disassembly; a local is the same thing said once. */
AM2_Point __cdecl ObjAnchorPoint(const AM2_Object *o)
{
    const AM2_MapRow *row;
    const AM2_Sprite *spr;
    AM2_Point         pt;

    if (!o)
        return g_zeroXY;

    pt = o->pos;
    if (o->rowSet.count == 1)
        row = o->rowSet.rows;
    else if (o->rowSet.count > 1)
        row = &o->rowSet.rows[1];
    else
        row = 0;        /* no rows: the point as it is */
    if (row) {
        spr = row->sprite;
        if (spr) {
            pt.x -= spr->attachX;
            pt.y -= spr->attachY;
        }
    }
    return pt;
}



/* FormationPointFar -- original 0x004042A0, one caller, which is
 * FormationPoint below. It is what happens past slot 11, where the twelve-entry
 * ADDR_FORMATION_SLOTS table runs out.
 *
 * The table is replaced by arithmetic, and the arithmetic is a set of RINGS.
 * Sixteen slots to a ring, the facing spread evenly round it -- `slot + 4`
 * taken as a byte and shifted up four, so the low nibble becomes the high one
 * and the ring covers the whole circle in steps of 16 -- and the distance
 * `((slot - 11) / 16 + 4) * 32`, so ring 0 sits at 128 and each ring after it
 * 32 further out. The leader's own facing is added to every slot exactly as
 * the table version adds it, so the rings turn with the leader.
 *
 * The division is C's. The original is the usual `cdq; and edx, 0xF; add;
 * sar 4`, which is truncation TOWARD ZERO for a signed dividend, which is what
 * C's `/` gives -- checked rather than assumed, and it matters for slots below
 * 11, which nothing calls this with but which the arithmetic accepts.
 *
 * EVERYTHING FROM THE TYPE TEST DOWN IS FormationPoint'S TAIL, and the two
 * were transcribed separately rather than shared, because the original does
 * not share them either. The same type 2/3/8 facing rule, the same doubling
 * for a type 3, the same "no standing in front of a vehicle" swing of 0x3D or
 * 0xC3 with 0x20 more distance, the same Cos8/Sin8 step, the same clamp to
 * ADDR_MAP_BOUNDS_*, and the same settle through the region rule.
 *
 * ITS FIRST ARGUMENT IS NEVER READ. FormationPoint hands the follower on and
 * nothing here touches it -- the same shape as 0x00404ED0, which orig.h
 * already records. Kept in the signature because the call site pushes it.
 */
void __cdecl FormationPointFar(AM2_Object *follower, AM2_Object *leader, AM2_Point *out,
                               int32_t slot)
{
    const AM2_Object *l = leader;
    uint8_t        leaderFacing;
    uint8_t        facing;
    int32_t        dist;
    uint32_t       shift;

    (void)follower;

    out->y = l->pos.y;
    out->x = l->pos.x;

    switch (*(const int32_t *)l) {
    case 3:
        leaderFacing = l->facing;
        shift        = 1;
        break;
    case 2:
    case 8:
        leaderFacing = l->facing;
        shift        = 0;
        break;
    default:
        leaderFacing = 0;
        shift        = 0;
        break;
    }

    facing = (uint8_t)((uint8_t)((uint8_t)(slot + 4) << 4) + leaderFacing);
    dist   = (((slot - 11) / 16 + 4) * 32) << shift;

    if (ObjIsType3((const AM2_Object *)leader)) {
        int32_t delta = AngleDelta(facing, leaderFacing);

        if (abs(delta) < 0x40) {
            if (delta < 0)
                facing += 0xC3;
            else
                facing += 0x3D;
            dist += 0x20;
        }
    }

    if (dist > 0) {
        float d = (float)dist;

        out->x = (int16_t)(int32_t)(Cos8(facing) * d + out->x);
        out->y = (int16_t)(int32_t)(Sin8(facing) * d + out->y);

        out->x = (int16_t)Clamp(out->x, g_mapBoundsLeft, g_mapBoundsRight);
        out->y = (int16_t)Clamp(out->y, g_mapBoundsTop, g_mapBoundsBottom);
    }

    SettlePointInRegion(TileOfPoint(*out), out);
}

/* 0x00404400, two callers. Put `out` where the follower in formation `slot`
 * belongs, relative to `leader`. `follower` itself is only null-checked and
 * handed on -- nothing below slot 12 reads it.
 *
 * The slot table is twelve {facing, distance} pairs; see ADDR_FORMATION_SLOTS.
 * The slot's facing is added to the LEADER's, so the formation turns with it,
 * and the distance is doubled for a type 3 -- a vehicle keeps its followers
 * twice as far out. A leader that is not a type 2, 3 or 8 contributes a facing
 * of zero, so the formation is laid out in absolute map directions.
 *
 * The type 3 case has one more rule and it is the interesting one. The angle
 * between the slot and the leader's own heading is just the slot's facing
 * negated -- ADDR_ANGLE_DELTA masks both arguments, which is what makes the
 * original's dirty dword there harmless -- and when that is inside a quarter
 * turn, the slot is in FRONT of the leader. Such a follower is swung 0x3D or
 * 0xC3 to one side, by the sign of the delta, and pushed 0x20 further out: no
 * standing in front of a moving vehicle.
 *
 * Arithmetic note. The original keeps cos/sin, the distance and the existing
 * coordinate in x87 registers and truncates once, through _ftol. Done here in
 * double, which is exact for these values -- the product of a 24-bit float and
 * a distance under 200 needs 32 bits, and the coordinate added to it 17, well
 * inside 53 -- and a C cast to an integer truncates toward zero exactly as
 * _ftol does. Both were checked rather than assumed. */
void __cdecl FormationPoint(AM2_Object *follower, AM2_Object *leader, AM2_Point *out,
                            int32_t slot)
{
#define l (leader)
    uint8_t        leaderFacing;
    uint8_t        facing;
    int32_t        dist;
    uint32_t       shift;

    if (!follower || !leader)
        return;

    if (slot >= AM2_FORMATION_SLOTS) {
        FormationPointFar(follower, leader, out, slot);
        return;
    }

    out->x = l->pos.x;
    out->y = l->pos.y;

    switch (*(const int32_t *)l) {
    case 3:
        leaderFacing = l->facing;
        shift        = 1;
        break;
    case 2:
    case 8:
        leaderFacing = l->facing;
        shift        = 0;
        break;
    default:
        leaderFacing = 0;
        shift        = 0;
        break;
    }

    /* Facing first, then distance, each indexing the table afresh. */
    facing = (uint8_t)(g_formationSlots[slot].facing + leaderFacing);
    dist   = (int32_t)g_formationSlots[slot].dist << shift;

    if (ObjIsType3((const AM2_Object *)leader)) {
        int32_t delta = AngleDelta(facing, leaderFacing);

        if (abs(delta) < 0x40) {
            if (delta < 0)
                facing += 0xC3;
            else
                facing += 0x3D;
            dist += 0x20;
        }
    }

    if (dist > 0) {
        float d = (float)dist;

        out->x = (int16_t)(int32_t)(Cos8(facing) * d + out->x);
        out->y = (int16_t)(int32_t)(Sin8(facing) * d + out->y);


        out->x = (int16_t)Clamp(out->x, g_mapBoundsLeft, g_mapBoundsRight);
        out->y = (int16_t)Clamp(out->y, g_mapBoundsTop, g_mapBoundsBottom);
    }

    SettlePointInRegion(TileOfPoint(*out), out);
#undef l
}

typedef int32_t (__cdecl *AM2_AirRandFn)(void);
#define orig_air_rand ((AM2_AirRandFn)((void *)(uintptr_t)(ADDR_GAME_RAND)))

/* RandomPointToward -- original 0x00404E50, two callers, and the same
 * function as
 * RandomPointAhead below with one substitution: the heading comes from where
 * ANOTHER object is rather than from the way this one faces.
 *
 * AngleBetween the two positions, add the same `(rand & 0x3F) - 32` spread,
 * and step `dist` from the moving object's position along it. So the two are
 * "wander roughly ahead" and "move roughly toward", and they differ in one
 * line.
 *
 * THE ANGLE IS TAKEN FROM obj TO target AND THE STEP IS FROM obj, which is the
 * only ordering that makes the result approach the target. The original passes
 * `&obj->pos` first and `&target->pos` second, which is worth stating because
 * getting AngleBetween's arguments the wrong way round gives a heading 128 out
 * and a unit that runs away -- and both arguments are the same type, so
 * nothing would catch it.
 *
 * Same two quirks as its sibling: the heading reaches Cos8 and Sin8 as a dword
 * with three uninitialised bytes above it, which they mask off; and the rand
 * is the image's own LCG, which it must be.
 */
void __cdecl RandomPointToward(const AM2_Object *target, const AM2_Object *obj,
                               int32_t dist, AM2_Point *out)
{
    const AM2_Object *o = obj;
    const AM2_Object *t = target;
    uint8_t        heading;
    float          d;

    heading = (uint8_t)(AngleBetween(&o->pos,
                                     &t->pos)
                        + (orig_air_rand() & 0x3F) - 0x20);
    d = (float)dist;

    /* Single precision throughout, as RandomPointAhead. */
    out->x = (int16_t)(Cos8(heading) * d + o->pos.x);
    out->y = (int16_t)(Sin8(heading) * d + o->pos.y);
}

/* RandomPointAhead -- original 0x00404ED0, two callers, and a sibling of the
 * formation-point pair above.
 *
 * Pick a point `dist` away from an object, on a heading within +/-32 of the
 * way it is facing -- a quarter-turn's spread, since a heading here is 0..255.
 * Both callers use it as a wander target, re-picked every five seconds.
 *
 * ITS FIRST ARGUMENT IS NEVER READ. Both call sites push four and the body
 * uses the last three; the first is the object doing the wandering, which the
 * caller then writes the result into itself. Reproduced as an unnamed
 * parameter rather than dropped, because the calling convention is cdecl and
 * the sites are the original's.
 *
 * THE HEADING IS PASSED AS A DWORD WITH THREE UNINITIALISED BYTES. The
 * original computes the sum in `al`, stores that one byte into a stack local,
 * and then loads the whole dword back to push it. Cos8 and Sin8 mask their
 * index, so the rubbish above the low byte cannot reach the table -- which is
 * why nothing has ever gone wrong with it. Written here as the uint8_t the
 * arithmetic is actually done in.
 *
 * The rand is the image's own LCG and must be: libc's would give a different
 * sequence and every wandering unit would go somewhere else.
 */
void __cdecl RandomPointAhead(void *, const AM2_Object *obj, int32_t dist,
                              AM2_Point *out)
{
    const AM2_Object *o = obj;
    uint8_t        facing;
    float          d;

    facing = (uint8_t)((orig_air_rand() & 0x3F)
                       + o->facing - 0x20);
    d = (float)dist;

    /* Single precision throughout: the image adds the position with fiadd. */
    out->x = (int16_t)(Cos8(facing) * d + o->pos.x);
    out->y = (int16_t)(Sin8(facing) * d + o->pos.y);
}

/* 0x00404580, three callers. Place a follower in formation on its leader,
 * except that a leader who is RIDING something is not the thing to follow --
 * the vehicle is.
 *
 * So a type 2 leader with a non-zero OBJ_OFF_RIDING is looked up, and on
 * success the follower's OBJ_OFF_FOLLOW_UID is repointed at the vehicle and
 * the vehicle becomes the leader for the placement below. Every other case
 * falls through with the leader unchanged, including a riding uid that no
 * longer resolves -- the stale uid is left alone rather than cleared, which
 * 0x00404730 is the function that eventually clears.
 *
 * The type test is only ObjIsType2. A type 3 leader is never redirected, which
 * is consistent: a vehicle does not ride anything.
 *
 * The placement itself is FormationPoint, called directly now that it is
 * reconstructed -- so this function's counter moves and FormationPoint's does
 * not move from here. Its other caller, 0x0043E0EF, is still the original's
 * and can. */
void __cdecl ResolveFormationPoint(AM2_Object *follower, AM2_Object *leader,
                                   AM2_Point *out)
{
    AM2_Object *f = follower;

    if (!follower || !leader)
        return;

    if (ObjIsType2((const AM2_Object *)leader)) {
        uint32_t riding =
            leader->riding;

        if (riding) {
            AM2_Object *veh = LookupByUID(riding);

            if (veh) {
                f->followUid = veh->uid;
                leader = veh;
            }
        }
    }

    FormationPoint(follower, leader, out,
                         f->formationSlot);
}

/* Spelled exactly as event.cpp spells it, AM2_IMAGE and all, so the two
 * stay one definition. */

void __cdecl RevealNearby(AM2_Point where, int32_t radius, int32_t delayMs)
{
    AM2_Object *o;

    for (o = (AM2_Object *)FirstItem(); o; o = (AM2_Object *)NextItem()) {
        /* Types 2, 3 and 8 only; not one that is already revealed; and
         * ApproxDist -- a diamond, not a circle -- within the radius. */
        if (!ObjIsTypeIn238(o))
            continue;
        if (o->flags & OBJ_FLAG_REVEALED)
            continue;
        if (ApproxDist(&where, &o->pos)
                > radius)
            continue;

        RevealObj(o);
        o->revealedUntil =
            (int32_t)g_gameClockMs + delayMs;
    }
}

/* ReleaseSprite is reconstructed, in win32/sprite.cpp. Declared here rather
 * than by including that header because this module is flat and AM2_Sprite has
 * an LPDIRECTDRAWSURFACE in it -- the same reason script.cpp declares
 * PreloadSprite. An incomplete type is all this needs. */
struct AM2_Sprite;
void __cdecl ReleaseSprite(AM2_Sprite *spr);   /* C++ linkage, as sprite.h declares it */

/* DrawSprite is reconstructed too, and declared here for the same reason.
 * Its four arguments name no Win32 type once AM2_Sprite is opaque. */
void __cdecl DrawSprite(AM2_Sprite *spr, int32_t x, int32_t y,
                                   int32_t mode);


void __cdecl RemapSpriteRuns(void *img, int32_t unused, const uint8_t *table,
                             int32_t from)
{
    uint8_t        scratch;
    uint16_t       width;
    uint16_t       height;
    int32_t        rows;
    int32_t        cols;
    uint8_t       *p;

    (void)unused;   /* pushed by the call site, read by nothing here. */

    /* Each run-header byte goes through `scratch`, which VC6 keeps in a
     * stack slot, and is widened from there. */

    if (!table)
        return;

    p      = (uint8_t *)img;
    height = *(const uint16_t *)(p + 2);
    width  = *(const uint16_t *)(p + 0);

    /* Four bytes of header and then one uint16 per row. */
    p += 4 + height * 2;

    /* The HEIGHT is what is tested, not the width -- through a temporary,
     * so the row counter's store to its stack slot comes after the test. */
    {
        int32_t h = height;

        if (h <= 0)
            return;
        cols = width;
        rows = h;
    }
    do {
        int32_t covered = 0;

        /* The width is tested once, before the row rather than inside it --
         * it cannot change, and the original checks it exactly here. */
        if (cols > 0) {
            do {
                int32_t run;

                scratch = *p++;
                covered += scratch;
                scratch = *p++;
                run = scratch;
                covered += run;

                for (; run > 0; run--, p++) {
                    int32_t v = *p;

                    /* Below `from` is a reserved index and stays put. */
                    if (v >= from)
                        *p = table[v];
                }
            } while (covered < cols);
        }
    } while (--rows);
}

void __cdecl FreeSpriteList(void)
{
    int32_t i;

    if ((*(AM2_Sprite ** *)&g_spriteList)) {
        /* The count is re-read every iteration, not held. */
        for (i = 0; i < g_spriteListN; i++)
            ReleaseSprite((*(AM2_Sprite ** *)&g_spriteList)[i]);

        am2_free((*(AM2_Sprite ** *)&g_spriteList));
        (*(AM2_Sprite ** *)&g_spriteList)    = (AM2_Sprite **)0;
        g_spriteListN   = 0;
        g_spriteListCap = 0;
    } else {
        /* No array: the count and the capacity are cleared anyway. */
        g_spriteListN   = 0;
        g_spriteListCap = 0;
    }
}

void __cdecl GrowSpriteList(void)
{
    /* A hundred more, and the COUNT is not consulted -- this is "make room",
     * not "grow if full". The realloc is not checked. */
    g_spriteListCap += AM2_SPRITE_LIST_GROW;
    (*(AM2_Sprite ** *)&g_spriteList) = (AM2_Sprite **)am2_realloc((*(AM2_Sprite ** *)&g_spriteList),
                                              (size_t)g_spriteListCap * 4u);
}

int32_t __cdecl DoAirSupport(int32_t kind, AM2_Point where, uint32_t from)
{
    int32_t extra = 0;

    /* Kind 2 is taken as given; anything else is promoted to 3 by an enemy. */
    if (kind != 2) {
        extra = (int32_t)FindEnemyNear(where, from);
        if (extra)
            kind = 3;
    }

    if (g_airCount >= AM2_AIR_MAX)
        return 0;

    orig_log("DoAirSupport paratroopers where: %d, from %d, army %d, "
             "count: %d\n",
             where, from, UidArmy(from), g_airCount);

    /* One dword, where AirSupportPop moves the same field as two words. */
    g_airWhere[g_airCount] = where;
    g_airKind[g_airCount]  = kind;
    g_airFrom[g_airCount]  = from;
    g_airExtra[g_airCount] = extra;

    /* Called with the entry written and the count still zero, so Begin reads a
     * slot the count says is empty. Harmless -- Begin only looks at slot 0 --
     * and it is the original's order. */
    if (!g_airCount)
        AirSupportBegin();

    g_airCount += 1;
    orig_log("EndMission  AirSupport.count increasing to: %d\n", g_airCount);
    return 1;
}

void __cdecl AirSupportBegin(void)
{
    /* The head entry's `extra` decides which of the two shapes runs, and the
     * two do NOT agree about the active flag: only the first sets it. */
    if (!g_airExtra[0]) {
        PlaySoundAt(AM2_AIR_SOUND, 0, 0, 0, 0);
        g_airActive = 1;
        g_airFlagA  = 0;
        g_airFlagB  = 0;
        return;
    }

    g_airFlagA = 1;
    g_airFlagB = 1;
}

void __cdecl AirSupportClear(void)
{
    g_airActive = 0;
    g_airFlagA  = 0;
    g_airFlagB  = 0;
}

void __cdecl AirSupportPop(void)
{
    int32_t i;

    /* Shift all four arrays down one. The point is copied as its two 16-bit
     * halves, which is how the packing shows through. */
    for (i = 1; i < g_airCount; i++) {
        g_airKind[i - 1]      = g_airKind[i];
        g_airWhere[i - 1].x         = g_airWhere[i].x;
        g_airWhere[i - 1].y         = g_airWhere[i].y;
        g_airFrom[i - 1]      = g_airFrom[i];
        g_airExtra[i - 1]     = g_airExtra[i];
    }

    g_airCount -= 1;
    /* The count is written BEFORE the log, and the log is not gated on
     * anything. "EndMission" here is a prefix, not this function's name. */
    orig_log("EndMission  AirSupport.count decreasing to: %d\n", g_airCount);
    g_airPending = 0;

    /* Tail calls in the original, both of them. */
    if (g_airCount)
        AirSupportBegin();
    else
        AirSupportClear();
}

/* 0x004064E0, four callers -- and the four are what name it. Each is a
 * one-line wrapper passing an army 0..3 and one of "gflagbase", "tflagbase",
 * "bflagbase" and "grflagbase", so this is the capture-the-flag proximity
 * test: is `who` standing at the named army's flag base.
 *
 * IT ANSWERS 0 FOR YES. AM2_NOT_AT_FLAG_BASE is the failure code, and it is
 * shared with the three functions immediately above it, which answer 0x1E,
 * 0x60 and 0x80 -- so these are CODES rather than booleans, and a
 * reconstruction that returned 1 and 0 would be wrong in a way no `if` on the
 * result would show.
 *
 * Two conditions and both are required: `owner`'s army has to map through
 * CommArmyOfSlot to the army asked for, and `who` has to be within
 * AM2_FLAG_BASE_RANGE of whatever the script name resolves to. The name goes
 * through ScriptFindName and the entry's VALUE is a uid, which is the same
 * two-step the script layer uses everywhere.
 *
 * A name that is not in the table resolves to entry 0 rather than failing --
 * ScriptFindName answers 0 for "not found" and index 0 is a real entry -- so a
 * misspelled flag base measures the distance to whatever entry 0 holds. The
 * four callers all pass literals, which is what makes that harmless here.
 *
 * The distance is ApproxDist's diamond metric, not a circle. */
int32_t __cdecl AtFlagBase(const AM2_Object *who, const AM2_Object *owner, int32_t army,
                           const char *name)
{
    const AM2_Object *w = (const AM2_Object *)who;
    const AM2_Object *o = (const AM2_Object *)owner;
    const AM2_Object *base;
    int32_t        answer = AM2_NOT_AT_FLAG_BASE;

    if (AM2_THE_COMM->CommArmyOfSlot(o->owner) == army) {
        base = LookupByUID(
                   (uint32_t)kScriptNames[ScriptFindName(name)].value);
        if (base
            && ApproxDist(&base->pos,
                          &w->pos)
               < AM2_FLAG_BASE_RANGE)
            answer = 0;
    }

    return answer;
}

/* 0x00448E60, three callers, 160 bytes. How much one object obstructs.
 *
 * Nothing in the body says "obstruct"; the callers do, and they say it three
 * ways. All three walk the chain of objects standing at a map point,
 * accumulate this per object, and stop the walk once the total reaches 15.
 * 0x0043CF70 then keeps going on the terrain: it adds 15 for a tile whose flag
 * byte has 0x80 set, and 15 again when two tile heights differ by more than
 * 16 -- the same 15 and the same 16 this function uses on objects. A function
 * and its caller agreeing on two constants is better evidence for the reading
 * than either alone, and it is the whole of the evidence here.
 *
 * The five exits, in the order the original takes them:
 *
 *   - an object never obstructs ITSELF, tested by pointer;
 *   - nor does one more than 16 above or below the viewer, which is the
 *     height test, and note it is skipped entirely when there is no viewer;
 *   - an ITEM contributes its own OBJ_OFF_RANK byte, read SIGNED, so an item
 *     can carry any weight the data gives it including none;
 *   - with no viewer, or for anything that is not type 3 or type 8, the
 *     contribution is total;
 *   - and a type 3 or 8 obstructs only when the reference point is CLOSER to
 *     it than the viewer is.
 *
 * That last comparison is the one worth spelling out, because both distances
 * are measured to the obstacle and it is easy to read them as being between
 * viewer and point. ApproxDist(at, obj) < ApproxDist(from, obj) blocks.
 *
 * THE THIRD ARGUMENT IS NEVER READ. All three callers push four dwords, so
 * four is the signature; the original simply does not use one of them. It is
 * named `unused` rather than dropped, because dropping it would silently
 * change the stack layout of the fourth, which IS read -- by address, since
 * ApproxDist takes a pointer and the point arrives by value. It is spelled
 * uint32_t rather than AM2_Point for the same reason HeightAtPoint and
 * TileOfPoint are -- a packed point is what the callers hold.
 *
 * The height field is declared uint8_t in orig.h and read here with `movsx`,
 * so the difference is a signed one. Transcribed as the original spells it
 * rather than as the declaration suggests.
 *
 * MEASURED, and it reads 0. All three callers are the original's and reach
 * this by address, so the counter is not blind -- a Boot Camp mission driven
 * standing still and then with four rounds of walking and firing leaves it at
 * 0 while HeldWeaponCode, patched in the same batch, climbs to 12,293 on the
 * same run. So the callers themselves are not reached, and this is verified by
 * reading and by nothing else. Said as a measurement rather than as the guess
 * it would otherwise have been; the sibling commit got exactly that guess
 * wrong in the other direction.
 */
int32_t __cdecl ObjBlockWeight(AM2_Object *from, AM2_Object *obj, AM2_Point unused,
                               AM2_Point at)
{
    const AM2_Object *o = obj;
    const AM2_Object *f = from;

    (void)unused;

    if (o == f)
        return 0;

    if (f
        && abs((int32_t)o->heightSet
               - (int32_t)f->heightSet)
           > AM2_BLOCK_HEIGHT_STEP)
        return 0;

    if (ObjIsItem((const AM2_Object *)o))
        return o->itemFlag;

    /* The distance test only for a 3 or an 8, and only with a mover; every
     * other way out is the full weight, from one shared tail. */
    if (f && (ObjIsType3((const AM2_Object *)o)
              || ObjIsType8((const AM2_Object *)o))) {
        int32_t d = ApproxDist(&f->pos,
                               &o->pos);

        if (ApproxDist(&at,
                       &o->pos) >= d)
            return 0;
    }

    return AM2_BLOCK_FULL;
}

/* 0x00406550, two callers. Turn a thing's own code -- the dword its
 * OBJ_OFF_FIELD_C0 pointer points at -- into one of about a dozen result
 * codes.
 *
 * The original does it with a 39-entry byte table indexed by `code - 2` and a
 * 17-arm jump table; written out as a switch on the CODE itself, so the
 * numbers in the source are the ones a script or a data file would carry
 * rather than an offset into a table nobody can see. The mapping is
 * transcribed from those two tables and nothing else.
 *
 * MOST ARMS ARE A PROPERTY OF THE THING AND A FEW ARE THE OWNER'S. Thirteen
 * return a constant; four are the flag-base tests, one per army colour; and
 * one compares `owner`'s health against half its maximum. So the same code can
 * answer differently for the same thing depending on who holds it, which is
 * not something the shape of a lookup table suggests.
 *
 * TWO ARMS ANSWER ZERO BY DIFFERENT ROUTES -- one an explicit zero, the other
 * the out-of-range default falling out of the entry `xor`. Kept as two cases,
 * because the table distinguishes them even though the answer does not.
 *
 * The health test is `health >= max / 2`. The original spells that division as
 * `cdq; sub; sar 1` -- MSVC's signed divide-by-two, which rounds towards zero
 * -- so plain C `/ 2` is the same function and not an approximation of it.
 * Writing the correction out by hand as well, which was the first attempt,
 * applies it twice and is wrong for a negative maximum. No object ships with
 * one, which is exactly why that would have gone unnoticed.
 */
int32_t __cdecl ThingCode(const AM2_Object *who, const AM2_Object *owner)
{
    const AM2_Object *w = (const AM2_Object *)who;
    const AM2_Object *o = (const AM2_Object *)owner;
    int32_t        result = 0;

    /* Cases in the image's layout order; the answer for an unlisted code is
     * the zero preset before the switch. */
    switch (w->typeRec->kind) {
    case 2:               result = 8; break;
    case 3: case 4: case 5:  result = 0x10; break;
    case 6: case 7:       result = 0x20; break;
    case 21:              result = 0x21; break;
    case 29:              result = 0x40; break;
    case 22: {
        int32_t max    = o->maxHealth;
        int32_t health = o->health;

        result = health < max / 2 ? 0x20 : 8;

        break;
    }
    case 23:              result = 0; break;  /* explicit, though the preset is 0 too */
    case 8: case 25: case 30: result = 0x30; break;
    case 26:              result = 1; break;
    case 10: case 28:     result = 0x1E; break;
    case 24: case 39: case 40: result = 0x60; break;
    case 15:              result = AM2_NOT_AT_FLAG_BASE; break;
    case 16: result = AtFlagBase(w, o, 0,
                               "gflagbase"); break;
    case 17: result = AtFlagBase(w, o, 1,
                               "tflagbase"); break;
    case 18: result = AtFlagBase(w, o, 2,
                               "bflagbase"); break;
    case 19: result = AtFlagBase(w, o, 3,
                               "grflagbase"); break;
    }
    return result;
}

/* 0x0041A1B0, two callers -- both arms of the cheat table at 0x00417B80.
 * Invert the fog flag and bring every enemy object into line with it.
 *
 * THE FLAG IS WRITTEN TWICE PER CHEAT. The arm stores what it wants, this
 * opens by storing the COMPLEMENT, and only then does the sweep read it. So
 * "I see everything!" stores 0, the flag becomes 1, and the sweep reveals;
 * "I bury my head 'neath the sand." stores 1, the flag becomes 0, and the
 * sweep conceals. Reading either write alone gives the opposite polarity to
 * the other, which is how orig.h came to carry both answers at once.
 *
 * THE CONCEAL ARM OVERRIDES A LIVE REVEAL WINDOW. It conceals when
 * OBJ_OFF_REVEALED_UNTIL is 0 or the clock has NOT reached it, and SKIPS when
 * the clock has -- `jae` on `cmp clock, stamp`, jumping to the loop tail. The
 * objects it leaves alone are the ones whose window has already expired, which
 * the ordinary sweep conceals anyway; the ones it acts on include those a
 * RevealNearby has just lit up. That is the opposite of what this tree said
 * and it is read off the branch rather than off the shape.
 *
 * Three filters before either arm, and all three are needed: not already
 * destroyed, one of types 2, 3 and 8, and NOT friendly -- so the cheat moves
 * the enemy only, and your own units are visible either way.
 */
void __cdecl ToggleFogOfWar(void)
{
    void *o;

    int32_t *fog = &g_fogOfWar;

    *fog = (*fog == 0);

    for (o = FirstItem(); o; o = NextItem()) {
        AM2_Object *p = (AM2_Object *)o;

        if (p->flagsLo & OBJ_FLAG_DESTROYED)
            continue;
        if (!ObjIsTypeIn238(p))
            continue;
        if (ObjIsFriendly(p))
            continue;

        if (*fog) {
            RevealObj(p);
        } else {
            uint32_t until = p->revealedUntil;

            /* `<= 0` on an unsigned, i.e. == 0: the original's `jbe` after
             * `test`, where `== 0` compiles `je`. */
            if (until <= 0u
                || g_gameClockMs < until)
                ObjConceal(p, 0);
        }
    }
}

/* The pass queue: eight slots inside the same block the savegame section is,
 * so they are fields rather than globals of their own. */
#define g_airPassCount (kAirSaveBlock->passCount)
#define g_airPassTimer (kAirSaveBlock->passTimer)
#define g_airPassWhere (kAirSaveBlock->passWhere)
#define g_airPassArmy  (kAirSaveBlock->passArmy)

/* The strike itself, still original -- what a pass does when its timer runs
 * out. Its second argument is an army: it compares it against
 * ADDR_DEFAULT_OWNER, which is the third reading agreeing on that field. */
typedef void (__cdecl *AM2_AirStrikeFn)(AM2_Point at, int32_t army);

/* The ten-argument creator is CreateExplosion and is reconstructed in
 * item.cpp, so the two call sites below name it. */
#define orig_spawn_at CreateExplosion

/* AirDeliver -- original 0x004093D0, one caller. What the head of the air
 * support queue actually DOES, dispatched on its AIR_OFF_KIND: kind 1 pushes a
 * pass onto the sub-queue AirPassesDraw then flies, kind 0 drops a strike
 * immediately, and any other kind does nothing at all.
 *
 * THE STRIKE FALLS ALONG A LINE, NOT IN A DISC, and the slope is what says so:
 * each of the twelve blasts takes a random X offset in a 320-unit band and
 * then SUBTRACTS 0.43 of it from its own random Y. So the scatter is a
 * strafing run across the point rather than a circle around it, which is the
 * one thing about this function that could not be read off the constants
 * alone.
 *
 * The delay carries the same idea: it is the random jitter plus three times
 * the X offset, so blasts further along the run land later.
 *
 * THIRTEEN BLASTS, NOT TWELVE. The loop runs AM2_AIR_STRIKE_BLASTS times and
 * then one more goes in at the centre, undisplaced, with a fixed delay --
 * written out below the loop in the original and reproduced that way, since
 * folding it in would need a special case for the offsets.
 *
 * FOUR CALLS TO THE GAME'S rand PER BLAST, in a fixed order, and the order is
 * load-bearing rather than incidental: everything else in the process draws
 * from the same LCG, so a reconstruction that computed the same numbers in a
 * different sequence would leave the generator in a different place. The
 * centre blast draws once more.
 *
 * The pass push writes a timer of 1 rather than 0, which is what
 * AIR_OFF_PASS_TIMER's note means by "starts at 1", and takes the army through
 * UidArmy from the uid that asked -- the writer that named
 * AIR_OFF_PASS_ARMY.
 */
void __cdecl AirDeliver(void)
{
    int32_t i;

    switch (g_airKind[0]) {
    case 1: {
        int32_t n = g_airPassCount;

        if (n >= AM2_AIR_PASS_SLOTS)
            return;

        g_airPassWhere[n] = g_airWhere[0];
        g_airPassTimer[g_airPassCount] = 1;
        g_airPassArmy[g_airPassCount] =
            (int16_t)UidArmy(g_airFrom[0]);
        ++g_airPassCount;
        break;
    }

    case 0:
        for (i = 0; i < AM2_AIR_STRIKE_BLASTS; i++) {
            int32_t dx = orig_air_rand() % AM2_AIR_STRIKE_SPREAD
                         - AM2_AIR_STRIKE_HALF;
            int32_t dy = orig_air_rand() % AM2_AIR_STRIKE_HALF
                         - (int32_t)((double)dx
                                     * g_airStrikeSlope)
                         - AM2_AIR_STRIKE_Y_BIAS;
            int32_t delay = orig_air_rand() % AM2_AIR_STRIKE_JITTER
                            + dx * AM2_AIR_STRIKE_SLIDE + AM2_AIR_STRIKE_BASE_MS;

            orig_spawn_at(g_airWhere[0].x + dx,
                          g_airWhere[0].y + dy,
                          (g_airStrikeKinds)
                              [(uint32_t)orig_air_rand() % AM2_AIR_STRIKE_KINDS],
                          (int32_t)UidArmy(g_airFrom[0]), g_airFrom[0],
                          AM2_AIR_STRIKE_EXTRA, delay, 0, 0, 0);
        }

        orig_spawn_at(g_airWhere[0].x, g_airWhere[0].y,
                      (g_airStrikeKinds)
                          [(uint32_t)orig_air_rand() % AM2_AIR_STRIKE_KINDS],
                      (int32_t)UidArmy(g_airFrom[0]), g_airFrom[0],
                      AM2_AIR_STRIKE_EXTRA, AM2_AIR_STRIKE_JITTER, 0, 0, 0);
        break;
    }
}

/* AirPassesDraw -- original 0x00408E50, one caller: AirFrameDraw, first thing.
 * Draw every pass in the sub-queue and retire the head one when its time is
 * up.
 *
 * THE TWENTY SPRITES RUN TWICE, once in each half of ADDR_AIR_PASS_MS, and the
 * first half is drawn a further AM2_AIR_PASS_LIFT higher. So a pass is an
 * approach and a departure over the same twenty frames, and the lift is what
 * makes the two halves look different. The original spells that as one
 * `sub ecx, 0x72` before the branch and a second inside the near arm.
 *
 * IT NAMED TWO FIELDS THAT HAD BEEN NAMED FROM THE PUSH. +0x1F0 was
 * AIR_OFF_PASS_LIVE, "written 1"; this adds the frame delta to it every frame
 * and retires the pass when it reaches ADDR_AIR_PASS_MS, so it is a timer that
 * happens to start at 1. +0x230 was AIR_OFF_PASS_TAG, "from 0x0042A7A0" --
 * which is UidArmy, so the writer had already said what it was. This hands it
 * to CommMustBroadcast, whose parameter is an army.
 *
 * THE MULTIPLAYER GATE IS ONE-SIDED. With no session the strike always lands;
 * with one it lands only when CommMustBroadcast says this army is ours to
 * report. Either way the pass is popped, so a pass belonging to someone else
 * is drawn for its full three seconds and then quietly discarded.
 *
 * The original tests the count TWICE on entry, the second test unreachable
 * after the first. Written once.
 */
void __cdecl AirPassesDraw(void)
{
    int32_t i;
    int32_t x;
    int32_t y;
    int32_t frame;

    /* The pass time is re-read, and halved with a shift, at every use. */
#define MS (g_airPassMs)
    if (g_airPassCount <= 0)
        return;

    for (i = 0; i < g_airPassCount; i++) {
        x = g_airPassWhere[i].x
            - g_viewX;
        y = g_airPassWhere[i].y
            - g_viewY
            - AM2_AIR_PASS_LIFT;

        if (g_airPassTimer[i] < (MS >> 1)) {
            frame = (g_airPassTimer[i] * AM2_AIR_PASS_FRAMES) / (MS >> 1);
            y -= AM2_AIR_PASS_LIFT;
        } else {
            frame = ((g_airPassTimer[i] - (MS >> 1)) * AM2_AIR_PASS_FRAMES) / (MS >> 1);
        }

        DrawSprite((g_airSprites3)[frame],
                   x, y, 0);

        g_airPassTimer[i] += g_frameDeltaMs;
    }

    if (g_airPassTimer[0] < MS)
        return;
#undef MS

    if (!g_mpSession
        || AM2_THE_COMM->CommMustBroadcast(
                             g_airPassArmy[0]))
        AirPassStrike(g_airPassWhere[0],
                             g_airPassArmy[0]);

    for (i = 1; i < g_airPassCount; i++) {
        g_airPassTimer[i - 1]        = g_airPassTimer[i];
        g_airPassWhere[i - 1].x      = g_airPassWhere[i].x;
        g_airPassWhere[i - 1].y      = g_airPassWhere[i].y;
        g_airPassArmy[i - 1]         = g_airPassArmy[i];
    }
    --g_airPassCount;
}


/* 0x00456EA0, three callers. Where a formation slot sits.
 *
 * It CACHES its own answer -- the tail writes ADDR_SLOT_POSITIONS and
 * ADDR_SLOT_HEADINGS for this slot before returning -- so the three per-slot
 * tables are its output as well as its input. Slots resolve in order, each
 * reading its parent's cached entry, which is why the vehicle rule below
 * propagates DOWN the formation tree instead of being a local test on two
 * objects. Taking this for a pure "where does slot N go" query and dropping
 * the writes would leave every later slot reading a stale parent.
 *
 * Three regimes, and the tables settle which: slot 0 is the leader itself;
 * slots 1..8 read a parent, an angle and a distance out of ADDR_SLOT_RECS; and
 * slots 9 and up are procedural rings, ADDR_SLOT_BAND_HEADING splitting the
 * slot into a band and an index with the distance
 * band * AM2_SLOT_RING_STEP + AM2_SLOT_RING_BASE.
 *
 * In all three the distance DOUBLES when the slot or its parent holds a
 * vehicle. Note it is an OR over the two, not a test on the object passed in.
 *
 * Both early exits sit before the original's `push esi`, which is why they pop
 * one register fewer -- the conditional-push shape, the same one that gives
 * StepType6 two returns at different depths. */
void __cdecl FormationSlotPoint(int32_t slot, AM2_Point leaderPos, AM2_Object *obj,
                                AM2_Point *out)
{
    int32_t  *isVehicle = g_slotIsVehicle;
    AM2_Point *slotPos  = g_slotPositions;
    uint8_t  *headings  = g_slotHeadings;
    const AM2_Object *o = obj;
    uint8_t   heading;
    int32_t   parent;
    int16_t   dist;
    float     fdist;

    if (slot >= AM2_SLOT_MAX || obj == 0)
        return;

    isVehicle[slot] = ObjIsType3Inline(o);

    if (slot == 0) {
        *out = leaderPos;
        heading = AngleBetween(&o->pos,
                               &leaderPos);
    } else if (slot < 9) {
        const AM2_SlotRec *rec = &g_slotRecs[slot];

        /* Each arm places the point itself, through `out`: fmul, then fiadd,
         * then ftol -- the parent's coordinate is added BEFORE the
         * truncation. The distance is rounded to float once on the way in
         * (fild then fstp dword). */
        parent = rec->parent;
        *out = slotPos[parent];
        dist = rec->dist;
        if (isVehicle[parent] || isVehicle[slot])
            dist += dist;
        /* The angle is indexed off the table afresh, not through `rec`. */
        heading = (uint8_t)(g_slotRecs[slot].angle
                            + headings[parent]);
        fdist = (float)dist;
        out->x =
            (int16_t)(int32_t)(Cos8(heading) * fdist + (float)out->x);
        out->y =
            (int16_t)(int32_t)(Sin8(heading) * fdist + (float)out->y);
    } else {
        int32_t band;

        /* The ring's index comes back into `parent` itself -- one
         * address-taken variable for both arms. */
        SlotBandHeading(slot, &band, &parent, &heading);
        dist = (int16_t)(band * AM2_SLOT_RING_STEP + AM2_SLOT_RING_BASE);
        if (isVehicle[parent] || isVehicle[slot])
            dist += dist;
        *out = slotPos[parent];
        fdist = (float)dist;
        out->x =
            (int16_t)(int32_t)(Cos8(heading) * fdist + (float)out->x);
        out->y =
            (int16_t)(int32_t)(Sin8(heading) * fdist + (float)out->y);
    }

    slotPos[slot]  = *out;
    headings[slot] = heading;
}

/* AddSightBlocker -- original 0x004036F0, 1,024 bytes, one caller. What one
 * object does to another's view: take the blocker's silhouette as seen from
 * the viewer, and for every heading it subtends record how far the view is
 * obstructed, in three height bands.
 *
 * ITS OUTPUT IS A PER-DIRECTION BUFFER, not a return value -- the epilogue
 * sets no eax at all. ADDR_SIGHT_BLOCK_BY_DIR is sixty-four sixteen-byte
 * records, one per heading rounded down to a multiple of four, each holding
 * three int16 distances and a generation stamp. A record whose stamp is not
 * ADDR_SIGHT_GENERATION is stale and is overwritten; one that is current takes
 * the MINIMUM, so several blockers in one direction leave the nearest.
 *
 * THE THREE BANDS ARE HEIGHT, and which of them a blocker fills is the whole
 * point: one standing ABOVE the viewer fills all three, one LEVEL with it --
 * within AM2_SIGHT_BAND_STEP -- fills the middle and the far, and one BELOW
 * fills only the far. The bands it does not fill are set to the viewer's own
 * rank sight range, which is the same as "not obstructed at all".
 *
 * A SIXTEEN-ENTRY QUADRANT TABLE PICKS THE SILHOUETTE, and it is GENERATED
 * from the image rather than transcribed -- the same decision DirtyCollect's
 * eighty-one arms forced, where six hand-written codes landed in the wrong
 * arm. Four comparisons build a code out of (left < x), (top < y),
 * (right < x), (bottom < y); eight of the sixteen are geometrically impossible
 * or mean the viewer is INSIDE the box, and every one of those eight goes to
 * the same exit. The other eight name the two extreme corners.
 *
 * THE HEADING IS THE TURRET'S when the viewer is a vehicle with more than one
 * row -- OBJ_OFF_FIELD_530 rather than OBJ_OFF_FACING -- which is the one
 * place this function looks at what kind of thing is doing the seeing.
 *
 * ARC CLIPPING, and it is not symmetric. When one silhouette edge is inside
 * the rank's arc and the other is not, the outside one is CLIPPED to the arc
 * -- and which end gets clipped depends on which was inside. Both outside is a
 * refusal. A span that comes out at or below zero is a refusal too.
 *
 * The original hands AngleOfDelta a THIRD argument it does not read -- the
 * distance it has just computed. cdecl and caller-cleaned, so unobservable,
 * but the push is in the image and so it is reproduced, through the same
 * cast region.cpp's AI_ANGLE3 uses.
 */
#define AngleOfDelta3(x, y, d)                                                 \
    ((uint8_t (__cdecl *)(int32_t, int32_t, int32_t))(void (*)(void))AngleOfDelta)(x, y, d)
/* AngleDelta's answer with the compiler's abs() over it -- `cdq; xor; sub`,
 * which the original spells out at all four sites. */
static inline int32_t AbsAngle(uint32_t from, uint32_t to)
{
    return abs(AngleDelta(from, to));
}

void __cdecl AddSightBlocker(AM2_Object *viewer, AM2_Object *blocker)
{
    const AM2_Object *v = viewer;
    const AM2_Object *b = blocker;
    const AM2_RankRec *rank;
    uint8_t        heading;
    int32_t        hv;
    int32_t        hb;
    int32_t        l, t, r, bo;
    int16_t        vx, vy;
    AM2_Point      close;       /* a struct, so it lives in the frame */
    AM2_Point      farPt;
    int32_t        code;
    int32_t        dist;
    uint8_t        arc;
    int32_t        span;
    uint32_t       steps;
    int32_t        gen;
    int16_t        d;
    uint8_t        a0;
    uint8_t        a1;

    heading = v->facing;
    if (ObjIsType3((const AM2_Object *)v)
        && v->rowSet.count > 1)
        heading = v->turretFacing;

    hb = ObjHeight((const AM2_Object *)b);
    hv = ObjHeight((const AM2_Object *)v);
    if (hb + AM2_SIGHT_OVERHEAD < hv)
        return;

    if (b->rowSet.hitMask == (const void *)0) {
        int32_t bx = b->pos.x;
        int32_t by = b->pos.y;

        l  = b->rowSet.looseBox.left   + bx;
        r  = b->rowSet.looseBox.right  + bx;
        t  = b->rowSet.looseBox.top    + by;
        bo = b->rowSet.looseBox.bottom + by;
    } else {
        /* Through the rect, as objtype.cpp reaches the same four fields:
         * only OBJ_OFF_HIT_RECT has a macro and the other three are its
         * members, which is the composed-offset case checkoffsetuse lists. */
        const AM2_Rect *hit = &b->hit;

        l = hit->left; r = hit->right; t = hit->top; bo = hit->bottom;
    }

    vx = v->pos.x;

    /* The outcode, one test at a time. */
    code = 0;
    if (l < vx)
        code = 1;
    vy = v->pos.y;
    if (t < vy)
        code |= 2;
    if (r < vx)
        code |= 4;
    if (bo < vy)
        code |= 8;

    /* The two silhouette corners, by outcode (the byte table at 0x00403AE0
     * and the jump table at 0x00403ABC). Seven of the eight codes not listed
     * cannot happen with l <= r and t <= b; the eighth, code 3, is the
     * viewer standing INSIDE the box. */
    switch (code) {
    case 0:  close.x = l; close.y = bo; farPt.x = r; farPt.y = t;  break;  /* left,  above  */
    case 1:  close.x = l; close.y = t;  farPt.x = r; farPt.y = t;  break;  /* inside above  */
    case 2:  close.x = l; close.y = bo; farPt.x = l; farPt.y = t;  break;  /* left,  level  */
    case 5:  close.x = l; close.y = t;  farPt.x = r; farPt.y = bo; break;  /* right, above  */
    case 7:  close.x = r; close.y = t;  farPt.x = r; farPt.y = bo; break;  /* right, level  */
    case 10: close.x = r; close.y = bo; farPt.x = l; farPt.y = t;  break;  /* left,  below  */
    case 11: close.x = r; close.y = bo; farPt.x = l; farPt.y = bo; break;  /* inside below  */
    case 15: close.x = r; close.y = t;  farPt.x = l; farPt.y = bo; break;  /* right, below  */
    default:
        return;
    }

    {
        int32_t dx0 = (int16_t)(close.x - vx);
        int32_t dy0 = (int16_t)(close.y - vy);
        int32_t d0  = ApproxDistXY(dx0, dy0);
        int32_t dx1, dy1, d1;

        a0 = AngleOfDelta3(dx0, dy0, d0);
        dx1 = (int16_t)(farPt.x - v->pos.x);
        dy1 = (int16_t)(farPt.y - v->pos.y);
        d1 = ApproxDistXY(dx1, dy1);
        a1 = AngleOfDelta3(dx1, dy1, d1);
        /* The original pads the silhouette distance by one tile once (the
         * `add esi,0x10` at 0x004038AD), and every later use -- the range
         * check below and the band stores in the loop -- reads the padded
         * value.  The pad was previously applied only in the range check,
         * leaving the cache bands one tile short. */
        dist = d1;
        if (d0 > d1)
            dist = d0;
        dist += AM2_SIGHT_DIST_PAD;
    }

    rank = &g_rankRecords[v->rank];

    if ((int32_t)(int16_t)dist
        > rank->sight_range)
        return;

    arc = *(const uint8_t *)&rank->field_04;

    if (AbsAngle(heading, a0) < arc) {
        if (AbsAngle(heading, a1) > arc)
            a1 = (uint8_t)(arc + heading);      /* clip the farPt edge */
    } else {
        if (AbsAngle(heading, a1) > arc)
            return;                             /* both outside the arc */
        a0 = (uint8_t)(heading - arc);          /* clip the near edge */
    }

    span = AbsAngle(a1, a0);
    if ((int16_t)span <= 0)
        return;

    gen   = g_sightGeneration;
    steps = ((uint32_t)(int32_t)(int16_t)span + 3) >> 2;
    d     = (int16_t)dist;

    /* a0 itself is the sweep cursor, one record (four headings) a step. */
    do {
        uint32_t idx = (uint32_t)(a0 & 0xFF) >> 2;

#define REC   (g_sightBlockByDir[idx])
#define range (g_rankRecords[v->rank].sight_range)

        if (REC.stamp != gen) {
            REC.stamp = gen;

            if (hb > hv) {
                REC.low = d;
                REC.mid = d;
            } else if (hb + AM2_SIGHT_BAND_STEP >= hv) {
                REC.low = (int16_t)range;
                REC.mid = d;
            } else {
                REC.low = (int16_t)range;
                REC.mid = (int16_t)range;
            }
            REC.high = d;
        } else if (hb > hv) {
            /* Already stamped this generation: keep the nearest per band. */
            if (REC.low > d)
                REC.low = d;
            if (REC.mid > d)
                REC.mid = d;
            if (REC.high > d)
                REC.high = d;
        } else if (hb + AM2_SIGHT_BAND_STEP >= hv) {
            if (REC.mid > d)
                REC.mid = d;
            if (REC.high > d)
                REC.high = d;
        } else {
            if (REC.high > d)
                REC.high = d;
        }
        a0 = (uint8_t)(a0 + AM2_SIGHT_DIR_STEP);
    } while (--steps);
#undef range
#undef REC
}
#undef AngleOfDelta3


/* AirPassStrike -- original 0x00409540, 256 bytes, one caller: what an air
 * pass does when its timer runs out. It DROPS THREE PARATROOPERS, which the
 * function says nowhere and its two tables say between them -- offsets
 * (0,0), (-48,+32), (+48,+32), a V behind the drop point, and facings 0x00,
 * 0x60, 0xA0, so each lands looking a different way.
 *
 * The count is the distance between the two tables over four, not a literal.
 * Written that way for the same reason FreeAimSprites is: the bound IS the
 * next global.
 *
 * Each trooper gets a weapon and only then a soldier kind, in that order --
 * SoldierKindForWeapon is asked what kind suits the weapon that was just
 * made, so making the weapon first is required and not incidental.
 *
 * The flag word is 0x8000 always, plus 0x200 -- OBJ_FLAG_CONCEALED -- when
 * the drop is NOT for the default owner and the fog of war is ON
 * (ADDR_FOG_OF_WAR is 0 when fog is on). 0x00409557 skips the bit when the
 * army IS the owner, and 0x0040955F when the fog word is non-zero. So an
 * enemy drop lands hidden under fog; ours never does. This said the
 * opposite -- "for the default owner", "reveals the drop" -- and the code
 * followed it, concealing our own paratroopers until the byte-match read
 * the branch.
 *
 * A NULL WEAPON SKIPS THE WHOLE TAIL but still leaves the trooper on the map
 * -- the guard is around the four calls, not around the creation. */
void __cdecl AirPassStrike(AM2_Point at, int32_t army)
{
#define OFF  (g_airDropOffsets)
#define FACE (g_airDropFacings)
    int32_t flags = 0;
    int32_t i;

    if (army != (int32_t)g_defaultOwner && g_fogOfWar == 0)
        flags = AM2_AIR_DROP_CONCEAL;
    flags |= AM2_AIR_DROP_FLAG;

    for (i = 0; i < AM2_AIR_DROP_COUNT; i++) {
        AM2_Object *trooper;
        AM2_Object *weapon;

        trooper = (AM2_Object *)CreateTrooper(g_dirScratch,
                                at.x + OFF[i * 2],
                                at.y + OFF[i * 2 + 1],
                                AM2_THE_COMM->CommArmyOfSlot(army),
                                army, flags, 0, 0, 1, 0);

        weapon = (AM2_Object *)CreateWeapon(g_dirScratch, army,
                              KeyLookupTriple(AM2_AIR_DROP_WEAPON_KEY, 1, 0),
                              g_zeroXY,
                              4, -1, 0, 0);
        if (!weapon)
            continue;

        trooper->inventory[0] =
            weapon->uid;
        trooper->facing = FACE[i];

        SoldierKindForWeapon(trooper,
            (uint32_t)weapon->typeRec->kind);
        SendTrooperSetWeapon(trooper,
            weapon->uid, 0);
    }
#undef FACE
#undef OFF
}


/* ---- the air path's eleven startup initialisers -------------------------
 *
 * Entries in the C++ static-initializer table at 0x00473004 that _initterm
 * runs before WinMain. Each derives exactly one of the parameters orig.h
 * lists as "the nine derived parameters", plus the two turn waypoints.
 *
 * THEY ARE REACHABLE, which is worth stating before anyone reads a detour on
 * one as dead. tools/launcher.c creates the process SUSPENDED and injects
 * with a remote LoadLibraryA, so DllMain -- and install() with it -- finishes
 * before the EXE entry point and therefore before _initterm. The CRT calls
 * these through the table and reaches our code.
 *
 * TWO OF THEM WRITE BACK INTO .data: TURN_Y_IN and TURN_Y_OUT read 300 and 0
 * in the file and 320 and 160 once these have run. orig.h already uses that
 * pair as its example of asking who WRITES a value before treating a table's
 * number as a constant, and everything below them consumes the run-time pair.
 *
 * THE ARITHMETIC IS PLAIN C RATHER THAN x87, and the check is not that it
 * looks equivalent. The original computes the two slope terms on the FPU and
 * truncates through _ftol; double arithmetic with a truncating cast gives the
 * same answers, and they are the values orig.h independently records for
 * those globals -- LEG1_X0 is 110 and LEG3_X1 is -6, both exact. That is the
 * trig tables' precedent, where the x87 was reproduced first and then
 * measured to be unnecessary.
 *
 * LEG2_DIVISOR is stored as a FLOAT because the global is one; a double there
 * would change what every later read sees. */

void __cdecl AirInitTurnYIn(void)
{
    g_airPathTurnYIn = (int16_t)(g_airPathMidY + g_airPathHalfY);
}

void __cdecl AirInitTurnYOut(void)
{
    g_airPathTurnYOut = (int16_t)(g_airPathMidY - g_airPathHalfY);
}

void __cdecl AirInitLeg1X0(void)
{
    g_airLeg1X0 =
        (int32_t)((double)g_airPathTurnX - (double)(g_airPathInY - g_airPathTurnYIn)
                                   * g_airLeg1Slope);
}

void __cdecl AirInitLeg1Dx(void)
{
    g_airLeg1Dx =
        g_airPathTurnX - g_airLeg1X0;
}

void __cdecl AirInitLeg1Dy(void)
{
    g_airLeg1Dy = g_airPathInY - g_airPathTurnYIn;
}

void __cdecl AirInitLeg2MsSpan(void)
{
    g_airLeg2MsSpan =
        g_airLeg2Ms
        - g_airLeg1Ms;
}

void __cdecl AirInitLeg2Dy(void)
{
    g_airLeg2Dy = g_airPathTurnYIn - g_airPathTurnYOut;
}

/* (rise * rise) / run, the parabola leg 2 follows, with the run measured from
 * the apex. The original squares by duplicating the loaded integer rather
 * than loading it twice, so both factors are necessarily the same value. */
void __cdecl AirInitLeg2Divisor(void)
{
    int32_t rise = g_airPathTurnYIn - g_airPathHalfY;
    int32_t run  = g_airPathTurnX - g_airPathApexX;

    g_airLeg2Divisor =
        (float)rise * rise / run;         /* float, as the original: fidiv */
}

void __cdecl AirInitLeg3X1(void)
{
    g_airLeg3X1 =
        (int32_t)((double)(g_airPathOutY - g_airPathTurnYOut)
                  * g_airLeg3Slope
                  + (double)g_airPathAwayX);
}

void __cdecl AirInitLeg3Dx(void)
{
    g_airLeg3Dx =
        g_airPathAwayX - g_airLeg3X1;
}

void __cdecl AirInitLeg3Dy(void)
{
    g_airLeg3Dy = g_airPathTurnYOut - g_airPathOutY;
}

void air_install(void)
{
    patch_replace(ADDR_AIR_PASS_STRIKE, (const void *)AirPassStrike,
                  "AirPassStrike", 2);
    patch_replace(ADDR_FORMATION_SLOT_POINT,
                  (const void *)FormationSlotPoint,
                  "FormationSlotPoint", 3);
    patch_replace(ADDR_ADD_SIGHT_BLOCKER, (const void *)AddSightBlocker,
                  "AddSightBlocker", 1);
    patch_replace(ADDR_FORMATION_POINT, (const void *)FormationPoint,
                  "FormationPoint", 4);
    patch_replace(ADDR_FORMATION_POINT_FAR, (const void *)FormationPointFar,
                  "FormationPointFar", 1);
    patch_replace(ADDR_RANDOM_POINT_AHEAD, (const void *)RandomPointAhead,
                  "RandomPointAhead", 2);
    patch_replace(ADDR_RANDOM_POINT_TOWARD, (const void *)RandomPointToward,
                  "RandomPointToward", 2);
    patch_replace(ADDR_RESOLVE_FORMATION_POINT,
                  (const void *)ResolveFormationPoint,
                  "ResolveFormationPoint", 3);
    patch_replace(ADDR_CLEAR_FRAME_COUNTS, (const void *)ClearFrameCounts,
                  "ClearFrameCounts", 0);
    patch_replace(ADDR_OBJ_ANCHOR_POINT, (const void *)ObjAnchorPoint,
                  "ObjAnchorPoint", 1);
    patch_replace(ADDR_OBJ_REVEAL, (const void *)RevealObj,
                  "RevealObj", 1);
    patch_replace(ADDR_REMAP_SPRITE_RUNS, (const void *)RemapSpriteRuns,
                  "RemapSpriteRuns", 1);
    patch_replace(ADDR_FREE_SPRITE_LIST, (const void *)FreeSpriteList,
                  "FreeSpriteList", 3);
    patch_replace(ADDR_GROW_SPRITE_LIST, (const void *)GrowSpriteList,
                  "GrowSpriteList", 1);
    patch_replace(ADDR_REVEAL_NEARBY,
                  (const void *)RevealNearby,
                  "RevealNearby", 2);
    patch_replace(ADDR_DO_AIR_SUPPORT, (const void *)DoAirSupport,
                  "DoAirSupport", 3);
    patch_replace(ADDR_FIND_ENEMY_NEAR, (const void *)FindEnemyNear,
                  "FindEnemyNear", 1);
    patch_replace(ADDR_PEER_SHOULD_NACK, (const void *)PeerShouldNack,
                  "PeerShouldNack", 1);
    patch_replace(ADDR_AIR_BEGIN, (const void *)AirSupportBegin,
                  "AirSupportBegin", 2);
    patch_replace(ADDR_AIR_CLEAR, (const void *)AirSupportClear,
                  "AirSupportClear", 1);
    patch_replace(ADDR_AIR_POP, (const void *)AirSupportPop,
                  "AirSupportPop", 2);
    patch_replace(ADDR_AIR_PASSES_DRAW, (const void *)AirPassesDraw,
                  "AirPassesDraw", 1);
    patch_replace(ADDR_AIR_DELIVER, (const void *)AirDeliver,
                  "AirDeliver", 1);
    patch_replace(ADDR_SAVE_AIR_SECTION, (const void *)SaveAirSection,
                  "SaveAirSection", 1);
    patch_replace(ADDR_LOAD_AIR_SECTION, (const void *)LoadAirSection,
                  "LoadAirSection", 1);
    patch_replace(ADDR_OBJ_CONCEAL, (const void *)ObjConceal,
                  "ObjConceal", 13);
    patch_replace(ADDR_TOGGLE_FOG_OF_WAR, (const void *)ToggleFogOfWar,
                  "ToggleFogOfWar", 2);
    patch_replace(ADDR_AT_FLAG_BASE, (const void *)AtFlagBase, "AtFlagBase", 4);
    patch_replace(ADDR_THING_CODE, (const void *)ThingCode, "ThingCode", 2);
    patch_replace(ADDR_OBJ_BLOCK_WEIGHT, (const void *)ObjBlockWeight,
                  "ObjBlockWeight", 3);
    patch_replace(ADDR_SET_FOG_OF_WAR, (const void *)SetFogOfWar,
                  "SetFogOfWar", 1);
    patch_replace(ADDR_AIR_INIT_TURN_Y_IN, (const void *)AirInitTurnYIn, "AirInitTurnYIn", 0);
    patch_replace(ADDR_AIR_INIT_TURN_Y_OUT, (const void *)AirInitTurnYOut, "AirInitTurnYOut", 0);
    patch_replace(ADDR_AIR_INIT_LEG1_X0, (const void *)AirInitLeg1X0, "AirInitLeg1X0", 0);
    patch_replace(ADDR_AIR_INIT_LEG1_DX, (const void *)AirInitLeg1Dx, "AirInitLeg1Dx", 0);
    patch_replace(ADDR_AIR_INIT_LEG1_DY, (const void *)AirInitLeg1Dy, "AirInitLeg1Dy", 0);
    patch_replace(ADDR_AIR_INIT_LEG2_MS_SPAN, (const void *)AirInitLeg2MsSpan, "AirInitLeg2MsSpan", 0);
    patch_replace(ADDR_AIR_INIT_LEG2_DY, (const void *)AirInitLeg2Dy, "AirInitLeg2Dy", 0);
    patch_replace(ADDR_AIR_INIT_LEG2_DIVISOR, (const void *)AirInitLeg2Divisor, "AirInitLeg2Divisor", 0);
    patch_replace(ADDR_AIR_INIT_LEG3_X1, (const void *)AirInitLeg3X1, "AirInitLeg3X1", 0);
    patch_replace(ADDR_AIR_INIT_LEG3_DX, (const void *)AirInitLeg3Dx, "AirInitLeg3Dx", 0);
    patch_replace(ADDR_AIR_INIT_LEG3_DY, (const void *)AirInitLeg3Dy, "AirInitLeg3Dy", 0);
}

