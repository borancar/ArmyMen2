# How VC6 assigns registers

What the Visual C++ 6.0 code generator (C2.DLL, build 8168, `/O2 /GX`) does
when it picks registers, worked out empirically for the byte-match relink.
Every rule below comes with the experiment that shows it; rerun any of them
with `tools/vc6asm.py FILE.cpp` (compile a snippet, print each function's
code). Nothing here was read out of C2.DLL's code, so treat the rules as
measured behaviour, not as the algorithm's specification.

## What C2.DLL says about itself

The DLL carries its source file names. The allocator is a graph-colouring
module, and register assignment is a separate pass:

| file | presumably |
|---|---|
| `p2\color.c` | the global allocator: interference graph, colouring |
| `p2\regasg.c` | register assignment |
| `p2\sdsu.c` | Sethi-Ullman ordering of expression evaluation |
| `p2\globopt.c`, `globlopt.c`, `globdf.c` | global optimisation, dataflow |
| `p2\x86\schedmd.c` | the x86 instruction scheduler |
| `p2\x86\lower.c`, `addr.c`, `code.c` | lowering, addressing modes, emission |

No dump or trace switch was found among its strings. It does accept hidden
switches (`-noblend`, `-nodlp`, `-loopopt`, `-p6gj`, ...) that were not
explored.

## The model

Two layers, and most stuck byte-matches are a disagreement in one of them:

1. **Global allocation** colours *webs* (live ranges) of locals, parameters
   and compiler temporaries that span more than a few instructions.
2. **Local assignment** gives short-lived expression temporaries a scratch
   register from a rotation.

### Global allocation

- **The unit is the web, not the variable.** `z = g(z)` splits `z` into two
  webs that are allocated independently; a variable assigned once is one web.
  A plain local copy (`T c = x;`) is propagated away and allocates nothing.
- **Priority is a weighted use count.** Every read and write counts
  `2^depth`, where `depth` is the number of loops around it. One use inside
  one loop is worth 2, inside two nested loops 4, inside three 8. Measured
  by pitting a value with `k` extra straight-line uses against one with a
  single use in the loop: the loop value wins up to `k = 1, 3, 7` for depth
  1, 2, 3, and loses at `k = 2, 4, 8`.
- **Uses are counted before common subexpressions are merged.** Writing
  `angle + sector` twice (tested, then formed in each arm) gives `sector`
  two uses in the loop although the code computes the sum once. That was
  enough in DiedEffectB to give `sector` ebx over `height`, which then is
  read from its argument slot at each push as the image has it (204 -> 38
  bytes, 2026-10-10). So a value that loses a register it should win may be
  written fewer times in our source than in the original, even where the
  generated arithmetic is the same.
- **Ties go to the web that appears first in the generated instruction
  stream**, not in the source and not in declaration order. Call arguments
  are pushed right to left, so the LAST parameter of a call appears first
  and wins a tie (`h(a, b, c)` in a loop: `c` gets the better register).
  Declaration order was swept over 85 relink misses and changed nothing.
- **Webs live across a call need a callee-saved register**, handed out as
  `esi`, `edi`, `ebx`, `ebp` in priority order. A fifth such web spills: a
  local to a frame slot, a parameter by reading its argument slot at each
  use. The prologue always pushes in the fixed order `ebx`, `ebp`, `esi`,
  `edi`, whatever the assignment.
- **Webs not live across a call prefer the scratch registers** `eax`, `ecx`,
  `edx` in priority order; `eax` is held back for a value that becomes the
  return value. Only when those are taken do they move to `esi`, `edi`.

### Local assignment (the rotation)

- **Temporaries take scratch registers in rotation**, `eax` -> `ecx` ->
  `edx` -> `eax`, skipping busy ones, in the order of the final instruction
  stream. A register just freed is not reused for the next temporary; the
  next one in the cycle is. Six back-to-back copies use
  `eax ecx edx eax ecx edx`.
- **The rotation runs straight through calls and branches.** After a call
  the next temporary takes the register after the last one used before it;
  inside an `if` the rotation continues from the code before it.
- **Consequence: one extra or missing temporary early in a function shifts
  every later choice by one.** A stuck function whose differing lines are
  `eax`/`ecx`/`edx` swapped consistently from some point on has a temporary
  more or fewer than the image before that point.
