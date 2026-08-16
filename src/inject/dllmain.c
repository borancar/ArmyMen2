/* am2hook.dll -- the reconstruction harness.
 *
 * Injected into ArmyMen2.exe by launcher.exe before the game's entry point
 * runs. On attach it fingerprints the loaded image, then installs each
 * reconstructed function over its original.
 *
 * Nothing on disk is modified. Every patch is an in-memory `jmp rel32`, so
 * reverting is a matter of not injecting.
 */

#include "hooklog.h"
#include "gamelog.h"
#include "orig.h"
#include "patch.h"
#include "../game/savetag.h"

#include <windows.h>
#include <string.h>

/* Refuse to patch anything unless the bytes we are about to overwrite are
 * exactly the ones we disassembled. A different build of the game would have
 * everything at different addresses, and blind patching would corrupt it. */
static const struct {
    uint32_t       addr;
    const char    *name;
    uint8_t        bytes[8];
    uint32_t       len;
} kFingerprints[] = {
    { ADDR_CHECK_SAVE_TAG, "CheckSaveTag",
      { 0x8B, 0x44, 0x24, 0x04, 0x8D, 0x4C, 0x24, 0x04 }, 8 },
    { ADDR_LOG, "Log",
      { 0xC3, 0x90, 0x90, 0x90, 0x90 }, 5 },
    { ADDR_FREAD, "fread",
      { 0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57 }, 7 },
};

static int verify_image(void)
{
    size_t i;
    int    bad = 0;

    if (GetModuleHandleA(NULL) != (HMODULE)AM2_IMAGE_BASE) {
        hooklog("verify: image base is %p, expected %08x",
                GetModuleHandleA(NULL), AM2_IMAGE_BASE);
        return 1;
    }

    for (i = 0; i < sizeof kFingerprints / sizeof kFingerprints[0]; i++) {
        const void *p = (const void *)(uintptr_t)kFingerprints[i].addr;
        if (IsBadReadPtr(p, kFingerprints[i].len) ||
            memcmp(p, kFingerprints[i].bytes, kFingerprints[i].len) != 0) {
            hooklog("verify: %s at %08x does not match expected bytes",
                    kFingerprints[i].name, kFingerprints[i].addr);
            bad = 1;
        }
    }
    return bad;
}

static void install(void)
{
    hooklog_open();
    hooklog("am2hook attached to pid %lu", GetCurrentProcessId());

    if (verify_image()) {
        hooklog("am2hook: image fingerprint failed -- no patches installed");
        return;
    }
    hooklog("verify: image matches ArmyMen2.exe (MSVC 6, 1999-02-03)");

    gamelog_install();
    savetag_install();

    hooklog("am2hook: %d patch(es) installed", patch_count());
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(inst);
        install();
        break;
    case DLL_PROCESS_DETACH:
        hooklog("am2hook detaching");
        hooklog_close();
        break;
    default:
        break;
    }
    return TRUE;
}
