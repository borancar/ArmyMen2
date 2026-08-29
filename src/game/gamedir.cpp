/* gamedir.cpp -- see gamedir.h. */
#include <stdint.h>
#include <string.h>

#include "crt.h"
#include "gamedir.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

/* The original's buffer is 0x100 bytes on the stack and it uses plain strcpy
 * and strcat, so a long enough install path overruns it. Kept as it is: the
 * paths are the ones the installer wrote, and a bounded copy here would be a
 * different function whose failure mode nothing else in the image expects. */
int32_t __cdecl SetGameDir(const char *subdir)
{
    char path[0x100];

    strcpy(path, (const char *)AM2_IMAGE(ADDR_GAME_DIR));
    strcat(path, (const char *)AM2_IMAGE(ADDR_STR_PATH_SEP));
    strcat(path, subdir);

    if (am2_chdir(path) == 0) {
        /* Latched, never cleared here: leaving the directory again does not
         * put it back. Only the name matters, not where we ended up. */
        if (strcmp(subdir, *(const char *const *)AM2_IMAGE(ADDR_STR_AVI_DIR)) == 0)
            *(int32_t *)AM2_IMAGE(ADDR_OPT_BIG_MOVIES) = 1;
        return 1;
    }

    if (!*(const int32_t *)AM2_IMAGE(ADDR_CD_PRESENT))
        return 0;

    /* No separator appended -- the fallback base already ends in one. */
    strcpy(path, (const char *)AM2_IMAGE(ADDR_CD_PATH));
    strcat(path, subdir);

    return am2_chdir(path) == 0;
}

/* 0x00422D80 -- "is this file there", answered by opening it and closing it
 * again. There is no GetFileAttributes anywhere in the image; the whole of
 * this game's file handling goes through the CRT, so this is what an
 * existence test looks like here.
 *
 * The mode is "r" and not the "rb" the checksum uses. Nothing is read, so the
 * text-mode translation never happens -- but it is the byte the original
 * pushes and there is no reason to improve on it. */
int32_t __cdecl FileExists(const char *path)
{
    am2_FILE *fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_STR_MODE_R));

    if (fp == (am2_FILE *)0)
        return 0;

    orig_fclose(fp);
    return 1;
}

/* FileHasSaveTag -- original 0x00423620, two callers, both save-game dialogs.
 *
 * Does this file begin with a save tag? Open it, read four bytes, close it,
 * and answer whether they are one of TWO accepted values.
 *
 * THE TWO ARMS ARE NOT THE SAME SHAPE, which is the only interesting thing
 * about it: the first comparison answers a literal 1 and the second answers a
 * `sete`. Identical for every caller -- both are 1 or 0 -- and reproduced as
 * one expression, since the difference is the compiler's and not the
 * function's.
 *
 * IT DOES NOT CHECK WHETHER THE READ SUCCEEDED. A file that opens and gives
 * fewer than four bytes leaves the tag holding whatever the stack had, and
 * the answer is then whatever that compares to. Its callers hand it directory
 * entries, so a zero-length file is reachable. The original's.
 *
 * A failed open answers 0 without closing anything, which is right -- there
 * is nothing to close.
 */
int32_t __cdecl FileHasSaveTag(const char *path)
{
    am2_FILE *fp;
    uint32_t  tag;

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_MODE_RB));
    if (!fp)
        return 0;

    orig_fread(&tag, 4, 1, fp);
    orig_fclose(fp);

    return tag == AM2_SAVETAG_GAMEPROC || tag == AM2_SAVETAG_ALT;
}

int gamedir_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_SET_DATA_DIR, (const void *)SetGameDir,
                        "SetGameDir", 82);
    rc |= patch_replace(ADDR_FILE_EXISTS, (const void *)FileExists,
                        "FileExists", 1);
    rc |= patch_replace(ADDR_FILE_HAS_SAVE_TAG, (const void *)FileHasSaveTag,
                        "FileHasSaveTag", 2);
    return rc;
}
