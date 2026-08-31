/* item.cpp -- the item list and the objects on it.
 *
 * Reconstructed from the translation unit the linker placed between the
 * item.cpp and map.cpp save-tag anchors (0x00428C40..0x0042DBB0). The item half
 * runs to about 0x0042B120, where the map code starts; docs/00-recon.md
 * explains why alphabetical link order lets a function be attributed at all.
 */
#ifndef AM2_ITEM_H
#define AM2_ITEM_H

#include <stdint.h>
#include "../inject/orig.h"   /* am2_FILE, for the savegame pair */

#ifdef __cplusplus
extern "C" {
/* 0x004494A0, three callers. Which pose a unit takes for the weapon it holds
 * -- an index into ADDR_WEAPON_POSE_FRAMES. */
int32_t __cdecl WeaponPoseIndex(void *obj, void *weapon);

/* 0x00429220, one caller -- the deploy dispatcher's default arm. Put an
 * object at a point: move every row, take it off the map and put it back,
 * re-apply its height. */
void __cdecl PlaceObj(void *obj, uint32_t where);

/* 0x00433C20, three callers. The height handler for types 1 and 4: stamp the
 * height, recompute the row's depth key from it, and do the same down the
 * chain until a link that is not an item. */
void __cdecl ApplyHeightItem(void *obj, int32_t height);

/* 0x0040A380, one caller -- StepObjRows, so this runs once per row of every
 * object every frame. Advance one row's animation. */
void __cdecl StepRowAnim(void *row);

/* 0x00457BC0, one caller. Bump an object's rank and, for a plain type 2 at
 * ranks 3, 5 and 7, hand it a new weapon. The promotion happens for every
 * type; only the weapon is a trooper's. */
void __cdecl RankPromote(void *obj);

/* 0x00417AB0, one caller. Walk a uid list, drop what no longer resolves, and
 * act on every live type 2. */
void __cdecl Type2ActionAll(void);

#endif

/* Original: 0x00447990, and it names itself -- "RemoveInventoryItem". Take one
 * slot out of a unit's six-entry weapon inventory: shift the entries above it
 * down, clear the sixth, and fix up which slot is in hand.
 *
 * The shift is `memmove` of `0x14 - slot * 4` bytes, so it moves only the
 * entries that exist above `slot` and is skipped entirely for slot 5. The
 * sixth entry is cleared either way, which is what stops the shift leaving a
 * duplicate at the top.
 *
 * The selected slot is fixed in three cases and they are not symmetric. If the
 * removed slot WAS in hand the selection resets to 0 and the unit re-selects,
 * but only when ObjType2Field548 agrees. If the selection was ABOVE the removed
 * slot it slides down by one. If it was below it is left alone -- and the
 * `jle` that decides this makes "equal" go the first way, which is why the
 * equal case is tested first and cannot fall through to the decrement. */
/* Original: 0x00428140, nineteen callers. Damage `obj`: dispatch to the
 * handler for its type, notify and broadcast, and run the death sequence if it
 * has just died. `suppress` non-zero skips the multiplayer broadcast tests,
 * which is how a machine applies damage it was TOLD about. */
/* Original: 0x0041DB20. Take one row out of every map cell list it is linked
 * into, marking the region it occupied dirty first. */
void __cdecl RowUnregisterAll(void *row, void *desc);

/* Original: 0x00428CA0, seven callers. Put an object into the world at
 * `where` and tell the other machines. `resurrect` takes the revive path,
 * which refuses an object that is alive and not flagged destroyed. */
/* Original: 0x00428700. The per-frame object sweep: bump the stamp, step every
 * registered object, and check comm in a session. */
void __cdecl ObjFrameSweep(void);

/* Original: 0x00437A50. Push every repeating pad's deadline forward once the
 * clock has passed it, one period per frame. */
void __cdecl PadAdvanceDeadlines(void);

/* Original: 0x00417810. The "Flame On!" cheat's per-frame effect: every 200 ms
 * it re-arms the army leader and fires an effect just above it. */
void __cdecl FlameTick(void);

/* Original: 0x00425E70. Re-resolve the three object context slots from their
 * uids, so a slot whose object has gone becomes null rather than stale. */
void __cdecl RefreshObjCtx(void);

/* Original: 0x00424FE0. Push a one-second deadline forward once the clock
 * passes it. Nothing reads the deadline; see the source. */
void __cdecl AdvanceSecondDeadline(void);

void __cdecl DeployItem(void *obj, uint32_t where, int32_t resurrect,
                        int32_t suppress);

void __cdecl DamageObject(void *obj, int32_t amount, int32_t kind,
                          uint32_t attackerUid, int32_t extra,
                          int32_t suppress);

/* Original: 0x00428370, eight callers. Heal `obj` by `pct` percent of its
 * maximum health, clamped both ends, and raise the heal event. Items ignore
 * the percentage and go to full; nothing at or below zero health is healed. */
void __cdecl HealObject(void *obj, int32_t pct, void *src);

void __cdecl RemoveInventoryItem(void *unit, int32_t slot);

/* 0x00448D60, and it names itself in both log lines. Drop one inventory slot
 * on the ground. THE SLOT RANGE IS 1..5, not 0..5 -- slot 0 can never be
 * dropped -- and the deploy is gated on ITEM_OFF_AMMO, so a spent weapon
 * disappears instead. See item.cpp. */
void __cdecl TrooperDropItem(void *unit, int32_t slot, uint32_t at);

/* 0x00449760, one caller, and it names itself twice. Spend one charge of an
 * inventory slot; when the last one goes the slot is removed, the others are
 * told, and the item is FLAGGED rather than freed. Its slot is checked at one
 * end only -- `<= 0` and no upper bound. See item.cpp. */
void __cdecl UseInventoryItem(void *unit, int32_t slot);

/* A uid carries its owner in the top three bits, over a 29-bit per-owner
 * counter -- the layout objtable.h already describes and AddToItemList already
 * builds. UidArmy is the original's accessor for the owner half. */
uint32_t __cdecl UidArmy(uint32_t uid);

/* Applied to a uid on its way into, or out of, a comm message: all 100 call
 * sites are in the comm code around message construction and parsing, and what
 * they pass is the uid field at +4. It returns its argument unchanged.
 *
 * Two readings fit and both agree on the behaviour. It is the shape of a
 * host/network byte-order conversion, which is identity on x86; and it is the
 * shape of a debug-build validator stubbed out for retail, which this binary
 * demonstrably does elsewhere -- ADDR_LOG is a bare `ret`. Named for what it
 * does rather than for either guess. */
uint32_t __cdecl UidOnWire(uint32_t uid);

/* A 3-bit field packed at bit 18 of the word at +8, with a matched setter --
 * the strongest structural evidence available for a field, since get and set
 * agree on position and width.
 *
 * What it MEANS is not established, so it is named for where it is, the way
 * KeyFieldA/B/C already are in this tree. Two things point at an army or team
 * index and neither is proof: three bits give eight values and objtable.h
 * documents exactly eight uid counters, one per owner; and the only readers,
 * in 0x0041F8B0 and its neighbours, compare it against a parameter that uses
 * -1 for "any", which is how you filter a list by team. Against that,
 * AM2_Object already has an `owner` at +0x10, so if this is also an owner the
 * object carries two, and that wants explaining before it goes in a name. */
uint32_t __cdecl ObjFieldA(const void *obj);
void     __cdecl ObjSetFieldA(void *obj, uint32_t value);

/* Signed byte at +0x64. Read by three callers in 0x00420xxx, each passing it
 * straight to 0x0045F460 -- 3,200 bytes, no strings, unidentified. Sign
 * matters: the original uses movsx, so the field is int8_t and negative values
 * are meaningful. */
int32_t __cdecl ObjFieldB(const void *obj);

/* 0x00428950 and 0x00428BB0. The item section of a savegame, both named by
 * their own counts -- "Saved %d items" and "Loaded %d items".
 *
 * The wire format is settled by reading both ends independently and finding
 * they agree, which is better evidence than either alone:
 *
 *   0x06660007   opens the section        (checked by the loader)
 *   0x06660000   one before each item     (the loader's continue condition)
 *   0x06660001   closes it                (anything not 0x06660000 stops it)
 *
 * So the terminator is not really a value the loader knows: it stops on the
 * first marker that is not an item marker, and the saver happens to write
 * 0x06660001. A save ending any other way would load identically.
 *
 * The saver walks FirstItem/NextItem, which are the pair the registry
 * invariant in CLAUDE.md is about, and counts as it goes. It checks nothing:
 * a write that fails is not noticed, and it always answers 1.
 *
 * The loader clears the list first, and does it BEFORE reading its argument --
 * so a load that then fails its tag check has already emptied the world. The
 * failing path answers 0; every other path answers 1. */
int32_t __cdecl SaveItems(am2_FILE *fp);
int32_t __cdecl LoadItems(am2_FILE *fp);

/* 0x00429570, six callers. The attribute byte for the tile an object is
 * standing on: the tile index at +0x1A picks an entry of the table at
 * 0x00514EBC, and the byte comes back SIGN EXTENDED. The name is ours.
 *
 * The index is read as a word and used unsigned, so an object with 0xFFFF
 * there reads 64K into the table. Nothing bounds it; the original does not
 * either. */
int32_t __cdecl ObjTileAttr(const void *obj);

/* 0x00429540, three callers. The same byte taken by tile INDEX rather than by
 * object -- masked to 16 bits here where its neighbour reads a word, which is
 * the same value arrived at differently. */
int32_t __cdecl TileAttrAt(uint32_t tile);

/* 0x00448F00. The total obstruction between `from` and the map point `at`:
 * the objects standing there, the tile's own blocking bit, and a height step.
 * `ref` is the reference point ObjBlockWeight compares distances against. */
int32_t __cdecl BlockWeightAt(void *from, uint32_t at, uint32_t ref);

/* 0x0045B690. The same total for a chain the caller has already collected,
 * with the tile term on AM2_TILE_OPEN and no height step. */
int32_t __cdecl BlockWeightChain(void *from, uint32_t at, void *chain,
                                 uint32_t ref);

/* 0x0043CF70. The FOURTH variant, and the only one with a side effect: the
 * same walk as BlockWeightChain, with AM2_TILE_BLOCKS instead of
 * AM2_TILE_OPEN, with BlockWeightAt's height step, and DAMAGING every object
 * in the chain that ObjIsWatchedKind accepts. Its fifth argument is never
 * read. See item.cpp. */
int32_t __cdecl BlockWeightDamaging(void *from, uint32_t at, void *chain,
                                    uint32_t ref, int32_t unused);

/* 0x0043D050, four call sites in two functions. How obstructed is a roach at
 * `at` facing `dir`: BlockWeightDamaging summed over every point of its mask
 * for that direction. */
/* 0x0043CA00, three callers, one of them the type-8 destroy handler. Take a
 * roach's footprint back off the map: every cell its mask covers gets
 * AM2_TILE_COVER_STEP added back, once each however many mask points land in
 * it, and OBJ_FLAG_FOOTPRINT_ON is cleared. */
/* 0x0043C8D0, six call sites. Put a roach's footprint ON the map -- the exact
 * partner of the below, eighty-seven instructions each and four lines of diff.
 * Was ADDR_ROACH_STEP_TAIL_B, a role name from one call site. */
void __cdecl ObjSetRoachFootprint(void *obj);

/* 0x0043CDD0, two callers -- LoadType8 and the spawner at 0x00420B33.
 * Allocate a roach, seed it from the ROACH_* constants aai/game.aai names,
 * run the common init and the row set, and lay its footprint down unless the
 * state has already been entered. */
void *__cdecl CreateRoach(int32_t kind, const char *name, int32_t x, int32_t y,
                          int32_t army, int32_t flags, int32_t a7,
                          int32_t uid);

/* 0x0043D330, one caller -- the roach's per-frame step, in its state 4. Step
 * ADDR_ROACH_REACH along the facing, play a sound there, and damage every
 * object in ADDR_ROACH_BITE_BOX around that point which is not on the roach's
 * side. The name is descriptive; nothing in the image names this function. */
void __cdecl RoachBite(void *roach);

/* 0x00448280, eight callers. When a weapon leaves the map in a multiplayer
 * game the HOST puts another one back where it was, once. Four gates, the
 * second of which -- an ADDR_MP_SESSION -- no drive here can satisfy. */
void __cdecl WeaponRespawn(void *obj);

/* 0x00417930, one caller -- a cheat's effect. Twenty-five armed enemies appear
 * at random points inside the visible view, each with an explosion where it
 * lands, all facing ADDR_LISTENER_POS. */
void __cdecl PortalSpawn(void);

/* 0x0045A620 and 0x0045A770, six and seven callers -- the GENERAL footprint
 * pair, of which the roach pair is the special case. Lay an object's mask over
 * ADDR_CELL_WEIGHTS, or take it off, using the vehicle mask record
 * [kind * 32 + dir]. Gated on OBJ_FLAG_FOOTPRINT_ON each way round. */
void __cdecl ObjSetFootprint(void *obj);
void __cdecl ObjClearFootprint(void *obj);

void __cdecl ObjClearRoachFootprint(void *obj);

int32_t __cdecl RoachMaskWeight(void *from, int32_t dir, uint32_t at,
                                int32_t unused);

/* 0x0045EED0, eight callers. Is this object an ITEM whose OBJ_OFF_FIELD_94
 * record carries the type id in ADDR_CREATE_WATCHED_KIND? */
int32_t __cdecl ObjIsWatchedKind(const void *obj);

/* 0x0045B700, two callers. Does `from` run into `obj`? Height first, then a
 * flag-and-vehicle-kind shortcut, then the type 2/3/8 rules -- of which the
 * one worth knowing is that a vehicle the PLAYER is driving stops for
 * friendly troops and one the AI is driving does not. See item.cpp. */
int32_t __cdecl ObjCollidesWith(void *from, void *obj);

/* 0x0043BBE0, one caller. What a shot does to what it hit: a thirty-arm jump
 * table over the shot's TYPEREC_OFF_CODE scales its TYPEREC_OFF_DAMAGE, and
 * then the shooter turns on the target if the two are not allied. Two of its
 * five arguments are never read. See item.cpp. */
/* 0x0045B4D0, one caller -- DamageObject's type-3 arm. Take `amount` off a
 * vehicle, and if that empties it, empty the vehicle too. Armour is a
 * THRESHOLD with a damage-proportional chance of one point getting through. */
/* 0x004498F0, one caller. Move a trooper on to its next inventory slot,
 * wrapping to 0, on the weapon-switch action key or on the middle mouse button
 * being RELEASED. */
void __cdecl NextInventorySlot(void *obj);

void __cdecl DamageVehicle(void *obj, int32_t amount, int32_t d, int32_t kind,
                           uint32_t attacker);

void __cdecl ApplyShotDamage(void *target, void *shot, int32_t unusedA,
                             int32_t unusedB, int32_t doubled);

/* 0x0043C000, one caller -- the type-5 stepper, type 5 being the SHOT. Damage
 * everything at a point that this shot can hit, then ask the terrain whether
 * it stops there. Answers one of AM2_SHOT_STRUCK_*. */
int32_t __cdecl ShotStrike(void *shot, uint32_t at, int32_t height);

/* 0x0045B7E0. The third variant: the same walk with ObjBlockWeight inlined, no
 * height step, and one extra arm -- a trooper blocks only if it is an enemy,
 * and not even then for the unit the player is driving. */
int32_t __cdecl BlockWeightTroops(void *from, uint32_t at, void *chain,
                                  uint32_t ref);

/* 0x0045BBB0. Sum the block weight over a vehicle mask's points for a heading.
 * Kind 5 uses BlockWeightChain and every other kind BlockWeightTroops. */
int32_t __cdecl MaskBlockWeight(int32_t kind, int32_t heading, uint32_t at);

/* 0x0042A820. The ground height at a point, raised by any ITEM standing on
 * it. A byte return -- see item.cpp. */
uint8_t __cdecl HeightAtPoint(uint32_t packedPoint);

/* 0x0042A1B0. The mouse pick: every object in a cell whose own hit rectangle
 * contains the point, chained through OBJ_OFF_QUERY_NEXT. */
void *__cdecl ObjectsHitByPoint(const uint32_t *pt, const void *desc);

/* 0x0042A550, fifteen callers. Its sibling, asking a LOOSER question of the
 * same cell: the hit rectangle, then one of three further tests chosen by the
 * object's flags and whether it has a bitmask at all. */
void *__cdecl ObjectsAtPoint(const uint32_t *pt, const void *desc);

/* 0x004294C0, fifteen callers. Recompute an object's tile from its position
 * and, if anything moved, put it back on the map. */
void __cdecl ObjTileChanged(void *obj, int32_t height, int32_t force);

/* 0x0045B630. A vehicle has died. Its second argument is unused; see
 * item.cpp. */
void __cdecl VehicleDied(void *obj, uint32_t by);

/* 0x00447E50. A trooper has died; what it leaves belongs to the KILLER. */
void __cdecl TrooperDied(void *obj, int32_t a, uint32_t by);

/* 0x004278E0. Give an object a height and push it into the depth sort. A ZERO
 * height means "take the tile's own"; see item.cpp. */
void __cdecl ApplyObjHeight(void *obj, int32_t height);

/* 0x00428F80. Move an object to a point, taking every one of its rows with
 * it. The secondary rows are offset by the first sprite's attach point. */
void __cdecl PointActionC(void *obj, uint32_t point);

/* 0x00435550. Make a kind-7 object, or refuse a thirty-third. */
void *__cdecl MakeKind7(uint32_t pt, int32_t unused, int32_t army,
                        int32_t facing, int32_t e, int32_t f);

/* 0x00459FB0. A uid to a unit -- types 2, 3 and 8 only. */
void *__cdecl UnitByUid(uint32_t uid);

/* 0x004355D0, one caller. A second deadline on the mission clock, at +0x58:
 * once the clock is PAST it, bit 1 of the object's flags goes on. Unsigned
 * compare, and nothing clears the bit here. Both names are ours. */
void __cdecl ObjMarkIfOverdue(void *obj);

/* 0x00429590, 24 callers. How high an object stands: the byte at +0x65 is an
 * absolute floor when it is non-zero and otherwise the tile's own attribute
 * byte is used; either way the SIGNED byte at +0x64 is added. So +0x64 is an
 * offset and +0x65 an override -- neither name is the program's, and the
 * override is read unsigned where the offset is read signed. */
int32_t __cdecl ObjHeight(const void *obj);

/* 0x00429CE0, seven callers. A plain cdecl forwarder for 0x0042A0A0, which is
 * still original -- both arguments go straight through. */
void __cdecl ItemPreDestroyAlias(void *obj, int32_t arg);

/* 0x0041D3A0. One row's teardown: unregister it from the map's cell lists and
 * free the buffer it owns, both gated on the single flag at +0x34. */
void __cdecl RowRelease(void *row, void *desc);

/* 0x00428DA0, 22 callers. Destroy an object by type, then broadcast. */
void __cdecl DestroyByType(void *obj);

/* 0x00428C40. Free every item past its deadline; runs on leaving a level. */
void __cdecl FreeOverdueItems(void);

/* 0x00429320. The shared tail of every per-type destroy. */
void __cdecl DestroyObjCommon(void *obj);

/* 0x00449460 and 0x0045A9C0. DestroyByType's type-2 and type-3 arms. */
void __cdecl DestroyType2(void *obj);
void __cdecl DestroyType3(void *obj);
void __cdecl DestroyType8(void *obj);

/* 0x00428E00. Step every row of an object's row list. */
void __cdecl StepObjRows(void *obj);

void item_install(void);

#ifdef __cplusplus
}
#endif

