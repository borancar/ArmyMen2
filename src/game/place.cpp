/* place.cpp -- see place.h. */
#include <stdint.h>
#include <string.h>

#include "place.h"
#include "image.h"
#include "definfo.h"  /* DefParseInfoFile */
#include "packkey.h"  /* PackKey -- reconstructed */
#include "misc.h"     /* CommArmyOfSlot -- reconstructed */
#include "objtable.h"  /* AM2_Object */
#include "objtype.h"   /* ObjIsItem, ObjIsTypeIn238 -- reconstructed */
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

#define kSep         ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))

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

/* 0x0043B490. One `place` line: a unit type by name, x, y, a facing, a group
 * and a name, into the record AddPlacement copies.
 *
 * THE LAST TOKEN RESTARTS THE TOKENISER, and that is the original's. Every
 * other field continues with strtok(NULL); the name passes `line` again, so
 * strtok begins the line afresh and hands back the FIRST token -- the type.
 * Every one of the 1,264 lines the game ships therefore ends up with its type
 * name in the name field, and the `-` the files actually write in that column
 * is never seen. Reproduced, not corrected.
 *
 * Measured rather than argued: reading this I first wrote it up as an
 * ordinary sixth field, because `push ebx` next to a strtok looks like every
 * other continuation until you notice that the continuations push 0.
 * tools/placecheck.py runs the original over the whole corpus and every
 * recorded name is a unit type. See tests/placevec.h.
 *
 * SAY WHAT THE CORPUS DOES NOT CATCH. The three mutations that matter all
 * fail all 1,264 lines -- continuing the tokeniser instead of restarting it,
 * swapping x and y, and moving the facing by one. Deleting the `-` test
 * PASSES every line, and that is not a gap in the corpus so much as a
 * consequence of the defect above: the token can never be `-`, so the test
 * can never fire. It stays because the original has it.
 *
 * Two more things left as they are. The type name and the final name are
 * copied into 0x20-byte buffers with no length check, so a long enough token
 * runs into the record; nothing the game ships comes close. And the
 * not-found test after the type search compares the index against -1, which
 * it cannot be -- the search either matches or falls out of its own loop with
 * a return. That is an inlined lookup's `== -1` left behind.
 */
int32_t __cdecl ParsePlaceLine(int32_t cmd, char *line)
{
    AM2_Placement rec;
    char          typeName[0x20];
    char         *tok;
    int32_t       type;
    int32_t       x, y, facing;

    (void)cmd;

    tok = am2_strtok(line, kSep);
    if (!tok)
        return 2;
    strcpy(typeName, tok);

    for (type = 0; type < AM2_UNIT_TYPE_COUNT; type++)
        if (!strcmp(typeName, (const char *)(kUnitType(type)
                                             + UNIT_TYPE_OFF_NAME)))
            break;
    if (type == AM2_UNIT_TYPE_COUNT)
        return 2;
    if (type == -1)                       /* cannot happen -- see above */
        return 2;
    rec.type = type;

    tok = am2_strtok((char *)0, kSep);
    if (!tok || !DefParseNumber(&x, tok))
        return 3;

    tok = am2_strtok((char *)0, kSep);
    if (!tok || !DefParseNumber(&y, tok))
        return 4;

    rec.where = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;

    tok = am2_strtok((char *)0, kSep);
    if (!tok || !DefParseNumber(&facing, tok))
        return 5;
    rec.facing = (uint8_t)facing;

    tok = am2_strtok((char *)0, kSep);
    if (!tok || !DefParseNumber(&rec.group, tok))
        return 6;

    tok = am2_strtok(line, kSep);        /* NOT (char *)0 -- see above */
    if (!tok)
        return 7;
    strcpy(rec.name, tok);
    if (!strcmp(rec.name, (const char *)AM2_IMAGE(ADDR_STR_PLACE_NO_NAME)))
        rec.name[0] = '\0';

    AddPlacement(&rec);
    return 0;
}

/* SpriteKeyForKind -- original 0x0043A5F0, one caller.
 *
 * A packed sprite key for a selector in 0..7. Five of the eight arms call
 * PackKey with a set id of their own and their own arithmetic on `n`; one
 * answers a global outright; anything above 7 answers 0.
 *
 * SELECTORS 0, 1 AND 2 SHARE ONE ARM. The jump table has eight slots and only
 * six distinct targets, with the first three pointing at the same code -- so
 * counting the bodies gives six arms where the switch has eight cases. That is
 * the same trap the state-2 sub-state table set and WeaponClassOf's jump table
 * set again: read the TABLE, not the bodies.
 *
 * THE BOUND IS UNSIGNED, so a negative selector is refused by the same test
 * that refuses 8 and above rather than falling through to an arm.
 *
 * The five arithmetics on `n` are all different -- n+1 three times over three
 * different set ids, n+2 once, and 10*(n+1) once -- so nothing here collapses
 * into a table of set ids. Written as the switch it is.
 *
 * Arm 7 ignores `n` entirely and answers ADDR_CREATE_WATCHED_KIND, which is a
 * global something else writes; it is not a packed key at all, so the return
 * type is what the two kinds of answer have in common and nothing more.
 */
