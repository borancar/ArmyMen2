#include "observe.h"
#include "hooklog.h"
#include "trace.h"

#include <windows.h>

int observe_install(uint32_t target, const char *name, int32_t nargs,
                    const uint32_t *sites, int32_t nsites)
{
    const void *stub;
    int32_t     i, done = 0, skipped = 0;

    stub = trace_make_stub((const void *)(uintptr_t)target, name, nargs);
    if (!stub) {
        hooklog("observe: no stub for %s", name);
        return 1;
    }

    for (i = 0; i < nsites; i++) {
        uint8_t *p = (uint8_t *)(uintptr_t)sites[i];
        DWORD    prot = 0;
        int32_t  rel;

        /* Re-verify against the live image rather than trusting the generated
         * table: if this is not still a `call rel32` aimed at our target, the
         * binary is not the one the table was built from and writing here
         * would corrupt it. */
        if (IsBadReadPtr(p, 5) || p[0] != 0xE8) {
            skipped++;
            continue;
        }
        rel = (int32_t)((uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                        ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24));
        if ((uint32_t)((int32_t)(uintptr_t)p + 5 + rel) != target) {
            skipped++;
            continue;
        }

        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) {
            skipped++;
            continue;
        }
        rel = (int32_t)((const uint8_t *)stub - (p + 5));
        p[1] = (uint8_t)(rel         & 0xFF);
        p[2] = (uint8_t)((rel >> 8)  & 0xFF);
        p[3] = (uint8_t)((rel >> 16) & 0xFF);
        p[4] = (uint8_t)((rel >> 24) & 0xFF);
        VirtualProtect(p, 5, prot, &prot);
        done++;
    }

    FlushInstructionCache(GetCurrentProcess(), NULL, 0);

    hooklog("observe: %-22s %08x  %d/%d call site(s)%s", name, target,
            done, nsites, skipped ? " (some skipped)" : "");
    return done == 0;
}
