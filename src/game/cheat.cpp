/* cheat.cpp -- see cheat.h. */
#include <stdint.h>

#include "cheat.h"
#include "air.h"
#include "army.h"
#include "armymsg.h"
#include "item.h"
#include "maprow.h"
#include "misc.h"
#include "packkey.h"
#include "rect.h"
#include "script.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* Two functions this needs live behind win32 headers, so they are declared
 * here rather than included: cheat.cpp is on the flat side of the split and
 * both of those headers reach windows.h.
 *
 * HudMessage's second argument is an int32 the function uses as
 * (uint8_t)colour -- which is why every arm loads a BYTE from a colour global
 * and never sets the upper bits of the register it loads into. */
extern "C" void __cdecl HudMessage(const char *text, int32_t colour);
extern "C" void __cdecl PlayDynamicSound(const char *name, int32_t loop,
                                         int32_t unused, int32_t x, int32_t y,
                                         int32_t slot, int32_t pri,
                                         uint32_t owner);

/* ---- what stays in the original image --------------------------------- */
/* Only two, and that is the notable thing about this function: nineteen of
 * its twenty-one distinct callees are already reconstructed, so the arms call
 * them by NAME.  Writing them as orig_ seams -- which was the first draft --
 * would have been twenty-one lies at once, every one of them the failure
 * tools/checkseams.py exists for. */

typedef int32_t (__cdecl *am2_stricmp_fn)(const char *a, const char *b);
typedef void   *(__cdecl *am2_make_weapon_fn)(const char *name, int32_t army,
                                              int32_t kind, uint32_t where,
                                              int32_t a, int32_t b, int32_t c,
                                              uint32_t uid);

#define orig_stricmp      ((am2_stricmp_fn)(uintptr_t)ADDR_GAME_STRICMP)
#define orig_make_weapon  ((am2_make_weapon_fn)(uintptr_t)ADDR_CREATE_WEAPON)

/* The phrase table: entry 0 is the master switch, 1..39 are the cheats and
 * entry 40 is a sentinel the walk stops at rather than compares. */
#define kCheatWords  ((const char *const *)(uintptr_t)ADDR_CHEAT_WORDS)

#define g_cheatEnabled  (*(int32_t *)(uintptr_t)ADDR_CHEAT_ENABLED)
#define g_defaultOwner  (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_commObject   (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_invulnerable  (*(int32_t *)(uintptr_t)ADDR_CHEAT_INVULNERABLE)
#define g_cheatLevelSelect (*(int32_t *)(uintptr_t)ADDR_CHEAT_LEVEL_SELECT)
#define g_fogOfWar      (*(int32_t *)(uintptr_t)ADDR_FOG_OF_WAR)
#define g_flameOn       (*(int32_t *)(uintptr_t)ADDR_FLAME_ON)
#define g_flameNextMs   (*(int32_t *)(uintptr_t)ADDR_FLAME_NEXT_MS)
#define g_movieCount      (*(int32_t *)(uintptr_t)ADDR_MOVIE_COUNT)
#define g_viewRect   ((AM2_Rect *)(uintptr_t)ADDR_VIEW_ORIGIN_X)
#define g_cursorPoint  (*(int32_t *)(uintptr_t)ADDR_CURSOR_POINT)
#define g_secondRect    (*(const AM2_Rect *)(uintptr_t)ADDR_SECOND_RECT)

/* Every arm's reply colour is one byte read from one of ten globals. */
#define kColour(a)      ((int32_t)*(const uint8_t *)(uintptr_t)(a))

/* The group every item cheat passes KeyLookupTriple.  It is the same in all
 * nineteen, which is half of what makes them one helper. */
#define AM2_CHEAT_ITEM_GROUP  0x2D

/* The callback `doctor doctor` hands ForEachArmyObject, at 0x00417A90.  It
 * heals each of the army's objects by 100 with the leader as the source --
 * which it looks up per object rather than once, as the image does. */
static void __cdecl CheatHealOne(void *obj)
{
    HealObject(obj, 0x64, LookupOwnerObj(g_defaultOwner));
}

/* The nineteen item cheats differ in exactly two values.  0x0041827C is not a
 * helper in the image -- it is the middle of the `rubber cement` arm, which
 * falls into it, and the other eighteen jump in.  Written as a helper here
 * because that is what it is; see the note above ADDR_CHEAT_ENTRY for why the
 * distinction cost a correction. */
static void CheatGiveItem(void *unit, int32_t kind, int32_t ammo)
{
    int32_t key = KeyLookupTriple(AM2_CHEAT_ITEM_GROUP, kind, 0);

    orig_make_weapon((const char *)(uintptr_t)ADDR_DIR_SCRATCH, 0, key,
                     *(const uint32_t *)((const uint8_t *)unit + OBJ_OFF_POS),
                     0, ammo, 0, 0);
}

