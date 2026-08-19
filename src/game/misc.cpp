/* misc.cpp -- see misc.h. */
#include <stdint.h>

#include "misc.h"
#include "../inject/orig.h"

/* The original inlines `repne scasb` for length; a plain loop is the same
 * function and the vectors confirm it. */
static uint32_t StrLen(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return n;
}

#include "../inject/patch.h"

uint32_t __cdecl Field53C(const void *p)
{
    return *(const uint32_t *)((const uint8_t *)p + 0x53C);
}

int32_t __cdecl AddByteSat(uint32_t base, int32_t add)
{
    int32_t sum = (int32_t)(base & 0xFFu) + add;

    if (sum > 0xFF)
        sum |= 0xFF;          /* `or al, 0xFF`, not a clamp -- see misc.h */
    return sum;
}

int32_t __cdecl CompareDword(const void *a, const void *b)
{
    return *(const int32_t *)a - *(const int32_t *)b;
}

void __cdecl CopyByteIfSet(uint32_t unused, uint8_t *dst, const void *src)
{
    const uint8_t *s = (const uint8_t *)src;

    (void)unused;
    if (!*(const uint32_t *)(s + 0x10))
        return;
    *dst = s[0x18];
}

int32_t __cdecl ScaleBy32Blocks(int32_t v)
{
    int32_t blocks = (v + (v < 0 ? 31 : 0)) >> 5;

    return (blocks + 1) * 1000;
}

void __cdecl TitleCaseName(char *text)
{
    int32_t i = 0;

    /* strlen is recomputed each time round, as the original does. */
    while (i < (int32_t)StrLen(text)) {
        if (text[i] == '_')
            text[i] = ' ';

        if (i > 0) {
            char prev = text[i - 1];

            if (prev == ' ' || prev == '.') {
                if (text[i] >= 'a' && text[i] <= 'z')
                    text[i] = (char)(text[i] - 0x20);
            }
        } else if (text[i] >= 'a' && text[i] <= 'z') {
            text[i] = (char)(text[i] - 0x20);
        }
        i++;
    }
}

void __cdecl ResetPairMask(uint32_t *a, uint32_t *b)
{
    *a = 0xFFFFFFFFu;
    *b = 0xFFFFFFFFu;
    *a &= 0xFFDBFFFDu;
}

int32_t __cdecl IsKind7(const void *p)
{
    return (*(const int32_t *)p == 7) ? 1 : 0;
}

int32_t __cdecl IsBlank(uint8_t c)
{
    return (c == ' ' || c == '\t' || c == '\r') ? 1 : 0;
}

int32_t __cdecl IsScriptDelim(uint8_t c)
{
    return (c == ')' || c == '(' || c == ',' || c == '<' || c == '=' ||
            c == '>' || c == '{' || c == '}' || c == '&' || c == '+') ? 1 : 0;
}

uint32_t __cdecl SwapColourBytes(uint32_t colour, uint32_t unused)
{
    (void)unused;
    return ((colour >> 16) & 0xFFu)
         | (((colour >> 8) & 0xFFu) << 8)
         | ((colour & 0xFFu) << 16);
}

void __stdcall NullStub4(uint32_t arg) { (void)arg; }
void __cdecl   NullStub(void)          { }
int32_t __cdecl ReturnZero(void)       { return 0; }
int32_t __cdecl ReturnOne(void)        { return 1; }

int32_t __cdecl ReverseBlocks(void *dst, const void *src, int32_t total,
                              int32_t count)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s;
    int32_t        n, i, j;

    /* The division comes BEFORE the count test in the original, so a count of
     * zero faults on the idiv rather than returning. Keeping that order means
     * count == 0 is undefined here as it is fatal there -- no vector can reach
     * it either way, since the emulator faults on the same instruction and the
     * vector is dropped. */
    n = total / count;
    s = (const uint8_t *)src + (int32_t)(count - 1) * n;

    if (count <= 0)
        return 1;

    for (i = 0; i < count; i++) {
        for (j = 0; j < n; j++)
            d[j] = s[j];
        d += n;
        s -= n;
    }
    return 1;
}

int32_t __cdecl ScriptCompare(int32_t a, int32_t op, int32_t b)
{
    switch (op) {
    case 0:  return (b == a) ? 1 : 0;
    case 1:  return (b < a) ? 1 : 0;
    case 2:  return (b > a) ? 1 : 0;
    default: return 0;
    }
}