/* 0x004285F0. Destroy one item, dispatching on its kind at +0. `unlink` takes
 * it out of the item list first, and a failed unlink aborts and returns 0. An
 * unknown kind returns 1 having done nothing. Two callers. */
int32_t __cdecl FreeItem(void *item, int32_t unlink);

/* 0x00429450. The object registry's teardown -- FreeItem every entry with
 * `unlink` ZERO, which is what keeps the forward walk safe, then free the
 * array and clear the record. */
void __cdecl ItemsReset(void);

/* 0x00434EC0. Release an object's sub-list: every row's own teardown, then
 * the array, then the capacity -- which is cleared unconditionally where the
 * array is freed only when there is one. */
void __cdecl FreeSubrecordRows(void *subrecord);

/* 0x00429B60, one caller. Give the object a new hit box -- four offsets from
 * its own position -- and put it back on the map under it: unlink, write both
 * views of the box, grow and re-initialise the cell entries, relink. The item
 * counterpart of maprow.cpp's RowAlloc. */
void __cdecl ItemSetBox(void *obj, int32_t left, int32_t top,
                        int32_t right, int32_t bottom);

/* 0x00429F40, two callers. The other half of ItemPreDestroy: link the object
 * into every cell list its OBJ_OFF_HIT_RECT covers, and clear the entries it
 * did not need. It does NOT unlink first. See the definition. */
