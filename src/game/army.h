/* army.h -- who is on whose side.
 *
 * Four armies and a fifth value, 4, that means "everybody's". The alliance
 * matrix is 4x4 int32 at 0x00511E60, and it is a matrix rather than a set of
 * flags: 0x00424E80 fills it with the IDENTITY -- each army allied with itself
 * and nobody else -- and then walks the comm object's player records marking
 * any two on the same team.
 *
 * That initialiser is what settles the reading, and it settles a name too.
 * `EvtMarkSet` and `EvtMarkClear` write 1 and 0 into this table and were named
 * from their shape alone, over "a table of rows of four dwords". They are the
 * script's `ally` and its opposite; they are `EvtSetAllied` and
 * `EvtClearAllied` now.
 */
#ifndef AM2_ARMY_H
#define AM2_ARMY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x0040F230, two callers, and STDCALL rather than cdecl -- it is a `ret 8`
 * that reads both arguments off the stack, and both call sites load `ecx` with
 * the comm object first and then ignore it.
 *
 * A raw read of the matrix: no bounds check, no special case for army 4. */
int32_t __attribute__((stdcall)) AllyFlag(int32_t a, int32_t b);

/* 0x00457720, two callers. The same question with army 4 answering yes to
 * everything, on either side. */
int32_t __cdecl ArmiesAllied(int32_t a, int32_t b);

/* 0x004577C0, nine callers. Is this object on our side: its owner byte against
 * the global at 0x004F9FDC, through the matrix.
 *
 * Two things before that. In a multiplayer session an object of type 2 whose
 * field at +0x544 is 7 is NEVER friendly, whoever owns it. And an owner of 4
 * always is.
 *
 * It dereferences `obj` unconditionally on the ordinary path -- the null test
 * at the top guards only the multiplayer block, and falls THROUGH to the read.
 * Reproduced; no caller passes null. */
int32_t __cdecl ObjIsFriendly(const void *obj);

/* 0x00457750, twenty-one callers, and the name was already in orig.h -- kept,
 * because it is the more careful of the two readings.
 *
 * The object that stands for an army: for our OWN army the one the uid at
 * 0x00511E4C names, and for any other the first in its list that is type 2
 * with a non-zero field at +0x548. Null when the army is out of range or
 * nothing qualifies.
 *
 * `ArmyLeader` was the name I nearly gave it. What the +0x548 test means is
 * not established, so the claim is not made. What IS known is what audio.cpp
 * uses it for: `LookupOwnerObj(g_defaultOwner)` is where the ear is. */
void *__cdecl LookupOwnerObj(uint32_t owner);

/* 0x00457820, seven callers, and it went in as ADDR_SPRITE_DROP_NAMED -- named
 * from the one call site, which passes 0x0045A030 as the second argument. That
 * is a FUNCTION, and this calls it: `call ebp`. Nothing here is a sprite.
 *
 * Call `fn` on every object the army owns, skipping
 * any with flag 4 and -- in a multiplayer session -- any type 2 whose +0x544
 * is 7, which is the same exception ObjIsFriendly makes.
 *
 * A uid that no longer resolves is REMOVED from the list, and the index is not
 * advanced when that happens, because the list has shifted under it. So this
 * is a sweep as well as a walk. */
void __cdecl ForEachArmyObject(int32_t army, void(__cdecl *fn)(void *obj));

int army_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_ARMY_H */