int32_t __cdecl SpriteKeyForKind(int32_t sel, int32_t n)
{
    switch ((uint32_t)sel) {
    case 0:
    case 1:
    case 2:  return (int32_t)PackKey(0x26, (uint32_t)(n + 1), 0);
    case 3:  return (int32_t)PackKey(0x20, (uint32_t)(n + 1), 0);
    case 4:  return (int32_t)PackKey(0x21, (uint32_t)(n + 1), 0);
    case 5:  return (int32_t)PackKey(0x2A, (uint32_t)(n + 2), 0);
    case 6:  return (int32_t)PackKey(0x1F, (uint32_t)((n + 1) * 10), 0);
    case 7:  return *(const int32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND;
    default: return 0;
    }
}

typedef int32_t (__cdecl *AM2_UnitKindMatchesFn)(int32_t code, int32_t kind,
                                                 int32_t slot);
#define orig_unit_kind_matches \
    ((AM2_UnitKindMatchesFn)(uintptr_t)ADDR_UNIT_KIND_MATCHES)

/* IsPlacedUnit -- original 0x0043B0A0, one caller, which is the manual
 * placement screen at 0x00413BC0. Does this object count as one of that army's
 * placed units? The name is ours, from the caller and from what each arm
 * accepts.
 *
 * FOUR ANSWERS AND ONLY ONE OF THEM DOES ANY WORK. A VEHICLE (type 3) counts
 * outright. A TROOPER (type 2) counts unless it is OBJ_OFF_SARGE or carries
 * anything at OBJ_OFF_FIELD_94 -- so the squad leader is not a placed unit,
 * which is exactly right for a screen where you lay your squad out and Sarge
 * is always there. Anything that is neither an item nor one of types 2, 3 and
 * 8 answers 0 before the type is even looked at.
 *
 * AN ITEM (type 1) IS THE ONE THAT SEARCHES. It walks all eighteen
 * ADDR_UNIT_TYPES records, skips every one that is a trooper or a vehicle, and
 * asks ADDR_UNIT_KIND_MATCHES whether that kind claims this item; ANY yes
 * makes the answer 1. It does not stop at the first -- the flag is set and the
 * loop runs on -- which changes nothing and is written out as it stands.
 *
 * THE SLOT IT PASSES IS AN ARMY. ADDR_COMM_ARMY_OF_SLOT is prototyped
 * `(this, slot)` and this hands it the army argument, which CLAUDE.md records
 * as the identity for every army a script can write -- the comm slots hold
 * armies 0..3 in order. So the two coincide here rather than one being wrong,
 * and that is worth saying because the call reads like a mistake.
 *
 * The first gate accepts types 2, 3 and 8 OR an item, and the switch below
 * then has no arm for type 8 -- so a roach reaches the switch and falls out of
 * it with 0. Two tests that could have been one, kept apart because that is
 * how the original reads.
 */
int32_t __cdecl IsPlacedUnit(void *obj, int32_t army)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  found = 0;
    int32_t  slot;
    const uint8_t *rec;

    if (!ObjIsTypeIn238((const AM2_Object *)obj)
        && !ObjIsItem((const AM2_Object *)obj))
        return 0;

    if (*(const int8_t *)(o + OBJ_OFF_ARMY) != army)
        return 0;

    switch (*(const int32_t *)o) {
    case 2:
        if (*(const int32_t *)(o + OBJ_OFF_SARGE))
            return 0;
        if (*(const int32_t *)(o + OBJ_OFF_FIELD_94))
            return 0;
        return 1;

    case 3:
        return 1;

    case 1:
        break;

    default:
        return 0;
    }

    slot = CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT, army);

    for (rec = (const uint8_t *)AM2_IMAGE(ADDR_UNIT_TYPES);
         rec < (const uint8_t *)AM2_IMAGE(ADDR_UNIT_TYPES)
               + AM2_UNIT_TYPE_COUNT * AM2_UNIT_TYPE_STRIDE;
         rec += AM2_UNIT_TYPE_STRIDE) {
        if (*(const int32_t *)(rec + UNIT_TYPE_OFF_TROOPER)
            || *(const int32_t *)(rec + UNIT_TYPE_OFF_VEHICLE))
            continue;

        if (orig_unit_kind_matches(
                *(const int32_t *)(*(const uint8_t *const *)
                                       (o + OBJ_OFF_FIELD_94)
                                   + TYPEREC_OFF_FIELD_08),
                *(const int32_t *)(rec + UNIT_TYPE_OFF_KIND), slot))
            found = 1;
    }

    return found;
}

int place_install(void)
{
    patch_replace(ADDR_IS_PLACED_UNIT, (const void *)IsPlacedUnit,
                  "IsPlacedUnit", 1);
    int rc = 0;

    rc |= patch_replace(ADDR_SPRITE_KEY_FOR_KIND,
                        (const void *)SpriteKeyForKind,
                        "SpriteKeyForKind", 1);

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
    rc |= patch_replace(ADDR_PARSE_PLACE_LINE, (const void *)ParsePlaceLine,
                        "ParsePlaceLine", 0);
    return rc;
}
