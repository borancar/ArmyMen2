# What is unexercised, and what checks it anyway

**"Unexercised" is not "unverified", and the distinction is the whole point of
this file.** The heading below means NO DRIVE REACHES IT, which is a fact about
this environment -- no configuration here provokes a fight, opens a DirectPlay
session, or reaches an in-mission dialog. It says nothing about whether the
function agrees with the original.

Thirty-one of the thirty-two are checked, almost all by ENUMERATING ORACLES:
tools that build a corpus, run the ORIGINAL under Unicorn over it, and compare.
`tools/checkclaims.py` recomputes that split from the tools themselves, so the
number in the heading below cannot go stale.

Two rules govern everything here and are worth reading before any of the
detail:

- **A counter of 0 usually means nothing.** Of 1,640 traced functions,
  `tools/blindspots.py` reports 54 counters that mean what they say, 1,346
  blind because every caller is ours, and 240 reached by address. So a zero is
  meaningless about thirty times out of thirty-one, and it gets WORSE as the
  reconstruction gets better.
- **A check that cannot fail has not passed.** Every oracle here is
  mutation-checked, and the useful part is usually the COUNT: when a mutation
  fails exactly as many cases as the table has entries for that arm, that is
  evidence the corpus reaches every arm, which no amount of "0 disagree" is.

The recurring trap, which this project has now hit four times: **a corpus
derived from the model under test cannot fail against it.** Deleting an entry
from the model deletes the case that would have caught it, and the tell is that
the CASE COUNT moves under the mutation rather than the verdict.

- **SEVENTEEN OF THE EIGHTEEN NAMES BELOW HAVE BLIND COUNTERS, so the list's
  own zeros are not evidence for it.** Measured: a Boot Camp drive past both
  dialogs, with HudSquadPaint at 55,274 to prove the mission was live, reads 0
  for every name in the list -- and `tools/blindspots.py` says only
  `StateLeave` among them has a counter that could have moved. The other
  seventeen have every caller reconstructed, and this file says elsewhere,
  repeatedly, that such a zero "is not evidence of anything".

  That does not make the list WRONG. Most entries carry their own reasoning
  beside them -- the software rasteriser is a layer nothing enters, the
  multiplayer three need a session this machine cannot open, StopNamedSound
  needs a level record that names a sound -- and that reasoning stands on its
  own. What is not evidence is the counter, and the heading "unexercised"
  reads as though it were.

  The measurement that DOES generalise: of 1,640 traced functions,
  blindspots.py reports **54 counters that mean what they say**, 1,346 blind
  because every caller is ours, and 240 reached by address. So a zero is now
  meaningless about thirty times out of thirty-one, and reaching for `counts`
  as evidence is worth checking against that tool first. The number gets
  worse as the reconstruction gets better, which is the same observation
  this file already makes about counts being "a measure of what still crosses
  an original boundary, not of what runs".