- **Callee-saved registers can serve as temporaries** once the function has
  saved them for something else. A by-value struct argument is copied with
  `ebx` as the destination pointer and `ebp` as the copy temporary, so any
  function that passes a `RECT` by value has `ebp` "available", and a
  temporary can land there.

### Constraints that redirect registers

- **Byte operations need `al`, `bl`, `cl` or `dl`.** `setne` writes its
  result into a byte register, preferring the register of a dying operand.
  If that operand sits in `esi`, `edi` or `ebp`, the result goes elsewhere
  and every later choice shifts. (OptionsDefaults: `bit` in `ebp` forces
  `setne al`.)
- **Loads never move above stores.** VC6 does no alias analysis between a
  store through one pointer and a load through another, or from a global.
  So the image's order of a load relative to a store is the source's order.
  One exception, measured on ScriptIf (2026-10-09): a local whose address
  was passed to a call, but which is declared in an inner block, is read
  before a store through a heap pointer. Declared at function scope, it is
  read after the store. When the image reads a local before a store and we
  read it after, try narrowing the local's scope.
  Loads DO sink toward their use: a value assigned early but used late can
  be loaded just before use, and then takes its register at that point in
  the rotation, not at its source position.
- **A walking pointer is anchored on one field.** When a loop over an array
  of structs is strength-reduced, the induction pointer points at one field
  and the others are reached at fixed offsets from it. Which field is chosen
  depends on the order the fields are referenced: in OptionsDefaults,
  reading `bit` last anchors on `bit`, and reading `which` last anchors on
  `which`. The rule is only partly pinned down.
  Lab, 2026-10-10: in a straight-line loop body the anchor is the field read
  LAST (`g(t[r].widget); g(t[r].last)` anchors on `last`; swap the calls and
  it anchors on `widget`). Once the body opens with a test and `continue`,
  indexing `t[r]` anchors on the first field read after the test. Holding a
  record pointer (`p = &t[r]`, every read through `p`) anchors on the last
  field read again, inner loops included. MpOptionsScreen's group pass
  matched that way.
  Advancing the array pointer itself is a third shape: ParseScenarioPart's
  `for (i = 0; i < count; i++) { row = &rows[i]; ... }` anchored on pos.y,
  six bytes in, whatever order the fields were written in; written
  `for (i = 0; i < count; i++, rows++) { row = rows; ... }` it anchors on
  the row's start as the image does (82 -> 48 bytes).

- **A memory-operand OR means register pressure (lab, 2026-10-10).** VC6
  compiles `*c |= 3` as load, `or`, store for `char`, `signed char`,
  `unsigned char`, a bitfield and `int` alike, through a pointer or an
  index. It emits `or byte ptr [mem], 3` only when no register is free (a
  loop with seven live values does it). So where the image has the
  memory form and we have the three-instruction one, as in ObjHitMaskAction
  and ListMaskAction (both with `marked` spilled to the stack in the image,
  and a frame four bytes larger), the original keeps one more value alive
  across the loop than ours does. Look for a variable we folded away, not
  for another spelling of the store.

### Evaluation order

- **Commutative chains are evaluated right to left**: `p[0] + p[1] + p[2]`
  loads `p[2]` first.
- **Source order of independent statements is often irrelevant**: the
  scheduler reorders them, and the instruction scorer reports the same
  result for every order. When order DOES matter it is through the rotation
  or the anchor rule above.

## Using this on a stuck function

1. Classify the diff. Callee-saved registers swapped between two values
   means a priority difference: count each value's uses with the `2^depth`
   weights, and check which appears first in the instruction stream.
   Scratch registers rotated by one from some point on means a temporary
   more or fewer before it.
2. For a priority swap, change the use counts: hold a value in a local (one
   web, one load) or re-read it (more uses of the source, fewer of the
   local); move a use into or out of a loop; change which value the code
   touches first.
3. For a rotation offset, find the extra or missing temporary: a sub-expression
   the image computes once and we compute twice, or the reverse; a value the
   image keeps in a register that we reload.
4. For a byte-register redirect, look at what feeds the `setne` and why it
   is in a register without a byte form.
5. Before rewriting, check the load/store order against the image: a load
   above a store in the image is above it in the source.

## Measured examples

From `tools/vc6asm.py` runs (2026-10-09). Each line is one variant compiled
on its own.

