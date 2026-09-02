/* misc.cpp -- see misc.h. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "misc.h"
#include "rect.h"   /* PointInRect -- reconstructed */
#include "map.h"    /* TileOfPoint -- reconstructed */
#include "item.h"   /* TileAttrAt -- reconstructed */
#include "maprow.h" /* RowUpdate -- reconstructed */
#include "msgslot.h" /* CommMustBroadcast -- reconstructed */
#include "objtype.h" /* ObjIsTypeIn238 -- reconstructed */
#include "place.h"   /* LoadArmyPlacement */
#include "gamedir.h" /* SetGameDir -- the chdir the mask glob needs */
#include "script.h"  /* GetVarValue */
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

/* The two the loose arm needs that are still original: the packed half and
 * MSVC's
 * _findfirst. */
typedef void (__cdecl *AM2_LoadMaskPackedFn)(void *out, int32_t set,
                                             int32_t index, int32_t frame);
#define orig_load_mask_packed \
    ((AM2_LoadMaskPackedFn)AM2_IMAGE(ADDR_LOAD_MASK_PACKED))
typedef int32_t (__cdecl *AM2_FindFirstFn)(const char *pattern, void *data);
#define orig_findfirst  ((AM2_FindFirstFn)AM2_IMAGE(ADDR_CRT_FINDFIRST))
/* LoadDibFlipped is reconstructed, in win32/surface.cpp. Declared here rather
 * than by including that header, for the reason script.cpp declares
 * PreloadSprite: misc.cpp is on the FLAT side of the split and must name no
 * Win32 or COM type, and surface.h reaches LPDIRECTDRAWSURFACE. Its own
 * header is `extern "C"`, so this has to be too or the two mangle
 * differently and only the linker notices. */
extern "C" void *__cdecl LoadDibFlipped(const char *path, void *hdr,
                                        uint16_t *size);

/* LoadMask -- original 0x00435280, two callers. Fill a mask record from
 * {set, index, frame}, and it is SpriteLoadTriple's twin one file over: the
 * same ADDR_OPT_DF fork, the same `%02d_%03d_%02d_*` glob convention, the
 * same set directory table. Sprites glob `.bmp` and `.sha`; this globs
 * `.msk`.
 *
 * THE TWO HALVES ARE NOT SYMMETRICAL AND THAT IS THE THING TO KNOW. Without
 * `-df` it is a plain forward to the packed loader with the arguments
 * unchanged. With it, the packed loader is never reached AND a set below
 * AM2_SPRITE_SET_MAP_FIRST returns having done nothing at all -- so under
 * `-df` the fixed sets have no masks, where under the pack they do. That is
 * the original's, not a transcription: the `< 0x14` test guards only the
 * loose arm, because only the map's own sets have a directory to glob in.
 *
 * IT GLOBS AND NEVER CLOSES THE HANDLE. _findfirst's result is compared
 * against -1 and then discarded; there is no _findclose, unlike
 * SpriteLoadTriple, which closes both of its. Reproduced -- it is a leak per
 * mask loaded under a switch nothing here ships with, and tidying it would be
 * inventing behaviour.
 *
 * THE FOUND NAME IS USED WITHOUT THE DIRECTORY, which works only because
 * SetGameDir has already chdir'd into it. Same shape as the sprite loader.
 *
 * The record's first two words are zeroed and two more are copied out of the
 * DIB descriptor the reader filled; the pixels land at MASKREC_OFF_BITS and a
 * null there is the failure, checked before the two words are written. So a
 * failed load leaves the record's words untouched rather than zeroed -- only
 * the pointer says whether it worked.
 */
