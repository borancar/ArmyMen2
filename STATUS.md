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
now ours, and so are all eleven loaders** -- the serialiser is complete. The two
halves mirror each other, which is what confirmed each struct's layout from
both ends. The newest is `SaveObjScriptSection`, the deepest of them -- four
nested levels, with each action's string length-prefixed in place of the
pointer field it occupies in memory.

## Measured

| | | how |
|---|---:|---|
| `patch_replace` sites | 389 | `grep -rho patch_replace src/game \| wc -l` |
| distinct addresses reconstructed | 389 | 382 of them below the CRT line |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 90,576 / 372,816 B (**24.3%**) | patched entries' sizes over the total |
| modules | 27 flat + 15 `win32/` | `tools/checkclaims.py` |
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

1. **The savegame serialiser is complete** -- eleven savers and eleven
   loaders. What it still lacks is EXECUTION of the load half; see 4 below,
   which is now the most valuable single thing left in this area. `LoadGame`
   itself (`0x00425A10`, 224 B) is fully mapped and still unwritten, and
   `FreeObjScripts` (`0x004368D0`) is done and, unlike the loaders, actually
   executes -- its counter reads 1 on a campaign run.
3. **Fold the pointer-aware comparison into a tool.** It was done by hand for
   objscript here -- walk the section, collect the offsets that hold heap
   pointers, and compare everything else -- and it turned "188 differing bytes"
   into a clean result with a sharp pass criterion. `tools/actdiff.py` already
   renumbers pointers by first-seen index; the savefile deserves the same, and
   then `tools/ab.sh` could carry it as a standing check.
4. **Drive a LOAD -- narrowed to a genuine puzzle, with the ruled-out
   branches named.** `LoadGame` (`0x00425A10`) is now reconstructed, patched
   and traced, and it still never runs. What a temporary `hooklog` probe plus
   the trace log establish, all measured:

   - The GAME SELECT PANEL's LOAD arm (`0x00452060`) fires: `0x00511B88`
     holds `"map1_mission1.sav"`, `0x00511A68` holds `"sarge"`.
   - Mission start (`0x00425300`) takes the LOAD branch, so `0x00511DD8` was
     set when it read it at `0x00425360`.
   - `0x00425950` **succeeds**. The log shows its three steps in order --
     `SetGameDir("save\sarge")`, `CheckSaveTag(fp, 0x06660666, gameproc.cpp,
     0x528)`, `LoadGameProcSection` returning 1. So the flag is NOT cleared at
     `0x00425373`, which was one of the two candidates.
   - `LoadGame` is patched (`patch: LoadGame 00425a10` in the log) and never
     traced. `LoadLevelScript` is, so `0x004255CB` read the flag as 0.
   - There is **no write** to `0x00511DD8` between `0x00425385` and
     `0x004255CB`. The two map-loader references at `0x0042D078` and
     `0x0042D0D2` are `mov eax,`/`mov ecx,` -- reads, where the loader decides
     whether the map should place objects a save will supply.

   So the flag is set, survives the open, and is 0 by the test ~250 log lines
   later with nothing in range writing it. The most likely remaining reading
   is that mission start is ENTERED TWICE and the second entry clears at
   `0x00425358` or `0x00425373` before reaching the test -- which a probe on
   those two writes would settle in one run. That is the next step, and it is
   cheap now that the surrounding code is ours.

5. Keep taking self-naming functions -- **29 are left**, recomputed from
   `docs/logs.tsv` against the current patch list rather than quoted from an
   old sweep. Smallest first: `AddMsg` (`0x00401050`, 96 B, 12 callers),
   `RemHead` (`0x004010C0`, 144 B, 10), `RemMsg` (`0x00401410`, 176 B, 3) --
   the air.cpp message list, which CLAUDE.md warns is mutex-guarded and
   multi-threaded, so a mistake there is a race. Cleaner: `EventMessageSend`
   (`0x0041F150`, 176 B), `EventMessageReceive` (`0x0041F320`, 240 B),
   `EventTriggerDelayed` (`0x0041F410`, 144 B), `DefLinkParse` (`0x00436080`,
   512 B) and `DefObjParse` (`0x00435B60`, 768 B).
