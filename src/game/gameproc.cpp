/* gameproc.cpp -- see gameproc.h. */
#include <stdint.h>
#include <string.h>

#include "gameproc.h"
#include "pmf.h"
#include "packkey.h"  /* KeyFieldA, KeyFieldB -- reconstructed */
#include "gamedir.h"  /* SetGameDir -- the chdir into the save directory */
#include "savetag.h"
#include "defparse.h"  /* DefFreeTables -- reconstructed */
#include "definfo.h"   /* DefParseNumber, and the .aai vocabulary */
#include "map.h"
#include "pad.h"
#include "air.h"
#include "army.h"     /* SetMaxHealth -- reconstructed */
#include "item.h"
#include "event.h"
#include "script.h"
#include "objscript.h"
#include "objflag.h"   /* ObjFlagSet0 -- the row-visible bit */
#include "objtype.h"   /* ObjInitCommon -- reconstructed */
#include "maprow.h"    /* BuildRowSet, SetAnimFrame -- reconstructed */

/* FreeSpriteRegistry is reconstructed, in win32/sprite.cpp with the rest of
 * the sprite lifetime. It is declared here rather than by including that
 * header for the reason script.cpp declares PreloadSprite: gameproc.cpp is on
 * the flat side of the split and must name no Win32 or COM type, and
 * AM2_Sprite has an LPDIRECTDRAWSURFACE in it. This one takes no arguments and
 * returns nothing, so the declaration needs no types at all. */
void __cdecl FreeSpriteRegistry(void);
#include "image.h"
#include "../inject/orig.h"
#include "misc.h"      /* InitPtrList, ClearPtrListAlias -- reconstructed */

/* The CRT's atexit, above the CRT line and not on the patch list. */
typedef int32_t (__cdecl *AM2_AtExitFn)(void (__cdecl *)(void));
#define orig_atexit (*(AM2_AtExitFn)((void *)(uintptr_t)(ADDR_CRT_ATEXIT)))
#include "../inject/patch.h"
#include "misc.h"      /* ReturnOne */
#include "crt.h"       /* am2_realloc, am2_free -- the game's own */
#include "region.h"   /* AiPatrolStep -- reconstructed, and this forwards to it */

#ifdef AM2_STANDALONE
/* am2_key_defaults -- 0x0048AE80, the 21 default key scancodes (uint8, three
 * trailing zeros pad to 24) the controls dialog rebuilds ADDR_KEY_BINDINGS
 * from. A read-only source, so const; the bindings themselves stay a macro. */
extern "C" const uint8_t am2_key_defaults[24] = {
    17, 31, 30, 32, 16, 18, 46, 33, 56, 57, 42, 15, 41, 45, 19, 34,
    20, 21, 35, 14, 59, 0, 0, 0,
};
/* The frame tick interval at 0x00485104 (1000 ms base), read here and in
 * item.cpp; runtime-written, non-const, initial byte-checked. Plain linkage to
 * match standalone.h's shared declaration. */
int32_t am2_tick_interval_ms = 1000;
/* The current key bindings at 0x004854BC (21 {scancode, flag} uint16 pairs,
 * walked as bytes to ADDR_KEY_BINDINGS_END): rebindable, so runtime-written;
 * read across misc/gameproc/widget. Initial bytes byte-checked. */
uint8_t am2_key_bindings[42] = {
    0x11, 0xC8, 0x1F, 0xD0, 0x1E, 0xCB, 0x20, 0xCD, 0x10, 0x00, 0x12, 0x00, 0x1F, 0x00,
    0x21, 0x00, 0x38, 0x00, 0x39, 0x00, 0x2A, 0x00, 0x0F, 0x00, 0x29, 0x00, 0x2D, 0x00,
    0x13, 0x00, 0x22, 0x00, 0x14, 0x00, 0x15, 0x00, 0x23, 0x00, 0x0E, 0x00, 0x3B, 0x00,
};
#endif

/* SaveDefaultCof's three. The first two are spelled as army.cpp spells them,
 * so checkglobals sees one name per address and not two. */

/* The original's frame is 0x80 and holds nothing but the two stashes, at
 * +0 and +0x40 (the three volumes live in registers), so each has 64 bytes
 * and neither is bounded against the string it copies. Same size here, and
 * no guard added: a guard would be behaviour this build lacks. */
#define AM2_GAMEPROC_STASH 0x40

