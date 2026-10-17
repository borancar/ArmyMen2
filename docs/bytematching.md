# Matching the original byte for byte

How the reconstruction is brought from "behaves the same" to "the hosted VC6
compiles it to the same bytes as the 1999 build". This is the working method,
the tools, and the catalogue of source shapes that moved the count. Register
rules live in `docs/vc6-regalloc.md`; this file is the loop around them.

State when this was written (2026-10-15): 1613 of 1646 functions
byte-identical, 33 differing, 2,132 bytes of total edit distance. The live
numbers are in `STATUS.md`.

## Goal and metric

The goal is a relink whose function bytes equal `ArmyMen2.exe`'s. Byte equality
is a proof of reconstruction that no A/B run can give: if the compiler emits the
same bytes, the source is the original's up to things the compiler erases.

**The metric is the relink byte edit distance**, per function: difflib over the
bytes our relink emits and the bytes in the image. Zero means identical. It is
what `make vc6-relink` reports, and it is the only metric to trust.

The older shape metric (the lab tools' masked listings) hides absolute
addresses and register numbers. It misled repeatedly: a function can score "no
shape difference" while a wrong jump target or a swapped register keeps it
hundreds of bytes away. Use shape listings to read a diff, never to score one.

A single misplaced block or wrong jump target shifts everything after it, so
distances are lumpy: a one-line change moves a function from 770 to 170 bytes,
and a "fix" can raise the number while being right. Read the raw instruction
diff, and for every block that is identical in both, read what jumps to it.

## Tools

| tool | use |
|---|---|
| `make vc6host`, `make vc6-relink` | build the PE host; run the full relink and print the per-function result |
| `tools/relinkscreen.py` | the loop's instrument: baseline, one-second single-unit screen, full confirm |
| `tools/vc6asm.py FILE.cpp` | compile a snippet with the real compiler and print each function's code |
| `tools/vc6lab.py` | scratch compile of a cloned function with stub declarations, for experiments that must not touch the tree |
| `src/vc6/bytematch.py` | the relink's comparison and listing code |
| `docs/vc6-regalloc.md` | measured register-allocation and tie-break rules |

`relinkscreen.py` needs `build/vc6host` and `VC6_ROOT` as for the relink. Its
four modes:

```
tools/relinkscreen.py --baseline                     # full relink, saves build/relink-base.json + relink-syms.pkl (~15 s)
tools/relinkscreen.py --list                         # differing functions, largest first
tools/relinkscreen.py SRC EDITS.py FN [FN ...]       # screen one unit, print FN OLD -> NEW (~1 s), restore SRC
tools/relinkscreen.py --confirm SRC EDITS.py         # same edit through a full relink; every changed function
```

`EDITS.py` defines `E = [(old_text, new_text), ...]`. Every occurrence of
`old_text` is replaced, so anchor it with enough context that it does not also
match a sibling function. The screen reuses the symbol tables saved by the
baseline (`RELINK_ONLY`, `RELINK_LOAD` and `RELINK_SAVE` in `src/vc6/relink.py`;
inert when unset), which is why it takes a second instead of fifteen, and it
reproduces the full relink's distances exactly. Constructors and other members
are named by their mangled name in the json, for example `??0AM2_MpPanelScreen`.

Only one compiling job may run at a time. The screen edits the source in place
and restores it afterwards, and the relink rewrites listings, so two at once
corrupt each other's results.

## The loop

1. `--baseline` after every commit that changes code, so the screen's reference
   is the tree you are editing.
2. `--list`, pick a function. Choose by what the diff says, not by size: one
   misplaced block explains hundreds of bytes.
3. Read the instruction diff of the original against ours. Decide what the
   original's source must have been, in terms of control flow first.
4. Write the change as an edit list and screen it. A change that does not lower
   the number may still be right; read the new diff before discarding it.
5. `--confirm` every win. The screen compiles one unit, and a change to a shared
   header or a sibling that shares the edited text can move other functions.
   Check the `lost`/`gained` line and every other changed function.
6. Apply the edit, plain `make` (read the warnings, never grep for `error`),
   relink, `--rebase`, commit with the new count in the message.
7. Update `STATUS.md` when the count moves.

No game runs, `make check` or lockstep are needed for this loop; those are for
behaviour, and a byte change that keeps the compiler's output equal to the
image's cannot change behaviour.

**Behaviour comes first.** When the control flow of the image disagrees with the
source, that is a bug in the reconstruction, and fixing it beats any byte win.
The method found real ones this way: a depth-sorted insert that linked a new node
to itself, a held-weapon flag that was never set, an armour scan that did not
stop at the first empty slot, a script `addtovar` that must always add the first
operand.

## What moved the count

Each rule was found on a real function; the figures are edit distance before and
after.

**Control flow**

- *Remove hand-made shared labels and `goto`s and write each site out.* The
  compiler merges identical tails itself, so a hand-merged tail is a different
  layout. ScriptParseActionRecon 1498 to 218, AiTrooperStep 484 to 134.
- *A `goto` into a block that has identical twins is retargeted by the
  compiler.* To get a particular copy, write the return where it belongs
  instead of jumping to one. The suspend and revive AI actions 800 to 230.
- *A labelled block stays where it is only if no later `goto` targets it;
  otherwise it moves after its last predecessor.* Where a label sits inside the
  last site's `if` chooses which of the identical tails survives.
  FlowRecvMessage 540 to 18.
- *`else break` nesting decides where the loop exit block lands.* ScriptPad 496
  to 280.
- *Put the budget test inside the loop and combine the entry guards into one
  `||` condition.* FindPath 772 to 172.
- *Write the success path as the `if` body.* Reversing the condition changes
  which side falls through.
- *Per-`case` block-scoped locals*, and name the parameter directly rather than
  copying it into a local.
- *Guard tests in the order the image compares them, then if/else.* CommSend 26
  to 0.

**Variables and registers**

- *The loop's own variable instead of a copy of it.* SightScan 774 to 528.
- *The compiler counts variable uses before it merges identical arms.* An `||`
  guard written as else-if arms with the body repeated raises a variable's
  priority the way the original's separate arms did. VehicleUpdateAppend 328 to 0.
- *A bare declaration plus a zeroing statement right before first use decides
  register-web order*, where an initialiser at the declaration does not.
- *A parameter may be used differently from how it looks.* The first parameter
  of EncodeGlyph is the byte cursor; the prototype became `uint8_t *buf` and the
  function went from 164 to 38. Dead argument slots are used as spill slots.
- *A value taken at the top of an arm and used after calls* is a local, not a
  re-read: PointerFriendTail's uid, PointerPickMode0 256 to 214.
- *A `uint32_t` tested through an `(int8_t)` cast stays on the frame.* ObjAfterMove
  72 to 2.
- *Word-sized locals cost a `movsx`*; a byte or int16 parameter changes the
  widening the same way.

**Loops and indexing**

- *A plain byte-store loop becomes a `memset` expansion; a nested loop that
  computes the row address before the inner guard does not.* BoxAction 88 to 0
  with `for y { at = ...; for x { cells[at++] = V; } }`.
- *`g[x & 0xFFFF]` compiles `[idx+table]`; a variable index compiles
  `[table+idx]`.* The spelling of the index chooses the addressing form.
- *An index read back through a stored field* rather than kept in a local.
  MoviesScreen constructor 48 to 0.

**Structures**

- A stubborn mismatch is usually a stub struct reached through `base + OBJ_OFF`
  casts. Flesh the struct out and use typed members before blaming the code
  generator. Member signedness follows how the original reads it.
- Aggregates alias as one object, which changes register choice around them.
- Pointer subtraction on elements is not a signed byte divide.

For a language-specific trick that does not change codegen, prefer `static
inline` functions to macros; measure before and after.

**Open: SightScan's scoring block position.** The image places the block right
after the Kind7 arm, with the sight test and the allied arm jumping back to
it. VC6 moves it to the end of the `if (!ObjsAreAllied)` branch whenever any
later code jumps back with a `goto`; with every `goto score` turned into
`continue` (behaviour broken, diagnostic only) it stays after the Kind7 arm.
About 150 source variants did not reproduce the image's placement while keeping
the behaviour: inline functions at each site (VC6 merges only some copies), a
`do`/`for`/`switch`/`while (cand)` wrapper, forward gotos, the allied arm
first, struct-grouped candidate variables. The placement at the loop top
costs about 360 bytes elsewhere. Writing the scoring block as the join after an
if/else (the Kind7 arm and the sight arm both fall into it, no `goto score`) took
the function from 466 to 438 but leaves the block at 0x5e4: VC6 puts the sight arm
directly after the Kind7 test in every form tried and the block after it. The one
form that places the block at the image's offset is a duplicated tail (606), which is
not the image's code. A fuller attempt at the loop-top form (candidate state
hoisted to function scope, the reset placed before the loop, no per-iteration
initialisers for the bearing and range, which are only read when a candidate
exists) reached 834 with the block at 0x250, still well above the 466 of the
current source, and it cannot make the Kind7 arm fall into the block with the
later arms jumping back, which is what the image does. Writing every `continue` as `goto next`
(one label at the loop end) took the function from 528 to 466.

