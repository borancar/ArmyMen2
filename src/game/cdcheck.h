#ifndef AM2_CDCHECK_H
#define AM2_CDCHECK_H

#include <stdint.h>
#include "../inject/orig.h"
#include "../inject/win32.h"
#include "winmain.h"

/* The copy protection, written back out.
 *
 * Army Men II checks for its CD in five places, and in this executable all five
 * checks are disabled -- the conditional branch past the "insert the CD" dialog
 * has been overwritten with an unconditional one, EB where a 75 has to have
 * been. One byte each, nothing moved, and the `test` left in front of it sets
 * flags that nothing reads. `tools/binpatches.py` finds them and
 * `docs/binarypatches.md` lists the byte to change to put each one back.
 *
 * So a reconstruction that simply transcribed what the bytes do would record
 * "there is no copy protection in this game", which is not true of the game --
 * only of this copy of it. The check the retail build performed is therefore
 * written out here rather than lost, behind a switch that is off.
 *
 * IT IS OFF ON PURPOSE. The port's whole method is that reconstructed code must
 * behave exactly like the code it replaces, verified against the original
 * binary running side by side (see `tools/ab.sh`). Building this in would break
 * that: our copy of a menu function would demand a CD where the original's does
 * not, and every A/B comparison involving it would diverge for a reason that
 * has nothing to do with whether the reconstruction is correct.
 *
 * Nor would turning it on give a working copy protection. One of the five call
 * sites is reconstructed and calls this -- StartSelectedGame, the local-game
 * path -- and the other four are still the original's code with their patched
 * bytes, so four of the five checks would keep letting everyone through
 * regardless. A switch that changes one check in five is worse than one that
 * changes none.
 *
 * All five sites pass the same three arguments, so the shape below is the whole
 * of what was removed. What differs between them is only what each one does
 * afterwards with its own menu state.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 0x10 is what every site pushes for uType. */
static_assert(MB_ICONHAND == 0x10, "MB_ICONHAND");

/* The strings are the game's own, not copies -- as with the DirectPlay CLSIDs
 * and the registry key, there is no reason to restate what the image carries. */
#define kCdRequiredText    ((const char *)(uintptr_t)ADDR_CD_REQUIRED_TEXT)
#define kCdRequiredCaption ((const char *)(uintptr_t)ADDR_CD_REQUIRED_CAPTION)

#ifdef AM2_COPY_PROTECTION

/* What each of the five call sites did before it was patched:
 *
 *     call FindGameCD
 *     test eax, eax
 *     jne  carry_on          <-- now EB, unconditionally taken
 *     push 0x10
 *     push aCopyProtection
 *     push aTheArmymen2Cd
 *     call GetActiveWindow
 *     push eax
 *     call MessageBoxA
 *     ... site-specific state, then return
 *   carry_on:
 *     ...
 *
 * Answers non-zero when it is safe to carry on. */
static inline int32_t RequireGameCD(void)
{
    if (FindGameCD())
        return 1;

    MessageBoxA(GetActiveWindow(), kCdRequiredText, kCdRequiredCaption,
                MB_ICONHAND);
    return 0;
}

#else

/* What the binary actually does: call it, ignore the answer, carry on. The
 * call itself is NOT patched out -- only the branch on its result -- so
 * FindGameCD still runs and still sets the globals it sets, and leaving it out
 * would be a behavioural difference rather than a faithful transcription. */
static inline int32_t RequireGameCD(void)
{
    FindGameCD();
    return 1;
}

#endif /* AM2_COPY_PROTECTION */

#ifdef __cplusplus
}
#endif

#endif /* AM2_CDCHECK_H */