int32_t __cdecl ComparePair(const void *a, const void *b)
{
    const int32_t *x = (const int32_t *)a;
    const int32_t *y = (const int32_t *)b;

    if (x[0] != y[0])
        return x[0] - y[0];
    if (x[1] != y[1])
        return x[1] - y[1];
    return 0;
}

/* Read out of the byte table at 0x00406988 and the eight arms it selects
 * through 0x00406968; entries that land on the default arm are 0. */
static const uint8_t kCodeMap[30] = {
    1, 2, 5, 7, 6, 9, 3, 6, 1, 2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 6, 7,
};

int32_t __cdecl MapCode(int32_t code)
{
    uint32_t i = (uint32_t)(code - 1);

    if (i > 0x1Du)
        return 0;
    return kCodeMap[i];
}

int32_t __cdecl CompareTriple(const void *a, const void *b)
{
    const int32_t *x = (const int32_t *)a;
    const int32_t *y = (const int32_t *)b;

    if (x[0] != y[0])
        return x[0] - y[0];
    if (x[1] != y[1])
        return x[1] - y[1];
    if (x[2] != y[2])
        return x[2] - y[2];
    return 0;
}

int32_t __cdecl TypesCompatible(int32_t a, int32_t b)
{
    switch (a) {
    case 8:  return (b == 29) ? 1 : 0;
    case 9:  return (b != 9) ? 1 : 0;
    case 10: return (b == 29 || b == 8) ? 1 : 0;
    default: return 0;
    }
}

/* Shared body: `mode` is the value written for the wide facings, `narrow` for
 * the two that use the other one, and the offsets say which record. */
static void SetFacing(int32_t facing, const void *src, void *out,
                      uint32_t off_mode, uint32_t off_flag,
                      int32_t wide, int32_t narrow)
{
    uint8_t *o = (uint8_t *)out;
    uint8_t  base;

    /* The read of src+0x40 belongs INSIDE the arms. Hoisting it here is the
     * obvious tidy-up and it is wrong: for a facing above 7 the original never
     * touches src at all, so a null src is harmless there and fatal if the
     * read has been lifted. The vectors found it immediately -- a page fault
     * reading address 0x40. */
    if ((uint32_t)facing > 7)
        return;
    base = *((const uint8_t *)src + 0x40);

    switch (facing) {
    case 0:
        *(int32_t *)(o + off_mode) = wide;
        o[0] = base;
        break;
    case 1:
    case 3:
        *(int32_t *)(o + off_flag) = 1;
        *(int32_t *)(o + off_mode) = wide;
        o[0] = (uint8_t)(base + 0x20);
        break;
    case 2:
        *(int32_t *)(o + off_mode) = narrow;
        o[0] = (uint8_t)(base + 0x20);
        break;
    case 4:
        *(int32_t *)(o + off_flag) = 1;
        *(int32_t *)(o + off_mode) = wide;
        o[0] = base;
        break;
    case 5:
        *(int32_t *)(o + off_flag) = 1;
        *(int32_t *)(o + off_mode) = wide;
        o[0] = (uint8_t)(base - 0x20);
        break;
    case 6:
        *(int32_t *)(o + off_mode) = narrow;
        o[0] = (uint8_t)(base - 0x20);
        break;
    case 7:
        *(int32_t *)(o + off_mode) = wide;
        o[0] = (uint8_t)(base - 0x20);
        break;
    default:
        break;                       /* above 7 writes nothing */
    }
}

void __cdecl SetFacing14(int32_t facing, const void *src, void *out)
{
    SetFacing(facing, src, out, 0x14, 0x18, 2, 1);
}

void __cdecl SetFacing08(int32_t facing, const void *src, void *out)
{
    SetFacing(facing, src, out, 0x08, 0x0C, 4, 1);
}

int32_t __cdecl IsKind10To17(int32_t kind)
{
    return (kind >= 10 && kind <= 17) ? 1 : 0;
}

int32_t __cdecl IsKind14Or22(int32_t kind)
{
    return (kind == 14 || kind == 22) ? 1 : 0;
}

/* Read out of the index table at 0x0040D820 mapped through the three arms at
 * 0x0040D814, which return 1, 2 and 0. Index 0 is code 7. */
