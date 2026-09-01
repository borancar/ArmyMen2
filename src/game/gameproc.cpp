/* gameproc.cpp -- see gameproc.h. */
#include <stdint.h>
#include <string.h>

#include "gameproc.h"
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
#define orig_atexit (*(AM2_AtExitFn)AM2_IMAGE(ADDR_CRT_ATEXIT))
#include "../inject/patch.h"
#include "misc.h"      /* ReturnOne */
#include "crt.h"       /* am2_realloc, am2_free -- the game's own */

#define kBlock  ((char *)(uintptr_t)AM2_IMAGE(ADDR_GAMEPROC_BLOCK))
#define kStrB   ((char *)(uintptr_t)AM2_IMAGE(ADDR_GAMEPROC_STR_B))
#define kVolZero   (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_VOLUME_AT_ZERO))
#define kVolStream (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_STREAM_VOLUME))
#define kVolVoice  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_VOLUME_VOICE))
/* SaveDefaultCof's three. The first two are spelled as army.cpp spells them,
 * so checkglobals sees one name per address and not two. */
#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_armyObjLists ((void **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)
#define g_levelId      (*(int32_t *)(uintptr_t)ADDR_LEVEL_ID)

/* The original's frame is 0x80 with the two stashes at +0 and +0x50, so each
 * has 80 bytes and neither is bounded against the string it copies. Same size
 * here, and no guard added: a guard would be behaviour this build lacks. */
#define AM2_GAMEPROC_STASH 0x50

