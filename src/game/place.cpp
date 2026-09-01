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
    return rc;
}
