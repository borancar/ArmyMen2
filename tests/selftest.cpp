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
#include "../src/game/place.h"
#include "../src/game/dirty.h"
#include "../src/game/image.h"
#include "../src/game/crt.h"

#include "loadimage.h"
#include "vectors.h"
#include "scriptvec.h"
#include "placevec.h"
#include "fireposevec.h"
#include "dirtyvec.h"

static uint8_t g_scratch[AM2_SCRATCH_LEN];

/* The scratch as it stood when the reconstruction was called, and which bytes
 * the ORIGINAL changed. Recording only the changed offsets, and comparing only
 * those, cannot see a write the original never made -- so a reconstruction
 * that scribbles somewhere extra passed silently. Found by mutation:
 * ListUnlink writes through `head` only when the node is first, and making it
 * write unconditionally left all 6708 vectors green. */
static uint8_t g_before[AM2_SCRATCH_CMP];
static uint8_t g_touched[AM2_SCRATCH_CMP];

/* Every one of these is cdecl, so a single six-argument invoker serves them
 * all: the caller cleans up, so passing more arguments than the callee reads
 * is harmless. */
typedef uint32_t (__cdecl *am2_any_fn)(uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, uint32_t);

static void FillScratch(uint32_t salt)
{
    for (uint32_t i = 0; i < sizeof g_scratch; i++)
        /* Must match scratch_pattern() in tools/vectors.py exactly, both
         * salts and all.
         *
         * `i >> 11` exists because without it every PTR_STRIDE region held
         * the same bytes and no copy was observable. `salt * 37` exists
         * because without it every VECTOR held the same bytes: a function
         * whose only variation is behind a pointer was called with identical
         * memory 96 times, and 7,353 recorded vectors were 5,355 distinct.
         * The salt travels as one uint32 and the buffer is recomputed from
         * it, so the bytes never have to be stored. */
        g_scratch[i] = (uint8_t)(((i * 7 + 13) ^ (i >> 11) ^ (salt * 37))
                                 & 0xFF);
}

static int ScriptTokens(int *passed);
static int PlaceLines(int *passed);
static int DirtyList(int *passed);
static int ScriptLines(int *passed);
static int ScriptSpine(int *passed);
static int ScriptVariables(int *passed);

/* gameproc.cpp's FreeSpriteRegistry call is a real symbol now that the seam
 * is closed, and the function lives in win32/sprite.cpp, which this harness
 * cannot link: it reaches DirectDraw. Nothing under test calls it -- the
 * teardown is not a pure function and has no vectors -- so a stub satisfies
 * the linker and nothing else. Fourth stub here for the same reason; see
 * LoadSpriteFile above. */
/* NOT extern "C": sprite.h and gameproc.cpp both declare it as ordinary
 * C++, so the stub has to carry the same mangled name. The stubs below
 * are extern "C" because their declarations are. */
void __cdecl FreeSpriteRegistry(void) { }

/* item.cpp's SetObjContext calls this now that its seam is closed, and it
 * lives in win32/widget.cpp, which this harness cannot link. Fifth stub, and
 * extern "C" this time because widget.h declares it inside such a block --
 * the linkage has to match the declaration, not the file it is stubbed in. */
extern "C" void __cdecl SetPointerMode(int32_t) { }

/* misc.cpp's LoadMask calls this now that its seam is closed, and it lives in
 * win32/surface.cpp, which this harness cannot link. Sixth stub, extern "C"
 * because surface.h declares it inside such a block. Nothing under test
 * reaches it: LoadMask's whole loose arm needs a chdir and a directory of
 * `.msk` files. */
extern "C" void *__cdecl LoadDibFlipped(const char *, void *, uint16_t *)
{
    return 0;
}

/* air.cpp's FindEnemyNear calls this now that its seam is closed, and it lives
 * in win32/mapdraw.cpp, which this harness cannot link: it clips with
 * IntersectRect. Seventh stub, and NOT extern "C" -- air.cpp forward-declares
 * it as ordinary C++, the same reason FreeSpriteRegistry above is not.
 * FindEnemyNear has no vectors: it reads the map descriptor. */