int32_t __cdecl SaveGameProcSection(am2_FILE *fp)
{
    /* The LENGTH, not a tag. SaveGame has already written 0x06660666. */
    WriteSaveTag(fp, AM2_GAMEPROC_SAVE_SIZE);
    orig_fwrite(kBlock, AM2_GAMEPROC_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadGameProcSection(am2_FILE *fp)
{
    char    stashA[AM2_GAMEPROC_STASH];   /* the string at +0x120 */
    char    stashB[AM2_GAMEPROC_STASH];   /* the string at the block start */
    int32_t volZero, volStream, volVoice;

    /* Everything that must outlive the read is copied out first, before the
     * tag is even checked -- which costs nothing when the check fails. */
    strcpy(stashA, kStrB);
    strcpy(stashB, kBlock);
    volVoice  = kVolVoice;
    volZero   = kVolZero;
    volStream = kVolStream;

    if (!CheckSaveTag(fp, AM2_GAMEPROC_SAVE_SIZE,
                      (const char *)AM2_IMAGE(ADDR_STR_GAMEPROC_CPP), 0x8CD))
        return 0;

    orig_fread(kBlock, AM2_GAMEPROC_SAVE_SIZE, 1, fp);

    kVolStream = volStream;
    kVolZero   = volZero;
    kVolVoice  = volVoice;
    strcpy(kStrB, stashA);
    strcpy(kBlock, stashB);
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
#define orig_findfirst ((AM2_FindFirstFn2)AM2_IMAGE(ADDR_CRT_FINDFIRST))
#define orig_findclose ((AM2_FindCloseFn)AM2_IMAGE(ADDR_CRT_FINDCLOSE))
#define orig_mkdir     ((AM2_MkdirFn)AM2_IMAGE(ADDR_CRT_MKDIR))
#define orig_sprintf   ((AM2_SprintfFn2)AM2_IMAGE(ADDR_GAME_SPRINTF))

/* _chmod and fflush, neither of which this tree had needed before. */
typedef int32_t (__cdecl *AM2_ChmodFn)(const char *path, int32_t mode);
#define orig_chmod  ((AM2_ChmodFn)AM2_IMAGE(ADDR_CRT_CHMOD))
typedef int32_t (__cdecl *AM2_FflushFn)(am2_FILE *fp);
#define orig_fflush ((AM2_FflushFn)AM2_IMAGE(ADDR_CRT_FFLUSH))

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
    const uint8_t *key;
    int32_t   len;

    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    orig_chmod((const char *)AM2_IMAGE(ADDR_STR_OPTIONS_CFG), AM2_CHMOD_RW);

    fp = orig_fopen((const char *)AM2_IMAGE(ADDR_STR_OPTIONS_CFG),
                    (const char *)AM2_IMAGE(ADDR_MODE_W));
    if (!fp) {
        orig_log((const char *)AM2_IMAGE(ADDR_STR_OPTIONS_NOWRITE));
        return;
    }

    orig_fwrite((const void *)AM2_IMAGE(ADDR_VOLUME_AT_ZERO), 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_STREAM_VOLUME), 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_VOLUME_VOICE), 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_MOVIE_COUNT), 4, 1, fp);

    for (key = (const uint8_t *)AM2_IMAGE(ADDR_KEY_BINDINGS);
         key < (const uint8_t *)AM2_IMAGE(ADDR_KEY_BINDINGS_END);
         key += 2)
        orig_fwrite(key, 1, 1, fp);

    orig_fwrite((const void *)AM2_IMAGE(ADDR_DIFFICULTY), 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_HOST_MASK_A), 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_HOST_MASK_B), 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_HOST_VALUE_3E8), 4, 1, fp);

    len = (int32_t)strlen((const char *)AM2_IMAGE(ADDR_SAVED_PLAYER_NAME));
    orig_fwrite(&len, 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_SAVED_PLAYER_NAME),
                1, (size_t)len, fp);

    len = (int32_t)strlen((const char *)AM2_IMAGE(ADDR_SAVED_BATTLE_NAME));
    orig_fwrite(&len, 4, 1, fp);
    orig_fwrite((const void *)AM2_IMAGE(ADDR_SAVED_BATTLE_NAME),
                1, (size_t)len, fp);

    orig_fflush(fp);
    orig_fclose(fp);
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
 * 8 are admitted, OBJ_OFF_FIELD_94 must be zero, and then the type must be
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
    int32_t   i = 0;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_SAVE_DIR));

    orig_chmod((const char *)AM2_IMAGE(ADDR_STR_DEFAULT_COF), AM2_CHMOD_RW);

    fp = orig_fopen((const char *)AM2_IMAGE(ADDR_STR_DEFAULT_COF),
                    (const char *)AM2_IMAGE(ADDR_MODE_WB));
    if (!fp)
        return 0;

    WriteSaveTag(fp, AM2_SAVETAG_COF);

    for (;;) {
        uint8_t *list = (uint8_t *)g_armyObjLists[g_defaultOwner];
        uint8_t *obj;

        if (i >= *(const int32_t *)(list + LIST_OFF_COUNT))
            break;

        obj = (uint8_t *)LookupByUID(
                  ((const uint32_t *)*(void **)(list + LIST_OFF_UIDS))[i]);
        if (!obj) {
            ListRemoveAt(list, i);
            continue;
        }

        if (*(const int16_t *)(obj + OBJ_OFF_HEALTH) <= 0 &&
            *(const int16_t *)(obj + OBJ_OFF_MAX_HEALTH) > 0) {
            i++;
            continue;
        }

        if (*(const int32_t *)obj == AM2_OBJ_TYPE_TROOPER) {
            ObjDropAltRecord(obj);
            Type238Action(obj, (g_levelId * 5 + 5) * 2);
            *(uint32_t *)(obj + OBJ_OFF_FIELD_C0) =
                *(const uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_ZERO_POINT);

            if (*(const uint32_t *)(obj + OBJ_OFF_RIDING)) {
                void *rows = *(void **)(obj + OBJ_OFF_ROWS);

                *(uint32_t *)(obj + OBJ_OFF_FLAGS) &=
                    ~(uint32_t)OBJ_FLAG8_BLOCKED;
                *(uint32_t *)(obj + OBJ_OFF_RIDING) = 0;
                ObjFlagSet0(rows);
            }
        }

        *(uint32_t *)(obj + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG8_BLOCKED;

        {
            int32_t type = *(const int32_t *)obj;

            if (type >= AM2_OBJ_TYPE_TROOPER &&
                (type <= AM2_OBJ_TYPE_VEHICLE || type == AM2_OBJ_TYPE_ROACH) &&
                *(const int32_t *)(obj + OBJ_OFF_FIELD_94) == 0 &&
                type == AM2_OBJ_TYPE_TROOPER) {
                int32_t slot;

                SaveOneItem(fp, obj);
                SaveScriptName(fp, obj);

                for (slot = 0; slot < AM2_INVENTORY_SLOTS; slot++) {
                    void *w = LookupByUID(
                        ((const uint32_t *)(obj + UNIT_OFF_INVENTORY))[slot]);

                    if (w) {
                        SaveOneItem(fp, w);
                        SaveScriptName(fp, w);
                    }
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
    int32_t   green = 0;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_SAVE_DIR));

    fp = orig_fopen((const char *)AM2_IMAGE(ADDR_STR_DEFAULT_COF),
                    (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp)
        return 0;

    if (!CheckSaveTag(fp, AM2_SAVETAG_COF,
                      (const char *)AM2_IMAGE(ADDR_STR_UNIT_CPP),
                      AM2_COF_TAG_LINE)) {
        orig_fclose(fp);
        return 0;
    }

    UidRemapClear();

    orig_fread(&tag, 4, 1, fp);
    while (tag == AM2_SAVE_RECORD_MARK) {
        uint8_t *obj = (uint8_t *)LoadOneItem(fp, 1);

        if (obj) {
            int16_t max = *(const int16_t *)(obj + OBJ_OFF_MAX_HEALTH);

            if (max > 0 && *(const int16_t *)(obj + OBJ_OFF_HEALTH) > 0) {
                *(int16_t *)(obj + OBJ_OFF_HEALTH) = max;
                if (*(const int32_t *)obj == AM2_OBJ_TYPE_TROOPER)
                    green++;
            }
            LoadScriptName(fp, obj);
        }
        orig_fread(&tag, 4, 1, fp);
    }

    RemapInventoryUids();
    UidRemapClear();

    SetVarValueByName((const char *)AM2_IMAGE(ADDR_STR_NUMGREEN), green);

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
    char       lvl[0x104];
    char       path[0xFC];
    uint8_t    fd[AM2_FINDDATA_BYTES];
    am2_FILE  *fp;
    int32_t    h;

    if (!*(const char *)(uintptr_t)AM2_IMAGE(ADDR_GAMEPROC_BLOCK))
        return 0;
    if (!*name)
        return 0;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_SAVE_DIR));

    strcpy(lvl, (const char *)AM2_IMAGE(ADDR_GAMEPROC_BLOCK));

    h = orig_findfirst(lvl, fd);
    if (h == -1) {
        orig_mkdir(lvl);
    } else {
        if (!(fd[0] & AM2_ATTR_SUBDIR))
            orig_mkdir(lvl);
        orig_findclose(h);
    }

    orig_sprintf(path, (const char *)AM2_IMAGE(ADDR_STR_SAVE_PLAYER_FMT),
                 (const char *)AM2_IMAGE(ADDR_GAMEPROC_BLOCK));
    SetGameDir(path);

    fp = orig_fopen(name, (const char *)AM2_IMAGE(ADDR_MODE_WB));
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
                      (const char *)AM2_IMAGE(ADDR_STR_GAMEPROC_CPP), 0x53D))
        goto fail;

    ScriptResetTokens((AM2_ScriptCtx *)AM2_IMAGE(ADDR_SCRIPT_CONTEXT));

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
#define orig_strtok (*(AM2_StrtokFn)AM2_IMAGE(ADDR_CRT_STRTOK))
#define kSep        ((const char *)AM2_IMAGE(ADDR_DEF_SEPARATORS))
#define kGameConst  ((int32_t *)AM2_IMAGE(ADDR_GAME_CONSTANTS))

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

    if (cmd < AM2_DEF_CMD_GAME_FIRST || cmd > AM2_DEF_CMD_GAME_LAST) {
        orig_log("DefGameParse: Bad Game Constant Type\n");
        return 3;
    }

    if (cmd >= AM2_DEF_CMD_ROACH_FIRST)
        kGameConst[cmd - AM2_DEF_CMD_ROACH_FIRST] = value;

    /* else: one of the twelve the original drops on the floor */
    return 0;
}

#define g_endState       (*(int32_t *)(uintptr_t)ADDR_GAME_OVER_STATE)
#define g_gameOverSaved  ((int32_t *)(uintptr_t)ADDR_GAME_OVER_SAVED)
#define g_gameOverSource ((const int32_t *)(uintptr_t)ADDR_GAME_OVER_SOURCE)

/* StateLeave lives in win32/movie.cpp -- it clears the primary surface, so it
 * cannot live in the flat half -- and this module cannot include movie.h for
 * the same reason. Declared instead, the way commmsg.cpp declares the comm
 * methods and the widget helpers it needs. */
extern "C" void __cdecl StateLeave(void);

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

#define g_statePending (*(int32_t *)(uintptr_t)ADDR_STATE_PENDING)
#define g_stateWanted  (*(int32_t *)(uintptr_t)ADDR_STATE_WANTED)
#define g_gameState    (*(int32_t *)(uintptr_t)ADDR_GAME_STATE)
#define g_stateEntered (*(int32_t *)(uintptr_t)ADDR_STATE_ENTERED)

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
    int32_t *on = (int32_t *)(uintptr_t)ADDR_INFO_OVERLAY_ON;

    *on = (*on == 0) ? 1 : 0;
}

