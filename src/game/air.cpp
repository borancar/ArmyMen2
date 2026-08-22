/* air.cpp -- see air.h. */
#include <stdint.h>

#include "air.h"
#include "rect.h"     /* AM2_Rect */
#include "item.h"     /* UidArmy -- reconstructed */
#include "objtable.h" /* AM2_Object, FirstItem, NextItem */
#include "dist.h"     /* ApproxDist -- reconstructed */
#include "objtype.h"  /* ObjIsTypeIn238 -- reconstructed */
#include "savetag.h"
#include "image.h"
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
                                        (const void *)(uintptr_t)
                                            ADDR_MEETS_ALL_THREE);

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

typedef void (__cdecl *AM2_TakeOffMapFn)(void *obj);
#define orig_take_off_map   ((AM2_TakeOffMapFn)(uintptr_t)ADDR_OBJ_TAKE_OFF_MAP)
/* Spelled exactly as event.cpp spells it, AM2_IMAGE and all, so the two
 * stay one definition. */
#define g_gameClockMs (*(const uint32_t *)AM2_IMAGE(ADDR_GAME_CLOCK_MS))

void __cdecl TakeNearbyOffMap(AM2_Point where, int32_t radius, int32_t delayMs)
{
    uint8_t *o;

    for (o = (uint8_t *)FirstItem(); o; o = (uint8_t *)NextItem()) {
        /* Types 2, 3 and 8 only; not one that is already off the map; and
         * ApproxDist -- a diamond, not a circle -- within the radius. */
        if (!ObjIsTypeIn238((const AM2_Object *)o))
            continue;
        if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_OFF_MAP)
            continue;
        if (ApproxDist(&where, (const AM2_Point *)(o + OBJ_OFF_POS))
                > radius)
            continue;

        orig_take_off_map(o);
        *(int32_t *)(o + OBJ_OFF_RETURN_AT) =
            (int32_t)g_gameClockMs + delayMs;
    }
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
    patch_replace(ADDR_TAKE_NEARBY_OFF_MAP,
                  (const void *)TakeNearbyOffMap,
                  "TakeNearbyOffMap", 2);
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