void __cdecl LoadMask(void *out, int32_t set, int32_t index, int32_t frame)
{
    uint8_t *rec = (uint8_t *)out;
    char     dir[AM2_MASK_PATH_MAX];
    char     pattern[AM2_MASK_PATH_MAX];
    uint8_t  find[AM2_FINDDATA_BYTES];
    uint8_t  desc[AM2_DIB_DESC_BYTES];

    if (!*(const int32_t *)AM2_IMAGE(ADDR_OPT_DF)) {
        orig_load_mask_packed(out, set, index, frame);
        return;
    }

    if (set < AM2_SPRITE_SET_MAP_FIRST)
        return;

    am2_sprintf(dir, (const char *)AM2_IMAGE(ADDR_STR_FMT_MASK_DIR),
                (const char *)AM2_IMAGE(ADDR_MAP_BLOCK),
                ((const char *const *)AM2_IMAGE(ADDR_SPRITE_SET_DIRS))[set]);
    SetGameDir(dir);

    am2_sprintf(pattern, (const char *)AM2_IMAGE(ADDR_STR_GLOB_MSK),
                set, index, frame);

    if (orig_findfirst(pattern, find) == -1)
        return;

    *(void **)(rec + MASKREC_OFF_BITS) =
        LoadDibFlipped((const char *)(find + AM2_FIND_OFF_NAME), desc,
                       (uint16_t *)(rec + MASKREC_OFF_LOADER_OUT));
    if (!*(void *const *)(rec + MASKREC_OFF_BITS))
        return;

    *(uint16_t *)(rec + MASKREC_OFF_ZERO_A) = 0;
    *(uint16_t *)(rec + MASKREC_OFF_ZERO_B) = 0;
    *(uint16_t *)(rec + MASKREC_OFF_DESC_BLOCKS) =
        *(const uint16_t *)(desc + DIB_OFF_BLOCKS);
    *(uint16_t *)(rec + MASKREC_OFF_DESC4) = *(const uint16_t *)(desc + 4);
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

/* The mask record's offsets were private to this file until ObjHitMaskAction
 * needed the same five; they are OBJMASK_OFF_* in orig.h now, beside the note
 * about the two readers disagreeing over the origin's sign. The three object
 * offsets that were here were a second spelling of OBJ_OFF_HIT_MASK and of
 * OBJ_OFF_HIT_RECT's first two edges. */

int32_t __cdecl ObjMaskBitAt(const void *obj, const AM2_Point *at)
{
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *m;
    const uint8_t *bits;
    int32_t        x, y, w, h, stride;

    if (!obj)
        return 0;
    m = *(const uint8_t *const *)(o + OBJ_OFF_HIT_MASK);
    if (!m)
        return 0;

    x = *(const int16_t *)(m + OBJMASK_OFF_ORIGIN_X)
        - ((const AM2_Rect *)(o + OBJ_OFF_HIT_RECT))->left + at->x;
    y = *(const int16_t *)(m + OBJMASK_OFF_ORIGIN_Y)
        - ((const AM2_Rect *)(o + OBJ_OFF_HIT_RECT))->top + at->y;

    /* Tested in this order, and each bound is loaded only once its coordinate
     * has passed the zero test -- reproduced as written. */
    if (x < 0)
        return 0;
    w = *(const int16_t *)(m + OBJMASK_OFF_WIDTH);
    if (x >= w)
        return 0;
    if (y < 0)
        return 0;
    h = *(const int16_t *)(m + OBJMASK_OFF_HEIGHT);
    if (y >= h)
        return 0;

    stride = ((w + 31) >> 3) & ~3;
    bits   = *(const uint8_t *const *)(m + OBJMASK_OFF_BITS);

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

/* Read out of the index table at 0x00406A94 and the five arms at 0x00406A7C.
 *
 * ONLY FIVE OF THE SEVENTEEN ENTRIES ANSWER ANYTHING -- 0x18, 0x19, 0x1A,
 * 0x27 and 0x28 -- and the twelve between them share the default arm with
 * everything outside the range, which is why the table can be the answer
 * rather than an index into one. The values are not monotonic in the code, so
 * it is a ranking and not a formula.
 *
 * Its two callers are SelectBestWeapon's twin SelectRankedWeapon and nothing
 * else. It was very nearly reconstructed a SECOND time under the name
 * WeaponRank while that twin was being written; checkpatches refused the
 * build on both counts, the duplicate patch and the second ADDR_ name. Fourth
 * near-miss of this kind in the project, and the first where the tool caught
 * both halves at once. */
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

/* 0x0040F960, thiscall, three callers. One army's score.
 *
 * The slot's army colour picks one of the four ADDR_SVAR_*SCORE name-table
 * indices and GetVarValue resolves it -- so a script's `greenscore` variable
 * and this accessor are the same storage, which is what makes the score
 * scriptable at all.
 *
 * THE LOCAL IS ZEROED BEFORE THE CALL AND THAT IS LOAD-BEARING. GetVarValue
 * writes through the pointer for a non-positive index and for a variable, and
 * NOT for a name that is not one: that path complains and leaves the caller's
 * memory alone. Without the zero this would return stack.
 *
 * The original reuses its own incoming argument slot as that local, which is
 * the same MSVC idiom ParsePlaceLine turned out to use. Nothing observes the
 * difference; written as an ordinary local here. */
int32_t __attribute__((thiscall)) GetArmyScore(void *comm, int32_t slot)
{
    const int32_t *vars = (const int32_t *)(uintptr_t)ADDR_SVAR_GREENSCORE;
    int32_t        out  = 0;

    GetVarValue(vars[CommSlotForArmy(comm, slot)], &out);
    return out;
}

/* 0x0040F1C0, thiscall, one caller. The same walk as CommSlotForArmy above
 * and a different answer: the matching slot's COMM_ARMY_OFF_WAS_HERE rather
 * than its index.
 *
 * NO `army == 4` SHORTCUT here, which is the one structural difference and
 * not an oversight to tidy: CommSlotForArmy has it because 4 is the neutral
 * army and answers slot 4, and there is no fifth record to read a field out
 * of. Colour 4 simply finds nothing here and answers 0.
 *
 * No match and a slot whose field is 0 are indistinguishable, exactly as in
 * CommSlotForArmy. Reproduced. */
int32_t __attribute__((thiscall)) CommWasHereForArmy(void *comm, int32_t army)
{
    const uint8_t *p = (const uint8_t *)comm + COMM_ARMY_OFF_COLOUR;
    int32_t        i;

    for (i = 0; i < AM2_PLAYERS_MAX; i++) {
        if (*(const int32_t *)p == army)
            return *(const int32_t *)((const uint8_t *)comm
                                      + (uint32_t)i * AM2_PLAYER_STRIDE
                                      + COMM_ARMY_OFF_WAS_HERE);
        p += AM2_PLAYER_STRIDE;
    }

    return 0;
}

/* 0x0040F280, thiscall, two callers. Gives a slot an army colour, moving
 * whoever already had that colour into the slot it displaces -- a SWAP, not an
 * assignment, so no two players end up the same colour.
 *
 * Three things gate it and each rules out a different mistake.
 *
 * A negative colour is refused outright with -1, and that is the only path
 * that does not return the slot it was given.
 *
 * Only the HOST rearranges anything: the test is COMM_OFF_IS_HOST on the
 * GLOBAL comm object, not on `this`, and it is reproduced that way rather than
 * tidied into a field of the object being edited.
 *
 * The last guard is `jae` -- UNSIGNED -- against the slot CommSlotForArmy
 * answers for the incoming colour, and its safety comes from that callee
 * rather than from anything here. CommSlotForArmy answers 0 when no slot holds
 * the colour, so an unheld colour gives `slot >= 0`, which is always true
 * unsigned, and the swap is skipped. Had the miss answered -1 the comparison
 * would pass for every ordinary slot and the write would land 112 bytes before
 * the array. Worth stating because the guard reads as an ordinary bounds check
 * and is not one.
 *
 * WHAT VERIFIED THIS. Its counter is 0 and always will be -- both callers are
 * reconstructed, so nothing crosses the patched entry, and blindspots.py lists
 * it. A temporary probe on the mpoptions drive resolved it: one call,
 * `slot=0 colour=1 held=1`, which takes the guard and performs the SWAP rather
 * than skipping it. The arm that matters is the arm that runs.
 *
 * And ab.sh mpoptions can see a defect here, which was measured rather than
 * assumed: with the swap suppressed, the 128-node widget tree differs on the
 * colour toggle's SPRITE ID -- 1574528 against 1574530, the real datum carried
 * beside the renumbered pointer for exactly this reason -- and the frame goes
 * to 826 pixels against a budget of 300. Two independent detectors, neither of
 * which is the log. The -1 arm and the skip arm stay verified by reading. */
int32_t __attribute__((thiscall)) CommSetArmyColour(void *comm, int32_t slot,
                                                    int32_t colour)
{
    if (colour < 0)
        return -1;

    int32_t        held  = CommSlotForArmy(comm, colour);
    const uint8_t *world = *(const uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;

    if (*(const int32_t *)(world + COMM_OFF_IS_HOST)
        && (uint32_t)slot < (uint32_t)held) {
        uint8_t *base = (uint8_t *)comm + AM2_PLAYER_ARMY;
        int32_t *mine = (int32_t *)(base + (uint32_t)slot * AM2_PLAYER_STRIDE);
        int32_t  was  = *mine;

        *mine = colour;
        *(int32_t *)(base + (uint32_t)held * AM2_PLAYER_STRIDE) = was;
    }
    return slot;
}

int32_t __attribute__((thiscall)) CommSlotHasPlayer(void *comm, int32_t slot)
{
    int32_t id = *(const int32_t *)((const uint8_t *)comm +
                                    (size_t)slot * AM2_PLAYER_STRIDE +
                                    AM2_PLAYER_ID);

    return id != 0 && id != -1;
}

#define g_menuRow (*(int32_t *)(uintptr_t)ADDR_MENU_ROW)


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

/* TakeNumberKey -- original 0x00413A80, one caller. The 1..8 keys latch 0..7
 * into ADDR_NUMBER_KEY_SLOT.
 *
 * Eight arms and every one of them identical: `IsKeyDown(dik) &&
 * KeyChanged(dik)` -- held now and different from last poll, which is this
 * program's idiom for a fresh press -- then store and RETURN. The return is
 * the only thing that makes the eight an if-else rather than eight
 * independent tests, and with two keys pressed in one frame the lowest wins.
 *
 * WRITTEN AS A LOOP where the original is eight inlined copies, and that is a
 * choice worth stating rather than leaving to be noticed. The two are
 * behaviourally identical -- the return makes the arms an if/else chain, so
 * the lowest key wins either way, and `&&` keeps KeyChanged behind IsKeyDown
 * exactly as the original's second branch does. Eight copies of five lines
 * would say nothing the table does not. The same trade State1Menu's
 * twenty-one-entry table makes.
 *
 * WHAT THE NUMBER SELECTS IS NOT DECIDED HERE. The slot's only reader is
 * 0x00413BC0, which pushes it into a formatting call beside ADDR_OUR_POINTS
 * and ADDR_HUD_INDEX; nothing in this function knows or cares. The name is
 * for how it is written, not for what it is for.
 */
void __cdecl TakeNumberKey(void)
{
    static const int32_t kKeys[] = { 2, 3, 4, 5, 6, 7, 8, 9 };
    int32_t i;

    for (i = 0; i < (int32_t)(sizeof kKeys / sizeof kKeys[0]); i++) {
        if (IsKeyDown(kKeys[i]) && KeyChanged(kKeys[i])) {
            *(int32_t *)(uintptr_t)ADDR_NUMBER_KEY_SLOT = i;
            return;
        }
    }
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

/* The original addresses this as `[type*40 + 0x004878B8]`, which is the cost
 * field of record `type` -- written out from the table's real base here. See
 * ADDR_UNIT_TYPES: the constant in the instruction is the base plus the
 * field, and taking it for the base is what put that table 0x20 bytes late. */
uint32_t __cdecl UnitTypeCost(int32_t type)
{
    return *(const uint32_t *)((uintptr_t)ADDR_UNIT_TYPES
                               + (uint32_t)type * AM2_UNIT_TYPE_STRIDE
                               + UNIT_TYPE_OFF_COST);
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

/* 0x004275B0, four callers. The exact mirror of the function above, and the
 * only difference is which way the first test goes: PRESSED wants the key down
 * and changed, RELEASED wants it up and changed. Both bindings are tried, and
 * either one answering yes is enough.
 *
 * That is the `!IsKeyDown && KeyChanged` idiom the in-mission ESCAPE handler
 * and the widget layer's cancel both use, here over an ACTION rather than a
 * scancode -- ADDR_KEY_BINDINGS is a pair of scancodes per action, so a player
 * with two keys bound to one action releases it by letting go of either.
 *
 * The original unrolls the two bindings and bounces each scancode through a
 * stack slot to zero-extend it. Written as the same loop the pressed variant
 * uses: the memory round-trip is the compiler's, not the function's, and
 * having the pair differ only in one operator is worth more than matching
 * instruction for instruction.
 *
 * VERIFIED BY DRIVING THE KEY APART, which is the one thing an A/B can never
 * do here: both sides get the same keystroke and would agree about ignoring
 * it, so a pressed/released mix-up is invisible to the frame comparison.
 *
 * F1 is action 0x14 and MissionInput is one of the four callers. In a live
 * mission, with the sub-state read over the control socket:
 *
 *   before        0x21   ordinary play
 *   F1 HELD       0x21   still ordinary play
 *   F1 RELEASED   0x16   the info bitmap
 *
 * `key F1 down` and `key F1 up` separate the two edges; a tap has both and
 * would not have told the two apart. Tested in the failing direction with the
 * first test inverted -- i.e. spelled exactly like ActionKeyPressed -- and the
 * held reading becomes 0x16, so the check discriminates the only thing that
 * distinguishes this function from its sibling.
 *
 * It runs 11,419 times in a Boot Camp mission, so the coverage is real. */
int32_t __cdecl ActionKeyReleased(int32_t action)
{
    int32_t i;

    for (i = 0; i < 2; i++) {
        uint32_t k = g_keyBindings[action * 2 + i];

        if (g_curKeys[k] & 0x80)
            continue;
        if ((g_prevKeys[k] ^ g_curKeys[k]) & 0x80)
            return 1;
    }
    return 0;
}

/* 0x00424900, four callers. Asks whether any of four keys has just been
 * RELEASED, and CONSUMES the one that had.
 *
 * The four are SPACE, F1, ESCAPE and RETURN -- every key that means "I have
 * read this, take it away" -- and they are tried in that order, first match
 * wins. Each is the same `!IsKeyDown && KeyChanged` idiom ActionKeyReleased
 * uses, so this is that function's logic over a fixed set rather than over a
 * bound action.
 *
 * IT IS DESTRUCTIVE, which the old name hid completely. ConsumeKey runs on the
 * key that matched, so asking twice in one frame answers yes then no. Both of
 * frame.cpp's call sites depend on that: the overlay is dismissed once, not on
 * every frame the key happens to be up.
 *
 * The name it replaces was ADDR_EVENT_FLAG_8_TEST, taken from what two callers
 * do with the answer -- clear pause flag 8 -- rather than from what the
 * function decides.
 *
 * DRIVEN, on the info bitmap. In a live mission F1 puts the sub-state to 0x16
 * and each of these dismisses it back to 0x21: a click, SPACE, and ESCAPE,
 * checked on separate runs. The counter is blind -- all three call sites are
 * in frame.cpp and ours -- so the sub-state is the evidence.
 *
 * Stubbing it to always answer no changes the outcome: F1 then never reaches
 * 0x16 at all. That says the drive genuinely exercises this function and not
 * something else doing the dismissing. What it does NOT say is why the RAISE
 * is affected -- the three call sites interact through the pause flag and the
 * bitmap slot, and that chain was not traced. Recorded as an observation, not
 * as a mechanism.
 *
 * The consume itself is verified by READING. Nothing in these drives asks
 * twice in one frame, so a build without ConsumeKey would behave identically
 * on every configuration this project has. */
int32_t __cdecl DismissKeyReleased(void)
{
    static const int32_t keys[4] = {
        AM2_DIK_SPACE, AM2_DIK_F1, AM2_DIK_ESCAPE, AM2_DIK_RETURN
    };
    int32_t i;

    for (i = 0; i < 4; i++) {
        if (IsKeyDown(keys[i]))
            continue;
        if (!KeyChanged(keys[i]))
            continue;
        ConsumeKey(keys[i]);
        return 1;
    }
    return 0;
}

/* The game's own atoi. The offline test maps the image as data and cannot
 * call into it, so this goes through AM2_IMAGE like the rest of misc.cpp's
 * seams -- and atoi is stateless, so the two agree anyway. */
typedef int32_t (__cdecl *AM2_AtoiFn)(const char *s);
#define orig_atoi ((AM2_AtoiFn)AM2_IMAGE(ADDR_CRT_ATOI))

typedef void (__cdecl *AM2_SeqRunFn)(void *ctx);

/* The three the seq adders below still reach the image for: the pool
 * allocator, the kind-6 adder, and the game's own LCG. */
typedef void  (__cdecl *AM2_SeqAdd6Fn)(const int32_t *at, int32_t a);
typedef int32_t (__cdecl *AM2_SeqRandFn)(void);
/* SeqAlloc is reconstructed; maprow.h declares it and is included above. */
#define orig_seq_rand         ((AM2_SeqRandFn)(uintptr_t)ADDR_GAME_RAND)

/* The seven steppers SeqRun dispatches to. All take (index, record, context)
 * and answer the next index; kinds 2 and 3 share one. Still original. */
typedef int32_t (__cdecl *AM2_SeqStepFn)(int32_t at, void *rec, void *ctx);
/* SeqRetire is reconstructed and declared in maprow.h; this typedef and the
 * macro under it reached it through the image, which checkseams now refuses. */
/* Reconstructed now -- maprow.h declares it. */
/* Reconstructed now -- misc.h declares it. */
/* Reconstructed now -- misc.h declares it. */
/* Reconstructed now -- misc.h declares it. */

/* 0x0043B7C0, one caller -- the per-frame path, and reached only in a network
 * game. The AI taking over armies whose players have gone.
 *
 * Two conditions per army and both are needed: COMM_ARMY_OFF_WAS_HERE set, so
 * somebody once held it, and CommSlotHasPlayer false, so nobody holds it now.
 * An army that was never occupied is left alone, which is what stops the AI
 * being handed every empty slot at the start of a session.
 *
 * There is no "already done" flag, and reconstructing the callee has settled
 * what that means. LoadArmyPlacement reads a file, walks its records and
 * writes NOTHING back to the comm record -- so this really does re-read the
 * army's placement file every frame for as long as the slot stays abandoned.
 * That was left open here as "unless loading it clears WAS_HERE"; it does not.
 * Worth knowing before reading a repeated parse as a fault.
 *
 * Reached only in a network game and this project cannot start one, so it is
 * verified by reading. Its counter reads 0 on a Boot Camp mission, which is
 * the expected answer rather than a surprise: TakeMenuRequest guards the call
 * on ADDR_NET_GAME. Confirmed installed the other way, against the log --
 * 856 patch lines, 856 from checkpatches. */
void __cdecl AiTakeAbandoned(void)
{
    const uint8_t *comm = *(const uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    int32_t        army;

    for (army = 0; army < AM2_COMM_ARMY_COUNT; army++) {
        const uint8_t *rec = comm + (uint32_t)army * AM2_PLAYER_STRIDE;

        if (!*(const int32_t *)(rec + COMM_ARMY_OFF_WAS_HERE))
            continue;
        if (CommSlotHasPlayer((void *)(uintptr_t)comm, army))
            continue;

        LoadArmyPlacement(army);
    }
}

/* 0x00461930, one caller -- the per-frame path. Run the seq walker over both
 * contexts, in this order.
 *
 * That is the whole function. The walker itself stays original: see
 * ADDR_SEQ_RUN for its record layout, and note that "seq" is the program's own
 * word for this band rather than one of ours -- it comes from "Couldn't Blt Seq
 * Pixels" a few hundred bytes further on. What a seq IS remains unestablished,
 * so the name says where the vocabulary came from and stops there.
 *
 * The original pushes both context pointers and cleans eight bytes once at the
 * end rather than four after each call, which is ordinary cdecl bookkeeping and
 * leaves nothing to reproduce.
 *
 * Measured at 19,066 calls against ComposeFrame's 19,144 -- once a frame.
 *
 * That measurement is also how this was caught being NOT INSTALLED. The first
 * attempt at adding its patch_replace did not match misc_install's opening
 * line and silently changed nothing, so the function compiled, linked, and was
 * never reached. `counts` answered "(nothing traced)" -- which reads exactly
 * like a full trace table -- and the game's own log settled it: 853 patch
 * lines where checkpatches counts 854. A reconstruction nothing calls is
 * invisible to every static check there is, and the log is the list of
 * installs. */
void __cdecl SeqRunBoth(void)
{
    SeqRun((void *)(uintptr_t)ADDR_SEQ_CTX_A);
    SeqRun((void *)(uintptr_t)ADDR_SEQ_CTX_B);
}

/* 0x0042E310, one caller -- read a sprite's identity back out of its FILENAME.
 *
 * The loader turns three integers into "%02d_%03d_%02d_*.bmp"; this is the
 * same convention read the other way, at fixed positions 0, 3 and 7. Hence
 * the two tests: at least ten characters, and an underscore at index 2. It
 * checks the FIRST separator and not the second, so "01_001x00_..." parses.
 *
 * All three outputs are cleared before either test, so a caller that ignores
 * the answer still sees zeroes rather than whatever was on its stack. */
int32_t __cdecl ParseSpriteName(const char *name, int32_t *set,
                                int32_t *index, int32_t *frame)
{
    *set   = 0;
    *index = 0;
    *frame = 0;

    if (strlen(name) < 10)
        return 0;
    if (name[2] != '_')
        return 0;

    *set   = orig_atoi(name);
    *index = orig_atoi(name + 3);
    *frame = orig_atoi(name + 7);
    return 1;
}


/* ShowMpResult is reconstructed, in win32/frame.cpp. Declared here rather than
 * by including that header, because misc.cpp is on the flat side of the split
 * and frame.h names Win32 types -- the same reason air.cpp declares
 * PlaySoundAt. Its own signature names none, which is what makes this legal.
 *
 * `extern "C"` is correct here: frame.h opens an extern "C" block at line 25
 * and ShowMpResult is inside it. gameproc.cpp's LoadAudioSection is the
 * opposite case, so the two spellings differ for a reason. */
extern "C" void __cdecl ShowMpResult(int32_t result);

/* 0x00421800, one caller. Decides whether the mission was WON and shows the
 * matching end screen. Two ways of deciding, and which one is used depends on
 * whether teams are in play.
 *
 * THE ARGUMENT IT PASSES ON IS "LOST", NOT "WON". Both arms compute a win with
 * `sete` and then invert it with a second `sete` before the push, so
 * ADDR_SHOW_MP_RESULT takes 0 for a win. Two inversions in four instructions
 * is exactly the shape that gets transcribed once instead of twice.
 *
 * The team arm needs BOTH the caller's flag and a non-zero team of our own;
 * a player with no team falls through to the solo test even in a team game.
 * Then the win is "the winning army is on my team".
 *
 * The fallback is "the winning army's SLOT is my owner id" -- a slot compared
 * against ADDR_DEFAULT_OWNER, which reads oddly until you notice
 * CommSlotForArmy answers the identity for every army a script can write, so
 * the two are the same number in practice. Reproduced as the comparison the
 * original makes rather than as the one it means.
 *
 * VERIFIED BY READING. Its one caller is the end-of-mission path in a
 * multiplayer game, which no drive reaches. */
void __cdecl MissionNetworked(int32_t army, int32_t teamGame)
{
    const uint8_t *comm  = *(const uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    uint32_t       mine  = *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER;
    int32_t        slot  = CommSlotForArmy((void *)(uintptr_t)comm, army);
    int32_t        won;

    if (teamGame) {
        int32_t myTeam = *(const int32_t *)(comm + (size_t)mine
                                            * AM2_PLAYER_STRIDE
                                            + COMM_ARMY_OFF_TEAM);

        if (myTeam) {
            int32_t theirTeam =
                *(const int32_t *)(comm + (size_t)slot * AM2_PLAYER_STRIDE
                                   + COMM_ARMY_OFF_TEAM);

            won = (myTeam == theirTeam);
            ShowMpResult(won ? 0 : 1);
            return;
        }
    }

    won = (slot == (int32_t)mine);
    ShowMpResult(won ? 0 : 1);
}

/* 0x0040F2F0, one caller, thiscall. The DirectPlay id of a comm slot.
 *
 * SLOT -1 ANSWERS 0 RATHER THAN INDEXING BACKWARDS, which is the whole of the
 * guard -- there is no upper bound, so a slot above three reads past the four
 * player records and nothing here stops it. Reproduced.
 *
 * The stride is written `(slot * 8 - slot) << 4` in the original, which is
 * 112 and is AM2_PLAYER_STRIDE. Written as the multiplication it is; the
 * shift-and-subtract is MSVC's, not a fact about the layout.
 *
 * MEASURED AT 0. Its one caller is on the multiplayer path, which no drive
 * here reaches with a live session, so the -1 guard and the stride are
 * verified by reading. The counter is not blind -- that caller is the
 * original's.
 */
int32_t __attribute__((thiscall)) CommPlayerId(void *comm, int32_t slot)
{
    if (slot == -1)
        return 0;

    return *(const int32_t *)((const uint8_t *)comm
                              + (size_t)slot * AM2_PLAYER_STRIDE
                              + AM2_PLAYER_ID);
}

/* 0x0040F8A0 and 0x0040F8E0, one caller each and both in 0x00431850. The same
 * loop over the comm object's players, differing only in which flag it tests.
 *
 * THEIR CALLER IS WHAT NAMES THEM, since the two fields say nothing. When the
 * first answers no, 0x00431850 shows "Not everybody has the same
 * version/map/rules." -- so AM2_PLAYER_AGREED is the checksum handshake's
 * answer. The second is called immediately after that same function sets
 * AM2_PLAYER_READY on ADDR_DEFAULT_OWNER's own slot, which is what a
 * ready-check looks like from the outside.
 *
 * A SLOT WITH NO PLAYER IS SKIPPED, not failed: an AM2_PLAYER_ID of 0 or -1
 * means empty, and the flag under it is never read. So four slots with one
 * player answer yes on that player alone.
 *
 * AN EMPTY TABLE ANSWERS YES. COMM_OFF_PLAYER_COUNT being zero or less returns
 * 1 without looking at anything, so "everyone agrees" and "everyone is ready"
 * are both vacuously true with nobody present. Reproduced; the caller guards
 * that separately by refusing a count of one or less before it calls.
 *
 * BOTH READ THE COUNT FROM THE GLOBAL COMM OBJECT AND THE RECORDS FROM `this`.
 * The two are the same object at every call site -- the caller loads ecx from
 * the same global -- so the redundancy is invisible. Reproduced as written
 * rather than folded, because folding it would assert they must agree.
 *
 * BOTH MEASURED AT 0. Their one caller is 0x00431850, which is on the
 * multiplayer start path, and no drive here reaches a live session -- the
 * suite gets as far as ENTER BATTLE NAME and DirectPlay will not open a
 * TCP/IP session on this machine. The counters are not blind; that caller
 * is the original's. So both are verified by reading, and the caller is
 * where the names come from, so the naming is no better verified than the
 * bodies are.
 */
static int32_t CommAllPlayersFlagged(void *comm, uint32_t flag)
{
    const uint8_t *rec  = (const uint8_t *)comm + AM2_PLAYER_ID;
    int32_t        n    = *(const int32_t *)
                              ((const uint8_t *)*(void *const *)
                                   (uintptr_t)ADDR_COMM_OBJECT
                               + COMM_OFF_PLAYER_COUNT);
    int32_t        i;

    for (i = 0; i < n; i++, rec += AM2_PLAYER_STRIDE) {
        int32_t id = *(const int32_t *)rec;

        if (id == 0 || id == -1)
            continue;                   /* no player in this slot */
        if (*(const int32_t *)(rec + (flag - AM2_PLAYER_ID)) == 0)
            return 0;
    }
    return 1;
}

int32_t __attribute__((thiscall)) CommAllPlayersAgreed(void *comm)
{
    return CommAllPlayersFlagged(comm, AM2_PLAYER_AGREED);
}

int32_t __attribute__((thiscall)) CommAllPlayersReady(void *comm)
{
    return CommAllPlayersFlagged(comm, AM2_PLAYER_READY);
}

typedef int32_t (__cdecl *AM2_RandFn)(void);

/* 0x00402F00, one caller. A random value that AVERAGES its first argument.
 *
 * `100 - spread` draws of `rand() % (centre * 2)`, summed and divided by that
 * count. Each draw averages the centre, so the mean is the centre whatever the
 * spread is -- what the spread changes is how many draws are averaged, and so
 * how far a single answer strays. A spread of 0 short-circuits and returns the
 * centre unchanged; 99 leaves one draw; 100 or more clamps to one as well.
 *
 * SO THE ARGUMENT IS AN INVERSE TIGHTNESS AND NOT A RANGE, which is the thing
 * to get right: a bigger `spread` gives a noisier answer by taking FEWER
 * samples, not by widening any of them. The width is fixed at twice the
 * centre.
 *
 * It draws from ADDR_GAME_RAND, the image's own LCG, and must -- libc's would
 * leave the game's sequence standing still.
 *
 * The division is signed and the count is at least one, so there is no divide
 * by zero; the sum can overflow with a large centre and ninety-nine draws, and
 * the original does not guard it either.
 *
 * Measured at 0: its one caller does not run on any drive here, and that
 * caller is the original's, so the counter is not blind. Verified by reading.
 */
int32_t __cdecl RandomAround(int32_t centre, int32_t spread)
{
    int32_t n     = 100 - spread;
    int32_t total = 0;
    int32_t i;

    if (spread == 0)
        return centre;

    if (n < 1)
        n = 1;

    for (i = 0; i < n; i++)
        total += ((AM2_RandFn)AM2_IMAGE(ADDR_GAME_RAND))() % (centre * 2);

    return total / n;
}

/* MovieBuildName -- original 0x0042E770, four call sites.
 *
 * Turn a short movie name into a filename: copy it, append "sml" when the
 * small set is in use, and append ".smk". Four names are exempt from the
 * "sml" -- they are already the file the game wants.
 *
 * THE FLAG IT CONSULTS WAS MISNAMED, AND THIS IS WHAT SETTLED IT.
 * ADDR_OPT_BIG_MOVIES was ADDR_OPT_MUSIC, named off `-bm` and `-sm` alone,
 * with CLAUDE.md recording that SetGameDir also latches it on entering the
 * `avi` directory and that "one of the two readings is wrong". Neither
 * reading came from a function that USES the value. This one does, and it
 * uses it to choose between `name.smk` and `namesml.smk` -- so `-bm` is big
 * movies, `-sm` is small ones, and latching it on finding `avi` means the
 * full-size set is present. Read a consumer, not another writer.
 *
 * THE ORIGINAL'S FOUR COMPARISONS ARE INLINED strcmp, not a table walk, and
 * they are reproduced in the order it makes them. A loop over four pointers
 * would be the same function and a smaller one; it would also be the first
 * place a fifth name could quietly appear.
 *
 * Unbounded, like SetGameDir beside it: `dst` is a 0x20-byte stack buffer at
 * three of the four call sites and the longest name the image ships is seven
 * characters, so "credits" + ".smk" is twelve. The fourth passes
 * ADDR_MOVIE_TO_PLAY, which a mission's script fills, and nothing checks its
 * length. Kept as it is, for the reason gamedir.cpp gives.
 */
void __cdecl MovieBuildName(char *dst, const char *name)
{
    strcpy(dst, name);

    if (strcmp(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_3DO)) != 0
        && strcmp(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_CREDITS)) != 0
        && strcmp(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_ACT2)) != 0
        && strcmp(name, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_PORTAL)) != 0
        && *(const int32_t *)AM2_IMAGE(ADDR_SLOW_MACHINE)
        && !*(const int32_t *)AM2_IMAGE(ADDR_OPT_BIG_MOVIES))
        strcat(dst, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_SMALL));

    strcat(dst, (const char *)AM2_IMAGE(ADDR_STR_MOVIE_EXT));
}

/* SeqStepKind2 -- original 0x00461310, and BOTH kinds 2 and 3 reach it.
 *
 * The whole of it is a two-frame life: bump ROW_OFF_STAMP_54, and once that
 * has passed 1 hand the record to SeqRetire. On the frame before that it
 * re-links the row into the map descriptor's cell lists instead.
 *
 * `inc; cmp 1; jbe` is UNSIGNED, so a stamp of 0xFFFFFFFF wraps to 0 and takes
 * the same arm as a fresh record. Nothing writes that value -- the adders zero
 * it -- and the comparison is reproduced as written rather than as `n == 1`,
 * which would be the same function for every reachable input and a different
 * one on paper.
 *
 * The relink is RowUpdate with its second argument set, which orig.h
 * records as "force the work even when nothing moved". So a kind-2 record's
 * row is put back into the map every frame it is alive, whether it moved or
 * not.
 */
int32_t __cdecl SeqStepKind2(int32_t at, void *rec, void *ctx)
{
    uint8_t *row = *(uint8_t **)((uint8_t *)rec + SEQ_OFF_ROW);
    uint32_t n   = *(const uint32_t *)(row + ROW_OFF_STAMP_54) + 1;

    (void)at;
    *(uint32_t *)(row + ROW_OFF_STAMP_54) = n;

    if (n > 1)
        return SeqRetire(ctx, rec);

    RowUpdate(row, 1, (void *)(uintptr_t)ADDR_MAP_DESC);
    return *(const int16_t *)((uint8_t *)rec + SEQ_OFF_NEXT);
}

/* SeqAddKind6 -- original 0x00461660, two callers, and the third of the
 * adders. SeqAddKind7 is one of its callers, with a random 0..2.
 *
 * The same shape as the other two and three things of its own.
 *
 * ITS SPRITE IS A VARIANT RATHER THAN A FRAME. It shares ADDR_SEQ_SPRITES_7
 * with kind 7, which takes entry 0, and indexes it by `variant * 8` -- so the
 * array is variants of eight frames each and this picks the first frame of
 * one of them. The 8 is a global, not a literal.
 *
 * ROW_OFF_FIELD_26 IS PROBABLY A DEPTH KEY, and this is what suggests it.
 * Kind 5 writes 0x3E8 and kind 7 writes 1, which said only "a scale or a
 * count"; kind 6 writes the terrain attribute under its own point, scaled,
 * plus 0x3F2. Three constants of 1000, 1010-ish and 1 with a ground-height
 * term in the middle one read as a sort order rather than as a size.
 * Recorded as a reading; nothing here shows what consumes it.
 *
 * It also writes 1 into the row's first dword where kinds 5 and 7 write 0.
 * That is ROW_OFF_FLAGS bit 0, which ADDR_ROW_RELINK tests to decide between
 * unlinking and re-linking -- so a kind-6 row starts linkable and the other
 * two start not. Reproduced; the asymmetry is the original's.
 */
void __cdecl SeqAddKind6(const int32_t *at, int32_t variant)
{
    uint8_t *seq;
    uint8_t *row;

    if (!PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT),
                     (const AM2_Point *)at))
        return;

    seq = (uint8_t *)SeqAlloc((void *)(uintptr_t)ADDR_SEQ_CTX_A);
    row = *(uint8_t **)(seq + SEQ_OFF_ROW);

    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND6;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = (uint8_t)variant;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_10) = 0;
    *(int32_t *)(seq + SEQ_OFF_OWNER)    = 0;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     = AM2_SEQ_LIFE6;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    *(uint8_t *)(row + ROW_OFF_CELL)     = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54) = 0;
    *(int32_t *)(row + 0)                = 1;
    *(void **)(row + ROW_OFF_SPRITE)     =
        (*(void *const *const *)AM2_IMAGE(ADDR_SEQ_SPRITES_7))
            [variant * *(const int32_t *)AM2_IMAGE(AM2_SEQ_VARIANT_STRIDE)];
    *(int32_t *)(row + ROW_OFF_X)        = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST) = 0;
    *(int32_t *)(row + ROW_OFF_FIELD_2C) = 0;
    *(int16_t *)(row + ROW_OFF_FIELD_26) =
        (int16_t)(ScaleBy32Blocks(TileAttrAt(TileOfPoint((uint32_t)*at)))
                  + AM2_SEQ_DEPTH_BIAS);
}

