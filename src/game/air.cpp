/* air.cpp -- see air.h. */
#include <stdint.h>

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
#include "image.h"
#include "misc.h"   /* MeetsAllThree -- reconstructed */
#include "msgslot.h" /* CommMustBroadcast -- reconstructed */
#include "script.h"     /* ScriptFindName -- reconstructed */
#include "scriptint.h"  /* kScriptNames */
#include "army.h"   /* ObjIsFriendly -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "commmsg.h"  /* MsgListSetFlag, MsgListAdd -- reconstructed */
#include "gameproc.h" /* NoteKind31 -- reconstructed */
#include "maprow.h"   /* RowUpdate -- reconstructed */

#define kAirSaveBlock ((void *)(uintptr_t)AM2_IMAGE(ADDR_AIR_SAVE_BLOCK))

int32_t __cdecl SaveAirSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_AIR);
    orig_fwrite(kAirSaveBlock, AM2_AIR_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadAirSection(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_AIR,
                      (const char *)AM2_IMAGE(ADDR_STR_AIR_CPP), 0x28B))
        return 0;

    orig_fread(kAirSaveBlock, AM2_AIR_SAVE_SIZE, 1, fp);
    return 1;
}

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header because this module is on the flat side of the
 * split and audio.h names Win32 types -- the same reason commmsg.cpp does it,
 * and spelled the same way so the two cannot drift. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int32_t x, int32_t y);

/* Every one of these is a field of the block above -- the queue and the
 * savegame section are the same 584 bytes, which is why they are written as
 * offsets rather than as addresses of their own. */
#define kAirField(off) ((uint8_t *)kAirSaveBlock + (off))
#define g_airActive  (*(int32_t *)kAirField(AIR_OFF_ACTIVE))
#define g_airPending (*(int32_t *)kAirField(AIR_OFF_PENDING))
#define g_airCount   (*(int32_t *)kAirField(AIR_OFF_COUNT))
#define g_airWhere   ((uint16_t *)kAirField(AIR_OFF_WHERE))
#define g_airKind    ((int32_t *)kAirField(AIR_OFF_KIND))
#define g_airFrom    ((uint32_t *)kAirField(AIR_OFF_FROM))
#define g_airExtra   ((int32_t *)kAirField(AIR_OFF_EXTRA))
#define g_airFlagA   (*(int32_t *)kAirField(AIR_OFF_FLAG_A))
#define g_airFlagB   (*(int32_t *)kAirField(AIR_OFF_FLAG_B))

typedef uint32_t (__stdcall *AM2_GetTickCountFn)(void);
#define orig_get_tick_count \
    (*(AM2_GetTickCountFn *)AM2_IMAGE(ADDR_IAT_GET_TICK_COUNT))

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
int32_t __cdecl PeerShouldNack(void *peer, uint32_t seq)
{
    uint8_t  *p   = (uint8_t *)peer;
    uint32_t  now = (uint32_t)orig_get_tick_count();
    uint32_t  n;
    uint32_t  interval;
    uint32_t  latency;
    uint32_t  capped;
    int32_t   count;
    int32_t   i;

    n = *(const uint32_t *)(p + PEER_OFF_INTERVAL_N);
    interval = *(const uint32_t *)(p + PEER_OFF_INTERVAL_SUM)
               / (n < 2 ? 1u : n - 1u);

    n = *(const uint32_t *)(p + PEER_OFF_LATENCY_N);
    latency = *(const uint32_t *)(p + PEER_OFF_LATENCY_SUM) / (n < 1 ? 1u : n);
    capped = latency > AM2_NACK_INTERVAL_CAP ? AM2_NACK_INTERVAL_CAP : latency;

    count = *(const int32_t *)(p + PEER_OFF_NACK_COUNT);
    for (i = 0; i < count; i++) {
        uint8_t *rec = p + PEER_OFF_NACKS + (uint32_t)i * AM2_NACKREC_STRIDE;

        if (*(const uint32_t *)(rec + NACKREC_OFF_SEQ) != seq)
            continue;

        if (now <= *(const uint32_t *)(rec + NACKREC_OFF_TIME) + capped)
            return 0;

        if (*(const int32_t *)((const uint8_t *)
                *(void *const *)(uintptr_t)ADDR_COMM_OBJECT + COMM_OFF_VERBOSE))
            am2_log((const char *)AM2_IMAGE(ADDR_STR_NACKING),
                    seq, *(const uint32_t *)(p + PEER_OFF_ID), now,
                    *(const uint32_t *)(rec + NACKREC_OFF_TIME) + capped,
                    now - *(const uint32_t *)(rec + NACKREC_OFF_TIME) - capped,
                    interval, latency,
                    *(const int32_t *)(rec + NACKREC_OFF_COUNT), capped);

        *(int32_t *)(rec + NACKREC_OFF_COUNT) += 1;
        *(uint32_t *)(rec + NACKREC_OFF_TIME) = now;
        return 1;
    }

    {
        uint8_t *rec = p + PEER_OFF_NACKS + (uint32_t)count * AM2_NACKREC_STRIDE;
        int32_t  first = *(const uint32_t *)(p + PEER_OFF_FIELD_40)
                         < (*(const uint32_t *)(p + PEER_OFF_FIELD_38) >> 1);

        *(uint32_t *)(rec + NACKREC_OFF_SEQ)  = seq;
        *(uint32_t *)(rec + NACKREC_OFF_TIME) = now;
        *(int32_t *)(rec + NACKREC_OFF_COUNT) = first ? 1 : 0;

        *(int32_t *)(p + PEER_OFF_NACK_COUNT) = count + 1;
        if (count + 1 >= AM2_NACK_RECS_MAX) {
            *(int32_t *)(p + PEER_OFF_NACK_COUNT) = count;
            am2_log((const char *)AM2_IMAGE(ADDR_STR_NACK_FULL),
                    *(const uint32_t *)(p + PEER_OFF_ID), count);
        }

        return first;
    }
}