**Resolved: the PointerPick friend tail takes the uid as a parameter.**
A mode-private copy of the tail was declined (the image has one shared tail).
What worked is a second parameter: each of the four callers reads `o->uid` at a
spot of its own (Mode0 and Mode5 after their early refusals, Mode4 at the top,
Mode6 at the call) and passes it in. One shared tail, four spellings of the
read; the sum over the four went from 494 to 314. The remaining difference in
Mode0 is the object copy the image keeps in `ebp`.

**Shared tails moved out of the switch (SelectFirePose 300 -> 240).** The image
has one block for each raise pose. Writing the raise sites as `goto` into labels
placed INSIDE the switch kept VC6 from sharing them; placing the two labelled
blocks after the function's final `return 0` and giving every other raise site a
direct store plus `return 1` took the jump table from 24 bytes too far to 8 bytes
short. About 840 variants led to it, which is why the rule is: when a goto target
will not stay put, try putting it after the last statement of the function.

**Check which block a hunk belongs to.** ScriptParseActionRecon was diagnosed as
a register choice in the ADDEXP tail. A second agent matched the hunk addresses
to the source and found they were in the SETITEMFLAG case, where the kind-name
pointer is a local taken inside the error block. Taking it after the first step
of the case (218 to 50) removed the byte shift and the jump-table move that
made up most of the distance. When a long sweep finds nothing, re-derive which
source lines the differing addresses come from.

