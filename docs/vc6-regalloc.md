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

- **The rotation follows SOURCE order where layout and source disagree.**
  DepthResort's two backward-walk arms were laid out identically by both
  compilers, ran-off-front first, yet the insert-after arm's temporaries
  were one register out until the source put the arms in that order too
  (`for (;;) { if (!e->prev) { ran-off; return; } ...; if (found) break; }`
  with the insert after the loop). Same instructions, same layout, and the
  choices still followed the text.
- **Two locals copied out for an `if` cost a temporary.** The same
  function's `{ pv = n->prev; nx = n->next; if (!pv) *head = nx; else
  pv->next = nx; }` and the direct `if (!n->prev) *head = n->next; else
  n->prev->next = n->next;` generate the same loads, but the locals shift
  the rotation after the join. With both changes DepthResort matched
  outright (2026-10-11).

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

**A block-scoped local is a different variable to VC6.** ScriptPad's two
pad-centre scans used one function-level set of `i, sx, sy, n, total`; the
image gives the two scans different slots and registers for the same roles
(the first scan's count spilled, the second's in `ebp`), which one shared set
cannot produce. Declaring the set inside each scan's block took the code shape
110 -> 105. Declaration ORDER inside the block changed nothing (all 120 orders
of the second block scored the same). Worth trying wherever two disjoint
blocks do the same job with the same locals.

**Several copies of one body in the image are several arms in the source.**
UpdateTrooperAction's stop tests were one `if (a || b || c) { body }`; the
image has the body three times, once per test, and the chain laid out after
the sweep. Written as three `else if` arms with the body in each, VC6 keeps
the three copies and the layout follows (v3 shape 86 -> 30, 1182 -> 900
bytes). The same goes for a CALL the image reaches by `push args; jmp` into
another call site: ScriptPad's PadFinalise is written twice, once for a
script that ends after the comparison (96 -> 87). A cheap scan for both:
count identical straight-line blocks (four or more instructions) in each
listing and list those the image has more copies of.

**A switch's case bodies are laid out in source order**, so a case written
in the wrong place moves every body after it. SelectFirePose had `case 43`
second where the image has it last (shape 34 -> 24). `casecmp.py`-style
comparison is cheap: read both jump tables (through the byte index table
when there is one), map every case value to its body address, and diff the
two orders. Labels and `goto` placement INSIDE the arms, by contrast, did
not move the shared blocks at all in that function -- VC6 placed them the
same for all nine label positions tried.

## Which copy of a merged tail survives

When two switch arms end in the same code (`case 2: return 6;` in two inner
switches), VC6 keeps the copy from the arm that comes LATER in the source and
points the other arm at it. Arms are laid out in source order, so the
surviving block normally sits in the later arm (lab, 2026-10-09, a
WeaponPoseIndex clone). WeaponPoseIndex, SelectWeapon, ShotHitsObj and
CommSend all have the image keeping the EARLIER copy while arms are still laid
out in source order. Changing the order of arms inside one switch moves
nothing. A fall-through from one arm into the next does put the shared block
where the image has it, but it also moves the arm.

**A single `goto` into the earlier copy makes it the survivor (2026-10-10).**
ScriptParseActionRecon has six arms ending in the same `return
ScriptResolveName(...) != 0;`. Written as six returns, VC6 kept a later copy.
A label on the first copy and ONE `goto` to it from any other arm, with the
remaining arms still written as plain returns, makes the first copy the one
the others jump to, as in the image. Which arm holds the `goto` does not
matter, and the other arms do not need one. The same fix applies to its two
`ScriptObjectUid` tails (shape 295 -> 243). A sweep that tried this for
every repeated `return X;` in the mismatching functions found no other hits.

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

Partly solved in the real function (2026-10-10): writing `return 0` at the
LAST site only (the NACK arm's `!me` test), with the other three left as
`break`, makes that log the copy the second and third jump into, and the
shape goes 25 -> 19. `return 0` at all four, or at any other single site,
changes nothing or is worse. The first site still keeps its own copy. A
clone with all four callee-saved registers live (a triple loop in one arm)
and two more one-call `break` arms still gives each inline arm its own
epilogue copy (2026-10-11), so register pressure alone is not what the
original had.

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

- **A call nested as an argument is evaluated after the outer call's later
  arguments are pushed.** `turn = Clamp(AngleDelta(a, b), -8, 8)` pushes 8
  and -8, then calls AngleDelta; the image calls AngleDelta first and pushes
  the constants afterwards, which is `turn = AngleDelta(a, b); turn =
  Clamp(turn, -8, 8);` (Step3Drive, twice: shape 83 -> 67). A sweep that
  hoisted every nested call into an `int32_t` temporary found nothing else.
- **An unread argument still shows in the caller.** Every image caller of
  AngleOfDelta pushes the distance it has just computed as a third
  argument the callee never reads. SightScan called it with two and so
  pushed one dword less, which moved every later stack offset and cleanup.
  `AI_ANGLE3` (region.cpp) and `AngleOfDelta3` (air.cpp) pass the third.

- **A source counter that shadows another is a web of its own; VC6's is
  not.** CollapseEqualDeltas' inner loop was `for (j = i + 1; j < n; j++)
  { ...; i++; }`, where `j` is always `i + 1`. The image steps a second
  register beside `i` too, but that one is VC6's induction variable and
  carries no weight. A source `j` outweighed `n` for `ecx` and pushed `n`
  into `edi`. Written `while (i + 1 < n)`, with the count re-read into `n`
  for the outer test (the image loads it into n's own register), it
  matches (2026-10-11).
- **A sum stored and then tested again as a sum.** DiedEffectB's wrap is
  `dir = angle + sector; if (angle + sector < 0) dir += 0x100; else if
  (dir >= 0x100) dir -= 0x100;`. Testing `dir` lets VC6 use the add's own
  flags (`jns`) where the image has `cmp` against the zero register. The
  second written sum also gives `sector` the use that wins it `ebx`.
  Matches.
- **An indexed store is formed after the loop guard; a slot pointer before
  it.** RegionFindPath's path copy as `for (slot = path + depth; walk;
  ...) *slot-- = ...` computes the `lea` ahead of the walk's null test.
  The image computes it after the test, which is `for (i = depth; walk;
  ...) path[i--] = ...` (24 -> 6 bytes).

- **A ternary argument is evaluated before the call's other arguments;
  two calls VC6 merges are not.** PausedFrameStep's badge was
  `DrawSprite(variant == 3 ? g_mpMarkA : g_mpMarkB, xy[0], xy[1], 0)`,
  which loads the sprite first. The image loads the coordinates, pushes
  them, and picks the sprite last, `mov eax, A; ...; je; mov eax, B; push
  eax; call`: that is `if (variant == 3) DrawSprite(A, ...); else
  DrawSprite(B, ...);` with the two calls' common tail merged (482 -> 72
  bytes, and to the image's shape once `badge` was an int32).

- **Identical arms are merged by what the source says, so an image that
  keeps them apart wrote them differently.** UpdateTrooperAction's three
  stop arms, `out->state = 1; turned = 1; out->bearing = facing; speed =
  0;` written alike in `if ... else if (a) S else if (b) S`, came out as
  one copy behind an `a || b` test; the image has a copy per arm. With the
  four statements in a different order in each arm (the generated stores
  are scheduled the same either way) VC6 keeps them apart (v3 25 -> 18,
  then 16 with the third arm). This merge happens on the intermediate
  code: retyping a store or spelling its operand differently does not
  stop it, and once registers are assigned a tail merge needs matching
  registers too (PointerPickMode0's hint tail, HudSquadUpdate's ink arms).
- **Retry a rejected type after a structural fix.** UpdateTrooperAction's
  `klass` as an int32 was worse (28 -> 30) while the arms were merged and
  better (16 -> 10) once they were not; PausedFrameStep's `badge` the same.

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

- **A shared block reached by `goto` can be the image's inline copy plus a
  second one.** Where the image keeps a shared block in the arm the source
  writes it in and every other arm jumps back to it, writing it once as a
  label inside that arm lets VC6 move it after the last `goto`. Writing an
  unlabelled copy in the arm and the labelled one elsewhere restores the
  layout: SightScan's scoring (the kind-7 arm's copy in place, `score:` at
  the end of the loop body, shape 78 -> 66) and FindPath's out-of-budget
  refusal (its own `PathChargeFrame; return 0;` in the budget test, the
  shared `fail:` still below, 82 -> 60). Sweeps that did this mechanically
  for every return-ending label found nothing else. What remains of
  SightScan (2026-10-11) is the second copy itself: the image has only the
  in-arm one, entered from four `goto`s. The labelled block alone still
  moves to after the allied arm's last `goto` whatever that arm's tail
  looks like (28 -> 43); a full copy at each `goto` merges nowhere (52);
  and the kind-7 arm written after the sight test is worse (54).
- **VehicleUpdateAppend tests the position bit twice** (2026-10-11): the
  image does `and ecx, 0x80000000` before the position bytes and `test ecx,
  ecx` again before the action byte, so its source has two `if (flags &
  0x80000000u)` blocks where ours has one. Splitting ours reproduces both
  tests exactly, but VC6 then spills `flags` to an 8-byte frame (the image
  keeps it in ebp with a `push ecx` frame) and the earlier flag tests
  reshuffle: 11 -> 17. Declaration order and an int32 `action` do not move
  the spill. Kept as one block until something does.
- **RowUpdate re-reads `rect.top` right after storing it** (2026-10-11) for
  `bottom = top + h`, while `left` is reused from its register. Writing the
  top store through an `AM2_Rect *` copy of `&r->rect` reproduces that
  reload, but VC6 then re-reads `left` too (13 either way). All 64
  per-store/per-read choices of pointer or member, casts to `int32_t *`, an
  index into the rect, statement orders and dropping `w`/`h` leave it at 13
  or worse.
- **ScriptIf's two reversed end-of-script tests are not about spelling**
  (2026-10-11). The image has eight separate `cmp; jl; push "Unexpected end
  of script"; call; xor eax, eax; epilogue` blocks. Ours writes two of them
  as `count <= at` (`jg`), because in the image's direction VC6 merges each
  with another block that has the same branch target (4 -> 9, and 19 for the
  ALLOF/INORDER step). Extra parentheses, `!(a < b)`, an `int32_t` cast,
  `+ 0`, and a separate `++` statement all merge the same way, so this merge
  is not removed by respelling, as UpdateTrooperAction's (above) was by
  reordering whole statements; ScriptIf's blocks have one statement each.
