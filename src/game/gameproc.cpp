/* gameproc.cpp -- see gameproc.h. */
#include <stdint.h>
#include <string.h>

#include "gameproc.h"
#include "savetag.h"
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

void gameproc_install(void)
{
    patch_replace(ADDR_SAVE_GAMEPROC, (const void *)SaveGameProcSection,
                  "SaveGameProcSection", 1);
    patch_replace(ADDR_LOAD_GAMEPROC, (const void *)LoadGameProcSection,
                  "LoadGameProcSection", 1);
}