void __cdecl ItemLinkCells(void *obj, void *cells);

/* 0x0042A0A0. Unlink an object from every cell list it is registered in.
 * Each unlink writes -1 back, so a second call returns on the first entry --
 * and entry zero's index is tested BEFORE the loop as well as inside it. */
void __cdecl ItemPreDestroy(void *obj, int32_t cells);

/* 0x0045EE80. A weapon by uid. Null for a zero uid or one that resolves to
 * nothing, and null WITH a log line for one that resolves to a non-weapon. */
void *__cdecl WeaponByUid(uint32_t uid);

/* 0x00448880. The code of the weapon in a unit's selected inventory slot, or 0
 * when the slot is empty or does not hold a weapon. */
int32_t __cdecl HeldWeaponCode(void *unit);

/* 0x0044BAF0. A unit's class name: "Sarge" when OBJ_OFF_SARGE is set, else
 * the name for its weapon's code, else entry 0. */
const char *__cdecl UnitClassName(void *unit);

/* 0x004337C0. Can this item be picked up: a weapon, past its
 * OBJ_OFF_PICKUP_AFTER, with a code of 0x1F, 0x20 or 0x21. */
int32_t __cdecl CanPickUp(void *obj);

/* 0x00449200. Put an object into state 5 and give up its alternate table
 * record; idempotent, and what stops ObjsAreAllied choosing REC3 again. */
