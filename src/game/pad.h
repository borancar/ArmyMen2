/* pad.cpp -- the pad tables and their savegame section.
 *
 * A module of its own because the image names it: the loader hands
 * "C:\\ArmyMen2\\source\\pad.cpp" to CheckSaveTag. Only the save trio is
 * reconstructed so far.
 *
 * Pads are the map's named regions -- the `pad` statement declares them and
 * ADDR_PADS/ADDR_PAD_NUMBERS are the two tables behind them.
 */
#ifndef AM2_PAD_H
#define AM2_PAD_H

#include <stdint.h>
#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

/* 0x004373C0. Zero both pad tables and the count. Two callers -- the loader
 * below and one other -- so it is the general "forget every pad" and not a
 * detail of loading.
 *
 * Its `rep stosd` counts are what confirm the table sizes independently of the
 * save code: 0x1300 dwords over ADDR_PAD_NUMBERS and 0x2400 over ADDR_PADS,
 * which are exactly the 0x4C00 and 0x9000 the section writes. */
void __cdecl ResetPads(void);

/* 0x004373F0, one `jmp` to the line above and nothing else. Reconstructed as
 * the alias it is, the same shape as FreeSpriteListAlias. */
void __cdecl ResetPadsAlias(void);

/* 0x00437A90 and 0x00437AE0. The pad section: a tag, the pad COUNT, and then
 * the two tables whole. 56 KB, a third of the entire savegame.
 *
 * The count goes out through WriteSaveTag and comes back through a plain
 * fread -- so here that helper is neither writing a tag nor a length but a
 * data value, which is the third thing it is used for. It is simply "write a
 * dword"; the name describes its commonest use.
 *
 * The tables are adjacent in memory (ADDR_PADS + 0x9000 == ADDR_PAD_NUMBERS)
 * and yet the section writes the HIGHER one first. Reproduced in that order,
 * because a reconstruction that wrote them in address order would produce a
 * file the original cannot read.
 *
 * The loader clears both tables BEFORE its tag check, so a failed load has
 * already forgotten every pad -- the same shape as LoadItems and the opposite
 * of LoadEventBlock. */
int32_t __cdecl SavePadSection(am2_FILE *fp);
int32_t __cdecl LoadPadSection(am2_FILE *fp);

void pad_install(void);

#ifdef __cplusplus
}
#endif

/* 0x004375A0, one caller. Run one pad's trigger for a frame: act on the two
 * transitions of its comparison and nothing else. */
void __cdecl PadFinalise(void *pad, void *obj);

/* 0x004376C0 and 0x00437770, two callers each, both ObjTileHook. The two
 * halves of the pad walk: when an object's tile changes, every pad number it
 * entered gets the first and every one it left gets the second. A pad with a
 * comparison counts the object and hands it to PadFinalise; one without
 * notifies immediately, type 3 for entering and 2 for leaving. See pad.cpp
 * for what the leave half's own log line names. */
/* 0x00437860, one caller. What happens to an object as it crosses trigger
 * pads: a damage pass BEFORE the tile-changed early exit, then two independent
 * pad mechanisms. Returns 1 when any enter or leave fired -- the caller
 * discards it. */
int32_t __cdecl ObjTileHook(void *obj);

/* 0x00437570 and 0x00437540. The two handlers a pad registers when it has an
 * enter or leave event to raise: match the uid the pad recorded against the
 * one the notify carried, then raise the pad event itself. Registered here and
 * in event.cpp's savegame restore, and compared by pointer in its save -- so
 * all four sites must name the same function, never the image address. */
void __cdecl EvtPadOn(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t, const uint8_t *pad);
void __cdecl EvtPadOff(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                       int32_t, const uint8_t *pad);

void __cdecl PadNumberEnter(void *obj, void *padNumber);
void __cdecl PadNumberLeave(void *obj, void *padNumber);

#endif /* AM2_PAD_H */
