/* Replay the recorded vectors against the reconstruction.
 *
 * The vectors come from emulating the ORIGINAL function over the mapped PE
 * image (tools/vectors.py), so this compares our C++ against the binary
 * itself, with no part of the game running -- no Wine display, no mission, no
 * scripted clicks. A failure names one function and the arguments that expose
 * it, which the whole-game A/B has never been able to do.
 *
 * Functions that read constant tables in the image are checkable too, since
 * tests/loadimage.h copies the sections in from the file -- still with no part
 * of the game running. What stays out of reach is a global the game WRITES at
 * runtime; those need the in-process check (AM2_SELFCHECK=1).
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

#include "../src/game/script.h"
#include "../src/game/crt.h"

#include "loadimage.h"
#include "vectors.h"
#include "scriptvec.h"

static uint8_t g_scratch[AM2_SCRATCH_LEN];

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

static int ScriptTokens(int *passed);
static int ScriptLines(int *passed);

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
        /* Pointers the function was expected to write. These cannot be
         * compared byte for byte: the value is an address, and the emulator
         * and the replay have different buffers. */
        for (int32_t w = 0; w < t->nwptr && !bad; w++) {
            uint32_t at = t->wptr[w * 2], target = t->wptr[w * 2 + 1];
            if (*(uint32_t *)(g_scratch + at)
                != (uint32_t)(uintptr_t)(g_scratch + target))
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

    fail += ScriptTokens(&pass);
    fail += ScriptLines(&pass);
    return fail ? 1 : 0;
}

/* The tokeniser over every distinct line the game ships.
 *
 * The expected stream comes from the ORIGINAL NextToken under Unicorn with
 * AddToken hooked, so what is compared is the sequence of tokens it asked to
 * append -- kind, line number and value, and the text itself for strings.
 *
 * It found a real misreading on its first run: ParseNumber's loop bound is
 * `i < len`, not `i < len - 1`, because the `repne scasb` that measures the
 * string counts the terminator as well. With the off-by-one "1." came out as
 * the integer 1 where the original gives the float 1.0. What exposed it was
 * the numbered headings in the EULA text that ships beside the scripts --
 * nothing in a mission file happens to end a number with a dot.
 *
 * What the corpus does NOT reach: the 0x3F clamp on word length. Moving it to
 * 0x40 passes all 13,956 lines, because no token the game ships is 63
 * characters long. That path and its "Token too long" message are read-only
 * verification.
 */
static int ScriptLines(int *passed)
{
    /* The reconstruction's token list is on this build's heap, not the
     * game's. */
    am2_crt_use_host();

    int pass = 0, fail = 0;
    for (uint32_t i = 0; i < sizeof am2_script_lines /
                             sizeof am2_script_lines[0]; i++) {
        const AM2_ScriptLineVec *lv = &am2_script_lines[i];
        AM2_ScriptCtx ctx = { 0, 0, 0 };
        int bad = 0, at = -1;

        ScriptNextToken(lv->line, &ctx, lv->lineno);

        if (ctx.count != lv->count) {
            bad = 1;
        } else {
            for (int32_t k = 0; k < ctx.count; k++) {
                const AM2_ScriptTokVec *w = &am2_script_toks[lv->at + k];
                const AM2_ScriptTok *g = &ctx.tokens[k];
                if (g->kind != w->kind || g->line != w->line) {
                    bad = 1;
                } else if (w->text) {
                    if (!g->value || strcmp((const char *)g->value, w->text))
                        bad = 1;
                } else if ((uint32_t)(uintptr_t)g->value != w->value) {
                    bad = 1;
                }
                if (bad) {
                    at = k;
                    break;
                }
            }
        }

        if (bad) {
            if (fail < 6) {
                printf("  FAIL line %d: %.60s\n", lv->lineno, lv->line);
                if (at < 0) {
                    printf("     got %d tokens, want %d\n",
                           (int)ctx.count, (int)lv->count);
                } else {
                    const AM2_ScriptTokVec *w = &am2_script_toks[lv->at + at];
                    const AM2_ScriptTok *g = &ctx.tokens[at];
                    printf("     token %d: got kind %d value ", at, g->kind);
                    if (g->kind == AM2_TOKEN_STRING)
                        printf("\"%s\"", (const char *)g->value);
                    else
                        printf("0x%08X", (unsigned)(uintptr_t)g->value);
                    printf(", want kind %d value ", w->kind);
                    if (w->text)
                        printf("\"%s\"\n", w->text);
                    else
                        printf("0x%08X\n", (unsigned)w->value);
                }
            }
            fail++;
        } else {
            pass++;
        }
        ScriptResetTokens(&ctx);
    }
    printf("  %d script lines: %d pass, %d fail\n", pass + fail, pass, fail);
    *passed += pass;
    return fail;
}

/* The script tokeniser, against every distinct word the game ships.
 *
 * A different kind of test from the vectors above and a stronger one for this
 * function: a keyword lookup learns nothing from a random 32-bit argument, and
 * everything from the 9,000-odd words that appear in the real missions. The
 * expected ids come from the original under Unicorn (tools/scriptcheck.py), so
 * this is still the binary as the specification.
 */
static int ScriptTokens(int *passed)
{
    /* The prefix is in-tree, so the image has a fixed path relative to the
     * repository root -- which is where `make selftest` runs from. */
    if (am2_load_image(".wine/drive_c/GOG Games/Army Men II/ArmyMen2.exe")
        != 0) {
        printf("\n  script tokens: SKIPPED (no image)\n");
        return 1;
    }

    int pass = 0, fail = 0, keywords = 0;
    for (uint32_t i = 0; i < sizeof am2_script_vectors /
                             sizeof am2_script_vectors[0]; i++) {
        const AM2_ScriptVec *v = &am2_script_vectors[i];
        int32_t got = ScriptLookupToken(v->word);
        if (v->id >= 0)
            keywords++;
        if (got != v->id) {
            if (fail < 10)
                printf("  FAIL ScriptLookupToken(\"%s\") -> %d, want %d\n",
                       v->word, got, v->id);
            fail++;
        } else {
            pass++;
        }
    }
    printf("  %d script words (%d keywords): %d pass, %d fail\n",
           pass + fail, keywords, pass, fail);
    *passed += pass;
    return fail;
}

/* The reconstruction sources end with an install function that registers each
 * detour. Nothing installs anything here -- there is no game in the process --
 * so one stub satisfies the link and is never called. */
extern "C" int patch_replace(uint32_t, const void *, const char *, int32_t)
{
    return 0;
}