int32_t __cdecl SaveGameProcSection(am2_FILE *fp)
{
    /* The LENGTH, not a tag. SaveGame has already written 0x06660666. */
    WriteSaveTag(fp, AM2_GAMEPROC_SAVE_SIZE);
    orig_fwrite(g_gameprocBlock, AM2_GAMEPROC_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadGameProcSection(am2_FILE *fp)
{
    char    stashA[AM2_GAMEPROC_STASH];   /* the string at +0x120 */
    char    stashB[AM2_GAMEPROC_STASH];   /* the string at the block start */
    int32_t volStream, volVoice, volZero;

    /* Everything that must outlive the read is copied out first, before the
     * tag is even checked -- which costs nothing when the check fails. */
    strcpy(stashA, g_gameprocStrB);
    strcpy(stashB, g_gameprocBlock);
    volZero   = g_volumeAtZero;
    volStream = g_streamVolume;
    volVoice  = g_voiceVolume;

    if (!CheckSaveTag(fp, AM2_GAMEPROC_SAVE_SIZE,
                      "C:\\ArmyMen2\\source\\gameproc.cpp", 0x8CD))
        return 0;

    orig_fread(g_gameprocBlock, AM2_GAMEPROC_SAVE_SIZE, 1, fp);

    g_streamVolume = volStream;
    g_volumeAtZero   = volZero;
    g_voiceVolume  = volVoice;
    strcpy(g_gameprocStrB, stashA);
    strcpy(g_gameprocBlock, stashB);

    /* THE READ CLOBBERS THE LOAD FLAG AND THE ORIGINAL PUTS IT BACK. The
     * 0x438-byte block starts at ADDR_GAMEPROC_BLOCK and ADDR_LOAD_PENDING is
     * 0x370 into it, so the fread above overwrites the very flag that says a
     * load is in progress -- with the 0 that was in the file, because nothing
     * was pending when the game was saved. `mov [0x511ddc], 0` and
     * `mov [0x511dd8], 1` at 0x0042698B are the original restoring both, on
     * the SUCCESS path only; the tag failure returns before them.
     *
     * Omitting these was invisible to every configuration in the suite: they
     * all run with -dbg, which pauses the mission and skips the arm that
     * notices. Without it the flag reads 0, State2Enter takes the "start
     * fresh" branch, and TakeMenuRequest reaches MissionStartup, whose retry
     * stamp then SAVES OVER the file the player asked to load. The original
     * does not, and that difference is what found this. */
    /* Spelled through the block they live in: both flags are inside it, and
     * that is what keeps VC6 from hoisting the stores above the strcpy
     * that could overwrite them. */
    ((AM2_SelList *)g_gameprocBlock)->haveDefaultCof = 0;
    ((AM2_SelList *)g_gameprocBlock)->loadPending    = 1;
    return 1;
}

/* LoadAudioSection lives in win32/audio.cpp with the rest of the sound code,
 * and gameproc.cpp is on the flat side of the split, so it must name no Win32
 * or COM type. Declared here rather than by including that header -- the same
 * reason script.cpp declares PreloadSprite and event.cpp PlayDynamicSound.
 * Not `extern "C"`: audio.h does not wrap it, so the symbol is C++-mangled
 * and a C linkage declaration here would not resolve. */
int32_t __cdecl LoadAudioSection(am2_FILE *fp);

/* SaveAudioSection, the eleventh section writer, declared here for the same
 * reason and NOT `extern "C"` -- audio.h's extern block spans lines 11 to 182
 * and this sits at 188, outside it, exactly like LoadAudioSection above.
 * PlaySoundAt in region.cpp is at 157 and DOES need the wrapper. Both spellings
 * compile and only the linker tells them apart, so read where the block ends
 * rather than assuming the header is uniform. */
int32_t __cdecl SaveAudioSection(am2_FILE *fp);

/* SaveGame's three CRT seams. _findfirst and _findclose are named in orig.h
 * already; _mkdir is 0x00465F56, which docs/imports.tsv identifies from the
 * KERNEL32!CreateDirectoryA at 0x00465F5C inside it. */
typedef int32_t (__cdecl *AM2_FindFirstFn2)(const char *pattern, void *data);
typedef int32_t (__cdecl *AM2_FindCloseFn)(int32_t handle);
typedef int32_t (__cdecl *AM2_MkdirFn)(const char *path);
typedef int32_t (__cdecl *AM2_SprintfFn2)(char *dst, const char *fmt, ...);
#define orig_findfirst ((AM2_FindFirstFn2)((void *)(uintptr_t)(ADDR_CRT_FINDFIRST)))
#define orig_findclose ((AM2_FindCloseFn)((void *)(uintptr_t)(ADDR_CRT_FINDCLOSE)))
#define orig_mkdir     ((AM2_MkdirFn)((void *)(uintptr_t)(ADDR_CRT_MKDIR)))
#define orig_sprintf   ((AM2_SprintfFn2)((void *)(uintptr_t)(ADDR_GAME_SPRINTF)))

/* _chmod and fflush, neither of which this tree had needed before. */
typedef int32_t (__cdecl *AM2_ChmodFn)(const char *path, int32_t mode);
#define orig_chmod  ((AM2_ChmodFn)((void *)(uintptr_t)(ADDR_CRT_CHMOD)))
typedef int32_t (__cdecl *AM2_FflushFn)(am2_FILE *fp);
#define orig_fflush ((AM2_FflushFn)((void *)(uintptr_t)(ADDR_CRT_FFLUSH)))

/* LoadOptions -- original 0x0044D110, 395 bytes, one caller. SaveOptions's
 * other half, and it agrees with it field for field: the same four volumes,
 * the same one-byte-in-two key bindings, the same four host settings, and
 * the same two length-prefixed names.
 *
 * IT PROVES THE FILE IS WHOLE BY SEEKING TO 0x24 AND BACK. A seek past the
 * end fails, so a truncated Options.cfg is rejected before a single field is
 * read rather than half-loaded. The rewind is a second fseek to 0, not a
 * rewind() call, and its result is not checked.
 *
 * THE NAMES ARE TERMINATED HERE AND NOT THERE. SaveOptions writes a length
 * and exactly that many bytes with no NUL; this reads the length, reads that
 * many bytes, and writes the NUL itself at [len]. So the terminator lives in
 * the reader, which is why a name containing one is truncated by this and
 * not by the writer -- the note on SaveOptions says so from the other side.
 *
 * THE THREE VOLUMES ARE REWRITTEN AFTER THE FILE IS CLOSED. A stored -2000
 * becomes -10000, and only those three; MOVIE_COUNT and DIFFICULTY are taken
 * as read. -2000 is what the mixer means by "quiet" and -10000 by "silent",
 * so this is an old settings file being migrated, not a range clamp -- it
 * fires on exactly one value and leaves every other alone.
 *
 * It fflushes before fclose, as SaveOptions does. Reproduced. */
void __cdecl LoadOptions(void)
{
    am2_FILE *fp;
    int32_t   i;
    int32_t   len;

    SetGameDir(g_dirScratch);

    fp = orig_fopen("Options.cfg",
                    "r");
    /* Whole-file check: if it cannot seek this far there is nothing to read.
     * NOTE it does NOT close the file on that path -- it falls into the same
     * defaults tail as a failed open, which never had one to close. The
     * handle leaks. Reproduced. */
    if (fp && orig_fseek(fp, AM2_OPTIONS_MIN_BYTES, 0) == 0) {
        orig_fseek(fp, 0, 0);

        orig_fread((void *)&g_volumeAtZero, 4, 1, fp);
        orig_fread((void *)&g_streamVolume, 4, 1, fp);
        orig_fread((void *)&g_voiceVolume, 4, 1, fp);
        orig_fread((void *)&g_movieCount, 4, 1, fp);

        /* Index loops, as SaveOptions': the pointer VC6 walks compares signed. */
        for (i = 0; i < (int32_t)(((uintptr_t)g_keyBindingsEnd) - ((uintptr_t)g_keyBindings)) / 2; i++)
            orig_fread(&((uint8_t (*)[2])((uintptr_t)g_keyBindings))[i][0], 1, 1, fp);

        orig_fread((void *)&g_difficulty, 4, 1, fp);
        orig_fread((void *)&g_hostMaskA, 4, 1, fp);
        orig_fread((void *)&g_hostMaskB, 4, 1, fp);
        orig_fread((void *)&g_hostValue3e8, 4, 1, fp);

        len = 0;
        orig_fread(&len, 4, 1, fp);
        orig_fread((void *)&g_savedPlayerName, 1, (size_t)len, fp);
        (&g_savedPlayerName)[len] = '\0';

        /* Not re-zeroed: a short read here keeps the first length. */
        orig_fread(&len, 4, 1, fp);
        orig_fread((void *)&g_savedBattleName, 1, (size_t)len, fp);
        (&g_savedBattleName)[len] = '\0';

        orig_fflush(fp);
        orig_fclose(fp);

    } else {
        /* BOTH failure exits land here, and it is not a bare return: the settings
         * are reset and the key bindings rebuilt from ADDR_KEY_DEFAULTS. The
         * defaults go into every OTHER byte, matching the one-in-two stride
         * SaveOptions writes and this function reads -- so only the primary of
         * each binding is restored and the alternate is left as it was. */
        orig_log("Failed to open Options.cfg for reading\n");

        g_volumeAtZero = 0;
        g_streamVolume  = 0;
        g_voiceVolume   = 0;
        g_movieCount    = 0;

        for (i = 0; i < (int32_t)(((uintptr_t)g_keyBindingsEnd) - ((uintptr_t)g_keyBindings)) / 2; i++)
            ((uint8_t (*)[2])((uintptr_t)g_keyBindings))[i][0] =
                g_keyDefaults[i];
    }

    /* Migrate an old file's "quiet" to "silent". Only these three. */
    if (g_volumeAtZero == AM2_VOLUME_SENTINEL)
        g_volumeAtZero = AM2_VOLUME_SILENT;
    if (g_streamVolume == AM2_VOLUME_SENTINEL)
        g_streamVolume = AM2_VOLUME_SILENT;
    if (g_voiceVolume == AM2_VOLUME_SENTINEL)
        g_voiceVolume = AM2_VOLUME_SILENT;
}

/* SaveOptions -- original 0x0044CFA0, seven callers. Write Options.cfg: every
 * setting the game keeps between runs, as one straight line of fwrites with
 * no header, no tag and no version.
 *
 * IT CHMODS THE FILE BEFORE OPENING IT. `_chmod(path, _S_IREAD|_S_IWRITE)` on
 * a file it is about to `fopen("w")` -- so a read-only Options.cfg, which is
 * what a CD install would leave behind, is made writable first. The chmod's
 * result is not checked and neither is anything else; the only failure path
 * is the fopen, which logs and returns.
 *
 * THE MODE IS "w" AND NOT "wb", so this is a TEXT stream carrying binary
 * dwords. On Windows that turns every 0x0A byte into 0x0D 0x0A on the way
 * out, and the reader at 0x0044D110 opens the same way, so the pair agrees
 * with itself. It is still a file format that cannot survive being moved
 * between platforms, and worth knowing before anyone tries to read one here.
 *
 * THE KEY BINDINGS ARE WRITTEN ONE BYTE OUT OF EVERY TWO. ADDR_KEY_BINDINGS
 * is pairs -- a primary scancode and an alternate -- and the loop steps TWO
 * bytes while writing ONE, from the table's start to ADDR_KEY_BINDINGS_END.
 * So only the primary of each binding is persisted and the alternate is
 * rebuilt from the defaults on every run. That is the whole reason the first
 * four actions can bind W/UP, S/DOWN, A/LEFT and D/RIGHT and still round-trip.
 *
 * THE TWO NAMES ARE LENGTH-PREFIXED AND NOT TERMINATED. Each is measured with
 * strlen, the length goes out as a dword, and exactly that many bytes follow
 * -- no NUL. A name containing one would be truncated by the reader and not
 * by this.
 *
 * It fflushes and then fcloses, which is belt and braces; fclose flushes.
 * Reproduced.
 */
void __cdecl SaveOptions(void)
{
    am2_FILE *fp;
    int32_t   i;
    int32_t   len;

    SetGameDir(g_dirScratch);

    orig_chmod("Options.cfg", AM2_CHMOD_RW);

    fp = orig_fopen("Options.cfg",
                    "w");
    if (fp) {
        orig_fwrite((const void *)&g_volumeAtZero, 4, 1, fp);
        orig_fwrite((const void *)&g_streamVolume, 4, 1, fp);
        orig_fwrite((const void *)&g_voiceVolume, 4, 1, fp);
        orig_fwrite((const void *)&g_movieCount, 4, 1, fp);

        /* An index loop over the scancode pairs: VC6 walks a pointer but
         * compares it signed, which is what an int index strength-reduces to. */
        for (i = 0; i < (int32_t)(((uintptr_t)g_keyBindingsEnd) - ((uintptr_t)g_keyBindings)) / 2; i++)
            orig_fwrite(&((const uint8_t (*)[2])((uintptr_t)g_keyBindings))[i][0],
                        1, 1, fp);

        orig_fwrite((const void *)&g_difficulty, 4, 1, fp);
        orig_fwrite((const void *)&g_hostMaskA, 4, 1, fp);
        orig_fwrite((const void *)&g_hostMaskB, 4, 1, fp);
        orig_fwrite((const void *)&g_hostValue3e8, 4, 1, fp);

        len = (int32_t)strlen(&g_savedPlayerName);
        orig_fwrite(&len, 4, 1, fp);
        orig_fwrite((const void *)&g_savedPlayerName,
                    1, (size_t)len, fp);

        len = (int32_t)strlen(&g_savedBattleName);
        orig_fwrite(&len, 4, 1, fp);
        orig_fwrite((const void *)&g_savedBattleName,
                    1, (size_t)len, fp);

        orig_fflush(fp);
        orig_fclose(fp);
    } else {
        orig_log("Failed to open Options.cfg for writing\n");
    }
}

/* SaveDefaultCof -- original 0x00457070, one caller, and it is the write half
 * of the pair below: the only thing in the image that produces
 * `save\default.cof`.
 *
 * IT RETIRES EACH UNIT ON THE WAY PAST. This is not a snapshot writer. For
 * every type-2 object in the list it calls ObjDropAltRecord -- state 5, the
 * alternate table record given up -- then Type238Action with the level's
 * completion award, then resets the position to the zero point, then
 * dismounts a rider: OBJ_OFF_RIDING cleared, OBJ_FLAG8_BLOCKED cleared, and
 * the object's first map row made drawable again through ObjFlagSet0. Calling
 * it twice would award the score twice, which is why the one caller is a
 * level-completion path and not a menu.
 *
 * THE ORDER OF THE TWO HEALTH TESTS IS NOT WHAT IT LOOKS LIKE. It reads as
 * "skip the dead", and it is not: `health > 0` proceeds, and only when health
 * is at or below zero does maxHealth decide -- a positive maxHealth then
 * SKIPS. So an object with health 0 and maxHealth 0 is written, and one with
 * health 0 and maxHealth 10 is not. Transcribed from the branch structure,
 * not from the reading, for exactly that reason.
 *
 * THE TYPE FILTER IS THREE TESTS AND ONLY THE LAST ONE BITES. Types 2, 3 and
 * 8 are admitted, OBJ_OFF_BY_TYPE_94 must be zero, and then the type must be
 * exactly 2. Vehicles and roaches are excluded twice over. Kept as written:
 * folding it to `type == 2` would give the same behaviour and hide that the
 * wider test is what the original asks.
 *
 * A MISSING OBJECT IS REMOVED AND THE INDEX DOES NOT ADVANCE. ListRemoveAt
 * compacts, so the slot now holds the next uid and must be re-examined; the
 * original jumps straight to the loop condition without its `inc`. Both the
 * list pointer and the count are re-read from the globals every iteration,
 * which is what makes that safe.
 *
 * THE INVENTORY LOOP CLOBBERS THE OBJECT and the original does not care --
 * `esi` is reused for each weapon and the saved index is restored from the
 * frame afterwards. Nothing reads the unit again, so a local of our own is
 * the same function.
 *
 * NOTHING HERE IS EXERCISED. The one caller is a level-completion path this
 * project has no drive for, and the file it writes does not ship, so no
 * configuration in tools/ab.sh reaches a line of it -- the load half below
 * says the same about itself. What would compare it is a byte-for-byte diff
 * of save/default.cof between a patched and an AM2_NOPATCH=1 run of a
 * completed mission; that drive does not exist yet, so this is verified by
 * reading alone and that is worth saying plainly.
 */
int32_t __cdecl SaveDefaultCof(void)
{
    am2_FILE *fp;
    int32_t   i;

    SetGameDir("save");

    orig_chmod("default.cof", AM2_CHMOD_RW);

    fp = orig_fopen("default.cof",
                    "wb");
    if (!fp)
        return 0;

    WriteSaveTag(fp, AM2_SAVETAG_COF);

    /* The army's list is re-read through its slot at each use. */
    i = 0;
    for (;;) {
        AM2_Object *obj;

        if (i >= g_armyObjLists[g_defaultOwner]->count)
            break;

        obj = LookupByUID(
                  g_armyObjLists[g_defaultOwner]->uids[i]);
        if (!obj) {
            ((AM2_Rec *)g_armyObjLists[g_defaultOwner])->ListRemoveAt(i);
            continue;
        }

        if (obj->health <= 0 &&
            obj->maxHealth > 0) {
            i++;
            continue;
        }

        if (ObjIsType2Inline(obj)) {
            ObjDropAltRecord(obj);

            Type238Action(obj, (g_levelId * 5 + 5) * 2);
            obj->dest = g_zeroPoint;

            if (obj->riding) {
                obj->riding = 0;
                obj->flags &=
                    ~(uint32_t)OBJ_FLAG8_BLOCKED;
                ObjFlagSet0((AM2_MapRow *)(obj->rowSet.rows));
            }
        }

        obj->flags &= ~(uint32_t)OBJ_FLAG8_BLOCKED;

        if (ObjIsTypeIn238Inline(obj)
            && obj->aiDriven == 0) {
            switch (*(const int32_t *)obj) {
            case AM2_OBJ_TYPE_TROOPER: {
                int32_t slot;

                SaveOneItem(fp, obj);
                SaveScriptName(fp, obj);

                for (slot = 0; slot < AM2_INVENTORY_SLOTS; slot++) {
                    AM2_Object *w = LookupByUID(
                        (&obj->inventory[0])[slot]);

                    if (w) {
                        SaveOneItem(fp, w);
                        SaveScriptName(fp, w);
                    }
                }
                break;
            }
            }
        }

        i++;
    }


    WriteSaveTag(fp, AM2_SAVE_TAG_END);
    orig_fclose(fp);
    return 1;
}

/* LoadDefaultCof -- original 0x00457320, one caller, and that caller is the
 * state-2 ENTRY: this runs on the way into a level, not on a save load.
 *
 * IT NAMES ITS OWN SOURCE FILE, and that file was already known: its
 * CheckSaveTag call passes "C:\ArmyMen2\source\unit.cpp", which
 * docs/00-recon.md already lists against tag 0x06660668 from the survey of
 * every CheckSaveTag site. Worth checking before writing up -- CLAUDE.md's
 * "two module names come from the image" is about the ones the SPLIT follows,
 * not about how many the image carries, and the answer to the second question
 * is ten.
 *
 * What it does: chdir into `save`, open `default.cof`, check its tag, then
 * read a run of objects each preceded by AM2_SAVE_RECORD_MARK. Every object
 * that loads is HEALED TO FULL -- OBJ_OFF_HEALTH takes OBJ_OFF_MAX_HEALTH,
 * but only when both are already positive, so a dead one is left dead -- and
 * every type 2 among them is counted. The count goes into the script variable
 * `numgreen` at the end.
 *
 * Its per-object marker is AM2_SAVE_RECORD_MARK, the same "another record
 * follows" dword every list-storing section uses -- orig.h already records
 * that it is not section-specific, and this is one more section agreeing.
 *
 * THE LOOP READS ITS TAG BEFORE THE TEST AND AGAIN AT THE BOTTOM, so the tag
 * that ends the run is consumed and not pushed back. Nothing reads the file
 * afterwards, so it does not matter here; it does mean the terminator can be
 * any dword that is not the item tag, including EOF leaving the buffer
 * unchanged -- the fread's result is discarded, which is the same missing
 * check LoadScriptName documents one file over.
 *
 * THE UID REMAP TABLE IS CLEARED AT BOTH ENDS. UidRemapClear runs before the
 * first object and again after the last, with RemapInventoryUids between --
 * so the table is built by the loads, consumed once, and left empty. Those
 * are the "two callers in 0x00457370's band" orig.h already predicted for it,
 * and this is both of them.
 *
 * ITS FIRST FAILURE ANSWERS THE fopen RESULT ITSELF -- a null FILE * returned
 * as the int32 zero, which is the same value the tag failure returns
 * deliberately. Reproduced as a literal 0, since that is what it is.
 *
 * EVERYTHING PAST THE fopen IS UNREACHABLE ON THIS DATA SET. orig.h already
 * records that default.cof does not ship with the GOG install and that
 * ADDR_HAVE_DEFAULT_COF therefore reads 0 for the whole of any run. This
 * function does not consult that flag -- it simply tries the file and finds
 * nothing -- so the reading of the body is verified by reading alone, and
 * that is a stronger statement than a counter at 0 would be.
 */
int32_t __cdecl LoadDefaultCof(void)
{
    am2_FILE *fp;
    uint32_t  tag;
    int32_t   green;

    SetGameDir("save");

    fp = orig_fopen("default.cof",
                    "rb");
    if (!fp)
        return 0;

    if (!CheckSaveTag(fp, AM2_SAVETAG_COF,
                      "C:\\ArmyMen2\\source\\unit.cpp",
                      AM2_COF_TAG_LINE)) {
        orig_fclose(fp);
        return 0;
    }

    UidRemapClear();

    green = 0;
    orig_fread(&tag, 4, 1, fp);
    while (tag == AM2_SAVE_RECORD_MARK) {
        AM2_Object *obj = LoadOneItem(fp, 1);

        if (obj) {
            int16_t max = obj->maxHealth;

            if (max > 0 && obj->health > 0) {
                obj->health = max;
                if (ObjIsType2Inline(obj))
                    green++;
            }
            LoadScriptName(fp, obj);
        }
        orig_fread(&tag, 4, 1, fp);
    }

    RemapInventoryUids();
    UidRemapClear();

    SetVarValueByName("numgreen", green);

    orig_fclose(fp);
    return 1;
}

/* 0x00425A10. The read end of the savegame. Check the outer tag, throw away
 * whatever tokens the context is holding, then run the eleven loaders in the
 * order SaveGame wrote them -- which is also the order the sections appear in
 * the file, confirmed against a real save rather than only read here.
 *
 * Any loader returning 0 abandons the rest. Both exits close the file, so the
 * caller's FILE * is dead on return either way; 0x00425950 opened it and does
 * not close it again.
 *
 * Every callee is reconstructed, so none of their counters can move when this
 * runs -- the usual blind spot. This one's counter does, because the caller
 * is still original. */
/* SaveGame -- original 0x00425790, the write half of the savegame and the
 * mirror of LoadGame below.
 *
 * ITS FRAME TILES EXACTLY, which is what settled every offset in it: a
 * 0x104-byte level-name buffer, a 0xFC-byte path buffer, and a 0x118-byte
 * _finddata_t at +0x200. 0x200 + AM2_FINDDATA_BYTES is 0x318, the frame the
 * original reserves, and `attrib` at the struct's offset 0 is what the
 * `test BYTE PTR [esp+0x208], 0x10` reads. Same tiling argument that re-based
 * ADDR_RANK_RECORDS.
 *
 * IT CREATES THE SAVE DIRECTORY, which took two wrong readings to see. The
 * middle block is not a slot search and 0x00465F56 is not _findnext -- that is
 * ADDR_CRT_FINDNEXT at 0x00465C6D. docs/imports.tsv puts
 * KERNEL32!CreateDirectoryA at 0x00465F5C, inside 0x00465F56, so it is _mkdir:
 * `save\<level>\` is created when _findfirst does not find it, and again when
 * it finds something that is not a directory.
 *
 * THE TEN SECTION GUARDS ARE NOT RETURNS. Each `je 0x0042591C` lands on
 * `push esi; call fclose` -- writing them as a bare `return 0` would leak the
 * handle on every failure path, which is the defect StepType2 carried three
 * times today. Its three genuine early returns are fall-through rets.
 *
 * THE ORDER IS CONFIRMED TWICE INDEPENDENTLY: LoadGame calls the eleven
 * loaders in it, and CLAUDE.md's savegame oracle reports its per-section
 * results in the same sequence. Every writer is already reconstructed, so this
 * needs no orig_ seam -- gameproc.cpp already includes every module involved.
 */
int32_t __cdecl SaveGame(const char *name)
{
    char       lvl[0x100];
    char       path[0x100];
    uint8_t    fd[AM2_FINDDATA_BYTES];
    am2_FILE  *fp;
    int32_t    h;

    if (!g_gameprocBlock[0])
        return 0;
    if (!*name)
        return 0;

    SetGameDir("save");

    strcpy(lvl, g_gameprocBlock);

    h = orig_findfirst(lvl, fd);
    if (h == -1) {
        orig_mkdir(lvl);
    } else {
        if (!(fd[0] & AM2_ATTR_SUBDIR))
            orig_mkdir(lvl);
        orig_findclose(h);
    }

    orig_sprintf(path, "save\\%s",
                 g_gameprocBlock);
    SetGameDir(path);

    fp = orig_fopen(name, "wb");
    if (!fp)
        return 0;

    WriteSaveTag(fp, AM2_SAVETAG_GAMEPROC);

    if (!SaveGameProcSection(fp))   goto fail;
    if (!SaveMapSection(fp))        goto fail;
    if (!SavePadSection(fp))        goto fail;
    if (!SaveObjScriptSection(fp))  goto fail;
    if (!SaveScriptSection(fp))     goto fail;
    if (!SaveEventBlock(fp))        goto fail;
    if (!SaveScriptConditions(fp))  goto fail;
    if (!SaveEventSection(fp))      goto fail;
    if (!SaveItems(fp))             goto fail;
    if (!SaveAirSection(fp))        goto fail;
    if (!SaveAudioSection(fp))      goto fail;

    orig_fclose(fp);
    return 1;

fail:
    orig_fclose(fp);
    return 0;
}

int32_t __cdecl LoadGame(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_GAMEPROC,
                      "C:\\ArmyMen2\\source\\gameproc.cpp", 0x53D))
        goto fail;

    ScriptResetTokens(&g_scriptContext);

    if (!LoadGameProcSection(fp))   goto fail;
    if (!LoadMapSection(fp))        goto fail;
    if (!LoadPadSection(fp))        goto fail;
    if (!LoadObjScriptSection(fp))  goto fail;
    if (!LoadScriptSection(fp))     goto fail;
    if (!LoadEventBlock(fp))        goto fail;
    if (!LoadScriptConditions(fp))  goto fail;
    if (!LoadEventSection(fp))      goto fail;
    if (!LoadItems(fp))             goto fail;
    if (!LoadAirSection(fp))        goto fail;
    if (!LoadAudioSection(fp))      goto fail;

    orig_fclose(fp);
    return 1;

fail:
    orig_fclose(fp);
    return 0;
}

/* ------------------------------------------------ game constants ---- */

typedef char *(__cdecl *AM2_StrtokFn)(char *s, const char *sep);
#define orig_strtok (*(AM2_StrtokFn)((void *)(uintptr_t)(ADDR_CRT_STRTOK)))
#define kSep        (" \t\n;,")

/* 0x00424780. The .aai handler for twenty game-constant keywords.
 *
 * It lives here rather than in definfo.cpp because the band says so: this is
 * event.cpp..gameproc.cpp, the same range our other gameproc functions sit in,
 * while definfo.cpp's are audio.cpp..event.cpp. The image names no source file
 * for either, so the band is the only evidence there is.
 *
 * The number is parsed BEFORE the keyword is examined, so an unknown keyword
 * still consumes its argument -- and a bad number is rejected (2) ahead of a
 * bad keyword (3) even when both are wrong.
 *
 * Twelve of the twenty keywords are accepted and then thrown away. In the
 * original they share one arm that does nothing but return 0, so
 * vehicle_danger through scroll_speed parse, validate, and have no effect in
 * this build. Reproduced: the `default` of the inner switch below is that
 * arm, not an error. Only the eight roach_* constants reach a global, and
 * they land in consecutive dwords, which is why they are one array here. */
int32_t __cdecl DefGameParse(int32_t cmd, char *line)
{
    int32_t value;

    if (!DefParseNumber(&value, orig_strtok(line, kSep)))
        return 2;

    /* A jump table over the twenty: the first twelve are cases that do
     * nothing -- the original drops them on the floor -- and each of the last
     * eight stores its own constant. */
    switch (cmd) {
    case 0x3B: break;
    case 0x3C: break;
    case 0x3D: break;
    case 0x3E: break;
    case 0x3F: break;
    case 0x40: break;
    case 0x41: break;
    case 0x42: break;
    case 0x43: break;
    case 0x44: break;
    case 0x45: break;
    case 0x46: break;
    case 0x47: g_gameConstants[0] = value; break;
    case 0x48: g_gameConstants[1] = value; break;
    case 0x49: g_gameConstants[2] = value; break;
    case 0x4A: g_gameConstants[3] = value; break;
    case 0x4B: g_gameConstants[4] = value; break;
    case 0x4C: g_gameConstants[5] = value; break;
    case 0x4D: g_gameConstants[6] = value; break;
    case 0x4E: g_gameConstants[7] = value; break;
    default:
        orig_log("DefGameParse: Bad Game Constant Type\n");
        return 3;
    }
    return 0;
}


/* StateLeave lives in win32/movie.cpp -- it clears the primary surface, so it
 * cannot live in the flat half -- and this module cannot include movie.h for
 * the same reason. Declared instead, the way commmsg.cpp declares the comm
 * methods and the widget helpers it needs. */
#if !defined(_MSC_VER)
extern "C"
#endif
void __cdecl StateLeave(void);  /* movie.h's linkage: C++ under VC6 */