static const uint8_t kCode74Class[64] = {
    1, 2, 0, 1, 2, 1, 2, 1, 2, 0, 0, 0, 1, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 2, 2, 2,
    0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1,
};

int32_t __cdecl ClassifyByCode74(const void *obj)
{
    const uint8_t *sub = *(const uint8_t *const *)((const uint8_t *)obj + 0x74);
    int32_t        code = *(const int16_t *)(sub + 0x4C);   /* movsx: signed */
    uint32_t       i = (uint32_t)(code - 7);

    if (i > 0x3F)
        return 0;
    return kCode74Class[i];
}

/* 0x0045EE20. Accepts 2-6, 11-12, 21-26, 30, 35-42. Biased by 2 over a
 * 41-entry table; outside 2..42 the answer is no. */
int32_t __cdecl KindInSetA(int32_t kind)
{
    static const uint8_t yes[41] = {
        1,1,1,1,1, 0,0,0,0, 1,1, 0,0,0,0,0,0,0,0,
        1,1,1,1,1,1, 0,0,0, 1, 0,0,0,0,
        1,1,1,1,1,1,1,1,
    };
    uint32_t i = (uint32_t)(kind - 2);

    if (i > 0x28)
        return 0;
    return yes[i];
}

/* 0x00433520. Accepts 1, 7-10 and 29. Biased by 1 over a 29-entry table. */
int32_t __cdecl KindInSetB(int32_t kind)
{
    static const uint8_t yes[29] = {
        1, 0,0,0,0,0, 1,1,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,
    };
    uint32_t i = (uint32_t)(kind - 1);

    if (i > 0x1C)
        return 0;
    return yes[i];
}

int32_t __cdecl MaskPixelSolid(uint32_t x, uint32_t y, const void *mask)
{
    const uint8_t *m = (const uint8_t *)mask;
    uint16_t       xw = (uint16_t)x;
    uint16_t       acc = 0;
    const uint8_t *row;

    if (xw > *(const uint16_t *)m)
        return 0;
    if ((uint16_t)y > *(const uint16_t *)(m + 2))
        return 0;

    row = m + *(const uint16_t *)(m + 4 + (uint32_t)(uint16_t)y * 2);

    for (;;) {
        uint8_t skip = *row++;
        uint8_t run;

        acc = (uint16_t)(acc + skip);
        if (acc > xw)
            return 0;               /* x fell inside a transparent run */

        run = *row;
        acc = (uint16_t)(acc + run);
        if (acc >= xw)
            return 1;               /* x fell inside a solid run */

        /* Past the run length byte and the run's own pixels. */
        row += (uint32_t)run + 1;
    }
}

uint32_t __cdecl XorChecksum(const void *record)
{
    const uint32_t *d = (const uint32_t *)record;
    int32_t         n = (int32_t)(d[1] >> 2);
    uint32_t        acc = 0;

    if (n <= 0)
        return 0;
    do {
        acc ^= *d++;
    } while (--n);
    return acc;
}

uint32_t __cdecl ChainField14(const void *p)
{
    const uint8_t *q = *(const uint8_t *const *)((const uint8_t *)p + 4);

    if (!q)
        return 0;
    return *(const uint32_t *)(q + 0x14);
}

void __cdecl ListPushFront(void *node, void **head)
{
    uint8_t *n = (uint8_t *)node;
    void    *old = *head;

    *(void **)(n + 4) = 0;              /* prev */
    *(void **)(n + 8) = old;            /* next */
    if (old)
        *(void **)((uint8_t *)old + 4) = node;
    *head = node;
}

int32_t __cdecl SetFieldInAll(void *record, void *value)
{
    uint8_t *r = (uint8_t *)record;
    int32_t  i = 0;
    uint32_t off = 0;

    if (*(const int32_t *)(r + 4) <= 0)
        return 0;

    do {
        uint8_t *base = *(uint8_t **)(r + 8);   /* re-read, as the original */

        i++;
        *(void **)(base + off + 0x2C) = value;
        off += 0x60;
    } while (i < *(const int32_t *)(r + 4));

    return i;
}