void __cdecl ObjDropAltRecord(void *obj);

/* 0x00447570. An unused index into ADDR_SOLDIER_NAMES, marked taken. Returns
 * the random starting index unmarked when every name is gone. */
int32_t __cdecl TakeSoldierName(void);

/* 0x00429420. ItemsReset, then seed the first five uid counters to 1000 --
 * the four armies plus the neutral one; owners 5..7 are left alone. */
void __cdecl ResetItemsAndUids(void);

/* 0x00449660. Set a unit's soldier kind from the code of the weapon now in
 * its hands -- the value HeldWeaponCode returns. Code 0 writes nothing. */
void __cdecl SoldierKindForWeapon(void *unit, uint32_t code);

/* 0x00446E70. A weapon handler: record a fire request on the current unit, at
 * a target object or at a bare point. Writes nothing if a menu row is up, or
 * if the unit does not resolve, is not a type 2, or holds no weapon. */
void __cdecl SetWeaponTarget(void *target, uint32_t at);

/* 0x00447950. Set UNIT_OFF_FIRE_MODE to 0x25 when the unit has
 * OBJ_OFF_FIELD_5A4 and its OBJ_OFF_DEADLINE_58 is over fifteen seconds old,
 * and to 1 otherwise. */
void __cdecl PickFireMode(void *obj);

