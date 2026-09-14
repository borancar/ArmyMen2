# The WinMain chain

## The WinMain chain

**Twelve functions between WinMain and the engine are reconstructed**, none
over 320 bytes: `CheckBasePath`, `InitTimer`, `ShutdownSubsystems`,
`FreeSpriteListAlias`, `InitAudio`, `ClearGameOver`, `ResetToTitle`,
`StartIntro`, `RunFrame`, `FreeSpriteSets`, `ReportLeaks`, `FreeMemTracker`,
plus `BuildTrigTables` in `trig.cpp`. One level down stays original -- the
thirteen teardowns, the five per-state frame handlers, the sprite-set loader.

**A second name for an address you already named is how a misreading survives,
and this session added thirteen.** `orig.h` had 39 addresses carrying more than
one `ADDR_` name before today and 49 after. Some of that is renaming done
properly -- `ADDR_STARTUP_4249C0` becoming `ADDR_RESET_TO_TITLE` once the body
was read -- but the rest is a new name invented beside an old one that was
already there, and in five cases the OLD name was the one that knew something:

| address | I called it | it already was | what that changes |
|---|---|---|---|
| `0x00512464` | `ADDR_GAME_DIR_ALT` | `ADDR_CD_PATH` | `SetGameDir`'s fallback is the CD |
| `0x00512588` | `ADDR_GAME_DIR_ALT_OK` | `ADDR_CD_PRESENT` | and it is gated on the CD being in |
| `0x004FA038` | `ADDR_INTRO_SEEN` | `ADDR_OPT_NO_INTRO` | `StartIntro`'s third test is the switch |
| `0x004FA02C` | `ADDR_FRAME_ENABLED` | `ADDR_APP_ACTIVE` | `RunFrame` is gated on the foreground |
| `0x0047894C` | `ADDR_SPRITE_SETS_LOADED` | `ADDR_OPT_DF` | that pair is behind the `-df` switch |

None of it changed behaviour -- the address is the address -- but every one of
those comments described the mechanism less well than the name already in the
file did. **Before naming a global, grep `orig.h` for its address.**

**And a table with ONE consumer is a table you cannot name.**
`ADDR_SOLDIER_NAMES` pointed at `0x00489BFC` for as long as `TakeSoldierName`
was its only reader: that function touches only the taken flag, the stride is 8
either way, so it indexed correctly and nothing could see that the record
actually begins four bytes earlier at `0x00489BF8` with the name at +0.
`SoldierNameOf` (`0x004475C0`) needs the name and would not compile as a lie.
`MSGNODE_OFF_OWNER` went the same way -- named from `DumpMsgList` printing one
dereference past it, corrected once `MsgListCopyByKey` was seen memcpy'ing the
field wholesale for the length beside it.

**`SCENARIO_PART_OFF_NAME` is the third, and its one toucher was a `free`.**
That offset was named from `FreeScenarios`, which walks the scenario table and
hands `[rec + 0x18 + n*0x0C]` to the allocator four times per record. A free
site tells you the field is owned memory and NOTHING ELSE -- it cannot tell an
array from a string, and "name" is the guess that fits a `char *`.
`ParseScenarioPart` is the writer, and it puts a malloc'd array of
`count * SCEN_ROW_BYTES` records there. Renamed `SCENARIO_PART_OFF_ROWS`, and
`FreeScenarios`'s own comment -- "free every string the scenario table owns" --
was wrong with it. **A `free` is the weakest possible toucher: prefer the
writer, and treat a name derived from a teardown as unsupported.**

The cure is the same in both directions: `ADDR_CELL_WEIGHTS` and
`ADDR_TILE_COVER` sat unnamed for months and were settled in an afternoon by a
`+1`/`-1` PAIR writing one of them and a footprint routine subtracting fifteen
from the other. **Look for a second toucher before believing a layout, and
prefer the writer/reader pair over either alone.** This rule is stated
here, restated under the alias ratchet, and was still broken THREE TIMES in a
single session -- on `0x00428DA0`, which was `ADDR_OBJ_ACTION`; on
`0x0040F560`, which was already reconstructed; and on `0x00458070`, which was
`ADDR_OBJ_PAIR_ACTION`. Every one was caught by `checkpatches` rather than by
remembering, which is the argument for the ratchet existing and NOT an
argument that the rule is optional: the check only fires after the name has
been written, and each time the right fix was to decide which name is
body-derived and delete the other. The grep costs one command.

It went to FIVE before the session ended, and the fifth was committed in the
act of writing this paragraph -- `0x0041DB20` was already
`ADDR_ROW_UNREGISTER_ALL` and got a second name anyway. Writing the rule down
and citing it repeatedly did not produce compliance; only the check did. Treat
that as the finding: the ratchet is not a backstop for carelessness, it is the
mechanism, and the same argument says the offset macros need one too, since
their fourth duplicate passed every check silently. The one that was
genuinely unresolved -- `0x005125C4`, `ADDR_OPT_MUSIC`, which `SetGameDir`
latches on entering the `avi` directory -- is settled: it is
`ADDR_OPT_BIG_MOVIES`. `MovieBuildName` appends `sml` to a movie's filename
when it is 0 and the machine is slow, so the avi latch means "the full-size
movies are here" and `-bm`/`-sm` are big and small. **What settled it was
reading a function that USES the flag**, where both earlier readings came
from functions that write it.

**The "event flags" are the pause mask, and both functions that move it say
so themselves.** `0x004267C0` logs `"PauseGame: %x (set: %x)"` and `0x00426800`
logs `"UnPauseGame: %x (reset: %x)"`, so `0x005122FC` is one bit per reason the
game is paused. That matters where it is READ rather than where it is written:
every `if (!GetPauseFlags())` in the frame chain is "if the game is not
paused", which under the old names read as a check on some event queue.
`GetPauseFlags` is called 767,153 times in a Boot Camp run and the pair fires
once each.

**The registration table has NINE buckets, and the teardown's loop bound is
what says so** -- it walks `0x005101F0` up to `ADDR_SCRIPT_CONDITIONS`, which
is the next global. This file said 1024, invented. A bucket is a chain of
entries keyed on a PAIR, each entry a chain of handlers, and the sixth argument
to a registration decides whether the teardown frees the handler's argument
too. `DeclareRuleVars` passes 0, so the `if` records it registers are not freed
by the table pointing at them.

**A guard is not compared until something takes the arm that needs it.** The
four multiplayer row buttons share one test -- a row may be edited when it is
ours, or when we are the host and the row is at or past the human player
count. I read the `jl` backwards and wrote `row < count`, which is the host
editing other people's rows and never the computer ones. It passed a clean
`mpoptions` A/B, because that configuration clicked row 0 and row 0 is ours:
it takes the "or it is mine" arm and never reaches the comparison at all.
Clicking row 1 as well puts the inverted guard 584 pixels out. Before
believing a configuration covers a branch, ask which arm the input takes.

