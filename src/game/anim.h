/* anim.h -- the animation table that follows a sprite set in a `.ani` file.
 *
 * `LoadSpriteFile` opens `data/ani/<name>.ani`, reads its 256-colour palette,
 * loads every sprite in it through `LoadSpriteSet`, and then hands the file --
 * still open, positioned exactly where the sprites ended -- to this. What is
 * left is a list of named animations, each a grid of cells over the sprites
 * that were just loaded.
 *
 * The format is confirmed rather than read: walking all twenty shipped `.ani`
 * files with this layout consumes each one to its last byte, 349 animation
 * entries in all. That is the check worth having on a file format -- a
 * mis-sized field would leave a remainder or run off the end.
 */
#ifndef AM2_ANIM_H
#define AM2_ANIM_H

#include <stdint.h>

#include "../inject/orig.h"   /* am2_FILE */

#ifdef __cplusplus
extern "C" {
#endif

/* One cell of an animation: a sprite index and a value the loader only ever
 * copies. The index is stored RELATIVE to the file in the file itself and made
 * absolute here by adding the sprite list's length from before the load. */
typedef struct AM2_AnimCell {
    int16_t field0;
    int16_t sprite;
} AM2_AnimCell;

/* An animation: `frames * facings` cells, laid out facing-major -- the shipped
 * data steps consecutively within a facing and jumps between them, which is
 * what fixes the order. `facings` is a power of two in every shipped file
 * (1, 2, 8, 16 or 32) and `facingBits` is its log, so a consumer can get from
 * an 8-bit heading to a facing with a shift instead of a divide.
 *
 * field4 and field6 are copied and not used here; across the shipped files
 * field4 takes sixteen values from 0 to 99 and field6 only 0, 1, 2 and 4.
 *
 * The record is malloc'd and NOT cleared, so when `frames * facings` is not
 * positive `cells` is left holding whatever the heap had. The original leaves
 * it that way and nothing reads it, since the same test guards the loop. */
typedef struct AM2_Anim {
    int16_t       frames;      /* +0x00 */
    int16_t       facings;     /* +0x02 */
    int16_t       field4;      /* +0x04 */
    int16_t       field6;      /* +0x06 */
    uint8_t       facingBits;  /* +0x08 -- Log2Mask(facings) */
    uint8_t       zero9;       /* +0x09 -- written 0, never read here */
    uint8_t       pad0A[2];
    AM2_AnimCell *cells;       /* +0x0C */
} AM2_Anim;

/* `next` chains animations into a cycle -- rifleman.ani has 2 -> 23 -> 3 ->
 * 24 -> 2 -- and the file writing 0 means "none", which becomes -2 here.
 *
 * `borrowed` says the animation belongs to another table and must not be freed
 * with this one. */
typedef struct AM2_AnimEntry {
    int32_t   id;          /* +0x00 */
    int32_t   next;        /* +0x04 */
    AM2_Anim *anim;        /* +0x08 */
    int32_t   borrowed;    /* +0x0C */
} AM2_AnimEntry;

/* The caller owns this pair and passes its address. A non-zero count is how
 * every caller decides the file is already loaded and skips it. */
typedef struct AM2_AnimTable {
    int32_t        count;
    AM2_AnimEntry *entries;
} AM2_AnimTable;

/* Original: 0x00409BE0, 768 bytes, one caller -- LoadSpriteFile's tail. The
 * name is ours; the function carries one string, "Error!  %d\n", and that is
 * shared vocabulary rather than a name.
 *
 * `base` is the sprite list's length from BEFORE the set was loaded, so a
 * cell's file-relative index plus `base` is its index in the list. Anything
 * that lands at or past the end of the list is logged and stored anyway.
 *
 * `fallback` is another table already loaded, and it is what makes the soldier
 * files small: an entry whose kind is not 1 carries no animation of its own
 * and takes one with the same id out of `fallback` instead -- grenadier.ani
 * borrows 43 of its 49 that way, all of them from rifleman.ani, which every
 * caller in that group passes. It may be null, and is for the first file of a
 * group.
 *
 * Two details worth stating because they read as mistakes. The borrow search
 * does not stop at the first match, so the LAST entry with the id wins. And
 * when nothing matched -- including when `fallback` is empty -- it takes
 * entry 0's animation without checking the count, which is a read out of
 * bounds on a table that has none. No shipped file reaches it. */
void __cdecl LoadAnimTable(am2_FILE *fp, AM2_AnimTable *table, int32_t base,
                           const AM2_AnimTable *fallback);

int anim_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_ANIM_H */
