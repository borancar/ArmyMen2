/* anim.cpp -- see anim.h.
 *
 * Flat rather than under win32/: the animation table holds sprite INDICES and
 * never an AM2_Sprite, so nothing here names a DirectDraw type. Its caller,
 * LoadSpriteFile, has to stay on the other side because AM2_Sprite carries an
 * LPDIRECTDRAWSURFACE.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anim.h"
#include "crt.h"
#include "dist.h"   /* Log2Mask, RoundTo8 */
#include "image.h"
#include "gamedir.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define g_spriteListN   (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_COUNT)
/* TWO dereferences: the global holds the array, it is not the array. Spelled
 * as air.cpp spells it. Getting this wrong is what CLAUDE.md's
 * `obj -> table -> slot` note is about, and it cost an A/B here -- 293,671
 * differing pixels on `bootcamp`, which is the whole map drawn with whatever
 * the sprite-list pointer's own bytes indexed to. */
struct AM2_Sprite;   /* incomplete: it has an LPDIRECTDRAWSURFACE in it and
                      * this module is flat, the same reason air.cpp and
                      * script.cpp declare it rather than including the header */
#define g_spriteList    (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_LIST)

/* Forward-declared rather than including win32/sprite.h, the same reason
 * script.cpp forward-declares PreloadSprite: that header names
 * LPDIRECTDRAWSURFACE and this module must not. None of these three
 * signatures does, so the declaration costs nothing. */
extern "C" int32_t __cdecl LoadSpriteFile(const char *path,
                                          AM2_AnimTable *anims,
                                          const AM2_AnimTable *fallback,
                                          int32_t from, uint32_t flags);
extern "C" void __cdecl BuildRoachMask(void);
extern "C" void __cdecl BuildVehicleMask(int32_t kind);

#define g_explosionAnims ((AM2_AnimTable *)(uintptr_t)ADDR_EXPLOSION_ANIMS)
#define g_missileAnims   ((AM2_AnimTable *)(uintptr_t)ADDR_MISSILE_ANIMS)
#define g_roachAnims     ((AM2_AnimTable *)(uintptr_t)ADDR_ROACH_ANIMS)
#define g_soldierAnims   ((AM2_AnimTable *)(uintptr_t)ADDR_SOLDIER_ANIMS)
#define g_turretAnims    ((AM2_AnimTable *)(uintptr_t)ADDR_TURRET_ANIMS)
#define g_vehicleAnims   ((AM2_AnimTable *)(uintptr_t)ADDR_VEHICLE_ANIMS)

/* Both from the teardown loops, not from a guess: 0x004470D0 writes out nine
 * calls and 0x0045A990 walks 0..0x30 in steps of 8. */
#define AM2_SOLDIER_ANIM_TABLES  9
#define AM2_VEHICLE_ANIM_TABLES  6

/* AM2_DUMP_ANIMS=1 prints every table this parses, which tools/anicheck.py
 * compares against its own reading of the `.ani` files. Latched once: the
 * function runs 21 times per Boot Camp mission and getenv is not free. */
static int32_t am2_dump_anims = -1;

static void DumpAnimTable(const AM2_AnimTable *t)
{
    char    buf[512];
    int32_t i;
    int32_t j;
    int32_t n;

    am2_log("ANIMS count=%d\n", t->count);
    for (i = 0; i < t->count; i++) {
        const AM2_AnimEntry *e = &t->entries[i];

        /* One call per entry, not one per field: the game's logger writes a
         * line per call and drops anything not ending in a newline. */
        n = sprintf(buf, "ANIM %3d id=%d next=%d borrowed=%d anim=%p", i,
                    e->id, e->next, e->borrowed, (const void *)e->anim);
        if (e->anim && !e->borrowed) {
            const AM2_Anim *a = e->anim;
            int32_t cells = (int16_t)(a->directions * a->frames);

            n += sprintf(buf + n, " f=%d fa=%d bits=%d w4=%d w6=%d cells=%d",
                         a->frames, a->directions, a->directionBits, a->field4,
                         a->field6, cells);
            for (j = 0; j < cells && j < 6; j++)
                n += sprintf(buf + n, " (%d,%d)", a->cells[j].hold,
                             a->cells[j].sprite);
        }
        sprintf(buf + n, "\n");
        am2_log("%s", buf);
    }
}