/* SeqAddKind4 -- original 0x00461350, one caller, and the fourth adder.
 *
 * The only one that fills ADDR_SEQ_CTX_B rather than ctx A, which is what
 * makes the per-frame step walk two contexts. Its one caller is
 * SeqStepKind5's emitter, so a kind 4 is what a kind 5 throws off every
 * 300 ms.
 *
 * IT IDENTIFIES ROW_OFF_FIELD_2C, which the other three adders only ever
 * zeroed. This one writes ADDR_REMAP_SHADES[0] into it -- and MAPOBJ_OFF_LUT
 * is already 0x2C, "-> the sprite's +0x34", which is AM2_Sprite::lut. So the
 * field is the row's REMAP TABLE and a kind 4 is a shaded sprite where the
 * other kinds are not. A field that three writers agree is zero says nothing;
 * the fourth writer is what names it.
 *
 * AND IT SAYS MAPOBJ_* AND ROW_OFF_* ARE ONE STRUCTURE. Two name families
 * have been growing at the same offsets -- 0x00 is MAPOBJ_OFF_FLAGS "bit 0
 * clear means do not draw" and is also the bit RowUpdate tests, and 0x2C is
 * both. Recorded in orig.h rather than merged here: collapsing two families
 * is a change of its own.
 *
 * ITS SECOND ARGUMENT IS A Y-ADJUST, and kind 5 passes 15. So the emitted
 * sprite floats fifteen above the point that threw it.
 *
 * The depth key is a flat 0x3F2, where kind 6 writes that plus the terrain
 * under its point. Two writers agreeing on the bias is what turns "probably a
 * depth key" from one function's guess into a shape.
 *
 * Three of its constants are globals rather than literals -- 3, -1 and a life
 * of 120 -- which is the only reason they can be named at all.
 */