6. `tools/ab.sh all` -- only `campaign` has been run against current HEAD.

## Leads

- **`0x0041F8B0` is not another shim and should not be taken as one.** It is
  the third "At" address, so it looked like a peer of `EvtAtPointA` and
  `EvtAtPointC` -- but it resolves an ARMY through `CommSlotForArmy`, indexes a
  per-slot list at `0x004F9ECC`, and walks every object in it. 192 bytes of
  iteration rather than a guard and a call. Worth reading before assuming the
  pattern holds, which is the same trap the "At" halves already sprang once.

- **`AM2_ScriptAction.relative` is honoured two levels below the parser, and
  finding that corrected a naming guess.** The "At" halves were described last
  commit as plain point-takers, on the strength of the "On" wrappers that call
  them. They are not: each takes its own uid and a `relative` flag, and ADDS
  the object's position to the point when it is set. That flag is the leading
  `+` a script may write on coordinates, which script.h has recorded since the
  parser was done -- this is the far end of it. The lesson is the usual one in
  reverse: a caller can mislead about a callee just as a call site can mislead
  about a function.

- **Hoisting a lookup above its guard is an easy way to change behaviour while
  the code still reads right.** Three of these shims went in with
  `LookupByUID` called before the `uid >= 1000` test, because writing the
  pointer as an initialiser is the natural C shape. The original checks the
  threshold FIRST and only then looks up -- so the hoisted version calls into
  the object table for uids the game never would, moving a counter and running
  code the original does not reach. Caught by re-reading the disassembly beside
  the C rather than by any check; nothing in `make check` or the A/B would have
  shown it.

- **The Evt* shims differ in which check they make, and it is worth recording
  rather than smoothing.** Three patterns now appear in the family: check the
  UID against 1000 (`EvtSetOwner`), check the POINTER LookupByUID returned
  (`EvtSetByte40`, `EvtObjAction`, `EvtDeployItem`), and check BOTH
  (`EvtType2ActionA`/`B`, which test the uid and then hand the possibly-null
  result to ObjIsType2, safe only because that function opens with a null
  test), and check NEITHER, passing a possibly-null object straight on
  (`EvtObjSet`, the unsafe one). Writing them all the same way would lose a
  real distinction, so they are written as found.

- **25 functions are left in the event.cpp band, and most are tiny.** Four
  are 32 bytes, eight are 48, and nearly all have a single caller -- they are
  the `Evt*` shim family this module already holds ten of: check a uid or a
  pointer, look the object up, poke one field or call one thing. They are cheap
  to take a few at a time, and the naming convention is settled. The one that
  is not tiny is the 4096-byte action executor at `0x00420410`, which is the
  last thing under the condition layer.

- **A mutation resolves the count-of-0 blind spot as well as a probe does, and
  verifies the function while it is at it.** `CondRunAction` reads 0 -- its
  only live caller is our own `RunCondActions` -- and CLAUDE.md's standing
  advice is to settle that with a temporary probe. Making it always run action
  0 instead of the i'th puts `campaign` at 294,304 differing pixels, 37.4% of
  the frame. That proves it runs AND that this drive checks what it does, in
  one run, with nothing to add or remove from the harness. Prefer it to a probe
  when the function is small enough to mutate meaningfully.

- **The alias ratchet has now caught four call-site names this run, and each
  time the body's name was better.** `ADDR_ARMY_MESSAGE_FLUSH` vs
  `ADDR_COMM_FRAME_POST_A`, `ADDR_DEF_LINK_PARSE` and `ADDR_DEF_GAME_PARSE` on
  merged-entry addresses, and now `ADDR_SEND_GAME_PAUSE` vs
  `ADDR_EVENT_FLAG_8_SEND`. That last one repays the rename immediately:
  frame.cpp calls it `(0, AM2_EVENT_FLAG_8)`, which under the old name read as
  a flag poke and under the new one is "tell the other players the game has
  un-paused, reason 8" -- consistent with CLAUDE.md's finding that the event
  flags ARE the pause mask.