void *__cdecl ObjectsInRect(const AM2_Rect *, const void *,
                            int32_t (__cdecl *)(void *))
{
    return 0;
}

/* item.cpp's RowUnregisterAll calls this now that its seam is closed, and it
 * lives in win32/mapdraw.cpp for the same reason the two around it do -- it
 * clips with IntersectRect. NOT extern "C": item.cpp forward-declares it as
 * ordinary C++. It has no vectors; it walks a .bss list. */
void __cdecl DirtyCollect(const AM2_Rect *)
{
}

/* army.cpp's ExitOneFromVehicle calls this now that its seam is closed, and it
 * lives in win32/mapdraw.cpp for the same reason ObjectsInRect does -- it
 * clips with IntersectRect. Eighth stub, and extern "C" because army.cpp
 * forward-declares it that way. It has no vectors: it reads the map bounds,
 * the row shift and the cell weights. */
extern "C" int32_t __cdecl BoatExitPoint(void *, uint32_t *)
{
    return 0;
}

/* item.cpp's StepType6 calls this now that type 6 is reconstructed, and it
 * lives in win32/mapdraw.cpp. Ninth stub, and NOT extern "C" -- mapdraw.h's
 * block closes at line 129 and ShakeAt is declared at 158, outside it. The
 * linkage matches the header, not the file it is stubbed in, which is the same
 * rule ObjectsInRect above follows for the opposite reason. StepType6 has no
 * vectors: it reads the game clock and the tile layers. */
void __cdecl ShakeAt(const AM2_Point *, int32_t)
{
}

/* msgslot.cpp's SendChatMsg and armymsg.cpp's ArmyMessageFlush call this now
 * that its seam is closed, and it lives in win32/dplay.cpp -- which cannot
 * join this link at all: it names DirectPlay. Tenth stub, and `extern "C"`
 * because dplay.h declares it inside that block and both callers
 * forward-declare it the same way. It has no vectors and could not: it reads
 * the comm object, the player records and four .bss message lists. */
extern "C" int32_t __cdecl SendGameMsg(void *, int32_t, int32_t)
{
    return 0;
}

/* region.cpp's TrooperFire calls this now that 0x00449FD0 is ours, and it
 * lives in commmsg.cpp -- which this link does not carry, for the reason
 * msgslot.cpp's own comment gives about SendChatMsg. Eleventh stub, and
 * `extern "C"` because commmsg.h's block covers it -- lines 16 to 122, and
 * this declaration is inside. TrooperFire has no vectors and could not: it
 * reads the comm object, the game clock and the object registry. */
extern "C" void __cdecl TrooperFireSend(void *, void *)
{
}

static int FirePoses(int *passed);