/* 0x0045AAC0. Put a unit aboard a vehicle: OBJ_OFF_RIDING takes the vehicle's
 * uid and the unit's uid goes onto the vehicle's VEHICLE_OFF_PTR_LIST. */
void __cdecl BoardVehicle(uint32_t uid, void *vehicle);

/* 0x0045AA00, three callers. Put a unit into a vehicle, all the way: the seat
 * check, the two fields BoardVehicle writes, Sarge's claim on seat zero, the
 * selection moving from the unit to the vehicle, the broadcast, and then the
 * unit's own destruction. See item.cpp -- what rides in a vehicle is a uid in
 * a list and not a live object. */
void __cdecl EnterVehicle(void *vehicle, void *unit);

/* 0x00427BA0. Deselect everything: clear OBJ_FLAG_SELECTED on each selected
 * object that still resolves, drop the ones that do not, empty the list and
 * report the change. */
void __cdecl DeselectAll(void);

/* 0x0040D930. Put a unit into a pose. Idempotent; nine poses queue rather
 * than switch, and most wait for the current animation's last cell. */
void __cdecl SetUnitPose(void *obj, int32_t pose);

/* 0x00448220. A unit gives up: soldier kind 8, AI mode 2 (ignore), and its
 * weapon marked OBJ_FLAG_OVERDUE before the uid is cleared -- in that order,
 * or the weapon would leak. The name is orig.h's and deliberately neutral. */