void __cdecl SeqAddKind4(const int32_t *at, int32_t yAdjust)
{
    uint8_t *seq;
    uint8_t *row;

    if (!PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT),
                     (const AM2_Point *)at))
        return;

    seq = (uint8_t *)SeqAlloc((void *)(uintptr_t)ADDR_SEQ_CTX_B);
    row = *(uint8_t **)(seq + SEQ_OFF_ROW);

    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND4;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) =
        *(const int32_t *)AM2_IMAGE(ADDR_SEQ_K4_DRIFT_X);
    *(int32_t *)(seq + SEQ_OFF_FIELD_10) =
        *(const int32_t *)AM2_IMAGE(ADDR_SEQ_K4_DRIFT_Y);
    *(int32_t *)(seq + SEQ_OFF_OWNER)    = 0;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     =
        *(const int32_t *)AM2_IMAGE(ADDR_SEQ_K4_STEP_MS);

    *(uint8_t *)(row + ROW_OFF_CELL)     = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54) = 0;
    *(int32_t *)(row + 0)                = 1;
    *(void **)(row + ROW_OFF_SPRITE)     =
        **(void *const *const *)AM2_IMAGE(ADDR_SEQ_SPRITES_5);
    *(int32_t *)(row + ROW_OFF_X)        = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST) = (int16_t)yAdjust;
    *(void **)(row + ROW_OFF_FIELD_2C)   =
        *(void *const *)AM2_IMAGE(ADDR_REMAP_SHADES);
    *(int16_t *)(row + ROW_OFF_FIELD_26) = AM2_SEQ_DEPTH_BIAS;
}