- **"UNEXERCISED" IS NOT "UNVERIFIED", and 31 of the 32 below are
  now checked.** The heading means no drive reaches them, which is a fact
  about this environment; it says nothing about whether they agree with the
  original. Measured against what actually exists:

  | | checked by |
  |---|---|
  | `KeyFieldC` | 96 vectors in tests/vectors.h -- it has been in the selftest all along |
  | `EncodeBig`, `EncodeSmall` | tools/rlecheck.py, 280 cases |
  | `MpNameInk`, `MpNamePaper` | tools/mprowcheck.py, 240 cases |

  `UnitWeaponInfo` joins them: `tools/weaponcheck.py` enumerates its THREE
  range arms and TWO readiness arms over 3,240 cases. The mutation that
  matters is the tightest one -- turning the cooldown comparison from `<` to
  `<=` fails 54, which is exactly the cases where the elapsed time equals the
  cooldown, and is the evidence that the corpus straddles that boundary
  rather than merely passing near it. Dropping the zero-range arm fails 216,
  applying the timed weapon's extra scale to every kind 486.

  `ListDropOldest` joins them too, by `tools/listcheck.py` -- this file calls
  it "the sharpest case of a function that cannot be driven rather than merely
  has not been", and fourteen cases now cover its whole space. malloc is a
  BUMP allocator rather than a fixed address, because a fixed one would alias
  the array being copied out of; free is a bare `ret` with a hook recording
  its argument, so "what was freed" is an observable rather than a crash.

  Its mutation counts are all exactly derivable -- copying from row 0 instead
  of row 1 fails 10, which is the counts of two or more; never freeing the
  owned value fails 6, which is the owning cases with a non-null pointer.

  **AND THE ORACLE CAUGHT THE MODEL, not the code.** The first model freed the
  dropped row's value whenever the list owns its values, and the
  reconstruction guards on the pointer being non-null. The one case that
  distinguishes them is a count of ZERO, where row 0 is blank -- so the corpus
  reaching an empty list is what made the difference visible at all.

  `CanPlaceAt` joins them by `tools/placementcheck.py`, 560 cases over four
  rects, five cell patterns and seven blockers. It stubs the two mask
  builders with an ASSEMBLED COPIER rather than a bare `ret`, because both
  WRITE the mask into a stack buffer whose address is not known until the
  call happens -- a `ret` leaves the scan reading whatever the stack held.

  **ITS FIRST RUN DISAGREED ON 64 OF 320 CASES AND THE HARNESS WAS THE ONE
  THAT WAS WRONG.** Two of my own scratch buffers overlapped: the kind grid
  was 0x10000 bytes at DATA+0x9000 and the mask source sat at DATA+0x11000,
  so every case zeroed the mask before the call, every cell was skipped, and
  the original was recorded as accepting placements it refuses. It reads
  exactly like a defect in the game. What found it was hooking the stub and
  printing what it had been handed -- the mask source was all zeros, which no
  amount of re-reading the disassembly would have shown.

  Two mutations of the model passed the first corpus and both gaps were the
  CORPUS: every blocker filled the whole grid, so an origin off by one still
  landed on a blocked cell, and every cell byte was 0 or 1, so the original's
  `test al,al` and `test al,1` were the same predicate. Position-dependent
  blockers and a cell value of 2 make them fail 30 and 48. What stays
  uncaught is the `and eax,0xffff` on the tile index, and that one is a
  theorem rather than a gap: with a tile shift of 4 the index only exceeds
  16 bits above y = 4096, which is a thousand times more tile rows than any
  map in the game has.

  `RestoreTileSet` joins them by `tools/tilesetcheck.py`, 94 cases, and it
  was the last one standing on reading alone. Twelve callees are stubbed in
  THREE conventions -- the two COM slots are stdcall and pop their own
  arguments -- and a synthetic `.atl` is served through the CRT stubs, so the
  file walk, the palette remap and the lock bracket are all compared.

  **ITS SIBLING DOES NOT COVER IT, and that was worth measuring rather than
  assuming.** `LoadAtlFile` reads the same format and runs on every map load,
  so the tempting argument is that the parse is verified by it. Normalised
  disassembly says no: 177 instructions against 206, similarity **0.245**,
  and not one shared run of six. The two are a rewrite of each other rather
  than a re-emission -- the SeqCtx shape, not the AiStepTrack one -- so what
  the sibling establishes is the FILE FORMAT and nothing about these
  instructions. This file already says to diff before believing a
  resemblance; the same command is worth running before leaning on one for
  coverage.

  **A MODEL THAT LOOPS ON THE CORPUS CANNOT SEE THE ORIGINAL'S LOOP
  CONTROL.** The original walks chunks while `offset < formSize`, accumulating
  eight bytes of header plus each payload; my model iterated the case's chunk
  LIST instead, so removing the DIB payload from the accumulation passed all
  84 cases. It is the boolcheck trap in a new place -- the corpus was driving
  the very thing under test. Cases whose declared size is SHORTER than the
  bytes present make the arithmetic decide, and the four offset mutations
  then fail 8, 2, 10 and 50.

  The original's own defect is reproduced and the corpus reaches it: the arm
  that logs "Error on Lock in RestoreTileSet()" is the arm holding the lock,
  and it does not unlock. Tidying that fails 16 cases.

  `ExitOneFromVehicle` joins them by `tools/vehexitcheck.py`, 85 cases. Its
  output is not its return value -- five separate refusals all answer 0, so
  an oracle comparing `eax` would pass with four of them deleted. What the
  function DOES is call things, so all ten callees are stubbed and the
  compared value is the TRACE with the deploy point in it. Three mutation
  counts are exactly derivable: ignoring the boat's health fails 8, the boat
  cases with none; ungating the broadcast fails 6, the reachable cases that
  do not broadcast; and loosening the last-occupant test fails 10.

  **AND A STUB HAS AN ABI, which cost the first run of it.** Two of the ten
  callees are thiscall -- `CommMustBroadcast` and `ListRemoveAt` -- so `this`
  arrives in ECX rather than on the stack AND the callee pops the stack
  arguments itself, the rule this file already states for reading these
  bodies. A cdecl-only stub gets both halves wrong and shifts every frame
  below it.

  The symptom is worth knowing because it does not look like an ABI error:
  71 of 85 cases differed with the ARM SEQUENCE CORRECT on every one of them,
  and only the arguments wrong. Control flow surviving while every argument
  is rubbish is the tell for a stack that is off, not for a misread branch.

  `PlanPathTo` joins them by `tools/pathplancheck.py`, 24 cases. It is a
  model-versus-original check and NOT a replay, on this file's own rule: it
  calls the image's 1,168-byte `FindPath`, so replaying the cases against our
  C would mean a test hook in production code for a call the game never makes
  that way. Six callees are stubbed and what is compared is the work AROUND
  them -- the route written into the object at a stride, the TERMINATOR one
  step past the last waypoint, and the two different deadlines a success and
  a failure leave behind.

  Every mutation count is derivable, which is the evidence the corpus reaches
  each arm rather than merely passing near it: dropping the terminator fails
  12, which is every success; giving a failed search the success deadline
  fails the other 12; ignoring what the collapse returned fails 4, which is
  the two routes that collapse; and both a halved stride and a swapped x/y
  fail 8, which is the successes with at least one waypoint -- the zero-length
  success writes only a terminator, where a swap of two zeros is invisible.

  `AiStep` joins them by `tools/aicheck.py`, 92 cases, and it is the
  dispatcher rather than an arm on purpose: this file's own rule is that a
  family of cold functions is best attacked where it converges, and the six
  arms reach sighting, pathfinding and the object model, none of which
  emulates outside the game. So the arms are stubbed and what is compared is
  WHICH ONE RAN -- the question a jump table answers and reading the arms
  cannot.

  Its mutation counts are all derivable, which is the evidence the corpus
  reaches every arm rather than merely passing: sending mode 6 to the default
  fails 5, the number of tiles tried; swapping modes 0 and 1 fails 10; making
  the default `ignore` instead of `track` fails 65, which is thirteen modes
  by five tiles and is exactly how many of the eighteen mode values are not
  one of the five named arms; and shifting the region byte fails 90, every
  case but the two null ones.

  **THE OUTPUT IS THE REGION HALFWORD, NOT THE RETURN VALUE**, so the field
  is seeded with a sentinel before each case -- the null guard really does
  write nothing, and "wrote nothing" has to be distinguishable from "wrote
  zero". An oracle comparing `eax` here would pass with the body deleted.

  **AND IT MADE `checkclaims.py` CREDIT FIVE FUNCTIONS IT ONLY STUBS.** The
  split above is computed by resolving each name to its address and searching
  the oracles; `aicheck.py` holds all six arm addresses in its CODE, where
  stripping docstrings cannot help. So a tool may now DECLARE its subject in
  a `CHECKS` tuple, and a declaration wins over the search outright. The
  general shape is one this file already states twice -- a mention is not a
  test -- and the new part is that a stub is a mention the prose filter
  cannot see.

  `AiStepTrack` and `AiStepDefend` join them together, by
  `tools/aitwincheck.py`, and the tool exists because of the near-miss this
  file already records: two functions that read as one written twice, and a
  helper factored out of them would have flattened a real difference in
  silence.

  **THE DIFFERENCE IS OBSERVABLE ONLY WHILE ROUTING, which is sharper than
  the account above and was measured rather than reasoned.** The turn test
  sits before the second promotion in one and after it in the other, and the
  still-moving path jumps straight to that promotion in both -- so Track
  turns while walking and Defend does not. On the ARRIVED path the two orders
  are equivalent, because the first promotion has already run and promoting
  again is idempotent: swapping the order there fails NOTHING, and that is a
  theorem rather than a gap in the corpus. Of the 12 inputs on which the two
  functions disagree, all 12 are routing and none is arrived.

  So the tool checks each against one model parameterised by which twin it
  is, AND asserts that the two still disagree somewhere -- a check that only
  confirmed each against itself would still pass if one were rewritten into
  the other. Making Defend turn on the route path fails at once; so does
  dropping the route path's promotion.

  `RoachBite` joins them by `tools/roachbitecheck.py`, 90 cases -- and this
  file already says why it must be checked here or nowhere: a live MAP 01 run
  with nine roaches alive leaves its counter at 0 after two minutes, because
  the roaches run and nothing ever walks into one. A longer wait was tried.

  **THE TRIG TABLES ARE SEEDED**, which is what makes the geometry checkable:
  Cos8 and Sin8 are `table[h & 0xFF]` over two float[256] that
  BuildTrigTables fills at startup, so in the file they are .bss zeros and an
  unseeded run computes the same bite point for every facing. That is
  shakecheck's trap one subsystem over.

  **AND THE FIFTH ARGUMENT'S UPPER BYTES ARE UNINITIALISED STACK.** The
  original computes `facing + 0x80` into AL, stores it as a BYTE into a frame
  slot, and pushes that slot as a DWORD -- so the top three bytes are
  leftovers, and the first run of this tool reported 36 differences that were
  all 0x0781 against 0x0081. Only the low byte is the value. Comparing all 32
  bits would be comparing the frame, which is the rule this file already
  states for the widget field the constructor never writes.

  One mutation is a theorem: the bite box is (-24, -24, 24, 24), so swapping
  LEFT with TOP cannot be detected by any corpus. Swapping left with RIGHT
  fails all 90, which is what says the rectangle is checked at all.

  `NearestClearVehiclePoint` joins them by `tools/vehpointcheck.py`, 36 cases
  -- and what is compared is the SEQUENCE OF POINTS it asks about, not the
  point it returns. That is the choice `tools/roachcheck.py` makes for the
  same reason: the answer is one point, and a wrong turn order gives the
  right answer from the wrong place often enough to pass a spot check. This
  file already records the sibling `NearestClearPoint` running eight times on
  a drive with a mutation that skipped the spiral entirely going unnoticed.

  Three of the four geometry facts fail a mutation: the leg grows every
  SECOND turn -- the flag that makes it a spiral rather than a diamond, 18
  cases -- the step is shifted left by 4 so the table's 1 is sixteen world
  units, 27, and the turn fires on `>=`, 27. The direction wrap needed care:
  wrapping at 4 makes the MODEL index past a four-entry table and crash,
  which proves nothing about the corpus, so it is wrapped at 2 instead and
  fails 18. **A mutation that crashes the model is not a mutation that the
  corpus caught.**

  `RefreshScreen` joins them by `tools/refreshcheck.py`, four cases over four
  things a reading gets wrong. The present flag is SAVED AND RESTORED rather
  than cleared, so setting it back to 1 turns presenting on for a caller that
  had turned it off -- that fails 3 of the 4, the three where it did not
  start at 1. RefreshDraw is called TWICE, which a reading naturally
  collapses to once. And the blit is BltFast through vtable slot 0x1C with
  six arguments, whose destination comes from the screen RECT and whose
  source rectangle comes from the screen CLIP -- two globals sixteen bytes
  apart, and taking the source from the rect fails every case.

  **THE SURFACE IS A FAKE OBJECT WITH A FAKE VTABLE**, so the call through
  slot 0x1C lands on a stub that records what it was handed. That is the only
  way to check the argument order of a COM call: nothing else in the process
  knows what a BltFast was asked to do, which is why this file's note that
  the DirectX boundary is "verified by reading" held for so long.

  `StateLeave` joins them by `tools/stateleavecheck.py`, and it is four cases
  because only one of them matters. **THE SLOT IS RE-READ AFTER
  MovieForget** -- `mov esi, [0x515F98]` after the call, not a register kept
  across it -- so if that callee clears the slot, the original skips the stop
  and the delete. Our C reproduces the second load, which looks redundant
  until the stub is allowed to clear it: keeping the pointer in a register
  instead fails exactly ONE case, the one where forgetting clears the slot.

  Nothing here makes MovieForget clear it, so the difference is unreachable
  by any drive and invisible to a reading that does not ask what the callee
  may do to the global it was reading. **A re-read after a call is a question
  about the CALLEE, and the corpus has to answer it rather than assume it.**

  `RowRelease` joins them by `tools/rowreleasecheck.py`, twelve cases over
  forty-eight bytes. **THE FREE'S ARGUMENT IS THE POINT.** This file says a
  free is the weakest possible TOUCHER when naming a field; the same fact
  from the other side is that a free of the wrong pointer, or a double one,
  is invisible to every artifact an A/B compares. So the free is hooked and
  its argument recorded, and the ORDER of the two calls with it -- the
  unregister must come first, or the map is handed a buffer that has just
  been released. Freeing the row instead of its buffer fails 9 of the 12,
  and so does swapping the order; making the guard `== 1` rather than
  non-zero fails 6, which is exactly the two owning values that are neither
  0 nor 1.

  **THE WHOLE AI BAND IS COVERED NOW, and that paragraph below about it is
  history.** This file says of `counts Ai` returning every counter at 0 that
  "everything in that band is verified by reading until a drive exists that
  provokes a fight". No such drive exists and the band is checked anyway: the
  dispatcher by `tools/aicheck.py`, the six arms by `aiignorecheck`,
  `aitwincheck` (two at once), `aifollowcheck` and `aicheck`'s forwarder
  test, and the two helpers by `aiwalkcheck` and `hitreactcheck`. Nine
  functions, none of them reachable here, none of them now resting on a
  reading.

  `AiKeepRange` is the last of them, by `tools/aikeeprangecheck.py`, 3,840
  cases over its eight decisions. **THE EARLY RETURN IS THE ONE WORTH
  NAMING**: with no observer this arm returns before the turn test that every
  sibling still runs, so a unit with nothing in sight does not even turn. It
  is one `je` to the epilogue rather than to the tail, invisible in any
  summary of what the function does, and letting it fall through instead
  fails 1,680 cases.

  `AiStepFollow` joins them by `tools/aifollowcheck.py`, 384 cases. Three
  things separate it from its siblings and each is a merge waiting to happen:
  it zeroes OBJ_OFF_SCRIPT_STATE UNCONDITIONALLY at the top where the others
  do it only on arrival; its route arm is reached TWO ways, by range or by the
  leader having MOVED; and its copy into OBJ_OFF_FIELD_C0 comes from the
  CONTEXT where every sibling takes it from the object. That last is the
  dangerous one -- same instruction shape, different source -- and it fails
  160 of the cases when taken from the object instead.

  **AND ITS FIRST CORPUS COULD NOT SEE THE UNSIGNED COMPARE.** The signed
  mutation that fails 32 cases in tools/aiignorecheck.py failed NOTHING here,
  because this corpus had no deadline in the FUTURE -- the one input where
  the wrap matters. Adding one takes it to 384 cases and the mutation to 36.
  A sibling's corpus is not inherited by writing a sibling's tool.

  `AiWalkStep` joins them by `tools/aiwalkcheck.py`, 144 cases -- and it is
  the family's SHAPE WITH DIFFERENT NUMBERS, which is why it is checked
  rather than read beside its siblings. It compares SIGHTC_OFF_DEST_DIST at
  0x34 against 12 where AiStepIgnore and the twins use SIGHT_OFF_DEST_DIST at
  0x28 against 32, reads SIGHTC_OFF_FOUND at 0x20 rather than 0x1C, and
  writes `out[4]` where every sibling writes `out[1]`. Four numbers, none
  shared, in functions that otherwise read alike -- the shape this file
  records as costing a rewrite when one is written from another's outline.
  Giving it the siblings' threshold fails 24 cases, which is exactly the
  `dist == 13` inputs.

  `AiStepAttack` joins them inside `tools/aicheck.py`, where the dispatcher
  it belongs to is already checked. It is a pure forwarder, so THE ONLY THING
  THAT CAN BE WRONG IS THE ORDER -- and this project has had exactly that:
  ADDR_ENTER_VEHICLE's two arguments went in reversed from a comment in
  orig.h, every field access landed on the other record, and nothing inside
  the function looked wrong. A forwarder is that failure with nothing else to
  hide behind, and this one lived in gameproc.cpp as `Call407710`, typed
  `void(int32, int32, int32)`, until the dispatcher said what the three were.
  Three distinguishable values go in and the stubbed body records what it was
  handed.

  `AiStepIgnore` joins them by `tools/aiignorecheck.py`, 480 cases, and it is
  the first of the AI ARMS checked from the inside -- `tools/aicheck.py`
  compares only which arm the dispatcher picks and stubs the arms themselves.

  **THE DELAY IS COMPARED UNSIGNED, and that is why a reading is not enough.**
  `cmp eax, 0x82; jb` on `clock - deadline` means a deadline in the FUTURE
  wraps to a huge number and PASSES, so a unit whose deadline has not arrived
  turns immediately rather than waiting. No drive produces that -- a deadline
  ahead of the clock is not a state the game reaches on its own -- and a
  signed compare is indistinguishable from it in every other case. Making the
  model signed fails 32 cases, all of them that one.

  Two of the counts are exactly derivable, which is the evidence worth
  keeping: moving the arrival boundary fails 80, the single `dist == 0x20`
  value times four hit values, two observers, two found flags and five
  clocks; and not consuming the hit fails 240, the four arrived distances
  times the three non-zero hits times the same twenty. Turning while
  observing fails 84 and the delay boundary 32.

  What it does not cover is AiRouteToward, which is stubbed -- the first arm
  is checked by whether the call HAPPENS, not by what it does.

  `TakeNumberKey` joins them by `tools/numberkeycheck.py`, 2,048 cases, and
  it is the first oracle here written to check a REWRITE rather than a
  transcription. Our C is a loop where the original is eight inlined copies,
  and this file already argues that is the right trade -- but the argument
  rests on two claims that reading it twice cannot settle: that the `return`
  after each store makes the arms an if/else chain so the LOWEST key wins,
  and that `&&` keeps KeyChanged behind IsKeyDown as the original's second
  branch does.

  **IT COMPARES THE CALL COUNT AS WELL AS THE SLOT**, which is what makes the
  first claim checkable: a scan that does not stop asks IsKeyDown eight times
  where the original asks it once per key up to the match. Making the model
  run on fails 1,100 cases; reversing the scan so the highest key wins fails
  846. Neither is visible in the answer alone on the single-key inputs, which
  is most of what a drive would ever produce.

  The output is the SLOT and not the return value: the original leaves
  whatever its last failed test left in eax and our C returns void, so
  comparing returns would compare the register allocator. Seeded with a
  sentinel, so the no-match path -- which really does fall through without
  storing -- is distinct from a key-1 press. Making no-match write 0 instead
  fails 802.

  `CheckSaveTag` joins them by `tools/savetagcheck.py`, 200 cases -- and the
  reason a nine-line function needs an oracle is the case no drive can make.
  Its read goes into ITS OWN FIRST ARGUMENT SLOT: `lea ecx, [esp+4]` hands
  fread the address of the `FILE *`, so the pointer is overwritten by the
  bytes read. Our C reproduces that by initialising the local FROM fp, which
  reads as a pointless cast until fread returns SHORT -- then the untouched
  bytes still hold the old pointer and the tag compared is a mixture of the
  file's bytes and the caller's stack. A savegame either has four bytes or
  the read fails, so that is verified here or nowhere.

  fread is hooked in Python rather than stubbed in assembly, because the
  destination is a stack address not known until the call happens -- the same
  reason tools/placementcheck.py assembles a copier. The hook writes exactly
  `n` of the four bytes.

  The mutation counts are internally consistent, which is the evidence worth
  having: 24 of the 200 cases match and 176 do not, so returning 0 on a match
  fails 24 and mis-logging the line fails 176, and they sum. Starting the
  local at zero instead of at fp fails 32 -- the short-read cases alone, which
  is what says the corpus reaches them and that the initialisation is
  load-bearing rather than decoration.

  `AiHitReact` joins them by `tools/hitreactcheck.py`, 14,594 cases -- and it
  is the second REPLAY oracle in the tree after `tools/firepose.py`, not a
  model comparison. It calls NOTHING: not one call instruction in its 176
  bytes, and it reads only its three arguments and two constant tables. So
  nothing it reaches is still the image's, and `--emit` records the cases into
  `tests/hitreactvec.h` for `tests/selftest.cpp` to replay against our C.
  That proves the transcription, where a model comparison only proves the
  model.

  The corpus straddles both boundaries of the ladder for every rank: the
  thresholds are 32, 48, 56, 64, 80, 96, 112 and 128, and the seeds sit just
  below, on, and just above each half and each whole. Mutating the C rather
  than the model gives the SAME counts as mutating the model -- 96 for the
  half boundary, 7,296 for the observer gate -- which is what says the two
  halves are testing the same thing.

  **AND TWO MUTATIONS CANNOT FAIL, PROVABLY.** The pose table is indexed
  `class * 2 + (seed >= 0x80)`, and that index is only read where
  `seed < limit >> 1`. The largest threshold is 128, so the largest half is
  64, and 64 < 0x80 -- the bit is always zero, entries 13, 15 and 17 of
  ADDR_HIT_POSE_BY_CLASS are unreachable, and the original's
  `cmp cl, 0x80; sbb edx, edx; inc edx` computes a constant. Third instance
  of a theorem rather than a gap, after AM2_SHAKE_FALLOFF's continuous ramp
  and placementcheck's 16-bit tile index.

  `AllObjectsInRect` joins them by `tools/rectquerycheck.py`, 312 cases over
  two map configurations. This file listed it as beyond BOTH existing
  harnesses -- `tools/vectors.py` refuses it because it reads two globals,
  and `AM2_SELFCHECK=1` would take the process down on its null descriptor,
  the way `LookupOwnerObj` did -- and that was true of those two harnesses
  and not of the function.

  **THE POINT OF IT IS THE SIBLING.** `ObjectsInRect` runs 112 times on a
  drive and the two were transcribed within an hour of each other, differing
  by four bytes of jump apiece: an entry clip that is the OPPOSITE test, and
  a home-cell rule with two arms here against three there. Both differences
  are invisible to any run that exercises only the other one. Taking the
  sibling's clip fails 120 cases and its third arm 20 by row and 16 by
  column -- so the pair really is distinguished now, where before the only
  evidence either way was that the two disassemblies had been read
  carefully.

  **`IntersectRect` IS SHARED RATHER THAN MODELLED TWICE.** The one import
  does not exist under emulation, so the IAT slot points at a mapped stub
  implemented in Python -- and the MODEL calls that same Python. That is
  `tools/formationcheck.py`'s lesson used in reverse: there a hand-written
  model of `AngleDelta` produced 256 mismatches that were all the model's,
  and the cure was to call the image's own function. Where that is
  impossible, one implementation serving both sides keeps the count of
  truths at one.

  **AND THREE MUTATIONS FAILED NOTHING UNTIL THE CORPUS LEARNED SOMETHING.**
  The home-cell guard only applies where `y > y0`, so an object in ROW 0
  cannot reach it however its hit rect is placed, and the first attempt at
  covering the dropped arm put its objects in row 0. A cell index past the
  reported width ALIASES a later row's slot, and the column guard skips
  whatever it finds there unless that object's own left is in the
  out-of-range column. And with the world extent equal to `cols <<
  CELL_SHIFT` the entry clip makes both clamps vacuous, so a second map
  configuration whose descriptor under-reports its extent is what makes them
  observable at all.

  The other four are verified by READING, which is the standing worth
  stating plainly rather than leaving a reader to infer it from a list whose
  title is about drives. `KeyFieldC` in particular should never have read as
  unverified: a pure function of one argument is what tools/vectors.py is
  for, and it was covered before this list was written.