- **CORRECTION: the "two-word poke" claim was wrong, and the probe that tested
  it says so.** The previous commit asserted that setting `ADDR_VIEW_RECT_ON`
  and a rectangle would push DrawViewRect's whole trio through on the next
  frame. A temporary `poke` command was added to the control socket and it did
  not happen. What the probe DID establish, by sampling counters at every step
  of the drive:

  | point | DrawViewRect | flag |
  |---|---:|---|
  | title, SINGLE PLAYER, player row, SELECT, loading | 0 | 0 |
  | mission live | 621 | 0 |

  So it runs only once the mission is live, roughly per frame, and the flag is
  0 for the whole of a normal drive -- setting it at the title never takes,
  because nothing has called the function yet. Poking during the mission left
  DrawViewRect's own counter unmoved over the following four seconds, which
  means that sampling window was not a rendering one; why is not isolated.
  DrawRect stayed 0 throughout.

  The claim to carry forward is the narrow one: DrawViewRect is per-frame in a
  live mission and gated off, so the trio is reachable in principle. Getting
  the flag set at a moment the function is actually running is the unsolved
  part, and it is more than a two-word poke.

- **The Lock/Unlock pairing is per FEATURE, not per function.** DrawVLine and
  DrawHLine each Lock and never Unlock; DrawViewRect Locks once, draws the
  whole outline through DrawRect, and Unlocks once. So a census of "functions
  calling the bracket" necessarily finds halves, and 29 is not 29 pairs.
  CLAUDE.md now says so.

- **The line trio is MENU drawing, and that makes it drivable -- just not by
  this drive.** `DrawRect`'s only caller is `0x00413610`, which is itself the
  first entry on CLAUDE.md's bracket shortlist, and chasing it upward reaches
  `0x00425EE0` (the menu-request consumer, under `ADDR_STATE2_FRAME`) and a
  widget helper at `0x0044D6D0` with seven callers spread through the
  `0x0045xxxx` menu code. So a rectangle outline is a widget border, and the
  reason all three read 0 is that the campaign drive visits SINGLE PLAYER and
  nothing else. A drive through OPTIONS or the in-mission menu would very
  likely light them up -- the cheapest observability win currently on the
  table, and it needs a drive rather than a reconstruction.

- **A rasteriser is not automatically observable.** I switched to CLAUDE.md's
  Lock/Unlock bracket list precisely because those draw PIXELS and the A/B
  compares pixels -- and `DrawVLine` still reads 0. Its two call sites are both
  inside one 96-byte function, `0x0041CDC0`, which has a single caller of its
  own; the whole trio (`0x0041CBA0` vertical, `0x0041CC40` horizontal,
  `0x0041CDC0` the rectangle that uses both) never fires on a campaign drive.
  Subsystem is not the right unit for guessing observability; reachability is.

- **`0x0041CC40` is `DrawVLine`'s horizontal twin** -- same null-stub branch,
  clips the other pair of edges, and replicates the colour byte into a dword
  for a wide fill rather than stepping a byte at a time. It is the obvious next
  one off the bracket list, and `0x0041CDC0` after it completes the trio.

- **Pathfinding is a whole subsystem the drive cannot see.** `AddRegionLink`
  runs 2,230 times building the region graph at map load, and building NO graph
  at all leaves `campaign` with an identical log and 2,571 pixels. Not the
  dedup rule, not the edges -- nothing. The reason is the drive: it clears two
  dialogs and scrolls, and no unit ever needs to path anywhere in that window.
  So every function in `region.cpp` will be reading-verified until there is a
  drive that makes something walk somewhere.

- **A global that holds a POINTER to a table looks exactly like the table.**
  `0x00514EF0` is `mov edx, dword ptr [0x514ef0]` followed by indexing off
  `edx` -- one indirection, same as the cell map beside it. Writing it as the
  array base took the game down instantly on the first run, with 14
  AddRegionLink log lines and then nothing. CLAUDE.md records the same shape
  for `obj -> table -> slot`; this is the one-level version and it is just as
  easy to miss.