**VC6 merges identical return blocks into the LAST one emitted.** RegionFindPath
and ShotHitsObj both show the image using the FIRST. What worked for
RegionFindPath was sending the one early return to a label placed after the
function's final return (the same trick as SelectFirePose); ShotHitsObj is still
open because a label on its first return changes that first test's shape.

**An unattended search for statement order: `tools/swapsearch.py`.** The order of
independent statements has won more than once, and trying every adjacent pair is
cheap (about 1 s per compile). `tools/swapsearch.py FN [FN ...]` swaps each pair of
adjacent one-line statements in a function, keeps the best swap that lowers the
distance and worsens no other function, repeats up to `ROUNDS` times, and writes a
diff to `build/swapsearch/`. It only proposes: read each swap for independence
before applying (a store and a read of the same object, or a call with side effects,
are not interchangeable). `MAXMOVE=k` also moves a statement down over up to k others; it proposed RowUpdate 164 -> 152 by moving `spr = r->sprite;` below the statements that read `spr`, which is a bug, so that one was rejected. First run over all 49 differing functions: three swaps
helped, 46 had none.

**Clone-lab fidelity (2026-10-16).** A standalone clone only means something if it reproduces
our relink output first: compile it with the relink's defines (`/DAM2_STANDALONE /DAM2_RELINK`)
and path, or its register picks differ for reasons that have nothing to do with the function.
Even then a clone's differing-line count is not a proxy for the relink distance
(BlockWeightTroops: 47 -> 27 lines in the clone, 82 -> 98 in the relink). `relink-base.json`
stores the original truncated to our length, so return counts read from it are wrong: read the
image. `sweep2` prints `FUNCTION-MISSING` for a VC6 compile failure, and its replace-all hits
every copy of the same text, so anchor each edit with unique context.