```
x = g(a); y = g(b); z = g(c); g(0); return x+y+z;      x=esi y=edi z=ebx
same, 4 values                                         esi edi ebx ebp
same, 5 values                                         5th to a frame slot
int z, y, x; x = g(a); y = g(b); z = g(c);             x=esi (definition order)
z defined first                                        z=esi
one extra g(z)                                         z=esi (weight)
for (...) h(a, b, c);                                  c=edi b=ebx a=ebp
H[0]=G[0] ... H[5]=G[5]                                eax ecx edx eax ecx edx
H[0]=G[0]; g(); H[1]=G[1];                             eax, then ecx after the call
p->a = 0; return G[3];                                 store, THEN the load
```

## A parameter copied into a local

Using a parameter through a local copy (`T *c = param;` at the top, `c`
everywhere after) is NOT always propagated away: it changed EvtCondition's
base/index register order (`[eax + ebx]` against `[ebx + eax]`), which the
IL-override probe had shown no operand order in the IL could reach. It is the
`AM2_Object *o = obj;` idiom already common in this codebase, and worth trying
early on any function whose parameter is used many times.

It also moves a parameter's RELOADS. ReceivePlayerMsg's `msg` lives in esi,
which the loop's inline `strcpy` clobbers; read directly, VC6 reloads it at
the loop head, and through `const AM2_PlayerMsg *m = msg;` it reloads at the
bottom, right after the `rep movsd`, as the image does (2 -> 0 with the
indexing below).

## Spill slots follow the source before merging

A spilled local reuses a dead argument's home slot, and which slot it gets is
decided before identical blocks are merged. FormationPoint's `case 2: case 8:`
arm, written as two separate arms that compile to the same merged code, swapped
the slots of its two spilled byte locals (`leaderFacing` and `facing`) into the
image's order. Use counts did not move them in a lab clone; neither did
declaration order, `(void)` uses, or reordering the arms. When two spills trade
slots, try writing a shared `case` label as separate arms, or duplicating some
other block the compiler will merge. (decomp-permuter found this as an unnatural
`if (slot) {...} else {...}` with the same body on both sides.)

## Which copy of a merged tail survives

When two switch arms end in the same code (`case 2: return 6;` in two inner
switches), VC6 keeps the copy from the arm that comes LATER in the source and
points the other arm at it. Arms are laid out in source order, so the
surviving block normally sits in the later arm (lab, 2026-10-09, a
WeaponPoseIndex clone). WeaponPoseIndex, SelectWeapon, ShotHitsObj and
CommSend all have the image keeping the EARLIER copy while arms are still laid
out in source order. Changing the order of arms inside one switch moves
nothing. A fall-through from one arm into the next does put the shared block
where the image has it, but it also moves the arm. The source that gives the
image's result has not been found.

Writing the tail out once per arm does not keep the copies apart either
(2026-10-10). VC6 cross-jumps code that is identical AFTER register
allocation, whatever the source looked like:

- UpdateTrooperAction's stop block, written under each of its three tests
  (as else-if arms, or as separate `if`s each ending in its own `goto`):
  two of the three copies still merge, because our copies use the same zero
  register. 952 -> 980 bytes.
- HudPostUpdate's generic action call, inlined at its `goto`, called through
  a local, an int-returning pointer type, or a split test: identical output,
  still jumps into the "ours" arm's call.
- ScriptPad's two averaging blocks, given block-local counters per arm: the
  `cx` halves separate but the `cy` halves still merge, and the rest of the
  function's registers move (302 -> 365 instructions off).

Where the image keeps copies, they usually differ in a register (ScriptPad
divides by `edi` in one arm and `ebp` in the other; MakeBitmap's two
`Release` calls go through `ecx` and `edx`), so in those cases it is the
register choice that has to be fixed. HudPostUpdate's and UpdateTrooperAction's
copies are byte-identical in the image and still kept apart.

**HudPostUpdate solved (2026-10-10): the shape of the arm merged INTO decides
it.** Our generic action call was cross-jumped into hit_ours' identical call,
which sat inside a braced `if (CHANGED) { ...; goto tail; }`. Written as
`if (!CHANGED) goto hold;` with the changed arm falling through, nothing merges
(308 -> 256 bytes). Found by bisecting the real function: deleting hit_ours'
hold arm also stopped the merge, and spellings of the generic arm itself
(inlined, through a local, other pointer or argument types) never mattered.
In the lab, the same rewrite only moved the merge to another identical
candidate (the f14 call, when it also passed `obj`). So the rule is about
which blocks qualify as cross-jump targets, and every identical candidate has
to be ruled out.