#define kItemHeaderSize (*(const int32_t *)(uintptr_t)ADDR_ITEM_HEADER_SIZE)

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
int32_t __cdecl SaveItemHeader(am2_FILE *fp, void *obj)
{
    WriteSaveTag(fp, AM2_ITEM_HEADER_TAG);
    orig_fwrite(obj, (size_t)kItemHeaderSize, 1, fp);
    return 1;
}

int32_t __cdecl LoadItemHeader(am2_FILE *fp, void *hdr)
{
    orig_fread(hdr, (size_t)kItemHeaderSize, 1, fp);
    return 1;
}

/* 0x00433D20, 0x00422750 and 0x0043CB30 -- three of the eight per-type
 * savers, and the three that are nothing but a write.
 *
 * All three write from OBJ_OFF_FIELD_94, which is what settles what that
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
int32_t __cdecl SaveType1(am2_FILE *fp, void *obj)
{
    uint8_t *rec = (uint8_t *)obj + OBJ_OFF_FIELD_94;

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
void __cdecl ResetLevelState(void)
{
    int32_t  *matrix = (int32_t *)(uintptr_t)ADDR_ALLY_MATRIX;
    uint8_t  *comm;
    int32_t   interval;
    int32_t   i, j;

    *(int32_t *)(uintptr_t)ADDR_LEVEL_FLAG_E30 = 1;
    *(int32_t *)(uintptr_t)ADDR_VIEW_SNAP      = 1;
    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET    = 1;

    *(int32_t *)(uintptr_t)ADDR_NEXT_UID          = 0x186A0;
    *(int32_t *)(uintptr_t)ADDR_PAD_COUNT         = 0;
    *(int32_t *)(uintptr_t)ADDR_CLOCK_BASE_MS     = 0;
    *(int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS     = 0;
    *(int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS    = 0;
    *(int32_t *)(uintptr_t)ADDR_LAST_TICK_MS      = 0;
    *(int32_t *)(uintptr_t)ADDR_EVT_ID15_UID      = 0;
    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A     = 0;
    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL       = 0;
    *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_PREV  = 0;
    *(int32_t *)(uintptr_t)ADDR_EVT_ID15_FLAG     = 0;

    interval = *(const int32_t *)(uintptr_t)ADDR_DIFFICULTY * AM2_TICK_PER_STEP
               + AM2_TICK_BASE_MS;
    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION != 0)
        interval = AM2_TICK_NET_MS;

    *(int32_t *)(uintptr_t)ADDR_TICK_INTERVAL_MS = interval;
    *(int32_t *)(uintptr_t)ADDR_SECOND_DEADLINE  = interval;

    *(int32_t *)(uintptr_t)ADDR_OUR_LEADER_UID   = 0;
    *(int32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID  = 0;
    *(int32_t *)(uintptr_t)ADDR_WEAPON_SLOT      = 0;
    *(int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS   = 0;

    ClearPtrList((void *)(uintptr_t)ADDR_SELECTED_UIDS);

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION == 0)
        *(int32_t *)(uintptr_t)ADDR_NET_GAME = 0;

    for (i = 0; i < AM2_COMM_PLAYERS; i++)
        for (j = 0; j < AM2_COMM_PLAYERS; j++)
            matrix[i * AM2_COMM_PLAYERS + j] = (i == j) ? 1 : 0;

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;

    for (i = 0; i < AM2_COMM_PLAYERS; i++) {
        const uint8_t *a = comm + COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE;

        if (!*(const int32_t *)(a + COMM_SLOT_OFF_TAKEN))
            continue;
        if (!*(const int32_t *)(a + COMM_SLOT_OFF_TEAM))
            continue;

        for (j = 0; j < AM2_COMM_PLAYERS; j++) {
            const uint8_t *b = comm + COMM_OFF_PLAYERS
                               + j * COMM_PLAYER_STRIDE;

            if (!*(const int32_t *)(b + COMM_SLOT_OFF_TAKEN))
                continue;
            if (*(const int32_t *)(a + COMM_SLOT_OFF_TEAM)
                != *(const int32_t *)(b + COMM_SLOT_OFF_TEAM))
                continue;

            matrix[i * AM2_COMM_PLAYERS + j] = 1;
            matrix[j * AM2_COMM_PLAYERS + i] = 1;
        }
    }

    *(int32_t *)(uintptr_t)ADDR_THROTTLE_DEADLINE   = 0;
    *(int32_t *)(uintptr_t)ADDR_CHEAT_INVULNERABLE  = 0;
}

/* 0x00461F90 is above the CRT line and stays original; reached by name here
 * rather than by address so checkseams can see it. */
typedef int32_t (__cdecl *AM2_TimedDirFrameFn)(void *rows, int32_t dir);
#define orig_timed_dir_frame \
    ((AM2_TimedDirFrameFn)(uintptr_t)AM2_IMAGE(ADDR_TIMED_DIR_FRAME))