void __cdecl LoadAnimTable(am2_FILE *fp, AM2_AnimTable *table, int32_t base,
                           const AM2_AnimTable *fallback)
{
    int32_t i;
    int32_t k;
    int16_t w;

    /* The count, then that many 16-byte entries, allocated and cleared before
     * anything is read into them -- which is what makes the "did anyone fill
     * this in" tests below work. */
    orig_fread(&table->count, 4, 1, fp);
    table->entries = (AM2_AnimEntry *)am2_malloc((size_t)(table->count * 16));
    memset(table->entries, 0, (size_t)(table->count * 16));

    for (i = 0; i < table->count; i++) {
        AM2_AnimEntry *e = &table->entries[i];

        orig_fread(&w, 2, 1, fp);
        e->id = w;
        orig_fread(&w, 2, 1, fp);
        e->next = w;
        if (e->next == 0)
            e->next = -2;

        /* The kind. Only 0 and 1 appear in any shipped file: 1 carries an
         * animation, anything else borrows one. */
        orig_fread(&w, 2, 1, fp);
        if (w == 1) {
            AM2_Anim *a;
            int32_t   cells;
            int32_t   j;

            e->borrowed = 0;
            a = (AM2_Anim *)am2_malloc(16);
            e->anim = a;

            orig_fread(&w, 2, 1, fp);
            a->frames = w;
            orig_fread(&w, 2, 1, fp);
            a->directions = w;
            a->directionBits = Log2Mask(a->directions);
            orig_fread(&w, 2, 1, fp);
            a->field4 = w;
            a->zero9 = 0;
            orig_fread(&w, 2, 1, fp);
            a->field6 = w;

            /* Computed 16-bit and sign-extended afterwards, exactly as the
             * original does -- so a grid big enough to overflow an int16 would
             * come out negative and skip both the allocation and the loop. */
            cells = (int16_t)(a->directions * a->frames);
            if (cells > 0)
                a->cells = (AM2_AnimCell *)am2_malloc((size_t)cells * 4);

            for (j = 0; j < cells; j++) {
                orig_fread(&w, 2, 1, fp);
                a->cells[j].hold = w;
                orig_fread(&w, 2, 1, fp);
                /* The original reads this scratch back as a DWORD, so the top
                 * half is stale -- it is the file pointer's own argument slot,
                 * which MSVC reused. Only the low half is stored, so the stale
                 * bits cannot reach the result and a 16-bit read is the same
                 * function. */
                a->cells[j].sprite = (int16_t)((uint16_t)w + (uint16_t)base);
                if (a->cells[j].sprite >= g_spriteListN)
                    am2_log("Error!  %d\n", a->cells[j].sprite);
            }
        } else {
            e->borrowed = 1;
            if (fallback) {
                /* No break on a match: the last entry carrying the id wins. */
                for (k = 0; k < fallback->count; k++)
                    if (e->id == fallback->entries[k].id)
                        e->anim = fallback->entries[k].anim;
                /* Reached whether the loop ran or not, so an empty fallback
                 * reads entry 0 of a table that has none. See anim.h. */
                if (!e->anim)
                    e->anim = fallback->entries[0].anim;
            }
        }
    }

    /* Anything still without an animation -- no fallback, or a fallback that
     * did not have it either -- takes this table's own first one. */
    for (k = 0; k < table->count; k++)
        if (!table->entries[k].anim) {
            table->entries[k].anim = table->entries[0].anim;
            table->entries[k].borrowed = 1;
        }

    if (am2_dump_anims < 0)
        am2_dump_anims = getenv("AM2_DUMP_ANIMS") != 0;
    if (am2_dump_anims)
        DumpAnimTable(table);
}

/* The `.ani` directory, and the flags each group is loaded with. `from` is
 * NearestPalIndex's reserved-block threshold and `flags` decides the sprite
 * format; both are the caller's decision, not the file's. */
#define AM2_ANI_DIR       "data\\ani"
#define AM2_ANI_FLAGS_FX  8
#define AM2_ANI_FLAGS_MEN 0x10

void __cdecl LoadExplosionAnims(void)
{
    /* The chdir happens BEFORE the already-loaded test here and in
     * LoadMissileAnims, and AFTER it in LoadRoachAnims. Reproduced rather
     * than tidied: a chdir is a side effect, so which side of the test it
     * falls on is behaviour. */
    SetGameDir(AM2_ANI_DIR);
    if (g_explosionAnims->count <= 0)
        LoadSpriteFile("explosions.ani", g_explosionAnims, 0, 0,
                       AM2_ANI_FLAGS_FX);
}