**The whole AI mode family runs for VEHICLES ONLY, and reconstructing the
DISPATCHER turned six explanations into one counter.** `0x00407F80` has exactly
one caller, `ADDR_STEP_TYPE3`, so nothing that is not a vehicle reaches an arm;
Boot Camp is not vehicle-free either -- four `createvehicle` lines -- so the
arms were cold for a narrower reason than "no vehicles", and finding the
dispatcher's caller answered for all six at once.

Then the dispatcher itself was reconstructed and `AiStep` reads **0** on a
driven Boot Camp mission. That is better than the xref argument in every way:
it is a measurement rather than a chain of reasoning, it covers the arms
whatever their modes, and it gives the family ONE number to check before
anyone tries to exercise it again. **When a family of cold functions shares a
dispatcher, reconstruct the dispatcher early -- its counter is the family's.**

**DIFF THE DISASSEMBLY when two functions look like the same one twice --
the resemblance is what makes the difference invisible.** `AiStepTrack` and
`AiStepDefend` are 224 and 208 bytes of what reads as one function written
out twice. Normalising branch targets to displacements gives SIXTY-NINE
instructions each, the same instructions in the same order, and one
structural difference: a ten-instruction turn test sits BEFORE the second
promotion in one and AFTER it in the other. Since the still-moving path jumps
to that promotion in both, the block is behind you in one and ahead of you in
the other -- so a defending unit turns toward what it sees only once it has
arrived and the other turns while still walking. That is visible in the game
and invisible in a summary.

The failure this guards is specific and tempting: noticing two functions are
"the same", factoring the shared tail into a helper both call, and flattening
a real difference in silence. Thirty seconds of `difflib` over normalised
disassembly settles it, and the instruction counts matching exactly is the
tell that nothing else moved.

**A new PREFIX is invisible to the offset ratchet, and I proved it on myself
one commit after quoting the rule.** Reading the AI context I named
`AICTX_OFF_OBJ_10`, `_RANGE` and `_BEARING` at 0x10, 0x14 and 0x18 -- which
are `SIGHT_OFF_OBSERVER`, `SIGHT_OFF_RANGE` and `SIGHT_OFF_BEARING`, already
in the file at those offsets. `checkoffsets` passed, because its family-alias
rule compares WITHIN a prefix and a brand-new prefix has nothing to compare
against. This is the same hole CLAUDE.md already describes for the ROW_/OBJ_
pair; the difference is that there the two prefixes were both old.

What caught it was the next function, one commit later: `AiStepDefend` hands
the record straight to `ConsiderSighting` as its `sight` argument, so it is
one structure and the coincidence of offsets was not a coincidence. **Before
opening a new offset family, grep the offsets it would contain, not the names
-- the ratchet cannot.**

**The AI MODES are an eight-arm jump table, and the numbers the scripts write
land on it.** `0x00407F80` builds a context and dispatches on
`OBJ_OFF_AI_MODE` through `0x0040803C`: index 0 goes straight to `0x00407710`,
1, 4 and 5 SHARE one arm, 2 is `ignore`, 3 is `0x00407C80`, 6 is `attack` and
reaches `0x00407710` through the pass-through thunk `orig.h` had noticed and
could not explain, 7 is `defend`. Six handlers for eight modes -- the
slots-sharing-an-arm case again, and another reason to read the table rather
than the layout.

The mode numbers were not guessed here: `tests/actions-reference.txt` settled
them from the shipped scripts long before this table was read, and the two
agree. Two independent routes to the same fact, which is what makes it worth
recording rather than a plausible mapping.

**And the `ignore` arm cannot run in this environment, by four measurements
rather than one.** Its counter is 0 on a driven Boot Camp mission; `setaimode`
appears in NEITHER drivable map -- `bootcamp` and `kitchen` issue it zero
times, while `ignore` appears 42 times across 8ball, airborne, fortress,
frontyard, mania and playset; and the two code paths that also write mode 2,
`Type2ActionB` and `ResetObjOnCof`, both read 0 on the same run. Grepping the
data answers "can a script reach this"; it does not answer "can anything
reach this", and the two counters are what closed the gap.

**Before reconstructing a function reached from a SCRIPT ACTION, grep the
shipped data for the keyword.** `SelectUnit` (`0x00427CE0`) has fifteen
callers and reaches none of them here: its script route is the `group` action,
and `group` appears only under `data/mp*` -- the multiplayer maps this
environment cannot open a session for. One `grep -rl group data/` would have
said so in seconds, before the function was written. The 109 shipped scripts
are the cheapest reachability oracle this project has and it is not only for
the parser.

**Reach for the count the defect changes, not the count that is easy to
read.** Leaving one of `MpPanelDestruct`'s two sprite arrays unreleased passed
the pixels, the log and the 128-node widget tree -- and passed the REGISTERED
SPRITE COUNT too, which was the obvious global to check and answers a
different question: every one of those sprites is still referenced elsewhere,
so no slot is ever freed. What moves is the refcount INSIDE the sprite, 1
against 3. `tools/ab.sh` reads the array's pointer, dumps the dword at +4 and
puts only that in the `state` file; the pointer is a heap address and stays
out, which is the rule the widget dump already follows.

**Where the evidence is a global rather than a pixel, dump the global.**
The three data checksums the multiplayer handshake compares never reach the
screen, and the game's own logger is stubbed to `ret` in this build, so the
"Checksum of %s is %x" lines the code writes go nowhere -- a wrong total is
invisible to both halves of an A/B. `tools/ab.sh` has a `state` artifact for
this: bytes read over the control socket on both sides and diffed exactly. It
is the same idea as `tools/trigdump.py` and worth reaching for whenever a
subsystem's output is a number the frame does not show.

**`mpoptions` is clean, and the six defects that got it there are in
`docs/screen-mpoptions.md`.** It went from a process that DIED when the screen
was requested to `widgets identical (131 nodes)`, `state identical`, `log
identical (35 messages)` and 151 pixels against a budget of 300.

Four rules came out of it and are stated here because they generalise past that
screen:

- **A field our code READS and our code never WRITES is a defect, and one grep
  finds every instance on a screen.** Six of `MP_PANEL_OFF_*` were unbuilt and
  each announced itself differently -- a crash on open, an empty global, a
  crash one dereference later, four misplaced nodes, a selection that must
  start at -1. The grep found all six before any was diagnosed.
- **A never-written field does not fault where it is READ, it faults one
  dereference LATER**, so solve the fault address for a garbage base plus a
  real field: 0x280 + 0x7C is the 0x2FC that named `MP_PANEL_OFF_CHATBOX`.