/* CreateMissile -- original 0x0043B9B0, nine callers. Build a type-5 object
 * from a weapon and a firing position: allocate it, run the shared init, give
 * it a row set, and start its animation.
 *
 * TYPE 5 IS A MISSILE and LoadType5 (0x0043B870, reconstructed) is the
 * evidence and the template: the savegame loader for the same type, sitting
 * immediately before this in the image. Everything structural here is its
 * vocabulary -- AM2_MISSILE_BYTES, ADDR_MISSILE_BOX, ADDR_MISSILE_ROW_SPEC,
 * ADDR_MISSILE_ANIMS, OBJ_OFF_FIELD_94 for the def pointer -- and the two
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
void *__cdecl CreateMissile(void *weapon, void *source, uint32_t at,
                            int32_t heading, int32_t a5, int32_t a6,
                            int32_t repairFrame, int32_t ptrList,
                            int32_t initUnused, uint32_t uid,
                            int32_t scriptId)
{
    uint8_t *w = (uint8_t *)weapon;
    uint8_t *b = (uint8_t *)source;
    uint8_t *o;
    uint8_t *rows;
    int32_t  def;
    int32_t  frame;

    if (!weapon)
        return (void *)0;

    o = (uint8_t *)am2_malloc(AM2_MISSILE_BYTES);
    memset(o, 0, AM2_MISSILE_BYTES);

    *(const void **)(o + OBJ_OFF_FIELD_94) =
        *(const void *const *)(w + OBJ_OFF_FIELD_C0);
    *(uint8_t *)(o + OBJ_OFF_FACING) = (uint8_t)heading;
    *(int32_t *)(o + OBJ_OFF_REPAIR_FRAME) = repairFrame;
    *(int32_t *)(o + OBJ_OFF_PTR_LIST) = ptrList;
    *(int32_t *)(o + OBJ_OFF_RANK) = (int32_t)((const AM2_Object *)source)->uid;
    *(uint8_t *)(o + OBJ_OFF_SCRIPT_ID) = (uint8_t)scriptId;

    def = **(const int32_t *const *)(o + OBJ_OFF_FIELD_94);
    frame = (def == 2 || def == 5) ? AM2_MISSILE_FRAME_A : AM2_MISSILE_FRAME_B;

    *(int8_t *)(o + OBJ_OFF_ARMY) = *(const int8_t *)(w + OBJ_OFF_ARMY);
    ObjInitCommon(o, (char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                  AM2_OBJ_TYPE_MISSILE, at,
                  (const int32_t *)AM2_IMAGE(ADDR_MISSILE_BOX),
                  initUnused, uid);

    BuildRowSet(o + OBJ_OFF_SUBRECORD, 1,
                (const void *)AM2_IMAGE(ADDR_MISSILE_ROW_SPEC),
                (int16_t)at, (int16_t)(at >> 16),
                (const void *)(uintptr_t)ADDR_ZERO_RECT);

    *(int32_t *)(o + MISSILE_OFF_FIELD_A8) = a5;
    *(int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST) = (int16_t)a5;
    *(uint32_t *)(o + OBJ_OFF_DEADLINE_58) =
        *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
    *(int32_t *)(o + OBJ_OFF_FIELD_44) = *(const int32_t *)(b + OBJ_OFF_FIELD_44);

    {
        int32_t n = *(const int32_t *)
            (*(const uint8_t *const *)(w + OBJ_OFF_FIELD_C0)
             + MISSILEDEF_OFF_FIELD_0C);

        if (n > 0) {
            *(float *)(o + OBJ_OFF_VEL_Z) = (float)n;
        } else {
            int32_t cur = *(const int16_t *)(o + OBJ_OFF_ROW0_Y_ADJUST);

            /* Equal leaves OBJ_OFF_VEL_Z alone -- the original branches past
             * the fstp rather than storing zero, so a missile launched at its
             * own height keeps whatever the zeroed allocation left. */
            if (cur != a6)
                *(float *)(o + OBJ_OFF_VEL_Z) = (float)((a6 - cur) << 1);
        }
    }

    rows = *(uint8_t **)(o + OBJ_OFF_ROWS);

    if (**(const int32_t *const *)(o + OBJ_OFF_FIELD_94) == 3) {
        *(const void **)(rows + ROW_OFF_ANIM_CUR) = (const void *)0;
        *(int16_t *)(rows + ROW_OFF_FIELD_26) =
            (int16_t)(ScaleBy32Blocks(*(const int16_t *)
                          (o + OBJ_OFF_ROW0_Y_ADJUST)) + AM2_MISSILE_ROW26_BIAS);
        *(uint8_t *)(rows + ROW_OFF_HEADING) = (uint8_t)heading;
        *(uint32_t *)(rows + ROW_OFF_STAMP_54) =
            *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
        orig_timed_dir_frame(rows, heading);

        if (*(const uint32_t *)(w + MISSILE_OFF_LAST_UID)) {
            uint8_t *prev = (uint8_t *)
                LookupByUID(*(const uint32_t *)
                            (w + MISSILE_OFF_LAST_UID));

            if (prev
                && *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                   - *(const uint32_t *)
                       (*(const uint8_t *const *)(prev + OBJ_OFF_ROWS)
                        + ROW_OFF_STAMP_54) < AM2_FRAME_PERIOD_MS)
                *(uint32_t *)(prev + MISSILE_OFF_NEXT_UID) =
                    ((const AM2_Object *)o)->uid;
        }

        *(uint32_t *)(w + MISSILE_OFF_LAST_UID) =
            ((const AM2_Object *)o)->uid;
        return o;
    }

    *(const void **)(rows + ROW_OFF_ANIM_CUR) =
        (const void *)(uintptr_t)ADDR_MISSILE_ANIMS;
    *(int16_t *)(rows + ROW_OFF_FIELD_26) =
        (int16_t)(ScaleBy32Blocks(*(const int16_t *)
                      (o + OBJ_OFF_ROW0_Y_ADJUST)) + AM2_MISSILE_ROW26_BIAS);
    *(uint8_t *)(rows + ROW_OFF_HEADING) = (uint8_t)heading;
    SetAnimFrame(rows, (int16_t)frame, 1);
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
 * OBJ_OFF_FIELD_94. A pointer written to disk would not survive a reload.
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
void *__cdecl LoadType5(am2_FILE *fp, void *hdr)
{
    uint8_t       *o = (uint8_t *)am2_malloc(AM2_MISSILE_BYTES);
    const uint8_t *h = (const uint8_t *)hdr;
    uint8_t       *rows;
    int32_t        def;
    int32_t        frame;

    memset(o, 0, AM2_MISSILE_BYTES);

    *(int8_t *)(o + OBJ_OFF_ARMY) = *(const int8_t *)(h + OBJ_OFF_ARMY);

    ObjInitCommon(o, (char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                         AM2_OBJ_TYPE_MISSILE,
                         *(const uint32_t *)(h + OBJ_OFF_POS),
                         (const int32_t *)AM2_IMAGE(ADDR_MISSILE_BOX),
                         1, *(const int32_t *)(h + 4));

    memcpy(o, h, (size_t)*(const int32_t *)(uintptr_t)ADDR_ITEM_HEADER_SIZE);

    orig_fread(&def, 4, 1, fp);
    *(const void **)(o + OBJ_OFF_FIELD_94) =
        (const uint8_t *)(uintptr_t)ADDR_MISSILE_DEFS
        + def * AM2_MISSILE_DEF_BYTES;

    orig_fread(o + OBJ_OFF_RANK, 4, 1, fp);
    orig_fread(o + OBJ_OFF_REPAIR_FRAME, 4, 1, fp);
    orig_fread(o + OBJ_OFF_PTR_LIST, 4, 1, fp);
    orig_fread(o + OBJ_OFF_CHAIN_NEXT_UID, 4, 1, fp);

    def = **(const int32_t *const *)(o + OBJ_OFF_FIELD_94);
    frame = (def == 2 || def == 5) ? AM2_MISSILE_FRAME_A : AM2_MISSILE_FRAME_B;

    BuildRowSet(o + OBJ_OFF_SUBRECORD, 1,
                (const void *)AM2_IMAGE(ADDR_MISSILE_ROW_SPEC),
                *(const int16_t *)(h + OBJ_OFF_POS),
                *(const int16_t *)(h + OBJ_OFF_POS + 2),
                (const void *)(uintptr_t)ADDR_ZERO_RECT);

    rows = *(uint8_t **)(o + OBJ_OFF_ROWS);
    *(const void **)(rows + ROW_OFF_ANIM_CUR) =
        (const void *)(uintptr_t)ADDR_MISSILE_ANIMS;
    *(int16_t *)(rows + ROW_OFF_FIELD_26) = AM2_ROW_FIELD26_INIT;
    *(uint8_t *)(rows + ROW_OFF_HEADING) = *(const uint8_t *)(o + OBJ_OFF_FACING);

    SetAnimFrame(rows, (int16_t)frame, 1);
    return o;
}

/* LoadType8 -- original 0x0043CB60, one caller, and the ROACH member of the
 * per-type savegame loader family LoadType1 belongs to. Read the 0x4CC-byte
 * record, build a roach from the header, and paste the record over the
 * object's OBJ_OFF_FIELD_94.
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
void *__cdecl LoadType8(am2_FILE *fp, const void *hdr)
{
    uint8_t        rec[AM2_TYPE8_RECORD_SIZE];
    const uint8_t *h   = (const uint8_t *)hdr;
    uint8_t       *o;
    float          part;

    InitPtrList(rec + 0x10);
    orig_fread(rec, AM2_TYPE8_RECORD_SIZE, 1, fp);

    o = (uint8_t *)CreateRoach(
            *(const int32_t *)(rec + 0x498),
            (char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
            *(const int16_t *)(h + OBJ_OFF_POS),
            *(const int16_t *)(h + OBJ_OFF_POS + 2),
            *(const int8_t *)(h + OBJ_OFF_ARMY),
            *(const int32_t *)(h + OBJ_OFF_FLAGS), 1,
            *(const int32_t *)(h + 4));

    memcpy(o, h, (size_t)*(const int32_t *)(uintptr_t)ADDR_ITEM_HEADER_SIZE);
    memcpy(o + OBJ_OFF_FIELD_94, rec, AM2_TYPE8_RECORD_SIZE);

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) > 0) {
        int32_t was = *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);
        int32_t now;

        part = (float)((double)*(const int16_t *)(o + OBJ_OFF_HEALTH)
                       / (double)was);
        SetMaxHealth(o, *(const int32_t *)AM2_IMAGE(ADDR_ROACH_HEALTH));

        now = *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH);
        *(int16_t *)(o + OBJ_OFF_HEALTH) =
            (int16_t)(int32_t)((double)now * (double)part);
        *(int16_t *)(o + OBJ_OFF_HEALTH) =
            (int16_t)Clamp(*(const int16_t *)(o + OBJ_OFF_HEALTH), 1, now);
    }

    *(int32_t *)(o + OBJ_OFF_PTR_LIST)     = 0;
    *(int32_t *)(o + OBJ_OFF_PTR_LIST + 4) = 0;
    *(int32_t *)(o + OBJ_OFF_PTR_LIST + 8) = 0;
    ResetObjOnCof(o);

    *(uint32_t *)(o + OBJ_OFF_FLAGS) &= ~OBJ_FLAG_FOOTPRINT_ON;

    *(int32_t *)(rec + 0x10)     = 0;
    *(int32_t *)(rec + 0x10 + 4) = 0;
    *(int32_t *)(rec + 0x10 + 8) = 0;
    ClearPtrListAlias(rec + 0x10);

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
 * record over its OBJ_OFF_FIELD_94. What survives that is what the function
 * carefully saves in locals first:
 *
 *   - the FRESH object's OBJ_OFF_FIELD_94 pointer, put back over the one the
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
void *__cdecl LoadType1(am2_FILE *fp, const void *hdr)
{
    uint8_t   rec[AM2_TYPE1_RECORD_SIZE];
    uint32_t  tag;
    uint8_t  *obj;
    void     *ownRec;
    uint8_t   blockA[16];
    uint8_t   blockB[16];

    orig_fread(rec, AM2_TYPE1_RECORD_SIZE, 1, fp);
    orig_fread(&tag, 4, 1, fp);

    obj = (uint8_t *)CreateItem(
        (char *)AM2_IMAGE(ADDR_DIR_SCRATCH), AM2_ARMY_NEUTRAL,
        (int32_t)tag,
        *(const int32_t *)((const uint8_t *)hdr + OBJ_OFF_POS),
        *(const int32_t *)((const uint8_t *)hdr + OBJ_OFF_FLAGS), 1,
        *(const uint32_t *)((const uint8_t *)hdr + 4));
    if (!obj)
        return (void *)0;

    ownRec = *(void **)(obj + OBJ_OFF_FIELD_94);
    memcpy(blockA, obj + 0x20, sizeof blockA);
    memcpy(blockB, obj + 0x30, sizeof blockB);

    memcpy(obj, hdr, (size_t)kItemHeaderSize);
    memcpy(obj + OBJ_OFF_FIELD_94, rec, AM2_TYPE1_RECORD_SIZE);
    *(void **)(obj + OBJ_OFF_FIELD_94) = ownRec;

    memcpy(obj + 0x20, blockA, sizeof blockA);
    memcpy(obj + 0x30, blockB, sizeof blockB);

    if (*(const int32_t *)(obj + OBJ_OFF_REPAIR_FRAME) > 0)
        ChangeObjectFrame(obj, *(const int32_t *)(obj + OBJ_OFF_REPAIR_FRAME),
                          0);
    if (*(const int32_t *)(obj + OBJ_OFF_FORMATION_SLOT) > 0)
        ChangeObjectFrame(obj,
                          *(const int32_t *)(obj + OBJ_OFF_FORMATION_SLOT), 1);

    return obj;
}


int32_t __cdecl SaveType6(am2_FILE *fp, void *obj)
{
    orig_fwrite((uint8_t *)obj + OBJ_OFF_FIELD_94,
                AM2_TYPE6_RECORD_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl SaveType8(am2_FILE *fp, void *obj)
{
    if (!obj)
        return 0;

    orig_fwrite((uint8_t *)obj + OBJ_OFF_FIELD_94,
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
 * on the uid at TROOPER_OFF_WEAPON_UID, then a pointer at OBJ_OFF_FIELD_C0 and
 * the dword it points at. A trooper who has dropped his weapon saves a 1 where
 * an armed one saves the weapon's own code, so the tag is not a constant and a
 * reader cannot skip it.
 *
 * Both write a trailing list of dwords when its count is positive, and type 3
 * writes a second such list. Neither length goes through WriteSaveTag, unlike
 * every other length in this format.
 */
int32_t __cdecl SaveType2(am2_FILE *fp, void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  rec, n;
    uint8_t *weapon;
    uint32_t tag;

    if (!o)
        return 0;

    rec = *(const int32_t *)(o + SAVED_OFF_TABLE_REC2);
    if (rec)
        *(int32_t *)(o + SAVED_OFF_TABLE_REC2) =
            (int32_t)(((uint32_t)rec - ADDR_OBJ_TABLE_RECORDS) >> 8);

    orig_fwrite(o + OBJ_OFF_FIELD_94, AM2_TYPE2_RECORD_SIZE, 1, fp);

    weapon = (uint8_t *)WeaponByUid(
                 *(const uint32_t *)(o + TROOPER_OFF_WEAPON_UID));
    tag = weapon
          ? **(const uint32_t *const *)(weapon + OBJ_OFF_FIELD_C0)
          : 1u;
    WriteSaveTag(fp, tag);

    n = *(const int32_t *)(o + SAVED_OFF_LIST_COUNT);
    if (n > 0)
        orig_fwrite(*(void *const *)(o + SAVED_OFF_LIST),
                    (size_t)n * 4, 1, fp);

    *(int32_t *)(o + SAVED_OFF_TABLE_REC2) = rec;
    return 1;
}

int32_t __cdecl SaveType3(am2_FILE *fp, void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    int32_t  rec, n;

    if (!o)
        return 0;

    ObjClearFootprint(o);

    rec = *(const int32_t *)(o + SAVED_OFF_TABLE_REC3);
    if (rec)
        *(int32_t *)(o + SAVED_OFF_TABLE_REC3) =
            (int32_t)(((uint32_t)rec - ADDR_OBJ_TABLE_RECORDS) >> 8);

    orig_fwrite(o + OBJ_OFF_FIELD_94, AM2_TYPE3_RECORD_SIZE, 1, fp);

    n = *(const int32_t *)(o + SAVED_OFF_LIST_COUNT);
    if (n > 0)
        orig_fwrite(*(void *const *)(o + SAVED_OFF_LIST),
                    (size_t)n * 4, 1, fp);

    n = *(const int32_t *)(o + SAVED_OFF_LIST2_COUNT);
    if (n > 0)
        orig_fwrite(*(void *const *)(o + SAVED_OFF_LIST2),
                    (size_t)n * 4, 1, fp);

    *(int32_t *)(o + SAVED_OFF_TABLE_REC3) = rec;
    ObjSetFootprint(o);
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
int32_t __cdecl SaveType4(am2_FILE *fp, void *obj)
{
    const uint8_t *rec = (const uint8_t *)obj + OBJ_OFF_FIELD_94;

    if (!SaveType1(fp, obj))
        return 0;

    WriteSaveTag(fp, *(const uint32_t *)(rec + 0x30));
    WriteSaveTag(fp, *(const uint32_t *)(rec + 0x34));
    WriteSaveTag(fp, *(const uint32_t *)(rec + 0x38));
    return 1;
}

int32_t __cdecl SaveType5(am2_FILE *fp, void *obj)
{
    const uint8_t *rec = (const uint8_t *)obj + OBJ_OFF_FIELD_94;

    WriteSaveTag(fp, **(const uint32_t *const *)rec);
    WriteSaveTag(fp, *(const uint32_t *)(rec + 0x04));

    orig_fwrite(rec + 0x08, 4, 1, fp);
    orig_fwrite(rec + 0x10, 4, 1, fp);
    orig_fwrite(rec + 0x18, 4, 1, fp);
    return 1;
}

/* Still original: the ten-argument maker, declared here as item.cpp declares
 * it -- same address, same shape, and the two modules are not each other's
 * headers. */
typedef void *(__cdecl *AM2_SpawnAtFn)(int32_t x, int32_t y, int32_t kind,
                                       int32_t army, uint32_t uid,
                                       int32_t extra, int32_t e, int32_t f,
                                       int32_t g, int32_t h);
#define orig_spawn_at ((AM2_SpawnAtFn)(uintptr_t)ADDR_SPAWN_AT)

/* 0x00422780, one caller. The type 6 loader.
 *
 * IT MAKES THE OBJECT OUT OF THE RECORD AND THEN OVERWRITES IT WITH THE SAME
 * RECORD. The 0x28 bytes are read onto the stack, three of their fields go to
 * ADDR_SPAWN_AT as the kind, the uid and one more, and the whole record is
 * then copied to OBJ_OFF_FIELD_94 -- so whatever the maker derived from those
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
void *__cdecl LoadType6(am2_FILE *fp, const void *hdr)
{
    const uint8_t *h = (const uint8_t *)hdr;
    uint8_t        rec[AM2_TYPE6_RECORD_SIZE];
    uint8_t       *made;

    orig_fread(rec, AM2_TYPE6_RECORD_SIZE, 1, fp);

    made = (uint8_t *)orig_spawn_at(
               *(const int16_t *)(h + OBJ_OFF_POS),
               *(const int16_t *)(h + OBJ_OFF_POS + 2),
               *(const int32_t *)(rec + TYPE6_REC_OFF_KIND),
               *(const int8_t *)(h + OBJ_OFF_ARMY),
               *(const uint32_t *)(rec + TYPE6_REC_OFF_UID),
               *(const int32_t *)(rec + TYPE6_REC_OFF_EXTRA),
               0, 1, *(const int32_t *)(h + 4), 0);

    memcpy(made, h, (size_t)kItemHeaderSize);
    memcpy(made + OBJ_OFF_FIELD_94, rec, AM2_TYPE6_RECORD_SIZE);
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
void *__cdecl LoadType7(am2_FILE *fp, const void *hdr)
{
    const uint8_t *h = (const uint8_t *)hdr;
    uint8_t       *made;

    (void)fp;

    made = (uint8_t *)MakeKind7(*(const uint32_t *)(h + OBJ_OFF_POS), 1,
                                      *(const int8_t *)(h + OBJ_OFF_ARMY),
                                      *(const uint8_t *)(h + OBJ_OFF_FACING),
                                      1,
                                      *(const int32_t *)(h + 4));
    if (!made)
        return (void *)0;

    memcpy(made, h, (size_t)kItemHeaderSize);
    return made;
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
int32_t __cdecl SaveOneItem(am2_FILE *fp, void *obj)
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
    case 7:  if (!ReturnOne()) return 0; break;   /* the stub; see above */
    case 8:  if (!SaveType8(fp, obj)) return 0; break;
    default: break;          /* unknown type: header only, and NOT an error */
    }
    return 1;
}