void __cdecl SetGameOver(int32_t state)
{
    /* All three read before any is written, which is what the original's three
     * loads then three stores do; nothing here can alias, but reproduce it. */
    int32_t a = g_gameOverSource[0];
    int32_t b = g_gameOverSource[1];
    int32_t c = g_gameOverSource[2];

    g_gameOverSaved[0] = a;
    g_gameOverSaved[1] = b;
    g_gameOverSaved[2] = c;
    g_endState = state;
}

int32_t __cdecl GameOverState(void)
{
    return g_endState;
}

void __cdecl StateLeaveAlias(void)
{
    StateLeave();
}


void __cdecl RequestState(int32_t state)
{
    g_statePending = 1;
    g_stateWanted = state;
}

void __cdecl CommitState(void)
{
    int32_t wanted = g_stateWanted;

    g_stateWanted = -1;
    g_gameState = wanted;
    g_statePending = 0;
    g_stateEntered = 1;
}

/* 0x00462A40, one caller -- MissionInput, when the info action is pressed in a
 * NETWORK game. Toggles the info overlay and nothing else: one player must not
 * be able to stop everybody else's game, so where a solo mission pauses, this
 * flips a display flag instead.
 *
 * The original writes the flag through `sete`, so the stored value is strictly
 * 0 or 1 rather than the negation of whatever was there. That matters because
 * the two sites in the map painter that read it are testing it, not counting
 * it -- but a reconstruction that wrote `!x` on an int would agree only while
 * the flag stayed in {0,1}, and this is the only writer that keeps it there.
 *
 * VERIFIED BY READING. Its only caller is ours, so the counter is 0 by
 * construction and blindspots.py lists it -- and the branch that reaches it
 * needs a network game, which this machine cannot host.
 *
 * Forcing it by poking ADDR_NET_GAME to 1 in a live solo mission does not
 * work, and the reason is recorded so nobody tries it twice: the process dies
 * within seconds. That is NOT this reconstruction. The identical poke under
 * AM2_NOPATCH=1 kills the original in the same place, because the flag steers
 * the frame path into the paint-object branch of a comm session that does not
 * exist. An unexplained crash beside new code is worth ten minutes to
 * attribute rather than leaving as a suspicion. */
void __cdecl ShowInfoMp(void)
{
    int32_t *on = &g_infoOverlayOn;

    *on = (*on == 0) ? 1 : 0;
}

typedef int32_t (__cdecl *AM2_SaveObjFn)(am2_FILE *fp, void *obj);

/* 0x00428730 and 0x004289B0 -- the two halves of the object HEADER, and the
 * pair that makes the whole family agree with itself.
 *
 * The size is ADDR_ITEM_HEADER_SIZE, a global set to 0x68 by 0x00427640 and
 * written nowhere else. Both sides read it, so neither can be given a literal
 * that drifts from the other. Every per-type saver and loader below reads the
 * same global for its own header step.
 *
 * ONLY THE SAVE SIDE WRITES A TAG. The load side does not check one, so the
 * tag before an item header is a marker for a reader walking the file rather
 * than something the game verifies -- unlike the lengths CheckSaveTag guards.
 *
 * Neither checks fwrite or fread. Both return 1 unconditionally, so a
 * truncated file reads as a successful load of whatever was in the buffer;
 * SaveOneItem's careful per-step checking has nothing to check here.
 */
int32_t __cdecl SaveItemHeader(am2_FILE *fp, AM2_Object *obj)
{
    WriteSaveTag(fp, AM2_ITEM_HEADER_TAG);
    orig_fwrite(obj, (size_t)g_itemHeaderSize, 1, fp);
    return 1;
}

int32_t __cdecl LoadItemHeader(am2_FILE *fp, AM2_Object *hdr)
{
    orig_fread(hdr, (size_t)g_itemHeaderSize, 1, fp);
    return 1;
}

/* 0x00433D20, 0x00422750 and 0x0043CB30 -- three of the eight per-type
 * savers, and the three that are nothing but a write.
 *
 * All three write from OBJ_OFF_BY_TYPE_94, which is what settles what that
 * field is: the object's TYPE-SPECIFIC RECORD, saved whole. The sizes are
 * literals in the savers rather than a table -- 0x2C for type 1, 0x28 for
 * type 6, 0x4CC for type 8 -- so the record grows by an order of magnitude
 * between an item and a roach.
 *
 * TYPE 1 WRITES A SECOND TAG and the other two do not. Its record opens with
 * a POINTER -- ADDR_STEP_TYPE1_4 dereferences the same field -- and the tag
 * is that pointee's +8, written after the record that contains the pointer.
 * So the file carries a raw pointer AND a value read through it, which is
 * exactly why CLAUDE.md calls the savefile an oracle only once it can ignore
 * pointers.
 *
 * TYPE 8 IS THE ONLY ONE THAT CHECKS ITS OBJECT, and it answers 0 for a null
 * where the other two would fault. Reproduced; nothing says why it is the one
 * that guards.
 */
int32_t __cdecl SaveType1(am2_FILE *fp, AM2_Object *obj)
{
    uint8_t *rec = (uint8_t *)&obj->saveRec;

    orig_fwrite(rec, AM2_TYPE1_RECORD_SIZE, 1, fp);
    WriteSaveTag(fp, *(const uint32_t *)(*(uint8_t *const *)rec + 8));
    return 1;
}
/* CreateItem is reconstructed, in item.cpp, and reached by name -- the
 * typedef and orig_ macro that used to sit here named the type-1 arm of the
 * four creators and would now resolve to our own detour. */


/* ResetLevelState -- original 0x00424E80, one caller: the level start.
 *
 * Clear two dozen globals, seed the tick interval from the difficulty, empty
 * the selection list, fill ADDR_ALLY_MATRIX with the identity and then ally any
 * two comm players sharing a team.
 *
 * THE TICK INTERVAL IS DIFFICULTY-SCALED AND ITS STATIC VALUE IS NEVER USED.
 * ADDR_TICK_INTERVAL_MS holds 1000 in the image and this overwrites it with
 * 3000, 5000 or 7000 -- and with a flat AM2_TICK_NET_MS in a session, so a
 * network game runs on the hardest single-player cadence whatever the
 * difficulty says. ADDR_SECOND_DEADLINE is seeded to the same number, which
 * makes the first tick one whole interval long rather than immediate.
 *
 * THE ALLY PASS IS SYMMETRIC AND THE ORIGINAL WRITES BOTH HALVES SEPARATELY,
 * through two pointers walking the matrix in opposite senses -- one striding a
 * row, the other a column. Written here as the two assignments they are; the
 * pointer arithmetic is the same either way and the symmetry is the point.
 *
 * A RECORD IS SKIPPED ENTIRELY when its COMM_SLOT_OFF_TAKEN is zero, and
 * that test is made on the OUTER record before the inner loop and again on
 * each inner one -- so an empty slot neither allies nor is allied with,
 * rather than being allied by the other side of the pair.
 *
 * The identity fill runs first and unconditionally, so an army is always
 * allied with itself even when the comm object holds nothing.
 */
/* Reconstructed in item.cpp, declared there rather than in item.h. */
void __cdecl SelectInventorySlot(AM2_Object *unit, int32_t slot);

/* The kind a scenario row's KIND field selects, for the five it names. Read
 * out of the image's own jump table at 0x0043E130 rather than off the arms:
 * kinds 0 and 4 share the default with everything above 5, and the five that
 * do not are 1->2, 2->3, 3->4, 5->5. Six slots, five distinct answers, and
 * two of them the default -- exactly the shape the DirtyCollect rule is for. */
static inline int32_t ScenRowWeapon(int32_t kind)
{
    switch (kind) {
    case 0:  return 1;
    case 1:  return 2;
    case 2:  return 3;
    case 3:  return 4;
    case 4:  return 1;
    case 5:  return 5;
    default: return 1;   /* everything past 5 */
    }
}

/* PlaceScenario -- original 0x0043DDA0, 908 bytes, one caller: State2Enter,
 * and only when nothing is being loaded. Put every scenario row on the map.
 *
 * Four parts, one per army slot, each a count and an array of 0x6C-byte rows.
 * The original walks the parts through a FIELD POINTER at scen + 0x14, which
 * is SCENARIO_OFF_PARTS plus SCENARIO_PART_OFF_COUNT -- so `[esi]` is the
 * count and `[esi+4]` the rows, and the record itself starts four bytes
 * earlier. Written from the record base with the existing names.
 *
 * A NETWORK GAME SKIPS SLOTS NOBODY OCCUPIES. In single player every slot is
 * placed; with a session up, a slot is placed only if CommWasHereForArmy says
 * someone has been in it.
 *
 * KIND 0x8005 IS THE LEADER and gets everything the others do not: a weapon
 * from key 0x2D group 9, SetLeadsAndAct, soldier kind 7, and -- only when the
 * slot is the local player's -- the camera and the selection. Every other
 * kind takes a weapon chosen by the table above and a soldier kind derived
 * from it.
 *
 * THE TAIL IS A SEPARATE PASS and runs only when a default .cof was found:
 * it moves the leader to the saved point, then walks the local army's object
 * list placing every unencumbered type 2 into a formation around it. That
 * loop REMOVES dead entries from the list as it goes -- a uid that no longer
 * resolves is dropped and the index is NOT advanced -- so it rebuilds the
 * list while iterating it, which is why it re-reads the list head every
 * time. */
int32_t __cdecl PlaceScenario(void)
{
    AM2_Scenario *scen = g_scenarios;
    int32_t  slot;

    if (!scen)
        return 0;

    /* The comm object is read afresh at each use. */
    for (slot = 0; slot < AM2_COMM_SLOTS; slot++) {
        AM2_ScenPart *part = &scen->parts[slot];
        AM2_ScenRow  *row;
        int32_t  army;
        int32_t  i;

        if (g_mpSession != 0
            && !CommWasHereForArmy((*(void * *)&g_commObject), slot))
            continue;

        army = ((AM2_Comm *)(*(void * *)&g_commObject))->CommSlotForArmy(slot);

        row = part->rows;
        for (i = 0; i < part->count; row++, i++) {
            AM2_Object *unit;
            int32_t  amount;
            int32_t  flags;

            if (row->pos.x <= 0)
                continue;

            /* WHEN A SAVED PLATOON EXISTS, OUR ARMY IS NOT PLACED FROM THE
             * SCENARIO -- it comes from default.cof (LoadDefaultCof). The
             * original guards the creation at 0x0043DE16: with
             * ADDR_HAVE_DEFAULT_COF set and this row's army our own, it creates
             * NOTHING here. For the leader row (0x8005) it instead records the
             * spawn point into ADDR_LEADER_POS / ADDR_LEADER_FACING and parks
             * the view on it, so the .cof form-up pass below can move the
             * cof-loaded leader here; every other of our rows it simply drops.
             *
             * Without this the scenario leader is created as a SECOND Sarge
             * beside the cof-loaded one -- the campaign level-2 divergence,
             * 907 objects against 905, two sarge=1 units against one. It was
             * invisible until now because default.cof does not ship: single
             * levels leave ADDR_HAVE_DEFAULT_COF at 0 and this arm never runs.
             * The LookupOwnerObj call is the original's and its result is
             * discarded there too. */
            flags = 0;
            if (!row->flag)
                flags = 4;
            if (g_haveDefaultCof
                && (uint32_t)army == g_defaultOwner) {
                if (row->kind == AM2_SCEN_KIND_LEADER) {
                    uint8_t amt;

                    (void)LookupOwnerObj(g_defaultOwner);
                    g_leaderPos.x =
                        row->pos.x;
                    g_leaderPos.y =
                        row->pos.y;
                    amt = row->amount;
                    g_leaderFacing =
                        amt < 1 ? 1 : amt;
                    g_viewHold = 1;
                    g_viewTarget =
                        g_leaderPos;
                }
                continue;
            }

            /* THE NAME IS THE ROW'S OWN, at +0x13 -- not the shared scratch
             * buffer every other creator in this tree passes. And the flags
             * are 4 or 0 chosen by SCEN_ROW_OFF_FLAG, not a constant.
             *
             * Getting either wrong is invisible in the pixels and in the log:
             * the first attempt passed ADDR_DIR_SCRATCH and fixed flags, and
             * bootcamp came back with 1,609 of 1,610 object lines identical
             * and Sarge's UID reading 1 instead of 0x3E8. The state dump is
             * the only artifact that could see it. */
            unit = CreateTrooper(
                       row->name,
                       row->pos.x,
                       row->pos.y,
                       ((AM2_Comm *)(*(void * *)&g_commObject))->CommArmyOfSlot(army), army,
                       flags,
                       1, 0, 1, 0);

            /* SCEN_ROW_OFF_AMOUNT, clamped up to 1, into OBJ_OFF_FACING_COPY.
             * THE TWO NAMES DISAGREE and this writer is the evidence: one of
             * them is wrong, since clamping a facing to >= 1 makes no sense
             * and neither does calling 0xF8 a facing if a scenario amount
             * lands in it. Six other sites write 0xF8; settling it means
             * reading one of them, which this commit does not. Reproduced
             * with the offsets as they stand and the conflict recorded. */
            amount = row->amount < 1
                         ? 1 : row->amount;
            unit->facingCopy = (uint8_t)amount;

            if (row->kind == AM2_SCEN_KIND_LEADER) {
                AM2_Object *w = CreateWeapon(g_dirScratch,
                                       army,
                                       KeyLookupTriple(0x2D, 9, 0),
                                       g_zeroPoint,
                                       4, -1, 1, 0);

                unit->inventory[0] =
                    w->uid;
                SetLeadsAndAct(unit);
                unit->rank = 7;   /* the leader outranks */

                if ((uint32_t)army == g_defaultOwner) {
                    g_ourLeaderUid = unit->uid;
                    g_leaderPos.x =
                        row->pos.x;
                    g_leaderPos.y =
                        row->pos.y;
                    g_leaderFacing =
                        unit->facing;
                    DeselectAll();
                    SelectUnit(unit);
                    g_viewHold   = 1;
                    g_viewTarget =
                        g_leaderPos;
                }
                SelectInventorySlot(unit, 0);
            } else {
                AM2_Object *w = CreateWeapon(g_dirScratch,
                                       army,
                                       KeyLookupTriple(0x2D, ScenRowWeapon(row->kind), 0),
                                       g_zeroPoint,
                                       4, -1, 1, 0);

                unit->inventory[0] =
                    w->uid;
                SoldierKindForWeapon(unit,
                    (uint32_t)w->typeRec->kind);
            }
        }
    }

    if (g_haveDefaultCof == 0)
        goto done;

    /* The .cof pass: put the leader where the save says, then form up on it. */
    {
        AM2_Object *lead;
        int32_t  i      = 0;
        int32_t  formed = 0;

        lead = LookupOwnerObj(g_defaultOwner);

        if (!lead)
            goto done;

        lead->pos =
            g_leaderPos;
        lead->facing =
            g_leaderFacing;
        ObjTileChanged(lead, 0, 0);
        SetUnitPose(lead, 1);
        DeselectAll();
        SelectUnit(lead);

        /* Our army's list, re-read from the table at every use. */
        while (1) {         /* not `for (;;)`, which VC6 rotates */
            AM2_Object  *obj;

            if (i >= g_armyObjLists[g_defaultOwner]->count)
                break;

            obj = LookupByUID(
                      g_armyObjLists[g_defaultOwner]->uids[i]);
            if (!obj) {
                /* A stale uid is REMOVED and the index does not advance. */
                ((AM2_Rec *)g_armyObjLists[g_defaultOwner])->ListRemoveAt(i);
                continue;
            }
            /* The script state is reset for every unit the first three tests
             * pass -- BEFORE the type and Sarge tests (0x0043E0D5 stores it
             * ahead of `sub eax,2; jne`). Only a non-Sarge type 2 is formed
             * up. */
            if (!(obj->flagsLo & 4)
                && ObjIsTypeIn238(obj)
                && obj->saveRec == 0) {
                obj->scriptPoint =
                    g_zeroPoint;
                /* A one-case switch, not `== 2`: the image tests the type
                 * with `sub eax, 2`. */
                switch (*(const int32_t *)obj) {
                case 2:
                    if (obj->sarge == 0) {
                        AM2_Point p;

                        FormationPoint(obj, lead, &p, formed);
                        /* The point is copied as one dword. */
                        obj->pos = p;
                        obj->facing =
                            g_leaderFacing;
                        ObjTileChanged(obj, 0, 0);
                        SetUnitPose(obj, 1);
                        formed++;
                    }
                    break;
                }
            }
            i++;
        }
    }
done:
    return 1;
}