- **Comparing two code arms BYTE for byte is the wrong test.** `FreeItem`'s
  kinds 1, 5, 6 and 8 all call the same destructor, and a `memcmp` of their
  arms says they differ -- because `call rel32` encodes a RELATIVE
  displacement, so identical code at four addresses has four encodings.
  Disassembled, kinds 1, 5 and 6 are instruction-for-instruction identical and
  kind 8 differs only in whether `pop edi` precedes `mov eax, 1`. Reading the
  byte difference as a real one would have produced four spurious cases.

- **A drive that never kills anything never frees an item.** `FreeItem` and
  `RemoveFromItemList` both read 0 on a campaign run: 325 items are added
  during load and none is destroyed in the ~25 s observed. That is what
  CLAUDE.md's long-standing "RemoveFromItemList unexercised" note has been
  recording without saying why, and it is now written down there -- the gap is
  the DRIVE, not a missing code path. A mission driven long enough for
  something to die would exercise both.

- **The `.aai` files are checksummed for peer agreement, which is why none of
  it runs here.** `Checksum` (`0x0042DBB0`) XORs a file's dwords; the wrapper
  at `0x004303B0` chdirs into `aai` and XORs the checksums of `game.aai`,
  `object.aai`, `troop.aai`, `vehicle.aai` and `weapon.aai` into one number.
  That is a data-integrity handshake, so it needs a second player and reads 0
  in every configuration this project can drive -- despite seven call sites.
  Those are the same files defparse.cpp and definfo.cpp parse, so the whole
  `.aai` subsystem now has both halves: what the files MEAN and how two
  machines agree they are the same files.

- **Fifty-three callers, fifteen executions, and the drive still cannot see
  what it returns.** `ResolveUid` is the one place a script name becomes a uid.
  Dropping its type test is invisible; so is returning 0 for EVERY call, which
  was meant to be the control. Both leave `campaign` with an identical log and
  2,571 pixels. So the whole function is verified by READING despite being
  genuinely executed -- the fifteen resolutions this mission performs feed
  nothing the drive watches. Call-site count is not coverage, and neither is a
  counter.

  Note this makes it the second kind of control failure worth naming: not
  "the mutation did not apply" (checked -- the marker was there) but "the
  control itself passes", which means the observation channel, not the test,
  is what is missing.

- **`AM2_ScriptCond`'s `unused28` is the round-robin cursor.** The parser never
  writes it, which is why it went in as unused; `RunCondActions` mode 2 reads
  it as a SIGNED byte, runs that action, and stores `(cursor + 1) % nactions`
  back. A field the writer leaves alone is not evidence that nobody uses it --
  the reader is where to look.

- **`AM2_ScriptAction.extra` really is `onobjstate`'s name, in the third of the
  three roles script.h lists for it.** Mode 3 treats it as a name-table index,
  considers only type-2 entries, and runs the first action whose value equals
  the object's current state. So all three readings of that field now have a
  use site rather than a guess.

- **`0x0041F520` has 53 callers and no name.** 80 bytes, in event.cpp, turning
  a name into a uid. It is the densest unnamed thing left in that module and
  worth doing on its own merits rather than as somebody's helper.

- **The event message's two pass-through fields are FilterMatches' masks.**
  They went into `AM2_EventMsg` as `aux1`/`aux2` two weeks of commits ago,
  positional names, because neither EventMessageSend nor EventMessageReceive
  logs or inspects them. `EventTriggerImmediate` hands them straight to
  `FilterMatches` as `maskA`/`maskB` -- the sets the event belongs to, against
  which an entry's NEGATIVE key is a subset test rather than an equality. They
  are renamed throughout. Neither uid takes part in matching at all.

