# Matching the original byte for byte

How the reconstruction is brought from "behaves the same" to "the hosted VC6
compiles it to the same bytes as the 1999 build". This is the working method,
the tools, and the catalogue of source shapes that moved the count. Register
rules live in `docs/vc6-regalloc.md`; this file is the loop around them. How a lab is set up and how the compiler
and the match check are run is in `docs/bytematch-lab.md`.

State when this was written (2026-10-15): 1639 of 1646 functions
byte-identical, 7 differing, 604 bytes of total edit distance. The live
numbers are in `STATUS.md`.

## Goal and metric

The goal is a relink whose function bytes equal `ArmyMen2.exe`'s. Byte equality
is a proof of reconstruction that no A/B run can give: if the compiler emits the
same bytes, the source is the original's up to things the compiler erases.

**The metric is the relink byte edit distance**, per function: difflib over the
bytes our relink emits and the bytes in the image. Zero means identical. It is
what `make vc6-relink` reports, and it is the only metric to trust.

The manifest covers the whole goal. Measured 2026-10-24 by diffing the image's
function entries (`docs/functions.tsv`) against the manifest's addresses: the only
game-side entries with no manifest address are `0x0045CAA0` (ADDR_LOG, a bare `ret`
stub in the retail build — logging was compiled out; the harness owns it via
`src/inject/gamelog.c`, so it is not a reconstructable function) and
`0x00463390`/`0x00463396` (the DirectSound/DirectDraw-create IAT thunks, linker
artifacts). Nothing below the CRT frontier is reconstructed-but-unscored, and
`tools/remaining.py` finds zero unreconstructed game functions. The retail CRT
block (0x00464xxx+) is not part of the byte-match: our CRT is hand-written in
`src/platform/crt`.

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
| `make vc6`, `make vc6-relink` | build the PE host; run the full relink and print the per-function result |
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
6. Apply the edit, plain `make` and `make all` (read the warnings, never grep for `error`),
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

## Where the last 31 stand (2026-10-18)

Random annealing over dead reads, impossible pairs, compare respellings and
statement swaps found nothing in 15 runs of Round Q, after finding seven wins in
Rounds N to P. What worked in those rounds was a hand-built seed that is closer
to the image in register-blind instruction shape but worse in bytes, followed by
a search that may also delete the older hacks (ObjRemap 124 -> 108, RowUpdate
154 -> 138, BlockWeightRoute 26 -> 16 by dropping a pair that had gone stale).

Measured facts from Round R, for whoever tries next:

- Impossible pairs are quantised steps, not use counts: a single `&&` chain on
  one variable is inert, two separate `||` pairs can flip a colouring, and a
  pair on a pointer or on a far-ahead variable does nothing.
- Colouring is not monotone: extra uses of `drop` in DrawSelection pushed `wide`
  below `mh` instead of just raising `drop`.
- Several residues share one shape: the image gives the scratch register to the
  short temporaries and a callee-saved one to the long-lived pointer, ours the
  reverse (HudSquadUpdate, DrawSelection, ScrollMapCache). Unverified on a toy.
- VC6 sinks trailing byte stores into the inline strlen/strcpy expansion, so a
  store's place relative to it is a scheduling effect (MissionEnded, LoadMap).
- A dead computation such as `(void)(a - b)` does not shift the temporary
  rotation; only a live test does. Local-copy spellings never induced the
  `mov ecx,eax` copy in HudPanel.
- Structurally right but byte-worse seeds waiting for a colour flip:
  DrawSelection with `int32_t fill` (68 -> 78, shape 21 -> 8), ParseScenarios
  with the parameter used in place (38 -> 106, shape 3), UpdateTrooperAction
  arm 1 as `turned = 1; speed = 0; s->bearing = facing;` (22 -> 46).

**A respelling is only a respelling if it is equal for every input.** RowUpdate's
`cl <= 0 ? 0 : cl` was once written `(uint32_t)(cl) <= (uint32_t)(0) ? 0 : cl`
for 16 bytes. That is true only for `cl == 0`, so a negative `cl` (a rect
hanging off the left edge) went unclamped. The unsigned-cast compares in
ScriptIf are safe because both sides are a token index and a count; check each
one for the domain it is used on, not for the bytes it saves.