void __cdecl Type2ActionB(void *obj);

/* 0x004480E0. Type2ActionB's sibling: soldier kind 6, and a SELECTION
 * handover -- deselect, and if that emptied the player's selection, select
 * the player's own object instead. `prev` is incremented before storing. */
void __cdecl Type2ActionC(void *obj, int32_t prev);

/* 0x00448170. The third sibling: soldier kind 7, the old weapon abandoned as
 * OBJ_FLAG_OVERDUE, and a NEW one created and handed over. Refuses when
 * ADDR_TYPE2_FIELD5A4_SET says so, which B and C do not check. */
void __cdecl Type2ActionA(void *obj);

/* 0x00428450. An object died: health zeroed FIRST, then a per-type handler
 * for types 2 and 3 only, then the common tail that always runs. A slot below
 * zero gives a NULL attacker rather than an error. */
void __cdecl ObjDie(void *obj, int32_t kind, uint32_t by);

/* 0x004351C0. Changes an object's frame and every object chained to it.
 * Returns whether ANY changed -- but a broken chain returns 0 even when the
 * first one did; see the body. */
int32_t __cdecl ChangeObjectFrame(void *obj, int32_t frame, int32_t flag);

/* 0x00457CD0. Awards experience and promotes -- and LOOPS, so one award can
 * carry a unit through more than one rank. */
void __cdecl Type238Action(void *obj, int32_t award);

/* 0x004284D0. Copies OBJ_OFF_POS aside into OBJ_OFF_PREV_POS -- first, and
 * ahead of every guard -- then dispatches to the object's type stepper. Types
 * 1 and 4 share one; type 2 is the only arm with no destroyed check. */
void __cdecl ObjFrameStep(void *obj);

/* 0x004582F0. Points an object at a place: detach, clear the script id, and
 * store the point ADDR_NEAREST_ALLOWED_TILE snaps it to -- NOT the point
 * passed in. Types 2, 3 and 8 only. */
void __cdecl PointActionA(void *obj, uint32_t point);

/* 0x00429C80. Release an item object's allocation. Idempotent: the byte at
 * +0x8C is both the guard and the record. `notify` gates a call that runs
 * BEFORE the free. Five callers. */
void __cdecl DestroyItemObject(void *obj, int32_t arg, int32_t notify);

/* Original: 0x004478C0, "DestroyTrooper %x" -- and it is the KIND 2 arm of
 * FreeItem's switch, which is why orig.h calls the address
 * ADDR_FREE_ITEM_KIND2. Kind 2 is the trooper, so the two names agree.
 *
 * Take one trooper down: mark its
 * weapon dead, free the allocation at 0x00AC, free the subrecord's rows, hand
 * the object itself to DestroyItemObject, and free it.
 *
 * The weapon is reached by uid through WeaponByUid, which complains and
 * answers null for anything that is not kind 4 -- so a trooper holding
 * something that is not a weapon leaves that step undone and carries on. Both
 * the uid being zero and the lookup failing land on the same path.
 *
 * The flag is set with an 8-bit OR on a 32-bit load and stored back as 32
 * bits, which for bit 1 is the same thing.
 *
 * Note the object is freed here AND handed to DestroyItemObject, which frees
 * its 0x0090 allocation and clears the live byte -- so the order matters and
 * is reproduced: DestroyItemObject first, then the object. */