- **A mutation script that fails to parse produces a clean A/B, and it looks
  exactly like a pass.** A quoting error in the python that was meant to gut
  EventTriggerImmediate's handler loop left the source untouched; the build
  succeeded, the drive ran, and the result read "A/B clean". CLAUDE.md already
  says a test that cannot fail has not passed -- the practical form of that is
  to `grep -c` for the mutation marker between editing and building, which the
  re-run did.

- **The event propagation model, from three functions agreeing.** A locally
  raised event broadcasts once through EventMessageSend and only when `remote`
  is 0, so an event arriving from the wire does not echo; the broadcast happens
  on the first MATCHING entry, before its handlers run. `type` is the bucket
  index straight into the nine-entry table, so the buckets are event types
  rather than a hash.

- **Twelve .aai game constants are parsed and thrown away.** `DefGameParse`'s
  jump table has twenty arms and twelve of them share one target that is
  literally `xor eax,eax; ret` -- vehicle_danger, vehicle_standoff,
  trooper_turn_rate, trooper_pose_rate, trooper_slide_rate, defense_radius,
  attack_radius, attack_hunt, follow_radius, follow_engaged_radius, gravity
  and scroll_speed. Only the eight `roach_*` values reach a global. The
  keywords still parse, so the shipped files remain valid; the values simply do
  nothing in this build. Read the table, not the arm layout -- twelve arms
  pointing at one address is not visible any other way.

- **Not filtering `redefined` caught the same mistake a second time, before it
  shipped.** `ADDR_DEF_GAME_PARSE` already existed pointing at `0x00424590`,
  the merged entry, exactly as `ADDR_DEF_LINK_PARSE` had. Two commits ago that
  cost a wasted run and a wrong patch; this time GCC said so and the build was
  fixed before it was ever driven. The stale name is now `ADDR_DEF_GAME_ENTRY`.

- **`0x00511E04` is static during play but not a constant.** It read 500 across
  every earlier sample and 501 on this run. So "it does not tick" still holds
  -- it did not move across twelve seconds -- but it is not fixed either, and
  the name remains unestablished.

- **CORRECTION to the previous commit: the handler is passed the entry's
  VALUE, not its index.** The dispatcher reads `[eax + 0x476FE4]`, the `+4`
  field, and hands that to the handler. Value and index coincide for entries
  77..96 -- which is every keyword with a handler I had looked at -- so
  "the index IS the command id" survived the off-by-one mutation that shifts
  BOTH. It is wrong as a rule: entry 1 is "trooperlevel1" with value 45. And it
  is observably wrong, not just pedantically: passing the index instead moves
  `campaign` from 2,571 to 2,616 differing pixels, because the entries where
  they disagree have a handler of their own at `0x0044CDA0`.

- **The .aai vocabulary can be read by name, which ends the guessing.**
  Entries 79..94 of the table are rocks, bush, trees, ground, fence, wall,
  bridge, barrel, building, pillbox, aagun, tent, garage, radar,
  miscellaneous, powerups -- exactly DefObjParse's sixteen tokens, in order --
  and entry 95 is literally "link". `AM2_DEF_CMD_LINK = 0x5F` began as an
  inference from a bare `cmp`; it is now the name in the table.

- **A counter can survive its caller being reconstructed if the call goes
  through a POINTER in the image.** `DefFindKeyword` fell 395 -> 0 when
  DefDispatchFile became ours, the ordinary blind spot. `DefObjLine` stayed at
  183 through the same change, because the dispatcher reaches it through the
  vocabulary table's function pointer -- the original address, so still the
  patched entry. Same commit, same caller, opposite outcomes.

- **The .aai handlers are reached through a table, not a call, which is why
  `tools/callsites.py` reports them as having no callers at all.** Neither
  `DefObjLine` nor `DefLinkParse` has a single `call rel32`. Scanning for
  ALIGNED dwords found them in `.data`: sixteen slots at `0x0047739C..0x00477450`
  and one at `0x0047745C`. Those are the `+8` field of entries 79..95 of the
  vocabulary table at `0x00476FE0` -- 12 bytes an entry, `{name, value,
  handler}` -- so the entry INDEX is the command id, 0x4F..0x5E reaching
  DefObjLine and 0x5F reaching DefLinkParse. That is where `AM2_DEF_CMD_LINK`
  came from, now confirmed from the data rather than inferred from a compare.

