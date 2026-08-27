/* place.cpp -- the multiplayer unit-placement files.
 *
 * A name of ours, but only just: the keyword these lines open with is `place`
 * and the files are called `<map>_<colour>_place.txt`, so the vocabulary is
 * the program's even though no source filename is. They sit in the
 * pad.cpp..script.cpp band, which is a THIRD translation unit from
 * defparse.cpp's record parsers and definfo.cpp's file reader -- the same
 * reason those two are separate modules from each other.
 *
 * Thirty-six such files ship, all of them under multiplayer map directories.
 * A line names a unit type, a position, a facing, a group and a name:
 *
 *     place  tank        2974  2787  0    2  -
 *     place  mgpill      2571  3519  112  4  -
 *
 * They are read once per COMPUTER army when a network game starts, and again
 * whenever a human leaves and the AI takes their army over.
 *
 * THE RECORD IS CONFIRMED FROM BOTH ENDS, which is worth more than either
 * alone. The line parser at 0x0043B490 fills a 0x30-byte block on its stack
 * and hands its base to AddPlacement; the consumer in LoadArmyPlacement reads
 * the same block back out. Every field lands at the same offset in both, and
 * two of them corroborate their type independently -- +0x00 goes to
 * TileOfPoint, which takes a packed AM2_Point, and +0x08 indexes the 40-byte
 * unit-type table at 0x00487898, whose names start at 0x004878A4 with
 * "rifleman".
 */
#ifndef AM2_PLACE_H
#define AM2_PLACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x30 bytes. The parser writes x and y as separate int16s into +0x00 and
 * +0x02 and the consumer reads the pair as one packed point, so `where` is
 * spelled as the dword both halves agree on. */
typedef struct {
    uint32_t where;      /* +0x00, packed AM2_Point */
    uint8_t  facing;     /* +0x04, one byte -- the game's 0..255 angle */
    uint8_t  pad[3];     /* +0x05, never written and never read */
    int32_t  type;       /* +0x08, index into the unit-type table */
    int32_t  group;      /* +0x0C, the line's group column */
    char     name[0x20]; /* +0x10, "-" in the file becomes empty here */
} AM2_Placement;

/* 0x0043B3D0. Drop the table and forget it. */
void __cdecl FreePlacements(void);

/* 0x0043B410. Append one record, growing the table. */
void __cdecl AddPlacement(const AM2_Placement *rec);

/* 0x0043A690. Whether a unit type may be placed at all in this game type,
 * and whether `points` will cover it. Both halves come out of the unit-type
 * table: +0x20 is the cost and +0x24 is the game-type mask. */
int32_t __cdecl CanAffordUnit(int32_t type, int32_t points);

/* 0x0043A560. Build "<map>_<colour>_place.txt" for one comm slot. Returns
 * `dest`, which is what the original leaves in eax. */
char *__cdecl BuildPlacementPath(char *dest, int32_t slot);

/* 0x0043B700. Read one slot's placement file and lay its units out. */
void __cdecl LoadArmyPlacement(int32_t slot);

int place_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PLACE_H */