void __cdecl LoadMissileAnims(void)
{
    SetGameDir(AM2_ANI_DIR);
    if (g_missileAnims->count <= 0)
        LoadSpriteFile("missile.ani", g_missileAnims, 0, 0, AM2_ANI_FLAGS_FX);
}

void __cdecl LoadRoachAnims(void)
{
    if (g_roachAnims->count > 0)
        return;
    SetGameDir(AM2_ANI_DIR);
    LoadSpriteFile("roach.ani", g_roachAnims, 0, 0, AM2_ANI_FLAGS_FX);
    /* A tail jump in the original, so the mask is built only on the load
     * path -- a second call does nothing at all. */
    BuildRoachMask();
}

void __cdecl LoadSoldierAnims(void)
{
    /* rifleman first and with no fallback, because it IS the fallback for the
     * other eight -- see LoadAnimTable. The zombie and the scientists reserve
     * 0x3C palette entries where everyone else reserves 10. */
    static const struct {
        const char *path;
        int32_t     from;
    } kMen[] = {
        { "rifleman.ani",   0x0A }, { "bazookaman.ani", 0x0A },
        { "grenadier.ani",  0x0A }, { "flamer.ani",     0x0A },
        { "mortarman.ani",  0x0A }, { "sweeper.ani",    0x0A },
        { "m80.ani",        0x0A }, { "zombie.ani",     0x3C },
        { "scientists.ani", 0x3C },
    };
    int32_t i;

    SetGameDir(AM2_ANI_DIR);
    for (i = 0; i < AM2_SOLDIER_ANIM_TABLES; i++)
        if (g_soldierAnims[i].count <= 0)
            LoadSpriteFile(kMen[i].path, &g_soldierAnims[i],
                           i ? &g_soldierAnims[0] : 0, kMen[i].from,
                           AM2_ANI_FLAGS_MEN);

    /* Then one link is cut: the animation with id 0x46 in the rifleman's own
     * table gets `next` = -1, where LoadAnimTable would have left whatever the
     * file said. Nothing is done if the table has no such entry, or none at
     * all. */
    for (i = 0; i < g_soldierAnims[0].count; i++)
        if (g_soldierAnims[0].entries[i].id == AM2_ANIM_ID_LINK_BREAK) {
            g_soldierAnims[0].entries[i].next = -1;
            return;
        }
}

void __cdecl LoadVehicleAnims(void)
{
    /* Six pairs, laid out interleaved in the original's frame as {vehicle,
     * turret}. Two of the twelve slots point at the image's shared empty
     * string at 0x004F96B8 -- 67 sites use it and nothing writes it -- and an
     * empty path is skipped before the count is even looked at.
     *
     * So kind 4 has no vehicle and no turret, which is why its direction count
     * stays 0 and BuildVehicleMask runs five times rather than six. And the
     * boat is given the JEEP's turret, which reads like a mistake and is
     * reproduced as it stands. */
    static const char *const kPaths[AM2_VEHICLE_ANIM_TABLES][2] = {
        { "jeep.ani",      "jeepturret.ani" },
        { "tank.ani",      "tankturret.ani" },
        { "halftrack.ani", "halfturret.ani" },
        { "truck.ani",     ""               },
        { "",              ""               },
        { "boat.ani",      "jeepturret.ani" },
    };
    int32_t i;

    SetGameDir(AM2_ANI_DIR);
    for (i = 0; i < AM2_VEHICLE_ANIM_TABLES; i++) {
        if (kPaths[i][0][0] && g_vehicleAnims[i].count <= 0) {
            LoadSpriteFile(kPaths[i][0], &g_vehicleAnims[i], 0, 0x0A,
                           AM2_ANI_FLAGS_MEN);
            BuildVehicleMask(i);
        }
        if (kPaths[i][1][0] && g_turretAnims[i].count <= 0)
            LoadSpriteFile(kPaths[i][1], &g_turretAnims[i], 0, 0x0A,
                           AM2_ANI_FLAGS_MEN);
    }
}

