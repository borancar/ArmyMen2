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
            *(int32_t *)AM2_IMAGE(ADDR_OPT_MUSIC) = 1;
        return 1;
    }

    if (!*(const int32_t *)AM2_IMAGE(ADDR_CD_PRESENT))
        return 0;

    /* No separator appended -- the fallback base already ends in one. */
    strcpy(path, (const char *)AM2_IMAGE(ADDR_CD_PATH));
    strcat(path, subdir);

    return am2_chdir(path) == 0;
}

int gamedir_install(void)
{
    return patch_replace(ADDR_SET_DATA_DIR, (const void *)SetGameDir,
                         "SetGameDir", 82);
}
