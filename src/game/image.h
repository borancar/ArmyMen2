/* image.h -- reaching the original image's constant tables.
 *
 * Some reconstructions are mostly a table lookup: ScriptLookupToken is a walk
 * of the 185 keywords at 0x00487C90 and nothing else. The table is the game's
 * data, so the port reads it out of the image rather than carrying a copy --
 * a copy would be redistribution, and it would be a second place for it to
 * drift.
 *
 * In the game the image is at 0x00400000 and the address is literal. Elsewhere
 * it may not be, and the offline test is the case that forced this: Wine maps
 * locale.nls at 0x00380000..0x00443000 and three more .nls files through to
 * 0x0084A000, so a test process that is not itself the game cannot get
 * 0x00400000 -- those mappings are made by the loader before main() runs and
 * there is nothing user code can do about them.
 *
 * So an image address is written AM2_IMAGE(0x00487C90) and resolves through a
 * slide that is zero in the game. The cost is one add; the gain is that a
 * table-driven reconstruction can be tested with no game and no emulator, and
 * that the eventual native ELF -- where 0x00400000 is equally unavailable --
 * needs no further change here.
 */
#ifndef AM2_IMAGE_H
#define AM2_IMAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zero in the game, where the image is at its own base. Set once by whoever
 * mapped the image somewhere else (the offline selftest). */
extern int32_t am2_image_slide;

/* The image maps at its own base in the injected and byte-match builds
 * (am2_image_slide is always 0 there -- see standalone.h), so fold to the plain
 * address: that reproduces the original's direct `[addr]` access instead of an
 * `addr + slide` read the original never had.  Only the relocatable standalone
 * build carries the runtime slide. */
#if defined(AM2_STANDALONE) && !defined(AM2_RELINK)
#define AM2_IMAGE(addr) \
    ((void *)((uint8_t *)(uintptr_t)(addr) + am2_image_slide))
#else
#define AM2_IMAGE(addr) ((void *)(uintptr_t)(addr))
#endif

#ifdef __cplusplus
}
#endif

#endif /* AM2_IMAGE_H */
