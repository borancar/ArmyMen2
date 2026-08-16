/* RectSet -- reconstructed from ArmyMen2.exe 0x0042E1C0.
 *
 * Stores four dwords into a caller-supplied structure. The original name is not
 * recovered; `RectSet` is ours, chosen because the shape and usage match
 * SetRect(RECT*, left, top, right, bottom) exactly. The game imports USER32's
 * SetRect separately, so this is a private equivalent rather than a wrapper.
 *
 * Measured as the hottest function on the startup path: 90,185 calls in a
 * 45-second run to the title screen, across 186 direct call sites. That makes
 * it a good correctness canary -- if the argument order were wrong, rendering
 * would break immediately and visibly rather than subtly.
 *
 * Original body, for reference:
 *      mov  eax, [esp+4]     ; dst
 *      mov  ecx, [esp+8]
 *      mov  edx, [esp+0xc]
 *      push ebx / mov ebx, eax / push esi
 *      mov  esi, [esp+0x18]  ; == [esp+0x10] before the pushes
 *      push edi
 *      mov  edi, [esp+0x20]  ; == [esp+0x14] before the pushes
 *      mov  [ebx], ecx / [ebx+4], edx / [ebx+8], esi / [ebx+0xc], edi
 */

#include "rect.h"
#include "../inject/patch.h"

#include <stdint.h>

void __cdecl RectSet(int32_t *dst, int32_t a, int32_t b, int32_t c, int32_t d)
{
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
}

int rect_install(void)
{
    return patch_replace(ADDR_RECT_SET, RectSet, "RectSet", 5);
}