- **Take a long constructor's ORDER from its field stores, not from reading it
  top to bottom.** The game box writes the script name and the map list looks
  up BY that name, so the wrong order searches for an empty string. And a child
  list is ordered by when each child was added, so a block in the right place
  with the wrong neighbours still moves four nodes.
- **A widget that is CONSTRUCTED but not STORED is invisible to every check we
  have.** It draws, it is in the child list, it appears in `ctl widgets` -- and
  the field naming it holds whatever the allocator left.
**GENERALISED TO EVERY WIDGET CONSTRUCTOR, IT FINDS DUPLICATE PREFIXES RATHER
THAN MISSING WRITES.** Run over the 19 `*_OFF_*` families that have a matching
`*Construct`, the same scan reports nine families with unwritten fields -- and
the ones worth anything are not bugs in the constructors, they are records
described by TWO prefixes at once, so the constructor writes every field under
the other spelling. `CheckBoxConstruct` uses `CHECK_OFF_*`; the painter uses
`CHECKBOX_OFF_*`; eight fields had both.

**And two of the eight CONTRADICTED each other**, which is what makes this
worth more than tidying: `CHECK_OFF_SPRITE_OFF` and `CHECKBOX_OFF_SPR_ON` are
both 0x68, and `CHECK_OFF_SPRITE_ON` and `CHECKBOX_OFF_SPR_OFF` are both
0x6C. `CheckboxPaint` settles it -- it takes 0x68 when the box is checked and
0x6C when it is not -- so the `SPR_` names are body-derived and right and the
`SPRITE_` pair was named BACKWARDS at those addresses. Collapsed onto the
painter's names, and `controls`, `audiovol` and `difficulty` stay clean.

**`checkoffsets` cannot see any of this and the ratchet still earned its
keep**: it failed the build the moment the rename produced two
`CHECKBOX_OFF_CAPTION` defines. It compares WITHIN a prefix, so it is blind to
a second prefix and sharp about a second name -- which is exactly the split
this file already describes for a NEW prefix, met from the other direction.

**AND IT HAD A SECOND BLIND SPOT, WHICH WAS THE FILE LIST.** `checkoffsets.py`
read `src/inject/orig.h` and nothing else, so the **68** `_OFF_` macros in
`src/game/**.h` were invisible to it -- including `LIST_OFF_ARG7C`, a
"constructed 0" placeholder sitting on `LIST_OFF_ARROWBAR`'s 0x7C in the SAME
family, which is the one thing the tool exists to catch. Three
`FOCUSLABEL_OFF_INK2/3/4` were the same shape against `INK_FOCUS`, `PAPER` and
`PAPER_FOCUS`.

It reads them now, and found a straight duplicate on its first run:
`VTABLE_EDIT` defined identically in `orig.h` and `widget.h`.

**A NAME THAT LIVES IN A GAME HEADER STILL BELONGS IN `orig.h` IF ANYTHING
ELSE USES IT.** Deleting `LIST_OFF_ARROWBAR` from `orig.h` in favour of the
descriptive copy broke `commmsg.cpp`, which does not include `widget.h` --
the shared header is the canonical home and the comment moves to it, not the
other way round.

**The baseline went 15 to 17 and that is COVERAGE, not decay.** Four aliases
were collapsed before the number was taken, so it would have been higher
still. Whenever this figure moves, say which of the two it is.

**THE OTHER RATCHETS WERE AUDITED FOR THE SAME HOLE AND ONLY THIS ONE HAD
IT.** `checkglobals` walks `am2.game_sources()`, `checkseams` globs
`src/game/**/*.cpp` recursively, `checkinstalled` takes both header
directories and `checksplit` reads the two halves by design -- all correct.
`checkpatches` reads `orig.h` alone and that is right rather than lucky:
**all 2,957 `ADDR_` defines are in `orig.h` and none is anywhere else**, so
its scope matches its population exactly.

Recorded because the question is cheap to ask and tedious to re-derive: the
file list of a checker is part of what it checks, and four of the five were
already right. That is the check to run on any screen whose
constructor is suspected, and it is the scoped form of the read-only-offset
idea that was rejected as a whole-tree gate.

**A READ-ONLY OFFSET LOOKED LIKE A CHEAP RATCHET AND IS NOT ONE, measured
before it was built.** `checkoffsets.py` refuses a second name on an offset;
nothing asks whether an offset we READ is ever WRITTEN, and a field with no
writer is already recorded here as dead code. Pointed at our own source that
scan finds this defect -- so it looked like the obvious next gate.

It is a noise generator, and the numbers say so: **329 of 1,106** `*_OFF_*`
macros in `src/game` are never on the left of an `=`. Most are legitimate --
we read a field the ORIGINAL's code writes, which is the normal state of a
half-reconstructed program and not a defect at all.

**And its false positives look exactly like its true one.** The same scan
flags `MP_PANEL_OFF_COLOURS` and `MP_PANEL_OFF_TEAMS`, which sit beside
`ARMY_ROWS` in the same panel and read as three instances of one bug. They are
written -- through a LOCAL taken from the macro (`colours[i] = ...`), which a
macro-level scan cannot see. Believing them would have turned one real defect
into three and sent the fix at code that is already correct.

What has signal is the SCOPED question, not the global one: for a structure
whose CONSTRUCTOR is ours, every field we read must be written by us, because
nothing else builds it. That is a claim about one structure at a time and
needs to know which those are, so it is not a cheap gate either -- but it is
the form worth reaching for by hand when a reconstructed constructor is
suspected.


**Reusing an entry for a repeated key pair is load-bearing**, which a mutation
settles: making the lookup always miss, so every registration makes its own
entry, puts `bootcamp` at 79,695 differing pixels against a budget of 500.
Events in a shipped mission really do share a key and need the second handler
on one entry.

**An `orig_` macro pointing at a reconstructed address is a lie, and there
were twenty-one of them.** `tools/checkseams.py` resolves every `orig_` macro
to its address and fails if that address is in the patch list. It exists
because the same thing had been found by hand four times -- `InitInput` and
friends in winmain.cpp, `SetGameDir` under `orig_path_exists` in three modules,
the pause pair in dplay.cpp, `CreateOffscreenSurface` in device.cpp -- and each
time the comment beside it had gone stale with it: `orig_create_offscreen` sat
under "on the list to reconstruct next" long after it was done. One entry is
allowed and named: `orig_parse_action`, which the `AM2_PROBE_NOACTION` switch
needs.

**Fixing them found a defect no A/B can see.** `PlaySoundAt` tested whether a
sound is at its owner's position with `PointsEqual(&where, at)` -- two
POINTERS, under a local typedef that declared them. The original pushes
`[eax+0x12]` and the caller's packed point, by VALUE, and keeps the address in
a register at the same time for the `ApproxDist` beside it, which is how the
confusion arose. So the test compared two addresses, never fired, and the
near-distance case was dead code. Nothing could have caught it: the only thing
it changes is a sound's volume, and the A/B compares logs and pixels.
**A local typedef for a function that already has a header is a place for a
signature to be wrong in private.**