### Census of the last 28 (2026-10-19, 1,220 bytes)

Differing instruction pairs by class (jump and call targets masked): register-only
swaps 122 across 18 functions; a store or load placed one or two instructions off
about 50 (16 functions); argument-materialising `lea`/`add` about 30; mov operand
form 18; one structural block 13 (RowUpdate); `lea` against `add` 5; signed against
unsigned jump 2 (ScriptIf); `test` against `cmp` 2; `sub` against `cmp` (the
difference-test trick) 4; width 1 (DrawSelection `int16_t fill`); push order 0.
About 80% is register colouring or single-instruction scheduling; only about 12
pairs belong to opcode-level levers. Open leads, each needing a colour lever and
not a spelling: SightScan (swap the two increments, 64, and sink the argument
loads after the stores); DrawSelection (`int32_t fill`, then raise `drop`'s key by
about 16); ParseScenarios (`test al,al` without the {ebx} singleton, e.g. a second
byte-restricted web); ScriptIf (a signed compare with distinct continuation targets
so the shared tails do not merge); AddToItemList (needs both the counter value in
ecx and the counter web's key above uid's).

### Re-deriving from the disassembly (2026-10-20)

ScrollMapCache 138 -> 0 came from writing the function again from the image's disassembly
BEFORE reading the old source, then comparing: after months of single-lever tuning the
old source had become a local optimum (a hoisted `px`, a volatile `tt`, bottom-before-top
clamps), while the image's layout was structurally different: the full repaint is the last
block and three forward jumps reach it, `abs()` is evaluated at each use, and the camera
blocks hold `nx`/`ny`. The same re-derivation did NOT beat the current source for ObjRemap,
RowUpdate or FindPath (fresh versions 195, 220 and 1,356 bytes against 88, 154 and 66), so
the method pays when a function's CONTROL-FLOW layout is off, not when its residue is
colour. Order of work: derive the block order and exits from the jump targets first, then
the locals from the frame layout, then compile and compare.

## The remaining 17 (2026-10-21, 512 bytes): what each needs

Every residue below has a measured diagnosis; none has a known source form. See docs/vc6-regalloc.md
for the allocator model (keys, penalty vector, hints, rotation) and the tools (`c2webs`, `c2blk`,
`c2allow`, `c2rot`, `c2force`, `c2lines`, `c2fixed`, `c2pen`, `c2hint`).

| function | bytes | what the image has that ours lacks |
|---|---|---|
| RowUpdate | 154 | `right`/`bottom` recomputed (`left + w`, `top + h`) unshared, `rect.top` reloaded after its store, `cols` spilled to [esp+0x10]; c2 shares the sums |
| ObjRemap | 90 | pre-clamp `cb` (web 1a) in edx, needs an ecx hint/penalty from a neighbour live in its range, or `left` (key 92) above 164; image spills `top`, ours `left` |
| FindPath | 66 | first inline distance masks `cur` destructively first; `mov ecx,[eax]; mov ebp,ebx; add ebp,ecx`; `shl eax,1; mov edi,eax`; 16-bit `test ax,ax`/`test dx,dx` |
| SightScan | 38 | the allied-arm end test is proven dead (`cand == w`), which threads two jumps past the reload (`lea ecx,[edi+0x12]` for `add edi,0x12`); removing it moves the score block (436) |
| MpPanelScreen ctor | 30 | one rotation consumer between the `flag50` store and the ArrowBar `lea` (ecx), and the `add esi,0x20; add ebx,4` order |
| HudPanel ctor | 22 | `pointsField` stored before `btn->disabled`, reload into ecx (one more rotation slot before it), `mov ecx,eax` copy of `px` |
| ScriptParseEvent | 22 | four name/line loads are webs that take eax; the image has edx/ecx: needs a neighbour with an eax penalty or hint live across them |
| UpdateTrooperAction | 18 | arm-1 `s` web (zero neighbours) needs edx; arms 2-4 reloads of `out` consume no rotation slot in the image |
| LoadMap | 18 | the `sub` at TRIG supplies one rotation consumer the image gets from a `cmp`; two scheduling swaps (`mov [esi+0x15],dl`, `xor eax,eax`/`add esp,4`) |
| ListMaskAction | 14 | `f.end` spill store before the `lea` for `c`; `mov esi,eax` before the store |
| ScriptPad | 12 | loop-1 `n` load/inc/store sits between the `sy` sum and the `sy` store; `n` is plain in the image |
| ObjHitMaskAction | 8 | same `f.end` store position as ListMaskAction |
| TrooperFire | 6 | `PointsDiffer(o->pos, at->at)` argument temps eax/ecx, ours edx/eax: bump at event 24 and a freeze at 25 or 26 |
| OptionsDefaults | 4 | `neg eax` where the image has `test eax,eax`; plain test puts `setne` in al (20) |
| ScriptIf | 4 | two `jb` where the image has `jl`; signed merges identical tails into their pairs (156 to 286) |
| PausedFrameStep | 4 | `mov [esp+0x10],edx` before `mov esi,eax` |
| Update3DAudioVolumes | 2 | `mov eax,ecx` copy of the loaded buffer where ours reloads `mov eax,[eax]` |

