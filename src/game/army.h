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

/* 0x00458070, twenty callers. Detach an object from whatever it is attached
 * to, then attach it to a target if one is given -- a NULL target is the pure
 * detach the three per-type destroy handlers use. Types 2, 3 and 8 only. */
void __cdecl ObjAttachTo(void *subject, void *target);

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

/* 0x004574D0. Whether two objects are on the same side: AllyFlag with four
 * exceptions -- a multiplayer kind 7 is allied only to another kind 7, army 4
 * is allied to everything, and sharing an object-table record is alliance.
 * `useRec3` chooses which of the second object's two record pointers to use. */
int32_t __cdecl ObjsAreAllied(void *a, void *b, int32_t useRec3);

/* 0x00457620. The same question with an army on the left: ObjsAreAllied from
 * its `army == 4` test onward, held as a separate body in the image. */
int32_t __cdecl ArmyAlliedWithObj(int32_t army, void *b, int32_t useRec3);

/* 0x00457750, twenty-one callers, and the name was already in orig.h -- kept,
 * because it is the more careful of the two readings.
 *
 * The object that stands for an army: for our OWN army the one the uid at
 * 0x00511E4C names, and for any other the first in its list that is type 2
 * with a non-zero field at +0x548. Null when the army is out of range or
 * nothing qualifies.
 *
 * `ArmyLeader` was the name I nearly gave it, and the +0x548 test is why it
 * was not: what that field meant was not established. It is now. UnitClassName
 * answers "Sarge" for a unit with OBJ_OFF_SARGE set, so this really does find
 * the army's leader and `ArmyLeader` would have been right. The name stays as
 * the image's own, but the caution can go. What IS known is what audio.cpp
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

/* 0x00457B30, seven callers, every one of them creating a unit. Put the type's
 * health figure into the object's MAX health at +0x60, scaled by who owns it
 * and how the game is set up. Every caller then computes the current health at
 * +0x62 from what this left behind, which is what fixes the two fields as max
 * and current.
 *
 * Four exits, and only one of them is the interesting one:
 *
 *   - a max health already above 400 is left exactly as it is;
 *   - in a multiplayer session the amount is stored unscaled;
 *   - for OUR OWN army it is multiplied by 4, 2 or 1.5 as the difficulty is
 *     easy, normal or hard;
 *   - for anyone else on hard, nothing happens at all -- and on easy or normal
 *     the enemy keeps the LARGER of a third of the amount and the amount less
 *     five for every retry of this level, divided by 2*difficulty + 2.
 *
 * That last term is a rubber band: 0x00512330 is the retry counter the level
 * loader logs as "Attempt# %d", so an enemy gets weaker the more times the
 * player has restarted, and faster on easy than on normal.
 *
 * The arithmetic is `long double` on purpose. The original is x87 throughout
 * -- `fild` an int, `fmul` a float or a double, then MSVC's truncating _ftol
 * -- so every product is rounded to 80 bits and not to 64. On both i386 and
 * x86-64 `long double` IS the x87 type, so this is the same function; plain
 * `double` would differ in the last place for some amounts and would only ever
 * show up as one health point somewhere. */
void __cdecl SetMaxHealth(void *obj, int32_t amount);

/* 0x0044BBD0, two callers. Put 1 in the dword at +0x548 -- the same field
 * LookupOwnerObj looks for -- and then run the type-2/3/8 action at
 * 0x00457CD0 with 0x2710. Whatever +0x548 means, this is what SETS it, and
 * nothing else in the tree does. The name is ours. */
void __cdecl SetLeadsAndAct(void *obj);

/* 0x0045AFB0 and 0x0045AFE0, four and four callers. The first entry of the
 * three-field list at +0x538, resolved to an object; and the same run through
 * ObjType2Field548.
 *
 * Null for a null argument or an empty list, and the empty case answers 0
 * rather than falling through -- the count at +0x53C is tested against 1
 * before the pointer at +0x540 is touched. */
void *__cdecl ListFirstObj(const void *obj);
uint32_t __cdecl ListFirstField548(const void *obj);

/* 0x0045AE30, one caller, and it names itself twice: "ExitAllFromVehicle: I
 * was killed in a vehicle, damage owner is me" and "... not me".
 *
 * Empty a vehicle from the LAST seat down, and hurt whoever was in it. Each
 * seat is asked first whether it is emptied at all; a seat that is gets its
 * occupant taken off the list, its riding field cleared, and -- when we must
 * broadcast for the vehicle's own army -- one more call on it.
 *
 * The damage is separate and runs even for a seat that was NOT emptied, unless
 * the vehicle's kind is 2 or 3. Which of the two messages comes out is decided
 * by CommMustBroadcast on the DAMAGE owner's army, with no multiplayer session
 * answering "is me" -- and the "not me" path sends one extra call before the
 * same damage.
 *
 * The loop index lives in the argument slot the vehicle came in on, MSVC
 * having kept the vehicle in a register; it is restored from there after the
 * ejection, because that half clobbers the register. */
/* 0x0045AC90, four callers. Empty ONE seat of a vehicle: look the occupant up,
 * choose a spot beside it, unlink the seat, put the occupant on the ground and
 * move the selection if that emptied it. Answers 1 when somebody came out.
 * Was ADDR_VEHICLE_SEAT_BLOCKED, which was named from a call site. */
int32_t __cdecl ExitOneFromVehicle(int32_t seat, void *vehicle);

void __cdecl ExitAllFromVehicle(void *vehicle, uint32_t damageOwner);

int army_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00403600, 0x00403660 and 0x004036C0 -- three target predicates sharing
 * one functions.tsv entry, so all three are reconstructed together. */
int32_t __cdecl ObjIsOurs(void *obj, int32_t allies);
int32_t __cdecl ObjIsLiveTarget(void *obj);
int32_t __cdecl ObjIsHittable(void *obj);

#endif /* AM2_ARMY_H */