int32_t __cdecl Field51MeetsMin(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    const uint8_t *rec = *(const uint8_t *const *)(b + 0x44);

    if (!rec)
        return 0;
    /* zero-extended byte, signed 16-bit compare */
    if ((int16_t)(uint16_t)b[0x51] < *(const int16_t *)(rec + 6))
        return 0;
    return 1;
}

int32_t __cdecl ObjKind538In10To17(const void *obj)
{
    int32_t v = *(const int32_t *)((const uint8_t *)obj + 0x538);

    return (v >= 10 && v <= 17) ? 1 : 0;
}

int32_t __cdecl FilterMatches(int32_t wantA, int32_t wantB,
                              int32_t haveA, int32_t haveB,
                              int32_t maskA, int32_t maskB)
{
    /* -1 in the first criterion returns immediately -- it does not merely
     * skip that criterion, it skips the second one too. */
    if (wantA == -1)
        return 1;

    if (wantA & (int32_t)0x80000000) {
        if ((wantA & maskA) != wantA)
            return 0;
    } else if (wantA != haveA) {
        return 0;
    }

    if (wantB != 0) {
        if (wantB & (int32_t)0x80000000) {
            if ((wantB & maskB) != wantB)
                return 0;
        } else if (wantB != haveB) {
            return 0;
        }
    }
    return 1;
}

void __cdecl ConsumePendingByte(void *src, void *dst, const void *cfg)
{
    uint8_t       *s = (uint8_t *)src;
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *c = (const uint8_t *)cfg;

    if (!s[0x104])
        return;

    if (c[0x2C] > 0x40)                       /* unsigned */
        *(int32_t *)(d + 0x14) = 3;

    if (*(const uint32_t *)(c + 0x10) == 0)
        *d = s[0x104];                        /* re-read, as the original */

    s[0x104] = 0;
}

static int32_t FacingFromDelta(const void *rec, int32_t delta,
                               uint32_t off_mode, uint32_t off_flag)
{
    const uint8_t *r = (const uint8_t *)rec;

    if (*(const int32_t *)(r + off_mode) == 1)
        return (delta < 0) ? 7 : 2;

    if (*(const int32_t *)(r + off_flag) != 0) {
        if (delta >= 0x10)
            return 5;
        return (delta > -0x10) ? 4 : 3;
    }

    if (delta >= 0x10)
        return 1;
    return (delta > -0x10) ? 0 : 7;
}

int32_t __cdecl FacingFromDelta08(const void *rec, int32_t delta)
{
    return FacingFromDelta(rec, delta, 0x08, 0x0C);
}

int32_t __cdecl FacingFromDelta14(const void *rec, int32_t delta)
{
    return FacingFromDelta(rec, delta, 0x14, 0x18);
}

/* Read out of the index table at 0x00406A94 and the five arms at 0x00406A7C. */
int32_t __cdecl MapCode18To28(int32_t code)
{
    static const uint8_t kMap[17] = {
        8, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 6,
    };
    uint32_t i = (uint32_t)(code - 0x18);

    if (i > 0x10)
        return 0;
    return kMap[i];
}

int32_t __cdecl MeetsAllThree(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;

    if (b[8] & 4)
        return 0;
    if (*(const int32_t *)b != 1)
        return 0;
    return (**(const int32_t *const *)(b + 0x94) == 0x1F) ? 1 : 0;
}