/* SeqStepKind4 -- original 0x004613E0, one caller.
 *
 * A kind 4 is a four-cell shaded sprite that DRIFTS and then goes out. Every
 * ADDR_SEQ_K4_STEP_MS it moves 3 right, 1 up and 3 further off the ground,
 * and after a per-cell number of steps it advances to the next cell -- new
 * sprite, new shade -- retiring once it has used all four. It also retires
 * the moment it drifts off the map. Smoke, on the evidence, but that is a
 * reading and the code says only "shaded, drifting, four cells".
 *
 * SEQ_OFF_LIFE IS AN INTERVAL HERE AND A TOTAL IN KIND 5. This stepper
 * compares the row's elapsed against it and then ZEROES the elapsed, so it is
 * the time between steps; kind 5 compares and retires, so it is a whole
 * lifetime. That makes two fields in this record -- LIFE and the row's
 * STAMP_54 -- whose units are chosen by the kind. Three of the four kinds
 * read this way now, and the pattern is that nothing in a seq record means
 * one thing.
 *
 * SEQ_OFF_GATE IS A STEP COUNTER HERE, where kind 5 uses it as a millisecond
 * accumulator. Same field, incremented by one per step and reset to 1 -- not
 * to 0 -- when the cell advances, which keeps the walker from skipping the
 * record on the frame after.
 *
 * TWO PARALLEL TABLES INDEXED BY THE CELL. The sprite comes from
 * ADDR_SEQ_SPRITES_5 and the remap from ADDR_REMAP_SHADES, both at the same
 * index, so cell 0..3 is progressively dimmer as it is a later frame. That
 * the two are read with one index is what says the four shade tables are an
 * animation and not four independent effects.
 *
 * The relink passes force 0 where kind 2 passes 1; this row really has moved.
 */
int32_t __cdecl SeqStepKind4(int32_t at, void *rec, void *ctx)
{
    uint8_t *r   = (uint8_t *)rec;
    uint8_t *row = *(uint8_t **)(r + SEQ_OFF_ROW);

    (void)at;

    if (*(const uint32_t *)(row + ROW_OFF_STAMP_54)
            > *(const uint32_t *)(r + SEQ_OFF_LIFE)) {
        uint32_t step = *(const uint32_t *)(r + SEQ_OFF_GATE) + 1;
        uint32_t cell = *(const uint8_t *)(row + ROW_OFF_CELL);

        *(uint32_t *)(r + SEQ_OFF_GATE) = step;

        if (step > ((const uint32_t *)AM2_IMAGE(ADDR_SEQ_K4_HOLD))[cell]) {
            cell++;
            *(uint8_t *)(row + ROW_OFF_CELL) = (uint8_t)cell;

            if (cell >= AM2_SEQ_K4_CELLS)
                return SeqRetire(ctx, rec);

            *(void **)(row + ROW_OFF_SPRITE) =
                (*(void *const *const *)AM2_IMAGE(ADDR_SEQ_SPRITES_5))[cell];
            *(void **)(row + ROW_OFF_FIELD_2C) =
                ((void *const *)AM2_IMAGE(ADDR_REMAP_SHADES))[cell];

            *(uint32_t *)(r + SEQ_OFF_GATE) = 1;
        }

        *(int16_t *)(row + ROW_OFF_Y_ADJUST) +=
            *(const int16_t *)AM2_IMAGE(ADDR_SEQ_K4_RISE);
        *(int16_t *)(row + ROW_OFF_X) +=
            (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_0C);
        *(int16_t *)(row + ROW_OFF_Y) +=
            (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_10);

        *(int32_t *)(row + ROW_OFF_STAMP_54) = 0;

        if (!PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT),
                         (const AM2_Point *)(row + ROW_OFF_X)))
            return SeqRetire(ctx, rec);

        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    *(int32_t *)(row + ROW_OFF_STAMP_54) +=
        *(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;

    return *(const int16_t *)(r + SEQ_OFF_NEXT);
}

/* SeqStepKind5 -- original 0x004614D0, one caller.
 *
 * Kind 5 is an EMITTER. Every ADDR_SEQ_EMIT_MS -- 300 -- it adds a kind 4 at
 * its own point jittered by -4, 0 or +4 in x, and it retires once its elapsed
 * time has passed SEQ_OFF_LIFE, which for a kind 5 defaults to 95 seconds.
 * The kind 4 goes into ADDR_SEQ_CTX_B, the other context, which is what the
 * per-frame step walks second.
 *
 * SO SEQ_OFF_GATE IS NOT A FLAG, and orig.h has said "gate" since before
 * anything read it. This stepper adds the frame delta to it every frame and
 * subtracts the interval whenever it passes it: a millisecond accumulator.
 * The adders write 1 into it, which is not "true" but "start just above zero
 * so the walker does not skip me". The name is kept because what the WALKER
 * does with it is still a gate, and the field's comment now carries both.
 *
 * ROW_OFF_STAMP_54 IS MILLISECONDS HERE AND A FRAME COUNT IN KIND 2. One
 * field, two units, chosen by the kind that owns the record. Worth saying
 * plainly: there is no reading of that field that is right for both.
 *
 * The jitter is `(rand() % 3) * 4 - 4` added to the low half of the packed
 * point, so it moves in x only, and the original does it with a 16-bit add
 * into a stack copy of the point -- an argument slot it reuses as a local.
 * Written as a local; the reuse is the compiler's.
 */
