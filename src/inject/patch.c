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

    /* AM2_NOPATCH_NAMES=A,B,C leaves those reconstructions uninstalled and
     * everything else patched: the differential the whole-program
     * AM2_NOPATCH cannot give, since a subsystem's output is the sum of
     * several functions and this says which one moved it. Found necessary
     * when the cell-weight plane differed from the original's in thousands
     * of cells and four functions could each have been the reason. */
    {
        static const char *skip;
        static int         looked;
        if (!looked) {
            looked = 1;
            skip = getenv("AM2_NOPATCH_NAMES");
        }
        if (skip && *skip) {
            const char *s = skip;
            size_t      n = strlen(name);
            while (*s) {
                const char *e = s;
                while (*e && *e != ',')
                    e++;
                if ((size_t)(e - s) == n && !strncmp(s, name, n)) {
                    hooklog("patch: %-14s %08x LEFT ORIGINAL (AM2_NOPATCH_NAMES)",
                            name, target);
                    return 0;
                }
                s = *e ? e + 1 : e;
            }
        }
    }

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

int patch_byte(uint32_t target, uint8_t expect, uint8_t value, const char *what)
{
    uint8_t *p = (uint8_t *)(uintptr_t)target;
    DWORD    prot = 0;

    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &prot)) {
        hooklog("restore: VirtualProtect failed for %s at %08x (err %lu)",
                what, target, GetLastError());
        return 1;
    }

    if (*p != expect) {
        hooklog("restore: %s at %08x is %02x, expected %02x -- not touching it",
                what, target, *p, expect);
        VirtualProtect(p, 1, prot, &prot);
        return 1;
    }

    *p = value;
    VirtualProtect(p, 1, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    hooklog("restore: %s at %08x  %02x -> %02x", what, target, expect, value);
    return 0;
}

int patch_count(void)
{
    return g_installed;
}
