/* objflag.cpp -- bit 0 and bit 1 of the word at an object's offset 0.
 *
 * Four functions at 0x0040A010..0x0040A040: set bit 0, clear bit 0, test bit
 * 0, test bit 1. Named for position; what the bits mean is not established.
 * The readers are spread across map and object code -- DrawMapTiles is one --
 * so the word is not specific to any one subsystem.
 *
 * The two tests return the MASKED VALUE, 1 or 2, not a normalised boolean.
 * That matters: a caller comparing against 1 works for bit 0 and would fail
 * for bit 1, and a reconstruction that "tidied" these into 0/1 would change
 * ObjFlagBit1's answer from 2 to 1.
 */
#ifndef AM2_OBJFLAG_H
#define AM2_OBJFLAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     __cdecl ObjFlagSet0(void *obj);
void     __cdecl ObjFlagClear0(void *obj);
uint32_t __cdecl ObjFlagBit0(const void *obj);
uint32_t __cdecl ObjFlagBit1(const void *obj);

int objflag_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_OBJFLAG_H */