- **The CPU target and per-function pragmas are not the lever
  (2026-10-11).** For StepRowAnim, ViewUpdate, MissionEnded and DirtyCollect,
  `/G3`, `/G4`, `/G5` and `/GB` compile identically to the default and `/G6`
  is far worse (DirtyCollect 2 -> 70), so the original used the default
  scheduler. On ObjHitMaskAction `#pragma optimize` with `a`, `w`, `p`, `y` or
  `t` on changes nothing, and `g` off or `s` on wrecks it.
  Across seven small functions, `/Ob0`, `/Ox` match the default exactly;
  `/Ob2`, `/Op`, `/Oa` and `/Ow` are each worse somewhere (ObjRemap 7 -> 60
  under `/Ob2`), so `/O2 /GX` stands.

- **An inline helper's exact wording leaks into its callers (2026-10-11).**
  VC6 inlines before it merges, fuses and schedules, so a helper that never
  appears in the binary still shapes the caller's code. The ring marker's
  `at = ring[i] + idx; if (at < 0)` fused into `add; js`; the same loop with
  the sum written twice (`if (*p + idx < 0) return; cells[*p + idx] |= 2`)
  gives the image's `add; test; jl`, and with it the rest of the ring block:
  ObjHitMaskAction 40 -> 36 (-> 29 with one arm's flag store moved after the
  ring), ListMaskAction 41 -> 35 once it marks through the mask too and its
  pixel row is a word. Converting the other helpers the off functions use
  between macro and inline-function form (TYPEERR, WANT, STEP, AIMED, ENGINE,
  UNLINK, SQUAD_INK, the depth-list links, FLOW_QUEUE_HEAD, PointerFriendTail
  expanded in place) compiles identically or worse; the depth-list links and
  SQUAD_INK as functions are much worse, so those were macros or open code.
- **A rotated loop sinks a switch's common store (ScriptPad).** The image's
  trigger loop has the kind test at its head and `++(*at) < count` as a
  conditional back edge, falling into the `<=>` switch; every source form
  with that back edge (`do/while`, `for` with `break`, explicit `goto`s) makes
  VC6 sink the arms' identical `trigger |= bit` store into the join block,
  splitting every `or [mem], x` into load/or/store (60 -> 95). Volatile or
  pointer-formed stores and an increment in each arm do not stop it. The
  `while (1)` form with `goto` exits keeps the arms but ends `jge; jmp`
  where the image has `jl`. Unresolved.

- **A sweep hit can match the count without matching the image.** The
  shape count drops stack loads, so a variant that reads a GLOBAL where the
  image reads a stack local can score better while being wrong:
  HudPostUpdate's handler calls pushed `g_dragAnchor` in place of the local
  `at` (4 -> 1) where every `call eax` in the image pushes `[esp+0x10]`.
  Check the image's operand before keeping a substitution the count likes.
- **An inline helper's local the image reads twice (HudPostUpdate,
  2026-10-11).** PointerIdleOverlay held `g_weaponFnSlot2` in a `row` local;
  reading the global for the test and again for the argument is 8 -> 4. The
  same unnaming of `reach` in SightScan is 25 -> 23. `unname.py` and
  `unname_helper.py` sweep this over callers and helper bodies.
  Sweeps that found nothing on 2026-10-11, all run over every off function
  from the then-current source: a global read into a local at the top of the
  function (namevar), each `for`/`while` as `while (1) { if (!c) break; }`
  and as a guarded `do/while` (loopform), each cast assignment through a
  temporary (tempify), each labelled tail block moved to the function's end
  (labelend), and the helper bodies' own locals unnamed (unname_helper, after
  PointerIdleOverlay's). The pattern they would have caught is spent.

- **A re-read VC6 will not reproduce otherwise: a volatile read
  (RefundPlacedUnit, 2026-10-11).** The image loads the follower uid twice
  in `while (uid) o = LookupByUID(uid);`, once for the test and again for the
  argument. VC6 merges any two plain reads of one address -- through a
  member, a cast to another integer type, a raw offset, an inline accessor
  -- and only `*(volatile uint32_t *)&...` at the argument keeps the second
  load: shape 1 -> 0, relink bytes 62 -> 20. The same cast elsewhere
  (ObjRemap's box top, ScriptParseEvent's token kind, RowUpdate's rect top)
  is worse, because a volatile access also stops VC6 scheduling around it;
  use it only where the re-read is the whole difference.

- **Which `return 0` a conditional exit reuses (RegionFindPath, 2026-10-11).**
  RegionFindPath is at the image's shape; its one remaining byte difference
  is the target of the empty-open-list `je`: the image jumps to the FIRST
  `return 0` epilogue (the inactive-destination test), ours to the LAST (the
  depth test after the loop). In a lab clone the choice flips with the code
  after the depth test: with nothing but `return 1` it becomes a `setcc`, no
  depth block exists and the jump takes the first epilogue; with any
  statement after it (a store, a call, the path loop) the jump takes the
  depth block. A `goto` to a label on the first return moves that block to
  the end; a combined early test, a top-tested `while`, the depth test
  inside the found arm, `(cur = head) == 0`, a `goto` to a return at the
  very end, and five spellings of the depth test all fail to reproduce it.
- **ScriptParseActionRecon, 129 -> 74 by the same reordering**
  (2026-10-11). Its arms end in a handful of identical tails, and wherever
  the image keeps two copies VC6 had merged ours into one. Swapping two
  independent statements in ONE of the copies keeps them apart:
  SETFACING's VARNAME stores `xvar` after the step rather than before
  (107 -> 94), ADDEXP steps before storing `n0`, which leaves SETITEMFLAG its own tail (94 -> 77), INACTIVATEREGION the same (77 -> 74). The
  generated code differs only in that order. Two more pieces were about
  which copy survives: TRIGGERDELAY's refvar arm folds VARNAME's increment
  into its own end-of-script test (129 -> 123), and the creates' location
  and army tail is written out in each arm rather than called through one
  inline helper (123 -> 107). Applying the reorder to every other `n0 =
  value; (*at)++;` tail, singly and in every pair, found nothing further.
- **Re-transcribing a stuck function from its listing, measured
  (FindPath, ScriptPad, the mask pair, 2026-10-11).** A whole-body rewrite
  of FindPath in the image's block order scored 110 against the source's
  50: the gains were in two details and everything structural lost. What
  paid: PathUnwind indexing `out[i--]` with the depth re-read into an int
  after `*n` is stored, so VC6 forms the pointer in the loop preheader,
  after the `end > 0` test, as the image does (50 -> 47); ScriptPad's pad
  slot taking `(int16_t)g_padCount` rather than the id just stored there,
  which the image stores from the register (60 -> 53). What lost, each
  measured alone from the committed source: `nb` used directly instead of
  re-reading `curTile + g_tileStep8[i]` (+3..+9), node[cur] spelled one way
  throughout (+1..+9), `while ((uint16_t)from)` (+5), the heuristics as
  locals (+2), and every form of the budget block. The image's budget
  block falls into the exhausted-list charge and shares one `return 1`
  charge with the found arm; every source with the charge written in
  both `return 1` arms gave VC6 three or four charge copies it did not
  cross-jump (the copies differ in which register holds `considered`), and
  every `goto fail` form moved the `return 0` block to the end of the
  function: at function level VC6 moves a block ending in `ret` that is
  reached only by jumps to the end (A1/A4 forms, 70), while inside a loop
  it defers if-bodies ending in a jump to just after the loop, in source
  order, nested after parent (the budget variants, 99-134). ListMaskAction
  recomputing the row width from the box inside the row loop as the image
  does (+12), ObjHitMaskAction without its `ci` index (+8..+22), a helper
  taking the cell pointer (+22), ScriptPad's trigger loop as `do/while`
  or `goto` (+35..+47; the arm store sinks), and its string block with an
  explicit `goto triggers_done` (0) all lose or do nothing. ObjRemap's
  bottom cell as `(box.bottom - box.top) + top` rather than `+ y +
  box.top` is the image's reload of the saved top (7 -> 6; the relink
  byte count rises 170 -> 212 as registers move). Measured with no gain
  the same day: SelectFirePose's two raise poses as label/copy/goto in
  every arm (VC6 keeps duplicating the machine-gun arm's copies, 20 ->
  22..35); RowUpdate's rect stores in the image's right-before-bottom
  order with and without the rect pointer (12 -> 13..30); BoxAction's row
  loop as `y <= c.bottom` (9, unchanged); EncodeGlyph with a
  `rowOffset` pointer local (11 -> 15); VehicleUpdateAppend's bit-31
  test as `!= 0`, a masked local, or a signed compare (11, 11, 12: the
  image's `and` over ours' `test` is a copy the allocator makes);
  UpdateTrooperAction's `pos` assigned before the classify call,
  non-const, or with the step point read from the object (10, all
  identical code: the image hoists `lea edi, [esi+0x12]` because its
  zero register is ebx where ours is edi). SightScan's score block:
  the kind-7 arm's `goto score`, the arm falling through to `score:`
  after an if/else chain, and the label inside the arm with the end
  copy removed give identical code (23) or worse (38); VC6 pulls the
  labelled block up to the aiMode arm's unconditional `goto` and
  duplicates it for the kind-7 fall-through whatever the source order.
  Three of the remaining counts were listing artifacts, not shape: a
  `push` of a function address that the per-TU relink listing left as
  `push 0`. THE SCORER'S SYMBOL TABLE WAS STALE: the one-TU scorer
  (xsc, via rl1 with LOAD=syms.pkl) resolves a reference to another
  TU's function through a table saved from an earlier full compile,
  keyed by DECORATED name, and `ObjIsHittable(void *)` had since been
  retyped to `ObjIsHittable(AM2_Object *)` -- so SightScan's two
  callback pushes, and every retyped callee's call, missed. The full
  relink compiles all TUs together and never saw it. Rebuilt the table
  (SAVE= on a full rl1 run) and gave rl1 a fallback under LOAD to the
  undecorated name when it is unique, which survives the next retyping.
  With fresh listings for all 39: SightScan 23 -> 21, WndProc's single
  remaining instruction was the same artifact and it is at the image's
  shape; 487 on code alone. MpPanelScreen's `"%s.txt"` stays as the
  one genuine unresolvable: the string occurs more than once in the
  image and the site is misaligned, so the content check cannot pick
  its copy.
  MpPanelScreen 18 -> 11 from reading `bar->span` in the ThumbShift
  argument rather than into a local ahead of the call: the local made
  VC6 load the sprite bottom to a register where the image subtracts it
  as a memory operand (the other four ThumbShift callers already read
  it in place). AiTrooperStep's shared move-target tail: the image has
  the else arm fall into `set_move_to` (moveTo, then moveEnd as an
  immediate 0) with the two planning arms jumping forward to it; ours
  pulls the label up to the last planning arm and keeps the else arm's
  own copy, and neither store order (12, unchanged) nor dropping the
  else arm's copy (25, 26) moves it. The functions at one to four are
  the same kind of thing, measured 2026-10-11: ObjAfterMove's height as
  an int (1 -> 16; the image's `movsx` into a dword slot is the
  allocator widening an int8 local), ScriptIf's three count compares in
  the image's operand order singly and together (4 -> 9..21), ScriptParseEvent
  reading the kind by index beside the token pointer (4, same code),
  DamageTrooper's armour arm as a goto or hoisted after the loop (4,
  4 -> 6), CommSend's two error logs swapped or as an if/else chain
  (4, 4 -> 8). VC6 places those blocks from the CFG, not from the
  source order. MoviesScreen's four button blocks read `sprites[idx][0]`
  twice through a `lea`'d pointer in the image; a pointer local to the
  element or to the pair gives the same code as the indexed reads (12),
  a chained assignment 20, the page index written inline 37.
  ScrollMapCache blitting through its `cache` local rather than the
  global (the image reloads the spilled local) is 10 -> 11.

