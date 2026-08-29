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
 *   0x06660005 pad      0x06660010 air
 *
 * 0x00003E88 is in docs/savetags.tsv and used to be listed here as a twelfth
 * tag. It is not a tag: it is the LENGTH of the 16008-byte block at
 * 0x0050C368, which SaveEventBlock writes with WriteSaveTag and LoadEventBlock
 * checks with this function. The two helpers are general enough that a length
 * travels exactly as a tag does, so from the loader's side the two are
 * indistinguishable -- only reading the saver settles it.
 */

#include "savetag.h"
#include "script.h"   /* AM2_ScriptName */
#include "../inject/orig.h"
#include "../inject/patch.h"

#include <stdint.h>
#include <string.h>

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

void __cdecl WriteSaveTag(am2_FILE *fp, uint32_t tag)
{
    orig_fwrite(&tag, 4, 1, fp);
}

/* SaveScriptName -- original 0x00428760, two callers, both inside one save
 * function and one immediately after the other.
 *
 * Write a script-name reference into a savegame. Either the record names
 * nothing, in which case the "no name" tag goes out alone; or it names a
 * script-name-table entry, and the tag, the length and the string follow.
 *
 * THE LENGTH WRITTEN IS strlen + 1, and the string is written with the same
 * count -- so the terminator IS in the file and the length includes it. The
 * original computes strlen with `repne scasb`, `not`, `dec`, giving the length
 * without the terminator, and then increments; both halves are needed and it
 * is easy to reproduce one and not the other.
 *
 * IT ALWAYS ANSWERS 1. Both exits set it, and neither caller looks. Kept
 * because the prototype is the original's, not because anything turns on it.
 *
 * The name is fetched TWICE from the table -- once to measure it and once to
 * write it -- with the index re-read from the record in between. Nothing can
 * change either in the meantime; it is the compiler running out of registers.
 * Written as one lookup, which is what it means.
 *
 * The write goes through the game's own fwrite for the same reason the tags go
 * through the game's own helper: the FILE * came from the game's CRT.
 */
int32_t __cdecl SaveScriptName(am2_FILE *fp, const void *rec)
{
    int32_t index = *(const int32_t *)((const uint8_t *)rec
                                       + SCRIPT_REF_OFF_NAME_INDEX);
    const char *name;
    uint32_t    n;

    if (index < 0) {
        WriteSaveTag(fp, AM2_SAVETAG_NO_NAME);
        return 1;
    }

    name = (*(const AM2_ScriptName *const *)(uintptr_t)ADDR_SCRIPT_NAMES)
               [index].name;
    n    = (uint32_t)strlen(name) + 1;

    WriteSaveTag(fp, AM2_SAVETAG_NAME);
    WriteSaveTag(fp, n);
    orig_fwrite(name, n, 1, fp);

    return 1;
}

int savetag_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_CHECK_SAVE_TAG, (const void *)CheckSaveTag,
                        "CheckSaveTag", 4);
    rc |= patch_replace(ADDR_SAVE_SCRIPT_NAME, (const void *)SaveScriptName,
                        "SaveScriptName", 2);
    rc |= patch_replace(ADDR_WRITE_SAVE_TAG, (const void *)WriteSaveTag,
                        "WriteSaveTag", 2);
    return rc;
}
