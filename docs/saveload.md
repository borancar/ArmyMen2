# The save/load path

Nothing in the suite had ever pressed LOAD -- `ab.sh campaign` reaches a mission
by clicking NEW -- so this whole path went unexercised until `tools/loadcheck.sh`
existed. What it found, in order: `LoadGameProcSection` was missing the two
stores that make a load happen at all, so every load silently became a fresh
start AND overwrote the save the player asked for; and underneath that, a latent
use-after-free on the cell grid that is the ORIGINAL'S, which it survives by
luck and we did not.

The second one carries **the one deliberate deviation in this tree**: `LoadItems`
unlinks every registered object from the cell grid before `ItemsReset` frees
them. Say plainly that it IS a deviation, and note it is confined to `LoadItems`
rather than put in `ItemsReset`, which `LoadMap` and `ResetItemsAndUids` also
call -- so no path but a load can reach it.

Three things here generalise and are stated in `CLAUDE.md` rather than only
here: a latent defect is invisible until EVERY path that reaches it works, so a
suite going red after a correct change is not evidence the change was wrong;
`WINEDBG=-all` discards the page fault, so a run that dies with nothing in its
own log should be re-run outside the harness before anything is instrumented;
and static comparison can be EXHAUSTED -- thirteen functions on this chain were
compared against the image and all thirteen were faithful.

**FIXED: `LoadGameProcSection` DROPPED THE TWO STORES THAT MAKE A LOAD
HAPPEN.** The original ends its success path with `mov [0x511ddc], 0` and
`mov [0x511dd8], 1` at 0x0042698B -- `ADDR_HAVE_DEFAULT_COF` and
`ADDR_LOAD_PENDING`. Our reconstruction had neither.

They are there because the function's own `fread` destroys the flag: the
0x438-byte block starts at `ADDR_GAMEPROC_BLOCK` and `ADDR_LOAD_PENDING` is
0x370 into it, so reading the section overwrites the very flag that says a
load is in progress -- with the 0 that was in the file, because nothing was
pending when the game was saved. The original puts it back; we did not, so
every load silently became a fresh start.

**THE CONSEQUENCE WAS A SAVE BEING OVERWRITTEN.** With the flag clear,
`State2Enter` takes the start-fresh branch and `TakeMenuRequest` reaches
`MissionStartup`, whose retry stamp saves over the file the player asked to
load. That is why the md5 moved on our side and not the original's.

Measured after the fix, against a restored save each time: our build logs
`Loaded 317 items` where it never did before, and the save's md5 is
UNCHANGED. `ab.sh bootcamp campaign` is clean -- 1,610 state lines and 13
messages identical, 35 widget nodes identical -- so nothing else moved.

**THE NEXT DEFECT IS INSIDE `BuildRegionGraph`.** Both builds reach
`calculating region data...` on a load. The ORIGINAL then emits TWO of the
harness's "call site passed a non-string format" notes and lives; ours emits
ONE and the process detaches.

Decoding `State2Enter` at 0x00425628 says what sits between them:
`BuildRegionGraph`, `LookupOwnerObj`, `SelectInventorySlot`, `DeselectAll`,
a `SelectUnit`, and then a `call 0x45CAA0` with NOTHING pushed at 0x0042568A.
That last one was MISSING from our C -- a blank line with stray whitespace
sat where it belongs -- and it is added now, which is why the note counts
could be compared at all. It still does not appear on a load, so we exit
BEFORE it, inside that block.

Which leaves `BuildRegionGraph` (0x0042B9A0) or something under it, and the
ARGUMENT is the tell: the note our build emits carries `0076FEA4`, an address
inside our own code, where the original's carries `00000001`. A log call
handed a code pointer in one build and a small integer in the other is a call
site passing the wrong thing, and the fault follows it.

Wine reports nothing -- `WINEDBG=err+all` produced no output -- so this is an
ordinary exit or a fault Wine swallows rather than a page fault it would
print.

**THE FAULT IS FOUND: A MAP CELL HOLDS A NON-POINTER AFTER A LOAD.** Running
the STANDALONE directly, where Wine's stderr is not redirected, prints what
`drive.sh` had been swallowing:

    wine: Unhandled page fault on read access to 000009AF
          at address 0075C8E2

`nm` puts 0x0075C8E2 in **`ObjectsInRect` + 0x112**, and the two instructions
there are

    mov   (%ebx),%esi        ; take the object pointer out of a map cell
    testb $0x4,0x8(%esi)     ; esi->flags & OBJ_FLAG_DESTROYED

