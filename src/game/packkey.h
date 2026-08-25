#ifndef AM2_PACKKEY_H
#define AM2_PACKKEY_H

#include <stdint.h>
#include "../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* A packed 26-bit key built from three fields, used throughout the map code.
 *
 *   bit  25 .. 19   field A   7 bits    PackKey's first argument
 *   bit  18 .. 17   unused
 *   bit  16 ..  7   field B  10 bits    second argument
 *   bit   6 ..  0   field C   7 bits    third argument
 *
 * The two-bit gap is real, not a mistake in reading it. PackKey shifts the
 * first argument left by 12 and then the whole thing by 7, which leaves room
 * for twelve bits of B, but every reader masks only ten (`& 0x3FF`). So either
 * B is guaranteed below 1024 by its callers, or values above that are silently
 * truncated on the way back out. Worth remembering if a map ever comes back
 * wrong: it is exactly the kind of latent limit that would only show up on a
 * large map.
 *
 * What the three fields mean is established for ONE family of users and not
 * for the other, which is why they keep structural names. In the sprite
 * loaders A is the SET, B the index and C the frame, and four readers say so
 * independently. PreloadSprite composes a sprite id with exactly PackKey's
 * arithmetic written out inline; SpriteSetForKey takes field A and splits it
 * on the same 1..9 / 20 bands ADDR_SPRITE_SET_DIRS uses to pick a directory;
 * SpriteLoadFromDataFile packs its own {set, index, frame} arguments with
 * PackKey and stores the result as the sprite id; and PreloadSpriteByKey
 * unpacks a key with all three accessors and hands the results straight to
 * PreloadSprite's set, index and frame parameters, which is as direct as it
 * gets.
 *
 * KeyLookupTriple's table at 0x00516150 is a different user of the same
 * packing and none of that applies to it, so the accessors are not renamed.
 */

/* Original: 0x00433810. */
uint32_t __cdecl PackKey(uint32_t a, uint32_t b, uint32_t c);

/* Originals: 0x00433830, 0x00433840, 0x00433850. */
uint32_t __cdecl KeyFieldA(uint32_t key);
uint32_t __cdecl KeyFieldB(uint32_t key);
uint32_t __cdecl KeyFieldC(uint32_t key);

/* 0x00434290 and 0x004346E0, two and sixteen callers. The table those keys are
 * FOR: a sorted array of {key, value} dwords at 0x00516150, searched by
 * halving, with -1 for a key that is not there. Both names are ours.
 *
 * KeyLookupTriple is what ties the two families together -- it packs its three
 * arguments with exactly PackKey's arithmetic, written out inline rather than
 * called, and hands the result over.
 *
 * The comparison is UNSIGNED where the loop bound is signed: `jae` picks the
 * half, `jg` decides whether to go round again. Reproduced as it stands.
 *
 * Thoroughly checked, and measured rather than assumed: KeyLookup runs 1,588
 * times in a Boot Camp mission, and making the search never match puts the
 * frame 293,671 pixels wrong -- 37% of it -- and drops the game's own
 * "calculating region data..." line. So this table is what the region pass
 * consults, which is the one thing known about what it is FOR. */
int32_t __cdecl KeyLookup(uint32_t key);
int32_t __cdecl KeyLookupTriple(uint32_t a, uint32_t b, uint32_t c);

int packkey_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PACKKEY_H */
