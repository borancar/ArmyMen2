/* rand.cpp -- rand and srand, from 0x00464420 and 0x00464416.
 *
 * The seed is the image's word, ADDR_RAND_SEED, because that is what the
 * original's rand reads and what the reconstruction's GameSrand writes:
 * StartPacketThread seeds 0 at startup and BuildRespawnPool seeds again at
 * level load, and a private seed lost both. STATUS.md records the 15,022
 * cell weights that cost.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The shared rand seed at 0x0048CC1C (initial _holdrand = 1). Written by both
 * crt_srand here and GameSrand in maprow.cpp, so migrated non-const; the
 * initial byte value equals the image and is byte-checked. (Plain linkage to
 * match standalone.h's shared declaration; a global's symbol name is unmangled
 * regardless, so maprow.cpp and rand.cpp resolve to the one object.) */
uint32_t am2_rand_seed = 1;
#endif

#define crt_holdrand (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_RAND_SEED))

int32_t __cdecl crt_rand(void)
{
    crt_holdrand = crt_holdrand * 0x343FDu + 0x269EC3u;
    return (int32_t)((crt_holdrand >> 16) & 0x7FFFu);
}

void __cdecl crt_srand(uint32_t seed)
{
    crt_holdrand = seed;
}
