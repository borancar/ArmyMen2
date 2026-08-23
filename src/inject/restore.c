/* Undoing edits made to the shipping executable.
 *
 * This is not reconstruction and it is not a hook. The GOG build of ArmyMen2.exe
 * has had a handful of conditional branches overwritten with unconditional ones
 * -- one byte each, `74`/`75` becoming `EB`, same length so nothing moved -- and
 * this puts chosen ones back. `tools/binpatches.py` finds them all and
 * `docs/binarypatches.md` lists them with the evidence.
 *
 * Everything here is off unless asked for, because turning any of it on makes
 * this process behave differently from the executable it is derived from, and
 * that is exactly what `tools/ab.sh` exists to detect. An A/B run with a
 * restore enabled on one side will diverge for a reason that has nothing to do
 * with whether the reconstruction is correct.
 */

#include "patch.h"
#include "hooklog.h"
#include "restore.h"

#include <stdlib.h>

/* The MULTIPLAYER entry on the title screen.
 *
 * 0x0044D110 builds the title menu, one button at a time, each with the same
 * shape: allocate 0x78 bytes, compare the result against zero, and skip the
 * button when the allocation failed. Eight of them read `74 55` -- `je`. The
 * ninth, the one that would create MULTIPLAYER with handler 0x0044D380 and the
 * `03_101_0*_multiplay.bmp` artwork, reads `EB 55`. Same displacement, one
 * different byte, and the effect is that the button is never built.
 *
 * That is why the title screen has a gap between SINGLE PLAYER and OPTIONS.
 *
 * Putting it back makes the whole DirectPlay path reachable, which matters
 * beyond the feature: the reconstructed comm functions -- CommEnumConnections,
 * CommEnumSessions, CommSend, StartSelectedGame, StartMultiplayerGame -- have
 * no other way to be exercised, and until this existed they were verified by
 * reading and nothing else.
 */
#define MULTIPLAYER_JUMP   0x0044D8FEu
#define MULTIPLAYER_NOW    0xEB          /* jmp -- always skip the button */
#define MULTIPLAYER_RETAIL 0x74          /* je  -- skip only if the alloc failed */

static int enabled(const char *name)
{
    const char *opt = getenv(name);
    return opt && *opt == '1';
}

int restore_multiplayer(void)
{
    return enabled("AM2_MULTIPLAYER");
}

void restore_install(void)
{
    if (!enabled("AM2_MULTIPLAYER"))
        return;

    if (patch_byte(MULTIPLAYER_JUMP, MULTIPLAYER_NOW, MULTIPLAYER_RETAIL,
                   "MULTIPLAYER button") == 0)
        hooklog("restore: multiplayer enabled -- the title screen gains its "
                "missing entry");
}