int32_t __cdecl SeqStepKind5(int32_t at, void *rec, void *ctx)
{
    uint8_t *r    = (uint8_t *)rec;
    uint8_t *row  = *(uint8_t **)(r + SEQ_OFF_ROW);
    int32_t  step;

    (void)at;

    if (*(const uint32_t *)(row + ROW_OFF_STAMP_54)
            > *(const uint32_t *)(r + SEQ_OFF_LIFE))
        return SeqRetire(ctx, rec);

    if (*(const uint32_t *)(r + SEQ_OFF_GATE)
            > *(const uint32_t *)AM2_IMAGE(ADDR_SEQ_EMIT_MS)) {
        int32_t pt = *(const int32_t *)(row + ROW_OFF_X);

        *(int16_t *)&pt = (int16_t)(*(const int16_t *)&pt
                                    + (orig_seq_rand() % 3) * 4 - 4);
        SeqAddKind4(&pt, AM2_SEQ_EMIT_ARG);

        *(int32_t *)(r + SEQ_OFF_GATE) -=
            *(const int32_t *)AM2_IMAGE(ADDR_SEQ_EMIT_MS);
    }

    step = *(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;
    *(int32_t *)(row + ROW_OFF_STAMP_54) += step;
    *(int32_t *)(r + SEQ_OFF_GATE)       += step;

    return *(const int16_t *)(r + SEQ_OFF_NEXT);
}

/* SeqRun -- original 0x00461870, two callers.
 *
 * Walk one seq context and step every live record. The walk is INDEX-CHAINED
 * rather than sequential: entry 0 is a head whose SEQ_OFF_NEXT names the first
 * record, each arm RETURNS the next index, and a record that is skipped
 * supplies its own successor. So the order is the list's and not the array's.
 *
 * THE GATE IS TESTED FOR ZERO, NOT FOR POSITIVE, and orig.h said the second
 * until this was read. `test edx, edx; jbe` is `je`, because `test` clears the
 * carry -- an idiom CLAUDE.md already records, and this is the second time it
 * has caught somebody in this tree. A negative gate would be walked.
 *
 * ARM 1 IS AN INFINITE LOOP AND IT IS REPRODUCED. Kind 1's jump-table entry
 * goes straight to the loop test with the index UNCHANGED, so a live record of
 * kind 1 would be stepped forever and the game would hang. orig.h described it
 * as "does nothing but continue", which is what it looks like and not what it
 * does -- continuing needs a new index and this arm supplies none. Nothing
 * creates a kind 1: the three adders make 5, 6 and 7. Written as an empty case
 * inside the loop, which hangs in exactly the same way; correcting it would be
 * inventing behaviour the original does not have, and a `break` here would
 * silently paper over a state that should be impossible.
 *
 * A kind above 7 takes the same exit as a closed gate, which is the arm the
 * jump table cannot express.
 *
 * The seven steppers stay original and are reached by address; all seven take
 * (index, record, context) and answer the next index.
 *
 * IT WALKS AN EMPTY LIST ON EVERY DRIVE HERE, measured rather than assumed.
 * Reading ADDR_SEQ_CTX_A over the control socket through a Boot Camp mission
 * with firing gives a count of ZERO throughout -- while its record array is
 * allocated and its capacity reads 200, so the subsystem is up and simply has
 * nothing in it. Nothing on these drives calls the adders: kind 5 comes from
 * an event action and kind 7 from the flame cheat.
 *
 * Which is why the mutation says nothing. Making the loop step NO record --
 * take the next index and continue, for every entry -- leaves `bootcamp` at
 * 22 and `mission` at 287. That is not "the steppers do not matter"; it is
 * "there are no records", and the two are worth separating. The chain, the
 * gate and the dispatch stand on the disassembly.
 */
void __cdecl SeqRun(void *ctx)
{
    uint8_t *recs = *(uint8_t **)((uint8_t *)ctx + SEQ_CTX_OFF_RECORDS);
    int32_t  at   = *(const int16_t *)(recs + SEQ_OFF_NEXT);

    while (at > 0) {
        uint8_t *rec = *(uint8_t **)((uint8_t *)ctx + SEQ_CTX_OFF_RECORDS)
                       + at * AM2_SEQ_RECORD_SIZE;

        if (*(const uint32_t *)(rec + SEQ_OFF_GATE) == 0) {
            at = *(const int16_t *)(rec + SEQ_OFF_NEXT);
            continue;
        }

        switch (*(const uint32_t *)(rec + SEQ_OFF_KIND)) {
        case 0: at = SeqStepKind0(at, rec, ctx); break;
        case 1: break;   /* hangs; see above */
        case 2:          /* both reach the same stepper */
        case 3: at = SeqStepKind2(at, rec, ctx); break;
        case 4: at = SeqStepKind4(at, rec, ctx);   break;
        case 5: at = SeqStepKind5(at, rec, ctx);   break;
        case 6: at = SeqStepKind6(at, rec, ctx); break;
        case 7: at = SeqStepKind7(at, rec, ctx); break;
        default:
            at = *(const int16_t *)(rec + SEQ_OFF_NEXT);
            break;
        }
    }

    (void)recs;
}

/* SeqAddKind5 and SeqAddKind7 -- originals 0x00462000 and 0x00462080.
 *
 * Add one 48-byte SEQ record at a map point. Two of a family of three that
 * differ only in the kind they stamp, the sprite they take and their default
 * lifetime; 0x00461660 is the third, kind 6, and kind 7 calls it with a
 * random 0..2 on the way out.
 *
 * THEY WERE ADDR_BY_REF_ACTION_A AND _B, named off the one thing their call
 * sites showed -- a point passed by reference. That is a fact about the
 * argument list rather than about the function, which is the failure orig.h
 * keeps recording under "name a function from its body". The body says:
 * refuse a point off the map, allocate from ADDR_SEQ_CTX_A, stamp a kind,
 * open the walker's gate, and fill in the row it draws through.
 *
 * The two are written out rather than shared. They differ in five places --
 * kind, sprite array, default lifetime, the value at ROW_OFF_26, and whether
 * SEQ_OFF_FIELD_10 is zeroed -- and kind 7 has the extra call at the end, so
 * a common helper would take five parameters and a flag to save eight lines.
 * The original has them as two functions and so does this.
 *
 * THE POINT IS TESTED AGAINST THE MAP BOUNDS AND NOTHING ELSE IS TESTED. The
 * allocator's answer is used without a null check, which matters because it
 * can grow the pool and a failure there is not visibly handled anywhere in
 * this family. The original's, and reproduced.
 *
 * Both are on live paths -- kind 5 from an event action, kind 7 from the
 * flame cheat and from two other callers -- so a Boot Camp A/B compares them
 * whenever a mission fires one. What is NOT established is what a seq is for;
 * ADDR_SEQ_RUN's own comment says the same, and neither of these adds to it.
 */
void __cdecl SeqAddKind5(const int32_t *at, int32_t owner, int32_t life)
{
    uint8_t *seq;
    uint8_t *row;

    if (!PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT),
                     (const AM2_Point *)at))
        return;

    seq = (uint8_t *)SeqAlloc((void *)(uintptr_t)ADDR_SEQ_CTX_A);

    *(int32_t *)(seq + SEQ_OFF_OWNER)    = owner;
    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND5;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_10) = 0;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     = life > 0 ? life : AM2_SEQ_LIFE5;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    row = *(uint8_t **)(seq + SEQ_OFF_ROW);

    *(uint8_t *)(row + ROW_OFF_CELL)      = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54)  = 0;
    *(int32_t *)(row + 0)                 = 0;
    *(void **)(row + ROW_OFF_SPRITE)      =
        **(void ***)AM2_IMAGE(ADDR_SEQ_SPRITES_5);
    *(int32_t *)(row + ROW_OFF_X)         = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST)  = 0;
    *(int32_t *)(row + ROW_OFF_FIELD_2C)  = 0;
    *(int16_t *)(row + ROW_OFF_FIELD_26)  = AM2_SEQ_ROW26_5;
}

void __cdecl SeqAddKind7(const int32_t *at, int32_t owner, int32_t b,
                         int32_t c, int32_t life)
{
    uint8_t *seq;
    uint8_t *row;

    (void)b;
    (void)c;

    if (!PointInRect((const AM2_Rect *)AM2_IMAGE(ADDR_MAP_BOUNDS_LEFT),
                     (const AM2_Point *)at))
        return;

    seq = (uint8_t *)SeqAlloc((void *)(uintptr_t)ADDR_SEQ_CTX_A);

    *(int32_t *)(seq + SEQ_OFF_OWNER)    = owner;
    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND7;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = 0;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     = life > 0 ? life : AM2_SEQ_LIFE7;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) = 0;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    row = *(uint8_t **)(seq + SEQ_OFF_ROW);

    *(int32_t *)(row + 0)                 = 0;
    *(uint8_t *)(row + ROW_OFF_CELL)      = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54)  = 0;
    *(void **)(row + ROW_OFF_SPRITE)      =
        **(void ***)AM2_IMAGE(ADDR_SEQ_SPRITES_7);
    *(int32_t *)(row + ROW_OFF_X)         = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST)  = 0;
    *(int32_t *)(row + ROW_OFF_FIELD_2C)  = 0;
    *(int16_t *)(row + ROW_OFF_FIELD_26)  = AM2_SEQ_ROW26_7;

    SeqAddKind6(at, orig_seq_rand() % 3);
}

/* ListGrowFind -- original 0x00453AB0, thiscall with one argument.
 *
 * Grow a list of 260-byte records by one and then SEARCH it for a key at
 * +0x100, answering the index or -1. Two jobs in one function, which is
 * unusual enough to be worth the name saying both.
 *
 * THE SEARCH RUNS OVER THE NEW COUNT. The count is bumped BEFORE the realloc
 * and the loop bound is the bumped value, so the last record it compares is
 * the one just allocated and never written -- uninitialised memory, read as a
 * key. A match there would answer the index of a record the caller has not
 * filled in yet. Reproduced: it is the original's, it is what the caller's
 * -1 test is written against, and "fixing" it would change which index comes
 * back on a collision nobody has observed.
 *
 * The record size is written as `((n << 6) + n) << 2` -- n * 65 * 4 -- which
 * is the compiler's way of saying 260 and is why the stride shows up twice in
 * different forms. One name for it.
 */
int32_t __attribute__((thiscall)) ListGrowFind(void *self, int32_t key)
{
    uint8_t *s = (uint8_t *)self;
    int32_t  n = ++*(int32_t *)s;
    uint8_t *items;
    int32_t  i;

    items = (uint8_t *)am2_realloc(*(void **)(s + 4),
                                   (size_t)n * AM2_GROWLIST_STRIDE);
    *(void **)(s + 4) = items;

    n = *(const int32_t *)s;

    for (i = 0; i < n; i++)
        if (*(const int32_t *)(items + i * AM2_GROWLIST_STRIDE
                               + AM2_GROWLIST_KEY) == key)
            return i;

    return -1;
}

