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
extern "C" void __cdecl HudMessage(const char *text, uint8_t colour);
extern "C" void __cdecl PlayDynamicSound(const char *name, int32_t loop,
                                         int32_t unused, int16_t x, int16_t y,
                                         int32_t slot, int32_t pri,
                                         uint32_t owner);

/* ---- what stays in the original image --------------------------------- */
/* Only two, and that is the notable thing about this function: nineteen of
 * its twenty-one distinct callees are already reconstructed, so the arms call
 * them by NAME.  Writing them as orig_ seams -- which was the first draft --
 * would have been twenty-one lies at once, every one of them the failure
 * tools/checkseams.py exists for. */

typedef int32_t (__cdecl *am2_stricmp_fn)(const char *a, const char *b);

#define orig_stricmp      ((am2_stricmp_fn)(uintptr_t)ADDR_GAME_STRICMP)

/* The phrase table: entry 0 is the master switch, 1..39 are the cheats and
 * entry 40 is a sentinel the walk stops at rather than compares. */
#ifdef AM2_STANDALONE
/* Transcribed to C literals out of the blob at 0x00476704 (const char*[41]);
 * the blob's phrase strings then go dead and the sweep drops them. */
extern "C" const char *const am2_cheat_words[41] = {
    "when all else fails...", "santini", "warp 6", "jumpjets",
    "spidey senses tingling", "moleman", "doctor doctor", "ucla",
    "armageddon", "surprise party", "phoenix!", "cliche ending",
    "i have a rock", "village people", "no rocket launcher", "a better tomorrow",
    "acme discs", "gnomish inventions", "pooper scooper", "shrink wrap",
    "metal sheeting", "smorfs", "aluminum foil", "ruby ray",
    "roach spray", "fourth of july", "beautiful nikita", "geronimo!",
    "watchtower in the sky", "paper dolls", "rubber cement", "techno",
    "god of gamblers", "ninja arts", "suicide kings", "night of the walking dead",
    "fond memories", "veni vidi vinci", "i give up", "patton's speach",
    "xxx",
};
#endif


/* Every arm's reply colour is one byte read from one of ten globals. */
#define kColour(a)      (*(const uint8_t *)(uintptr_t)(a))

/* The group every item cheat passes KeyLookupTriple.  It is the same in all
 * nineteen, which is half of what makes them one helper. */
#define AM2_CHEAT_ITEM_GROUP  0x2D

/* The callback `doctor doctor` hands ForEachArmyObject, at 0x00417A90.  It
 * heals each of the army's objects by 100 with the leader as the source --
 * which it looks up per object rather than once, as the image does. */
static void __cdecl CheatHealOne(AM2_Object *obj)
{
    HealObject(obj, 0x64, LookupOwnerObj(g_defaultOwner));
}

/* The nineteen item cheats differ in exactly two values.  0x0041827C is not a
 * helper in the image -- it is the middle of the `rubber cement` arm, which
 * falls into it, and the other eighteen jump in: VC6 merged the identical
 * tails of nineteen arms that each wrote the call out.  The key is a call
 * written as an argument, which is why it is looked up after the other
 * arguments are pushed. */
#define CHEAT_GIVE_ITEM(kind, ammo)                                          \
    CreateWeapon(g_dirScratch, 0,               \
                 KeyLookupTriple(AM2_CHEAT_ITEM_GROUP, (kind), 0),           \
                 u->pos, 0, (ammo), 0, 0)

typedef int32_t (__cdecl *am2_rand_fn)(void);
#define orig_rand  ((am2_rand_fn)(uintptr_t)ADDR_GAME_RAND)

/* 39 phrases sit at kCheatWords[1..39]; [0] is the master and [40] is a
 * sentinel the walk stops at rather than compares. */
#define AM2_CHEAT_COUNT  39

/* `phoenix!` and `cliche ending` swap the leader's weapon for a flamethrower
 * and back.  They are NOT one function twice: the two differ in the weapon
 * kind, in which template goes into OBJ_OFF_SUBRECORD, and in whether that
 * write happens before or after the swap.  Written out in each arm, as the
 * image has them; the no-old-weapon exit is one block both arms reach. */