**SelectWeapon matched the same day by ARM ORDER.** Its case 0x1A, written
after 0x23, was cross-jumped into 0x26's identical call. The image lays 0x1A
out between 0x17 and 0x23, so it comes first in the source too; moved there,
it keeps its own block and the function byte-matches. Read the image's block
order before changing anything else: an arm out of place can be swallowed by
a merge it would never take in its own place.

**CommSend: which copy survives follows layout, not case order.** Swapping
the two `case`s changes nothing; VC6 lays out the switch the same way and keeps
the copy that falls into the common code (E_INVALIDARG). An if-chain testing
E_INVALIDARG first makes the image's survivor (DPERR_INVALIDPLAYER) keep the
call, but the losing arm's push-push-jmp stub stays inline, where the image
moves it into the gap after the player-lookup loop. Not solved.

UpdateTrooperAction is still open. A lab clone of its control flow
(have_facing through settle_facing) keeps all copies apart until three things
are present together: `speed` reassigned in the item loop, `turned` read
after it, and `speed` read after it. With all three, VC6 keeps `speed` in
memory and makes `eax` a zero register for the `hit` test, the `found` test
and the `speed = 0` stores, and two copies merge. The image tests with
`test eax, eax` and stores immediates there instead. Its three stop copies merge in the skip
form too (and in a lab clone). The image's copies differ from ours in one
visible way: each one reloads `out` from its argument slot for the `bearing`
store (`mov edx, [esp+0x40]`) while `state` and `hit` go through `ebp`. That
suggests two pointer variables in the original, but a local alias for
`state`/`hit` was propagated away.

**ScriptParseActionRecon (2026-10-10): remove the later copy and the earlier
one survives.** The image keeps the end-of-script log and its `return 0` in
MOVEITEM's place (its `eos:` label, falling into `default:`), and every other
end-of-script path jumps there. Ours kept the copy in the ORDER case, which
wrote its own `{ log; return 0; }` later in the switch. Writing that check as
`STEP()` (`goto eos`) leaves one copy, in the image's place: 2476 -> 2316, and
tools/actcheck.py still finds 0 diffs in 13,122 cases. The jump-table scan
(`$T/armorder.py`: decode both tables, compare the order of the case bodies)
is how it was found: the default's entry pointed somewhere else in each.

Still open there: the image also keeps three separate, byte-identical
`if (++*at >= count) { log; return 0; }` blocks (TRIGGERDELAY,
CREATETROOPER and one more), each falling into its own continuation. VC6
merges such blocks in every lab shape tried (inline, inverted test, an inline
helper), and ours merges two of the three into CREATETROOPER's.

**Epilogue copies block cross-jumping (lab, 2026-10-10).** Under /O2 VC6
copies a small constant `return` epilogue (`xor eax, eax; pop...; ret`)
into an inline predecessor that would otherwise jump to it, and a block
ending in `ret` is never a cross-jump candidate. So two byte-identical
`{ log(...); break; }` blocks that each sit inline after their test stay
apart, while the ones reached by a jump merge and fall into the shared
epilogue. A clone of FlowRecvMessage reproduces our three copies of the
"Can't find FlowQ" log; the image has one, with all four sites pushing
their argument and jumping in, and only one `return 0` epilogue in the
whole function. In the clone, `return g_v` (a non-constant result) or
/O1 stops the copying and all four merge. But commmsg.cpp is /O2
(compiled /O1, every matched function in it breaks), and a result
variable, `return 0` at the sites, nested tests, an if-chain and `goto` to
a label before the return all keep the copies. What stopped VC6 copying the
epilogue in the original is not known.

## Loop and temporary shapes that show in the image

- **A rotated `while (p->next) { ... }` is not `while (1) { if (!p->next) break;
  ... }`.** The first compiles to an entry test plus a bottom test that falls
  into the code after the loop; the second stays top-tested with a `jmp` back.
  DepthInsert's last walk was the second and the image is the first (516 -> 514).
  Its backward walk, rewritten the same way, got much worse (514 -> 588), so
  read each loop's exits in the image rather than converting them all.
- **A byte read used straight in a test chain stays in `al`.** FindPath's
  neighbour region (`test al, al; jbe`, then `and eax, 0xff` before comparing
  with the int32 regions) is `region[t]` read in place each time, not an
  `int32_t` local, which gives a signed `jle` on the widened value, nor a
  `uint8_t` local, which spills it (1368 -> 1358).