so `esi` is 0x9A7 -- the fault address less the 8 -- a small integer where a
pointer belongs. A cell slot holds garbage, and `ObjectsInRect` walks it.

**0x9A7 IS THE SIZE OF A UID, NOT OF A POINTER**, which is the hypothesis to
test first: a save stores objects by uid and the load has to resolve those
back to pointers, so a uid left unresolved in a cell list is exactly this
shape. `LookupByUID` and the relink in `ItemLinkCells` are where to look.

It only happens after a load because until the `LoadGameProcSection` fix above
no load had ever completed here -- the cell lists have never once been
rebuilt from a save file in this project.

**THE CHAIN ABOVE THE FAULT IS VERIFIED FAITHFUL, so the defect is below it.**
Checked instruction by instruction against the image, and all three match:

- `RemoveFromItemList` (0x00428590) -- the count decrement, `tail = count - i`,
  and the `memmove` of `tail * 12` bytes from `&table[i+1]` to `&table[i]`,
  which the original spells with the `lea ecx,[ecx+ecx*2]; shl ecx,2` pair.
- `ItemsReset` (0x00429450) -- the loop re-reading the count each iteration,
  `FreeItem(table[i].obj, 0)` with unlink ZERO, then `free(table)` and the
  three globals zeroed.
- `LoadItems`/`LoadOneItem`'s dispatch and the guards around it.

The `unlink` argument being 0 is the thing to carry forward: `ItemsReset` does
NOT ask `RemoveFromItemList` to run, because the whole table is about to be
freed -- so whatever takes an object out of the map's CELLS has to be inside
the per-type `Destroy*` path. `DestroyWeapon` demonstrably runs, since the log
carries one line per weapon. That path, and `ItemPreDestroy` under it, is
where to look next.

`DestroyWeapon` and `DestroyItemObject` (0x00429C80) are faithful too: the
original gates `ItemPreDestroy` on the THIRD argument exactly as ours does --
`mov eax,[esp+0x10]; test eax,eax; je` -- so with `ItemsReset` passing 0
neither build unlinks from the cells there. That kills the tidy theory that we
alone leave dangling cell entries.