void __cdecl ResetLevelState(void)
{
    int32_t  *matrix = g_allyMatrix;
    AM2_Comm  *comm;
    int32_t   i, j;

    g_levelFlagE30 = 1;
    g_viewSnap      = 1;
    g_objCtxSet    = 1;

    g_nextUid          = 0x186A0;
    g_padCount         = 0;
    g_clockBaseMs     = 0;
    *(int32_t *)&g_gameClockMs     = 0;
    g_frameDeltaMs    = 0;
    *(int32_t *)&g_lastTickMs      = 0;
    g_evtId15Uid      = 0;
    g_objCtxValA     = 0;
    g_objCtxVal       = 0;
    g_objCtxValPrev  = 0;
    g_evtId15Flag     = 0;

    g_tickIntervalMs =
        g_difficulty * AM2_TICK_PER_STEP
        + AM2_TICK_BASE_MS;
    if (g_mpSession != 0)
        g_tickIntervalMs = AM2_TICK_NET_MS;
    g_secondDeadline =
        g_tickIntervalMs;

    *(int32_t *)&g_ourLeaderUid   = 0;
    *(int32_t *)&g_weaponOwnerId  = 0;
    g_weaponSlot      = 0;
    g_inputSuppress   = 0;

    ClearPtrList(g_selectedUids);

    if (g_mpSession == 0)
        g_netGame = 0;

    for (i = 0; i < AM2_COMM_PLAYERS; i++)
        for (j = 0; j < AM2_COMM_PLAYERS; j++)
            matrix[i * AM2_COMM_PLAYERS + j] = (i == j) ? 1 : 0;

    comm = g_commObject;

    for (i = 0; i < AM2_COMM_PLAYERS; i++) {
        const AM2_CommSlot *a = &comm->slots[i];

        if (!a->wasHere)
            continue;
        if (!a->team)
            continue;

        for (j = 0; j < AM2_COMM_PLAYERS; j++) {
            const AM2_CommSlot *b = &comm->slots[j];

            if (!b->wasHere)
                continue;
            if (a->team
                != b->team)
                continue;

            matrix[i * AM2_COMM_PLAYERS + j] = 1;
            matrix[j * AM2_COMM_PLAYERS + i] = 1;
        }
    }

    g_throttleDeadline   = 0;
    g_invulnerable  = 0;
}

/* TimedDirFrame is reconstructed now; maprow.h declares it. */

/* CreateMissile -- original 0x0043B9B0, nine callers. Build a type-5 object
 * from a weapon and a firing position: allocate it, run the shared init, give
 * it a row set, and start its animation.
 *
 * TYPE 5 IS A MISSILE and LoadType5 (0x0043B870, reconstructed) is the
 * evidence and the template: the savegame loader for the same type, sitting
 * immediately before this in the image. Everything structural here is its
 * vocabulary -- AM2_MISSILE_BYTES, ADDR_MISSILE_BOX, ADDR_MISSILE_ROW_SPEC,
 * ADDR_MISSILE_ANIMS, OBJ_OFF_BY_TYPE_94 for the def pointer -- and the two
 * agreeing is better evidence than either alone.
 *
 * THE FRAME RULE IS `def == 2 || def == 5`, which is what LoadType5 does too.
 * The original writes it as `sub 2; je` then `sub 3; jne`, the compiler's
 * chained-comparison idiom; reading only the first `sub eax,2` gives
 * `def == 2` and silently drops the second arm.
 *
 * DEF 3 IS A CHAINED KIND and it is the whole reason this is not LoadType5
 * with different inputs. It sets no animation table at all -- ROW_OFF_ANIM_CUR
 * stays 0 -- stamps ROW_OFF_STAMP_54 with the clock and calls the timed frame
 * lookup at 0x00461F90 instead, which writes ROW_OFF_SPRITE from a table
 * indexed by direction and elapsed time. Then, if the previously created
 * missile still resolves and is younger than AM2_FRAME_PERIOD_MS, this one's
 * uid goes into that one's +0xB4: consecutive segments link into a trail, and
 * the weapon's +0xD0 carries the last-created uid forward. Every other def
 * takes ADDR_MISSILE_ANIMS and an ordinary SetAnimFrame.
 *
 * ARGUMENT SLOTS ARE REUSED AS SCRATCH, TWICE, and the map below came from
 * tracking esp per PATH rather than reading operands -- a linear walk is wrong
 * here in two separate ways, because the function has an early exit AND a
 * two-way tail, and stepping through either one's pops skews every depth after
 * it. ARG3's slot is written and `fild`ed to convert an int to a float; ARG11
 * holds the frame between the comparison chain and SetAnimFrame. And
 * `[esp+0x34]` is ARG8 before the `add esp,4` that cleans malloc's argument and
 * ARG9 after it -- one displacement, two parameters, on one straight path.
 *
 * `+0xA8` IS NOT OBJ_OFF_CHAIN_UID HERE. That name is the item's, this is a
 * missile, and the fields at 0xA8 are overloaded by type the way orig.h
 * records for 0x52C and 0x538. Nothing else in the tree touches a missile's
 * +0xA8 -- LoadType5 restores RANK, REPAIR_FRAME, PTR_LIST and CHAIN_NEXT_UID
 * and steps over it -- so with one toucher it gets a field-numbered name and
 * no claim. What IS evidenced is that the same argument also lands in
 * OBJ_OFF_ROW0_Y_ADJUST and goes through ScaleBy32Blocks into the row. */
void *__cdecl CreateMissile(AM2_Object *weapon, AM2_Object *source, AM2_Point at,
                            int32_t heading, int32_t a5, int32_t a6,
                            int32_t repairFrame, float speedScale,
                            int32_t initUnused, uint32_t uid,
                            uint8_t scriptId)
{
    AM2_Object *w = weapon;
    AM2_Object *b = source;
    AM2_Object *o;
    int32_t  def;
    int32_t  frame;

    if (!weapon)
        return (void *)0;

    o = (AM2_Object *)am2_malloc(AM2_MISSILE_BYTES);
    memset(o, 0, AM2_MISSILE_BYTES);

    o->def = w->typeRec;
    o->facing = (uint8_t)heading;
    o->repairFrame = repairFrame;
    o->speedScale = speedScale;
    o->rank = (int32_t)((const AM2_Object *)source)->uid;
    o->power = (uint8_t)scriptId;

    def = o->def->kind;
    switch (def) {
    case 2:
    case 5:
        frame = AM2_MISSILE_FRAME_A;
        break;
    default:
        frame = AM2_MISSILE_FRAME_B;
        break;
    }

    o->owner = w->owner;
    ObjInitCommon(o, g_dirScratch,
                  AM2_OBJ_TYPE_MISSILE, at,
                  g_missileBox,
                  initUnused, uid);

    BuildRowSet(&o->rowSet, 1,
                g_missileRowSpec,
                at.x, at.y,
                g_zeroRect);

    o->followers.count = a5;
    /* From the field just stored, not from a5: the image copies the
     * stored register into a fresh one for the 16-bit store. */
    o->row0YAdjust =
        (int16_t)o->followers.count;
    o->deadline58 =
        g_gameClockMs;
    o->speed = b->speed;

    {
        int32_t n = w->typeRec->zVel;

        if (n > 0) {
            o->velZ = (float)n;
        } else {
            int32_t cur = o->row0YAdjust;

            /* Equal leaves OBJ_OFF_VEL_Z alone -- the original branches past
             * the fstp rather than storing zero, so a missile launched at its
             * own height keeps whatever the zeroed allocation left. */
            if (cur != a6)
                o->velZ = (float)((a6 - cur) << 1);
        }
    }

    if (o->def->kind == 3) {
        o->rowSet.rows->animCur = (AM2_AnimTable *)0;
        o->rowSet.rows->depthLayer =
            (int16_t)(ScaleBy32Blocks(o->row0YAdjust) + AM2_MISSILE_ROW26_BIAS);
        o->rowSet.rows->heading = (uint8_t)heading;
        o->rowSet.rows->stamp54 =
            g_gameClockMs;
        TimedDirFrame(o->rowSet.rows, heading);

        if (w->deadlineD0) {
            AM2_Object *prev = LookupByUID(w->deadlineD0);

            if (prev
                && g_gameClockMs
                   - prev->rowSet.rows->stamp54 < AM2_FRAME_PERIOD_MS)
                prev->scriptState =
                    (o)->uid;
        }

        w->deadlineD0 =
            (o)->uid;
    } else {
        o->rowSet.rows->animCur =
            g_missileAnims;
        o->rowSet.rows->depthLayer =
            (int16_t)(ScaleBy32Blocks(o->row0YAdjust) + AM2_MISSILE_ROW26_BIAS);
        o->rowSet.rows->heading = (uint8_t)heading;
        SetAnimFrame(o->rowSet.rows, (int16_t)frame, 1);
    }
    return o;
}

/* LoadType5 -- original 0x0043B870, one caller, and the MISSILE member of the
 * per-type savegame loader family.
 *
 * TYPE 5 IS A MISSILE, and this function is the evidence. It calls
 * ObjInitCommon with type 5 and then puts ADDR_MISSILE_ANIMS -- missile.ani --
 * into the row it builds, which is exactly how type 8 was settled as a roach
 * and type 3 as a vehicle. CLAUDE.md lists 5, 6 and 7 as unread; this is one
 * of the three, and the other two are still open.
 *
 * IT READS AN INDEX WHERE THE OBJECT HOLDS A POINTER, which is the same trade
 * LoadType1 makes with its save tag: the file gives a number, the loader turns
 * it into ADDR_MISSILE_DEFS + index * AM2_MISSILE_DEF_BYTES and stores that in
 * OBJ_OFF_BY_TYPE_94. A pointer written to disk would not survive a reload.
 *
 * THE FOUR DWORDS AFTER IT ARE READ BY POSITION and the offsets they land on
 * carry other types' names -- OBJ_OFF_RANK, OBJ_OFF_REPAIR_FRAME,
 * OBJ_OFF_PTR_LIST and OBJ_OFF_CHAIN_NEXT_UID. That is overloading, the same
 * as at 0x52C and 0x538, and the names are kept rather than duplicated: what
 * defines these four for a missile is the save format, and nothing here says
 * what they mean.
 *
 * The starting frame comes off the def record's first dword, with 2 and 5
 * taking one frame and everything else the other. What that dword IS has not
 * been read -- only which two values it treats alike.
 *
 * The bounding rect handed to BuildRowSet is ADDR_ZERO_RECT, which no code in
 * the image writes; three call sites use it the same way.
 */
AM2_Object *__cdecl LoadType5(am2_FILE *fp, AM2_Object *hdr)
{
    AM2_Object       *o = (AM2_Object *)am2_malloc(AM2_MISSILE_BYTES);
    const AM2_Object *h = (const AM2_Object *)hdr;
    int32_t        def;
    int32_t        frame;

    memset(o, 0, AM2_MISSILE_BYTES);

    o->owner = h->owner;

    ObjInitCommon(o, g_dirScratch,
                         AM2_OBJ_TYPE_MISSILE,
                         h->pos,
                         g_missileBox,
                         1, (int32_t)h->uid);

    memcpy(o, h, (size_t)g_itemHeaderSize);

    orig_fread(&def, 4, 1, fp);
    o->def =
        &g_missileDefs[def];

    orig_fread(&o->shooterUid, 4, 1, fp);
    orig_fread(&o->repairFrame, 4, 1, fp);
    orig_fread(&o->speedScale, 4, 1, fp);
    orig_fread(&o->flown, 4, 1, fp);

    switch (o->def->kind) {
    case 2:
    case 5:
        frame = AM2_MISSILE_FRAME_A;
        break;
    default:
        frame = AM2_MISSILE_FRAME_B;
        break;
    }

    BuildRowSet(&o->rowSet, 1,
                g_missileRowSpec,
                h->pos.x,
                h->pos.y,
                g_zeroRect);

    /* The row pointer is re-read at every use. */
    o->rowSet.rows->animCur =
        g_missileAnims;
    o->rowSet.rows->depthLayer = AM2_ROW_FIELD26_INIT;
    o->rowSet.rows->heading = o->facing;

    SetAnimFrame(o->rowSet.rows, (int16_t)frame, 1);
    return o;
}

/* LoadType8 -- original 0x0043CB60, one caller, and the ROACH member of the
 * per-type savegame loader family LoadType1 belongs to. Read the 0x4CC-byte
 * record, build a roach from the header, and paste the record over the
 * object's OBJ_OFF_BY_TYPE_94.
 *
 * A C++ LOCAL IS CONSTRUCTED INTO THE READ BUFFER AND THEN READ OVER. The
 * pointer-list constructor runs on buffer + 0x10 and the fread that follows
 * covers all 0x4CC bytes, so nothing the constructor wrote survives. What
 * makes the pair matter is the OTHER end: three dwords are zeroed at that same
 * address before the destructor runs, and without them the destructor would
 * free a pointer that came off the disk. So the ctor is the compiler's and the
 * explicit clear is the source's, defending against exactly that.
 *
 * THE HEALTH IS RESCALED RATHER THAN RESTORED, and the order is the whole of
 * it: take the saved health as a fraction of the saved maximum, THEN call
 * SetMaxHealth -- which replaces the maximum -- and multiply the new maximum
 * by that fraction. So a save made on one difficulty loads correctly on
 * another, and a roach that was on half health stays on half health rather
 * than keeping a number that no longer means anything. Clamped to at least 1,
 * so a rounding that would have killed it does not.
 *
 * IT CLEARS OBJ_FLAG_FOOTPRINT_ON on the way out, which is what makes the
 * loaded roach's footprint get laid down again by ObjSetRoachFootprint rather
 * than being assumed present from a flag the file supplied.
 *
 * The MSVC SEH prologue is not reproduced, per the standing decision recorded
 * in CLAUDE.md: nothing in this program throws, VC6's operator new answers
 * NULL rather than throwing, and the registered frame is never consulted.
 */
/* The record copy the type loaders end with. An inline call, not a bare
 * memcpy: VC6 then sets up the count and the source before the destination,
 * as the image does. */