int misc_install(void)
{
    patch_replace(ADDR_FIELD_53C, (const void *)Field53C, "Field53C", 1);
    patch_replace(ADDR_ADD_BYTE_SAT, (const void *)AddByteSat, "AddByteSat", 2);
    patch_replace(ADDR_COMPARE_DWORD, (const void *)CompareDword, "CompareDword", 2);
    patch_replace(ADDR_COPY_BYTE_IF_SET, (const void *)CopyByteIfSet,
                  "CopyByteIfSet", 3);
    patch_replace(ADDR_SCALE_32_BLOCKS, (const void *)ScaleBy32Blocks,
                  "ScaleBy32Blocks", 1);
    patch_replace(ADDR_TITLE_CASE, (const void *)TitleCaseName, "TitleCaseName", 1);
    patch_replace(ADDR_RESET_PAIR_MASK, (const void *)ResetPairMask,
                  "ResetPairMask", 2);
    patch_replace(ADDR_IS_KIND_7, (const void *)IsKind7, "IsKind7", 1);
    patch_replace(ADDR_IS_BLANK, (const void *)IsBlank, "IsBlank", 1);
    patch_replace(ADDR_IS_SCRIPT_DELIM, (const void *)IsScriptDelim,
                  "IsScriptDelim", 1);
    patch_replace(ADDR_SWAP_COLOUR_BYTES, (const void *)SwapColourBytes,
                  "SwapColourBytes", 2);
    patch_replace(ADDR_NULL_STUB_4, (const void *)NullStub4, "NullStub4", 1);
    patch_replace(ADDR_NULL_STUB, (const void *)NullStub, "NullStub", 0);
    patch_replace(ADDR_RETURN_ZERO, (const void *)ReturnZero, "ReturnZero", 0);
    patch_replace(ADDR_RETURN_ONE, (const void *)ReturnOne, "ReturnOne", 0);
    patch_replace(ADDR_REVERSE_BLOCKS, (const void *)ReverseBlocks,
                  "ReverseBlocks", 4);
    patch_replace(ADDR_SCRIPT_COMPARE, (const void *)ScriptCompare,
                  "ScriptCompare", 3);
    patch_replace(ADDR_COMPARE_PAIR, (const void *)ComparePair, "ComparePair", 2);
    patch_replace(ADDR_MAP_CODE, (const void *)MapCode, "MapCode", 1);
    patch_replace(ADDR_COMPARE_TRIPLE, (const void *)CompareTriple,
                  "CompareTriple", 2);
    patch_replace(ADDR_TYPES_COMPATIBLE, (const void *)TypesCompatible,
                  "TypesCompatible", 2);
    patch_replace(ADDR_SET_FACING_14, (const void *)SetFacing14, "SetFacing14", 3);
    patch_replace(ADDR_SET_FACING_08, (const void *)SetFacing08, "SetFacing08", 3);
    patch_replace(ADDR_IS_KIND_10_17, (const void *)IsKind10To17, "IsKind10To17", 1);
    patch_replace(ADDR_IS_KIND_14_22, (const void *)IsKind14Or22, "IsKind14Or22", 1);
    patch_replace(ADDR_CLASSIFY_CODE74, (const void *)ClassifyByCode74,
                  "ClassifyByCode74", 1);
    patch_replace(ADDR_KIND_IN_SET_A, (const void *)KindInSetA, "KindInSetA", 1);
    patch_replace(ADDR_KIND_IN_SET_B, (const void *)KindInSetB, "KindInSetB", 1);
    patch_replace(ADDR_MASK_PIXEL_SOLID, (const void *)MaskPixelSolid,
                  "MaskPixelSolid", 3);
    patch_replace(ADDR_XOR_CHECKSUM, (const void *)XorChecksum, "XorChecksum", 1);
    patch_replace(ADDR_CHAIN_FIELD_14, (const void *)ChainField14,
                  "ChainField14", 1);
    patch_replace(ADDR_LIST_PUSH_FRONT, (const void *)ListPushFront,
                  "ListPushFront", 2);
    patch_replace(ADDR_SET_FIELD_IN_ALL, (const void *)SetFieldInAll,
                  "SetFieldInAll", 2);
    patch_replace(ADDR_FIELD51_MEETS_MIN, (const void *)Field51MeetsMin,
                  "Field51MeetsMin", 1);
    patch_replace(ADDR_OBJ_KIND538_10_17, (const void *)ObjKind538In10To17,
                  "ObjKind538In10To17", 1);
    patch_replace(ADDR_FILTER_MATCHES, (const void *)FilterMatches,
                  "FilterMatches", 6);
    patch_replace(ADDR_CONSUME_PENDING, (const void *)ConsumePendingByte,
                  "ConsumePendingByte", 3);
    patch_replace(ADDR_FACING_DELTA_08, (const void *)FacingFromDelta08,
                  "FacingFromDelta08", 2);
    patch_replace(ADDR_FACING_DELTA_14, (const void *)FacingFromDelta14,
                  "FacingFromDelta14", 2);
    patch_replace(ADDR_MAP_CODE_18_28, (const void *)MapCode18To28,
                  "MapCode18To28", 1);
    patch_replace(ADDR_MEETS_ALL_THREE, (const void *)MeetsAllThree,
                  "MeetsAllThree", 1);
    return 0;
}
