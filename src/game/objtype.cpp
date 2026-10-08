/* Object type predicates, reconstructed from ArmyMen2.exe.
 *
 *   ObjIsItem       0x00433860   types 1, 4      33 call sites
 *   ObjIsType2      0x00457470   type 2          47 call sites
 *   ObjIsType3      0x00457490   type 3          28 call sites
 *   ObjIsTypeIn238  0x00457420   types 2, 3, 8   40 call sites
 *
 * and one lookup that ends in a predicate rather than starting from an object:
 *
 *   LookupType3ByUID 0x0045D970  uid -> type 3 or NULL   8 call sites
 *
 * All four accept NULL and answer 0 for it, which is why they are used so
 * freely -- callers do not null-check first. See objtype.h for what is and is
 * not established about the type taxonomy.
 */

#include "objtype.h"
#include "maprow.h"   /* BuildRowsFromDef -- reconstructed */
#include "spritetype.h" /* AM2_Sprite: the hot spot, bounds and key */
#include "crt.h"           /* am2_malloc -- the game's own heap */
#include "objtable.h"
#include "rect.h"        /* AM2_Rect -- the box and the hit rect */
#include "objflag.h"       /* ObjFlagClear0 -- reconstructed */
#include "misc.h"          /* CommArmyOfSlot -- reconstructed */
#include "defparse.h"      /* DefFindObjRec -- reconstructed */
#include "script.h"        /* ScriptFindName, AddNameTableName */
#include "scriptint.h"     /* kScriptNames -- the table itself */
#include "map.h"           /* TileOfPoint -- reconstructed */
#include "item.h"          /* ItemLinkCells -- reconstructed */
#include "packkey.h"       /* PackKey, KeyLookup -- reconstructed */
#include "region.h"        /* ItemTeardown, ObjAfterMove -- reconstructed */

/* PreloadSprite and PreloadSpriteByKey are reconstructed, in win32/sprite.cpp.
 * Declared here rather than by including that header because objtype.cpp is on
 * the flat side of the split and must name no Win32 or COM type -- AM2_Sprite
 * has an LPDIRECTDRAWSURFACE in it. An incomplete type is enough: everything
 * below reaches the sprite through byte offsets, never a member. Same reason
 * and same shape as script.cpp's declaration of the first of the two. */
struct AM2_Sprite;
AM2_Sprite *__cdecl PreloadSprite(int32_t set, int32_t index,
                                             int32_t frame, int32_t flags,
                                             int32_t addref);
AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a,
                                                  int32_t b);

/* The CRT's _strlwr, which ObjInitCommon applies to the CALLER's buffer. Same
 * seam map.cpp and definfo.cpp use; it is CRT, so it stays original. */
typedef char *(__cdecl *AM2_StrlwrFn)(char *);
#define orig_strlwr ((AM2_StrlwrFn)((void *)(uintptr_t)(ADDR_CRT_STRLWR)))
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

int32_t __cdecl ObjIsItem(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 1 || obj->type == 4) ? 1 : 0;
}

int32_t __cdecl ObjIsType2(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 2) ? 1 : 0;
}

int32_t __cdecl ObjIsType3(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 3) ? 1 : 0;
}

int32_t __cdecl ObjIsTypeIn238(const AM2_Object *obj)
{
    if (obj) {
        /* A switch: VC6 turns the three cases into 2 <= t <= 3, then 8. */
        switch ((int32_t)obj->type) {
        case 2:
        case 3:
        case 8:
            return 1;
        }
    }
    return 0;
}

/* 0x004574B0 and 0x0045EEB0. The two remaining single-type tests, in the same
 * shape as the three above: null gives 0, otherwise the type word at +0 is
 * compared and the answer normalised to 0 or 1 with sete.
 *
 * These complete the family -- ObjIsTypeIn238 already tested 2, 3 and 8
 * together, and ObjIsItem tests 1 or 4, so every type either has its own
 * predicate now or appears in a combined one. */
int32_t __cdecl ObjIsType8(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 8) ? 1 : 0;
}

int32_t __cdecl ObjIsType4(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    return (obj->type == 4) ? 1 : 0;
}

uint32_t __cdecl ObjType2Field548(const AM2_Object *obj)
{
    if (!obj)
        return 0;
    if (obj->type != 2)
        return 0;
    return (uint32_t)obj->sarge;
}

/* The lookup and the test together. The original is branchless -- ObjIsType3's
 * 0-or-1 is turned into an all-ones mask with `neg eax; sbb eax, eax` and
 * ANDed over the pointer -- which is the same function as the conditional
 * written here, because the predicate answers only 0 or 1 and never a third
 * thing. Transcribing the mask would reproduce a compiler's choice rather
 * than the source's.
 *
 * LookupByUID and ObjIsType3 are both reconstructed, so this calls them
 * directly rather than reaching into the image for either. */
AM2_Object *__cdecl LookupType3ByUID(uint32_t uid)
{
    AM2_Object *obj;

    obj = LookupByUID(uid);
    return ObjIsType3(obj) ? obj : 0;
}

#define kEventComm (*(void *const *)&g_commObject)

/* 0x00427D40, fifteen callers. The event MASK for an object: the top bit
 * always, one more bit for the owner's ARMY, and then a bit per type property.
 * event.h calls EventNotify's third and sixth parameters masks, and this is
 * what fills them.
 *
 * Note the army comes out of CommArmyOfSlot applied to the object's owner
 * byte, so that byte is a SLOT here and the switch is over the army it maps
 * to. Anything above 3 -- including the slot-4 answer that function is
 * documented to give -- leaves the mask with only its top bit, which is the
 * default rather than a special case.
 *
 * The six type tests are independent `if`s and NOT a chain, and their bits
 * deliberately overlap: 0x01C00000 for a type 2 carrying field 548, then
 * 0x01400000 for any type 2, then 0x01000000 for any of types 2, 3 and 8. So
 * an ordinary type 2 accumulates two of them and a type 3 accumulates
 * 0x01000000 | 0x00200000. Reproduced as written; collapsing them into a
 * switch would change the answer.
 *
 * Exercised: its counter reads 1 on a Boot Camp mission, from one of the
 * thirteen callers that are still the original's, on top of six more calls
 * from our own notifiers that the counter cannot see. All six of those are
 * type 2, so they take the 0x01400000 and 0x01000000 bits and no other; the
 * item, type 4 and type 3 bits are unexercised here. */
int32_t __cdecl ObjEventMask(const AM2_Object *obj)
{
    int32_t mask = (int32_t)0x80000000;

    switch (((AM2_Comm *)kEventComm)->CommArmyOfSlot(
                           obj->owner)) {
    case 0:  mask = (int32_t)0xC0000000; break;
    case 1:  mask = (int32_t)0xA0000000; break;
    case 2:  mask = (int32_t)0x90000000; break;
    case 3:  mask = (int32_t)0x88000000; break;
    default: break;
    }

    if (ObjIsItem(obj))
        mask |= 0x04000000;
    if (ObjIsType4(obj))
        mask |= 0x02000000;
    if (ObjType2Field548(obj))
        mask |= 0x01C00000;
    if (ObjIsType2(obj))
        mask |= 0x01400000;
    if (ObjIsTypeIn238(obj))
        mask |= 0x01000000;
    if (ObjIsType3(obj))
        mask |= 0x00200000;

    return mask;
}
/* 0x0044BBA0, four callers. True only when the object is a TYPE 2 and its
 * OBJ_OFF_FIELD_5A4 is positive.
 *
 * Type2ActionA is the reader that says anything about what it means: that
 * function refuses to re-arm a unit when this is true, so the counter is a
 * reason not to. Nothing else read so far narrows it further.
 *
 * The type test comes FIRST and short-circuits, so the field is never read on
 * an object of another type -- which matters, because 0x5A4 is far enough into
 * the record that other types may not have it. */
