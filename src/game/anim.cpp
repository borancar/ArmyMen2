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
#include "dist.h"   /* Log2Mask */
#include "../inject/orig.h"
#include "../inject/patch.h"

#define g_spriteListN   (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_COUNT)

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
            int32_t cells = (int16_t)(a->facings * a->frames);

            n += sprintf(buf + n, " f=%d fa=%d bits=%d w4=%d w6=%d cells=%d",
                         a->frames, a->facings, a->facingBits, a->field4,
                         a->field6, cells);
            for (j = 0; j < cells && j < 6; j++)
                n += sprintf(buf + n, " (%d,%d)", a->cells[j].field0,
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
            a->facings = w;
            a->facingBits = Log2Mask(a->facings);
            orig_fread(&w, 2, 1, fp);
            a->field4 = w;
            a->zero9 = 0;
            orig_fread(&w, 2, 1, fp);
            a->field6 = w;

            /* Computed 16-bit and sign-extended afterwards, exactly as the
             * original does -- so a grid big enough to overflow an int16 would
             * come out negative and skip both the allocation and the loop. */
            cells = (int16_t)(a->facings * a->frames);
            if (cells > 0)
                a->cells = (AM2_AnimCell *)am2_malloc((size_t)cells * 4);

            for (j = 0; j < cells; j++) {
                orig_fread(&w, 2, 1, fp);
                a->cells[j].field0 = w;
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

int anim_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_LOAD_ANIM_TABLE, (const void *)LoadAnimTable,
                        "LoadAnimTable", 1);
    rc |= patch_replace(ADDR_FREE_ANIM_TABLE, (const void *)FreeAnimTable,
                        "FreeAnimTable", 1);
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