typedef int32_t (__cdecl *am2_rand_fn)(void);
#define orig_rand  ((am2_rand_fn)(uintptr_t)ADDR_GAME_RAND)

/* 39 phrases sit at kCheatWords[1..39]; [0] is the master and [40] is a
 * sentinel the walk stops at rather than compares. */
#define AM2_CHEAT_COUNT  39

/* `phoenix!` and `cliche ending` swap the leader's weapon for a flamethrower
 * and back.  They are NOT one function twice: the two differ in the weapon
 * kind, in which template goes into OBJ_OFF_SUBRECORD, and in whether that
 * write happens before or after the swap.  Diffed rather than merged. */
static void CheatSwapWeapon(void *unit, int32_t kind, void *subrecord,
                            int32_t subrecordFirst)
{
    uint8_t *u = (uint8_t *)unit;
    void    *old;
    uint8_t *fresh;
    int32_t  key;

    if (subrecordFirst) {
        SetFieldInAll(u + OBJ_OFF_SUBRECORD, subrecord);
        RowUpdate(*(void **)(u + OBJ_OFF_ROWS), 1,
                  (void *)(uintptr_t)ADDR_MAP_DESC);
    }

    old = WeaponByUid(*(const int32_t *)(u + OBJ_OFF_WEAPON_UID));
    key = KeyLookupTriple(AM2_CHEAT_ITEM_GROUP, kind, 0);
    fresh = (uint8_t *)orig_make_weapon(
                (const char *)(uintptr_t)ADDR_DIR_SCRATCH, 0, key,
                *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT, 4, -1, 0, 0);

    /* WHICH OBJECT GETS THE FLAG DEPENDS ON WHETHER THERE WAS AN OLD ONE.
     * With no old weapon the image flags the NEW one and stops; with one, it
     * flags the OLD one and goes on to the swap.  Two different objects. */
    if (old == 0) {
        if (fresh)
            *(int32_t *)(fresh + OBJ_OFF_FLAGS) |= 2;
        return;
    }
    if (fresh == 0)
        return;
    *(int32_t *)((uint8_t *)old + OBJ_OFF_FLAGS) |= 2;

    fresh[OBJ_OFF_ARMY] = u[OBJ_OFF_ARMY];
    *(int32_t *)(u + OBJ_OFF_WEAPON_UID) = *(const int32_t *)(fresh + OBJ_OFF_UID);
    SoldierKindForWeapon(unit, **(int32_t **)(fresh + OBJ_OFF_FIELD_C0));
    SendTrooperSetWeapon(unit, *(const int32_t *)(fresh + OBJ_OFF_UID), 0);

    if (!subrecordFirst) {
        SetFieldInAll(u + OBJ_OFF_SUBRECORD, subrecord);
        RowUpdate(*(void **)(u + OBJ_OFF_ROWS), 1,
                  (void *)(uintptr_t)ADDR_MAP_DESC);
    }
}

