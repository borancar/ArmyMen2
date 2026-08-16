/* CheckSaveTag -- reconstructed from ArmyMen2.exe 0x004235D0.
 *
 * Verifies the next four bytes of a savegame stream against an expected section
 * tag. Reconstructed from disassembly; original at 0x004235D0, 66 bytes.
 *
 * The one subtlety worth preserving: the original passes `&fp` as the fread
 * destination, so the four bytes read land in the function's own first argument
 * slot, on top of the FILE pointer. If fread comes up short that slot still
 * holds the pointer value, and the comparison is made against *that*, not
 * against zero. Seeding `tag` from `fp` keeps the short-read path bit-faithful
 * rather than merely usually-correct.
 *
 * Known section tags, from the 15 call sites (see docs/savetags.tsv):
 *   0x06660002 script   0x06660007 item      0x06660666 gameproc
 *   0x06660003 event    0x06660008 objscript 0x06660668 unit
 *   0x06660004 event    0x06660009 map       0x01326413 audio
 *   0x06660005 pad      0x06660010 air       0x00003E88 event
 */

#include "savetag.h"
#include "../inject/orig.h"
#include "../inject/patch.h"

#include <stdint.h>

static const char kSaveReadError[] =
    "Error reading save file, source file: %s  line: %d\n";

int32_t __cdecl CheckSaveTag(am2_FILE *fp, uint32_t expected,
                             const char *file, int32_t line)
{
    uint32_t tag = (uint32_t)(uintptr_t)fp;

    orig_fread(&tag, 4, 1, fp);

    if (tag == expected)
        return 1;

    orig_log(kSaveReadError, file, line);
    return 0;
}

int savetag_install(void)
{
    return patch_replace(ADDR_CHECK_SAVE_TAG, CheckSaveTag, "CheckSaveTag", 4);
}