int main(void)
{
    int32_t pass = 0, fail = 0;

    for (uint32_t v = 0; v < sizeof kVectors / sizeof kVectors[0]; v++) {
        const AM2_Vector *t = &kVectors[v];
        uint32_t a[6];

        FillScratch(t->salt);
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

        for (uint32_t o = 0; o < AM2_SCRATCH_CMP; o++)
            g_before[o] = g_scratch[o];

        uint32_t got = ((am2_any_fn)t->fn)(a[0], a[1], a[2], a[3], a[4], a[5]);

        uint32_t want_eax = t->eax_is_ptr
                          ? (uint32_t)(uintptr_t)(g_scratch + t->eax)
                          : t->eax;
        /* A byte-returning prototype carries its value in `al` only, and the
         * original leaves the rest of eax holding whatever it last had --
         * Log2Mask leaves its own argument there. Comparing 32 bits would test
         * the register allocator rather than the function. */
        if (t->byte_ret) {
            got &= 0xFFu;
            want_eax &= 0xFFu;
        }
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

        /* And every byte the original did NOT touch must be untouched here.
         * Without this the harness only ever asked whether the expected writes
         * happened, never whether anything else did. */
        if (!bad) {
            for (uint32_t o = 0; o < AM2_SCRATCH_CMP; o++)
                g_touched[o] = 0;
            for (int32_t w = 0; w < t->nwrites; w++) {
                uint32_t off = t->writes[w * 2];
                if (off < AM2_SCRATCH_CMP)
                    g_touched[off] = 1;
            }
            for (int32_t w = 0; w < t->nwptr; w++) {
                uint32_t at = t->wptr[w * 2];
                for (uint32_t b = 0; b < 4; b++)
                    if (at + b < AM2_SCRATCH_CMP)
                        g_touched[at + b] = 1;
            }
            /* A slot that HELD a pointer before the call is not comparable
             * byte for byte in either direction. The emulator's pointers are
             * SCRATCH-based and the replay's are host addresses, so clearing
             * one is a change here and was not a change there -- the write is
             * correct and simply never got recorded. That is what a fixup
             * offset is, so exclude those four bytes. */
            for (int32_t w = 0; w < t->nfixups; w++) {
                uint32_t at = t->fixups[w * 2];
                for (uint32_t b = 0; b < 4; b++)
                    if (at + b < AM2_SCRATCH_CMP)
                        g_touched[at + b] = 1;
            }
            for (uint32_t o = 0; o < AM2_SCRATCH_CMP && !bad; o++)
                if (!g_touched[o] && g_scratch[o] != g_before[o])
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
    fail += PlaceLines(&pass);
    fail += FirePoses(&pass);
    fail += DirtyList(&pass);
    fail += ScriptLines(&pass);
    fail += ScriptSpine(&pass);
    fail += ScriptVariables(&pass);
    return fail ? 1 : 0;
}

/* The `variable` statement, over every such line the scripts contain plus the
 * malformed shapes they never take -- which is most of the value here, since a
 * handler with four exits and three of them errors gets one exit exercised by
 * a shipped script.
 *
 * `variable x 1.5` is absent for the same reason kind 4 is absent above: its
 * error path renders the Float through ScriptTokenText and "%6.2f" does not
 * emulate.
 */
static int ScriptVariables(int *passed)
{
    am2_crt_use_host();

    int pass = 0, fail = 0;
    for (uint32_t i = 0; i < sizeof am2_script_vars /
                             sizeof am2_script_vars[0]; i++) {
        const AM2_ScriptVarVec *v = &am2_script_vars[i];
        AM2_ScriptCtx ctx = { 0, 0, 0 };
        int32_t at = 0;

        /* Each case starts from an empty name table, so the entry it leaves
         * behind is the whole of what it added. */
        am2_script_reset_names();

        /* A `pre` line runs first against the same table -- the only route to
         * the duplicate-name path, which nothing in the corpus takes. */
        if (v->pre) {
            AM2_ScriptCtx p = { 0, 0, 0 };
            int32_t pat = 0;
            ScriptNextToken(v->pre, &p, 5);
            ScriptVariable(&p, &pat);
            ScriptResetTokens(&p);
        }

        ScriptNextToken(v->line, &ctx, 5);
        int32_t rc = ScriptVariable(&ctx, &at);

        int32_t names = am2_script_name_count();
        const AM2_ScriptName *n0 = names ? am2_script_name(0) : 0;
        int32_t kind1 = ctx.count > 1 ? ctx.tokens[1].kind : -1;

        int bad = rc != v->rc || at != v->at || ctx.count != v->count ||
                  names != v->names || kind1 != v->kind0;
        if (!bad && names) {
            bad = strcmp(n0->name, v->name0) || n0->type != v->type0 ||
                  n0->value != v->value0;
        }

        if (bad) {
            if (fail < 6)
                printf("  FAIL variable %-28s rc %d/%d at %d/%d "
                       "names %d/%d kind1 %d/%d\n",
                       v->line, (int)rc, (int)v->rc, (int)at, (int)v->at,
                       (int)names, (int)v->names, (int)kind1, (int)v->kind0);
            fail++;
        } else {
            pass++;
        }
        ScriptResetTokens(&ctx);
    }
    printf("  %d variable cases: %d pass, %d fail\n", pass + fail, pass, fail);
    *passed += pass;
    return fail;
}

/* The layer above the tokeniser: ScriptTokenName, ScriptTokenText and
 * ScriptIsStatementStart, all of which are table walks over the image.
 *
 * ScriptTokenText's kind-4 arm is absent from the corpus: "%6.2f" formats
 * through the MSVC CRT and does not emulate, so there is no recorded answer to
 * compare against. That one arm is verified by reading.
 */
static int ScriptSpine(int *passed)
{
    int pass = 0, fail = 0;
    char out[0x100];

    for (uint32_t i = 0; i < sizeof am2_script_names /
                             sizeof am2_script_names[0]; i++) {
        const AM2_ScriptVec *v = &am2_script_names[i];
        const char *got = ScriptTokenName(v->id);
        int bad = v->word ? (!got || strcmp(got, v->word)) : (got != 0);
        if (bad) {
            if (fail < 4)
                printf("  FAIL ScriptTokenName(%d) -> %s, want %s\n",
                       v->id, got ? got : "(null)",
                       v->word ? v->word : "(null)");
            fail++;
        } else {
            pass++;
        }
    }

    for (uint32_t i = 0; i < sizeof am2_script_stmt /
                             sizeof am2_script_stmt[0]; i++) {
        AM2_ScriptTok tok = { AM2_TOKEN_RESERVED, 0, 0 };
        AM2_ScriptCtx ctx = { 0, 1, &tok };
        int32_t at = 0;
        tok.value = (void *)(uintptr_t)i;
        int32_t got = ScriptIsStatementStart(&ctx, &at);
        if (got != am2_script_stmt[i].id) {
            if (fail < 8)
                printf("  FAIL ScriptIsStatementStart(%u) -> %d, want %d\n",
                       i, (int)got, (int)am2_script_stmt[i].id);
            fail++;
        } else {
            pass++;
        }
    }

    for (uint32_t i = 0; i < sizeof am2_script_text /
                             sizeof am2_script_text[0]; i++) {
        const AM2_ScriptTextVec *v = &am2_script_text[i];
        AM2_ScriptTok tok;
        tok.kind = v->kind;
        tok.line = 7;
        tok.value = v->text ? (void *)v->text
                            : (void *)(uintptr_t)v->value;
        memset(out, 0, sizeof out);
        ScriptTokenText(&tok, out);
        if (strcmp(out, v->want)) {
            if (fail < 8)
                printf("  FAIL ScriptTokenText(kind %d) -> \"%s\", want \"%s\"\n",
                       (int)v->kind, out, v->want);
            fail++;
        } else {
            pass++;
        }
    }

    printf("  %d spine cases: %d pass, %d fail\n", pass + fail, pass, fail);
    *passed += pass;
    return fail;
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

/* The `place` line parser, against every line the thirty-six shipped
 * placement files contain.
 *
 * The same argument as the script tokeniser above, one layer up: this
 * function's whole job is turning six text columns into a record, and a
 * random argument says nothing about it. The expected records come from the
 * ORIGINAL under Unicorn (tools/placecheck.py).
 *
 * It runs offline because everything it reaches is either ours or DATA. The
 * unit-type names, the separators and the "-" placeholder are all in the
 * mapped image; DefParseNumber and AddPlacement are reconstructed; and strtok
 * comes through crt.h, which points at the host's when there is no game. That
 * last one is why this is a test rather than a note saying it could not be.
 */
/* NOT a whole-record memcmp, and the reason is the one CLAUDE.md already
 * records about the widget dump: an uninitialised field cannot be part of an
 * exact oracle. Three bytes of padding after `facing` are never written, and
 * `name` is filled by a strcpy that stops at the NUL -- so both hold whatever
 * was on the stack. Unicorn's stack is zero and Wine's is not, which is
 * exactly how this was found: every one of the 1,264 rows failed with the
 * return code and every defined byte already agreeing.
 *
 * So the comparison is the defined span: where, facing, type, group, and the
 * name as a string. */
static int PlaceRecordMatches(const AM2_Placement *got,
                              const unsigned char *want)
{
    const AM2_Placement *w = (const AM2_Placement *)want;

    return got->where == w->where
        && got->facing == w->facing
        && got->type == w->type
        && got->group == w->group
        && strcmp(got->name, w->name) == 0;
}

/* SelectFirePose against the cases tools/firepose.py recorded from the
 * ORIGINAL. That tool checks its own Python model against the original; this
 * checks the C against the same recorded answers, which is the half a model
 * cannot cover on its own -- both were written from one reading, and only the
 * emulator's run is ground truth.
 *
 * The object is REBUILT from the six inputs rather than stored, so the header
 * cannot drift from what the function is actually handed. No image is needed:
 * everything it reads is in the buffers below.
 */
static int FirePoses(int *passed)
{
    static unsigned char obj[0x600];
    static unsigned char row[0x60];
    static unsigned char wpn[0x100];
    static unsigned char kindrec[0x10];
    static unsigned char sight[0x40];
    int pass = 0, fail = 0;

    for (uint32_t i = 0; i < sizeof am2_firepose_vectors /
                             sizeof am2_firepose_vectors[0]; i++) {
        const AM2_FirePoseVector *v = &am2_firepose_vectors[i];
        int32_t rc;
        int32_t got;
        int     bad = 0;

        memset(obj, 0, sizeof obj);
        memset(row, 0, sizeof row);
        memset(wpn, 0, sizeof wpn);
        memset(sight, 0, sizeof sight);

        *(int32_t *)(obj + 0x00)  = v->objtype;
        *(int32_t *)(obj + 0x08)  = v->flags;
        *(int32_t *)(obj + 0x44)  = v->speed;
        *(unsigned char **)(obj + 0x74) = row;
        *(int32_t *)(obj + 0x538) = v->pose;
        *(int32_t *)(obj + 0x544) = v->soldier;
        *(int32_t *)(obj + 0x5A4) = v->f5a4;
        *(int16_t *)(row + 0x4C)  = (int16_t)v->frame;
        *(unsigned char **)(wpn + 0xC0) = kindrec;
        *(int32_t *)kindrec = v->kind;
        *(int32_t *)(sight + 0x08) = 0x7BADF00D;
        *(int32_t *)(sight + 0x10) = v->seen;

        rc  = SelectFirePose(v->nullobj ? (void *)0 : obj, wpn, sight,
                             v->ready);
        got = *(const int32_t *)(sight + 0x08);

        if (rc != v->rc)
            bad = 1;
        else if (v->wrote && got != v->state)
            bad = 1;
        else if (!v->wrote && got != (int32_t)0x7BADF00D)
            bad = 1;

        if (bad) {
            if (fail < 10)
                printf("  FAIL SelectFirePose kind=%d frame=%d pose=%d "
                       "sk=%d spd=%d seen=%d rdy=%d -> rc=%d pose=%d, "
                       "want rc=%d pose=%d\n",
                       (int)v->kind, (int)v->frame, (int)v->pose,
                       (int)v->soldier, (int)v->speed, (int)v->seen,
                       (int)v->ready, (int)rc, (int)got,
                       (int)v->rc, v->wrote ? (int)v->state : -1);
            fail++;
        } else {
            pass++;
        }
    }

    printf("  %d fire poses: %d pass, %d fail\n", pass + fail, pass, fail);
    *passed += pass;
    return fail;
}

#define kPlacements     (*(AM2_Placement **)AM2_IMAGE(0x00654C7Cu))
#define kPlacementCount (*(int32_t *)AM2_IMAGE(0x00654C80u))

static int PlaceLines(int *passed)
{
    if (am2_load_image(".wine/drive_c/GOG Games/Army Men II/ArmyMen2.exe")
        != 0) {
        printf("\n  place lines: SKIPPED (no image)\n");
        return 1;
    }

    int pass = 0, fail = 0;
    for (uint32_t i = 0; i < sizeof am2_place_vectors /
                             sizeof am2_place_vectors[0]; i++) {
        const AM2_PlaceVector *v = &am2_place_vectors[i];
        char line[256];
        int  bad = 0;

        /* The parser writes NULs into the line, so each run needs its own
         * copy -- and strtok's cursor points into it, which is the other
         * reason a shared buffer would not do. */
        strncpy(line, v->line, sizeof line - 1);
        line[sizeof line - 1] = '\0';

        FreePlacements();
        int32_t rc = ParsePlaceLine(0x63, line);
        if (rc != v->rc)
            bad = 1;
        else if (v->added != (kPlacementCount != 0))
            bad = 1;
        else if (v->added && !PlaceRecordMatches(kPlacements, v->rec))
            bad = 1;

        if (bad) {
            if (fail < 10)
                printf("  FAIL ParsePlaceLine(\"%s\") -> %d, want %d\n",
                       v->line, (int)rc, (int)v->rc);
            fail++;
        } else {
            pass++;
        }
    }
    FreePlacements();
    printf("  %d place lines: %d pass, %d fail\n", pass + fail, pass, fail);
    *passed += pass;
    return fail;
}

/* The dirty-rectangle list, against the ORIGINAL run over the same sequences.
 *
 * A THIRD KIND OF ORACLE, and the reason for it is that the other two cannot
 * reach this. AddDirtyRect's answer is not a return value, it is the array
 * afterwards -- so the vector is a sequence of calls and the recording is the
 * whole 10,004-byte span, tail index included. Compared byte for byte with no
 * masking, because unlike the place records there is nothing uninitialised in
 * it: the region is zeroed before the sequence starts on both sides.
 *
 * The rectangles are generated from the step index rather than stored, which
 * is the same trade tools/vectors.py makes with its salt -- the formula below
 * has to match rects() in tools/dirtycheck.py or every row fails at once.
 *
 * THE STATE IS SEEDED, NOT ZEROED, and that is not tidiness. With zeros,
 * deleting one of ResetDirtyList's three stores passed all eight sequences,
 * because clearing something already zero cannot be observed. ADDR_FULL_REDRAW
 * is seeded with a value neither function writes for the same reason.
 */
static int DirtyList(int *passed)
{
    if (am2_load_image(".wine/drive_c/GOG Games/Army Men II/ArmyMen2.exe")
        != 0) {
        printf("\n  dirty list: SKIPPED (no image)\n");
        return 1;
    }

    unsigned char *region = (unsigned char *)AM2_IMAGE(ADDR_DIRTY_TAIL);
    int32_t       *full   = (int32_t *)AM2_IMAGE(ADDR_FULL_REDRAW);
    int            pass = 0, fail = 0;

    for (uint32_t v = 0; v < sizeof am2_dirty_vectors /
                             sizeof am2_dirty_vectors[0]; v++) {
        const AM2_DirtyVector *d = &am2_dirty_vectors[v];

        for (int32_t i = 0; i < AM2_DIRTY_REGION_LEN; i++)
            region[i] = (unsigned char)((i * 7 + 13) & 0xFF);
        *full = 0x5A5A5A5A;
        ResetDirtyList();

        for (int32_t i = 0; i < d->adds; i++) {
            int32_t l = (i * 37) % 640 - 64;
            int32_t t = (i * 53) % 480 - 32;

            AddDirtyRect(l, t, l + i % 7, t + i % 5);
        }

        if (*full != d->full
            || memcmp(region, d->region, AM2_DIRTY_REGION_LEN) != 0) {
            if (fail < 10)
                printf("  FAIL dirty list after %d add(s)\n", (int)d->adds);
            fail++;
        } else {
            pass++;
        }
    }

    memset(region, 0, AM2_DIRTY_REGION_LEN);
    *full = 0;
    printf("  %d dirty sequences: %d pass, %d fail\n", pass + fail, pass, fail);
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

/* script.cpp reaches PreloadSprite, which lives in the win32 half and pulls in
 * DirectDraw. Nothing here exercises sprite preloading -- the script vectors
 * cover the tokeniser and the statement handlers -- so a stub keeps the
 * self-test free of Win32, which is the whole point of it running without the
 * game. If a vector ever does reach this, it will return 0 rather than load
 * anything, and that is a wrong answer rather than a link error; worth
 * knowing before adding sprite coverage here. */
struct AM2_Sprite;
extern "C" AM2_Sprite *__cdecl PreloadSprite(int32_t, int32_t, int32_t,
                                             int32_t, int32_t)
{
    return 0;
}

/* And its by-key twin, which objtype.cpp's EnsureSpriteAaiRecord reaches on
 * the path where the record already exists. Same reasoning as PreloadSprite
 * above, and the same caveat: a vector that reached this would get 0 rather
 * than a sprite, which is a wrong answer and not a link error. */
extern "C" AM2_Sprite *__cdecl PreloadSpriteByKey(uint32_t, int32_t, int32_t)
{
    return 0;
}

/* The two speech entry points, which item.cpp's pickup pair reaches. They
 * live in win32/audio.cpp with the rest of the sound code, so the same
 * reasoning as PreloadSprite applies -- and here the caveat is weaker, since
 * both return void and a vector that reached one would simply say nothing. */
extern "C" void __cdecl SpeakLine(int32_t, int32_t)
{
}

extern "C" void __cdecl SpeakItemPickupLine(int32_t, int32_t)
{
}

/* gameproc.cpp's Teardown40A4B0 now calls BuildRemapTables by name, and that
 * lives in win32/palette.cpp, which the selftest cannot link -- it names
 * DirectDraw types. A stub for the same reason as the two above: nothing here
 * has a palette, and no vector reaches a function that takes no arguments and
 * only writes globals. */
extern "C" void __cdecl BuildRemapTables(void)
{
}

/* armymsg.cpp's TellOneSlot now calls AppendTroopState by name, and that lives
 * in commmsg.cpp which is not in SELFTEST_SRC. A stub rather than pulling the
 * whole comm module in: nothing here has a message buffer to append to, and a
 * vector that reached it would leave the message unchanged. */
extern "C" void __cdecl AppendTroopState(void *, void *)
{
}

/* event.cpp's two bitmap triggers reach FreeBitmap, which is in win32/sprite.cpp
 * with the rest of the sprite code. Same reasoning as PreloadSprite above.
 *
 * This one arrived by CLOSING a seam rather than by adding a function:
 * event.cpp called it through ADDR_FREE_BITMAP until 0x00446410 was
 * reconstructed, at which point checkseams asked for the direct call and the
 * direct call asked for the module. A stub is the third option, and the right
 * one -- moving EvtShowBitmap out of event.cpp to satisfy a linker would be
 * the tail wagging the dog. */
extern "C" void __cdecl FreeBitmap(void **)
{
}

/* event.cpp's two sound triggers reach PlayDynamicSound, which is in the win32
 * half with the rest of the sound code. Same reasoning as PreloadSprite above:
 * nothing here plays anything, and pulling win32/audio.cpp in would bring
 * DirectSound with it. */
extern "C" void __cdecl PlayDynamicSound(const char *, int32_t, int32_t,
                                         int32_t, int32_t, int32_t, int32_t,
                                         uint32_t)
{
}

/* item.cpp's DamageRoach plays a wave when the roach dies, and PlaySoundAt is
 * in win32/audio.cpp. Same reasoning again, and the same route in: this
 * arrived by CLOSING a seam, not by adding a call.
 *
 * air.cpp reaches the same function and needs no stub, which is worth knowing
 * before assuming a flat module can always call it -- air.cpp is not in
 * SELFTEST_SRC, so the link never sees it. The rule is not "flat modules may
 * not call into win32"; it is "modules IN THIS LINK may not", and the two sets
 * are different. */
extern "C" void __cdecl PlaySoundAt(int32_t, int32_t, int32_t, int32_t,
                                    int32_t)
{
}

/* item.cpp's RoachBite queries the map, and AllObjectsInRect is in
 * win32/mapdraw.cpp. Declared C++ rather than extern "C" -- mapdraw.h declares
 * it outside any linkage block -- so the stub must be too, which is the same
 * trap the widget stub above records. Answering null makes the bite hit
 * nothing, which is all a linker stub can honestly do. */
void *__cdecl AllObjectsInRect(const AM2_Rect *, const void *)
{
    return 0;
}

/* air.cpp's AirPassesDraw reaches DrawSprite, in win32/sprite.cpp. Same
 * reasoning as the stubs around it, and the same route in: this arrived by
 * closing a seam. extern "C" to match the forward declaration air.cpp makes,
 * which is what the widget stub above records as the trap. */
extern "C" void __cdecl DrawSprite(void *, int32_t, int32_t, int32_t)
{
}

/* map.cpp's FreeMapLayers reaches FreeMapSurfaces, in win32/surface.cpp. Same
 * reasoning as the stubs around it, and the same route in: this arrived by
 * closing a seam rather than by adding a call. */
extern "C" void __cdecl FreeMapSurfaces(void)
{
}

/* gameproc.cpp's SaveGame reaches SaveAudioSection, in win32/audio.cpp. Same
 * reasoning as the stubs around it, and the same route in: closing the
 * orig_save_game seam in event.cpp pulled SaveGame into the link. */
int32_t __cdecl SaveAudioSection(am2_FILE *)
{
    return 1;
}

/* air.cpp's sprite teardown reaches ReleaseSprite, in win32/sprite.cpp. Same
 * reasoning as the stubs above, and air.cpp arrived in SELFTEST_SRC for the
 * same reason map.cpp did: item.cpp gained a call into it, and adding the flat
 * module it lives in beats stubbing a flat function. Its own flat dependency,
 * trig.cpp, came with it; only this one crosses into win32. */
extern "C" void __cdecl ReleaseSprite(AM2_Sprite *)
{
}

/* anim.cpp's two sprite loaders, both in win32/sprite.cpp. Same reasoning as
 * the stubs above and the same route in: item.cpp's StepType8 gained calls to
 * RowAnimFinished and RowFaceSprite, which live in anim.cpp, so anim.cpp
 * joined SELFTEST_SRC -- and it brings these two with it.
 *
 * Adding the module they live in was tried FIRST and is not an option here:
 * win32/sprite.cpp pulls in ddraw and the whole point of this harness is that
 * no part of the game runs. Two stubs and a note is the honest shape. */
extern "C" int32_t __cdecl LoadSpriteFile(const char *, void *, const void *,
                                          int32_t, uint32_t)
{
    return 0;
}

extern "C" void __cdecl BuildVehicleMask(int32_t)
{
}

extern "C" void __cdecl BuildRoachMask(void)
{
}

/* misc.cpp's MissionNetworked shows the multiplayer end screen, and
 * ShowMpResult is in win32/frame.cpp. Same reasoning as the stubs above, and
 * the same route in -- a seam closed rather than a call added. Its signature
 * names no Win32 type, which is what lets misc.cpp declare it at all; only
 * the definition is on the other side of the split. */
extern "C" void __cdecl ShowMpResult(int32_t)
{
}

/* event.cpp's two showbitmap handlers reached LoadBitmap through the image
 * until it was reconstructed; now they call it, and it is in win32/sprite.cpp
 * with the rest of the sprite code. Stubbed for the same reason as the two
 * above -- adding that module would drag DirectDraw into a test that runs
 * without a display.
 *
 * It returns void * here and AM2_Sprite * there, which is what event.cpp
 * declares too: the struct cannot be named on the flat side. C linkage makes
 * the mismatch invisible to the linker, so it is written down in both places
 * rather than left to be discovered. */
/* loadimage.h pulls in windows.h, whose LoadBitmap macro would rename this
   stub to LoadBitmapA and leave the real symbol undefined. Same wrinkle
   src/inject/win32.h undoes for DrawText and now for this. */
#undef LoadBitmap

extern "C" void *__cdecl LoadBitmap(const char *, int32_t)
{
    return (void *)0;
}

/* gameproc.cpp joined SELFTEST_SRC when item.cpp gained a call to SaveOneItem,
 * and it brought pad.cpp with it -- both flat, both added rather than stubbed,
 * for the reason map.cpp and air.cpp were. These two are the only things it
 * reaches that are NOT flat: StateLeave clears the primary surface and
 * LoadAudioSection is in win32/audio.cpp with the rest of the sound code.
 *
 * LoadAudioSection is deliberately NOT `extern "C"`: audio.h's block ends
 * before it, so gameproc.cpp declares it with C++ linkage and this must match.
 * StateLeave is the opposite case. Getting either wrong fails at link time
 * with a mangled name, which is how the distinction was checked rather than
 * assumed. */
extern "C" void __cdecl StateLeave(void)
{
}

int32_t __cdecl LoadAudioSection(am2_FILE *)
{
    return 1;
}
