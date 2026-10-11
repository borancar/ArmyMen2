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
  explicit `goto triggers_done` (0) all lose or do nothing.
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
