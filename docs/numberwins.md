# Number wins: source kept only because it lowers the relink distance

To review later (Boran, 2026-10-18). These are edits made to bring a function nearer
the original's bytes that do NOT recover the original's source. They are
behaviour-neutral, and each one is marked in the source with the words "a number win"
(or, for the impossible pairs, "impossible pair"). List them with:

    grep -rn "number win\|impossible pair" src

The kinds, and why each works (all measured; the mechanism notes are in
`docs/bytematching.md`):

- **Dead volatile reads**, `(void)*(volatile T *)&x;` of a value the function already
  reads. One load, and it supplies a register-rotation temporary or takes a local's address.
- **Impossible conditions**, `|| (v == 1000 && v == 1001)`. They fold away and raise `v`'s
  pre-CSE use count, which orders callee-saved registers. Identical pairs are merged, so
  the constants differ per pair.
- **Self-referential ternaries**, `pos = klass ? pos : &o->pos;` and
  `sp = o ? &rows->sprite : &rows->sprite;`, a second reaching definition so VC6 cannot
  propagate a pointer away.
- **Volatile stores and locals**, `*(volatile int8_t *)&r->numb = ...`,
  `d1 = ApproxDist(...)` through a volatile local, a volatile pointer store in a written-out
  DEFAULT_LAYER.
- **Frame struct sharing** `struct { int32_t d1; AM2_Point lp; } fs;` in BlockWeightRoute replaces the old volatile `d1`; a second load `y = f.world.top` in ListMaskAction; a pair on `i` in UpdateTrooperAction; revisit.
- **Two impossible pairs on `drop`** in DrawSelection's `mh / 2 < wide` test (the `hp` injection it replaced is gone); revisit.
- **Pairs on `anim->cells` / `r->sprite`** (StepRowAnim) and **a pair inside a call-argument ternary** (ScriptParseEvent) make a load a web instead of a rotation temporary; revisit.
- **Pairs on `o->facing`** (two, in TrooperFire's conditions) and **on `f.world.top`** (ListMaskAction) resync the rotation cursor; revisit.
- **A pair in a call-argument ternary** in ScriptParseActionRecon's setspeed log (resyncs the rotation cursor); revisit.
- **Plain-click path first, with its own tail copy** in SelectionClick (arm order decides the rotation walk); revisit.
- **Volatile increments** of `*nOther` and `o->sightHits` in SightScan's score block (52 -> 38); revisit.
- **`n` stepped through a volatile read into a temporary and a volatile store** in ScriptPad's first scan loop (replaces the do-while with a difference latch; 158 -> 84); revisit.
- **Byte-restricted return** `return (int8_t)w == 0 ? w : w;` in the TroopBlockWeight inline (BlockWeightTroops 30 -> 0); revisit.
- **A pair on `obj->uid`** inside SQUAD_BAR's clamp test (HudSquadUpdate, 20 -> 0; replaces the `hp < X + 1` spelling) (SightScan's pair of `o->pos` reads was removed again in the next round); revisit.
- **ObjRemap dead reads** of `y` and `o->pos.y` (124 -> 108); revisit.
- **Volatile test then plain read** of `g_dynamic[i]->buffer` in Update3DAudioVolumes (replaces the self-referential ternary; 12 -> 2); revisit.
- **Dead read after a return**, `(void)*(volatile int32_t *)&g_menuRow;` in TickMenuAnimation (DrawMenuCursor 30 -> 0); revisit.
- **Negated tests**, `(-r->which) ? a : b` in OptionsDefaults, which is `x != 0` with a real `neg` the image does not have; revisit.
- **Difference tests**, `a - b` for `a != b` (and `x - y >= 0`), which change which scratch
  register a compare uses.
- **Frame shaping**: local structs/unions in the image's slot order, an array for an extra
  frame slot, a named `bias` local defined inside a loop plus a redundant guard.
- **Unused labels and flattened tails** that keep VC6 from merging identical return blocks.

None of these should change what the program does. Before removing one, run
`tools/relinkscreen.py` to see what it costs. Each comes with the distance it
bought in the commit that added it (`git log -S"number win"`).

## What they probably stand in for

Boran's reading (2026-10-18): the original source referred to conditions or reads that
the compiler optimized away. The measurements agree that the original's variables had
more uses, at the point where VC6 ranks registers, than our source gives them; they do
not say what those uses were, and the constants in the pairs are arbitrary. Candidates:
debug-only code compiled out of the release build (asserts, traces, consistency checks),
conditions the compiler proved constant from earlier code, inlined helpers whose result
was unused, macro expansions that re-tested a value, and leftovers from edits.

