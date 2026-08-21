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

/* 0x0042DBB0. XOR every whole dword of a file together.
 *
 * Named by its own two log lines, which are a matched pair -- "Checksum of %s "
 * with no newline, then "is %x \n" -- so one call produces one line whichever
 * way it goes. Both are unconditional: a file that will not open still gets
 * announced and still reports a checksum, which is 0.
 *
 * A TRAILING PARTIAL DWORD IS DROPPED. fread asks for one 4-byte item and
 * returns 0 for anything short, so a file whose length is not a multiple of
 * four ignores its last one to three bytes. Reproduced.
 *
 * The original reuses its own argument slot as the read buffer; that is a
 * register-allocation detail with no observable side, so this uses a local. */
uint32_t __cdecl Checksum(const char *path)
{
    uint32_t  sum = 0;
    uint32_t  word;
    am2_FILE *fp;

    orig_log("Checksum of %s ", path);

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_MODE_RB));

    if (fp != (am2_FILE *)0) {
        while (orig_fread(&word, 4, 1, fp) != 0)
            sum ^= word;

        orig_fclose(fp);
    }

    orig_log("is %x \n", sum);
    return sum;
}

void map_install(void)
{
    patch_replace(ADDR_CHECKSUM, (const void *)Checksum, "Checksum", 1);
    patch_replace(ADDR_SAVE_MAP_SECTION, (const void *)SaveMapSection,
                  "SaveMapSection", 1);
    patch_replace(ADDR_LOAD_MAP_SECTION, (const void *)LoadMapSection,
                  "LoadMapSection", 1);
}
