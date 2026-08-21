/* gameproc.cpp -- see gameproc.h. */
#include <stdint.h>
#include <string.h>

#include "gameproc.h"
#include "savetag.h"
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

void gameproc_install(void)
{
    patch_replace(ADDR_LOAD_GAME, (const void *)LoadGame, "LoadGame", 1);
    patch_replace(ADDR_SAVE_GAMEPROC, (const void *)SaveGameProcSection,
                  "SaveGameProcSection", 1);
    patch_replace(ADDR_LOAD_GAMEPROC, (const void *)LoadGameProcSection,
                  "LoadGameProcSection", 1);
}