- **Read each hunk with its raw context, and ask what expression emits
  the image's instruction (2026-10-11).** Every gain of the day came
  that way and none from a structural rewrite or a sweep. What the
  hunks said, function by function:
  - Step3Drive 30 -> 21: the image pushes `&target` before `&o->pos` at
    both distance tests, i.e. `ApproxDist(&o->pos, &target)` (the
    distance is symmetric); and it multiplies `stuckCount * speed` in
    EACH clamp arm, where a `t` hoisted ahead of the `if` was one `imul`.
    Re-reading `o->rowSet.rows` for the bias and the stamp (not `gun`)
    stays: the image reloads it (33, 37 with `gun`).
  - ListMaskAction 32 -> 29, ObjHitMaskAction 24 -> 21: the ring marker's
    end test is `jl` in the image, `jb` from a pointer walk; written as
    an index loop (`ring[i]`, `i < AM2_TILEMASK_RING`) VC6 still
    strength-reduces to the pointer walk but keeps the signed compare.
    The phase split with `k -= phase` after the join, or as a ternary,
    and a `cells` local for the index are all worse (29..76).
  - RowUpdate 12 -> 10: the loop increments in the image's order,
    `x++, used++, cell++` (the entry pointer stepped before the cell).
  - EncodeGlyph 11 -> 10: a walking `uint16_t *offs` stored through
    `*offs++`, which the image keeps in a register with `out` on the
    frame; indexing `offs[row]` was 15.
  - UpdateTrooperAction 10 -> 5: `i = 1` stored before the sweep limit
    is chosen -- the image's `mov [esp+0x1c], 1` sits ahead of the
    ObjType2Field548 call, and VC6 does not move a store across a call.
  - ScrollMapCache 10 -> 9: the lower strip's `wide.bottom = wide.top +
    dy` -- the image computes bottom - dy, stores it, adds dy back.
  - Measured and worse: AiTrooperStep through its `pos` pointer (12 ->
    20; the image recomputes `&ob->pos` at each use after all),
    VehicleUpdateAppend's position and action as two `if`s on the same
    mask (11 -> 17; the image's second `test ecx, ecx` is the masked
    value kept, not a second test), FlowRecvMessage's in-switch
    `return 0`s as breaks (15 -> 20), ScriptParseActionRecon's `eos:`
    moved into the TRIGGERDELAY arm (70 -> 192), INACTIVATEREGION through
    a token pointer (76), LoadMap counting the scenario chunk before the
    parse (8 -> 18), ScriptPad's pad count incremented last (53 -> 69),
    FindPath's `g` as a sum or a shift (47, same code), SelectFirePose's
    grenade arm returning in place (20, same code), PointActionC through
    a sprite-slot pointer (3, same code), ObjRemap's `y` as an int (8),
    MissionEnded's attempt count stored after the close (2 -> 4),
    DepthInsert's link-after arm written out with the new node formed
    first, in three orders (2 -> 10..17; its 460 bytes are one register
    swap across the whole function behind a two-line shape difference).
    Step3Drive's big remainder is `r` in ebp against the image's `speed`
    there (27 field uses of `r` to 21 of `speed`); taking the six
    `r->spot` uses through a pointer local, or the three `r->field0C`
    tests through one, moves the weight but not the way the image went
    (21 -> 29, 26, 32). UpdateTrooperAction's `found = 1` is a register
    in the image's loop and a stack store in ours; the image zeroes its
    try counter in five arms where ours stores a different local.
    SightScan's image gives ebx to the zero and ebp to the constant 1
    (so `*outBearing = 0` stores `bl`, and ebp is pushed in the
    prologue); ours gives ebx to the 1 and ebp to the zero (a byte
    immediate, and a late `push ebp`). Lightening the 1 -- the turret
    test as `count >= 2`, first site, second, both -- is 21 -> 27, 23,
    29; the 1's weight is not what decides it.
  - The 22 functions at the image's shape but not its bytes (1,106 bytes
    between them, 2 to 220 each) were read the same way, from the raw
    instruction diff of the relink json (`rawdiff.py`, no compile):
    ObjTileHook is one `test` with its operands swapped and the source
    operand order does not reach it (1 either way); LoadOptions's last
    sentinel store is scheduled between the epilogue's pops with the
    skip edge jumping to the else arm's copy, and an explicit `return`
    before the else changes nothing; PausedFrameStep's `active = 0`
    initialised at its declaration is 2 -> 10; MovieStart's poll
    guarded on the global 4 -> 9; ParseScenarios's size parameter used
    in place (the image's `sub ebp, 4`) 15 -> 37; RefundPlacedUnit's
    case arms in the other order the same bytes; WndProc's paint rect
    at function scope the same bytes (ours shares its slot with
    another arm's buffer, the image does not). SelectionClick's deselect
    site loads the zero point into eax where the image takes ecx (the
    tail's call takes eax in both); the local copy the source carries
    for it no longer moves it, and passing the global directly is 2 ->
    42 bytes. None applied.
  - What remains in those functions is which register a value gets
    (ScriptParseActionRecon's kind-name pushes come from edx, eax and
    ecx in both listings, and VC6 cross-jumps only the common suffix),
    the zero register's identity (Step3Drive's `r` in ebp against the
    image's stack slot), and a spill the allocator chooses (SightScan's
    `cand`).
- **A combination can match where each half is worse.** ParseScenarioPart
  matched only with three changes together (`took = 8` before an
  overwriting `if`, no separate `count <= 0` return ahead of the loop, and
  `rows = p->rows = malloc(...)`); the first alone went 3 -> 12.

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

- **Why the image keeps four copies of ScriptParseActionRecon's
  end-of-script error.** The image has the shared `eos:` block (its `xor
  eax, eax` among the pops, as a fall-through into the common `return 0`)
  and three more inline copies, TRIGGERDELAY's, FIREWEAPON's and one in
  the 0x14..0x1E arms, each with its own epilogue and `xor eax, eax`
  before the pops. Our build merges all four into one. In the lab
  (`tools/vc6lab.py`, 2026-10-11) a small switch keeps such copies apart
  only when each arm is an inlined helper, and even that merges again
  once a dozen more arms jump to `eos:`. Explicit copies, inlined checks,
  an inline step helper, returning a variable and an EH frame (the image
  has none) do not reproduce it at full size. Whatever keeps the copies
  apart is not a source spelling found so far.

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

## SelectionClick in the lab (2026-10-11)

Two bytes from the image: the ctrl-click deselect arm loads the zero point
for `OnSelectionChanged` into eax where the image takes ecx. A clone with
the globals as extern symbols (absolute addresses in the clone give VC6 a
different aliasing picture and 164 lines of difference; typed symbols
give our relink's bytes exactly) reproduces the real build, so the tie is
local to the function. What the ablation found:

- With the site written as a local copy (`AM2_Point pt = g_zeroPoint;`,
  the committed form) the load is a web and takes eax, and the rest of
  the function matches the image. Written as the global directly, in any
  spelling (`*(const AM2_Point *)&g`, `(&g)[0]`, `&g.x`, through an int
  prototype, through a volatile int), the load is a rotation temporary
  and takes edx -- and the drag arm's tail stops merging into the
  function's tail as the image has it, 34 lines off.
- The image's ecx is the rotation one step before ours. Two dead
  volatile reads inserted ahead of the site move it to ecx and restore
  the structure (9 lines, the lowest seen); one read gives eax. So the
  original had two more rotation-consuming temporaries before this site
  that emit nothing we can see, and no local copy of `n`, `uid`, the
  item array, the list pointer or the flags, no operand order, no
  pre-increment, no positive `if`, and no arm order (the loop written
  after the select tail as a `goto` target) supplies them.

Left as it is. The method is worth keeping: `labvsours.py` diffs a clone
against OUR listing first, `labab.py` runs a variant file through the lab
and reports the one register under study, and the volatile-read probe
measures the rotation distance to the image in steps.

## ObjTileHook in the lab: `test` operand order (2026-10-11)

One byte: `test ebx, eax` against the image's `test eax, ebx`, in `if
(mask[i] & entered) ... else if (mask[i] & left)`. A mini-lab (one loop,
no image comparison, `minilab.py`) showed every spelling of the test --
operands swapped, `!= 0`, inverted, a `hit` temporary, the mask walked
by pointer, `entered` widened or cast, the array indexed directly --
writes `test <variable>, <loaded value>`. Reading the entry into a local
used by BOTH tests, `int32_t m = mask[i]; if (m & entered) ... else if
(m & left)`, writes `test eax, <variable>`: the local is now the first
operand, and the second test still reads `left` from the frame as a
byte. Carried into pad.cpp it is byte-identical, relink 1585 -> 1586. The
rule, as far as one case shows it: a value loaded for a single test is
the second operand of `test`; a value that is a local (even one VC6 keeps
only in a scratch register) is the first.

RefundPlacedUnit (20 bytes, all one rotation step from one site on) went
the same way without a clone: the raw diff's first hunk was `mov eax,
[ebx+0x54c]; push eax` against the image's ecx, the source had `uint32_t
wuid = inside->inventory[0]; w = LookupByUID(wuid);`, and the local is a
web (eax by priority) where a direct argument is a rotation temporary
(ecx, after the `test eax, eax` before it). Passed directly it is
byte-identical, relink 1586 -> 1587. Together with SelectionClick's
finding this is a usable rule: **a value that feeds one call is a
rotation temporary when written in the call and a web when copied to a
local first; the two take different registers, and the image says
which it was.**

The same mini-lab on the next two register-only functions found nothing:
PausedFrameStep's `slot = 0` store scheduled before the row-pointer copy
in the image and after it in ours is unchanged by zeroing the counter
ahead of the copy, by putting the copy in the `for` header, or by any
declaration placement; LoadOptions's last sentinel `if` has its skip
edge jump to the else arm's epilogue in ours (with `pop esi` hoisted
above the store) and to its own in the image, and fourteen spellings --
`return`s in either arm, a dead statement after the `if` or after the
else loop, a ternary, an empty-else, nesting, the tests reordered, the
store through a pointer, the key loop as a pointer walk -- all give
ours. Both stay.

AirFrameDraw (42 bytes) in the mini-lab: the image's frame counter is
`mov esi, [edi+0xa0]; inc esi; cmp esi, 1; jne; inc esi; cmp esi, 3`
with the object in edi. Written `f = slot + 1; if (f == 1) f++;` VC6
gives the registers (f esi, obj edi) but folds the arm to `mov esi, 2`
and jumps past the `> 3` test; so do `1 == f`, `f -= -1`, a `switch`,
unsigned, int16, uint8, a `++f` test, a ternary, a nested `if` and
`/O1` (which spells the constant `push 2; pop esi`). `f - 1 == 0` and
`!(f ^ 1)` keep an `inc` but change the test. The committed sum test
(`obj->formationSlot + 1 == 1`) is the only form that keeps the `inc`,
and its second read of the field is one more use of `obj`, which is
what hands `obj` esi over f. The two cannot both be had from any
spelling found; it stays. (`/O1` on the whole of mapdraw.cpp is far
worse everywhere, 42 -> 150 here, so the file was not built that way.)
BlockWeightRoute's `abs(a - b)` with the operands swapped and its two
distances compared the other way round change nothing (36).

## VehicleBlockWeight in the lab: a register web is earned by reads (2026-10-11)

Thirty-two bytes, all in the prologue's two zeros and the clamp. The
image zeroes ebp, stores it to `sound`'s slot and tests both mask counts
against it, zeroes edi separately for `weight`'s slot, and after the loop
reloads `weight` into edi -- once at the loop's exit (the empty-mask edge
jumps past that load, edi still holding the prologue's zero) and again
after the clamp's `mov [esp+10h], 40h`. Ours materialised one zero for
both slots and the clamp wrote `mov edi, 40h`.

A full clone of the function in the lab (extern symbols for the globals;
it reproduced our build to the instruction) settled it in four steps:

- **`i = 0` with the declarations.** The first zero is the non-boat
  loop's index, which the image's second loop starts from without an
  `xor` of its own; `sound`'s store and both `count > 0` tests reuse it.
  Ours zeroed `i` inside each `if`; 32 -> 14 on its own. The boat loop
  counts on a second variable, zeroed just before its own count test.
- **One `weight`, no `answer`.** Two variables with `answer = weight`
  after the loop and `answer = weight = 40h` in the clamp give the
  loop-exit load but fold the clamp to `mov edi, 40h` (the store to
  `weight` is dead); written as two statements VC6 sinks the copy to
  the join (it knows `answer == weight` on every path, the empty one
  through the two zeros) and the loop-exit load disappears. An
  uninitialised `answer` changes the frame.
- **With one `weight` and one `return`, it stays in memory** (the three
  epilogues each read the slot). The image's edi is `weight`'s register
  copy in the tail, which VC6 grants when the tail reads it enough:
  **`return weight;` written in the TANK and JEEP arms as well** makes
  four reads, and with them the prologue's separate `xor edi, edi`, the
  reload at the loop's exit, and the store-then-reload clamp all appear.
- **The final sample's point is the loop's `pt`.** A fresh block-scope
  point takes `apply`'s dead argument slot (0x28) where the image's is in
  `facing`'s (0x20), the slot the loop's `pt` already has.

Byte-identical, relink 1587 -> 1588. The rule to carry: **a local that
lives in memory across a loop gets a register copy for the tail only
when the tail's reads pay for it, and explicit returns are reads.** Where
the image reloads a variable right after storing a constant to its slot,
that is the copy being re-synced, not a second variable.

## BlockWeightTroops in the lab: a priority threshold, not a spelling (2026-10-11)

Eighty-two bytes, every one a swap: the image holds `from` in ebp and
the inlined helper's `w` in ebx, ours the reverse, and the image's
distance arm calls `ApproxDist(&f->pos, &o->pos)` FIRST, keeping its
result in the register `from` just died in (`add ebp, 12h` forms the
argument in place, `mov ebp, eax` takes the result), then
`ApproxDist(&ref, ...)`. A clone (0 lines from our build) measured:

- The call order is not the operand order. Swapping the comparison only
  flips the `cmp`; VC6 evaluates the `ref` call first either way, and
  spills `&f->pos` across it. Written `int32_t d = ApproxDist(&f->pos,
  &o->pos);` ahead of the test the calls come out as the image's, but
  then `from`'s load is hoisted above the loop (the image, and ours,
  load it at the loop head), `d` takes ebx and `w` spills to a slot.
  A per-iteration `f = from`, `ref` by pointer, `register`, and the
  local at the helper's top or the caller's all leave that.
- The ebx/ebp swap is priority. Taking uses off `from` one at a time,
  seven keep it in ebx (ours) and six hand ebx to `w` (the image), with
  nothing else moving. Which two of its eight reads the original did
  not count is not known: a local alias for the type-2 arm, a second
  inline helper for it, `return w` where `w` is known to be the full
  weight (VC6 still emits `mov eax, 0Fh`), extra `return w` sites, the
  item flag returned directly, `w` as int16/uint32, and declaration
  orders all leave the count; an `int8_t w` takes ebx by the
  byte-register constraint and writes `mov bl`.

It stays; the two mechanisms are usable elsewhere (a distance computed
into a local ahead of its comparison is how the first call comes first,
and a register-only swap between two webs is a use count, which the
threshold measures).

## TrooperFire in the lab: a web at the kind test, and the else arm's start (2026-10-11)

Fourteen bytes, all one rotation step from the target arm's facing call
on: the image forms both addresses first (`lea eax, [ebx+12h]; lea ecx,
[esi+12h]; push eax; push ecx`), ours pushes each as it is formed with
`eax` twice. A full clone (4 artifact lines from our build) found:

- **The `eax, eax` pair comes from `def` being a web at that point.**
  `def = w->typeRec; if (def->kind != MEDI && def->kind != WREN)` gives
  it; so does a fresh local for the record. Written as the expression,
  `w->typeRec->kind` in both tests (or `kind = w->typeRec->kind` and
  testing the local), the arm is the image's to the instruction, the
  `(int16_t)f` store included. A local held only for two reads in the
  same statement still counts as a web for the rotation.
- **But the else arm then starts one register late**, and so does
  everything after it (its first temporary `ecx` where the image has
  `eax`). The arm's first temporary follows the if arm's TEXT: a
  `volatile` read appended after the if arm's last statement moves the
  else arm's first temporary one step, and two of them put it back on
  the image (diff 38, the two reads and nothing else). So the original's
  if arm ends with two temporaries more than the expression form
  spells, and none of the natural ones found supply them: the sight
  stores or the ground chained, `heading` assigned inside the call or
  declared in the arm, `spot`/`target` declared in the arm, ObjHeight
  into a local, `fired` normalised, the record read earlier. `int32_t
  heading` restores the else arm's start but changes the byte slot's
  reload. The committed form keeps the else arm and the tail right and
  leaves the one arm a step out; it stays.

## Three more register-only functions in the lab, nothing landed (2026-10-11)

- **ParseScenarios** (15 bytes) holds `remaining` in ebp and `table` in
  ebx; ours the reverse, and the image loads the parameter as a web
  (`mov ebp, [esp+28h]` ahead of `push edi`, then `sub ebp, 4`) where
  ours copies it into a local first (`lea ebx, [eax-4]`). Using the
  parameter in place gives the image's web structure, but ebx/ebp stay
  swapped and the `count << 6` temporary then reuses eax straight after
  the `xor eax, eax` (the image rotates to ecx), 15 -> 37. In a clone
  (4 artifact lines), no spelling moved either register: `rec` removed,
  `took` folded, the two globals chained or reordered, the count read at
  its declaration, the loop as `while` or counted down, a byte table
  pointer, `uint32_t remaining`, the parts walked by pointer, extra
  stores of `table`, and even dropping `remaining`'s inner decrement
  (which should hand ebx to `table` by count, and does not). The local
  copy stays.
- **RegionFindPath** (one `je`): the loop's `if (!head) return 0;` is
  cross-jumped into the first `return 0` in the image and the last in
  ours. A label on the first copy (inside its `if` block) with a `goto`
  from the loop makes VC6 move that block to the end for both; `for
  (;;)`, `do/while`, a label on the second copy and `while (head)` with
  the test after the loop are all worse. It stays at one byte.
- **OptionsDefaults** (15 bytes): `mask = r->which ? flags : other` is
  a web in the image (eax, loaded after the `test` that frees it) and a
  rotation temporary in ours (edx, hoisted above the test), and every
  temporary after it is one register out. An if/else for the select, a
  function-scope `mask`, the test operands swapped, `(mask & bit) ? 1 :
  0`, and the checked store as an if/else all keep it a temporary; the
  if/else stores change the shape.

## ObjAfterMove: the height local's type moves three slots (2026-10-11)

Its one shape hunk is the item height: the image sign-extends it into a
dword slot (`movsx eax, [edi+98h]; mov [esp+1Ch], eax`), tests the full
register, and reads the slot's low byte in the loop; ours keeps an
`int8_t` (`mov al; mov [esp+0Fh], al; test al, al`). An `int32_t` tested
as an int takes ebx and reshapes the loops (53 -> 159 bytes); tested
through `(int8_t)` it stays in memory and the frame becomes the image's
(idx at 0Ch, row at 10h, height at 1Ch; 53 -> 41 bytes) but the test is
`test al, al` and the two inlined ShiftTileCover loops change their
operand order (`mov edx, [ecx]; add edx, eax` becomes `mov esi, [ecx];
mov edx, eax; add edx, esi`), shape 1 -> 4. Swapping the helper's add,
the declaration order, the test on the object's field and `uint8_t`
casts move nothing further. Shape wins; the byte local stays.

## The rest of the register-only list in the lab (2026-10-11, second pass)

Each was cloned (or scored with the per-TU runner where a clone was dear)
and probed until the spellings ran out; none moved. What each one IS:

- **ShotHitsObj** (7 bytes): the seven refusals of the below-the-shot
  branch are cross-jumped into the FIRST `return 0` (the shot-proof one)
  in the image and into the last (the armour switch's fall-through) in
  ours; the five epilogue copies sit at the same addresses in both. A
  label on the first copy, a forward `goto` to a label at the end, a
  label on the sarge or heightAdj copy, and seven plain `return 0`s all
  make VC6 move the labelled block to the end (the same thing
  RegionFindPath did). The `goto`-into-the-earlier-copy trick works for
  switch arms, not for an `if` at the head of a function.
- **DrawMapObjects** (92 bytes, slots only): six spilled locals in a
  different frame order (image, ascending: cell, stride, l0, t0, r, b;
  ours: r, cell, b, l0, t0, stride). Declaration order changes nothing
  (five orders tried); the ORDER OF THE CLAMPS does swap r and b, so
  slots follow the order webs are created in, but no statement order
  produced the image's. Loop forms, `cols`/`last` removed, `d` removed:
  nothing. RunScriptAction's 60 bytes are the same kind of difference.
- **PointerPickMode6** (110 bytes): the image keeps the object in eax
  through every test before the first call and copies it to ebp (`mov
  ebp, eax`) just before the board-hint loop, reading `o->uid` through
  the copy afterwards; modes 4 and 5 do the same (`mov ebp, esi`). Ours
  holds it in edi from the prologue. A local copy in the tail or at the
  call, a `const` or `void *` parameter, the hint body as a second
  inline helper, and the loop reusing the object variable with the
  original saved first are all propagated back to one web.
- **DrawEffectLayer** (220 bytes -> byte-identical, relink 1588 ->
  1589; found in a clone, 12 artifact lines from our build, with the
  aim arrays declared as one struct so VC6 can share a pointer over
  them as it does in the image). `frame = e / 50 + 1; if (frame >= 5) frame = 5;` copies the
  quotient into the frame's register before the shift (`mov esi, edx;
  shr esi, 4; inc esi`) and keeps the frame in esi through the lock
  loops, reloading it three times. **Written as a min over the sum
  seen twice** (`E < 5 ? E : 5`, what a `min()` macro expands to) the
  sum is formed in a temporary, `shr edx, 4; lea eax, [edx+1]`, the
  frame lives in memory and is read once at its use, and every slot
  falls into the image's order. The mini-lab showed the duplicated sum
  is the trigger (`q + 1` twice; a `q` local, two statements, `1 + q`,
  the cast outside, a helper returning the quotient, `uint32_t frame`
  all keep the copy). The B arm's `shr edx, 4; lea esi, [edx+1]` is the
  same thing: the sum assigned and then tested. **Two side effects:**
  the duplicated expression references `g_aimStampA[army]` twice and
  that moves the base register every aim array shares from the A
  point's y to the B point's (in the clone and the real build alike; a
  pointer to the element, `*(g_aimStampA + army)`, a byte-offset form,
  and the point through a local pointer all do the same, while fewer
  references to the B point's y move it back). Reading the stamp into a
  local keeps the base on A but makes the stamp a web (eax) where the
  image's is a rotation temporary that lands in ebp with eax, ecx and
  edx taken by the clock and the multiply; `register`, `const`,
  `uint32_t`, function scope, `volatile` and a clock local instead all
  leave that. **What settles both is the B arm's point:** read from the
  array at each use (`g_aimPointB[army].x`, as the A arm reads its
  own) instead of through `const int16_t *ptB = &g_aimPointB[army]`,
  the base stays on the A point with the A sum duplicated in full, the
  stamp is the temporary in ebp, and the function is exact. So the base
  register's anchor is decided by how the references are spelled, not
  by how many there are: a pointer local to an element counts for the
  arm it is in, and a CSE'd expression over an element counts against
  its own arm. Both are worth trying on any function that walks
  several parallel global arrays through one register.
- **The HudPanel constructor** (100 bytes): the image loads `w->x` into
  eax, stores it to `stop`, and keeps a COPY in ecx for `g_hudPanelX`
  (`mov ecx, eax` between the two stores); ours stores both from one
  register. `stop` stored from the field, the global read back from
  `stop`, a chained assignment, and two locals all CSE back to one.

## The two largest, by the lab and the per-TU runner (2026-10-12)

- **ScriptParseActionRecon** (2,336 bytes): four of its hunks are one
  thing. The image's type-error block for the INTEGER/STRING arms that
  push the kind name from edx (six arms) is shared from the kind-name
  load on: each arm does `mov edx, [name]; jmp` and the block pushes
  edx, the word buffer and the token's address from eax. Ours shares it
  only from the call, each arm pushing for itself, because the arm that
  owns the block (ADDEXP's `int_tail`) holds the token's address in ecx
  -- its tail is written with the step before the store (`n = value;
  (*at)++; act->n0 = n;`), which keeps `*at` in eax across the test, and
  that spelling is there because the plain tail (`act->n0 = value;
  (*at)++;`) makes SETITEMFLAG's tail identical and VC6 merges it away
  (v3 70 -> 113). In the image the two tails differ: ADDEXP tests the
  kind from memory (`cmp [eax], 3`) and SETITEMFLAG through a register
  (`mov ecx, [edx+ecx*4]; cmp ecx, 3`), so they never merge. A mini-lab
  (five variants of a two-arm switch) shows VC6 picks the register form
  for the arm whose error path is cross-jumped from the kind-name load
  into another arm's, and the memory form for the arm that owns the
  block or merges only from the pushes; which arm owns it is layout
  order. No spelling tried gives ADDEXP the memory form, eax and the
  plain tail together: `*at += 1`, `++(*at)`, the test reversed, a
  block-local for the value with the store first, the index in a local,
  and SETITEMFLAG's kind read into a local all leave it (113, or 118
  with the index local). The step-before-store trade stays.
- **Step3Drive** (974 bytes): the speed clamps written as the min/max
  macros expand to (ternaries, with and without the clamp value inlined
  twice, for the two acceleration arms and the two stuck-count arms)
  compile to the same bytes as the `if`s; the turn stamp stored through
  `gun` instead of the re-read row is worse (v3 24 -> 30), and the stamp
  read into a local changes nothing. Its big remainder is still `r` in
  ebp where the image reloads the parameter from its slot.

## ScriptParseActionRecon in a full clone (2026-10-12)

The clone is the real function: `mkspar.py` (job scratch) copies
script.cpp's includes, `ActStep` and the function body into one file,
and `labvar4.py` compiles it IN PLACE under src/game with the relink's
defines, then resolves the object's relocations itself (functions from
the per-TU symbol table, placed data, the `am2_os_*` redirects, and the
clone's own `.text` for the jump tables; string literals get one
sentinel address each) and scores the v3 shape against the image. It
reproduces the real build to the instruction and costs about forty
seconds a variant, against two minutes for the per-TU runner. Three
things landed (shape 70 -> 63, 2,336 -> 1,862 bytes):

- **INACTIVATEREGION**: `n0 = 0; { n = value; subject = n; (*at)++ }`
  recomputed the token's address after the zero store; the image loads
  the value, stores the zero, stores the value, reloads the index.
  `{ n = value; n0 = 0; subject = n; } (*at)++;` is it.
- **The INTEGER/STRING type-error block that six arms share from the
  kind-name load** (eight hunks, one cause). Its owner is ADDEXP's
  `int_tail`. Written as WANT and the stores, ADDEXP's block is
  identical to SETITEMFLAG's INTEGER tail and VC6 merges them; written
  with the step before the store (the old fix for that), the owner
  holds the token address in ecx and every member pushes for itself.
  **A positive test with the stores inside it** (`if (kind == INTEGER)
  { n0 = value; (*at)++; return 1; } TYPEERR`) is not identical to
  SETITEMFLAG's tail and keeps the memory-form test with the address in
  eax, and the members' pushes merge. SETUILOCK then needs its own copy
  of the WANT+stores rather than `goto int_tail`: a goto into the block
  gives it a second predecessor and VC6 allocates it the register way.
  (The image still has SETUILOCK jumping into ADDEXP's block and ADDEXP
  as the owner; ours has SETUILOCK's copy as the owner and ADDEXP's
  error jumping into it -- same code, three hunks of placement.)
- **SETITEMFLAG's RESERVED type error** kept a whole copy of the shared
  block because its kind-name temporary was eax where the shared block's
  tail wants edx; the kind name read into a local is a web, takes edx,
  and the tail joins the block after the pushes as the image does.

A mini-lab of a two-arm switch pinned the register-form/memory-form
rule behind the first of these: VC6 compares the token's kind from a
register when the arm's error path is cross-jumped from the kind-name
load into another arm's block, and from memory when the arm owns the
block or shares it only from the pushes; which arm owns it is layout
order.

Two more landed the next day (1,862 -> 1,834 bytes; lab hunks
21 -> 12), both the natural spelling:

- **MAKESMOKE and MAKEFLAME**: `if (kind != INTEGER) return 1; n0 =
  value; (*at)++; return 1;` gives the early return an inline
  epilogue of its own before the store. `if (kind == INTEGER) { n0 =
  value; (*at)++; } return 1;` makes the test a forward jump to the
  arm's final return 1, which is the block SETM80 and SETDAMAGEPAD
  share -- the image's `jne 0x442af4`.
- **SETITEMOWNER**: `if (n0 < 0) return 0; return 1;` is a `setge`
  (and so are a local, a ternary and the reversed test). With the
  INTEGER path and the colour path as one if/else joined on a single
  `return 1`, the join is duplicated into both and the sign test stays
  a branch to the shared return 0, as the image has it.

**The end-of-script block's owner** (the one hunk that moves the whole
function's addresses): the image has the shared block at MOVEITEM's
`eos:` label, falling into `default: return 0`, with TRIGGERDELAY's
refvar path and FIREWEAPON each holding a PRIVATE copy; ours puts the
shared block at TRIGGERDELAY's copy. Mini-labs of a switch with the
same shape pinned the mechanism:

- A labelled block with many jumps to it merges into the FIRST
  identical block in layout order, whatever that block's origin -- an
  explicit inline copy, `ActStep`'s inline expansion, or a copy VC6 made
  itself -- once the label block's own fall-through predecessor has
  been redirected (MOVEITEM's `return ScriptLocation()` goes to the
  shared call at the function's end, so MOVEITEM becomes `jge eos; jmp
  place` and the eos block is free to move). A fall-through-only copy
  later in layout is never merged (FIREWEAPON's and CREATETROOPER's
  `ActStep` copies stay private in ours as in the image).
- The image's private copies are therefore made AFTER that merge. VC6
  does make such a copy for `STEP(); L:` when the labelled block `L`
  is moved away: in the lab, UNITFIRE's `place:` moved to the function
  end gives UNITFIRE `jl place` and a private eos copy, while
  FIREWEAPON's `STEP(); return ScriptLocation()` (redirected to the
  same block) is left as `jge eos; jmp place`. Which arm the copy lands
  in does not follow the label's position: with `place:` in either arm
  the copy is UNITFIRE's, where the image has it in FIREWEAPON with
  UNITFIRE's step cross-jumped into FIREWEAPON's.
- What was tried against this and failed: TRIGGERDELAY's refvar as
  `STEP(); goto trigger_uid` (cross-jumps into TRIGGER's identical
  step), as `ActStep`, as the positive test with the log inline, with
  `trigger_uid:` in TRIGGER's arm or after the refvar's step; the
  integer path's check as `goto eos`; MOVEITEM without the `located:`
  label and DROPITEM with its own test in four spellings; FIREWEAPON
  as `STEP()` with `place:` in either arm. Every all-`goto` spelling
  hands the label block to the first `jcc eos; jmp X` claimant in
  layout (CREATETROOPER's `ActStep` when TRIGGERDELAY's step has been
  cross-jumped away). The label's own source position is irrelevant:
  `eos:` and `default:` moved to the end of the switch, with MOVEITEM
  as `if (*at >= count) goto eos; return ScriptLocation()`, compiles to
  the identical function. An explicit `return 0` on the label instead
  of the fall into `default:` makes VC6 merge about eighty
  instructions' worth of return-0 epilogues into it (2,997 instructions
  against the image's 3,087). STEP as an inline function at every site
  (`if (!ActStep(ctx, at)) return 0;`) merges every copy into the
  first one in layout, TRIGGER's. The remaining unknown is what kept
  MOVEITEM's block as `jl place; [eos]` in the original -- i.e. what
  stopped TRIGGERDELAY's copy from existing at merge time.

The other four helpers -- WANT, TYPEERR, VARNAME and VARNAME_EQ -- are
inline functions now (`ActWant`, `ActTypeErr`, `ActVarName`,
`ActVarNameEq`): VC6 expands them to the same code as the macros, to the
instruction, in the lab and in the real build. Only STEP has to stay a
macro, for the `goto eos` above.

**ADDEXP and SETUILOCK, both as the plain WANT and stores** (1,834 ->
1,498 bytes; lab hunks 12 -> 5, v3 62 -> 14). The first clone session
gave ADDEXP a positive test because ADDEXP-as-WANT merged SETITEMFLAG's
identical tail into it; with MAKESMOKE/MAKEFLAME and SETITEMOWNER in
their final forms that merge no longer happens, and it was the positive
test that kept SETUILOCK's copy as the block's owner (SETUILOCK's
positive form alone: 25 hunks; SETITEMFLAG positive as well: 20). With
the two tails written alike VC6 keeps the LATER copy, ADDEXP's -- its
error block is the one the six other arms jump into -- and SETUILOCK's
whole tail becomes `jmp` into it from the token load on, as the image
has it. FIREWEAPON's and UNITFIRE's kind tests, register-form and
memory-form the wrong way round before, take the image's forms as a
consequence, which says those forms follow the function's overall
allocation and are not a property of the arm.

Still open: SETFACING's `xvar` store before the step merges its
VARNAME block into SETGUNFACING's identical one (the image keeps both;
a positive test, a message local, two separate ifs, `*at += 1`, the
store through `act->xvar` directly, a function-scope or `long`
variable all still merge), and the end-of-script block above. Those
two are the whole remaining shape difference of the function.

## Step3Drive, 974 -> 382 bytes (2026-10-12)

A review of the two listings by a second model, then each suggestion
measured on the real build. Everything that moved the function was a
change to what VC6 can keep in a register across the calls, not to the
logic:

- **The turret's new aim in a local** (974 -> 836): `r->aim = (uint8_t)
  (r->aim + turn); swing = AngleDelta(..., r->aim)` reads the record back
  and leaves `speed` out of ebp; held in a local `speed` takes ebp across
  the function as the image has it. As a chained assignment
  (`aim = r->aim = (uint8_t)(r->aim + turn)`) the temporary also stays in
  a register instead of a stack slot (struct 50 -> 40, 402 -> 382).
  `r->aim += turn`, an assignment inside the call, an int local and
  `register` all measured worse or no different.
- **The first block through `gun`** (`animRec = &gun->animPlaying`,
  `gun->headingBias`, `gun->turnStamp = ...`): VC6 does not CSE
  `o->rowSet.rows` across the calls, so each use through the object is a
  fresh load and `gun`'s web ends early. Measured against the same block
  with r in ebp this had been worse (33 against 37); with `speed` in ebp
  it is better (v3 38 -> 19), which is the lesson: re-measure an old
  "worse" once the competing web has moved.
- **The gun facing assigned straight from the second `RoundTo8`** instead
  of through `second` and a copy: the result goes straight into the
  variable the image stores it into, frees ebx, and lets the scheduler
  split `sub eax,[edi+0x58]` into a load and a `sub` (974 diff -> 342
  alone).
- **The two distances compared in one expression**: within one tree VC6
  hoists the invariant `lea ebx,[esi+0x12]` ahead of the first call;
  across two statements (`d1`, `d2`) it does not.
- **The straight-ahead fire arm copies `r->spot` whole**, as the second
  arm does.

Together: 369 -> 203 bytes, and the relink 836 -> 402. Still different:
the blocked branch's load of `o->stuckCount` ahead of `facing = o->facing`
(five permutations of the three statements and `++`/`+ 1` spellings do
not move it) and where `o->facing` is stored after the second AngleDelta.

**SightScan 910 -> 808 and FindPath, from the same kind of review**
(2026-10-12). Three things moved SightScan: the box's left edge first
(VC6 computes the first of a +/- pair into a copy and the second in
place, which decides which edge is `lea` and which `sub`);
AiSightTrace's walk as a `for` over `n` and `i` left uninitialised and
`g_tileAttrs` indexed at each use (the image reloads the global every
iteration, and has no `n = 0` store before the trace call); and the
function's result held in a variable that starts at zero, so the zero
register is shared with the tests. Measured and not kept: `cand` cleared
once at the loop top only, the kind-7 arm setting `cand` first and
dropping its own null test, a second pointer for the allied arm's target.
All three were within a few bytes either way. FindPath: the budget
refusal as the fall-through of the heuristic compare with `fail:` inside
its body, the goto-partial form, only the first heuristic held, the
doubling split into a shift and an add, and `nb` held across the rule
call with `state` read through `t` all scored worse or the same (397 or
342 against 287).

**The end-of-script block, second look (2026-10-12).** A review of the
image found the string pushed at FOUR sites, not three: TRIGGERDELAY's
common step, CREATETROOPER, MOVEITEM (the only one every `jge` reaches,
falling into `default:`) and FIREWEAPON. Every private copy ends in its
own fused `xor eax,eax; pop; pop; pop; pop; ret`, so the private copies
are not duplicates of the shared block: they are fall-through copies of a
`jl` whose continuation lives elsewhere (TRIGGER's uid call, the army
lookup, `place:`). Mini-labs of a nine-arm switch showed that no `goto eos`
spelling keeps the labelled block at MOVEITEM, and that STEP as an inline
`if (!ActStep(ctx, at)) return 0;` at every site, with no `eos:` label,
does: the shared block then sits at MOVEITEM exactly as in the image and
CREATE* keeps a private fused copy. In the real function that form, with
TRIGGER returning its uid call directly and TRIGGERDELAY ending in its own
call or one common step-then-call tail, cuts the lab to 7 hunks (from 13
for the label-free form with `trigger_uid`) but v3 rises from 14 to 23:
TRIGGERDELAY's check still merges into TRIGGER's, jumping to TRIGGER's
`cmp eax,[edi+4]` (memory form) where the image has the register form
`mov ecx,[edi+4]; cmp eax,ecx` in both. What differs is the instruction
before the cross-jump point, so the next thing to try is a spelling where
the two checks begin differently, not a different placement of the block.
Not landed.

The same review of other functions: DepthInsert's append was linking the
new node to itself (APPEND took the cursor global the macro had just
repointed); fixed, 460 -> 468. FlowRecvMessage: inlining the two pool
returns, `break` for the sequence mismatch and for the second FlowQ log
all scored worse (215 -> 233 to 309). AiTrooperStep: reading the copies
through `pos`, the store order of `moveTo`/`moveEnd`, the `into` temp and
the BeginMoveTo store order scored the same or worse. The MpPanelScreen
constructor's map-list counter set before the lookup: 584 -> 578.

**The near-matches, reviewed (2026-10-12).** LoadOptions, MovieStart,
PausedFrameStep, WndProc, DirtyCollect, SelectionClick and RegionFindPath
each differ by one to three instructions and have zero shape difference
(a register, a frame slot, or one block's position). A second-model review
of each proposed new spellings, all measured on the real build and none
moving a byte: LoadOptions' success arm with its own `return`; MovieStart's
`movie` alias reused for the delete, the test written on the global, a
function-scope holder; PausedFrameStep's row and walker as a chained
assignment, as duplicated expressions, in reverse roles, and with the row
from `active - 1` (the last three cost 50 to 64 bytes); WndProc's
`PAINTSTRUCT` hoisted to function scope (37 bytes worse) or declared first
in the paint helper; DirtyCollect's walk from the sentinel record (133
worse), with the record bound after the test, and with a 16-bit index;
SelectionClick's casts dropped one at a time and the zero point passed
directly (42 worse); RegionFindPath with the guards nested around the
whole body and one trailing `return 0` (59 worse). Where a spelling
changes nothing it is because VC6 canonicalises it; where it changes
something it is worse, so these seven are at the edge of what the source
can say and the remaining difference is allocator tie-breaking.

**A review round over the mid-sized functions (2026-10-12).** Landed:
ListMaskAction's pixel row as `int32_t` (342 -> 328; a word costs a `movsx`
at each use), SelectFirePose's bazooka standing raise stored in place with
one `goto` into the shared copy (304 -> 300), HudSquadUpdate's Sarge arm
through a record pointer (278 -> 276), UpdateTrooperAction's pickup
quantity subtracted from the weapon's own count and then copied (408 ->
382). UpdateTrooperAction also had a behaviour bug the review found from
the image: CanPickUpWeapon's last argument is `already` (the object at the
point is the weapon the unit holds), which the original zeroes once after
the null test on the first object, passes to every call, and tests after
the walk to decide whether to clear the held weapon; with nothing at the
point it skips all of that. The reconstruction used it as a scratch
quantity and cleared the held weapon only when the point was empty. Fixed;
the flag has to be BLOCK-scoped inside the `if (at)` or it takes a frame
slot of its own (464 against 408). Not landed, each measured on the
relink: ObjHitMaskAction's cell walk by index only (392), its word
`end`/`col` as int32 (378), the flag before the ring (392); LoadMap's SCEN
count before the parse (378 -> 494); VehicleUpdateAppend's `flags = 0` moved
after `dx`/`dz` (328 -> 584); SelectFirePose's raise-kneel stores written in
place (304 -> 614); UpdateTrooperAction's facing delta from the record
(382 -> 390) and one `stop:` join for the four stop arms (384).

**A third review round (2026-10-12): two more behaviour bugs, one more
byte-identical function.** DamageTrooper is byte-identical: the armour scan
stops at the FIRST EMPTY inventory slot (the image's `je` on an empty uid
goes to the loop's exit); ours skipped empty slots and kept scanning.
ViewUpdate 112 -> 10: both glide products written with the one `step` local
(a second spelling of the same product made VC6 keep a second temporary in
eax). ScrollMapCache 252 -> 230 (the shift inside the negative arm of the
destination), HudPostUpdate 206 -> 134 (the hold arm first, unbraced, in the
ours arm, so the three identical callbacks stay as three copies).
Measured and not kept: PointerFriendTail with a `vehicle` copy and `o`
reused as the loop cursor (no change in any of the four siblings);
AddToItemList's id loaded straight into its register, ObjRemap's `y` as
int32 and its `y`/`x` order, RowUpdate's `cols` read once (208 -> 220),
EncodeGlyph's base-pointer cast (182 -> 196), ScrollMapCache's abs split
(230 -> 362), ViewUpdate's dy first (10 -> 46) and its snap stores
separately or reloaded (60, 166), DrawSelection's `top` (156 -> 178).

**A fourth review round (2026-10-12), behaviour first.** The reviewers were
asked to compare control flow against the image before proposing bytes. One
more real difference: RunScriptAction's `addtovar` (0x22). The image reads
the variable into a slot nothing reads back and pushes n0 (loaded into edi
before the call), so the amount added is always n0; the source read the
same local back, which VC6 compiles to the same thing by accident (it keeps
n0 in a register across the call) and GCC does not, so the native port and
the injected GCC build added the variable's value. The explicit form costs
the VC6 build 326 bytes (60 -> 386: `v9` stops being address-taken and the
frame packs differently), so it is `#ifdef _MSC_VER` split. The other 21
functions reviewed had no behaviour difference. Byte-level: DrawMapObjects
92 -> 68 (the subdivide stores in the order the image emits them, which
VC6 follows in source order), and no change or worse for BoxAction's
pointer before the guard, BlockWeightRoute's first distance in a local
(54 -> 92), PointActionC's loop on `rows` (52 -> 72), StepRowAnim's operand
order, Update3DAudioVolumes without the `buf` local, OptionsDefaults'
self-reused mask, MissionEnded's two store orders (14 -> 20), AddToItemList's
compound form.

