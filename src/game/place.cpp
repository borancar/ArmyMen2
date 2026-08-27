/* place.cpp -- see place.h. */
#include <stdint.h>

#include "place.h"
#include "image.h"
#include "definfo.h"  /* DefParseInfoFile */
#include "gamedir.h"  /* SetGameDir */
#include "crt.h"       /* the game's allocator -- this table is its memory */
#include "../inject/orig.h"
#include "../inject/patch.h"

#define g_placements     (*(AM2_Placement **)AM2_IMAGE(ADDR_PLACEMENTS))
#define g_placementCount (*(int32_t *)AM2_IMAGE(ADDR_PLACEMENT_COUNT))
#define g_placementCap   (*(int32_t *)AM2_IMAGE(ADDR_PLACEMENT_CAP))
#define g_gameSetting22C (*(uint32_t *)(uintptr_t)ADDR_GAME_SETTING_22C)
#define g_armyPoints     ((const int32_t *)(uintptr_t)ADDR_ARMY_POINTS)
#define g_commObject   (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
/* Spelled as script.cpp spells them, which is the file already reaching
 * these two -- g_mapName is taken, on a DIFFERENT address. */
#define kMapName      ((const char *)AM2_IMAGE(ADDR_MAP_NAME))
#define kMapFolder    ((const char *)AM2_IMAGE(ADDR_MAP_FOLDER))

#define kUnitType(i) ((const uint8_t *)AM2_IMAGE(ADDR_UNIT_TYPES) \
                      + (size_t)(i) * AM2_UNIT_TYPE_STRIDE)

/* Still original, and both reached by address. They are shared with the manual
 * placement screen at 0x00413BC0, so they are a live layer under this one
 * rather than something waiting on it. */
typedef int32_t (__cdecl *AM2_PlaceAllowedFn)(uint32_t where, int32_t type,
                                              int32_t slot, int32_t points,
                                              int32_t facing);
typedef void (__cdecl *AM2_MakePlacedFn)(uint32_t where, int32_t type,
                                         int32_t slot, int32_t *points,
                                         int32_t facing, int32_t group,
                                         const char *name);

#define orig_place_allowed   (*(AM2_PlaceAllowedFn)AM2_IMAGE(ADDR_PLACEMENT_ALLOWED))
#define orig_make_placed     (*(AM2_MakePlacedFn)AM2_IMAGE(ADDR_MAKE_PLACED_UNIT))

/* 0x0043B3D0. The three globals are cleared in the original's order --
 * pointer, capacity, count -- which nothing can observe, and reproduced
 * because nothing can observe it either way. */
void __cdecl FreePlacements(void)
{
    if (!g_placements)
        return;

    am2_free(g_placements);
    g_placements     = (AM2_Placement *)0;
    g_placementCap   = 0;
    g_placementCount = 0;
}

/* 0x0043B410. First record allocates room for 32; after that the table grows
 * eight at a time, which for a file of ninety units is nine reallocs. Neither
 * the malloc nor the realloc is checked, and that is the original's. */
void __cdecl AddPlacement(const AM2_Placement *rec)
{
    int32_t cap;

    if (!g_placements) {
        g_placements   = (AM2_Placement *)am2_malloc(32 * sizeof(AM2_Placement));
        cap            = 32;
        g_placementCap = cap;
    } else {
        cap = g_placementCap;
    }

    if (g_placementCount >= cap) {
        cap            += 8;
        g_placementCap  = cap;
        g_placements    = (AM2_Placement *)am2_realloc(
            g_placements, (size_t)cap * sizeof(AM2_Placement));
    }

    g_placements[g_placementCount] = *rec;
    g_placementCount++;
}

/* 0x0043A690. The mask test comes FIRST and returns 0 on its own, so a type
 * this game type does not allow is refused however many points are left. */
int32_t __cdecl CanAffordUnit(int32_t type, int32_t points)
{
    const uint8_t *rec = kUnitType(type);

    if (!(*(const uint32_t *)(rec + UNIT_TYPE_OFF_GAME_MASK) & g_gameSetting22C))
        return 0;

    return points >= *(const int32_t *)(rec + UNIT_TYPE_OFF_COST);
}

/* 0x0043A560. The colour index is the comm slot's own army field, read inline
 * -- see ADDR_PLACEMENT_PATH for why that is not CommArmyOfSlot. */
char *__cdecl BuildPlacementPath(char *dest, int32_t slot)
{
    const char *colour = (const char *)0;
    int32_t     army   = *(const int32_t *)(g_commObject
                                            + (size_t)slot * AM2_PLAYER_STRIDE
                                            + COMM_ARMY_OFF_COLOUR);

    switch (army) {
    case 0: colour = (const char *)AM2_IMAGE(ADDR_STR_GREEN); break;
    case 1: colour = (const char *)AM2_IMAGE(ADDR_STR_TAN);   break;
    case 2: colour = (const char *)AM2_IMAGE(ADDR_STR_BLUE);  break;
    case 3: colour = (const char *)AM2_IMAGE(ADDR_STR_GREY);  break;
    default: break;
    }

    am2_sprintf(dest, (const char *)AM2_IMAGE(ADDR_FMT_PLACE_FILE),
                kMapName, colour);
    return dest;
}

/* 0x0043B700. Read one slot's placement file and lay its units out.
 *
 * Three things the loop does that reading it once does not suggest. The count
 * AND the table base are re-read every iteration, because the creator below
 * may append -- so this is not a cached `end` walk. The points budget is a
 * LOCAL copy of the slot's entry, handed to the creator by address and spent
 * down as units go in, so the global is never written. And a file that will
 * not parse is only complained about: the walk runs anyway, over whatever the
 * previous army left behind, which FreePlacements has just emptied.
 */
void __cdecl LoadArmyPlacement(int32_t slot)
{
    char    path[0x38];
    int32_t points;
    int32_t i;

    FreePlacements();
    SetGameDir(kMapFolder);
    BuildPlacementPath(path, slot);
    if (!DefParseInfoFile(path))
        am2_log((const char *)AM2_IMAGE(ADDR_FMT_COULDNT_PARSE), path);

    points = g_armyPoints[slot];

    for (i = 0; i < g_placementCount; i++) {
        const AM2_Placement *p = &g_placements[i];

        if (orig_place_allowed(p->where, p->type, slot, points, p->facing))
            orig_make_placed(p->where, p->type, slot, &points, p->facing,
                             p->group, p->name);
    }
}

int place_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_FREE_PLACEMENTS, (const void *)FreePlacements,
                        "FreePlacements", 1);
    rc |= patch_replace(ADDR_ADD_PLACEMENT, (const void *)AddPlacement,
                        "AddPlacement", 1);
    rc |= patch_replace(ADDR_CAN_AFFORD_UNIT, (const void *)CanAffordUnit,
                        "CanAffordUnit", 3);
    rc |= patch_replace(ADDR_PLACEMENT_PATH, (const void *)BuildPlacementPath,
                        "BuildPlacementPath", 1);
    rc |= patch_replace(ADDR_LOAD_ARMY_PLACEMENT, (const void *)LoadArmyPlacement,
                        "LoadArmyPlacement", 1);
    return rc;
}