typedef int32_t (__cdecl *AM2_LoadHdrFn)(am2_FILE *fp, void *hdr);
typedef void *(__cdecl *AM2_LoadObjFn)(am2_FILE *fp, void *hdr);
typedef void *(__cdecl *AM2_LoadObj3Fn)(am2_FILE *fp, void *hdr, int32_t a);
#define orig_load_type2  ((AM2_LoadObj3Fn)(uintptr_t)ADDR_LOAD_TYPE2)
#define orig_load_type3  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE3)
#define orig_load_type4  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE4)

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
void *__cdecl LoadOneItem(am2_FILE *fp, int32_t arg)
{
    uint8_t  hdr[AM2_ITEM_HEADER_BYTES];
    uint8_t *made = NULL;
    uint32_t footprint;

    if (!LoadItemHeader(fp, hdr))
        return NULL;

    footprint = *(const uint32_t *)(hdr + OBJ_OFF_FLAGS) & OBJ_FLAG_FOOTPRINT_ON;
    *(uint32_t *)(hdr + OBJ_OFF_FLAGS) &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;

    switch (*(const int32_t *)hdr) {
    case 1:  made = (uint8_t *)LoadType1(fp, hdr);            break;
    case 2:  made = (uint8_t *)orig_load_type2(fp, hdr, arg); break;
    case 3:  made = (uint8_t *)orig_load_type3(fp, hdr);      break;
    case 4:  made = (uint8_t *)orig_load_type4(fp, hdr);      break;
    case 5:  made = (uint8_t *)LoadType5(fp, hdr);      break;
    case 6:  made = (uint8_t *)LoadType6(fp, hdr);      break;
    case 7:  made = (uint8_t *)LoadType7(fp, hdr);            break;
    case 8:  made = (uint8_t *)LoadType8(fp, hdr);      break;
    default: break;
    }
    if (!made)
        return NULL;

    {
        uint32_t flags = *(const uint32_t *)(made + OBJ_OFF_FLAGS);

        if (footprint)
            flags |= OBJ_FLAG_FOOTPRINT_ON;
        else
            flags &= ~(uint32_t)OBJ_FLAG_FOOTPRINT_ON;
        *(uint32_t *)(made + OBJ_OFF_FLAGS) = flags;

        *(uint32_t *)(made + OBJ_OFF_FLAGS) =
            flags & ~(uint32_t)OBJ_FLAG_SELECTED;
    }

    ApplyObjHeight(made,
                          (int32_t)*(const int8_t *)(made + OBJ_OFF_HEIGHT_SET));
    return made;
}