Where several pairs were needed (DrawMapObjects: four on `bottom`; BlockWeightTroops:
three on `o`), one stripped macro could account for all of them. Treat each stand-in as a
marker that the original touched that variable at that point in some way not yet found.

## When to revisit

Boran (2026-10-18): the stand-ins are fine for now; revisit them once the binary matches 100%.
That is the point where each can be traded against a real source shape without losing a
match: replace one, run `tools/relinkscreen.py`, keep whichever form keeps the function at 0.

## Relink-only symbols (not number wins, but per-build seams)

`am2_rl_army_points` in `MpPanelScreen` (relink builds only; the other builds read `g_armyPoints`).
Same storage, same behaviour; it exists because the original's loop reads the array as a lone global.

## Volatile read into a temporary, then a plain use (Round AW)

`{ int32_t t = *(volatile int32_t *)&g_levelIndex; g_levelIndex = t + 1; }` in MissionEnded
(12 -> 0, replacing the volatile store of `g_attemptCount`) and
`{ uint32_t t = *(volatile uint32_t *)&flags; mask = t; }` in OptionsDefaults (10 -> 4). The
direct volatile forms (`*(volatile T *)&X += 1`) give 62. Revisit.

ObjRemap: `{ int32_t t = *(volatile int32_t *)&o->pos.x; x = (int16_t)t; }` and `{ int16_t t = *(volatile int16_t *)&o->pos.y; y = t; }` (96 -> 92). FindPath no longer carries any impossible pair. Revisit.

AddToItemList (objtable.cpp): `if (owner > 7) break;` after the counter store, where `owner = id & 7`; never taken, keeps `owner` live past the load. Revisit.

HudPanel ctor no longer carries the dead read of `self->open`; its other dead read (`w->x`) remains. The `w->WidgetAddChild(x)` member-call spelling in MpPanelScreen and HudPanel is not a number win: it is the same call as the wrapper.

HudPanel ctor carries no number win any more (its `w->x` dead read is gone).

Update3DAudioVolumes and LoadMap's `numb` store no longer carry number wins (the volatile test and the volatile store are gone).

FindPath: `dd <<= 1; if (dd == 70000 && dd == 70001) continue;` (a never-true pair on `dd`; part of a three-edit unit, 66 -> 54). Revisit.

ObjHitMaskAction and ListMaskAction: never-true pairs on the product `((y >> shift) - box.top) * (box.right - box.left + 1)` ahead of the `f.end` store; ScriptParseEvent: the pair is on `(intptr_t)ctx->tokens`. Revisit.

ObjRemap was rewritten: two never-true pairs in the flag test (`force == 1000 && force == 1001`, `d->shift == 1000 && d->shift == 1001`) and one volatile dword read of `o->pos.x`; the older dead reads are gone. Revisit.

SightScan: `kp = flags ? &keep->pos : &keep->pos` as an if/else in the type-4 weapon arm (two reaching definitions; 38 -> 26). Revisit.

TrooperFire, UpdateTrooperAction, OptionsDefaults, ScriptPad (Round BP): never-true pairs `(F == K && F == K+1)` on a byte field of a register-held pointer (`o->facing`, `box->forcePlain`) or on the int32 `box->disabled`, nested as `(A && !P) || P2`, each supplying one rotation-cursor event with no code left behind; the `-r->which` respelling in OptionsDefaults is gone in favour of this. ScriptPad: `sy = (ny == 1000 && ny == 1001) ? ny : ny`. Revisit.

LoadMap (Round BR): the reveal-grid slot pointer is `(uintptr_t)g_tileRevealGrids - ((uintptr_t)g_mapTilesW - (uintptr_t)g_mapTilesW)`; a pointer built from an integer expression with a runtime read loses its provenance, so c2 keeps the store through `slot` ordered before the map-size reload (12 -> 2; replaces the old volatile pointer store). Revisit.

FindPath (Round BW): `PathDistXYT(cur & 0xFFFF, curTile, t, t)` takes one tile under two spellings; `nb = (cur & 0xFFFF) + *sp`; the flag byte through `const uint8_t *fl = g_tileFlags`. Revisit.

LoadMap TRIG `size - (uint32_t)plane`: withdrawn (2026-10-22); the check is a plain `!=` again.
