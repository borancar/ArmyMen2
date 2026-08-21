/* gameproc.cpp -- the game-process state block and its savegame section.
 *
 * Named by the image: the loader hands "C:\\ArmyMen2\\source\\gameproc.cpp" to
 * CheckSaveTag. Only the save pair is reconstructed.
 */
#ifndef AM2_GAMEPROC_H
#define AM2_GAMEPROC_H

#include <stdint.h>
#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

/* 0x00426850 and 0x00426880. 1080 bytes from 0x00511A68, and no section tag of
 * its own -- SaveGame writes 0x06660666 immediately before calling the saver,
 * so what this pair writes and checks is the block LENGTH. That is the third
 * place the tag helpers carry something that is not a tag, after the event
 * block's length and the pad count.
 *
 * FIVE FIELDS INSIDE THE BLOCK SURVIVE A LOAD. The loader copies them out
 * before reading and puts them back afterwards:
 *
 *   the three audio volumes at ADDR_VOLUME_AT_ZERO, ADDR_STREAM_VOLUME and
 *   ADDR_VOLUME_VOICE -- so loading a save does not reset what the player set
 *
 *   two strings, at the block's own start and at +0x120
 *
 * The first of those strings is also what gates the autosave: the mission-start
 * hook at 0x00444F3D tests its first byte and skips saving when it is empty.
 * So the block begins with a name that has to be non-empty for a save to
 * happen at all, and that same name is not restored FROM a save.
 *
 * The loader answers 0 on a bad length and reads nothing, leaving the block as
 * it was. */
int32_t __cdecl SaveGameProcSection(am2_FILE *fp);
int32_t __cdecl LoadGameProcSection(am2_FILE *fp);

void gameproc_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00425A10. Read a whole savegame: check the outer tag, reset the token
 * context, then run the eleven loaders in the order SaveGame wrote them.
 * Closes `fp` on both exits. Returns 1 on success, 0 if any loader failed. */
int32_t __cdecl LoadGame(am2_FILE *fp);

#endif /* AM2_GAMEPROC_H */