- **"Index is the command id" is not a reading, it is measured.** An off-by-one
  in `DefFindKeyword` puts `campaign` at 297,845 differing pixels -- 37.9% of
  the frame -- and the save at 317 items instead of 310.

- **No shipped .aai file uses a hex or octal literal.** `DefParseNumber` calls
  strtol with base 0; forcing base 10 changes nothing at all. So the base-0
  behaviour, and the fact that "12abc" is accepted as 12 because only
  `end != tok` is tested, both stay verified by reading.

- **`defparse.cpp` is complete: ten functions, both .aai tables end to end.**
  DefObjParse, DefObjLine, DefAddObjRec, DefFindObjRec for the object records;
  DefLinkParse, DefAddLink, DefFindLink, DefCountLinks, DefCheckLinks for the
  links; DefFreeTables for both. Every one arrived in this run of work, and
  each table is now confirmed from at least three directions -- who packs the
  key, who unpacks it, and what the comparator orders on.

  What is still original below it: the .aai FILE reader that dispatches OBJ and
  LINK lines, `DefGameParse` (`0x00424590`) and `DefParseInfoFile`, plus the
  two shared helpers `0x0041A250` (parse a number, 48 callers) and
  `0x0041A640` (name -> index). Those two sit in the audio.cpp..event.cpp band
  rather than this one, which is why they are not here.

- **Mutate a CONTROL before concluding a field is unobserved.** Two field-level
  mutations of `DefObjLine` passed clean -- keeping `rec[3]` instead of zeroing
  it, and swapping `rec[1]` with `rec[2]`. On its own that reads as "the drive
  cannot see this function". It can: making `DefObjLine` return immediately
  puts `campaign` at 31,494 differing pixels and the save at **25** items
  instead of 310. So the function is observed as hard as anything in this
  project, and the honest statement is narrower and more useful -- those three
  particular slots are not discriminated, while `rec[0]` is (the DefObjParse
  33->34 mutation is caught in the log). Without the control the first result
  would have been written up as the wrong claim.

- **The self-naming sweep attributes a string to whatever `functions.tsv` says
  contains it, and merged entries make that a guess.** `DefObjParse`'s own
  default arm is `or eax,-1; ret` and logs NOTHING. The string "DefObjParse:
  Bad object Constant Type" is at `0x00435C4C`, inside a DIFFERENT function --
  the OBJ-line parser at `0x00435C20`, which `functions.tsv` merges into the
  same 768-byte entry. The sweep got the right name only because a caller
  happens to name the callee it is complaining about. Treat "function X names
  itself" as "something inside X's ENTRY names X" until the entry is known not
  to be merged; that is now three merged entries found in this one band.

- **The .aai chain is verified end to end by one legible mutation.** Changing
  `DefObjParse`'s token `0x5B` from 33 to 34 turns the startup complaint from
  "link 33-4" into "link 34-4". So the keyword maps to 33 in DefObjParse,
  DefLinkParse packs it with PackKey, DefCheckLinks unpacks it with KeyFieldA
  and prints it -- four reconstructed functions and the game's own message
  agreeing on one number.

- **Passing a comparator by ADDRESS rather than as our own symbol keeps its
  counter honest.** `DefFindObjRec` hands bsearch `AM2_IMAGE(ADDR_COMPARE_TRIPLE)`
  because that is what the original pushes; the call therefore still crosses
  the patched entry and `CompareTriple` stayed at 22,535 across the change.
  Handing it `&CompareTriple` would have been one instruction shorter, silently
  correct, and would have zeroed a counter that is currently the best evidence
  the sort is ours. The same choice was made for `ComparePair` in
  `DefCheckLinks` -- where it is also why `ComparePair` fell rather than
  vanished when `DefAddLink` landed.