**A fifth round (2026-10-12): named locals that were not in the original.**
The largest single gain of the review rounds was deleting two locals.
AM2_MpPanelScreen's chat list ended `AM2_ListBox *cb = *slot; AM2_ListRows
*rows = cb->rows; cb->selected = rows->count - 1;`; written as
`(*slot)->selected = (*slot)->rows->count - 1;` it went 576 -> 388, because
a named web leaves the rotation the later `topRow`/`visible` tests share.
Worth sweeping the other big functions for the same shape (a local that
holds a pointer already reachable through a global or a slot, used once or
twice). Also landed: the map-list test folded into the loop condition
(`for (; maps && i < maps->count; i++)`, 388 -> 360), LoadMap's OLAY walk by
index with no pointer local (378 -> 318), ScriptPad's HALFTRACK as its own
arm before CONVOY (512 -> 502: the bodies are laid out in token order and
the identical arm is cross-jumped into GREY's) and its first centroid's
`total` after `sy` (496), SightScan's two counts cleared allied first
(796), the distance's x delta through `dx` (786), and `cand` set first in
the kind-7 arm with no clear at `in_sight` (774). Not kept: ScriptPad's
centroid body reordered (502 -> 956), its trigger loop as do-while (804),
LoadMap's hoisted record pointer in the build loop and the reveal grid
through a local (no change), SightScan's best/slack store order (822) and
`result` first in the alt arm (806), MpPanelScreen's `name` local dropped
(no change).

A sweep of the other 55 differing functions for the same pattern (a
pointer local initialised from a global, a field or a parameter and used
once to three times) inlined each candidate at its uses and relinked:
RunScriptAction `num`, LoadMap `attrs`, Step3Drive `g`, ListMaskAction `h`,
PausedFrameStep `o`, AirFrameDraw `hot`, StepRowAnim `want`, CommSend
`slot`, VehicleUpdateAppend `m`, HudPostUpdate `type` changed nothing;
RowUpdate `rc` (208 -> 248), PausedFrameStep `rec` (4 -> 52),
OptionsDefaults `parent` (32 -> 64), EncodeGlyph `dst` (182 -> 184) and
FlowRecvMessage `m` (540 -> 560) were worse. So MpPanelScreen's `cb`/`rows`
pair was a one-off, not a pattern.

**A sixth round (2026-10-12), second passes with the raw diff.** Landed:
FindPath's start-node `h` from `fromTile` (one web for the value, 834 ->
800) and `goal = to` first in the fresh-search arm (800 -> 774), RowUpdate's
rect in natural order with no pointer to it (208 -> 176), AiTrooperStep's
`moveUntil` before the `pt` copy (486 -> 484). Measured and not kept, each on
the relink: FlowRecvMessage's `seq != expected` as a `break` (540 -> 586),
either pool return inline (594, 740) and `#pragma optimize("s")` around the
function (2,244); FindPath's `nb` as the value for all five word stores
(964), the doubled step through an int32 block local (-4, nothing on top of
`fromTile`), the budget refusal as a `break` out of the loop with the shared
`PathChargeFrameL` after it (1,046); Step3Drive's `aim` as the assignment's
value passed to the call (382 -> 532); ObjHitMaskAction with a `cells` local
and no `ci` (474), plus int32 `end`/`col` (596); ObjRemap's `cl` clamp first
(246); HudSquadUpdate back to `recs[slot]` (278); AiTrooperStep with the
no-region arm folded into `set_move_to` (860); VehicleUpdateAppend with
`flags` declared bare and zeroed before the position test, with and without
the `age` locals (562); SelectFirePose with either raise block returning in
place (no change).

