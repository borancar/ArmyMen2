# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-08-21**, at `412efb8`. Working tree clean.

## In flight

Nothing uncommitted.

- **A whole setter family is reconstructed and never runs.** A counts probe on
  the campaign reads `EvtSetField540=0`, `EvtSetByte530=0` and
  `LookupType3ByUID=0`; of the family only `EvtSetByte40` fires, 4 times. They
  are arms of the action executor at `0x00420410`, so which ones run is decided
  by which actions the shipped scripts use, and `kitchen1.txt` uses almost none
  of them. **The clean campaign A/B says nothing about any of them** -- it
  establishes only that nothing else broke. Verified by reading.
  `blindspots.py` confirms these counters can move, so the zeroes are real
  rather than the usual blind spot.
- **The pure-leaf pool is nearly empty**: 14 pure unreconstructed leaves left,
  from the 161 this project started that count at. Three of the 14 are false
  positives -- see Leads.
- **Six holes closed in the vector harness, all found by mutation.** The
  newest is a lockstep: a seeded field and an argument set of the SAME list
  length move together, so `CommRemoveKeyed` never met the record that
  exercises its clamp. Co-prime lengths, the lesson `vectors.py` already
  carried for SEED periods. The
  newest: a `u32v` seed varies on a period fixed by its position in the chain,
  so a table with answers at BOTH ends could not be aimed at -- `ObjCodeUnmapped`
  hit 100% coverage with two of its five zero answers never produced. `u32s`
  takes an explicit value set, as `ARG_VALUES` already does for arguments. The
  correlation heuristics -- copy one argument to another, set one to -1 -- were
  overwriting explicit `ARG_VALUES` sets, so `MaskPixelSolid32` reached only 4
  of 55 (x, y) combinations and could not tell `acc >= x` from `acc > x`. Every
  pointer argument took its NULL from one shared decision, so they were null
  together and never one at a time -- which made `RemapBytes`' whole copy path
  unreachable. And the scratch fill had period 256 while `PTR_STRIDE` is
  `0x800`, so **every pointer argument's region held identical bytes** and a
  copy from src to dst changed nothing observable; every copy-like function in
  the set had been checked against indistinguishable buffers. Both fixed at
  `bbefc4e`, and no existing reconstruction was relying on either.
- **The vectors now check for writes the original did NOT make.** They only
  ever asked whether the expected writes happened, so a reconstruction that
  scribbled elsewhere passed. Found by a `ListUnlink` mutation that should have
  failed and did not. Closing it immediately found a real defect:
  `SetFacing14` and `SetFacing08` set a dword on facing 1 that the original
  sets only on facing 3, and had done since they were written. Fixed at
  `1b6d541`.

## Where the work is

The **Win32/DirectX boundary phase is finished** -- `docs/boundary.md` reports
0 outstanding on every channel it can see: named imports, imports by ordinal,
COM vtables, runtime resolution, delay imports. The three `MessageBoxA` sites
left are a decision, not an omission: all three sit behind CD checks this build
has patched to jump past them.

The front has moved into **game logic**, and the current front is savegame
serialisation. `SaveGame` writes eleven sections and **all eleven savers are
now ours**, as are ten of the eleven loaders; the two halves mirror each other,
which is what keeps confirming each struct's layout from both ends. Only the
objscript loader is left. The newest is `SaveObjScriptSection`, the deepest of them -- four
nested levels, with each action's string length-prefixed in place of the
pointer field it occupies in memory.

## Measured

| | | how |
|---|---:|---|
| `patch_replace` sites | 338 | `grep -rho patch_replace src/game \| wc -l` |
| distinct addresses reconstructed | 338 | 331 of them below the CRT line |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 80,800 / 372,816 B (**21.7%**) | patched entries' sizes over the total |
| modules | 23 flat + 15 `win32/` | `tools/checkclaims.py` |
| pure unreconstructed leaves | **0** (2 listed, both false positives) |
| self-naming unreconstructed functions | 109 at the sweep, 10 taken since | `tools/vectors.py --all` |
| boundary functions reconstructed | 56, 160 import sites | `docs/boundary.md` |
| COM dispatch outstanding | 0 of 79 functions | `docs/boundary.md` |

Read the percentage as what still crosses an original boundary, not as how
much of the game runs on our code -- the count-of-0 blind spot cuts the other
way, and `tools/blindspots.py` says which counters can move at all.

## Verification state