Levers that are known NOT to exist: a zero-code rotation consumer, a hint without code (hints point at
fixed registers only), a pair on a local or register operand (folds in c1), a dead non-volatile read
(deleted in c1). Levers that work only with a measured prerequisite are listed with each win in docs/status-history.md (VC6 BYTE-MATCH).

**Subsets of hunk fixes (Round BJ, FindPath 66 -> 54).** In a function whose residue is a few hunks, list every known
single fix per hunk and test ALL subsets: each fix alone changes the length and shifts every jump displacement
after it (`uint16_t after` 98, `before = 0` moved 100, `dd <<= 1` plus a pair 132), but the three together land on
the image's length (1798) and 54. Judge by length first: a subset whose length equals the image's is the one whose
displacements stop mismatching. The remaining FindPath hunks: `mov ecx,[eax]; mov ebp,ebx; add ebp,ecx` for `nb`;
the first mask `and` destructive (a helper taking the same value under two spellings fixes it and drops the length
to 1794); `shl eax,1; mov edi,eax; add di,[mem]` for `g`; `mov ecx,esi; and ecx,eax` in the final heuristic.

**Re-derive again after new levers (Round BN, ObjRemap 90 -> 0).** A second from-scratch derivation, done after the
earlier one's levers had been superseded, took ObjRemap from 90 to 0 where hundreds of local edits to the old source had
not: the old source had been shaped by levers that no longer applied. Procedure: derive from the image first (block order,
frame slots), write it, match the layout, then anneal with the CURRENT lever set (dead pairs on the flag test, statement
permutations of the independent stores), then minimise: drop every hack whose ablation does not cost (here the volatile
y read, the unreachable read). Re-run this for any function whose source predates a lever batch.

## Round BQ negative results (2026-10-22, 282 bytes, 7 differ)

Pairs `(F==K && F==K+1)` fold to nothing on a frame local, a global, `*(uint8_t *)f`
or a ternary with constant arms; they emit real code on `o->facing` inside a
`g_mpSession`-led condition. Only nested `(A && !P) || P2` forms with a call result
or frame local as the first operand give a surviving event. Residues:
UpdateTrooperAction 8 (arm-1 `s` web wants edx; arm-4 `movsx` needs two more events
after the arm-3 reload), ScriptPad 10 (moving the `sy` store after the `n` load
matches order but colours `ny` eax, 16), LoadMap 12 (slot-loop `xor eax,eax`/`add esp,4`
order; TRIG `sub` event), ScriptParseEvent 18 (name loads need edx),
SightScan 26 (needs the allied-arm `add edi,0x12` hint AND a non-consuming nOther
value temp together; pairs cannot add a consumer between two temps in one block),
FindPath 54 and RowUpdate 154 (pair lever finds nothing; RowUpdate wants the
`left+w`/`top+h` sums unshared by a store c1 treats as aliasing).

## Round BR/BS (2026-10-22, 272 bytes, 7 differ)

LoadMap 12 -> 2 came from losing pointer provenance (`slot = table - (X - X)`, X a runtime
global read); it works only on an independent local or loop pointer, not inline at the
use. It found nothing on RowUpdate (about 650 variants: the sums stay shared whatever the
stores go through), SightScan, ScriptPad, UpdateTrooperAction, FindPath or
ScriptParseEvent. LoadMap's last 2 bytes are the TRIG `sub eax,esi` that supplies a
rotation temp; every `cmp` form shifts the rotation early in the function (272 to 504).
SightScan's image skips eax for the sightHits temp as if a scratch were live there; the
retest `if (!stt.cand) goto next;` both lays the score block out early and threads the
jumps, and nothing separates the two. The only untried route for RowUpdate is
hand-edited IL (`VC6_IL_OVERRIDE`), which is not source.

