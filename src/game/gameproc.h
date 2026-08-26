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

/* Three more of the 96-byte run around ADDR_GAME_OVER_STATE. The fourth,
 * 0x0042E580, is ClearGameOver and has been in winmain.cpp since the WinMain
 * chain went in -- which is what named these: they nearly went in as
 * SetEndState and CurrentEndState until the address was grepped.
 *
 * SetGameOver copies three dwords from 0x00511E14 in beside the state before
 * storing it, so the record is four wide: an index and three numbers that were
 * true when it was set.
 *
 * What the index MEANS is not settled. winproc.cpp uses GameOverState's answer
 * to select an entry of ADDR_STATE_DISPATCH, while the rest of the family
 * calls it game over; an end-of-mission outcome that picks an end screen would
 * be both. The names describe the record, not what it is for.
 *
 * StateLeaveAlias is one `jmp` to 0x0042E720 and lives among them by accident
 * of layout; it is the third function of that shape in the tree. */
/* 0x00424AD0 and 0x00424AF0, 29 callers and one. The two halves of a state
 * change, and CLAUDE.md's account of the state machine is written in terms of
 * them: RequestState raises the pending flag and records what is wanted;
 * CommitState takes it, puts the wanted value back to -1, moves it into
 * ADDR_GAME_STATE, drops the pending flag and raises "entered".
 *
 * Nothing validates the state, and CommitState runs whether anything was
 * pending or not -- called with nothing wanted it would put -1 into the game
 * state. Its one caller checks first. */
void __cdecl RequestState(int32_t state);
void __cdecl CommitState(void);

void __cdecl SetGameOver(int32_t state);
int32_t __cdecl GameOverState(void);
void __cdecl StateLeaveAlias(void);

/* 0x00462A40. Toggles the info overlay -- what the info action does in a
 * network game, where pausing is not allowed. Strictly 0 or 1. */
void __cdecl ShowInfoMp(void);

void gameproc_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00425A10. Read a whole savegame: check the outer tag, reset the token
 * context, then run the eleven loaders in the order SaveGame wrote them.
 * Closes `fp` on both exits. Returns 1 on success, 0 if any loader failed. */
int32_t __cdecl LoadGame(am2_FILE *fp);

#endif /* AM2_GAMEPROC_H */