void __cdecl FreeAnimTable(AM2_AnimTable *table)
{
    int32_t i;

    for (i = 0; i < table->count; i++) {
        AM2_AnimEntry *e = &table->entries[i];

        if (!e->anim || e->borrowed)
            continue;
        if (e->anim->cells)
            am2_free(e->anim->cells);
        /* Re-read through the table rather than reusing `e->anim`, as the
         * original does -- it reloads entries and the pointer for this one. */
        am2_free(table->entries[i].anim);
    }

    /* Outside the loop, so a table with no entries is still emptied. */
    am2_free(table->entries);
    table->entries = 0;
    table->count = 0;
}

void __cdecl FreeExplosionAnims(void)
{
    FreeAnimTable(g_explosionAnims);
}

void __cdecl FreeMissileAnims(void)
{
    FreeAnimTable(g_missileAnims);
}

void __cdecl FreeRoachAnims(void)
{
    FreeAnimTable(g_roachAnims);
}

void __cdecl FreeSoldierAnims(void)
{
    int32_t i;

    /* Nine calls written out in the original, one per table. rifleman is
     * first, so it is freed BEFORE the eight that borrow from it -- which is
     * safe only because a borrowed entry is skipped. */
    for (i = 0; i < AM2_SOLDIER_ANIM_TABLES; i++)
        FreeAnimTable(&g_soldierAnims[i]);
}

void __cdecl FreeVehicleAnims(void)
{
    int32_t i;

    for (i = 0; i < AM2_VEHICLE_ANIM_TABLES; i++) {
        FreeAnimTable(&g_vehicleAnims[i]);
        FreeAnimTable(&g_turretAnims[i]);
    }
}

/* 0x0040A2D0, six callers -- and this is what named AM2_AnimCell::hold.
 *
 * Three ways out and all of them 0: bit 0 of the row's flags clear, no
 * animation playing, or the cell index short of the last one. Only on the
 * LAST cell does it look at the clock, so the question is "is the animation
 * over", not "is it time for the next cell". Its one caller in the type-6
 * stepper sets OBJ_FLAG_OVERDUE when it answers yes.
 *
 * The clock comparison is UNSIGNED -- `jb`, not `jl` -- so a hold that pushed
 * the sum past 2^31 would read as still running. The clock is mission
 * milliseconds and a mission would have to last 24 days.
 *
 * VERIFIED BY READING, and say so with the measurement. It runs 446 times in
 * one live mission, so the path is warm -- and returning 1 where it returns 0
 * leaves `mission` at 281 pixels, inside the band clean runs give. Neither
 * this nor TileToXY beside it is discriminated by anything in the suite.
 */
int32_t __cdecl RowAnimFinished(const void *row)
{
    const uint8_t  *r = (const uint8_t *)row;
    const AM2_Anim *a;
    uint8_t         cell;

    if (!(*(const uint8_t *)r & MAPOBJ_FLAG_VISIBLE))
        return 0;

    a = *(AM2_Anim *const *)(r + ROW_OFF_ANIM_PLAYING);
    if (!a)
        return 0;

    cell = *(const uint8_t *)(r + ROW_OFF_CELL);
    if ((int32_t)cell < (int32_t)a->frames - 1)
        return 0;

    return *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
           >= (uint32_t)(a->cells[cell].hold
                         + *(const int32_t *)(r + ROW_OFF_STAMP_54));
}

/* Still original: 176 bytes of re-registration, not a store. */
typedef void (__cdecl *AM2_SetRowSpriteFn)(void *row, AM2_Sprite *sprite,
                                           void *desc);
#define orig_row_set_sprite ((AM2_SetRowSpriteFn)(uintptr_t)ADDR_ROW_SET_SPRITE)