#define CHEAT_NEW_WEAPON(kind)                                               \
    (CreateWeapon(g_dirScratch, 0,   \
                 KeyLookupTriple(AM2_CHEAT_ITEM_GROUP, (kind), 0),           \
                 g_zeroPoint, 4, -1, 0, 0))

/* 0x00417B80. */
void __cdecl CheatLine(const char *line)
{
    AM2_Object *u;
    int32_t  n;
    AM2_Point where;
    AM2_Object *old;
    AM2_Object *fresh;

    if (line == 0)
        return;

    /* The master switch is compared before the table is walked. */
    if (orig_stricmp(line, g_cheatWords[0]) == 0) {
        int32_t army = ((AM2_Comm *)(void *)g_commObject)->CommArmyOfSlot((int32_t)g_defaultOwner);

        /* The reply's colour is byte 1 of the army's own object-table record,
         * NOT a colour table of its own -- 0x004F9ACD is
         * ADDR_OBJ_TABLE_RECORDS + 1 and the shift by 8 is the record size. */
        HudMessage("Cheat!!!",
                   *(g_objTableRecords
                              + army * AM2_OBJ_TABLE_REC_SIZE + 1));
        g_cheatEnabled = 1;
        return;
    }

    if (g_cheatEnabled == 0)
        return;

    u = LookupOwnerObj(g_defaultOwner);
    if (u == 0)
        return;

    for (n = 1; n < AM2_CHEAT_COUNT + 1; n++)
        if (orig_stricmp(line, g_cheatWords[n]) == 0)
            break;

again:
    switch (n) {
    case 1:                                     /* santini */
        HudMessage("I am the Juggernaut!", kColour(((uintptr_t)&g_colourLagMid)));
        g_invulnerable = 1;
        return;
    case 2:                                     /* warp 6 */
        HudMessage("Aye aye Captain!", kColour(((uintptr_t)&g_colourBlue)));
        g_cheatLevelSelect = 1;
        return;
    case 3:                                     /* jumpjets */
        HudMessage("I can fly!", kColour(((uintptr_t)&g_colourBlue)));
        PlayDynamicSound("portal2_8bit.wav", 0, 0, 0, 0, 0x10, 3, 0);

        /* x adds the two PACKED dwords and keeps the low word; y adds the
         * two high words as words. The view's local is a number win: which
         * of the two loads VC6 gives edx follows its symbol table, so the
         * plain `g_cursorPoint + g_viewRect.left` matches or not depending
         * on what the headers declare (docs/numberwins.md). */
        {
            int32_t vx = g_viewRect.left;

            where.x = (int16_t)(vx + g_cursorPoint);
        }
        where.y = (int16_t)(((const int16_t *)&g_cursorPoint)[1]
                            + *(const int16_t *)&g_viewRect.top);
        if (!PointInRect(&g_secondRect, &where))
            return;

        CreateExplosion(where.x, where.y, 0x85, 0, 0, 0, 0, 0, 0, 0);
        u->pos = where;
        ObjTileChanged(u, 0, 0);
        return;
    case 4:                                     /* spidey senses tingling */
        HudMessage("I see everything!", kColour(((uintptr_t)&g_whiteInk)));
        g_fogOfWar = 0;
        ToggleFogOfWar();
        return;
    case 5:                                     /* moleman */
        HudMessage("I bury my head 'neath the sand.",
                   kColour(((uintptr_t)&g_backgroundColour)));
        g_fogOfWar = 1;
        ToggleFogOfWar();
        return;
    case 6:                                     /* doctor doctor */
        HudMessage("Avoid the agony...", kColour(((uintptr_t)&g_whiteInk)));
        ForEachArmyObject((int32_t)g_defaultOwner, CheatHealOne);
        return;
    case 7:                                     /* ucla -- reply only */
        HudMessage("Goooooo Bruins!", kColour(((uintptr_t)&g_colourBlue)));
        return;
    case 8:                                     /* armageddon */
        HudMessage("Duck and cover!", kColour(((uintptr_t)&g_hudColour)));
        SpawnRandomBarrage();
        return;
    case 9:                                     /* surprise party */
        HudMessage("Holy smokes!", kColour(((uintptr_t)&g_colourStale)));
        PortalSpawn();
        return;
    case 10:                                     /* phoenix! */
        if (g_flameOn != 0)
            return;
        HudMessage("Flame On!", kColour(((uintptr_t)&g_hudColour)));
        g_flameOn = 1;
        (*(int32_t *)&g_flameNextMs) = 0;
        old = WeaponByUid(u->inventory[0]);
        fresh = CHEAT_NEW_WEAPON(3);
        if (old) {
            if (fresh == 0)
                return;
            old->flags |= 2;
            fresh->owner = u->owner;
            u->inventory[0] = fresh->uid;
            SoldierKindForWeapon(u, fresh->typeRec->kind);
            SendTrooperSetWeapon(u, (int32_t)fresh->uid, 0);
            SetFieldInAll(&u->rowSet,
                          g_flameRecord);
            RowUpdate((AM2_MapRow *)(u->rowSet.rows), 1,
                      g_mapDesc);
            return;
        }
        goto no_old;
    case 11:                                    /* cliche ending */
        if (g_flameOn == 0)
            return;
        HudMessage("Flame Off!", kColour(((uintptr_t)&g_hudColour)));
        g_flameOn = 0;
        (*(int32_t *)&g_flameNextMs) = 0;
        SetFieldInAll(&u->rowSet, (void *)u->tableRec2);
        RowUpdate((AM2_MapRow *)(u->rowSet.rows), 1,
                  g_mapDesc);
        old = WeaponByUid(u->inventory[0]);
        fresh = CHEAT_NEW_WEAPON(9);
        if (old) {
            if (fresh == 0)
                return;
            old->flags |= 2;
            fresh->owner = u->owner;
            u->inventory[0] = fresh->uid;
            SoldierKindForWeapon(u, fresh->typeRec->kind);
            SendTrooperSetWeapon(u, (int32_t)fresh->uid, 0);
            return;
        }
    no_old:
        /* WHICH OBJECT GETS THE FLAG DEPENDS ON WHETHER THERE WAS AN OLD
         * ONE.  With no old weapon the image flags the NEW one and stops. */
        if (fresh)
            fresh->flags |= 2;
        return;

    /* The nineteen item cheats.  Reply, colour, kind, ammo -- nothing else
     * differs between them, which is what reading them side by side showed
     * and reading them in sequence did not. */
    case 12: HudMessage("Hear me roar!", kColour(((uintptr_t)&g_colourStale)));
             CHEAT_GIVE_ITEM(0x02, -1); return;
    case 13: HudMessage("Flame forever!", kColour(((uintptr_t)&g_hudColour)));
             CHEAT_GIVE_ITEM(0x03, -1); return;
    case 14: HudMessage("Sigh...", kColour(((uintptr_t)&g_colourLagMid)));
             CHEAT_GIVE_ITEM(0x04, -1); return;
    case 15: HudMessage("May your gun never jam",
                        kColour(((uintptr_t)&g_hiliteColour)));
             CHEAT_GIVE_ITEM(0x1D, -1); return;
    case 16: HudMessage("Follow the blueprints!", kColour(((uintptr_t)&g_colourBlue)));
             CHEAT_GIVE_ITEM(0x0B, 0x2C); return;
    case 17: HudMessage("Tinker away with these...",
                        kColour(((uintptr_t)&g_colourNoMap)));
             CHEAT_GIVE_ITEM(0x0C, 0x0D); return;
    case 18: HudMessage("To clean up the mess.", kColour(((uintptr_t)&g_colourNoMap)));
             CHEAT_GIVE_ITEM(0x14, -1); return;
    case 19: HudMessage("Feel small...", kColour(((uintptr_t)g_kind7Names)));
             CHEAT_GIVE_ITEM(0x24, 0x0C); return;
    case 20: HudMessage("Not!", kColour(((uintptr_t)&g_colourStale)));
             CHEAT_GIVE_ITEM(0x26, 0x0C); return;
    case 21: HudMessage("La la la la la la!", kColour(((uintptr_t)&g_colourBlue)));
             CHEAT_GIVE_ITEM(0x25, 0x0C); return;
    case 22: HudMessage("I kid you not.", kColour(((uintptr_t)&g_colourStale)));
             CHEAT_GIVE_ITEM(0x1C, -1); return;
    case 23: HudMessage("To see you better with my dear",
                        kColour(((uintptr_t)&g_whiteInk)));
             CHEAT_GIVE_ITEM(0x27, -1); return;
    case 24: HudMessage("Begone all ye vermin!", kColour(((uintptr_t)&g_colourLagMid)));
             CHEAT_GIVE_ITEM(0x28, -1); return;
    case 25: HudMessage("Rockets bursting in air",
                        kColour(((uintptr_t)&g_hudColour)));
             CHEAT_GIVE_ITEM(0x2A, -1); return;
    case 26: HudMessage("For my darling professional...",
                        kColour(((uintptr_t)&g_colourBlue)));
             CHEAT_GIVE_ITEM(0x1E, -1); return;
    case 27: HudMessage("Death from above", kColour(((uintptr_t)&g_whiteInk)));
             CHEAT_GIVE_ITEM(0x18, 0x0C); return;
    case 28: HudMessage("Take heart my comrade.",
                        kColour(((uintptr_t)&g_hiliteColour)));
             CHEAT_GIVE_ITEM(0x1A, 3); return;
    case 29: HudMessage("Paper cuts.", kColour(((uintptr_t)&g_hiliteColour)));
             CHEAT_GIVE_ITEM(0x19, 3); return;
    case 30: HudMessage("And super glue too.", kColour(((uintptr_t)&g_whiteInk)));
             CHEAT_GIVE_ITEM(0x17, -1); return;

    case 31:                                    /* techno */
        HudMessage("Rocks.", kColour(((uintptr_t)&g_colourStale)));
        PlayDynamicSound("portal1_8bit.wav", 1, 0, 0, 0, 0x10, 2,
                         u->uid);
        return;

    case 32:                                    /* god of gamblers */
        /* Prints, then re-enters the table on a random index.  The image's
         * guard is `cmp edx, 0x26; jbe` and the modulus is exactly the table
         * size, so it can never fail -- an unconditional branch spelled as a
         * conditional one.  It can pick itself. */
        HudMessage("No luck whatsover.", kColour(((uintptr_t)&g_colourLagMid)));
        n = orig_rand() % AM2_CHEAT_COUNT + 1;
        goto again;

    case 33:                                    /* ninja arts */
        HudMessage("Stealth mode on!", kColour(((uintptr_t)g_colourDarkBlue)));
        SetFieldInAll(&u->rowSet,
                      g_rowLutDoubles);
        RowUpdate((AM2_MapRow *)(u->rowSet.rows), 1,
                  g_mapDesc);
        return;
    case 34:                                    /* suicide kings */
        HudMessage("And you're the king!", kColour(((uintptr_t)&g_colourLagMid)));
        u->health = 1;
        g_invulnerable = 0;
        DamageObject(u, 0x64, 2, (int32_t)u->uid, 0, 0);
        return;
    case 35:                                    /* night of the walking dead */
        HudMessage("Brains, Brains!", kColour(((uintptr_t)g_colourDarkBlue)));
        Type2ActionAll();
        return;
    case 36:                                    /* fond memories */
        HudMessage("Memories...", kColour(((uintptr_t)&g_colourLagMid)));
        g_movieCount = 3;
        return;
    case 37:                                    /* veni vidi vinci */
        HudMessage("Victory is belongs to Caesar!",
                   kColour(((uintptr_t)&g_hiliteColour)));
        ScriptRunLine("trigger greenwins");
        return;
    case 38:                                    /* i give up */
        HudMessage("Why so glum?", kColour(((uintptr_t)g_kind7Names)));
        ScriptRunLine("trigger tanwins");
        return;
    case 39:                                    /* patton's speach */
        HudMessage("You've inspired your troops!",
                   kColour(((uintptr_t)&g_hiliteColour)));
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
    int rc = 0;

    rc |= patch_replace(ADDR_CHEAT_HEAL_ONE, (const void *)CheatHealOne,
                        "CheatHealOne", 1);
    rc |= patch_replace(ADDR_CHEAT_ENTRY, (const void *)CheatLine,
                        "CheatLine", 1);
    return rc;
}
