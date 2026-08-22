#ifndef AM2_DIST_H
#define AM2_DIST_H

#include <stdint.h>
#include "../inject/orig.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
#endif

/* A map point: two signed 16-bit coordinates. Inferred from the movsx reads at
 * offsets 0 and 2 in ApproxDist. */
typedef struct {
    int16_t x;
    int16_t y;
} AM2_Point;

/* Original: 0x0042DDE0. Octagonal approximation of the distance between two
 * points -- max(|dx|,|dy|) + min(|dx|,|dy|)/2. */
int32_t __cdecl ApproxDist(const AM2_Point *a, const AM2_Point *b);

/* 0x0042DE20. ApproxDist's own formula, taking the deltas instead of two
 * points -- identical arithmetic, so the two agree by construction. */
int32_t __cdecl ApproxDistXY(int32_t dx, int32_t dy);

/* 0x0042DD90. Signed difference between two 8-bit headings, 256 to the turn,
 * wrapped into -128..128 in both directions.
 *
 * Worth recording how this was nearly got wrong: the first reading stopped at
 * the function's first `ret` and saw only the `d > 0x80` correction, and was
 * written up as a deliberate asymmetry with a confident comment explaining
 * behaviour the function does not have. The second branch lives past that ret.
 * The vectors caught it on the first run -- AngleDelta(255, 2) is 3, not -253.
 * A function with two returns is not unusual and a disassembly helper that
 * stops at the first one will misread it every time. */
int32_t __cdecl AngleDelta(uint32_t from, uint32_t to);

/* 0x0042DFB0. Rounds an 8-bit value to `bits` significant bits, adding half a
 * step first. The mask to 8 bits happens AFTER the rounding term, so a value
 * carrying past 255 wraps rather than saturating. */
int32_t __cdecl RoundTo8(int32_t value, uint32_t bits);

/* 0x0042DFE0. The bit index of a power of two in 1..0x8000, and 0 for
 * anything else -- see the note in dist.cpp for how MSVC laid it out and why
 * the return is a byte. */
uint8_t __cdecl Log2Mask(int32_t value);

/* 0x0042DEB0, 30 callers. The 8-bit heading from `from` to `to`.
 *
 * The ratio of the smaller delta to the larger, scaled by 512, indexes
 * whichever reverse table matches which delta was larger -- so the tables are
 * arctangents and the two halves of the circle are picked apart by comparing
 * |dx| and |dy| first. Straight up is 0 and straight down is 0x80.
 *
 * Returns a BYTE. The two table paths leave the division's quotient in the
 * upper 24 bits of eax and only AL is written; the dx == 0 path leaves a clean
 * 0 or 0x80, which is the argument that nothing may read above AL. Same
 * reasoning as Log2Mask, and the same consequence: neither differential
 * harness can take it, since both compare eax.
 *
 * What DOES check it is one layer down. The two tables are 1,025 signed bytes
 * each, centred on the addresses the code carries, and tools/trigdump.py
 * compares them byte for byte against the original. */
uint8_t __cdecl AngleBetween(const AM2_Point *from, const AM2_Point *to);

int dist_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_DIST_H */