**ScriptParseActionRecon 1,498 -> 230 (2026-10-12): no end-of-script label,
no gotos to a shared tail.** A lab session run by a second model (the
shape metric masks absolute addresses, so its "1 hunk" was shape only; the
relink is what counts) found that the whole placement problem was the
`eos:` label. With NO label and every step and end test written as an
inline call used as an early return (`if (!ActStep(ctx, at)) return 0;`,
`if (!ActAtEnd(ctx, at)) return 0;`), VC6 folds the eighty-odd identical
`log; return 0` copies into the one beside `default: return 0` (MOVEITEM's)
and keeps the fall-through ones private (CREATETROOPER, FIREWEAPON,
TRIGGERDELAY), which is the image's layout. The pieces that make it work:
TRIGGER returns its own uid call; TRIGGERDELAY's refvar path has its own
step and `goto trigger_uid`, so the common step is a two-predecessor join and
only the call merges with TRIGGER's; MOVEITEM and DROPITEM each have their
own end test; SETFACING is back in the natural order of its two stores.
The last 570 bytes came from one more `goto`: SUSPENDAI's `goto
resolve_subject` into SETCAMERAFOCUS's `return ScriptResolveName` did not
make the first copy survive (as an older comment claimed); VC6 retargeted
its jump to UNDEPLOY's identical copy, which made two short jumps long and
shifted the rest of the function by four bytes. Plain returns in SUSPENDAI
and REVIVEAI point both jumps at the first copy, as the image does (800 ->
230). Lessons: a shape score that masks addresses cannot see a jump going to
the wrong one of several identical blocks, so check the referrers of each
copy in the raw listing; and a `goto` into a block that has identical
twins is retargeted by VC6, so it is not a way to choose the survivor.
What is left: SETSTRATMAP's shared `(*at)++; return 1` tail begins at
`mov eax,[esi]` in the image (four facing-arm jumps land on it) where ours
has already CSE'd the load, so the jumps land one instruction later and the
`pop edi` schedules after the `inc`.

**The relink-metric lab (2026-10-12): the largest gains of the whole effort.**
Letting a second model run experiments itself, scored by the RELINK edit
distance rather than the shape metric, produced: SightScan 774 -> 528 (the
kept list as the scoring cursor, `for (; keep; keep = keep->queryNext)`,
which removes a register copy at the loop entry and gives the whole prologue
the image's allocation; one score block inside the kind-7 arm; the image's
store orders), ScriptPad 496 -> 280 (the trigger loop NESTED under
`if (RESERVED) {...} else break;` -- VC6 lays a loop's exit blocks out in the
order the exit branches are discovered, so which exit is written first decides
the fall-through block; the second centroid's counter in the `for`),
FlowRecvMessage 540 -> 454 (the deliver path as the `if` body so the mismatch
is a jump into the shared epilogue instead of a private copy), AiTrooperStep
484 -> 134 (the shared `set_move_to`/`two_waypoints` labels removed and the
four store tails written out; VC6 cross-jumps the copies itself and the
callee-saved allocation flips to the image's), FindPath -2 (an expression
index compiles `[idx+table]`, a variable index `[table+idx]`), DepthInsert -4.
The pattern across all of them: a hand-made shared label or a `goto` is
usually what the original did not have, and removing it lets VC6 build the
sharing itself; and loop and branch STRUCTURE (which exit is written first,
whether the body is the `if` or the `else`) decides block layout. Three
rules for the next function: measure on the relink, not the masked shape
score; read the raw referrers of every identical block; and try the
nested-else form of any loop with an early `break`.

**The fast screener (2026-10-12).** A per-translation-unit compile that
reproduces the relink's byte distance exactly in under a second
(`screen2.py` in the job scratch: apply an edit list, compile one unit with
the hosted VC6, diff each named function against the image, restore) turned
the search from one experiment every ninety seconds into hundreds. With it
the structural rules above became systematic, and the results were:
FindPath 772 -> 172 (the budget block INSIDE the loop with the shared
refusal as its fall-through and `fail:` inside its `if`; the entry guards as
ONE `||` condition, since separate `return 0`s each get a private epilogue;
`nb` as the value for the stores; the heuristic split into a call and a
multiply), FlowRecvMessage 454 -> 18 (the parameter named `node` with no copy;
all `goto pool` sites written out; the three "Can't find FlowQ" sites `goto
fail` into a `fail: return 0;` INSIDE the fourth's `if` -- the label's
placement is the only thing that moves the surviving copy, break and return
at any site are byte-identical; per-arm block-scoped locals, which is what
packs the frame slots the image has), LoadMap -24 (the MHDR arm first),
ObjHitMaskAction -16 (int32 `col`/`end`, no `ci`, the flag before the ring),
SightScan and ScriptPad as above. Two properties of VC6 worth keeping: a
labelled block stays where it is written only when no LATER `goto` targets
it, otherwise it moves to just after its last predecessor in source order;
and a block-scoped local in a `case` shares a frame slot with its sibling
cases where a function-scope one does not.

**More from the screener (2026-10-12).** Byte-identical now: CommSend (the
guard's two tests in the image's compare order, then an `if`/`else` on
E_INVALIDARG; a `return 0` in the guard costs 66, `goto failed` must stay),
the MoviesScreen constructor (the index read back through the button, no
`idx` local), VehicleUpdateAppend. VehicleUpdateAppend's key: VC6 counts a
variable's uses BEFORE it cross-jumps identical arms, so an accumulator that
loses its callee-saved register to a value with fewer uses (`flags` against
`seq`) wins it back when the original's `||` guards are written as else-if
arms with the body repeated (the three `flags |= X` copies merge afterwards);
with the single-`|=` form it needed twelve artificial extra reads. Together
with the action record split into its own second `if (flags & 0x80000000u)`
and `flags = 0` placed after the `dz` abs, 328 -> 0. Also: HudPostUpdate 134
-> 32 (`repeat` bare and zeroed before the leader lookup; the world point
through an int32 sum), ObjAfterMove 72 -> 2 (`uint32_t height` tested through
an `(int8_t)` cast keeps it on the frame), ObjHitMaskAction 330 -> 246
(`marked` bare and zeroed just before the row loop), ScrollMapCache 226 -> 204
(the cache surface in a local, loaded back for both BltFast pushes).

**Arg slots and loop shape (2026-10-12).** EncodeGlyph 164 -> 38 came from
asking what variable naturally lives in the image's spill slots: the first
parameter is the BYTE cursor that gets advanced (its arg slot then holds
`written`, a local copy `out` is what gets spilled), the row-offset pointer is
a second local, the key is read from the global, and `offs` is stepped before
`p` at the loop end (the prototype in font.h changed to `uint8_t *buf`).
BoxAction 88 -> 0: VC6 /O2 turns a plain byte-store loop into the memset
expansion (`rep stosd; and ecx,3; rep stosb`), and with a nested loop the row
address is computed BEFORE the inner guard, which no `memset(...)` + `if`
spelling reproduces (VC6 sinks the address into the guarded block); the
image's shape is `for y { at = ...; for x { cells[at++] = CELL; } }`, with a
separate `y` kept in ebp and spilled to the dead arg slot. AddToItemList 104
-> 64 (the two uid arms are coupled: they cross-jump when identical, so the
case-0 arm uses the index form with the pointer formed before the increment
and the own arm the index form with the pointer last). PointActionC 52 -> 30
(`rows` re-read after the first row's stores), ScrollMapCache 204 -> 168 (the
two src stores in the image's order).

Not source-addressable, found by the last pass: Step3Drive's apparent "extra
call" and the RunScriptAction/HudSquadUpdate tails are jump-table data decoded
as instructions; RunScriptAction's 60 is frame slots only. (An earlier version
of this paragraph also blamed 2-byte `jmp`/`jne` differences in OnBootCamp,
OnSinglePlayer, OnStartWar, StartSelectedGame, OpenTitleScreen and
CommLobbyStart on the GOG patches. That was wrong: the comparison reference
already has the six patched jumps put back (docs/binarypatches.md), those
functions are byte-identical in the relink, and none of the six patched sites
differs in any function.) And the
PointerPick family's remaining difference is the object POINTER copied into
ebp at the board arm, which no source form reaches (a plain copy, const, void *,
uintptr_t, a helper parameter, a loop cursor, two live defs: all propagate back);
the uid value local landed instead (256 -> 214, 138 -> 108, 110 -> 64).

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