int32_t __cdecl Type2Field5A4Set(const AM2_Object *obj)
{
    if (!ObjIsType2(obj))
        return 0;

    return obj->field5A4 > 0;
}



/* 0x00434E60, three callers. Clear bit 0 on every row the sub-list holds, so
 * none of them draws.
 *
 * ITS ARGUMENT IS THE SUB-LIST HEADER, not the object. The count it reads is
 * at +0x04 and the rows at +0x08, which are SUBREC_OFF_COUNT and
 * SUBREC_OFF_ROWS -- the same two dwords the object reaches as
 * OBJ_OFF_ROW_COUNT and OBJ_OFF_ROWS at +0x70 and +0x74, seen from
 * OBJ_OFF_SUBRECORD instead. That pair of names was already in orig.h, which
 * is what turned "a struct with a count at +4" into a certainty rather than a
 * guess; the callers hand it a pointer they are already holding.
 *
 * The count is RE-READ every iteration, as it is in StepObjRows and
 * ObjTileChanged -- four functions in this tree now, so it is the compiler's
 * habit and not any of them allowing the body to change it.
 *
 * Two of the three callers test OBJ_FLAG_DESTROYED immediately before, so this
 * is what taking a destroyed object off the screen looks like from the
 * sub-list's side. The third does not, which is why the name says what it does
 * rather than why.
 */
void __cdecl SubrecHideRows(AM2_RowSet *subrec)
{
    int32_t  i;

    /* The parameter itself at each use: a local copy of it turns the row
     * address into `i*stride + rows` rather than `rows + i*stride`. */
    for (i = 0; i < subrec->count; i++)
        ObjFlagClear0(&subrec->rows[i]);
}

/* 0x00434060, eight callers. Make a list: a 0x30-byte header carrying an
 * owner, a count and a pointer, plus a copy of `count` twelve-byte records.
 *
 * THE COPY IS FIELD BY FIELD -- int32, int16, int16, int32 -- rather than
 * twelve bytes at a time, and that is the only evidence anywhere for the
 * record's shape: nothing in this function reads a field, so the compiler had
 * a struct assignment to work from and left its outline in the instructions.
 * Kept as four moves rather than collapsed into a memcpy, which would agree
 * on every byte and say nothing about the layout.
 *
 * A count of zero or less answers NULL having allocated nothing, so the
 * header's existence implies at least one record. Neither allocation is
 * checked. Only three of the header's twelve dwords are written; the rest are
 * zeroed and unexplained.
 *
 * Filed here because 0x00433860 -- ObjIsItem, already in this file -- is its
 * nearest reconstructed neighbour. The band is the only evidence for the home
 * and the records are not identified, so the name is structural.
 */
AM2_RecordList *__cdecl MakeRecordList(int32_t count, const AM2_ListRec *src,
                                       uint32_t owner)
{
    AM2_RecordList *hdr;
    AM2_ListRec    *recs;
    int32_t         i;

    if (count <= 0)
        return (AM2_RecordList *)0;

    hdr = (AM2_RecordList *)am2_malloc(AM2_LISTHDR_BYTES);
    memset(hdr, 0, AM2_LISTHDR_BYTES);

    hdr->count = count;
    hdr->owner = owner;

    recs = (AM2_ListRec *)am2_malloc((size_t)count * AM2_LIST_RECORD_BYTES);
    hdr->records = recs;
    memset(recs, 0, (size_t)count * AM2_LIST_RECORD_BYTES);

    /* The record array is re-read for every field, as the original does. */
    for (i = 0; i < count; i++) {
        hdr->records[i].sprite = src[i].sprite;
        hdr->records[i].dx     = src[i].dx;
        hdr->records[i].dy     = src[i].dy;
        hdr->records[i].depth  = src[i].depth;
    }

    return hdr;
}

/* 0x00434150, eight callers. Register one of MakeRecordList's headers under
 * its owner and hand back the slot it went into, or -1 if that owner already
 * has one.
 *
 * IT MAINTAINS TWO PARALLEL STRUCTURES and that is the whole design.
 * ADDR_RECORD_LISTS is an unsorted array of header pointers, appended to, and
 * the new slot index is written back into the header at LISTHDR_OFF_INDEX.
 * ADDR_RECORD_LIST_INDEX is a SORTED array of {owner, slot} pairs, binary
 * searched on the way in and memmove'd open to keep it sorted. So a lookup by
 * owner is a halving search and an iteration is a walk of the unsorted array,
 * and neither invalidates the other.
 *
 * THE SEARCH IS ALSO THE DUPLICATE CHECK. Finding the owner returns -1 without
 * inserting anything; not finding it leaves the low bound sitting exactly
 * where the pair belongs, which is where the memmove opens the gap. One walk
 * does both jobs.
 *
 * The owner keys are compared UNSIGNED -- the branch is `jae` -- so a header
 * whose owner pointer has the top bit set still sorts correctly. Reproduced as
 * unsigned; signed would put such a list at the wrong end and the halving
 * search would then miss it.
 *
 * BOTH ARRAYS GROW TOGETHER TO count + 17, not to a multiple of anything, and
 * both are realloc'd without checking. The zeroing after each realloc starts
 * at the OLD capacity and writes 17 entries, which fits because the grow only
 * happens when the count has caught the capacity. Neither result is checked.
 *
 * The original computes the memmove length as `(lo << 29) - lo + count` then
 * shifts left by three. The first term is lo * 2^29, and shifting by three
 * makes it lo * 2^32, which is zero in 32 bits -- so the whole expression is
 * (count - lo) * 8, which is what it is written as here. Strength reduction
 * that relies on the overflow, and worth naming rather than transcribing.
 *
 * MEASURED AT 151 CALLS on a driven Boot Camp mission, exactly matching
 * MakeRecordList on the same run -- so every list made is registered and none
 * is registered twice. 151 slots at 17 per grow means both reallocs ran about
 * nine times and the memmove ran on most insertions, so the grow, the search
 * and the shift are all on the compared path. The duplicate-owner exit is the
 * one arm those 151 cannot have taken, since none of them returned -1.
 *
 * THIS FUNCTION WAS DECLINED ONCE, an hour before it was written, because the
 * records had no name and inventing one for a sorted insert would have said
 * nothing true. What unblocked it was already in orig.h: 0x00434060 is
 * ADDR_MAKE_RECORD_LIST and its header's first dword is LISTHDR_OFF_OWNER.
 * Grepping the callee rather than the caller is what CLAUDE.md says to do, and
 * doing it late cost the detour.
 */