- **A walking pointer compared against an address may be VC6's, not the
  source's.** ReceivePlayerMsg's `cmp eax, 0x515FF0; jl` over a pointer
  starting at 0x515FE0 was written as that pointer (`slotSetting`), and its
  initialisation always landed beside `i = 0` instead of beside the other
  strength-reduced pointer (`lea ebx, [esi+0xAC]`). Written as
  `for (i = 0; i < 4; i++) g_armyPoints[i] = ...`, VC6 makes the pointer
  itself, replaces the `i < 4` test with the pointer compare, keeps the
  signed `jl`, and puts the store where the image has it. When an image's
  pointer is initialised together with other induction variables, try
  the index.
- **An array slot indexed twice is not a pointer to it kept across calls.**
  The footprint family (ObjSetFootprint and its three siblings) tested a
  16 x 16 stamp window as `*mark == stamp` and later stored `*mark = stamp`
  through one `uint16_t *mark`. VC6 then reassociates the index into one
  sum, loads the slot into a register and compares it with the stamp in
  memory. The image keeps the two window coordinates apart (`cx`, `cy`),
  loads the stamp first, compares it with the slot in memory and forms the
  `lea` for the store only afterwards. That is `g_mark[cx * 16 + cy] ==
  stamp` at the test and `g_mark[cx * 16 + cy] = stamp` at the store, with
  `cx` and `cy` as locals. With these, three of the four functions match
  outright (40 -> 0 bytes each) and the fourth comes to 2. Locals alone, or
  the indexing alone, changed nothing.

## `for (;;)` and `while (1)` are different loops to VC6

The two compile differently (2026-10-10). VC6 rotates `for (;;) { if (c)
break; ... }`, copying the test to the bottom of the loop, and leaves
`while (1) { if (c) break; ... }` tested at the top with a `jmp` back.
RecvThreadProc's outer loop is the image's `while (1)` (shape 17 -> 2,
bytes 122 -> 16); UpdateTrooperAction's claimed-weapon walk is the image's
`for (;;)`. A sweep of every such loop in the mismatching functions, and of
`while (c)` against `for (; c; )`, found no others.

## A forward goto pulls its target up

`if (x) goto L;` with `L:` at the end of a loop body did NOT keep the block
at the end: VC6 inverted the jump and laid the target out straight after
the test, ahead of the code the source has between them. SightScan's
`if (ObjsAreAllied(o, it, 0)) goto allied;` put the allied arm first; the
image has it last. Written as `if (!ObjsAreAllied(...)) { ... } else
{ allied }`, the arm stays where the source puts it (1324 -> 1048 bytes
off). When the image's block order matches the source text but ours does
not, look for a forward goto over the code in between.

The same pull works FOR you when the image has a return inline with its own
epilogue. Step3Input's mouse arm, `if (!changed) return;` inside
`if (!button) {...} else if (!automatic) return;`, jumped to the shared
epilogue and then jumped again to the cooldown test; the image tests
`changed`, branches FORWARD to the cooldown and falls into a duplicated
`pop...ret`. Written `if (changed) goto cool; return;`, with `cool:` on
the cooldown test, it does the same (96 -> 54 bytes off, with the
`!button` arm tested first as the image lays it out).

A walk that leaves by `goto` has the same trouble. DepthInsert and
DepthResort walked with `while (next) { ...; if (found) goto before; }`
followed by the ran-off-the-end arm and then `before:`; VC6 laid the shared
tail out after the ran-off arm, where the image keeps the ran-off arm, then
the insert-before arm, then the tail. Written as `for (;;) { if (!next) {
ran-off arm; return or goto done; } ...; if (found) break; }` with the
insert-before arm after the loop, both take the image's order (DepthInsert
shape 14 -> 6, DepthResort 16 -> 0).

A two-way choice whose image lays one arm out, JUMPS over the other and
joins on a shared test is the ternary's shape, not an if/else's. VC6 put an
if/else's second arm at the end of PointerActionFollow in either orientation
and threaded the scan's NULL straight to the return. Written as
`leader = owner == g_defaultOwner ? LookupByUID(...) : DeadLeaderScan(owner);`,
with the scan as a static inline returning NULL at its end, the function
matched outright.