## Round BT (2026-10-22, 272 bytes, 7 differ)

Rewrites from the disassembly found no byte win. Leads kept: SightScan's score-block
layout CAN be had without the dead retest (merge `pick` into `stt`, write the score block
inline in the Kind7 arm, `goto score` from the other arms, labelled copy after the final
`return`): 282 bytes, 236 with a local `c = stt.cand`; the residue is the Kind7 copy being
scheduled with its arm (cand load hoisted above `add esp,8`, `cmp eax,ebp` instead of
`test edi,edi`). FindPath: `nb = (cur & 0xFFFF) + *sp` closes the `nb` hunk but costs two
bytes of length (54 -> 90); the three commutative-operand hunks (first mask `and ebx,eax`,
`g` sum, final-heuristic mask) are untouched by ~150 spellings. ScriptParseEvent needs the
tokens-base load to stop consuming a rotation slot while the name load becomes one; LoadMap
needs one invisible rotation event between SCEN's tail and MHDR's `lea hdr` other than the
TRIG `sub`. RowUpdate's sum sharing is unexplained; 2,520 statement orders and ~1,000
helper shapes scored 164 or worse.

## Round BV (2026-10-22, 272 bytes, 7 differ)

No gain. RowUpdate: struct-assign of a local `AM2_Rect` (560), `RectSet`-built rect (386-468),
inline `RowBounds(rc,x,y,w,h)` (260-432), labels/no-op branches (154-194) - the sums stay
shared. LoadMap: the missing event sits exactly between SCEN's fread size load (event 11)
and MHDR's `lea hdr` (event 12); dead volatile reads at arm starts cost real loads
(214-738); `(int32_t)size - plane != 0` still emits `sub`. ScriptParseEvent: a static
inline wrapper over the pad lookup scores 32. UpdateTrooperAction: arm 1's `s` web needs a
neighbour with positive cost and an eax hint, or an edx hint; nothing adds one without code.

## Round BW (2026-10-22, 272 bytes, 7 differ): the IL route on LoadMap