| check | when | result |
|---|---|---|
| `make` | current | builds clean |
| `make check` (16 static checks) | current | all pass, generated files regenerate identically |
| `make selftest` | current | **7,186** vectors, 15,228 words, 13,956 lines, 9,062 spine, 198 variable -- 0 fail |
| `tools/ab.sh campaign` | current | clean, three times: log identical at 14 messages, 2,571/786,432 pixels every time |
| savegame oracle, per section | current | `map` `pad` `script` `eventblock` `event` `air` `audio` **0**; `objscript` 376, all inside pointer fields; `conds` 372, a uniform -196 uid shift; `item` 16 heap pointers; `gameproc` 2 volatile |
| `tools/ab.sh bootcamp\|windowed\|intro\|audio\|mission\|quit` | not since this run began | the rest of `ab.sh all` is still owed |

A clean A/B is not evidence about a function the run never calls. Check with a
counts probe before reading one as coverage -- that is what turned the
`EvtSetByte530` result from "verified" into "verified by reading", above.

## Next

1. **The objscript loader is the last unreconstructed half of a pair.**
   `0x004364A0`, 1072 B, and it is the first function of the objscript.cpp
   band. Its saver is now ours and verified byte for byte, so the round trip
   would check both halves at once. It has to rebuild three pointer levels
   from the raw dwords the saver stored -- states, frames, actions -- and turn
   each action's stored LENGTH back into a `malloc`'d string.
2. **One loader is all that is left of the serialiser**: `0x004364A0`
   objscript, 1072 B, which has to rebuild three pointer levels from the raw
   dwords the saver stored and turn each action's stored LENGTH back into a
   `malloc`'d string.
3. **Fold the pointer-aware comparison into a tool.** It was done by hand for
   objscript here -- walk the section, collect the offsets that hold heap
   pointers, and compare everything else -- and it turned "188 differing bytes"
   into a clean result with a sharp pass criterion. `tools/actdiff.py` already
   renumbers pointers by first-seen index; the savefile deserves the same, and
   then `tools/ab.sh` could carry it as a standing check.
4. **Drive a LOAD -- and the gate is now mapped, which is most of the work.**
   Attempted three ways and not reached; recorded here so the next attempt
   starts from the chain rather than from the menu. `LoadGame` (`0x00425A10`)
   has exactly one caller, `0x004255EE`, inside mission start
   (`0x00425300`), behind

       if ([0x00511DD8] == 0) LoadLevelScript(); else LoadGame(fp);

   `0x00511DD8` is armed only by the GAME SELECT PANEL's LOAD arm at
   `0x004520BB` (inside `0x00451E10`, 720 B), which first copies the selected
   save's NAME into `0x00511B88`. That is a string buffer, not a boolean --
   `0x0044F1A0` tests its first byte, which is what made it look like a flag.
   Mission start then calls `0x00425950`, which needs BOTH `0x00511B88` and
   the player name at `0x00511A68` non-empty, chdirs to `save\<player>` and
   fopens the save; a 0 from it clears the flag and the load silently becomes
   a fresh start. Every attempt so far ends with `LoadLevelScript=1`, so the
   LOAD arm is not running -- the panel is reached and the row is highlighted,
   but the click is not arming `0x00511B88`.

   Worth knowing what the panel DOES exercise: listing the save reads its
   header, so `LoadGameProcSection` and `CheckSaveTag` both run for real
   against a real file (`=1` each). Every other loader is still unexecuted.
5. Keep taking self-naming functions. 109 were found below the CRT line, 51 KB,
   median 288 B, 34 under 200 B; ten are done. `SendGameMsg` (`0x004022D0`,
   928 B, 14 callers) is the hub two of them already reach by address.
6. `tools/ab.sh all` -- only `campaign` has been run against current HEAD.

## Leads

- **The audio section is the last in the file, which turns its LENGTH into a
  check.** 68 bytes -- a tag and sixteen zero lengths -- ending exactly at EOF.
  The saver's loop bound is exclusive (`jl`) where `FreeDynamicSounds` walks
  the same table with `jle`, so one covers 16 slots and the other 17. Writing
  the seventeenth would put the file at 176,854 bytes instead of 176,850. The
  two functions genuinely disagree about that table; neither was made to agree
  with the other.