**And the value argues against dangling anyway: 0x9A7 is a tiny integer, not
a freed heap pointer.** The faulting pair maps exactly onto
`ObjectsInRect`'s inner loop in `win32/mapdraw.cpp`:

    uint8_t *o = *(uint8_t **)(node + CELL_NODE_OFF_OBJ);   /* mov (%ebx),%esi   */
    if (*(const uint8_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)  /* testb $4,0x8(%esi) */

`CELL_NODE_OFF_OBJ` is 0 and `OBJ_OFF_FLAGS` is 8, so it is a CELL NODE whose
object pointer is 0x9A7 -- the node list is intact enough to be walked and one
node's payload is not a pointer. That is narrower than "the cell array is
wrong": the array and the `next` chain both survived, so what to read is who
BUILDS those nodes on a load and what it puts in `+0x00`.

**BOTH WRITERS OF `+0x00` ARE CORRECT**, checked: `ObjInitCommon`'s allocator
in `objtype.cpp` mallocs `count * 0x10` and fills every entry with
`obj`/0/0/-1, and `ItemSetBox`'s realloc in `item.cpp` rewrites all of them
the same way. `ItemLinkCells` never touches `+0x00`, only the index and the
list pointers, which is right. The row pool is installed -- all four entry
points are patched -- so cell entries are not coming from an uninstalled
allocator either.

**AND THE STRONGEST LEAD IS A COMMENT THIS TREE ALREADY CARRIES.**
`objtype.cpp`'s no-cells exit is documented with the symptom of a bug fixed
earlier: writing it as two independent `if`s "linked every such object into
the map a SECOND TIME, which cost a campaign A/B: the load never finished and
five log lines from 'calculating region data...' on were missing." That is
this failure, word for word -- a load that stops at exactly that line.

**THE RUNTIME PROBE NAMED THE ARTIFACT, and it is a STALE CELL HEAD.** A
temporary guard in `ObjectsInRect` that reports a payload below 0x10000
instead of dereferencing it caught exactly one node on a load:

    PROBE badnode cell=181 node=0364a088 obj=000009a7
                  idx=161940511 prev=00000468 next=000009e4

`node` is a plausible heap address and EVERY FIELD IN IT is a small integer --
`obj`, `prev` and `next` all look like coordinates or uids. So the node is not
a cell entry at all: cell 181's head points at storage that was an entry once
and has since been freed and reused. With the guard in place the game
survives the load, which confirms this one node is the whole fault.

That closes the mechanism: `ItemsReset` calls `FreeItem(obj, 0)`, the 0 travels
to `DestroyItemObject`'s third argument, `ItemPreDestroy` is therefore NOT
called, and the object's storage -- including its cell entries -- is freed
while the grid still points at it. `LoadItems` then allocates the new objects
over that memory.

**AND THE ALLOCATOR IS NOT THE EXPLANATION EITHER, which is the useful
elimination.** The tempting answer is that the original's heap happens not to
reuse the freed entries before the walk while ours does. It cannot be that:
the INJECTED build faults too, and `src/game/crt.h` points `am2_malloc` at the
game's own statically linked MSVC CRT, so that build allocates from the same
heap as the original. Same allocator, same path, one faults. The divergence is
real rather than luck.

`LoadItems` itself is faithful -- the original opens with `call ItemsReset`,
checks tag 0x06660007, and loops on the record mark 0x06660000, which are
`AM2_SAVE_TAG_ITEMS` and `AM2_SAVE_RECORD_MARK` exactly. And the grid is not
rebuilt in between: `MapDescInit`'s only caller is `LoadMap` and
`MapDescFree`'s other caller is the map teardown, so neither runs between
`ItemsReset` and the first walk in either build.

**305 OBJECTS ARE FREED WHILE STILL LINKED, and the fault is INTERMITTENT.**
A probe in `DestroyItemObject`'s `notify == 0` arm, counting entries whose
cell index is still >= 0, reports 305 on one load -- every object the map
built, freed by `ItemsReset` with its cell entries still in the grid. The
first is `uid=200003e8 type=1 cells=4 linked=1`.

The run carrying that probe SURVIVED. So the crash depends on what reuses the
freed storage, not on whether the dangling entries exist: they always do, and
the walk faults only when the memory underneath has been rewritten into
something whose first dword is small. That explains the one bad node out of
305 and why the earlier probe found exactly one.

**AND THE STANDALONE LOADS ONE TOO, measured the same way.** Driven through
SINGLE PLAYER, the player row, SELECT, the save row and LOAD, with `-dbg` as
both other halves take, `build/ArmyMen2.exe` comes back ALIVE at sub-state
0x18 with 325 objects, the save's md5 unchanged, and `Loaded 317 items` in its
own log -- the original's answer on every one. So the port can now load a
saved campaign, which it could not do at any point before this session.

**FIXED, BY THE ONE DELIBERATE DEVIATION IN THIS TREE.** `LoadItems` now
unlinks every registered object from the cell grid before `ItemsReset` frees
them. `tools/loadcheck.sh` goes from failing to passing: our build comes back
alive at sub-state 0x18 with 325 objects and the save's md5 unchanged, which
is the original's answer on all four.

Say plainly that it IS a deviation. The original frees those objects with
unlink 0 and leaves 305 dangling entries; it survives because the blocks are
reoccupied by the objects `LoadItems` then creates, so each stale head still
reads as a plausible entry. That is luck, not design, and ours reoccupies them
differently.

**THE PLACEMENT IS THE CAREFUL PART.** It went first into `ItemsReset` itself,
which was wrong: that function is also called by `LoadMap` -- which frees and
rebuilds the descriptor in the same breath -- and by `ResetItemsAndUids`, so
unlinking there could write through stale indices into a grid that is gone or
new. Confined to `LoadItems`, the deviation cannot reach any path but a load,
which is what keeps every other configuration byte-for-byte as it was.

`ab.sh bootcamp` is clean at its floor with the change in -- 1,610 state lines
and 13 messages identical -- and `campaign` was clean on the wider variant
before it was narrowed.

**A NOTE ON THE COMBAT FRAME GATE, which is why the narrow version exists.**
The first, broader version was run against `ab.sh combat` and failed the frame
gate at 277% and 384%, with a single control run at 195%. One control sample
of a metric this file already documents as varying two-fold is not evidence
either way, and rather than spend four more runs settling it, the change was
narrowed so that configuration cannot be affected at all. When a measurement
is too noisy to settle cheaply, shrink what you are asking it about.

**AND THE TWO DEFECTS THIS SESSION FOUND ARE LINKED: THE MOVEMENT FIX IS WHAT
EXPOSED THIS ONE.** `SightScan` has five callers in the image, and the one that
reaches it here is `NextInventorySlot`, whose ONE caller is
`Type2PlayerInput` -- the function whose gate was inverted until this session.
While that gate read `!ADDR_OBJ_CTX_OBJ_A` instead of comparing it with the
object, `Type2PlayerInput` never ran, so `NextInventorySlot` never ran, so
`SightScan` never ran, so nothing ever walked the cells around the leader.

So the fault is on the FIRST FRAME OF PLAY after a load, not inside
`State2Enter` -- the log's last line is that state's own
`calculating region data...`, and the crash follows it. Two fixes landing in
one session uncovered a third defect that neither could have shown alone: the
load path had to work before there were stale entries to walk, and the input
path had to work before anything walked them.

Worth keeping as a general point about this kind of porting. A latent defect
is invisible until EVERY path that reaches it works, so fixing one thing
routinely makes the next thing fail, and a suite that goes red after a
correct change is not evidence the change was wrong.

**THE WALK THAT FAULTS IS `SightScan`'s, OVER THE LOADED POSITION.** A probe
logging the caller and the rectangle at the bad node:

    cell=181 obj=000009a7 caller=0074011c
    rect=1479,3036,2119,3676 cells=5..8/11..14

`nm` puts the caller in `SightScan` + 0x11C, and the rectangle is a 640x640 box
centred on 1799,3356 -- SARGE'S SAVED POSITION. The grid is `cols`=16 with
shift 4, so cell 181 is `(11 << 4) + 5`, squarely inside the 256-entry
allocation. Those cells are visited only after a load: on a fresh map the
leader stands somewhere else entirely, which is why no drive had ever walked
them.

That completes the picture and also says why chasing it further needs a
different tool. The original loads the leader to the same point and its
`SightScan` walks the same cells, so its grid must be CLEAN there. Both builds
leave 305 entries dangling; the original's stale heads apparently land on
memory that new objects' entry blocks reoccupy, so every one still reads as a
plausible entry, while one of ours lands on something else. Same heap, same
faithful functions, different malloc/free SEQUENCE -- which is a property of
everything the two builds allocate around the load, not of any function on
this chain.

**BUT THE ORIGINAL SURVIVES 3 OF 3 AND WE FAULT ABOUT 2 IN 3, so it is
SYSTEMATIC rather than luck.** Three consecutive `AM2_NOPATCH=1` loads of the
same save all came back alive with 325 objects; ours has faulted on two runs
and survived one. A latent defect that the original hits and shrugs off would
not be that lopsided.

`DestroyItemCommon` (0x0043BBB0) -- the handler for the type-1 objects the
probe actually names -- is faithful too: same null guard, same
`FreeSubrecordRows(obj + 0x6C)`, same `DestroyItemObject(obj, 0x514F10,
unlink)`, and the same order of the two frees, which is what would matter for
reuse.

So thirteen functions on this path are verified and the difference is not in
any of them. What is left is the ALLOCATION PATTERN around the load -- what
else our build mallocs and frees, and in what order, between `ItemsReset` and
the first walk. That is a different kind of investigation from reading
functions, and the tool for it is a malloc/free trace compared between the
two builds rather than more disassembly.

**SO THIS IS A LATENT USE-AFTER-FREE ON THE LOAD PATH, and the evidence says
it is the ORIGINAL'S.** `ItemsReset` passes 0, the original passes 0, and
`DestroyItemObject` gates the unlink on that argument in both. Nothing on the
chain is mis-transcribed -- twelve functions checked -- so the game frees
those entries without unlinking them and then walks the grid. What differs is
only whether the reused bytes happen to fault, which is why the original gets
away with it and we do not always.

That reframes the fix. It is not a transcription correction: it is a choice
between reproducing a latent defect faithfully and crashing sometimes, or
deviating deliberately. This file's standing position is that the original's
defects are the original's -- but that was written about defects nobody could
reach, and this one takes the process down on an ordinary LOAD GAME. Say
which it is before changing anything, and record the deviation if one is
made.

**WHAT IS NOT YET EXPLAINED IS WHY THE ORIGINAL SURVIVES IT**, since it takes
the same path with the same 0. `MapDescInit` memsets the grid to zero and is
faithful -- including the implicit `& 0xFF` on `Log2Mask`, which our `uint8_t`
prototype supplies where the original writes `and eax, 0xff`. So the question
is what clears or rebuilds that grid on the original's load path between
`ItemsReset` and the first walk, and whether our load skips it.

**STATIC COMPARISON IS EXHAUSTED ON THIS CHAIN -- TEN FUNCTIONS, ALL
FAITHFUL.** `RemoveFromItemList`, `ItemsReset`, `LoadItems`/`LoadOneItem`,
`DestroyWeapon`, `DestroyItemObject`, `BuildRegionGraph`'s three allocations,
the row pool's four entry points, `ItemLinkCells`, both writers of the cell
entry, and `ItemPreDestroy` -- each compared against the image and each
correct, including `ItemSetBox` unlinking through `ItemPreDestroy` BEFORE it
reallocs and relinks, so the obvious double-link is not there.

That is worth stating as a result rather than a failure: the defect is not a
mis-transcribed instruction in the linking layer, so reading more of it is
not the way in. What is needed next is RUNTIME instrumentation -- dump the
cell entries of the objects a load creates, and find which one carries a
payload that is not its object -- which is the same move that turned the
movement bug from a mystery into one `cmp`.

So the family is DOUBLE-LINKING, not a bad pointer written once.
`ItemLinkCells` is called from two places -- `ObjInitCommon`'s tail and
`ItemSetBox` -- and an object that reaches both on the load path is linked
twice into the same cell list, which walks into itself. That is what to test
next, and it explains the shape better than anything else here: a list whose
`next` chain is intact enough to walk, with one payload that is not a pointer.

 A cell holding a stale pointer would fault on a large
address or not at all; this looks like uninitialised or mis-indexed memory --
a cell array read past its end, or one reallocated without being cleared.
That is where to go next, not the destroy path.

None of it had ever executed before this session's `LoadGameProcSection` fix:
CLAUDE.md's own unexercised list carries `FreeItem` and `RemoveFromItemList`,
and a load is the first thing in this project to call them in bulk.

**AND `WINEDBG=-all` IS WHY THIS TOOK SO LONG.** `make run` sets it, so the
page fault Wine prints was discarded on every run through `drive.sh`; the
game just "detached". Launching the binary directly cost one command and gave
the faulting instruction outright. When a run dies with nothing in the log,
run it outside the harness before instrumenting anything.

**WHAT IS ALREADY RULED OUT INSIDE THAT BLOCK**, so the next pass need not
redo it. `BuildRegionGraph`'s three allocations match the original's
arithmetic exactly: the realloc is `44 * (region + 1)`, which the original
spells as `lea ecx,[ebp+ebp*4+5]; lea edx,[ebp+ecx*2+1]; shl edx,2` and which
`AM2_REGION_SIZE` 44 reproduces, and both mallocs are `stride * stride` off
the int16 at `ADDR_REGION_STRIDE`, matching `movsx eax,[0x514eec]; imul ecx,
eax`. Its log call is guarded and passes a real format plus one integer, as
the original's is at 0x0042BAF1.

So the sizes are not the fault. What has NOT been checked is the rest of that
block -- `LookupOwnerObj`, `SelectInventorySlot`, `DeselectAll`,
`LookupType3ByUID` and `SelectUnit` -- reached on a load with a leader whose
`OBJ_OFF_RIDING` came out of a save file rather than out of a fresh map.

<!-- superseded -->
**Earlier localisation, kept for the reasoning.** Both builds reach
`calculating region data...` on a load. The ORIGINAL then emits TWO of the
harness's "call site passed a non-string format" notes and lives; ours emits
ONE and the process detaches. So we die between the first of those calls and
the second.

The ARGUMENTS differ, and that is the sharper clue: the original's first note
carries `00000001`, an integer, where ours carries `0076FEA4` -- an address
inside OUR OWN code, since the standalone links text at 0x00700000 and the
injected DLL is mapped high too. A log call whose format argument is a code
pointer in our build and a small integer in the original's is a call site
handing over the wrong thing, and this tree has the shape on file: several
log calls in the image take NO arguments and read whatever sits above the
return address, which `TakeMenuRequest`'s `orig_log_noargs` already
reproduces deliberately.

So the next step is to find which log call runs immediately after the region
pass, and what our caller leaves on the stack there. Wine prints no fault --
`WINEDBG=err+all` produced nothing -- so this is an ordinary exit or a fault
Wine swallows, not a page fault it would report.

**AND IT EXPOSES THE NEXT DEFECT, which is the honest half.** The load path
had never once executed here, so what it reaches was never exercised: with
the flag now set, our build loads and then EXITS, right after
`calculating region data...`, where the ORIGINAL loads and lives -- the
original ends at sub-state 0x18 with 325 objects and its process alive. So
"the save loads" is now true and "the game survives loading it" is not yet.
No configuration in the suite loads a save, which is why this was invisible
and why it stays invisible until one does.

Worth keeping as the reason the earlier readings were confusing: the
ORIGINAL's post-load state is 325 objects at sub-state 0x18, and our unfixed
build's 316 at 0x21 was the FRESH mission, not the loaded one. I had those
two the wrong way round for several turns, and it is what made a
build-versus-build difference look like a load working on one side.

**RETRACTED: THE STANDALONE DOES NOT LOSE SAVED GAMES. `-dbg` DID.** This
file carried, for several commits, a confident open defect saying the
standalone failed to load a save and overwrote it. It was a CONFOUND of my
own making: `drive.sh` defaults to `ARGS ?= -nointro -dbg` and every
standalone launch I wrote passes `-nointro` alone. The pause that decides all
of this is gated on exactly that switch --
`if (!LOAD_PENDING && !MP_SESSION && OPT_DBG) PauseGame(8)`.

Re-measured with the command lines MATCHED, against one restored save each
time:

| build | args | sub-state | objects | save md5 |
|---|---|---|---|---|
| injected | `-dbg` | 0x21 | 316 | unchanged |
| standalone | `-dbg` | 0x21 | **316** | **unchanged** |
| injected | no `-dbg` | 0x18 | 325 | CHANGED |
| standalone | no `-dbg` | 0x18 | 325 | CHANGED |
| ORIGINAL | no `-dbg` | 0x18 | 325 | unchanged |

So the standalone loads a save exactly as the injected build does, and the
whole difference was an argument. Four commits of evidence-gathering rested
on a control I never checked, in a file that says to match the halves before
believing either.

**WHAT SURVIVES IS A REAL AND NARROWER DEFECT, and it is not the standalone's.**
Read the last two rows: without `-dbg` OUR reconstruction rewrites the save
and the ORIGINAL does not, in BOTH builds. Starting fresh without `-dbg` is
the game's own behaviour -- the original does that too -- but the retry stamp
`MissionStartup` writes is ours alone.

`MissionStartup`'s three guards are transcribed CORRECTLY: at 0x00444F3D the
original tests `LEVEL_ID <= 0`, then `GAMEPROC_BLOCK[0] == 0`, then
`WIN_ENABLED != 0`, and calls 0x00425790, which is `SaveGame`. So the
divergence is upstream -- whether the call is REACHED -- and `TakeMenuRequest`
is where to look, since `State2Frame` enters it on `arm == 11` only when
`GetPauseFlags()` is zero.

**And this matters to a player rather than to the suite**, which is the
reason to keep it: every configuration here runs with `-dbg`, so no A/B can
see it, and someone playing the port normally is the one whose save gets a
retry stamp the original would not have written.

**`tools/loadcheck.sh` IS THE SAVE/LOAD GATE, and it FAILS TODAY ON A KNOWN
DEFECT.** Nothing in the suite ever pressed LOAD -- `ab.sh campaign` reaches a
mission by clicking NEW -- which is how `LoadGameProcSection` came to be
missing the two stores that make a load happen at all, and why that went
unnoticed while it silently rewrote the player's save.

It drives the real sequence on our build and on the original under
`AM2_NOPATCH=1`, from the same restored fixture each half, and compares four
things: the process is still alive, the sub-state matches, the registered
object count matches, and the save file's md5 is UNCHANGED. The md5 is the
sharpest, because a load that becomes a fresh start rewrites the slot -- the
whole original defect in one line.

Measured now: the original comes back alive at sub-state 0x18 with 325 objects
and the file untouched; ours comes back DEAD, with the file untouched. So the
fix holds -- no more overwriting -- and the remaining fault is the page fault
in `ObjectsInRect` this file records above.

**A CHECK THAT FAILS IS STILL WORTH COMMITTING when the failure is a defect
you have measured and written down.** It converts "loading crashes sometimes"
from folklore into a gate that will go green the day the cell-entry defect is
fixed, and it pins the two halves' arguments together so the `-dbg` confound
cannot come back.
