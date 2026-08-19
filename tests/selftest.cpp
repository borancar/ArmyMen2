/* Replay the recorded vectors against the reconstruction.
 *
 * The vectors come from emulating the ORIGINAL function over the mapped PE
 * image (tools/vectors.py), so this compares our C++ against the binary
 * itself, with no part of the game running -- no Wine display, no mission, no
 * scripted clicks. A failure names one function and the arguments that expose
 * it, which the whole-game A/B has never been able to do.
 *
 * Only functions that touch no global data can be checked this way. One that
 * reads a global would need that global mapped, and mapping it means starting
 * the game, which is the thing this avoids.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/game/rect.h"
#include "../src/game/dist.h"
#include "../src/game/packkey.h"
#include "../src/game/item.h"
#include "../src/game/msgslot.h"
#include "../src/game/objflag.h"
#include "../src/game/misc.h"
#include "../src/game/objtype.h"

#include "vectors.h"

static uint8_t g_scratch[AM2_SCRATCH_LEN + 0x800];

/* Every one of these is cdecl, so a single six-argument invoker serves them
 * all: the caller cleans up, so passing more arguments than the callee reads
 * is harmless. */
typedef uint32_t (__cdecl *am2_any_fn)(uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, uint32_t);

static void FillScratch(void)
{
    for (uint32_t i = 0; i < sizeof g_scratch; i++)
        g_scratch[i] = (uint8_t)((i * 7 + 13) & 0xFF);
}

int main(void)
{
    int32_t pass = 0, fail = 0;

    for (uint32_t v = 0; v < sizeof kVectors / sizeof kVectors[0]; v++) {
        const AM2_Vector *t = &kVectors[v];
        uint32_t a[6];

        FillScratch();
        /* angr chose these bytes to reach a particular branch; without them
         * the pointer arguments would all see the same fixed pattern and the
         * path coverage would be worth nothing. */
        for (int32_t w = 0; w < t->ninputs; w++)
            g_scratch[t->inputs[w * 2]] = (uint8_t)t->inputs[w * 2 + 1];
        /* Pointer chains: a dword in the scratch that must hold the address of
         * another part of it. These cannot travel as bytes, because the value
         * is an address and the two sides have different buffers. */
        for (int32_t w = 0; w < t->nfixups; w++)
            *(uint32_t *)(g_scratch + t->fixups[w * 2]) =
                (uint32_t)(uintptr_t)(g_scratch + t->fixups[w * 2 + 1]);
        for (int32_t i = 0; i < 6; i++)
            a[i] = t->isptr[i] ? (uint32_t)(uintptr_t)(g_scratch + t->arg[i])
                               : t->arg[i];

        uint32_t got = ((am2_any_fn)t->fn)(a[0], a[1], a[2], a[3], a[4], a[5]);

        uint32_t want_eax = t->eax_is_ptr
                          ? (uint32_t)(uintptr_t)(g_scratch + t->eax)
                          : t->eax;
        int bad = t->void_ret ? 0 : (got != want_eax);
        for (int32_t w = 0; w < t->nwrites && !bad; w++) {
            uint32_t off = t->writes[w * 2], want = t->writes[w * 2 + 1];
            if (g_scratch[off] != (uint8_t)want)
                bad = 1;
        }
        if (bad) {
            if (fail < 10)
                printf("  FAIL %-14s (%08x,%08x,%08x) -> %08x, want %08x\n",
                       t->name, t->arg[0], t->arg[1], t->arg[2], got, want_eax);
            fail++;
        } else {
            pass++;
        }
    }
    printf("\n  %d vectors: %d pass, %d fail\n", pass + fail, pass, fail);
    return fail ? 1 : 0;
}

/* The reconstruction sources end with an install function that registers each
 * detour. Nothing installs anything here -- there is no game in the process --
 * so one stub satisfies the link and is never called. */
extern "C" int patch_replace(uint32_t, const void *, const char *, int32_t)
{
    return 0;
}