- **All sixteen slots are empty at the autosave, so the populated path is
  verified by READING only.** A dynamic sound has to be looping and active to
  be written, and none is at mission start. What the oracle checked is the tag,
  the slot count and the empty-slot arm; the length-prefixed name and the four
  dwords behind it -- looping, position, priority, owner, which are exactly
  PlayDynamicSound's arguments -- have never been executed. Driving a mission
  long enough to start an ambient loop before the save would close that.

- **A structural parse that lands exactly on the next tag proves every record
  boundary, and the objscript section is the strongest case of it yet.** Four
  nested levels -- 11 scripts, 73 states, 137 frames, 104 actions, one embedded
  string -- walked from the count alone, ending precisely on `0x06660002`. Get
  any record size or any count field wrong and the walk lands somewhere else.
  The sizes the original pushes (0x14, 0x10, 0x14, 0x48) are a second
  derivation of the layouts in `objscript.h` and `script.h`, neither of which
  was written from this function.

- **Do not read "0 differing bytes" in a pointer-bearing section as a stronger
  result than it is.** The objscript section stores three levels of raw heap
  pointer -- 221 dwords -- and this run pair matched on all of them, where an
  earlier pair of the same section differed on 188 bytes, every one inside a
  pointer field. What changed is heap layout between the two runs, not the
  serialiser. So the honest pass criterion is the one applied here: walk the
  section, set the pointer offsets aside, and require everything else to match.
  A future run differing in those 221 dwords means nothing on its own.

- **The savegame oracle needs a control, and the campaign drive is not
  deterministic to the uid.** Two runs of one tree gave 766 differing bytes and
  then 391 -- every difference a UNIFORM shift of every stored uid, by -125 in
  one run and -71 in the other, in exactly the two sections that store uids.
  The two sides reach the autosave having allocated different numbers. So a
  clean comparison (nothing but heap pointers) is strong evidence, and a
  difference in a uid-bearing section is not evidence of anything without a
  control -- the same relationship `ab.sh`'s pixel figure has with its budget.
  It was read as a regression twice before it was measured.
- **A savegame is an exact oracle the log-and-pixel A/B cannot be.** A
  mis-serialised save shows in neither the log nor the screen. Comparing the
  `.sav` two runs produce catches what `ab.sh` structurally cannot -- and it
  took two wrong readings to interpret the first result: "written by original
  code, so noise" (the control disproved it: two ORIGINAL runs differ by one
  byte) and then "a real divergence in object state" (the bytes are heap
  pointers). Run the control, then look at what the differing bytes ARE.
- **The savegame SAVE half is verified by execution; the LOAD half is not.**
  `SaveItems=1` and `SaveScriptCond=91` on the campaign path -- the game
  autosaves at mission start. Every loader reads 0 and none is blind.
- **A zero on one half of a pair says nothing about the other half.** I read
  `CheckSaveTag=0` as "the savegame layer is undriven" and wrote that down. It
  only meant nothing was LOADED: saving goes through `WriteSaveTag`, which had
  no counter because it was not reconstructed yet. The prefix settles it --
  `save/sarge/map1_mission1.sav` is rewritten on every campaign run. Check that
  the counter you are reading is on the path you are asking about.
- **The self-naming sweep replaced the pure-leaf pool as the source of
  targets.** 109 unreconstructed functions below the CRT line push their own
  name in a string. It is a better basis than naming from a call site, which is
  how three wrong names got into `orig.h` before -- every function taken since
  the sweep is named by its own message.

Things believed but not established. Written down so they are not re-derived,
and not promoted to fact without evidence.

- **Type 3 may be vehicles.** `0x0045A9C0`, the type-3 arm of the per-type
  teardown at `0x00428DA0`, sits inside a band that is entirely vehicle code --
  `vehicle mask direction`, `ExitAllFromVehicle`, `Vehicle aai entry not found`.
  That is adjacency, not proof, and the check that would settle it came back
  empty: none of `ObjIsType3`'s 25 caller functions carries a log string at all.
  Types 2, 3 and 8 stay unidentified.
- **Three of `vectors.py`'s 14 pure leaves are not targets.** `0x00427974` is
  the jump table `0x004278E0` dispatches through, not a function -- the split
  list is a lower bound and says so. `0x0045CAA0` is the stubbed logger, which
  IS reconstructed, by `src/inject/gamelog.c`; the scan reads the reconstructed
  set from `patch_replace` calls in `src/game` only and cannot see a harness
  patch. Worth fixing in `merges.reconstructed()`, which is where the same
  lesson is already written down.