int misc_install(void)
{
    patch_replace(ADDR_AI_TAKE_ABANDONED, (const void *)AiTakeAbandoned,
                  "AiTakeAbandoned", 0);
    patch_replace(ADDR_SEQ_RUN_BOTH, (const void *)SeqRunBoth,
                  "SeqRunBoth", 0);
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
    patch_replace(ADDR_LOAD_MASK, (const void *)LoadMask,
                  "LoadMask", 2);
    patch_replace(ADDR_XOR_CHECKSUM, (const void *)XorChecksum, "XorChecksum", 1);
    patch_replace(ADDR_CHAIN_FIELD_14, (const void *)ChainField14,
                  "ChainField14", 1);
    patch_replace(ADDR_LIST_PUSH_FRONT, (const void *)ListPushFront,
                  "ListPushFront", 2);
    patch_replace(ADDR_TAKE_NUMBER_KEY, (const void *)TakeNumberKey,
                  "TakeNumberKey", 1);
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
                  "MapCode18To28", 2);
    patch_replace(ADDR_MEETS_ALL_THREE, (const void *)MeetsAllThree,
                  "MeetsAllThree", 1);
    patch_replace(ADDR_MISSION_NETWORKED, (const void *)MissionNetworked,
                  "MissionNetworked", 1);
    patch_replace(ADDR_COMM_SLOT_FOR_ARMY, (const void *)CommSlotForArmy,
                  "CommSlotForArmy", 20);
    patch_replace(ADDR_COMM_PLAYER_ID, (const void *)CommPlayerId,
                  "CommPlayerId", 1);
    patch_replace(ADDR_RANDOM_AROUND, (const void *)RandomAround,
                  "RandomAround", 1);
    patch_replace(ADDR_ALL_PLAYERS_AGREED, (const void *)CommAllPlayersAgreed,
                  "CommAllPlayersAgreed", 1);
    patch_replace(ADDR_ALL_PLAYERS_READY, (const void *)CommAllPlayersReady,
                  "CommAllPlayersReady", 1);
    patch_replace(ADDR_COMM_WAS_HERE_FOR_ARMY, (const void *)CommWasHereForArmy,
                  "CommWasHereForArmy", 1);
    patch_replace(ADDR_GET_ARMY_SCORE, (const void *)GetArmyScore,
                  "GetArmyScore", 3);
    patch_replace(ADDR_COMM_SLOT_HAS_PLAYER, (const void *)CommSlotHasPlayer,
                  "CommSlotHasPlayer", 5);
    patch_replace(ADDR_COMM_SET_ARMY_COLOUR, (const void *)CommSetArmyColour,
                  "CommSetArmyColour", 2);
    patch_replace(ADDR_BITMAP_BIT_SET, (const void *)BitmapBitSet,
                  "BitmapBitSet", 5);
    patch_replace(ADDR_LIST_UNLINK, (const void *)ListUnlink, "ListUnlink", 6);
    patch_replace(ADDR_REMAP_BYTES, (const void *)RemapBytes, "RemapBytes", 2);
    patch_replace(ADDR_PARSE_SPRITE_NAME, (const void *)ParseSpriteName,
                  "ParseSpriteName", 1);
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
    patch_replace(ADDR_ACTION_KEY_RELEASED, (const void *)ActionKeyReleased,
                  "ActionKeyReleased", 4);
    patch_replace(ADDR_DISMISS_KEY_RELEASED, (const void *)DismissKeyReleased,
                  "DismissKeyReleased", 4);
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
    patch_replace(ADDR_LIST_GROW_FIND, (const void *)ListGrowFind,
                  "ListGrowFind", 1);
    patch_replace(ADDR_SEQ_RUN, (const void *)SeqRun, "SeqRun", 2);
    patch_replace(ADDR_SEQ_STEP2, (const void *)SeqStepKind2,
                  "SeqStepKind2", 2);
    patch_replace(ADDR_SEQ_STEP5, (const void *)SeqStepKind5,
                  "SeqStepKind5", 1);
    patch_replace(ADDR_SEQ_STEP6, (const void *)SeqStepKind6,
                  "SeqStepKind6", 3);
    patch_replace(ADDR_SEQ_STEP7, (const void *)SeqStepKind7,
                  "SeqStepKind7", 3);
    patch_replace(ADDR_SEQ_STEP0, (const void *)SeqStepKind0,
                  "SeqStepKind0", 3);
    patch_replace(ADDR_SEQ_ADD_KIND4, (const void *)SeqAddKind4,
                  "SeqAddKind4", 1);
    patch_replace(ADDR_SEQ_STEP4, (const void *)SeqStepKind4,
                  "SeqStepKind4", 1);
    patch_replace(ADDR_SEQ_ADD_KIND6, (const void *)SeqAddKind6,
                  "SeqAddKind6", 2);
    patch_replace(ADDR_SEQ_ADD_KIND5, (const void *)SeqAddKind5,
                  "SeqAddKind5", 1);
    patch_replace(ADDR_SEQ_ADD_KIND7, (const void *)SeqAddKind7,
                  "SeqAddKind7", 3);
    patch_replace(ADDR_MOVIE_BUILD_NAME, (const void *)MovieBuildName,
                  "MovieBuildName", 4);
    return 0;
}

/* SeqStepKind6 -- original 0x00461560, one of the seven SeqRun dispatches to.
 * Kinds 2, 4 and 5 are already reconstructed; this is the fourth.
 *
 * ITS SPRITE CHOICE HAS TWO REGIMES AND THE SWITCH BETWEEN THEM IS THE LIFE.
 * More than 100 ms from the end it CYCLES -- ROW_OFF_ANIM_PHASE up by one per
 * emit, wrapping at `stride - tail` -- and inside the last 100 ms it RAMPS
 * instead, mapping the remaining time onto the tail frames so the thing runs
 * out rather than looping. The two arms share nothing but the base index.
 *
 * THE RAMP DIVIDES 100 BY THE TAIL COUNT AND THEN DIVIDES BY THAT, which is
 * two divisions where one would do -- `idx = (left * tail) / 100` is the same
 * function -- and it is written the original's way because the intermediate
 * is an integer and the rounding differs. `100 / tail` truncates first.
 *
 * THE CLAMP IS ON THE RAMP ONLY. `if (idx >= tail - 1) idx = tail - 1` caps
 * the last frame; the cycling arm has no clamp because its wrap already
 * bounds it. Sharing a clamp between them would change the cycle.
 *
 * BOTH ACCUMULATORS ADVANCE ON EVERY CALL, after the emit and outside its
 * gate -- ROW_OFF_STAMP_54 and SEQ_OFF_GATE each gain ADDR_FRAME_DELTA_MS.
 * So an emit that does not happen still ages the sequence, which is what
 * makes the life test terminate.
 *
 * ADDR_SEQ_SPRITES_7 is named for kind 7 and kind 6 reads the same array.
 * One array with two users is not two concepts; the name is left alone. */
