/* misc.cpp -- see misc.h. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>   /* abs */
#include <string.h>

#include "misc.h"
#include "pmf.h"
#include "packkey.h"  /* PackKey -- reconstructed */
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

#ifdef AM2_STANDALONE
/* The -df debug-mode flag at 0x0047894C (ships as 1, so -df is OFF by default;
 * `-df` on the command line clears it). Read across winmain/frame/misc/sprite,
 * runtime-written, so non-const; initial value byte-checked. Plain linkage to
 * match standalone.h's shared declaration. */
uint32_t am2_opt_df = 1u;
/* The float/double CONSTANTS the reconstruction reads out of the carried
 * .rdata blob, transcribed into typed C (STATUS.md's MIGRATION note). Each is
 * verified to round-trip to the image's exact bit pattern -- the decimal
 * literals below reproduce the stored bytes byte-for-byte -- and every reader
 * is a `*(const double *)` or `*(const float *)` at a known address (the type
 * is the reader's, not the byte spacing: WEAPON_RANGE is double at 8-byte
 * spacing). Centralised here because a bare numeric constant has no natural
 * owning module the way the pose and air TABLES do. */
extern "C" const double am2_weapon_range_hi   = 1.1;         /* 0x0046F2E0 */
extern "C" const double am2_weapon_range_lo   = 0.9;         /* 0x0046F2E8 */
extern "C" const double am2_weapon_range_k3   = 1.2;         /* 0x0046F2F0 */
extern "C" const double am2_sight_range_want  = 0.75;        /* 0x0046F2F8 */
extern "C" const double am2_enemy_health_share= 0.33;        /* 0x0046FD50 */
extern "C" const double am2_dbl_zero          = 0.0;         /* 0x0046F920 */
extern "C" const double am2_dbl_max_period    = 1.0;         /* 0x0046F988 */
extern "C" const double am2_dbl_ms_per_sec    = 1000.0;      /* 0x0046F990 */
extern "C" const double am2_dbl_512           = 512.0;       /* 0x0046F9B0 */
extern "C" const double am2_dbl_sin_scale     = -0.85;       /* 0x0046F9B8 */
extern "C" const double am2_dbl_one_256       = 0.00390625;  /* 0x0046F9C0 */
extern "C" const double am2_dbl_two_pi        = 6.283185307; /* 0x0046F9C8 */
extern "C" const float  am2_f_one             = 1.0f;        /* 0x0046F2D8 */
extern "C" const float  am2_f_one_hundredth   = 0.01f;       /* 0x0046F2DC */
extern "C" const float  am2_float_zero        = 0.0f;        /* 0x0046F928 */
extern "C" const float  am2_hud_slide_shut    = -320.0f;     /* 0x0046F958 */
extern "C" const float  am2_hud_slide_open    = 320.0f;      /* 0x0046F95C */
extern "C" const float  am2_roach_reach       = 24.0f;       /* 0x0046FAA8 */
extern "C" const float  am2_ms_to_sec         = 0.001f;      /* 0x0046F980 */

/* Gameplay scalar parameters, transcribed from the blob. Types are the
 * readers' (int16/int32/float); centralised here for the same reason the
 * float constants above are -- a bare tunable has no owning-module the way a
 * table does. */
/* am2_pillbox_trooper_health (0x00473E44, value 55) is NOT a separate symbol:
 * the image aliases it onto rank record 4's max-health field, so it is a view
 * into am2_rank_records (item.cpp); see ADDR_PILLBOX_TROOPER_HEALTH in
 * standalone.h. */
/* The {handler, name} record at 0x0047588C: WriteDotString and a pointer to
 * "FOO", eight bytes up to ADDR_GAME_VERSION. Nothing references its base, so
 * its consumer is unidentified and it is dead; it is transcribed anyway so the
 * carried blob's copy of the handler slot needs no startup fixup -- the last
 * such slot outside a table. checkimagedata dereference-checks both fields.
 * Plain linkage, matching standalone.h's shared declaration (the rank-records
 * precedent): an extern "C" definition against that C++-linkage extern is a
 * conflicting-declaration error. */
const AM2_WriteDotRecord am2_write_dot_record[1] = {
    { (void *)WriteDotString, "FOO" },
};
extern "C" const float   am2_gravity                = 440.0f;/* 0x004852EC */
extern "C" const int32_t am2_view_speed             = 1000;  /* 0x004852E0 */
/* 0x00485104 tick_interval_ms is runtime state (ResetLevelState recomputes it),
 * so it is NOT migrated -- it stays at its writable .origdat placement. */
/* float[3], indexed by ADDR_DIFFICULTY: easy, normal, hard. Once a lone
 * float, which only ever read correctly at difficulty 0 -- anywhere else it
 * indexed whatever the linker put next (a VC6 link put a zero there, and every
 * friendly trooper spawned with 0 health). */
extern "C" const float   am2_difficulty_scale[3]    = { 4.0f, 2.0f, 1.5f };  /* 0x00489870 */
/* 0x0048788C path_max_nodes ships 10000 but ADAPTS at runtime (RegionBudget),
 * so it too stays at its writable .origdat placement rather than a C const. */
extern "C" const int32_t am2_path_max_searches      = 10;    /* 0x00487890 */
extern "C" const int32_t am2_path_retry_ms          = 500;   /* 0x00487894 */
extern "C" const int32_t am2_seq_grid_rows          = 3;     /* 0x0048CB88 */
extern "C" const int32_t am2_seq_tail_frames        = 2;     /* 0x0048CB90 */
extern "C" const int32_t am2_seq_advance_ms         = 40;    /* 0x0048CB94 */
extern "C" const int32_t am2_seq_emit_ms            = 300;   /* 0x0048CBE4 */
extern "C" const int32_t am2_seq_k4_step_ms         = 120;   /* 0x0048CBC4 */
extern "C" const int16_t am2_seq_k4_rise            = 3;     /* 0x0048CBE0 */
extern "C" const int32_t am2_seq_k4_drift_x         = 3;     /* 0x0048CBD8 */
extern "C" const int32_t am2_seq_k4_drift_y         = -1;    /* 0x0048CBDC */
extern "C" const uint32_t am2_seq_k4_hold[4] = { 2, 3, 4, 5 };/* 0x0048CBC8, per-cell hold */
#endif

#include "../inject/patch.h"

uint32_t __cdecl Field53C(const void *p)
{
    return *(const uint32_t *)((const uint8_t *)p + 0x53C);
}

int32_t __cdecl AddByteSat(uint8_t base, int32_t add)
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

void __cdecl CopyByteIfSet(const AM2_Object *unused, AM2_RoachOut *out,
                           const AM2_RoachCtx *c)
{
    (void)unused;
    if (!c->observer)
        return;
    out->heading = c->bearing;
}

int32_t __cdecl ScaleBy32Blocks(int32_t v)
{
    int32_t blocks = v / 32;

    return (blocks + 1) * 1000;
}

