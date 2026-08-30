/* air.cpp -- see air.h. */
#include <stdint.h>

#include "air.h"
#include "rect.h"     /* AM2_Rect, Clamp */
#include "map.h"      /* TileOfPoint -- reconstructed */
#include "item.h"     /* UidArmy -- reconstructed */
#include "objtable.h" /* AM2_Object, FirstItem, NextItem */
#include "dist.h"     /* ApproxDist, AngleDelta -- reconstructed */
#include "trig.h"     /* Cos8, Sin8 -- reconstructed */
#include "objtype.h"  /* ObjIsTypeIn238 -- reconstructed */
#include "crt.h"      /* am2_free, am2_realloc -- the game's own */
#include "savetag.h"
#include "image.h"
#include "misc.h"   /* MeetsAllThree -- reconstructed */
#include "script.h"     /* ScriptFindName -- reconstructed */
#include "scriptint.h"  /* kScriptNames */
#include "army.h"   /* ObjIsFriendly -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"
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

typedef int32_t (__cdecl *AM2_SettlePointFn)(int32_t tile, AM2_Point *pt);
typedef void    (__cdecl *AM2_FormationFarFn)(void *follower, void *leader,
                                              AM2_Point *out, int32_t slot);

#define orig_settle_point \
    ((AM2_SettlePointFn)(uintptr_t)ADDR_SETTLE_POINT_IN_REGION)
#define orig_formation_far \
    ((AM2_FormationFarFn)(uintptr_t)ADDR_FORMATION_POINT_FAR)

#define g_mapBoundsLeft   (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT))
#define g_mapBoundsTop    (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_TOP))
#define g_mapBoundsRight  (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_RIGHT))
#define g_mapBoundsBottom (*(const int32_t *)AM2_IMAGE(ADDR_MAP_BOUNDS_BOTTOM))

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
        orig_formation_far(follower, leader, out, slot);
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

    orig_settle_point(TileOfPoint(*(const uint32_t *)out), out);
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

void air_install(void)
{
    patch_replace(ADDR_FORMATION_POINT, (const void *)FormationPoint,
                  "FormationPoint", 4);
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
    patch_replace(ADDR_AIR_BEGIN, (const void *)AirSupportBegin,
                  "AirSupportBegin", 2);
    patch_replace(ADDR_AIR_CLEAR, (const void *)AirSupportClear,
                  "AirSupportClear", 1);
    patch_replace(ADDR_AIR_POP, (const void *)AirSupportPop,
                  "AirSupportPop", 2);
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