**The IL probe found its first win (2026-10-16, MovieStart 10 -> 0).** `VC6_KEEPTMP=1` keeps c1xx's
IL and `VC6_IL_OVERRIDE=DIR` feeds c2 edited IL. Reading the kept IL shows which temporaries
c1xx made and which c2 then coalesced: for `delete g` it makes `e2 = g; e3 = e2; (e3 != 0) &&
??_G(e3, 1)`, and c2 folds the three into one callee-saved web (`mov esi,[g]; test esi,esi;
je; mov ecx,esi`). The image instead has the destructor argument as a scratch CSE of the
global read and the free argument as a separate temp copied BEFORE the test, which in source
is `m = g; if (g) { g->~T(); operator delete(m); }`. Editing the IL until the code matches
tells you the shape; then find the spelling. Every single-value spelling (local copy, two
locals, `if (g) delete g`, casts, volatile) compiles to the same code. The IL format notes are
in the memory file `vc6-il-override`.

**Separate the walk pointer from the tail pointer (DepthInsert 322 -> 0, 2026-10-16).** When a scratch
register pair is swapped through a whole function, look at which variable owns the loops: if the loop
pointer is the same variable as a pointer used after the loops, its in-loop uses give that one web
the loops' weight (2^depth) and it takes the first register. Declaring a distinct loop local (`p`)
and assigning the shared one only when the loop finds its match gives two webs. DepthInsert also
needed a count local read once at the top and re-read after each compare call, and a flags local
read once (`f = obj->flags`), which is what puts the flags dword in ecx.

**Jump tables flatter the metric.** A function with a jump table after its `ret` (Step3Drive, HudSquadUpdate)
holds absolute label addresses there, so a 4-byte change in the function's length shifts about 70 bytes of table
data and a few displacement bytes. An edit that lengthens the function can print a lower distance and score the
same or worse against the full image (HudSquadUpdate 176 -> 172, 181 -> 181). Judge any length-changing edit against
the full image, and never accept a narrowing of a 32-bit uid or of a field (`approach` holds 2 for a roach) because
it scores well.

**Impossible disjuncts as a register-priority lever (2026-10-17).** VC6 orders callee-saved registers by a
weighted use count taken BEFORE common-subexpression elimination. `|| (v == 7 && v == 8)` on a variable `v` in a
live condition adds uses of `v` and folds away entirely, so it changes which variable gets which register without
changing the code. Use distinct constants in each pair (identical pairs are merged and add nothing), keep it in a
condition that is really evaluated (a standalone dead `if` is removed before counting), and give the variable an
initialiser if the extra uses would otherwise read it unassigned. DrawMapObjects (18 -> 0) needed four pairs on
`bottom`; AirFrameDraw (44 -> 0) replaced a guard that was itself a number win; PointActionC (30 -> 24) used the loop
counter. Every use is a number win and is commented as one in the source.

## Pitfalls

- **Stale listings.** The shape tools read listings from the last relink. Rerun
  the relink after an edit before reading a diff.
- **Edits that also match a sibling.** A replace-all edit list changes every
  function sharing the text. `--confirm` shows it as an extra changed function.
- **A compile failure looks like missing output.** The screen prints `MISSING`
  for a function that was not emitted, and the relink prints no json. Fix the
  compile first.
- **Jump-table data decoded as code.** Tails of Step3Drive, RunScriptAction and
  HudSquadUpdate contain a switch's table that the disassembly shows as
  instructions. The residual distance there is not source-addressable; check
  that the differing bytes are table words before chasing them.
- **GOG patches are not a cause.** The comparison reference has the six patched
  jumps put back (`docs/binarypatches.md`); a function that differs by two bytes
  is not "a GOG-patched jump". An earlier claim to that effect was wrong.
- **Pointer copy in a register that every source form propagates back.** The
  PointerPick family keeps a pointer copy in `ebp`; no spelling tried has
  reproduced it. Recorded in `docs/vc6-regalloc.md`.
- **Check claims from another model or agent.** Verify them against the bytes
  before they go into a doc. Two retractions of this kind are on record.

## Delegating the search

A second model can drive the screen while the first keeps the tree. What
worked: give it the screen, the rule list above and `docs/vc6-regalloc.md`, name
the one function, forbid touching the tree except through the screen, and have
it report the edit list and the screened distance. One agent at a time, because
the tree is shared and a compile writes source files in place. Confirm the
result with a full relink yourself and review the diff for behaviour before
applying; an edit that lowers the number by changing meaning is a regression.

The largest wins of the stretch above came from this arrangement, mostly by
trying many variants of one function quickly. Variants that stall are usually
register-priority ties and frame-slot order, where the rules in
`docs/vc6-regalloc.md` are the next place to look.
