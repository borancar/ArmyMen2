/* misc.cpp -- see misc.h. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "commmsg.h" /* SendChatMsg -- reconstructed */
#include "misc.h"
#include "crt.h"
#include "image.h"
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
    /* 1 is NOT grouped with 3, and it was. The jump table sends facing 1 to an
     * arm that loads `out` and jumps straight into the common tail, while
     * facing 3 writes off_flag first and then falls into the same tail -- so 1
     * mirrors 7 and 3 mirrors 5, and folding 1 in with 3 broke that symmetry.
     * The defect was invisible for as long as the vectors only checked that
     * the expected writes happened; it writes one dword the original never
     * writes, and every value it was expected to produce it still produced. */
    case 1:
        *(int32_t *)(o + off_mode) = wide;
        o[0] = (uint8_t)(base + 0x20);
        break;
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

/* Shared body. `wide_rows` selects the row table's entry width, which is the
 * only thing that differs between 0x0041CF20 and 0x0041CEC0 -- and it is a
 * genuine difference, not a tidy-up: one reads a word at m+4+y*2 and the other
 * a dword at m+4+y*4. Everything below that line is the same in both. */
static int32_t MaskSolid(uint32_t x, uint32_t y, const void *mask,
                         int32_t wide_rows)
{
    const uint8_t *m = (const uint8_t *)mask;
    uint16_t       xw = (uint16_t)x;
    uint16_t       acc = 0;
    const uint8_t *row;

    if (xw > *(const uint16_t *)m)
        return 0;
    if ((uint16_t)y > *(const uint16_t *)(m + 2))
        return 0;

    row = m + (wide_rows
               ? *(const uint32_t *)(m + 4 + (uint32_t)(uint16_t)y * 4)
               : *(const uint16_t *)(m + 4 + (uint32_t)(uint16_t)y * 2));

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

int32_t __cdecl MaskPixelSolid(uint32_t x, uint32_t y, const void *mask)
{
    return MaskSolid(x, y, mask, 0);
}

int32_t __cdecl MaskPixelSolid32(uint32_t x, uint32_t y, const void *mask)
{
    return MaskSolid(x, y, mask, 1);
}

/* The other end of the same format: the encoder at 0x00423300 reads its source
 * through this and MaskPixelSolid reads the result. Rows run bottom-up, so the
 * height is needed to find row y at all.
 *
 * `x % 8` is C's signed remainder, which is what the original's
 * `and 0x80000007` / `dec` / `or 0xFFFFFFF8` / `inc` computes; for a negative
 * x that makes the shift count exceed 7. Nothing shows a negative x ever
 * arrives -- this reproduces the arithmetic rather than arguing for it. */
int32_t __cdecl BitmapBitSet(const void *base, int32_t x, int32_t y,
                             int32_t height, int32_t stride)
{
    const uint8_t *bytes = (const uint8_t *)base;
    int32_t        bit   = 1 << (7 - (x % 8));
    const uint8_t *row   = bytes + (height - y - 1) * stride;

    return bit & row[x >> 3];
}

/* Mask record: origin at +0 and +2, extent at +4 and +6, pixels at +0xC, all
 * reached by offset because the record ends in a pointer. See misc.h. */
#define AM2_OBJMASK_ORIGIN_X  0x00
#define AM2_OBJMASK_ORIGIN_Y  0x02
#define AM2_OBJMASK_WIDTH     0x04
#define AM2_OBJMASK_HEIGHT    0x06
#define AM2_OBJMASK_BITS      0x0C
#define AM2_OBJ_MASK          0x78
#define AM2_OBJ_POS_X         0x30
#define AM2_OBJ_POS_Y         0x34

int32_t __cdecl ObjMaskBitAt(const void *obj, const AM2_Point *at)
{
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *m;
    const uint8_t *bits;
    int32_t        x, y, w, h, stride;

    if (!obj)
        return 0;
    m = *(const uint8_t *const *)(o + AM2_OBJ_MASK);
    if (!m)
        return 0;

    x = *(const int16_t *)(m + AM2_OBJMASK_ORIGIN_X)
        - *(const int32_t *)(o + AM2_OBJ_POS_X) + at->x;
    y = *(const int16_t *)(m + AM2_OBJMASK_ORIGIN_Y)
        - *(const int32_t *)(o + AM2_OBJ_POS_Y) + at->y;

    /* Tested in this order, and each bound is loaded only once its coordinate
     * has passed the zero test -- reproduced as written. */
    if (x < 0)
        return 0;
    w = *(const int16_t *)(m + AM2_OBJMASK_WIDTH);
    if (x >= w)
        return 0;
    if (y < 0)
        return 0;
    h = *(const int16_t *)(m + AM2_OBJMASK_HEIGHT);
    if (y >= h)
        return 0;

    stride = ((w + 31) >> 3) & ~3;
    bits   = *(const uint8_t *const *)(m + AM2_OBJMASK_BITS);

    return bits[stride * y + (x >> 3)] & (0x80 >> (x & 7));
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

/* The inverse. Both arms end by clearing the node's own links, and only the
 * head arm writes through `head`. */
void __cdecl ListUnlink(void *node, void **head)
{
    uint8_t *n    = (uint8_t *)node;
    void    *prev = *(void **)(n + 4);
    void    *next = *(void **)(n + 8);

    if (!prev)
        *head = next;
    else
        *(void **)((uint8_t *)prev + 8) = next;

    if (next)
        *(void **)((uint8_t *)next + 4) = prev;

    *(void **)(n + 8) = 0;
    *(void **)(n + 4) = 0;
}

/* The remap loop walks dst with the register it was handed, so at `ret` eax is
 * dst + count on the table path and plain dst on the other two. Both callers
 * discard it and recompute `dst += count` themselves, so the prototype is
 * void. */
void __cdecl RemapBytes(void *dst, const void *src, const void *table,
                        int32_t count)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    const uint8_t *m = (const uint8_t *)table;

    if (m) {
        if (count <= 0)
            return;
        do {
            *d++ = m[*s++];
        } while (--count);
        return;
    }

    if (d == s)
        return;

    {
        uint32_t words = (uint32_t)count >> 2;
        uint32_t bytes = (uint32_t)count & 3;

        while (words--) {
            *(uint32_t *)d = *(const uint32_t *)s;
            d += 4;
            s += 4;
        }
        while (bytes--)
            *d++ = *s++;
    }
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

/* The three arms, read out of the byte table at 0x0040D8FC. Anything above
 * 0x24 never reaches the table and behaves as ARM_NEITHER. */
#define AM2_KIND538_ARM_SKIP      0   /* 0x00, 0x20..0x24 */
#define AM2_KIND538_ARM_OVERRIDE  1   /* 0x05, 0x08..0x0F */
#define AM2_KIND538_ARM_NEITHER   2

#define AM2_OBJ_KIND538   0x538
#define AM2_OBJ_KIND53C   0x53C
#define AM2_OBJ_SEQ       0x74        /* sub-record the readiness test reads */
#define AM2_SEQ_FRAMES    0x44        /* pointer; its first int16 is a count */
#define AM2_SEQ_POS       0x51        /* unsigned byte, the position */

int32_t __cdecl ObjNextKind538(const void *obj, int32_t want)
{
    static const uint8_t kArm[0x25] = {
        0, 2, 2, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        0, 0, 0, 0, 0,
    };
    const uint8_t *o    = (const uint8_t *)obj;
    int32_t        cur  = *(const int32_t *)(o + AM2_OBJ_KIND538);
    int32_t        over = *(const int32_t *)(o + AM2_OBJ_KIND53C);
    int32_t        alt  = over;
    int32_t        skip;
    const uint8_t *seq;

    if (cur == want)
        return cur;

    skip = (cur == 1) ? 1 : 0;
    if ((uint32_t)want <= 0x24) {
        if (kArm[want] == AM2_KIND538_ARM_SKIP)
            skip = 1;
        else if (kArm[want] == AM2_KIND538_ARM_OVERRIDE)
            alt = want;
    }

    seq = *(const uint8_t *const *)(o + AM2_OBJ_SEQ);
    if (!skip) {
        const uint8_t *frames = *(const uint8_t *const *)(seq + AM2_SEQ_FRAMES);

        if (frames) {
            int32_t last = *(const int16_t *)frames - 1;
            int32_t pos  = seq[AM2_SEQ_POS];

            if (pos < last)
                return cur;         /* the sequence has not finished */
        }
    }

    if (over)
        want = alt;
    return want;
}

#define AM2_RGB332_ENTRIES  256
#define AM2_RGB332_PACKED   0x400

void __cdecl BuildRgb332Palette(void *out)
{
    uint8_t  *quad   = (uint8_t *)out;
    uint32_t *packed = (uint32_t *)((uint8_t *)out + AM2_RGB332_PACKED);
    int32_t   i;

    for (i = 0; i < AM2_RGB332_ENTRIES; i++) {
        int32_t c0 = ((i >> 5) & 7) * 255 / 7;
        int32_t c1 = ((i >> 2) & 7) * 255 / 7;
        int32_t c2 = (i & 3) * 255 / 3;

        quad[i * 4 + 0] = (uint8_t)c0;
        quad[i * 4 + 1] = (uint8_t)c1;
        quad[i * 4 + 2] = (uint8_t)c2;
        quad[i * 4 + 3] = 0;

        /* Reassembled from the bytes just stored, not from c0..c2 -- the
         * original reads them back out of the buffer. Same values, and worth
         * saying because it means the two tables cannot disagree. */
        packed[i] = ((uint32_t)quad[i * 4 + 2] << 16)
                  | ((uint32_t)quad[i * 4 + 1] << 8)
                  |  (uint32_t)quad[i * 4 + 0];
    }
}

void __cdecl CollapseEqualDeltas(uint16_t *values, int32_t *count)
{
    int32_t   n    = *count;
    int32_t   kept = 0;
    uint16_t *wr   = values;
    int32_t   i    = 1;

    if (n <= 1) {
        *count = 1;
        return;
    }

    do {
        int32_t delta, j;

        /* Advanced before use, so the first pass writes values[1] and reads
         * values[0] behind it. */
        wr++;
        delta = (int32_t)values[i] - (int32_t)wr[-1];
        kept++;

        for (j = i + 1; j < n; j++) {
            if ((int32_t)values[i + 1] - (int32_t)values[i] != delta)
                break;
            i++;
        }

        i++;
        *wr = values[i - 1];

        /* Re-read, as the original does. Nothing in the loop writes it, so it
         * cannot change -- unless the caller passed a count that lives inside
         * the array, and reproducing the read costs nothing. */
        n = *count;
    } while (i < n);

    *count = kept + 1;
}

/* Shared body; `wide` is the only difference between the two arms, exactly as
 * it is between MaskPixelSolid and MaskPixelSolid32. */
static void RemapRuns(uint8_t *rle, int32_t wide, const uint8_t *table)
{
    uint16_t       width  = *(const uint16_t *)rle;
    uint16_t       height = *(const uint16_t *)(rle + 2);
    uint32_t       step   = wide ? 4u : 2u;
    const uint8_t *row    = rle + 4;
    const uint8_t *end    = rle + 4 + (uint32_t)height * step;

    while (row < end) {
        uint8_t *p   = rle + (wide ? *(const uint32_t *)row
                                   : *(const uint16_t *)row);
        uint16_t acc = 0;

        /* A width of zero skips the row entirely rather than walking it once:
         * the original tests the width before the loop, not after. */
        if (width != 0) {
            do {
                uint32_t skip = *p++;
                uint32_t run;

                acc = (uint16_t)(acc + skip);
                run = *p++;
                acc = (uint16_t)(acc + run);

                /* The original's test is signed, on a value it has just masked
                 * to a byte, so it is `run != 0`. */
                if (run) {
                    uint32_t n = run;

                    do {
                        *p = table[*p];
                        p++;
                    } while (--n);
                }
            } while (acc < width);
        }
        row += step;
    }
}

void __cdecl RemapRleRuns(void *rle, void *unused, int32_t wide,
                          const void *table)
{
    (void)unused;
    RemapRuns((uint8_t *)rle, wide == 1, (const uint8_t *)table);
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

/* The index table at 0x00449F24 selects between two arms, and its entries are
 * 0 and 1 -- the same values the arms return -- so the table IS the answer.
 * Read out of the image along with the two-entry jump table at 0x00449F1C. */
int32_t __cdecl ObjCodeUnmapped(const void *obj)
{
    static const uint8_t kAnswer[17] = {
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    };
    const uint8_t  *o    = (const uint8_t *)obj;
    const uint32_t *link = *(const uint32_t *const *)(o + 0xC0);
    uint32_t        i    = (uint32_t)(*link - 0x18);

    if (i > 0x10)
        return 1;
    return kAnswer[i];
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

int32_t __attribute__((thiscall)) CommSlotForArmy(void *comm, int32_t army)
{
    if (army == 4)
        return 4;

    const uint8_t *p = (const uint8_t *)comm + AM2_PLAYER_ARMY;

    for (int32_t i = 0; i < AM2_PLAYERS_MAX; i++) {
        if (*(const int32_t *)p == army)
            return i;
        p += AM2_PLAYER_STRIDE;
    }
    /* No match and slot 0 share an answer. */
    return 0;
}

int32_t __attribute__((thiscall)) CommSlotHasPlayer(void *comm, int32_t slot)
{
    int32_t id = *(const int32_t *)((const uint8_t *)comm +
                                    (size_t)slot * AM2_PLAYER_STRIDE +
                                    AM2_PLAYER_ID);

    return id != 0 && id != -1;
}

/* 0x00422FF0, still original: reads one DIB chunk from an open stream into the
 * 0x428-byte header and answers the pixels in a buffer the caller frees. */
typedef void *(__cdecl *AM2_ReadDibChunkFn)(void *fp, void *hdr);
#define orig_read_dib_chunk \
    (*(AM2_ReadDibChunkFn)AM2_IMAGE(ADDR_READ_DIB_CHUNK))

void *__cdecl LoadDibFlipped(const char *path, void *hdr, uint16_t *size)
{
    am2_FILE *fp;
    void     *src;
    void     *dst;
    int32_t   total;

    if (!path || !path[0])
        return (void *)0;

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp)
        return (void *)0;

    src = orig_read_dib_chunk(fp, hdr);
    orig_fclose(fp);
    if (!src)
        return (void *)0;

    /* Cleared before anything below can fail, so a caller that ignores the
     * return value still sees zero. */
    *size = 0;

    total = *(const int32_t *)((const uint8_t *)hdr + DIB_OFF_SIZE);
    if (!total) {
        orig_log("ERROR: %s has listed size of 0 (try resaving)\n", path);
        return (void *)0;
    }

    dst = orig_malloc((size_t)total);
    if (!dst)
        return (void *)0;   /* leaks `src` -- the original's only such exit */

    ReverseBlocks(dst, src, total,
                  *(const int32_t *)((const uint8_t *)hdr + DIB_OFF_BLOCKS));
    *size = (uint16_t)total;
    orig_free(src);
    return dst;
}


#define g_menuRow (*(const int32_t *)(uintptr_t)ADDR_MENU_ROW)


int32_t __cdecl GetMenuRow(void)
{
    return g_menuRow;
}

void *__attribute__((thiscall)) InitPtrList(void *rec)
{
    int32_t *r = (int32_t *)rec;

    r[0] = 0;
    r[1] = 0;
    r[2] = 0;
    return rec;
}

void __attribute__((thiscall)) ClearPtrListAlias(void *rec)
{
    ClearPtrList(rec);
}

/* The same spelling device.cpp uses -- PollKeyboard owns these and swaps them
 * each poll, and two names on one address is what checkglobals exists to
 * stop. */
#define g_curKeys  (*(uint8_t **)(uintptr_t)ADDR_KEYS_NOW_PTR)
#define g_prevKeys (*(uint8_t **)(uintptr_t)ADDR_KEYS_PREV_PTR)

int32_t __cdecl IsKeyDown(int32_t dik)
{
    /* 0x80, not 1: the original masks and returns, and never normalises. */
    return g_curKeys[(uint32_t)dik & 0xFFu] & 0x80;
}

int32_t __cdecl KeyChanged(int32_t dik)
{
    uint32_t k = (uint32_t)dik & 0xFFu;

    return (int32_t)((uint32_t)(g_prevKeys[k] ^ g_curKeys[k]) >> 7);
}

void __cdecl FreeIfNotNull(void *p)
{
    if (p)
        am2_free(p);
}

typedef void (__cdecl *AM2_MenuMessageFn)(const char *s, int32_t a, int32_t b);
#define orig_menu_message (*(AM2_MenuMessageFn)AM2_IMAGE(ADDR_MENU_MESSAGE))

/* The second call is a BROADCAST, not a second local append -- see
 * SendChatMsg. The cast is the honest one: that function truncates a text
 * longer than 255 bytes in the buffer it is given, and the original hands it
 * this argument unchanged, so Announce's own `const` is a promise the callee
 * does not keep. Nothing in the image announces anything near that long. */
void __cdecl Announce(const char *text)
{
    orig_menu_message(text, 4, 0);
    SendChatMsg((char *)text, 1);
}

#define g_keyPressed ((int32_t *)(uintptr_t)ADDR_KEY_PRESSED)

int32_t __cdecl KeyPressed(int32_t dik)
{
    return g_keyPressed[(uint32_t)dik & 0xFFu];
}

void __cdecl LatchKeyState(void)
{
    /* 0x40 dwords in the original, which is the whole 256-byte buffer. */
    memcpy(g_prevKeys, g_curKeys, 256);
}

uint32_t __cdecl UnitTypeCost(int32_t type)
{
    return *(const uint32_t *)((uintptr_t)ADDR_UNIT_TYPES
                               + (uint32_t)type * AM2_UNIT_TYPE_STRIDE);
}

#define g_keyBindings ((const uint8_t *)(uintptr_t)ADDR_KEY_BINDINGS)

void __cdecl ConsumeKey(int32_t dik)
{
    uint32_t k = (uint32_t)dik & 0xFFu;

    g_prevKeys[k] = g_curKeys[k];
    g_keyPressed[k] = 0;
}

int32_t __cdecl ActionKeyDown(int32_t action)
{
    /* No mask on `action` and no bound: the table is indexed raw. */
    if (g_curKeys[g_keyBindings[action * 2]] & 0x80)
        return 1;
    if (g_curKeys[g_keyBindings[action * 2 + 1]] & 0x80)
        return 1;
    return 0;
}

int32_t __attribute__((thiscall)) CommArmyOfSlot(void *comm, int32_t slot)
{
    if (slot == 4)
        return 4;
    return *(const int32_t *)((const uint8_t *)comm
                              + (uint32_t)slot * AM2_PLAYER_STRIDE
                              + AM2_PLAYER_ARMY);
}

/* 0x0042A6B0 and 0x0042A710, thiscall. The {capacity, count, items} record's
 * two capacity moves, and the pair are not symmetric.
 *
 * The grow adds twenty and reallocs. The shrink takes twenty off and, if that
 * leaves nothing, FREES the array and nulls the pointer rather than reallocing
 * to zero -- so an emptied list gives its storage back entirely and the next
 * push starts from a null pointer, which realloc treats as a fresh malloc.
 *
 * Neither touches the count. PtrListPush and ListRemoveAt own that, which is
 * why the grow can be called before the item is stored and the shrink after
 * the count has already come down. */
#define AM2_PTR_LIST_SLACK 20

/* 0x0041B760. Two colours' distance: the sum of the squares of the three
 * per-channel differences, each taken as an absolute value with the `cdq`
 * / `xor` / `sub` idiom rather than a branch.
 *
 * It reads THREE BYTES from each argument, so the fourth byte of a palette
 * entry -- whatever it holds -- takes no part. That is why the caller can
 * hand it the address of a plain `uint32_t` colour and a palette slot and
 * have both mean the same thing. */
int32_t __cdecl ColourDistance(const void *a, const void *b)
{
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    int32_t        d0 = (int32_t)p[0] - (int32_t)q[0];
    int32_t        d1 = (int32_t)p[1] - (int32_t)q[1];
    int32_t        d2 = (int32_t)p[2] - (int32_t)q[2];

    if (d0 < 0) d0 = -d0;
    if (d1 < 0) d1 = -d1;
    if (d2 < 0) d2 = -d2;
    return d0 * d0 + d1 * d1 + d2 * d2;
}

void __attribute__((thiscall)) PtrListGrow(void *rec)
{
    int32_t *r = (int32_t *)rec;

    r[0] = r[0] + AM2_PTR_LIST_SLACK;
    r[2] = (int32_t)(uintptr_t)am2_realloc((void *)(uintptr_t)r[2],
                                           (size_t)r[0] * 4);
}

void __attribute__((thiscall)) PtrListShrink(void *rec)
{
    int32_t *r = (int32_t *)rec;

    r[0] = r[0] - AM2_PTR_LIST_SLACK;
    if (r[0] <= 0) {
        am2_free((void *)(uintptr_t)r[2]);
        r[2] = 0;
        return;
    }
    r[2] = (int32_t)(uintptr_t)am2_realloc((void *)(uintptr_t)r[2],
                                           (size_t)r[0] * 4);
}

void __attribute__((thiscall)) PtrListPush(void *rec, void *item)
{
    int32_t *r = (int32_t *)rec;

    if (r[1] >= r[0])
        PtrListGrow(rec);
    /* Re-read the count after the grow, as the original does. */
    ((void **)r[2])[r[1]] = item;
    r[1] = r[1] + 1;
}

#define AM2_PTR_LIST_SLACK 20
void __attribute__((thiscall)) ListRemoveAt(void *rec, int32_t index)
{
    int32_t *r = (int32_t *)rec;
    int32_t  count;

    if (index >= r[1])
        return;
    count = r[1] - 1;
    r[1] = count;
    if (index < count)
        memmove((void **)r[2] + index, (void **)r[2] + index + 1,
                (size_t)(count - index) * 4);
    if (count + AM2_PTR_LIST_SLACK < r[0])
        PtrListShrink(rec);
}

void __attribute__((thiscall)) ClearPtrList(void *rec)
{
    int32_t *r     = (int32_t *)rec;
    void    *items = (void *)(uintptr_t)r[2];

    r[0] = 0;
    r[1] = 0;
    if (items)
        am2_free(items);
    r[2] = 0;
}

int32_t __cdecl ActionKeyPressed(int32_t action)
{
    int32_t i;

    for (i = 0; i < 2; i++) {
        uint32_t k = g_keyBindings[action * 2 + i];

        if (!(g_curKeys[k] & 0x80))
            continue;
        if ((g_prevKeys[k] ^ g_curKeys[k]) & 0x80)
            return 1;
    }
    return 0;
}

int misc_install(void)
{
    patch_replace(ADDR_COLOUR_DISTANCE, (const void *)ColourDistance,
                  "ColourDistance", 2);
    patch_replace(ADDR_PTR_LIST_GROW, (const void *)PtrListGrow,
                  "PtrListGrow", 1);
    patch_replace(ADDR_PTR_LIST_SHRINK, (const void *)PtrListShrink,
                  "PtrListShrink", 1);
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
    patch_replace(ADDR_SWAP_COLOUR_BYTES, (const void *)SwapColourBytes,
                  "SwapColourBytes", 2);
    patch_replace(ADDR_NULL_STUB_4, (const void *)NullStub4, "NullStub4", 1);
    patch_replace(ADDR_NULL_STUB, (const void *)NullStub, "NullStub", 0);
    patch_replace(ADDR_RETURN_ZERO, (const void *)ReturnZero, "ReturnZero", 0);
    patch_replace(ADDR_RETURN_ONE, (const void *)ReturnOne, "ReturnOne", 0);
    patch_replace(ADDR_LOAD_DIB_FLIPPED, (const void *)LoadDibFlipped,
                  "LoadDibFlipped", 1);
    patch_replace(ADDR_REVERSE_BLOCKS, (const void *)ReverseBlocks,
                  "ReverseBlocks", 4);
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
    patch_replace(ADDR_COMM_SLOT_FOR_ARMY, (const void *)CommSlotForArmy,
                  "CommSlotForArmy", 20);
    patch_replace(ADDR_COMM_SLOT_HAS_PLAYER, (const void *)CommSlotHasPlayer,
                  "CommSlotHasPlayer", 5);
    patch_replace(ADDR_BITMAP_BIT_SET, (const void *)BitmapBitSet,
                  "BitmapBitSet", 5);
    patch_replace(ADDR_LIST_UNLINK, (const void *)ListUnlink, "ListUnlink", 6);
    patch_replace(ADDR_REMAP_BYTES, (const void *)RemapBytes, "RemapBytes", 2);
    patch_replace(ADDR_MASK_PIXEL_SOLID32, (const void *)MaskPixelSolid32,
                  "MaskPixelSolid32", 2);
    patch_replace(ADDR_OBJ_MASK_BIT_AT, (const void *)ObjMaskBitAt,
                  "ObjMaskBitAt", 3);
    patch_replace(ADDR_OBJ_NEXT_KIND538, (const void *)ObjNextKind538,
                  "ObjNextKind538", 1);
    patch_replace(ADDR_BUILD_RGB332, (const void *)BuildRgb332Palette,
                  "BuildRgb332Palette", 1);
    patch_replace(ADDR_COLLAPSE_DELTAS, (const void *)CollapseEqualDeltas,
                  "CollapseEqualDeltas", 1);
    patch_replace(ADDR_REMAP_RLE_RUNS, (const void *)RemapRleRuns,
                  "RemapRleRuns", 1);
    patch_replace(ADDR_OBJ_CODE_UNMAPPED, (const void *)ObjCodeUnmapped,
                  "ObjCodeUnmapped", 1);
    patch_replace(ADDR_GET_MENU_ROW, (const void *)GetMenuRow, "GetMenuRow", 0);
    patch_replace(ADDR_ANNOUNCE, (const void *)Announce, "Announce", 1);
    patch_replace(ADDR_UNIT_TYPE_COST, (const void *)UnitTypeCost,
                  "UnitTypeCost", 1);
    patch_replace(ADDR_FREE_IF_NOT_NULL, (const void *)FreeIfNotNull,
                  "FreeIfNotNull", 1);
    patch_replace(ADDR_IS_KEY_DOWN, (const void *)IsKeyDown, "IsKeyDown", 1);
    patch_replace(ADDR_KEY_PRESSED_FN, (const void *)KeyPressed, "KeyPressed", 1);
    patch_replace(ADDR_LATCH_KEY_STATE, (const void *)LatchKeyState,
                  "LatchKeyState", 0);
    patch_replace(ADDR_CONSUME_KEY, (const void *)ConsumeKey, "ConsumeKey", 1);
    patch_replace(ADDR_ACTION_KEY_DOWN, (const void *)ActionKeyDown,
                  "ActionKeyDown", 1);
    patch_replace(ADDR_ACTION_KEY_PRESSED, (const void *)ActionKeyPressed,
                  "ActionKeyPressed", 1);
    patch_replace(ADDR_COMM_ARMY_OF_SLOT, (const void *)CommArmyOfSlot,
                  "CommArmyOfSlot", 20);
    patch_replace(ADDR_PTR_LIST_PUSH, (const void *)PtrListPush,
                  "PtrListPush", 2);
    patch_replace(ADDR_LIST_REMOVE_AT, (const void *)ListRemoveAt,
                  "ListRemoveAt", 2);
    patch_replace(ADDR_CLEAR_PTR_LIST, (const void *)ClearPtrList,
                  "ClearPtrList", 1);
    patch_replace(ADDR_KEY_CHANGED, (const void *)KeyChanged, "KeyChanged", 1);
    patch_replace(ADDR_INIT_PTR_LIST, (const void *)InitPtrList,
                  "InitPtrList", 1);
    patch_replace(ADDR_CLEAR_PTR_LIST_ALIAS, (const void *)ClearPtrListAlias,
                  "ClearPtrListAlias", 1);
    return 0;
}
