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

/* PlaySoundAt is reconstructed, in win32/audio.cpp. Declared here rather than
 * by including that header because this module is on the flat side of the
 * split and audio.h names Win32 types -- the same reason commmsg.cpp does it,
 * and spelled the same way so the two cannot drift. */
extern "C" void __cdecl PlaySoundAt(int32_t index, int32_t flags,
                                    int32_t unused, int32_t x, int32_t y);

/* Every one of these is a field of the block above -- the queue and the
 * savegame section are the same 584 bytes, which is why they are written as
 * offsets rather than as addresses of their own. */
#define kAirField(off) ((uint8_t *)kAirSaveBlock + (off))
#define g_airActive  (*(int32_t *)kAirField(AIR_OFF_ACTIVE))
#define g_airPending (*(int32_t *)kAirField(AIR_OFF_PENDING))
#define g_airCount   (*(int32_t *)kAirField(AIR_OFF_COUNT))
#define g_airWhere   ((uint16_t *)kAirField(AIR_OFF_WHERE))
#define g_airKind    ((int32_t *)kAirField(AIR_OFF_KIND))
#define g_airFrom    ((uint32_t *)kAirField(AIR_OFF_FROM))
#define g_airExtra   ((int32_t *)kAirField(AIR_OFF_EXTRA))
#define g_airFlagA   (*(int32_t *)kAirField(AIR_OFF_FLAG_A))
#define g_airFlagB   (*(int32_t *)kAirField(AIR_OFF_FLAG_B))

void __cdecl AirSupportBegin(void)
{
    /* The head entry's `extra` decides which of the two shapes runs, and the
     * two do NOT agree about the active flag: only the first sets it. */
    if (g_airExtra[0]) {
        g_airFlagA = 1;
        g_airFlagB = 1;
        return;
    }

    PlaySoundAt(AM2_AIR_SOUND, 0, 0, 0, 0);
    g_airActive = 1;
    g_airFlagA  = 0;
    g_airFlagB  = 0;
}

void __cdecl AirSupportClear(void)
{
    g_airActive = 0;
    g_airFlagA  = 0;
    g_airFlagB  = 0;
}

void __cdecl AirSupportPop(void)
{
    int32_t i;

    /* Shift all four arrays down one. The point is copied as its two 16-bit
     * halves, which is how the packing shows through. */
    for (i = 1; i < g_airCount; i++) {
        g_airKind[i - 1]      = g_airKind[i];
        g_airWhere[(i - 1) * 2]     = g_airWhere[i * 2];
        g_airWhere[(i - 1) * 2 + 1] = g_airWhere[i * 2 + 1];
        g_airFrom[i - 1]      = g_airFrom[i];
        g_airExtra[i - 1]     = g_airExtra[i];
    }

    g_airCount -= 1;
    /* The count is written BEFORE the log, and the log is not gated on
     * anything. "EndMission" here is a prefix, not this function's name. */
    orig_log("EndMission  AirSupport.count decreasing to: %d\n", g_airCount);
    g_airPending = 0;

    /* Tail calls in the original, both of them. */
    if (g_airCount)
        AirSupportBegin();
    else
        AirSupportClear();
}

void air_install(void)
{
    patch_replace(ADDR_AIR_BEGIN, (const void *)AirSupportBegin,
                  "AirSupportBegin", 2);
    patch_replace(ADDR_AIR_CLEAR, (const void *)AirSupportClear,
                  "AirSupportClear", 1);
    patch_replace(ADDR_AIR_POP, (const void *)AirSupportPop,
                  "AirSupportPop", 2);
    patch_replace(ADDR_SAVE_AIR_SECTION, (const void *)SaveAirSection,
                  "SaveAirSection", 1);
    patch_replace(ADDR_LOAD_AIR_SECTION, (const void *)LoadAirSection,
                  "LoadAirSection", 1);
}
