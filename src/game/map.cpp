/* map.cpp -- see map.h. */
#include <stdint.h>

#include "map.h"
#include "savetag.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kMapSaveBlock ((void *)(uintptr_t)AM2_IMAGE(ADDR_MAP_SAVE_BLOCK))

int32_t __cdecl SaveMapSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_MAP);
    orig_fwrite(kMapSaveBlock, AM2_MAP_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadMapSection(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_MAP,
                      (const char *)AM2_IMAGE(ADDR_STR_MAP_CPP), 0x906))
        return 0;

    orig_fread(kMapSaveBlock, AM2_MAP_SAVE_SIZE, 1, fp);
    return 1;
}

void map_install(void)
{
    patch_replace(ADDR_SAVE_MAP_SECTION, (const void *)SaveMapSection,
                  "SaveMapSection", 1);
    patch_replace(ADDR_LOAD_MAP_SECTION, (const void *)LoadMapSection,
                  "LoadMapSection", 1);
}