static inline void CopyTypeRecord(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

/* A whole roach object on the stack -- the header's 0x94 bytes and the
 * saved record after them -- because that is what the original declares:
 * its frame is the object's size, and the file is read into it at +0x94.
 * Only the list at OBJ_OFF_PTR_LIST is modelled; being a class member, it
 * is what makes the construction and destruction the compiler's. */
struct AM2_Type8Obj {
    uint8_t     head[0x94];             /* the part the loader does not read */
    uint8_t     recStart[0x10];         /* +0x94 the saved record starts here and runs on */
    AM2_PtrList list;                   /* OBJ_OFF_PTR_LIST */
    uint8_t     padB0_[0x52C - 0xB0];
    int32_t     kind;                   /* +0x52C, what CreateRoach is given */
    uint8_t     rest_[0x94 + AM2_TYPE8_RECORD_SIZE - 0x530];
};
#if !defined(_MSC_VER)
static_assert(sizeof(AM2_Type8Obj) == 0x94 + AM2_TYPE8_RECORD_SIZE, "AM2_Type8Obj");
#endif

AM2_Object *__cdecl LoadType8(am2_FILE *fp, const AM2_Object *hdr)
{
    AM2_Type8Obj   obj;
    uint8_t       *rec = obj.recStart;
    const AM2_Object *h   = (const AM2_Object *)hdr;
    AM2_Object       *o;
    float          part;

    orig_fread(rec, AM2_TYPE8_RECORD_SIZE, 1, fp);

    o = CreateRoach(
            obj.kind,
            g_dirScratch,
            h->pos.x,
            h->pos.y,
            h->owner,
            (int32_t)h->flags, 1,
            (int32_t)h->uid);

    memcpy(o, h, (size_t)g_itemHeaderSize);
    CopyTypeRecord(&o->saveRec, rec, AM2_TYPE8_RECORD_SIZE);

    if (o->health > 0) {
        int32_t now;

        part = (float)o->health
               / o->maxHealth;
        SetMaxHealth(o, g_roachHealth);

        now = o->maxHealth;
        o->health = (int16_t)(int32_t)(now * part);
        o->health =
            (int16_t)Clamp(o->health, 1, now);
    }

    /* The list the record carried is a stale pointer from the file: the
     * object's copy is zeroed before anything can free it. */
    memset(&o->followers, 0, sizeof(AM2_PtrList));
    ResetObjOnCof(o);

    o->flags &= ~OBJ_FLAG_FOOTPRINT_ON;

    memset((void *)&obj.list, 0, sizeof obj.list);
    return o;
}

/* LoadType2 -- original 0x004471D0, one caller, and the TROOPER member of the
 * per-type savegame loader family. It had been DEFERRED: orig.h recorded ten
 * arguments for the creator below it and then said "what is NOT established:
 * the exact identity of four small stack locals around SoldierKindForWeapon
 * and the Type2Action arms".
 *
 * ALL FOUR ARE SETTLED AND THREE OF THEM ARE ONE MISTAKE. The first is a
 * four-byte fread AFTER the record -- the weapon code, and the default arm of
 * the kind switch hands it to SoldierKindForWeapon. The second is the per-uid
 * buffer the inventory loop reads into. The last two are a health PAIR saved
 * across the kind-7 handler and put back afterwards; they read as three
 * different slots because an `add esp, 4` sits between the writes and the
 * reads, so the same two words are named 0x84/0x86 going in and 0x84/0x82
 * coming out. Writing CreateTrooper and MakePlacedUnit first is what made this
 * legible: both do the CreateTrooper/weapon/SoldierKindForWeapon dance, and
 * knowing its shape is what separated the record's fields from the scratch.
 *
 * ITS THIRD ARGUMENT IS "RENUMBER". Clear, the trooper keeps the uid the file
 * gave it. Set, CreateTrooper is passed 0 and allocates a fresh one, the pair
 * goes to UidRemapAdd, the header takes the new value, OBJ_FLAG_NEEDS_REMAP
 * goes on and five record fields that named the old uid are zeroed. orig.h's
 * note on UidRemapAdd already said both its call sites "build a replacement
 * object and pass (old->uid, new->uid)"; this is one of them, so the two
 * readings meet.
 *
 * THE TWO ARMS DIFFER IN ONE ARGUMENT AND ARE OTHERWISE THE SAME CALL. Written
 * out twice, as the original has it -- factoring them would need a variable
 * for the uid and would hide that the renumbering arm has a whole tail the
 * other does not.
 *
 * THE KIND SWITCH IS ON OBJ_OFF_SOLDIER_KIND and three of its four arms clear
 * that field before they act, so a loaded trooper of kind 6, 7 or 8 comes back
 * as kind 0 with the handler's effect applied. The fourth takes the weapon
 * code instead. What the three kinds ARE is not established; the arms are
 * named for the callee each reaches.
 *
 * THE HEALTH RESCALE IS LoadType8's, with the maximum coming from the RANK
 * record indexed by OBJ_OFF_RANK rather than from a global.
 *
 * DELIBERATE DEVIATION -- the MSVC structured-exception frame is not
 * reproduced, per the standing decision.
 */
/* The trooper's, likewise: the whole object on the stack, the record read
 * in at +0x94, the one list at OBJ_OFF_PTR_LIST a class member. */
struct AM2_Type2Obj {
    uint8_t     head[0x94];             /* the part the loader does not read */
    uint8_t     recStart[0x10];         /* +0x94 the saved record starts here and runs on */
    AM2_PtrList list;                   /* OBJ_OFF_PTR_LIST, TYPE2_REC_OFF_LIST */
    uint8_t     padB0_[4];
    int32_t     scriptState;            /* +0xB4 TYPE2_REC_OFF_SCRIPT */
    uint8_t     padB8_[0xC4 - 0xB8];
    uint32_t    followUid;              /* +0xC4 TYPE2_REC_OFF_CLEAR_30 */
    uint8_t     padC8_[4];
    uint32_t    targetUid;              /* +0xCC TYPE2_REC_OFF_CLEAR_38 */
    uint8_t     padD0_[0x52C - 0xD0];
    int32_t     tableRec2Index;         /* +0x52C TYPE2_REC_OFF_SLOT */
    uint8_t     pad530_[0x538 - 0x530];
    int32_t     pose;                   /* +0x538 TYPE2_REC_OFF_POSE */
    uint8_t     pad53C_[0x564 - 0x53C];
    uint32_t    heldWeaponUid;          /* +0x564 TYPE2_REC_OFF_CLEAR_4D0 */
    uint8_t     pad568_[4];
    uint32_t    uid56C;                 /* +0x56C TYPE2_REC_OFF_CLEAR_4D8 */
    uint32_t    riding;                 /* +0x570 TYPE2_REC_OFF_CLEAR_4DC */
    uint8_t     rest_[0x94 + AM2_TYPE2_RECORD_SIZE - 0x574];
};
#if !defined(_MSC_VER)
static_assert(sizeof(AM2_Type2Obj) == 0x94 + AM2_TYPE2_RECORD_SIZE, "AM2_Type2Obj");
#endif

AM2_Object *__cdecl LoadType2(am2_FILE *fp, AM2_Object *hdr, int32_t renumber)
{
    AM2_Type2Obj obj;
    uint8_t *rec = obj.recStart;
    AM2_Object *h = (AM2_Object *)hdr;
    AM2_Object *o;
    int32_t  weaponCode;
    int32_t  uid;
    int32_t  i;
    float    part;

    orig_fread(rec, AM2_TYPE2_RECORD_SIZE, 1, fp);
    orig_fread(&weaponCode, 4, 1, fp);

    if (renumber) {
        o = CreateTrooper(
                g_dirScratch,
                h->pos.x,
                h->pos.y,
                obj.tableRec2Index,
                h->owner,
                (int32_t)h->flags, 1, 0, 0, 0);

        UidRemapAdd(h->uid, o->uid);
        h->uid = o->uid;

        obj.heldWeaponUid = 0;
        obj.followUid     = 0;
        obj.targetUid     = 0;
        obj.uid56C        = 0;
        obj.riding        = 0;
    } else {
        o = CreateTrooper(
                g_dirScratch,
                h->pos.x,
                h->pos.y,
                obj.tableRec2Index,
                h->owner,
                (int32_t)h->flags, 1,
                h->uid, 0, 0);
    }

    memcpy(o, h, (size_t)g_itemHeaderSize);
    CopyTypeRecord(&o->saveRec, rec, AM2_TYPE2_RECORD_SIZE);

    if (!(h->flags & OBJ_FLAG_BIT0)) {
        o->flags =
            (o->flags
             & ~(uint32_t)OBJ_FLAG_DESTROYED) | OBJ_FLAG_BIT0;
        DestroyByType(o);
        o->health = 0;
    }

    if (renumber)
        o->flags |= OBJ_FLAG_NEEDS_REMAP;

    o->faceTo = -1;
    o->inventorySel = 0;
    memset(&o->sight, 0, AM2_DEPLOY_CLEAR_DWORDS * 4);

    if (o->health > 0) {
        int32_t now;
        int32_t rank = o->rank;

        part = (float)o->health
               / o->maxHealth;
        SetMaxHealth(o, g_rankRecords[rank].max_health);

        now = o->maxHealth;
        o->health = (int16_t)(int32_t)(now * part);
        o->health =
            (int16_t)Clamp(o->health, 1, now);
    }

    o->tableRec2 =
        g_objTableRecords
        + obj.tableRec2Index * AM2_OBJ_TABLE_REC_SIZE;

    memset(&o->followers, 0, sizeof(AM2_PtrList));

    if (obj.list.count > 0) {
        for (i = 0; i < obj.list.count; i++) {
            orig_fread(&uid, 4, 1, fp);
            /* Renumbering DROPS the inventory rather than remapping it: the
             * uid is read and thrown away. The original's own test, not a
             * shortcut. */
            if (!renumber)
                ((AM2_Rec *)(&o->followers))->PtrListPush((void *)(uintptr_t)uid);
        }
    }

    switch (o->soldierKind) {
    case AM2_SOLDIER_KIND_ACTION_C:
        o->soldierKind = 0;
        Type2ActionC(o, o->kind7Army - 1);
        break;

    case AM2_SOLDIER_KIND_ACTION_A:
        o->soldierKind = 0;
        if (o->maxHealth
            > AM2_TYPE2_KEEP_HEALTH_OVER) {
            /* Kept across the call in the stack object's own health fields,
             * which the original uses as its temporaries. */
            AM2_Object *keep = (AM2_Object *)&obj;

            keep->maxHealth =
                o->maxHealth;
            keep->health =
                o->health;
            Type2ActionA(o);
            o->maxHealth =
                keep->maxHealth;
            o->health =
                keep->health;
        } else {
            Type2ActionA(o);
        }
        break;

    case AM2_SOLDIER_KIND_ACTION_B:
        o->soldierKind = 0;
        Type2ActionB(o);
        break;

    default:
        SoldierKindForWeapon(o, (uint32_t)weaponCode);
        break;
    }

    o->deadline58 =
        (int32_t)h->deadline58;
    o->scriptState =
        obj.scriptState;
    o->pose = 1;
    SetUnitPose(o, obj.pose);

    memset((void *)&obj.list, 0, sizeof obj.list);

    if (o->sarge)
        g_ourLeaderUid =
            o->uid;

    ResetObjOnCof(o);
    return o;
}

/* LoadType3 -- original 0x0045A120, one caller, and the VEHICLE member of the
 * per-type savegame loader family. LoadType8 above is its near twin and was
 * read beside it the whole way; what follows is the diff.
 *
 * IT REBUILDS TWO LISTS AND ONE POINTER OUT OF SAVED COUNTS. A vehicle carries
 * a {capacity, count, items} record at OBJ_OFF_PTR_LIST and a second at
 * VEHICLE_OFF_PTR_LIST, and pointers do not survive a reload -- so the file
 * holds a COUNT for each and that many uids after the record, and this reads
 * them one at a time and PtrListPushes each. Same trade as the third rebuild
 * beside them: OBJ_OFF_TABLE_REC_SLOT is written from a saved comm SLOT,
 * `ADDR_OBJ_TABLE_RECORDS + (slot << 8)`, where the object holds a pointer.
 *
 * THE TWO STACK RECORDS ARE THE FILE'S COPIES AND ARE DELIBERATELY EMPTIED. It
 * zeroes both {capacity, count, items} triples on the stack before the
 * destructors run, so the frees at the end free nothing -- the items they name
 * are now the object's. LoadType8 does exactly this with its one record; there
 * are two here.
 *
 * CreateVehicle's `table` is the saved SLOT, the same field the table-record
 * pointer is rebuilt from below. This read +0x494 -- "the one field not
 * already named" -- until the byte match put the load 0xC bytes further on:
 * every field the record hands over lands on one this file names (the kind,
 * the slot, the two lists), so the record is the object's own tail written
 * out.
 *
 * IT FORCES OBJ_OFF_RANK TO 5 AFTER copying the record over it, so whatever
 * the file said there is discarded. Reproduced; nothing read says what a rank
 * of 5 means to a vehicle.
 *
 * THE HEALTH RESCALE IS LoadType8's, with one difference that matters: the new
 * maximum comes from the vehicle DEFINITION rather than a global, through a
 * bsearch that can MISS -- and the whole rescale is skipped when it does, so a
 * vehicle whose type has no .aai entry keeps the health the file gave it.
 *
 * AND THE DEAD-VEHICLE ARM RUNS FIRST. A vehicle arriving without
 * OBJ_FLAG_BIT0, or with no health left, is forced to BIT0 set and
 * OBJ_FLAG_DESTROYED clear, handed to DestroyByType, and zeroed to no health
 * -- before the rescale, which then sees a health of zero and does nothing.
 * So the two are exclusive by construction rather than by a test.
 *
 * AND ITS LAST WRITE IS OBJ_FLAG_FOOTPRINT_ON, which went in as a structural
 * "bit 21" with nothing said about it -- and tools/checkoffsets.py refused the
 * new name as a second one on a flag that already existed. It is the same
 * clear LoadType8 makes. The sibling reading predicted it and the ratchet is
 * what proved it.
 *
 * DELIBERATE DEVIATION -- the MSVC structured-exception frame is not
 * reproduced, per the standing decision in CLAUDE.md and for the reason
 * LoadType8 already records: nothing in this program throws.
 */
/* The vehicle's: both lists, so the compiler builds and tears down two. */
struct AM2_Type3Obj {
    uint8_t     head[0x94];             /* the part the loader does not read */
    uint8_t     recStart[0x10];         /* +0x94 the saved record starts here and runs on */
    AM2_PtrList list;                   /* OBJ_OFF_PTR_LIST, TYPE3_REC_OFF_LIST */
    uint8_t     padB0_[0x52C - 0xB0];
    int32_t     vehicleKind;            /* +0x52C TYPE3_REC_OFF_KIND */
    uint8_t     pad530_[4];
    int32_t     tableRec3Index;         /* +0x534 TYPE3_REC_OFF_SLOT */
    AM2_PtrList riders;                 /* +0x538 VEHICLE_OFF_PTR_LIST */
    uint8_t     rest_[0x94 + AM2_TYPE3_RECORD_SIZE - 0x538 - sizeof(AM2_PtrList)];
};
#if !defined(_MSC_VER)
static_assert(sizeof(AM2_Type3Obj) == 0x94 + AM2_TYPE3_RECORD_SIZE, "AM2_Type3Obj");
#endif

AM2_Object *__cdecl LoadType3(am2_FILE *fp, const AM2_Object *hdr)
{
    AM2_Type3Obj   obj;
    uint8_t       *rec = obj.recStart;
    const AM2_Object *h = (const AM2_Object *)hdr;
    AM2_Object       *o;
    const AM2_VehicleDef *def;
    int32_t        i;
    uint32_t       uid;
    float          part;

    orig_fread(rec, AM2_TYPE3_RECORD_SIZE, 1, fp);

    o = CreateVehicle(obj.vehicleKind,
                                 g_dirScratch,
                                 h->pos.x,
                                 h->pos.y,
                                 obj.tableRec3Index,
                                 h->owner,
                                 (int32_t)h->flags, 1,
                                 (int32_t)h->uid, 0);

    memcpy(o, h, (size_t)g_itemHeaderSize);

    ClearPtrList(&o->followers);
    ClearPtrList(&o->riders);

    CopyTypeRecord(&o->saveRec, rec, AM2_TYPE3_RECORD_SIZE);

    o->tableRec3 =
        g_objTableRecords
        + (obj.tableRec3Index
           * AM2_OBJ_TABLE_REC_SIZE);
    o->rank = AM2_TYPE3_LOAD_RANK;

    memset(&o->followers, 0, sizeof(AM2_PtrList));

    if (obj.list.count > 0) {
        for (i = 0; i < obj.list.count; i++) {
            orig_fread(&uid, 4, 1, fp);
            ((AM2_Rec *)(&o->followers))->PtrListPush((void *)(uintptr_t)uid);
        }
    }

    memset(&o->riders, 0, sizeof(AM2_PtrList));

    if (obj.riders.count > 0) {
        for (i = 0; i < obj.riders.count; i++) {
            orig_fread(&uid, 4, 1, fp);
            ((AM2_Rec *)(&o->riders))->PtrListPush((void *)(uintptr_t)uid);
        }
    }

    /* Both stack records emptied so the destructors below free nothing. */
    memset((void *)&obj.list, 0, sizeof obj.list);
    memset((void *)&obj.riders, 0, sizeof obj.riders);

    ResetObjOnCof(o);

    if (!(o->flags & OBJ_FLAG_BIT0)
        || o->health <= 0) {
        o->flags =
            (o->flags & ~(uint32_t)OBJ_FLAG_DESTROYED)
            | OBJ_FLAG_BIT0;
        DestroyByType(o);
        o->health = 0;
    }

    def = VehicleDefFind(
              o->vehicleKind);

    if (o->health > 0 && def) {
        int32_t now;

        part = (float)o->health
               / o->maxHealth;
        SetMaxHealth(o, def->health);

        now = o->maxHealth;
        o->health = (int16_t)(int32_t)(now * part);
        o->health =
            (int16_t)Clamp(o->health, 1, now);
    }

    /* The same clear LoadType8 makes, and for the same reason: the loaded
     * vehicle's footprint gets laid down again rather than being assumed
     * present from a flag the file supplied. */
    o->flags &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;

    return o;
}

/* LoadType1 -- original 0x00433D60, one caller, and SaveType1's counterpart.
 *
 * The saver writes the 0x2C-byte type record and then a TAG taken from the
 * pointer inside it; this reads both back and uses the tag to build the
 * object. So the file carries a KIND where the object carries a POINTER, and
 * the round trip is: pointer -> tag on the way out, tag -> fresh object on the
 * way in.
 *
 * IT BUILDS THE OBJECT AND THEN OVERWRITES MOST OF IT. CreateItem makes a
 * live item from the header's uid, position and flags; the saved header is
 * then memcpy'd over its first ADDR_ITEM_HEADER_SIZE bytes and the saved type
 * record over its OBJ_OFF_BY_TYPE_94. What survives that is what the function
 * carefully saves in locals first:
 *
 *   - the FRESH object's OBJ_OFF_BY_TYPE_94 pointer, put back over the one the
 *     file supplied -- the mirror of the saver's tag, and the reason a loaded
 *     item points at this session's table rather than at a stale address;
 *   - the two 16-byte blocks at +0x20 and +0x30, saved before the header copy
 *     and restored after it, so the header's copies of them are discarded.
 *
 * Those two blocks are the only part of the header the loader refuses. The
 * rest of it -- every other field CreateItem just filled in -- is replaced
 * wholesale.
 *
 * IT ENDS BY REPLAYING TWO FRAMES, and that is a second reading for both
 * fields. OBJ_OFF_REPAIR_FRAME goes to ChangeObjectFrame with flag 0 and
 * OBJ_OFF_FORMATION_SLOT with flag 1, each only when positive -- so on a type
 * 1 those two are frame indices for two layers, which is one more piece of
 * evidence that 0xA0 is type-dependent exactly as orig.h suspects.
 *
 * A failed create returns 0 rather than the object, and nothing is read past
 * that point -- the two freads have already happened, so the file position is
 * correct either way.
 */
AM2_Object *__cdecl LoadType1(am2_FILE *fp, const AM2_Object *hdr)
{
    /* The whole object on the stack, the record read in at +0x94, as the
     * other loaders have it. */
    struct {
        uint8_t head[OBJ_OFF_BY_TYPE_94];
        uint8_t rec[AM2_TYPE1_RECORD_SIZE];
    } whole;
    uint32_t  tag;
    AM2_Object  *obj;
    const uint8_t     *ownRec;
    uint8_t   blockA[16];
    uint8_t   blockB[16];

    orig_fread(whole.rec, AM2_TYPE1_RECORD_SIZE, 1, fp);
    orig_fread(&tag, 4, 1, fp);

    obj = CreateItem(
        g_dirScratch, AM2_ARMY_NEUTRAL,
        (int32_t)tag,
        hdr->pos,
        (int32_t)hdr->flags, 1,
        hdr->uid);
    if (!obj)
        return (AM2_Object *)0;

    ownRec = obj->saveRec;
    memcpy(blockA, &obj->box, sizeof blockA);
    memcpy(blockB, &obj->hit, sizeof blockB);

    memcpy(obj, hdr, (size_t)g_itemHeaderSize);
    memcpy(&obj->saveRec, whole.rec, AM2_TYPE1_RECORD_SIZE);
    obj->saveRec = ownRec;

    memcpy(&obj->box, blockA, sizeof blockA);
    memcpy(&obj->hit, blockB, sizeof blockB);

    if (obj->repairFrame > 0)
        ChangeObjectFrame(obj, obj->repairFrame,
                          0);
    if (obj->formationSlot > 0)
        ChangeObjectFrame(obj,
                          obj->formationSlot, 1);

    return obj;
}


int32_t __cdecl SaveType6(am2_FILE *fp, AM2_Object *obj)
{
    orig_fwrite((uint8_t *)&obj->saveRec,
                AM2_TYPE6_RECORD_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl SaveType8(am2_FILE *fp, AM2_Object *obj)
{
    if (!obj)
        return 0;

    orig_fwrite((uint8_t *)&obj->saveRec,
                AM2_TYPE8_RECORD_SIZE, 1, fp);
    return 1;
}

/* Still original: the footprint pair SaveType3 brackets its write with, and
 * the weapon lookup SaveType2 tags through. */
typedef void (__cdecl *AM2_FootprintFn)(void *obj);

/* 0x00447130 and 0x0045A070 -- the two big per-type savers, and the two that
 * do more than write.
 *
 * EACH NORMALISES A POINTER AND PUTS IT BACK. The field holds a pointer to one
 * of the four 256-byte records at ADDR_OBJ_TABLE_RECORDS; the saver turns it
 * into `(p - base) >> 8`, writes the record, and restores the original as its
 * last act. So the FILE carries an index and the object is unchanged -- and
 * this file said the object was left holding the index for two commits,
 * because that was written from the heads of these two functions and the
 * restore is at the tail.
 *
 * TYPE 3 ALSO LIFTS ITS FOOTPRINT out of the map's cell weights before the
 * write and puts it back after. That is what a save function was doing calling
 * ADDR_OBJ_CLEAR_FOOTPRINT, which was recorded as an open question when
 * SaveType4 landed: the saved record is the object with its footprint lifted.
 *
 * TYPE 2'S TAG COMES FROM ITS WEAPON, or is 1 when it has none -- WeaponByUid
 * on the uid at TROOPER_OFF_WEAPON_UID, then a pointer at OBJ_OFF_BY_TYPE_C0 and
 * the dword it points at. A trooper who has dropped his weapon saves a 1 where
 * an armed one saves the weapon's own code, so the tag is not a constant and a
 * reader cannot skip it.
 *
 * Both write a trailing list of dwords when its count is positive, and type 3
 * writes a second such list. Neither length goes through WriteSaveTag, unlike
 * every other length in this format.
 */
int32_t __cdecl SaveType2(am2_FILE *fp, AM2_Object *obj)
{
    AM2_Object *o = obj;
    int32_t  rec, n;
    AM2_Object *weapon;

    if (!o)
        return 0;

    rec = o->tableRec2Index;
    if (rec)
        o->tableRec2Index =
            (int32_t)(((uint32_t)rec - ((uintptr_t)g_objTableRecords)) >> 8);

    orig_fwrite(&o->saveRec, AM2_TYPE2_RECORD_SIZE, 1, fp);

    weapon = WeaponByUid(
                 o->inventory[0]);
    if (weapon)
        WriteSaveTag(fp, (uint32_t)weapon->typeRec->kind);
    else
        WriteSaveTag(fp, 1u);

    n = o->followers.count;
    if (n > 0)
        orig_fwrite(o->followers.uids,
                    (size_t)n * 4, 1, fp);

    o->tableRec2Index = rec;
    return 1;
}

int32_t __cdecl SaveType3(am2_FILE *fp, AM2_Object *obj)
{
    AM2_Object *o = obj;
    int32_t  rec, n;

    if (!o)
        return 0;

    ObjClearFootprint((AM2_Object *)o);

    rec = o->tableRec3Index;
    if (rec)
        o->tableRec3Index =
            (int32_t)(((uint32_t)rec - ((uintptr_t)g_objTableRecords)) >> 8);

    orig_fwrite(&o->saveRec, AM2_TYPE3_RECORD_SIZE, 1, fp);

    n = o->followers.count;
    if (n > 0)
        orig_fwrite(o->followers.uids,
                    (size_t)n * 4, 1, fp);

    n = o->riders.count;
    if (n > 0)
        orig_fwrite(o->riders.uids,
                    (size_t)n * 4, 1, fp);

    o->tableRec3Index = rec;
    ObjSetFootprint((AM2_Object *)o);
    return 1;
}

/* 0x0045EF00 and 0x0043B800 -- two more per-type savers, and neither is a
 * plain write like the three above.
 *
 * TYPE 4 IS TYPE 1 PLUS THREE TAGS. It calls SaveType1 outright and gives up
 * if that fails -- the only saver in the family that delegates to another --
 * then writes three more tags out of its own record. So types 1 and 4 share a
 * layout for the first 0x2C bytes, which is the same pairing ADDR_STEP_TYPE1_4
 * shows on the frame-stepping side: one arm for both.
 *
 * TYPE 5 WRITES FIVE FIELDS AND SKIPS TWO. It tags the dword its record's
 * first field POINTS AT, tags the second field by value, and then writes
 * exactly three 4-byte fields -- at +0x08, +0x10 and +0x18 of the record --
 * leaving +0x0C and +0x14 out. Three separate four-byte fwrites where one of
 * twenty bytes would do, so the gaps are deliberate rather than a stride;
 * what is in them is not established.
 */
int32_t __cdecl SaveType4(am2_FILE *fp, AM2_Object *obj)
{
    if (!SaveType1(fp, obj))
        return 0;

    WriteSaveTag(fp, obj->lastUse);
    WriteSaveTag(fp, obj->pickupAfter);
    WriteSaveTag(fp, (uint32_t)obj->ammo);
    return 1;
}

int32_t __cdecl SaveType5(am2_FILE *fp, AM2_Object *obj)
{
    WriteSaveTag(fp, (uint32_t)obj->def->kind);
    WriteSaveTag(fp, obj->shooterUid);

    orig_fwrite(&obj->repairFrame, 4, 1, fp);
    orig_fwrite(&obj->speedScale, 4, 1, fp);
    orig_fwrite(&obj->flown, 4, 1, fp);
    return 1;
}

/* The ten-argument maker is CreateExplosion and is reconstructed in item.cpp,
 * whose header this file already includes. Four private typedefs of it existed
 * across three modules; all four are gone. */
#define orig_spawn_at CreateExplosion

/* 0x00422780, one caller. The type 6 loader.
 *
 * IT MAKES THE OBJECT OUT OF THE RECORD AND THEN OVERWRITES IT WITH THE SAME
 * RECORD. The 0x28 bytes are read onto the stack, three of their fields go to
 * ADDR_SPAWN_AT as the kind, the uid and one more, and the whole record is
 * then copied to OBJ_OFF_BY_TYPE_94 -- so whatever the maker derived from those
 * three is replaced by the saved values a moment later. Reproduced; the two
 * are the same numbers unless the maker changes them.
 *
 * The POSITION and the ARMY come from the header rather than the record, as
 * they do in LoadType7 -- the header is the part every type shares.
 *
 * The header copy happens BEFORE the record copy and they do not overlap: the
 * header is ADDR_ITEM_HEADER_SIZE bytes at 0x68 and the record starts at 0x94.
 * The 0x68..0x93 gap the save side leaves unwritten is left untouched here
 * too, so a loaded object carries whatever the maker put there.
 *
 * NO ERROR CHECKING ANYWHERE. The fread is unchecked and so is the maker's
 * answer, so a truncated file or an exhausted object table walks straight into
 * a null dereference. That is the original's, and SaveOneItem's careful
 * per-step checking has no counterpart on this side of the format.
 */
AM2_Object *__cdecl LoadType6(am2_FILE *fp, const AM2_Object *hdr)
{
    /* The whole object on the stack, the record read in at +0x94, as the
     * other loaders have it. */
    struct {
        uint8_t head[OBJ_OFF_BY_TYPE_94];
        struct {                        /* the explosion arm, TYPE6_REC_OFF_* */
            int32_t  kind;              /* +0x94 */
            int32_t  damage;            /* +0x98 */
            AM2_Rect rect;              /* +0x9C */
            uint32_t due;               /* +0xAC */
            int32_t  sound;             /* +0xB0 */
            int32_t  mode;              /* +0xB4 */
            uint32_t source;            /* +0xB8 */
        } rec;
    } obj;
    const AM2_Object *h = (const AM2_Object *)hdr;
    AM2_Object       *made;

    orig_fread(&obj.rec, AM2_TYPE6_RECORD_SIZE, 1, fp);

    made = (AM2_Object *)orig_spawn_at(
               h->pos.x,
               h->pos.y,
               obj.rec.kind,
               h->owner,
               obj.rec.source,
               obj.rec.damage,
               0, 1, (int32_t)h->uid, 0);

    memcpy(made, h, (size_t)g_itemHeaderSize);
    CopyTypeRecord(&made->saveRec, &obj.rec, AM2_TYPE6_RECORD_SIZE);
    return made;
}

/* 0x00435500, one caller. The type 7 loader, and the shortest of the nine.
 *
 * IT DOES NOT READ THE FILE AT ALL. Every other loader in the family starts
 * with an fread of its type's record; this one takes what it needs from the
 * HEADER LoadItemHeader has already read and builds the object from that. So
 * type 7 is the only type whose save side writes nothing (its arm is the
 * shared `return 1`) and whose load side reads nothing -- the pair is
 * consistent, which is worth checking rather than assuming when a saver looks
 * like a stub.
 *
 * The object is made by ADDR_MAKE_KIND7, which refuses a thirty-third, and
 * the WHOLE HEADER is then copied over it -- ADDR_ITEM_HEADER_SIZE bytes,
 * including the fields the creator was just given. A failed creation answers
 * 0 and copies nothing.
 */
AM2_Object *__cdecl LoadType7(am2_FILE *fp, const AM2_Object *hdr)
{
    const AM2_Object *h = (const AM2_Object *)hdr;
    uint8_t       *made;

    (void)fp;

    made = (uint8_t *)MakeKind7(h->pos, 1,
                                      h->owner,
                                      h->facing,
                                      1,
                                      (int32_t)h->uid);
    if (!made)
        return (AM2_Object *)0;

    memcpy(made, h, (size_t)g_itemHeaderSize);
    return (AM2_Object *)made;
}

/* 0x00428870, one caller -- SaveItems, once per registered object. Writes the
 * common header and then whatever the object's type adds.
 *
 * EVERY STEP IS CHECKED AND ANY FAILURE ANSWERS 0. The header first: if that
 * fails nothing type-specific is attempted. Then one of eight arms, and an
 * out-of-range type answers 1 -- so an object of an unknown type is saved as
 * a header and NOT treated as an error, which is the opposite of what the
 * per-step checks suggest.
 *
 * TYPE 7 SAVES NOTHING TYPE-SPECIFIC. Its arm calls ADDR_RETURN_ONE, a shared
 * `return 1`, rather than being special-cased out of the switch. That is why
 * there are eight arms and not seven, and it is worth reproducing as the call
 * it is: a reconstruction that dropped the arm would agree today and diverge
 * the moment that stub stopped returning 1.
 *
 * The original hands that stub the file and the object like every other arm,
 * and it takes no arguments -- cdecl, so the two extra pushes are harmless and
 * ReturnOne is called here with none. The pushes are the compiler emitting one
 * arm shape eight times, not a signature.
 *
 * THE MAPPING IS CORROBORATED BY THE LOAD SIDE, structurally. Every loader
 * ADDR_LOAD_ONE_ITEM calls sits IMMEDIATELY AFTER its saver in the image --
 * eight adjacent pairs, in the same order in both dispatch tables. That is
 * better evidence than two readings agreeing, because it is a fact about the
 * layout rather than about my reading of either.
 *
 * VERIFIED BY READING. Its caller is the savegame writer; nothing in the
 * suite saves. */
int32_t __cdecl SaveOneItem(am2_FILE *fp, AM2_Object *obj)
{
    if (!SaveItemHeader(fp, obj))
        return 0;

    switch (*(const int32_t *)obj) {
    case 1:  if (!SaveType1(fp, obj)) return 0; break;
    case 2:  if (!SaveType2(fp, obj)) return 0; break;
    case 3:  if (!SaveType3(fp, obj)) return 0; break;
    case 4:  if (!SaveType4(fp, obj)) return 0; break;
    case 5:  if (!SaveType5(fp, obj)) return 0; break;
    case 6:  if (!SaveType6(fp, obj)) return 0; break;
    case 7:  if (!ReturnOne(fp, obj)) return 0; break;   /* the stub; see above */
    case 8:  if (!SaveType8(fp, obj)) return 0; break;
    default: break;          /* unknown type: header only, and NOT an error */
    }
    return 1;
}

typedef int32_t (__cdecl *AM2_LoadHdrFn)(am2_FILE *fp, void *hdr);
typedef void *(__cdecl *AM2_LoadObjFn)(am2_FILE *fp, void *hdr);
typedef void *(__cdecl *AM2_LoadObj3Fn)(am2_FILE *fp, void *hdr, int32_t a);
/* LoadType4 is reconstructed below; gameproc.h declares it. */

/* 0x004289E0, SaveOneItem's counterpart. Reads a header onto the stack and
 * hands it to the type's loader, which builds the object and RETURNS it.
 *
 * IT RETURNS THE OBJECT, NOT A FLAG. orig.h said `void`; every exit answers
 * either the created object or NULL, and the failure exits get their NULL by
 * falling out with the loader's own return value still in place.
 *
 * THE FOOTPRINT BIT MAKES A ROUND TRIP AROUND CONSTRUCTION. Bit 0x200000 is
 * taken out of the header's flags and CLEARED before the loader sees them,
 * then put back on the finished object -- set or cleared to match what was
 * saved. Constructing with it set would presumably stamp a footprint into the
 * map that the load has not placed yet; whatever the reason, the order is the
 * point and a reconstruction that simply copied the flags through would be
 * wrong only for objects that have it.
 *
 * THEN THE SELECTED BIT IS CLEARED UNCONDITIONALLY, in a second write to the
 * same field. A loaded object never comes back selected, however it was saved.
 * Two writes to flags in three instructions, and they are doing different
 * jobs.
 *
 * TYPE 2 TAKES A THIRD ARGUMENT and the other seven do not -- the caller's own
 * second parameter, passed through. That asymmetry is in the dispatch, not in
 * the loaders, so it cannot be tidied into a common signature.
 *
 * An unknown type falls through to the common tail like SaveOneItem does, but
 * with no object made -- so it dereferences NULL. SaveOneItem's equivalent
 * path is harmless; this one is not. The original does not guard it and
 * neither does this: LoadItems only ever feeds it types it has just written.
 *
 * VERIFIED BY READING. Nothing in the suite loads a savegame. */
AM2_Object *__cdecl LoadOneItem(am2_FILE *fp, int32_t arg)
{
    uint8_t  buf[AM2_ITEM_HEADER_BYTES];
    AM2_Object *hdr = (AM2_Object *)(void *)buf;   /* the leading 0x94 bytes only */
    AM2_Object *made = NULL;
    uint32_t footprint;

    if (!LoadItemHeader(fp, hdr))
        return NULL;

    footprint = hdr->flags & OBJ_FLAG_FOOTPRINT_ON;
    hdr->flags &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;

    /* Each loader's null test is inside its own case. DEFAULT HAS NONE: a
     * type outside 1..8 falls to the tail with `made` still NULL and reads
     * its flags through it -- the original's, and reproduced. */
    switch ((int32_t)hdr->type) {
    case 1:
        made = LoadType1(fp, hdr);
        if (!made)
            return NULL;
        break;
    case 2:
        made = LoadType2(fp, hdr, arg);
        if (!made)
            return NULL;
        break;
    case 3:
        made = LoadType3(fp, hdr);
        if (!made)
            return NULL;
        break;
    /* THREE arguments, like case 2 and unlike the other six. The seam this
     * replaced declared two -- AM2_LoadObjFn, shared with the readers that
     * really do take two -- so the renumber flag was never passed and case 4
     * read whatever was on the stack for it. The call site settles it: it
     * pushes three and cleans 0xC. */
    case 4:
        made = LoadType4(fp, hdr, arg);
        if (!made)
            return NULL;
        break;
    case 5:
        made = LoadType5(fp, hdr);
        if (!made)
            return NULL;
        break;
    case 6:
        made = LoadType6(fp, hdr);
        if (!made)
            return NULL;
        break;
    case 7:
        made = LoadType7(fp, hdr);
        if (!made)
            return NULL;
        break;
    case 8:
        made = LoadType8(fp, hdr);
        if (!made)
            return NULL;
        break;
    }

    if (footprint)
        made->flags |= OBJ_FLAG_FOOTPRINT_ON;
    else
        made->flags &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;
    made->flags &= ~(uint32_t)OBJ_FLAG_SELECTED;

    ApplyObjHeight(made,
                          (int32_t)made->heightSet);
    return made;
}

/* ---------------------------------------------- the uid remap table ---- */


/* 0x00427650 and 0x00427680, two callers each: the two halves of a growable
 * array of (from, to) uid pairs.
 *
 * WHAT THE TABLE IS COMES FROM THE ONE FUNCTION THAT READS IT, which is still
 * original. 0x004276F0 walks a unit's six UNIT_OFF_INVENTORY slots and, for
 * each, scans this table for a record whose first dword equals the uid in the
 * slot, then writes that record's SECOND dword back. So the pairs are a rename
 * map, which is what a load needs: a saved uid means nothing in the new
 * session. Neither of these two functions would say that on its own -- an
 * append and a free are the same shape whatever the records mean.
 *
 * THE THREE GLOBALS ARE capacity, count, pointer, IN THAT ORDER, and getting
 * that backwards would be invisible until the first grow. The compare at
 * 0x0042768B is `count < capacity` and it decides whether to grow, which is
 * what settles which is which; the alternative reading makes a full table look
 * empty and an empty one look full, and both still append correctly to
 * whatever memory is there.
 *
 * NEITHER CHECKS ITS ALLOCATOR. The capacity is raised BEFORE the realloc and
 * kept whether or not the realloc succeeded, so a failure leaves the table
 * claiming space it does not have and the very next append writes past the
 * end. Reproduced; it is the original's, and a load that cannot allocate 80
 * bytes has lost already.
 *
 * The grow is ten records, and the first append reallocs from a NULL pointer,
 * which is a malloc. Both are the original's spelling and neither needs a
 * special case here for the same reason it does not there.
 *
 * BOTH APPEND SITES CONFIRM THE PAIR ORDER INDEPENDENTLY OF THE READER, which
 * is worth more than the reader alone. 0x00447278 and 0x0045F013 each build a
 * REPLACEMENT object, call this with (old->uid, new->uid), and then write the
 * new uid into the old object's own field. So a record is (from, to) and the
 * table exists because an object was recreated, not only because a file was
 * loaded -- three sites agreeing beats one.
 *
 * MEASURED AT 0 ON BOTH DRIVES. All four callers are the original's and reach
 * these by address, so the counters are not blind; Boot Camp with movement and
 * fire, and the campaign through SELECT PLAYER into MAP 01, leave both at 0
 * while BlockWeightAt reads 8 and 6 on those same two runs. Verified by
 * reading, and the reading is the three-site agreement above rather than the
 * bodies, which are an append and a free and say nothing on their own.
 */
void __cdecl UidRemapClear(void)
{
    if (g_uidRemap)
        am2_free(g_uidRemap);

    /* {cap, count, table}: one 12-byte clear, as the image's single
     * zeroed register stored three times says. */
    memset((void *)&g_uidRemapCap, 0, 12);
}

void __cdecl UidRemapAdd(uint32_t from, uint32_t to)
{
    if (g_uidRemapCount >= g_uidRemapCap) {
        g_uidRemapCap += AM2_UID_REMAP_GROW;
        g_uidRemap = (uint32_t *)am2_realloc(
                         g_uidRemap, (size_t)(g_uidRemapCap * 8));
    }

    g_uidRemap[g_uidRemapCount * 2]     = from;
    g_uidRemap[g_uidRemapCount * 2 + 1] = to;
    g_uidRemapCount++;
}

/* ---- Six small teardown steps -----------------------------------------
 *
 * Four are called from the level teardown at 0x00425300 and share one shape:
 * free something, then TAIL-JUMP to the logger with no arguments. The log
 * line is the game's own -- these are the steps, not the messages -- so none
 * of them names itself and all of their names here are structural.
 *
 * They are reconstructed together because each is between sixteen and
 * thirty-two bytes and not one is worth a commit of its own. Their callees
 * stay original and are reached by address; what these functions ARE is the
 * order they run in, exactly as ShutdownSubsystems' thirteen turned out to
 * be, and that order is now in the source rather than in the image.
 *
 * A NO-ARGUMENT TAIL JUMP TO THE LOGGER IS NOT Log(""). Reproduced through a
 * no-argument pointer, as frame.cpp does in three places; the varargs call
 * with none is what the original makes.
 */
typedef void (__cdecl *AM2_TeardownFn)(void);
typedef void *(__cdecl *AM2_OperatorNewFn)(uint32_t size);
typedef void  (__cdecl *AM2_OperatorDeleteFn)(void *p);
#define orig_operator_new \
    ((AM2_OperatorNewFn)((void *)(uintptr_t)(ADDR_GAME_OPERATOR_NEW)))
#define orig_operator_delete \
    ((AM2_OperatorDeleteFn)((void *)(uintptr_t)(ADDR_GAME_DELETE)))
typedef void (__cdecl *AM2_Call3Fn)(int32_t a, int32_t b, int32_t c);

#define orig_teardown_log   ((am2_log_fn)(uintptr_t)ADDR_LOG)
/* TeardownDefTables has the SAME tail-jump shape and its one caller,
 * LevelTeardown, pushes NOTHING -- the stack is balanced by an `add esp, 8`
 * well before the call. So in the original its Log inherits whatever is
 * there, which is not a message it chose, and there is nothing to pass on.
 * Left as a no-argument call rather than invented. */
#define orig_teardown_log0  ((AM2_TeardownFn)(uintptr_t)ADDR_LOG)
/* 0x0045EDF0 is FreeVehicleDefs now; definfo.h declares it. */
/* 0x004607D0 is FreeMissileDefs now; definfo.h declares it. */
/* 0x0040A4B0 is BuildRemapTables, reconstructed in win32/palette.cpp and
 * called by name -- see the note below. Declared here rather than by
 * including that header: gameproc.cpp is on the flat side of the split and
 * palette.h names Win32 types, while this one signature names none. */
extern "C" void __cdecl BuildRemapTables(void);

/* 0x0041E740. Zero a global NOTHING READS. One reference in the whole image
 * -- this store -- confirmed by a decoded scan and a raw dword scan both. So
 * the teardown clears it every time and no code anywhere looks. Reproduced;
 * a write to a dead global is still a write, and dropping it would be the
 * first thing to go wrong if something starts reading it. */
void __cdecl ZeroUnread50C34C(void)
{
    g_unread50c34c = 0;
}

/* 0x00402670. Its answer is always 0 and its one caller ignores it, so the
 * only thing it does is the flag. */
int32_t __cdecl NoteKind31(AM2_MsgNode *rec)
{
    if (((const AM2_CommMsgHdr *)rec->body)->kind == AM2_KIND_31)
        g_commObject->sawKind31 = 1;

    return 0;
}

/* 0x004057B0, two callers. A pass-through and nothing else: three arguments
 * forwarded to a 1,424-byte function that has two other callers of its own. */
void __cdecl Call405220(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    AiGuardStep(o, out, c);
}

/* 0x00445FE0. Free, then log -- and the MESSAGE IS AN ARGUMENT.
 *
 * The body is `call 0x00445F40` then `jmp ADDR_LOG`. A tail jump leaves the
 * frame alone, so Log inherits this function's first argument as its format.
 * This was reconstructed as `void(void)` calling Log with nothing.
 *
 * PROVED BY AN A/B, not by reading. Nothing could catch it while its only
 * caller -- State2Enter -- was the image's, because the original pushed the
 * string and our stub ignored it, and both halves of every run behaved the
 * same. Reconstructing that caller made ours pass nothing, and `ab.sh
 * bootcamp` came back with the log line "data\bootcamp" on the original
 * side only, and 189,990 pixels.
 *
 * The string is ADDR_DIR_SCRATCH, so what it logs is the map directory the
 * level was loaded from. CLAUDE.md's rule that a no-format Log call must not
 * be reproduced is about a call with TWO arguments and no format; this one
 * has exactly one and it IS the format. */
void __cdecl Teardown445F40(const char *msg)
{
    FreeSpriteRegistry();
#ifdef _MSC_VER
    /* The original is `jmp Log` with nothing pushed: Log finds msg in this
     * function's own argument slot. VC6 tail-jumps a no-argument call, so
     * that is how it is written for VC6; GCC promises no tail jump, so it
     * passes msg for real. The two log the same line. */
    orig_teardown_log0();
    (void)msg;
#else
    orig_teardown_log(msg);
#endif
}

/* 0x004033E0. Free four things, then log. Two of the four already had names,
 * which is the only reason this one reads as anything but four addresses. */
void __cdecl TeardownDefTables(void)
{
    DefFreeTables();
    FreeVehicleDefs();
    FreeMissileDefs();
    DefFreeTrooperRecs();
    orig_teardown_log0();
}


/* 0x0040A5F0 and 0x0040A660, the two halves of the ARMY OBJECT LISTS: one
 * 12-byte {capacity, count, items} record per comm slot, in the four-entry
 * array at ADDR_ARMY_OBJ_LISTS.
 *
 * The loop bound is the NEXT GLOBAL -- both walk to 0x004F9EDC, which is
 * ADDR_ROW_LUT_DOUBLES -- so the array's length is four because that is where
 * the neighbour starts, the same way the registration table's nine buckets
 * were settled. AM2_COMM_SLOTS is that four.
 *
 * THE INIT STORES THE CONSTRUCTOR'S RETURN, not the pointer it allocated.
 * They are the same value, because InitPtrList answers `this` like every i386
 * MSVC constructor -- misc.h records that and notes no caller had reached it
 * yet. This is that caller, and it is written the way the original does it.
 * A failed allocation stores a null and the loop carries on.
 *
 * THE FREE DOES NOT CLEAR THE SLOTS. It empties each record and frees it, and
 * leaves the array holding dangling pointers -- reproduced, not tidied.
 *
 * It is a `new` of the list class, which is where the init's unwind frame
 * comes from: VC6 wraps the constructor so a throw would free the block. */

void __cdecl InitArmyObjLists(void)
{
    AM2_PtrList **slot;
    int32_t i;

    slot = g_armyObjLists;
    for (i = 0; i < AM2_COMM_SLOTS; i++)
        slot[i] = new AM2_PtrList;
}

void __cdecl FreeArmyObjLists(void)
{
    AM2_PtrList **slot;
    int32_t i;

    slot = g_armyObjLists;
    for (i = 0; i < AM2_COMM_SLOTS; i++) {
        AM2_PtrList *list = slot[i];

        if (list) {
            ClearPtrListAlias(list);
            orig_operator_delete(list);
        }
    }
}

/* 0x0040A690. Two steps, the second a tail jump -- and both halves sit inside
 * ONE functions.tsv entry, which is why the second has no entry of its own
 * and why tools/merges.py is what finds it.
 *
 * THE DEFERRED RENAME IS SETTLED. This carried "Teardown" under a comment
 * saying the name was known wrong and was waiting on one fact: whether the
 * second step frees anything. It does not -- that step is InitArmyObjLists
 * above, which ALLOCATES four records. So both halves build: BuildRemapTables
 * fills the army remaps, and this fills the object lists. Renamed on the whole
 * evidence, which is what the half-evidence rename failed to do. */
void __cdecl InitRemapsAndLists(void)
{
    BuildRemapTables();
    InitArmyObjLists();
}

/* ---- Six more, same size and same argument ----------------------------
 *
 * ONE MORE THREE-ARGUMENT PASS-THROUGH, which makes three in this tree with
 * Call405220 above. Each forwards its three arguments to one large function
 * and adds nothing at all. The compiler did not produce these: a thunk that
 * only moves arguments is what a source-level wrapper compiles to, so the
 * original had these as one-line functions and so does this. Worth knowing
 * before reading one as a place where something happens.
 *
 * THERE WERE FOUR AND ONE HAS LEFT. Call407710 was in this group until the AI
 * mode dispatcher was read: it is the `attack` arm, mode 6, and it now sits in
 * region.cpp with the other five as AiStepAttack. A function grouped by SHAPE
 * moves out the moment its subsystem is identified -- the shape was never the
 * reason it belonged anywhere.
 */
typedef void (__cdecl *AM2_Call3Fn2)(int32_t a, int32_t b, int32_t c);
typedef void (__cdecl *AM2_VoidFn)(void);
typedef void (__cdecl *AM2_WalkCellFn)(const uint32_t *pt, void *desc,
                                       void *fn);

/* 0x00460290 is DefSortMissileRecs now; defparse.h declares it. */

void __cdecl Call4057D0(AM2_Object *o, AM2_SightOut *out, AM2_SightCtx *c)
{
    AiPatrolStep(o, out, c);
}


/* 0x0041A230. Four calls and a tail jump, all five into the def tables:
 * DefSortTrooperRecs, one unnamed, DefSortObjRecs, DefCheckLinks and one
 * more -- three of the five are ours now, and the two sorts arrived a
 * commit later than this function did. The ORDER is
 * the fact here -- DefCheckLinks needs the link table sorted, which the step
 * before it is presumably what does. Five invented names would say less than
 * five addresses in the right sequence. */
void __cdecl DefFinish(void)
{
    DefSortTrooperRecs();
    DefSortMissileRecs();
    DefSortObjRecs();
    DefCheckLinks();
    SortVehicleDefs();
}

/* 0x0044A3A0, two callers. Walk the objects in the cell a point falls in,
 * calling ADDR_WALK_CELL_CALLBACK for each -- the same cell arithmetic
 * ObjectsAtPoint uses, with a callback instead of a chain.
 *
 * ITS FIRST ARGUMENT IS NOT READ. Both callers push an object into it and
 * this function never touches it; what it passes on is the ADDRESS of its
 * second argument, which is the packed point. That is the same "the point
 * arrives by value and its address is taken" shape orig.h records for
 * RevealNearby, one step further -- here the argument slot IS the storage.
 * Reproduced, unused parameter and all. */
/* IT RETURNS ITS CALLEE'S ANSWER, and this said `void` until a caller wanted
 * it. The original is a tail call with no `xor eax, eax` after it, so eax is
 * WalkCellAtPoint's -- the object standing at the point, or null. Nothing in
 * the tree read it, so the wrong prototype was invisible; Type2PlayerInput
 * tests it and asks the result for its height and uid. */

/* 0x0044A380, ADDR_WALK_CELL_CALLBACK -- the FILTER WalkCellWrapper hands
 * WalkCellAtPoint. Its answer decides whether an object joins the chain.
 *
 * `neg eax; sbb eax, eax; inc eax` is the compiler's `== 0`, so the filter
 * keeps exactly the objects our army is NOT allied with: enemies. Written as
 * the negation rather than transcribed as three instructions. */
int32_t __cdecl WalkCellKeepEnemy(AM2_Object *obj)
{
    return ArmyAlliedWithObj((int32_t)g_defaultOwner, (const AM2_Object *)obj, 0) == 0;
}

AM2_Object *__cdecl WalkCellWrapper(void *unused, AM2_Point at)
{
    (void)unused;

    /* The callback is ours now, so it goes in by NAME. It used to be handed
     * over by address, as a typed function pointer rather than a void *. */
    return WalkCellAtPoint(&at, g_objMapDesc,
                           WalkCellKeepEnemy);
}

/* 0x0042F140. Undo what HostBattle set up: reset the pair mask, put 1000 in
 * the value beside it, and EMPTY BOTH SAVED NAMES -- by storing a zero byte
 * at the front of each, not by clearing the buffer. The two names are
 * ADDR_SAVED_PLAYER_NAME and ADDR_SAVED_BATTLE_NAME, which is what makes this
 * the reset rather than an unrelated three-global write. */
void __cdecl ResetHostState(void)
{
    ResetPairMask(&g_hostMaskA,
                  &g_hostMaskB);

    g_hostValue3e8 = 1000;
    g_savedPlayerName = '\0';
    g_savedBattleName = '\0';
}

/* OpenSaveForLoad -- original 0x00425950, one caller.
 *
 * Open the current save for reading and hand back the open FILE, positioned at
 * byte zero, or NULL. Four things have to hold: both names must be non-empty,
 * the file must open, its first section must carry the gameproc tag, and
 * LoadGameproc must accept it.
 *
 * THE TWO EMPTINESS TESTS READ ONE BYTE EACH, not a length -- `mov al,[name];
 * test al,al` -- so "no save selected" is spelled as an empty string. They are
 * checked in the opposite order to the order they are USED: the file name is
 * tested first and the directory name second, while the directory is what gets
 * formatted and chdir'd into first.
 *
 * IT LEAVES THE PROCESS IN THE SAVE DIRECTORY. SetGameDir is a chdir and
 * nothing here puts it back, on any path including the two failures after it.
 * The caller owns that, and every later relative path depends on it.
 *
 * THE REWIND IS THE POINT OF THE FUNCTION. Both checks consume from the
 * stream -- CheckSaveTag reads four bytes and LoadGameproc reads its whole
 * 0x438-byte block -- so without the fseek the caller would resume in the
 * middle. It rewinds to 0 rather than to just past the header, so the caller
 * reads the gameproc section again for itself.
 *
 * A FAILED CHECK CLOSES THE FILE, and a failed OPEN does not -- there is
 * nothing to close, and the same `return` serves both by answering the null
 * pointer it already has.
 *
 * LoadGameProcSection is ours, further down this file, and is called by name;
 * checkseams caught the orig_ macro that went in first out of habit. Fourth
 * time this session, and the reflex is now well enough attested to be worth
 * inverting: when writing a call into the image, assume the callee is already
 * reconstructed and go looking for the proof, rather than the other way round.
 *
 * The line number handed to CheckSaveTag is the original's `__LINE__`, 1320,
 * from a source file whose name the binary still carries. It is passed
 * verbatim: it names a line in the 1999 source, not in this one.
 */
am2_FILE *__cdecl OpenSaveForLoad(void)
{
    char      path[AM2_SAVE_PATH_BYTES];
    am2_FILE *fp;

    if (!g_gameprocStrB[0])
        return (am2_FILE *)0;
    if (!g_gameprocBlock[0])
        return (am2_FILE *)0;

    am2_sprintf(path, "save\\%s",
                 g_gameprocBlock);
    SetGameDir(path);

    fp = orig_fopen(g_gameprocStrB,
                    "rb");
    if (fp) {
        if (!CheckSaveTag(fp, AM2_SAVETAG_GAMEPROC,
                          "C:\\ArmyMen2\\source\\gameproc.cpp",
                          AM2_GAMEPROC_TAG_LINE))
            goto fail;      /* one failure block, entered from both tests */
        if (!LoadGameProcSection(fp)) {
fail:
            orig_fclose(fp);
            return (am2_FILE *)0;
        }
        orig_fseek(fp, 0, 0);
    }
    return fp;
}

/* The selected-units list is a static C++ global, and these five are the
 * boilerplate MSVC emits around one. See orig.h for the shape, and for the
 * measurement that says all of it runs -- the harness patches before the CRT
 * initterm, so SelListInit executes under our code rather than ahead of it.
 *
 * What makes the group worth naming rather than skipping is where the object
 * lives: base + GAMEPROC_OFF_SELECTED resolves to ADDR_SELECTED_UIDS, the
 * {capacity, count, items} list that DrawSelection and twenty other sites
 * read. Nothing had recorded that the selection list is a MEMBER of the
 * gameproc block. The arithmetic is written the way the original writes it,
 * rather than as a direct reference to the absolute address, so that fact
 * survives in the code and not only in a comment. */

/* 0x004248E0, thiscall. Construct the member and answer the OUTER object --
 * `mov eax, esi` restores the block, not the list, which is what a C++
 * constructor returns. */
void * __attribute__((thiscall)) AM2_SelList::SelListConstruct()
{
    InitPtrList(this->selected);
    return this;
}

/* 0x004248D0, thiscall. One `add ecx` and a tail jump. */
void __attribute__((thiscall)) AM2_SelList::SelListDestruct()
{
    ClearPtrListAlias(this->selected);
}

/* 0x004248A0 and 0x004248C0: the two thunks that name the object. Each is
 * `mov ecx, <block>; jmp <body>`, so they are the only place the global is
 * mentioned. */
void *__cdecl SelListConstructThunk(void)
{
    return SelListConstruct((void *)g_gameprocBlock);
}

void __cdecl SelListDestructThunk(void)
{
    SelListDestruct((void *)g_gameprocBlock);
}

/* 0x004248B0. Hands the destructor thunk to the CRT's atexit and returns its
 * answer -- the `ret` follows the call with eax untouched, so the result is
 * atexit's 0 or -1 and not a value of its own.
 *
 * It passes OUR thunk rather than the image address the original pushes. The
 * two are the same registration: the original address holds a detour to this
 * code by the time exit runs. Naming the patched address here would be the lie
 * tools/checkseams.py exists to catch, and it would buy one extra hop. */
/* A compiler-generated $E function in the original, which VC6 emits
 * optimised for size: the argument is popped, not added off. */
#if defined(_MSC_VER)
#pragma optimize("s", on)
#endif
int32_t __cdecl SelListAtExit(void)
{
    return orig_atexit(SelListDestructThunk);
}
#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

/* 0x00424890. The initialiser-table entry: construct, then register the
 * teardown. Its only reference in the image is a dword at 0x00473048, so it is
 * reached by `refs_to` and never by `xrefs`. */
int32_t __cdecl SelListInit(void)
{
    SelListConstructThunk();
    return SelListAtExit();
}


/* 0x00424BB0. Copy five palette indices into ADDR_STARTUP_COLOURS at startup,
 * one of the C++ static initialisers _initterm runs before WinMain.
 *
 * ALL FIVE BYTES ARE READ BY NOTHING. Each of 0x00512340..0x00512344 has
 * exactly one reference in the image and it is the store below -- checked by
 * scanning every absolute reference, which is the method this project uses to
 * prove a field has no writer, run the other way round. Reproduced because
 * the original does it, not because anything depends on it.
 *
 * The five sources are all named colours, which makes the array's CONTENT
 * legible even though its purpose is not: the view rectangle's, the two
 * latency shades, blue, and the HUD message colour.
 *
 * The original interleaves its loads and stores, which is the scheduler
 * rather than an ordering that matters -- the sources and the destination do
 * not overlap. */
void __cdecl InitStartupColours(void)
{
    uint8_t *out = g_startupColours;

    out[0] = g_hiliteColour;
    out[1] = g_colourLagMid;
    out[2] = g_colourBlue;
    out[3] = g_colourStale;
    out[4] = g_hudColour;
}


/* 0x004623A0, the TWIN of InitStartupColours above, and it fails the same way.
 *
 * Four bytes rather than five, into a different array, from four of the same
 * named colour globals -- and every one of ADDR_STARTUP_COLOURS_B's bytes has
 * exactly ONE reference in the image: this store. No reader, for the same
 * reason: _initterm runs before anything sets those colours, so it captures
 * .bss zeros and nothing refreshes them.
 *
 * Two independent instances of one dead pattern is worth more than one. The
 * first could have been an oversight in a single function; a second, writing
 * a different array from an overlapping set of sources, says the original
 * had a habit rather than a slip. */
void __cdecl InitStartupColoursB(void)
{
    uint8_t *out = g_startupColoursB;

    out[0] = g_hiliteColour;
    out[1] = g_colourLagMid;
    out[2] = g_colourNoMap;
    out[3] = g_hudColour;
}

void gameproc_install(void)
{
    patch_replace(ADDR_LOAD_OPTIONS, (const void *)LoadOptions,
                  "LoadOptions", 1);
    patch_replace(ADDR_PLACE_SCENARIO, (const void *)PlaceScenario,
                  "PlaceScenario", 1);
    patch_replace(ADDR_OPEN_SAVE_FOR_LOAD, (const void *)OpenSaveForLoad,
                  "OpenSaveForLoad", 1);
    patch_replace(ADDR_ZERO_50C34C, (const void *)ZeroUnread50C34C,
                  "ZeroUnread50C34C", 1);
    patch_replace(ADDR_CALL_4057D0, (const void *)Call4057D0, "Call4057D0", 1);
    patch_replace(ADDR_DEF_FINISH, (const void *)DefFinish, "DefFinish", 1);
    patch_replace(ADDR_WALK_CELL_WRAPPER, (const void *)WalkCellWrapper,
                  "WalkCellWrapper", 2);
    patch_replace(ADDR_RESET_HOST_STATE, (const void *)ResetHostState,
                  "ResetHostState", 1);
    patch_replace(ADDR_NOTE_KIND_31, (const void *)NoteKind31, "NoteKind31", 1);
    patch_replace(ADDR_CALL_405220, (const void *)Call405220, "Call405220", 2);
    patch_replace(ADDR_TEARDOWN_445F40, (const void *)Teardown445F40,
                  "Teardown445F40", 1);
    patch_replace(ADDR_TEARDOWN_DEF_TABLES, (const void *)TeardownDefTables,
                  "TeardownDefTables", 1);
    patch_replace(ADDR_INIT_REMAPS_AND_LISTS, (const void *)InitRemapsAndLists,
                  "InitRemapsAndLists", 0);
    patch_replace(ADDR_INIT_ARMY_OBJ_LISTS, (const void *)InitArmyObjLists,
                  "InitArmyObjLists", 0);
    patch_replace(ADDR_FREE_ARMY_OBJ_LISTS, (const void *)FreeArmyObjLists,
                  "FreeArmyObjLists", 0);
    patch_replace(ADDR_UID_REMAP_CLEAR, (const void *)UidRemapClear,
                  "UidRemapClear", 2);
    patch_replace(ADDR_UID_REMAP_ADD, (const void *)UidRemapAdd,
                  "UidRemapAdd", 2);
    patch_replace(ADDR_LOAD_GAME, (const void *)LoadGame, "LoadGame", 1);
    patch_replace(ADDR_SAVE_GAME, (const void *)SaveGame, "SaveGame", 1);
    patch_replace(ADDR_LOAD_DEFAULT_COF, (const void *)LoadDefaultCof,
                  "LoadDefaultCof", 1);
    patch_replace(ADDR_SAVE_DEFAULT_COF, (const void *)SaveDefaultCof,
                  "SaveDefaultCof", 1);
    patch_replace(ADDR_SAVE_OPTIONS, (const void *)SaveOptions,
                  "SaveOptions", 7);
    patch_replace(ADDR_DEF_GAME_PARSE, (const void *)DefGameParse,
                  "DefGameParse", 1);
    patch_replace(ADDR_SAVE_GAMEPROC, (const void *)SaveGameProcSection,
                  "SaveGameProcSection", 1);
    patch_replace(ADDR_LOAD_GAMEPROC, (const void *)LoadGameProcSection,
                  "LoadGameProcSection", 1);
    patch_replace(ADDR_REQUEST_STATE, (const void *)RequestState,
                  "RequestState", 1);
    patch_replace(ADDR_COMMIT_STATE, (const void *)CommitState,
                  "CommitState", 0);
    patch_replace(ADDR_SET_GAME_OVER, (const void *)SetGameOver,
                  "SetGameOver", 1);
    patch_replace(ADDR_CURRENT_STATE, (const void *)GameOverState,
                  "GameOverState", 0);
    patch_replace(ADDR_STATE_LEAVE_ALIAS, (const void *)StateLeaveAlias,
                  "StateLeaveAlias", 0);
    patch_replace(ADDR_SHOW_INFO_MP, (const void *)ShowInfoMp, "ShowInfoMp", 0);
    patch_replace(ADDR_SAVE_ONE_ITEM, (const void *)SaveOneItem, "SaveOneItem", 1);
    patch_replace(ADDR_LOAD_ONE_ITEM, (const void *)LoadOneItem, "LoadOneItem", 1);
    patch_replace(ADDR_SAVE_ITEM_HEADER, (const void *)SaveItemHeader,
                  "SaveItemHeader", 1);
    patch_replace(ADDR_LOAD_ITEM_HEADER, (const void *)LoadItemHeader,
                  "LoadItemHeader", 1);
    patch_replace(ADDR_SAVE_TYPE1, (const void *)SaveType1, "SaveType1", 2);
    patch_replace(ADDR_LOAD_TYPE1, (const void *)LoadType1, "LoadType1", 1);
    patch_replace(ADDR_LOAD_TYPE8, (const void *)LoadType8, "LoadType8", 1);
    patch_replace(ADDR_LOAD_TYPE5, (const void *)LoadType5, "LoadType5", 1);
    patch_replace(ADDR_CREATE_MISSILE, (const void *)CreateMissile,
                  "CreateMissile", 9);
    patch_replace(ADDR_LEVEL_STATE_RESET, (const void *)ResetLevelState,
                  "ResetLevelState", 1);
    patch_replace(ADDR_SAVE_TYPE6, (const void *)SaveType6, "SaveType6", 1);
    patch_replace(ADDR_SAVE_TYPE8, (const void *)SaveType8, "SaveType8", 1);
    patch_replace(ADDR_SAVE_TYPE4, (const void *)SaveType4, "SaveType4", 1);
    patch_replace(ADDR_SAVE_TYPE5, (const void *)SaveType5, "SaveType5", 1);
    patch_replace(ADDR_SAVE_TYPE2, (const void *)SaveType2, "SaveType2", 1);
    patch_replace(ADDR_SAVE_TYPE3, (const void *)SaveType3, "SaveType3", 1);
    patch_replace(ADDR_LOAD_TYPE7, (const void *)LoadType7, "LoadType7", 1);
    patch_replace(ADDR_LOAD_TYPE6, (const void *)LoadType6, "LoadType6", 1);
    patch_replace(ADDR_LOAD_TYPE3, (const void *)LoadType3, "LoadType3", 1);
    patch_replace(ADDR_LOAD_TYPE2, (const void *)LoadType2, "LoadType2", 1);
    patch_replace(ADDR_SEL_LIST_INIT, (const void *)SelListInit,
                  "SelListInit", 1);
    patch_replace(ADDR_SEL_LIST_CTOR_THUNK, (const void *)SelListConstructThunk,
                  "SelListConstructThunk", 1);
    patch_replace(ADDR_SEL_LIST_ATEXIT, (const void *)SelListAtExit,
                  "SelListAtExit", 1);
    patch_replace(ADDR_SEL_LIST_DTOR_THUNK, (const void *)SelListDestructThunk,
                  "SelListDestructThunk", 1);
    patch_replace(ADDR_SEL_LIST_DTOR, pmf_addr(&AM2_SelList::SelListDestruct),
                  "SelListDestruct", 1);
    patch_replace(ADDR_SEL_LIST_CTOR, pmf_addr(&AM2_SelList::SelListConstruct),
                  "SelListConstruct", 1);
    patch_replace(ADDR_LOAD_TYPE4, (const void *)LoadType4,
                  "LoadType4", 3);
    patch_replace(ADDR_WALK_CELL_CALLBACK, (const void *)WalkCellKeepEnemy,
                  "WalkCellKeepEnemy", 1);
    patch_replace(ADDR_INIT_STARTUP_COLOURS, (const void *)InitStartupColours, "InitStartupColours", 0);
    patch_replace(ADDR_INIT_STARTUP_COLOURS_B,
                  (const void *)InitStartupColoursB,
                  "InitStartupColoursB", 0);
}

/* CreateWeapon is still the image's and item.h already reaches it -- with a
 * correction this function would otherwise have got wrong. The literal 4 in
 * its second argument is AM2_ARMY_NEUTRAL, an ARMY, not the object type;
 * item.h established that from CommMustBroadcast's parameter and records that
 * the two concepts merely share a value. Reading it as "type 4" here would
 * have been consistent, plausible and wrong. */

/* LoadType4 -- original 0x0045EF50, one caller: LoadOneItem's type-4 arm. The
 * LAST of the eight savegame readers; the other seven have been reconstructed
 * for some time, and this one is not a copy of any of them -- its closest
 * sibling is LoadType1 at 0.381 over normalised disassembly.
 *
 * FIVE READS, AND ONE OF THEM IS DISCARDED. The 0x2C record and four dwords
 * come off the file; three of the dwords are used -- the sprite key, a value
 * handed to CreateWeapon, and one that ends up in the weapon's +0xC4 -- and
 * the fourth is read into a local nothing ever touches. tools/espmap.py says
 * so: that slot has exactly one reference, the `lea` that hands its address
 * to fread. It is reproduced because the FILE POSITION depends on it; a
 * reader that skipped it would misparse everything after.
 *
 * THE RENUMBER ARGUMENT PICKS BETWEEN TWO CreateWeapon CALLS that differ in
 * one argument. With renumber set the weapon is created with uid 0 and then
 * UidRemapAdd records the mapping from the saved uid to the fresh one; with
 * it clear the saved uid is passed straight in. Same eight arguments, same
 * order, one value different -- and the arms are written out separately
 * because the ORIGINAL has two calls, and the remap is only in one of them.
 *
 * IT KEEPS THE FRESH OBJECT'S +0x94 POINTER, exactly as LoadType1 does: saved
 * before the header copy and put back after the record copy, so a loaded
 * weapon points at this session's table rather than the saved address. The
 * header copy is ADDR_ITEM_HEADER_SIZE bytes, the same global LoadType1 uses.
 *
 * THE RENUMBER FLAG IS READ TWICE. Once to choose the create, and again at
 * the end to set bit 0x4000000 in OBJ_OFF_FLAGS -- so a renumbered weapon is
 * marked as such, and the flag is not simply the create path's leftover. */
AM2_Object *__cdecl LoadType4(am2_FILE *fp, AM2_Object *hdr, int32_t renumber)
{
    AM2_Object  *h = (AM2_Object *)hdr;
    /* The whole object on the stack, the record read in at +0x94, as the
     * other loaders have it. */
    struct {
        uint8_t head[OBJ_OFF_BY_TYPE_94];
        uint8_t rec[AM2_TYPE1_RECORD_SIZE];
    } obj;
    uint32_t  spriteKey;
    uint32_t  extra;
    uint32_t  cooldown;
    uint32_t  unused;
    int32_t   aai;
    AM2_Object  *w;
    const uint8_t     *ownRec;

    orig_fread(obj.rec, AM2_TYPE1_RECORD_SIZE, 1, fp);
    orig_fread(&spriteKey, 4, 1, fp);
    orig_fread(&cooldown, 4, 1, fp);
    orig_fread(&unused, 4, 1, fp);
    orig_fread(&extra, 4, 1, fp);

    aai = EnsureSpriteAaiRecord((int32_t)KeyFieldA(spriteKey),
                                (int32_t)KeyFieldB(spriteKey), 0);

    if (renumber) {
        w = CreateWeapon(
                g_dirScratch, AM2_ARMY_NEUTRAL, aai,
                h->pos,
                (int32_t)h->flags,
                (int32_t)extra, 1, 0);
        UidRemapAdd(h->uid,
                    w->uid);
        h->uid = w->uid;
    } else {
        w = CreateWeapon(
                g_dirScratch, AM2_ARMY_NEUTRAL, aai,
                h->pos,
                (int32_t)h->flags,
                (int32_t)extra, 1,
                h->uid);
    }

    ownRec = w->saveRec;
    memcpy(w, h, (size_t)g_itemHeaderSize);
    memcpy(&w->saveRec, obj.rec, AM2_TYPE1_RECORD_SIZE);
    w->saveRec = ownRec;

    w->followUid = cooldown;
    w->pickupAfter = 0;

    if (renumber)
        w->flags |= 0x4000000u;

    (void)unused;
    return w;
}