- **The def-object fallback is load-bearing and heavily observed.** Removing
  the two less-specific bsearches from `DefFindObjRec` puts `campaign` at
  22,125 differing pixels against a budget of 500, and the save drops from 310
  items to 303. That is the strongest mutation signal seen in this run of
  work; contrast the event and message functions, where mutations were
  invisible.

- **A counter going DOWN can be the evidence you wanted.** `ComparePair` read
  3,022 before `DefAddLink` was reconstructed and 1,846 after. Nothing changed
  about the sort: the difference is `DefAddLink`'s duplicate scan, which used
  to cross the patched entry from original code and now calls our ComparePair
  directly. So the drop measures exactly the calls that moved inside the
  reconstruction, and it is a cheap confirmation that the new patch is the one
  doing the work. Worth reading counter deltas rather than only absolute
  values.

- **`siblings` is an INDEX, and three separate uses say so.** DefLinkParse
  fills it with the count of links already sharing the parent; ComparePair
  orders on (parent, siblings); DefAddLink refuses a duplicate on that pair;
  DefFindLink bsearches it. So the field is this link's ordinal among its
  parent's, the pair is unique per link, and the sort DefCheckLinks runs is
  what makes the search well defined. The name in defparse.h says count
  because that is how it is COMPUTED; the comment says what it means.

- **The link table is now confirmed from three sides.** `DefLinkParse` PACKS
  `(type, number)` into the parent key with `PackKey`; `DefCheckLinks` UNPACKS
  the same key with `KeyFieldA`/`KeyFieldB` to print "link 33-1"; and the
  qsort it runs first is given stride `0x14` and `ComparePair`, which is the
  record size arrived at independently from the table's search wrapper. Three
  readings, one layout, none of them taken from the others.

- **The original makes a bare `Log()` call with no format pushed.**
  `DefCheckLinks` does it right after qsort: the callee reads whatever sits
  above the popped arguments. Every observed run has that slot at 0 --
  `trace Log#10(00000000)` -- so the reconstruction passes a literal 0 rather
  than reading its own stack garbage, which would be mechanism-faithful and
  less reproducible. Documented at the call site; the logger is a `ret` here,
  so nothing but the trace line can see it.

- **A name collision in orig.h, not an address collision -- and the build said
  so while a filter ate it.** `ADDR_DEF_LINK_PARSE` already existed, pointing
  at `0x00436080`, the merged wrapper; adding a second `#define` with
  `0x004360C0` meant the OLD one won, so the patch landed on the wrong function
  and the first run logged "'LINK' command not found" with a pointer where the
  command should be. GCC warned `redefined` and my own `grep -v` for the
  pre-existing `g_defaultOwner` warning hid it. Two rules, then: grep orig.h
  for the NAME as well as the address, and never filter `redefined` out of a
  build you are about to trust. The stale name is now
  `ADDR_DEF_LINK_SEARCH`, which is what `0x00436080` actually is.

- **`DefLinkParse` is the first reconstruction in several with a
  DISCRIMINATING A/B.** It runs 49 times at load, and swapping the parent and
  child keys diverges the log ("Saved 310 items") and moves the pixels from
  2,571 to 2,575. Contrast the preceding four, where the campaign A/B was
  clean for mutations too. Worth remembering which subsystems the drive
  actually observes: load-time parsing yes, event records and outgoing
  messages no.

- **A function can be live code, correctly wired, and still unreachable from
  anything the game ships.** `ScriptSetObjBitmap` is one arm of the 4096-byte
  action executor at `0x00420410`, so it is reached the way every other action
  handler is -- but no `.txt` under the prefix names a keyword that gets there,
  and the 185-entry token table has `showbitmap` and `showbitmapnopause` and
  nothing else bitmap-shaped. Its counter reads 0 and always will on shipped
  content. CLAUDE.md already records that 48 of the 59 action keywords appear
  in scripts; this is what one of the other eleven looks like from the inside.