/* 0x00417B80. */
void __cdecl CheatLine(const char *line)
{
    uint8_t *u;
    int32_t  n;

    if (line == 0)
        return;

    /* The master switch is compared before the table is walked. */
    if (orig_stricmp(line, kCheatWords[0]) == 0) {
        int32_t army = CommArmyOfSlot((void *)g_commObject, (int32_t)g_defaultOwner);

        /* The reply's colour is byte 1 of the army's own object-table record,
         * NOT a colour table of its own -- 0x004F9ACD is
         * ADDR_OBJ_TABLE_RECORDS + 1 and the shift by 8 is the record size. */
        HudMessage("Cheat!!!",
                   (int32_t)*((const uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS
                              + army * AM2_OBJ_TABLE_REC_SIZE + 1));
        g_cheatEnabled = 1;
        return;
    }

    if (g_cheatEnabled == 0)
        return;

    u = (uint8_t *)LookupOwnerObj(g_defaultOwner);
    if (u == 0)
        return;

    for (n = 0; n < AM2_CHEAT_COUNT; n++)
        if (orig_stricmp(line, kCheatWords[n + 1]) == 0)
            break;

again:
    switch (n) {
    case 0:                                     /* santini */
        HudMessage("I am the Juggernaut!", kColour(ADDR_COLOUR_LAG_MID));
        g_invulnerable = 1;
        return;
    case 1:                                     /* warp 6 */
        HudMessage("Aye aye Captain!", kColour(ADDR_COLOUR_BLUE));
        g_cheatLevelSelect = 1;
        return;
    case 2: {                                   /* jumpjets */
        AM2_Point where;

        HudMessage("I can fly!", kColour(ADDR_COLOUR_BLUE));
        PlayDynamicSound("portal2_8bit.wav", 0, 0, 0, 0, 0x10, 3, 0);

        /* The image adds the two PACKED dwords and keeps the low word for x,
         * then adds the two high words for y. Same answer as adding the
         * halves, and written as the halves here. */
        where.x = (int16_t)((int16_t)g_viewRect->left
                            + (int16_t)g_cursorPoint);
        where.y = (int16_t)((int16_t)g_viewRect->top
                            + (int16_t)(g_cursorPoint >> 16));
        if (!PointInRect(&g_secondRect, &where))
            return;

        CreateExplosion(where.x, where.y, 0x85, 0, 0, 0, 0, 0, 0, 0);
        *(uint32_t *)(u + OBJ_OFF_POS) = *(const uint32_t *)&where;
        ObjTileChanged(u, 0, 0);
        return;
    }
    case 3:                                     /* spidey senses tingling */
        HudMessage("I see everything!", kColour(ADDR_COLOUR_WHITE));
        g_fogOfWar = 0;
        ToggleFogOfWar();
        return;
    case 4:                                     /* moleman */
        HudMessage("I bury my head 'neath the sand.",
                   kColour(ADDR_BACKGROUND_COLOUR));
        g_fogOfWar = 1;
        ToggleFogOfWar();
        return;
    case 5:                                     /* doctor doctor */
        HudMessage("Avoid the agony...", kColour(ADDR_COLOUR_WHITE));
        ForEachArmyObject((int32_t)g_defaultOwner, CheatHealOne);
        return;
    case 6:                                     /* ucla -- reply only */
        HudMessage("Goooooo Bruins!", kColour(ADDR_COLOUR_BLUE));
        return;
    case 7:                                     /* armageddon */
        HudMessage("Duck and cover!", kColour(ADDR_HUD_MESSAGE_COLOUR));
        SpawnRandomBarrage();
        return;
    case 8:                                     /* surprise party */
        HudMessage("Holy smokes!", kColour(ADDR_COLOUR_STALE));
        PortalSpawn();
        return;
    case 9:                                     /* phoenix! */
        if (g_flameOn != 0)
            return;
        HudMessage("Flame On!", kColour(ADDR_HUD_MESSAGE_COLOUR));
        g_flameOn = 1;
        g_flameNextMs = 0;
        CheatSwapWeapon(u, 3, (void *)(uintptr_t)ADDR_FLAME_RECORD, 0);
        return;
    case 10:                                    /* cliche ending */
        if (g_flameOn == 0)
            return;
        HudMessage("Flame Off!", kColour(ADDR_HUD_MESSAGE_COLOUR));
        g_flameOn = 0;
        g_flameNextMs = 0;
        CheatSwapWeapon(u, 9,
                        *(void **)(u + OBJ_OFF_TABLE_REC_KIND), 1);
        return;

    /* The nineteen item cheats.  Reply, colour, kind, ammo -- nothing else
     * differs between them, which is what reading them side by side showed
     * and reading them in sequence did not. */
    case 11: HudMessage("Hear me roar!", kColour(ADDR_COLOUR_STALE));
             CheatGiveItem(u, 0x02, -1); return;
    case 12: HudMessage("Flame forever!", kColour(ADDR_HUD_MESSAGE_COLOUR));
             CheatGiveItem(u, 0x03, -1); return;
    case 13: HudMessage("Sigh...", kColour(ADDR_COLOUR_LAG_MID));
             CheatGiveItem(u, 0x04, -1); return;
    case 14: HudMessage("May your gun never jam",
                        kColour(ADDR_VIEW_RECT_COLOUR));
             CheatGiveItem(u, 0x1D, -1); return;
    case 15: HudMessage("Follow the blueprints!", kColour(ADDR_COLOUR_BLUE));
             CheatGiveItem(u, 0x0B, 0x2C); return;
    case 16: HudMessage("Tinker away with these...",
                        kColour(ADDR_COLOUR_NO_MAP));
             CheatGiveItem(u, 0x0C, 0x0D); return;
    case 17: HudMessage("To clean up the mess.", kColour(ADDR_COLOUR_NO_MAP));
             CheatGiveItem(u, 0x14, -1); return;
    case 18: HudMessage("Feel small...", kColour(ADDR_KIND7_NAMES));
             CheatGiveItem(u, 0x24, 0x0C); return;
    case 19: HudMessage("Not!", kColour(ADDR_COLOUR_STALE));
             CheatGiveItem(u, 0x26, 0x0C); return;
    case 20: HudMessage("La la la la la la!", kColour(ADDR_COLOUR_BLUE));
             CheatGiveItem(u, 0x25, 0x0C); return;
    case 21: HudMessage("I kid you not.", kColour(ADDR_COLOUR_STALE));
             CheatGiveItem(u, 0x1C, -1); return;
    case 22: HudMessage("To see you better with my dear",
                        kColour(ADDR_COLOUR_WHITE));
             CheatGiveItem(u, 0x27, -1); return;
    case 23: HudMessage("Begone all ye vermin!", kColour(ADDR_COLOUR_LAG_MID));
             CheatGiveItem(u, 0x28, -1); return;
    case 24: HudMessage("Rockets bursting in air",
                        kColour(ADDR_HUD_MESSAGE_COLOUR));
             CheatGiveItem(u, 0x2A, -1); return;
    case 25: HudMessage("For my darling professional...",
                        kColour(ADDR_COLOUR_BLUE));
             CheatGiveItem(u, 0x1E, -1); return;
    case 26: HudMessage("Death from above", kColour(ADDR_COLOUR_WHITE));
             CheatGiveItem(u, 0x18, 0x0C); return;
    case 27: HudMessage("Take heart my comrade.",
                        kColour(ADDR_VIEW_RECT_COLOUR));
             CheatGiveItem(u, 0x1A, 3); return;
    case 28: HudMessage("Paper cuts.", kColour(ADDR_VIEW_RECT_COLOUR));
             CheatGiveItem(u, 0x19, 3); return;
    case 29: HudMessage("And super glue too.", kColour(ADDR_COLOUR_WHITE));
             CheatGiveItem(u, 0x17, -1); return;

    case 30:                                    /* techno */
        HudMessage("Rocks.", kColour(ADDR_COLOUR_STALE));
        PlayDynamicSound("portal1_8bit.wav", 1, 0, 0, 0, 0x10, 2,
                         *(const uint32_t *)(u + OBJ_OFF_UID));
        return;

    case 31:                                    /* god of gamblers */
        /* Prints, then re-enters the table on a random index.  The image's
         * guard is `cmp edx, 0x26; jbe` and the modulus is exactly the table
         * size, so it can never fail -- an unconditional branch spelled as a
         * conditional one.  It can pick itself. */
        HudMessage("No luck whatsover.", kColour(ADDR_COLOUR_LAG_MID));
        n = orig_rand() % AM2_CHEAT_COUNT;
        goto again;

    case 32:                                    /* ninja arts */
        HudMessage("Stealth mode on!", kColour(ADDR_COLOUR_DARK_BLUE));
        SetFieldInAll(u + OBJ_OFF_SUBRECORD,
                      (void *)(uintptr_t)ADDR_ROW_LUT_DOUBLES);
        RowUpdate(*(void **)(u + OBJ_OFF_ROWS), 1,
                  (void *)(uintptr_t)ADDR_MAP_DESC);
        return;
    case 33:                                    /* suicide kings */
        HudMessage("And you're the king!", kColour(ADDR_COLOUR_LAG_MID));
        *(uint16_t *)(u + OBJ_OFF_COUNT62) = 1;
        g_invulnerable = 0;
        DamageObject(u, 0x64, 2, *(const int32_t *)(u + OBJ_OFF_UID), 0, 0);
        return;
    case 34:                                    /* night of the walking dead */
        HudMessage("Brains, Brains!", kColour(ADDR_COLOUR_DARK_BLUE));
        Type2ActionAll();
        return;
    case 35:                                    /* fond memories */
        HudMessage("Memories...", kColour(ADDR_COLOUR_LAG_MID));
        g_movieCount = 3;
        return;
    case 36:                                    /* veni vidi vinci */
        HudMessage("Victory is belongs to Caesar!",
                   kColour(ADDR_VIEW_RECT_COLOUR));
        ScriptRunLine("trigger greenwins");
        return;
    case 37:                                    /* i give up */
        HudMessage("Why so glum?", kColour(ADDR_KIND7_NAMES));
        ScriptRunLine("trigger tanwins");
        return;
    case 38:                                    /* patton's speach */
        HudMessage("You've inspired your troops!",
                   kColour(ADDR_VIEW_RECT_COLOUR));
        AwardOwnArmyXp();
        return;

    default:
        /* Not a cheat at all -- hand it to the script runner, which is how
         * `trigger greenwins` typed at the console works without being an arm
         * and why ScriptRunLine is reachable from here in the first place. */
        ScriptRunLine(line);
        return;
    }
}

int cheat_install(void)
{
    return patch_replace(ADDR_CHEAT_ENTRY, (const void *)CheatLine,
                         "CheatLine", 1);
}