- **Some things cannot be checked offline at all, and `MaskPixelSolid32` is
  the clean example.** The only difference between it and `MaskPixelSolid` is
  that its row table holds dword offsets rather than word ones -- and a row
  offset must land inside a 0x8000-byte scratch to be followed, so its high
  word is always zero and the two reads are indistinguishable. Mutating the
  dword read back to a word passes every vector. Raising `SCRATCH_SZ` past
  64K would fix it and would re-cut every vector in the set; not done, and
  recorded here rather than left as a silent gap.
- **A seed that is "valid" can still be untestable.** `ObjMaskBitAt` computes
  `mask.origin - obj.pos + point`, and the first seed put the object at (0, 0)
  -- where subtracting and adding are the same expression. The sign mutation
  passed all 6982 vectors at 100% coverage. Ask of every seeded field whether
  its value makes the operation on it observable, not merely legal.
- **The two mask decoders are one format with a parameter, not a duplicated
  pair.** `MaskPixelSolid` and `MaskPixelSolid32` went in as near-twins
  differing only in a word row table against a dword one, guessed to be one
  source function with the width chosen at compile time. `RemapRleRuns` takes
  that width as an ARGUMENT and walks the same rows, so it is a property of the
  format. A third function is what settled a question two others could only
  raise.
- **A mutation that passes is only evidence when the mutation is a change.**
  Two of `CollapseEqualDeltas`' five mutations left every vector green and
  neither was a gap: both rewrites are provably the same function, confirmed by
  modelling them over 20,000 random arrays. From the test output that is
  indistinguishable from a real hole, so check that a passing mutation actually
  alters behaviour before widening the inputs to chase it.
- **Two tools caught the author in one commit, and both were right.**
  `BuildRgb332Palette` went in as a second name on `0x0041ADE0`, which already
  had `ADDR_FILL_PALETTE` -- the alias ratchet failed the build at 32. And
  `winmain.cpp` reached the same address through `orig_fill_palette`, which
  became a lie the moment it was reconstructed; `checkseams` caught that. Both
  rules are written in CLAUDE.md and both were broken anyway, which is the
  argument for having them as tools rather than as prose.
- **`+0x538` looks like an animation state and `+0x74` like its sequence.**
  `ObjNextKind538` refuses a change to `+0x538` while an unsigned byte at
  `obj->[0x74]->[0x51]` has not reached `[0x44][0]-1` -- a position, a length,
  and a refusal until the end. Its first dispatch arm is exactly the set of
  codes allowed to interrupt, and a current value of 1 never holds anything up.
  Suggestive, not established: nothing yet says what a code means.
- **The comm object's flow control is coming into focus.** `0x004014C0` is the
  ack handler -- `"??? PULSE seq %d latency %d acks for %d msgs %d thru %d"`,
  `"Flow Ack for Message not in sendqueue sequence %d"` -- and it is the only
  caller of both `RingPush32` and `CommRemoveKeyed`. So the 32-dword ring at
  `+0x3A0` that `CommMean32` averages is a **latency** average, which `msgslot.h`
  had guessed from its shape before the writer was found; and the 12-byte
  records at `+0xBC` are very probably the send queue, keyed by sequence number.
  The second reading is the caller's vocabulary, not the function's, so it is
  recorded and not in a name.
- **Object codes 0x18..0x28 are a real vocabulary, and two tables agree on
  which five matter.** `MapCode18To28` maps `{0x18,0x19,0x1A,0x27,0x28}` to
  `{8,2,1,4,6}` and everything between to 0; `ObjCodeUnmapped` answers 0 for
  exactly those five. Two independently transcribed tables in different parts
  of the image singling out the same subset is worth more than either alone,
  and it is a thread to pull on for what the codes mean.
- **100% instruction coverage is not verification, and there are now two
  worked examples.** `RemapBytes` reached every instruction while `count & 3` and
  `count & 7` were indistinguishable, because no count that reached the copy
  path had bit 2 set. Coverage says which lines ran, never whether the values
  that ran them could tell two behaviours apart. Mutation is what says that.
- **Coverage percentages in `--validate` are not being read.** `SetFacing14`
  and `SetFacing08` have sat at **39.3%** for as long as they have existed and
  the line printing it went past every time. The defect found at `1b6d541` was
  in an arm those vectors do reach, so coverage was not the thing that hid it
  -- but a third of two functions is still unvisited and nothing is tracking
  which functions are short.
