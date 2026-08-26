/* air.cpp -- see air.h. */
#include <stdint.h>

#include "air.h"
#include "rect.h"     /* AM2_Rect */
#include "item.h"     /* UidArmy -- reconstructed */
#include "objtable.h" /* AM2_Object, FirstItem, NextItem */
#include "dist.h"     /* ApproxDist -- reconstructed */
#include "objtype.h"  /* ObjIsTypeIn238 -- reconstructed */
#include "crt.h"      /* am2_free, am2_realloc -- the game's own */
#include "savetag.h"
#include "image.h"
#include "misc.h"   /* MeetsAllThree -- reconstructed */
#include "../inject/orig.h"
#include "../inject/patch.h"

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

typedef void *(__cdecl *AM2_ObjectsInRectFn)(const AM2_Rect *r, void *table,
                                             const void *pred);
#define orig_objects_in_rect \
    ((AM2_ObjectsInRectFn)(uintptr_t)ADDR_OBJECTS_IN_RECT)

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

    o = (uint8_t *)orig_objects_in_rect(&box,
                                        (void *)(uintptr_t)ADDR_OBJ_TABLE_ARG,
                                        (const void *)MeetsAllThree);

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

typedef void (__cdecl *AM2_RowUpdateFn)(void *row, int32_t a, void *desc);
#define orig_row_update \
    ((AM2_RowUpdateFn)(uintptr_t)ADDR_ROW_UPDATE)

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
        orig_row_update(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }
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

typedef void (__cdecl *AM2_FormationPointFn)(void *follower, void *leader,
                                             AM2_Point *out, int32_t slot);
#define orig_formation_point \
    ((AM2_FormationPointFn)(uintptr_t)ADDR_FORMATION_POINT)

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
 * The placement itself stays original and is reached by address; see
 * ADDR_FORMATION_POINT for the twelve-slot table it indexes. */
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

    orig_formation_point(follower, leader, out,
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

void air_install(void)
{
    patch_replace(ADDR_RESOLVE_FORMATION_POINT,
                  (const void *)ResolveFormationPoint,
                  "ResolveFormationPoint", 3);
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
}
