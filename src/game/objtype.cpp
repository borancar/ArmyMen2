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
#include "crt.h"           /* am2_malloc -- the game's own heap */
#include "objtable.h"
#include "objflag.h"       /* ObjFlagClear0 -- reconstructed */
#include "misc.h"          /* CommArmyOfSlot -- reconstructed */
#include "defparse.h"      /* DefFindObjRec -- reconstructed */
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

int objtype_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_OBJ_EVENT_MASK, (const void *)ObjEventMask,
                        "ObjEventMask", 1);

    rc |= patch_replace(ADDR_OBJ_IS_ITEM, (const void *)ObjIsItem, "ObjIsItem", 1);
    rc |= patch_replace(ADDR_MAKE_RECORD_LIST, (const void *)MakeRecordList,
                        "MakeRecordList", 8);
    rc |= patch_replace(ADDR_ADD_RECORD_LIST, (const void *)AddRecordList,
                        "AddRecordList", 8);
    rc |= patch_replace(ADDR_MAKE_AAI_RECORD, (const void *)MakeAaiRecord,
                        "MakeAaiRecord", 7);
    rc |= patch_replace(ADDR_ADD_AAI_RECORD, (const void *)AddAaiRecord,
                        "AddAaiRecord", 7);
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
    return rc;
}
