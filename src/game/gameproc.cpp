/* gameproc.cpp -- see gameproc.h. */
#include <stdint.h>
#include <string.h>

#include "gameproc.h"
#include "savetag.h"
#include "defparse.h"  /* DefFreeTables -- reconstructed */
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
#include "crt.h"       /* am2_realloc, am2_free -- the game's own */

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

/* Still original: the footprint pair SaveType3 brackets its write with, and
 * the weapon lookup SaveType2 tags through. */
typedef void (__cdecl *AM2_FootprintFn)(void *obj);
#define orig_clear_footprint ((AM2_FootprintFn)(uintptr_t)ADDR_OBJ_CLEAR_FOOTPRINT)
#define orig_set_footprint   ((AM2_FootprintFn)(uintptr_t)ADDR_OBJ_SET_FOOTPRINT)

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

    orig_clear_footprint(o);

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
    orig_set_footprint(o);
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
#define orig_load_type1  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE1)
#define orig_load_type2  ((AM2_LoadObj3Fn)(uintptr_t)ADDR_LOAD_TYPE2)
#define orig_load_type3  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE3)
#define orig_load_type4  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE4)
#define orig_load_type5  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE5)
#define orig_load_type8  ((AM2_LoadObjFn)(uintptr_t)ADDR_LOAD_TYPE8)

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
    case 6:  made = (uint8_t *)LoadType6(fp, hdr);      break;
    case 7:  made = (uint8_t *)LoadType7(fp, hdr);            break;
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
#define orig_free_445f40    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_445F40)
#define orig_free_list_a    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_LIST_662024)
#define orig_free_list_b    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_LIST_662928)
#define orig_free_40a4b0    ((AM2_TeardownFn)(uintptr_t)ADDR_FREE_40A4B0)
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
    orig_free_445f40();
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
 * and why tools/merges.py is what finds it. */
void __cdecl Teardown40A4B0(void)
{
    orig_free_40a4b0();
    orig_free_40a5f0();
}

/* ---- Six more, same size and same argument ----------------------------
 *
 * TWO MORE THREE-ARGUMENT PASS-THROUGHS, which makes four in this tree with
 * Call405220 above. Each forwards its three arguments to one large function
 * and adds nothing at all. The compiler did not produce these: a thunk that
 * only moves arguments is what a source-level wrapper compiles to, so the
 * original had four one-line functions and so does this. Worth knowing before
 * reading one as a place where something happens.
 */
typedef void (__cdecl *AM2_Call3Fn2)(int32_t a, int32_t b, int32_t c);
typedef void (__cdecl *AM2_VoidFn)(void);
typedef void (__cdecl *AM2_WalkCellFn)(const uint32_t *pt, void *desc,
                                       void *fn);

#define orig_big_4057d0   ((AM2_Call3Fn2)(uintptr_t)ADDR_BIG_4057D0)
#define orig_big_407710   ((AM2_Call3Fn2)(uintptr_t)ADDR_BIG_407710)
#define orig_def_460290   ((AM2_VoidFn)(uintptr_t)ADDR_DEF_STEP_460290)
#define orig_def_45ebc0   ((AM2_VoidFn)(uintptr_t)ADDR_DEF_STEP_45EBC0)
#define orig_walk_cell    ((AM2_WalkCellFn)(uintptr_t)ADDR_WALK_CELL_AT_POINT)

void __cdecl Call4057D0(int32_t a, int32_t b, int32_t c)
{
    orig_big_4057d0(a, b, c);
}

void __cdecl Call407710(int32_t a, int32_t b, int32_t c)
{
    orig_big_407710(a, b, c);
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

    orig_walk_cell(&at, (void *)(uintptr_t)ADDR_OBJ_MAP_DESC,
                   (void *)(uintptr_t)ADDR_WALK_CELL_CALLBACK);
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

void gameproc_install(void)
{
    patch_replace(ADDR_ZERO_50C34C, (const void *)ZeroUnread50C34C,
                  "ZeroUnread50C34C", 1);
    patch_replace(ADDR_CALL_4057D0, (const void *)Call4057D0, "Call4057D0", 1);
    patch_replace(ADDR_CALL_407710, (const void *)Call407710, "Call407710", 1);
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
    patch_replace(ADDR_SAVE_TYPE4, (const void *)SaveType4, "SaveType4", 1);
    patch_replace(ADDR_SAVE_TYPE5, (const void *)SaveType5, "SaveType5", 1);
    patch_replace(ADDR_SAVE_TYPE2, (const void *)SaveType2, "SaveType2", 1);
    patch_replace(ADDR_SAVE_TYPE3, (const void *)SaveType3, "SaveType3", 1);
    patch_replace(ADDR_LOAD_TYPE7, (const void *)LoadType7, "LoadType7", 1);
    patch_replace(ADDR_LOAD_TYPE6, (const void *)LoadType6, "LoadType6", 1);
}