void __cdecl DestroyTrooper(void *trooper, int32_t unlink);

/* Original: 0x0045B470, the KIND 3 arm, and a near-twin of DestroyTrooper.
 * Three differences and all three are the original's:
 *
 *  - the weapon uid is at 0x0550, not the trooper's 0x054C;
 *  - there is no log at all, where the trooper has one behind the verbosity
 *    flag and the weapon has one in front of it;
 *  - and it empties a pointer list at 0x0538 first, which the other two arms
 *    have nothing corresponding to.
 *
 * The weapon flag is set with a 32-bit OR here and with an 8-bit OR on a
 * 32-bit load in the trooper. For bit 1 the two are the same thing; the
 * difference is the compiler's, from the same source written twice.
 *
 * The name is ours: this arm carries no string, and kind 3 is the vehicle
 * because ReceiveArmyMsg's switch sends kind 3 to the vehicle handler. */
void __cdecl DestroyVehicle(void *vehicle, int32_t unlink);

/* Original: 0x0045F290, the KIND 4 arm, "DestroyWeapon, %x" -- its own name.
 * The shortest of the three: no weapon of its own to mark, no list to empty,
 * just the subrecord rows, DestroyItemObject and the free.
 *
 * Its log is NOT gated on the comm object's verbosity where the trooper's is.
 * Both go to ADDR_LOG, which this build stubs to a single `ret`. */
void __cdecl DestroyWeapon(void *weapon, int32_t unlink);

/* Original: 0x0043BBB0, the arm kinds 1, 5, 6 and 8 share -- and the BARE
 * version of the family: free the subrecord's rows, hand the object to
 * DestroyItemObject, free the object. Every other arm is this plus something.
 *
 * Forty-eight bytes, and four kinds reach it, which is why the family's shared
 * tail is visible at all. */
void __cdecl DestroyItemCommon(void *item, int32_t unlink);

/* Original: 0x004355F0, the KIND 7 arm: the bare one plus a population
 * decrement, clamped at zero.
 *
 * The clamp is not defensive tidying -- 0x00435550 refuses to create a
 * thirty-third kind-7 object, so the counter is bounded at both ends by
 * design. The name is ours; what a kind-7 object is has not been established,
 * beyond its being 0x94 bytes and limited to 32 live. */
void __cdecl DestroyKind7(void *item, int32_t unlink);


/* Original: 0x0045F2D0, one caller -- the sarge panel's update. Has this
 * weapon recharged? `clock - ITEM_OFF_LAST_USE` against the type record's
 * ITEMTYPE_OFF_COOLDOWN. It is what makes that panel's selected-slot flag 2
 * rather than 1. */
int32_t __cdecl ItemIsReady(const void *item);

/* Original: 0x004600E0, three instructions: one bounds-free read of
 * ADDR_ITEM_TYPE_NAMES. The sarge panel's tooltip is its only caller. */
const char *__cdecl ItemTypeName(uint32_t kind);

/* 0x004475C0, two callers, both in the HUD. Copy a type 2's personal name out
 * of the soldier-name table, or leave the buffer empty. */
void __cdecl SoldierNameOf(char *out, const void *obj);

/* 0x00417B10, one caller. Award 300 experience to every live type 2 the
 * player's own army owns, dropping stale uids on the way past. */
void __cdecl AwardOwnArmyXp(void);

/* 0x0042AAE0, one caller. Classify a weapon uid; 0 for anything that does not
 * resolve to a type 4 with a kind in 2..5. */
int32_t __cdecl WeaponClassOf(uint32_t uid);

/* 0x00435650, one caller -- DamageItem itself. Damage an item and then every
 * item in the chain hanging off it. */
void __cdecl DamageItemChain(void *obj, int32_t amount, int32_t d,
                             int32_t kind, uint32_t attacker);

/* 0x0044A3C0, one caller. The negated overlay Y of the sprite the object's
 * first row is showing. */
int32_t __cdecl ObjOverlayY(const void *obj);

/* 0x00456E20, one caller. Split a slot number into a band code, an index
 * within the band, and a heading byte. */
void __cdecl SlotBandHeading(int32_t slot, int32_t *band, int32_t *index,
                             uint8_t *heading);

/* 0x004069B0, one caller. Select the inventory slot whose weapon has the
 * highest map code, and apply the soldier kind that goes with it. */
void __cdecl SelectBestWeapon(void *unit);

/* 0x0043CD40, two callers. Move an object to a new field-530 state and put its
 * first row on the frame that goes with it, when the animation allows. */
void __cdecl SetObjField530(void *obj, int32_t state);