/* Forward-declared rather than included: ObjectsInRect lives in
 * win32/mapdraw.h because it clips with IntersectRect, and including that
 * header here would drag windows.h into a flat module. The same reason
 * script.cpp forward-declares PreloadSprite. */
void *__cdecl ObjectsInRect(const AM2_Rect *r, const void *desc,
                            int32_t (__cdecl *keep)(void *obj));

uint32_t __cdecl FindEnemyNear(uint32_t where, uint32_t from)
{
    int32_t   x = (int32_t)(int16_t)(where & 0xFFFF);
    int32_t   y = (int32_t)(int16_t)(where >> 16);
    AM2_Rect  box;
    uint8_t  *o;

    box.left   = x - AM2_AIR_ENEMY_RADIUS;
    box.top    = y - AM2_AIR_ENEMY_RADIUS;
    box.right  = x + AM2_AIR_ENEMY_RADIUS;
    box.bottom = y + AM2_AIR_ENEMY_RADIUS;

    o = (uint8_t *)ObjectsInRect(&box, (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC,
                                 (int32_t (__cdecl *)(void *))MeetsAllThree);

    /* `owner` is objtable.h's AM2_Object field at 0x0010, which orig.h's
     * OBJ_OFF_OWNER is NOT -- that constant is 0x0004 and belongs to a
     * different structure entirely. Two right names, one collision. */
    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT)) {
        const AM2_Object *obj = (const AM2_Object *)o;

        /* Once per candidate, not once before the loop. */
        if ((int32_t)obj->owner != (int32_t)UidArmy(from)
            && *(const int16_t *)(o + OBJ_OFF_HEALTH) > 0)
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
void __cdecl RevealObj(void *obj)
{
    uint8_t  *o = (uint8_t *)obj;
    uint32_t  flags;
    int32_t   i;

    if (!obj)
        return;

    flags = *(uint32_t *)(o + OBJ_OFF_FLAGS) | OBJ_FLAG_REVEALED;
    *(uint32_t *)(o + OBJ_OFF_FLAGS) = flags;
    if (!(flags & OBJ_FLAG_CONCEALED))
        return;
    *(uint32_t *)(o + OBJ_OFF_FLAGS) = flags & ~(uint32_t)OBJ_FLAG_CONCEALED;

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        uint8_t *row = *(uint8_t **)(o + OBJ_OFF_ROWS)
                       + (uint32_t)i * AM2_OBJ_ROW_STRIDE;

        *(uint32_t *)row &= ~(uint32_t)ROW_FLAG_REMOVED;
        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
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
void __cdecl ObjConceal(void *obj, int32_t force)
{
    uint8_t *o = (uint8_t *)obj;
    uint32_t flags;
    int32_t  i;

    if (!obj)
        return;

    flags = *(const uint32_t *)(o + OBJ_OFF_FLAGS);
    if (flags & OBJ_FLAG_CONCEALED)
        return;

    if (!(flags & OBJ_FLAG_BIT4)
        || !ObjIsType2((const AM2_Object *)obj)
        || *(const int32_t *)(o + OBJ_OFF_FIELD_530) != 5) {
        *(uint32_t *)(o + OBJ_OFF_FLAGS) =
            *(const uint32_t *)(o + OBJ_OFF_FLAGS)
            & ~(uint32_t)OBJ_FLAG_REVEALED;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_FOG_OF_WAR && !force)
        return;

    *(uint32_t *)(o + OBJ_OFF_FLAGS) |= OBJ_FLAG_CONCEALED;

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
        uint8_t *row = *(uint8_t **)(o + OBJ_OFF_ROWS)
                       + (uint32_t)i * AM2_OBJ_ROW_STRIDE;

        *(uint32_t *)row |= (uint32_t)ROW_FLAG_REMOVED;
        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
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
        *(int32_t *)(uintptr_t)ADDR_FOG_OF_WAR = 0;
        return;
    }
    *(int32_t *)(uintptr_t)ADDR_FOG_OF_WAR = 1;

    for (obj = FirstItem(); obj; obj = NextItem()) {
        uint8_t *o = (uint8_t *)obj;
        uint32_t flags;
        int32_t  i;

        if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            continue;
        if (!ObjIsTypeIn238((const AM2_Object *)obj))
            continue;

        flags = *(const uint32_t *)(o + OBJ_OFF_FLAGS);
        if (!(flags & OBJ_FLAG_CONCEALED))
            continue;
        *(uint32_t *)(o + OBJ_OFF_FLAGS) = flags & ~(uint32_t)OBJ_FLAG_CONCEALED;

        for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++) {
            uint8_t *row = *(uint8_t **)(o + OBJ_OFF_ROWS)
                           + (uint32_t)i * AM2_OBJ_ROW_STRIDE;

            *(uint32_t *)row &= ~(uint32_t)ROW_FLAG_REMOVED;
            RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
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
    *(int32_t *)(uintptr_t)ADDR_PERFRAME_COUNT_A = 0;
    *(int32_t *)(uintptr_t)ADDR_PERFRAME_COUNT_B = 0;
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
uint32_t __cdecl ObjAnchorPoint(const void *obj)
{
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *row;
    const uint8_t *spr;
    AM2_Point      pt;
    int32_t        rows;

    if (!obj)
        return *(const uint32_t *)AM2_IMAGE(ADDR_ZERO_POINT);

    pt   = *(const AM2_Point *)(o + OBJ_OFF_POS);
    rows = *(const int32_t *)(o + OBJ_OFF_ROW_COUNT);
    if (rows < 1)
        return *(const uint32_t *)&pt;

    row = *(const uint8_t *const *)(o + OBJ_OFF_ROWS);
    if (rows > 1)
        row += AM2_OBJ_ROW_STRIDE;
    if (!row)
        return *(const uint32_t *)&pt;

    spr = *(const uint8_t *const *)(row + ROW_OFF_SPRITE);
    if (!spr)
        return *(const uint32_t *)&pt;

    pt.x = (int16_t)(pt.x - *(const int16_t *)(spr + SPR_OFF_OVX));
    pt.y = (int16_t)(pt.y - *(const int16_t *)(spr + SPR_OFF_OVY));
    return *(const uint32_t *)&pt;
}


#define g_mapBoundsLeft   (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT))
#define g_mapBoundsTop    (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_TOP))
#define g_mapBoundsRight  (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_RIGHT))
#define g_mapBoundsBottom (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_BOTTOM))

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
void __cdecl FormationPointFar(void *follower, void *leader, AM2_Point *out,
                               int32_t slot)
{
    const uint8_t *l = (const uint8_t *)leader;
    uint8_t        leaderFacing;
    uint8_t        facing;
    int32_t        dist;
    int32_t        type;
    uint32_t       shift;

    (void)follower;

    out->x = *(const int16_t *)(l + OBJ_OFF_POS);
    out->y = *(const int16_t *)(l + OBJ_OFF_POS + 2);

    type = *(const int32_t *)l;
    if (type == 3) {
        leaderFacing = *(const uint8_t *)(l + OBJ_OFF_FACING);
        shift        = 1;
    } else if (type == 2 || type == 8) {
        leaderFacing = *(const uint8_t *)(l + OBJ_OFF_FACING);
        shift        = 0;
    } else {
        leaderFacing = 0;
        shift        = 0;
    }

    facing = (uint8_t)((uint8_t)((uint8_t)(slot + 4) << 4) + leaderFacing);
    dist   = (((slot - 11) / 16 + 4) * 32) << shift;

    if (ObjIsType3((const AM2_Object *)leader)) {
        int32_t delta = AngleDelta(facing, leaderFacing);
        int32_t mag   = delta < 0 ? -delta : delta;

        if (mag < 0x40) {
            facing = (uint8_t)(facing + (delta < 0 ? 0xC3 : 0x3D));
            dist  += 0x20;
        }
    }

    if (dist > 0) {
        double d = (double)dist;

        out->x = (int16_t)(int32_t)((double)Cos8(facing) * d
                                    + (double)out->x);
        out->y = (int16_t)(int32_t)((double)Sin8(facing) * d
                                    + (double)out->y);

        out->x = (int16_t)Clamp(out->x, g_mapBoundsLeft, g_mapBoundsRight);
        out->y = (int16_t)Clamp(out->y, g_mapBoundsTop, g_mapBoundsBottom);
    }

    SettlePointInRegion(TileOfPoint(*(const uint32_t *)out),
                        (uint32_t *)out);
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
void __cdecl FormationPoint(void *follower, void *leader, AM2_Point *out,
                            int32_t slot)
{
    const uint8_t *l = (const uint8_t *)leader;
    const uint8_t *rec;
    uint8_t        leaderFacing;
    uint8_t        facing;
    int32_t        dist;
    int32_t        type;
    uint32_t       shift;

    if (!follower || !leader)
        return;

    if (slot >= AM2_FORMATION_SLOTS) {
        FormationPointFar(follower, leader, out, slot);
        return;
    }

    out->x = *(const int16_t *)(l + OBJ_OFF_POS);
    out->y = *(const int16_t *)(l + OBJ_OFF_POS + 2);

    type = *(const int32_t *)l;
    if (type == 3) {
        leaderFacing = *(const uint8_t *)(l + OBJ_OFF_FACING);
        shift        = 1;
    } else if (type == 2 || type == 8) {
        leaderFacing = *(const uint8_t *)(l + OBJ_OFF_FACING);
        shift        = 0;
    } else {
        leaderFacing = 0;
        shift        = 0;
    }

    rec    = (const uint8_t *)AM2_IMAGE(ADDR_FORMATION_SLOTS)
             + (uint32_t)slot * AM2_FORMATION_SLOT_STRIDE;
    dist   = (int32_t)*(const int16_t *)(rec + 2) << shift;
    facing = (uint8_t)(*rec + leaderFacing);

    if (ObjIsType3((const AM2_Object *)leader)) {
        int32_t delta = AngleDelta(facing, leaderFacing);
        int32_t mag   = delta < 0 ? -delta : delta;

        if (mag < 0x40) {
            facing = (uint8_t)(facing + (delta < 0 ? 0xC3 : 0x3D));
            dist  += 0x20;
        }
    }

    if (dist > 0) {
        double d = (double)dist;

        out->x = (int16_t)(int32_t)((double)Cos8(facing) * d
                                    + (double)out->x);
        out->y = (int16_t)(int32_t)((double)Sin8(facing) * d
                                    + (double)out->y);

        out->x = (int16_t)Clamp(out->x, g_mapBoundsLeft, g_mapBoundsRight);
        out->y = (int16_t)Clamp(out->y, g_mapBoundsTop, g_mapBoundsBottom);
    }

    SettlePointInRegion(TileOfPoint(*(const uint32_t *)out),
                        (uint32_t *)out);
}

typedef int32_t (__cdecl *AM2_AirRandFn)(void);
#define orig_air_rand ((AM2_AirRandFn)AM2_IMAGE(ADDR_GAME_RAND))

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
void __cdecl RandomPointToward(const void *target, const void *obj,
                               int32_t dist, AM2_Point *out)
{
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *t = (const uint8_t *)target;
    uint8_t        heading;
    double         d = (double)dist;

    heading = (uint8_t)(AngleBetween((const AM2_Point *)(o + OBJ_OFF_POS),
                                     (const AM2_Point *)(t + OBJ_OFF_POS))
                        + (orig_air_rand() & 0x3F) - 0x20);

    out->x = (int16_t)(int32_t)((double)Cos8(heading) * d
                                + (double)*(const int16_t *)(o + OBJ_OFF_X));
    out->y = (int16_t)(int32_t)((double)Sin8(heading) * d
                                + (double)*(const int16_t *)(o + OBJ_OFF_Y));
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
void __cdecl RandomPointAhead(void *, const void *obj, int32_t dist,
                              AM2_Point *out)
{
    const uint8_t *o = (const uint8_t *)obj;
    uint8_t        facing;
    double         d = (double)dist;

    facing = (uint8_t)((orig_air_rand() & 0x3F)
                       + *(const uint8_t *)(o + OBJ_OFF_FACING) - 0x20);

    out->x = (int16_t)(int32_t)((double)Cos8(facing) * d
                                + (double)*(const int16_t *)(o + OBJ_OFF_X));
    out->y = (int16_t)(int32_t)((double)Sin8(facing) * d
                                + (double)*(const int16_t *)(o + OBJ_OFF_Y));
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
void __cdecl ResolveFormationPoint(void *follower, void *leader,
                                   AM2_Point *out)
{
    uint8_t *f = (uint8_t *)follower;

    if (!follower || !leader)
        return;

    if (ObjIsType2((const AM2_Object *)leader)) {
        uint32_t riding =
            *(const uint32_t *)((const uint8_t *)leader + OBJ_OFF_RIDING);

        if (riding) {
            AM2_Object *veh = (AM2_Object *)LookupByUID(riding);

            if (veh) {
                *(uint32_t *)(f + OBJ_OFF_FOLLOW_UID) = veh->uid;
                leader = veh;
            }
        }
    }

    FormationPoint(follower, leader, out,
                         *(const int32_t *)(f + OBJ_OFF_FORMATION_SLOT));
}

/* Spelled exactly as event.cpp spells it, AM2_IMAGE and all, so the two
 * stay one definition. */
#define g_gameClockMs (*(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS))

void __cdecl RevealNearby(AM2_Point where, int32_t radius, int32_t delayMs)
{
    uint8_t *o;

    for (o = (uint8_t *)FirstItem(); o; o = (uint8_t *)NextItem()) {
        /* Types 2, 3 and 8 only; not one that is already revealed; and
         * ApproxDist -- a diamond, not a circle -- within the radius. */
        if (!ObjIsTypeIn238((const AM2_Object *)o))
            continue;
        if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_REVEALED)
            continue;
        if (ApproxDist(&where, (const AM2_Point *)(o + OBJ_OFF_POS))
                > radius)
            continue;

        RevealObj(o);
        *(int32_t *)(o + OBJ_OFF_REVEALED_UNTIL) =
            (int32_t)g_gameClockMs + delayMs;
    }
}

/* ReleaseSprite is reconstructed, in win32/sprite.cpp. Declared here rather
 * than by including that header because this module is flat and AM2_Sprite has
 * an LPDIRECTDRAWSURFACE in it -- the same reason script.cpp declares
 * PreloadSprite. An incomplete type is all this needs. */
struct AM2_Sprite;
extern "C" void __cdecl ReleaseSprite(AM2_Sprite *spr);

/* DrawSprite is reconstructed too, and declared here for the same reason.
 * Its four arguments name no Win32 type once AM2_Sprite is opaque. */
extern "C" void __cdecl DrawSprite(AM2_Sprite *spr, int32_t x, int32_t y,
                                   int32_t mode);

#define g_spriteList    (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_LIST)
#define g_spriteListN   (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_COUNT)
#define g_spriteListCap (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_CAP)

void __cdecl RemapSpriteRuns(void *img, int32_t unused, const uint8_t *table,
                             int32_t from)
{
    const uint8_t *hdr = (const uint8_t *)img;
    uint32_t       width;
    uint32_t       height;
    uint8_t       *p;

    (void)unused;   /* pushed by the call site, read by nothing here. */

    if (!table)
        return;

    height = *(const uint16_t *)(hdr + 2);
    width  = *(const uint16_t *)(hdr + 0);

    /* Four bytes of header and then one uint16 per row. */
    p = (uint8_t *)img + 4 + height * 2;

    /* The HEIGHT is what is tested, not the width. */
    if ((int32_t)height <= 0)
        return;

    do {
        int32_t covered = 0;

        /* The width is tested once, before the row rather than inside it --
         * it cannot change, and the original checks it exactly here. */
        if ((int32_t)width > 0) {
            do {
                int32_t skip = *p++;
                int32_t run  = *p++;

                covered += skip;
                covered += run;

                for (; run > 0; run--, p++) {
                    int32_t v = *p;

                    /* Below `from` is a reserved index and stays put. */
                    if (v >= from)
                        *p = table[v];
                }
            } while (covered < (int32_t)width);
        }
    } while (--height);
}

void __cdecl FreeSpriteList(void)
{
    int32_t i;

    /* No array: the count and the capacity are cleared anyway. */
    if (!g_spriteList) {
        g_spriteListN   = 0;
        g_spriteListCap = 0;
        return;
    }

    /* The count is re-read every iteration, not held. */
    for (i = 0; i < g_spriteListN; i++)
        ReleaseSprite(g_spriteList[i]);

    am2_free(g_spriteList);
    g_spriteList    = (AM2_Sprite **)0;
    g_spriteListN   = 0;
    g_spriteListCap = 0;
}

void __cdecl GrowSpriteList(void)
{
    /* A hundred more, and the COUNT is not consulted -- this is "make room",
     * not "grow if full". The realloc is not checked. */
    g_spriteListCap += AM2_SPRITE_LIST_GROW;
    g_spriteList = (AM2_Sprite **)am2_realloc(g_spriteList,
                                              (size_t)g_spriteListCap * 4u);
}

int32_t __cdecl DoAirSupport(int32_t kind, uint32_t where, uint32_t from)
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
    ((uint32_t *)g_airWhere)[g_airCount] = where;
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
    if (g_airExtra[0]) {
        g_airFlagA = 1;
        g_airFlagB = 1;
        return;
    }

    PlaySoundAt(AM2_AIR_SOUND, 0, 0, 0, 0);
    g_airActive = 1;
    g_airFlagA  = 0;
    g_airFlagB  = 0;
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
        g_airWhere[(i - 1) * 2]     = g_airWhere[i * 2];
        g_airWhere[(i - 1) * 2 + 1] = g_airWhere[i * 2 + 1];
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
int32_t __cdecl AtFlagBase(const void *who, const void *owner, int32_t army,
                           const char *name)
{
    const uint8_t *w = (const uint8_t *)who;
    const uint8_t *o = (const uint8_t *)owner;
    const uint8_t *base;

    if (CommArmyOfSlot(*(void *const *)(uintptr_t)ADDR_COMM_OBJECT,
                       *(const int8_t *)(o + OBJ_OFF_ARMY)) != army)
        return AM2_NOT_AT_FLAG_BASE;

    base = (const uint8_t *)LookupByUID(
               (uint32_t)kScriptNames[ScriptFindName(name)].value);
    if (!base)
        return AM2_NOT_AT_FLAG_BASE;

    if (ApproxDist((const AM2_Point *)(base + OBJ_OFF_POS),
                   (const AM2_Point *)(w + OBJ_OFF_POS))
        >= AM2_FLAG_BASE_RANGE)
        return AM2_NOT_AT_FLAG_BASE;

    return 0;
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
int32_t __cdecl ObjBlockWeight(void *from, void *obj, int32_t unused,
                               uint32_t at)
{
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *f = (const uint8_t *)from;

    (void)unused;

    if (o == f)
        return 0;

    if (f) {
        int32_t d = (int32_t)*(const int8_t *)(o + OBJ_OFF_HEIGHT_SET)
                  - (int32_t)*(const int8_t *)(f + OBJ_OFF_HEIGHT_SET);

        if (d < 0)
            d = -d;
        if (d > AM2_BLOCK_HEIGHT_STEP)
            return 0;
    }

    if (ObjIsItem((const AM2_Object *)obj))
        return *(const int8_t *)(o + OBJ_OFF_RANK);

    if (!f)
        return AM2_BLOCK_FULL;

    if (!ObjIsType3((const AM2_Object *)obj)
        && !ObjIsType8((const AM2_Object *)obj))
        return AM2_BLOCK_FULL;

    if (ApproxDist((const AM2_Point *)&at, (const AM2_Point *)(o + OBJ_OFF_POS))
        < ApproxDist((const AM2_Point *)(f + OBJ_OFF_POS),
                     (const AM2_Point *)(o + OBJ_OFF_POS)))
        return AM2_BLOCK_FULL;

    return 0;
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
int32_t __cdecl ThingCode(const void *who, const void *owner)
{
    const uint8_t *w = (const uint8_t *)who;
    const uint8_t *o = (const uint8_t *)owner;
    int32_t        code = **(const int32_t *const *)(w + OBJ_OFF_FIELD_C0);

    switch (code) {
    case 2:               return 8;
    case 3: case 4: case 5:  return 0x10;
    case 6: case 7:       return 0x20;
    case 8: case 25: case 30: return 0x30;
    case 10: case 28:     return 0x1E;
    case 15:              return AM2_NOT_AT_FLAG_BASE;
    case 16: return AtFlagBase(w, o, 0,
                               (const char *)AM2_IMAGE(ADDR_STR_FLAGBASE_GREEN));
    case 17: return AtFlagBase(w, o, 1,
                               (const char *)AM2_IMAGE(ADDR_STR_FLAGBASE_TAN));
    case 18: return AtFlagBase(w, o, 2,
                               (const char *)AM2_IMAGE(ADDR_STR_FLAGBASE_BLUE));
    case 19: return AtFlagBase(w, o, 3,
                               (const char *)AM2_IMAGE(ADDR_STR_FLAGBASE_GREY));
    case 21:              return 0x21;
    case 22: {
        int32_t max    = *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);
        int32_t health = *(const int16_t *)(o + OBJ_OFF_HEALTH);

        return health >= max / 2 ? 8 : 0x20;
    }
    case 23:              return 0;   /* explicit, unlike the default below */
    case 24: case 39: case 40: return 0x60;
    case 26:              return 1;
    case 29:              return 0x40;
    default:              return 0;
    }
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

    int32_t *fog = (int32_t *)(uintptr_t)ADDR_FOG_OF_WAR;

    *fog = (*fog == 0);

    for (o = FirstItem(); o; o = NextItem()) {
        uint8_t *p = (uint8_t *)o;

        if (*(const uint8_t *)(p + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
            continue;
        if (!ObjIsTypeIn238((const AM2_Object *)p))
            continue;
        if (ObjIsFriendly(p))
            continue;

        if (*fog) {
            RevealObj(p);
        } else {
            uint32_t until = *(const uint32_t *)(p + OBJ_OFF_REVEALED_UNTIL);

            if (until == 0
                || *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS < until)
                ObjConceal(p, 0);
        }
    }
}

/* The pass queue: eight slots inside the same block the savegame section is,
 * so they are fields rather than globals of their own. */
#define g_airPassCount (*(int32_t *)kAirField(AIR_OFF_PASS_COUNT))
#define g_airPassTimer ((int32_t *)kAirField(AIR_OFF_PASS_TIMER))
#define g_airPassWhere ((int16_t *)kAirField(AIR_OFF_PASS_WHERE))
#define g_airPassArmy  ((int16_t *)kAirField(AIR_OFF_PASS_ARMY))

/* The strike itself, still original -- what a pass does when its timer runs
 * out. Its second argument is an army: it compares it against
 * ADDR_DEFAULT_OWNER, which is the third reading agreeing on that field. */
typedef void (__cdecl *AM2_AirStrikeFn)(uint32_t at, int32_t army);

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

    if (g_airKind[0] == 1) {
        int32_t n = g_airPassCount;

        if (n >= AM2_AIR_PASS_SLOTS)
            return;

        ((uint32_t *)g_airPassWhere)[n] = ((const uint32_t *)g_airWhere)[0];
        g_airPassTimer[g_airPassCount] = 1;
        g_airPassArmy[g_airPassCount] =
            (int16_t)UidArmy(g_airFrom[0]);
        ++g_airPassCount;
        return;
    }

    if (g_airKind[0] != 0)
        return;

    for (i = 0; i < AM2_AIR_STRIKE_BLASTS; i++) {
        int32_t dx = orig_air_rand() % AM2_AIR_STRIKE_SPREAD
                     - AM2_AIR_STRIKE_HALF;
        int32_t dy = orig_air_rand() % AM2_AIR_STRIKE_HALF;
        int32_t delay;
        uint32_t who;

        dy -= (int32_t)((double)dx
                        * *(const double *)AM2_IMAGE(ADDR_AIR_STRIKE_SLOPE));
        dy -= AM2_AIR_STRIKE_Y_BIAS;

        delay = orig_air_rand() % AM2_AIR_STRIKE_JITTER
                + dx * AM2_AIR_STRIKE_SLIDE + AM2_AIR_STRIKE_BASE_MS;

        who = g_airFrom[0];
        orig_spawn_at((int16_t)g_airWhere[0] + dx,
                      (int16_t)g_airWhere[1] + dy,
                      ((const int32_t *)AM2_IMAGE(ADDR_AIR_STRIKE_KINDS))
                          [(uint32_t)orig_air_rand() % AM2_AIR_STRIKE_KINDS],
                      (int32_t)UidArmy(who), who, AM2_AIR_STRIKE_EXTRA,
                      delay, 0, 0, 0);
    }

    {
        uint32_t who = g_airFrom[0];

        orig_spawn_at((int16_t)g_airWhere[0], (int16_t)g_airWhere[1],
                      ((const int32_t *)AM2_IMAGE(ADDR_AIR_STRIKE_KINDS))
                          [(uint32_t)orig_air_rand() % AM2_AIR_STRIKE_KINDS],
                      (int32_t)UidArmy(who), who, AM2_AIR_STRIKE_EXTRA,
                      AM2_AIR_STRIKE_JITTER, 0, 0, 0);
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
    int32_t ms   = *(const int32_t *)AM2_IMAGE(ADDR_AIR_PASS_MS);
    int32_t half = ms / 2;
    int32_t i;

    if (g_airPassCount <= 0)
        return;

    for (i = 0; i < g_airPassCount; i++) {
        int32_t t = g_airPassTimer[i];
        int32_t x = g_airPassWhere[i * 2]
                    - *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X;
        int32_t y = g_airPassWhere[i * 2 + 1]
                    - *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y
                    - AM2_AIR_PASS_LIFT;
        int32_t frame;

        if (t < half) {
            frame = (t * AM2_AIR_PASS_FRAMES) / half;
            y -= AM2_AIR_PASS_LIFT;
        } else {
            frame = ((t - half) * AM2_AIR_PASS_FRAMES) / half;
        }

        DrawSprite(((AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_3)[frame],
                   x, y, 0);

        g_airPassTimer[i] += *(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;
    }

    if (g_airPassTimer[0] < ms)
        return;

    if (!*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        || CommMustBroadcast(*(void *const *)(uintptr_t)ADDR_COMM_OBJECT,
                             g_airPassArmy[0]))
        AirPassStrike(*(const uint32_t *)g_airPassWhere,
                             g_airPassArmy[0]);

    for (i = 1; i < g_airPassCount; i++) {
        g_airPassTimer[i - 1]        = g_airPassTimer[i];
        g_airPassWhere[(i - 1) * 2]     = g_airPassWhere[i * 2];
        g_airPassWhere[(i - 1) * 2 + 1] = g_airPassWhere[i * 2 + 1];
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
void __cdecl FormationSlotPoint(int32_t slot, uint32_t leaderPos, void *obj,
                                uint32_t *out)
{
    int32_t  *isVehicle = (int32_t *)AM2_IMAGE(ADDR_SLOT_IS_VEHICLE);
    uint32_t *slotPos   = (uint32_t *)AM2_IMAGE(ADDR_SLOT_POSITIONS);
    uint8_t  *headings  = (uint8_t *)AM2_IMAGE(ADDR_SLOT_HEADINGS);
    const uint8_t *o = (const uint8_t *)obj;
    uint8_t   heading = 0;
    int32_t   parent, dist;
    AM2_Point at;

    if (slot >= AM2_SLOT_MAX || obj == 0)
        return;

    isVehicle[slot] = (*(const int32_t *)o == AM2_OBJ_TYPE_VEHICLE);

    if (slot == 0) {
        AM2_Point lead;

        *out = leaderPos;
        lead = *(const AM2_Point *)&leaderPos;
        heading = AngleBetween((const AM2_Point *)(o + OBJ_OFF_POS), &lead);
    } else {
        uint8_t angle;

        if (slot < 9) {
            const uint8_t *rec = (const uint8_t *)AM2_IMAGE(ADDR_SLOT_RECS)
                                 + (uint32_t)slot * AM2_SLOT_REC_BYTES;

            parent = *(const int32_t *)(rec + SLOT_REC_OFF_PARENT);
            dist   = *(const int16_t *)(rec + SLOT_REC_OFF_DIST);
            angle  = (uint8_t)(*(const uint8_t *)(rec + SLOT_REC_OFF_ANGLE)
                               + headings[parent]);
        } else {
            int32_t band, index;

            SlotBandHeading(slot, &band, &index, &angle);
            parent = index;
            dist   = band * AM2_SLOT_RING_STEP + AM2_SLOT_RING_BASE;
        }

        if (isVehicle[parent] || isVehicle[slot])
            dist += dist;

        *out = slotPos[parent];
        at = *(const AM2_Point *)out;
        /* fmul, then fiadd, then ftol -- the parent's coordinate is added
         * BEFORE the truncation, not after. The distance is rounded to float
         * once on the way in (fild then fstp dword), which is why it is cast
         * rather than left as an int in the product. */
        at.x = (int16_t)(int32_t)(Cos8(angle) * (float)dist + (float)at.x);
        at.y = (int16_t)(int32_t)(Sin8(angle) * (float)dist + (float)at.y);
        *out = *(const uint32_t *)&at;
        heading = angle;
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
 * The original hands AngleOfDelta a THIRD argument it does not read; cdecl and
 * caller-cleaned, so it is unobservable and is not reproduced.
 */
/* AngleDelta's answer with the compiler's abs() over it -- `cdq; xor; sub`,
 * which the original spells out at all four sites. */
static int32_t AbsAngle(uint32_t from, uint32_t to)
{
    int32_t d = AngleDelta(from, to);

    return d < 0 ? -d : d;
}

static void SightSilhouette(int32_t code, int32_t l, int32_t t, int32_t r,
                            int32_t b, int32_t *nx, int32_t *ny,
                            int32_t *fx, int32_t *fy)
{
    /* Generated from the byte table at 0x00403AE0 and the jump table at
     * 0x00403ABC; the eight codes not listed share the refusal exit. */
    switch (code) {
    case 0:  *nx = l; *ny = b; *fx = r; *fy = t; return;  /* left,  above  */
    case 1:  *nx = l; *ny = t; *fx = r; *fy = t; return;  /* inside above  */
    case 2:  *nx = l; *ny = b; *fx = l; *fy = t; return;  /* left,  level  */
    case 5:  *nx = l; *ny = t; *fx = r; *fy = b; return;  /* right, above  */
    case 7:  *nx = r; *ny = t; *fx = r; *fy = b; return;  /* right, level  */
    case 10: *nx = r; *ny = b; *fx = l; *fy = t; return;  /* left,  below  */
    case 11: *nx = r; *ny = b; *fx = l; *fy = b; return;  /* inside below  */
    case 15: *nx = r; *ny = t; *fx = l; *fy = b; return;  /* right, below  */
    default: return;
    }
}

void __cdecl AddSightBlocker(void *viewer, void *blocker)
{
    const uint8_t *v = (const uint8_t *)viewer;
    const uint8_t *b = (const uint8_t *)blocker;
    const uint8_t *rank;
    uint8_t       *slots = (uint8_t *)(uintptr_t)ADDR_SIGHT_BLOCK_BY_DIR;
    int32_t        heading;
    int32_t        hv;
    int32_t        hb;
    int32_t        l, t, r, bo;
    int32_t        vx, vy;
    int32_t        nx = 0, ny = 0, fx = 0, fy = 0;
    int32_t        code;
    int32_t        dist;
    int32_t        arc;
    int32_t        span;
    int32_t        steps;
    uint8_t        a0;
    uint8_t        a1;
    uint8_t        dir;

    heading = *(const uint8_t *)(v + OBJ_OFF_FACING);
    if (ObjIsType3((const AM2_Object *)v)
        && *(const int32_t *)(v + OBJ_OFF_ROW_COUNT) > 1)
        heading = *(const uint8_t *)(v + OBJ_OFF_FIELD_530);

    hb = ObjHeight(b);
    hv = ObjHeight(v);
    if (hb + AM2_SIGHT_OVERHEAD < hv)
        return;

    if (*(const void *const *)(b + OBJ_OFF_HIT_MASK) != (const void *)0) {
        /* Through the rect, as objtype.cpp reaches the same four fields:
         * only OBJ_OFF_HIT_RECT has a macro and the other three are its
         * members, which is the composed-offset case checkoffsetuse lists. */
        const AM2_Rect *hit = (const AM2_Rect *)(b + OBJ_OFF_HIT_RECT);

        l = hit->left; t = hit->top; r = hit->right; bo = hit->bottom;
    } else {
        int32_t bx = *(const int16_t *)(b + OBJ_OFF_X);
        int32_t by = *(const int16_t *)(b + OBJ_OFF_Y);

        l  = *(const int32_t *)(b + OBJ_OFF_BOX_LEFT)   + bx;
        r  = *(const int32_t *)(b + OBJ_OFF_BOX_RIGHT)  + bx;
        t  = *(const int32_t *)(b + OBJ_OFF_BOX_TOP)    + by;
        bo = *(const int32_t *)(b + OBJ_OFF_BOX_BOTTOM) + by;
    }

    vx = *(const int16_t *)(v + OBJ_OFF_X);
    vy = *(const int16_t *)(v + OBJ_OFF_Y);

    code = (l < vx ? 1 : 0) | (t < vy ? 2 : 0)
         | (r < vx ? 4 : 0) | (bo < vy ? 8 : 0);

    switch (code) {
    case 0: case 1: case 2: case 5: case 7: case 10: case 11: case 15:
        break;
    default:
        /* Seven of these cannot happen with l <= r and t <= b; the eighth,
         * code 3, is the viewer standing INSIDE the box. */
        return;
    }
    SightSilhouette(code, l, t, r, bo, &nx, &ny, &fx, &fy);

    {
        int32_t dx0 = (int16_t)(nx - vx);
        int32_t dy0 = (int16_t)(ny - vy);
        int32_t dx1 = (int16_t)(fx - vx);
        int32_t dy1 = (int16_t)(fy - vy);
        int32_t d0  = ApproxDistXY(dx0, dy0);
        int32_t d1;

        a0 = AngleOfDelta(dx0, dy0);
        d1 = ApproxDistXY(dx1, dy1);
        a1 = AngleOfDelta(dx1, dy1);
        dist = d0 > d1 ? d0 : d1;
    }

    rank = (const uint8_t *)AM2_IMAGE(ADDR_RANK_RECORDS)
         + (uint32_t)*(const int32_t *)(v + OBJ_OFF_RANK) * RANK_REC_BYTES;

    if ((int32_t)(int16_t)(dist + AM2_SIGHT_DIST_PAD)
        > *(const int32_t *)(rank + RANK_REC_OFF_SIGHT_RANGE))
        return;

    arc = *(const uint8_t *)(rank + RANK_REC_OFF_FIELD_04);

    if (AbsAngle(heading, a0) < arc) {
        if (AbsAngle(heading, a1) > arc)
            a1 = (uint8_t)(arc + heading);      /* clip the far edge */
    } else {
        if (AbsAngle(heading, a1) > arc)
            return;                             /* both outside the arc */
        a0 = (uint8_t)(heading - arc);          /* clip the near edge */
    }

    span = AbsAngle(a1, a0);
    if ((int16_t)span <= 0)
        return;

    steps = ((int32_t)(int16_t)span + 3) >> 2;
    dir   = a0;

    for (; steps > 0; steps--, dir = (uint8_t)(dir + AM2_SIGHT_DIR_STEP)) {
        uint8_t *rec = slots
                     + (uint32_t)((dir & 0xFF) >> 2) * AM2_SIGHT_DIR_STRIDE;
        int16_t  d   = (int16_t)dist;

        if (*(const int32_t *)(rec + SIGHTDIR_OFF_STAMP)
            != *(const int32_t *)(uintptr_t)ADDR_SIGHT_GENERATION) {
            int32_t range = *(const int32_t *)(rank + RANK_REC_OFF_SIGHT_RANGE);

            *(int32_t *)(rec + SIGHTDIR_OFF_STAMP) =
                *(const int32_t *)(uintptr_t)ADDR_SIGHT_GENERATION;

            if (hb > hv) {
                *(int16_t *)(rec + SIGHTDIR_OFF_LOW) = d;
                *(int16_t *)(rec + SIGHTDIR_OFF_MID) = d;
            } else if (hb + AM2_SIGHT_BAND_STEP >= hv) {
                *(int16_t *)(rec + SIGHTDIR_OFF_LOW) = (int16_t)range;
                *(int16_t *)(rec + SIGHTDIR_OFF_MID) = d;
            } else {
                *(int16_t *)(rec + SIGHTDIR_OFF_LOW) = (int16_t)range;
                *(int16_t *)(rec + SIGHTDIR_OFF_MID) = (int16_t)range;
            }
            *(int16_t *)(rec + SIGHTDIR_OFF_HIGH) = d;
            continue;
        }

        /* Already stamped this generation: keep the nearest per band. */
        if (hb > hv) {
            if (*(const int16_t *)(rec + SIGHTDIR_OFF_LOW) > d)
                *(int16_t *)(rec + SIGHTDIR_OFF_LOW) = d;
            if (*(const int16_t *)(rec + SIGHTDIR_OFF_MID) > d)
                *(int16_t *)(rec + SIGHTDIR_OFF_MID) = d;
        } else if (hb + AM2_SIGHT_BAND_STEP >= hv) {
            if (*(const int16_t *)(rec + SIGHTDIR_OFF_MID) > d)
                *(int16_t *)(rec + SIGHTDIR_OFF_MID) = d;
        }
        if (*(const int16_t *)(rec + SIGHTDIR_OFF_HIGH) > d)
            *(int16_t *)(rec + SIGHTDIR_OFF_HIGH) = d;
    }
}

#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_fogOfWar     (*(int32_t *)(uintptr_t)ADDR_FOG_OF_WAR)

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
 * The flag word is 0x8000 always, plus 0x200 when the drop is for the
 * DEFAULT OWNER and the fog of war is off. ADDR_FOG_OF_WAR is 0 when fog is
 * ON, so the test reads inverted and is not: `!= 0` there means no fog, and
 * the extra bit is what reveals the drop. Both halves of that had to be read
 * from orig.h's note on the global rather than from the compare.
 *
 * A NULL WEAPON SKIPS THE WHOLE TAIL but still leaves the trooper on the map
 * -- the guard is around the four calls, not around the creation. */
void __cdecl AirPassStrike(uint32_t at, int32_t army)
{
    const int16_t *off  = (const int16_t *)(uintptr_t)ADDR_AIR_DROP_OFFSETS;
    const uint8_t *face = (const uint8_t *)(uintptr_t)ADDR_AIR_DROP_FACINGS;
    void          *comm = *(void **)(uintptr_t)ADDR_COMM_OBJECT;
    int32_t        flags = 0;
    int32_t        i;

    if (army == (int32_t)g_defaultOwner && g_fogOfWar == 0)
        flags = AM2_AIR_DROP_REVEAL;
    flags |= AM2_AIR_DROP_FLAG;

    for (i = 0; i < AM2_AIR_DROP_COUNT; i++) {
        void *trooper;
        void *weapon;

        trooper = CreateTrooper((char *)(uintptr_t)ADDR_DIR_SCRATCH,
                                (int16_t)at + off[i * 2],
                                (int16_t)(at >> 16) + off[i * 2 + 1],
                                CommArmyOfSlot(comm, army),
                                army, flags, 0, 0, 1, 0);

        weapon = CreateWeapon((const char *)(uintptr_t)ADDR_DIR_SCRATCH, army,
                              KeyLookupTriple(AM2_AIR_DROP_WEAPON_KEY, 1, 0),
                              *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT,
                              4, -1, 0, 0);
        if (!weapon)
            continue;

        *(uint32_t *)((uint8_t *)trooper + OBJ_OFF_WEAPON_UID) =
            *(const uint32_t *)((const uint8_t *)weapon + OBJ_OFF_UID);
        *((uint8_t *)trooper + OBJ_OFF_FACING) = face[i];

        SoldierKindForWeapon(trooper,
            **(const uint32_t **)((const uint8_t *)weapon + OBJ_OFF_FIELD_C0));
        SendTrooperSetWeapon(trooper,
            *(const uint32_t *)((const uint8_t *)weapon + OBJ_OFF_UID), 0);
    }
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
}

