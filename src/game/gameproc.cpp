/* gameproc.cpp -- see gameproc.h. */
#include <stdint.h>
#include <string.h>

#include "gameproc.h"
#include "savetag.h"
#include "definfo.h"   /* DefParseNumber, and the .aai vocabulary */
#include "map.h"
#include "pad.h"
#include "air.h"
#include "item.h"
#include "event.h"
#include "script.h"
#include "objscript.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "misc.h"      /* ReturnOne */

#define kBlock  ((char *)(uintptr_t)AM2_IMAGE(ADDR_GAMEPROC_BLOCK))
#define kStrB   ((char *)(uintptr_t)AM2_IMAGE(ADDR_GAMEPROC_STR_B))
#define kVolZero   (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_VOLUME_AT_ZERO))
#define kVolStream (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_STREAM_VOLUME))
#define kVolVoice  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_VOLUME_VOICE))

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
#define orig_save_type2       ((AM2_SaveObjFn)(uintptr_t)ADDR_SAVE_TYPE2)
#define orig_save_type3       ((AM2_SaveObjFn)(uintptr_t)ADDR_SAVE_TYPE3)
#define orig_save_type4       ((AM2_SaveObjFn)(uintptr_t)ADDR_SAVE_TYPE4)
#define orig_save_type5       ((AM2_SaveObjFn)(uintptr_t)ADDR_SAVE_TYPE5)

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
    case 2:  if (!orig_save_type2(fp, obj)) return 0; break;
    case 3:  if (!orig_save_type3(fp, obj)) return 0; break;
    case 4:  if (!orig_save_type4(fp, obj)) return 0; break;
    case 5:  if (!orig_save_type5(fp, obj)) return 0; break;
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
typedef void (__cdecl *AM2_ApplyHeightFn)(void *obj, int32_t height);
#define orig_load_type1  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE1)
#define orig_load_type2  ((AM2_LoadObj3Fn)(uintptr_t)ADDR_LOAD_TYPE2)
#define orig_load_type3  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE3)
#define orig_load_type4  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE4)
#define orig_load_type5  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE5)
#define orig_load_type6  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE6)
#define orig_load_type7  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE7)
#define orig_load_type8  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE8)
#define orig_apply_obj_height ((AM2_ApplyHeightFn)(uintptr_t)ADDR_APPLY_OBJ_HEIGHT)

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
    case 1:  made = (uint8_t *)orig_load_type1(fp, hdr);      break;
    case 2:  made = (uint8_t *)orig_load_type2(fp, hdr, arg); break;
    case 3:  made = (uint8_t *)orig_load_type3(fp, hdr);      break;
    case 4:  made = (uint8_t *)orig_load_type4(fp, hdr);      break;
    case 5:  made = (uint8_t *)orig_load_type5(fp, hdr);      break;
    case 6:  made = (uint8_t *)orig_load_type6(fp, hdr);      break;
    case 7:  made = (uint8_t *)orig_load_type7(fp, hdr);      break;
    case 8:  made = (uint8_t *)orig_load_type8(fp, hdr);      break;
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

    orig_apply_obj_height(made,
                          (int32_t)*(const int8_t *)(made + OBJ_OFF_HEIGHT_SET));
    return made;
}

void gameproc_install(void)
{
    patch_replace(ADDR_LOAD_GAME, (const void *)LoadGame, "LoadGame", 1);
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
    patch_replace(ADDR_SAVE_TYPE6, (const void *)SaveType6, "SaveType6", 1);
    patch_replace(ADDR_SAVE_TYPE8, (const void *)SaveType8, "SaveType8", 1);
}
