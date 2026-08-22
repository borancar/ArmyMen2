/* pad.cpp -- see pad.h. */
#include <stdint.h>
#include <string.h>

#include "pad.h"
#include "savetag.h"
#include "image.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#define kPads        ((void *)(uintptr_t)AM2_IMAGE(ADDR_PADS))
#define kPadNumbers  ((void *)(uintptr_t)AM2_IMAGE(ADDR_PAD_NUMBERS))
#define kPadCount    (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_PAD_COUNT))

void __cdecl ResetPads(void)
{
    memset(kPadNumbers, 0, AM2_PAD_NUMBERS_BYTES);
    memset(kPads, 0, AM2_PADS_BYTES);
    kPadCount = 0;
}

int32_t __cdecl SavePadSection(am2_FILE *fp)
{
    WriteSaveTag(fp, AM2_SAVETAG_PAD);

    /* Not a tag: the count, as data. The loader reads it with fread. */
    WriteSaveTag(fp, (uint32_t)kPadCount);

    /* Higher address first, which is the order the file wants. */
    orig_fwrite(kPadNumbers, AM2_PAD_NUMBERS_BYTES, 1, fp);
    orig_fwrite(kPads, AM2_PADS_BYTES, 1, fp);
    return 1;
}

int32_t __cdecl LoadPadSection(am2_FILE *fp)
{
    int32_t count;

    /* Before the tag check: a failed load has already forgotten every pad. */
    ResetPads();

    if (!CheckSaveTag(fp, AM2_SAVETAG_PAD,
                      (const char *)AM2_IMAGE(ADDR_STR_PAD_CPP), 0x1AD))
        return 0;

    orig_fread(&count, 4, 1, fp);
    kPadCount = count;

    orig_fread(kPadNumbers, AM2_PAD_NUMBERS_BYTES, 1, fp);
    orig_fread(kPads, AM2_PADS_BYTES, 1, fp);
    return 1;
}

void __cdecl ResetPadsAlias(void)
{
    ResetPads();
}

void pad_install(void)
{
    patch_replace(ADDR_RESET_PADS, (const void *)ResetPads, "ResetPads", 0);
    patch_replace(ADDR_RESET_PADS_ALIAS, (const void *)ResetPadsAlias,
                  "ResetPadsAlias", 0);
    patch_replace(ADDR_SAVE_PAD_SECTION, (const void *)SavePadSection,
                  "SavePadSection", 1);
    patch_replace(ADDR_LOAD_PAD_SECTION, (const void *)LoadPadSection,
                  "LoadPadSection", 1);
}
