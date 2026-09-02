/* place.cpp -- see place.h. */
#include <stdint.h>
#include <string.h>

#include "place.h"
#include "image.h"
#include "definfo.h"  /* DefParseInfoFile */
#include "packkey.h"  /* PackKey -- reconstructed */
#include "misc.h"     /* CommArmyOfSlot -- reconstructed */
#include "item.h"     /* BlockWeightAt, MaskBlockWeight -- reconstructed */
#include "region.h"   /* CanPlaceAt -- reconstructed */
#include "map.h"      /* TileOfPoint -- reconstructed */
#include "objtable.h"  /* AM2_Object */
#include "objtype.h"   /* ObjIsItem, ObjIsTypeIn238 -- reconstructed */
#include "gamedir.h"  /* SetGameDir */
#include "crt.h"       /* the game's allocator -- this table is its memory */
#include "armymsg.h"   /* SendTrooperSetWeapon -- reconstructed */
#include "defparse.h"  /* AM2_DefLink, DefFindLink -- reconstructed */
#include "maprow.h"    /* RowUpdate -- reconstructed */
#include "army.h"      /* AllyFlag -- reconstructed */
#include "air.h"       /* ObjConceal -- reconstructed */
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

        if (PlacementAllowed(p->where, p->type, slot, points, p->facing))
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

/* UnitKindMatches -- original 0x0043AAB0, TWO callers, and the membership
 * half of the function directly above.
 *
 * READ IT BESIDE SpriteKeyForKind AND ALMOST NOTHING IS NEW. Same eight-slot
 * jump table, same unsigned bound, same three selectors sharing arm zero, and
 * the same six set ids in the same order -- 0x26, 0x20, 0x21, 0x2A, 0x1F and
 * the watched-kind global. The FIRST test of every arm is literally
 * SpriteKeyForKind(kind, n): n+1 for the first three sets, n+2 for 0x2A,
 * (n+1)*10 for 0x1F. So this asks "is `code` one of the keys kind `kind` uses
 * at `n`", and its first candidate is the one key the sibling answers with.
 * orig.h said the name came from the call site and the body was unread; the
 * body agrees with the name, which is not the usual outcome.
 *
 * WHAT IS NEW IS THE EXTRA CANDIDATES, and they do not follow one rule:
 *
 *   kinds 0, 1, 2 and kind 4   n+1, (n+1)*10, n*10+11, n*10+12
 *   kind 3                     n+1, (n+1)*10, n*10+11
 *   kind 5                     n+2, then the constant 1, then a DIFFERENT
 *                              SET -- 0x2B rather than 0x2A -- at n+0x3D5
 *   kinds 6 and 7              exactly what the sibling answers, nothing more
 *
 * KIND 3 IS ONE TEST SHORT AND A LINEAR READ MISSES IT. Its third arm ends in
 * `jmp` into kind 4's fourth test, so the two share one epilogue -- the code
 * for `n*10+12` sits inside kind 4's arm and kind 3 never reaches it. Reading
 * the bodies top to bottom gives kind 3 four candidates. It has three. Same
 * lesson as the jump table itself, one level in: follow the branch, do not
 * count the layout.
 *
 * KIND 5's THIRD CANDIDATE IS NEAR THE TOP OF ITS FIELD. PackKey's B field is
 * ten bits and every reader masks 0x3FF, so n + 0x3D5 = 981 + n stays inside
 * it for n in 0..3 and would truncate above 42. The one caller in this tree
 * passes an ARMY, which is 0..3, so nothing here reaches that -- see
 * packkey.h, which records the same limit as a latent one.
 *
 * THE ARMS DISAGREE ABOUT WHICH REGISTER HOLDS WHAT -- kind 5 keeps the code
 * in esi and the slot in edi where every other arm has them the other way
 * round -- which is register allocation and not a difference. Written as
 * values.
 */
