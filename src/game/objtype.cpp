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
extern "C" AM2_Sprite *__cdecl PreloadSprite(int32_t set, int32_t index,
                                             int32_t frame, int32_t flags,
                                             int32_t addref);
extern "C" AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t key, int32_t a,
                                                  int32_t b);

/* The CRT's _strlwr, which ObjInitCommon applies to the CALLER's buffer. Same
 * seam map.cpp and definfo.cpp use; it is CRT, so it stays original. */
typedef char *(__cdecl *AM2_StrlwrFn)(char *);
#define orig_strlwr ((AM2_StrlwrFn)AM2_IMAGE(ADDR_CRT_STRLWR))
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
    if (!obj)
        return 0;
    /* The original tests 2 <= t <= 3 first, then 8 -- a range check plus one
     * outlier, which is what a compiler makes of `t == 2 || t == 3 || t == 8`. */
    return (obj->type == 2 || obj->type == 3 || obj->type == 8) ? 1 : 0;
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
    return *(const uint32_t *)((const uint8_t *)obj + 0x548);
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

    obj = (AM2_Object *)LookupByUID(uid);
    return ObjIsType3(obj) ? obj : 0;
}

#define kEventComm (*(void *const *)(uintptr_t)ADDR_COMM_OBJECT)

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

    switch (CommArmyOfSlot(kEventComm,
                           *(const int8_t *)((const uint8_t *)obj + 0x10))) {
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

    return *(const int32_t *)((const uint8_t *)obj + OBJ_OFF_FIELD_5A4) > 0;
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
void __cdecl SubrecHideRows(void *subrec)
{
    uint8_t *sr = (uint8_t *)subrec;
    int32_t  i;

    for (i = 0; i < *(const int32_t *)(sr + SUBREC_OFF_COUNT); i++)
        ObjFlagClear0(*(uint8_t **)(sr + SUBREC_OFF_ROWS)
                      + (uint32_t)i * AM2_OBJ_ROW_STRIDE);
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
void *__cdecl MakeRecordList(int32_t count, const void *src, void *owner)
{
    const uint8_t *in;
    uint8_t       *hdr;
    uint8_t       *recs;
    int32_t        i;

    if (count <= 0)
        return (void *)0;

    hdr = (uint8_t *)am2_malloc(AM2_LISTHDR_BYTES);
    memset(hdr, 0, AM2_LISTHDR_BYTES);

    *(int32_t *)(hdr + LISTHDR_OFF_COUNT) = count;
    *(void **)(hdr + LISTHDR_OFF_OWNER)   = owner;

    recs = (uint8_t *)am2_malloc((size_t)count * AM2_LIST_RECORD_BYTES);
    *(void **)(hdr + LISTHDR_OFF_RECORDS) = recs;
    memset(recs, 0, (size_t)count * AM2_LIST_RECORD_BYTES);

    in = (const uint8_t *)src;
    for (i = 0; i < count; i++) {
        uint8_t *out = *(uint8_t **)(hdr + LISTHDR_OFF_RECORDS)
                       + (uint32_t)i * AM2_LIST_RECORD_BYTES;

        *(int32_t *)(out + 0) = *(const int32_t *)(in + 0);
        *(int16_t *)(out + 4) = *(const int16_t *)(in + 4);
        *(int16_t *)(out + 6) = *(const int16_t *)(in + 6);
        *(int32_t *)(out + 8) = *(const int32_t *)(in + 8);

        in += AM2_LIST_RECORD_BYTES;
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
int32_t __cdecl AddRecordList(void *list)
{
    uint8_t   *hdr = (uint8_t *)list;
    uint32_t   owner;
    int32_t    lo = 0, hi;
    int32_t    n, slot;
    uint32_t **pairs;

    if (!list)
        return -1;

    n     = *(const int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT;
    owner = *(const uint32_t *)(hdr + LISTHDR_OFF_OWNER);
    hi    = n;

    if (hi > 0) {
        uint32_t *ix = *(uint32_t **)(uintptr_t)ADDR_RECORD_LIST_INDEX;

        do {
            int32_t mid = lo + (hi - lo) / 2;

            if (ix[mid * 2] == owner)
                return -1;                  /* already registered */
            if (ix[mid * 2] > owner)
                hi = mid;
            else
                lo = mid + 1;
        } while (hi > lo);
    }

    if (n + 1 > *(const int32_t *)(uintptr_t)ADDR_RECORD_LIST_CAP) {
        int32_t cap = *(const int32_t *)(uintptr_t)ADDR_RECORD_LIST_CAP;
        int32_t grown = n + AM2_RECORD_LIST_GROW;
        void  **lists;
        uint32_t *ix;

        lists = (void **)am2_realloc(
                    *(void **)(uintptr_t)ADDR_RECORD_LISTS,
                    (size_t)grown * 4);
        *(void ***)(uintptr_t)ADDR_RECORD_LISTS = lists;
        memset(lists + cap, 0, AM2_RECORD_LIST_GROW * 4);

        ix = (uint32_t *)am2_realloc(
                 *(void **)(uintptr_t)ADDR_RECORD_LIST_INDEX,
                 (size_t)grown * 8);
        *(uint32_t **)(uintptr_t)ADDR_RECORD_LIST_INDEX = ix;
        memset(ix + cap * 2, 0, AM2_RECORD_LIST_GROW * 8);

        *(int32_t *)(uintptr_t)ADDR_RECORD_LIST_CAP = grown;
        n = *(const int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT;
    }

    *(int32_t *)(hdr + LISTHDR_OFF_INDEX) = n;
    (*(void ***)(uintptr_t)ADDR_RECORD_LISTS)[n] = list;

    pairs = (uint32_t **)(uintptr_t)ADDR_RECORD_LIST_INDEX;
    if (lo < n)
        orig_memmove(*pairs + (lo + 1) * 2, *pairs + lo * 2,
                     (size_t)(n - lo) * 8);

    (*pairs)[lo * 2]     = owner;
    (*pairs)[lo * 2 + 1] = (uint32_t)n;

    slot = n;
    *(int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT = n + 1;
    return slot;
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
    int32_t   lo = 0;
    int32_t   hi = *(const int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT;
    uint32_t *ix;

    if (hi <= 0)
        return -1;

    ix = *(uint32_t **)(uintptr_t)ADDR_RECORD_LIST_INDEX;

    do {
        int32_t mid = lo + (hi - lo) / 2;

        if (ix[mid * 2] == owner)
            return (int32_t)ix[mid * 2 + 1];
        if (ix[mid * 2] > owner)
            hi = mid;
        else
            lo = mid + 1;
    } while (hi > lo);

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
void __cdecl FreeRecordList(void *list)
{
    uint8_t *h = (uint8_t *)list;
    void    *p;

    if (!list)
        return;

    p = *(void *const *)(h + LISTHDR_OFF_MASK + OBJMASK_OFF_BITS);
    if (p)
        am2_free(p);

    p = *(void *const *)(h + LISTHDR_OFF_RECORDS);
    if (p)
        am2_free(p);

    am2_free(list);
}

/* One built-in AAI record, which BuildAaiBuiltins below makes eight ways. The
 * list record is twelve bytes -- LISTREC_OFF_SPRITE and then two int16 and a
 * dword -- and MakeAaiRecord takes the rect as its last FOUR arguments, which
 * the original passes by writing them into reserved stack rather than pushing.
 */
static void AaiBuiltin(void *spr, int16_t a, int16_t b, int32_t c,
                       int32_t type, uint32_t key,
                       int32_t l, int32_t t, int32_t r, int32_t bo)
{
    uint8_t  rec[AM2_LIST_RECORD_BYTES];
    AM2_Rect box;
    void    *list;
    int32_t  slot;

    *(void **)(rec + LISTREC_OFF_SPRITE) = spr;
    *(int16_t *)(rec + 4) = a;
    *(int16_t *)(rec + 6) = b;
    *(int32_t *)(rec + 8) = c;

    list = MakeRecordList(1, rec, (void *)(uintptr_t)key);

    /* The SLOT, not the list. AddRecordList's return value is what
     * MakeAaiRecord's third parameter takes -- objtype.h has always named it
     * `slot`, and passing the pointer instead loads no map at all. */
    slot = AddRecordList(list);

    RectSet(&box, l, t, r, bo);
    AddAaiRecord(MakeAaiRecord(type, (int32_t)key, slot,
                               box.left, box.top, box.right, box.bottom));
}

/* The same, for the three blocks that ALSO give the list its own
 * LISTHDR_OFF_BOX before registering it and whose rect is always the same
 * sixteen-unit square. The singletons above do not touch the list's box. */
static void AaiBuiltinBoxed(void *spr, int16_t a, int16_t b, int32_t c,
                            int32_t type, uint32_t key)
{
    uint8_t  rec[AM2_LIST_RECORD_BYTES];
    AM2_Rect box;
    void    *list;
    int32_t  slot;

    *(void **)(rec + LISTREC_OFF_SPRITE) = spr;
    *(int16_t *)(rec + 4) = a;
    *(int16_t *)(rec + 6) = b;
    *(int32_t *)(rec + 8) = c;

    list = MakeRecordList(1, rec, (void *)(uintptr_t)key);

    RectSet(&box, -0x10, -0x10, 0x10, 0x10);
    *(AM2_Rect *)((uint8_t *)list + LISTHDR_OFF_BOX_LEFT) = box;

    slot = AddRecordList(list);

    RectSet(&box, -0x10, -0x10, 0x10, 0x10);
    AddAaiRecord(MakeAaiRecord(type, (int32_t)key, slot,
                               box.left, box.top, box.right, box.bottom));
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
 * THE RUN OF 44 IS A do-while OVER THE KEY, not over an index: the key starts
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
 * The three that run in a loop also write their rect into the LIST's own
 * LISTHDR_OFF_BOX before registering it, which the singletons do not.
 *
 * AM2_AIM_PRELOAD_FLAGS is used for the flag word here. That name came from the
 * aim sprites, which is where 0x1000 was first seen; nothing in this function
 * confirms the name, only the value. Used rather than given a second spelling.
 *
 * Not exercised by any drive here as far as its effects go, but it runs at
 * startup -- its caller is the AAI table load. */
void __cdecl BuildAaiBuiltins(void)
{
    uint8_t  *spr;
    uint32_t  key;
    int32_t   i;

    FreeAaiTables();

    spr = (uint8_t *)PreloadSprite(0x13, 0, 0, AM2_AIM_PRELOAD_FLAGS, 1);
    *(int32_t *)(uintptr_t)ADDR_AAI_KEY_980000 = 0x980000;
    AaiBuiltin(spr, 0, 0, 1, -1, 0x980000, -0x14, -5, 0x3C, 0x23);

    /* The SAME record again -- no second load -- under another key. */
    *(int32_t *)(uintptr_t)ADDR_AAI_KEY_980100 = 0x980100;
    AaiBuiltin(spr, 0, 0, 1, -1, 0x980100, 0, 0, 1, 1);

    spr = (uint8_t *)PreloadSprite(0x13, 1, 0, AM2_AIM_PRELOAD_FLAGS, 1);
    *(int32_t *)(uintptr_t)ADDR_AAI_KEY_980080 = 0x980080;
    AaiBuiltin(spr, -2, -2, 0x5DC, -1, 0x980080, -2, -2, 2, 2);

    i   = 0;
    key = 0x01680000;
    do {
        void    *list;
        AM2_Rect box;
        int32_t  slot;
        uint8_t  rec[AM2_LIST_RECORD_BYTES];

        spr = (uint8_t *)PreloadSprite(0x2D, i, 0, AM2_AIM_PRELOAD_FLAGS, 1);
        if (!spr)
            spr = (uint8_t *)PreloadSprite(0x2D, 0x0A, 0,
                                           AM2_AIM_PRELOAD_FLAGS, 1);

        if (key < 0x01680780 || key > 0x01680980) {
            *(int16_t *)(spr + SPR_OFF_HOTX) =
                (int16_t)(*(const int32_t *)(spr + SPR_OFF_W) / 2);
            *(int16_t *)(spr + SPR_OFF_HOTY) =
                (int16_t)(*(const int32_t *)(spr + SPR_OFF_H) / 2);
        }

        *(void **)(rec + LISTREC_OFF_SPRITE) = spr;
        *(int16_t *)(rec + 4) = 0;
        *(int16_t *)(rec + 6) = 0;
        *(int32_t *)(rec + 8) = 0x3E8;

        list = MakeRecordList(1, rec, (void *)(uintptr_t)key);

        /* The loop blocks give the LIST its own box; the singletons do not. */
        RectSet(&box, -0x10, -0x10, 0x10, 0x10);
        *(AM2_Rect *)((uint8_t *)list + LISTHDR_OFF_BOX_LEFT) = box;

        slot = AddRecordList(list);

        RectSet(&box, -0x10, -0x10, 0x10, 0x10);
        AddAaiRecord(MakeAaiRecord(0x2D, (int32_t)key, slot,
                                   box.left, box.top, box.right, box.bottom));
        key += 0x80;
        i++;
    } while (key < 0x01681600);

    spr = (uint8_t *)PreloadSprite(0x1D, 0x0B, 0, AM2_AIM_PRELOAD_FLAGS, 1);
    *(int32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND = 0xE80580;
    AaiBuiltinBoxed(spr, 0, 0, 3, 0x1D, 0xE80580);

    spr = (uint8_t *)PreloadSprite(0x1D, 0x0C, 9, AM2_AIM_PRELOAD_FLAGS, 1);
    *(int32_t *)(uintptr_t)ADDR_WATCHED_TYPE_ID = 0xE80609;
    AaiBuiltinBoxed(spr, 0, 0, 3, 0x1D, 0xE80609);
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
void *__cdecl MakeAaiRecord(int32_t type, int32_t key, int32_t slot,
                            int32_t a, int32_t b, int32_t c, int32_t d)
{
    uint8_t *rec = (uint8_t *)am2_malloc(AM2_AAI_RECORD_BYTES);
    uint8_t *def;

    memset(rec, 0, AM2_AAI_RECORD_BYTES);

    *(int32_t *)(rec + AAIREC_OFF_TYPE)      = type >= 0 ? type : 0x2E;
    *(int32_t *)(rec + AAIREC_OFF_KEY)       = key;
    *(int32_t *)(rec + AAIREC_OFF_SLOT)      = -1;  /* AddAaiRecord fills it */
    *(int32_t *)(rec + AAIREC_OFF_LIST_SLOT) = slot;
    *(int32_t *)(rec + 0x14)                 = a;
    *(int32_t *)(rec + 0x18)                 = b;
    *(int32_t *)(rec + 0x1C)                 = c;
    *(int32_t *)(rec + 0x20)                 = d;

    if (type < 0)
        return rec;

    def = (uint8_t *)DefFindObjRec(type, (key >> 7) & 0x3FF, key & 0x7F);
    if (def) {
        *(int32_t *)(rec + 0x24) = *(const int32_t *)(rec + AAIREC_OFF_TYPE);
        *(int32_t *)(rec + 0x28) = *(const int32_t *)(def + 0x10);
        *(int16_t *)(rec + 0x2C) = *(const int16_t *)(def + 0x14);
        *(uint8_t *)(rec + 0x2E) = *(const uint8_t *)(def + 0x18);
        *(uint8_t *)(rec + 0x2F) = *(const uint8_t *)(def + 0x1C);
        *(int16_t *)(rec + 0x30) = *(const int16_t *)(def + 0x20);
        *(int32_t *)(rec + 0x34) = *(const int32_t *)(def + 0x24);
        /* Out of order, and deliberately: +0x38 takes +0x30, +0x3A takes
           +0x28. */
        *(int16_t *)(rec + 0x38) = *(const int16_t *)(def + 0x30);
        *(int16_t *)(rec + 0x3A) = *(const int16_t *)(def + 0x28);
        *(uint8_t *)(rec + 0x3C) = *(const uint8_t *)(def + 0x34);
    }

    if (slot > 0) {
        uint8_t *list = (uint8_t *)
            (*(void *const *const *)(uintptr_t)ADDR_RECORD_LISTS)[slot];
        int32_t  n    = *(const int32_t *)(list + LISTHDR_OFF_COUNT);
        uint8_t *ents = *(uint8_t *const *)(list + LISTHDR_OFF_RECORDS);
        int32_t  i;

        for (i = 0; i < n; i++)
            *(int32_t *)(ents + i * AM2_LIST_RECORD_BYTES + 8) =
                (int32_t)*(const uint16_t *)(rec + 0x30);
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
int32_t __cdecl AddAaiRecord(void *rec)
{
    uint8_t   *r = (uint8_t *)rec;
    uint32_t   key;
    int32_t    lo = 0, hi;
    int32_t    n, slot;
    uint32_t **pairs;

    if (!rec)
        return -1;

    n   = *(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT;
    key = *(const uint32_t *)(r + AAIREC_OFF_KEY);
    hi  = n;

    if (hi > 0) {
        uint32_t *ix = *(uint32_t **)(uintptr_t)ADDR_KEY_TABLE;

        do {
            int32_t mid = lo + (hi - lo) / 2;

            if (ix[mid * 2] == key)
                return -1;
            if (ix[mid * 2] > key)
                hi = mid;
            else
                lo = mid + 1;
        } while (hi > lo);
    }

    if (n + 1 > *(const int32_t *)(uintptr_t)ADDR_AAI_RECORD_CAP) {
        int32_t   cap   = *(const int32_t *)(uintptr_t)ADDR_AAI_RECORD_CAP;
        int32_t   grown = n + AM2_AAI_RECORD_GROW;
        void    **recs;
        uint32_t *ix;

        recs = (void **)am2_realloc(*(void **)(uintptr_t)ADDR_AAI_RECORDS,
                                    (size_t)grown * 4);
        *(void ***)(uintptr_t)ADDR_AAI_RECORDS = recs;
        memset(recs + cap, 0, AM2_AAI_RECORD_GROW * 4);

        ix = (uint32_t *)am2_realloc(*(void **)(uintptr_t)ADDR_KEY_TABLE,
                                     (size_t)grown * 8);
        *(uint32_t **)(uintptr_t)ADDR_KEY_TABLE = ix;
        memset(ix + cap * 2, 0, AM2_AAI_RECORD_GROW * 8);

        *(int32_t *)(uintptr_t)ADDR_AAI_RECORD_CAP = grown;
        n = *(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT;
    }

    *(int32_t *)(r + AAIREC_OFF_SLOT) = n;
    (*(void ***)(uintptr_t)ADDR_AAI_RECORDS)[n] = rec;

    pairs = (uint32_t **)(uintptr_t)ADDR_KEY_TABLE;
    if (lo < n)
        orig_memmove(*pairs + (lo + 1) * 2, *pairs + lo * 2,
                     (size_t)(n - lo) * 8);

    (*pairs)[lo * 2]     = key;
    (*pairs)[lo * 2 + 1] = (uint32_t)n;

    slot = n;
    *(int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT = n + 1;
    return slot;
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
 * NINE ARGUMENTS AND THE NINTH IS NEVER READ. Both call sites push nine and
 * `add esp, 0x24` confirms it; every stack read in the body lands on one of
 * the first eight. Third unused parameter in this tree after
 * RandomPointAhead's first and AmmChecksum's second, and as with those the
 * signature keeps it because the call sites do.
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
int32_t __cdecl InitObjFromAai(void *obj, char *name, int32_t army,
                               int32_t index, uint32_t at, int32_t orFlags,
                               int32_t a7, int32_t a8, int32_t)
{
    uint8_t       *o = (uint8_t *)obj;
    const uint8_t *rec;
    int32_t        i;

    if (index < 0 || index >= *(const int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT)
        return 0;

    rec = (*(const uint8_t *const *const *)(uintptr_t)ADDR_AAI_RECORDS)[index];
    if (!rec)
        return 0;

    *(uint8_t *)(o + OBJ_OFF_ARMY) = (uint8_t)army;
    *(uint32_t *)(o + OBJ_OFF_FLAGS) |=
        (uint32_t)(*(const int32_t *)(rec + AAI_OFF_OR_FLAGS) | orFlags);

    ObjInitCommon(o, name, 1, (int32_t)at,
                         (const int32_t *)(rec + AAI_OFF_BOX), a7, a8);

    *(const void **)(o + OBJ_OFF_FIELD_94) = rec;

    *(int16_t *)(o + OBJ_OFF_MAX_HEALTH) =
        *(const int16_t *)(rec + AAI_OFF_HEALTH);
    *(int16_t *)(o + OBJ_OFF_HEALTH) =
        *(const int16_t *)(rec + AAI_OFF_HEALTH);
    *(int8_t *)(o + OBJ_OFF_HEIGHT_ADJ) =
        *(const int8_t *)(rec + AAI_OFF_HEIGHT_ADJ);
    *(uint8_t *)(o + OBJ_OFF_RANK) =
        *(const uint8_t *)(rec + AAI_OFF_RANK);

    *(int32_t *)(o + OBJ_OFF_DEADLINE_58) =
        *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    BuildRowsFromDef(o + OBJ_OFF_SUBRECORD,
                     (*(void *const *const *)(uintptr_t)ADDR_RECORD_LISTS)
                         [*(const int32_t *)(rec + AAIREC_OFF_LIST_SLOT)],
                     (int32_t)(int16_t)at,
                     (int32_t)(int16_t)(at >> 16),
                     *(const uint32_t *)(o + OBJ_OFF_FLAGS));

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++)
        *(int32_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS)
                     + (size_t)i * AM2_OBJ_ROW_STRIDE + ROW_OFF_FIELD_28) =
            *(const int32_t *)(rec + AAI_OFF_ROW_FIELD28);

    ObjAfterMove(o, 0, 0);
    return 1;
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
    uint32_t  key = PackKey((uint32_t)set, (uint32_t)index, (uint32_t)frame);
    int32_t   slot = KeyLookup(key);
    void     *rec;
    AM2_Sprite *spr;
    uint8_t  *list;
    uint8_t   entry[AM2_LIST_RECORD_BYTES];  /* +8 deliberately unset */
    int32_t   i;

    if (slot >= 0) {
        /* BOTH GLOBALS HOLD A POINTER TO THE ARRAY, not the array. The
         * original loads `mov eax,[0x51614c]` and only then indexes, and the
         * rest of this file already spells it
         * `(*(void ***)(uintptr_t)ADDR_RECORD_LISTS)[n]`. Indexing the global
         * itself is one dereference short -- the same mistake as the loop
         * below, made twice more in the same function. */
        const uint8_t *aai = (const uint8_t *)
            (*(void *const *const *)(uintptr_t)ADDR_AAI_RECORDS)[slot];
        const uint8_t *l = (const uint8_t *)
            (*(void *const *const *)(uintptr_t)ADDR_RECORD_LISTS)
                [*(const int32_t *)(aai + AAIREC_OFF_LIST_SLOT)];

        for (i = 0; i < *(const int32_t *)(l + LISTHDR_OFF_COUNT); i++) {
            /* TWO dereferences, and records[0] every time -- see the note
             * above. `mov eax,[esi+0xc]` is the record ARRAY, `mov eax,[eax]`
             * is records[0]'s first dword, which is its sprite, and
             * `mov ecx,[eax]` is that sprite's first dword, which is the key.
             * Stopping one short passes a heap pointer as a sprite key; it
             * cost a campaign and bootcamp A/B, failing exactly the way a
             * broken map load does. */
            const uint8_t *recs =
                *(const uint8_t *const *)(l + LISTHDR_OFF_RECORDS);
            const uint8_t *sp = *(const uint8_t *const *)recs;

            if (sp)
                PreloadSpriteByKey(*(const uint32_t *)sp, 0x1000, 1);
        }
        return slot;
    }

    rec = DefFindObjRec(set, index, frame);
    if (!rec)
        return -1;

    spr = PreloadSprite(set, index, frame, 0x1000, 1);
    if (!spr)
        return -1;

    /* ONLY EIGHT OF THE TWELVE BYTES ARE WRITTEN, and MakeRecordList copies
     * all twelve -- its `out+8` comes from `in+8`. So the record's third dword
     * is whatever this frame happened to hold, in the original as much as
     * here. Not memset: zeroing it would be a different function, and it is
     * the one part of this record that cannot be reproduced by value. */
    *(AM2_Sprite **)entry = spr;
    *(int16_t *)(entry + 4) = 0;
    *(int16_t *)(entry + 6) = 0;

    list = (uint8_t *)MakeRecordList(1, entry, (void *)(uintptr_t)key);
    LoadMask(list + LISTHDR_OFF_MASK, set, index, frame);

    if (!*(const int32_t *)(list + LISTHDR_OFF_MASK + OBJMASK_OFF_BITS)) {
        AM2_Rect box;

        RectSet(&box, -0x10, -0x10, 0x10, 0x10);
        *(int32_t *)(list + LISTHDR_OFF_BOX_LEFT)   = box.left;
        *(int32_t *)(list + LISTHDR_OFF_BOX_TOP)    = box.top;
        *(int32_t *)(list + LISTHDR_OFF_BOX_RIGHT)  = box.right;
        *(int32_t *)(list + LISTHDR_OFF_BOX_BOTTOM) = box.bottom;
    }

    {
        int32_t s = AddRecordList(list);
        int32_t hx = *(const int16_t *)((const uint8_t *)spr + SPR_OFF_HOTX);
        int32_t hy = *(const int16_t *)((const uint8_t *)spr + SPR_OFF_HOTY);
        int32_t w  = *(const int32_t *)((const uint8_t *)spr + SPR_OFF_W);
        int32_t h  = *(const int32_t *)((const uint8_t *)spr + SPR_OFF_H);

        return AddAaiRecord(MakeAaiRecord(set, (int32_t)key, s,
                                          -hx, -hy, w - hx, h - hy));
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
int32_t __cdecl ObjInitCommon(void *obj, char *name, int32_t type,
                              uint32_t at, const int32_t *box,
                              int32_t unused, uint32_t uid)
{
    uint8_t        *o = (uint8_t *)obj;
    const AM2_Rect *b = (const AM2_Rect *)box;
    char            tried[AM2_SCRIPT_UNIQUE_BUF];
    int32_t         n = 1;
    int32_t         bytes = 0;
    int16_t         x, y;

    (void)unused;

    *(int32_t *)o = type;
    AddToItemList((AM2_Object *)obj, uid);

    if (!name || !*name) {
        *(int32_t *)(o + SCRIPT_REF_OFF_NAME_INDEX) = -1;
    } else {
        orig_strlwr(name);
        strcpy(tried, name);

        for (;;) {
            int32_t index = ScriptFindName(tried);

            if (index < 0) {
                *(int32_t *)(o + SCRIPT_REF_OFF_NAME_INDEX) =
                    AddNameTableName(tried, AM2_NAME_TYPE_REF,
                                     *(const int32_t *)
                                         (o + SCRIPT_REF_OFF_VALUE));
                break;
            }

            if (kScriptNames[index].value == 0) {
                *(int32_t *)(o + SCRIPT_REF_OFF_NAME_INDEX) = index;
                kScriptNames[index].value =
                    *(const int32_t *)(o + SCRIPT_REF_OFF_VALUE);
                break;
            }

            am2_sprintf(tried, (const char *)AM2_IMAGE(AM2_STR_UNIQUE_SUFFIX),
                        name, n);
            n++;
        }
    }

    x = (int16_t)(at & 0xFFFFu);
    y = (int16_t)(at >> 16);
    *(int16_t *)(o + OBJ_OFF_POS) = x;
    *(int16_t *)(o + OBJ_OFF_POS + 2) = y;
    *(uint16_t *)(o + OBJ_OFF_TILE) =
        (uint16_t)TileOfPoint(*(const uint32_t *)(o + OBJ_OFF_POS));

    {
        AM2_Rect *keep = (AM2_Rect *)(o + OBJ_OFF_BOX_OFFSETS);
        AM2_Rect *hit  = (AM2_Rect *)(o + OBJ_OFF_HIT_RECT);

        keep->left   = b->left;
        keep->top    = b->top;
        keep->right  = b->right;
        keep->bottom = b->bottom;

        hit->left   = b->left + x;
        hit->top    = b->top + y;
        hit->right  = b->right + x;
        hit->bottom = b->bottom + y;
    }

    /* THE NO-CELLS EXIT IS AN EARLY RETURN, not a skipped block. `test al,1 /
     * jne` falls straight into the epilogue with eax zeroed, so an object
     * without a cell list never reaches ItemLinkCells either. Writing this as
     * two independent ifs with a shared tail linked every such object into the
     * map a second time, which cost a campaign A/B: the load never finished
     * and five log lines from "calculating region data..." on were missing. */
    if (!(*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_BIT0))
        return 0;

    {
        int32_t  w = b->right - b->left;
        int32_t  h = b->bottom - b->top;
        uint8_t  count;
        int32_t  i;
        uint8_t *cells;

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

            count = (uint8_t)(int8_t)(hc * wc);
        }
        *(uint8_t *)(o + OBJ_OFF_CELL_COUNT) = count;

        bytes = (int32_t)count * 0x10;
        *(void **)(o + OBJ_OFF_CELL_ENTRIES) = am2_malloc((size_t)bytes);

        cells = *(uint8_t **)(o + OBJ_OFF_CELL_ENTRIES);
        for (i = 0; i < *(const uint8_t *)(o + OBJ_OFF_CELL_COUNT); i++) {
            *(void **)(cells + i * 0x10) = obj;
            *(int32_t *)(cells + i * 0x10 + 8) = 0;
            *(int32_t *)(cells + i * 0x10 + 4) = 0;
            *(int32_t *)(cells + i * 0x10 + 0xC) = -1;
        }
    }

    if (!(*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED))
        ItemLinkCells(obj, (void *)AM2_IMAGE(ADDR_OBJ_MAP_DESC));

    return bytes;
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
 * THREE DIFFERENCES FROM THOSE TWO, and each is a fact rather than a
 * variation:
 *
 *   THE ENTRY'S THIRD DWORD IS WRITTEN HERE. EnsureSpriteAaiRecord leaves it
 *   holding whatever the stack held and its own note says so; this fills it
 *   from the def-obj record's +0x20, read as a DWORD. So the twelve-byte list
 *   record's last field has a source at last, even though what the two bytes
 *   past DEF_OBJ_REC_OFF_DEPTH mean is still unread.
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
int32_t __cdecl ApplyObjFrame(void *obj, int32_t set, int32_t index,
                              int32_t frame, int32_t flag)
{
    uint8_t  *o = (uint8_t *)obj;
    uint32_t  key;
    uint8_t  *rec;
    uint8_t  *sub;
    int32_t   slot;
    int32_t   loadFlags;
    int32_t   i;

    /* PackKey, inline, exactly as the original writes it. */
    key = (uint32_t)((((set << 12) + index) << 7) + frame);

    {
        const uint8_t *cur =
            *(const uint8_t *const *)(o + OBJ_OFF_SUBRECORD + SUBREC_OFF_LIST);

        if (!cur)
            return 0;
        if (*(const uint32_t *)(cur + LISTHDR_OFF_OWNER) == key)
            return 1;
    }

    rec = (uint8_t *)DefFindObjRec(set, index, frame);
    if (!rec)
        return 0;

    ItemTeardown(obj);

    loadFlags = (set == AM2_SET_WITH_EXTRA_LOAD_FLAG) ? 0x1080 : 0x1000;

    slot = FindRecordList(key);
    if (slot < 0) {
        uint8_t    entry[AM2_LIST_RECORD_BYTES];
        AM2_Sprite *spr;
        uint8_t    *list;

        spr = PreloadSprite(set, index, frame, loadFlags, 1);
        if (!spr)
            return 0;

        *(AM2_Sprite **)entry    = spr;
        *(int16_t *)(entry + 4)  = 0;
        *(int16_t *)(entry + 6)  = 0;
        /* The DWORD at DEF_OBJ_REC_OFF_DEPTH, not the int16 that name means --
         * see orig.h. */
        *(int32_t *)(entry + 8)  =
            *(const int32_t *)(rec + DEF_OBJ_REC_OFF_DEPTH);

        list = (uint8_t *)MakeRecordList(1, entry, (void *)(uintptr_t)key);
        LoadMask(list + LISTHDR_OFF_MASK, set, index, frame);

        if (!*(const int32_t *)(list + LISTHDR_OFF_MASK + OBJMASK_OFF_BITS)) {
            AM2_Rect box;

            RectSet(&box, -0x10, -0x10, 0x10, 0x10);
            *(int32_t *)(list + LISTHDR_OFF_BOX_LEFT)   = box.left;
            *(int32_t *)(list + LISTHDR_OFF_BOX_TOP)    = box.top;
            *(int32_t *)(list + LISTHDR_OFF_BOX_RIGHT)  = box.right;
            *(int32_t *)(list + LISTHDR_OFF_BOX_BOTTOM) = box.bottom;
        }

        slot = AddRecordList(list);
    }

    sub = o + OBJ_OFF_SUBRECORD;
    FreeSubrecordRows(sub);
    BuildRowsFromDef(sub,
                     (*(void *const *const *)(uintptr_t)ADDR_RECORD_LISTS)[slot],
                     (int32_t)*(const int16_t *)(o + OBJ_OFF_POS),
                     (int32_t)*(const int16_t *)(o + OBJ_OFF_Y),
                     *(const uint32_t *)(o + OBJ_OFF_FLAGS));

    for (i = 0; i < *(const int32_t *)(o + OBJ_OFF_ROW_COUNT); i++)
        *(int32_t *)(*(uint8_t **)(o + OBJ_OFF_ROWS)
                     + (uint32_t)i * AM2_OBJ_ROW_STRIDE + ROW_OFF_FIELD_28) =
            *(const int32_t *)(rec + DEF_OBJ_REC_OFF_ROW_FIELD28);

    /* ASSIGNED, not OR'd -- see the note above. */
    *(uint32_t *)(o + OBJ_OFF_FLAGS) =
        *(const uint32_t *)(rec + DEF_OBJ_REC_OFF_FLAGS);

    if (!flag) {
        *(int16_t *)(o + OBJ_OFF_MAX_HEALTH) =
            *(const int16_t *)(rec + DEF_OBJ_REC_OFF_HEALTH);
        *(int16_t *)(o + OBJ_OFF_HEALTH) =
            *(const int16_t *)(rec + DEF_OBJ_REC_OFF_HEALTH);
    }

    *(int8_t *)(o + OBJ_OFF_HEIGHT_ADJ) =
        *(const int8_t *)(rec + DEF_OBJ_REC_OFF_HEIGHT_ADJ);
    *(uint8_t *)(o + OBJ_OFF_RANK) =
        *(const uint8_t *)(rec + DEF_OBJ_REC_OFF_RANK);

    /* The original's own null test, kept -- it is scheduled AFTER the store
     * above and cannot fire. */
    if (o) {
        int32_t type = *(const int32_t *)o;

        if (type == 1 || type == 4) {
            if (flag) {
                *(int32_t *)(o + OBJ_OFF_FORMATION_SLOT) = frame;
            } else {
                *(int32_t *)(o + OBJ_OFF_FORMATION_SLOT) = 0;
                *(int32_t *)(o + OBJ_OFF_REPAIR_FRAME)   = frame;
            }
        }
    }

    if (*(const int32_t *)(o + OBJ_OFF_ROW_COUNT) > 0) {
        const uint8_t *spr =
            *(const uint8_t *const *)(*(uint8_t **)(o + OBJ_OFF_ROWS)
                                      + ROW_OFF_SPRITE);

        if (spr) {
            int32_t l = -(int32_t)*(const int16_t *)(spr + SPR_OFF_HOTX);
            int32_t t = -(int32_t)*(const int16_t *)(spr + SPR_OFF_HOTY);
            int32_t r = *(const int32_t *)(spr + SPR_OFF_W) + l;
            int32_t b = *(const int32_t *)(spr + SPR_OFF_H) + t;

            if (l != *(const int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 0)
                || t != *(const int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 4)
                || r != *(const int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 8)
                || b != *(const int32_t *)(o + OBJ_OFF_BOX_OFFSETS + 12))
                ItemSetBox(obj, l, t, r, b);
        }

        ApplyObjHeight(obj,
                       (int32_t)*(const int8_t *)(o + OBJ_OFF_HEIGHT_SET));
        RowUpdate(*(void **)(o + OBJ_OFF_ROWS), 0,
                  (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    ObjAfterMove(obj, 1,
                        *(const int32_t *)(rec + DEF_OBJ_REC_OFF_FIELD_30));
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
int32_t __cdecl ObjIsSelectable(void *o)
{
    const uint8_t *obj = (const uint8_t *)o;

    if (*(const int8_t *)(obj + OBJ_OFF_ARMY) != (int8_t)g_defaultOwner)
        return 0;

    if (*(const int32_t *)(obj + OBJ_OFF_FLAGS) & AM2_SIGHT_DROP)
        return 0;

    if (*(const int16_t *)(obj + OBJ_OFF_HEALTH) == 0
            && *(const int16_t *)(obj + OBJ_OFF_MAX_HEALTH) > 0)
        return 0;

    if (!ObjIsTypeIn238((const AM2_Object *)obj))
        return 0;

    if (*(const int32_t *)(obj + OBJ_OFF_FIELD_94) != 0)
        return 0;

    if (ObjIsType3((const AM2_Object *)obj)
            && *(const int32_t *)(obj + OBJ_OFF_POSE_PENDING) <= 0)
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