/* ---------------------------------------------- the uid remap table ---- */

#define g_uidRemapCap   (*(int32_t *)(uintptr_t)ADDR_UID_REMAP_CAP)
#define g_uidRemapCount (*(int32_t *)(uintptr_t)ADDR_UID_REMAP_COUNT)
#define g_uidRemap      (*(uint32_t **)(uintptr_t)ADDR_UID_REMAP)

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

    g_uidRemapCap   = 0;
    g_uidRemapCount = 0;
    g_uidRemap      = (uint32_t *)0;
}

void __cdecl UidRemapAdd(uint32_t from, uint32_t to)
{
    int32_t n;

    if (g_uidRemapCount >= g_uidRemapCap) {
        g_uidRemapCap += AM2_UID_REMAP_GROW;
        g_uidRemap = (uint32_t *)am2_realloc(
                         g_uidRemap, (size_t)(g_uidRemapCap * 8));
    }

    n = g_uidRemapCount;
    g_uidRemap[n * 2]     = from;
    g_uidRemap[n * 2 + 1] = to;
    g_uidRemapCount       = n + 1;
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
typedef void (__cdecl *AM2_Call3Fn)(int32_t a, int32_t b, int32_t c);

#define orig_teardown_log   ((AM2_TeardownFn)(uintptr_t)ADDR_LOG)
#define orig_free_list_a    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_LIST_662024)
#define orig_free_list_b    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_LIST_662928)
/* 0x0040A4B0 is BuildRemapTables, reconstructed in win32/palette.cpp and
 * called by name -- see the note below. Declared here rather than by
 * including that header: gameproc.cpp is on the flat side of the split and
 * palette.h names Win32 types, while this one signature names none. */
extern "C" void __cdecl BuildRemapTables(void);
#define orig_free_40a5f0    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_40A5F0)
#define orig_big_405220     ((AM2_Call3Fn)(uintptr_t)ADDR_BIG_405220)

/* 0x0041E740. Zero a global NOTHING READS. One reference in the whole image
 * -- this store -- confirmed by a decoded scan and a raw dword scan both. So
 * the teardown clears it every time and no code anywhere looks. Reproduced;
 * a write to a dead global is still a write, and dropping it would be the
 * first thing to go wrong if something starts reading it. */
void __cdecl ZeroUnread50C34C(void)
{
    *(int32_t *)(uintptr_t)ADDR_UNREAD_50C34C = 0;
}

/* 0x00402670. Its answer is always 0 and its one caller ignores it, so the
 * only thing it does is the flag. */
int32_t __cdecl NoteKind31(void *rec)
{
    if (**(const int32_t *const *)((const uint8_t *)rec + 0x20) == AM2_KIND_31)
        *(int32_t *)(*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT
                     + COMM_OFF_SAW_KIND_31) = 1;

    return 0;
}

/* 0x004057B0, two callers. A pass-through and nothing else: three arguments
 * forwarded to a 1,424-byte function that has two other callers of its own. */
void __cdecl Call405220(int32_t a, int32_t b, int32_t c)
{
    orig_big_405220(a, b, c);
}

/* 0x00445FE0. Free, then log. */
void __cdecl Teardown445F40(void)
{
    FreeSpriteRegistry();
    orig_teardown_log();
}

/* 0x004033E0. Free four things, then log. Two of the four already had names,
 * which is the only reason this one reads as anything but four addresses. */
void __cdecl TeardownDefTables(void)
{
    DefFreeTables();
    orig_free_list_a();
    orig_free_list_b();
    DefFreeTrooperRecs();
    orig_teardown_log();
}

/* 0x0040A690. Two steps, the second a tail jump -- and both halves sit inside
 * ONE functions.tsv entry, which is why the second has no entry of its own
 * and why tools/merges.py is what finds it.
 *
 * ITS NAME IS NOW KNOWN TO BE WRONG and is left for the commit that fixes it
 * properly. The first step is BuildRemapTables, which FILLS four army remaps,
 * sixty-four variation blocks and a grey ramp -- so this is an INIT, and
 * "Teardown" came from the same guess ADDR_FREE_40A4B0 did. Whether the
 * second step frees anything is unread; renaming the pair on half the
 * evidence is what put the wrong name here in the first place. */
void __cdecl Teardown40A4B0(void)
{
    BuildRemapTables();
    orig_free_40a5f0();
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

#define orig_big_4057d0   ((AM2_Call3Fn2)(uintptr_t)ADDR_BIG_4057D0)
#define orig_def_460290   ((AM2_VoidFn)(uintptr_t)ADDR_DEF_STEP_460290)
#define orig_def_45ebc0   ((AM2_VoidFn)(uintptr_t)ADDR_DEF_STEP_45EBC0)

void __cdecl Call4057D0(int32_t a, int32_t b, int32_t c)
{
    orig_big_4057d0(a, b, c);
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
    orig_def_460290();
    DefSortObjRecs();
    DefCheckLinks();
    orig_def_45ebc0();
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
void __cdecl WalkCellWrapper(void *unused, uint32_t at)
{
    (void)unused;

    /* The callback is still the original's, so it goes in by address -- and
     * as a typed function pointer rather than a void *, now that
     * WalkCellAtPoint's third parameter says what it is. That parameter used
     * to be untyped because the macro declared the whole function `void`. */
    WalkCellAtPoint(&at, (void *)(uintptr_t)ADDR_OBJ_MAP_DESC,
                    (int32_t (__cdecl *)(void *))(uintptr_t)
                        ADDR_WALK_CELL_CALLBACK);
}

/* 0x0042F140. Undo what HostBattle set up: reset the pair mask, put 1000 in
 * the value beside it, and EMPTY BOTH SAVED NAMES -- by storing a zero byte
 * at the front of each, not by clearing the buffer. The two names are
 * ADDR_SAVED_PLAYER_NAME and ADDR_SAVED_BATTLE_NAME, which is what makes this
 * the reset rather than an unrelated three-global write. */
void __cdecl ResetHostState(void)
{
    ResetPairMask((uint32_t *)(uintptr_t)ADDR_HOST_MASK_A,
                  (uint32_t *)(uintptr_t)ADDR_HOST_MASK_B);

    *(int32_t *)(uintptr_t)ADDR_HOST_VALUE_3E8 = 1000;
    *(char *)(uintptr_t)ADDR_SAVED_PLAYER_NAME = '\0';
    *(char *)(uintptr_t)ADDR_SAVED_BATTLE_NAME = '\0';
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

    if (!*(const char *)(uintptr_t)ADDR_GAMEPROC_STR_B)
        return (am2_FILE *)0;
    if (!*(const char *)(uintptr_t)ADDR_GAMEPROC_BLOCK)
        return (am2_FILE *)0;

    am2_sprintf(path, (const char *)(uintptr_t)ADDR_STR_SAVE_PLAYER_FMT,
                 (const char *)(uintptr_t)ADDR_GAMEPROC_BLOCK);
    SetGameDir(path);

    fp = orig_fopen((const char *)(uintptr_t)ADDR_GAMEPROC_STR_B,
                    (const char *)(uintptr_t)ADDR_MODE_RB);
    if (!fp)
        return fp;

    if (!CheckSaveTag(fp, AM2_SAVETAG_GAMEPROC,
                      (const char *)(uintptr_t)ADDR_STR_GAMEPROC_CPP,
                      AM2_GAMEPROC_TAG_LINE)
        || !LoadGameProcSection(fp)) {
        orig_fclose(fp);
        return (am2_FILE *)0;
    }

    orig_fseek(fp, 0, 0);
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
void *__attribute__((thiscall)) SelListConstruct(void *block)
{
    InitPtrList((uint8_t *)block + GAMEPROC_OFF_SELECTED);
    return block;
}

/* 0x004248D0, thiscall. One `add ecx` and a tail jump. */
void __attribute__((thiscall)) SelListDestruct(void *block)
{
    ClearPtrListAlias((uint8_t *)block + GAMEPROC_OFF_SELECTED);
}

/* 0x004248A0 and 0x004248C0: the two thunks that name the object. Each is
 * `mov ecx, <block>; jmp <body>`, so they are the only place the global is
 * mentioned. */
void *__cdecl SelListConstructThunk(void)
{
    return SelListConstruct((void *)AM2_IMAGE(ADDR_GAMEPROC_BLOCK));
}

void __cdecl SelListDestructThunk(void)
{
    SelListDestruct((void *)AM2_IMAGE(ADDR_GAMEPROC_BLOCK));
}

/* 0x004248B0. Hands the destructor thunk to the CRT's atexit and returns its
 * answer -- the `ret` follows the call with eax untouched, so the result is
 * atexit's 0 or -1 and not a value of its own.
 *
 * It passes OUR thunk rather than the image address the original pushes. The
 * two are the same registration: the original address holds a detour to this
 * code by the time exit runs. Naming the patched address here would be the lie
 * tools/checkseams.py exists to catch, and it would buy one extra hop. */
int32_t __cdecl SelListAtExit(void)
{
    return orig_atexit(SelListDestructThunk);
}

/* 0x00424890. The initialiser-table entry: construct, then register the
 * teardown. Its only reference in the image is a dword at 0x00473048, so it is
 * reached by `refs_to` and never by `xrefs`. */
int32_t __cdecl SelListInit(void)
{
    SelListConstructThunk();
    return SelListAtExit();
}

void gameproc_install(void)
{
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
    patch_replace(ADDR_TEARDOWN_40A4B0, (const void *)Teardown40A4B0,
                  "Teardown40A4B0", 1);
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
    patch_replace(ADDR_SEL_LIST_INIT, (const void *)SelListInit,
                  "SelListInit", 1);
    patch_replace(ADDR_SEL_LIST_CTOR_THUNK, (const void *)SelListConstructThunk,
                  "SelListConstructThunk", 1);
    patch_replace(ADDR_SEL_LIST_ATEXIT, (const void *)SelListAtExit,
                  "SelListAtExit", 1);
    patch_replace(ADDR_SEL_LIST_DTOR_THUNK, (const void *)SelListDestructThunk,
                  "SelListDestructThunk", 1);
    patch_replace(ADDR_SEL_LIST_DTOR, (const void *)SelListDestruct,
                  "SelListDestruct", 1);
    patch_replace(ADDR_SEL_LIST_CTOR, (const void *)SelListConstruct,
                  "SelListConstruct", 1);
}