void __cdecl TitleCaseName(char *text)
{
    TitleCaseNameInline(text);
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

uint32_t __cdecl SwapColourBytes(uint32_t colour)
{
    const uint8_t *in = (const uint8_t *)&colour;
    uint32_t       out;
    uint8_t       *o = (uint8_t *)&out;

    o[3] = 0;
    o[0] = in[2];
    o[1] = in[1];
    o[2] = in[0];
    return out;
}

void __stdcall NullStub4(uint32_t arg) { (void)arg; }
void __cdecl   NullStub(int32_t a, int32_t b) { (void)a; (void)b; }
int32_t __cdecl ReturnZero(void)       { return 0; }
int32_t __cdecl ReturnOne(void *fp, void *obj) { (void)fp; (void)obj; return 1; }

int32_t __cdecl ReverseBlocks(void *dst, const void *src, int32_t total,
                              int32_t count)
{
    uint8_t       *d;
    const uint8_t *s;
    int32_t        n, i;

    /* The division comes BEFORE the count test in the original, so a count of
     * zero faults on the idiv rather than returning. Keeping that order means
     * count == 0 is undefined here as it is fatal there -- no vector can reach
     * it either way, since the emulator faults on the same instruction and the
     * vector is dropped. */
    n = total / count;
    s = (const uint8_t *)src + (int32_t)(count - 1) * n;

    if (count > 0) {
        d = (uint8_t *)dst;
        for (i = 0; i < count; i++) {
            memcpy(d, s, (size_t)n);
            d += n;
            s -= n;
        }
        return 1;
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

/* A switch, which VC6 compiled into the byte table at 0x00406988 selecting
 * one of eight arms through 0x00406968 -- so both tables are the compiler's
 * and not data to transcribe. The case order is the arms' order in the
 * image. */
int32_t __cdecl MapCode(int32_t code)
{
    int32_t r = 0;

    switch (code) {
    case 1: case 9:          r = 1; break;
    case 2: case 10:         r = 2; break;
    case 3:                  r = 5; break;
    case 6:                  r = 9; break;
    case 7: case 21:         r = 3; break;
    case 5: case 8: case 29: r = 6; break;
    case 4: case 30:         r = 7; break;
    }
    return r;
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
    /* Four cases, not three: 29 has an arm of its own in the jump table
     * (index 3) that lands on the same `return 0` as the default. The arms
     * are laid out 9, 10, 8, which is the order they are written in. */
    switch (a) {
    case 9:
        if (b != 9)
            return 1;
        break;
    case 10:
        if (b == 29 || b == 8)
            return 1;
        break;
    case 8:
        if (b == 29)
            return 1;
        break;
    case 29:
        return 0;
    }
    return 0;
}

/* Shared body, over either record: `FLAG` names the record's flag member,
 * `wide` is the mode written for the wide facings and `narrow` for the two
 * that use the other one. A macro rather than an inline over field
 * pointers: the original addresses each field off the record pointer, and
 * VC6 keeps that only when the body names the members.
 *
 * The read of src->facing belongs INSIDE the arms. Hoisting it is the
 * obvious tidy-up and it is wrong: for a facing above 7 the original never
 * touches src at all, so a null src is harmless there and fatal if the read
 * has been lifted. The vectors found it immediately -- a page fault reading
 * 0x40. The record is likewise written in each arm.
 *
 * 1 is NOT grouped with 3. The image sends facing 1 into 3's tail past the
 * flag store -- the compiler merging two identical endings -- so 1 mirrors
 * 7 and 3 mirrors 5. Folding 1 in with 3 writes one dword the original never
 * writes. */
#define AM2_SET_FACING(FLAG, wide, narrow)                                    \
    switch (facing) {                                                         \
    case 0:                                                                   \
        out->state   = (wide);                                                \
        out->heading = src->facing;                                           \
        break;                                                                \
    case 1:                                                                   \
        out->state   = (wide);                                                \
        out->heading = (uint8_t)(src->facing + 0x20);                         \
        break;                                                                \
    case 2:                                                                   \
        out->state   = (narrow);                                              \
        out->heading = (uint8_t)(src->facing + 0x20);                         \
        break;                                                                \
    case 3:                                                                   \
        out->FLAG    = 1;                                                     \
        out->state   = (wide);                                                \
        out->heading = (uint8_t)(src->facing + 0x20);                         \
        break;                                                                \
    case 4:                                                                   \
        out->FLAG    = 1;                                                     \
        out->state   = (wide);                                                \
        out->heading = src->facing;                                           \
        break;                                                                \
    case 5:                                                                   \
        out->FLAG    = 1;                                                     \
        out->state   = (wide);                                                \
        out->heading = (uint8_t)(src->facing - 0x20);                         \
        break;                                                                \
    case 6:                                                                   \
        out->state   = (narrow);                                              \
        out->heading = (uint8_t)(src->facing - 0x20);                         \
        break;                                                                \
    case 7:                                                                   \
        out->state   = (wide);                                                \
        out->heading = (uint8_t)(src->facing - 0x20);                         \
        break;                                                                \
    }

void __cdecl SetFacing14(int32_t facing, const AM2_Object *src, AM2_RoachOut *out)
{
    AM2_SET_FACING(flag18, 2, 1)
}

void __cdecl SetFacing08(int32_t facing, const AM2_Object *src, AM2_SteerOut *out)
{
    AM2_SET_FACING(field0C, 4, 1)
}
#undef AM2_SET_FACING

int32_t __cdecl IsKind10To17(int32_t kind)
{
    return (kind >= 10 && kind <= 17) ? 1 : 0;
}

int32_t __cdecl IsKind14Or22(int32_t kind)
{
    return (kind == 14 || kind == 22) ? 1 : 0;
}

/* A switch over codes 7..70 with three arms -- 1, 2, and 0 for the rest --
 * which VC6 compiles to the byte index table at 0x0040D820 and the jump
 * table at 0x0040D814. Written as the switch it is. */
int32_t __cdecl ClassifyByCode74(const void *obj)
{
    const uint8_t *sub = *(const uint8_t *const *)((const uint8_t *)obj + 0x74);

    switch (*(const int16_t *)(sub + 0x4C)) {     /* movsx: signed */
    case 7: case 10: case 12: case 14: case 19: case 42: case 43: case 45:
    case 49: case 50: case 51: case 70:
        return 1;
    case 8: case 11: case 13: case 15: case 20: case 52: case 53: case 54:
    case 59: case 68:
        return 2;
    }
    return 0;
}

/* 0x0045EE20. Accepts 2-6, 11-12, 21-26, 30, 35-42. Biased by 2 over a
 * 41-entry table; outside 2..42 the answer is no. */
int32_t __cdecl KindInSetA(int32_t kind)
{
    switch (kind) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 11:
    case 12:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 30:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
        return 1;
    }
    return 0;
}

/* 0x00433520. Accepts 1, 7-10 and 29. Biased by 1 over a 29-entry table. */
int32_t __cdecl KindInSetB(int32_t kind)
{
    switch (kind) {
    case 1:
    case 7:
    case 8:
    case 9:
    case 10:
    case 29:
        return 1;
    }
    return 0;
}

/* The one the loose arm still needs: MSVC's _findfirst. The packed half was
 * here too until it was reconstructed. */
typedef int32_t (__cdecl *AM2_FindFirstFn)(const char *pattern, void *data);
#define orig_findfirst  ((AM2_FindFirstFn)((void *)(uintptr_t)(ADDR_CRT_FINDFIRST)))
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
 * THE TWO HALVES ARE NOT SYMMETRICAL AND THAT IS THE THING TO KNOW. With
 * ADDR_OPT_DF SET -- which is the default: it ships as 1 and `-df` CLEARS
 * it, as its own note in orig.h says -- it is a plain forward to the packed
 * loader with the arguments unchanged. Cleared, the packed loader is never
 * reached AND a set below AM2_SPRITE_SET_MAP_FIRST returns having done
 * nothing at all -- so under `-df` the fixed sets have no masks, where under
 * the pack they do. That is the original's, not a transcription: the
 * `< 0x14` test guards only the loose arm, because only the map's own sets
 * have a directory to glob in.
 *
 * THE BRANCH SENSE WAS INVERTED HERE FOR MONTHS, and no A/B could see it.
 * `test eax, eax; je <loose>` at 0x0043528B: ZERO takes the loose arm,
 * non-zero the packed one, and this read it the other way -- so with the
 * flag at its shipped 1 every mask load globbed for a `.msk` that does not
 * exist, found nothing, and left the record empty. Not one of the 1,538
 * items on Boot Camp had a hit mask; the original gives 1,356 of them one,
 * 60x60 bits for the common scenery, and stamps the terrain cover through
 * it. Both sides of an injected A/B run this reconstruction, so both lost
 * the masks alike; it was the native build's cell-weight plane, diffed
 * against the ORIGINAL's under AM2_NOPATCH, that differed in 9,380 cells
 * and led here, through the objects' +0x78 being null.
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
/* Forward-declared rather than included: both live in win32/sprite.h and this
 * module is on the flat side of the split, so the include would drag
 * AM2_Sprite's LPDIRECTDRAWSURFACE across it and fail checksplit. The same
 * reason script.cpp forward-declares PreloadSprite.
 *
 * C++ linkage, matching sprite.h (which has no extern "C" block). */
void *__cdecl SpriteSetForKey(uint32_t key);
int32_t __cdecl SpriteDirIndex(void *set, uint32_t key);
#define orig_fread_m (*(am2_fread_fn)((void *)(uintptr_t)(ADDR_FREAD)))
#define orig_fseek_m (*(am2_fseek_fn)((void *)(uintptr_t)(ADDR_FSEEK)))
#define orig_log_m   (*(am2_log_fn)((void *)(uintptr_t)(ADDR_LOG)))

/* LoadMaskPacked -- original 0x00424590, 496 bytes, one caller: LoadMask's
 * non-`-df` branch, which is the DEFAULT, so this is the arm that actually
 * runs and the loose-file one is the exception.
 *
 * PackKey the three arguments, find the sprite set that owns the key, find
 * the key's entry in that set's directory, seek to it and read the record.
 *
 * THREE OF THE SEVEN READS ARE DISCARDED AND ALL SEVEN MUST HAPPEN. The
 * original reads four bytes at a time and consumes each immediately -- the
 * value comes back at the same displacement plus 0x10, because the four
 * pushes are still live when it is read -- and three of them are simply
 * dropped. They are not dead: `fread` advances the file position, so
 * skipping one would misalign every read after it. Reproduced as reads into
 * a scratch rather than as seeks, which is what the original does.
 *
 * The two words it keeps go into MASKREC_OFF_DESC4 and
 * MASKREC_OFF_DESC_BLOCKS. Those names were given from the LOOSE arm, which
 * takes them from a DIB descriptor, and orig.h says so explicitly -- "only
 * the loose arm is read here, so the two words are named for WHERE THEY COME
 * FROM rather than for what they mean". This arm reads the same two fields
 * out of the archive, so the pairing is now confirmed from both sides even
 * though what they MEAN is still open.
 *
 * Both failure paths -- a bad seek and a key that does not match -- free
 * MASKREC_OFF_BITS if it is set, null it, and answer 0. The field is zeroed
 * on entry, so on those paths the free is of whatever a previous call left,
 * not of anything this one allocated.
 *
 * A zero or negative size skips the malloc entirely and still answers 1. */
int32_t __cdecl LoadMaskPacked(void *out, int32_t set, int32_t index,
                               int32_t frame)
{
    uint8_t *rec = (uint8_t *)out;
    uint8_t *ss;
    uint32_t key;
    uint32_t got;
    int32_t  dir;
    int32_t  scratch;
    int32_t  junk;

    *(void **)(rec + MASKREC_OFF_BITS) = (void *)0;

    key = (uint32_t)PackKey(set, index, frame);
    ss  = (uint8_t *)SpriteSetForKey(key);
    if (!ss)
        return 0;

    dir = SpriteDirIndex(ss, key);
    if (dir < 0)
        return 0;

    /* The file is re-read from the sprite set at every call. */
#define fp (*(am2_FILE **)(ss + SPRITE_SET_OFF_FILE))

    if (orig_fseek_m(fp, ((const int32_t *)(*(void **)(ss + SPRITE_SET_OFF_DIR)))
                       [dir * 2 + 1], 0) != 0) {
        orig_log_m("Error seeking to location %d in data file.\n",
                 ((const int32_t *)(*(void **)(ss + SPRITE_SET_OFF_DIR)))
                     [dir * 2 + 1]);
        goto fail;
    }

    orig_fread_m(&got, 4, 1, fp);
    if (key != got) {
        orig_log_m("Error in validating object in data file.\n");
        goto fail;
    }

    orig_fread_m(&scratch, 4, 1, fp);
    *(uint16_t *)(rec + MASKREC_OFF_DESC4) = (uint16_t)scratch;
    orig_fread_m(&scratch, 4, 1, fp);
    *(uint16_t *)(rec + MASKREC_OFF_DESC_BLOCKS) = (uint16_t)scratch;

    orig_fread_m(&scratch, 4, 1, fp);   /* three reads whose values are dropped */
    orig_fread_m(&junk, 4, 1, fp);      /* and whose SIDE EFFECT on the file    */
    orig_fread_m(&junk, 4, 1, fp);      /* position is the point of them        */

    /* The same variable carries the two skips and the size after them. */
    orig_fread_m(&scratch, 4, 1, fp);
    orig_fseek_m(fp, scratch, 1);
    orig_fread_m(&scratch, 4, 1, fp);
    orig_fseek_m(fp, scratch, 1);

    orig_fread_m(&scratch, 4, 1, fp);
    if (scratch > 0) {
        void *bits = am2_malloc((size_t)scratch);
        *(void **)(rec + MASKREC_OFF_BITS) = bits;
        orig_fread_m(bits, (size_t)scratch, 1, fp);
    }
    return 1;

fail:
    if (*(void **)(rec + MASKREC_OFF_BITS))
        am2_free(*(void **)(rec + MASKREC_OFF_BITS));
    *(void **)(rec + MASKREC_OFF_BITS) = (void *)0;
    return 0;
#undef fp
}

void __cdecl LoadMask(void *out, int32_t set, int32_t index, int32_t frame)
{
    uint8_t *rec = (uint8_t *)out;
    char     dir[AM2_MASK_PATH_MAX];
    char     pattern[AM2_MASK_PATH_MAX];
    uint8_t  find[AM2_FINDDATA_BYTES];
    uint8_t  desc[AM2_DIB_DESC_BYTES];

    if (g_optDf) {
        LoadMaskPacked(out, set, index, frame);
        return;
    }

    if (set < AM2_SPRITE_SET_MAP_FIRST)
        return;

    am2_sprintf(dir, "%s\\%s\\masks",
                g_mapBlock,
                (g_spriteSetDirs)[set]);
    SetGameDir(dir);

    am2_sprintf(pattern, "%02d_%03d_%02d_*.msk",
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

/* 0x0041CF20 and 0x0041CEC0 differ only in the row table's entry width: one
 * reads a word at m+4+y*2 and the other a dword at m+4+y*4.  Two functions in
 * the image, so two here. */
int32_t __cdecl MaskPixelSolid(uint16_t x, uint16_t y, const void *mask)
{
    const uint8_t *m = (const uint8_t *)mask;
    uint16_t       acc;
    const uint8_t *row;
    uint8_t        run, skip;

    if (x > *(const uint16_t *)m || y > *(const uint16_t *)(m + 2))
        return 0;
    row = m + *(const uint16_t *)(m + 4 + (uint32_t)y * 2);
    acc = 0;
    while (1) {
        /* A transparent run that carries acc past x is a miss. */
        skip = *row++;
        acc += skip;
        if (acc > x)
            return 0;
        run = *row;
        acc += run;
        if (acc >= x)
            return 1;               /* x fell inside a solid run */

        /* Past the run length byte and the run's own pixels. */
        row += run + 1;
    }
}

int32_t __cdecl MaskPixelSolid32(uint16_t x, uint16_t y, const void *mask)
{
    const uint8_t *m = (const uint8_t *)mask;
    uint16_t       acc;
    const uint8_t *row;
    uint8_t        run, skip;

    if (x > *(const uint16_t *)m || y > *(const uint16_t *)(m + 2))
        return 0;
    row = m + *(const uint32_t *)(m + 4 + (uint32_t)y * 4);
    acc = 0;
    while (1) {
        /* A transparent run that carries acc past x is a miss. */
        skip = *row++;
        acc += skip;
        if (acc > x)
            return 0;
        run = *row;
        acc += run;
        if (acc >= x)
            return 1;               /* x fell inside a solid run */

        /* Past the run length byte and the run's own pixels. */
        row += run + 1;
    }
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
    int32_t        bit   = 1 << (uint8_t)(7 - (x % 8));
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
    /* The bit for each column within its byte, built on the stack. */
    uint8_t        bit[8] = { 0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1 };
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *m;
    int32_t        x, y, w;

    if (o) {
        m = *(const uint8_t *const *)(o + OBJ_OFF_HIT_MASK);
        if (m) {
            x = *(const int16_t *)(m + OBJMASK_OFF_ORIGIN_X)
                - ((const AM2_Rect *)(o + OBJ_OFF_HIT_RECT))->left + at->x;
            y = *(const int16_t *)(m + OBJMASK_OFF_ORIGIN_Y)
                - ((const AM2_Rect *)(o + OBJ_OFF_HIT_RECT))->top + at->y;

            /* Each bound is loaded only once its coordinate has passed the
             * zero test. */
            if (x >= 0 && x < (w = *(const int16_t *)(m + OBJMASK_OFF_WIDTH))
                && y >= 0 && y < *(const int16_t *)(m + OBJMASK_OFF_HEIGHT))
                return (*(const uint8_t *const *)(m + OBJMASK_OFF_BITS))
                           [(((w + 31) >> 3) & ~3) * y + (x >> 3)]
                       & bit[x & 7];
        }
    }
    return 0;
}


uint32_t __cdecl XorChecksum(const void *record)
{
    const uint32_t *d = (const uint32_t *)record;
    int32_t         n = (int32_t)(d[1] >> 2);
    uint32_t        acc = 0;

    if (n > 0) {
        do {
            acc ^= *d++;
        } while (--n);
    }
    return acc;
}

uint32_t __cdecl ChainField14(const void *p)
{
    const uint8_t *q = *(const uint8_t *const *)((const uint8_t *)p + 4);

    if (!q)
        return 0;
    return *(const uint32_t *)(q + 0x14);
}

void __cdecl ListPushFront(AM2_CellEntry *node, AM2_CellEntry **head)
{
    node->prev = 0;                     /* zeroed before *head is read,
                                         * the order the original emits */
    AM2_CellEntry *old = *head;
    node->next = old;
    if (old)
        old->prev = node;
    *head = node;
}

/* The inverse. Both arms end by clearing the node's own links, and only the
 * head arm writes through `head`. */
void __cdecl ListUnlink(AM2_CellEntry *node, AM2_CellEntry **head)
{
    /* Each link is read again at each use. Each arm clears the node's own
     * links and returns. */
    if (!node->prev) {
        *head = node->next;
        if (node->next)
            node->next->prev = 0;
        node->next = 0;
        node->prev = 0;
        return;
    }
    node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    node->next = 0;
    node->prev = 0;
}

/* The remap loop walks dst itself, so the answer is dst + count on the table
 * path and plain dst on the other two. Both callers discard it and recompute
 * `dst += count` themselves.
 *
 * The copy is VC6's intrinsic memcpy -- `rep movsd` then `rep movsb`, which
 * copies FORWARD. dst and src are only known to be different, not disjoint,
 * and memcpy is undefined on overlap elsewhere, so GCC gets the same forward
 * copy spelled as a dword loop and a byte loop. */
uint8_t *__cdecl RemapBytes(uint8_t *dst, const uint8_t *src,
                            const uint8_t *table, int32_t count)
{
    if (table) {
        if (count > 0) {
            do {
                *dst++ = table[*src++];
            } while (--count);
        }
    } else if (dst != src) {
#ifdef _MSC_VER
        memcpy(dst, src, count);
#else
        uint8_t       *d     = dst;
        const uint8_t *s     = src;
        uint32_t       words = (uint32_t)count >> 2;
        uint32_t       bytes = (uint32_t)count & 3;

        while (words--) {
            *(uint32_t *)d = *(const uint32_t *)s;
            d += 4;
            s += 4;
        }
        while (bytes--)
            *d++ = *s++;
#endif
    }
    return dst;
}

int32_t __cdecl SetFieldInAll(void *record, void *value)
{
    uint8_t *r = (uint8_t *)record;
    int32_t  i;

    /* The rows pointer is re-read every iteration, as the original does. */
    for (i = 0; i < *(const int32_t *)(r + 4); i++)
        *(void **)(*(uint8_t **)(r + 8) + i * 0x60 + 0x2C) = value;

    return i;
}

int32_t __cdecl Field51MeetsMin(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    const uint8_t *rec = *(const uint8_t *const *)(b + 0x44);

    /* zero-extended byte, signed 16-bit compare */
    if (rec && (int16_t)(uint16_t)b[0x51] >= *(const int16_t *)(rec + 6))
        return 1;
    return 0;
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
    const uint8_t *o    = (const uint8_t *)obj;
    int32_t        cur  = *(const int32_t *)(o + AM2_OBJ_KIND538);
    int32_t        over = *(const int32_t *)(o + AM2_OBJ_KIND53C);
    int32_t        alt  = over;
    int32_t        skip;
    const uint8_t *seq;

    if (cur == want)
        return cur;

    skip = (cur == 1) ? 1 : 0;
    /* A switch, which VC6 compiled into the byte table at 0x0040D8FC and the
     * three-entry jump table before it; the override arm is written first,
     * which is where it sits in the image. */
    switch (want) {
    case 0x05: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
        alt = want;
        break;
    case 0x00: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
        skip = 1;
        break;
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
        ((uint32_t *)((uint8_t *)out + AM2_RGB332_PACKED))[i] = ((uint32_t)quad[i * 4 + 2] << 16)
                  | ((uint32_t)quad[i * 4 + 1] << 8)
                  |  (uint32_t)quad[i * 4 + 0];
    }
}

void __cdecl CollapseEqualDeltas(uint16_t *values, int32_t *count)
{
    int32_t   n    = *count;
    int32_t   kept = 0;
    uint16_t *wr;
    int32_t   i    = 1;

    /* No early exit: a count of one or less falls through to the same
     * store, with kept still zero. */
    if (n > 1) {
        wr = values;
        do {
            int32_t delta, j;
            /* Read and advanced together, so the first pass reads values[0]
             * and writes values[1]. As one statement, VC6 walks the run with
             * a pointer as the image does. */
            int32_t prev = *wr++;

            delta = (int32_t)values[i] - prev;
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
    }

    *count = kept + 1;
}

/* Both arms written out, as the original has them: `wide` is the only
 * difference -- dword row offsets or word ones -- exactly as it is between
 * MaskPixelSolid and MaskPixelSolid32.
 *
 * A width of zero skips a row entirely rather than walking it once: the run
 * walk is a `while`, tested before the first pair. The run length is a byte
 * compared signed after masking, so it is `run != 0`; the original keeps it in
 * the dead `table` argument slot. */
void __cdecl RemapRleRuns(void *rle, void *unused, int32_t wide,
                          const void *table)
{
    uint8_t  *base   = (uint8_t *)rle;
    uint16_t  width  = *(const uint16_t *)base;
    uint16_t  height = *(const uint16_t *)(base + 2);
    uint8_t  *row;
    uint8_t  *end;
    uint8_t  *p;
    uint16_t  acc;
    const uint8_t *map = (const uint8_t *)table;
    uint8_t   run;
    int32_t   i;

    (void)unused;
    if (wide == 1) {
        for (row = base + 4, end = base + 4 + (uint32_t)height * 4;
             row < end; row += 4) {
            p   = base + *(const uint32_t *)row;
            acc = 0;
            while (acc < width) {
                run = *p++;
                acc = (uint16_t)(acc + run);
                run = *p++;
                acc = (uint16_t)(acc + run);
                for (i = 0; i < run; i++) {
                    *p = map[*p];
                    p++;
                }
            }
        }
    } else {
        for (row = base + 4, end = base + 4 + (uint32_t)height * 2;
             row < end; row += 2) {
            p   = base + *(const uint16_t *)row;
            acc = 0;
            while (acc < width) {
                run = *p++;
                acc = (uint16_t)(acc + run);
                run = *p++;
                acc = (uint16_t)(acc + run);
                for (i = 0; i < run; i++) {
                    *p = map[*p];
                    p++;
                }
            }
        }
    }
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

void __cdecl ConsumePendingByte(AM2_Object *o, AM2_RoachOut *out,
                                const AM2_RoachCtx *c)
{
    if (!o->hitDir)
        return;

    if (c->seed > 0x40)                       /* unsigned */
        out->state = 3;

    if (c->observer == 0)
        out->heading = o->hitDir;             /* re-read, as the original */

    o->hitDir = 0;
}

/* Inline, and in the original's branch order. VC6 materialises each ternary
 * with the set-instruction of its condition's INVERSE, so the two written
 * `<=`/`>=` forms are what give the image's setg/setl. */
static inline int32_t FacingFromDelta(const int32_t *mode, const int32_t *flag,
                                      int32_t delta)
{
    if (*mode != 1) {
        if (*flag != 0) {
            if (delta >= 0x10)
                return 5;
            return (delta > -0x10) ? 4 : 3;
        }
        if (delta >= 0x10)
            return 1;
        return (delta <= -0x10) ? 7 : 0;
    }
    return (delta >= 0) ? 2 : 7;
}

int32_t __cdecl FacingFromDelta08(const AM2_SteerOut *rec, int32_t delta)
{
    return FacingFromDelta(&rec->state, &rec->field0C, delta);
}

int32_t __cdecl FacingFromDelta14(const AM2_RoachOut *rec, int32_t delta)
{
    return FacingFromDelta(&rec->state, &rec->flag18, delta);
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
    int32_t result = 0;

    switch (code) {
    case 0x18: result = 8; break;
    case 0x19: result = 2; break;
    case 0x1a: result = 1; break;
    case 0x27: result = 4; break;
    case 0x28: result = 6; break;
    }
    return result;
}

/* The index table at 0x00449F24 selects between two arms, and its entries are
 * 0 and 1 -- the same values the arms return -- so the table IS the answer.
 * Read out of the image along with the two-entry jump table at 0x00449F1C. */
int32_t __cdecl ObjCodeUnmapped(const void *obj)
{
    const uint8_t  *o    = (const uint8_t *)obj;
    const uint32_t *link = *(const uint32_t *const *)(o + 0xC0);

    switch (*link) {
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x27:
    case 0x28:
        return 0;
    }
    return 1;
}

int32_t __cdecl MeetsAllThree(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;

    if (!(b[8] & 4)
        && *(const int32_t *)b == 1
        && **(const int32_t *const *)(b + 0x94) == 0x1F)
        return 1;
    return 0;
}

int32_t __attribute__((thiscall)) AM2_Comm::CommSlotForArmy(int32_t army)
{
    void *comm = this;

    if (army == 4)
        return 4;

    const uint8_t *p;
    uint32_t       i;                   /* unsigned: the loop test is `jb` */

    /* i before p, and i++ before p's step: the original's order. */
    for (i = 0, p = (const uint8_t *)comm + AM2_PLAYER_ARMY;
         i < AM2_PLAYERS_MAX; i++, p += AM2_PLAYER_STRIDE) {
        if (*(const int32_t *)p == army)
            return (int32_t)i;
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
int32_t __attribute__((thiscall)) AM2_Comm::GetArmyScore(int32_t slot)
{
    void *comm = this;

    int32_t idx = this->CommArmyOfSlot(slot);
    int32_t out;

    (void)comm;
    out = 0;
    GetVarValue((&g_svarGreenscore)[idx], &out);
    return out;
}

/* ApplyGameSettings -- original 0x0042F170, 258 bytes, six callers. Copy the
 * saved/host settings into the live ones and reset every comm slot.
 *
 * It is the counterpart of ADDR_RESET_HOST_STATE, which sits directly above
 * it at 0x0042F140 and EMPTIES the same two names. One writes the staging
 * block, the other applies it.
 *
 * The pairing is the whole function and it settles a name: HOST_VALUE_3E8
 * lands in ADDR_SCORE_LIMIT, so the 1000 that RESET_HOST_STATE writes is the
 * default SCORE LIMIT rather than an unexplained constant. MASK_A becomes the
 * game-over flags and MASK_B GAME_SETTING_22C.
 *
 * Every slot is reset with its own INDEX, not with zero -- the original
 * carries a second counter in edx purely for that -- while the team beside it
 * is zeroed. So applying settings un-teams everybody and renumbers the slots.
 *
 * The four army point pools are seeded with 1000 each, and the loop bound is
 * the distance to ADDR_SCORE_LIMIT, the next global: 0x00515FE0 to
 * 0x00515FF0 is four. Written that way, as the other distance-bounded loops
 * in this tree are.
 *
 * The tail is conditional on the name table being non-empty and copies two
 * more strings out of the record it points at. The original inlines all four
 * strcpys as repne scasb plus rep movsd -- MSVC's /Oi -- which is why the
 * disassembly is mostly string primitives and no calls. */
void __cdecl ApplyGameSettings(void)
{
    /* The comm object is read at every use. */
#define comm ((uint8_t *)(g_commObject))
    int32_t *pool = g_armyPoints;
    int32_t  i;

    strcpy(g_livePlayerName,
               &g_savedPlayerName);
    strcpy(g_liveBattleName,
               &g_savedBattleName);

    /* In the order that schedules into the image's stores. */
    g_scoreLimit =
        g_hostValue3e8;
    g_gameOverFlags =
        g_hostMaskA;
    *(uint32_t *)&g_gameSetting22c =
        g_hostMaskB;

    for (i = 0; i < AM2_COMM_SLOTS; i++) {
        pool[i] = AM2_DEFAULT_SCORE_LIMIT;
        *(int32_t *)(comm + COMM_OFF_PLAYERS
                     + (size_t)i * AM2_COMM_SLOT_STRIDE
                     + COMM_SLOT_OFF_INDEX) = i;
        *(int32_t *)(comm + COMM_OFF_PLAYERS
                     + (size_t)i * AM2_COMM_SLOT_STRIDE
                     + COMM_SLOT_OFF_TEAM) = 0;
    }

    if (g_nameTableCount != 0) {
        const uint8_t *rec = g_nameTableBase;

        strcpy(&g_mpScriptName, (const char *)rec);
        strcpy(&g_mapName_,
                   *(const char *const *)(rec + NAMEREC_OFF_MAPS));
    } else {
        /* THE ELSE ARM WAS MISSING. `mov byte [0x511c08], bl` and
         * `mov byte [0x511a88], bl` at 0x0042F273 and 0x0042F279 -- with the
         * table empty the original EMPTIES both names rather than leaving
         * them, and `bl` is that function's zero register.
         *
         * Without it our two buffers went out of step: ADDR_MP_SCRIPT_NAME
         * kept a stale "death.txt" while ADDR_MAP_NAME was empty, and the
         * multiplayer panel looks its map list up by the first of them. */
        g_mpScriptName = '\0';
        g_mapName_       = '\0';
    }
#undef comm
}

/* CommTeamScore -- original 0x0040F990, thiscall. What the `teamscore`
 * keyword answers: EvalOperand's kind 8 returns whatever this does.
 *
 * A slot with NO TEAM scores alone. Team 0 means unteamed rather than "team
 * number zero", so the whole sum is skipped and the slot's own score is
 * returned -- one call, not a loop that happens to match once. Otherwise every
 * TAKEN slot sharing that team is added, the asking slot included, because it
 * shares its own team by definition.
 *
 * Both tests matter and neither subsumes the other: an untaken slot can still
 * carry a stale team, and a taken slot on a different team is skipped. The
 * original reads TAKEN first and only then compares the team.
 *
 * The addressing is worth writing out because it decomposes exactly. The
 * original computes the slot base as `eax*8 - eax` shifted left four, which is
 * eax * 0x70 -- AM2_COMM_SLOT_STRIDE -- and reaches the team at a literal
 * 0x258. That is COMM_OFF_PLAYERS (0x20C) plus COMM_SLOT_OFF_TEAM (0x4C), and
 * the `+4` beside it is COMM_SLOT_OFF_TAKEN (0x50). Nothing here needed a new
 * name; the literal confirms the existing layout rather than competing with
 * it, which is the useful direction for a function this small. */
int32_t __attribute__((thiscall)) AM2_Comm::CommTeamScore(int32_t slot)
{
    void *comm = this;

    const uint8_t *players = (const uint8_t *)comm + COMM_OFF_PLAYERS;
    int32_t        team;
    int32_t        total;
    int32_t        i;

    team = *(const int32_t *)(players + (size_t)slot * AM2_COMM_SLOT_STRIDE
                              + COMM_SLOT_OFF_TEAM);
    total = 0;
    if (team == 0)
        return ::GetArmyScore(comm, slot);

    for (i = 0; i < AM2_COMM_SLOTS; i++) {
        const uint8_t *rec = players + (size_t)i * AM2_COMM_SLOT_STRIDE;
        if (*(const int32_t *)(rec + COMM_SLOT_OFF_TAKEN) != 0
            && *(const int32_t *)(rec + COMM_SLOT_OFF_TEAM) == team)
            total += ::GetArmyScore(comm, i);
    }
    return total;
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
int32_t __attribute__((thiscall)) AM2_Comm::CommWasHereForArmy(int32_t army)
{
    void *comm = this;

    const uint8_t *p;
    int32_t        i;

    /* i before p, and i++ before p's step: the original's order. */
    for (i = 0, p = (const uint8_t *)comm + COMM_ARMY_OFF_COLOUR;
         i < AM2_PLAYERS_MAX; i++, p += AM2_PLAYER_STRIDE) {
        if (*(const int32_t *)p == army)
            return *(const int32_t *)((const uint8_t *)comm
                                      + (uint32_t)i * AM2_PLAYER_STRIDE
                                      + COMM_ARMY_OFF_WAS_HERE);
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
int32_t __attribute__((thiscall)) AM2_Comm::CommSetArmyColour(int32_t slot, int32_t colour)
{
    int32_t held;

    if (colour < 0)
        return -1;

    held = this->CommSlotForArmy(colour);

    if (*(const int32_t *)((const uint8_t *)AM2_THE_COMM + COMM_OFF_IS_HOST)
        && (uint32_t)slot < (uint32_t)held) {
        int32_t *mine = (int32_t *)((uint8_t *)this
                                    + (uint32_t)slot * AM2_PLAYER_STRIDE
                                    + AM2_PLAYER_ARMY);
        int32_t  was  = *mine;

        *mine = colour;
        *(int32_t *)((uint8_t *)this + (uint32_t)held * AM2_PLAYER_STRIDE
                     + AM2_PLAYER_ARMY) = was;
    }
    return slot;
}

int32_t __attribute__((thiscall)) AM2_Comm::CommSlotHasPlayer(int32_t slot)
{
    void *comm = this;

    int32_t id = *(const int32_t *)((const uint8_t *)comm +
                                    (size_t)slot * AM2_PLAYER_STRIDE +
                                    AM2_PLAYER_ID);

    return id != 0 && id != -1;
}



int32_t __cdecl GetMenuRow(void)
{
    return g_menuRow;
}

AM2_PtrList::AM2_PtrList()
{
    capacity = 0;
    count    = 0;
    items    = 0;
}

AM2_PtrList::~AM2_PtrList()
{
    ::ClearPtrList(this);
}

#if defined(__GNUC__)
void *AM2_PtrList::operator new(size_t n)
{
    return ((void *(__cdecl *)(uint32_t))(uintptr_t)ADDR_GAME_OPERATOR_NEW)(n);
}
void AM2_PtrList::operator delete(void *p)
{
    ((void (__cdecl *)(void *))(uintptr_t)ADDR_GAME_DELETE)(p);
}
void *__attribute__((thiscall)) am2_ctor_AM2_PtrList(void *mem)
{ return ::new (mem) AM2_PtrList(); }
void __attribute__((thiscall)) am2_dtor_AM2_PtrList(AM2_PtrList *l)
{ l->AM2_PtrList::~AM2_PtrList(); }
#endif

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
    int32_t n;

    /* An else-if chain onto one store, not a loop: VC6 duplicates the store
     * into each arm, which is the eight `mov eax, n; mov [slot], eax; ret`
     * the image has. DIK_1..DIK_8 are scancodes 2..9. */
    if (IsKeyDown(2) && KeyChanged(2))      n = 0;
    else if (IsKeyDown(3) && KeyChanged(3)) n = 1;
    else if (IsKeyDown(4) && KeyChanged(4)) n = 2;
    else if (IsKeyDown(5) && KeyChanged(5)) n = 3;
    else if (IsKeyDown(6) && KeyChanged(6)) n = 4;
    else if (IsKeyDown(7) && KeyChanged(7)) n = 5;
    else if (IsKeyDown(8) && KeyChanged(8)) n = 6;
    else if (IsKeyDown(9) && KeyChanged(9)) n = 7;
    else return;

    g_numberKeySlot = n;
}

/* The same spelling device.cpp uses -- PollKeyboard owns these and swaps them
 * each poll, and two names on one address is what checkglobals exists to
 * stop. */

/* The two below as the image expands them in their callers. */
static inline int32_t IsKeyDownInline(uint8_t dik)
{
    return g_curKeys[(uint32_t)dik & 0xFFu] & 0x80;
}

static inline int32_t KeyChangedInline(uint8_t dik)
{
    return (uint32_t)((int8_t)g_curKeys[dik & 0xFF]
                      ^ (int8_t)g_prevKeys[dik & 0xFF]) >> 7 & 1;
}

int32_t __cdecl IsKeyDown(uint8_t dik)
{
    /* 0x80, not 1: the original masks and returns, and never normalises. */
    return g_curKeys[(uint32_t)dik & 0xFFu] & 0x80;
}

int32_t __cdecl KeyChanged(uint8_t dik)
{
    /* Bit 7 of the xor. As signed bytes, shifted unsigned: the image loads
     * them without extending and masks with 1, which only a char operand
     * makes necessary; the scancode mask is spelled at each use. */
    return (uint32_t)((int8_t)g_curKeys[dik & 0xFF]
                      ^ (int8_t)g_prevKeys[dik & 0xFF]) >> 7 & 1;
}

void __cdecl FreeIfNotNull(void *p)
{
    if (p)
        am2_free(p);
}


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
    return *(const uint32_t *)(((uintptr_t)g_unitTypes)
                               + (uint32_t)type * AM2_UNIT_TYPE_STRIDE
                               + UNIT_TYPE_OFF_COST);
}


void __cdecl ConsumeKey(int32_t dik)
{
    uint8_t k = (uint8_t)dik;

    g_prevKeys[k] = g_curKeys[k];
    g_keyPressed[k] = 0;
}

int32_t __cdecl ActionKeyDown(int32_t action)
{
    /* No mask on `action` and no bound: the table is indexed raw. */
    if ((g_curKeys[g_keyBindings[action * 2]] & 0x80)
        || (g_curKeys[g_keyBindings[action * 2 + 1]] & 0x80))
        return 1;
    return 0;
}

int32_t __attribute__((thiscall)) AM2_Comm::CommArmyOfSlot(int32_t slot)
{
    void *comm = this;

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
    /* abs() each: the `cdq; xor; sub` the image has three times. */
    int32_t        d0 = abs((int32_t)p[0] - (int32_t)q[0]);
    int32_t        d1 = abs((int32_t)p[1] - (int32_t)q[1]);
    int32_t        d2 = abs((int32_t)p[2] - (int32_t)q[2]);

    return d0 * d0 + d1 * d1 + d2 * d2;
}

void __attribute__((thiscall)) AM2_Rec::PtrListGrow()
{
    void *rec = this;

    int32_t *r = (int32_t *)rec;

    r[0] = r[0] + AM2_PTR_LIST_SLACK;
    r[2] = (int32_t)(uintptr_t)am2_realloc((void *)(uintptr_t)r[2],
                                           (size_t)r[0] * 4);
}

void __attribute__((thiscall)) AM2_Rec::PtrListShrink()
{
    void *rec = this;

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

void __attribute__((thiscall)) AM2_Rec::PtrListPush(void *item)
{
    void *rec = this;

    int32_t *r = (int32_t *)rec;

    if (r[1] >= r[0])
        ::PtrListGrow(rec);
    /* Re-read the count after the grow, as the original does. */
    ((void **)r[2])[r[1]] = item;
    r[1] = r[1] + 1;
}

#define AM2_PTR_LIST_SLACK 20
void __attribute__((thiscall)) AM2_Rec::ListRemoveAt(int32_t index)
{
    void *rec = this;

    int32_t *r = (int32_t *)rec;

    if (index >= r[1])
        return;
    if (index < --r[1])
        memmove((void **)r[2] + index, (void **)r[2] + index + 1,
                (size_t)(r[1] - index) * 4);
    if (r[1] + AM2_PTR_LIST_SLACK < r[0])
        ((AM2_Rec *)rec)->PtrListShrink();
}

void __attribute__((thiscall)) AM2_Rec::ClearPtrList()
{
    void *rec = this;

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
    /* Both bindings written out, each through the two predicates in their
     * inline form -- the image expands them here. */
    if ((IsKeyDownInline(g_keyBindings[action * 2])
         && KeyChangedInline(g_keyBindings[action * 2]))
        || (IsKeyDownInline(g_keyBindings[action * 2 + 1])
            && KeyChangedInline(g_keyBindings[action * 2 + 1])))
        return 1;
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
    /* Both bindings written out, each through the two predicates in their
     * inline form -- the image expands them here. */
    if ((!IsKeyDownInline(g_keyBindings[action * 2])
         && KeyChangedInline(g_keyBindings[action * 2]))
        || (!IsKeyDownInline(g_keyBindings[action * 2 + 1])
            && KeyChangedInline(g_keyBindings[action * 2 + 1])))
        return 1;
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
    /* Written out per key, not a loop over a table: the image has the four
     * tests one after another. */
#define DISMISS(k) \
    if (!IsKeyDown(k) && KeyChanged(k)) { ConsumeKey(k); return 1; }
    DISMISS(AM2_DIK_SPACE)
    DISMISS(AM2_DIK_F1)
    DISMISS(AM2_DIK_ESCAPE)
    DISMISS(AM2_DIK_RETURN)
#undef DISMISS
    return 0;
}

/* The game's own atoi. The offline test maps the image as data and cannot
 * call into it, so this goes through AM2_IMAGE like the rest of misc.cpp's
 * seams -- and atoi is stateless, so the two agree anyway. */
typedef int32_t (__cdecl *AM2_AtoiFn)(const char *s);
#define orig_atoi ((AM2_AtoiFn)((void *)(uintptr_t)(ADDR_CRT_ATOI)))

typedef void (__cdecl *AM2_SeqRunFn)(void *ctx);

/* The three the seq adders below still reach the image for: the pool
 * allocator, the kind-6 adder, and the game's own LCG. */
typedef void  (__cdecl *AM2_SeqAdd6Fn)(const AM2_Point *at, int32_t a);
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
#define COMM (g_commObject)
    int32_t army;

    for (army = 0; army < AM2_COMM_ARMY_COUNT; army++) {
        if (!*(const int32_t *)(COMM + (uint32_t)army * AM2_PLAYER_STRIDE
                                + COMM_ARMY_OFF_WAS_HERE))
            continue;
        if (((AM2_Comm *)(void *)(uintptr_t)COMM)->CommSlotHasPlayer(army))
            continue;

        LoadArmyPlacement(army);
    }
#undef COMM
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
    SeqRun(g_seqCtxA);
    SeqRun(g_seqCtxB);
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
    /* The comm object and our army are re-read at each use. */
#define COMM  (g_commObject)
#define MINE  (g_defaultOwner)
#define TEAM(slot) (*(const int32_t *)(COMM + (size_t)(slot) * AM2_PLAYER_STRIDE \
                                        + COMM_ARMY_OFF_TEAM))
    int32_t slot = ((AM2_Comm *)(void *)(uintptr_t)COMM)->CommSlotForArmy(army);
    int32_t won;

    if (teamGame && TEAM(MINE))
        won = TEAM(MINE) == TEAM(slot);
    else
        won = slot == (int32_t)MINE;
    ShowMpResult(!won);
#undef COMM
#undef MINE
#undef TEAM
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
int32_t __attribute__((thiscall)) AM2_Comm::CommPlayerId(int32_t slot)
{
    void *comm = this;

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
/* `inline` is load-bearing: the original has this body at every call site,
 * and VC6's /O2 (/Ob1) inlines only functions declared inline. Out of line,
 * each caller compiled to push/call/pop -- the relink found it. */
static inline int32_t CommAllPlayersFlagged(void *comm, uint32_t flag)
{
    const uint8_t *rec;
    int32_t        n    = *(const int32_t *)
                              ((const uint8_t *)*(void *const *)&g_commObject
                               + COMM_OFF_PLAYER_COUNT);
    int32_t        i;

    /* The record is indexed from the counter, which VC6 strength-reduces
     * into a pointer set up after the count test -- the original's order. */
    for (i = 0; i < n; i++) {
        int32_t id;

        rec = (const uint8_t *)comm + AM2_PLAYER_ID + i * AM2_PLAYER_STRIDE;
        id  = *(const int32_t *)rec;

        if (id == 0 || id == -1)
            continue;                   /* no player in this slot */
        if (*(const int32_t *)(rec + (flag - AM2_PLAYER_ID)) == 0)
            return 0;
    }
    return 1;
}

int32_t __attribute__((thiscall)) AM2_Comm::CommAllPlayersAgreed()
{
    void *comm = this;

    return CommAllPlayersFlagged(comm, AM2_PLAYER_AGREED);
}

int32_t __attribute__((thiscall)) AM2_Comm::CommAllPlayersReady()
{
    void *comm = this;

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
    int32_t n;
    int32_t range;
    int32_t total;
    int32_t i;

    if (spread == 0)
        return centre;

    n = 100 - spread;
    if (n < 1)
        n = 1;

    range = centre * 2;
    total = 0;
    for (i = 0; i < n; i++)
        total += ((AM2_RandFn)((void *)(uintptr_t)(ADDR_GAME_RAND)))() % range;

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

    if (strcmp(name, "3do") != 0
        && strcmp(name, "credits") != 0
        && strcmp(name, "act2") != 0
        && strcmp(name, "portal") != 0
        && g_slowMachine
        && !g_optBigMovies)
        strcat(dst, "sml");

    strcat(dst, ".smk");
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

    (void)at;
    if (++*(uint32_t *)(row + ROW_OFF_STAMP_54) > 1)
        return SeqRetire(ctx, rec);

    RowUpdate((AM2_MapRow *)row, 1, g_mapDesc);
    return *(const int16_t *)((uint8_t *)rec + SEQ_OFF_NEXT);
}

/* Kind 3's stepper: SeqStepKind2's twin, body for body. The image has one
 * function at 0x00461310, yet SeqRun keeps two separate arms that both call it
 * -- which VC6 does not emit for one callee (it merges identical arms), and
 * does for two identical functions the linker then folds (/OPT:ICF). So the
 * source had two, and this is the second. Not installed: it has no address of
 * its own. */
static int32_t __cdecl SeqStepKind3(int32_t at, void *rec, void *ctx)
{
    uint8_t *row = *(uint8_t **)((uint8_t *)rec + SEQ_OFF_ROW);

    (void)at;
    if (++*(uint32_t *)(row + ROW_OFF_STAMP_54) > 1)
        return SeqRetire(ctx, rec);

    RowUpdate((AM2_MapRow *)row, 1, g_mapDesc);
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
void __cdecl SeqAddKind6(const AM2_Point *at, int32_t variant)
{
    uint8_t *seq;
    uint8_t *row;

    if (!PointInRect((const AM2_Rect *)&g_mapBoundsLeft, at))
        return;

    seq = (uint8_t *)SeqAlloc(g_seqCtxA);

    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND6;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = (uint8_t)variant;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_10) = 0;
    *(int32_t *)(seq + SEQ_OFF_OWNER)    = 0;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     = AM2_SEQ_LIFE6;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    row = *(uint8_t **)(seq + SEQ_OFF_ROW);
    *(uint8_t *)(row + ROW_OFF_CELL)     = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54) = 0;
    *(int32_t *)(row + 0)                = 1;
    *(void **)(row + ROW_OFF_SPRITE)     =
        (g_seqSprites7)
            [variant * *(const int32_t *)((void *)(uintptr_t)(AM2_SEQ_VARIANT_STRIDE))];
    *(AM2_Point *)(row + ROW_OFF_X)      = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST) = 0;
    *(int32_t *)(row + ROW_OFF_FIELD_2C) = 0;
    *(int16_t *)(row + ROW_OFF_FIELD_26) =
        (int16_t)(ScaleBy32Blocks(TileAttrAt(TileOfPoint(*at)))
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
void __cdecl SeqAddKind4(const AM2_Point *at, int32_t yAdjust)
{
    uint8_t *seq;
    uint8_t *row;

    if (!PointInRect((const AM2_Rect *)&g_mapBoundsLeft, at))
        return;

    seq = (uint8_t *)SeqAlloc(g_seqCtxB);

    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND4;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) =
        g_seqK4DriftX;
    *(int32_t *)(seq + SEQ_OFF_FIELD_10) =
        g_seqK4DriftY;
    *(int32_t *)(seq + SEQ_OFF_OWNER)    = 0;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     =
        g_seqK4StepMs;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    row = *(uint8_t **)(seq + SEQ_OFF_ROW);   /* taken after the seq fields */
    *(uint8_t *)(row + ROW_OFF_CELL)     = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54) = 0;
    *(int32_t *)(row + 0)                = 1;
    *(void **)(row + ROW_OFF_SPRITE)     =
        *g_seqSprites5;
    *(AM2_Point *)(row + ROW_OFF_X)      = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST) = (int16_t)yAdjust;
    *(void **)(row + ROW_OFF_FIELD_2C)   =
        g_remapShades[0];
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
#define GATE (*(uint32_t *)(r + SEQ_OFF_GATE))
#define CELL (*(uint8_t *)(row + ROW_OFF_CELL))
        if (++GATE > (g_seqK4Hold)[CELL]) {
            if (++CELL >= AM2_SEQ_K4_CELLS)
                return SeqRetire(ctx, rec);

            *(void **)(row + ROW_OFF_SPRITE) =
                (g_seqSprites5)[CELL];
            *(void **)(row + ROW_OFF_FIELD_2C) =
                g_remapShades[CELL];

            GATE = 1;
        }
#undef GATE
#undef CELL

        *(int16_t *)(row + ROW_OFF_Y_ADJUST) +=
            g_seqK4Rise;
        *(int16_t *)(row + ROW_OFF_X) +=
            (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_0C);
        *(int16_t *)(row + ROW_OFF_Y) +=
            (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_10);

        *(int32_t *)(row + ROW_OFF_STAMP_54) = 0;

        if (!PointInRect((const AM2_Rect *)&g_mapBoundsLeft,
                         (const AM2_Point *)(row + ROW_OFF_X)))
            return SeqRetire(ctx, rec);

        RowUpdate((AM2_MapRow *)row, 0, g_mapDesc);
    }

    *(int32_t *)(row + ROW_OFF_STAMP_54) +=
        g_frameDeltaMs;

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

    (void)at;

    if (*(const uint32_t *)(row + ROW_OFF_STAMP_54)
            > *(const uint32_t *)(r + SEQ_OFF_LIFE))
        return SeqRetire(ctx, rec);

    if (*(const uint32_t *)(r + SEQ_OFF_GATE)
            > g_seqEmitMs) {
        AM2_Point pt = *(const AM2_Point *)(row + ROW_OFF_X);

        pt.x = (int16_t)(pt.x + (orig_seq_rand() % 3) * 4 - 4);
        SeqAddKind4(&pt, AM2_SEQ_EMIT_ARG);

        *(int32_t *)(r + SEQ_OFF_GATE) -=
            *(const int32_t *)&g_seqEmitMs;
    }

    *(int32_t *)(row + ROW_OFF_STAMP_54) += g_frameDeltaMs;
    *(int32_t *)(r + SEQ_OFF_GATE)       += g_frameDeltaMs;

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

        if (*(const uint32_t *)(rec + SEQ_OFF_GATE) > 0) {
            switch (*(const uint32_t *)(rec + SEQ_OFF_KIND)) {
            case 0: at = SeqStepKind0(at, rec, ctx); break;
            case 1: break;   /* hangs; see above */
            case 2: at = SeqStepKind2(at, rec, ctx); break;
            case 3: at = SeqStepKind3(at, rec, ctx); break;
            case 4: at = SeqStepKind4(at, rec, ctx); break;
            case 5: at = SeqStepKind5(at, rec, ctx); break;
            case 6: at = SeqStepKind6(at, rec, ctx); break;
            case 7: at = SeqStepKind7(at, rec, ctx); break;
            default:
                at = *(const int16_t *)(rec + SEQ_OFF_NEXT);
                break;
            }
        } else {
            at = *(const int16_t *)(rec + SEQ_OFF_NEXT);
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
void __cdecl SeqAddKind5(const AM2_Point *at, int32_t owner, int32_t life)
{
    uint8_t *seq;
    uint8_t *row;

    if (!PointInRect((const AM2_Rect *)&g_mapBoundsLeft, at))
        return;

    seq = (uint8_t *)SeqAlloc(g_seqCtxA);

    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND5;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = 0;
    *(int32_t *)(seq + SEQ_OFF_OWNER)    = owner;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) = 0;
    *(int32_t *)(seq + SEQ_OFF_FIELD_10) = 0;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     = AM2_SEQ_LIFE5;
    if (life > 0)
        *(int32_t *)(seq + SEQ_OFF_LIFE) = life;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    row = *(uint8_t **)(seq + SEQ_OFF_ROW);

    *(uint8_t *)(row + ROW_OFF_CELL)      = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54)  = 0;
    *(int32_t *)(row + 0)                 = 0;
    *(void **)(row + ROW_OFF_SPRITE)      =
        *g_seqSprites5;
    *(AM2_Point *)(row + ROW_OFF_X)       = *at;
    *(int16_t *)(row + ROW_OFF_Y_ADJUST)  = 0;
    *(int32_t *)(row + ROW_OFF_FIELD_2C)  = 0;
    *(int16_t *)(row + ROW_OFF_FIELD_26)  = AM2_SEQ_ROW26_5;
}

void __cdecl SeqAddKind7(const AM2_Point *at, int32_t owner, int32_t b,
                         int32_t c, int32_t life)
{
    uint8_t *seq;
    uint8_t *row;

    (void)b;
    (void)c;

    if (!PointInRect((const AM2_Rect *)&g_mapBoundsLeft,
                     at))
        return;

    seq = (uint8_t *)SeqAlloc(g_seqCtxA);

    *(int32_t *)(seq + SEQ_OFF_KIND)     = AM2_SEQ_KIND7;
    *(uint8_t *)(seq + SEQ_OFF_FLAG4)    = 0;
    *(int32_t *)(seq + SEQ_OFF_OWNER)    = owner;
    *(int32_t *)(seq + SEQ_OFF_LIFE)     = AM2_SEQ_LIFE7;
    *(int32_t *)(seq + SEQ_OFF_FIELD_0C) = 0;
    if (life > 0)
        *(int32_t *)(seq + SEQ_OFF_LIFE) = life;
    *(int32_t *)(seq + SEQ_OFF_GATE)     = 1;

    row = *(uint8_t **)(seq + SEQ_OFF_ROW);

    *(int32_t *)(row + 0)                 = 0;
    *(uint8_t *)(row + ROW_OFF_CELL)      = 0;
    *(int32_t *)(row + ROW_OFF_STAMP_54)  = 0;
    *(void **)(row + ROW_OFF_SPRITE)      =
        *g_seqSprites7;
    *(AM2_Point *)(row + ROW_OFF_X)       = *at;
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
 * THE COUNT IS NOT BUMPED. The array is reallocated to count + 1 records,
 * but the incremented count lives only in a register (`inc eax` at
 * 0x00453AB8, never stored); the search then reloads the unchanged count.
 * This was reconstructed as `++count` -- read as a bump that the search then
 * ran over -- which grew the list by one uninitialised row on every call and
 * compared that row's garbage as a key. The byte-match is what showed the
 * missing store.
 *
 * The record size is written as `((n << 6) + n) << 2` -- n * 65 * 4 -- which
 * is the compiler's way of saying 260 and is why the stride shows up twice in
 * different forms. One name for it.
 */
int32_t __attribute__((thiscall)) AM2_Rec::ListGrowFind(int32_t key)
{
    void *self = this;
    uint8_t *s = (uint8_t *)self;
    int32_t  n;
    uint8_t *items;
    int32_t  i;

    items = (uint8_t *)am2_realloc(*(void **)(s + 4),
                                   (size_t)(*(const int32_t *)s + 1)
                                       * AM2_LIST_ROW_STRIDE);
    *(void **)(s + 4) = items;

    n = *(const int32_t *)s;

    for (i = 0; i < n; i++)
        if (*(const int32_t *)(items + i * AM2_LIST_ROW_STRIDE
                               + AM2_GROWLIST_KEY) == key)
            return i;

    return -1;
}


/* 0x0043CCE0. One line: step `this` on by 0xA4 and empty the pointer list
 * that lives there. Four callers, all of them tail jumps from game code above
 * the nominal CRT line, and not one names the enclosing object -- so the name
 * carries the OFFSET rather than a record it cannot support. */
void __attribute__((thiscall)) AM2_ObjLists::ClearPtrListAtA4()
{
    void *obj = this;

    ClearPtrListAlias((uint8_t *)obj + 0xA4);
}


/* 0x0040FB90. Format a bare "." into the caller's buffer and answer 1.
 *
 * Its only reference is a slot in a record at 0x0047588C that also holds the
 * string "FOO" -- and nothing in the image references that record's base, so
 * the record's consumer is NOT identified. The name therefore describes the
 * BODY and claims nothing about the role, which is the standing rule for a
 * table with no readable consumer.
 *
 * orig.h already records the neighbourhood being misread once: 0x0040FB80 was
 * described as "formats a string and hands it to CommSendProperty" because the
 * merged functions.tsv entry was swept as one function. It is eight bytes; the
 * sprintf is this function. */
int32_t __cdecl WriteDotString(char *dst)
{
    am2_sprintf(dst, ".");
    return 1;
}


/* 0x0045A3F0. Empty both of a vehicle's pointer lists: the one at
 * VEHICLE_OFF_PTR_LIST and the common one at OBJ_OFF_PTR_LIST. It is the MSVC
 * member-destructor sequence -- two subobjects taken down in order, with the
 * unwind index stepped between them -- and the 0xA4 half is the same field
 * ClearPtrListAtA4 above reaches on its own. So it is written as the
 * destructor it is, with an empty body: the compiler supplies both calls and
 * the unwind frame (misc.h, AM2_VehicleLists). */
AM2_VehicleLists::~AM2_VehicleLists()
{
}

#if defined(__GNUC__)
void __attribute__((thiscall)) am2_dtor_AM2_VehicleLists(AM2_VehicleLists *v)
{ v->AM2_VehicleLists::~AM2_VehicleLists(); }
#endif

int misc_install(void)
{
    patch_replace(ADDR_AI_TAKE_ABANDONED, (const void *)AiTakeAbandoned,
                  "AiTakeAbandoned", 0);
    patch_replace(ADDR_SEQ_RUN_BOTH, (const void *)SeqRunBoth,
                  "SeqRunBoth", 0);
    patch_replace(ADDR_COLOUR_DISTANCE, (const void *)ColourDistance,
                  "ColourDistance", 2);
    patch_replace(ADDR_PTR_LIST_GROW, pmf_addr(&AM2_Rec::PtrListGrow),
                  "PtrListGrow", 1);
    patch_replace(ADDR_PTR_LIST_SHRINK, pmf_addr(&AM2_Rec::PtrListShrink),
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
                  "SwapColourBytes", 1);
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
    patch_replace(ADDR_COMM_SLOT_FOR_ARMY, pmf_addr(&AM2_Comm::CommSlotForArmy),
                  "CommSlotForArmy", 20);
    patch_replace(ADDR_COMM_PLAYER_ID, pmf_addr(&AM2_Comm::CommPlayerId),
                  "CommPlayerId", 1);
    patch_replace(ADDR_RANDOM_AROUND, (const void *)RandomAround,
                  "RandomAround", 1);
    patch_replace(ADDR_ALL_PLAYERS_AGREED, pmf_addr(&AM2_Comm::CommAllPlayersAgreed),
                  "CommAllPlayersAgreed", 1);
    patch_replace(ADDR_ALL_PLAYERS_READY, pmf_addr(&AM2_Comm::CommAllPlayersReady),
                  "CommAllPlayersReady", 1);
    patch_replace(ADDR_COMM_WAS_HERE_FOR_ARMY, pmf_addr(&AM2_Comm::CommWasHereForArmy),
                  "CommWasHereForArmy", 1);
    patch_replace(ADDR_LOAD_MASK_PACKED, (const void *)LoadMaskPacked,
                  "LoadMaskPacked", 1);
    patch_replace(ADDR_APPLY_GAME_SETTINGS, (const void *)ApplyGameSettings,
                  "ApplyGameSettings", 6);
    patch_replace(ADDR_COMM_TEAM_SCORE, pmf_addr(&AM2_Comm::CommTeamScore),
                  "CommTeamScore", 1);
    patch_replace(ADDR_GET_ARMY_SCORE, pmf_addr(&AM2_Comm::GetArmyScore),
                  "GetArmyScore", 3);
    patch_replace(ADDR_COMM_SLOT_HAS_PLAYER, pmf_addr(&AM2_Comm::CommSlotHasPlayer),
                  "CommSlotHasPlayer", 5);
    patch_replace(ADDR_COMM_SET_ARMY_COLOUR, pmf_addr(&AM2_Comm::CommSetArmyColour),
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
    patch_replace(ADDR_COMM_ARMY_OF_SLOT, pmf_addr(&AM2_Comm::CommArmyOfSlot),
                  "CommArmyOfSlot", 20);
    patch_replace(ADDR_PTR_LIST_PUSH, pmf_addr(&AM2_Rec::PtrListPush),
                  "PtrListPush", 2);
    patch_replace(ADDR_LIST_REMOVE_AT, pmf_addr(&AM2_Rec::ListRemoveAt),
                  "ListRemoveAt", 2);
    patch_replace(ADDR_CLEAR_PTR_LIST, pmf_addr(&AM2_Rec::ClearPtrList),
                  "ClearPtrList", 1);
    patch_replace(ADDR_KEY_CHANGED, (const void *)KeyChanged, "KeyChanged", 1);
    patch_replace(ADDR_INIT_PTR_LIST, AM2_CTOR(AM2_PtrList),
                  "InitPtrList", 1);
    patch_replace(ADDR_CLEAR_PTR_LIST_ALIAS, AM2_DTOR(AM2_PtrList),
                  "ClearPtrListAlias", 1);
    patch_replace(ADDR_LIST_GROW_FIND, pmf_addr(&AM2_Rec::ListGrowFind),
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
    patch_replace(ADDR_CLEAR_PTR_LIST_A4, pmf_addr(&AM2_ObjLists::ClearPtrListAtA4),
                  "ClearPtrListAtA4", 1);
    patch_replace(ADDR_WRITE_DOT_STRING, (const void *)WriteDotString,
                  "WriteDotString", 1);
    patch_replace(ADDR_CLEAR_VEHICLE_LISTS, AM2_DTOR(AM2_VehicleLists),
                  "ClearVehicleLists", 1);
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
    uint8_t *r     = (uint8_t *)rec;
    uint8_t *row   = *(uint8_t **)(r + SEQ_OFF_ROW);
    uint32_t life  = *(const uint32_t *)(r + SEQ_OFF_LIFE);
    uint32_t stamp = *(const uint32_t *)(row + ROW_OFF_STAMP_54);

    /* The tail count, the stride, the advance and the frame delta are
     * globals, read afresh at every use. */
#define TAIL    (g_seqTailFrames)
#define STRIDE  (*(const int32_t *)(uintptr_t)AM2_SEQ_VARIANT_STRIDE)
#define ADVANCE (g_seqAdvanceMs)
#define DELTA   (*(const uint32_t *)&g_frameDeltaMs)
#define PHASE   (*(uint8_t *)(row + 0x51u))

    (void)at;

    if (stamp > life)
        return SeqRetire(ctx, rec);

    if (*(const uint32_t *)(r + SEQ_OFF_GATE) > ADVANCE) {
        int32_t variant = *(const uint8_t *)(r + SEQ_OFF_FLAG4) * STRIDE;

        if ((int32_t)stamp > (int32_t)life - 100) {
            int32_t per = 100 / TAIL;
            int32_t idx = ((int32_t)stamp - (int32_t)life + 100) / per;

            if (idx >= TAIL - 1)
                idx = TAIL - 1;
            variant += STRIDE - TAIL + idx;
        } else {
            if (++PHASE >= STRIDE - TAIL)
                PHASE = 0;
            variant += PHASE;
        }

        *(uint32_t *)(row + ROW_OFF_SPRITE) =
            (*(uint32_t *const *)&g_seqSprites7)[variant];
        RowUpdate((AM2_MapRow *)row, 0, g_mapDesc);

        *(uint32_t *)(r + SEQ_OFF_GATE) -= ADVANCE;
    }

    *(uint32_t *)(row + ROW_OFF_STAMP_54) += DELTA;
    *(uint32_t *)(r + SEQ_OFF_GATE) += DELTA;

    return *(const int16_t *)(r + SEQ_OFF_NEXT);
#undef TAIL
#undef STRIDE
#undef ADVANCE
#undef DELTA
#undef PHASE
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
        AM2_Point pt = *(const AM2_Point *)(row + ROW_OFF_X);
        int32_t   variant = orig_seq_rand() % 3;
        int32_t   skip = 0;

        pt.y = (int16_t)(pt.y - (orig_seq_rand() % 16));
        pt.x = (int16_t)(pt.x + ((orig_seq_rand() % 16) - 8) * variant);

        SeqAddKind6(&pt, variant);
        *(int32_t *)(r + SEQ_OFF_FIELD_0C) += 1;

        if (g_mpSession != 0
            && !AM2_THE_COMM->CommMustBroadcast(
                    (int16_t)UidArmy(*(const uint32_t *)(r + SEQ_OFF_OWNER))))
            skip = 1;

        if (!skip) {
            AM2_Rect box;
            uint8_t *obj;

            box.left   = *(const int16_t *)(row + ROW_OFF_X) - 0x28;
            box.right  = *(const int16_t *)(row + ROW_OFF_X) + 0x28;
            box.top    = *(const int16_t *)(row + ROW_OFF_X + 2) - 0x40;
            box.bottom = *(const int16_t *)(row + ROW_OFF_X + 2) + 0x10;

            obj = (uint8_t *)AllObjectsInRect(
                      &box, g_objMapDesc);
            while (obj != 0) {
                if (ObjIsTypeIn238((const AM2_Object *)obj))
                    DamageObject((AM2_Object *)obj, 1, 1,
                                 *(const uint32_t *)(r + SEQ_OFF_OWNER), 0, 0);
                else if (*(const int32_t *)(r + SEQ_OFF_FIELD_0C) > 9)
                    DamageObject((AM2_Object *)obj, 0x0F, 1,
                                 *(const uint32_t *)(r + SEQ_OFF_OWNER), 0, 0);
                obj = *(uint8_t **)(obj + 0x68u);
            }
        }

        *(int32_t *)(r + SEQ_OFF_GATE) -= 0x12C;
        if (*(const int32_t *)(r + SEQ_OFF_FIELD_0C) > 9)
            *(int32_t *)(r + SEQ_OFF_FIELD_0C) = 0;
    }

    *(uint32_t *)(row + ROW_OFF_STAMP_54) +=
        *(const uint32_t *)&g_frameDeltaMs;
    *(int32_t *)(r + SEQ_OFF_GATE) +=
        g_frameDeltaMs;

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
    uint32_t pt;
    /* The particle kind's record is re-indexed, and the fields re-read, at
     * every use. The kind is SEQ_OFF_FLAG4 read as a dword. */
    typedef struct { int32_t frames; const uint32_t *sprites; uint8_t pad[16]; } Kind;
#define KIND  ((g_particleKinds) \
               [*(const int32_t *)(r + SEQ_OFF_FLAG4)])
#define LIFE  (*(int32_t *)(r + SEQ_OFF_LIFE))
#define STAMP (*(uint32_t *)(row + ROW_OFF_STAMP_54))
#define PHASE (*(uint8_t *)(row + 0x51u))
#define Z     (*(int16_t *)(row + 0x20u))
    uint8_t *r   = (uint8_t *)rec;
    uint8_t *row = *(uint8_t **)(r + SEQ_OFF_ROW);

    (void)at;

    STAMP += *(const uint32_t *)&g_frameDeltaMs;

    if (STAMP > *(const uint32_t *)(r + SEQ_OFF_GATE)) {
        int32_t   tile;
        char      ground;

        if (LIFE != 0) {
            if (++PHASE >= KIND.frames)
                PHASE = 0;
            *(uint32_t *)(row + ROW_OFF_SPRITE) = KIND.sprites[PHASE];
        }

        *(int16_t *)(row + ROW_OFF_X) +=
            (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_0C);
        *(int16_t *)(row + ROW_OFF_X + 2) +=
            (int16_t)*(const int32_t *)(r + SEQ_OFF_FIELD_10);
        Z += (int16_t)LIFE;

        /* The bounds test is on the DRAWN point -- the row's position with
         * its height subtracted from z -- not on the ground position. */
        pt = *(const uint32_t *)(row + ROW_OFF_X);
        *((int16_t *)&pt + 1) -= Z;
        if (!PointInRect((const AM2_Rect *)&g_mapBoundsLeft,
                         (const AM2_Point *)&pt))
            return SeqRetire(ctx, rec);

        tile = TileOfPoint(*(const AM2_Point *)(row + ROW_OFF_X)) & 0xFFFF;
        ground = (*(const char *const *)&g_tileAttrs)[tile];

        if (Z + *(const int32_t *)(r + 0x18u) <= ground) {
            Z = (int16_t)((int16_t)ground - *(const int16_t *)(r + 0x18u));

            if ((g_tileFlags)[tile] & 1) {
                CreateExplosion(*(const int16_t *)(row + ROW_OFF_X),
                                *(const int16_t *)(row + ROW_OFF_X + 2),
                                0x93, 4, 0, 0, 0, 0, 0, 0);
                return SeqRetire(ctx, rec);
            }

            if (LIFE < -4) {
                LIFE = abs(LIFE >> 1) - 2 >= 7 ? 7 : abs(LIFE >> 1) - 2;
                STAMP = 0;
            } else {
                /* The row is read again for the copy. */
                uint8_t *old  = *(uint8_t **)(r + SEQ_OFF_ROW);
                uint8_t *rest = *(uint8_t **)RowPoolAAlloc();

                *(int32_t *)rest = 1;
                *(uint32_t *)(rest + ROW_OFF_SPRITE) =
                    *(const uint32_t *)(old + ROW_OFF_SPRITE);
                *(int16_t *)(rest + ROW_OFF_X) = *(const int16_t *)(old + ROW_OFF_X);
                *(int16_t *)(rest + ROW_OFF_X + 2) =
                    *(const int16_t *)(old + ROW_OFF_X + 2);
                *(uint32_t *)(rest + ROW_OFF_FIELD_2C) =
                    *(const uint32_t *)(old + ROW_OFF_FIELD_2C);
                *(int16_t *)(rest + ROW_OFF_FIELD_26) = 2;
                RowUpdate((AM2_MapRow *)rest, 0, g_mapDesc);
                return SeqRetire(ctx, rec);
            }
        } else {
            LIFE -= 1;                  /* gravity */
            STAMP = 0;
        }
    }

    RowUpdate((AM2_MapRow *)row, 0, g_mapDesc);
    return *(const int16_t *)(r + SEQ_OFF_NEXT);
#undef KIND
#undef LIFE
#undef STAMP
#undef PHASE
#undef Z
}