Hand-edited c1 IL fed to c2 (VC6_IL_OVERRIDE) and a logger of c2's temp-assignment walk
(`tools/c2walk.py`) pinned LoadMap's last 2 bytes to one fact: the image has a rotation event
between SCEN's fread argument load and MHDR's `lea hdr`, and the only tuple there that could
carry it with identical code is SCEN's third read of `size` (`mov eax,[esp+0x20]; push eax;
push esi; add ebp,eax`), which c2 makes a web for us (no event) because it has two uses. Every
IL and source shape tried (about 120) left it a web; the `sub` at TRIG is the only known way to
supply the event, at the cost of `sub` for `cmp`. The mechanism and the IL notes are in
`docs/vc6-regalloc.md` ("The AssignTemp walk").

The same walk on the other rotation residues (same round): UpdateTrooperAction needs two cursor
events between arm 3's `out` reload and arm 4's `movsx` where ours has none; the only tuple there
with identical code is the `mov eax,[g_mpSession]` test load (a late-split compare operand for us,
not an event), and a volatile read of it changes nothing; nested pairs placed in that conjunct
emit real code (256) whatever their first operand (`out`, `at`, `o`). SightScan's score block:
the image's `sightHits` increment is a real temporary at cursor 1 with the `hold.facing` load hoisted
above the `inc`; ours is either a volatile temporary at cursor 0 with no hoist (26) or, written
plain, an RMW tuple split late into eax with the hoist (58). Arm 1 of UpdateTrooperAction (`s`
in eax, image edx) is a two-use web, so it is a colouring tie, not a rotation one.

ScriptPad's last 10 bytes, read with c2webs/c2blk (same round): the plain body `sy += i / H; n++;`
compiles to the image's loop code (sum into the `sy` temporary, `n`'s load hoisted above the `sy`
store), but loop-1's `i`/zero web then keys 84 against `ctx`'s 86 and `ctx` takes edi first; our
volatile `t_n` form adds two tuples to the loop body (block N 16 -> 18) which lifts `i` to key 100
at the price of the body shape. Pairs on the layer byte change code (458-626); pairs and
pair-ternaries on frame locals and globals fold before the key is computed; fifteen initialisation
orders with the plain body all give 384.

FindPath 54 -> 32 (Round BW, after the walk): the earlier levers combined at last once the `g` sum
was spelled `g = (uint16_t)(dd + g_pathNodes[curTile].g)` and the flag byte read through a pointer
local declared after it: the two-spelling distance helper (`PathDistXYT(cur & 0xFFFF, curTile, t,
t)`), `nb = (cur & 0xFFFF) + *sp`, and both flag tests through `fl[t]`. Length stays 1798. Left:
the flags pointer is hoisted into ecx in the image and the node offset read late as a folded add
operand, where ours hoists the node offset and loads the flags pointer late; and the budget arm's
`mov ecx,esi; and ecx,eax`.
Follow-ups on the 32 that did not pay: the two-spelling helper at a second site, whether the same
helper or a duplicate (196-254; the budget arm's distance and heuristic are different webs), the
same trick on the heuristic call (124-182), `dd` as any of the four integer types (inert), named
`pg`/node-pointer forms (58-324), and a volatile `node.g` (104). The flags-pointer local before the
`g` sum hoists the pointer into ecx as the image does but turns the node-offset read into a
temporary and shifts the rotation (90-92); both halves of that hunk are one c2 destination choice.

LoadMap back to the plain compare (2 -> 356, by decision): the TRIG check is `(int32_t)size != plane`
like its six siblings; the `size - (uint32_t)plane` number win is withdrawn because no author writes
one plane check as a difference. The rotation step it supplied is the image's SCEN third read of
`size`, a single load that is a rotation temporary in the image and a web for us; a second read
(volatile, or after the call) restores the step and matches MHDR onward at the cost of a load
(214-216). Still open: a source shape that gives one load AND the step. Ruled out this round: case
order (2,321 orders), keeping `tag` live, a shared mismatch label or inlined `bad:` tails (the image
has three tail-merged copies), fread return values, `consumed` spellings, `size`'s type.

LoadMap's missing step, three hypotheses in parallel (same day, after the `!=` decision):
(1) SCEN's third read of `size`: c1 emits two plain loads; c2 merges a local's loads with no kill
between them into the variable's own node (op 4, a web) at CSE time, and every source-level kill
found emits code (stores through `tmp`, memset, volatile, narrower re-reads); folded "kills" (self
stores, empty ifs/switches, inline functions taking `&size`, unions, arrays, pointer reads) do not
separate them. Untried: the c2 code that creates that node (0x10732fd9 / 0x107331b0).
(2) TRIG/REGN/OWNR size loads: one extra event in any ONE of the three arms between SCEN and MHDR
fixes MHDR onward (`size - plane` on any of them: 2; `plane = size; if (plane != W*H)` on REGN alone:
206, arm registers swapped); events before SCEN cost 14-16, two or three arms 400-500; ~40 symmetric
shapes are inert (356). The image's arms are uniform non-events.
(3) MHDR's `cmp [esp+0x10], 0xc`: a frame-variable compare is always op47 (flags only) whatever the
spelling; only real arithmetic (`size - 0xC != 0`: 216) takes the step.

## LoadMap and the other residues, second pass (2026-10-22, 604 bytes / 302 unmatched, 7 differ)

Rule used from here: all seven plane checks stay `(int32_t)size != plane` (a check written as a
difference, or one arm spelled differently from its six siblings, is not source a 1999 author
writes), and in SCEN `consumed += size` stays before `ParseScenarios`. That puts LoadMap at 356
(the `sub` form was 2 bytes). The distance counts a replaced byte twice and a one-byte length
shift moves every later offset: 302 bytes of 389,636 are unmatched after alignment (0.078%).

LoadMap. The image's rotation is one step ahead of ours from MHDR's `lea` onward; the step
belongs between SCEN's fread-argument load (event 11) and MHDR's `lea` (event 12). Established:
- c2 gives a load with two uses in one stretch the variable's own node (a web, no step); the
  push and the add of SCEN's third read are two separate loads in c1's IL and c2 merges them.
  One-use loads (the malloc and fread arguments) are temporaries and take a turn.
- One extra step anywhere in that window, by IL edit, restores everything from MHDR onward
  (`size - plane` at TRIG: 2 bytes; `consumed = ParseScenarios(...)`: 6), but no edit adds a step
  without adding code. IL and C variants tried (about 250): declaration form and scope of
  `size`/`tag`/`consumed`, structs packing them (520-1740), inline functions (356-468),
  extra blocks and `if (1)`, guards on provable conditions (fold) and real ones (420+), `register`,
  code-free kills between the reads (18 forms), compiler switches (34), statement and case order
  (2,321 + 720 orders), shared `bad:`/mismatch tails (the image has three tail-merged copies),
  fread return values, `consumed` spellings, ParseScenarios parameter types, MHDR compare
  spellings (always op47, flags only), switch-expression IL, plane-check IL variants.
- Second read after the call, or a volatile read, restores the step at the cost of one reload
  (214-216). Rich header: all 65 C++ objects are compiler build 8168 (ours). `op26` is a
  zero-extending byte/word load, not a compare.
- Mutation search (commutative flips, `+=`/`= +`, mirrored comparisons, `++`, adjacent independent
  assignment swaps): about 9,500 evaluations over LoadMap, FindPath, UpdateTrooperAction,
  ScriptPad, RowUpdate: no gain. The first version flipped `cr + cl` inside `cols - cr + cl - 1`,
  which is a different value; its "improvement" (154 -> 150) was rejected.

Other residues, new negatives: ScriptPad (struct for `n`/`sx`/`sy` 676-680, plain body and every
initialisation order 382-638, `register` no effect); UpdateTrooperAction (`bearing` through
`&out` aliases 146-182, `owner` spellings identical); SightScan (allied-arm DistAndAngle
argument spellings identical or worse, plain counter increments 40-62); ScriptParseEvent (natural
kind test and plain line arguments 130-248, the pairs are what hold 18); FindPath (budget-arm
distance spellings identical, 36 with the subtraction swapped); RowUpdate (clamp spellings
166-272, `w`/`h` load order 156-380, rect as struct/locals 164-560).

## Round BX (2026-10-22): the web-vs-temp rule, measured — LoadMap's last step is not source-reachable

The walk reproduced with `tools/c2walk.py src/game/map.cpp =_LoadMap`: EVENT 10 is SCEN's
malloc-argument load, EVENT 11 is SCEN's fread-argument load (both one-use temps), and the very
next candidate tuple — SCEN's third read of `size`, one `mov` feeding the ParseScenarios push
and the `consumed +=` add — is an op-4 WEB (`op1 L917 [A4]`), so the MHDR `lea hdr` that follows
it in the walk takes the cursor one slot early (eax; the image has ecx). The image's one extra
event in that window can sit anywhere in it (an IL step at any point restores MHDR onward), but
the only zero-code tuple in it is that web.

A battery of ten two-use-load shapes (local, global, pointer-deref, push+push, push+store,
push+lea, uses in two branch arms, a 16-bit load, three uses), each walked with the gdb probe,
gave the same classification every time: a load with two or more uses in one stretch is ALWAYS a
web (A4, never an AssignTemp event); a one-use load is always a temp (A3*, an event). Types,
scopes, spellings and use positions do not move it. With c1 unable to emit one load with two
uses except as one variable (and every source-level kill between the uses emitting code), the
image's temporary there is not reproducible from C++ as far as the rule is known. This closes
the SCEN third-read hypothesis the same way the ~250 IL/C variants and the 120 IL edits did:
LoadMap's 356 stands at the source-reachable floor unless a c2 web-building input not covered by
the battery exists.

Same rule, two more probes (same day): web PRESSURE does not move it — with 0/8/16/32/48 extra
two-use locals around the arm, the SCEN walk is unchanged (the third read stays a web); and no
expression-tree trick defeats the merge — double negation, `^ 0`, `- 0`, a comma prefix, a
same-value ternary, and a cast chain each compile BYTE-IDENTICAL and leave the third read a web
(c1 folds them all before c2). The web-building classification is unconditional at the c1 level.

Also closed (2026-10-23, pragma sweep over all seven residues, 11 settings each): per-function
`#pragma optimize` with a/w/y/g/s/t (and gs/gt) either changes nothing or is far worse —
the aliasing letters a/w are codegen-neutral for all seven, so c1's merge behaviour does not
come from an aliasing switch. And the comma-statement forms (`(consumed += size, ParseScenarios(tmp, size))`
and the comma inside the argument list) are likewise byte-identical with the third read a web.
Writing the arm as a `switch` case instead of an `if` changes nothing either (same walk).
The remaining lead is the c2 pass that CREATES a load's value node as op 3 vs op 4 (the CSE
merge / IL reader, before AssignTemp): the webify sites are 0x10731d5a (jump-table handler
calling WebNode 0x10733134) and the per-use wrap at 0x10732fd2; the decision branch that picks
them is not yet located.