- **`DefLinkParse` is a merged entry, and the tool that would say so is
  silent.** `docs/functions.tsv` files it at `0x00436080` with 512 bytes, but
  that address is a 52-byte table-search wrapper and the function with the
  three "DefLinkParse:" strings starts at `0x004360C0`. `tools/merges.py`
  does not split it -- the same lower-bound caveat its docstring carries.
  Worth knowing before ranking it: it parses `LINK <parent> <n> <child> <m>`
  with seven numbered failure exits, and it is the obvious place to look for
  the unexplained `object.aai` complaint about `link 33-1..4`.

- **A counter of 12 can mean the first line ran twelve times.**
  `ArmyMessageSend` reads 12 on a campaign mission -- matching
  `EventMessageSend` exactly, since the event system is single player's only
  sender -- and every one of those calls returns at the FIRST gate. Probed
  live: the comm object at `0x004FA480` has `dplay` 0, `joined` 0 and
  `playerCount` 1, and the packet length at `0x004FAA6C` still holds its
  initial `0x14`, which is independent proof nothing was ever appended. So the
  whole body -- the size complaints, the copy, the flush lookahead -- is
  verified by READING. The blind spot this project already documents is a
  counter that cannot move; this is the opposite one, a counter that moves
  without the function doing anything.

- **Single player sends event messages that nobody receives, so the campaign
  A/B cannot check what is IN one.** `EventMessageSend` runs 12 times a
  mission -- it is genuinely executed -- and packing `num2`/`uid2` into the
  `num1`/`uid1` slots still leaves `campaign` with an identical log and 2,571
  pixels. `EventMessageReceive` never runs at all here; it needs an inbound
  message. What DOES confirm the 40-byte layout is structural rather than
  behavioural: the sender writes eleven offsets and the receiver reads the same
  eleven back, and a native `offsetof` check reproduces all of them plus the
  0x28 size. The behavioural check needs `AM2_MULTIPLAYER=1` with a second
  player, which is the configuration this project has never had.

- **The self-naming sweep undercounts, and by a known amount.** The 29 figure
  comes from matching `Name:` at the start of a log message. `ArmyMessageSend`
  -- 304 bytes, 20 callers, the transport the whole game sends through -- names
  itself three times without ever using a colon ("ArmyMessageSend Zero length
  message"). Anything relying on that list should treat it as a floor.

- **`EventTriggerDelayed` runs seven times a mission and the A/B does not check
  what it puts in the record.** Swapping `uid` and `removeevent` in the
  16 bytes it allocates leaves `campaign` completely clean -- identical log,
  2,571 pixels. So the field ORDER is verified by reading only, even though the
  function is genuinely executed. The likely reason is that the handler fires
  outside the ~25 s the drive observes. Anything that wants to check it needs
  to reach `ADDR_EVT_RECORD_HANDLER` firing, not just the registration.
  Contrast `UpdateObjectScript`, where a one-field mutation is caught
  immediately -- executing a function often is not the same as observing it.

- **`0x00511E04` is not a clock, and the reading that said so lasted one
  probe.** `UpdateObjectScript` skips an object while `obj[0xBC] >= this` and
  on advancing sets `obj[0xBC] = frame->a + this`. That is a deadline against a
  rising tick in every particular, and it is what went into `orig.h` first.
  Then `dump 511E04` three times over twelve seconds of Boot Camp: 500, 500,
  500, while `ComposeFrame` climbed and `UpdateObjectScript` ran 177,370 times.
  It does not tick. `orig.h` still calls it `ADDR_INPUT_CONTEXT` and still says
  the meaning is unestablished -- now with one candidate positively excluded,
  which is worth more than the plausible name would have been.

- **A clean pixel figure can hide what the log catches.** Passing an object
  frame's `a` where the original passes `b` leaves `campaign` at exactly 2,571
  differing pixels -- the usual number, inside budget -- while the log grows
  `ChangeObjectFrame failed in UpdateObjectScript` lines that are not in the
  original. The mutation is caught, but only by the half of `ab.sh` that
  compares text. Worth remembering when judging a reconstruction whose effect
  is a sprite choice.

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