- Unexercised by any drive: `KeyFieldC`, `CheckSaveTag`, `ListDropOldest`,
  `MpNameInk`, `MpNamePaper`, `PlayerLatency`,
  `StateLeave`, `RowRelease`, `EncodeBig`, `EncodeSmall`,
  `RestoreTileSet`, `AllObjectsInRect`, `ItemSetBox`, `AiStepIgnore`,
  `AiStepDefend`, `AiStepTrack`, `AiStepFollow`, `AiStepAttack`, `AiStep`,
  `AiKeepRange`, `AiWalkStep`, `TakeNumberKey`, `ExitOneFromVehicle`,
  `AiHitReact`, `PlanPathTo`, `NearestClearVehiclePoint`,
  `ShakeAt`, `StartShake`, `RoachBite`, `CanPlaceAt`, `UnitWeaponInfo`, and
  `RefreshScreen` —

  **THE ROACH FAMILY IS HEAVILY EXERCISED AND ENTIRELY UNCOMPARED, which is a
  different failure from being cold.** A live MAP 01 run past the briefing
  gives `RoachStepAllowed` 366,870 calls, `RoachMaskWeight` half a million and
  `CreateRoach` nine -- and `ab.sh campaign` reaches NONE of it, because that
  configuration deliberately stops at the briefing dialog. Its own comment says
  why and it is not laziness: MAP 01 turns hostile the moment the dialog
  clears, and the 24 FIRE lines that follow land in a 38-line log whose ORDER
  differs between two unsynchronised runs. That was tried and it failed.

  So the usual three artifacts are all unavailable at once on that screen. The
  log is combat noise, the pixels are live play, and the object table -- which
  works for `bootcamp` precisely because it is taken before anything moves --
  is a moving target the instant the mission starts. Adding a configuration
  will not fix this; what would is an EXHAUSTIVE ORACLE of the
  `tools/shakecheck.py` shape, seeding the roach constants and the mask tables
  and comparing against the original under Unicorn. That is the tool the roach
  layer needs and it does not exist yet.

  Do not read a large counter as coverage. `RoachStepAllowed` running a third
  of a million times says the code executes and the mission plays; it says
  nothing about whether it agrees with the original.

  **HALF OF THAT IS CLOSED NOW, and saying which half is the point.**
  `tools/roachcheck.py` is the tool this paragraph asked for, in the PARTIAL
  form the layer allows: RoachMaskWeight cannot be emulated whole, because
  every point it makes goes to ObjectsAtPoint and then to
  BlockWeightDamaging, which walks the live object map and damages what it
  finds. Both are stubbed to `xor eax, eax; ret` in the emulator, and what is
  compared is the SEQUENCE OF POINTS the original asks about -- which is the
  half most likely to be wrong: a 164-byte stride into a table whose base is
  four bytes before the points, and two axes added in SIXTEEN BITS that wrap
  independently.

  The tables are SEEDED, and without that the tool would prove nothing: they
  live in .bss, BuildRoachFootprints fills them at startup, so in the file
  every count is zero and every case returns immediately. That is
  shakecheck's finding one layer along.

  640 cases and three mutations, and the counts are the useful part: a 32-bit
  add where the original wraps at sixteen fails 264, swapping the axes fails
  560, and seeding at a 163-byte stride fails 560 AND DROPS THE CORPUS to 630
  -- the boolcheck tell, a mutation that shrinks the case count rather than
  being caught by it. Check the count, not only the verdict.

  What it does NOT cover: the accumulation, the zero-count early exit reaching
  the caller, and everything inside the two stubs.

  **The second oracle covers RoachStepAllowed's SPEED**, which is the head of
  that function up to 0x0043D168 -- the four roach velocity and acceleration
  constants, the frame delta, and the object's own field, through _ftol.
  Past that point it reads the object's rows, its playing animation and the
  map, so the same partial-oracle line applies. 576 cases and four mutations:
  clamping the reverse arm with min instead of max fails 190, inverting the
  state test 182, dropping the frame-delta scale 106, and making _ftol round
  instead of truncate fails 26 -- which is the count that matters, because it
  is exactly the cases whose float has a fractional part, and it proves the
  deltas reach non-integral values.

  **AND THE FIRST RUN OF IT REPORTED 348 FAILURES THAT WERE THE HARNESS'S.**
  `Emu.call` writes its scratch bytes at SCRATCH, and the control record had
  been placed there -- so `state` and `reverse` were zeroed AFTER being
  seeded, and every case ran the forward arm. The seeds have to outlive the
  call that uses them, which is the same class as shakecheck's unseeded run
  and reads the same way from outside: a confident disagreement.

  **espmap KEPT A FALSE DEFECT OUT OF THIS ONE.** Hand-counting the prologue
  made the original look as though it reads the state from argument 1 and the
  speed field from argument 2 -- the opposite of the reconstruction, and the
  ADDR_ENTER_VEHICLE shape exactly. The count had dropped the leading
  `push ecx`. tools/espmap.py puts the three arguments at slots +0x18, +0x1C
  and +0x20 and shows ebx taking the second, which is what the C already
  does. Hand-tracking esp through a prologue is what the tool exists for.

  **`RoachBite`'s drive is identified and it is not a longer wait.** Its one
  caller reaches it only in the roach's state 4, and a live MAP 01 run with
  nine roaches alive -- `CreateRoach=9`, `RoachMaskWeight=506808` -- leaves it
  at 0 after TWO MINUTES. So the roaches are running and simply never attack
  anything: what is missing is a unit walking into one, which no configuration
  here does. Measured rather than assumed, and the longer wait was tried
  first.

  **`RoachMaskWeight` came OFF this list by someone doing the drive it named**,
  which is what an identified drive is for. The entry used to say that
  `bootcamp` issues no `createroach` while `kitchen` issues nine, and that what
  was missing was a configuration clearing MAP 01's briefing. Driving exactly
  that -- SINGLE PLAYER, the player row, SELECT, NEW, RETURN at the strategic
  map, OK on MESSAGE FROM HQ, a click for the instruction sign, then thirty
  seconds -- gives `CreateRoach=9` and `RoachMaskWeight=591980`. Nine against
  the nine `createroach` statements in `kitchen1.txt` is better evidence than a
  bare non-zero.

  **`ab.sh campaign` still does not reach it**, and that is worth separating
  from the above. That configuration dumps the briefing widgets WITHOUT
  dismissing the dialog, deliberately, so it can compare the HUD behind it --
  and the game pauses while a dialog is up, so the clock never reaches
  `triggerdelay 1000 init_roaches`. A clean `campaign` run is therefore not
  evidence about anything the roaches touch.

  **Two probe runs read `CreateRoach=0` before that, and the counter was not
  what settled it.** Zero looks exactly like dead code. The screenshot showed
  the game sitting on MESSAGE FROM HQ, and `ComposeFrame=0` on the same dump
  said no frame had been composed at all. **Read a liveness counter beside the
  one you care about** -- a zero next to `ComposeFrame=0` is a drive that did
  not happen, not a function that did not run.

  **`ShakeAt` and `StartShake` are unexercised and no longer unverified.**
  `tools/shakecheck.py` enumerates 11,088 cases against the original, which is
  the whole of what decides them; see the section above for the defect that
  found. The pair is the argument for reaching for an exhaustive oracle
  rather than another entry on this list.

  **The AI is a WHOLE LAYER this environment does not reach, not a handful of
  cold functions, and ONE `counts` line says so.** `drive.sh ctl "counts Ai"`
  on a driven Boot Camp mission returns every `Ai*` counter at 0 --
  `AiStep` and its six arms, `AiKeepRange`, `AiWalkStep`, `AiTakeAbandoned`,
  `EvtSetAiMode`, the whole air-support block -- with the only non-zeroes
  being `AimInit` and `ResetAirSupport`, which are startup and not AI at all.
  `ConsiderSightingC` and `RandomPointToward` read 0 beside them. Boot Camp's
  enemies never engage, so nothing below the sighting layer runs.

  Reach for the FILTER rather than a per-function probe when a whole band is
  in question: one command, one line, and the two non-zeroes in it are the
  part that makes it evidence rather than a broken dump. Everything in that
  band is verified by reading until a drive exists that provokes a fight.

  **`AllObjectsInRect` is unexercised while its near-twin runs 112 times on
  the same drive**, which is the useful shape of it. `ObjectsInRect` and
  `AllObjectsInRect` are instruction for instruction the same query; what
  differs is who calls them. The first has two still-original callers in the
  live path, the second has three -- `ADDR_STEP_TYPE6`, `0x0043D330` and
  `ADDR_SEQ_STEP7` -- and none of the three fires on a Boot Camp mission,
  through walking, firing or scrolling. Object type 6 is one of the three
  types this file still records as unread, so the likeliest answer is that
  Boot Camp has none. Verified by reading, and neither harness can help: it
  reads two globals, so `tools/vectors.py` will not take it, and its
  descriptor is null before `install()`, so `AM2_SELFCHECK=1` would take the
  process down the way `LookupOwnerObj` did.


  **`OverlayPrepare` and `SelectUnit` were on this list and should not have
  been.** With a drive that actually reaches a Boot Camp mission they read
  86 and 3. What put them here was a hand-written probe script that clicked
  BOOT CAMP at the wrong Y -- `ab.sh` uses (306, 143) -- so the game sat on
  the title screen while the script reported counters as though a mission
  were running. **Check a live control value before believing a probe**: the
  game clock at `ADDR_GAME_CLOCK_MS` read 0 in every one of those runs, and
  CLAUDE.md already records that it ticks in play. One dump would have caught
  it.

  `OverlayPrepare` does run, once per frame, and always with row 0 -- so
  after the first call the "already on that row" test returns and the tail is
  skipped. That is why three mutations to the tail passed: not because the
  function is dead, but because the tail runs ONCE per session.

  **The two encoders are a SUBSYSTEM rather than a path.** They sit behind
  `BMP_FLAG_SOFTWARE`, which nothing sets while DirectDraw is handing out real
  surfaces -- `MakeBitmap` runs three times in a Boot Camp mission and takes
  the hardware branch every time. It is the same reason `BlitCopy16` and
  `BlitCopy32` have always read 0. The software rasteriser is not a function
  this environment misses, it is a layer it never enters.

  **EncodeBig and EncodeSmall are checked now, by `tools/rlecheck.py`.** They
  are the sharpest case for an offline oracle in the tree: behind
  BMP_FLAG_SOFTWARE, which nothing sets, so the layer is never entered; and
  both counters BLIND, so `counts` cannot speak for them either. The original
  is emulated with its allocator stubbed to a fixed buffer -- `malloc` reaches
  HeapAlloc, which does not exist under emulation -- and the whole output is
  compared byte for byte against a model of the reconstruction: header, row
  table and payload, plus the size the caller keeps. The harness maps more
  stack because the function reserves 199,000 bytes through _chkstk, which is
  larger than vectors.py's whole stack.

  280 cases. Its mutations are worth reading for what they say about the
  CORPUS rather than the code: dropping the remap fails 164 and making the row
  table 16-bit for both encoders fails 48, but INVERTING THE ROW DIRECTION
  first failed only 8 -- because five of six patterns were functions of x
  alone and read identically upside down. A corpus with no power over the one
  thing `h`'s sign decides. With the patterns made row-dependent it fails 32
  of 280, and the case count went UP rather than down, which is the boolcheck
  tell in the good direction.

  And say which mutation is not a test: raising the 0xFF cap on a skip or a
  run makes the MODEL throw, because the count is written into one byte. That
  cap is fixed by the format, not by the corpus, so it is verified by the
  encoding's shape and no case can speak to it.

  **TWO OF THE THREE MULTIPLAYER ONES ARE CHECKED NOW**, by
  `tools/mprowcheck.py`: MpNameInk and MpNamePaper, enumerated over every
  branch either can take. Paper is three flags; ink is the three latency
  bands, whether the row is our own, whether a player record exists and
  whether it has gone silent. 240 cases, and the mutation counts are exactly
  derivable, which is the strongest form this evidence takes -- dropping the
  host gate on the "has not confirmed the map" colour fails SIX, precisely
  the host-clear, map-clear paper cases, and swapping the ready pair fails
  EIGHTEEN, precisely the other twenty-four minus those six.

  Three stubs, each the standard answer to a wall already recorded here:
  GetTickCount is an import so the IAT slot points at a stub in a MAPPED page
  (collectcheck's finding), and PlayerLatency and FindPlayerById are stubbed
  so the latency and the record's presence become inputs rather than
  consequences of a comm object nobody can build offline.

  **AND A STUB THAT REWRITES ITS OWN IMMEDIATE DOES NOT WORK.** The first
  version wrote `mov eax, <value>; ret` afresh for each case, and Unicorn
  CACHES TRANSLATED BLOCKS -- so once 0x00402EC0 had executed, every later
  call returned the first case's value. It failed 72 of 216 while a single
  call in isolation passed, which is a confusing shape to debug: the tool
  looked wrong only in bulk. The stubs read their answers from memory cells
  now and the code is written once, before either address has run. Vary the
  DATA, never the CODE, in an emulator that caches.

  The third, PlayerLatency's own body, still needs a live DirectPlay session
  with a second player, which this machine cannot open: the row painter has two branches and with nothing connected it
  takes the other one. `MpNameSetInk` beside them runs 60,152 times, so the
  painter itself is thoroughly exercised and the branch is not.

  `ListDropOldest` is the sharpest case of a function that cannot be driven
  rather than merely has not been: its one caller is `MenuMessage` and it
  fires only above a hundred logged menu lines, which no configuration in
  `ab.sh` produces. **AND POKING THE SUB-STATE DOES NOT OPEN A DIALOG, which was tried.**
Setting `ADDR_MENU_MODE` to 23 with the socket's `poke` puts the state-2
dispatch on the game-menu ARM, and `ctl widgets` then falls back to printing
the HUD -- because the dialog is built by the OPENER, and the mode is only
where the built dialog is dispatched to. `RefreshScreen` stayed at 0 for the
same reason: its callers are those openers.

Forcing arm 34 -- the in-mission ESCAPE handler -- and releasing ESCAPE is
worse: the process exits. Clicking the HUD's own boxes does not open one
either: the three panels at 480,430, 480,169 and 486,31 were each clicked in
a live mission and the sub-state stayed 33, the dump kept falling back to the
HUD, and `RefreshScreen` stayed 0. So the radar, the commands panel and the
portrait are displays rather than openers.

Three routes tried and three failed, which is worth more than the list of
functions it leaves unexercised: the in-mission dialogs appear to have NO
reachable trigger in this environment, and `RefreshScreen`, the save dialog
and the game menu are unexercised for that reason rather than for want of a
drive that nobody has written. That is an inconsistent state nobody constructed
properly, not a defect, and it is recorded here only so the route is not
tried a third time. Reaching those dialogs needs the opener CALLED, not the
mode set.

  `RefreshScreen` has 7 callers and "whatever forces an
  out-of-band repaint is somewhere further in" is no longer the state of
  knowledge: six of the seven are the in-mission dialog openers — GAME MENU,
  SAVE, LOAD, DELETE, OVERWRITE, AUDIO — and every one of them calls it only
  when `ADDR_GAME_STATE` is 2, so opening AUDIO from the TITLE screen does not
  reach it and a probe confirms that (0 calls with the dialog on screen). The
  seventh is the WndProc activation handler, which needs an alt-tab. Reaching
  it means opening one of those dialogs from inside a mission, and the
  state-2 sub-state table at `0x00426230` says which arm does it: index
  `substate - 22`, with 23 the game menu and 27 AUDIO. Ordinary Boot Camp
  play sits at sub-state 24. `RestoreTileSet` is a
  different case and probably a permanent one: it runs only when DirectDraw
  takes a surface back, which needs an alt-tab or a mode change, and nothing
  under Xvfb does either. Anyone on a real display should alt-tab out of a
  mission and back. `CalibratePalette` came off this list once
  `-w` was understood — it runs twice per windowed startup, and
  `SnapshotSystemPalette` came off this list once the intro movie was allowed to
  play, and stays off it: its counter reads 0 only because its caller is ours.