RowUpdate (same round, 154 stands): the image computes each geometry sum TWICE — `left+w`/`top+h`
as an unshared `lea` at the store and again for the `>>8` — and reloads `rect.top` once while
`left` stays in a register. Clone-lab probes (~25 shapes, scratch removed) found:
(1) unshared sums ARE reachable — reading the summed field as a real memory load between the
sum's store and its recompute splits the tuples (2 adds + a reload) — but only with code the
image does not have; (2) forwarding poison is per-OBJECT, not per-field: any construct that
forces `top` to reload also reloads `left`; (3) clone experiments with intervening global stores
are misleading (they kill cached loads the real function's clean region never touches). The
image needs one load with two unmerged same-input add tuples — the dual of LoadMap's web — and
no C++ spelling reachable from this compiler produces it. Screens on the real function all lost:
clamps 166, recompute 188-194, volatile placements 164-482, store reorder + recompute 284,
`spr->bounds` recompute 362.

ScriptParseEvent (18 stands) and ScriptPad (10 stands), ~75 more candidates: the four
ScriptParseEvent hunks are single-use loads folded into their consumers (`[A+0x14]=0`, no
AssignTemp event) that inherit their dying base's colour where the image has a fresh register —
the cursor levers do not reach folded loads, and the oracle finds no event the image treats
differently. The two placed impossible pairs are load-bearing: all 8 single-pair placements
scored 18-232. Pairs attach only to un-aliased through-`ctx` loads (`ctx->tokens`,
`ctx->tokens[*at].line`); pairs on `tok->value`, `tok->kind`, `*at` fold to nothing. ScriptPad's
10 is one 3-instruction hunk in loop-1's body: the volatile read/store pair on `n` (the lever
that lifts `i`'s key 84 -> 100 over `ctx`) is a scheduling barrier pinning n's load/inc/store
together where the image splits them around the `sy` store; the nearest miss (12) needs n's inc
and store split across it, which a barrier forbids. Both want the same thing as LoadMap: a key
lift / event from a second use that c2 accepts without code.

FindPath (32 stands), SightScan (26 stands), UpdateTrooperAction (8 stands), with two precise
targets left for a future round. `tools/c2rot.py web 0xb:3` on UpdateTrooperAction colours
EXACTLY the two arm-1 instructions into the image's (`mov edx,[esp+0x40]` / `mov [edx+4],bl`)
with no other change: the arm-1 `s` web (key 24, refs 2, cost 4, no hints, ZERO neighbours) is a
pure colouring tie that takes eax and wants edx — it needs an uncoloured neighbour with positive
cost and an edx hint, or an own-hint, and no spelling adds one without code. SightScan: oracle
bump at K=51 (the `*nOther` value load) is 36 -> 35 (fixes the sightHits register but re-breaks
later matches) — the image is not ours+1 there; the needed extra consumer between two temps in
one block is unreachable. FindPath: the image's `mov edi,eax; mov eax,[esp+0x18]; add di,[mem]`
for the `g` sum survives every type/cast/order/statement-split spelling (c1 re-merges; the sum's
codegen is invariant) — one c2 destination choice.

## Register-only residues (2026-10-26, 610 bytes, 7 differ)

Boran's call: a higher distance is acceptable when what remains is register choice only.
The check is a disassembly diff with register names masked and jump targets rewritten as
instruction indices (a jump-displacement change from an encoding-length shift is not a
difference); a function qualifies when that diff is empty, or when its only hunks are a spill
reload or store whose position follows from which register frees first.

- Already register-only: UpdateTrooperAction (8), ScriptParseEvent (18), and LoadMap's root
  cause (the rotation step; its other bytes chain from it).
- ScriptPad 10 -> 16, adopted: the `sy` store moved between n's volatile load and store, the
  image's order. What remains is ny/n swapping eax/edx and the spilled `number` reload sitting
  after whichever of them frees edx.
- SightScan (26) is two levers short: `++o->sightHits` plain (the volatile one was a scheduling
  barrier: the image loads hold.facing between the load and the inc) and the weapon arm's
  `DistAndAngle(&o->pos, &keep->pos, ...)` without the two-definition `kp` (its push order) both
  remove a real hunk; the store-order hunks they add are register-caused (pick.bearing's byte in
  al forces the eax store first). Left: the allied arm's end threads past `score:`'s `stt.cand`
  test (`lea ecx,[edi+0x12]` / `jne` to the inc) where the image jumps back to the test and kills
  edi. Removing the arm's own test, a second label, a volatile read, moving `stt.cand = w`, and
  laying the score block out by hand all scored 180-568: the arm's test is what keeps the score
  block in place, and c2 threads by value (w was tested nonzero).
- FindPath (32): the heuristic's `and` order is register-only; the `g` sum is not (see above).
- RowUpdate (154): 21+ hunks in every spelling -- top reloaded after its store, the two sums
  computed twice, setle clamps, slot assignment. Not a respelling.

Second pass on the two near ones (same day):

- SightScan's allied-arm jump is web-building, not a branch choice: `c2rot.py web 0x21:3` moves
  `w`'s definition (`mov edi,eax` after LookupByUID), so `w` and the score block's `stt.cand`
  reload are one web in our build. The arm's trailing `if (!stt.cand) goto next;` is what joins
  them: its read is forwarded from `w`, which keeps `w` live to the arm's end and lets the jump
  skip the reload. Without that test (`th1`) the score block's own code matches the image exactly
  -- `mov dl,[esp+0x18]` / `mov [esp+0x38],edi` / `mov [esp+0x1c],eax`, store order included --
  but c2 then lays the score block out after the allied arm instead of after the Kind7 arm (388).
  Every other placement of a `stt.cand` read in the arm (before DistAndAngle, before
  ObjsAreAllied, before the health test) and every rewrite of the aiMode tail gives the same 388;
  merging `pick` and `stt` into one address-taken struct (the image's slots 0x14..0x20 fit it)
  makes the score block reload `stt.cand` after AngleDelta, which the image does not (170-230).
  So the open question is c2's block layout, not the allocator.
- FindPath's `g` sum is invariant under the types of `g` (uint16/int16) and `dd`
  (uint32/int32/uint16), the statement split, and casting `dd` to 16 bits first (32-58).
- FindPath's `g` sum, at the IL level (2026-10-26): c1's postfix for line 1390 is
  `&g; dd(30 42); node.g (… 30 22) widen(2c 41 00); 02; narrow(2c 22 00); 32 22`. Swapping the
  operands in the IL (VC6_IL_OVERRIDE, same length) compiles identically (32), and narrowing `dd`
  to 16 bits in the IL instead of widening `node.g` does not change the sum either (58, other
  hunks only). c2 picks the image's `mov edi,eax; add di,[mem]` vs our `mov di,[mem]; add di,ax`
  from something other than the sum's IL -- casts on the field (`(int16_t)`, `(uint32_t)`, through
  an `int16_t *`) were already no-ops in the source (32).
- FindPath (32) classified register-only under the goal's spill rule (2026-10-26, confirmed by
  Boran): its one masked hunk is the reload of the spilled `curTile * 16` offset (`[esp+0x18]`) --
  ours into ecx right after the call, the image's into eax after `dd` has left it. With the reload
  in eax, `dd` must move to `g`'s register first (`mov edi,eax`) and the parent's g is added from
  memory (`add di,[eax+...]`); with it in ecx, ours loads the parent's g into di and adds ax; and
  `g_tileFlags` takes whichever of ecx/eax is free (image: ecx before the reload). The reload is a
  rotation temporary (forcing web 0x4c, the offset itself, does not move it), so every difference
  follows from one scratch-register choice. No source change.