A ternary lays its FIRST value out as the fall-through. Step3Drive's clamp
`speed = (c <= t) ? t : c` came out `cmp; jg; t; jmp; c`; the image's `cmp;
jle; c; jmp; t` is `speed = (c > t) ? c : t`, the same function written with
the arms the other way round (shape 110 -> 108).

- **Separate `return 0`s are not one `fail:` label.** PlacementAllowed's
  refusals all went to a `fail: return 0;` written inside the radar arm,
  and the function ended `if (weight >= FULL) goto fail; return <setcc>;`.
  VC6 then laid the setcc tail right after the radar arm and put the
  `return 0` last. Writing every refusal as its own `return 0` and ending
  with `if (weight < FULL) return <setcc>; return 0;` lets VC6 merge the
  returns into the block after the radar arm, with the setcc tail last
  as the image has it (36 -> 0 bytes).

## Shape first, then registers

Ranking misses by a register-masked diff (registers and stack offsets
masked, reg-reg moves and spill loads/stores dropped) separates what the
source decides from what the allocator decides. Fixing the shape first
paid off on 2026-10-10, often with bytes following:

- **An element pointer moves the walking-pointer anchor.** DrawEffectLayer
  read the A marker through `const AM2_Point *ptA = &g_aimPointA[army]`;
  VC6 anchored the per-army pointer on aimPointB.y. Indexed at each use
  (`g_aimPointA[army].x`), it anchors on aimPointA.y as the image does
  (616 -> 584).
- **Store order is source order, mostly.** ViewUpdate's two rect fills and
  the eye position, written in the order the image stores them (and the
  eye's y stored straight from its clamp), went 186 -> 90. ScrollMapCache's
  log rect the same way, 252 -> 236. Where the scheduler reorders anyway
  (MissionEnded, LoadMap), changing the source order did nothing.
- **`if (!a) goto L;` against `if (a) {...} else {L}`** again: FindPath's
  stamp arms (claim last), AiTrooperStep's no-region arm, UpdateTrooperAction's
  sweep limit as an if/else rather than default-then-override.
- **A zero in a register changes stores.** `*c |= 3` through a pointer is a
  load/or/store when a flag set beside it (`marked = 1`) lives in a
  register, and `or byte [c], 3` when it lives in memory, as in
  ObjHitMaskAction's image. That one is the allocator's, not the shape's.

Store order is cheap to search mechanically. Swapping each pair of adjacent
single-line assignments that do not read each other's target, scoring the
register-masked shape and keeping the winners, then running again until
nothing moves, took eleven functions closer in two passes (UpdateTrooperAction
109 -> 101, ListMaskAction 49 -> 44, AiTrooperStep 56 -> 52). Check each winner
by hand for a dependency the filter missed: the first, unfiltered pass
proposed swapping `delta = AngleDelta(...)` with `delta = abs(delta)`.

## Declarations ahead of a function can turn its ties

A type declared earlier in the translation unit can change a function that
never names it. Measured while typing globals (2026-10-10):

- **One more struct in `globaltypes.h` broke CheatLine**, which every TU
  reaches through `globals.h`. `AM2_MousePress` added there swapped edx/eax
  in CheatLine's view-origin adds (OK -> 7 bytes off). Tagged or untagged,
  first or last in the header, the same. In a header only its two readers
  include (`mousepress.h`), CheatLine matches again.
- **A typedef's POSITION in the file mattered to SightScan.** The
  GetTickCount function-pointer typedef in `globaltypes.h` (so, before
  SightScan) cost it 2 bytes (419 -> 421); the identical typedef at
  region.cpp's tick seam, after SightScan, costs nothing. So each seam keeps
  its own `AM2_TickFn` typedef rather than sharing one from a header.

So a change that only adds declarations to a widely included header is not
free: relink and compare every function, not just the ones the change names.

## Not yet understood

- Exactly how the field a walking pointer is anchored on is chosen.
- Why, in OptionsDefaults, the image allocates `bit` before `mask` while
  still loading `which` (through the ternary) first. Every order of its five
  statements, both signednesses and three spellings of the test were tried
  in a clone (`checked` stays 26 lines off the image at best).
- Whether the hidden C2 switches change any of this.
- How VC6 picks a dead argument slot for a local. RunScriptAction's image
  (frame 0x60) keeps several values in its parameters' slots: `v1` and the
  `moveto` point `p1` in `act`'s (act lives in `esi`), and a later variable
  read in `owner`'s. `p1` is never address-taken. Making `v1` or `p1`
  block-scoped moves them within our frame (to 0x5c, or to another frame
  slot), never into an argument slot (2026-10-10).
- What makes VC6 split a pointer's live range around a loop. In
  PointerPickMode0/4/5/6 the image keeps the object in `esi` up to the
  shared friend tail, then copies it to `ebp` (`mov ebp, esi`) so the
  selection loop's index can take `esi`. That leaves `ebx` free, and the loop
  holds the 0x400 flag in it (`test [eax+8], ebx`). Ours keeps the object in
  `ebx` throughout and tests the flag as a byte (`test ch, 4`). A copy of the
  parameter at function scope, a copy inside the inline tail, the tail's
  board branch as an inline of its own, and a `const` parameter were all
  propagated away and changed nothing (2026-10-10).
- Which register `delete p` loads `p` into. MovieStart's image loads the
  global into ecx, tests it and copies it to esi (`mov ecx, [g]; test ecx,
  ecx; mov esi, ecx`) before the destructor and `operator delete`; ours, and
  a clone that reproduces the rest of the function exactly (virtual Poll,
  thiscall), loads esi and copies to ecx. Unchanged by `delete` through a
  local, an explicit null test, `g->~M(); ::operator delete(g)`, an inline
  `Destroy() { delete this; }`, a class `operator delete`, a `void *` global
  cast at the delete, and dropping the `movie = this` alias (2026-10-10).
- What leaves DrawMenuCursor's inlined TickMenuAnimation loading the frame's
  sprite into ecx (the dead frame index) where ours takes edx (the dead row
  base). That one choice moves the backbuffer load later in the function off
  eax's short `mov`. Index spellings, a temporary, and every order of the
  helper's locals and opening statements compile the same.
- When VC6 splits a parameter's web at a loop head. BlockWeightTroops' image
  reloads `from` from its argument slot at the top of every iteration
  (`mov ebp, [esp+0x18]` is the back edge's target), so its register is free
  again after the last use and holds the first ApproxDist result across the
  second call. Ours keeps `from` in ebx for the whole loop and spills that
  result. Both have the same eight uses of `from`, so it is not the weight.
  Dropping the local copy of `from`, naming the distance (which does move
  `from` to ebp, but spills the weight instead), and four spellings of the
  weight helper do not reproduce the split (2026-10-10).

## Running decomp-permuter against VC6

[decomp-permuter](https://github.com/simonlindholm/decomp-permuter) supports
x86 but expects ELF objects scored with GNU `objdump`, and parses C with
pycparser. Recipe used on OptionsDefaults (2026-10-09):

- **base.c**: a C clone of the function (C structs, no `__cdecl`); any C++-only
  construct is written as a stand-in call, e.g. `Paint_(box, box->rect)` for
  the virtual call.
- **compile.sh** calls a converter that keeps everything after the C
  declarations (so the permuter's helper functions survive), swaps in a C++
  header, rewrites the stand-in to `(box)->Paint(box->rect)`, adds
  `extern "C"`, maps `long long` to `__int64`, compiles with the hosted VC6,
  and converts the COFF object with `objcopy -I pe-i386 -O elf32-i386`.
- **target.o**: the compiled base with its `.text` replaced by the image's
  bytes (`objcopy --update-section`), after copying the base's own bytes into
  every relocation field, so relocated operands compare equal and only real
  code differences score. Works when the two differ only in registers, so
  lengths and relocation offsets line up.
- Its score weighs an inserted or deleted instruction at 100 and a register
  difference at 5, so it happily trades order problems for register swaps
  and produces nonsense along the way: check every candidate with
  `tools/vc6lab.py` AND for behaviour before believing it.

What it found on OptionsDefaults, which hand search had not: reusing `bit` to
hold the ternary before reading `r->bit` (the load lands before the
`disabled` store and `mask`/`bit` get the image's registers), splitting
`mask = r->which; bit = mask ? flags : other;` (fixes the `test` operand
order), and then a `widget` temporary plus an empty `if (!mask) {}`. The
empty `if` executes nothing but adds a use of `mask` at the loop top, which
keeps `mask` live and fixes `setne dl`. That took the clone from 26 lines
off the image to 10, of which 4 are relocated constants and 6 are the `box`
load's registers. No natural source with that effect has been found, so
nothing is applied yet.