**The two DirectX creators are NOT this.** `orig_DirectDrawCreate` and
`orig_DirectInputCreate` name `0x00463396` and `0x00464410`, which are the
game's own one-instruction `jmp [IAT]` import thunks -- six bytes each, not
game code, and nothing to reconstruct. For DirectInput going through the thunk
is required: `dinput_hook.c` patches the IAT slot at `0x0046F014`, and an
import of our own would resolve through our IAT and walk past the hook.
`tools/checkhooks.py` guards exactly that.

**A function can already be reconstructed under a name you would not have
looked for.** `SwapColourBytes` was about to be written a second time as
`ColourOf`, and `misc.cpp` has had it since long before -- the two bodies came
out identical. The compiler caught this one, on the conflicting declaration,
where `checkpatches.py` could not: it only sees duplicate PATCHES, and the
second patch had not been added yet. Three near-misses now, each caught by a
different mechanism. **Before reconstructing anything, grep the tree for the
address as well as for the name.**

**And grep for the SHAPE, which neither the address nor the name will find.**
This image specialises one routine for two record types more than once, and
two consecutive commits found a pair each: `SettlePointInRegion` is
`NearestAllowedTile`'s spiral run under the default rule, and `ItemLinkCells`
is `RowRegisterAll`'s cell registration done to an object instead of a row --
same clip, same four clamps, same stride, same two loops, same tail, differing
only in which record's fields they read and in one guard apiece. Both siblings
were ALREADY reconstructed, and in both cases the sibling's comment held the
answer to the very thing that looked odd in the new one: `RowRegisterAll`
already carried "COLS, not ROWS" for a clamp I was about to re-derive. The
tell is cheap -- a disassembly that feels familiar probably is -- and the
neighbours in `orig.h` are where to look.

**`NearestPalIndex`'s output is 39% of the frame, and its `from` guard is not
checked at all.** Making it pick a far entry instead of a near one puts
`bootcamp` at 306,886 differing pixels, so the choice is thoroughly observed.
Making it ignore `from` and search from 0 changes nothing -- and a probe says
that is not for want of coverage: `from` arrives as 0, 9, 10, 60 and 100 over
8,498 calls. The nearest colour simply lies above the reserved block anyway
for everything Boot Camp remaps. Covered, and still not discriminating; the
guard stays verified by reading.

**`orig_` is one spelling of the seam and not the only one.** `frame.cpp`
reached the movie step as `call0(ADDR_MOVIE_FRAME_STEP)` — fine until
`0x00445630` was reconstructed, at which point the call went through the detour
into our own code. `tools/checkseams.py` checks `callN(ADDR_X)` at a call site
now as well as `#define orig_x ... ADDR_X`, and is tested by putting the call
back.

**There is a fourth spelling and it is now the general rule: naming a
reconstructed address at all.** The menu installs a button handler by address,
and `MakeButton` turned that address into a pointer with `AM2_IMAGE` — right
while the handler is the original's, a lie once it is ours, and invisible,
because nothing on the line looks like a call. Same for an inline
`((Fn)(uintptr_t)ADDR_X)(...)` and for a table of plain integers that are
function pointers. So the check now fails on any `ADDR_` in `src/game` that
resolves to a patched address, outside its own `patch_replace`.

**It looked unpromotable and the reason was a bug in the check.** It reported
about two hundred sites with a caveat that some were data rather than calls —
and it was scanning COMMENTS, where every `ADDR_` name in this tree is
discussed. With comments stripped it was 21, all genuine, and closing them was
an afternoon. A caveat attached to a check is worth suspecting before it is
worth believing.

That fix exposed an older one. The single-line `#define orig_x ... ADDR_Y`
match never saw a macro continued with a backslash, and six were — six real
seams the gate had been green over for as long as they existed. Continuations
are joined before matching now.

What stays out of reach is a seam that reaches the image through a VARIABLE:
the tool resolves macros, not dataflow.

That seam also made `tools/blindspots.py` wrong in the other direction: it
reported `MovieStepCurrent` blind because both callers are reconstructed, while
the counter read 746,792 — because those callers reached it by ADDRESS. Closing
the seam took the counter to 0 with no behaviour change; `MoviePoll` still
reads 712,509 on the same run. **"All callers reconstructed" only implies blind
if they call by NAME.**

**Four reconstructions had never been installed, and every tool said they
were done.** `dist_install` opened with
`return patch_replace(ADDR_APPROX_DIST, ...)` and had three more calls under
it; `savetag_install` had one. The calls are there, so `tools/coverage.py`,
`docs/boundary.md` and `tools/checkpatches.py`'s own count all read them as
reconstructed — and `ApproxDistXY`, `AngleDelta`, `RoundTo8` and `WriteSaveTag`
had never run in the game once. Every A/B that "covered" them was comparing the
original against itself.

GCC does not warn: `-Wunreachable-code` has been a no-op for years. What
settles it is the game's own log, which prints one `patch:` line per install —
one where there should have been four.

**The same hole has a second shape, and `checkpatches.py` cannot see that one
either.** There, the `patch_replace` call is present and unreachable. Here it
is ABSENT: the function is written, its declaration goes in the header, and
the edit meant to add the install line matches nothing. The build is clean,
every static check passes, and **`tools/ab.sh` passes too** -- the address
still holds the original, so the A/B compares the original against itself and
reports a clean run of code that is not in the binary. `StartShake`
(`0x0042B2E0`) and `TroopSubParse` (`0x0044BEA0`) both got that far.

`tools/checkinstalled.py` closes it, and what makes it possible is a
convention these headers already keep: a reconstruction's declaration is
preceded by a comment OPENING with the address it replaces. 801 declarations
follow it and there were no exceptions when the tool was written, so it is a
gate rather than a hint. A comment that merely MENTIONS an address partway
through is not matched -- that was tried first and produced three false hits,
each an address the comment was discussing rather than replacing.

Two things worth keeping from how it was found. The coverage count is the
cheap manual version -- it does not move for a function that was never
patched -- and it **must be read BEFORE the A/B, not after**, because
otherwise the clean result you are reading belongs to the parent's code. And
a scripted edit that anchors on something not every install function has
(`int rc = 0;`) will fail silently on the ones that lack it; anchor on
something that always exists, or verify the line landed.

`tools/checkpatches.py` fails on a `patch_replace` after a `return` at an
install function's own brace depth now, and tested in the failing direction by
putting the `return` back. A return that is the unbraced body of an `if` is
ordinary and is allowed — `winmain_install`'s `AM2_PROBE_NOWIN` is one.

