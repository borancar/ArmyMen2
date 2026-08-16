#include "patch.h"
#include "hooklog.h"
#include "trace.h"

#include <windows.h>

static int g_installed;

int patch_replace(uint32_t target, const void *replacement, const char *name,
                  int32_t nargs)
{
    uint8_t *p = (uint8_t *)(uintptr_t)target;
    const void *dest;
    DWORD    prot = 0;
    int32_t  rel;

    /* When tracing is on this returns a stub that logs and then jumps to
     * `replacement`; otherwise it hands back `replacement` untouched. */
    dest = trace_wrap(replacement, name, nargs);

    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) {
        hooklog("patch: VirtualProtect failed for %s at %08x (err %lu)",
                name, target, GetLastError());
        return 1;
    }

    /* rel32 is measured from the end of the five-byte jmp. */
    rel = (int32_t)((const uint8_t *)dest - (p + 5));
    p[0] = 0xE9;
    p[1] = (uint8_t)(rel         & 0xFF);
    p[2] = (uint8_t)((rel >> 8)  & 0xFF);
    p[3] = (uint8_t)((rel >> 16) & 0xFF);
    p[4] = (uint8_t)((rel >> 24) & 0xFF);

    VirtualProtect(p, 5, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, 5);

    g_installed++;
    hooklog("patch: %-14s %08x -> %p%s", name, target, replacement,
            dest == replacement ? "" : " (traced)");
    return 0;
}

int patch_count(void)
{
    return g_installed;
}