/* 0x0040A310, three callers -- the type 2, 3 and 8 frame steppers, so this
 * runs once per unit per frame. Point the row's sprite at the animation cell
 * for the heading it is facing.
 *
 * THE CELL IS THE LAST OF ITS DIRECTION, not the first: the index is
 * `(dir + 1) * frames - 1`, and the original reaches it as
 * `[cells + eax*4 - 2]` with eax already `(dir + 1) * frames` -- the -2
 * landing on the cell's `sprite` field rather than its `hold`. Written out as
 * an ordinary index here; the arithmetic is the same.
 *
 * The two heading bytes are ADDED as bytes and the sum is masked to eight
 * bits before RoundTo8 sees it, so a bias that carries past 255 wraps rather
 * than saturating -- which is what an 8-bit heading should do.
 *
 * The swap is CONDITIONAL. ADDR_ROW_SET_SPRITE is called only when the sprite
 * differs from the one the row already holds, and that matters because it is
 * 176 bytes of re-registration rather than a store.
 *
 * IT WENT IN WITH ONE DEREFERENCE TOO FEW and the A/B caught it: 293,671 of
 * 786,432 pixels on `bootcamp`, against a budget of 500. ADDR_SPRITE_LIST is
 * a pointer TO the array and the first version indexed the global itself, so
 * every unit on the map took its sprite from whatever the pointer's own bytes
 * decoded to. Exactly the `obj -> table -> slot` shape CLAUDE.md warns about,
 * and the reason to spell it as air.cpp already did rather than write a fresh
 * cast. Bisected by disabling this one patch_replace, which put the run back
 * to its usual 22.
 *
 * Worth saying which check found it, because three of the last four functions
 * were invisible to the pixels: this one is not, and dramatically so. A wrong
 * sprite per unit per frame is the most visible thing reconstructed lately. */
void __cdecl RowFaceSprite(void *row)
{
    uint8_t         *r = (uint8_t *)row;
    const AM2_Anim  *a;
    uint8_t          heading;
    int32_t          dir, cell;
    AM2_Sprite      *sprite;

    if (!(*(const uint8_t *)r & MAPOBJ_FLAG_VISIBLE))
        return;

    a = *(AM2_Anim *const *)(r + ROW_OFF_ANIM_PLAYING);
    if (!a)
        return;

    heading = (uint8_t)(*(const uint8_t *)(r + ROW_OFF_HEADING_BIAS)
                        + *(const uint8_t *)(r + ROW_OFF_HEADING));
    dir     = RoundTo8(heading, a->directionBits) & 0xFF;
    cell    = (dir + 1) * a->frames - 1;
    sprite  = g_spriteList[a->cells[cell].sprite];

    if (*(AM2_Sprite *const *)(r + ROW_OFF_SPRITE) != sprite)
        orig_row_set_sprite(r, sprite, (void *)(uintptr_t)ADDR_MAP_DESC);
}

int anim_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_LOAD_ANIM_TABLE, (const void *)LoadAnimTable,
                        "LoadAnimTable", 1);
    rc |= patch_replace(ADDR_LOAD_EXPLOSION_ANIMS,
                        (const void *)LoadExplosionAnims,
                        "LoadExplosionAnims", 0);
    rc |= patch_replace(ADDR_LOAD_MISSILE_ANIMS,
                        (const void *)LoadMissileAnims, "LoadMissileAnims", 0);
    rc |= patch_replace(ADDR_LOAD_ROACH_ANIMS, (const void *)LoadRoachAnims,
                        "LoadRoachAnims", 0);
    rc |= patch_replace(ADDR_LOAD_SOLDIER_ANIMS,
                        (const void *)LoadSoldierAnims, "LoadSoldierAnims", 0);
    rc |= patch_replace(ADDR_LOAD_VEHICLE_ANIMS,
                        (const void *)LoadVehicleAnims, "LoadVehicleAnims", 0);
    rc |= patch_replace(ADDR_FREE_ANIM_TABLE, (const void *)FreeAnimTable,
                        "FreeAnimTable", 1);
    rc |= patch_replace(ADDR_ROW_ANIM_FINISHED, (const void *)RowAnimFinished,
                        "RowAnimFinished", 6);
    rc |= patch_replace(ADDR_ROW_FACE_SPRITE, (const void *)RowFaceSprite,
                        "RowFaceSprite", 3);
    rc |= patch_replace(ADDR_FREE_EXPLOSION_ANIMS,
                        (const void *)FreeExplosionAnims,
                        "FreeExplosionAnims", 0);
    rc |= patch_replace(ADDR_FREE_MISSILE_ANIMS, (const void *)FreeMissileAnims,
                        "FreeMissileAnims", 0);
    rc |= patch_replace(ADDR_FREE_ROACH_ANIMS, (const void *)FreeRoachAnims,
                        "FreeRoachAnims", 0);
    rc |= patch_replace(ADDR_FREE_SOLDIER_ANIMS,
                        (const void *)FreeSoldierAnims, "FreeSoldierAnims", 0);
    rc |= patch_replace(ADDR_FREE_VEHICLE_ANIMS,
                        (const void *)FreeVehicleAnims, "FreeVehicleAnims", 0);
    return rc;
}