Fixing it put three functions into live play for the first time:
`ApproxDistXY` 58, `AngleDelta` 1,252 and `RoundTo8` 4,876 in one Boot Camp
mission, with `bootcamp` still clean. **A patch list is a list of intentions;
the log is the list of installs.**

**The duplicate-patch check earned its keep a second time.** `TakeUid` was
about to be a second reconstruction of `AllocUid`, which `script.cpp` had
already patched at the same address -- the `ScriptCompare` mistake exactly.
`tools/checkpatches.py` failed the build before it could be committed.

**Two `orig.h` names were wrong about what they named, and both came from a
single call site.** `ADDR_SPRITE_DROP_NAMED` (`0x00457820`) walks every object
an army owns and CALLS its second argument — `call ebp` — so nothing about it
is a sprite; the one call site passes `0x0045A030`, which is itself a function
that hands a unit to the AI, matching the "left, AI takes over" message beside
it. And `EvtMarkSet`/`EvtMarkClear` write into the 4x4 ALLIANCE matrix:
`0x00424E80` fills that table with the identity and then allies any two comm
players on the same team, which is what settles it. Fourth and fifth instances
of the same failure, and the fix is the same — read the callee.

**A function can be safe for `AM2_SELFCHECK=1` and still not survive it.**
`LookupOwnerObj` range-checks its army perfectly well and then indexes
`0x004F9ECC`, which is still NULL that early: the selfcheck runs before
`install()`, which is before the game has loaded anything. It took the process
down with 47 functions announced and no summary — the same symptom
`XorChecksum` produced, for a different reason. The question is not only
whether a function survives a random argument but whether it survives the
empty world this runs in.

**And the alias ratchet caught its author.** Naming `0x004267C0`
`ADDR_PAUSE_GAME` collided with the `ADDR_SET_EVENT_FLAGS` already on it: the
very rule added above -- grep the address first -- ignored within the hour. It
failed the check rather than landing. Three modules were also calling
`SetGameDir` under `orig_path_exists`, which made a chdir look like an
existence test; that alias is gone too and the ratchet is 38.

**Everything RunFrame calls is reconstructed too** -- the input poll, the two
comm bookkeeping steps around the state handler, and all five per-state
handlers, in `src/game/win32/frame.cpp`. The sentence here used to end "one
level further down stays original", and that stopped being true without being
noticed: every level below it is ours as well.

**A CONFIRMATION of the rule below rather than another breach of it.** The
menu-request dispatch at `0x00426400` lays its arms out 1, 2, 3, 4, 5, 6, 10,
11, 12, 7, 8, 9, 13 -- so reading them top to bottom and numbering as you go
puts the war menu, ENTER BATTLE NAME and the battle browser three places early.
`frame.cpp`'s `kMenuScreens` already has them right, because it was built from
the table at `0x00426518` and not from the layout. Worth recording that the
rule held where it was followed, not only the places it was not.

**Jump tables and the tables they index are `docs/tables.md`, and this image
dispatches through byte tables constantly.** Six distinct failure modes have
been hit -- arms laid out in a different order from the table, several slots
sharing one arm, an arm that ENDS INSIDE another, an arm unreadable from its own
body, a table that is mostly impossible, and a table that is a FILTER rather
than a dispatch. Every one compiles, passes every static check, and is invisible
to an A/B, because each indexes *something* correctly.

**So: DUMP THE TABLE before writing anything down about the order, and for a
table of any size GENERATE the code from it rather than transcribing it.** A
hand-written mapping of eighty-one codes onto twenty-four arms put six in the
wrong arm and no amount of re-reading found it; what found it was diffing the
groupings against the image's own table programmatically. Dumping a table is the
first half of the rule and not retyping it is the second.

**And find the table that gives the numbers NAMES before reading the arms** --
the item captions turn five zero entries from numbers into the items that do not
turn the soldier to face what he is aiming at.

**States 0 and 3 check the same two flags in opposite orders.** State 0 tests
"leaving" first and state 3 tests "entering" first, so a state entered and left
in the same frame runs its entry action in 3 and not in 0. Reproduced rather
than tidied; it is not obviously deliberate and it is not ours to decide.

**`ADDR_MOUSE_MOVED` was another name from a call site.** `PollMouse` sets it
on X or Y movement, which is where the name came from -- but `UpdateMouseState`
also sets it when button 0 or button 1 CHANGES, so it means "the mouse did
something". The same function has a `je` that can never be taken: it tests
flags left by a compare it has already branched on. Not reproduced, and the
comment says why.

**Three of winmain.cpp's `orig_` macros pointed at addresses that were already
patched.** `orig_init_input`, `orig_init_directdraw` and `orig_report_error`
resolved to the detour and landed in our own `InitInput`, `InitDirectDraw` and
`ReportError` -- correct behaviour under a name that said the opposite. Harmless
here, unlike `orig_parse_action`, where the same mistake silently re-recorded
the oracle. Worth grepping for after any batch: an `orig_` whose address is in
the patch list is either a deliberate probe or a lie.

**Four of those thirteen are no longer guesses, and what identified them was
the loader below them.** Entries 1, 3, 4 and 5 are the `.ani` sweeps --
explosions, roach, vehicles-and-turrets, soldiers -- each a `push <table>;
call FreeAnimTable`, and the table says which file it holds. Naming
`FreeAnimTable` named four of its callers for free. The other nine stay
literals, including `0x0043C720`, which is 432 bytes and does more than free.

**`counts` truncates its reply, and the filter argument is the answer.** Three
newly patched functions were simply absent from an unfiltered `counts`, which
reads exactly like "never installed" -- the failure `src/inject/control.c`'s
own comment predicts and was raised to 4,096 bytes for once already.
`drive.sh ctl "counts Anim"` lists only the matching names. Reach for it
whenever a name you expect is missing rather than concluding anything.

**`ShutdownSubsystems` was `ReleaseAppMutex`, which is its last line.** The
thirteen calls before it are the subsystems coming down in order and only three
are identified, so they are an ordered table in the source rather than thirteen
invented names in `orig.h`. The order is the fact worth keeping; a name each
would be a guess each.

**A table address in this image can be the CENTRE.** The two reverse trig
tables are indexed `[esi + base]` with `esi` running -512..512, so `0x00515D84`
and `0x00515580` are their middles, not their starts. Reading them as starts
put both 512 bytes late and the sin one then wrote over half the cos table --
which is how it was found, because the cos hash moved and the sin hash did not.
The four tables are contiguous and that is the check: sin `0x00514F80` ends
exactly where atanS `0x00515380` begins, which ends exactly where cos
`0x00515784` begins, which ends exactly where atanC `0x00515B84` begins. If a
layout does not tile, one of the bases is wrong.