int32_t __cdecl SeqStepKind6(int32_t at, void *rec, void *ctx)
{
    uint8_t *r    = (uint8_t *)rec;
    uint8_t *row  = *(uint8_t **)(r + SEQ_OFF_ROW);
    int32_t  life = *(const int32_t *)(r + SEQ_OFF_LIFE);
    int32_t  tail = *(const int32_t *)(uintptr_t)ADDR_SEQ_TAIL_FRAMES;
    int32_t  stride = *(const int32_t *)(uintptr_t)AM2_SEQ_VARIANT_STRIDE;

    (void)at;

    if (*(const uint32_t *)(row + ROW_OFF_STAMP_54) > (uint32_t)life)
        return SeqRetire(ctx, rec);

    if (*(const uint32_t *)(r + SEQ_OFF_GATE)
            > *(const uint32_t *)(uintptr_t)ADDR_SEQ_ADVANCE_MS) {
        int32_t variant = (int32_t)*(const uint8_t *)(r + SEQ_OFF_FLAG4)
                          * stride;

        if ((int32_t)*(const uint32_t *)(row + ROW_OFF_STAMP_54) > life - 100) {
            int32_t per = 100 / tail;
            int32_t idx = ((int32_t)*(const uint32_t *)(row + ROW_OFF_STAMP_54)
                           - life + 100) / per;

            if (idx >= tail - 1)
                idx = tail - 1;
            variant += stride - tail + idx;
        } else {
            uint8_t phase = (uint8_t)(*(const uint8_t *)(row + 0x51u) + 1);

            *(uint8_t *)(row + 0x51u) = phase;
            if ((int32_t)phase >= stride - tail)
                *(uint8_t *)(row + 0x51u) = 0;
            variant += *(const uint8_t *)(row + 0x51u);
        }

        *(uint32_t *)(row + ROW_OFF_SPRITE) =
            (*(uint32_t *const *)(uintptr_t)ADDR_SEQ_SPRITES_7)[variant];
        RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);

        *(int32_t *)(r + SEQ_OFF_GATE) -=
            *(const int32_t *)(uintptr_t)ADDR_SEQ_ADVANCE_MS;
    }

    *(uint32_t *)(row + ROW_OFF_STAMP_54) +=
        *(const uint32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;
    *(int32_t *)(r + SEQ_OFF_GATE) +=
        *(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;

    return *(const int16_t *)(r + SEQ_OFF_NEXT);
}


/* AllObjectsInRect is reconstructed, in win32/mapdraw.cpp, and is declared
 * here rather than by including that header: misc.cpp is on the FLAT side of
 * the split and mapdraw.h reaches win32.h. Neither parameter names a Win32
 * type, which is the same test item.cpp applies to the same function.
 *
 * It is on CLAUDE.md's "unexercised" list, which means it does not RUN here --
 * not that it is unwritten. I read the two as the same thing for a moment and
 * built a seam to the image; checkseams refused it. */
void *__cdecl AllObjectsInRect(const AM2_Rect *r, const void *desc);

/* SeqStepKind7 -- original 0x00461700, the fifth of the seven steppers. A
 * FLAME: every 300 ms it puffs a kind-6 sequence and burns everything inside
 * a box around its row.
 *
 * TWO DAMAGE ARMS, AND ONE OF THEM IS RATE-LIMITED. A type-2, -3 or -8 object
 * takes 1 every puff; anything else takes 15 but only once SEQ_OFF_FIELD_0C
 * has passed 9, and that counter is reset to 0 in the same breath. So troops
 * and vehicles burn steadily while scenery takes a single large hit every ten
 * puffs -- and the two arms differ in the AMOUNT as well as the gate, which a
 * shared call with a computed amount would blur.
 *
 * THE COUNTER IS TESTED TWICE against 9, once to choose the arm and once to
 * reset it, and the reset is OUTSIDE the object loop. Resetting inside would
 * give the first non-type-238 object the hit and spare the rest.
 *
 * THE RANDOM OFFSETS ARE SIGNED AND THE COMPILER SAYS SO. `and eax,
 * 0x8000000F; jns; dec; or 0xFFFFFFF0; inc` is MSVC's `% 16` on a value that
 * may be negative -- rand() here can be -- so the puff can move either way.
 * Reading it as `& 15` gives a flame that only ever drifts one direction.
 *
 * THE TWO AXES ARE NOT TREATED ALIKE: z loses a plain `rand % 16` while x
 * gains `((rand % 16) - 8) * variant`, so the horizontal spread scales with
 * which of the three variants was drawn and the vertical does not.
 *
 * The point is copied into a stack local first and the puff is spawned from
 * the COPY, so the row itself never moves -- the flame licks around a fixed
 * origin rather than wandering. */
int32_t __cdecl SeqStepKind7(int32_t at, void *rec, void *ctx)
{
    uint8_t *r   = (uint8_t *)rec;
    uint8_t *row = *(uint8_t **)(r + SEQ_OFF_ROW);

    (void)at;

    if (*(const uint32_t *)(row + ROW_OFF_STAMP_54)
            > *(const uint32_t *)(r + SEQ_OFF_LIFE))
        return SeqRetire(ctx, rec);

    if (*(const uint32_t *)(r + SEQ_OFF_GATE) > 0x12Cu) {
        int32_t pt = *(const int32_t *)(row + ROW_OFF_X);
        int32_t variant = orig_seq_rand() % 3;
        int32_t skip = 0;

        *((int16_t *)&pt + 1) =
            (int16_t)(*((const int16_t *)&pt + 1) - (orig_seq_rand() % 16));
        *(int16_t *)&pt =
            (int16_t)(*(const int16_t *)&pt
                      + ((orig_seq_rand() % 16) - 8) * variant);

        SeqAddKind6(&pt, variant);
        *(int32_t *)(r + SEQ_OFF_FIELD_0C) += 1;

        if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION != 0
            && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                    (int16_t)UidArmy(*(const uint32_t *)(r + SEQ_OFF_OWNER))))
            skip = 1;

        if (!skip) {
            AM2_Rect box;
            uint8_t *obj;

            box.left   = *(const int16_t *)(row + ROW_OFF_X) - 0x28;
            box.top    = *(const int16_t *)(row + ROW_OFF_X + 2) - 0x40;
            box.right  = *(const int16_t *)(row + ROW_OFF_X) + 0x28;
            box.bottom = *(const int16_t *)(row + ROW_OFF_X + 2) + 0x10;

            obj = (uint8_t *)AllObjectsInRect(
                      &box, (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC);
            while (obj != 0) {
                if (ObjIsTypeIn238((const AM2_Object *)obj))
                    DamageObject(obj, 1, 1,
                                 *(const uint32_t *)(r + SEQ_OFF_OWNER), 0, 0);
                else if (*(const int32_t *)(r + SEQ_OFF_FIELD_0C) > 9)
                    DamageObject(obj, 0x0F, 1,
                                 *(const uint32_t *)(r + SEQ_OFF_OWNER), 0, 0);
                obj = *(uint8_t **)(obj + 0x68u);
            }
        }

        *(int32_t *)(r + SEQ_OFF_GATE) -= 0x12C;
        if (*(const int32_t *)(r + SEQ_OFF_FIELD_0C) > 9)
            *(int32_t *)(r + SEQ_OFF_FIELD_0C) = 0;
    }

    *(uint32_t *)(row + ROW_OFF_STAMP_54) +=
        *(const uint32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;
    *(int32_t *)(r + SEQ_OFF_GATE) +=
        *(const int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;

    return *(const int16_t *)(r + SEQ_OFF_NEXT);
}

/* SeqStepKind0 -- original 0x00461150, the last of the seven steppers. A
 * BALLISTIC particle: it flies, falls under gravity, and either bounces or
 * settles when it reaches the ground.
 *
 * SEQ_OFF_LIFE IS NOT A LIFE HERE. Kind 0 reads +0x14 four ways -- as a gate
 * on the animation, as the vertical velocity added to ROW_OFF_HEIGHT, as the
 * impact test against -4, and as the thing gravity DECREMENTS each step. One
 * field, two units, chosen by the kind that owns the record, which is exactly
 * what SeqStepKind5's header already records for ROW_OFF_STAMP_54. There is
 * no reading of +0x14 that is right for both kinds.
 *
 * THE IMPACT TEST IS ON SPEED AND ITS SENSE IS EASY TO INVERT. `cmp eax, -4;
 * jge` sends a velocity of -4 OR HIGHER to the settle arm and only a faster
 * fall to the bounce -- so a hard landing bounces and a gentle one stops,
 * which is right and reads backwards from the comparison alone.
 *
 * THE BOUNCE IS `min(|v / 2| - 2, 7)` and the halving is an ARITHMETIC shift
 * before the absolute value, so a velocity of -5 gives |-3| - 2 = 1 and not
 * |-2| - 2 = 0. Written in that order; folding the abs inward changes where
 * the particle stops bouncing.
 *
 * IT RETIRES THREE DIFFERENT WAYS -- off the map, into an explosion on a
 * blocked tile, and settling into a decal row -- and only the last leaves
 * anything behind. The decal copies four fields off the flying row and forces
 * ROW_OFF_FIELD_26 to 2, which is what PlaceGroundDecal does too.
 *
 * AND IT SETTLES THE PARTICLE TABLE'S BASE. Its `lea ecx,[ecx+ecx*2]` then
 * `[ecx*8 + 0x0048C7E4]` is kind * 24 off 0x0048C7E4, four bytes BELOW the
 * base SpawnParticle appeared to use -- so that function's "field +0" is
 * really field +4, and the record is {count, sprites, dx, dz, ...}. The
 * comment there flagged this as undecided; a second toucher decided it. */
int32_t __cdecl SeqStepKind0(int32_t at, void *rec, void *ctx)
{
    uint8_t       *r    = (uint8_t *)rec;
    uint8_t       *row  = *(uint8_t **)(r + SEQ_OFF_ROW);
    const uint8_t *tab;
    uint32_t       pt;
    int32_t        tile;
    int32_t        ground;

    (void)at;

    *(uint32_t *)(row + ROW_OFF_STAMP_54) +=
        *(const uint32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS;

    if (*(const uint32_t *)(row + ROW_OFF_STAMP_54)
            <= *(const uint32_t *)(r + SEQ_OFF_GATE))
        goto done;

    tab = (const uint8_t *)(uintptr_t)ADDR_PARTICLE_KINDS
          + (uint32_t)*(const uint8_t *)(r + SEQ_OFF_FLAG4) * AM2_PARTICLE_KIND_BYTES;

    if (*(const int32_t *)(r + SEQ_OFF_LIFE) != 0) {
        uint8_t phase = (uint8_t)(*(const uint8_t *)(row + 0x51u) + 1);

        *(uint8_t *)(row + 0x51u) = phase;
        if ((int32_t)phase >= *(const int32_t *)tab)
            *(uint8_t *)(row + 0x51u) = 0;
        *(uint32_t *)(row + ROW_OFF_SPRITE) =
            (*(uint32_t *const *)(tab + 4))[*(const uint8_t *)(row + 0x51u)];
    }

    *(int16_t *)(row + ROW_OFF_X) +=
        (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_0C);
    *(int16_t *)(row + ROW_OFF_X + 2) +=
        (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_10);
    *(int16_t *)(row + 0x20u) +=
        (int16_t)*(const int32_t *)(r + SEQ_OFF_LIFE);

    /* The bounds test is on the DRAWN point -- the row's position with its
     * height subtracted from z -- not on the ground position. */
    pt = *(const uint32_t *)(row + ROW_OFF_X);
    *((int16_t *)&pt + 1) =
        (int16_t)(*((const int16_t *)&pt + 1) - *(const int16_t *)(row + 0x20u));
    if (!PointInRect((const AM2_Rect *)(uintptr_t)ADDR_MAP_BOUNDS_LEFT,
                     (const AM2_Point *)&pt))
        return SeqRetire(ctx, rec);

    tile = TileOfPoint(*(const uint32_t *)(row + ROW_OFF_X)) & 0xFFFF;
    ground = *(const int8_t *)(
        *(const uint8_t *const *)(uintptr_t)ADDR_TILE_ATTRS + tile);

    if (*(const int16_t *)(row + 0x20u)
            + *(const int32_t *)(r + 0x18u) > ground) {
        *(int32_t *)(r + SEQ_OFF_LIFE) -= 1;   /* gravity */
        *(uint32_t *)(row + ROW_OFF_STAMP_54) = 0;
        goto done;
    }

    *(int16_t *)(row + 0x20u) =
        (int16_t)(ground - *(const int16_t *)(r + 0x18u));

    if ((*(const uint8_t *const *)(uintptr_t)ADDR_TILE_FLAGS)[tile] & 1) {
        CreateExplosion(*(const int16_t *)(row + ROW_OFF_X),
                        *(const int16_t *)(row + ROW_OFF_X + 2),
                        0x93, 4, 0, 0, 0, 0, 0, 0);
        return SeqRetire(ctx, rec);
    }

    if (*(const int32_t *)(r + SEQ_OFF_LIFE) < -4) {
        int32_t v = *(const int32_t *)(r + SEQ_OFF_LIFE) >> 1;

        if (v < 0)
            v = -v;
        v -= 2;
        if (v >= 7)
            v = 7;
        *(int32_t *)(r + SEQ_OFF_LIFE) = v;
        *(uint32_t *)(row + ROW_OFF_STAMP_54) = 0;
        goto done;
    }

    {
        uint8_t *rest = *(uint8_t **)RowPoolAAlloc();

        *(int32_t *)rest = 1;
        *(uint32_t *)(rest + ROW_OFF_SPRITE) =
            *(const uint32_t *)(row + ROW_OFF_SPRITE);
        *(int16_t *)(rest + ROW_OFF_X) = *(const int16_t *)(row + ROW_OFF_X);
        *(int16_t *)(rest + ROW_OFF_X + 2) =
            *(const int16_t *)(row + ROW_OFF_X + 2);
        *(uint32_t *)(rest + ROW_OFF_FIELD_2C) =
            *(const uint32_t *)(row + ROW_OFF_FIELD_2C);
        *(int16_t *)(rest + ROW_OFF_FIELD_26) = 2;
        RowUpdate(rest, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    }
    return SeqRetire(ctx, rec);

done:
    RowUpdate(row, 0, (void *)(uintptr_t)ADDR_MAP_DESC);
    return *(const int16_t *)(r + SEQ_OFF_NEXT);
}
