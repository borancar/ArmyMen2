/* air.cpp -- see air.h. */
#include <stdint.h>

#include "air.h"
#include "savetag.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kAirSaveBlock ((void *)(uintptr_t)AM2_IMAGE(ADDR_AIR_SAVE_BLOCK))

int32_t __cdecl SaveAirSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_AIR);
    orig_fwrite(kAirSaveBlock, AM2_AIR_SAVE_SIZE, 1, fp);
    return 1;
}

int32_t __cdecl LoadAirSection(am2_FILE *fp)
{
    if (!CheckSaveTag(fp, AM2_SAVETAG_AIR,
                      (const char *)AM2_IMAGE(ADDR_STR_AIR_CPP), 0x28B))
        return 0;

    orig_fread(kAirSaveBlock, AM2_AIR_SAVE_SIZE, 1, fp);
    return 1;
}

void air_install(void)
{
    patch_replace(ADDR_SAVE_AIR_SECTION, (const void *)SaveAirSection,
                  "SaveAirSection", 1);
    patch_replace(ADDR_LOAD_AIR_SECTION, (const void *)LoadAirSection,
                  "LoadAirSection", 1);
}