**Let the function name the family, not just itself.** The roach and vehicle
mask builders went in as "footprints" over "facings" until the vehicle one was
read: it logs `"vehicle mask direction: %d"` under `-traceVEH`. So the tables
are masks and their index is a direction, and `AM2_Anim::directions` was
renamed with them, because that message's counter runs over exactly that field.
One string settled the vocabulary for a struct, two functions, six globals and
a tool.

**A pixel budget can hide a real difference, and that is measured rather than
feared.** Forcing `SetMaxHealth`'s difficulty index to 0 doubles the player's
health; `ab.sh bootcamp` goes from 22 differing pixels to 96 and reports A/B
clean, because 96 is well inside the budget of 500. The budget is what lets the
check survive a moving scene, and it is also what lets a small real difference
through. Read the number, not the verdict — the same lesson the 33,137-pixel
tile painter taught, one rung further in.

**`tools/objdump.py` reads a registered object's fields out of the running
game**, by uid, binary-searching the sorted table `src/game/objtable.c`
describes. Run it against a reconstruction and again under `AM2_NOPATCH=1`: the
leader's max health is 140 on a correct build and 280 on that mutation, which
is 4.0 against 2.0 exactly. It is the check for functions that write an object
field and return nothing — a class neither `tools/vectors.py` (it cannot map
the game's globals) nor `AM2_SELFCHECK=1` (it compares `eax`) can reach.

**A table's BASE is the part an A/B cannot check, and this file's own warning
came true a second time.** `BuildRoachFootprints` writes each record's count
through `[ebp-4]` with `ebp` starting at the points, so the array begins at
`0x00654CA8`; taking `0x00654CAC` as the base put the whole table one dword
early, over the global next to it, with every point still correct.
`tools/maskdump.py` caught it on its first run by hashing the raw region
rather than the decoded records — decode the records and a uniform shift looks
like a table that is simply somewhere else.

**And measure whether the A/B could have caught it, rather than assuming.**
With the footprint sample step doubled from 2 to 4, all 32 records change and
the point total drops from 237 to 25; `ab.sh bootcamp` is still clean at the
usual 22 pixels with an identical log. So that table is verified by
`maskdump.py` or by nothing at all — the same standing as the trig tables
below.

**`tools/trigdump.py` compares the tables byte for byte, and no A/B could.**
They are 4,098 bytes built once at startup and never rewritten, so reading them
out of the running game over the control socket and hashing is a complete
comparison -- run it against our build and again under `AM2_NOPATCH=1`. A wrong
float in a table the renderer consults per sprite need not be visible at all;
the mis-centred write above passed `bootcamp` cleanly.

**And the x87 asm in there is conservative, not required.** `fsin` and `fcos`
are what the original executes, so `trig.cpp` uses them through inline asm
rather than libm. Then the mutation: building the tables with `__builtin_cos`
-- which on this target really is a call to libm and not an inlined `fcos` --
gives the same 4,098 bytes. The results are rounded to float on the way in, and
24 bits of mantissa hide the disagreement for all 256 of these arguments. Say
which of two defensible choices was measured and which was merely reasoned.

**`-Wrestrict` caught a reversed `strcpy`.** `ResetToTitle` copies the
command-line map name into the level's own copy, and I had the operands the
other way round; the two globals are `0x004F9FEC` and `0x00511A88` and the
compiler noticed source and destination were the same object. Compiler warnings
are worth reading on transcribed code -- the wrong direction here would have
silently cleared the map name.

**script.cpp reaches into the image for one thing now, and that one is
deliberate.** The other five `orig_` seams are reconstructed:
`CommSlotForArmy` and `CommSlotHasPlayer` (misc.cpp), `SetGameDir`
(gamedir.cpp), `PreloadSprite` (win32/sprite.cpp) and `DeclareRuleVars`
(event.cpp). What is left is `orig_parse_action`, which exists so
`AM2_PROBE_NOACTION` can re-record the oracle.

`DeclareRuleVars` is the first of event.cpp and shows the usual shape: the
registration table, its teardown, the uid counter and the notify all stay
original and are reached by address, so only the declaring is ours and it runs
in the middle of a live path. Its counter reads 0 -- `LoadLevelScript` calls it
directly -- and a probe says `conds=16 terms=17` on Boot Camp, where
`ReadScript` independently reports `compounds: 16`. Two numbers from different
places agreeing is better evidence than either alone.

**And that A/B can fail, which was worth checking.** Registering none of the
condition terms leaves `bootcamp` at 79,748 differing pixels against a budget
of 500 -- the mission's scripted content never appears, because nothing is
listening for the events that produce it.

**A colour lookup that is the identity cannot prove much, and the mutation
said so.** `ScriptArmyColour` ends in `CommSlotForArmy` (`0x0040F250`), which
walks the comm object's four player records for one holding that army. Making
its match arm return `i + 1` diverges on 20 files across `createexplosion`,
`createvehicle`, `ally`, `createroach` and `setforcecolor`, so that arm is
genuinely covered -- but making the NO-match arm return 3 changes nothing at
all, and neither does the `army == 4` shortcut.

A probe says why in one run where three mutations would have taken three: the
slots hold armies 0, 1, 2, 3, and only 0..3 ever arrive. The lookup is the
identity for every colour a script can write, so two of its three exits are
verified by reading. **Probe to find out which paths run; mutate to find out
whether the ones that run are checked.**

**Renumbering heap pointers has to be scoped, and a global sequence turns one
event into thousands.** `tools/actdiff.py` replaces each pointer with its
first-seen index so a dump survives the DLL changing size. The first version
numbered across the whole log, and the sweep frees each file's strings before
the next -- so when `am2hook.dll` grew, one pointer was first seen in a
different order and every index after it shifted by one. It reported 32 files
diverging, `P1922` against `P1921`, on a parse that was identical. Allocation
order is deterministic inside a file and nothing across files is worth
comparing, so the sequence restarts at each `PARSEALL`.

**Two of the 9,934 records are not stable, and it is the original reading out
of bounds.** `tests/actions-reference.txt` is otherwise exact, but a
`triggerdelay` in `rules/koth_ai_green.txt` recorded `72616C75` in the uid
field -- `"ular"`, a fragment of the `greenRegularOrders` on that very line.
The name is a forward reference, so `ScriptNameUid` falls through to
`AddNameTableName`, and past the table overflow the sweep is already known to
hit after about seventy files that index reads heap that still holds name
strings. The value tracks heap layout, so it moves between runs of the same
build. So the oracle is 9,932 exact records and a short unstable tail: a
divergence there has to reproduce before it is evidence of anything.

**A switch that selects the original has to skip the patch too, and mine had
silently stopped working.** `tests/actions-reference.txt` is recorded by running
the ORIGINAL action parser under our `ReadScript` and our dump, which needed a
runtime flag because `ScriptIf` calls the reconstruction directly and would
never reach `0x00440D70` at all. But once `ScriptParseAction` was itself
detoured, calling through that address came straight back to us -- there is no
trampoline -- so the flag quietly re-recorded the oracle from the very code it
was meant to check. `AM2_PROBE_NOACTION` now both skips the `patch_replace` and
routes the call through the address, because neither half reaches the original
alone.

Proved by mutation rather than by reading, and the first attempt proved nothing:
a `sed` that did not match the padded `case` column left the code untouched, and
both runs "passed". With `attack` actually returning 99, the probe reproduces
all 9,934 records and the same build without it diverges on 37 -- `setaimode`
and, because a bad mode ends the statement, `order`. **A test that cannot fail
has not passed** -- check the mutation landed before believing the result.

**A handler can call the original's callees, and that is the strongest test
available.** Every one of the five is reconstructed while the parsers below it
stay original and are reached by address, so our code runs in the middle of a
live path and the A/B compares the result. Nothing had to wait for the layer
beneath it.

**GENERALISING THAT BUG INTO A CHECK WAS TRIED AND IS NOT WORTH BUILDING.**
The obvious next move is to find every OTHER gate where the original compares
a global against a REGISTER and our C tests the same global for null. Scanned
per function, since a linear sweep of `.text` desynchronises on data and
misses the very site that motivated it -- 8 hits swept linearly against 202
scanned properly, and 0x5122C8 absent from the first.

202 sites over 97 globals, of which 15 functions also null-test the same
global in our C. **Every one of the fifteen is correct**, and they fail for
two reasons that are both ordinary:

- MSVC compiles a null test AS a register compare. `TakeMenuRequest` opens
  `xor esi, esi` and then compares ten globals against `esi`; `HudPostUpdate`
  does the same with `edi`. Those ARE `== 0`, spelled in the register.
- `movsx edx, [esi+0x10]; cmp edx, [0x4F9FDC]` is the `army == g_defaultOwner`
  idiom, in `DamageTrooper` and `UpdateTrooperAction` both, and our C has it.

The one that looked like a signed-versus-null difference was not either:
`cmp [0x511D98], edi; jle` is `ADDR_LEVEL_ID > 1`, which is exactly what
`frame.cpp` writes.

So the discriminator is not the instruction, it is WHAT THE REGISTER HOLDS --
zero, an argument, or a field -- and answering that needs dominator-aware
dataflow, not a scan. A check without it reports fifteen correct functions,
which this file already says to suspect before believing. The real instance
was found by RUNNING the game, and that is what `movecheck.sh` now keeps.

**The save/load path is `docs/saveload.md`**, and it holds the one deliberate
deviation in this tree -- `LoadItems` unlinks objects from the cell grid before
`ItemsReset` frees them, because the original's latent use-after-free there
takes the process down on an ordinary LOAD GAME. Three rules came out of it:

- **A latent defect is invisible until EVERY path that reaches it works.**
  Fixing one thing routinely makes the next thing fail, and a suite that goes
  red after a correct change is not evidence the change was wrong. Two fixes
  landing in one session uncovered a third defect neither could have shown
  alone.
- **`WINEDBG=-all` discards the page fault**, which `make run` sets, so a run
  that dies with nothing in its own log says only "detached". Launch the binary
  outside the harness before instrumenting anything -- one command gave the
  faulting instruction outright after five runs had produced nothing.
- **Static comparison can be EXHAUSTED, and saying so is a result.** Thirteen
  functions on that chain were compared against the image and all thirteen were
  faithful, which is what said the defect was not a mis-transcribed instruction
  and that reading more of it was not the way in.

**`tools/saquit.sh` COVERS THE STANDALONE'S TEARDOWN, which nothing did.**
`samenu.sh` compares a title screen and `samission.sh` a live mission's object
table, and both KILL the process -- so the comm shutdown, the sprite frees and
the leak report never ran in the standalone even once. `ab.sh quit` exists for
exactly that reason on the injected side and found a real bug the first time
it ran.

There is more at stake here than on that side: the standalone's C++ static
initializers run from OUR runtime rather than the MSVC CRT's, so anything they
registered comes down here or not at all. Measured: the game's own messages
are IDENTICAL through a full menu quit -- 7 of them -- and the standalone
reports `Unreleased memory (0) blocks`.

The harness lines are filtered rather than diffed, because the injected build
logs an attach banner, a patch list and the DirectInput hook that the
standalone cannot produce by construction. The two racing thread lines are
compared as a SET and flagged rather than failed, for the reason `ab.sh quit`
already records: one of them can be ABSENT, not merely out of order.

Mutation-checked in both directions, since a new test that cannot fail has not
passed: dropping one game line from the standalone side names the missing
lines and fails, and asking for a heap report that is not there fails
separately.

**`AM2_NOPATCH_NAMES` BISECTS A BEHAVIOUR, AND THE UNIT OF BISECTION IS A
CHAIN, NOT A FUNCTION.** It leaves the named reconstructions original, and
a reconstruction reached by NAME from another reconstruction is not reached
at all -- the caller compiled a direct call to ours. So a set that restores
the original's behaviour has to run from something reached by ADDRESS (in
the injected build that is `WinMain`, which the CRT calls) down to the
defect, and halves of the patch list fail on both sides while the whole
half passes. That is what "trial(822) -> 1, trial(411) -> 0, trial(411) ->
0" means, and delta-debugging over 821 names would have taken an hour to
say it. Write the by-name chain down from the call sites, confirm it as a
set, then drop members greedily: what survives is the path to the defect,
and the deepest survivor is the function to read. Sarge walking through
Boot Camp's hut took eight names and twenty-two trials that way, and the
defect was three misreadings in one function, none of them an offset.