int32_t __cdecl UnitKindMatches(int32_t code, int32_t kind, int32_t n)
{
    switch ((uint32_t)kind) {
    case 0:
    case 1:
    case 2:
        if (code == (int32_t)PackKey(0x26, (uint32_t)(n + 1), 0))
            return 1;
        if (code == (int32_t)PackKey(0x26, (uint32_t)((n + 1) * 10), 0))
            return 1;
        if (code == (int32_t)PackKey(0x26, (uint32_t)(n * 10 + 11), 0))
            return 1;
        return code == (int32_t)PackKey(0x26, (uint32_t)(n * 10 + 12), 0)
               ? 1 : 0;

    case 3:
        if (code == (int32_t)PackKey(0x20, (uint32_t)(n + 1), 0))
            return 1;
        if (code == (int32_t)PackKey(0x20, (uint32_t)((n + 1) * 10), 0))
            return 1;
        /* and NOT n*10+12 -- see the note above. */
        return code == (int32_t)PackKey(0x20, (uint32_t)(n * 10 + 11), 0)
               ? 1 : 0;

    case 4:
        if (code == (int32_t)PackKey(0x21, (uint32_t)(n + 1), 0))
            return 1;
        if (code == (int32_t)PackKey(0x21, (uint32_t)((n + 1) * 10), 0))
            return 1;
        if (code == (int32_t)PackKey(0x21, (uint32_t)(n * 10 + 11), 0))
            return 1;
        return code == (int32_t)PackKey(0x21, (uint32_t)(n * 10 + 12), 0)
               ? 1 : 0;

    case 5:
        if (code == (int32_t)PackKey(0x2A, (uint32_t)(n + 2), 0))
            return 1;
        if (code == (int32_t)PackKey(0x2A, 1, 0))
            return 1;
        return code == (int32_t)PackKey(0x2B, (uint32_t)(n + 0x3D5), 0)
               ? 1 : 0;

    case 6:
        return code == (int32_t)PackKey(0x1F, (uint32_t)((n + 1) * 10), 0)
               ? 1 : 0;

    case 7:
        return code == *(const int32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND
               ? 1 : 0;

    default:
        return 0;
    }
}


/* PlacementAllowed -- original 0x0043A810, two callers: the `place` line
 * parser and the manual placement screen. May this unit type go down at this
 * point, for this comm slot, on this budget? The name is ours and predates
 * the reading; the body agrees with it.
 *
 * AN EIGHTEEN-WAY SWITCH, ONE ARM PER ADDR_UNIT_TYPES RECORD, and it is
 * unreadable until that table is dumped. See orig.h: 0..4 are the soldiers,
 * 5..9 the vehicles, 10..16 the buildings, 17 the mine. Then:
 *
 *   THE VEHICLE ARMS ARE NOT SCRAMBLED. They pass MaskBlockWeight kinds 1, 0,
 *   2, 3, 5 in table order, which reads exactly like WeaponClassOf's jump
 *   table laying its arms out in one order and dispatching them in another --
 *   and is not. Each arm passes ITS OWN RECORD'S +0x08: tank 1, jeep 0,
 *   halftrack 2, truck 3, ptboat 5. The apparent scramble is the table's.
 *
 *   THE BUILDING ARMS ARE THE THIRD MEMBER OF A FAMILY. Arm 10+k uses the
 *   same set id SpriteKeyForKind and UnitKindMatches use for kind k -- 0x26
 *   for 0, 1 and 2, 0x20 for 3, 0x21 for 4, 0x2A for 5, 0x1F for 6 -- and the
 *   same (n+1)*10, n*10+11 and n*10+12 arithmetic. It picks a SUBSET of
 *   UnitKindMatches' candidates: the two extras for kinds 3 and 4, the bare
 *   `1` for kind 5, the only one there is for kind 6.
 *
 *   AND THAT TRIO'S THREE-WAY SHARE IS EXPLAINED HERE. Kinds 0, 1 and 2 are
 *   riflepill, bazookapill and mgpill, all on set 0x26. The jump tables show
 *   the share; the record table says what it is.
 *
 * ARM 16 ENDS INSIDE ARM 15 -- it pushes its three arguments and `jmp`s into
 * the middle of the radar arm to make the call and run its tail. Second
 * instance today, after UnitKindMatches' kind 3, and the same lesson: read
 * the branch, not the layout.
 *
 * THE `kind` IT HANDS CanPlaceAt IS AN ARMY, and this function is what
 * settles what ADDR_TILE_KIND's bytes mean -- `(army + 1) * 0x10`, computed
 * once and used twice: as CanPlaceAt's third argument and again on the
 * placement's own tile in the final test. See orig.h. Everything else in the
 * function can pass and that last comparison still refuses, so the tile the
 * cursor is over has to belong to you.
 *
 * ONE BRANCH IS NOT REPRODUCED AND CANNOT FIRE. `cmp ax, 0xFFFF` followed by
 * `ja` is taken only when a sixteen-bit value exceeds 0xFFFF. The bytes are
 * `66 3d ff ff / 0f 87`, so it is `ja` and not `jae` or `je`; VC6 emitted a
 * comparison it could have folded. Writing it out would only draw a
 * compiler warning saying the same thing. Same treatment as
 * UpdateMouseState's unreachable `je`, and the comment is the record.
 *
 * THE WEIGHT CHECK IS SHARED AND HALF THE ARMS JUMP PAST IT. Arms 0..9, 13,
 * 14 and 17 reach `cmp ebx, 0xF`; the rest go straight to the tile test. It
 * is written here as one test after the switch, which is the same outcome:
 * those arms leave the accumulator at its initial zero and zero is never
 * AM2_BLOCK_FULL.
 *
 * CommArmyOfSlot IS CALLED TWICE on every arm that needs the army -- once for
 * the tile kind and once inside the arm. One local here; the two calls cannot
 * disagree.
 */
int32_t __cdecl PlacementAllowed(uint32_t where, int32_t type, int32_t slot,
                                 int32_t pts, int32_t facing)
{
    void    *comm = *(void **)(uintptr_t)ADDR_COMM_OBJECT;
    int32_t  tile;
    int32_t  army;
    int32_t  kind;
    int32_t  weight = 0;
    int32_t  rec;

    if (!CanAffordUnit(type, pts))
        return 0;

    tile = TileOfPoint(where);
    /* `cmp ax, 0xFFFF; ja` sits here and cannot be taken -- see above. */

    army = CommArmyOfSlot(comm, slot);
    kind = (army + 1) * AM2_TILE_KIND_ARMY_STEP;

    switch ((uint32_t)type) {
    /* The five soldiers, and the mine. */
    case 0: case 1: case 2: case 3: case 4: case 17:
        weight = BlockWeightAt((void *)0, where, where);
        break;

    /* The five vehicles, each passing its record's own kind. */
    case 5: weight = MaskBlockWeight(1, facing, where); break;
    case 6: weight = MaskBlockWeight(0, facing, where); break;
    case 7: weight = MaskBlockWeight(2, facing, where); break;
    case 8: weight = MaskBlockWeight(3, facing, where); break;
    case 9: weight = MaskBlockWeight(5, facing, where); break;

    /* riflepill, bazookapill, mgpill -- one set between them. */
    case 10: case 11: case 12:
        rec = EnsureSpriteAaiRecord(0x26, (army + 1) * 10, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            return 0;
        break;

    case 13:  /* medtent */
        rec = EnsureSpriteAaiRecord(0x20, (army + 1) * 10, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            weight = AM2_BLOCK_FULL;
        rec = EnsureSpriteAaiRecord(0x20, army * 10 + 11, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            weight += AM2_BLOCK_FULL;
        break;

    case 14:  /* garage */
        rec = EnsureSpriteAaiRecord(0x21, (army + 1) * 10, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            weight = AM2_BLOCK_FULL;
        rec = EnsureSpriteAaiRecord(0x21, army * 10 + 12, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            weight += AM2_BLOCK_FULL;
        break;

    case 15:  /* radar */
        rec = EnsureSpriteAaiRecord(0x2A, 1, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            return 0;
        break;

    case 16:  /* aagun */
        rec = EnsureSpriteAaiRecord(0x1F, (army + 1) * 10, 0);
        if (rec != -1 && !CanPlaceAt(where, rec, kind))
            return 0;
        break;

    default:
        break;
    }

    if (weight >= AM2_BLOCK_FULL)
        return 0;

    return ((const uint8_t *)*(void *const *)(uintptr_t)ADDR_TILE_KIND)
               [(uint32_t)tile & 0xFFFFu] == (uint32_t)kind ? 1 : 0;
}

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

        if (UnitKindMatches(
                *(const int32_t *)(*(const uint8_t *const *)
                                       (o + OBJ_OFF_FIELD_94)
                                   + TYPEREC_OFF_FIELD_08),
                *(const int32_t *)(rec + UNIT_TYPE_OFF_KIND), slot))
            found = 1;
    }

    return found;
}

/* MakePlacedUnit -- original 0x0043ACF0, two callers: the manual placement
 * screen's click handler and the `place` line loader. The last thing in this
 * module that was reached by address, so with it the placement subsystem is
 * entirely ours.
 *
 * IT CHARGES FIRST AND ASKS QUESTIONS AFTER. The cost comes off the caller's
 * points before anything is looked at, and nothing below can refuse -- so a
 * type this function does not recognise still costs its money and makes
 * nothing. Reproduced.
 *
 * THREE CLASSES, TESTED IN ORDER, off the unit-type record: a TROOPER if
 * UNIT_TYPE_OFF_TROOPER is set, a VEHICLE if UNIT_TYPE_OFF_VEHICLE is, and
 * otherwise an ITEM -- which is where the buildings live, and where the arms
 * multiply.
 *
 * A TROOPER COMES WITH A WEAPON. CreateTrooper, then CreateWeapon with a key
 * looked up from the record's kind, then three steps that tie them together:
 * the weapon's uid into +0x54C, SoldierKindForWeapon off the weapon's code,
 * and SendTrooperSetWeapon so the other players hear about it.
 *
 * A VEHICLE REGISTERS ITS EXTRA ROWS BY HAND. Rows past the first are placed
 * at the object's position plus that row's own attach offsets and handed to
 * RowUpdate one at a time -- the loop steps 0x60 per row, which is the row
 * stride, and starts at row 1 because row 0 is already registered.
 *
 * A PILLBOX IS THREE OBJECTS. UNIT_TYPE_OFF_KIND 0..2 makes the building, then
 * a TROOPER to man it and a WEAPON for him -- and the weapon kind is 9, 4 or 8
 * by which pillbox it is, computed with a `dec`/`neg`/`sbb`/`and 4` chain
 * rather than a table. The occupant gets OBJ_OFF_RANK 4 and a health of its
 * own from ADDR_PILLBOX_TROOPER_HEALTH rather than the rank record's, which is
 * why RefundPlacedUnit has to find him by uid arithmetic rather than by asking
 * the building.
 *
 * AND THE SECOND ARGUMENT OF CreateWeapon IS AN ARMY. item.cpp passed
 * AM2_OBJ_TYPE_WEAPON there and this passes a comm slot; the callee hands it
 * to CommMustBroadcast, which takes an army, so this one is right and that one
 * was a constant named for the wrong concept -- invisible because army 4, the
 * neutral one, and object type 4 share a value. Corrected at that call site.
 *
 * KIND 7 IS REACHED BY ELIMINATION and gets neither an occupant nor a weapon:
 * ItemPostCreate and, when the placing army is not allied to ours, a conceal.
 * What kind 7 IS is not established here.
 *
 * The dead `mov ecx, ADDR_COMM_OBJECT` in front of the AllyFlag call is the
 * original's -- AllyFlag is stdcall and never reads ecx. Not reproduced,
 * because a register load with no effect has nothing to reproduce. */
void __cdecl MakePlacedUnit(uint32_t where, int32_t type, int32_t slot,
                            int32_t *points, int32_t facing, int32_t group,
                            const char *name)
{
    const uint8_t *rec = (const uint8_t *)AM2_IMAGE(ADDR_UNIT_TYPES)
                         + (uintptr_t)type * AM2_UNIT_TYPE_STRIDE;
    const int32_t  kind = *(const int32_t *)(rec + UNIT_TYPE_OFF_KIND);
    uint8_t       *made;
    uint8_t       *weapon;
    uint8_t       *rows;

    *points -= *(const int32_t *)(rec + UNIT_TYPE_OFF_COST);

    if (*(const int32_t *)(rec + UNIT_TYPE_OFF_TROOPER)) {
        made = (uint8_t *)CreateTrooper(
                   (char *)name, (int16_t)where, (int16_t)(where >> 16), slot,
                   CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT, slot),
                   0, 0, 0, 1, facing);
        ObjSetFieldA(made, (uint32_t)group);

        weapon = (uint8_t *)CreateWeapon(
                     (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH), slot,
                     KeyLookupTriple(AM2_WEAPON_KEY_KIND, (uint32_t)kind, 0),
                     *(const uint32_t *)AM2_IMAGE(ADDR_ZERO_POINT),
                     AM2_OBJ_TYPE_WEAPON, -1, 0, 0);

        if (weapon) {
            *(int32_t *)(made + TROOPER_OFF_WEAPON_UID) =
                *(const int32_t *)(weapon + 4);
            SoldierKindForWeapon(made,
                **(const uint32_t *const *)(weapon + OBJ_OFF_FIELD_C0));
            SendTrooperSetWeapon(made, *(const uint32_t *)(weapon + 4), 0);
        }

        rows = *(uint8_t **)(made + OBJ_OFF_ROWS);
        *(uint8_t *)(rows + ROW_OFF_HEADING) =
            *(const uint8_t *)(made + OBJ_OFF_FACING);
        StepObjRows(made);
        ObjTileChanged(made, *(const int8_t *)(made + OBJ_OFF_HEIGHT_SET), 1);
        return;
    }

    if (*(const int32_t *)(rec + UNIT_TYPE_OFF_VEHICLE)) {
        int32_t n;
        int32_t i;

        made = (uint8_t *)CreateVehicle(
                   kind, (char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                   (int16_t)where, (int16_t)(where >> 16), slot,
                   CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT, slot),
                   0, 0, 0, facing);

        rows = *(uint8_t **)(made + OBJ_OFF_ROWS);
        *(uint8_t *)(rows + ROW_OFF_HEADING) =
            *(const uint8_t *)(made + OBJ_OFF_FACING);

        if (*(const int32_t *)(made + OBJ_OFF_ROW_COUNT) > 1)
            *(uint8_t *)(*(uint8_t **)(made + OBJ_OFF_ROWS)
                         + ROW_OFF_FIELD_B0) =
                *(const uint8_t *)(made + OBJ_OFF_FIELD_530);

        StepObjRows(made);
        ObjTileChanged(made, *(const int8_t *)(made + OBJ_OFF_HEIGHT_SET), 1);

        n = *(const int32_t *)(made + OBJ_OFF_ROW_COUNT);
        rows = (n > 0) ? *(uint8_t **)(made + OBJ_OFF_ROWS) : (uint8_t *)0;

        for (i = 1; i < n; i++) {
            uint8_t       *row = *(uint8_t **)(made + OBJ_OFF_ROWS)
                                 + (uintptr_t)i * AM2_OBJ_ROW_STRIDE;
            const uint8_t *spr = *(const uint8_t *const *)(rows + 4);

            *(uint32_t *)(row + ROW_OFF_X) =
                *(const uint32_t *)(made + OBJ_OFF_POS);
            *(int16_t *)(row + ROW_OFF_X) +=
                *(const int16_t *)(spr + SPR_OFF_OVX);
            *(int16_t *)(row + ROW_OFF_Y) +=
                *(const int16_t *)(spr + SPR_OFF_OVY);

            RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
            n = *(const int32_t *)(made + OBJ_OFF_ROW_COUNT);
        }

        ObjSetFieldA(made, (uint32_t)group);
        return;
    }

    /* ---- an ITEM, which is where the buildings live ---- */
    {
        int32_t army = CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                      slot);
        int32_t key  = SpriteKeyForKind(kind, army);
        int32_t flag = (kind >= AM2_PILLBOX_KIND_FIRST
                        && kind <= AM2_PILLBOX_KIND_LAST)
                       ? (int32_t)OBJ_FLAG_SHOT_PROOF : 0;

        made = (uint8_t *)CreateItem((char *)name, slot, key, where, flag,
                                     0, 0);

        if (made)
            ApplyHeightItem(made,
                (int8_t)(*(const uint8_t *const *)(uintptr_t)ADDR_TILE_ATTRS)
                    [*(const uint16_t *)(made + OBJ_OFF_TILE)]);

        if (kind < AM2_PILLBOX_KIND_FIRST || kind > AM2_PILLBOX_KIND_LAST) {
            if (kind == AM2_PLACE_KIND_POST_CREATE) {
                ItemPostCreate(slot, where);
                if (!AllyFlag(*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                              (uint32_t)slot))
                    ObjConceal(made, 1);
            }
            return;
        }

        /* A pillbox: the building is made, now the man inside and his gun. */
        *(uint32_t *)(made + OBJ_OFF_FLAGS) |= OBJ_FLAG_SHOT_PROOF;

        {
            int32_t wkind;
            uint8_t *occupant;

            if (kind == 0)
                wkind = AM2_PILLBOX_WEAPON_RIFLE;
            else
                wkind = (kind == 2) ? AM2_PILLBOX_WEAPON_MG
                                    : AM2_PILLBOX_WEAPON_BAZOOKA;

            occupant = (uint8_t *)CreateTrooper(
                           (char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                           (int16_t)where, (int16_t)(where >> 16), slot,
                           CommArmyOfSlot(
                               *(void **)(uintptr_t)ADDR_COMM_OBJECT, slot),
                           0, 0, 0, 1, facing);
            ObjSetFieldA(occupant, (uint32_t)group);

            weapon = (uint8_t *)CreateWeapon(
                         (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH), slot,
                         KeyLookupTriple(AM2_WEAPON_KEY_KIND,
                                         (uint32_t)wkind, 0),
                         *(const uint32_t *)AM2_IMAGE(ADDR_ZERO_POINT),
                         AM2_OBJ_TYPE_WEAPON, -1, 0, 0);

            if (weapon) {
                *(int32_t *)(occupant + TROOPER_OFF_WEAPON_UID) =
                    *(const int32_t *)(weapon + 4);
                SoldierKindForWeapon(occupant,
                    **(const uint32_t *const *)(weapon + OBJ_OFF_FIELD_C0));
                SendTrooperSetWeapon(occupant,
                                     *(const uint32_t *)(weapon + 4), 0);
            }

            rows = *(uint8_t **)(occupant + OBJ_OFF_ROWS);
            *(uint8_t *)(rows + ROW_OFF_HEADING) =
                *(const uint8_t *)(occupant + OBJ_OFF_FACING);
            StepObjRows(occupant);

            *(int32_t *)(occupant + OBJ_OFF_RANK) = 4;
            *(int16_t *)(occupant + OBJ_OFF_MAX_HEALTH) =
                *(const int16_t *)AM2_IMAGE(ADDR_PILLBOX_TROOPER_HEALTH);
            *(int16_t *)(occupant + OBJ_OFF_HEALTH) =
                *(const int16_t *)AM2_IMAGE(ADDR_PILLBOX_TROOPER_HEALTH);
            /* +0x540, not OBJ_OFF_SOLDIER_KIND at +0x544 -- four bytes
             * apart, and tools/checkoffsetuse.py is what caught the slip. */
            *(int32_t *)(occupant + OBJ_OFF_FIELD_94)  = 1;
            *(int32_t *)(occupant + OBJ_OFF_FIELD_540) = 1;

            ObjTileChanged(occupant,
                           *(const int8_t *)(occupant + OBJ_OFF_HEIGHT_SET), 1);
        }
    }
}

/* RefundPlacedUnit -- original 0x0043B160, one caller: the manual placement
 * screen at 0x00413BC0, which is also IsPlacedUnit's only caller. So this is
 * what happens when you take a unit back off the layout: work out what it
 * cost, destroy it, and add the points back.
 *
 * IT OPENS EXACTLY AS IsPlacedUnit DOES -- the same "types 2, 3 and 8 OR an
 * item" gate and the same army test -- and then diverges: where that one
 * answers yes or no, this one has to find the ADDR_UNIT_TYPES record and take
 * its UNIT_TYPE_OFF_COST. Three arms, and each finds the record a different
 * way:
 *
 *   A VEHICLE matches on its own OBJ_OFF_TABLE_REC_KIND against a record whose
 *   UNIT_TYPE_OFF_VEHICLE is set.
 *
 *   A TROOPER matches on THE KIND OF THE WEAPON IN ITS FIRST SLOT, against a
 *   record whose UNIT_TYPE_OFF_TROOPER is set -- so what a soldier is worth
 *   is decided by what it is holding. It refuses outright if the trooper is
 *   Sarge or has anything at OBJ_OFF_FIELD_94, which is the same pair
 *   IsPlacedUnit refuses on.
 *
 *   AN ITEM asks UnitKindMatches, one BUILDING record at a time, with the
 *   army for this slot -- the same three-function family as SpriteKeyForKind
 *   and PlacementAllowed, and the fourth caller of that vocabulary.
 *
 * THE ITEM ARM CLIMBS TO THE ROOT FIRST. OBJ_OFF_CHAIN_PARENT_UID is followed
 * until it runs out, so clicking any piece of a composite refunds the whole
 * thing -- which is the other end of what CreateItem builds.
 *
 * AND THE PILLBOXES FIND THEIR OCCUPANT BY UID ARITHMETIC. For kinds 0, 1 and
 * 2 -- riflepill, bazookapill and mgpill, the three that share a sprite set --
 * it looks up uid+1, uid+2 and uid+3 in turn and takes the first that is a
 * TROOPER. That works because CreateItem allocates a composite's children
 * immediately after its parent, so their uids are consecutive; nothing here
 * checks that assumption, and a uid allocated in between would break it.
 * Then the trooper's WEAPON decides which pillbox record to charge for: kind
 * 4 is the second, kind 8 the third, anything else the first.
 *
 * The occupant is destroyed on BOTH paths out of that arm -- whether or not a
 * record was found -- while the outer object is destroyed only if a cost was
 * found. A pillbox whose weapon matches nothing therefore loses its soldier
 * and stays on the map.
 *
 * NOTHING HAPPENS AT ALL FOR A COST OF ZERO OR LESS: no destroy, no refund.
 * That is the single exit every arm falls into.
 *
 * THE ORIGINAL WALKS THE TABLE FROM rec+8, not from rec+0 -- its pointer
 * starts at 0x004878A0 and reads the trooper and vehicle flags as [-8] and
 * [-4] and the cost as [+0x18]. Written here from rec+0 with the
 * UNIT_TYPE_OFF_ names, which is the same three addresses said legibly, and
 * is why checkoffsetuse reports 0 and 0x20 on one side and 0x18 on the other.
 */
void __cdecl RefundPlacedUnit(void *obj, int32_t slot, int32_t *points)
{
    uint8_t *o    = (uint8_t *)obj;
    int32_t  cost = 0;
    int32_t  i;

    if (!ObjIsTypeIn238((const AM2_Object *)o)
        && !ObjIsItem((const AM2_Object *)o))
        return;

    if (*(const int8_t *)(o + OBJ_OFF_ARMY) != slot)
        return;

    switch (*(const int32_t *)o) {
    case AM2_OBJ_TYPE_VEHICLE:
        for (i = 0; i < AM2_UNIT_TYPE_COUNT; i++) {
            const uint8_t *rec = kUnitType(i);

            if (!*(const int32_t *)(rec + UNIT_TYPE_OFF_VEHICLE))
                continue;
            if (*(const int32_t *)(rec + UNIT_TYPE_OFF_KIND)
                != *(const int32_t *)(o + OBJ_OFF_TABLE_REC_KIND))
                continue;
            cost = *(const int32_t *)(rec + UNIT_TYPE_OFF_COST);
        }
        break;

    case AM2_OBJ_TYPE_TROOPER: {
        uint8_t *w;

        if (*(const int32_t *)(o + OBJ_OFF_SARGE))
            return;
        if (*(const int32_t *)(o + OBJ_OFF_FIELD_94))
            return;

        w = (uint8_t *)LookupByUID(*(const uint32_t *)(o + OBJ_OFF_WEAPON_UID));
        if (!w)
            return;
        if (!ObjIsType4((const AM2_Object *)w))
            return;

        for (i = 0; i < AM2_UNIT_TYPE_COUNT; i++) {
            const uint8_t *rec = kUnitType(i);

            if (!*(const int32_t *)(rec + UNIT_TYPE_OFF_TROOPER))
                continue;
            if (*(const int32_t *)(rec + UNIT_TYPE_OFF_KIND)
                != **(const int32_t *const *)(w + OBJ_OFF_FIELD_C0))
                continue;
            cost = *(const int32_t *)(rec + UNIT_TYPE_OFF_COST);
        }
        break;
    }

    case AM2_OBJ_TYPE_ITEM: {
        int32_t army = CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                      slot);
        int32_t found = -1;
        int32_t kind;

        for (i = 0; i < AM2_UNIT_TYPE_COUNT; i++) {
            const uint8_t *rec = kUnitType(i);

            if (*(const int32_t *)(rec + UNIT_TYPE_OFF_TROOPER)
                || *(const int32_t *)(rec + UNIT_TYPE_OFF_VEHICLE))
                continue;
            if (UnitKindMatches(
                    *(const int32_t *)(
                        *(const uint8_t *const *)(o + OBJ_OFF_FIELD_94)
                        + AAIREC_OFF_KEY),
                    *(const int32_t *)(rec + UNIT_TYPE_OFF_KIND), army)) {
                found = i;
                break;
            }
        }
        if (found < 0)
            return;

        /* Climb to the root of the chain -- see the note above. */
        while (*(const uint32_t *)(o + OBJ_OFF_CHAIN_PARENT_UID))
            o = (uint8_t *)LookupByUID(
                *(const uint32_t *)(o + OBJ_OFF_CHAIN_PARENT_UID));

        kind = *(const int32_t *)(kUnitType(found) + UNIT_TYPE_OFF_KIND);
        if (kind < AM2_PILLBOX_KIND_FIRST || kind > AM2_PILLBOX_KIND_LAST) {
            cost = *(const int32_t *)(kUnitType(found) + UNIT_TYPE_OFF_COST);
            break;
        }

        {
            uint8_t *inside = (uint8_t *)0;
            uint8_t *w;
            int32_t  want;
            int32_t  n;

            for (n = 1; n <= AM2_PILLBOX_UID_SCAN; n++) {
                uint8_t *c = (uint8_t *)LookupByUID(
                    ((const AM2_Object *)o)->uid + (uint32_t)n);

                if (ObjIsType2((const AM2_Object *)c)) {
                    inside = c;
                    break;
                }
            }

            w = (uint8_t *)LookupByUID(
                *(const uint32_t *)(inside + OBJ_OFF_WEAPON_UID));
            if (!w)
                return;
            if (!ObjIsType4((const AM2_Object *)w))
                return;

            switch (**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0)) {
            case 4:  want = 1; break;
            case 8:  want = 2; break;
            default: want = 0; break;
            }

            for (i = 0; i < AM2_UNIT_TYPE_COUNT; i++) {
                const uint8_t *rec = kUnitType(i);

                if (*(const int32_t *)(rec + UNIT_TYPE_OFF_TROOPER)
                    || *(const int32_t *)(rec + UNIT_TYPE_OFF_VEHICLE))
                    continue;
                if (*(const int32_t *)(rec + UNIT_TYPE_OFF_KIND) != want)
                    continue;

                cost = *(const int32_t *)(rec + UNIT_TYPE_OFF_COST);
                break;
            }

            /* Destroyed either way -- see the note. */
            DestroyByType(inside);
        }
        break;
    }

    default:
        return;
    }

    if (cost <= 0)
        return;

    DestroyByType(o);
    *points += cost;
}

/* The three anim lookups are reconstructed, in win32/sprite.cpp with the rest
 * of the sprite record, and are declared here rather than by including that
 * header for the reason script.cpp gives about PreloadSprite: place.cpp is on
 * the flat side of the split and must name no Win32 type, and AM2_Sprite has
 * an LPDIRECTDRAWSURFACE in it. An incomplete type keeps the SIGNATURES exact,
 * which a `void *` stand-in would not. */
struct AM2_Sprite;
extern "C" AM2_Sprite *__cdecl SoldierAnimSprite(int32_t kind,
                                                 uint32_t heading);
extern "C" AM2_Sprite *__cdecl VehicleAnimSprite(int32_t kind,
                                                 uint32_t heading);
extern "C" AM2_Sprite *__cdecl TurretAnimSprite(int32_t kind,
                                                uint32_t heading);

extern "C" AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a,
                                                  int32_t b);

#define SET_CURSOR(x) (*(void **)(uintptr_t)ADDR_MENU_SPRITES_END = (void *)(x))

/* A vehicle: hull on the cursor, turret on overlay A, same kind and heading.
 * Kinds 1, 2, 0, 3, 5 over rows 0x18..0x1C -- four is absent, and the jump
 * table is what says so rather than the arms. Neither arm touches the ink. */
static void PlaceVehicle(int32_t kind, int32_t facing)
{
    SET_CURSOR(VehicleAnimSprite(kind, (uint32_t)facing));
    *(void **)(uintptr_t)ADDR_MENU_OVERLAY_A =
        TurretAnimSprite(kind, (uint32_t)facing);
}

/* One sprite out of a set, by the key PackKey builds. Every arm below that is
 * not an animation lookup is this shape, and the two constants never vary:
 * 0x1000 is the set the placement sprites live in and 1 says load it now. */
static AM2_Sprite *PlaceSprite(uint32_t set, uint32_t id)
{
    return PreloadSpriteByKey(PackKey(set, id, 0), AM2_PLACE_SPRITE_SET,
                                   1);
}

/* An emplacement: a base and a mount out of set 0x26 keyed off the ARMY, with
 * a soldier of `kind` manning it on overlay B. The two ids are 10*army + 10
 * and 10*army + 11, so each army owns a run of ten. Both sprite slots have
 * their ink cleared; overlay B's is left as the head set it, which is the
 * asymmetry that makes the manning soldier the player's colour. */
static void PlaceEmplacement(int32_t army, int32_t facing, int32_t kind)
{
    SET_CURSOR(PlaceSprite(0x26, (uint32_t)(army * 5 + 5) * 2));
    *(uint8_t **)(uintptr_t)ADDR_MENU_INK = (uint8_t *)0;
    *(void **)(uintptr_t)ADDR_MENU_OVERLAY_A =
        PlaceSprite(0x26, (uint32_t)(army * 5) * 2 + 11);
    *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = (uint8_t *)0;
    *(void **)(uintptr_t)ADDR_MENU_OVERLAY_B =
        SoldierAnimSprite(kind, (uint32_t)facing);
}

/* PlaceCursorPrepare -- original 0x004127B0, two callers, both of them arms of
 * PlacementScreenClick: this is that function's private helper and nothing
 * else's.
 *
 * IT IS A CURSOR. Argument 1 goes straight into ADDR_MENU_ROW, the same global
 * OverlayPrepare picks a row with, so the ghost unit the placement screen
 * hangs off the pointer is the MENU CURSOR wearing a different sprite rather
 * than a layer of its own -- and writing the row directly bypasses
 * OverlayPrepare's one-row-per-millisecond throttle.
 *
 * EIGHTEEN ROWS, SEVENTEEN ARMS, AND THE LAYOUT IS NOT THE ORDER. The table at
 * 0x00412CDC is indexed by `row - 0x13`; rows 0x1D and 0x1F share an arm, and
 * the arms are emitted 0x13, 0x17, 0x15, 0x14, 0x16, ... so reading the bodies
 * top to bottom and numbering as you go gets four of the first five wrong.
 * Generated from a decode of the table, which is the rule this project already
 * pays for twice over.
 *
 * `ok` IS WHAT MAKES THE CURSOR RED, and the mechanism is the ink rather than
 * the sprite. On an accepted placement all three ink slots start as the
 * player's own army ink -- record `CommArmyOfSlot(comm, defaultOwner)` of
 * ADDR_OBJ_TABLE_RECORDS, which is 0x100 bytes a side; on a refusal they start
 * as ADDR_FLAME_RECORD, and any an arm then clears to zero is filled in with
 * ADDR_PALETTE_GLYPHS by the tail. Same sprites either way.
 *
 * THAT IS A THIRD READING OF 0x004FCDF8, and it agrees with the two orig.h
 * already carries. InitMenuScreen writes 256 bytes there and the "Flame On!"
 * cheat points every unit's weapon record at it; here the menu hands the same
 * 256 bytes to the cursor as a colour table. Two subsystems that cannot be up
 * at once, and this is the menu one.
 *
 * THE ARMY LOOKUP IS DONE THREE TIMES with identical arguments, once per ink
 * slot, and the answer cannot change between them. Reproduced rather than
 * hoisted: CommArmyOfSlot is thiscall on the comm object and the original did
 * not common them up, so hoisting is a change to the call count for no gain.
 *
 * ROW 0x22's SECOND SPRITE IS INDEXED BY THE ROW. `row + 0x3D5` is what the
 * original computes, and since the arm is only ever entered with row 0x22 it
 * is the constant 0x3F7 written the long way -- the switch variable happened
 * to be live in a register. Written as the original has it. */
void __cdecl PlaceCursorPrepare(int32_t row, int32_t ok, int32_t facing,
                                int32_t army)
{
    void   *comm = *(void *const *)(uintptr_t)ADDR_ARMY_TABLE;
    int32_t owner = (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER;
    uint8_t *ink;

    /* The three draw offsets, zeroed from the image's own {0,0}. */
    *(int32_t *)(uintptr_t)ADDR_MENU_CURSOR_DX =
        *(const int32_t *)(uintptr_t)ADDR_ZERO_POINT;
    *(int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_A_DX =
        *(const int32_t *)(uintptr_t)ADDR_ZERO_POINT;
    *(int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_B_DX =
        *(const int32_t *)(uintptr_t)ADDR_ZERO_POINT;

    *(int32_t *)(uintptr_t)ADDR_MENU_ROW        = row;
    *(int32_t *)(uintptr_t)ADDR_MENU_ANIM_FRAME = -1;
    *(int32_t *)(uintptr_t)ADDR_MENU_ANIM_NEXT  = 0;
    *(void **)(uintptr_t)ADDR_MENU_OVERLAY_A    = (void *)0;
    *(void **)(uintptr_t)ADDR_MENU_OVERLAY_B    = (void *)0;

    if (ok) {
        ink = (uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS
              + ((uint32_t)CommArmyOfSlot(comm, owner) << 8);
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK = ink;
        ink = (uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS
              + ((uint32_t)CommArmyOfSlot(comm, owner) << 8);
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = ink;
        ink = (uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS
              + ((uint32_t)CommArmyOfSlot(comm, owner) << 8);
    } else {
        ink = (uint8_t *)(uintptr_t)ADDR_FLAME_RECORD;
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK           = ink;
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = ink;
    }
    *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_B_INK = ink;

    switch (row) {
    /* The five foot soldiers. The kind is the arm's whole content and the
     * numbering is the jump table's, not the layout's. */
    case 0x13: SET_CURSOR(SoldierAnimSprite(0, (uint32_t)facing)); break;
    case 0x14: SET_CURSOR(SoldierAnimSprite(2, (uint32_t)facing)); break;
    case 0x15: SET_CURSOR(SoldierAnimSprite(4, (uint32_t)facing)); break;
    case 0x16: SET_CURSOR(SoldierAnimSprite(3, (uint32_t)facing)); break;
    case 0x17: SET_CURSOR(SoldierAnimSprite(1, (uint32_t)facing)); break;

    /* The five vehicles, each a hull on the cursor and a turret on overlay A.
     * Kinds 0, 1, 2, 3, 5 -- four is missing and the table is what says so. */
    case 0x18: PlaceVehicle(1, facing); break;
    case 0x19: PlaceVehicle(2, facing); break;
    case 0x1A: PlaceVehicle(0, facing); break;
    case 0x1B: PlaceVehicle(3, facing); break;
    case 0x1C: PlaceVehicle(5, facing); break;

    /* Two emplacements: a base and a mount out of set 0x26, both keyed off the
     * ARMY rather than the row, with a soldier of a fixed kind manning it on
     * overlay B. The two ids are 10*army + 10 and 10*army + 11, so each army
     * owns a run of ten. Rows 0x1D and 0x1F are the same arm. */
    case 0x1D:
    case 0x1F: PlaceEmplacement(army, facing, 0); break;
    case 0x1E: PlaceEmplacement(army, facing, 1); break;

    case 0x20:
        SET_CURSOR(PlaceSprite(0x20, (uint32_t)(army * 5 + 5) * 2));
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK = (uint8_t *)0;
        break;

    /* THE BUILDINGS, and the only arm that consults the def files. The three
     * pieces are links 0, 1 and 2 of def key (army + 1) in set 0x21, each
     * missing piece simply skipped -- so a building with one link draws one
     * sprite and the other two slots keep whatever the head left. The link's
     * +0x0C is the piece's draw offset and goes into the matching DX slot,
     * which is the only place in this function those offsets are ever
     * non-zero. */
    case 0x21: {
        uint32_t key  = PackKey(0x21, (uint32_t)(army + 1), 0);
        AM2_DefLink *link = DefFindLink((int32_t)key, 0);

        if (link) {
            SET_CURSOR(PlaceSprite(0x21, (uint32_t)(army + 1) * 10));
            *(uint8_t **)(uintptr_t)ADDR_MENU_INK = (uint8_t *)0;
            *(int32_t *)(uintptr_t)ADDR_MENU_CURSOR_DX =
                *(const int32_t *)&link->dx;
        }
        link = DefFindLink((int32_t)key, 1);
        if (link) {
            *(void **)(uintptr_t)ADDR_MENU_OVERLAY_A =
                PlaceSprite(0x21, (uint32_t)army * 10 + 11);
            *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = (uint8_t *)0;
            *(int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_A_DX =
                *(const int32_t *)&link->dx;
        }
        link = DefFindLink((int32_t)key, 2);
        if (link) {
            *(void **)(uintptr_t)ADDR_MENU_OVERLAY_B =
                PlaceSprite(0x21, (uint32_t)army * 10 + 12);
            *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_B_INK = (uint8_t *)0;
            *(int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_B_DX =
                *(const int32_t *)&link->dx;
        }
        break;
    }

    case 0x22:
        SET_CURSOR(PlaceSprite(0x2A, 1));
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK = (uint8_t *)0;
        /* `row + 0x3D5`, which is 0x3F7 -- see the header. */
        *(void **)(uintptr_t)ADDR_MENU_OVERLAY_A =
            PlaceSprite(0x2B, (uint32_t)row + 0x3D5);
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = (uint8_t *)0;
        break;

    case 0x23:
        SET_CURSOR(PlaceSprite(0x1F, (uint32_t)(army * 5 + 5) * 2));
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK           = (uint8_t *)0;
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = (uint8_t *)0;
        break;

    /* The watched-object kind, whose key is already packed in a global. */
    case 0x24:
        SET_CURSOR(PreloadSpriteByKey(
            *(const uint32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND,
            AM2_PLACE_SPRITE_SET, 1));
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK           = (uint8_t *)0;
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = (uint8_t *)0;
        break;

    /* Anything else gets the menu's own first sprite, so the cursor is never
     * left holding whatever the last row put there. */
    default:
        SET_CURSOR(*(void *const *)(uintptr_t)ADDR_MENU_SPRITES);
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK           = (uint8_t *)0;
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK = (uint8_t *)0;
        break;
    }

    /* A refusal fills in whatever the arm cleared. Note the test is on `ok`
     * and the fills are on the slots that are ZERO, so an arm that set a slot
     * deliberately keeps it even on a refusal -- only the cleared ones turn. */
    if (ok)
        return;
    if (!*(uint8_t *const *)(uintptr_t)ADDR_MENU_INK)
        *(uint8_t **)(uintptr_t)ADDR_MENU_INK =
            (uint8_t *)(uintptr_t)ADDR_PALETTE_GLYPHS;
    if (!*(uint8_t *const *)(uintptr_t)ADDR_MENU_OVERLAY_A_INK)
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK =
            (uint8_t *)(uintptr_t)ADDR_PALETTE_GLYPHS;
    if (!*(uint8_t *const *)(uintptr_t)ADDR_MENU_OVERLAY_B_INK)
        *(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_B_INK =
            (uint8_t *)(uintptr_t)ADDR_PALETTE_GLYPHS;
}

int place_install(void)
{
    patch_replace(ADDR_IS_PLACED_UNIT, (const void *)IsPlacedUnit,
                  "IsPlacedUnit", 1);
    int rc = 0;

    rc |= patch_replace(ADDR_SPRITE_KEY_FOR_KIND,
                        (const void *)SpriteKeyForKind,
                        "SpriteKeyForKind", 1);
    rc |= patch_replace(ADDR_UNIT_KIND_MATCHES,
                        (const void *)UnitKindMatches,
                        "UnitKindMatches", 2);
    rc |= patch_replace(ADDR_MAKE_PLACED_UNIT,
                        (const void *)MakePlacedUnit, "MakePlacedUnit", 2);
    rc |= patch_replace(ADDR_PLACEMENT_ALLOWED,
                        (const void *)PlacementAllowed,
                        "PlacementAllowed", 2);
    rc |= patch_replace(ADDR_REFUND_PLACED_UNIT,
                        (const void *)RefundPlacedUnit,
                        "RefundPlacedUnit", 1);

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
    rc |= patch_replace(ADDR_PLACE_CURSOR_PREPARE,
                        (const void *)PlaceCursorPrepare,
                        "PlaceCursorPrepare", 2);
    return rc;
}