int32_t __cdecl AddRecordList(AM2_RecordList *list)
{
    uint32_t owner;
    int32_t  lo, hi, grown;

    if (!list)
        return -1;

    /* Every global is re-read where it is used, as the original does. */
#define COUNT (g_recordListCount)
#define CAP   (g_recordListCap)
#define LISTS (g_recordLists)
#define INDEX (*(uint32_t * *)&g_recordListIndex)
    hi    = COUNT;
    owner = list->owner;
    lo    = 0;

    /* FindRecordList's search: a shifted midpoint, a `while` on hi > lo. */
    while (hi > lo) {
        int32_t mid = ((hi - lo) >> 1) + lo;

        if (INDEX[mid * 2] == owner)
            return -1;                      /* already registered */
        if (INDEX[mid * 2] < owner)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (COUNT + 1 > CAP) {
        grown = COUNT + AM2_RECORD_LIST_GROW;
        LISTS = (AM2_RecordList **)am2_realloc(LISTS, (size_t)grown * 4);
        memset(LISTS + CAP, 0, AM2_RECORD_LIST_GROW * 4);
        INDEX = (uint32_t *)am2_realloc(INDEX, (size_t)grown * 8);
        memset(INDEX + CAP * 2, 0, AM2_RECORD_LIST_GROW * 8);
        CAP = grown;
    }

    list->index = COUNT;
    LISTS[COUNT] = list;

    if (lo < COUNT)
        orig_memmove(INDEX + (lo + 1) * 2, INDEX + lo * 2,
                     (size_t)(COUNT - lo) * 8);

    INDEX[lo * 2]     = owner;
    INDEX[lo * 2 + 1] = (uint32_t)COUNT;
    return COUNT++;
#undef COUNT
#undef CAP
#undef LISTS
#undef INDEX
}

/* 0x00434100, one caller -- the READ half of AddRecordList above, and the
 * fourth member of that family after the maker, the adder and the free.
 *
 * It is AddRecordList's search with the insertion arms removed: the same
 * halving over ADDR_RECORD_LIST_INDEX, the same UNSIGNED compare, and the same
 * `hi > lo` bottom test. A hit returns the pair's second dword -- the SLOT --
 * and a miss returns -1, where the adder returned -1 for a hit instead.
 *
 * So the two disagree about what -1 means and agree about everything else,
 * which is worth stating: the adder refuses a duplicate with -1 and this
 * reports a miss with -1. Reading one to write the other and carrying the
 * return convention across would invert both.
 *
 * Measured at 0 on a driven Boot Camp mission, where the maker, the adder and
 * the free run 151, 151 and 0 -- so 151 lists are registered and nothing looks
 * one up. Its counter is not blind; the one caller is the original's.
 */
int32_t __cdecl FindRecordList(uint32_t owner)
{
    int32_t lo = 0;
    int32_t hi = g_recordListCount;

    /* {owner, list} pairs, sorted by owner; the midpoint is a shift. */
#define IX (*(const uint32_t *const *)&g_recordListIndex)
    while (hi > lo) {
        int32_t mid = ((hi - lo) >> 1) + lo;

        if (IX[mid * 2] == owner)
            return (int32_t)IX[mid * 2 + 1];
        if (IX[mid * 2] < owner)
            lo = mid + 1;
        else
            hi = mid;
    }
#undef IX

    return -1;
}

/* 0x00434C40, one caller -- and that caller walks every entry of
 * ADDR_RECORD_LISTS, so this is MakeRecordList's counterpart.
 *
 * Free the header's records array, free a SECOND pointer at
 * the embedded mask's bits, then free the header. Each is tested first, so a header
 * with either pointer null is fine.
 *
 * THAT SECOND POINTER IS NOT MakeRecordList'S. The maker zeroes the 0x30 bytes
 * and writes only the owner, the count and the records -- nothing there
 * touches +0x1C. So something between the make and the free fills it in, and
 * this is the only reader of it in the whole image. What it points at is not
 * established and the name says only that the free releases it.
 *
 * THE ORDER IS EXTRA, RECORDS, HEADER -- the header last, which is the only
 * order that works, and worth noticing because the two pointers are freed in
 * the opposite order from the offsets. Nothing is nulled on the way out: the
 * caller is walking a table it is about to discard.
 *
 * MEASURED AT 0 on a driven Boot Camp mission, which does not tear the level
 * down -- CLAUDE.md records that the state-2 teardown runs on LEAVING a level
 * and that entering one does not trigger it. Its counter is not blind, so
 * this is verified by reading and by being the exact inverse of a maker whose
 * 151 calls are compared.
 */
void __cdecl FreeRecordList(AM2_RecordList *list)
{
    void    *p;

    if (!list)
        return;

    p = list->mask.bits;
    if (p)
        am2_free(p);

    p = list->records;
    if (p)
        am2_free(p);

    am2_free(list);
}

/* BuildAaiBuiltins -- original 0x00434700, 1,120 bytes, one caller. Empty the
 * AAI tables and put back the records the game does not read from object.aai:
 * three singletons, a run of FORTY-FOUR, and two more.
 *
 * THE KEY GLOBALS ARE WHY THIS MATTERS ELSEWHERE, and two of the five already
 * had names: ADDR_CREATE_WATCHED_KIND and ADDR_WATCHED_TYPE_ID, the pair
 * ObjIsWatchedKind matches an item's record against. So the "watched kind" the
 * default pointer stands aside for -- see PointerPickMode0 -- is the last
 * built-in record this function makes. That was reached from the reader's end
 * long ago and from the writer's end here, and the two agree.
 *
 * I named both a second time before checking, and checkpatches refused them.
 * Third time this session; grep the ADDRESS first.
 *
 * THE RUN OF 44 IS A LOOP OVER THE KEY, not over an index: the key starts
 * at 0x01680000 and steps 0x80 to 0x01681600, and the sprite index is carried
 * alongside it. Both are needed, so neither derives from the other here.
 *
 * ITS SPRITE LOAD HAS A FALLBACK -- index n, and index 0x0A if that yields
 * nothing -- so a missing sprite in that set silently becomes sprite 10 rather
 * than a null in the table.
 *
 * AND IT CENTRES THE HOTSPOT, EXCEPT IN ONE BAND. SPR_OFF_HOTX and _HOTY are
 * set to half of SPR_OFF_W and _H, skipped for keys from 0x01680780 to
 * 0x01680980 inclusive -- eight of the forty-four, which keep whatever the
 * sprite file gave them. The halving is a signed divide, `cdq/sub/sar`, so it
 * rounds toward zero rather than down.
 *
 * The last three blocks pass a REAL type to MakeAaiRecord rather than -1, so
 * they are seeded from the object.aai record for their (type, key) where the
 * first three are not.
 *
 * The loop and the last two also write their rect into the LIST's own
 * LISTHDR_OFF_BOX before registering it, which the first three do not.
 *
 * Written out rather than through a helper per block: the image keeps one
 * record buffer for all of them (the second singleton does not rewrite it),
 * reads the key back from its global for MakeAaiRecord, and copies the list's
 * box out of a by-value RectMake while the hit rect goes through
 * `*RectSet(temp)`. The buffer is TWO records wide -- the frame is 0x4C and
 * the record sits twelve bytes below its top -- though only the first is
 * ever written.
 *
 * AM2_AIM_PRELOAD_FLAGS is used for the flag word here. That name came from the
 * aim sprites, which is where 0x1000 was first seen; nothing in this function
 * confirms the name, only the value. Used rather than given a second spelling.
 *
 * Not exercised by any drive here as far as its effects go, but it runs at
 * startup -- its caller is the AAI table load. */
void __cdecl BuildAaiBuiltins(void)
{
    /* The list record is twelve bytes -- LISTREC_OFF_SPRITE and then two
     * int16 and a dword -- and the second singleton reuses the first's
     * without writing it again. */
#define REC_SPR (rec[0].sprite)
#define REC_A   (rec[0].dx)
#define REC_B   (rec[0].dy)
#define REC_C   (rec[0].depth)
#define KEY(a)  (*(int32_t *)(uintptr_t)(a))
    AM2_ListRec rec[2];                        /* only [0] used; see above */
    AM2_RecordList *list;
    int32_t  slot;
    uint32_t key;
    int32_t  i;

    FreeAaiTables();

    REC_SPR = PreloadSprite(0x13, 0, 0, AM2_AIM_PRELOAD_FLAGS, 1);
    REC_A = 0;
    REC_B = 0;
    REC_C = 1;
    KEY(((uintptr_t)&g_aaiKey980000)) = 0x980000;
    list = MakeRecordList(1, rec, 0x980000);
    slot = AddRecordList(list);
    AddAaiRecord(MakeAaiRecord(-1, KEY(((uintptr_t)&g_aaiKey980000)), slot,
                               *RectSet(AM2_TEMP(AM2_Rect), -0x14, -5, 0x3C, 0x23)));

    /* The SAME record again -- no second load -- under another key. */
    KEY(((uintptr_t)g_aaiKey980100)) = 0x980100;
    list = MakeRecordList(1, rec, 0x980100);
    slot = AddRecordList(list);
    AddAaiRecord(MakeAaiRecord(-1, KEY(((uintptr_t)g_aaiKey980100)), slot,
                               *RectSet(AM2_TEMP(AM2_Rect), 0, 0, 1, 1)));

    REC_SPR = PreloadSprite(0x13, 1, 0, AM2_AIM_PRELOAD_FLAGS, 1);
    REC_A = -2;
    REC_B = -2;
    REC_C = 0x5DC;
    KEY(((uintptr_t)g_aaiKey980080)) = 0x980080;
    list = MakeRecordList(1, rec, 0x980080);
    slot = AddRecordList(list);
    AddAaiRecord(MakeAaiRecord(-1, KEY(((uintptr_t)g_aaiKey980080)), slot,
                               *RectSet(AM2_TEMP(AM2_Rect), -2, -2, 2, 2)));

    i = 0;
    for (key = 0x01680000; key < 0x01681600; key += 0x80) {
        REC_SPR = PreloadSprite(0x2D, i, 0,
                                           AM2_AIM_PRELOAD_FLAGS, 1);
        if (!REC_SPR)
            REC_SPR = PreloadSprite(0x2D, 0x0A, 0,
                                               AM2_AIM_PRELOAD_FLAGS, 1);

        if (key < 0x01680780 || key > 0x01680980) {
            /* SPR_OFF_W and SPR_OFF_H: the bounds' right and bottom. */
            REC_SPR->hotX = (int16_t)(REC_SPR->bounds.right / 2);
            REC_SPR->hotY = (int16_t)(REC_SPR->bounds.bottom / 2);
        }

        REC_A = 0;
        REC_B = 0;
        REC_C = 0x3E8;
        list = MakeRecordList(1, rec, key);

        /* The loop blocks give the LIST its own box; the singletons do not. */
        list->box =
            RectMake(-0x10, -0x10, 0x10, 0x10);
        slot = AddRecordList(list);
        AddAaiRecord(MakeAaiRecord(0x2D, key, slot,
                                   *RectSet(AM2_TEMP(AM2_Rect), -0x10, -0x10, 0x10, 0x10)));
        i++;
    }

    REC_SPR = PreloadSprite(0x1D, 0x0B, 0, AM2_AIM_PRELOAD_FLAGS, 1);
    REC_A = 0;
    REC_B = 0;
    REC_C = 3;
    KEY(((uintptr_t)&g_createWatchedKind)) = 0xE80580;
    list = MakeRecordList(1, rec, 0xE80580);
    list->box =
        RectMake(-0x10, -0x10, 0x10, 0x10);
    slot = AddRecordList(list);
    AddAaiRecord(MakeAaiRecord(0x1D, KEY(((uintptr_t)&g_createWatchedKind)), slot,
                               *RectSet(AM2_TEMP(AM2_Rect), -0x10, -0x10, 0x10, 0x10)));

    REC_SPR = PreloadSprite(0x1D, 0x0C, 9, AM2_AIM_PRELOAD_FLAGS, 1);
    REC_A = 0;
    REC_B = 0;
    REC_C = 3;
    KEY(((uintptr_t)&g_watchedTypeId)) = 0xE80609;
    list = MakeRecordList(1, rec, 0xE80609);
    list->box =
        RectMake(-0x10, -0x10, 0x10, 0x10);
    slot = AddRecordList(list);
    AddAaiRecord(MakeAaiRecord(0x1D, KEY(((uintptr_t)&g_watchedTypeId)), slot,
                               *RectSet(AM2_TEMP(AM2_Rect), -0x10, -0x10, 0x10, 0x10)));
#undef KEY
#undef REC_C
#undef REC_B
#undef REC_A
#undef REC_SPR
}

/* 0x004344A0, seven callers, and every one of them hands the result straight
 * to 0x004345A0 -- the same make-then-register shape MakeRecordList and
 * AddRecordList have above.
 *
 * It allocates a zeroed 0x40-byte record, writes seven arguments into it, and
 * then -- only when the type is not negative -- SEEDS ten more fields from the
 * object.aai record for that (type, key) through DefFindObjRec. So the record
 * is an instance of a definition. What it is an instance OF is not established
 * here and the name claims nothing more than where the fields come from.
 *
 * A NEGATIVE TYPE BECOMES 0x2E AND SKIPS THE LOOKUP. Both halves of that are
 * the same test, spelled twice: the store picks 0x2E, and the branch further
 * down skips the seeding. So a record built with a negative type carries a
 * type of 46 and none of the definition's fields, which is a different thing
 * from a record whose lookup missed -- that one keeps its real type and is
 * also unseeded. The two are indistinguishable afterwards.
 *
 * THE KEY IS SPLIT WITH PackKey's ARITHMETIC -- low 7 bits, then the next 10 --
 * which is the same split KeyLookupTriple performs. That is what ties this to
 * the .aai vocabulary rather than to some table of its own.
 *
 * THE SEEDED COPY IS NOT IN ORDER, and the last two fields are the trap: the
 * destination's +0x38 takes the source's +0x30 and its +0x3A takes the
 * source's +0x28. Everything before them ascends in step. Transcribing the
 * block as a run would swap those two and nothing here would notice.
 *
 * THE TAIL WRITES BACK INTO THE RECORD LIST. When a slot was given, every
 * twelve-byte entry of that list gets its +8 set to the record's +0x30 --
 * zero-extended from a uint16 into an int32. The count is re-read from the
 * header on every iteration, which cannot change anything and is reproduced as
 * the plain loop it amounts to.
 *
 * MEASURED AT 151 CALLS on a driven Boot Camp mission, matching
 * AddRecordList exactly on the same run -- so the two really are
 * one-for-one, and the make-then-register reading holds by count as well
 * as by call site. The negative-type arm is reached by construction rather
 * than by luck: the caller at 0x00434789 pushes a literal -1, so the 0x2E
 * substitution and the skipped lookup are both on the compared path.
 * DefFindObjRec reads 1,689 on the same run, which is its OTHER callers --
 * ours reaches it by name.
 */
AM2_AaiRec *__cdecl MakeAaiRecord(int32_t type, uint32_t key, int32_t slot,
                                  AM2_Rect box)
{
    AM2_AaiRec *rec = (AM2_AaiRec *)am2_malloc(AM2_AAI_RECORD_BYTES);
    AM2_DefObjRec *def;

    memset(rec, 0, AM2_AAI_RECORD_BYTES);

    if (type < 0)
        rec->type  = 0x2E;
    else
        rec->type  = type;
    rec->key        = key;
    rec->slot       = -1;  /* AddAaiRecord fills it */
    rec->listSlot   = slot;
    rec->box.left   = box.left;
    rec->box.right  = box.right;
    rec->box.top    = box.top;
    rec->box.bottom = box.bottom;

    /* One return for every path; the negative type skips the definition
     * and the list. */
    if (type >= 0) {
        def = DefFindObjRec(type, (key >> 7) & 0x3FF, key & 0x7F);
        if (def) {
            rec->defType        = rec->type;
            rec->orFlags        = (int32_t)def->flags;
            rec->health         = def->health;
            rec->heightAdj      = def->heightAdj;
            rec->rank           = def->rank;
            rec->depth          = def->depth;
            rec->depthSlopeBits = def->depthSlopeBits;
            /* Out of order, and deliberately: +0x38 takes +0x30, +0x3A takes
               +0x28. */
            rec->crushDamage    = def->crushDamage;
            rec->armour         = def->armour;
            rec->field3C        = def->field34;
        }

        if (slot > 0) {
            /* The count and the record array are read afresh each pass: the
             * store could alias either. */
            AM2_RecordList *list = g_recordLists[slot];
            int32_t  i;

            for (i = 0; i < list->count; i++)
                list->records[i].depth =
                    (int32_t)(uint16_t)rec->depth;
        }
    }
    return rec;
}

/* 0x004345A0, seven callers -- the register half of MakeAaiRecord above, and
 * structurally the SAME FUNCTION as AddRecordList: null in, -1 out; a halving
 * search that doubles as the duplicate check; a grow of both arrays together;
 * a memmove to open the gap; the same `(lo << 29) - lo + count` shifted left
 * by three, which is (count - lo) * 8 after the overflow.
 *
 * Three differences and they are all it takes to make it a second function.
 * It keys on the record's AAIREC_OFF_KEY rather than on a header's owner. It
 * writes the slot back into AAIREC_OFF_SLOT, which MakeAaiRecord seeded to -1
 * -- so an unregistered record is distinguishable from a registered one, which
 * the record-list header has no equivalent of. And it grows by NINETEEN where
 * the other grows by seventeen. Two nearby tables with different growth
 * constants is exactly the sort of thing a reader assumes is one constant.
 *
 * ADDR_KEY_TABLE is the sorted half, and that is the table KeyLookup and
 * KeyLookupTriple search -- so this is where the entries they find come from,
 * and ADDR_AAI_RECORDS is what a found value indexes. That makes the naming of
 * this pair evidenced from three sides rather than from its own body: the
 * maker seeds from object.aai, the readers were already named, and this is the
 * only writer between them.
 *
 * The same two unchecked reallocs, and the same zeroing from the OLD capacity
 * for exactly one grow's worth of entries.
 *
 * MEASURED AT 151 CALLS, and so are the other three: MakeRecordList,
 * AddRecordList, MakeAaiRecord and this all read exactly 151 on one driven
 * Boot Camp mission. So each of the 151 things gets a record list AND an
 * AAI record, and both are registered -- the two pairs run in lockstep,
 * which no single function's body says and four counters do.
 *
 * None of the 151 returned -1, so the duplicate-key exit is the one arm
 * unexercised here, as it is in AddRecordList. 151 slots at nineteen per
 * grow puts both reallocs at about eight runs.
 */
int32_t __cdecl AddAaiRecord(AM2_AaiRec *rec)
{
    uint32_t key;
    int32_t  lo, hi, grown;

    if (!rec)
        return -1;

    /* AddRecordList's twin, over the key table: every global re-read where
     * it is used, a shifted midpoint, a `while` on hi > lo. */
#define COUNT (g_keyTableN)
#define CAP   (g_aaiRecordCap)
#define RECS  (g_aaiRecords)
#define INDEX (*(uint32_t * *)&g_keyTable)
    hi  = COUNT;
    key = rec->key;
    lo  = 0;

    while (hi > lo) {
        int32_t mid = ((hi - lo) >> 1) + lo;

        if (INDEX[mid * 2] == key)
            return -1;
        if (INDEX[mid * 2] < key)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (COUNT + 1 > CAP) {
        grown = COUNT + AM2_AAI_RECORD_GROW;
        RECS = (AM2_AaiRec **)am2_realloc(RECS, (size_t)grown * 4);
        memset(RECS + CAP, 0, AM2_AAI_RECORD_GROW * 4);
        INDEX = (uint32_t *)am2_realloc(INDEX, (size_t)grown * 8);
        memset(INDEX + CAP * 2, 0, AM2_AAI_RECORD_GROW * 8);
        CAP = grown;
    }

    rec->slot = COUNT;
    RECS[COUNT] = rec;

    if (lo < COUNT)
        orig_memmove(INDEX + (lo + 1) * 2, INDEX + lo * 2,
                     (size_t)(COUNT - lo) * 8);

    INDEX[lo * 2]     = key;
    INDEX[lo * 2 + 1] = (uint32_t)COUNT;
    return COUNT++;
#undef COUNT
#undef CAP
#undef RECS
#undef INDEX
}

typedef void (__cdecl *AM2_ObjAfterMoveFn)(void *obj, int32_t a, int32_t b);
/* ObjAfterMove is reconstructed, in region.cpp, and called by name. */

/* InitObjFromAai -- original 0x00433880, two callers.
 *
 * Build an object out of an AAI record: OR the record's flags into the
 * object's, set the army, run the common init, copy health and two bytes
 * across, build the row set from the record's def, stamp one field on every
 * row, and finish with the post-move step. Answers 1, or 0 if the record
 * index is out of range or its slot is empty -- and one of its callers frees
 * the object on that 0.
 *
 * EIGHT ARGUMENTS. Both call sites push eight; the `add esp, 0x24` after
 * each also pops the malloc argument pushed before it, which is what once
 * made this read as nine.
 *
 * EVERY FIELD NAME ON THE AAI RECORD COMES FROM WHERE IT LANDS. +0x2C is
 * copied to OBJ_OFF_MAX_HEALTH and OBJ_OFF_HEALTH in the same breath, so it is
 * the starting health and the maximum at once; +0x2E to OBJ_OFF_HEIGHT_ADJ and
 * +0x2F to OBJ_OFF_RANK. That is the strongest naming evidence available for a
 * record with no strings of its own.
 *
 * THE FLAGS ARE OR'd FROM TWO SOURCES AND THE ORDER IS FIXED: the record's
 * +0x28 and the sixth argument are combined first, then OR'd into whatever the
 * object already had. So nothing is cleared, and a caller cannot use this to
 * turn a flag off.
 *
 * THE POSITION IS ONE PACKED DWORD READ TWICE. The fifth argument goes to
 * ObjInitCommon whole, and its two halves are pulled out separately for
 * BuildRowsFromDef -- the low word as x through a register, the high word by
 * reading two bytes further up the stack. Same value, two routes.
 *
 * THE ROW LOOP RE-READS THE COUNT EVERY ITERATION and the row array once, and
 * writes the same constant into every row. Nothing in the loop can change
 * either; written as the plain loop that means.
 */
int32_t __cdecl InitObjFromAai(AM2_Object *obj, char *name, int32_t army,
                               int32_t index, AM2_Point at, int32_t orFlags,
                               int32_t a7, int32_t a8)
{
#define o (obj)
    const AM2_AaiRec *rec;
    int32_t        i;

    if (index < 0 || index >= g_keyTableN)
        return 0;

    rec = g_aaiRecords[index];
    if (!rec)
        return 0;

    o->flags |=
        (uint32_t)(rec->orFlags | orFlags);
    o->owner = (uint8_t)army;

    ObjInitCommon(o, name, 1, at,
                         &rec->box, a7, a8);

    o->aai = rec;

    o->health =
        o->maxHealth =
        rec->health;
    o->heightAdj =
        rec->heightAdj;
    o->itemFlag =
        (uint8_t)rec->rank;

    o->deadline58 =
        *(const int32_t *)&g_gameClockMs;

    BuildRowsFromDef(&o->rowSet,
                     (g_recordLists)
                         [rec->listSlot],
                     (int32_t)at.x,
                     (int32_t)at.y,
                     o->flags);

    for (i = 0; i < o->rowSet.count; i++)
        o->rowSet.rows[i].depthSlope =
            rec->depthSlope;

    ObjAfterMove(o, 0, 0);
    return 1;
#undef o
}


/* EnsureSpriteAaiRecord -- original 0x004342E0, ten callers. Find the AAI
 * record for one sprite triple, and BUILD IT when it is not there: load the
 * sprite, make a one-entry record list for it, give the list a hit mask and a
 * box, and register both.
 *
 * ITS HEAD IS KeyLookupTriple INLINED. `((set << 12) + index) << 7 + frame` is
 * PackKey's arithmetic to the instruction, and packkey.cpp already records
 * that the original computes it inline rather than calling PackKey. That is
 * also what identifies the three arguments: they go on to PreloadSprite in
 * that order, so they are a sprite set, index and frame rather than an
 * anonymous triple.
 *
 * THE HIT PATH'S LOOP NEVER USES ITS INDUCTION VARIABLE. It counts edi up to
 * the list's count and compares, but the body is `mov eax,[esi+0xc];
 * mov eax,[eax]` -- verified against the raw bytes, 8b 46 0c / 8b 00, no SIB
 * and no index -- so it preloads entries[0] as many times as there are
 * entries instead of walking them. Reproduced rather than corrected: it is
 * the original's behaviour, and it goes unnoticed because preloading a sprite
 * that is already loaded only takes a reference.
 *
 * `edi` IS THE KEY ON THE MISS PATH AND ZERO ON THE HIT PATH. The `xor edi,edi`
 * belongs to the hit path alone, so by the time MakeRecordList and
 * MakeAaiRecord are reached edi still holds what PackKey produced -- which is
 * why the list's owner and the record's key are the same value. Reading the
 * function top to bottom as one flow gets this wrong; it is two flows.
 *
 * The box defaults to (-16,-16,16,16) ONLY when LoadMask left the hit mask
 * null, and the record's own box is the sprite's extent about its hot spot,
 * -hotx, -hoty, w - hotx, h - hoty.
 *
 * Four exits: the index when the record was already there, -1 when the .aai
 * lookup fails, -1 when the sprite will not load, and AddAaiRecord's answer
 * when it built one. */
int32_t __cdecl EnsureSpriteAaiRecord(int32_t set, int32_t index, int32_t frame)
{
    /* PackKey written out, as the image has it. */
    uint32_t  key = (uint32_t)((((set << 12) + index) << 7) + frame);
    int32_t   slot = KeyLookup(key);
    AM2_DefObjRec *rec;
    AM2_Sprite *spr;
    AM2_RecordList *list;
    /* Room for twenty list records, one used: the image's frame is sized
     * for the array, and every stack offset follows from it. */
    AM2_ListRec entry[20];
    int32_t   i;

    if (slot >= 0) {
        /* BOTH GLOBALS HOLD A POINTER TO THE ARRAY, not the array. The
         * original loads `mov eax,[0x51614c]` and only then indexes, and the
         * rest of this file already spells it
         * `(*(void ***)(uintptr_t)ADDR_RECORD_LISTS)[n]`. Indexing the global
         * itself is one dereference short -- the same mistake as the loop
         * below, made twice more in the same function. */
        const AM2_AaiRec *aai = g_aaiRecords[slot];
        const AM2_RecordList *l =
            g_recordLists[aai->listSlot];

        for (i = 0; i < l->count; i++) {
            /* TWO dereferences, and records[0] every time -- see the note
             * above. `mov eax,[esi+0xc]` is the record ARRAY, `mov eax,[eax]`
             * is records[0]'s first dword, which is its sprite, and
             * `mov ecx,[eax]` is that sprite's first dword, which is the key.
             * Stopping one short passes a heap pointer as a sprite key; it
             * cost a campaign and bootcamp A/B, failing exactly the way a
             * broken map load does. */
            const AM2_ListRec *recs = l->records;
            const AM2_Sprite  *sp   = recs->sprite;

            if (sp)
                PreloadSpriteByKey(sp->id, 0x1000, 1);
        }
        return slot;
    }

    rec = DefFindObjRec(set, index, frame);
    if (!rec)
        return -1;

    spr = PreloadSprite(set, index, frame, 0x1000, 1);
    if (!spr)
        return -1;

    /* ALL TWELVE BYTES ARE WRITTEN: the third dword is the def record's
     * +0x20 (`mov eax,[edx+0x20]` from the spilled DefFindObjRec answer).
     * It was left unset here for a long time, on a misreading that only the
     * first eight were -- MakeRecordList copies all twelve, so every list
     * made on this path carried a stale stack dword where the original has
     * the def's value. */
    entry[0].sprite = spr;
    entry[0].dx     = 0;
    entry[0].dy     = 0;
    entry[0].depth  = rec->depthDword;

    list = MakeRecordList(1, entry, key);
    LoadMask(&list->mask, set, index, frame);

    if (!list->mask.bits) {
        AM2_Rect box, r;

        /* Copied out of RectSet's answer and then into the list, through
         * a named local: the image's copy goes via two spill slots. */
        r = *RectSet(&box, -0x10, -0x10, 0x10, 0x10);
        list->box = r;
    }

    {
        int32_t s = AddRecordList(list);
        int32_t hx = spr->hotX;
        int32_t hy = spr->hotY;
        int32_t w  = spr->bounds.right;     /* SPR_OFF_W */
        int32_t h  = spr->bounds.bottom;    /* SPR_OFF_H */

        AM2_Rect r;

        r.left   = -hx;
        r.top    = -hy;
        r.right  = w + r.left;
        r.bottom = h + r.top;
        return AddAaiRecord(MakeAaiRecord(set, (int32_t)key, s, r));
    }
}

/* ObjInitCommon -- original 0x00429940, eight callers. The shared tail of
 * every object constructor: register the object, give it a unique script
 * name, place it, copy its box and build the list of map cells it occupies.
 *
 * IT RETURNS int32 AND WAS DECLARED void, which only started to matter once
 * it became ours. Both exits set eax deliberately -- `xor eax,eax` when the
 * object gets no cell list, and the BYTE SIZE of the list when it does. Four
 * of the eight callers are still the original's, and a void reconstruction
 * would have handed those whatever eax happened to hold. Our four ignore the
 * result, which is exactly why the wrong prototype survived: nothing read it.
 * The same shape as ADDR_RECT_SET's `void`, one header along.
 *
 * ITS NAME BLOCK IS ScriptBindUniqueName INLINED, and the offsets prove it
 * rather than the shape: the index goes to SCRIPT_REF_OFF_NAME_INDEX and the
 * value comes from SCRIPT_REF_OFF_VALUE, the very fields that function uses,
 * because AN OBJECT'S FIRST SIXTEEN BYTES ARE A SCRIPT-REF RECORD. Same
 * lower-case-in-place, same adopt-an-entry-whose-value-is-zero, same "%s_%d"
 * formatted from the ORIGINAL name so suffixes cannot accumulate, same
 * AM2_NAME_TYPE_REF on the append. Written against that reconstruction rather
 * than from scratch, which is where its comments about the buffer belong too.
 *
 * THE COUNTER LIVES IN ARG1'S STACK SLOT and the three `lea`s that look like
 * three buffers are ONE 0x40-byte buffer at three different push depths. Both
 * come from tracking esp, not from reading operands: arg1 is dead once `obj`
 * is in ebp, so MSVC reused the slot.
 *
 * ARG6 IS NEVER READ. Kept in the signature because eight callers push it.
 *
 * The cell list is built only when OBJ_FLAG_BIT0 is set. Each axis of the box
 * is narrowed by 2 when it is wider than 2, shifted down by AM2_CELL_SHIFT and
 * widened by 2, and the two are multiplied AS BYTES -- `imul dl` is an 8-bit
 * multiply, so a footprint whose product exceeds 255 wraps, and the count
 * field is a byte to match. Reproduced with the truncation explicit.
 *
 * The four box offsets are emitted 0, 2, 1, 3 and the hit rect 0, 1, 2, 3;
 * that is scheduling, not meaning, and both are written in field order here.
 */
int32_t __cdecl ObjInitCommon(AM2_Object *obj, char *name, int32_t type,
                              AM2_Point at, const AM2_Rect *box,
                              int32_t unused, uint32_t uid)
{
    AM2_Object        *o = obj;
    const AM2_Rect *b = box;
    char            tried[AM2_SCRIPT_UNIQUE_BUF];
    int32_t         n;
    int32_t         bytes = 0;
    int16_t         x, y;

    (void)unused;

    *(int32_t *)o = type;
    AddToItemList((AM2_Object *)obj, uid);

    if (!name || !*name) {
        obj->nameIdx = -1;
    } else {
        n = 1;
        orig_strlwr(name);
        strcpy(tried, name);

        {
            int32_t index = ScriptFindName(tried);

            while (index >= 0) {
                if (kScriptNames[index].value == 0) {
                    obj->nameIdx = index;
                    kScriptNames[index].value =
                        (int32_t)o->uid;
                    goto named;
                }
                am2_sprintf(tried, (const char *)((void *)(uintptr_t)(AM2_STR_UNIQUE_SUFFIX)),
                            name, n++);
                index = ScriptFindName(tried);
            }
            o->nameIdx =
                AddNameTableName(tried, AM2_NAME_TYPE_REF,
                                 (int32_t)o->uid);
        }
named:
        ;
    }

    x = at.x;
    y = at.y;
    o->pos.x = x;
    o->pos.y = y;
    o->tile =
        (uint16_t)TileOfPoint(o->pos);

    {
#define keep (&o->box)
#define hit  (&o->hit)

        keep->left   = b->left;
        keep->right  = b->right;
        keep->top    = b->top;
        keep->bottom = b->bottom;

        hit->left   = b->left + at.x;
        hit->top    = b->top + at.y;
        hit->right  = b->right + x;
        hit->bottom = b->bottom + at.y;
#undef hit
#undef keep
    }

    /* THE NO-CELLS EXIT IS AN EARLY RETURN, not a skipped block. `test al,1 /
     * jne` falls straight into the epilogue with eax zeroed, so an object
     * without a cell list never reaches ItemLinkCells either. Writing this as
     * two independent ifs with a shared tail linked every such object into the
     * map a second time, which cost a campaign A/B: the load never finished
     * and five log lines from "calculating region data..." on were missing. */
    if (!(o->flagsLo & OBJ_FLAG_BIT0))
        return 0;

    {
        int32_t  w = b->right - b->left;
        int32_t  h = b->bottom - b->top;
        int32_t  i;

        if (w > 2)
            w -= 2;
        if (h > 2)
            h -= 2;
        h >>= AM2_CELL_SHIFT;
        w >>= AM2_CELL_SHIFT;

        /* `add al,2` / `add dl,2` / `imul dl`: an 8-bit signed multiply whose
         * low byte is what the count field keeps. */
        {
            int8_t hc = (int8_t)((int8_t)h + 2);
            int8_t wc = (int8_t)((int8_t)w + 2);

            o->cellCount = (uint8_t)(int8_t)(hc * wc);
        }

        bytes = (int32_t)o->cellCount * 0x10;
        o->cellEntries = (AM2_CellEntry *)am2_malloc((size_t)bytes);

#define cells (((uint8_t *)o->cellEntries))
        for (i = 0; i < o->cellCount; i++) {
            *(void **)(cells + i * 0x10) = obj;
            *(int32_t *)(cells + i * 0x10 + 8) = 0;
            *(int32_t *)(cells + i * 0x10 + 4) = 0;
            *(int32_t *)(cells + i * 0x10 + 0xC) = -1;
        }
    }

    if (!(o->flagsLo & OBJ_FLAG_DESTROYED))
        ItemLinkCells((AM2_Object *)obj, g_objMapDesc);

    return bytes;
#undef cells
}


/* ApplyObjFrame -- original 0x00434F20, three callers, and the name is the
 * image's own by way of ChangeObjectFrame, which is its only caller here.
 *
 * Give an object a new appearance: look up the def-obj record for the sprite
 * triple, tear the old subrecord down, make sure a record list exists for the
 * new key, rebuild the rows, copy the record's fields onto the object, resize
 * its box to the new sprite and tell the map it moved. Answers 1 when it did
 * the work and 0 when it could not.
 *
 * READ IT BESIDE InitObjFromAai AND EnsureSpriteAaiRecord, both in this file,
 * and two thirds of it is already written. The build path is
 * EnsureSpriteAaiRecord's miss path almost instruction for instruction --
 * PreloadSprite, a twelve-byte entry, MakeRecordList(1, entry, key),
 * LoadMask into +0x10, the RectSet(-16,-16,16,16) fallback when
 * the embedded mask had no bits, AddRecordList -- and the field copying
 * is InitObjFromAai's with a different record. That is why it needed six
 * offset names and no new function name at all.
 *
 * TWO DIFFERENCES FROM THOSE TWO, and each is a fact rather than a
 * variation. (The entry's third dword, from the def-obj record's +0x20 read
 * as a DWORD, was once listed here as a third; EnsureSpriteAaiRecord writes
 * it the same way, and only our transcription of it had left it unset.)
 *
 *   FLAGS ARE ASSIGNED, NOT OR'd. InitObjFromAai ORs the record's flags in;
 *   this writes them over OBJ_OFF_FLAGS whole. A frame change can therefore
 *   clear a flag where a create cannot.
 *
 *   SET 0x16 GETS AN EXTRA LOAD BIT. Every other set loads with 0x1000 and
 *   that one with 0x1080. One `cmp` and one `mov`; nothing else in the image
 *   does this.
 *
 * THE KEY IS PackKey WRITTEN OUT INLINE -- `((set << 12) + index) << 7 |
 * frame`, byte for byte what packkey.cpp reconstructs -- and the FIRST thing
 * the function does with it is compare it against the subrecord's own first
 * dword. If they match the object is already showing this frame and it
 * returns 1 having done nothing, which is the common case.
 *
 * ITS FIFTH ARGUMENT SETTLES OBJ_OFF_FORMATION_SLOT, and from the other end
 * than CreateItem did. flag non-zero writes the frame to +0xA0; flag zero
 * writes it to OBJ_OFF_REPAIR_FRAME and CLEARS +0xA0. orig.h records that
 * LoadType1 replays +0xA0 through ChangeObjectFrame with flag 1 and
 * OBJ_OFF_REPAIR_FRAME with flag 0 -- so the save format and this function
 * agree exactly, which is as close to a definition of that flag as the image
 * offers. Type 1 and type 4 only; anything else skips the pair.
 *
 * THE NULL CHECK ON `obj` COMES AFTER A DOZEN DEREFERENCES and after one more
 * STORE -- `test esi,esi` sits two instructions before the `je` and the write
 * to OBJ_OFF_RANK is scheduled between them. It cannot fire: the argument was
 * dereferenced in the third instruction of the function. Reproduced as the
 * plain test it is, because removing it would be a decision about code the
 * compiler kept.
 *
 * THE BOX IS THE SPRITE'S EXTENT ABOUT ITS HOT SPOT -- -hotx, -hoty,
 * w - hotx, h - hoty, the same four expressions EnsureSpriteAaiRecord builds
 * -- and ItemSetBox is called ONLY when all four differ from what the object
 * already has. The sprite comes from row zero, so an object with no rows, or
 * one whose first row has no sprite, skips the box and the height together.
 *
 * ONE `add esp` CLEANS TWO CALLS. FreeSubrecordRows' argument is left on the
 * stack and popped with BuildRowsFromDef's five, which is why the disassembly
 * shows `add esp, 0x18` after a five-argument call. Stack accounting, not a
 * sixth argument.
 */
int32_t __cdecl ApplyObjFrame(AM2_Object *obj, int32_t set, int32_t index,
                              int32_t frame, int32_t flag)
{
    AM2_Object  *o = obj;
    uint32_t  key;
    AM2_DefObjRec *rec;
    uint8_t  *sub;
    int32_t   slot;
    int32_t   loadFlags;
    int32_t   i;

    /* PackKey, inline, exactly as the original writes it. */
    key = (uint32_t)((((set << 12) + index) << 7) + frame);

    {
        const AM2_RecordList *cur = o->rowSet.list;

        if (!cur)
            return 0;
        if (cur->owner == key)
            return 1;
    }

    rec = DefFindObjRec(set, index, frame);
    if (!rec)
        return 0;

    ItemTeardown(obj);

    loadFlags = 0x1000;
    if (set == AM2_SET_WITH_EXTRA_LOAD_FLAG)
        loadFlags = 0x1080;

    slot = FindRecordList(key);
    if (slot < 0) {
        AM2_ListRec entry[20];                      /* one used */
        AM2_Sprite *spr;
        AM2_RecordList *list;

        spr = PreloadSprite(set, index, frame, loadFlags, 1);
        if (!spr)
            return 0;

        entry[0].sprite = spr;
        entry[0].dx     = 0;
        entry[0].dy     = 0;
        /* The DWORD at DEF_OBJ_REC_OFF_DEPTH, not the int16 that name means --
         * see orig.h. */
        entry[0].depth  =
            rec->depthDword;

        list = MakeRecordList(1, entry, key);
        LoadMask(&list->mask, set, index, frame);

        if (!list->mask.bits) {
            AM2_Rect box, r;

            r = *RectSet(&box, -0x10, -0x10, 0x10, 0x10);
            list->box = r;
        }

        slot = AddRecordList(list);
    }

    sub = (uint8_t *)&o->rowSet;
    FreeSubrecordRows((AM2_RowSet *)sub);
    BuildRowsFromDef((AM2_RowSet *)sub,
                     (g_recordLists)[slot],
                     (int32_t)o->pos.x,
                     (int32_t)o->pos.y,
                     o->flags);

    for (i = 0; i < o->rowSet.count; i++)
        o->rowSet.rows[i].depthSlope =
            rec->depthSlope;

    /* ASSIGNED, not OR'd -- see the note above. */
    o->flags =
        rec->flags;

    if (!flag)
        o->health =
            o->maxHealth =
                rec->health;

    o->heightAdj =
        rec->heightAdj;
    o->itemFlag =
        (uint8_t)rec->rank;

    /* The original's own null test, kept -- it is scheduled AFTER the store
     * above and cannot fire. */
    if (o) {
        int32_t type = *(const int32_t *)o;

        if (type == 1 || type == 4) {
            if (flag) {
                o->formationSlot = frame;
            } else {
                o->formationSlot = 0;
                o->repairFrame   = frame;
            }
        }
    }

    if (o->rowSet.count > 0) {
        const AM2_Sprite *spr = o->rowSet.rows->sprite;

        if (spr) {
            int32_t l = -(int32_t)spr->hotX;
            int32_t t = -(int32_t)spr->hotY;
            int32_t r = spr->bounds.right + l;      /* SPR_OFF_W */
            int32_t b = spr->bounds.bottom + t;     /* SPR_OFF_H */

            if (l != o->box.left
                || t != o->box.top
                || r != o->box.right
                || b != o->box.bottom) {
                AM2_Rect box;

                box.left   = l;
                box.top    = t;
                box.right  = r;
                box.bottom = b;
                ItemSetBox((AM2_Object *)obj, box);
            }
        }

        ApplyObjHeight((AM2_Object *)obj,
                       (int32_t)o->heightSet);
        RowUpdate(o->rowSet.rows, 0,
                  g_mapDesc);
    }

    ObjAfterMove(obj, 1,
                        rec->crushDamageDword);
    return 1;
}


/* 0x00413690, SelectionClick's filter -- which of our objects a click may
 * pick. Both of its references are in that one function, one as a callback
 * and one as a direct call.
 *
 * IT LIVES HERE RATHER THAN WITH SelectionClick, and the LINK decided that:
 * item.cpp hands it to WalkCellAtPoint twice, item.cpp is in SELFTEST_SRC, and
 * that link has no DirectX -- so a home in win32/ would break it. The function
 * touches no Win32 at all and both predicates it calls are in this file.
 *
 * SIX EXITS, FIVE OF THEM REFUSALS, and they were written from the epilogues
 * rather than read top to bottom: not ours, gone, dead, not an owned type,
 * busy, or a vehicle with no seat left. Only the fall-through answers 1.
 *
 * Two are worth spelling out. "Dead" is health == 0 AND max health > 0, so an
 * object with no health bar at all is not treated as a corpse. And the seat
 * test applies to VEHICLES ONLY -- ObjIsType3 gates it -- which is why a full
 * transport cannot be selected while a full squad can. */
int32_t __cdecl ObjIsSelectable(AM2_Object *o)
{
    const AM2_Object *obj = o;

    if (obj->owner != (int32_t)g_defaultOwner)
        return 0;

    if ((int32_t)obj->flags & AM2_SIGHT_DROP)
        return 0;

    if (obj->health == 0
            && obj->maxHealth > 0)
        return 0;

    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return 0;

    if (obj->aiDriven != 0)
        return 0;

    if (ObjIsType3((const AM2_Object *)obj)
            && obj->riders.count <= 0)
        return 0;

    return 1;
}

int objtype_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_INIT_OBJ_FROM_AAI,
                        (const void *)InitObjFromAai,
                        "InitObjFromAai", 2);
    rc |= patch_replace(ADDR_APPLY_OBJ_FRAME, (const void *)ApplyObjFrame,
                        "ApplyObjFrame", 3);

    rc |= patch_replace(ADDR_OBJ_EVENT_MASK, (const void *)ObjEventMask,
                        "ObjEventMask", 1);

    rc |= patch_replace(ADDR_OBJ_IS_ITEM, (const void *)ObjIsItem, "ObjIsItem", 1);
    rc |= patch_replace(ADDR_MAKE_RECORD_LIST, (const void *)MakeRecordList,
                        "MakeRecordList", 8);
    rc |= patch_replace(ADDR_ADD_RECORD_LIST, (const void *)AddRecordList,
                        "AddRecordList", 8);
    rc |= patch_replace(ADDR_FREE_RECORD_LIST, (const void *)FreeRecordList,
                        "FreeRecordList", 1);
    rc |= patch_replace(ADDR_FIND_RECORD_LIST, (const void *)FindRecordList,
                        "FindRecordList", 1);
    rc |= patch_replace(ADDR_MAKE_AAI_RECORD, (const void *)MakeAaiRecord,
                        "MakeAaiRecord", 7);
    rc |= patch_replace(ADDR_ADD_AAI_RECORD, (const void *)AddAaiRecord,
                        "AddAaiRecord", 7);
    rc |= patch_replace(ADDR_BUILD_AAI_BUILTINS,
                        (const void *)BuildAaiBuiltins,
                        "BuildAaiBuiltins", 1);
    rc |= patch_replace(ADDR_SUBREC_HIDE_ROWS, (const void *)SubrecHideRows,
                        "SubrecHideRows", 3);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE2, (const void *)ObjIsType2, "ObjIsType2", 1);
    rc |= patch_replace(ADDR_TYPE2_FIELD5A4_SET, (const void *)Type2Field5A4Set,
                        "Type2Field5A4Set", 4);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE3, (const void *)ObjIsType3, "ObjIsType3", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE238, (const void *)ObjIsTypeIn238, "ObjIsTypeIn238", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE8, (const void *)ObjIsType8, "ObjIsType8", 1);
    rc |= patch_replace(ADDR_OBJ_IS_TYPE4, (const void *)ObjIsType4, "ObjIsType4", 1);
    rc |= patch_replace(ADDR_OBJ_TYPE2_FIELD548, (const void *)ObjType2Field548,
                        "ObjType2Field548", 1);
    rc |= patch_replace(ADDR_LOOKUP_TYPE3_BY_UID, (const void *)LookupType3ByUID,
                        "LookupType3ByUID", 1);
    rc |= patch_replace(ADDR_OBJ_INIT_COMMON, (const void *)ObjInitCommon,
                        "ObjInitCommon", 7);
    rc |= patch_replace(ADDR_ENSURE_SPRITE_AAI_REC,
                        (const void *)EnsureSpriteAaiRecord,
                        "EnsureSpriteAaiRecord", 3);
    patch_replace(ADDR_SELECTABLE_PRED, (const void *)ObjIsSelectable,
                  "ObjIsSelectable", 1);
    return rc;
}