**THE NATIVE BUILD IS TWO BINARIES, and the one a drive talks to is
`build/armymen2-dev`.** `make native` builds both. The player's has no control socket at all; the
development one adds the socket (on by default), the
injected input, and savestates over a fixed-address arena
(`snap save|load FILE`); F5 writes the GAME's save as a fixture and F9
reloads it. Anything that pokes, dumps or drives the
native game uses the dev binary; STATUS.md has the measurements.
`tools/savecheck.sh` is the cross-build serialisation A/B built on it --
the game's SAVE GAME written by one build and loaded by the other, judged on
the object tables, because the file's bytes carry raw pointers by design.
**`tools/enterlevel.sh [-f FOLDER] SAVE` enters a mission from a save and
leaves it FROZEN before its first frame, on any of the three builds, in
fifteen seconds and without a menu**: the socket's `loadgame` makes the LOAD
button's four writes and `AM2_PAUSE_ON_ENTER=1` freezes the arrival. One
file gives one table md5 across runs and across builds, Boot Camp included
-- it saves once the game-proc block's first string holds a folder name,
which in Boot Camp is empty (no player), so the dev binary's F5 fills it
with the level's name; the level name is the string 0x20 further in, and
reading that one as the folder is how this was first got wrong. Reach for it before hand-driving to a point in a
mission: it is a fixed point, and a drive is not.

