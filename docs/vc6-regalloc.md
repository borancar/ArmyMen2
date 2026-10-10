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

UpdateTrooperAction is still open. Its three stop copies merge in the skip
form too (and in a lab clone). The image's copies differ from ours in one
visible way: each one reloads `out` from its argument slot for the `bearing`
store (`mov edx, [esp+0x40]`) while `state` and `hit` go through `ebp`. That
suggests two pointer variables in the original, but a local alias for
`state`/`hit` was propagated away.

## Not yet understood

- Exactly how the field a walking pointer is anchored on is chosen.
- Why, in OptionsDefaults, the image allocates `bit` before `mask` while
  still loading `which` (through the ternary) first. Every order of its five
  statements, both signednesses and three spellings of the test were tried
  in a clone (`checked` stays 26 lines off the image at best).
- Whether the hidden C2 switches change any of this.
- What makes VC6 split a pointer's live range around a loop. In
  PointerPickMode0/4/5/6 the image keeps the object in `esi` up to the
  shared friend tail, then copies it to `ebp` (`mov ebp, esi`) so the
  selection loop's index can take `esi`. That leaves `ebx` free, and the loop
  holds the 0x400 flag in it (`test [eax+8], ebx`). Ours keeps the object in
  `ebx` throughout and tests the flag as a byte (`test ch, 4`). A copy of the
  parameter at function scope, a copy inside the inline tail, the tail's
  board branch as an inline of its own, and a `const` parameter were all
  propagated away and changed nothing (2026-10-10).

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