/* 0x00449F40, three callers. Wobble a facing by -2..+2, keeping the wobble
 * only when it stays in the same direction bucket. */
uint8_t __cdecl JitterFacing(void *obj, uint8_t facing);

/* 0x00459FE0, two callers. The weapon object a unit or vehicle is holding. */
void *__cdecl HeldWeaponObj(const void *obj);

/* 0x0045A030, reached by address from the army walker. Hand a unit to the
 * AI: stop it firing if alive, and give Sarge stance 6. */
void __cdecl ObjToAI(void *obj);

/* 0x0045B000, three callers. Set the soldier kind and put the first two rows
 * on the frames that go with it, skipping any row mid-animation. */
void __cdecl SetKindFrames(void *obj, int32_t kind);

/* 0x00417890, one caller -- the "Duck and cover!" cheat. Two hundred spawns at
 * random points across the view, six kinds, each with a random delay. */
void __cdecl SpawnRandomBarrage(void);

/* 0x00458380, four callers. Select one object if it is ours and selectable,
 * clearing the existing selection first unless CONTROL is held. */
int32_t __cdecl SelectIfOwn(void *obj);

/* 0x004572A0, two callers. Clear a type 2's working block and stamp its
 * facing into the copy at +0xF8. */
void __cdecl ResetType2Fields(void *obj);

/* 0x00457220, three callers. Reset an object's hit record and detach it -- all
 * of it behind a data file that does not ship, so it never runs here. */
void __cdecl ResetObjOnCof(void *obj);

/* 0x00406AB0, SelectBestWeapon's twin: scored by WeaponRank, and it writes the
 * slot only when the winner is not slot 0. */
void __cdecl SelectRankedWeapon(void *unit);

/* 0x00435440, one caller. Is a point on a solid pixel of the object's FIRST
 * row? Rectangle first, then the sprite's own mask.
 *
 * The point is a `const void *` here rather than an AM2_Point, because this
 * header is included by translation units that do not pull in rect.h and
 * adding that include for one declaration would widen what item.h drags in.
 * The definition casts it back. */
int32_t __cdecl ObjRowsMaskAt(void *obj, const void *pt);

/* 0x004276F0, one caller. The savegame uid fixup: put every type 2's six
 * inventory uids through the remap table. */
void __cdecl RemapInventoryUids(void);

/* 0x00413710, one caller. Add an object to the selection or take it out;
 * CONTROL adds rather than replaces, and the last one cannot be removed. */
void __cdecl ToggleSelect(void *obj);

/* 0x00457A60, three callers. Point the object-context globals at one object
 * and set the pointer mode from what it is. */
/* 0x004579C0, two callers. Spiral out from a point until one is both inside
 * the map and passable, and write it back. It has no give-up: the only exit is
 * success. `out` is an AM2_Point, taken as void * for the reason given at
 * ObjRowsMaskAt above. */
void __cdecl NearestClearPoint(uint32_t from, void *out);

/* 0x0045B930, one caller. The same spiral asking a different question: walk out
 * until THAT VEHICLE fits at the point facing that way. `outPt` is an
 * AM2_Point, and it has no give-up either. */
void __cdecl NearestClearVehiclePoint(void *veh, int32_t facing,
                                      uint32_t from, void *outPt);

void __cdecl SetObjContext(void *obj);

/* 0x0042A110, two callers. ObjectsHitByPoint with a caller-supplied
 * predicate, which runs BETWEEN the rectangle test and the mask test. */
void *__cdecl WalkCellAtPoint(const uint32_t *pt, const void *desc,
                              int32_t (__cdecl *keep)(void *obj));

/* 0x004499A0, one caller. Is the object's first row on a frame from which its
 * weapon may act? Three answers -- see the definition. */
int32_t __cdecl WeaponFrameReady(void *obj, void *weapon);

/* 0x0044BA70, one caller. Set an object's kind, both table-record pointers --
 * one by kind, one by the owner's comm slot -- propagate one, refresh rows. */
void __cdecl SetObjTablePair(uint32_t uid, int32_t kind);

/* 0x00406800, one caller. Which inventory slot should take this weapon, and
 * may it be taken at all? The slot is an out-parameter carrying -1 for "no
 * slot needed" and -2 for "all six full"; the permission is the return. */
int32_t __cdecl PickWeaponSlot(void *cand, void *unit, int32_t *slot);

/* 0x00406720, two callers. Should this unit take this weapon, dropping its
 * least valuable one if the inventory is full? Answers the weapon's thing
 * code, or 0 for a refusal. */
int32_t __cdecl TryTakeWeapon(void *cand, void *unit);

/* 0x004578A0, one caller. Call a function for every selected object, dropping
 * stale and destroyed entries as it goes. */
void __cdecl ForEachSelected(void (__cdecl *fn)(void *obj));

#endif