**Movement and the input path are `docs/movement.md`.** Two defects a month
apart each made the player unable to move and neither was visible to any A/B --
both sides are driven with the same input and agree about ignoring it.
`tools/movecheck.sh` is the configuration that catches the class, and the only
one in the project that drives a REAL device: `xdotool` events go through Wine's
own DirectInput, where the socket's `cursor` and `key` write the game's globals
and exercise neither poller. Its assertion is DISPLACEMENT, not equality.

**A gate whose operand is a REGISTER cannot be checked by reading the global.**
`ADDR_OBJ_CTX_OBJ_A` is non-zero in a live mission on both sides, so reading it
proved nothing until the instruction was decoded -- and `checkoffsetuse` cannot
see it either, because every offset involved is correct.

**The combat layer is `docs/combat.md`, and NO configuration here reaches it.**
One `counts` line settles what used to be a dozen separate mysteries: on a
driven Boot Camp mission with the frame ticking, `ShotStrike`,
`ApplyShotDamage`, `DamageObject`, `CreateMissile`, `FireWeaponAtPoint` and
`FireWeaponAtObject` all read 0. So the whole layer is checked by the oracles in
`docs/oracles.md` or not at all.

**A HOT function can still be undiscriminated**, which is the finding worth
carrying out of it. `ShotStrike` runs 13,582 times on that drive and the suite
cannot see a mutation to its penetration branch, because almost every one of
those calls finds nothing at the point and falls straight through. **Ask which
BRANCH the calls take, not how many there are** -- a five-figure counter can
certify only the path that does nothing.

**And the suite cannot see MOVEMENT at all**, measured categorically rather than
inferred: `MoveStepPoint` runs 175,145 times and adding a flat 50 to every step
leaves `combat` clean. Every configuration reaching live play has its pixel
check disabled by construction, and nothing else it compares mentions a
position. `tools/objdump.py --table` closes half of that -- it diffs all 1,609
objects exactly, at the BRIEFING, so it covers what the map load builds and not
what happens once the mission starts.

**How to read an A/B result is `docs/abnotes.md`.** Two of the four artifacts
are noisy by construction and most of the time lost to this suite has gone on
reading a noisy number as a result. The five rules:

- **`mission` and `combat` have their pixel checks disabled by construction** --
  two unsynchronised runs of a live scrolling mission differ by a quarter of the
  frame either way. The log is the evidence there.
- **The `frames` line is `orig/recon`.** Read WHICH SIDE moved before the ratio,
  and read it out of the named `.volatile` artifacts rather than remembering the
  argument order -- that was got backwards within a day of being written down.
- **Check `uptime` before believing a failure**, and reach for the PARENT COMMIT
  rather than a fourth re-run.
- **Give the control as many samples as the thing it controls for, in BOTH
  directions.** One clean control does not establish a regression; one dirty
  control does not establish one either.
- **Ask whether the new code EXECUTES before comparing runs of it.** A counter
  of 0 answers "could this possibly be the cause" outright and needs no samples.

**Read the loop, not the data, when a table's bounds are in question.**
`tools/scripttokens.py` was reading a range I guessed -- `0x00487A00` to
`0x00488100` -- and reported 141 keywords. `ScriptLookupToken` walks from
`0x00487C90` and stops at `0x00488258`, eight bytes a step, so there are
**185**. The 44 it was missing are not filler: they are the whole AI vocabulary
-- `setaimode` with `attack`/`defend`/`ignore`/`evade`, `setaipose` with
`stand`/`kneel`/`prone`, `setnpc`, `setzombie`, `setscientist`, `fireweapon`,
`unitfire`, `hasitem`, `dropitem`, `isally`, `teamscore`, `group`, `setuilock`,
`setdamagepad`. `docs/scripttokens.md` is generated and lists all of them.

Lookup is case-sensitive over a lower-case table; the caller lower-cases first
through `_strlwr` at `0x0046D7D6`, whose ASCII path is the plain
`cmp 0x41 / cmp 0x5A / add 0x20`. That is why the scripts write `Pad`, `pad`
and `PAD` interchangeably.

The token record is `{kind, line, value}` -- 12 bytes -- and the context is
`{capacity, count, tokens}`, growing ten entries at a time. Both were read out
of `AddToken`'s body; the `TOK_TEXT`/`TOK_ID` labels this file's `orig.h` used
to carry were taken off a call site and were wrong about the middle field,
which is the LINE NUMBER. Kinds 1..4 store a dword by value, kind 5 owns a
`malloc`'d copy, and kind 0 or 6 advances the count leaving the value untouched
-- the switch covers exactly `kind - 1` in `0..4`.

**Boot Camp is not MAP 01, and confusing them costs a verification path.**
Boot Camp is `data/bootcamp/` and declares no `variable` at all; `kitchen` is
MAP 01 of the campaign and declares two. Only four map directories use the
statement -- `kitchen`, `homeland`, `frontyard`, `8ball` -- so the whole
name-table layer is compared on the campaign or nowhere. `tools/ab.sh campaign`
exists for that, and it is clean.

Its evidence is better than the usual log match. `ReadScript` prints its own
summary, `lines: 1225  tokens: 2895  names: 316  compounds: 91`, and that is
four independent totals agreeing rather than one message being identical.

**Do not drive the campaign through RECRUIT.** A name that already exists is
rejected in silence, so the second run of a scripted sequence sits on the
dialog looking exactly like a broken reconstruction. Select the existing player
instead -- SINGLE PLAYER, the player row, SELECT, NEW -- which is idempotent
and creates the player on the first run.

**A statement handler rewrites its own tokens, and that is where kind 7 comes
from.** `ScriptTokenText` has eight arms for seven kinds and they do not line
up: kind 6 -- the one the kind table calls `Name` -- writes nothing, while kind
7, which the kind table has no entry for at all, resolves through the name
table. `NextToken` emits neither. `ScriptVariable` is what produces kind 7: it
turns its String name token into a kind-7 reference to the table entry it just
made, and frees the string the token owned.

A name-table entry is sixteen bytes -- a `malloc`'d name, a type, a value and a
flag always written as 1. `AddNameTableName` type 0 takes a fresh uid from the
counter at `0x00511DF4`; types 1..3 store what they are given; anything else
logs and stores it anyway, so that arm complains rather than rejects.

**Token buffers cross between our code and the original's, so the allocator
has to be the game's.** `src/game/crt.h` points `am2_malloc`/`am2_realloc`/
`am2_free` at the statically linked MSVC CRT inside `ArmyMen2.exe`. This is a
narrow seam, not a general escape hatch: nearly all of `src/game/` is
arithmetic over memory the caller supplies and needs none of it.
